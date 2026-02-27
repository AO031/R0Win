#include <ntifs.h>
#include <ntstrsafe.h>

#define TEST_FILE L"\\??\\c:\\test.txt"
#define MEM_TAG 'ao31'
#define MESS_SIZE 4096
#define MODIFIED_OFFSET 100
#define MODIFIED_CONTENT "111111111111111111111111111111111111111111"

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS CreateTestFile(PCWSTR filePath) {
	NTSTATUS st = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING uniPath = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	PUCHAR buffer = NULL;
	HANDLE hFile = 0;

	if (!filePath) {
		st = STATUS_INVALID_PARAMETER;
		goto cleanup;
	}

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto cleanup;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN_IF,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] ZwCreateFile Failed->0x%lX\n", st);
		goto cleanup;
	}

	buffer = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, MESS_SIZE, MEM_TAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrint("[w] ExAllocatePoolWithTag Failed\n");
		goto cleanup;
	}

	for (int i = 0; i < MESS_SIZE; i++) {
		buffer[i] = 'a' + i % 26;
	}

	st = ZwWriteFile(
		hFile,
		NULL,
		NULL,
		NULL,
		&ioStatus,
		buffer,
		MESS_SIZE,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] ZwWrite Failed->0x%lX\n", st);
		goto cleanup;
	}

	DbgPrint("[W] CreateTestFile Success,File Name:%wZ\n", &uniPath);

cleanup:
	if (buffer) ExFreePoolWithTag(buffer, MEM_TAG);
	if (hFile) ZwClose(hFile);
	return st;
}

VOID TestTextFileMapping(VOID) {
	HANDLE hFile = 0;
	HANDLE hSection = 0;
	OBJECT_ATTRIBUTES fileObjAttr = { 0 };
	OBJECT_ATTRIBUTES sectionObjAttr = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	UNICODE_STRING uniFilePath = { 0 };
	LARGE_INTEGER sectionSize = { 0 };
	PVOID baseAddress = NULL;
	NTSTATUS st = STATUS_SUCCESS;
	SIZE_T viewSize = 0;

	st = RtlUnicodeStringInit(&uniFilePath, TEST_FILE);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto cleanup;
	}

	st = CreateTestFile(TEST_FILE);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] CreateTestFile Failed->0x%lX\n", st);
		goto cleanup;
	}

	InitializeObjectAttributes(&fileObjAttr, &uniFilePath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_READ,
		&fileObjAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] ZwCreateFile Failed->%lX\n", st);
		goto cleanup;
	}

	InitializeObjectAttributes(&sectionObjAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateSection(
		&hSection,
		SECTION_ALL_ACCESS,
		&sectionObjAttr,
		&sectionSize,
		PAGE_READWRITE,
		SEC_COMMIT,
		hFile
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] ZwCreateSection Failed->0x%lX\n", st);
		goto cleanup;
	}

	st = ZwMapViewOfSection(
		hSection,
		ZwCurrentProcess(),
		&baseAddress,
		0,
		0,
		NULL,
		&viewSize,
		ViewUnmap,
		0,
		PAGE_READWRITE
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[w] ZwMapViewOfSeciont Failed->0x%lX\n", st);
		goto cleanup;
	}

	__try {
		RtlCopyMemory((PUCHAR)baseAddress + MODIFIED_OFFSET, MODIFIED_CONTENT, strlen(MODIFIED_CONTENT));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DbgPrint("[E] Exception While Writing Memory->0x%lX\n",GetExceptionCode());
	}

	st = ZwFlushVirtualMemory(
		ZwCurrentProcess(),
		&baseAddress,
		&viewSize,
		&ioStatus
	);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwFlushVirtualMemory Failed->0x%lX\n", st);
		goto cleanup;
	}

cleanup:
	if (baseAddress) ZwUnmapViewOfSection(ZwCurrentProcess(), baseAddress); 
	if (hSection) ZwClose(hSection);
	if (hFile) ZwClose(hFile);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObject->DriverUnload = DriverUnload;

	TestTextFileMapping();

	return st;
}