#include <ntifs.h>

#define MEM_TAG 'aodf'


typedef struct _RTL_PROCESS_MODULE_INFORMATION{
    PVOID Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES{
	ULONG NumberOfModules;
	RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, * PRTL_PROCESS_MODULES;

typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemBasicInformation,                                 // q: SYSTEM_BASIC_INFORMATION
    SystemProcessorInformation,                             // q: SYSTEM_PROCESSOR_INFORMATION
    SystemPerformanceInformation,                           // q: SYSTEM_PERFORMANCE_INFORMATION
    SystemTimeOfDayInformation,                             // q: SYSTEM_TIMEOFDAY_INFORMATION
    SystemPathInformation,                                  // q: not implemented
    SystemProcessInformation,                               // q: SYSTEM_PROCESS_INFORMATION
    SystemCallCountInformation,                             // q: SYSTEM_CALL_COUNT_INFORMATION
    SystemDeviceInformation,                                // q: SYSTEM_DEVICE_INFORMATION
    SystemProcessorPerformanceInformation,                  // q: SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION (EX in: USHORT ProcessorGroup)
    SystemFlagsInformation,                                 // qs: SYSTEM_FLAGS_INFORMATION
    SystemCallTimeInformation,                              // q: SYSTEM_CALL_TIME_INFORMATION // not implemented // 10
    SystemModuleInformation,                                // q: RTL_PROCESS_MODULES
} SYSTEM_INFORMATION_CLASS;

typedef enum {
    CmpSuccess,
    CmpFail,
    CmpInvalidParam,
    CmpWrongPattern,
}CmpStatus;

typedef struct {
    PVOID ImageBase;
    SIZE_T ImageSize;
    CHAR ImageName[80];
}KERNEL_INFO;

/*
CmpVEExecuteParseLogic(x,x,x,x,x,x)+8C  Address:0x77e11c Offset:0x37e11c
 66 89 ?? ?? ?? 33 D2 0F BF CA 66 ?? ?? ?? 0F ?? ?? ?? ?? ?? 8B 4C ?? ?? 66 83 ?? ?? ?? 0F ?? ?? ?? ?? ?? 83 79 ?? ?? 74 ?? F6 80 ?? ?? ?? ?? ?? 75 ?? F7 46 ?? ?? ?? ?? ?? 75 ?? B8 ?? ?? ?? ?? 5F 5E 5B
*/

NTSTATUS NTAPI ZwQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength
);

CmpStatus PatternCmp(CONST WCHAR* pattern, ULONG patternLength, ULONG index, UCHAR dest) {


    WCHAR currByteStr[4] = { 0 };
    UNICODE_STRING currByteUnicodeStr = { 0 };
    UCHAR currByte = 0;
    NTSTATUS st = STATUS_SUCCESS;

    if (!pattern || !*pattern || 3 * index + 1 >= patternLength) return CmpInvalidParam;

    currByteStr[0] = pattern[3 * index + 0];
    currByteStr[1] = pattern[3 * index + 1];
    currByteStr[2] = L'\0';

    if (currByteStr[0] == L'?') {
        if (currByteStr[1] == L'?') {
            return CmpSuccess;
        }
        else {
            return CmpWrongPattern;
        }
    }
    else {
        RtlInitUnicodeString(&currByteUnicodeStr, currByteStr);
        
        st = RtlUnicodeStringToInteger(&currByteUnicodeStr, 16, &currByte);
        if (!NT_SUCCESS(st)) return CmpWrongPattern;

        return currByte == dest ? CmpSuccess : CmpFail;
    }
}

NTSTATUS GetKernelInfo(KERNEL_INFO* ntInfo) {
    DbgBreakPoint();

    NTSTATUS st = STATUS_SUCCESS;
    PVOID buffer = NULL;
    ULONG bufferSize = PAGE_SIZE;
    ULONG returnBytes = 0;
    PRTL_PROCESS_MODULES modulesInfo = NULL;
    PRTL_PROCESS_MODULE_INFORMATION currInfo = NULL;
    ULONG currInfoIndex = 0;

    if (!ntInfo) {
        st = STATUS_INVALID_PARAMETER;
        return st;
    }
    
Retry:
    buffer = ExAllocatePoolWithTag(NonPagedPool, bufferSize, MEM_TAG);
    if (!buffer) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        return st;
    }
    
    st = ZwQuerySystemInformation(SystemModuleInformation, buffer, bufferSize, &returnBytes);
    if (st == STATUS_INFO_LENGTH_MISMATCH) {
        ExFreePoolWithTag(buffer, MEM_TAG);
        bufferSize = returnBytes+0x100;
        goto Retry;
    }
    
    if (!NT_SUCCESS(st)) return st;

    modulesInfo = (PRTL_PROCESS_MODULES)buffer;
    currInfo = (PRTL_PROCESS_MODULE_INFORMATION)modulesInfo->Modules;

    for (size_t i = 0; i < modulesInfo->NumberOfModules; i++) {
        if (strstr(currInfo[i].FullPathName, ntInfo->ImageName)) {
            ntInfo->ImageBase = currInfo[i].ImageBase;
            ntInfo->ImageSize = currInfo[i].ImageSize;
            return st;
        }
    }
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
    WCHAR _CmpVEExecuteParseLogic_pattern[] =
        L"66 89 ?? ?? ?? 33 D2 0F BF CA 66 ?? ?? ?? 0F ?? ?? ?? ?? ?? 8B 4C ?? ?? 66 83 ?? ?? ?? 0F ?? ?? ?? ?? ?? 83 79 ?? ?? 74 ?? F6 80 ?? ?? ?? ?? ?? 75 ?? F7 46 ?? ?? ?? ?? ?? 75 ?? B8 ?? ?? ?? ?? 5F 5E 5B";
    ULONG _CmpVEExecuteParseLogic_offset = 0x8C;
    NTSTATUS st = STATUS_SUCCESS;

    DbgPrint("?? == 0x89 -> %s\n",
        PatternCmp(_CmpVEExecuteParseLogic_pattern,
            sizeof(_CmpVEExecuteParseLogic_pattern) / sizeof(WCHAR),
            2,
            0x89) == CmpSuccess ? "TRUE" : "FALSE");
    DbgPrint("89 == 0x89 -> %s\n",
        PatternCmp(_CmpVEExecuteParseLogic_pattern,
            sizeof(_CmpVEExecuteParseLogic_pattern) / sizeof(WCHAR),
            1,
            0x89) == CmpSuccess ? "TRUE" : "FALSE");

    KERNEL_INFO ntInfo = { 0 };
    strcpy(ntInfo.ImageName, "ntoskrnl.exe");

    st = GetKernelInfo(&ntInfo);
    DbgPrint("ntoskrnl.exe Base->%p\n", ntInfo.ImageBase);

	driverObject->DriverUnload = DriverUnload;
	return st;
}