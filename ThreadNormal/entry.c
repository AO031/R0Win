#include <ntifs.h>
#include <ntstrsafe.h>

#define TEST_THREAD_COUNT 3

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

typedef struct {
	PKEVENT pEvent;
	int threadIndex;
}EventContext;

VOID EventNotificationRoutine(PVOID context) {
	EventContext* ctx = (EventContext*)context;
	LARGE_INTEGER timeout = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] Thread %d Is Waiting\n", ctx->threadIndex);

	timeout.QuadPart = -100000000LL;
	st = KeWaitForSingleObject(ctx->pEvent, Executive, KernelMode, FALSE, &timeout);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] Thread %d Timeout\n", ctx->threadIndex);
		ExFreePool(ctx);
		PsTerminateSystemThread(st);
		return;
	}

	DbgPrint("[W] Thread %d Is Working\n", ctx->threadIndex);
	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

NTSTATUS TestEventNotification() {
	KEVENT kevent = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE threadHandle[TEST_THREAD_COUNT] = { 0 };
	EventContext* ctx[TEST_THREAD_COUNT] = { 0 };
	LARGE_INTEGER timeout = { 0 };
	LARGE_INTEGER delay = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	KeInitializeEvent(&kevent, NotificationEvent, FALSE);

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		ctx[i] = (EventContext*)ExAllocatePool(NonPagedPool, sizeof(EventContext));
		if (!ctx[i]) {
			st = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		ctx[i]->pEvent = &kevent;
		ctx[i]->threadIndex = i;

		st = PsCreateSystemThread(
			&threadHandle[i],
			THREAD_ALL_ACCESS,
			&objAttr,
			NULL,
			NULL,
			EventNotificationRoutine,
			ctx[i]
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", i, st);
			goto ret;
		}

	}

	delay.QuadPart = -20000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	DbgPrint("[W] Start To Set Event\n");
	KeSetEvent(&kevent, IO_NO_INCREMENT, FALSE);

	delay.QuadPart = -20000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

ret:
	timeout.QuadPart = -100000000LL;
	for (ULONG i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) {
			ZwWaitForSingleObject(threadHandle[i], FALSE, NULL); // Infinite wait
		}
	}

	// Close handles and free contexts
	for (ULONG i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) ZwClose(threadHandle[i]);
	}
	return st;
}

VOID EventSynchronizationRoutine(PVOID context) {
	EventContext* ctx = (EventContext*)context;
	LARGE_INTEGER timeout = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] Thread %d Is Waiting\n", ctx->threadIndex);

	timeout.QuadPart = -200000000LL;
	st = KeWaitForSingleObject(ctx->pEvent, Executive, KernelMode, FALSE, &timeout);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] Thread %d Timeout\n", ctx->threadIndex);
		ExFreePool(ctx);
		PsTerminateSystemThread(st);
		return;
	}

	DbgPrint("[W] Thread %d Is Working\n", ctx->threadIndex);
	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

NTSTATUS TestEventSynchronization() {
	KEVENT kevent = { 0 };
	EventContext* ctx[TEST_THREAD_COUNT] = { 0 };
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE threadHandle[TEST_THREAD_COUNT] = { 0 };
	LARGE_INTEGER timeout = { 0 };
	LARGE_INTEGER delay = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	KeInitializeEvent(&kevent, SynchronizationEvent, FALSE);

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		ctx[i] = (EventContext*)ExAllocatePool(NonPagedPool, sizeof(EventContext));
		if (!ctx[i]) {
			st = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		ctx[i]->pEvent = &kevent;
		ctx[i]->threadIndex = i;

		st = PsCreateSystemThread(
			&threadHandle[i],
			THREAD_ALL_ACCESS,
			&objAttr,
			NULL,
			NULL,
			EventSynchronizationRoutine,
			ctx[i]
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", i, st);
			goto ret;
		}
	}

	delay.QuadPart = -20000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	for (int i = 0; i < 3; i++) {
		DbgPrint("[W] Start To Set Event %d\n",i);
		KeSetEvent(&kevent, IO_NO_INCREMENT, FALSE);
		delay.QuadPart = -20000000LL;
		KeDelayExecutionThread(KernelMode, FALSE, &delay);
	}
	

	delay.QuadPart = -20000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

ret:
	timeout.QuadPart = -100000000LL;
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) {
			ZwWaitForSingleObject(threadHandle[i], FALSE, NULL);
		}
	}

	// Close handles and free contexts
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) ZwClose(threadHandle[i]);
		
	}
	return st;
}

