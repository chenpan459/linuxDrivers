# FPGA PCIE接口的Linux DMA Engine驱动

## 摘要

英创嵌入式主板，如ESM7000系列、ESM8000系列等，均可配置标准的PCIE×1高速接口。连接NVMe模块作高速大容量数据存储、连接多通道高速网络接口模块都是PCIE接口的典型应用。此外，对于工控领域中的高速数据采集，还可采用FPGA的PCIE IP核实现PCIE EP端点，与英创嵌入式主板构成高效低成本的应用方案。本文简要介绍方案硬件配置，以及PCIE在Linux平台上的驱动程序实现。

## 硬件设计要点

Xilinx公司为它的FPGA设计有多种PCIE EP端点的IP核，针对本文的应用需求，选择DMA/Bridge Subsystem for PCI Express v4.1（简称PCIE/XDMA）。PCIE/XDMA在硬件上把PCIE接口转换为AXI-Stream高速并行接口（简称AXIS），工控前端逻辑只需把采集数据转换成AXI-Stream格式提供给AXIS通道。IP核会采用PCIE总线的DMA机制，把AXIS通道数据按数据块的形式直接传送至Linux的内存中，这样在Linux的应用程序就可直接处理采集数据了。Xilinx Artix 7系的低成本芯片XC7A35T、XC7A50T均可容纳PCIE/XDMA IP核，这样可保证应用方案的成本处于合理的范围。

![img](E:\by4360\pcie驱动知识.assets\v2-bfcf3a236c8fcdea97e9593b2b8ec45e_720w.webp)

图1 ETA750 Block Design Diagram

图1中的实例xdma_0是Xilinx公司的PCIE/DMA IP模块，作为PCIE端点设备（PCIE Endpoint Device）。Dtaker1_5_0是应用相关的前端逻辑。对PCIE的主要配置如下图所示：

![img](E:\by4360\pcie驱动知识.assets\v2-a11eff8ae7067292c175072ad04249f6_720w.webp)

图2 PCIE接口配置

上述配置定义的AXIS总线为64-bit数据宽度、总线时钟62.5MHz（ACLK）。

AXIS总线典型的握手时序如图3所示，一个数据传输周期最快需要3个ACLK，T3上升沿为数据锁存时刻：

![img](E:\by4360\pcie驱动知识.assets\v2-b538774ca90ba0645aeb3a0026a26c8b_720w.webp)

图3 AXI同步握手时序

若前端逻辑每4个ACLK产生一个dword数据，则对应的数据速率就是125MB/s。

![img](E:\by4360\pcie驱动知识.assets\v2-561ba0c378d984fda1cb7b8b0fd10030_720w.webp)

图4 PCIE中断配置

基于XC7A50T的PCIE/DMA IP可支持最多4路DMA通道，分别为2路发送（H2C通道）和2路接收（C2H通道），加上用户前端逻辑中断，共有至少5个中断源。采用PCIE的MSI中断机制是解决多中断源的最好方式，所以配置8个中断矢量，实际使用5个。

## DMA Engine驱动

目前Xilinx公司为其IP核DMA/Bridge Subsystem for PCI Express v4.1，仅提供基于x86体系的驱动，而没有在Linux DMA Engine架构上做工作。而事实上，DMA Engine架构已成为ARM嵌入式Linux平台的DMA应用的事实标准（de facto），为此本方案首先构建了标准的DMA Engine架构驱动程序，包括通用DMA Controller驱动和面向应用的DMA Client驱动，应用程序通过标准的字符型设备节点，操作DMA Client驱动，从而实现所需的数据采集。图5是从软件开发角度来看的总体功能框图。

![img](E:\by4360\pcie驱动知识.assets\v2-80f0fe946ada71a632bfeab01fd46041_720w.webp)

图5 方案总体功能框图

DMA Engine架构为不同的DMA模式提供不同的API函数，其中最主要的是单次DMA和周期DMA两种，其API函数分别为：

