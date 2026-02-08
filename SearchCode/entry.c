#include <ntifs.h>
#include <ntimage.h>

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

typedef struct {
    PVOID ImageBase;
    SIZE_T ImageSize;
    PVOID SectionBase;
    SIZE_T SectionSize;
    CHAR SectionName[16];
    CHAR ImageName[80];
}KERNEL_INFO;

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

NTSTATUS NTAPI ZwQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength
);

CmpStatus PatternCmp(CONST WCHAR* pattern, ULONG patternLength, ULONG index, UCHAR dest) {
    WCHAR currByteStr[4] = { 0 };
    UNICODE_STRING currByteUnicodeStr = { 0 };
    ULONG currByte = 0;
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
    NTSTATUS st = STATUS_SUCCESS;
    PVOID buffer = NULL;
    ULONG bufferSize = PAGE_SIZE;
    ULONG returnBytes = 0;
    PRTL_PROCESS_MODULES modulesInfo = NULL;
    PRTL_PROCESS_MODULE_INFORMATION currInfo = NULL;

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
            ExFreePoolWithTag(buffer, MEM_TAG);
            return st;
        }
    }
}

BOOLEAN GetSectionInfo(KERNEL_INFO* ntInfo) {
    if (!ntInfo || !ntInfo->ImageBase || !ntInfo->ImageSize) {
        DbgPrint("[W] GetSectionInfo Invalid Param\n");
        return FALSE;
    }

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)ntInfo->ImageBase;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        DbgPrint("[W] Invalid Dos Header\n");
        return FALSE;
    }

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((SIZE_T)ntInfo->ImageBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        DbgPrint("[W] Invalide Nt Header\n");
        return FALSE;
    }

    PIMAGE_SECTION_HEADER sectionHeaders = IMAGE_FIRST_SECTION(ntHeaders);
    
    for (int i = 0; ntHeaders->FileHeader.NumberOfSections; i++) {
        if (!strcmp(sectionHeaders[i].Name, ntInfo->SectionName)) {
            ntInfo->SectionBase = sectionHeaders[i].VirtualAddress;
            ntInfo->SectionSize = sectionHeaders[i].Misc.VirtualSize;
            return TRUE;
        }
    }

    return FALSE;
}

INT SearchFunctionAddr(PUCHAR startAddress, SIZE_T searchLength, CONST WCHAR* pattern, SIZE_T patternStrLength) {
    if (!startAddress || !pattern || !searchLength || !patternStrLength) {
        DbgPrint("[W] SearchFunctionAddr Invalid Param\n");
        return 0;
    }

    ULONG patternByteNumber = (patternStrLength + 1) / 3;
    INT found = 1;

    DbgBreakPoint();
    for (int i = 0; i < searchLength - patternByteNumber; i++) {
        PUCHAR currSearch = &startAddress[i];
        if (!MmIsAddressValid(currSearch)) continue;

        for (int j = 0; j < patternByteNumber; j++) {
            if (PatternCmp(pattern, patternStrLength, j, currSearch[j]) != CmpSuccess) {
                found = 0;
            }
        }

        if (found) {
            return currSearch;
        }
        found = 1;
    }

    return 0; 
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
    WCHAR _CmpVEExecuteParseLogic_pattern[] =
        L"8B FF 55 8B EC 83 ?? ?? 83 ?? ?? 80 ?? ?? ?? ?? ?? ?? 53 56 57 89 54 ?? ?? 8B F1 74 ?? 8B 7D ?? F6 47 ?? ?? 75 ?? 66 83 ?? ?? ?? 75 ?? 8B 46 ?? 3B ?? ?? ?? ?? ?? 74 ?? 33 C9 89 74 ?? ?? 89 4C ?? ?? 89 4C ?? ?? 89 4C ?? ?? 66 89 ?? ?? ?? 33 D2 0F BF CA 66 ?? ?? ?? 0F ?? ?? ?? ?? ?? 8B 4C ?? ?? 66 83 ?? ?? ?? 0F ?? ?? ?? ?? ?? 83 79 ?? ?? 74 ?? F6 80 ?? ?? ?? ?? ?? 75 ?? F7 46 ?? ?? ?? ?? ?? 75 ?? B8 ?? ?? ?? ?? 5F 5E 5B 8B E5 5D C2 ?? ??";
    ULONG _CmpVEExecuteParseLogic_offset = 0;
    SIZE_T patternLength = sizeof(_CmpVEExecuteParseLogic_pattern) / sizeof(WCHAR);
    NTSTATUS st = STATUS_SUCCESS;

    DbgPrint("?? == 0x89 -> %s\n",
        PatternCmp(_CmpVEExecuteParseLogic_pattern,
            patternLength,
            2,
            0x89) == CmpSuccess ? "TRUE" : "FALSE");
    DbgPrint("89 == 0x89 -> %s\n",
        PatternCmp(_CmpVEExecuteParseLogic_pattern,
            patternLength,
            1,
            0x89) == CmpSuccess ? "TRUE" : "FALSE");

    KERNEL_INFO ntInfo = { 0 };
    strcpy(ntInfo.ImageName, "ntoskrnl.exe\0");

    st = GetKernelInfo(&ntInfo);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[W] GetKernelInfo Failed->0x%08X\n", st);
        return st;
    }
    DbgPrint("ntoskrnl.exe Base->%p\n", ntInfo.ImageBase);

    strcpy(ntInfo.SectionName, "PAGE\0");

    if (!GetSectionInfo(&ntInfo)) {
        DbgPrint("[W] GetSectionInfo Failed");
        return st;
    }

    ULONG _CmpVEExecuteParseLogic_fileOffset = SearchFunctionAddr(
        (ULONG)ntInfo.ImageBase + (ULONG)ntInfo.SectionBase,
        ntInfo.SectionSize,
        _CmpVEExecuteParseLogic_pattern,
        patternLength);
    if (!_CmpVEExecuteParseLogic_fileOffset) {
        DbgPrint("[W] SearchFunctionAddr Failed\n");
        return st;
    }
    DbgPrint("[W] Found CmpVEExecuteParseLogic Addr->0x%08X\n",
        _CmpVEExecuteParseLogic_fileOffset - _CmpVEExecuteParseLogic_offset);

	driverObject->DriverUnload = DriverUnload;
	return st;
}