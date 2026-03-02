#include <ntifs.h>
#include <ntstrsafe.h>

#define TEST_KEY_PATH L"\\Registry\\Machine\\SOFTWARE\\KernelRegTest"
#define TEST_VALUE_NAME L"TestValue"
#define TEST_SUB_KEY_NAME L"TestSubKey"
#define MEM_TAG 'ao31'

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS TestCreateKey() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hKey = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING keyPath = { 0 };
	ULONG disposition = 0;

	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	if (disposition == REG_CREATED_NEW_KEY) {
		DbgPrint("[W] Create A New Key\n");
	}
	else if (disposition == REG_OPENED_EXISTING_KEY){
		DbgPrint("[W] Opened A Existing Key\n");
	}

ret:
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestOpenKey() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hKey = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING keyPath = { 0 };
	
	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwOpenKey(&hKey, KEY_ALL_ACCESS, &objAttr);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwOpenKey Failed->0x%lX\n", st);
		goto ret;
	}

	DbgPrint("[W] ZwOpenKey Success->0x%p\n", hKey);
ret:
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestSetKeyValue() {
	HANDLE hKey = 0;
	UNICODE_STRING keyPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	DWORD keyValue = 0x12345678;
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING valueName = { 0 };
	ULONG disposition = 0;

	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	st = RtlUnicodeStringInit(&valueName, TEST_VALUE_NAME);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	st = ZwSetValueKey(
		hKey,
		&valueName,
		0,
		REG_DWORD,
		&keyValue,
		sizeof(DWORD)
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwSetValueKey Failed->0x%lX\n", st);
		goto ret;
	}
	
	DbgPrint("[W] SetKeyValue Success\n");
ret:
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestQueryKeyValue() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hKey = 0;
	UNICODE_STRING valueName = { 0 };
	UNICODE_STRING keyPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	PKEY_VALUE_PARTIAL_INFORMATION keyInfo = NULL;
	ULONG byteReturn = 0;
	
	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}


	st = RtlUnicodeStringInit(&valueName, TEST_VALUE_NAME);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	st = ZwQueryValueKey(
		hKey,
		&valueName,
		KeyValuePartialInformation,
		NULL,
		0,
		&byteReturn
	);

	if (st != STATUS_BUFFER_OVERFLOW && st != STATUS_BUFFER_TOO_SMALL) {
		DbgPrint("[E] ZwQueryValueKey Failed->0x%lX\n", st);
		goto ret;
	}

	keyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, byteReturn, MEM_TAG);
	if (!keyInfo) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrint("[E] ExAllocatePoolWithTag Failed->0x%lX\n", st);
		goto ret;
	}

	st = ZwQueryValueKey(
		hKey,
		&valueName,
		KeyValuePartialInformation,
		keyInfo,
		byteReturn,
		&byteReturn
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwQueryValueKey Failed->0x%lX\n", st);
		goto ret;
	}

	if (keyInfo->Type == REG_DWORD && keyInfo->DataLength == sizeof(DWORD)) {
		DWORD value = *(DWORD*)keyInfo->Data;
		DbgPrint("[W] ZwQueryValueKey Success,keyValue->0x%lX\n", value);
	}
ret:
	if (keyInfo) ExFreePoolWithTag(keyInfo, MEM_TAG);
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestEnumSubKey() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hKey = 0;
	HANDLE hSubKey = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING keyPath = { 0 };
	WCHAR fullKeyPath[260] = { 0 };
	ULONG disposition = 0;
	PKEY_BASIC_INFORMATION subKeyInfo = NULL;
	ULONG index = 0;
	ULONG byteReturn = 0;
	WCHAR subKeyName[512] = { 0 };
	ULONG copyLength = 0;

	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	st = RtlStringCbPrintfW(fullKeyPath, sizeof(fullKeyPath), L"%ws\\%ws", TEST_KEY_PATH, TEST_SUB_KEY_NAME);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlStringCbPrintfW Failed->0x%lX\n", st);
		goto ret;
	}

	st = RtlUnicodeStringInit(&keyPath, fullKeyPath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hSubKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	DbgPrint("[W] Create Sub Key Success\n");

	subKeyInfo = (PKEY_BASIC_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, 1024, MEM_TAG);
	if (!subKeyInfo) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto ret;
	}

	RtlZeroMemory(subKeyInfo, 1024);

	while (TRUE) {
		st = ZwEnumerateKey(
			hKey,
			index,
			KeyBasicInformation,
			subKeyInfo,
			1024,
			&byteReturn
		);

		if (st == STATUS_NO_MORE_ENTRIES) {
			DbgPrint("[W] Enum Is Over,Total:%d\n",index);
			break;
		}

		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] ZwEnumerateKey Failed->0x%lX\n", st);
			goto ret;
		}
		
		copyLength = subKeyInfo->NameLength < sizeof(fullKeyPath) - sizeof(WCHAR) ? subKeyInfo->NameLength : sizeof(fullKeyPath) - sizeof(WCHAR);
		RtlCopyMemory(subKeyName, subKeyInfo->Name, copyLength);
		subKeyName[copyLength / sizeof(WCHAR)] = L'\0';
		DbgPrint("[W] %ws\n", subKeyName);

		index++;
	}