typedef struct {
	PKSEMAPHORE pSemaphore;
	int threadIndex;
}SemaphoreContext;

VOID SemaphoreThreadRoutine(PVOID context) {
	SemaphoreContext* ctx = (SemaphoreContext*)context;
	LARGE_INTEGER timeout = { 0 };
	LARGE_INTEGER delay = { 0 };
	LONG previousCount = 0;
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] Thread %d Is Waiting\n", ctx->threadIndex);

	timeout.QuadPart = -500000000LL;
	st = KeWaitForSingleObject(ctx->pSemaphore, Executive, KernelMode, FALSE, &timeout);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] Thread %d Timeout\n", ctx->threadIndex);
		ExFreePool(ctx);
		PsTerminateSystemThread(st);
		return;
	}

	DbgPrint("[W] Thread %d Is Working\n", ctx->threadIndex);
	delay.QuadPart = -2000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	previousCount = KeReleaseSemaphore(ctx->pSemaphore, IO_NO_INCREMENT, 1, FALSE);
	DbgPrint("[W] Thread %d KeReleaseSemaphore Success Count:%d\n", ctx->threadIndex, previousCount);

	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

NTSTATUS TestSemaphore() {
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE threadHandle[TEST_THREAD_COUNT] = { 0 };
	LARGE_INTEGER timeout = { 0 };
	LARGE_INTEGER delay = { 0 };
	NTSTATUS st = STATUS_SUCCESS;
	KSEMAPHORE ksemaphore = { 0 };
	SemaphoreContext* ctx[TEST_THREAD_COUNT] = { 0 };

	KeInitializeSemaphore(&ksemaphore, 2, 2);

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		ctx[i] = (SemaphoreContext*)ExAllocatePool(NonPagedPool, sizeof(SemaphoreContext));
		if (!ctx[i]) {
			st = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		ctx[i]->pSemaphore = &ksemaphore;
		ctx[i]->threadIndex = i;

		st = PsCreateSystemThread(
			&threadHandle[i],
			THREAD_ALL_ACCESS,
			&objAttr,
			NULL,
			NULL,
			SemaphoreThreadRoutine,
			ctx[i]
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", i, st);
			goto ret;
		}
	}

	delay.QuadPart = -20000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

ret:
	timeout.QuadPart = -100000000LL;
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) {
			ZwWaitForSingleObject(threadHandle[i], FALSE, NULL);
		}
	}

	// Close handles and free contexts
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) ZwClose(threadHandle[i]);
	}
	return st;
}

typedef struct {
	PKMUTEX pMutex;
	int threadIndex;
	PLONG sharePointer;
}MutexContext;

VOID MutexThreadRoutine(PVOID context) {
	MutexContext* ctx = (MutexContext*)context;
	LARGE_INTEGER timeout = { 0 };
	LARGE_INTEGER delay = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] Thread %d Is Waiting\n", ctx->threadIndex);

	timeout.QuadPart = -500000000LL;
	st = KeWaitForSingleObject(ctx->pMutex, Executive, KernelMode, FALSE, &timeout);

	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] Thread %d Timeout\n", ctx->threadIndex);
		ExFreePool(ctx);
		PsTerminateSystemThread(st);
		return;
	}

	DbgPrint("[W] Thread %d Is Working\n", ctx->threadIndex);

	*ctx->sharePointer += 1;

	DbgPrint("[W] shareValue Is %d\n", *ctx->sharePointer);

	delay.QuadPart = -2000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);
	
	KeReleaseMutex(ctx->pMutex, FALSE);
	DbgPrint("[W] Thread %d KeReleaseMutex Success\n", ctx->threadIndex);
	
	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

