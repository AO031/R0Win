#include <ntifs.h>
#include <ntstrsafe.h>

BOOLEAN g_bDriverUnload = FALSE;

NTSTATUS TestThreadObjectAndId() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE currentTid = 0;
	PETHREAD currentThread = NULL;
	PETHREAD foundThread = NULL;
	HANDLE processId = 0;

	currentTid = PsGetCurrentThreadId();

	currentThread = PsGetCurrentThread();

	processId = PsGetThreadProcessId(currentThread);

	st = PsLookupThreadByThreadId(currentTid, &foundThread);
	if (!NT_SUCCESS(st)) return st;
	ObDereferenceObject(foundThread);

	return st;
}

VOID SimpleThreadRoutine(PVOID startContext) {
	LONG currentId = (LONG)startContext;
	
	DbgPrint("[W] currentId->%d\n", currentId);
	DbgPrint("[W] SimpleThreadRoutine Start\n");

	for (int i = 0; i < 5; i++) {
		if (g_bDriverUnload) break;

		DbgPrint("[W] SimpleThreadRoutine Working->%d\n", i);

		LARGE_INTEGER interval = { 0 };
		interval.QuadPart = -10000000;
		KeDelayExecutionThread(KernelMode, FALSE, &interval);
	}

	DbgPrint("[W] SimpleThreadRoutine End\n");

	PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS TestCreateSystemThread() {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE threadHandle = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	st = PsCreateSystemThread(
		&threadHandle,
		THREAD_ALL_ACCESS,
		&objAttr,
		NULL,
		NULL,
		SimpleThreadRoutine,
		(PVOID)2
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] PsCreateSystemThread Failed->%lX\n", st);
		return st;
	}

	ZwClose(threadHandle);

	return st;
}

static volatile long g_threadRunning = FALSE;

typedef struct {
	HANDLE processId;
	PKEVENT completeEvent;
	PVOID targetAddress;
	ULONG readSize;
	NTSTATUS status;
	UCHAR data[0x100];
}MemContext, *PMemContext;

VOID ReadThreadRoutine(PVOID startContext) {
	PMemContext ctx = (PMemContext)startContext;
	HANDLE currentProcessHandle = 0;
	HANDLE currentProcessId = 0;
	NTSTATUS exceptionCode = 0;
	
	if (ctx == NULL) {
		PsTerminateSystemThread(STATUS_SUCCESS);
		return;
	}

	InterlockedExchange(&g_threadRunning, 1);

	currentProcessHandle = PsGetCurrentProcess();
	currentProcessId = PsGetCurrentProcessId();
	
	DbgPrint("[W] currentProcessId->%p\n", currentProcessId);
	DbgPrint("[W] currentProcessHandle->%p\n", currentProcessHandle);

	if (currentProcessId != ctx->processId) {
		DbgPrint("[E] currentProcessId != ctx->processId\n");
		goto ret;
	}

	if (PsIsSystemThread(PsGetCurrentThread())) {
		DbgPrint("[W] System Thread\n");
	}


	__try {
		ProbeForRead(ctx->targetAddress, ctx->readSize, 1);
		RtlCopyMemory(ctx->data, ctx->targetAddress, ctx->readSize);
		DbgPrint("[W] %hhX %hhX\n", ctx->data[0], ctx->data[1]);
		ctx->status = STATUS_SUCCESS;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		exceptionCode = GetExceptionCode();
		ctx->status = exceptionCode;
		DbgPrint("[E] Error->0x%lX\n",exceptionCode);

		switch (exceptionCode)
		{
		case STATUS_ACCESS_VIOLATION:
			DbgPrint("[E] STATUS_ACCESS_VIOLATION\n");
			break;
		case STATUS_DATATYPE_MISALIGNMENT:
			DbgPrint("[E] STATUS_DATATYPE_MISALIGNMENT\n");
			break;
		case STATUS_IN_PAGE_ERROR:
			DbgPrint("[E] STATUS_IN_PAGE_ERROR\n");
			break;
		default:
			DbgPrint("[E] Unknown Error\n");
			break;
		}
	}
ret:
	if (ctx->completeEvent) KeSetEvent(ctx->completeEvent, 1, FALSE);
	InterlockedExchange(&g_threadRunning, 0);
	PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS TestAttachThread()  {
	NTSTATUS st = STATUS_SUCCESS;
	HANDLE processId = (PVOID)3012;
	PEPROCESS processObj = NULL;
	HANDLE processHandle = 0;
	KEVENT completeEvent = { 0 };
	MemContext* ctx = NULL;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE threadHandle = NULL;
	LARGE_INTEGER timeout = { 0 };

	st = PsLookupProcessByProcessId(processId, &processObj);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] PsLookupProcessByProcessId Failed->0x%lX\n", st);
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
		DbgPrint("[E] ObOpenObjectByPointer Failed->0x%lX\n", st);
		goto ret;
	}

	ctx = ExAllocatePool(NonPagedPool, sizeof(MemContext));
	if (!ctx) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrint("[E] ExAllocatePool Failed->0x%lX\n", st);
		goto ret;
	}

	RtlZeroMemory(ctx, sizeof(MemContext));

	KeInitializeEvent(&completeEvent, NotificationEvent, FALSE);

	ctx->processId = processId;
	ctx->readSize = 0x40;
	ctx->targetAddress = (PVOID)0x400000;
	ctx->completeEvent = &completeEvent;

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	st = PsCreateSystemThread(
		&threadHandle,
		THREAD_ALL_ACCESS,
		&objAttr,
		processHandle,
		NULL,
		ReadThreadRoutine,
		ctx
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] PsCreateSystemThread Failed->0x%lX\n", st);
		goto ret;
	}

	timeout.QuadPart = -100000000LL;

	st = KeWaitForSingleObject(
		&completeEvent,
		Executive,
		KernelMode,
		FALSE,
		&timeout
	);
	if (st == STATUS_TIMEOUT) {
		DbgPrint("[E] Time Out\n");
		KeWaitForSingleObject(threadHandle, Executive, KernelMode, FALSE, NULL);
		goto ret;
	}
	else if (NT_SUCCESS(st)){
		DbgPrint("[W] KeWaitForSingleObject Success\n");

	}
	else {
		DbgPrint("[E] KeWaitForSingleObject Failed->0x%lX\n", st);
		goto ret;
	}

	if (NT_SUCCESS(st) && ctx->status != STATUS_SUCCESS) {
		DbgPrint("[E] Read failed with status 0x%lX\n", ctx->status);
	}

ret:
	if (threadHandle) ZwClose(threadHandle);
	if (ctx) ExFreePool(ctx);
	if (processHandle) ZwClose(processHandle);
	if (processObj) ObDereferenceObject(processObj);
	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObject) {
	
	LARGE_INTEGER delay = { 0 };
	while (InterlockedCompareExchange(&g_threadRunning, 0, 0) != 0) {
		delay.QuadPart = -10000000LL;
		KeDelayExecutionThread(KernelMode, FALSE, &delay);
	}
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObject->DriverUnload = DriverUnload;

	//TestThreadObjectAndId();
	//TestCreateSystemThread();
	TestAttachThread();
	return st;
}