```cpp
struct dma_async_tx_descriptor *dmaengine_prep_slave_sg(
 struct dma_chan *chan, struct scatterlist *sgl,
 unsigned int sg_len, enum dma_data_direction direction,
 unsigned long flags);
 
struct dma_async_tx_descriptor *dmaengine_prep_dma_cyclic(
 struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
 size_t period_len, enum dma_data_direction direction);
```

DMA Controller驱动要求DMA支持Scatter-gather结构的非连续数据Buffer，但在本方案的应用中，对单次DMA情形，采用单个Buffer是最常见的应用方式，这时可采用DMAEngine的简化函数：

```cpp
struct dma_async_tx_descriptor *dmaengine_prep_slave_singl(
 struct dma_chan *chan, dma_addr_t buf, size_t len, 
 enum dma_data_direction direction, unsigned long flags); 
```

Cyclic DMA模式，是把多个DMA Buffer通过其描述符（dma descriptor）表连接成环状，当一个buffer的DMA传送结束后，驱动程序的中断线程将自动启动面向下一个描述符的DMA Buffer。由DMA descriptor表描述的逻辑流程如图2所示：

![img](E:\by4360\pcie驱动知识.assets\v2-8cd58632e4b74c177f2f6f88158145f3_720w.webp)

图6 Cyclic DMA逻辑流程

本方案的DMA Controller驱动实现了上述两种DMA传输方式，即单次DMA传输和周期DMA传输。DMA Controller驱动本质上讲，是一种通用的DMA服务器，如何使用DMA的传输功能，实现具体的数据传输任务，则是由DMA Client来决定的。Linux把DMA服务与具体应用分成两个部分，有利于DMA Controller驱动面向不同的应用场景。

## DMA Client驱动

DMA Client驱动是一个面向应用的驱动，如图5所示，它需要与User Space的上层应用程序配合运行，来完成所需的数据采集与处理。

单次DMA的操作如下所示。

```cpp
/* prepare a single buffer dma */
desc = dmaengine_prep_slave_single(dchan, edev->dma_phys, edev->total_len, DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
if (!desc) {
 dev_err(edev->dev, "dmaengine_prep_slave_single(..) failed\n");
 ret = -ENODEV;
 goto error_out;
}
 
/* setup dtaker hardware */
eta750_dtaker_setup(edev);
 
/* put callback, and submit dma */
desc->callback = dma_callback;
desc->callback_param = edev;
edev->cookie = dmaengine_submit(desc);
ret = dma_submit_error(edev->cookie);
if (ret) {
 dev_err(edev->dev, "DMA submit failed %d\n", ret);
 goto error_submit;
}
 
/* init complete, and fire */
reinit_completion(&edev->xdma_chan_complete);
dma_async_issue_pending(dchan);
 
/* simulate input data */
eta750_dtaker_run(edev);
 
/* wait dma complete */
count = wait_for_completion_timeout(&edev->xdma_chan_complete, msecs_to_jiffies(DMA_TIMEOUT));
if (count == 0) {
 dev_err(edev->dev, "wait_for_completion_timeout timeout\n");
 ret = -ETIMEDOUT;
 eta750_dtaker_end(edev);
 goto error_submit;
}
 
/* error processing */
eta750_dtaker_error_pro(edev);
 
/* stop front-end daq unit */
count = eta750_dtaker_end(edev);
 
/* dump data */
eta750_dtaker_dump_data(edev);
return edev->total_len;
 
error_submit:
dmaengine_terminate_all(dchan);
 
error_out: 
return ret;
```

只有周期DMA方式才能实现连续数据采集，在DMA Client中采用双DMA Buffer的乒乓结构来实现连续采集，应用程序处理0# Buffer数据时，DMA传输数据至1# Buffer，传输结束时，进行切换，应用程序处理1# Buffer数据，DMA传输新数据至0# Buffer。周期DMA需要指定每个buffer的长度period_len，同时需指定由2个buffer构成的ping-pong buffer的总长度total_len。其DMA流程如下所示。

