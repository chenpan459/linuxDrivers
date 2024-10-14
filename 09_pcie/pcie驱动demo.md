# linux PCIE驱动开发源代码

## 一、源码示例

- linux下PCI驱动源码实例1，该源码缺少pci_fops的初始化

```c
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm-generic/signal.h>
#undef debug
 
 
// ATTENTION copied from /uboot_for_mpc/arch/powerpc/include/asm/signal.h
// Maybe it don't work with that
//____________________________________________________________
#define SA_INTERRUPT    0x20000000 /* dummy -- ignored */
#define SA_SHIRQ        0x04000000
//____________________________________________________________
 
#define pci_module_init pci_register_driver // function is obsoleted
 
// Hardware specific part
#define MY_VENDOR_ID 0x5333
#define MY_DEVICE_ID 0x8e40
#define MAJOR_NR     240
#define DRIVER_NAME  "PCI-Driver"
 
static unsigned long ioport=0L, iolen=0L, memstart=0L, memlen=0L,flag0,flag1,flag2,temp=0L;
 
// private_data
struct _instance_data {
 
    int counter; // just as a example (5-27)
 
    // other instance specific data
};
 
// Interrupt Service Routine
static irqreturn_t pci_isr( int irq, void *dev_id, struct pt_regs *regs )
{
    return IRQ_HANDLED;
}
 
 
// Check if this driver is for the new device
static int device_init(struct pci_dev *dev,
        const struct pci_device_id *id)
{
    int err=0;  // temp variable
 
    #ifdef debug
 
    flag0=pci_resource_flags(dev, 0 );
    flag1=pci_resource_flags(dev, 1 );
    flag2=pci_resource_flags(dev, 2 );
    printk("DEBUG: FLAGS0 = %u\n",flag0);
    printk("DEBUG: FLAGS1 = %u\n",flag1);
    printk("DEBUG: FLAGS2 = %u\n",flag2);
 
    /*
     * The following sequence checks if the resource is in the
     * IO / Storage / Interrupt / DMA address space
     * and prints the result in the dmesg log
     */
    if(pci_resource_flags(dev,0) & IORESOURCE_IO)
    {
        // Ressource is in the IO address space
        printk("DEBUG: IORESOURCE_IO\n");
    }
    else if (pci_resource_flags(dev,0) & IORESOURCE_MEM)
    {
        // Resource is in the Storage address space
        printk("DEBUG: IORESOURCE_MEM\n");
    }
    else if (pci_resource_flags(dev,0) & IORESOURCE_IRQ)
    {
        // Resource is in the IRQ address space
        printk("DEBUG: IORESOURCE_IRQ\n");
    }
    else if (pci_resource_flags(dev,0) & IORESOURCE_DMA)
    {
        // Resource is in the DMA address space
        printk("DEBUG: IORESOURCE_DMA\n");
    }
    else
    {
        printk("DEBUG: NOTHING\n");
    }
 
    #endif /* debug */
 
    // allocate memory_region
    memstart = pci_resource_start( dev, 0 );
    memlen = pci_resource_len( dev, 0 );
    if( request_mem_region( memstart, memlen, dev->dev.kobj.name )==NULL ) {
        printk(KERN_ERR "Memory address conflict for device \"%s\"\n",
                dev->dev.kobj.name);
        return -EIO;
    }
    // allocate a interrupt
    if(request_irq(dev->irq,pci_isr,SA_INTERRUPT|SA_SHIRQ,
            "pci_drv",dev)) {
        printk( KERN_ERR "pci_drv: IRQ %d not free.\n", dev->irq );
    }
    else
    {
        err=pci_enable_device( dev );
        if(err==0)      // enable device successful
        {
            return 0;
        }
        else        // enable device not successful
        {
            return err;
        }
 
    }
    // cleanup_mem
    release_mem_region( memstart, memlen );
    return -EIO;
}
// Function for deinitialization of the device
static void device_deinit( struct pci_dev *pdev )
{
    free_irq( pdev->irq, pdev );
    if( memstart )
        release_mem_region( memstart, memlen );
}
 
static struct file_operations pci_fops;
 
static struct pci_device_id pci_drv_tbl[] __devinitdata = {
    {       MY_VENDOR_ID,           // manufacturer identifier
        MY_DEVICE_ID,           // device identifier
        PCI_ANY_ID,             // subsystem manufacturer identifier
        PCI_ANY_ID,             // subsystem device identifier
        0,                      // device class
        0,                      // mask for device class
        0 },                    // driver specific data
        { 0, }
};
 
static int driver_open( struct inode *geraetedatei, struct file *instance )
{
    struct _instance_data *iptr;
 
    iptr = (struct _instance_data *)kmalloc(sizeof(struct _instance_data),
            GFP_KERNEL);
    if( iptr==0 ) {
        printk("not enough kernel mem\n");
        return -ENOMEM;
    }
    /* replace the following line with your instructions  */
    iptr->counter= strlen("Hello World\n")+1;    // just as a example (5-27)
 
    instance->private_data = (void *)iptr;
    return 0;
}
 
static void driver_close( struct file *instance )
{
    if( instance->private_data )
        kfree( instance->private_data );
}
 
 
static struct pci_driver pci_drv = {
    .name= "pci_drv",
            .id_table= pci_drv_tbl,
            .probe= device_init,
            .remove= device_deinit,
};
 
static int __init pci_drv_init(void)
{    // register the driver by the OS
    if(register_chrdev(MAJOR_NR, DRIVER_NAME, &pci_fops)==0) {
        if(pci_module_init(&pci_drv) == 0 ) // register by the subsystem
            return 0;
        unregister_chrdev(MAJOR_NR,DRIVER_NAME); // unregister if no subsystem support
    }
    return -EIO;
}
 
static void __exit pci_drv_exit(void)
{
    pci_unregister_driver( &pci_drv );
    unregister_chrdev(MAJOR_NR,DRIVER_NAME);
}
 
module_init(pci_drv_init);
module_exit(pci_drv_exit);
 
MODULE_LICENSE("GPL");
```

