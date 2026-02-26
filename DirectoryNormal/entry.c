#include <ntifs.h>
#include <ntstrsafe.h>

#define TEST_DIR L"\\??\\c:\\test"
#define TTAG 'ao31'
#define BUFFER_SIZE_64KB 64*1024
#define NT_DEAL(status, functionName) \
    do { \
        if (!NT_SUCCESS(status)) { \
            DbgPrint("[W] " #functionName " Failed->0x%08X\n", status); \
            goto ret; \
        } \
    } while (0)

typedef struct _EnumResult {
	ULONG TotalFile;
	ULONG TotalDir;
	ULONG MaxDepth;
	LARGE_INTEGER TotalSize;
}EnumResult,*PEnumResult;

NTSTATUS CreateSubDir(WCHAR* dirPath) {
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE hDir = 0;

	if (!dirPath) {
		st = STATUS_INVALID_PARAMETER;
		goto ret;
	}

	st = RtlUnicodeStringInit(&uniPath, dirPath);
	NT_DEAL(st, RtlUnicodeStringInit);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hDir,
		GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE,
		NULL,
		0
	);
	NT_DEAL(st, ZwCreateFile);

ret:
	if (hDir) ZwClose(hDir);
	return st;
}

NTSTATUS CreateTestFile(WCHAR* filePath) {
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE hFile = 0;

	if (!filePath) {
		st = STATUS_INVALID_PARAMETER;
		goto ret;
	}

	st = RtlUnicodeStringInit(&uniPath, filePath);
	NT_DEAL(st, RtlUnicodeStringInit);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hFile,
		GENERIC_READ | GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	NT_DEAL(st, ZwCreateFile);

ret:
	if (hFile) ZwClose(hFile);
	return st;
}

NTSTATUS DirCreateTestFile(int fileCount, WCHAR* dirPath) {
	NTSTATUS st = STATUS_SUCCESS;
	if (!fileCount) {
		st = STATUS_INVALID_PARAMETER;
		return st;
	}

	for (int i = 0; i < fileCount; i++) {
		WCHAR filePath[260] = { 0 };
		st = RtlStringCbPrintfW(filePath, 260, L"%ws\\%ws%02d.txt", dirPath, L"subFile", i);
		NT_DEAL(st, RtlStringCbPrintfW);
		st = CreateTestFile(filePath);
		NT_DEAL(st, CreateTestFile);
	}

ret:
	return st;
}

NTSTATUS PrepareTestEnv(void) {
	NTSTATUS st = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatus = { 0 };
	UNICODE_STRING uniPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	WCHAR subDirPath[260] = { 0 };
	HANDLE hDir = 0;

	st = RtlUnicodeStringInit(&uniPath, TEST_DIR);
	NT_DEAL(st, RtlUnicodeStringInit);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hDir,
		GENERIC_WRITE,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE,
		NULL,
		0
	);
	NT_DEAL(st, ZwCreateFile);

	ZwClose(hDir);
	hDir = 0;
	for (int i = 0; i < 3; i++) {
		RtlStringCbPrintfW(subDirPath, 260, L"%ws\\subDir%02d", TEST_DIR, i);
		st = CreateSubDir(subDirPath);
		NT_DEAL(st, CreateSubDir);
		st = DirCreateTestFile(i + 2, subDirPath);
		NT_DEAL(st, DirCreateTestFile);
	}
ret:
	if (hDir) ZwClose(hDir);

	return st;
}

BOOLEAN IsDotDir(PUNICODE_STRING fileName) {
	return (fileName->Length == 1 * sizeof(WCHAR)) && (fileName->Buffer[0] == L'.');
}

BOOLEAN IsDotDotDir(PUNICODE_STRING fileName) {
	return (fileName->Length == 2 * sizeof(WCHAR)) && (fileName->Buffer[0] == L'.') && (fileName->Buffer[1] == L'.');
}

NTSTATUS PrintPatternFile(PCWSTR dirPath, PCWSTR pattern, PEnumResult eRes) {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hDir = 0;
	PVOID buffer = NULL;
	PFILE_DIRECTORY_INFORMATION fileInfo = NULL;
	UNICODE_STRING uniPath = { 0 };
	UNICODE_STRING uniPattern = { 0 };
	IO_STATUS_BLOCK ioStatus = { 0 };
	BOOLEAN firstQuery = TRUE;
	OBJECT_ATTRIBUTES objAttr = { 0 };

	if (!dirPath || !eRes || !pattern) {
		st = STATUS_INVALID_PARAMETER;
		goto ret;
	}

	st = RtlUnicodeStringInit(&uniPath, dirPath);
	NT_DEAL(st, RtlUnicodeStringInit);

	st = RtlUnicodeStringInit(&uniPattern, pattern);
	NT_DEAL(st, RtlUnicodeStringInit);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateFile(
		&hDir,
		FILE_LIST_DIRECTORY,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN,
		FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	NT_DEAL(st, ZwCreateFile);

	buffer = ExAllocatePoolWithTag(NonPagedPool, BUFFER_SIZE_64KB, TTAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto ret;
	}

	while (TRUE) {
		st = ZwQueryDirectoryFile(
			hDir,
			NULL,
			NULL,
			NULL,
			&ioStatus,
			buffer,
			BUFFER_SIZE_64KB,
			FileDirectoryInformation,
			FALSE,
			&uniPattern,
			firstQuery
		);
		
		if (st == STATUS_NO_MORE_FILES) {
			st = STATUS_SUCCESS;
			break;
		}

		if (!NT_SUCCESS(st)) break;

		firstQuery = FALSE;

		fileInfo = (PFILE_DIRECTORY_INFORMATION)buffer;

		while (TRUE) {
			eRes->TotalFile++;
			eRes->TotalSize.QuadPart += fileInfo->EndOfFile.QuadPart;
			DbgPrint("[FILE] %ws (%llX bytes)\n", fileInfo->FileName, fileInfo->EndOfFile.QuadPart);

			if (fileInfo->NextEntryOffset == 0) break;

			fileInfo = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)fileInfo + fileInfo->NextEntryOffset);
		}
	}

