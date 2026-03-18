#include <ntifs.h>
#include <ntstrsafe.h>

BOOLEAN g_registerProcessCallBack = FALSE;
BOOLEAN g_registerThreadCallBack = FALSE;
BOOLEAN g_registerImageCallBack = FALSE;
LARGE_INTEGER g_regCookie = { 0 };
PVOID g_regHandle = NULL;

NTKERNELAPI PCHAR PsGetProcessImageFileName(PEPROCESS process);

VOID ProcessNotifyRoutine(HANDLE ParentId, HANDLE ProcessId, BOOLEAN Create) {
	PEPROCESS processObj = NULL;
	PCHAR processName = NULL;
	NTSTATUS st = STATUS_SUCCESS;
	
	__try {
		st = PsLookupProcessByProcessId(ProcessId, &processObj);
		if (!NT_SUCCESS(st)) {
			DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
			goto cleanup;
		}

		processName = PsGetProcessImageFileName(processObj);
		if (processName) {
			if (Create) {
				DbgPrint("[W} Process Create Name->%s\n", processName);
			}
			else {
				DbgPrint("[W} Process Exit Name->%s\n", processName);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DbgPrint("[E] A Error Happened In ProcessNotifyRoutine\n");
		goto cleanup;
	}
cleanup:
	if (processObj) ObDereferenceObject(processObj);	
}

VOID ThreadNotifyRoutine(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create) {
	HANDLE currentProcessId = PsGetCurrentProcessId();

	if (Create) {
		DbgPrint("[W] Thread Create\n");
	}
	else {
		DbgPrint("[W] Thread Exit\n");
	}

	DbgPrint("[W] Current Process Id->0x%p\n", currentProcessId);
	DbgPrint("[W] Create Process Id->0x%p\n", ProcessId);
	DbgPrint("[w] Create Thread Id->0x%p\n", ThreadId);
}

VOID ImageNotifyRoutine(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo) {
	PCHAR processName = NULL;
	PEPROCESS processObj = NULL;
	NTSTATUS st = STATUS_SUCCESS;

	st = PsLookupProcessByProcessId(ProcessId, &processObj);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] PsLookupProcessByProcessId Failed->%lX\n", st);
		goto clean;
	}

	processName = PsGetProcessImageFileName(processObj);

	DbgPrint("[W] Process Name->%s\n", processName ? processName : "UNKNOWN");
	DbgPrint("[W] Full Image Name->%wZ\n", FullImageName);
	DbgPrint("[W] %s\n", ImageInfo->SystemModeImage ? "Kernel Model" : "User Model");
	DbgPrint("[W] Base Address->0x%p\n", ImageInfo->ImageBase);

clean:
	if (processObj) ObDereferenceObject(processObj);
}

OB_PREOP_CALLBACK_STATUS PreProcessOperationCallback(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION OperationInformation) {
	PCHAR processName = NULL;
	PEPROCESS currentProcess = PsGetCurrentProcess();

	if (OperationInformation->KernelHandle) return OB_PREOP_SUCCESS;
	
	processName = PsGetProcessImageFileName(OperationInformation->Object);
	if (!strstr(processName, "Notepad")) {
		if (currentProcess == OperationInformation->Object) return OB_PREOP_SUCCESS;
	
		if (OperationInformation->Operation == OB_OPERATION_HANDLE_CREATE) {
			OperationInformation->Parameters->CreateHandleInformation.DesiredAccess = 0;
		}
		else if (OperationInformation->Operation == OB_OPERATION_HANDLE_DUPLICATE) {
			OperationInformation->Parameters->DuplicateHandleInformation.DesiredAccess = 0;
		}
	}
	return OB_PREOP_SUCCESS;
}

NTSTATUS RegistNotifyRoutine(PVOID CallbackContext, PVOID Argument1, PVOID Argument2) {
	NTSTATUS st = STATUS_SUCCESS;
	REG_NOTIFY_CLASS notifyClass = (REG_NOTIFY_CLASS)Argument1;
	HANDLE currentPid = PsGetCurrentProcessId();

	switch (notifyClass) {
	case RegNtPreCreateKeyEx: {
		DbgPrint("[W] Current Pid->0x%p\n", currentPid);
		PREG_CREATE_KEY_INFORMATION createInfo = (PREG_CREATE_KEY_INFORMATION)Argument2;
		if (createInfo->CompleteName) {
			DbgPrint("[W] Create RegName->%wZ\n", createInfo->CompleteName);
		}
		break;
	}

	case RegNtPreDeleteKey: {
		DbgPrint("[W] Current Pid->0x%p\n", currentPid);
		PREG_DELETE_KEY_INFORMATION deleteInfo = (PREG_DELETE_KEY_INFORMATION)Argument2;
		if (deleteInfo && deleteInfo->Object) {
			POBJECT_NAME_INFORMATION nameInfo = NULL;
			ULONG returnLength = 0;

			ObQueryNameString(deleteInfo->Object, NULL, 0, &returnLength);
			if (returnLength > 0) {
				nameInfo = (POBJECT_NAME_INFORMATION)ExAllocatePool(NonPagedPool, returnLength);
				if (nameInfo) {
					st = ObQueryNameString(deleteInfo->Object, nameInfo, returnLength, &returnLength);
					if (NT_SUCCESS(st)) DbgPrint("[W] Delete Name->%wZ\n", &nameInfo->Name);
					ExFreePool(nameInfo);
				}
			}
		}
	}

	}
	return STATUS_SUCCESS;
}