NTSTATUS TestMutex() {
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE threadHandle[TEST_THREAD_COUNT] = { 0 };
	LARGE_INTEGER timeout = { 0 };
	LONG shareValue = 0;
	NTSTATUS st = STATUS_SUCCESS;
	KMUTEX kmutex = { 0 };
	MutexContext* ctx[TEST_THREAD_COUNT] = { 0 };

	KeInitializeMutex(&kmutex, 0);

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		ctx[i] = (MutexContext*)ExAllocatePool(NonPagedPool, sizeof(MutexContext));
		if (!ctx[i]) {
			st = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		ctx[i]->pMutex = &kmutex;
		ctx[i]->threadIndex = i;
		ctx[i]->sharePointer = &shareValue;

		st = PsCreateSystemThread(
			&threadHandle[i],
			THREAD_ALL_ACCESS,
			&objAttr,
			NULL,
			NULL,
			MutexThreadRoutine,
			ctx[i]
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", i, st);
			goto ret;
		}
	}

ret:
	timeout.QuadPart = -100000000LL;
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) {
			ZwWaitForSingleObject(threadHandle[i], FALSE, NULL);
		}
	}

	// Close handles and free contexts
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) ZwClose(threadHandle[i]);
	}
	return st;
}

typedef struct {
	PERESOURCE pResource;
	int threadIndex;
	PLONG sharePointer;
}ResourceContext;

VOID ResourceReadThreadRoutine(PVOID context) {
	ResourceContext* ctx = (ResourceContext*)context;
	LARGE_INTEGER delay = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] ReadThread %d Is Waiting\n", ctx->threadIndex);

	ExAcquireResourceSharedLite(ctx->pResource, TRUE);

	DbgPrint("[W] ReadThread %d Is Working\n", ctx->threadIndex);

	DbgPrint("[W] shareValue Is %d\n", *ctx->sharePointer);

	delay.QuadPart = -2000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	ExReleaseResourceLite(ctx->pResource);
	DbgPrint("[W] ReadThread %d ExReleaseResourceLite Success\n", ctx->threadIndex);

	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

VOID ResourceWriteThreadRoutine(PVOID context) {
	ResourceContext* ctx = (ResourceContext*)context;
	LARGE_INTEGER delay = { 0 };
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] Thread %d Is Waiting\n", ctx->threadIndex);

	ExAcquireResourceExclusiveLite(ctx->pResource, TRUE);

	DbgPrint("[W] Thread %d Is Working\n", ctx->threadIndex);

	*ctx->sharePointer += 1;

	DbgPrint("[W] shareValue Is %d\n", *ctx->sharePointer);

	delay.QuadPart = -2000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	ExReleaseResourceLite(ctx->pResource);
	DbgPrint("[W] Thread %d ExReleaseResourceLite Success\n", ctx->threadIndex);

	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

