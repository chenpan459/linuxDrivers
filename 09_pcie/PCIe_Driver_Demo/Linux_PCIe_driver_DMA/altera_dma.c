#include <linux/time.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/random.h>
#include "altera_dma_cmd.h"
#include "altera_dma.h"
#include <linux/unistd.h>
#include <linux/irq.h>

//dma transefer from fpga to x86
int read_block(u8 *dest_addr, int count, struct altera_pcie_dma_bookkeep *bk_ptr)
{
	spin_lock(&bk_ptr->lock);

	//配置DMA寄存器
	iowrite32((((0xAD4B)<<16) | 0x43), bk_ptr->bar[1]+0x20000+0x40);
	//传输长度
	iowrite32(count, bk_ptr->bar[1]+0x20000+0x44);	
	iowrite32(0x1, bk_ptr->bar[1]+0x20000+0x280);
	//源地址低32位
	iowrite32(ONCHIP_MEM_BASE, bk_ptr->bar[1]+0x20000+0x48);  
	//源地址高32位
	iowrite32(0x0, bk_ptr->bar[1]+0x20000+0x4c);
	//目的地址低32位
	iowrite32((dma_addr_t)virt_to_bus(dest_addr), bk_ptr->bar[1]+0x20000+0x50);
    //目的地址高32位	
	iowrite32(0x0, bk_ptr->bar[1]+0x20000+0x54);  
	//启动DMA一次
	iowrite32(0x0001, bk_ptr->bar[1]+0x20000+0x240);
	while(!ioread32(bk_ptr->bar[1]+0x20000+0x284))
	{
	}
	spin_unlock(&bk_ptr->lock);
    return 0;
}

//dma transefer from x86 to fpga
int write_block(u8 *src_addr, int count, struct altera_pcie_dma_bookkeep *bk_ptr)
{
	spin_lock(&bk_ptr->lock);
	
	//配置DMA寄存器
	iowrite32((((0xAD4B)<<16) | 0x43), bk_ptr->bar[1]+0x20000+0x40);
	//传输长度
	iowrite32(count, bk_ptr->bar[1]+0x20000+0x44);	
	iowrite32(0x1, bk_ptr->bar[1]+0x20000+0x280);
	//源地址低32位
	iowrite32((dma_addr_t)virt_to_bus(src_addr), bk_ptr->bar[1]+0x20000+0x48);
	//源地址高32位
	iowrite32(0x0, bk_ptr->bar[1]+0x20000+0x4c);
	//目的地址低32位
	iowrite32(ONCHIP_MEM_BASE, bk_ptr->bar[1]+0x20000+0x50);	
	//目的地址高32位
	iowrite32(0x0, bk_ptr->bar[1]+0x20000+0x54);  
	//启动DMA一次
	iowrite32(0x0001, bk_ptr->bar[1]+0x20000+0x200);	
	while(!ioread32(bk_ptr->bar[1]+0x20000+0x284))
	{
	}	
	spin_unlock(&bk_ptr->lock);
    return 0;
}


int altera_dma_open(struct inode *inode, struct file *pfile) 
{
    struct altera_pcie_dma_bookkeep *bk_ptr = 0;
	//container_of作用就是通过一个结构变量中一个成员的地址找到这个结构体变量的首地址
    bk_ptr = container_of(inode->i_cdev, struct altera_pcie_dma_bookkeep, cdev);
    pfile->private_data = bk_ptr;
    return 0;
}

int altera_dma_release(struct inode *inode, struct file *file) 
{
    return 0;
}

static ssize_t altera_dma_read(struct file *pfile, char __user *buf, size_t size, loff_t *offset)
{
	int ret = 0;
	struct altera_pcie_dma_bookkeep *bk_ptr;
	bk_ptr = pfile->private_data;
	if(size > bk_ptr->mem_len)
		return -ENOMEM;
	struct timeval start, end;
	do_gettimeofday(&start);
	ret = read_block(bk_ptr->buffer, size, bk_ptr);
	do_gettimeofday(&end);
	printk("Dma Read Time = %ld\n", (end.tv_usec-start.tv_usec)+1000000*(end.tv_sec-start.tv_sec));
	memset(buf, 0, size);

	//内核空间到用户空间
	copy_to_user(buf,bk_ptr->buffer,size);
	return ret;
}

