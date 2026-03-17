#include <ntifs.h>
#include <ntstrsafe.h>

typedef struct _DIRECTORY_BASIC_INFORMATION {
	UNICODE_STRING ObjectName;
	UNICODE_STRING ObjectTypeName;
} DIRECTORY_BASIC_INFORMATION, * PDIRECTORY_BASIC_INFORMATION;

NTSYSAPI NTSTATUS ZwQueryDirectoryObject(
	HANDLE DirectoryHandle,
	PVOID Buffer,
	ULONG Length,
	BOOLEAN ReturnSingleEntry,
	BOOLEAN RestartScan,
	PULONG Context,
	PULONG ReturnLength
);

NTSTATUS QuerySymbolicLinkTarget(PCWSTR linkPath) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING uniLinkPath = { 0 };
	UNICODE_STRING targetName = { 0 };
	HANDLE linkHandle = NULL;
	WCHAR targetBuffer[260] = { 0 };
	ULONG returnbyte = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };

	st = RtlUnicodeStringInit(&uniLinkPath, linkPath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &uniLinkPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwOpenSymbolicLinkObject(
		&linkHandle,
		SYMBOLIC_LINK_QUERY,
		&objAttr
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwOpenSymbolicLinkObject Failed->%lX\n", st);
		goto ret;
	}

	targetName.Buffer = targetBuffer;
	targetName.Length = 0;
	targetName.MaximumLength = sizeof(targetBuffer);

	st = ZwQuerySymbolicLinkObject(
		linkHandle,
		&targetName,
		&returnbyte
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwQuerySymbolicLinkObject Failed->%lX\n", st);
		goto ret;
	}

	DbgPrint("[W] %ws Symbolic Link %wZ\n", linkPath, &targetName);
ret:
	if (linkHandle) ZwClose(linkHandle);
	return st;
}

NTSTATUS EnumDirectory(PCWSTR directoryPath) {
	UNICODE_STRING dirName = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE dirHandle = NULL;
	NTSTATUS st = STATUS_SUCCESS;
	UCHAR buffer[0x1000] = { 0 };
	ULONG context = 0;
	ULONG returnByte = 0;
	ULONG objectCount = 1;
	PDIRECTORY_BASIC_INFORMATION dirInfo = NULL;

	st = RtlUnicodeStringInit(&dirName, directoryPath);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, &dirName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwOpenDirectoryObject(
		&dirHandle,
		DIRECTORY_QUERY,
		&objAttr
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwOpenDirectoryObject Failed->%lX\n", st);
		goto ret;
	}

	while (TRUE) {
		st = ZwQueryDirectoryObject(
			dirHandle,
			buffer,
			sizeof(buffer),
			TRUE,
			FALSE,
			&context,
			&returnByte
		);
		if (st == STATUS_NO_MORE_ENTRIES) {
			break;
		}
		else if (!NT_SUCCESS(st)) {
			DbgPrint("[W] ZwOpenDirectoryObject Failed->%lX\n", st);
			goto ret;
		}

		dirInfo = (PDIRECTORY_BASIC_INFORMATION)buffer;
		if (dirInfo->ObjectName.Buffer && dirInfo->ObjectTypeName.Buffer) {
			DbgPrint("[W] Object%d Name:%wZ Type:%wZ\n", objectCount, &dirInfo->ObjectName, &dirInfo->ObjectTypeName);
		}

		objectCount++;
	}

ret:
	if (dirHandle) ZwClose(dirHandle);
	return st;
}

NTSTATUS QueryObjectInfomation(HANDLE objHandle) {
	NTSTATUS st = STATUS_SUCCESS;
	PUBLIC_OBJECT_BASIC_INFORMATION basicInfo = { 0 };
	ULONG returnLength = 0;

	st = ZwQueryObject(
		objHandle,
		ObjectBasicInformation,
		&basicInfo,
		sizeof(PUBLIC_OBJECT_BASIC_INFORMATION),
		&returnLength
	);
	if (NT_SUCCESS(st)) {
		DbgPrint("[W] Object Attribute\n");
		DbgPrint("[W] \tHandle Count:%d\n", basicInfo.HandleCount);
		DbgPrint("[W] \tPointer Count:%d\n", basicInfo.PointerCount);
		DbgPrint("[W] \tAttributes Flag:%lX\n", basicInfo.Attributes);
	}

	return st;
}