NTSTATUS TestResourceLite() {
	NTSTATUS st = STATUS_SUCCESS;
	ERESOURCE resource = { 0 };
	HANDLE threadHandle[TEST_THREAD_COUNT + 1] = { 0 };
	ResourceContext* ctx[TEST_THREAD_COUNT + 1] = { 0 };
	LARGE_INTEGER timeout = { 0 };
	LONG shareValue = 0;
	OBJECT_ATTRIBUTES objAttr = { 0 };

	st = ExInitializeResourceLite(&resource);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ExInitializeResourceLite Failed->%lX\n", st);
		goto ret;
	}

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		ctx[i] = (ResourceContext*)ExAllocatePool(NonPagedPool, sizeof(ResourceContext));
		if (!ctx[i]) {
			st = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		ctx[i]->pResource = &resource;
		ctx[i]->threadIndex = i;
		ctx[i]->sharePointer = &shareValue;

		st = PsCreateSystemThread(
			&threadHandle[i],
			THREAD_ALL_ACCESS,
			&objAttr,
			NULL,
			NULL,
			ResourceWriteThreadRoutine,
			ctx[i]
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", i, st);
			goto ret;
		}
	}

	ctx[TEST_THREAD_COUNT] = (ResourceContext*)ExAllocatePool(NonPagedPool, sizeof(ResourceContext));
	if (!ctx[TEST_THREAD_COUNT]) {
		st = STATUS_INSUFFICIENT_RESOURCES;
		goto ret;
	}

	ctx[TEST_THREAD_COUNT]->pResource = &resource;
	ctx[TEST_THREAD_COUNT]->threadIndex = 91;
	ctx[TEST_THREAD_COUNT]->sharePointer = &shareValue;

	st = PsCreateSystemThread(
		&threadHandle[TEST_THREAD_COUNT],
		THREAD_ALL_ACCESS,
		&objAttr,
		NULL,
		NULL,
		ResourceReadThreadRoutine,
		ctx[TEST_THREAD_COUNT]
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", TEST_THREAD_COUNT, st);
		goto ret;
	}

ret:
	timeout.QuadPart = -100000000LL;
	for (int i = 0; i <= TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) {
			ZwWaitForSingleObject(threadHandle[i], FALSE, NULL);
		}
	}

	// Close handles and free contexts
	for (int i = 0; i <= TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) ZwClose(threadHandle[i]);
	}
	ExDeleteResourceLite(&resource);
	return st;
}

typedef struct {
	PKSPIN_LOCK pSpinLock;
	int threadIndex;
	PLONG sharePointer;
}SpinLockContext;

VOID SpinLockNormalRoutine(PVOID context) {
	SpinLockContext* ctx = (SpinLockContext*)context;
	LARGE_INTEGER delay = { 0 };
	KIRQL oldIrql = 0;
	NTSTATUS st = STATUS_SUCCESS;

	DbgPrint("[W] Thread %d Is Waiting\n", ctx->threadIndex);

	KeAcquireSpinLock(ctx->pSpinLock, &oldIrql);

	DbgPrint("[W] Thread %d Is Working old IRQL:%d new IRQL:%d\n", ctx->threadIndex,oldIrql,KeGetCurrentIrql());

	*ctx->sharePointer += 1;

	DbgPrint("[W] shareValue Is %d\n", *ctx->sharePointer);

	KeReleaseSpinLock(ctx->pSpinLock, oldIrql);

	delay.QuadPart = -2000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	DbgPrint("[W] Thread %d KeReleaseSpinLock Success\n", ctx->threadIndex);

	ExFreePool(ctx);
	PsTerminateSystemThread(st);
	return;
}

NTSTATUS TestSpinlockNormal() {
	OBJECT_ATTRIBUTES objAttr = { 0 };
	HANDLE threadHandle[TEST_THREAD_COUNT] = { 0 };
	LARGE_INTEGER timeout = { 0 };
	LONG shareValue = 0;
	NTSTATUS st = STATUS_SUCCESS;
	KSPIN_LOCK kspinLock = { 0 };
	SpinLockContext* ctx[TEST_THREAD_COUNT] = { 0 };

	KeInitializeSpinLock(&kspinLock);
	
	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		ctx[i] = (SpinLockContext*)ExAllocatePool(NonPagedPool, sizeof(SpinLockContext));
		if (!ctx[i]) {
			st = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		ctx[i]->pSpinLock = &kspinLock;
		ctx[i]->threadIndex = i;
		ctx[i]->sharePointer = &shareValue;

		st = PsCreateSystemThread(
			&threadHandle[i],
			THREAD_ALL_ACCESS,
			&objAttr,
			NULL,
			NULL,
			SpinLockNormalRoutine,
			ctx[i]
		);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsCreateSystemThread%d Failed->%lX\n", i, st);
			goto ret;
		}
	}

