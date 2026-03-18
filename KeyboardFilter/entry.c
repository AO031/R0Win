#include <ntifs.h>

//================================================================
//Macro Declaration
//================================================================
#define MAX_BLOCKED_KEYS 256
#define REMOVE_LOCK_TAG 'ao31'


//================================================================
//Struct Declaration
//================================================================
typedef struct _GLOBAL_CONTEXT {
	PDRIVER_OBJECT driverObj;
	PDEVICE_OBJECT controlDevice;
	UNICODE_STRING symbolicLinkName;
	BOOLEAN symbolicLinkCreated;
	BOOLEAN isUnloading;
	IO_REMOVE_LOCK controlRemoveLock;
}GLOBAL_CONTEXT, *PGLOBAL_CONTEXT;

typedef struct _KEYBOARD_DEVICE_EXTENSION {
	PDEVICE_OBJECT lowerDeviceObj;
	PDEVICE_OBJECT selfDevice;
	IO_REMOVE_LOCK removeLock;
	BOOLEAN enableFilter;
	BOOLEAN logKeys;
	ULONG totalKeys;
	ULONG blockedKey;
	ULONG blockedCount;
	BOOLEAN shiftPressed;
	BOOLEAN ctrlPressed;
	BOOLEAN altPressed;
	PIRP pendingReadIrp;
	KSPIN_LOCK pendingIrpLock;
	BOOLEAN isDetaching;
	USHORT blockedScanCode[MAX_BLOCKED_KEYS];
}KEYBOARD_DEVICE_EXTENSION, *PKEYBOARD_DEVICE_EXTENSION;


//================================================================
//Global Value Declaration
//================================================================
GLOBAL_CONTEXT g_globalContext = { 0 };


//================================================================
//Function Declaration
//================================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath);
VOID DriverUnload(PDRIVER_OBJECT driverObj);

NTSTATUS CreateControlDevice(PDRIVER_OBJECT driverObj);
VOID DeleteControlDevice(VOID);

NTSTATUS DispatchPassThrough(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchCreate(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchRead(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchClose(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchPnp(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchPower(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObj, PIRP irp);

NTSTATUS ReadCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);


//================================================================
//Function Implementation
//================================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;
	DbgPrint("[W] Driver Entry\n");

	RtlZeroMemory(&g_globalContext, sizeof(GLOBAL_CONTEXT));

	g_globalContext.driverObj = driverObj;
	g_globalContext.isUnloading = FALSE;

	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		driverObj->MajorFunction[i] = DispatchPassThrough;
	}

	driverObj->MajorFunction[IRP_MJ_PNP] = NULL;
	driverObj->MajorFunction[IRP_MJ_READ] = DispatchRead;
	driverObj->MajorFunction[IRP_MJ_POWER] = NULL;
	driverObj->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	driverObj->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	driverObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = NULL;

	driverObj->DriverUnload = DriverUnload;
	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObj) {
	LARGE_INTEGER delay = { 0 };

	g_globalContext.isUnloading = TRUE;

	delay.QuadPart = -5000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);


	DeleteControlDevice();
	DbgPrint("[W] Driver Unload\n");
}

NTSTATUS DispatchPassThrough(PDEVICE_OBJECT deviceObj, PIRP irp) {
	// Check If Control Device
	if (deviceObj == g_globalContext.controlDevice) {
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return STATUS_INVALID_DEVICE_REQUEST;
	}

	PKEYBOARD_DEVICE_EXTENSION devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;
	IoSkipCurrentIrpStackLocation(irp);

	return IoCallDriver(devExt->lowerDeviceObj, irp);
}

NTSTATUS CreateControlDevice(PDRIVER_OBJECT driverObj) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING deviceName = { 0 };
	UNICODE_STRING symlicLinkName = { 0 };
	PDEVICE_OBJECT controlDevice = NULL;

	RtlInitUnicodeString(&deviceName, L"\\Device\\KeyboardFilterControl");

	st = IoCreateDevice(
		driverObj,
		0,
		&deviceName,
		FILE_DEVICE_KEYBOARD,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&controlDevice
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] IoCreateDevice Failed->%lX\n", st);
		return st;
	}

	controlDevice->Flags |= DO_BUFFERED_IO;
	controlDevice->Flags &= ~DO_DEVICE_INITIALIZING;

	RtlInitUnicodeString(&symlicLinkName, L"\\DosDevice\\KeyboardFilterControl");

	st = IoCreateSymbolicLink(&symlicLinkName, &deviceName);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] IoCreateSymbolicLink Failed->%lX\n", st);
		IoDeleteDevice(controlDevice);
		return st;
	}

	IoInitializeRemoveLock(&g_globalContext.controlRemoveLock, REMOVE_LOCK_TAG, 0, 0);

	st = IoAcquireRemoveLock(&g_globalContext.controlRemoveLock, (PVOID)REMOVE_LOCK_TAG);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] IoAcquireRemoveLock Failed->%lX\n", st);
		IoDeleteSymbolicLink(&symlicLinkName);
		IoDeleteDevice(controlDevice);
		return st;
	}

	g_globalContext.controlDevice = controlDevice;
	g_globalContext.symbolicLinkName = symlicLinkName;
	g_globalContext.symbolicLinkCreated = TRUE;

	DbgPrint("[W] CreateCreateDevice Success\n");
	return st;
}

