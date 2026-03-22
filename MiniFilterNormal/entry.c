#include <ntifs.h>
#include <ntstrsafe.h>
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

// ==============================================================================
typedef struct _STATISTICS_INFO {
    ULONGLONG createCount;
    ULONGLONG readCount;
    ULONGLONG writeCount;
    ULONGLONG setInfoCount;
    ULONGLONG cleanupCount;
    ULONGLONG closeCount;
    ULONGLONG filteredCount;
    ULONGLONG totalCount;
    ULONGLONG protectedCount;
    LARGE_INTEGER startTime;
}STATISTICS_INFO,*PSTATISTICS_INFO;


// ==============================================================================
STATISTICS_INFO g_statistics = { 0 };
PFLT_FILTER g_filterHandle = NULL;
CONST PWCHAR g_protectedFiles[] = {
    L"\\Device\\HarddiskVolume1\\123.txt\0"
};
#define PROTECTED_FILE_COUNT sizeof(g_protectedFiles) / sizeof(PWCHAR)

// ==============================================================================
FLT_PREOP_CALLBACK_STATUS PreOperationCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);

NTSTATUS MyFltUnload(FLT_FILTER_UNLOAD_FLAGS Flags);
NTSTATUS InstanceSetupCallback(PCFLT_RELATED_OBJECTS FltObjects,FLT_INSTANCE_SETUP_FLAGS Flags,ULONG VolumeDeviceType,FLT_FILESYSTEM_TYPE VolumeFilesystemType);
NTSTATUS InstanceQueryTeardownCallback(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags);

CONST FLT_OPERATION_REGISTRATION CallBacks[] = {
    {IRP_MJ_CREATE,0,PreOperationCallback,NULL,NULL},
    {IRP_MJ_READ,0,PreOperationCallback,NULL,NULL},
    {IRP_MJ_WRITE,0,PreOperationCallback,NULL,NULL},
    {IRP_MJ_SET_INFORMATION,0,PreOperationCallback,NULL,NULL},
    {IRP_MJ_CLEANUP,0,PreOperationCallback,NULL,NULL},
    {IRP_MJ_CLOSE,0,PreOperationCallback,NULL,NULL},
    {IRP_MJ_OPERATION_END}
};

CONST FLT_REGISTRATION FilterRegisteration = {
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    0,
    NULL,
    CallBacks,
    MyFltUnload,
    InstanceSetupCallback,
    InstanceQueryTeardownCallback,
    NULL,
    NULL,
    NULL,NULL,NULL
};

// ==============================================================================
NTSTATUS InitRegConfig(PUNICODE_STRING regPath);
NTSTATUS ExtractServiceName(PUNICODE_STRING regPath, WCHAR* serviceName, ULONG bufferSize);
NTSTATUS SetupRegConfig(PUNICODE_STRING regPath, WCHAR* serviceName);
BOOLEAN IsProtectFile(PUNICODE_STRING filePath);

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

    RtlZeroMemory(&g_statistics, sizeof(STATISTICS_INFO));
    KeQuerySystemTime(&g_statistics.startTime);
    
    st = FltRegisterFilter(driverObj,&FilterRegisteration,&g_filterHandle);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] InitRegConfig Failed->%lX\n", st);
        return st;
    }

    st = FltStartFiltering(g_filterHandle);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] InitRegConfig Failed->%lX\n", st);
        return st;
    }
    
    DbgPrint("[W] DriverEntry Success\n");
	return st;
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
        (PVOID)&flags,
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

