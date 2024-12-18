/*******************************************************************************************
 * 
 * 
 * 使用 pci_alloc_consistent 分配一致性 DMA 内存后，可以通过返回的虚拟地址进行读写操作。
 * 
 * 
 * 
 *******************************************************************************************/



#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

#define DRIVER_NAME "pci_example"
#define PCI_VENDOR_ID_EXAMPLE 0x1234
#define PCI_DEVICE_ID_EXAMPLE 0x5678

struct pci_example_dev {
    struct pci_dev *pdev;
    void *dma_virt_addr;
    dma_addr_t dma_handle;
    size_t dma_size;
};

static struct pci_example_dev my_pci_dev;

static int pci_example_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
    int ret;

    printk(KERN_INFO DRIVER_NAME ": Probing device\n");

    // Enable the PCI device
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR DRIVER_NAME ": Failed to enable PCI device\n");
        return ret;
    }

    // Set DMA mask
    ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
    if (ret) {
        printk(KERN_ERR DRIVER_NAME ": Failed to set DMA mask\n");
        pci_disable_device(pdev);
        return ret;
    }

    // Set consistent DMA mask
    ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
    if (ret) {
        printk(KERN_ERR DRIVER_NAME ": Failed to set consistent DMA mask\n");
        pci_disable_device(pdev);
        return ret;
    }

    // Allocate consistent DMA memory
    my_pci_dev.dma_size = 4096; // 4 KB
    my_pci_dev.dma_virt_addr = pci_alloc_consistent(pdev, my_pci_dev.dma_size, &my_pci_dev.dma_handle);
    if (!my_pci_dev.dma_virt_addr) {
        printk(KERN_ERR DRIVER_NAME ": Failed to allocate consistent DMA memory\n");
        pci_disable_device(pdev);
        return -ENOMEM;
    }

    my_pci_dev.pdev = pdev;

    // 写入数据到 DMA 缓冲区
    memset(my_pci_dev.dma_virt_addr, 0xAA, my_pci_dev.dma_size);
    printk(KERN_INFO DRIVER_NAME ": Written data to DMA buffer\n");

    // 读取数据从 DMA 缓冲区
    printk(KERN_INFO DRIVER_NAME ": First byte of DMA buffer: 0x%02X\n", *((unsigned char *)my_pci_dev.dma_virt_addr));

    printk(KERN_INFO DRIVER_NAME ": Device probed successfully\n");
    return 0;
}

static void pci_example_remove(struct pci_dev *pdev) {
    printk(KERN_INFO DRIVER_NAME ": Removing device\n");

    // Free consistent DMA memory
    if (my_pci_dev.dma_virt_addr) {
        pci_free_consistent(pdev, my_pci_dev.dma_size, my_pci_dev.dma_virt_addr, my_pci_dev.dma_handle);
    }

    // Disable the PCI device
    pci_disable_device(pdev);

    printk(KERN_INFO DRIVER_NAME ": Device removed successfully\n");
}

static const struct pci_device_id pci_example_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_EXAMPLE, PCI_DEVICE_ID_EXAMPLE) },
    { 0, }
};

MODULE_DEVICE_TABLE(pci, pci_example_ids);

static struct pci_driver pci_example_driver = {
    .name = DRIVER_NAME,
    .id_table = pci_example_ids,
    .probe = pci_example_probe,
    .remove = pci_example_remove,
};

static int __init pci_example_init(void) {
    printk(KERN_INFO DRIVER_NAME ": Initializing driver\n");
    return pci_register_driver(&pci_example_driver);
}

static void __exit pci_example_exit(void) {
    printk(KERN_INFO DRIVER_NAME ": Exiting driver\n");
    pci_unregister_driver(&pci_example_driver);
}

module_init(pci_example_init);
module_exit(pci_example_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using pci_alloc_consistent in a PCI driver");