VOID DeleteControlDevice(VOID) {
	if (g_globalContext.controlDevice) {
		if (g_globalContext.symbolicLinkCreated) {
			IoDeleteSymbolicLink(&g_globalContext.symbolicLinkName);
			g_globalContext.symbolicLinkCreated = FALSE;
		}

		IoReleaseRemoveLock(&g_globalContext.controlRemoveLock, REMOVE_LOCK_TAG);

		IoDeleteDevice(g_globalContext.controlDevice);

		g_globalContext.controlDevice = NULL;
	}
}

NTSTATUS DispatchCreate(PDEVICE_OBJECT deviceObj, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;

	if (deviceObj == g_globalContext.controlDevice) {
		if (g_globalContext.isUnloading) {
			st = STATUS_DELETE_PENDING;
			irp->IoStatus.Status = st;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			return st;
		}

		st = IoAcquireRemoveLock(&g_globalContext, irp);
		if (!NT_SUCCESS(st)) {
			irp->IoStatus.Status = st;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			return st;
		}

		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);

		IoReleaseRemoveLock(&g_globalContext.controlRemoveLock, irp);
		return st;
	}

	PKEYBOARD_DEVICE_EXTENSION devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;
	IoSkipCurrentIrpStackLocation(irp);

	st = IoCallDriver(devExt->lowerDeviceObj, irp);
	return st;
}

NTSTATUS DispatchRead(PDEVICE_OBJECT deviceObj, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;
	KIRQL oldIrql = 0;

	if (deviceObj == g_globalContext.controlDevice) {
		st = STATUS_DELETE_PENDING;
		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return st;
	}
	
	devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;

	KeAcquireSpinLock(&devExt->pendingIrpLock, &oldIrql);

	if (devExt->isDetaching) {
		KeReleaseSpinLock(&devExt->pendingIrpLock, oldIrql);
		st = STATUS_DELETE_PENDING;
		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return st;
	}

	devExt->pendingReadIrp = irp;

	KeReleaseSpinLock(&devExt->pendingIrpLock, oldIrql);
	
	st = IoAcquireRemoveLock(&devExt->pendingIrpLock, irp);
	if (!NT_SUCCESS(st)) {
		KeAcquireSpinLock(&devExt->pendingIrpLock, &oldIrql);
		devExt->pendingReadIrp = NULL;
		KeReleaseSpinLock(&devExt->pendingIrpLock, oldIrql);

		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return st;
	}

	IoCopyCurrentIrpStackLocationToNext(irp);
	IoSetCompletionRoutine(irp, ReadCompletionRoutine, deviceObj, TRUE, TRUE, TRUE);

	//??
	//IoReleaseRemoveLock(&g_globalContext.controlRemoveLock, irp);

	st = IoCallDriver(devExt->lowerDeviceObj, irp);
	return st;
}

NTSTATUS DispatchClose(PDEVICE_OBJECT deviceObj, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;

	if (deviceObj == g_globalContext.controlDevice) {
		if (g_globalContext.isUnloading) {
			st = STATUS_DELETE_PENDING;
			irp->IoStatus.Status = st;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			return st;
		}

		st = IoAcquireRemoveLock(&g_globalContext, irp);
		if (!NT_SUCCESS(st)) {
			irp->IoStatus.Status = st;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			return st;
		}

		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);

		IoReleaseRemoveLock(&g_globalContext.controlRemoveLock, irp);
		return st;
	}

	PKEYBOARD_DEVICE_EXTENSION devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;
	IoSkipCurrentIrpStackLocation(irp);

	st = IoCallDriver(devExt->lowerDeviceObj, irp);
	return st;
}

NTSTATUS DispatchPnp(PDEVICE_OBJECT deviceObj, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = NULL;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;

	if (deviceObj == g_globalContext.controlDevice) {
		st = STATUS_SUCCESS;
		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return st;
	}

	irpSp = IoGetCurrentIrpStackLocation(irp);
	devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;

	switch (irpSp->MinorFunction) {
	case IRP_MN_REMOVE_DEVICE: {
		IoSkipCurrentIrpStackLocation(irp);

		st = IoCallDriver(devExt->lowerDeviceObj, irp);

		IoDetachDevice(devExt->lowerDeviceObj);

		IoDeleteDevice(deviceObj);

		return st;
	}
	default: {
		IoSkipCurrentIrpStackLocation(irp);
		st = IoCallDriver(devExt->lowerDeviceObj, irp);
		return st;
	}

	}
}

NTSTATUS DispatchPower(PDEVICE_OBJECT deviceObj, PIRP irp) {

}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObj, PIRP irp) {

}

NTSTATUS ReadCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {

}