FLT_PREOP_CALLBACK_STATUS PreOperationCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS st = STATUS_SUCCESS;
    WCHAR* operationName = L"UNKNOWN\0";
    BOOLEAN isProtected = FALSE;

    InterlockedIncrement64(&g_statistics.totalCount);

    st = FltGetFileNameInformation(Data,FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,&nameInfo);
    if (!NT_SUCCESS(st)) {
        //DbgPrint("[E] FltGetFileNameInformation Failed:%lX\n", st);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    
    st = FltParseFileNameInformation(nameInfo);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] FltParseFileNameInformation Failed:%lX\n", st);
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    switch (Data->Iopb->MajorFunction) {
    case IRP_MJ_CREATE: {
        operationName = L"CREATE";
        InterlockedIncrement64(&g_statistics.createCount);
        break;
    }
    case IRP_MJ_READ: {
        operationName = L"READ";
        InterlockedIncrement64(&g_statistics.readCount);
        break;
    }
    case IRP_MJ_WRITE: {
        operationName = L"WRITE";
        InterlockedIncrement64(&g_statistics.writeCount);
        break;
    }
    case IRP_MJ_SET_INFORMATION: {
        operationName = L"SETINFO";
        InterlockedIncrement64(&g_statistics.setInfoCount);
        break;
    }
    case IRP_MJ_CLEANUP: {
        operationName = L"CLEANUP";
        InterlockedIncrement64(&g_statistics.cleanupCount);
        break;
    }
    case IRP_MJ_CLOSE: {
        operationName = L"CLOSE";
        InterlockedIncrement64(&g_statistics.closeCount);
        break;
    }
    }

    //DbgPrint("[W] %ws | PID:%p | %wZ\n", operationName, PsGetCurrentProcessId(), &nameInfo->Name);
   
    if (IsProtectFile(&nameInfo->Name)) {
        InterlockedIncrement64(&g_statistics.protectedCount);
        DbgPrint("[W] Protected File\n");
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_COMPLETE;
    }

    FltReleaseFileNameInformation(nameInfo);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS MyFltUnload(FLT_FILTER_UNLOAD_FLAGS Flags) {
    LARGE_INTEGER currentTime = { 0 };
    LARGE_INTEGER runTime = { 0 };
    ULONGLONG totalOps = 0;
    
    KeQuerySystemTime(&currentTime);
    runTime.QuadPart = currentTime.QuadPart - g_statistics.startTime.QuadPart;
    totalOps = g_statistics.cleanupCount + g_statistics.readCount + g_statistics.writeCount
        + g_statistics.createCount + g_statistics.setInfoCount + g_statistics.closeCount
        + g_statistics.protectedCount + g_statistics.filteredCount;

    DbgPrint("\n");
    DbgPrint("==================================================\n");
    DbgPrint("[W] FltUnloadCallback\n");
    DbgPrint("[W] CREATE:%llX\n", g_statistics.createCount);
    DbgPrint("[W] READ:%llX\n", g_statistics.readCount);
    DbgPrint("[W] PROTECTED:%llX\n", g_statistics.protectedCount);
    DbgPrint("[W] TOTAl:%llX\n", totalOps);
    DbgPrint("==================================================\n");

    FltUnregisterFilter(g_filterHandle);
    return STATUS_SUCCESS;
}

NTSTATUS InstanceSetupCallback(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, ULONG VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType) {
    return (VolumeFilesystemType == FLT_FSTYPE_NTFS || VolumeFilesystemType == FLT_FSTYPE_FAT || VolumeFilesystemType == FLT_FSTYPE_EXFAT || VolumeFilesystemType == FLT_FSTYPE_REFS) ? STATUS_SUCCESS : STATUS_FLT_DO_NOT_ATTACH;
}
 
NTSTATUS InstanceQueryTeardownCallback(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags) {
    return STATUS_SUCCESS;
}

BOOLEAN IsProtectFile(PUNICODE_STRING filePath) {
    if (!filePath || !filePath->Length) return FALSE;
    UNICODE_STRING uniPath = { 0 };
    for (int i = 0; i < PROTECTED_FILE_COUNT; i++) {
        RtlInitUnicodeString(g_protectedFiles[i], &uniPath);
        if (RtlCompareUnicodeString(filePath, &uniPath,TRUE) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}