#include <ntifs.h>

VOID DriverUnload(PDRIVER_OBJECT driverObject) {
	DbgPrint("[W] DriverUnload\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	driverObject->DriverUnload = DriverUnload;
	DbgPrint("[W] DriverEntry\n");
	return STATUS_SUCCESS; 
}