#include <ntifs.h>
#include <ntstrsafe.h>

#define BASE_FILE_DIR L"\\??\\c:\\"
#define MEM_TAG 'AODF'

NTSTATUS GetFileSize(HANDLE hFile, PLARGE_INTEGER fileSize) {
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };
	FILE_STANDARD_INFORMATION fileInfo = { 0 };

	if (!hFile || !fileSize) {
		st = STATUS_INVALID_PARAMETER;
		goto Ret;
	}

	st = ZwQueryInformationFile(
		hFile,
		&ioStatus,
		&fileInfo,
		sizeof(FILE_STANDARD_INFORMATION),
		FileStandardInformation
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwQueryInformationFile Failed->0x%08X\n", st);
		goto Ret;
	}

	fileSize->QuadPart = fileInfo.EndOfFile.QuadPart;

Ret:
	return st;
}

NTSTATUS CreateNewFile(PWCHAR filePath) {
	HANDLE hFile = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING uniPath = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	RtlInitUnicodeString(&uniPath, filePath);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_CREATE,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_ALERT,
		NULL,
		0);

	if (NT_SUCCESS(st)) {
		DbgPrint("[W] Create New File Success\n");
		DbgPrint("[W] Information:%d\n", ioStatus.Information);
		ZwClose(hFile);
	}

	return st;
}

BOOLEAN TestCreateNewFile(void) {
	WCHAR filePath[260] = { 0 };
	NTSTATUS st = { 0 };

	st = RtlStringCbPrintfW(filePath, sizeof(filePath), L"%wsconfig_new.txt", BASE_FILE_DIR);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbPrintfW Failed->0x%08X\n", st);
		return FALSE;
	}

	st = CreateNewFile(filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] CreateNewFile Failed->0x%08X\n", st);
		return FALSE;
	}
	DbgPrint("[W] CreateNewFile Success\n");
	return TRUE;
}

BOOLEAN OpenFileAppendData(PWCHAR filePath, PHANDLE hFile) {
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		hFile = 0;
		return FALSE;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(hFile,
		FILE_APPEND_DATA,
		&objAttr,
		&ioStatus,
		0,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_WRITE,
		FILE_OPEN_IF,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_ALERT,
		NULL,
		0);
	if (NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Success: ");
		if (ioStatus.Information == FILE_CREATED) {
			DbgPrint("Create New File\n");
		}
		else if (ioStatus.Information == FILE_OPENED) {
			DbgPrint("Open A Existing File\n");
		}
		return TRUE;
	}
	else {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		return FALSE;
	}
}

NTSTATUS AppendData(HANDLE hFile, CONST CHAR* fileData, ULONG fileDataLength) {
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };
	
	if (!hFile || !fileData || !fileDataLength) {
		st = STATUS_INVALID_PARAMETER;
		return st;
	}

	st = ZwWriteFile(
		hFile,
		NULL,
		NULL,
		NULL,
		&ioStatus,
		fileData,
		fileDataLength,
		0,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwWriteFile Failed->0x%08X\n", st);
		return st;
	}

	DbgPrint("[W] ZwWriteFile Success\n");
	return st;
}

BOOLEAN TestFileAppendData(void) {
	WCHAR filePath[260] = { 0 };
	HANDLE hFile = 0;
	CHAR fileData[256] = { 0 };
	size_t fileDataLength = 0;
	BOOLEAN res = FALSE;
	NTSTATUS st = STATUS_SUCCESS;
	
	RtlStringCbPrintfW(filePath, 260, L"%wsdriverLog.txt", BASE_FILE_DIR);
	
	res = OpenFileAppendData(filePath, &hFile);
	if (!res || !hFile) {
		DbgPrint("[W] OpenFileAppendData Failed\n");
		return res;
	}

	st = RtlStringCbPrintfA(fileData, 256, "[+] test append data\n");
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbPrintfA Failed->0x%08X\n", st);
		return FALSE;
	}

	st = RtlStringCbLengthA(fileData, 256, &fileDataLength);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbLengthA Failed->0x%08X\n", st);
		return FALSE;
	}

	st = AppendData(hFile, fileData, fileDataLength);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] AppendData Failed->0x%08X\n", st);
		return FALSE;
	}

	ZwClose(hFile);
	return TRUE;
}

