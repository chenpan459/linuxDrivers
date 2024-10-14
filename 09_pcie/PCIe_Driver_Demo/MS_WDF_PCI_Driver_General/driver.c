#include "driver.h"
#include "driver.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, My_PCIeEvtDeviceAdd)
#pragma alloc_text (PAGE, My_PCIeEvtDriverContextCleanup)
#endif


NTSTATUS
DriverEntry(
   IN PDRIVER_OBJECT  DriverObject,
   IN PUNICODE_STRING RegistryPath
    )
/*++
驱动程序入口

--*/
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;

    //
    // Initialize WDF WPP Tracing,为了调试
    //
    WPP_INIT_TRACING( DriverObject, RegistryPath );

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
	// 初始化 WDF_OBJECT_ATTRIBUTES 对象
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    //设备移除是资源的释放操作
    attributes.EvtCleanupCallback = My_PCIeEvtDriverContextCleanup;
	
    //重要步骤，配置设备添加时初始化的操作
	WDF_DRIVER_CONFIG_INIT(&config,
		My_PCIeEvtDeviceAdd
		);

    //创建driver
    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             WDF_NO_HANDLE
                             );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}


NTSTATUS
My_PCIeEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++

Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. Here the driver should register all the
    PNP, power and Io callbacks, register interfaces and allocate other
    software resources required by the device. The driver can query
    any interfaces or get the config space information from the bus driver
    but cannot access hardware registers or initialize the device.

    设备添加时该函数会被系统调用，这个函数中需要驱动注册PNP,power,IO的回调相应操作
--*/
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
	WDF_OBJECT_ATTRIBUTES   deviceAttributes;
	WDFDEVICE device;
	PDEVICE_CONTEXT deviceContext;

	WDFQUEUE queue;
	WDF_IO_QUEUE_CONFIG    queueConfig;

	/*+++++Interrupt
	WDF_INTERRUPT_CONFIG	interruptConfig;
	-----*/

    // 原型#define UNREFERENCED_PARAMETER(P) (P) ,展开传递的参数或表达式。其目的是避免编译器关于未引用参数的警告
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

	//采用WdfDeviceIoDirect方式
	WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);


   // status = Spw_PCIeCreateDevice(DeviceInit);

	//初始化即插即用和电源管理例程配置结构
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

	//设置即插即用基本例程，硬件设备初始化或者叫获取硬件信息，以及设备资源释放
	pnpPowerCallbacks.EvtDevicePrepareHardware = My_PCIeEvtDevicePrepareHardware;  //重要，PCIe设备配置信息获取都在这个函数里
	pnpPowerCallbacks.EvtDeviceReleaseHardware = My_PCIeEvtDeviceReleaseHardware;

     //
    // These two callbacks set up and tear down hardware state that must be
    // done every time the device moves in and out of the D0-working state.
    //
	pnpPowerCallbacks.EvtDeviceD0Entry = My_PCIeEvtDeviceD0Entry;
	pnpPowerCallbacks.EvtDeviceD0Exit = My_PCIeEvtDeviceD0Exit;

	//注册即插即用和电源管理例程
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

	
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);


	//deviceAttributes.EvtCleanupCallback = Spw_PCIeEvtDriverContextCleanup;
	//
	// Set WDFDEVICE synchronization scope. By opting for device level
	// synchronization scope, all the queue and timer callbacks are
	// synchronized with the device-level spinlock.
	//
	deviceAttributes.SynchronizationScope = WdfSynchronizationScopeDevice;

	status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	deviceContext = GetDeviceContext(device);///????
	//deviceContext->Device = device;
	//
	// 初始化Context这个结构里的所有成员.
	//
	//deviceContext->PrivateDeviceData = 0;
	/*++++++Interrupt & DMA
	//设置中断服务例程和延迟过程调用
	WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
	PCISample_EvtInterruptIsr,
	PCISample_EvtInterruptDpc);

	//创建中断对象
	status = WdfInterruptCreate(device,
	&interruptConfig,
	WDF_NO_OBJECT_ATTRIBUTES,
	&pDeviceContext->Interrupt);
	if (!NT_SUCCESS (status)) {
	return status;
	}

	status = InitializeDMA(device);

	if (!NT_SUCCESS(status)) {
	return status;
	}
	-----*/
	//WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
	//Initialize the Queue
	//		queueConfig.EvtIoDefault = Spw_PCIeEvtIoDefault;
	//		queueConfig.EvtIoWrite = Spw_PCIeEvtIoWrite;
	//queueConfig.EvtIoRead = Spw_PCIeEvtIoRead;
	//		queueConfig.EvtIoStop = Spw_PCIeEvtIoStop;
	//The driver must initialize the WDF_IO_QUEUE_CONFIG structure 
	//by calling WDF_IO_QUEUE_CONFIG_INIT or WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE.
	//用default初始化default 队列，用另一个初始化非default队列
	WDF_IO_QUEUE_CONFIG_INIT(
		&queueConfig,
		WdfIoQueueDispatchSequential
		);

	queueConfig.EvtIoDeviceControl = Spw_PCIeEvtIoDeviceControl;


	status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//对于非默认队列，必须指定要分发的I/O请求类型
	//The WdfDeviceConfigureRequestDispatching method causes the framework to queue a specified type of I/O requests to a specified I/O queue.
	status = WdfDeviceConfigureRequestDispatching(
		device,
		queue,
		WdfRequestTypeDeviceControl
		);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	//创建驱动程序接口与应用程序通信
	status = WdfDeviceCreateDeviceInterface(
		device,
		(LPGUID)&GUID_DEVINTERFACE_Spw_PCIe,
		NULL // ReferenceString
		);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	/*
	if (NT_SUCCESS(status)) {
	//
	// Initialize the I/O Package and any Queues
	//
	status = Spw_PCIeQueueInitialize(device);
	}
	*/
	//deviceContext->MemLength = MAXNLEN;

    //TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

VOID
My_PCIeEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
/*++
Routine Description:
    释放资源
    Free all the resources allocated in DriverEntry.

Arguments:

    DriverObject - handle to a WDF Driver object.
--*/
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE ();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "My_PCIeEvtDriverContextCleanup : enter");

    WPP_CLEANUP( WdfDriverWdmGetDriverObject(DriverObject) );

}

NTSTATUS
My_PCIeEvtDevicePrepareHardware (
    WDFDEVICE      Device,
    WDFCMRESLIST   Resources,
    WDFCMRESLIST   ResourcesTranslated
    )
/*++

Routine Description:

    Performs whatever initialization is needed to setup the device, setting up
    a DMA channel or mapping any I/O port resources.  This will only be called
    as a device starts or restarts, not every time the device moves into the D0
    state.  Consequently, most hardware initialization belongs elsewhere.

Arguments:

    Device - A handle to the WDFDEVICE

    Resources - The raw PnP resources associated with the device.  Most of the
        time, these aren't useful for a PCI device.

    ResourcesTranslated - The translated PnP resources associated with the
        device.  This is what is important to a PCI device.

Return Value:

    NT status code - failure will result in the device stack being torn down

--*/
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_EXTENSION   devExt;

    UNREFERENCED_PARAMETER(Resources);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "--> EvtDevicePrepareHardware");

    devExt = PLxGetDeviceContext(Device);

    status =  (devExt, ResourcesTranslated);
    if (!NT_SUCCESS (status)){
        return status;
    }


    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "<-- PLxEvtDevicePrepareHardware, status %!STATUS!", status);

    return status;
}
