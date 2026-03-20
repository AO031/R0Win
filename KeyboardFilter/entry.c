#include <ntifs.h>
#include <ntstrsafe.h>
#include <ntddkbd.h>

//================================================================
//Macro Declaration
//================================================================
#define MAX_BLOCKED_KEYS 256
#define REMOVE_LOCK_TAG 'ao31'
#define MAX_KEYBOARD_DEVICES 10


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

NTSTATUS AttachKeyboardObject(PDRIVER_OBJECT driverObj);
VOID DetachKeyboardObject();

NTSTATUS DispatchPassThrough(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchCreate(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchRead(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchClose(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchPnp(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchPower(PDEVICE_OBJECT deviceObj, PIRP irp);
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObj, PIRP irp);

NTSTATUS ReadCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);
PCSTR GetKeyName(USHORT scanCode, USHORT flags);
VOID LogKeyPress(PKEYBOARD_DEVICE_EXTENSION devExt, PKEYBOARD_INPUT_DATA key, BOOLEAN blocked);
BOOLEAN ShouldBlocked(PKEYBOARD_DEVICE_EXTENSION devExt, PKEYBOARD_INPUT_DATA key);


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

	driverObj->MajorFunction[IRP_MJ_PNP] = DispatchPnp;
	driverObj->MajorFunction[IRP_MJ_READ] = DispatchRead;
	driverObj->MajorFunction[IRP_MJ_POWER] = DispatchPower;
	driverObj->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	driverObj->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	driverObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

	st = CreateControlDevice(driverObj);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] CreateControlDevice Failed->%lX\n", st);
		return st;
	}

	st = AttachKeyboardObject(driverObj);
	if (!NT_SUCCESS(st)) {
		DeleteControlDevice();
		DbgPrint("[E] AttachKeyboardObject Failed->%lX\n", st);
		return st;
	}

	driverObj->DriverUnload = DriverUnload;
	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObj) {
	LARGE_INTEGER delay = { 0 };

	g_globalContext.isUnloading = TRUE;

	delay.QuadPart = -5000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	DetachKeyboardObject();

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

	RtlInitUnicodeString(&symlicLinkName, L"\\DosDevices\\KeyboardFilterControl");

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

		IoReleaseRemoveLock(&g_globalContext.controlRemoveLock, (PVOID)REMOVE_LOCK_TAG);

		IoDeleteDevice(g_globalContext.controlDevice);

		g_globalContext.controlDevice = NULL;
	}
}

NTSTATUS AttachKeyboardObject(PDRIVER_OBJECT driverObj) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING kbdClassName = { 0 };
	PFILE_OBJECT fileObj = NULL;
	PDEVICE_OBJECT kbdDeviceObj = NULL;
	PDEVICE_OBJECT lowDevice = NULL;
	PDEVICE_OBJECT filterDeivce = NULL;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;
	ULONG deviceCount = 0;
	WCHAR deviceNameBuffer[64] = { 0 };

	for (ULONG i = 0; i < MAX_KEYBOARD_DEVICES; i++) {
		RtlStringCbPrintfW(deviceNameBuffer, sizeof(deviceNameBuffer), L"\\Device\\KeyboardClass%u", i);
		RtlInitUnicodeString(&kbdClassName, deviceNameBuffer);
		
		st = IoGetDeviceObjectPointer(&kbdClassName, FILE_READ_DATA, &fileObj, &kbdDeviceObj);
		if (!NT_SUCCESS(st)) continue;

		st = IoCreateDevice(
			driverObj,
			sizeof(KEYBOARD_DEVICE_EXTENSION),
			NULL,
			FILE_DEVICE_KEYBOARD,
			0,
			FALSE,
			&filterDeivce
		);
		if (!NT_SUCCESS(st)) {
			ObDereferenceObject(fileObj);
			continue;
		}

		lowDevice = IoAttachDeviceToDeviceStack(filterDeivce, kbdDeviceObj);
		if (!lowDevice) {
			IoDeleteDevice(filterDeivce);
			ObDereferenceObject(fileObj);
			continue;
		}

		devExt = (PKEYBOARD_DEVICE_EXTENSION)filterDeivce->DeviceExtension;
		RtlZeroMemory(devExt, sizeof(KEYBOARD_DEVICE_EXTENSION));

		devExt->lowerDeviceObj = lowDevice;
		devExt->selfDevice = filterDeivce;
		devExt->enableFilter = TRUE;
		devExt->logKeys = TRUE;
		devExt->totalKeys = 0;
		devExt->blockedKey = 0;
		devExt->blockedCount = 1;
		devExt->blockedScanCode[0] = 0x02;
		devExt->shiftPressed = FALSE;
		devExt->ctrlPressed = FALSE;
		devExt->altPressed = FALSE;
	
		IoInitializeRemoveLock(&devExt->removeLock, filterDeivce, 0, 0);
		st = IoAcquireRemoveLock(&devExt->removeLock, filterDeivce);
		if (!NT_SUCCESS(st)) {
			IoDetachDevice(lowDevice);
			IoDeleteDevice(filterDeivce);
			ObDereferenceObject(fileObj);
			continue;
		}

		KeInitializeSpinLock(&devExt->pendingIrpLock);

		devExt->pendingReadIrp = NULL;
		devExt->isDetaching = FALSE;

		filterDeivce->Flags |= lowDevice->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
		filterDeivce->Type = lowDevice->Type;
		filterDeivce->Characteristics = lowDevice->Characteristics;
		filterDeivce->Flags &= (~DO_DEVICE_INITIALIZING);

		ObDereferenceObject(fileObj);
		deviceCount++;
		DbgPrint("[W] Attached:%wZ\n", &kbdClassName);
	}

	if (deviceCount == 0) {
		DbgPrint("[E] No KeyBoard Device Found\n");
		return STATUS_UNSUCCESSFUL;
	}

	DbgPrint("[W] Total Attach:%u devices\n", deviceCount);

	return STATUS_SUCCESS;
}