ret:
	if (subKeyInfo) ExFreePoolWithTag(subKeyInfo, MEM_TAG);
	if (hSubKey) ZwClose(hSubKey);
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestEnumKeyValue() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hKey = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING keyPath = { 0 };
	ULONG disposition = 0;
	PKEY_VALUE_BASIC_INFORMATION keyValueInfo = NULL;
	ULONG index = 0;
	ULONG byteReturn = 0;
	WCHAR keyValueName[512] = { 0 };
	ULONG copyLength = 0;

	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	keyValueInfo = (PKEY_VALUE_BASIC_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, 1024, MEM_TAG);
	if (!keyValueInfo) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto ret;
	}

	RtlZeroMemory(keyValueInfo, 1024);

	while (TRUE) {
		st = ZwEnumerateValueKey(
			hKey,
			index,
			KeyValueBasicInformation,
			keyValueInfo,
			1024,
			&byteReturn
		);

		if (st == STATUS_NO_MORE_ENTRIES) {
			DbgPrint("[W] Enum Is Over,Total:%d\n", index);
			break;
		}

		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] ZwEnumerateKey Failed->0x%lX\n", st);
			goto ret;
		}

		copyLength = keyValueInfo->NameLength < sizeof(keyValueName) - sizeof(WCHAR) ? keyValueInfo->NameLength : sizeof(keyValueName) - sizeof(WCHAR);
		RtlCopyMemory(keyValueName, keyValueInfo->Name, copyLength);
		keyValueName[copyLength / sizeof(WCHAR)] = L'\0';
		DbgPrint("[W] %ws\n", keyValueName);

		index++;
	}

ret:
	if (keyValueInfo) ExFreePoolWithTag(keyValueInfo, MEM_TAG);
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestDeleteKeyValue() {
	HANDLE hKey = 0;
	UNICODE_STRING keyPath = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING valueName = { 0 };
	ULONG disposition = 0;

	st = RtlUnicodeStringInit(&keyPath, TEST_KEY_PATH);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	st = RtlUnicodeStringInit(&valueName, TEST_VALUE_NAME);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	st = ZwDeleteValueKey(
		hKey,
		&valueName
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwDeleteValueKey Failed->0x%lX\n", st);
		goto ret;
	}

	DbgPrint("[W] ZwDeleteValueKey Success\n");
ret:
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS TestDeleteKey() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE hKey = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING keyPath = { 0 };
	WCHAR fullKeyPath[260] = { 0 };
	ULONG disposition = 0;

	st = RtlStringCbPrintfW(fullKeyPath, sizeof(fullKeyPath), L"%ws\\%ws", TEST_KEY_PATH, TEST_SUB_KEY_NAME);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlStringCbPrintfW Failed->0x%lX\n", st);
		goto ret;
	}

	st = RtlUnicodeStringInit(&keyPath, fullKeyPath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] RtlUnicodeStringInit Failed->0x%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwCreateKey(
		&hKey,
		KEY_ALL_ACCESS,
		&objAttr,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&disposition
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwCreateKey Failed->0x%lX\n", st);
		goto ret;
	}

	st = ZwDeleteKey(hKey);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ZwDeleteKey Failed->0x%lX\n", st);
		goto ret;
	}

	DbgPrint("[W] ZwDeleteKey Success\n");
ret:
	if (hKey) ZwClose(hKey);
	return st;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObject->DriverUnload = DriverUnload;

	TestCreateKey();
	// TestOpenKey();
	TestSetKeyValue();
	// TestQueryKeyValue();
	// TestEnumSubKey();
	// TestEnumKeyValue();
	TestDeleteKeyValue();
	TestDeleteKey();
	return st;
}