VOID DriverUnload(PDRIVER_OBJECT driverObj) {
	NTSTATUS st = STATUS_SUCCESS;
	LARGE_INTEGER delay = { 0 };

	if (g_registerProcessCallBack) {
		st = PsSetCreateProcessNotifyRoutine(ProcessNotifyRoutine, TRUE);
		if (!NT_SUCCESS(st)) DbgPrint("[E] PsSetCreateProcessNotifyRoutine Failed->%lX\n", st);
	}

	if (g_registerThreadCallBack) {
		st = PsRemoveCreateThreadNotifyRoutine(ThreadNotifyRoutine);
		if (!NT_SUCCESS(st)) DbgPrint("[E] PsSetCreateThreadNotifyRoutine Failed->%lX\n", st);
	}

	if (g_registerImageCallBack) {
		st = PsRemoveLoadImageNotifyRoutine(ImageNotifyRoutine);
		if (!NT_SUCCESS(st)) DbgPrint("[E] PsRemoveLoadImageNotifyRoutine Failed->%lX\n", st);
	}
	
	if (g_regCookie.QuadPart) {
		st = CmUnRegisterCallback(g_regCookie);
		if (!NT_SUCCESS(st)) DbgPrint("[E] CmUnRegisterCallback Failed->%lX\n", st);
	}

	if (g_regHandle) {
		ObUnRegisterCallbacks(g_regHandle);
		if (!NT_SUCCESS(st)) DbgPrint("[E] ObUnRegisterCallbacks Failed->%lX\n", st);
	}
	
	delay.QuadPart = -10000000;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	driverObj->DriverUnload = DriverUnload;

	//st = PsSetCreateProcessNotifyRoutine(ProcessNotifyRoutine, FALSE);
	//if (!NT_SUCCESS(st)) {
	//	DbgPrint("[E] PsSetCreateProcessNotifyRoutine Failed->%lX\n", st);
	//	goto ret;
	//}
	//g_registerProcessCallBack = TRUE;

	//st = PsSetCreateThreadNotifyRoutine(ThreadNotifyRoutine);
	//if (!NT_SUCCESS(st)) {
	//	DbgPrint("[E] PsSetCreateThreadNotifyRoutine Failed->%lX\n", st);
	//	goto ret;
	//}
	//g_registerThreadCallBack = TRUE;

	//st = PsSetLoadImageNotifyRoutine(ImageNotifyRoutine);
	//if (!NT_SUCCESS(st)) {
	//	DbgPrint("[E] PsSetLoadImageNotifyRoutine Failed->%lX\n", st);
	//	goto ret;
	//}
	//g_registerImageCallBack = TRUE;
	
	//UNICODE_STRING altitude = { 0 };
	//RtlInitUnicodeString(&altitude, L"320000");
	//
	//st = CmRegisterCallbackEx(
	//	RegistNotifyRoutine,
	//	&altitude,
	//	driverObj,
	//	NULL,
	//	&g_regCookie,
	//	NULL
	//);
	//if (!NT_SUCCESS(st)) {
	//	DbgPrint("[E] CmRegisterCallbackEx Failed->%lX\n", st);
	//	goto ret;
	//}

	OB_CALLBACK_REGISTRATION callBackReg = { 0 };
	OB_OPERATION_REGISTRATION operationReg[1] = { 0 };

	RtlInitUnicodeString(&callBackReg.Altitude, L"321000");
	
	operationReg[0].ObjectType = PsProcessType;
	operationReg[0].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
	operationReg[0].PreOperation = PreProcessOperationCallback;
	operationReg[0].PostOperation = NULL;

	callBackReg.Version = OB_FLT_REGISTRATION_VERSION;
	callBackReg.OperationRegistrationCount = 1;
	callBackReg.RegistrationContext = NULL;
	callBackReg.OperationRegistration = &operationReg[0];

	st = ObRegisterCallbacks(&callBackReg, &g_regHandle);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] ObRegisterCallbacks Failed->%lX\n", st);
		goto ret;
	}

ret:
	return st;
}