static ssize_t altera_dma_write(struct file *pfile, const char __user *buf, size_t size, loff_t *offset)
{
	struct timeval start, end;
	int ret = 0;
	struct altera_pcie_dma_bookkeep *bk_ptr;
	bk_ptr = pfile->private_data;
	if(size > bk_ptr->mem_len)
		return -ENOMEM;
	copy_from_user(bk_ptr->buffer,buf,size);
	do_gettimeofday(&start);
	ret = write_block(bk_ptr->buffer, size, bk_ptr);
	do_gettimeofday(&end);
	printk("Dma Write Time = %ld\n", (end.tv_usec-start.tv_usec)+1000000*(end.tv_sec-start.tv_sec));
	
	return ret;
}

//文件读写偏移量，但是本例程中没有用到
static loff_t gtpci_llseek(struct file *pfile, loff_t offset, int origin)
{
	struct altera_pcie_dma_bookkeep *bk_ptr;
	bk_ptr = pfile->private_data;
	if(offset>bk_ptr->mem_len)
	{
		printk(KERN_INFO "offset error\n");
		return -EINVAL;
	}
	
	switch(origin)
	{
		case SEEK_SET:
			if(offset<0)
				return -EINVAL;
			bk_ptr->mem_offset= offset;
			break;
		default:
			return -EINVAL;
	}
	
	return 0;
}

//IO控制
static long altera_dma_ioctl(struct file *pfile, unsigned int code, unsigned long arg)
{
	struct altera_pcie_dma_bookkeep *bk_ptr;
	struct extmdl_cmd cur_cmd;
	struct driver_version cur_ver;
	int i;
	bk_ptr = pfile->private_data;
	
	switch(code)
	{
		//# define ACCESS_EXT_MODULE _IOWR(0xF6, 0x46, unsigned char)
		case ACCESS_EXT_MODULE:
			if(arg)
			{
				if(copy_from_user((void*)&cur_cmd,(void*)arg,sizeof(struct extmdl_cmd)))
					return -EFAULT;
				if(cur_cmd.accessType==ACCESS_TYPE_READ)
				{
					if(cur_cmd.count>bk_ptr->io_len)
						return -EFAULT;
					for(i=0;i<cur_cmd.count;i++)
					{
						//inw 访问寄存器的宏
						cur_cmd.dataRead[i] = inw(bk_ptr->ioreg + (cur_cmd.offset + i)*2);
					}
				}
				else if(cur_cmd.accessType==ACCESS_TYPE_WRITE)
				{
					if(cur_cmd.count>bk_ptr->io_len)
						return -EFAULT;
					for(i=0;i<cur_cmd.count;i++)
					{
						//outw 访问寄存器的宏
						outw(cur_cmd.dataWrite[i],bk_ptr->ioreg + (cur_cmd.offset + i)*2);
					}
				}
				if(copy_to_user((void *)arg,&cur_cmd,sizeof(struct extmdl_cmd)))
					return -EFAULT;
			}
			
			break;
		//#define GET_DRIVER_VERSION	_IOWR(0xF6, 0x47, unsigned char)
		case GET_DRIVER_VERSION:
			cur_ver.mainVer = PCI_MAIN_VER;
			cur_ver.slaveVer = PCI_SLAVE_VER;
			if(copy_to_user((void *)arg,&cur_ver,sizeof(struct driver_version)))
				return -EFAULT;
			break;
		default:
			printk(KERN_INFO "unknown ioctl cmd\n");
			break;
	}
	
	return 0;
}


struct file_operations altera_dma_fops = 
{
    .owner          = THIS_MODULE,
    .read           = altera_dma_read,
    .write          = altera_dma_write,
    .open           = altera_dma_open,
    .release        = altera_dma_release,
    .unlocked_ioctl = altera_dma_ioctl,
};

//初始化设备对象
static int __init init_chrdev(struct altera_pcie_dma_bookkeep *bk_ptr) 
{
    int dev_minor = 0;
    int dev_major = 0;
    int devno = -1;

	//最后一个参数就是执行 cat /proc/devices显示的名称’
	// 定义：#define ALTERA_DMA_DEVFILE        "gtspci"
	//用户程序靠这个名字打开使用设备
    int result = alloc_chrdev_region(&bk_ptr->cdevno, dev_minor, 1, ALTERA_DMA_DEVFILE);


	//在内核2.6.0之前，major和minor都是8位，合并组成16位的设备号(即syscall.stat中的dev_t)
	//然而在2.6.0之后，major和minor因为需要支持更多数量的设备，修改为12位major和20位minor，
	//组成32位的dev_t,为了兼容旧版本，在组成dev_t时拆出8位major和8位minor组成dev_t的低16位，高16位由剩下的填补   
    dev_major = MAJOR(bk_ptr->cdevno);
    if (result < 0) {
        printk(KERN_DEBUG "cannot get major ID %d", dev_major);
    }

	//MKDEV应该把主设备号和次设备号合成设备号dev_t
    devno = MKDEV(dev_major, dev_minor);

	//初始化设备，注册DMA操作函数，重要就在 altera_dma_fops 中read,write对应的方法
    cdev_init(&bk_ptr->cdev, &altera_dma_fops);

    bk_ptr->cdev.owner = THIS_MODULE;
    bk_ptr->cdev.ops = &altera_dma_fops;
    result = cdev_add(&bk_ptr->cdev, devno, 1);

    if (result)
        return -1; 
    return 0;
}