## 二、pcie驱动框架

-  PCIE同PCI驱动的差异

From a software standpoint, PCI and PCI Express devices are essentially the same. PCIe devices had the same configuration space, BARs, and (usually) support the same PCI INTx interrupts.一般情况下，两者基本保持一致

Example #1: Windows XP has no special knowledge of PCIe, but runs fine on PCIe systems.

Example #2: My company offers both PCI and PCIe versions of a peripheral board, and they use the same Windows/Linux driver package. The driver does not "know" the difference between the two boards.

However: PCIe devices frequently take advantage of "advanced" features, like [MSI](http://en.wikipedia.org/wiki/Message_Signaled_Interrupts), Hotplugging, extended configuration space, etc. Many of these feature existed on legacy PCI, but were unused. If this is a device you are designing, it is up to you whether or not you implement these advanced features.但是pcie在一些高级特性上有优势，比如MSI（**Message Signaled Interrupts**）、Hotplugging（热插拔）、配置空间扩展等。

- linux设备驱动程序框架

Linux将所有外部设备看成是一类特殊文件，称之为“设备文件”，如果说系统调用是Linux内核和应用程序之间的接口，那么设备驱动程序则可以看成是Linux内核与外部设备之间的接口。设备驱动程序向应用程序屏蔽了硬件在实现上的细节，使得应用程序可以像操作普通文件一样来操作外部设备。

1. 字符设备和块设备

Linux抽象了对硬件的处理，所有的硬件设备都可以像普通文件一样来看待：它们可以使用和操作文件相同的、标准的系统调用接口来完成打开、关闭、读写和I/O控制操作，而驱动程序的主要任务也就是要实现这些系统调用函数。Linux系统中的所有硬件设备都使用一个特殊的设备文件来表示，例如，系统中的第一个IDE硬盘使用/dev/hda表示。每个设备文件对应有两个设备号：一个是主设备号，标识该设备的种类，也标识了该设备所使用的驱动程序；另一个是次设备号，标识使用同一设备驱动程序的不同硬件设备。设备文件的主设备号必须与设备驱动程序在登录该设备时申请的主设备号一致，否则用户进程将无法访问到设备驱动程序。

在Linux操作系统下有两类主要的设备文件：一类是字符设备，另一类则是块设备。字符设备是以字节为单位逐个进行I/O操作的设备，在对字符设备发出读写请求时，实际的硬件I/O紧接着就发生了，一般来说字符设备中的缓存是可有可无的，而且也不支持随机访问。块设备则是利用一块系统内存作为缓冲区，当用户进程对设备进行读写请求时，驱动程序先查看缓冲区中的内容，如果缓冲区中的数据能满足用户的要求就返回相应的数据，否则就调用相应的请求函数来进行实际的I/O操作。块设备主要是针对磁盘等慢速设备设计的，其目的是避免耗费过多的CPU时间来等待操作的完成。一般说来，PCI卡通常都属于字符设备。

所有已经注册（即已经加载了驱动程序）的硬件设备的主设备号可以从/proc/devices文件中得到。使用mknod命令可以创建指定类型的设备文件，同时为其分配相应的主设备号和次设备号。例如，下面的命令：

```
[root@gary root]# mknod  /dev/lp0  c  6  0
```



将建立一个主设备号为6，次设备号为0的字符设备文件/dev/lp0。当应用程序对某个设备文件进行系统调用时，Linux内核会根据该设备文件的设备类型和主设备号调用相应的驱动程序，并从用户态进入到核心态，再由驱动程序判断该设备的次设备号，最终完成对相应硬件的操作。

2. 设备驱动程序接口

Linux中的I/O子系统向内核中的其他部分提供了一个统一的标准设备接口，这是通过include/linux/fs.h中的数据结构file_operations来完成的：

```
struct file_operations {
        struct module *owner;
        loff_t (*llseek) (struct file *, loff_t, int);
        ssize_t (*read) (struct file *, char *, size_t, loff_t *);
        ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
        int (*readdir) (struct file *, void *, filldir_t);
        unsigned int (*poll) (struct file *, struct poll_table_struct *);
        int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
        int (*mmap) (struct file *, struct vm_area_struct *);
        int (*open) (struct inode *, struct file *);
        int (*flush) (struct file *);
        int (*release) (struct inode *, struct file *);
        int (*fsync) (struct file *, struct dentry *, int datasync);
        int (*fasync) (int, struct file *, int);
        int (*lock) (struct file *, int, struct file_lock *);
        ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);
        ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);
        ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
        unsigned long (*get_unmapped_area)(struct file *, unsigned long, 
         unsigned long, unsigned long, unsigned long);
};
```

当应用程序对设备文件进行诸如open、close、read、write等操作时，Linux内核将通过file_operations结构访问驱动程序提供的函数。例如，当应用程序对设备文件执行读操作时，内核将调用file_operations结构中的read函数。

3. 设备驱动程序模块

Linux下的设备驱动程序可以按照两种方式进行编译，一种是直接静态编译成内核的一部分，另一种则是编译成可以动态加载的模块。如果编译进内核的话，会增加内核的大小，还要改动内核的源文件，而且不能动态地卸载，不利于调试，所有推荐使用模块方式。

从本质上来讲，模块也是内核的一部分，它不同于普通的应用程序，不能调用位于用户态下的C或者C++库函数，而只能调用Linux内核提供的函数，在/proc/ksyms中可以查看到内核提供的所有函数。

在以模块方式编写驱动程序时，要实现两个必不可少的函数init_module( )和cleanup_module( )，而且至少要包含<linux/krernel.h>和<linux/[module](https://so.csdn.net/so/search?q=module&spm=1001.2101.3001.7020).h>两个头文件。在用gcc编译内核模块时，需要加上-DMODULE -D__KERNEL__ -DLINUX这几个参数，编译生成的模块（一般为.o文件）可以使用命令insmod载入Linux内核，从而成为内核的一个组成部分，此时内核会调用模块中的函数init_module( )。当不需要该模块时，可以使用rmmod命令进行卸载，此进内核会调用模块中的函数cleanup_module( )。任何时候都可以使用命令来lsmod查看目前已经加载的模块以及正在使用该模块的用户数。

4. 设备驱动程序结构

了解设备驱动程序的基本结构（或者称为框架），对开发人员而言是非常重要的，Linux的设备驱动程序大致可以分为如下几个部分：驱动程序的注册与注销、设备的打开与释放、设备的读写操作、设备的控制操作、设备的中断和轮询处理。

- 驱动程序的注册与注销

  向系统增加一个驱动程序意味着要赋予它一个主设备号，这可以通过在驱动程序的初始化过程中调用register_chrdev( )或者register_blkdev( )来完成。而在关闭字符设备或者块设备时，则需要通过调用unregister_chrdev( )或unregister_blkdev( )从内核中注销设备，同时释放占用的主设备号。

- 设备的打开与释放

  打开设备是通过调用file_operations结构中的函数open( )来完成的，它是驱动程序用来为今后的操作完成初始化准备工作的。在大部分驱动程序中，open( )通常需要完成下列工作：

  1. 检查设备相关错误，如设备尚未准备好等。
  2. 如果是第一次打开，则初始化硬件设备。
  3. 识别次设备号，如果有必要则更新读写操作的当前位置指针f_ops。
  4. 分配和填写要放在file->private_data里的数据结构。
  5. 使用计数增1。

  释放设备是通过调用file_operations结构中的函数release( )来完成的，这个设备方法有时也被称为close( )，它的作用正好与open( )相反，通常要完成下列工作：

  1. 使用计数减1。
  2. 释放在file->private_data中分配的内存。
  3. 如果使用计算为0，则关闭设备。

- 设备的读写操作

  字符设备的读写操作相对比较简单，直接使用函数read( )和write( )就可以了。但如果是块设备的话，则需要调用函数block_read( )和block_write( )来进行数据读写，这两个函数将向设备请求表中增加读写请求，以便Linux内核可以对请求顺序进行优化。由于是对内存缓冲区而不是直接对设备进行操作的，因此能很大程度上加快读写速度。如果内存缓冲区中没有所要读入的数据，或者需要执行写操作将数据写入设备，那么就要执行真正的数据传输，这是通过调用数据结构blk_dev_struct中的函数request_fn( )来完成的。

- 设备的控制操作

  除了读写操作外，应用程序有时还需要对设备进行控制，这可以通过设备驱动程序中的函数ioctl( )来完成。ioctl( )的用法与具体设备密切关联，因此需要根据设备的实际情况进行具体分析。

- 设备的中断和轮询处理

  对于不支持中断的硬件设备，读写时需要轮流查询设备状态，以便决定是否继续进行数据传输。如果设备支持中断，则可以按中断方式进行操作。

  

## 三、PCI驱动程序实现

1. 关键数据结构

PCI设备上有三种地址空间：PCI的I/O空间、PCI的存储空间和PCI的配置空间。CPU可以访问PCI设备上的所有地址空间，其中I/O空间和存储空间提供给设备驱动程序使用，而配置空间则由Linux内核中的PCI初始化代码使用。内核在启动时负责对所有PCI设备进行初始化，配置好所有的PCI设备，包括中断号以及I/O基址，并在文件/proc/pci中列出所有找到的PCI设备，以及这些设备的参数和属性。

Linux驱动程序通常使用结构（struct）来表示一种设备，而结构体中的变量则代表某一具体设备，该变量存放了与该设备相关的所有信息。好的驱动程序都应该能驱动多个同种设备，每个设备之间用次设备号进行区分，如果采用结构数据来代表所有能由该驱动程序驱动的设备，那么就可以简单地使用数组下标来表示次设备号。

在PCI驱动程序中，下面几个关键数据结构起着非常核心的作用：

- pci_driver

  这个数据结构在文件include/linux/pci.h里，这是Linux内核版本2.4之后为新型的PCI设备驱动程序所添加的，其中最主要的是用于识别设备的id_table结构，以及用于检测设备的函数probe( )和卸载设备的函数remove( )：

```c
struct pci_driver {
    struct list_head node;
    char *name;
    const struct pci_device_id *id_table;
    int  (*probe)  (struct pci_dev *dev, const struct pci_device_id *id);
    void (*remove) (struct pci_dev *dev);
    int  (*save_state) (struct pci_dev *dev, u32 state);
    int  (*suspend)(struct pci_dev *dev, u32 state);
    int  (*resume) (struct pci_dev *dev);
    int  (*enable_wake) (struct pci_dev *dev, u32 state, int enable);
};
```

+ pci_dev

这个数据结构也在文件include/linux/pci.h里，它详细描述了一个PCI设备几乎所有的硬件信息，包括厂商ID、设备ID、各种资源等：

```
struct pci_dev {
    struct list_head global_list;
    struct list_head bus_list;
    struct pci_bus  *bus;
    struct pci_bus  *subordinate;
    void        *sysdata;
    struct proc_dir_entry *procent;
    unsigned int    devfn;
    unsigned short  vendor;
    unsigned short  device;
    unsigned short  subsystem_vendor;
    unsigned short  subsystem_device;
    unsigned int    class;
    u8      hdr_type;
    u8      rom_base_reg;
    struct pci_driver *driver;
    void        *driver_data;
    u64     dma_mask;
    u32             current_state;
    unsigned short vendor_compatible[DEVICE_COUNT_COMPATIBLE];
    unsigned short device_compatible[DEVICE_COUNT_COMPATIBLE];
    unsigned int    irq;
    struct resource resource[DEVICE_COUNT_RESOURCE];
    struct resource dma_resource[DEVICE_COUNT_DMA];
    struct resource irq_resource[DEVICE_COUNT_IRQ];
    char        name[80];
    char        slot_name[8];
    int     active;
    int     ro;
    unsigned short  regs;
    int (*prepare)(struct pci_dev *dev);
    int (*activate)(struct pci_dev *dev);
    int (*deactivate)(struct pci_dev *dev);
};
```

2. 基本框架

在用模块方式实现PCI设备驱动程序时，通常至少要实现以下几个部分：初始化设备模块、设备打开模块、数据读写和控制模块、中断处理模块、设备释放模块、设备卸载模块。下面给出一个典型的PCI设备驱动程序的基本框架，从中不难体会到这几个关键模块是如何组织起来的。

```c
/* 指明该驱动程序适用于哪一些PCI设备 */
static struct pci_device_id demo_pci_tbl [] __initdata = {
    {PCI_VENDOR_ID_DEMO, PCI_DEVICE_ID_DEMO,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEMO},
    {0,}
};
/* 对特定PCI设备进行描述的数据结构 */
struct demo_card {
    unsigned int magic;
    /* 使用链表保存所有同类的PCI设备 */
    struct demo_card *next;
    
    /* ... */
}
/* 中断处理模块 */
static void demo_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    /* ... */
}
/* 设备文件操作接口 */
static struct file_operations demo_fops = {
    owner:      THIS_MODULE,   /* demo_fops所属的设备模块 */
    read:       demo_read,    /* 读设备操作*/
    write:      demo_write,    /* 写设备操作*/
    ioctl:      demo_ioctl,    /* 控制设备操作*/
    mmap:       demo_mmap,    /* 内存重映射操作*/
    open:       demo_open,    /* 打开设备操作*/
    release:    demo_release    /* 释放设备操作*/
    /* ... */
};
/* 设备模块信息 */
static struct pci_driver demo_pci_driver = {
    name:       demo_MODULE_NAME,    /* 设备模块名称 */
    id_table:   demo_pci_tbl,    /* 能够驱动的设备列表 */
    probe:      demo_probe,    /* 查找并初始化设备 */
    remove:     demo_remove    /* 卸载设备模块 */
    /* ... */
};
static int __init demo_init_module (void)
{
    /* ... */
}
static void __exit demo_cleanup_module (void)
{
    pci_unregister_driver(&demo_pci_driver);
}
/* 加载驱动程序模块入口 */
module_init(demo_init_module);
/* 卸载驱动程序模块入口 */
module_exit(demo_cleanup_module);
```

上面这段代码给出了一个典型的PCI设备驱动程序的框架，是一种相对固定的模式。需要注意的是，同加载和卸载模块相关的函数或数据结构都要在前面加上__init、__exit等标志符，以使同普通函数区分开来。构造出这样一个框架之后，接下去的工作就是如何完成框架内的各个功能模块了。

3. 初始化设备模块

在Linux系统下，想要完成对一个PCI设备的初始化，需要完成以下工作：

- 检查PCI总线是否被Linux内核支持；
- 检查设备是否插在总线插槽上，如果在的话则保存它所占用的插槽的位置等信息。
- 读出配置头中的信息提供给驱动程序使用。

当Linux内核启动并完成对所有PCI设备进行扫描、登录和分配资源等初始化操作的同时，会建立起系统中所有PCI设备的拓扑结构，此后当PCI驱动程序需要对设备进行初始化时，一般都会调用如下的代码：

```
static int __init demo_init_module (void)
{
    /* 检查系统是否支持PCI总线 */
    if (!pci_present())
        return -ENODEV;
    /* 注册硬件驱动程序 */
    if (!pci_register_driver(&demo_pci_driver)) {
        pci_unregister_driver(&demo_pci_driver);
                return -ENODEV;
    }
    /* ... */
   
    return 0;
}
```

驱动程序首先调用函数pci_present( )检查PCI总线是否已经被Linux内核支持，如果系统支持PCI总线结构，这个函数的返回值为0，如果驱动程序在调用这个函数时得到了一个非0的返回值，那么驱动程序就必须得中止自己的任务了。在2.4以前的内核中，需要手工调用pci_find_device( )函数来查找PCI设备，但在2.4以后更好的办法是调用pci_register_driver( )函数来注册PCI设备的驱动程序，此时需要提供一个pci_driver结构，在该结构中给出的probe探测例程将负责完成对硬件的检测工作。

```c
static int __init demo_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
    struct demo_card *card;
    /* 启动PCI设备 */
    if (pci_enable_device(pci_dev))
        return -EIO;
    /* 设备DMA标识 */
    if (pci_set_dma_mask(pci_dev, DEMO_DMA_MASK)) {
        return -ENODEV;
    }
    /* 在内核空间中动态申请内存 */
    if ((card = kmalloc(sizeof(struct demo_card), GFP_KERNEL)) == NULL) {
        printk(KERN_ERR "pci_demo: out of memory\n");
        return -ENOMEM;
    }
    memset(card, 0, sizeof(*card));
    /* 读取PCI配置信息 */
    card->iobase = pci_resource_start (pci_dev, 1);
    card->pci_dev = pci_dev;
    card->pci_id = pci_id->device;
    card->irq = pci_dev->irq;
    card->next = devs;
    card->magic = DEMO_CARD_MAGIC;
    /* 设置成总线主DMA模式 */    
    pci_set_master(pci_dev);
    /* 申请I/O资源 */
    request_region(card->iobase, 64, card_names[pci_id->driver_data]);
    return 0;
}
```

4. 打开设备模块

在这个模块里主要实现申请中断、检查读写模式以及申请对设备的控制权等。在申请控制权的时候，非阻塞方式遇忙返回，否则进程主动接受调度，进入睡眠状态，等待其它进程释放对设备的控制权。

```c
static int demo_open(struct inode *inode, struct file *file)
{
    /* 申请中断，注册中断处理程序 */
    request_irq(card->irq, &demo_interrupt, SA_SHIRQ,
        card_names[pci_id->driver_data], card)) {
    /* 检查读写模式 */
    if(file->f_mode & FMODE_READ) {
        /* ... */
    }
    if(file->f_mode & FMODE_WRITE) {
       /* ... */
    }
    
    /* 申请对设备的控制权 */
    down(&card->open_sem);
    while(card->open_mode & file->f_mode) {
        if (file->f_flags & O_NONBLOCK) {
            /* NONBLOCK模式，返回-EBUSY */
            up(&card->open_sem);
            return -EBUSY;
        } else {
            /* 等待调度，获得控制权 */
            card->open_mode |= f_mode & (FMODE_READ | FMODE_WRITE);
            up(&card->open_sem);
            /* 设备打开计数增1 */
            MOD_INC_USE_COUNT;
            /* ... */
        }
    }
}
```

5. 数据读写和控制信息模块

PCI设备驱动程序可以通过demo_fops 结构中的函数demo_ioctl( )，向应用程序提供对硬件进行控制的接口。例如，通过它可以从I/O寄存器里读取一个数据，并传送到用户空间里：

```c
static int demo_ioctl(struct inode *inode, struct file *file,
      unsigned int cmd, unsigned long arg)
{
    /* ... */
    
    switch(cmd) {
        case DEMO_RDATA:
            /* 从I/O端口读取4字节的数据 */
            val = inl(card->iobae + 0x10);
            
/* 将读取的数据传输到用户空间 */
            return 0;
    }
    
    /* ... */
}
```

事实上，在demo_fops里还可以实现诸如demo_read( )、demo_mmap( )等操作，Linux内核源码中的driver目录里提供了许多设备驱动程序的源代码，找那里可以找到类似的例子。在对资源的访问方式上，除了有I/O指令以外，还有对外设I/O内存的访问。对这些内存的操作一方面可以通过把I/O内存重新映射后作为普通内存进行操作，另一方面也可以通过总线主DMA（Bus Master DMA）的方式让设备把数据通过DMA传送到系统内存中。

6. 中断处理模块

PC的中断资源比较有限，只有0~15的中断号，因此大部分外部设备都是以共享的形式申请中断号的。当中断发生的时候，中断处理程序首先负责对中断进行识别，然后再做进一步的处理。

```c
static void demo_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct demo_card *card = (struct demo_card *)dev_id;
    u32 status;
    spin_lock(&card->lock);
    /* 识别中断 */
    status = inl(card->iobase + GLOB_STA);
    if(!(status & INT_MASK)) 
    {
        spin_unlock(&card->lock);
        return;  /* not for us */
    }
    /* 告诉设备已经收到中断 */
    outl(status & INT_MASK, card->iobase + GLOB_STA);
    spin_unlock(&card->lock);
    
    /* 其它进一步的处理，如更新DMA缓冲区指针等 */
}
```

7. 释放设备模块

释放设备模块主要负责释放对设备的控制权，释放占用的内存和中断等，所做的事情正好与打开设备模块相反：

```c
static int demo_release(struct inode *inode, struct file *file)
{
    /* ... */
    
    /* 释放对设备的控制权 */
    card->open_mode &= (FMODE_READ | FMODE_WRITE);
    
    /* 唤醒其它等待获取控制权的进程 */
    wake_up(&card->open_wait);
    up(&card->open_sem);
    
    /* 释放中断 */
    free_irq(card->irq, card);
    
    /* 设备打开计数增1 */
    MOD_DEC_USE_COUNT;
    
    /* ... */  
}
```

8. 卸载设备模块

卸载设备模块与初始化设备模块是相对应的，实现起来相对比较简单，主要是调用函数pci_unregister_driver( )从Linux内核中注销设备驱动程序：

```c
static void __exit demo_cleanup_module (void)
{
    pci_unregister_driver(&demo_pci_driver);
}
```

-  如何将驱动程序编译后加载进内核

（1）编写Makefile文件

makefile文件实例

```makefile
ifneq ($(KERNELRELEASE),)
obj-m:=hello.o
else
#generate the path
CURRENT_PATH:=$(shell pwd)
#the absolute path
LINUX_KERNEL_PATH:=/lib/modules/$(shell uname -r)/build
#complie object
default:
make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules
clean:
make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) clean
endif
```

obj-m 表示该文件要作为模块编译  obj-y则表示该文件要编译进内核

正常情况下只需修改hello.o即可

（2）执行make命令生成 *.ko 文件

（3）sudo insmod *.ko加载驱动模块

（4）sudo rmmod *.ko卸载驱动模块

（5）使用dmesg | tail -10来查看内核输出的最后十条信息

（6）使用modinfo *.ko来查看模块信息

- PCI IO/内存地址区域

一个PCI设备可实现多达6个I/O地址区域，每个区域既可以使内存也可以是I/O地址。在内核中PCI设备的I/O区域已经被集成到通用资源管理器。因此，我们无需访问配置变量来了解设备被映射到内存或者I/O空间的何处。获得区域信息的首选接口是下面的宏定义：

```
#define pci_resource_start(dev, bar)((dev)->resource[(bar)].start)
```

 该宏返回六个PCI I/O区域之一的首地址(内存地址或者I/O端口号).该区域由整数的bar(base address register，基地址寄存器)指定，bar取值为0到5。

```
#define pci_resource_end(dev, bar)((dev)->resource[(bar)].end)
```

 该宏返回第bar个I/O区域的首地址。注意这是最后一个可用的地址，而不是该区域之后的第一个地址。

```
#define pci_resource_flags(dev, bar)((dev)->resource[(bar)].flags)
```

 该宏返回和该资源相关联的标志。资源标志用来定义单个资源的特性，对与PCI I/O区域相关的PCI资源，该信息从基地址寄存器中获得，但对于和PCI无关的资源，它可能来自其他地方。所有资源标志定义在<linux/ioport.h>。

- PCIE驱动下的DMA及中断

**（1）DMA循环缓冲区的分配与实现：**

​    对于高速数据信号的采集处理，需要在驱动程序的初始化模块（probe）中申请大量的DMA循环缓冲区，申请的大小直接关系着能否实时对高速数据处理的成败。直接内存访问（DMA）是一种硬件机制，允许外围设备和主内存直接直接传输I/O数据，避免了大量的计算开销。

**（2） Linux内核的内存分区段：**

​    三个区段，可用于DMA的内存，常规内存以及高端内存。

·    通常的内存分配发生在常规内存区，但是通过设置内存标识也可以请求在其他区段中分配。可用于DMA的内存指存在于特别地址范围内的内存，外设可以利用这些内存执行DMA访问，进行数据通信传输。

·    DMA循环缓冲区的分配要求：物理连续，DMA可以访问，足够大。

**（3）Linux内存分配函数：**

·    Linux系统使用虚拟地址，内存分配函数提供的都是虚拟地址，通过virt_to_bus转换才能得到物理地址。

·    分配内核内存空间的函数：kmalloc实现小于128KB的内核内存申请，申请空间物理连续；__get_free_pages实现最大4MB的内存申请，以页为单位，所申请空间物理连续；vmalloc分配的虚拟地址空间连续，但在物理上可能不连续。

·    Linux内核中专门提供了用于PCI设备申请内核内存的函数pci_alloc_consistent，支持按字节长度申请，该函数调用__get_free_pages，故一次最大为4MB。

**（4） DMA数据传输的方式：**

·    一种是软件发起的数据请求（例如通过read函数调用），另一种是硬件异步将数据传给系统。对于数据采集设备，即便没有进程去读取数据，也要不断写入，随时等待进程调用，因此驱动程序应该维护一个环形缓冲区，当read调用时可以随时返回给用户空间需要的数据。

**（5）PCIe中向CPU发起中断请求的方式：**

·    消息信号中断（MSI），INTx中断。

·    在MSI中断方式下，设备通过向OS预先分配的主存空间写入特定数据的方式请求CPU的中断服务，为PCIe系统首选的中断信号机制，对于PCIe到PCI/PCI-X的桥接设备和不能使用MSI机制的传统端点设备，采用INTx虚拟中断机制。

·    PCIe设备注册中断时使用共享中断方式，Linux系统通过request_irq实现中断处理程序的注册，调用位置在设备第一次打开，硬件产生中断之前；同样，free_irq时机在最后一次关闭设备，硬件不用中断处理器之后。

·    中断处理函数的功能是将有关中断接收的信息反馈给设备，并对数据进行相应读写。中断信号到来，系统调用相应的中断处理函数，函数判断中断号是否匹配，若是，则清除中断寄存器相应的位，即在驱动程序发起新的DMA之前设备不会产生其他中断，然后进行相应处理。

**（6）数据读写和ioctl控制：**

·    数据读写：应用进程不需要数据时，驱动程序动态维护DMA环形缓冲区，当应用进程请求数据，驱动程序通过Linux内核提供copy_from_user()/copy_to_user()实现内核态和用户态之间的数据拷贝。

·    硬件控制：用户空间经常回去请求设备锁门，报告错误信息，设置寄存器等，这些操作都通过ioctl支持，可以对PCIe卡给定的寄存器空间进行配置。

**（7）中断处理程序的注册：**

·    中断号在BIOS初始化阶段分配并写入设备配置空间，然后Linux在建立pci_dev时从配置空间中读出该中断号并写入pci_dev的irq成员中，所以注册中断程序时直接从pci_dev中读取就行。

·    当设备发生中断，8259A将中断号发给CPU，CPU根据中断号找到中断处理程序，执行。

**（8）DMA数据传输机制的产生：**

·    传统经典过程：数据到达网卡 -> 网卡产生一个中断给内核 -> 内核使用 I/O 指令，从网卡I/O区域中去读取数据。这种方式，当大流量数据到来时，网卡会产生大量中断，内核在中断上下文中，会浪费大量资源处理中断本身。

·    改进：NAPI，即轮询，即内核屏蔽中断，隔一定时间去问网卡，是否有数据。则在数据量小的情况下，这种方式会浪费大量资源。

·    另一个问题，CPU到网卡的I/O区域，包括I/O寄存器和I/O内存中读取，再放到系统物理内存，都占用大量CPU资源，做改进，即有了DMA，让网卡直接从主内存之间读写自己的I/O数据。

·    首先，内核在主内存中为收发数据建立一个环形的缓冲队列（DMA环形缓冲区），内核将这个缓冲区通过DMA映射，将这个队列交给网卡；网卡收到数据，直接放进环形缓冲区，即直接放到主内存，然后向系统产生中断；

·    内核收到中断，取消DMA映射，可以直接从主内存中读取数据。



# 详细讲解Linux PCI驱动框架分析

## 说明：

1. Kernel版本：4.14
2. ARM64处理器
3. 使用工具：Source Insight 3.5， Visio

## 1. 概述

从本文开始，将会针对PCIe专题来展开，涉及的内容包括：

1. PCI/PCIe总线硬件；
2. Linux PCI驱动核心框架；
3. Linux PCI Host控制器驱动；

不排除会包含PCIe外设驱动模块，一切随缘。

作为专题的第一篇，当然会先从硬件总线入手。进入主题前，先讲点背景知识。在PC时代，随着处理器的发展，经历了几代I/O总线的发展，解决的问题都是CPU主频提升与外部设备访问速度的问题：

1. 第一代总线包含ISA、EISA、VESA和Micro Channel等；
2. 第二代总线包含PCI、AGP、PCI-X等；
3. 第三代总线包含PCIe、mPCIe、m.2等；

PCIe（PCI Express）是目前PC和嵌入式系统中最常用的高速总线，PCIe在PCI的基础上发展而来，在软件上PCIe与PCI是后向兼容的，PCI的系统软件可以用在PCIe系统中。

本文会分两部分展开，先介绍PCI总线，然后再介绍PCIe总线，方便在理解上的过渡，开始旅程吧。

## 2. PCI Local Bus

## 2.1 PCI总线组成

- PCI总线（Peripheral Component Interconnect，外部设备互联），由Intel公司提出，其主要功能是连接外部设备；
- PCI Local Bus，PCI局部总线，局部总线技术是PC体系结构发展的一次变革，是在ISA总线和CPU总线之间增加的一级总线或管理层，可将一些高速外设，如图形卡、硬盘控制器等从ISA总线上卸下，而通过局部总线直接挂接在CPU总线上，使之与高速CPU总线相匹配。PCI总线，指的就是PCI Local Bus。

先来看一下PCI Local Bus的系统架构图：



![img](pcie驱动demo.assets/v2-b4a1d26a84e9439ae63a7e9be4d42028_720w.webp)



从图中看，与PCI总线相关的模块包括：

1. Host Bridge，比如PC中常见的North Bridge（北桥）。图中处理器、Cache、内存子系统通过Host Bridge连接到PCI上，Host Bridge管理PCI总线域，是联系处理器和PCI设备的桥梁，完成处理器与PCI设备间的数据交换。其中数据交换，包含处理器访问PCI设备的地址空间和PCI设备使用DMA机制访问主存储器，在PCI设备用DMA访问存储器时，会存在Cache一致性问题，这个也是Host Bridge设计时需要考虑的；此外，Host Bridge还可选的支持仲裁机制，热插拔等；
2. PCI Local Bus；PCI总线，由Host Bridge或者PCI-to-PCI Bridge管理，用来连接各类设备，比如声卡、网卡、IDE接口等。可以通过PCI-to-PCI Bridge来扩展PCI总线，并构成多级总线的总线树，比如图中的PCI Local Bus #0和PCI Local Bus #1两条PCI总线就构成一颗总线树，同属一个总线域；
3. PCI-To-PCI Bridge；PCI桥，用于扩展PCI总线，使采用PCI总线进行大规模系统互联成为可能，管理下游总线，并转发上下游总线之间的事务；
4. PCI Device；PCI总线中有三类设备：PCI从设备，PCI主设备，桥设备。PCI从设备：被动接收来自Host Bridge或者其他PCI设备的读写请求；PCI主设备：可以通过总线仲裁获得PCI总线的使用权，主动向其他PCI设备或主存储器发起读写请求；桥设备：管理下游的PCI总线，并转发上下游总线之间的总线事务，包括PCI桥、PCI-to-ISA桥、PCI-to-Cardbus桥等。

## 2.2 PCI总线信号定义

PCI总线是一条共享总线，可以挂接多个PCI设备，PCI设备通过一系列信号与PCI总线相连，包括：地址/数据信号、接口控制信号、仲裁信号、中断信号等。如下图：



![img](pcie驱动demo.assets/v2-2fa31fd7ad1b4798ba5232ef0ec9dd22_720w.webp)



- 左侧红色框里表示的是PCI总线必需的信号，而右侧蓝色框里表示的是可选的信号；

- AD[31:00]：地址与数据信号复用，在传送时第一个时钟周期传送地址，下一个时钟周期传送数据；

- C/BE[3:0]#：PCI总线命令与字节使能信号复用，在地址周期中表示的是PCI总线命令，在数据周期中用于字节选择，可以进行单字节、字、双字访问；

- PAR：奇偶校验信号，确保AD[31:00]和C/BE[3:0]#传递的正确性；

- Interface Control：接口控制信号，主要作用是保证数据的正常传递，并根据PCI主从设备的状态，暂停、终止或者正常完成总线事务：

- - FRAME#：表示PCI总线事务的开始与结束；
  - IRDY#：信号由PCI主设备驱动，信号有效时表示PCI主设备数据已经ready；
  - TRDY#：信号由目标设备驱动，信号有效时表示目标设备数据已经ready；
  - STOP#：目标设备请求主设备停止当前总线事务；
  - DEVSEL#：PCI总线的目标设备已经准备好；
  - IDSEL：PCI总线在配置读写总线事务时，使用该信号选择PCI目标设备；



- Arbitration：仲裁信号，由REQ#和GNT#组成，与PCI总线的仲裁器直接相连，只有PCI主设备需要使用该组信号，每条PCI总线上都有一个总线仲裁器；
- Error Reporting：错误信号，包括PERR#奇偶校验错误和SERR系统错误；
- System：系统信号，包括时钟信号和复位信号；

看一下C/BE[3:0]都有哪些命令吧：



![img](pcie驱动demo.assets/v2-054f3c3ab385f52f0aef11be55f7d2dd_720w.webp)



## 2.3 PCI事务模型

PCI使用三种模型用于数据的传输：



![img](pcie驱动demo.assets/v2-c04a0aa39f3382b5b2f37babd59ed349_720w.webp)



1. Programmed I/O：通过IO读写访问PCI设备空间；
2. DMA：PIO的方式比较低效，DMA的方式可以直接去访问主存储器而无需CPU干预，效率更高；
3. Peer-to-peer：两台PCI设备之间直接传送数据；

## 2.4 PCI总线地址空间映射

PCI体系架构支持三种地址空间：



![img](pcie驱动demo.assets/v2-5645caef07cdbb264c23c06465b41934_720w.webp)



1. memory空间：针对32bit寻址，支持4G的地址空间，针对64bit寻址，支持16EB的地址空间；
2. I/O空间PCI最大支持4G的IO空间，但受限于x86处理器的IO空间（16bits带宽），很多平台将PCI的IO地址空间限定在64KB；
3. 配置空间x86 CPU可以直接访问memory空间和I/O空间，而配置空间则不能直接访问；每个PCI功能最多可以有256字节的配置空间；PCI总线在进行配置的时候，采用ID译码方式，使用设备的ID号，包括Bus Number，Device Number，Function Number和Register Number，每个系统支持256条总线，每条总线支持32个设备，每个设备支持8个功能，由于每个功能最多有256字节的配置空间，因此总的配置空间大小为：256B * 8 * 32 * 256 = 16M；有必要再进一步介绍一下配置空间：x86 CPU无法直接访问配置空间，通过IO映射的数据端口和地址端口间接访问PCI的配置空间，其中地址端口映射到0CF8h - 0CFBh，数据端口映射到0CFCh - 0CFFh；



![img](pcie驱动demo.assets/v2-b150fb0719b76fce839d5a8a324d5ad2_720w.webp)



1. 图为配置地址寄存器构成，PCI的配置过程分为两步：

2. 1. CPU写CF8h端口，其中写的内容如图所示，BUS，Device，Function能标识出特定的设备功能，Doubleword来指定配置空间的具体某个寄存器；
   2. CPU可以IO读写CFCh端口，用于读取步骤1中的指定寄存器内容，或者写入指定寄存器内容。这个过程有点类似于通过I2C去配置外接芯片；



那具体的配置空间寄存器都是什么样的呢？每个功能256Byte，前边64Byte是Header，剩余的192Byte支持可选功能。有种类型的PCI功能：Bridge和Device，两者的Header都不一样。

- Bridge



![img](pcie驱动demo.assets/v2-0ecb5dace8dcc9b86d0f6de84b500d72_720w.webp)



- Device



![img](pcie驱动demo.assets/v2-ae6391b8024e00ebbd5607c0c850c121_720w.webp)



1. 配置空间中有个寄存器字段需要说明一下：
2. Base Address Register，也就是BAR空间，当PCI设备的配置空间被初始化后，该设备在PCI总线上就会拥有一个独立的PCI总线地址空间，这个空间就是BAR空间，BAR空间可以存放IO地址空间，也可以存放存储器地址空间。
3. PCI总线取得了很大的成功，但随着CPU的主频不断提高，PCI总线的带宽也捉襟见肘。此外，它本身存在一些架构上的缺陷，面临一系列挑战，包括带宽、流量控制、数据传送质量等；
4. PCIe应运而生，能有效解决这些问题，所以PCIe才是我们的主角；

## 3. PCI Express

## 3.1 PCIe体系结构

先看一下PCIe架构的组成图：



![img](pcie驱动demo.assets/v2-13fbb6618096a4689995c6d3f1cd5d64_720w.webp)



- Root Complex：CPU和PCIe总线之间的接口可能会包含几个模块（处理器接口、DRAM接口等），甚至可能还会包含芯片，这个集合就称为Root Complex，它作为PCIe架构的根，代表CPU与系统其它部分进行交互。广义来说，Root Complex可以认为是CPU和PCIe拓扑之间的接口，Root Complex会将CPU的request转换成PCIe的4种不同的请求（Configuration、Memory、I/O、Message）；
- Switch：从图中可以看出，Swtich提供扇出能力，让更多的PCIe设备连接在PCIe端口上；
- Bridge：桥接设备，用于去连接其他的总线，比如PCI总线或PCI-X总线，甚至另外的PCIe总线；
- PCIe Endpoint：PCIe设备；
- 图中白色的小方块代表Downstream端口，灰色的小方块代表Upstream端口；

前文提到过，PCIe在软件上保持了后向兼容性，那么在PCIe的设计上，需要考虑在PCI总线上的软件视角，比如Root Complex的实现可能就如下图所示，从而看起来与PCI总线相差无异：



![img](pcie驱动demo.assets/v2-230964d58a70695a02996cc42f6976bd_720w.webp)



- Root Complex通常会实现一个内部总线结构和多个桥，从而扇出到多个端口上；
- Root Complex的内部实现不需要遵循标准，因此都是厂家specific的；

而Switch的实现可能如下图所示：



![img](pcie驱动demo.assets/v2-131f134b9a5f573f481c721b479cc6b0_720w.webp)



- Switch就是一个扩展设备，所以看起来像是各种桥的连接路由；

## 3.2 PCIe数据传输



![img](pcie驱动demo.assets/v2-fe40eebf692d7ed53b3f3ca6ca599c2f_720w.webp)



- 与PCI总线不同（PCI设备共享总线），PCIe总线使用端到端的连接方式，互为接收端和发送端，全双工，基于数据包的传输；
- 物理底层采用差分信号（PCI链路采用并行总线，而PCIe链路采用串行总线），一条Lane中有两组差分信号，共四根信号线，而PCIe Link可以由多条Lane组成，可以支持1、2、4、8、12、16、32条；

PCIe规范定义了分层的架构设计，包含三层：



![img](pcie驱动demo.assets/v2-100bff2b00da2d24254428cc5f20de30_720w.webp)



1. Transaction层
2. 负责TLP包（Transaction Layer Packet）的封装与解封装，此外还负责QoS，流控、排序等功能；
3. Data Link层
4. 负责DLLP包（Data Link Layer Packet）的封装与解封装，此外还负责链接错误检测和校正，使用Ack/Nak协议来确保传输可靠；
5. Physical层
6. 负责Ordered-Set包的封装与解封装，物理层处理TLPs、DLLPs、Ordered-Set三种类型的包传输；

数据包的封装与解封装，与网络包的创建与解析很类似，如下图：



![img](pcie驱动demo.assets/v2-2a500c54feded6f1f0960f5ee3dd4b7d_720w.webp)



- 封装的时候，在Payload数据前添加各种包头，解析时是一个逆向的过程；

来一个更详细的PCIe分层图：



![img](pcie驱动demo.assets/v2-2fe6255633315d46bcaf5a7b767e66a6_720w.webp)



## 3.3 PCIe设备的配置空间

为了兼容PCI软件，PCIe保留了256Byte的配置空间，如下图：



![img](pcie驱动demo.assets/v2-b42ccd313466af770715c5d577bd137d_720w.webp)



此外，在这个基础上将配置空间扩展到了4KB，还进行了功能的扩展，比如Capability、Power Management、MSI中断等：



![img](pcie驱动demo.assets/v2-21d52fe9f15adc0bae4eb7810b1d8c35_720w.webp)



- 扩展后的区域将使用MMIO的方式进行访问；