#include <ntifs.h>

VOID DriverUnload(PDRIVER_OBJECT driverObject) {
	DbgPrint("[Winter] DriverUnload\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	driverObject->DriverUnload = DriverUnload;
	DbgPrint("[Winter] DriverEntry\n");
	return STATUS_SUCCESS; 
}