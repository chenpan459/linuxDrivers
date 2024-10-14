# 说明
本例程是Windows官方基于WDF框架的PCI/PCIe设备的驱动程序

# 驱动程序框架
在基于WDF的Windows驱动程序中是在DriverEntry()入口函数中配置自定义的EvtDeviceAdd()函数，该函数中在WDF_PNPPOWER_EVENT_CALLBACK对象的EvtDevicePrepareHardware属性上注册对应的硬件初始化函数。框架会传进来一个WDFCMRESLIST对象，这是列表对象，其中包含的类型是PCM_PARTIAL_RESOURCE_DESCRIPTOR，通过该类型对象可以获取BAR空间配置信息，然后使用MmMapIoSpace()函数将物理地址转化为虚拟地址

# 用户程序访问设备
- 首先使用GetDevicePath(GUID)获取设备的路径,参数是和驱动程序中相同的GUID;
- 使用CreateFile()创建设备句柄，参数有第一步获取的设备路径和读写控制模式等；
- 读写控制DeviceIoControl(),参数有设备句柄，读写模式，源数据和目的位置地址等；