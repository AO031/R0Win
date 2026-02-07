#include <ntifs.h>
#include <ntstrsafe.h>

PUNICODE_STRING g_testString1 = NULL;
PUNICODE_STRING g_testString2 = NULL;
PUNICODE_STRING g_testString3 = NULL;

VOID SafeFreeGlobalUnicode(PUNICODE_STRING g_unicodeString);
NTSTATUS SafeInitGlobalUnicode(PUNICODE_STRING* targetString, CONST WCHAR* safeString);

VOID DriverUnload(PDRIVER_OBJECT driverObject) {
	/*
	if (g_testString3) {
		SafeFreeGlobalUnicode(g_testString3);
		g_testString3 = NULL;
	}
	*/
}

NTSTATUS StringTest(void) {
	UNICODE_STRING ustr1, ustr2, ustr3;
	NTSTATUS st = STATUS_SUCCESS;

	RtlInitUnicodeString(&ustr1,L"Hello, I Love Driver");
	DbgPrint("[W] Length:0x%04X, MaxLength:0x%04X, Buffer:%wZ\n",
		ustr1.Length, ustr1.MaximumLength, &ustr1);

	st = RtlUnicodeStringInit(&ustr2,L"Hello, I Love Kernel");
	DbgPrint("[W] Length:0x%04X, MaxLength:0x%04X, Buffer:%ws\n",
		ustr2.Length, ustr2.MaximumLength, ustr2.Buffer);

	WCHAR tempStr[64] = L"Winter";
	ustr3.Length = (USHORT)(wcsnlen_s(tempStr,UNICODE_STRING_MAX_CHARS) * 2);
	ustr3.MaximumLength = sizeof(tempStr);
	ustr3.Buffer = tempStr;
	DbgPrint("[W] Length:0x%04X, MaxLength:0x%04X, Buffer:%wZ\n",
		ustr3.Length, ustr3.MaximumLength, &ustr3);

	return st;
}

NTSTATUS SafeInitGlobalUnicode(PUNICODE_STRING* targetString, CONST WCHAR* safeString) {
	if (!targetString || !safeString) {
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS st = STATUS_SUCCESS;
	PUNICODE_STRING g_unicodeString = NULL;
	PWCHAR buffer = NULL;

	size_t uStringLength = wcsnlen_s(safeString, NTSTRSAFE_UNICODE_STRING_MAX_CCH);

	if (uStringLength >= NTSTRSAFE_UNICODE_STRING_MAX_CCH) {
		st = STATUS_INVALID_PARAMETER;
		return st;
	}

	g_unicodeString = (PUNICODE_STRING)ExAllocatePool(NonPagedPool,
		sizeof(UNICODE_STRING));

	if (!g_unicodeString) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		return st;
	}
	g_unicodeString->Length = (USHORT)(uStringLength*sizeof(WCHAR));
	g_unicodeString->MaximumLength = (USHORT)((g_unicodeString->Length+1 + 8 - 1) & ~(8 - 1));

	buffer = ExAllocatePool(PagedPool, g_unicodeString->MaximumLength);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		ExFreePool(g_unicodeString);
		return st;
	}
	wcscpy_s(buffer, g_unicodeString->MaximumLength / sizeof(WCHAR), safeString);

	g_unicodeString->Buffer = buffer;

	*targetString = g_unicodeString;

	return st;
}