NTSTATUS CreateTempFile(PWCHAR filePath, PHANDLE hFile) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };

	if (!filePath) {
		st = STATUS_INVALID_PARAMETER;
		goto Return;
	}

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->0x%08X\n", st);
		goto Return;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		hFile,
		GENERIC_READ | GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		0,
		FILE_ATTRIBUTE_TEMPORARY,
		FILE_SHARE_DELETE,
		FILE_CREATE,
		FILE_DELETE_ON_CLOSE | FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_ALERT,
		NULL,
		0
	);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		goto Return;
	} 

Return:
	return st;
}

BOOLEAN TestCreateTempFile(void) {
	NTSTATUS st = STATUS_SUCCESS;
	WCHAR filePath[260] = { 0 };
	HANDLE hFile = 0;

	st = RtlStringCbPrintfW(filePath, 260, L"%wstemp_cache.txt", BASE_FILE_DIR);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbPrintfW Failed->0x%08X\n", st);
		return FALSE;
	}

	st = CreateTempFile(filePath, &hFile);
	if (!NT_SUCCESS(st) || !hFile) {
		DbgPrint("[W] CreateTempFile Failed->0x%08X\n", st);
		return FALSE;
	}

	DbgPrint("[W] CreateTempFile Success\n");
	
	ZwClose(hFile);
	
	return TRUE;
}

NTSTATUS CreateTestFile(PWCHAR filePath, ULONG fileSize) {
	NTSTATUS st = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING uniPath = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	PVOID buffer = NULL;
	HANDLE hFile = 0;

	if (!filePath || !fileSize) {
		st = STATUS_INVALID_PARAMETER;
		goto Ret;
	}

	RtlUnicodeStringInit(&uniPath, filePath);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_OVERWRITE_IF,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		goto Ret;
	}

	buffer = ExAllocatePoolWithTag(NonPagedPool, fileSize, MEM_TAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrint("[W] ExAllocatePoolWithTag Failed\n");
		goto Ret;
	}

	for (size_t i = 0; i < fileSize; i++) {
		((PCHAR)buffer)[i] = 'a' + i % 26;
	}

	st = ZwWriteFile(
		hFile,
		NULL,
		NULL,
		NULL,
		&ioStatus,
		buffer,
		fileSize,
		0,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwWriteFile Failed->0x%08X\n", st);
		goto Ret;
	}

	DbgPrint("[W] CreateTestFile Success,File Name:%wZ\n", &uniPath);

Ret:
	if (buffer) ExFreePoolWithTag(buffer, MEM_TAG);
	if (hFile) ZwClose(hFile);

	return st;
}

NTSTATUS SyncRead(CONST WCHAR* filePath, PULONG byteReturn) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	LARGE_INTEGER fileSize = {0};
	PVOID buffer = NULL;
	HANDLE hFile = 0;

	if (!filePath || !byteReturn) {
		st = STATUS_INVALID_PARAMETER;
		goto Ret;
	}

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->0x%08X\n", st);
		goto Ret;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	DbgBreakPoint();
	st = ZwCreateFile(
		&hFile,
		GENERIC_READ | SYNCHRONIZE,
		&objAttr,
		&ioStatus,
		0,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		goto Ret;
	}

	st = GetFileSize(hFile, &fileSize);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] GetFileSize Failed->0x%08X\n", st);
		goto Ret;
	}

	buffer = ExAllocatePoolWithTag(NonPagedPool, (size_t)fileSize.LowPart, MEM_TAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto Ret;
	}

	st = ZwReadFile(
		hFile,
		NULL,
		NULL,
		NULL,
		&ioStatus,
		buffer,
		fileSize.LowPart,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(st) || ioStatus.Information > fileSize.LowPart) {
		DbgPrint("[W] ZwReadFile Failed->0x%08X\n", st);
		goto Ret;
	}

	*byteReturn = ioStatus.Information;

Ret:
	if (buffer) ExFreePoolWithTag(buffer, MEM_TAG);
	if (hFile) ZwClose(hFile);
	return st;
}

