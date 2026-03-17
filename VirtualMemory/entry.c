#include <ntifs.h>

NTSYSCALLAPI
NTSTATUS
NTAPI
ZwProtectVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG NewProtection,
    _Out_ PULONG OldProtection
);

NTSTATUS NTAPI MmCopyVirtualMemory(
    PEPROCESS SourceProcess,
    PVOID SourceAddress,
    PEPROCESS TargetProcess,
    PVOID TargetAddress,
    SIZE_T BufferSize,
    KPROCESSOR_MODE PreviousMode,
    PSIZE_T ReturnSize
);

#define TEST_BUFFER_SIZE 8192

NTSTATUS TestAllocteMemory(HANDLE targetPid) {
    NTSTATUS st = STATUS_SUCCESS;
    PEPROCESS processObj = NULL;
    HANDLE processHandle = NULL;
    PVOID targetBaseAddress = NULL;
    ULONG regionSize = 0x1000;
    MEMORY_BASIC_INFORMATION memInfo = { 0 };
    ULONG returnLength = 0;
    ULONG oldProtect = 0;

    st = PsLookupProcessByProcessId(targetPid, &processObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
        goto ret;
    }

    st = ObOpenObjectByPointer(
        processObj,
        OBJ_KERNEL_HANDLE,
        NULL,
        PROCESS_ALL_ACCESS,
        *PsProcessType,
        KernelMode,
        &processHandle
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
        goto ret;
    }

    st = ZwAllocateVirtualMemory(
        processHandle,
        &targetBaseAddress,
        0,
        &regionSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] NtAllocateVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    st = ZwQueryVirtualMemory(
        processHandle,
        targetBaseAddress,
        MemoryBasicInformation,
        &memInfo,
        sizeof(MEMORY_BASIC_INFORMATION),
        &returnLength
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] NtQueryVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    DbgPrint("[W] Base Address:0x%p\n", memInfo.BaseAddress);
    DbgPrint("[W] Region Size:0x%lX\n", memInfo.RegionSize);
    DbgPrint("[W] Mem Protect:0x%lX\n", memInfo.Protect);

    st = ZwProtectVirtualMemory(
        processHandle,
        &targetBaseAddress,
        &regionSize,
        PAGE_READONLY,
        &oldProtect
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwProtectVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    st = ZwQueryVirtualMemory(
        processHandle,
        targetBaseAddress,
        MemoryBasicInformation,
        &memInfo,
        sizeof(MEMORY_BASIC_INFORMATION),
        &returnLength
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] NtQueryVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    DbgPrint("[W] Base Address:0x%p\n", memInfo.BaseAddress);
    DbgPrint("[W] Region Size:0x%lX\n", memInfo.RegionSize);
    DbgPrint("[W] Mem Protect:0x%lX\n", memInfo.Protect);
ret:
    if (targetBaseAddress) NtFreeVirtualMemory(processHandle, &targetBaseAddress, &regionSize, MEM_RELEASE);
    if (processObj) ObDereferenceObject(processObj);
    if (processHandle) ZwClose(processHandle);
    return st;
}

NTSTATUS TestReadVirtualMemory(HANDLE targetPid) {
    NTSTATUS st = STATUS_SUCCESS;
    HANDLE processHandle = NULL;
    PEPROCESS processObj = NULL;
    UCHAR buffer[32] = { 0 };
    ULONG readSize = 16;
    ULONG returnLength = 0;

    st = PsLookupProcessByProcessId(targetPid, &processObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
        goto ret;
    }

    st = ObOpenObjectByPointer(
        processObj,
        OBJ_KERNEL_HANDLE,
        NULL,
        PROCESS_ALL_ACCESS,
        *PsProcessType,
        KernelMode,
        &processHandle
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
        goto ret;
    }

    st = MmCopyVirtualMemory(
        processObj,
        (PVOID)0x400000,
        PsGetCurrentProcess(),
        (PVOID)buffer,
        readSize,
        KernelMode,
        &returnLength
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] MmCopyVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    for (ULONG i = 0; i < readSize; i++) {
        DbgPrint("%hhX ", buffer[i]);
    }
    DbgPrint("\n");

ret:
    if (processHandle) ZwClose(processHandle);
    if (processObj) ObDereferenceObject(processObj);
    return st;
}

NTSTATUS TestWriteVirtualMemory(HANDLE targetPid) {
    NTSTATUS st = STATUS_SUCCESS;
    HANDLE processHandle = NULL;
    PEPROCESS processObj = NULL;
    PVOID baseAddress = NULL;
    UCHAR buffer[32] = { 0 };
    ULONG writeSize = 16;
    SIZE_T regionSize = 0x1000;
    ULONG returnLength = 0;

    st = PsLookupProcessByProcessId(targetPid, &processObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
        goto ret;
    }

    st = ObOpenObjectByPointer(
        processObj,
        OBJ_KERNEL_HANDLE,
        NULL,
        PROCESS_ALL_ACCESS,
        *PsProcessType,
        KernelMode,
        &processHandle
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
        goto ret;
    }

    st = ZwAllocateVirtualMemory(
        processHandle,
        &baseAddress,
        0,
        &regionSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwAllocateVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    DbgPrint("[W] baseAddress->%p\n", baseAddress);

    for (ULONG i = 0; i < writeSize; i++) buffer[i] = (UCHAR)i;

    st = MmCopyVirtualMemory(
        PsGetCurrentProcess(),
        (PVOID)buffer,
        processObj,
        baseAddress,
        writeSize,
        KernelMode,
        &returnLength
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] MmCopyVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

ret:
    if (baseAddress) ZwFreeVirtualMemory(processHandle, &baseAddress, &regionSize, MEM_RELEASE);
    if (processHandle) ZwClose(processHandle);
    if (processObj) ObDereferenceObject(processObj);
    return st;
}

NTSTATUS TestBasicMdlOperations() {
    PVOID buffer = NULL;
    PMDL mdl = NULL;
    PVOID virtualAddress = NULL;
    PVOID systemAddress = NULL;
    SIZE_T mdlSize = 0;
    ULONG pageCount = 0;

    buffer = ExAllocatePool(NonPagedPool, TEST_BUFFER_SIZE);
    if (!buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlFillMemory(buffer, 256, 0xFF);

    pageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(buffer, TEST_BUFFER_SIZE);

    mdlSize = MmSizeOfMdl(buffer, TEST_BUFFER_SIZE);
    
    mdl = IoAllocateMdl(buffer, TEST_BUFFER_SIZE, FALSE, FALSE, NULL);
    
    if (!mdl) {
        ExFreePool(buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    virtualAddress = MmGetMdlVirtualAddress(mdl);

    MmBuildMdlForNonPagedPool(mdl);

    systemAddress = MmGetSystemAddressForMdl(mdl);

    IoFreeMdl(mdl);
    ExFreePool(buffer);
    return STATUS_SUCCESS;
}

NTSTATUS TestUserModeMemory() {
    NTSTATUS st = STATUS_SUCCESS;
    PEPROCESS currentProcess = PsGetCurrentProcess();
    KAPC_STATE apcState = { 0 };
    PVOID userBuffer = NULL;
    SIZE_T regionSize = TEST_BUFFER_SIZE;
    PMDL userMdl = NULL;
    PVOID systemAddress = NULL;
    PPFN_NUMBER pfnArray = NULL;
    
    KeStackAttachProcess(currentProcess, &apcState);

    st = ZwAllocateVirtualMemory(
        ZwCurrentProcess(),
        &userBuffer,
        0,
        &regionSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
    );
    if (!NT_SUCCESS(st)) {
        DbgPrint("[E] ZwAllocateVirtualMemory Failed->%lX\n", st);
        goto ret;
    }

    for (ULONG i = 0; i < 256; i++) {
        ((PUCHAR)userBuffer)[i] = (UCHAR)(i % 256);
    }

    userMdl = IoAllocateMdl(
        userBuffer,
        TEST_BUFFER_SIZE,
        FALSE,
        FALSE,
        NULL
    );
    if (!userMdl) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto ret;
    }

    __try {
        MmProbeAndLockPages(userMdl, UserMode, IoReadAccess);
        pfnArray = MmGetMdlPfnArray(userMdl);
        systemAddress = MmGetSystemAddressForMdlSafe(userMdl, NormalPagePriority);
        MmUnlockPages(userMdl);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        goto ret;
    }
ret:
    if (userMdl) IoFreeMdl(userMdl);
    if (userBuffer) ZwFreeVirtualMemory(ZwCurrentProcess(), &userBuffer, &regionSize, MEM_RELEASE);
    KeUnstackDetachProcess(&apcState);
    return st;
}

NTSTATUS TestPhysicalMem() {
    PHYSICAL_ADDRESS lowestAcceptable = { 0 };
    PHYSICAL_ADDRESS highestAcceptable = { 0 };
    PHYSICAL_ADDRESS boundaryAcceptable = { 0 };
    PHYSICAL_ADDRESS phyAddress = { 0 };
    PVOID contiguousBuffer = NULL;
    PVOID mappedAddress = NULL;
    NTSTATUS st = STATUS_SUCCESS;

    highestAcceptable.QuadPart = MAXULONG;

    contiguousBuffer = MmAllocateContiguousMemorySpecifyCache(
        PAGE_SIZE,
        lowestAcceptable,
        highestAcceptable,
        boundaryAcceptable,
        MmCached
    );
    if (!contiguousBuffer) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto ret;
    }

    phyAddress = MmGetPhysicalAddress(contiguousBuffer);
    if (!phyAddress.QuadPart) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto ret;
    }

    mappedAddress = MmMapIoSpace(phyAddress, PAGE_SIZE, MmCached);
    if (!mappedAddress) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto ret;
    }

ret:
    if (mappedAddress) MmUnmapIoSpace(mappedAddress, PAGE_SIZE);
    if (contiguousBuffer) MmFreeContiguousMemory(contiguousBuffer);
    return STATUS_SUCCESS;
}

VOID DriverUnload(PDRIVER_OBJECT driverObj) {

}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObj->DriverUnload = DriverUnload;
    
    //TestAllocteMemory((HANDLE)920);
    //TestReadVirtualMemory((HANDLE)920);
    //TestWriteVirtualMemory((HANDLE)920);
    //TestBasicMdlOperations();
    //TestUserModeMemory();
    //TestPhysicalMem();
    return st;
}