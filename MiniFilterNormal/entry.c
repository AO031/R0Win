#include <ntifs.h>
#include <ntstrsafe.h>
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>


VOID DriverUnload(PDRIVER_OBJECT driverObj) {

}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObj->DriverUnload = DriverUnload;

	return st;
}