```cpp
/* prepare cyclic buffer dma */
desc = dmaengine_prep_dma_cyclic(dchan, edev->dma_phys, edev->total_len, 
edev->period_len, DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
if (!desc) {
 dev_err(edev->dev, "%s: prep dma cyclic failed!\n", __func__);
 ret = -EINVAL;
 goto error_out;
}
 
/* in cyclic mode */
edev->cyclic = true;
 
/* setup dtaker hardware */
eta750_dtaker_setup(edev);
 
/* put callback, and submit dma */
desc->callback = dma_callback;
desc->callback_param = edev;
edev->cookie = dmaengine_submit(desc);
ret = dma_submit_error(edev->cookie);
if (ret) {
 dev_err(edev->dev, "cyclic dma submit failed %d\n", ret);
 goto error_submit;
}
 
/* init complete, and fire */
reinit_completion(&edev->xdma_chan_complete);
dma_async_issue_pending(dchan);
edev->running = true; 
 
/* simulate input data */
eta750_dtaker_run(edev);
edev->data_seed += ((edev->period_len / sizeof(u16)) * edev->data_incr);
 
while(!kthread_should_stop()) {
 /* wait dma complete */
 count = wait_for_completion_timeout(&edev->xdma_chan_complete, msecs_to_jiffies(DMA_TIMEOUT));
 if (count == 0) {
  dev_err(edev->dev, "wait_for_completion timeout, transfer %d\n",
  edev->transfer_count);
  ret = -ETIMEDOUT;
  break;
 }
 
 /* data processing */
 eta750_dtaker_error_pro(edev);
 edev->transfer_count++;
 reinit_completion(&edev->xdma_chan_complete);
 
 /* fill more data */
 eta750_dtaker_run(edev);
 edev->data_seed += ((edev->period_len / sizeof(u16)) * edev->data_incr);
}
 
/* stop front-end daq unit */
count = eta750_dtaker_end(edev);
edev->running = false; 
 
error_submit:
dmaengine_terminate_all(dchan);
edev->cyclic = false;
dev_info(edev->dev, "%s: dma stopped, cyclic %d, running %d\n", __func__,
edev->cyclic, edev->running);
 
error_out:
return ret;
```

从上面代码可见，传送过程是一个无限循环，DMA Controller驱动会自动进行ping-pong buffer的切换。并通过回调函数通知上层应用程序，新数据已准备就绪。应用程序可通过命令来终止采集传输过程。



# Linux内核：Linux下PCI设备驱动开发详解

## 一、PCI总线描述

PCI 是 CPU 和外围设备通信的高速传输总线。普通 PCI 总线带宽一般为 132MB/s(在32bit/33Mhz下)或者264MB/s（在32bit/66Mhz下）。

PCI 总线体系结构是一种层次式的体系结构，PCI 桥设备占据着重要的地位，它将父总线与子总线连接在一起，从而使整个系统看起来像一颗倒置的树形结构。

PCI 桥包括以下几种：

Host/PCI 桥：用于连接 CPU 与 PCI 根总线，在 PC 中，内存控制器也通常被集成到 Host/PCI 桥设备芯片， Host/PCI 桥通常被称 为“北桥芯片组”。

PCI/ISA 桥：用于连接旧的 ISA 总线。PCI/ISA 桥也被称为“南桥芯片组”。

PCI-to-PCI 桥：用于连接 PCI 主总线与次总线。

![img](E:\by4360\pcie驱动知识.assets\v2-835c8f06a5bdea15d8943ffa06088dab_720w.webp)

## 二、PCI 配置空间访问

PCI 有 3 种地址空间：PCI/IO 空间、PCI 内存地址空间和 PCI 配置空间。

**PCI 配置空间（共为256字节）：**

1. 制造商标识：由 PCI 组织分配给厂家。
2. 设备标识：按产品分类给本卡编号。
3. 分类码：本卡功能分类码。
4. 申请存储器空间：PCI 卡内有存储器或以存储器编址的寄存器和 I/O 空间，为使驱动程序和应用程序能访问它们。配置空间的基地址寄存器用于此目的。
5. 申请 I/O 空间：配置空间基地址寄存器用来进行系统 I/O 空间申请。
6. 中断资源申请：向系统申请中断资源。