VOID DetachKeyboardObject() {
	PDEVICE_OBJECT deviceObj = NULL;
	PDEVICE_OBJECT nextDeivce = NULL;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;
	PDEVICE_OBJECT* deviceArray = NULL;
	ULONG deviceCount = 0;
	LARGE_INTEGER delay = { 0 };
	KIRQL oldIrql = 0;

	deviceArray = (PDEVICE_OBJECT*)ExAllocatePool(NonPagedPool, sizeof(PDEVICE_OBJECT) * MAX_KEYBOARD_DEVICES);
	if (!deviceArray) return;

	deviceObj = g_globalContext.driverObj->DeviceObject;

	while (deviceObj && deviceCount < MAX_KEYBOARD_DEVICES) {
		nextDeivce = deviceObj->NextDevice;
		if (deviceObj != g_globalContext.controlDevice) deviceArray[deviceCount++] = deviceObj;
		deviceObj = nextDeivce;
	}

	for (ULONG i = 0; i < deviceCount; i++) {
		deviceObj = deviceArray[i];
		devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;

		KeAcquireSpinLock(&devExt->pendingIrpLock, &oldIrql);
		devExt->isDetaching = TRUE;
		KeReleaseSpinLock(&devExt->pendingIrpLock, oldIrql);
	}

	for (ULONG i = 0; i < deviceCount; i++) {
		deviceObj = deviceArray[i];

		devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;

		if (devExt->lowerDeviceObj) {
			IoDetachDevice(devExt->lowerDeviceObj);
			devExt->lowerDeviceObj = NULL;
		}
	}

	DbgPrint("[W] Waiting For IRP\n");
	
	delay.QuadPart = -10000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	for (ULONG i = 0; i < deviceCount; i++) {
		deviceObj = deviceArray[i];

		devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;
		//TODO

		IoReleaseRemoveLockAndWait(&devExt->removeLock, (PVOID)deviceObj);

		IoDeleteDevice(deviceObj);
	}

	ExFreePool(deviceArray);

	DbgPrint("[W] All Device Detach Success\n");

	return;
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

		st = IoAcquireRemoveLock(&g_globalContext.controlRemoveLock, irp);
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

	devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;
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
	
	st = IoAcquireRemoveLock(&devExt->removeLock, irp);
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

		st = IoAcquireRemoveLock(&g_globalContext.controlRemoveLock, irp);
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

	devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;
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
	NTSTATUS st = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;

	if (deviceObj == g_globalContext.controlDevice) {
		PoStartNextPowerIrp(irp);
		irp->IoStatus.Status = st;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return st;
	}
	
	devExt = (PKEYBOARD_DEVICE_EXTENSION)deviceObj->DeviceExtension;

	PoStartNextPowerIrp(irp);
	IoSkipCurrentIrpStackLocation(irp);
	st = PoCallDriver(devExt->lowerDeviceObj, irp);

	return st;
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObj, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpStack = NULL;
	ULONG ioControlCode = 0;
	ULONG inputBufferLength = 0;
	ULONG outputBufferLength = 0;
	PVOID systemBuffer = NULL;
	ULONG byteReturn = 0;
	
	irpStack = IoGetCurrentIrpStackLocation(irp);

	ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;
	inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	systemBuffer = irp->AssociatedIrp.SystemBuffer;

	DbgPrint("[W] IOCTL: %lX | In=%u | Out=%u\n", ioControlCode, inputBufferLength, outputBufferLength);

	switch (ioControlCode)
	{
	default:
		DbgPrint("[E] Unsupported IOCTL\n");
		st = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}


	irp->IoStatus.Status = st;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return st;
}