//扫描所有的Bar寄存器，数量是由硬件决定
static int scan_bars(struct altera_pcie_dma_bookkeep *bk_ptr, struct pci_dev *dev)
{
    int i;
	//#define ALTERA_DMA_BAR_NUM (6)
    for (i = 0; i < ALTERA_DMA_BAR_NUM; i++) 
	{
        unsigned long bar_start = pci_resource_start(dev, i);
        unsigned long bar_end = pci_resource_end(dev, i);
        unsigned long bar_flags = pci_resource_flags(dev, i);
        bk_ptr->bar_length[i] = pci_resource_len(dev, i);
        dev_info(&dev->dev, "BAR[%d] 0x%08lx-0x%08lx flags 0x%08lx, length %d", i, bar_start, bar_end, bar_flags, (int)bk_ptr->bar_length[i]);
    }
    return 0; 
}

//PCI总线地址映射到处理器虚拟地址，ioremap函数
static int __init map_bars(struct altera_pcie_dma_bookkeep *bk_ptr, struct pci_dev *dev)
{ 
	int i;
    for(i = 0; i < ALTERA_DMA_BAR_NUM; i++) 
	{
        unsigned long bar_start = pci_resource_start(dev, i);
        bk_ptr->bar_length[i] = pci_resource_len(dev, i);
        if (!bk_ptr->bar_length[i]) 
		{
            bk_ptr->bar[i] = NULL;
            continue;
        }
        bk_ptr->bar[i] = ioremap(bar_start, bk_ptr->bar_length[i]);
        if (!bk_ptr->bar[i]) 
		{
            dev_err(&dev->dev, "could not map BAR[%d]", i);
            return -1;
        } else
            dev_info(&dev->dev, "BAR[%d] mapped to 0x%p, length %lu", i, bk_ptr->bar[i], (long unsigned int)bk_ptr->bar_length[i]); 
    }
    return 0;
}

static void unmap_bars(struct altera_pcie_dma_bookkeep *bk_ptr, struct pci_dev *dev)
{
    int i;
    for(i = 0; i < ALTERA_DMA_BAR_NUM; i++) 
	{
        if (bk_ptr->bar[i]) 
		{
            pci_iounmap(dev, bk_ptr->bar[i]);
            bk_ptr->bar[i] = NULL;
        }
    }
}

