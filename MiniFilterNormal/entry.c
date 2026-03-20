#include <ntifs.h>
#include <ntstrsafe.h>
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

// ==============================================================================
// ==============================================================================
typedef struct _STATISTICS_INFO {
    ULONGLONG createCount;
    ULONGLONG readCount;
    ULONGLONG writeCount;
    ULONGLONG setInfoCount;
    ULONGLONG cleanupCount;
    ULONGLONG filteredCount;
    ULONGLONG totalCount;
    ULONGLONG protectedCount;
    LARGE_INTEGER startTime;
}STATISTICS_INFO,*PSTATISTICS_INFO;


// ==============================================================================


// ==============================================================================
NTSTATUS InitRegConfig(PUNICODE_STRING regPath);
NTSTATUS ExtractServiceName(PUNICODE_STRING regPath, WCHAR* serviceName, ULONG bufferSize);
NTSTATUS SetupRegConfig(PUNICODE_STRING regPath, WCHAR* serviceName);

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath);
VOID DriverUnload(PDRIVER_OBJECT driverObj);


// ==============================================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	st = InitRegConfig(regPath);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] InitRegConfig Failed->%lX\n", st);
        return st;
    }

	driverObj->DriverUnload = DriverUnload;

	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObj) {

}

NTSTATUS InitRegConfig(PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;
	WCHAR serviceName[260] = { 0 };

	st = ExtractServiceName(regPath, serviceName, 260);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ExtractServiceName Failed->%lX\n", st);
		return st;
	}

    st = SetupRegConfig(regPath, serviceName);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ExtractServiceName Failed->%lX\n", st);
        return st;
    }


    return st;
}

NTSTATUS ExtractServiceName(PUNICODE_STRING regPath, WCHAR* serviceName, ULONG bufferSize) {
    PWCHAR p, lastBackslash = NULL;
    ULONG nameLength;

    // 参数校验
    if (!regPath || !serviceName || bufferSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // 输入 UNICODE_STRING 必须有效且非空
    if (regPath->Length == 0 || regPath->Buffer == NULL) {
        return STATUS_UNSUCCESSFUL;
    }

    // 在指定长度内手动查找最后一个反斜杠
    p = regPath->Buffer;
    for (ULONG i = 0; i < regPath->Length / sizeof(WCHAR); i++) {
        if (p[i] == L'\\') {
            lastBackslash = &p[i];
        }
    }

    // 如果找不到反斜杠，则整个字符串作为服务名
    if (lastBackslash == NULL) {
        lastBackslash = regPath->Buffer;
        nameLength = regPath->Length / sizeof(WCHAR);
    }
    else {
        // 跳过反斜杠字符
        lastBackslash++;
        nameLength = (ULONG)((regPath->Buffer + (regPath->Length / sizeof(WCHAR))) - lastBackslash);
    }

    // 检查名称长度是否为空或超出缓冲区（需要留出结尾 NULL 的位置）
    if (nameLength == 0) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (nameLength >= bufferSize) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    // 复制名称（不含结尾 NULL）
    RtlCopyMemory(serviceName, lastBackslash, nameLength * sizeof(WCHAR));
    serviceName[nameLength] = L'\0';

    return STATUS_SUCCESS;
}

NTSTATUS SetupRegConfig(PUNICODE_STRING regPath, WCHAR* serviceName) {
    NTSTATUS st = STATUS_SUCCESS;
    WCHAR instancesPathBuffer[512] = { 0 };
    WCHAR instanceNameBuffer[512] = { 0 };
    UNICODE_STRING instancesPath = { 0 };
    UNICODE_STRING valueName = { 0 };
    UNICODE_STRING insPath = { 0 };
    WCHAR insPathBuffer[512] = { 0 };
    WCHAR altitude[] = L"370030\0";
    HANDLE hInstancesKey = NULL;
    HANDLE hInsKey = NULL;
    OBJECT_ATTRIBUTES objAttr = { 0 };
    ULONG flags = 0;

    if (!regPath || !serviceName) return STATUS_INVALID_PARAMETER;

    RtlStringCbPrintfW(
        instancesPathBuffer,
        sizeof(instancesPathBuffer),
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%ws\\Instances",
        serviceName);

    RtlInitUnicodeString(&instancesPath, instancesPathBuffer);

    InitializeObjectAttributes(&objAttr, &instancesPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    st = ZwCreateKey(
        &hInstancesKey,
        KEY_ALL_ACCESS,
        &objAttr,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        NULL
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwCreateKey Failed->%lX\n", st);
        goto ret;
    }


    RtlStringCbPrintfW(
        instanceNameBuffer,
        sizeof(instanceNameBuffer),
        L"%ws Instance",
        serviceName
    );

    RtlInitUnicodeString(&valueName, L"DefaultInstance");

    st = ZwSetValueKey(
        hInstancesKey,
        &valueName,
        0,
        REG_SZ,
        instanceNameBuffer,
        (ULONG)(wcslen(instanceNameBuffer) + 1) * sizeof(WCHAR)
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwGetValueKey DefaultInstance Failed->%lX\n", st);
        goto ret;
    }

    RtlStringCbPrintfW(
        insPathBuffer,
        sizeof(insPathBuffer),
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%ws\\Instances\\%ws Instance",
        serviceName,serviceName);

    RtlInitUnicodeString(&insPath, insPathBuffer);

    InitializeObjectAttributes(&objAttr, &insPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    st = ZwCreateKey(
        &hInsKey,
        KEY_ALL_ACCESS,
        &objAttr,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        NULL
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwCreateKey Failed->%lX\n", st);
        goto ret;
    }

    RtlInitUnicodeString(&valueName, L"Altitude");
    st = ZwSetValueKey(
        hInsKey,
        &valueName,
        0,
        REG_SZ,
        altitude,
        (ULONG)(wcslen(altitude) + 1) * sizeof(WCHAR)
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwCreateKey Failed->%lX\n", st);
        goto ret;
    }

    RtlInitUnicodeString(&valueName, L"Flags");
    st = ZwSetValueKey(
        hInsKey,
        &valueName,
        0,
        REG_DWORD,
        (PVOID)flags,
        (ULONG)sizeof(ULONG)
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwCreateKey Failed->%lX\n", st);
        goto ret;
    }


ret:
    if (hInsKey) ZwClose(hInsKey);
    if (hInstancesKey) ZwClose(hInstancesKey);
    return st;

}