NTSTATUS ReadCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {
	PDEVICE_OBJECT filterDevice = (PDEVICE_OBJECT)Context;
	PKEYBOARD_DEVICE_EXTENSION devExt = NULL;
	PIO_STACK_LOCATION irpSp = NULL;
	PKEYBOARD_INPUT_DATA keys = NULL;
	ULONG numKeys = 0;
	BOOLEAN mofidied = FALSE;
	KIRQL oldIrql = { 0 };
	BOOLEAN shouldBlocked = TRUE;
	ULONG i = 0;
	ULONG j = 0;

	devExt = (PKEYBOARD_DEVICE_EXTENSION)filterDevice->DeviceExtension;

	KeAcquireSpinLock(&devExt->pendingIrpLock, &oldIrql);
	if (devExt->pendingReadIrp) devExt->pendingReadIrp = NULL;
	KeReleaseSpinLock(&devExt->pendingIrpLock, oldIrql);

	if (devExt->isDetaching) {
		IoReleaseRemoveLock(&devExt->removeLock, Irp);
		if (Irp->PendingReturned) IoMarkIrpPending(Irp);
		return STATUS_SUCCESS;
	}

	if (!NT_SUCCESS(Irp->IoStatus.Status)) goto Exit;

	irpSp = IoGetCurrentIrpStackLocation(Irp);

	keys = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
	if (!keys) goto Exit;

	numKeys = (ULONG)(Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA));
	if (!numKeys) goto Exit;

	for (i = 0, j = 0; i < numKeys; i++) {
		PKEYBOARD_INPUT_DATA key = &keys[i];
		BOOLEAN shouldBlocked = FALSE;

		devExt->totalKeys++;

		if (devExt->logKeys) LogKeyPress(devExt, key, FALSE);

		if (devExt->enableFilter) {
			shouldBlocked = ShouldBlocked(devExt, key);
		}

		if (shouldBlocked) {
			devExt->blockedKey++;
			mofidied = TRUE;
			if (devExt->logKeys) LogKeyPress(devExt, key, TRUE);
		}
		else {
			if (j != i) keys[j] = keys[i];
			j++;
		}
	}

	if (mofidied) Irp->IoStatus.Information = j * sizeof(KEYBOARD_INPUT_DATA);

Exit:
	IoReleaseRemoveLock(&devExt->removeLock, Irp);
	if (Irp->PendingReturned) IoMarkIrpPending(Irp);
	return STATUS_SUCCESS;
}

VOID LogKeyPress(PKEYBOARD_DEVICE_EXTENSION devExt, PKEYBOARD_INPUT_DATA key, BOOLEAN blocked) {
	PCSTR keyName = GetKeyName(key->MakeCode,key->Flags);
	PCSTR action = (key->Flags & KEY_BREAK) ? "UP" : "ON";
	PCSTR prefix = blocked ? "[X]" : "[O]";

	DbgPrint("[W] %s %s | %-10s | ScanCode:0x%hX Flags:0x%hX\n", prefix, action, keyName, key->MakeCode, key->Flags);
}