VOID SafeFreeGlobalUnicode(PUNICODE_STRING g_unicodeString) {
	ExFreePool(g_unicodeString->Buffer);
	ExFreePool(g_unicodeString);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;
	driverObject->DriverUnload = DriverUnload;
	DbgBreakPoint();
	/*

	st = StringTest();
	if (!NT_SUCCESS(st)) return st;

	PWCHAR tooLong = NULL;
	st = SafeInitGlobalUnicode(&g_testString1, L"58 Unicode String And it's Init");
	if (!NT_SUCCESS(st) || !g_testString1) goto cleanup;
	DbgPrint("[W] Length:0x%04X(bytes), MaxLength:0x%04X(bytes), buffer:%wZ\n",
		g_testString1->Length, g_testString1->MaximumLength, g_testString1);
	
	tooLong = ExAllocatePool(PagedPool, 0x11000 * sizeof(WCHAR));
	if (!tooLong) goto cleanup;

	RtlFillMemory(tooLong, 0x11000 * sizeof(WCHAR), 0x31);
	st = SafeInitGlobalUnicode(&g_testString2, tooLong);
	if (!NT_SUCCESS(st) || !g_testString2) goto cleanup;
	DbgPrint("[W] Length:0x%04X(bytes), MaxLength:0x%04X(bytes), buffer:%wZ\n",
		g_testString2->Length, g_testString2->MaximumLength, g_testString2);
	
	st = SafeInitGlobalUnicode(&g_testString3, L"58 Kernel String And it's Init");
	if (!NT_SUCCESS(st) || !g_testString3) goto cleanup;
	DbgPrint("[W] Length:0x%04X(bytes), MaxLength:0x%04X(bytes), buffer:%wZ\n",
		g_testString3->Length, g_testString3->MaximumLength, g_testString3);

cleanup:
	if (tooLong) {
		ExFreePool(tooLong);
		tooLong = NULL;
	}

	if (g_testString1) {
		SafeFreeGlobalUnicode(g_testString1);
		g_testString1 = NULL;
	}

	if (g_testString2) {
		SafeFreeGlobalUnicode(g_testString2);
		g_testString2 = NULL;
	}
	UNICODE_STRING ustr1 = { 0 };
	UNICODE_STRING ustr2 = { 0 };
	WCHAR ustr1Buffer[20] = L"Helloaaaaa";
	
	ustr1.Length = (USHORT)(wcsnlen_s(ustr1Buffer,20) * sizeof(WCHAR));
	ustr1.MaximumLength = 20 * sizeof(WCHAR);
	ustr1.Buffer = ustr1Buffer;

	RtlInitUnicodeString(&ustr2, L"World");

	DbgBreakPoint();
	// This Function Will Add L'\0' In The End,So The Length Will Be Source 
	RtlCopyUnicodeString(&ustr1, &ustr2);;
	if (ustr1.Length != ustr2.Length) {
		DbgPrint("[W] ustr1:%wZ\n", &ustr1);
	}

	UNICODE_STRING ustr1 = { 0 };
	UNICODE_STRING ustr2 = { 0 };
	WCHAR ustr1Buffer[20] = L"Helloaaaaa";

	ustr1.Length = (USHORT)(wcsnlen_s(ustr1Buffer, 20) * sizeof(WCHAR));
	ustr1.MaximumLength = 20 * sizeof(WCHAR);
	ustr1.Buffer = ustr1Buffer;

	RtlInitUnicodeString(&ustr2, L"World");

	DbgBreakPoint();
	RtlAppendUnicodeStringToString(&ustr1, &ustr2);
	if (ustr1.Length != ustr2.Length) {
		DbgPrint("[W] ustr1:%wZ\n", &ustr1);
	}

	BOOLEAN res = FALSE;
	UNICODE_STRING str1 = { 0 };
	UNICODE_STRING str2 = { 0 };
	UNICODE_STRING str3 = { 0 };

	RtlInitUnicodeString(&str1, L"TestString");
	RtlInitUnicodeString(&str2, L"TestString");
	RtlInitUnicodeString(&str3, L"testString");

	res = RtlEqualUnicodeString(&str1, &str2, FALSE);
	res = RtlEqualUnicodeString(&str1, &str3, FALSE);
	res = RtlEqualUnicodeString(&str1, &str3, TRUE);
	

	res = RtlCompareUnicodeString(&str1, &str2, FALSE);


	ANSI_STRING ansiStr = { 0 };
	UNICODE_STRING unicodeStr = { 0 };
	UNICODE_STRING unicodeStr1 = { 0 };
	
	PVOID unicode1Buffer = NULL;
	unicode1Buffer = ExAllocatePool(NonPagedPool, 0x20);
	if (!unicode1Buffer) goto cleanup;
	
	unicodeStr1.Length = 0;
	unicodeStr1.MaximumLength = 0x20;
	unicodeStr1.Buffer = unicode1Buffer;

	RtlInitAnsiString(&ansiStr, "Hello World");

	st = RtlAnsiStringToUnicodeString(&unicodeStr, &ansiStr, TRUE);
	if (!NT_SUCCESS(st) || unicodeStr.Length != ansiStr.Length * sizeof(WCHAR)) {
		DbgPrint("[W] RtlAnsiStringToUnicodeString Failed\n");
		goto cleanup;
	}

	st = RtlAnsiStringToUnicodeString(&unicodeStr1, &ansiStr, FALSE);
	if (!NT_SUCCESS(st) || unicodeStr.Length != ansiStr.Length * sizeof(WCHAR)) {
		DbgPrint("[W] RtlAnsiStringToUnicodeString Failed\n");
		goto cleanup;
	}

cleanup:
	if (unicodeStr.Length) RtlFreeUnicodeString(&unicodeStr);
	if (unicode1Buffer) ExFreePool(unicode1Buffer);

	WCHAR destBuffer[8] = { 0 };
	st = RtlStringCchCopyW(destBuffer,8,L"Too Long");
	if (st == STATUS_BUFFER_OVERFLOW) DbgPrint("[W] STATUS_BUFFER_OVERFLOW\n");
	if (destBuffer[RTL_NUMBER_OF(destBuffer) - 1] != L'\0') DbgPrint("[W] RtlStringCchCopyW Failed\n");
		
	WCHAR destBuffer[8] = { 0 };
	st = RtlStringCchCatW(destBuffer, 8, L"Hello");
	if (st == STATUS_BUFFER_OVERFLOW) DbgPrint("[W] STATUS_BUFFER_OVERFLOW\n");

	WCHAR snBuffer[8] = { 0 };
	st = RtlStringCchPrintfW(snBuffer, 8, L"1+1=%d", 2);
	if (st == STATUS_BUFFER_OVERFLOW) DbgPrint("[W] STATUS_BUFFER_OVERFLOW\n");


	UNICODE_STRING ustr1, ustr2;

	RtlInitUnicodeString(&ustr1, L"Hello");
	RtlInitUnicodeString(&ustr2, L"World");
	DbgBreakPoint();
	RtlCopyUnicodeString(&ustr1, &ustr2);
	*/

	return STATUS_SUCCESS;
}