内核为驱动提供的函数访问配置空间：

```c
pci_read_config_byte/word/dword(struct pci_dev *pdev, int offset, int *value);
pci_write_config_byte/word/dword(struct pci_dev *pdev, int offset, int *value);
```

**PCI 的 I/O 和内存空间：**

获取 I/O 或内存资源：

```c
pci_resource_start(struct pci_dev *dev,  int bar);/*Bar值的范围为0-5*/
```

该函数返回六个 PCI I/O 区域之一的第一个地址（内存地址或 I/O 端口编号）。

```c
pci_resource_end(struct pci_dev *dev,  int bar)   /* Bar值的范围为0-5*/
```

该函数返回第 bar 个 I/O 区域的后一个地址。注意这是最后一个可用的地址，而不是该区域之后的第一个地址。

```c
pci_resource_flags(struct pci_dev *dev,  int bar)  /* Bar值的范围为0-5 */
```

该函数返回资源关联的标志。资源标志用来定义单个资源的某些特性。对与 PCI I/O 区域关联的 PCI 资源，该信息从基地址寄存器中获得，但对其它与 PCI 设备无关的资源，它可能来自任何地方。所有的资源标志定义在 <[linux](https://link.zhihu.com/?target=https%3A//so.csdn.net/so/search%3Ffrom%3Dpc_blog_highlight%26q%3Dlinux)/ioport.h> 中，下面列出其中最重要的几个：

IORESOURCE_IO

IORESOURCE_MEM

如果对应的 I/O 区域存在，将设置上面标志中的一个，而且只有一个。

IORESOURCE_PREFETCH

IORESOURCE_READONLY

上述标志定义内存区域是可预取的，或者是写保护的。对 PCI 资源来讲，从来不会设置后面的那个标志。通过使用 pci_resource_flags 函数，设备驱动程序可完全忽略底层的 PCI 寄存器，因为系统已经使用这些寄存器构建了资源信息。

申请/释放I/O或内存资源：

```c
int pci_request_regions(struct pci_dev *pdev, const char *res_name);
 
void pci_release_regions(struct pci_dev *pdev);
```

获取/设置驱动私有数据：

```c
void *pci_get_drvdata(struct pci_dev *pdev);
 
void pci_set_drvdata((struct pci_dev *pdev, void *data);
```

使能/禁止 PCI 设备：

```c
int pci_enable_device(struct pci_dev *pdev);
 
int pci_disable_device(struct pci_dev *pdev);
```

设置主总线为 DMA 模式：

```c
void pci_set_master(struct pci_dev *pdev);
```

寻找指定总线指定槽位的 PCI 设备：

```c
struct pci_dev *pci_find_slot (unsigned int bus,unsigned int devfn);
```

设置 PCI 能量管理状态：

```c
int pci_set_power_state(struct pci_dev *pdev, pci_power_t state);
```

在设备的能力中表中找出指定能力：

```c
int pci_find_capability(struct pci_dev *pdev, int cap);
```

启用设备内存写无效事务：

```c
int pci_set_mwi(struct pci_dev *pdev);
```

禁用设备内存写无效事务：

```c
void pci_clear_mwi(struct pci_dev *pdev);
```

## **三、PCI 设备驱动组成**

PCI 本质上就是一种总线，具体的 PCI 设备可以是字符设备、网络设备、USB等，所以 PCI 设备驱动应该包含两部分：

1. PCI 驱动
2. 根据需求的设备驱动

根据需求的设备驱动是最终目的，PCI 驱动只是手段帮助需求设备驱动达到最终目的而已。换句话说 PCI 设备驱动不仅要实现 PCI 驱动还要实现根据需求的设备驱动。

![img](E:\by4360\pcie驱动知识.assets\v2-fc5100317b4a263722e6ff726a14a111_720w.webp)

PCI 驱动注册与注销：

```c
int pci_register_driver(struct pci_driver * driver)；
 
int pci_unregister_driver(struct pci_driver * driver)；
```

pci_driver 结构体：

```c
struct pci_driver {
struct list_head node;
 
char *name;/* 描述该驱动程序的名字*/
 
struct moudule *owner;
 
const struct pci_device_id *id_table;/* 指向设备驱动程序感兴趣的设备ID的一个列表,包括：厂商ID、设备ID、子厂商ID、子设备ID、类别、类别掩码、私有数据*/
 
int   (*probe)   (struct pci_dev *dev, const struct pci_device_id *id);/* 指向一个函数对于每一个与id_table中的项匹配的且未被其他驱动程序处理的设备在执行pci_register_driver时候调用此函数或者如果是以后插入的一个新设备的话,只要满足上述条件也调用此函数*/
 
void (*remove) (struct pci_dev *dev);/* 指向一个函数当该驱动程序卸载或者被该驱动程序管理的设备被卸下的时候将调用此函数*/
 
int   (*save_state) (struct pci_dev *dev, u32 state);/* 用于在设备被挂起之前保存设备的相关状态*/
 
int   (*suspend)(struct pci_dev *dev, u32 state);/* 挂起设备使之处于节能状态*/
 
int   (*resume) (struct pci_dev *dev);/* 唤醒处于挂起态的设备*/
 
int   (*enable_wake) (struct pci_dev *dev, u32 state, int enable);/* 使设备能够从挂起态产生唤醒事件*/
};
```

## **四、小结**

PCI 总线是目前应用广泛的计算机总线标准，而且是一种兼容性最强、功能最全的计算机总线。而 Linux 作为一种新的操作系统，同时也为 PCI 总线与各种新型设备互连成为可能。由于 Linux 源码开放，因此给连接到 PCI 总线上的任何设备编写驱动程序变得相对容易。本文介绍如何编写 Linux 下的 PCI 驱动程序的关键函数接口。



# linux设备驱动之PCIE驱动开发

PCIE（PCI Express)是INTEL提出的新一代的总线接口,目前普及的PCIE 3.0的传输速率为8GT/s，下一代PCIE 4.0将翻番为16GT/S，因为传输速率快广泛应用于数据中心、云计算、人工智能、机器学习、视觉计算、显卡、存储和网络等领域。PCIE插槽是可以向下兼容的，比如PCIE 1X接口可以插4X、8X、16X的插槽上。 