PCSTR GetKeyName(USHORT scanCode, USHORT flags) {
	// 判断是否为扩展键（如右Ctrl、方向键、Ins/Del等） [citation:3]
	BOOLEAN isE0 = (flags & KEY_E0) != 0;
	BOOLEAN isE1 = (flags & KEY_E1) != 0;

	// 构建用于查找的完整扫描码值（考虑E0/E1前缀）
	// 注意：扫描码通常是1字节，但扩展键需要特殊处理
	USHORT fullScanCode = scanCode & 0xFF; // 取低8位

	// 处理带E0前缀的键（例如右Alt、右Ctrl、方向键等）
	if (isE0) {
		// 对于E0扩展键，很多需要特殊映射
		// 例如：右Ctrl的扫描码是0x1D，但带E0标志
		switch (scanCode) {
		case 0x1D: return "Right Ctrl";  // E0标志的右Ctrl [citation:10]
		case 0x38: return "Right Alt";   // E0标志的右Alt [citation:10]
		case 0x48: return "Up Arrow";    // 上箭头 [citation:10]
		case 0x50: return "Down Arrow";  // 下箭头 [citation:10]
		case 0x4B: return "Left Arrow";  // 左箭头 [citation:10]
		case 0x4D: return "Right Arrow"; // 右箭头 [citation:10]
		case 0x47: return "Home";        // Home [citation:10]
		case 0x4F: return "End";         // End [citation:10]
		case 0x49: return "Page Up";      // Page Up [citation:10]
		case 0x51: return "Page Down";    // Page Down [citation:10]
		case 0x52: return "Insert";       // Insert [citation:10]
		case 0x53: return "Delete";       // Delete [citation:10]
		case 0x35: return "/";            // 数字键盘/ [citation:10]
		case 0x5B: return "Left Win";     // 左Win键 [citation:5][citation:8]
		case 0x5C: return "Right Win";    // 右Win键 [citation:5][citation:8]
		case 0x5D: return "Menu";         // 菜单键/Application [citation:5][citation:8]
		}
		return "Unknown E0 Key";
	}

	// 处理带E1前缀的键（通常是Pause/Break等特殊键）
	if (isE1) {
		return "Pause/Break";  // E1通常用于Pause键
	}

	// 处理普通按键（不带E0/E1前缀）
	// 基于扫描码的映射表 [citation:5][citation:10]
	switch (scanCode) {
		// 字母键 (根据键盘扫描码标准映射)
	case 0x10: return "Q";
	case 0x11: return "W";
	case 0x12: return "E";
	case 0x13: return "R";
	case 0x14: return "T";
	case 0x15: return "Y";
	case 0x16: return "U";
	case 0x17: return "I";
	case 0x18: return "O";
	case 0x19: return "P";
	case 0x1E: return "A";
	case 0x1F: return "S";
	case 0x20: return "D";
	case 0x21: return "F";
	case 0x22: return "G";
	case 0x23: return "H";
	case 0x24: return "J";
	case 0x25: return "K";
	case 0x26: return "L";
	case 0x2C: return "Z";
	case 0x2D: return "X";
	case 0x2E: return "C";
	case 0x2F: return "V";
	case 0x30: return "B";
	case 0x31: return "N";
	case 0x32: return "M";

		// 数字键和符号键
	case 0x02: return "1";
	case 0x03: return "2";
	case 0x04: return "3";
	case 0x05: return "4";
	case 0x06: return "5";
	case 0x07: return "6";
	case 0x08: return "7";
	case 0x09: return "8";
	case 0x0A: return "9";
	case 0x0B: return "0";
	case 0x0C: return "-";
	case 0x0D: return "=";
	case 0x1A: return "[";
	case 0x1B: return "]";
	case 0x2B: return "\\";
	case 0x27: return ";";
	case 0x28: return "'";
	case 0x33: return ",";
	case 0x34: return ".";
	case 0x35: return "/";

		// 功能键和控制键
	case 0x01: return "Esc";
	case 0x3B: return "F1";
	case 0x3C: return "F2";
	case 0x3D: return "F3";
	case 0x3E: return "F4";
	case 0x3F: return "F5";
	case 0x40: return "F6";
	case 0x41: return "F7";
	case 0x42: return "F8";
	case 0x43: return "F9";
	case 0x44: return "F10";
	case 0x57: return "F11";
	case 0x58: return "F12";

	case 0x0E: return "Backspace";
	case 0x0F: return "Tab";
	case 0x1C: return "Enter";
	case 0x39: return "Space";
	case 0x3A: return "Caps Lock";
	case 0x45: return "Num Lock";
	case 0x46: return "Scroll Lock";
	case 0x2A: return "Left Shift";
	case 0x36: return "Right Shift";
	case 0x1D: return "Left Ctrl";
	case 0x38: return "Left Alt";

		// 数字键盘（不带Num Lock时）
	case 0x47: return "7 (NumPad)";
	case 0x48: return "8 (NumPad)";
	case 0x49: return "9 (NumPad)";
	case 0x4B: return "4 (NumPad)";
	case 0x4C: return "5 (NumPad)";
	case 0x4D: return "6 (NumPad)";
	case 0x4F: return "1 (NumPad)";
	case 0x50: return "2 (NumPad)";
	case 0x51: return "3 (NumPad)";
	case 0x52: return "0 (NumPad)";
	case 0x53: return "Del (NumPad)";
	case 0x4A: return "- (NumPad)";
	case 0x4E: return "+ (NumPad)";
	case 0x37: return "* (NumPad)";

	default:
		// 如果都不匹配，返回未知
		return "UNKNOWN";
	}
}

BOOLEAN ShouldBlocked(PKEYBOARD_DEVICE_EXTENSION devExt, PKEYBOARD_INPUT_DATA key) {
	if (!devExt->enableFilter) return FALSE;

	for (ULONG i = 0; i < devExt->blockedCount; i++) {
		if (devExt->blockedScanCode[i] == key->MakeCode) return TRUE;
	}
	
	return FALSE;
}