NTSTATUS QueryReferenceCount() {
	UNICODE_STRING eventName = { 0 };
	PKEVENT eventObj = {0};
	HANDLE eventHandle = NULL;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	st = RtlUnicodeStringInit(&eventName, L"\\BaseNamedObjects\\TestEvent_Winter");
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] RtlUnicodeStringInit Failed->%lX\n", st);
		goto ret;
	} 

	InitializeObjectAttributes(&objAttr, &eventName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	st = ZwCreateEvent(&eventHandle, EVENT_ALL_ACCESS, &objAttr, NotificationEvent, FALSE);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwCreateEvent Failed->%lX\n", st);
		goto ret;
	}

	st = ObReferenceObjectByHandle(
		eventHandle,
		EVENT_ALL_ACCESS,
		NULL,
		KernelMode,
		&eventObj,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ObReferenceObjectByHandle Failed->%lX\n", st);
		goto ret;
	}

	QueryObjectInfomation(eventHandle);
	ObDereferenceObject(eventObj);
	QueryObjectInfomation(eventHandle);

ret:
	if (eventHandle) ZwClose(eventHandle);
	return st;
}

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
    SystemLocksInformation,                                 // q: RTL_PROCESS_LOCKS
    SystemStackTraceInformation,                            // q: RTL_PROCESS_BACKTRACES
    SystemPagedPoolInformation,                             // q: not implemented
    SystemNonPagedPoolInformation,                          // q: not implemented
    SystemHandleInformation,                                // q: SYSTEM_HANDLE_INFORMATION
    SystemObjectInformation,                                // q: SYSTEM_OBJECTTYPE_INFORMATION mixed with SYSTEM_OBJECT_INFORMATION
    SystemPageFileInformation,                              // q: SYSTEM_PAGEFILE_INFORMATION
    SystemVdmInstemulInformation,                           // q: SYSTEM_VDM_INSTEMUL_INFO
    SystemVdmBopInformation,                                // q: not implemented // 20
    SystemFileCacheInformation,                             // qs: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypeSystemCache)
    SystemPoolTagInformation,                               // q: SYSTEM_POOLTAG_INFORMATION
    SystemInterruptInformation,                             // q: SYSTEM_INTERRUPT_INFORMATION (EX in: USHORT ProcessorGroup)
    SystemDpcBehaviorInformation,                           // qs: SYSTEM_DPC_BEHAVIOR_INFORMATION; s: SYSTEM_DPC_BEHAVIOR_INFORMATION (requires SeLoadDriverPrivilege)
    SystemFullMemoryInformation,                            // q: SYSTEM_MEMORY_USAGE_INFORMATION // not implemented
    SystemLoadGdiDriverInformation,                         // s: (kernel-mode only)
    SystemUnloadGdiDriverInformation,                       // s: (kernel-mode only)
    SystemTimeAdjustmentInformation,                        // qs: SYSTEM_QUERY_TIME_ADJUST_INFORMATION; s: SYSTEM_SET_TIME_ADJUST_INFORMATION (requires SeSystemtimePrivilege)
    SystemSummaryMemoryInformation,                         // q: SYSTEM_MEMORY_USAGE_INFORMATION // not implemented
    SystemMirrorMemoryInformation,                          // qs: (requires license value "Kernel-MemoryMirroringSupported") (requires SeShutdownPrivilege) // 30
    SystemPerformanceTraceInformation,                      // qs: (type depends on EVENT_TRACE_INFORMATION_CLASS)
    SystemObsolete0,                                        // q: not implemented
    SystemExceptionInformation,                             // q: SYSTEM_EXCEPTION_INFORMATION
    SystemCrashDumpStateInformation,                        // s: SYSTEM_CRASH_DUMP_STATE_INFORMATION (requires SeDebugPrivilege)
    SystemKernelDebuggerInformation,                        // q: SYSTEM_KERNEL_DEBUGGER_INFORMATION
    SystemContextSwitchInformation,                         // q: SYSTEM_CONTEXT_SWITCH_INFORMATION
    SystemRegistryQuotaInformation,                         // qs: SYSTEM_REGISTRY_QUOTA_INFORMATION; s (requires SeIncreaseQuotaPrivilege)
    SystemExtendServiceTableInformation,                    // s: (requires SeLoadDriverPrivilege) // loads win32k only
    SystemPrioritySeparation,                               // s: (requires SeTcbPrivilege)
    SystemVerifierAddDriverInformation,                     // s: UNICODE_STRING (requires SeDebugPrivilege) // 40
    SystemVerifierRemoveDriverInformation,                  // s: UNICODE_STRING (requires SeDebugPrivilege)
    SystemProcessorIdleInformation,                         // q: SYSTEM_PROCESSOR_IDLE_INFORMATION (EX in: USHORT ProcessorGroup)
    SystemLegacyDriverInformation,                          // q: SYSTEM_LEGACY_DRIVER_INFORMATION
    SystemCurrentTimeZoneInformation,                       // qs: RTL_TIME_ZONE_INFORMATION
    SystemLookasideInformation,                             // q: SYSTEM_LOOKASIDE_INFORMATION
    SystemTimeSlipNotification,                             // s: HANDLE (NtCreateEvent) (requires SeSystemtimePrivilege)
    SystemSessionCreate,                                    // q: not implemented
    SystemSessionDetach,                                    // q: not implemented
    SystemSessionInformation,                               // q: not implemented (SYSTEM_SESSION_INFORMATION)
    SystemRangeStartInformation,                            // q: SYSTEM_RANGE_START_INFORMATION // 50
    SystemVerifierInformation,                              // qs: SYSTEM_VERIFIER_INFORMATION; s (requires SeDebugPrivilege)
    SystemVerifierThunkExtend,                              // qs: (kernel-mode only)
    SystemSessionProcessInformation,                        // q: SYSTEM_SESSION_PROCESS_INFORMATION
    SystemLoadGdiDriverInSystemSpace,                       // qs: SYSTEM_GDI_DRIVER_INFORMATION (kernel-mode only) (same as SystemLoadGdiDriverInformation)
    SystemNumaProcessorMap,                                 // q: SYSTEM_NUMA_INFORMATION
    SystemPrefetcherInformation,                            // qs: PREFETCHER_INFORMATION // PfSnQueryPrefetcherInformation
    SystemExtendedProcessInformation,                       // q: SYSTEM_EXTENDED_PROCESS_INFORMATION
    SystemRecommendedSharedDataAlignment,                   // q: ULONG // KeGetRecommendedSharedDataAlignment
    SystemComPlusPackage,                                   // qs: ULONG
    SystemNumaAvailableMemory,                              // q: SYSTEM_NUMA_INFORMATION // 60
    SystemProcessorPowerInformation,                        // q: SYSTEM_PROCESSOR_POWER_INFORMATION (EX in: USHORT ProcessorGroup)
    SystemEmulationBasicInformation,                        // q: SYSTEM_BASIC_INFORMATION
    SystemEmulationProcessorInformation,                    // q: SYSTEM_PROCESSOR_INFORMATION
    SystemExtendedHandleInformation,                        // q: SYSTEM_HANDLE_INFORMATION_EX
} SYSTEM_INFORMATION_CLASS;

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX
{
	PVOID Object;
	HANDLE UniqueProcessId;
	HANDLE HandleValue;
	ACCESS_MASK GrantedAccess;
	USHORT CreatorBackTraceIndex;
	USHORT ObjectTypeIndex;
	ULONG HandleAttributes;
	ULONG Reserved;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX, * PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SYSTEM_HANDLE_INFORMATION_EX
{
	ULONG_PTR NumberOfHandles;
	ULONG_PTR Reserved;
	_Field_size_(NumberOfHandles) SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
} SYSTEM_HANDLE_INFORMATION_EX, * PSYSTEM_HANDLE_INFORMATION_EX;

NTSYSCALLAPI
NTSTATUS
NTAPI
ZwQuerySystemInformation(
	_In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
	_Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
	_In_ ULONG SystemInformationLength,
	_Out_opt_ PULONG ReturnLength
);

NTSTATUS EnumProcessHandles(HANDLE targetPid) {
	NTSTATUS st = STATUS_SUCCESS;
	PVOID buffer = NULL;
	ULONG bufferSize = 0x180000;
	ULONG handleCount = 0;
	ULONG returnLength = 0;
	PSYSTEM_HANDLE_INFORMATION_EX handleInfo = { 0 };
	buffer = ExAllocatePool(NonPagedPool, bufferSize);
	if (!buffer) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto ret;
	}

	st = ZwQuerySystemInformation(
		SystemExtendedHandleInformation,
		buffer,
		bufferSize,
		&returnLength
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ZwQuerySystemInformation Failed->%lX\n", st);
		goto ret;
	}

	handleInfo = (SYSTEM_HANDLE_INFORMATION_EX*)buffer;
	for (ULONG i = 0; i < handleInfo->NumberOfHandles; i++) {
		if (handleInfo->Handles[i].UniqueProcessId == targetPid) {
			DbgPrint("[W] Handle %d typedeIndex: %hX Object:0x%p Access:0x%lX\n",
				handleCount,
				handleInfo[i].Handles->ObjectTypeIndex,
				handleInfo[i].Handles->Object,
				handleInfo[i].Handles->GrantedAccess);
			handleCount++;
		}
	}

ret:
	if (buffer) ExFreePool(buffer);
	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {

}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObject->DriverUnload = DriverUnload;

	//EnumDirectory(L"\\");
	//QuerySymbolicLinkTarget(L"\\??\\C:");
	//QueryReferenceCount();
	EnumProcessHandles((HANDLE)7524);
	return st;
}