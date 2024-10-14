# 说明
<font size=4 color=red>本例程主要包含PCI/PCIe设备驱动中DMA操作处理和用户程序通过驱动程序使用设备的一般流程，驱动程序详细流程框架请参见[Linux_PCIe_driver_Gereral例程](../Linux_PCIe_driver_General/readme.md) （建议先看Linux_PCIe_driver_Gereral例程）</font><br><br>

# DMA读写操作

DMA操作需要手动去**申请空间、配置地址**，然后对空间上锁，使用内核函数: **ioread(),iowrite()进行读写**\
程序代码中请主要关注：file_operations 结构类型的对象 altera_dma_fops 中read(),write()等方法;

# 用户程序通过驱动程序使用设备
**Linux系统中所有设备都被抽象成文件，所以对设备的操作如同文件一样，就是打开、关闭、读写操作。使用的系统函数同样也是open(),close(),read(),write()**

- 1.打开设备，使用open()函数\
**参数1**：文件名"/dev/device_name"<font color=red>连通用户程序和驱动程序的桥梁</font>，这里的设备名需要和驱动程序中alloc_chrdev_region()函数指定的名称一致\
**参数2**：打开的操作类型，如 O_RDONLY 只读打开；O_WRONLY 只写打开；O_RDWR 读、写打开；O_APPEND 每次写时都加到文件的尾端，等\
**返回值**：[int]文件描述符
- 2.读写操作\
read(int fd, void *buf, size_t count),fd为文件描述符,open()函数获得；buf表示读出数据缓冲区地址；count表示读出的字节数；\
write(int fd, const void * buf, size_t count),fd为文件描述符；buf表示写入数据缓冲区地址；count表示写入的字节数；

- 3.关闭设备\
close(fd);fd为文件描述符