ret:
	timeout.QuadPart = -100000000LL;
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) {
			ZwWaitForSingleObject(threadHandle[i], FALSE, NULL);
		}
	}

	// Close handles and free contexts
	for (int i = 0; i < TEST_THREAD_COUNT; i++) {
		if (threadHandle[i] != NULL) ZwClose(threadHandle[i]);
	}
	return st;
}

typedef struct {
	INT threadIndex;
	volatile LONG executeCount;
	KTIMER ktimer;
	KDPC kdpc;
}TimerContext;

VOID TimerDpcRoutine(KDPC* Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
	TimerContext* ctx = (TimerContext*)DeferredContext;
	KIRQL currentIrql = 0;
	LONG count = 0;

	count = InterlockedIncrement(&ctx->executeCount);
	currentIrql = KeGetCurrentIrql();
	DbgPrint("[W] CurrentIrql->%d ExecuteCount->%d\n", currentIrql, count);
}

NTSTATUS TestTimerOneShot() {
	TimerContext ctx = { 0 };
	LARGE_INTEGER dueTime = { 0 };
	LARGE_INTEGER delay = { 0 };

	KeInitializeTimer(&ctx.ktimer);
	KeInitializeDpc(&ctx.kdpc, TimerDpcRoutine, &ctx);
	
	dueTime.QuadPart = -20000000LL;
	KeSetTimer(&ctx.ktimer, dueTime, &ctx.kdpc);

	delay.QuadPart = -100000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	KeCancelTimer(&ctx.ktimer);
	DbgPrint("[W] ExecuteCount->%d\n", ctx.executeCount);

	return STATUS_SUCCESS;
}

NTSTATUS TestTimerPeriod() {
	TimerContext ctx = { 0 };
	LARGE_INTEGER dueTime = { 0 };
	LARGE_INTEGER delay = { 0 };

	KeInitializeTimer(&ctx.ktimer);
	KeInitializeDpc(&ctx.kdpc, TimerDpcRoutine, &ctx);

	dueTime.QuadPart = -20000000LL;
	KeSetTimerEx(&ctx.ktimer, dueTime, 1000, &ctx.kdpc);

	delay.QuadPart = -100000000LL;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	KeCancelTimer(&ctx.ktimer);
	DbgPrint("[W] ExecuteCount->%d\n", ctx.executeCount);

	return STATUS_SUCCESS;
}

typedef enum _KAPC_ENVIRONMENT {
	OriginalApcEnvironment,
	AttachedApcEnvironment,
	CurrentApcEnvironment,
	InsertApcEnvironment
} KAPC_ENVIRONMENT;

typedef
VOID
(*PKNORMAL_ROUTINE) (
	IN PVOID NormalContext,
	IN PVOID SystemArgument1,
	IN PVOID SystemArgument2
	);

typedef
VOID
(*PKKERNEL_ROUTINE) (
	IN struct _KAPC* Apc,
	IN OUT PKNORMAL_ROUTINE* NormalRoutine,
	IN OUT PVOID* NormalContext,
	IN OUT PVOID* SystemArgument1,
	IN OUT PVOID* SystemArgument2
	);

typedef
VOID
(*PKRUNDOWN_ROUTINE) (
	IN struct _KAPC* Apc
	);

NTKERNELAPI
VOID
KeInitializeApc(
	__out PRKAPC Apc,
	__in PRKTHREAD Thread,
	__in KAPC_ENVIRONMENT Environment,
	__in PKKERNEL_ROUTINE KernelRoutine,
	__in_opt PKRUNDOWN_ROUTINE RundownRoutine,
	__in_opt PKNORMAL_ROUTINE NormalRoutine,
	__in_opt KPROCESSOR_MODE ApcMode,
	__in_opt PVOID NormalContext
);