实现基本的PCIE驱动程序，实现以下模块：初始化设备、设备打开、数据读写和控制、中断处理、设备释放、设备卸载。本程序适合PCIE驱动开发通用调试的基本框架，对于具体PCIE设备，需要配置相关寄存器才可以使用！ 

## 源代码

```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h> 
#include <asm/uaccess.h> 
 
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("pcie device driver");
 
#define DEV_NAME "hello_pcie"
#define DEBUG 
 
#ifdef DEBUG
	#define DEBUG_ERR(format,args...) \
	do{  \
		printk("[%s:%d] ",__FUNCTION__,__LINE__); \
		printk(format,##args); \
	}while(0)
#else
	#define DEBUG_PRINT(format,args...) 
#endif
 
//1M 
#define DMA_BUFFER_SIZE 1*1024*1024 
#define FASYNC_MINOR 1
#define FASYNC_MAJOR 244
#define DEVICE_NUMBER 1
 
static struct class * hello_class;
static struct device * hello_class_dev;
 
struct hello_device
{
	struct pci_dev* pci_dev;
	struct cdev cdev;
	dev_t devno;
}my_device;
 
//barn(n=0,1,2或者0，1，2，3，4，5) 空间的物理地址，长度，虚拟地址
unsigned long bar0_phy;
unsigned long bar0_vir;
unsigned long bar0_length;
unsigned long bar1_phy;
unsigned long bar1_vir;
unsigned long bar1_length;
 
//进行DMA转换时，dma的源地址和目的地址
dma_addr_t dma_src_phy;
dma_addr_t dma_src_vir;
dma_addr_t dma_dst_phy;
dma_addr_t dma_dst_vir;
 
//根据设备的id填写,这里假设厂商id和设备id
#define HELLO_VENDOR_ID 0x666
#define HELLO_DEVICE_ID 0x999
static struct pci_device_id hello_ids[] = {
    {HELLO_VENDOR_ID,HELLO_DEVICE_ID,PCI_ANY_ID,PCI_ANY_ID,0,0,0},
    {0,}
};
MODULE_DEVICE_TABLE(pci,hello_ids);
 
static int hello_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void hello_remove(struct pci_dev *pdev);
static irqreturn_t hello_interrupt(int irq, void * dev);
 
//往iATU写数据的函数
void iATU_write_config_dword(struct pci_dev *pdev,int offset,int value)
{
	
}
 
//假设需要将bar0映射到内存
static void iATU_bar0(void)
{
	//下面几步，在手册中有example
	//iATU_write_config_dword(my_device.pci_dev,iATU Lower Target Address ,xxx);//xxx表示内存中的地址，将bar0映射到这块内存
	//iATU_write_config_dword(my_device.pci_dev,iATU Upper Target Address ,xxx);//xxx表示内存中的地址，将bar0映射到这块内存
 
	//iATU_write_config_dword(my_device.pci_dev,iATU Control 1,0x0);//映射的时内存，所以写0x0
	//iATU_write_config_dword(my_device.pci_dev,iATU Control 2,xxx);//使能某个region，开始地址转换
}
 
 
//往dma配置寄存器中读写数据的函数，这是难点一：dma寄存器的寻址。
int dma_read_config_dword(struct pci_dev *pdev,int offset)
{
	int value =0;
	return value;
}
 
void dma_write_config_dword(struct pci_dev *pdev,int offset,int value)
{
	
}
 
void dma_init(void)
{
	int pos;
	u16 msi_control;
	u32 msi_addr_l;
	u32 msi_addr_h;
	u32 msi_data;
	
	//1.dma 通道0 写初始化 。如何访问DMA global register 寄存器组需要根据具体的硬件，可以通过pci_write/read_config_word/dword，
	//也可以通过某个bar，比如通过bar0+偏移量访问。
	//1.1 DMA write engine enable =0x1，这里请根据自己的芯片填写
	//dma_write_config_dword(->pci_dev,DMA write engine enable,0x1);	
	//1.2 获取msi能力寄存器的地址
	pos =pci_find_capability(my_device.pci_dev,PCI_CAP_ID_MSI);
	//1.3 读取msi的协议部分，得到pci设备是32位还是64位，不同的架构msi data寄存器地址同
	pci_read_config_word(my_device.pci_dev,pos+2,&msi_control);
	//1.4 读取msi能力寄存器组中的地址寄存器的值
	pci_read_config_dword(my_device.pci_dev,pos+4,&msi_addr_l);	
	//1.5 设置 DMA write done IMWr Address Low.这里请根据自己的芯片填写
	//dma_write_config_dword(my_device.pci_dev,DMA write done IMWr Address Low,msi_addr_l);
	//1.6 设置 DMA write abort IMWr Address Low.这里请根据自己的芯片填写
	//dma_write_config_dword(my_device.pci_dev,DMA write abort IMWr Address Low,msi_addr_l);
	
	if(msi_control&0x80){
		//64位的
		//1.7 读取msi能力寄存器组中的高32位地址寄存器的值
		pci_read_config_dword(my_device.pci_dev,pos+0x8,&msi_addr_h);
		//1.8 读取msi能力寄存器组中的数据寄存器的值
		pci_read_config_dword(my_device.pci_dev,pos+0xc,&msi_data);
		
		//1.9 设置 DMA write done IMWr Address High.这里请根据自己的芯片填写
		//dma_write_config_dword(my_device.pci_dev,DMA write done IMWr Address High,msi_addr_h);
		//1.10 设置 DMA write abort IMWr Address High.这里请根据自己的芯片填写
		//dma_write_config_dword(my_device.pci_dev,DMA write abort IMWr Address High,msi_addr_h);
		
	} else {
		//1.11 读取msi能力寄存器组中的数据寄存器的值
		pci_read_config_dword(my_device.pci_dev,pos+0x8,&msi_data);
	}
	
	//1.12 把数据寄存器的值写入到dma的控制寄存器组中的 DMA write channel 0 IMWr data中
	//dma_write_config_dword(my_device.pci_dev,DMA write channel 0 IMWr data,msi_data);
	
	//1.13 DMA channel 0 control register 1 = 0x4000010
	//dma_write_config_dword(my_device.pci_dev,DMA channel 0 control register 1,0x4000010);
	
	//2.dma 通道0 读初始化 和上述操作类似，不再叙述。
}
 
static int hello_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int i;
	int result;
	//使能pci设备
	if (pci_enable_device(pdev)){
        result = -EIO;
		goto end;
	}
	
	pci_set_master(pdev);	
	my_device.pci_dev=pdev;
 
	if(unlikely(pci_request_regions(pdev,DEV_NAME))){
		DEBUG_ERR("failed:pci_request_regions\n");
		result = -EIO;
		goto enable_device_err;
	}
	
	//获得bar0的物理地址和虚拟地址
	bar0_phy = pci_resource_start(pdev,0);
	if(bar0_phy<0){
		DEBUG_ERR("failed:pci_resource_start\n");
		result =-EIO;
		goto request_regions_err;
	}
	
	//假设bar0是作为内存，流程是这样的，但是在本程序中不对bar0进行任何操作。
	bar0_length = pci_resource_len(pdev,0);
	if(bar0_length!=0){
		bar0_vir = (unsigned long)ioremap(bar0_phy,bar0_length);
	}
	
	//申请一块DMA内存，作为源地址，在进行DMA读写的时候会用到。
	dma_src_vir=(dma_addr_t)pci_alloc_consistent(pdev,DMA_BUFFER_SIZE,&dma_src_phy);
	if(dma_src_vir != 0){
		for(i=0;i<DMA_BUFFER_SIZE/PAGE_SIZE;i++){
			SetPageReserved(virt_to_page(dma_src_phy+i*PAGE_SIZE));
		}
	} else {
		goto free_bar0;
	}
	
	//申请一块DMA内存，作为目的地址，在进行DMA读写的时候会用到。
	dma_dst_vir=(dma_addr_t)pci_alloc_consistent(pdev,DMA_BUFFER_SIZE,&dma_dst_phy);
	if(dma_dst_vir!=0){
		for(i=0;i<DMA_BUFFER_SIZE/PAGE_SIZE;i++){
			SetPageReserved(virt_to_page(dma_dst_phy+i*PAGE_SIZE));
		}
	} else {
		goto alloc_dma_src_err;
	}
	//使能msi，然后才能得到pdev->irq
	 result = pci_enable_msi(pdev);
	 if (unlikely(result)){
		DEBUG_ERR("failed:pci_enable_msi\n");
		goto alloc_dma_dst_err;
    }
	
	result = request_irq(pdev->irq, hello_interrupt, 0, DEV_NAME, my_device.pci_dev);
    if (unlikely(result)){
       DEBUG_ERR("failed:request_irq\n");
	   goto enable_msi_error;
    }
	
	//DMA 的读写初始化
	dma_init();
	
enable_msi_error:
		pci_disable_msi(pdev);
alloc_dma_dst_err:
	for(i=0;i<DMA_BUFFER_SIZE/PAGE_SIZE;i++){
		ClearPageReserved(virt_to_page(dma_dst_phy+i*PAGE_SIZE));
	}
	pci_free_consistent(pdev,DMA_BUFFER_SIZE,(void *)dma_dst_vir,dma_dst_phy);
alloc_dma_src_err:
	for(i=0;i<DMA_BUFFER_SIZE/PAGE_SIZE;i++){
		ClearPageReserved(virt_to_page(dma_src_phy+i*PAGE_SIZE));
	}
	pci_free_consistent(pdev,DMA_BUFFER_SIZE,(void *)dma_src_vir,dma_src_phy);
free_bar0:
	iounmap((void *)bar0_vir);
request_regions_err:
	pci_release_regions(pdev);
	
enable_device_err:
	pci_disable_device(pdev);
end:
	return result;
}
 
static void hello_remove(struct pci_dev *pdev)
{
	int i;
	
	free_irq(pdev->irq,my_device.pci_dev);
	pci_disable_msi(pdev);
 
	for(i=0;i<DMA_BUFFER_SIZE/PAGE_SIZE;i++){
		ClearPageReserved(virt_to_page(dma_dst_phy+i*PAGE_SIZE));
	}
	pci_free_consistent(pdev,DMA_BUFFER_SIZE,(void *)dma_dst_vir,dma_dst_phy);
 
	for(i=0;i<DMA_BUFFER_SIZE/PAGE_SIZE;i++){
		ClearPageReserved(virt_to_page(dma_src_phy+i*PAGE_SIZE));
	}
	pci_free_consistent(pdev,DMA_BUFFER_SIZE,(void *)dma_src_vir,dma_src_phy);
 
	iounmap((void *)bar0_vir);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}
 
//难点三：中断响应设置
static irqreturn_t hello_interrupt(int irq, void * dev)
{  
    //1.该中断调用时机：当DMA完成的时候，会往msi_addr中写入msi_data,从而产生中断调用这个函数
	//2.根据DMA Channel control 1 register寄存器的状态，判断读写状态，读失败，写失败，读成功，写成功，做出不同的处理。
	return 0;
}
static struct pci_driver hello_driver = {
    .name = DEV_NAME,
    .id_table = hello_ids,
    .probe = hello_probe,
    .remove = hello_remove,
};
 
static int hello_open(struct inode *inode, struct file *file)
{
	printk("driver: hello_open\n");
	//填写产品的逻辑
	return 0;
}
 
int hello_close(struct inode *inode, struct file *file)
{
	printk("driver: hello_close\n");
	//填写产品的逻辑
	return 0;
}
 
long hello_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	//填写产品的逻辑
	//为应用层提供的函数接口，通过解析cmd，在switch中做出不同的处理。 
	iATU_bar0();//某个合适的地方调用
	return 0;
	
}
 
//难点二：启动dma的读写（read和write函数).
static struct file_operations hello_fops = {
	.owner   		=  THIS_MODULE,    
	.open   		=  hello_open,     
	.release 		=  hello_close,
	.unlocked_ioctl =  hello_unlocked_ioctl,
};
 
static int hello_drv_init(void)
{
	int ret;
	ret = pci_register_driver(&hello_driver);
	if (ret < 0) {
		printk("failed: pci_register_driver\n");
		return ret;
	}
	
	ret=alloc_chrdev_region(&my_device.devno,0,DEVICE_NUMBER,"hello");
	if (ret < 0) {
		printk("failed: register_chrdev_region\n");
		return ret;
	}
 
	cdev_init(&my_device.cdev, &hello_fops);
	ret = cdev_add(&my_device.cdev, my_device.devno, DEVICE_NUMBER);
	if (ret < 0) {
		printk("faield: cdev_add\n");
		return ret;
	}
	
	hello_class = class_create(THIS_MODULE, "hello_class");
	hello_class_dev = device_create(hello_class, NULL, my_device.devno, NULL, "hello_device"); 
 
	return 0;
}
 
static void hello_drv_exit(void)
{
	device_destroy(hello_class,my_device.devno);
	class_destroy(hello_class);
		
	cdev_del(&(my_device.cdev));
	unregister_chrdev_region(my_device.devno,DEVICE_NUMBER);
	pci_unregister_driver(&hello_driver);
}
 
module_init(hello_drv_init);
module_exit(hello_drv_exit);
```

## 运行结果

  程序运行后，在linux内核注册PCIE设备，内容如下 

![img](E:\by4360\pcie驱动知识.assets\20171113133351988)

https://download.csdn.net/download/u010872301/10116259