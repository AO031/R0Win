#include <ntifs.h>

#define DEVICE_NAME L"\\Device\\MyFirstDevice"
#define SYMBOLIC_LINK_NAME L"\\??\\MyFirstDevice"

#define IOCTL_SEND_DATA		CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_RECEIVE_DATA	CTL_CODE(FILE_DEVICE_UNKNOWN,0x801,METHOD_BUFFERED,FILE_ANY_ACCESS)

typedef struct {
	PDEVICE_OBJECT DeviceObject;
	UNICODE_STRING DeviceName;
	UNICODE_STRING SymbolicName;
	UCHAR DataBuffer[0xFF];
	ULONG DataLength;
}DeviceContext;

DeviceContext g_deviceContext = { 0 };

NTSTATUS DispatchCreate(PDEVICE_OBJECT deviceObject, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] DispatchCreate\n");
	irp->IoStatus.Status = st;
	irp->IoStatus.Information = 0;

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return st;
}

NTSTATUS HandleSendData(PVOID inputBuffer, ULONG inputLength) {
	if (!inputBuffer || !inputLength) return STATUS_INVALID_PARAMETER;

	RtlCopyMemory(g_deviceContext.DataBuffer, inputBuffer, inputLength);
	g_deviceContext.DataLength = inputLength;

	return STATUS_SUCCESS;
}

NTSTATUS HandleReceiveData(PVOID outputBuffer, ULONG outputLength,ULONG* byteReturn) {
	if (!outputBuffer || !outputLength || !byteReturn) return STATUS_INVALID_PARAMETER;
	if (outputLength < g_deviceContext.DataLength) return STATUS_BUFFER_TOO_SMALL;
	
	RtlCopyMemory(outputBuffer, g_deviceContext.DataBuffer, g_deviceContext.DataLength);
	*byteReturn = g_deviceContext.DataLength;

	return STATUS_SUCCESS;
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpStack = { 0 };
	ULONG ioControlCode = 0;
	ULONG inputLength = 0;
	ULONG outputLength = 0;
	PVOID ioBuffer = NULL;
	ULONG byteReturn = 0;

	DbgPrint("[W] DispatchDeviceControl\n");

	ioBuffer = irp->AssociatedIrp.SystemBuffer;


	irpStack = IoGetCurrentIrpStackLocation(irp);
	inputLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outputLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioControlCode)
	{
	case IOCTL_SEND_DATA: {
		st = HandleSendData(ioBuffer, inputLength);
		break;
	}
	case IOCTL_RECEIVE_DATA: {
		st = HandleReceiveData(ioBuffer, outputLength, &byteReturn);
		break;
	}
	default: {
		DbgPrint("[W][-] Invalid IoControlCode->0x%08X\n", ioControlCode);
		byteReturn = 0;
		break;
	}
	}

	irp->IoStatus.Status = st;
	irp->IoStatus.Information = byteReturn;
	
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return st;
}

NTSTATUS DispatchClose(PDEVICE_OBJECT deviceObject, PIRP irp) {
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] DispatchClose\n");
	irp->IoStatus.Status = st;
	irp->IoStatus.Information = 0;

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {
	IoDeleteSymbolicLink(&g_deviceContext.SymbolicName);
	DbgPrint("[W] IoDeleteSymbolicLink\n");		

	if (driverObject->DeviceObject != NULL) {
		IoDeleteDevice(driverObject->DeviceObject);
		DbgPrint("[Winter] IoDeleteDevice\n");
	}

	DbgPrint("[Winter] DriverUnload");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	driverObject->DriverUnload = DriverUnload;
	DbgPrint("[Winter] DriverEntry\n");
	NTSTATUS st = STATUS_SUCCESS;
	PDEVICE_OBJECT deviceObject = NULL;

	RtlZeroMemory(&g_deviceContext, sizeof(DeviceContext));
	RtlInitUnicodeString(&g_deviceContext.DeviceName, DEVICE_NAME);
	RtlInitUnicodeString(&g_deviceContext.SymbolicName, SYMBOLIC_LINK_NAME);
	
	st = IoCreateDevice(
		driverObject,
		0,
		&g_deviceContext.DeviceName,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&deviceObject
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[Winter][-] IoCreateDevice Failed->0x%08X\n", st);
		return st;
	}
	g_deviceContext.DeviceObject = deviceObject;
	DbgPrint("[W][+] Create Device Object Success\n");

	st = IoCreateSymbolicLink(&g_deviceContext.SymbolicName, &g_deviceContext.DeviceName);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[Winter][-] IoCreateSymbolicLink Failed->0x%08X\n", st);
		IoDeleteDevice(driverObject->DeviceObject);
	}
	DbgPrint("[W][+] Create Symbolic Link Success\n");

	driverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
	DbgPrint("[W][+] Create Dispatch Function Success\n");
	return st;
}