static int __init altera_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int rc = 0;
	//altera_pcie_dma_bookkeep 在altera_dma.h中定义，包含设备的一些基础信息，如Bar,iqr,dma地址等
    struct altera_pcie_dma_bookkeep *bk_ptr = NULL;


	//为bk_ptr申请内存空间，基于slab分配在物理上连续的实际的内存，并初始化为0
	//第二个参数是控制函数的行为的标志，GFP_KERNEL：内核内存的正常分配，可能睡眠.
    bk_ptr = kzalloc(sizeof(struct altera_pcie_dma_bookkeep), GFP_KERNEL);
    if(!bk_ptr)
        goto err_bk_alloc;

    bk_ptr->pci_dev = dev;

	//存储驱动私有数据，存自定义的一些数据结构 bk_ptr : altera_pcie_dma_bookkeep自定义结构体
    pci_set_drvdata(dev, bk_ptr);

	//注册字符设备驱动，主要就是分配设备号，注册文件操作函数（DMA操作）
    rc = init_chrdev(bk_ptr); 
    if (rc) 
	{
        dev_err(&dev->dev, "init_chrdev() failed\n");
        goto err_initchrdev;
    }

	//使能pci设备
    rc = pci_enable_device(dev);
    if (rc) 
	{
        dev_err(&dev->dev, "pci_enable_device() failed\n");
        goto err_enable;
    } else 
	{
        dev_info(&dev->dev, "pci_enable_device() successful");
    }

	//通知内核该设备对应的IO端口和内存资源已经使用，其他的PCI设备不要再使用这个区域
    rc = pci_request_regions(dev, ALTERA_DMA_DRIVER_NAME);
    if (rc) 
	{
        dev_err(&dev->dev, "pci_request_regions() failed\n");
        goto err_regions;
    }

	//设定设备工作在总线主设备模式，申请成总线主DMA模式
    pci_set_master(dev);

	//通过设置dma掩码来改变地址长度
	//当设备支持64位地址总线时，就使用64位，当设备支持32位地址总线时，就使用32位。
	//pci_set_consistent_dma_mask会将掩码赋值给struct device结构体中的coherent_dma_mask，
	//当调用dma_alloc_coherent申请DMA内存时，会检测dma掩码，从而根据对应的掩码来分配地址。
    if (!pci_set_dma_mask(dev, DMA_BIT_MASK(64))) 
	{
        pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(64));
        dev_info(&dev->dev, "using a 64-bit irq mask\n");
    } else 
	{
        dev_info(&dev->dev, "unable to use 64-bit irq mask\n");
        goto err_dma_mask;
    }
	//读取Bar空间
    scan_bars(bk_ptr, dev);

	//PCI总线地址映射到处理器虚拟地址，ioremap函数
    map_bars(bk_ptr, dev);

	//获取PCI设备存储大小
	bk_ptr->mem_len = pci_resource_len(dev, 0);
	bk_ptr->buffer = kzalloc(4096, GFP_KERNEL);
    bk_ptr->numpages = (PAGE_SIZE >= MAX_NUM_DWORDS*4) ? 1 : (int)((MAX_NUM_DWORDS*4)/PAGE_SIZE);
	
    return 0;

err_dma_mask:
    dev_err(&dev->dev, "goto err_dma_mask");
    pci_release_regions(dev);
err_regions:
    dev_err(&dev->dev, "goto err_irq");
    pci_disable_device(dev);
err_enable:
    dev_err(&dev->dev, "goto err_enable");
    unregister_chrdev_region(bk_ptr->cdevno, 1);
err_initchrdev:
    dev_err(&dev->dev, "goto err_initchrdev");
    kfree(bk_ptr);
err_bk_alloc:
    dev_err(&dev->dev, "goto err_bk_alloc");
    return rc;
}


static void __exit altera_pci_remove(struct pci_dev *dev)
{
    struct altera_pcie_dma_bookkeep *bk_ptr = NULL;
    bk_ptr = pci_get_drvdata(dev);
    cdev_del(&bk_ptr->cdev);
    unregister_chrdev_region(bk_ptr->cdevno, 1);
    pci_disable_device(dev);
 
    unmap_bars(bk_ptr, dev);
    pci_release_regions(dev);

    kfree(bk_ptr->buffer);
    kfree(bk_ptr);
    printk(KERN_DEBUG ALTERA_DMA_DRIVER_NAME ": " "altera_dma_remove()," " " __DATE__ " " __TIME__ " " "\n");
}

static struct pci_device_id pci_ids[] = 
{
    { PCI_DEVICE(ALTERA_DMA_VID, ALTERA_DMA_DID) },
    { 0 }
};

static struct pci_driver dma_driver_ops = 
{
    .name = ALTERA_DMA_DRIVER_NAME,
    .id_table = pci_ids,
    .probe = altera_pci_probe,
    .remove = altera_pci_remove,
};

//模块的初始化，通过module_init()指定的入口函数
// __init没有实际意义，仅仅便于程序阅读
static int __init altera_dma_init(void)
{
    int rc = 0;
	// 调试
    printk(KERN_DEBUG ALTERA_DMA_DRIVER_NAME ": " "altera_dma_init()," " " __DATE__ " " __TIME__ " " "\n");
    // 注册驱动，会调用 dma_driver_ops中.probe指向的方法初始化，本程序中是 altera_pci_probe()方法
	rc = pci_register_driver(&dma_driver_ops);

	// rc==0为注册成功，rc<0为失败
    if (rc)
	{
        printk(KERN_ERR ALTERA_DMA_DRIVER_NAME ": PCI driver registration failed\n");
        goto exit;
    }

exit:
    return rc;
}

static void __exit altera_dma_exit(void)
{
	// 释放资源
    pci_unregister_driver(&dma_driver_ops);
}


module_init(altera_dma_init);
module_exit(altera_dma_exit);
MODULE_DEVICE_TABLE(pci, pci_ids);
MODULE_LICENSE("GPL");