NTKERNELAPI
BOOLEAN
KeInsertQueueApc(
	__inout PRKAPC Apc,
	__in_opt PVOID SystemArgument1,
	__in_opt PVOID SystemArgument2,
	__in KPRIORITY Increment
);

typedef struct {
	KAPC apc;
	KEVENT completeEvent;
}KernelApcContext;

VOID
KernelApcRoutine (
	IN struct _KAPC* Apc,
	IN OUT PKNORMAL_ROUTINE* NormalRoutine,
	IN OUT PVOID* NormalContext,
	IN OUT PVOID* SystemArgument1,
	IN OUT PVOID* SystemArgument2
) {

	KernelApcContext* ctx = CONTAINING_RECORD(Apc, KernelApcContext, apc);
	DbgPrint("[W] Apc Routine Is Working Tid:%d\n",PsGetCurrentThreadId());
	KeSetEvent(&ctx->completeEvent, IO_NO_INCREMENT, FALSE);
}

VOID KernelApcThreadRoutine(PVOID context) {
	LARGE_INTEGER delay = { 0 };

	DbgPrint("[W] Target Thread Is Starting\n");

	for (int i = 0; i < 10; i++) {
		DbgPrint("[W] Target Thread Is Working %d Tid %d\n", i, PsGetCurrentThreadId());
		delay.QuadPart = -10000000LL;
		KeDelayExecutionThread(KernelMode, FALSE, &delay);
	}

	DbgPrint("[W] Target Thread Is Ending\n");

	PsTerminateSystemThread(STATUS_SUCCESS);
	return;
}

NTSTATUS TestKernelApc() {
	HANDLE threadHandle = NULL;
	NTSTATUS st = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES objAttr = { 0 };
	LARGE_INTEGER timeout = { 0 };
	BOOLEAN inserted = FALSE;
	PETHREAD threadObject = NULL;
	KernelApcContext ctx = { 0 };

	KeInitializeEvent(&ctx.completeEvent, NotificationEvent, FALSE);

	InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	st = PsCreateSystemThread(
		&threadHandle,
		THREAD_ALL_ACCESS,
		&objAttr,
		NULL,
		NULL,
		KernelApcThreadRoutine,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] PsCreateSystemThread Failed->%lX\n", st);
		goto ret;
	}

	st = ObReferenceObjectByHandle(
		threadHandle,
		THREAD_ALL_ACCESS,
		*PsThreadType,
		KernelMode,
		&threadObject,
		NULL
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[W] ObReferenceObjectByHandle Failed->%lX\n", st);
		goto ret;
	}

	KeInitializeApc(
		&ctx.apc,
		threadObject,
		OriginalApcEnvironment,
		KernelApcRoutine,
		NULL,
		NULL,
		KernelMode,
		NULL
	);

	inserted = KeInsertQueueApc(
		&ctx.apc,
		NULL,
		NULL,
		IO_NO_INCREMENT
	);

	if (inserted) {
		DbgPrint("[W] Apc Insert Success\n");
		timeout.QuadPart = -200000000LL;
		st = KeWaitForSingleObject(
			&ctx.completeEvent,
			Executive,
			KernelMode,
			FALSE,
			&timeout
		);
		if (NT_SUCCESS(st)) DbgPrint("[W] Apc Is Running\n");
	}

ret:
	if (threadObject) ObDereferenceObject(threadObject);
	if (threadHandle) {
		timeout.QuadPart = -200000000LL;
		ZwWaitForSingleObject(threadHandle, FALSE, &timeout);
		ZwClose(threadHandle);
	}
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
	//TestAttachThread();
	//TestEventNotification();
	//TestEventSynchronization();
	//TestSemaphore();
	//TestMutex();
	//TestResourceLite();
	//TestSpinlockNormal();
	//TestTimerOneShot();
	//TestTimerPeriod();
	TestKernelApc();
	return st;
}