ret:
	if (buffer) ExFreePoolWithTag(buffer, TTAG);
	if (hDir) ZwClose(hDir);

	return st;
}

NTSTATUS RecursiveEnum(PCWSTR dirPath, ULONG depth, PEnumResult enumRes) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING uniPath = { 0 };
	HANDLE hDir = 0;
	IO_STATUS_BLOCK ioStatus = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	PVOID buffer = NULL;
	WCHAR subPath[260] = { 0 };
	UNICODE_STRING fileName = { 0 };
	PFILE_DIRECTORY_INFORMATION dirInfo = NULL;
	BOOLEAN firstQuery = TRUE;

	if (!dirPath || !enumRes) {
		st = STATUS_INVALID_PARAMETER;
		goto ret;
	}

	if (depth > 32) return STATUS_SUCCESS;

	if (enumRes->MaxDepth < depth) enumRes->MaxDepth = depth;

	st = PrintPatternFile(dirPath, L"*.txt", enumRes);
	if (st == STATUS_NO_SUCH_FILE) st = STATUS_SUCCESS;

	st = RtlUnicodeStringInit(&uniPath, dirPath);
	NT_DEAL(st, RtlUnicodeStringInit);

	InitializeObjectAttributes(&objAttr, &uniPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
	
	st = ZwCreateFile(
		&hDir,
		FILE_LIST_DIRECTORY,
		&objAttr,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN,
		FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0);
	if (!NT_SUCCESS(st) || !hDir) return STATUS_SUCCESS;

	buffer = ExAllocatePoolWithTag(NonPagedPool, BUFFER_SIZE_64KB, TTAG);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrint("[-] ExAllocatePoolWithTag Failed->0x%lX\n", st);
		goto ret;
	}
	
	while (TRUE) {
		st = ZwQueryDirectoryFile(
			hDir,
			NULL,
			NULL,
			NULL,
			&ioStatus,
			buffer,
			BUFFER_SIZE_64KB,
			FileDirectoryInformation,
			FALSE,
			NULL,
			firstQuery
		);
		if (st == STATUS_NO_MORE_FILES) {
			st = STATUS_SUCCESS;
			break;
		}

		if (!NT_SUCCESS(st)) break;

		firstQuery = FALSE;

		dirInfo = (PFILE_DIRECTORY_INFORMATION)buffer;

		while (TRUE) {
			fileName.Length = (USHORT)dirInfo->FileNameLength;
			fileName.MaximumLength = (USHORT)dirInfo->FileNameLength;
			fileName.Buffer = dirInfo->FileName;

			if (!IsDotDir(&fileName) && !IsDotDotDir(&fileName) && dirInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				for (size_t i = 0; i < depth; i++) DbgPrint("\t");
				DbgPrint("[DIR] %wZ\n", &fileName);
				enumRes->TotalDir += 1;
				if (!(dirInfo->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
					st = RtlStringCbPrintfW(subPath, 260, L"%ws\\%wZ", dirPath, &fileName);
					NT_DEAL(st, RtlStringCbPrintfW);
					RecursiveEnum(subPath, depth + 1, enumRes);
				}
			}

			if (dirInfo->NextEntryOffset == 0) break;

			dirInfo = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)dirInfo + dirInfo->NextEntryOffset);
		}
	}
	
ret:
	if (buffer) ExFreePoolWithTag(buffer, TTAG);
	if (hDir) ZwClose(hDir);
	return st;
}

BOOLEAN TestRecursiveEnum(void) {
	EnumResult eRes = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	st = RecursiveEnum(TEST_DIR, 0, &eRes);
	if (NT_SUCCESS(st)) {
		DbgPrint("[+] TeatDir:%ws\nTotal File:%d\nTotal Dir:%d\nMax Depth:%d\nTotal Size:0x%llX\n",
			TEST_DIR, eRes.TotalFile, eRes.TotalDir, eRes.MaxDepth, eRes.TotalSize.QuadPart);
		return TRUE;
	}
	else {
		DbgPrint("[-] RecursiveEnum Failed->0x%08X\n", st);
		return FALSE;
	}
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;
	BOOLEAN res = TRUE;
	driverObject->DriverUnload = DriverUnload;
	st = PrepareTestEnv();

	res = TestRecursiveEnum();
	if (!res) DbgPrint("[-] TestRecursiveEnum Failed\n");
	return st;
}