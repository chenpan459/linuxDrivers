#ifndef _ALTERA_DMA_H
#define _ALTERA_DMA_H

#define ALTERA_DMA_DRIVER_NAME    "Altera DMA"
#define ALTERA_DMA_DEVFILE        "gtspci"

#define PCI_MAIN_VER  0x0001
#define PCI_SLAVE_VER 0x1100


#define ALTERA_DMA_BAR_NUM (6)
#define ALTERA_DMA_DESCRIPTOR_NUM 128

#define ALTERA_EPLAST_DIFF              1


#define ONCHIP_MEM_BASE     0x0

#define MAX_NUM_DWORDS  0x7FFFF

#define ACCESS_EXT_MODULE	_IOWR(0xF6, 0x46, unsigned char)
#define GET_DRIVER_VERSION	_IOWR(0xF6, 0x47, unsigned char)

#define ACCESS_TYPE_READ	0
#define ACCESS_TYPE_WRITE	1


struct extmdl_cmd{
	unsigned short accessType;
	unsigned short offset;
	unsigned short count;
	unsigned short dataRead[16];
	unsigned short dataWrite[16];
};

struct driver_version{
	unsigned short mainVer;
	unsigned short slaveVer;
};
struct dma_descriptor {
    u32 src_addr_ldw;
    u32 src_addr_udw;
    u32 dest_addr_ldw;
    u32 dest_addr_udw;
    u32 ctl_dma_len;
    u32 reserved[3];
} __attribute__ ((packed));

struct dma_header {
    u32 eplast;
    u32 reserved[7];    
} __attribute__ ((packed));

struct dma_desc_table {
    struct dma_header header;
    struct dma_descriptor descriptors[ALTERA_DMA_DESCRIPTOR_NUM];
} __attribute__ ((packed));

struct lite_dma_header {
    volatile u32 flags[128];
} __attribute__ ((packed));


struct lite_dma_desc_table {
    struct lite_dma_header header;
    struct dma_descriptor descriptors[ALTERA_DMA_DESCRIPTOR_NUM];
} __attribute__ ((packed));

struct altera_pcie_dma_bookkeep {
    struct pci_dev *pci_dev;
	u8 *buffer;
	int mem_len;
	int mem_offset;
	int io_len;
	volatile unsigned long ioreg;
    u8 revision;
    u8 irq_pin;
    char msi_enabled;
    u8 irq_line;
    char dma_capable;
	spinlock_t lock;
    void * __iomem bar[ALTERA_DMA_BAR_NUM];
    size_t bar_length[ALTERA_DMA_BAR_NUM];

    struct dma_desc_table *table_rd_cpu_virt_addr;
    struct dma_desc_table *table_wr_cpu_virt_addr;
    struct lite_dma_desc_table *lite_table_rd_cpu_virt_addr;
    struct lite_dma_desc_table *lite_table_wr_cpu_virt_addr;
 
    dma_addr_t lite_table_rd_bus_addr; 
    dma_addr_t table_rd_bus_addr; 
    dma_addr_t lite_table_wr_bus_addr;
    dma_addr_t table_wr_bus_addr;

    int numpages;
    u8 *rp_rd_buffer_virt_addr;
    dma_addr_t rp_rd_buffer_bus_addr;
    u8 *rp_wr_buffer_virt_addr;
    dma_addr_t rp_wr_buffer_bus_addr;

    dev_t cdevno;
    struct cdev cdev;

	wait_queue_head_t wait_q;
	atomic_t status;
    struct task_struct *user_task;
    struct dma_status dma_status;
};

static int set_write_desc(struct dma_descriptor *rd_desc, dma_addr_t source, u64 dest, u32 ctl_dma_len, u32 id);
static int set_read_desc(struct dma_descriptor *wr_desc, u64 source, dma_addr_t dest, u32 ctl_dma_len, u32 id);
static int init_ep_mem(struct altera_pcie_dma_bookkeep *bk_ptr, u32 mem_byte_offset, u32 num_dwords, u32 init_value, u8 increment);
#endif /* _ALTERA_DMA_H */