BOOLEAN TestSyncRead(void) {
	WCHAR filePath[260] = { 0 };
	NTSTATUS st = STATUS_SUCCESS;
	ULONG byteReturn = 0;

	st = RtlStringCbPrintfW(filePath, 260, L"%wssmall.txt", BASE_FILE_DIR);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbPrintfW Failed->0x%08X\n", st);
		return FALSE;
	}

	st = SyncRead(filePath,&byteReturn);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] SyncRead Failed->0x%08X\n", st);
		return FALSE;
	}

	DbgPrint("[W] TestAsyncRead Success\n");
	return TRUE;
}

NTSTATUS AsyncRead(CONST WCHAR* filePath, PULONG byteReturn) {
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE hFile = 0;
	LARGE_INTEGER fileSize = { 0 };
	PVOID buffer = NULL;
	LARGE_INTEGER byteOffset = { 0 };

	if (!filePath || !byteReturn) {
		st = STATUS_INVALID_PARAMETER;
		goto Ret;
	}

	*byteReturn = 0;

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->0x%08X\n", st);
		goto Ret;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_READ,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE,
		NULL,
		0
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		goto Ret;
	}

	st = GetFileSize(hFile, &fileSize);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] GetFileSize Failed->0x%08X\n", st);
		goto Ret;
	}

	buffer = ExAllocatePoolWithTag(NonPagedPool, fileSize.LowPart, MEM_TAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto Ret;
	}

	st = ZwReadFile(
		hFile,
		NULL,
		NULL,
		NULL,
		&ioStatus,
		buffer,
		fileSize.LowPart,
		&byteOffset,
		NULL
	);
	if (st == STATUS_PENDING) {
		st = ZwWaitForSingleObject(hFile, FALSE, NULL);
		if (NT_SUCCESS(st)) {
			DbgPrint("[W] Wait For Read File\n");
		}
	}
	else if (NT_SUCCESS(st)) {
		DbgPrint("[W] No Wait For Read File\n");
	}
	else {
		DbgPrint("[W] ZwReadFile Failed->0x%08X\n", st);
		goto Ret;
	}

	*byteReturn = ioStatus.Information;

Ret:
	if (hFile) ZwClose(hFile);
	if (buffer) ExFreePoolWithTag(buffer, MEM_TAG);

	return st;
}

BOOLEAN TestAsyncRead(void) {
	WCHAR filePath[260] = { 0 };
	NTSTATUS st = STATUS_SUCCESS;
	ULONG byteReturn = 0;

	st = RtlStringCbPrintfW(filePath, 260, L"%wssmall.txt", BASE_FILE_DIR);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbPrintfW Failed->0x%08X\n", st);
		return FALSE;
	}

	st = AsyncRead(filePath, &byteReturn);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] AsyncRead Failed->0x%08X\n", st);
		return FALSE;
	}
	DbgPrint("[W] TestAsyncRead Success\n");
	return TRUE;
}

NTSTATUS ChunkRead(CONST WCHAR* filePath, PULONG byteReturn) {
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	LARGE_INTEGER byteOffset = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hFile = 0;
	LARGE_INTEGER fileSize = { 0 };
	ULONG chunkSize = 26;
	PVOID buffer = NULL;
	

	if (!filePath || !byteReturn) {
		st = STATUS_INVALID_PARAMETER;
		goto Ret;
	}

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->0x%08X\n", st);
		goto Ret;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_READ,
		&objAttr,
		&ioStatus,
		0,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY,
		NULL,
		0
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		goto Ret;
	}


	st = GetFileSize(hFile, &fileSize);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] GetFileSize Failed->0x%08X\n", st);
		goto Ret;
	}

	buffer = ExAllocatePoolWithTag(NonPagedPool, fileSize.QuadPart, MEM_TAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto Ret;
	}
	RtlZeroMemory(buffer, fileSize.QuadPart);

	DbgBreakPoint();
	while (byteOffset.QuadPart < fileSize.QuadPart) {
		st = ZwReadFile(
			hFile,
			NULL,
			NULL,
			NULL,
			&ioStatus,
			(PCHAR)buffer + byteOffset.QuadPart,
			chunkSize,
			&byteOffset,
			NULL
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[W] ZwReadFile Failed->0x%08X\n", st);
			goto Ret;
		}

		byteOffset.QuadPart += min(chunkSize,fileSize.QuadPart - byteOffset.QuadPart);
	}

	*byteReturn = byteOffset.LowPart;

