# 说明
<font size=4 color=red>本例程是Linux系统中一个简单通用的PCI/PCIe设备驱动框架</font>

## 1.驱动程序执行主要流程：
1)用module_init()指定入口函数

2)入口函数中主要的操作：
- 2.1将pci_driver对象和设备对象pci_dev绑定，初始化设备对象。相关的函数是pci_register_driver()和pci_driver中的probe对应的自定义函数，读取BAR空间在该函数中完成；\
**设备的初始化在probe对应的自定义函数中,对于PCI/PCIe主要就是：i)读取Bar空间信息；ii)DMA相关的配置；iii)中断响应函数配置**
- 2.2申请设备号，有了设备号后就能像文件一样操作
- 2.3初始化字符设备对象，主要就是注册 文件操作方法集(file_operations)到设备对象

- 2.4将设备注册的系统

## 2.相关知识
### 内核<linux/fs.h>中的结构体
大部分驱动程序操作都涉及三个重要的内核数据结构，分别是file_operations,file,inode。第一个是文件操作，file_operations结构就是用来连接驱动程序操作和设备；第二个file结构是一个内核结构，不会出现在用户程序中。它不仅仅代表一个打开的文件。它由内核在open时创建，并传递给该文件上进行操作的所有函数,直到最后的close函数，在文件的所有实例都被关闭后，内核会释放这个数据结；第三个inode结构它表示打开的文件描述符，包含大量有关文件的信息。而只有 dev_t i_rdev;（设备号） struct cdev *i_cdev（设备对象）与驱动程序代码有关用。struct file结构体中包含有struct file_operations结构体，使用系统调用open()打开一个设备节点struct inode时，会得到一个文件struct file,同时返回一个文件描述符，该文件描述符是一个整数，称之为句柄，通过访问句柄能够访问设备文件struct file,描述符是一个有着特殊含义的整数，特定位都有一定的意义或属性。