Ret:
	if (hFile) ZwClose(hFile);
	if (buffer) ExFreePoolWithTag(buffer, MEM_TAG);

	return st;
}

BOOLEAN TestChunkRead(void) {
	WCHAR filePath[260] = { 0 };
	ULONG byteReturn = 0;
	NTSTATUS st = STATUS_SUCCESS;

	st = RtlStringCbPrintfW(filePath, 260, L"%wslarge.txt", BASE_FILE_DIR);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlStringCbPrintfW Failed->0x%08X\n", st);
		return FALSE;
	}

	st = ChunkRead(filePath, &byteReturn);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ChunkRead Failed->0x%08X\n", st);
		return FALSE;
	}

	DbgPrint("[W] TestChunkRead Success\n");
	return TRUE;
}

VOID PrepareTestFile(void) {
	WCHAR smallFilePath[260] = { 0 };
	WCHAR largeFilePath[260] = { 0 };
	ULONG smallSize = 0x100;
	ULONG largeSize = 0x1000;
	NTSTATUS st = STATUS_SUCCESS;
	
	RtlStringCbPrintfW(smallFilePath, 260, L"%wssmall.txt", BASE_FILE_DIR);
	st = CreateTestFile(smallFilePath, smallSize);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] CreateTestFile Failed->0x%08X\n", st);
		return;
	}

	RtlStringCbPrintfW(largeFilePath, 260, L"%wslarge.txt", BASE_FILE_DIR);
	st = CreateTestFile(largeFilePath, largeSize);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] CreateTestFile Failed->0x%08X\n", st);
		return;
	}
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS QueryBasicInfo(CONST WCHAR* filePath) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	FILE_BASIC_INFORMATION basicInformation = { 0 };
	HANDLE hFile = 0;

	if (!filePath) {
		st = STATUS_INVALID_PARAMETER;
		goto Ret;
	}

	st = RtlUnicodeStringInit(&uniPath, filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->0x%08X\n", st);
		goto Ret;
	}

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_READ,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPENED,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateFile Failed->0x%08X\n", st);
		goto Ret;
	}

	st = ZwQueryInformationFile(
		hFile,
		&ioStatus,
		&basicInformation,
		sizeof(FILE_BASIC_INFORMATION),
		FileBasicInformation
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwQueryInformationFile Failed->0x%08X\n", st);
		goto Ret;
	}

	TIME_FIELDS timeField = { 0 };
	RtlTimeToTimeFields(&basicInformation.CreationTime, &timeField);
	DbgPrint("[W] File CreationTime: %04d:%02d:%02d\n",
		timeField.Hour,timeField.Minute,timeField.Second);

Ret:
	if (hFile) ZwClose(hFile);
	return st;
}

BOOLEAN TestQueryBasicInfo(void) {
	WCHAR filePath[260] = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	st = RtlStringCbPrintfW(filePath, 260, L"%wssmall.txt", BASE_FILE_DIR);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[+] RtlStringCbPrintfW Failed->0x%08X\n", st);
		return FALSE;
	}

	st = QueryBasicInfo(filePath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] QueryBasicInfo Failed->0x%08X\n", st);
		return FALSE;
	}

	DbgPrint("[W] TestQueryBasicInfo Success\n");
	return TRUE;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;
	BOOLEAN res = FALSE;
	driverObject->DriverUnload = DriverUnload;
	// res = TestCreateNewFile();
	// res = TestFileAppendData();
	// res = TestCreateTempFile();
	PrepareTestFile();
	// res = TestSyncRead();
	// res = TestAsyncRead();
	// res = TestChunkRead();
	res = TestQueryBasicInfo();
	if (!res) {
		DbgPrint("[W] Test*File* Failed\n");
	}

	return st;
}