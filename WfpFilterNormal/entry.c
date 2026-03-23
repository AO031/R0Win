#define NDIS_SUPPORT_NDIS6 1
#include <ntifs.h>
#include <ntstrsafe.h>
#define INITGUID
#include <guiddef.h>
#include <fwpsk.h>
#include <fwpmk.h>
#include <fwpvi.h>

#pragma comment(lib,"uuid.lib")

// =============================================================
#define DEVICE_NAME L"\\Device\\WfpNetFilter"
#define SYMBOLIC_NAME L"\\DosDevices\\WfpNetFilter"
// {86F9AAAE-D722-4141-9D47-E595BF702F09}
DEFINE_GUID(WFP_SANPLE_CALLOUT_V4,0x86f9aaae, 0xd722, 0x4141, 0x9d, 0x47, 0xe5, 0x95, 0xbf, 0x70, 0x2f, 0x9);

// =============================================================
typedef struct _STAT_FILTER {
	ULONG totalConnections;
	ULONG allowedConnections;
	ULONG blockedConnections;
	ULONG icmpBlocked;
	ULONG processBlocked;
	ULONG portBlocked;
}STAT_FILTER,*PSTAT_FILTER;

typedef struct _CONNECT_INFO {
	UINT32 remoteIp;
	UINT16 remotePort;
	UINT16 localPort;
	UINT16 protocol;
	UINT32 processId;
	WCHAR processName[260];
}CONNECT_INFO,*PCONNECT_INFO;

// =============================================================
PDEVICE_OBJECT g_deviceObj = NULL;
HANDLE g_engineHandle = NULL;
UINT32 g_calloutId = 0;
UINT64 g_filterId = 0;
STAT_FILTER g_stat = { 0 };

USHORT g_blockedPorts[] = { 8080,3389 };
#define BLOCKED_PORT_COUNT sizeof(g_blockedPorts) / sizeof(USHORT)

// =============================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath);
VOID DriverUnload(PDRIVER_OBJECT driverObj);
NTSTATUS DeviceCreate(PDRIVER_OBJECT driverObj);
VOID DeviceDelete();
NTSTATUS DispatchPassThough(PDEVICE_OBJECT deviceObj, PIRP irp);
VOID ExtractProcessName(PWCHAR fullPath, PWCHAR processName, SIZE_T maxLength);
BOOLEAN IsProcessBlocked(PWCHAR processPath);

NTSTATUS WfpStartup();
VOID WfpShutdown();
NTSTATUS WfpRegisterCallout(PDEVICE_OBJECT deviceObj);
NTSTATUS WfpAddFilter();
VOID WfpCalloutClassifyFn(const FWPS_INCOMING_VALUES0* inFixedValues,const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,void* layerData,const void* classifyContext,const FWPS_FILTER3* filter,UINT64 flowContext,FWPS_CLASSIFY_OUT0* classifyOut);
NTSTATUS WfpCalloutNotifyFn(FWPS_CALLOUT_NOTIFY_TYPE notifyType,const GUID* filterKey,FWPS_FILTER3* filter);
VOID WfpCalloutFlowDeleteNotifyFn(UINT16 layerId,UINT32 calloutId,UINT64 flowContext);

// =============================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObj, PUNICODE_STRING regPath) {
	NTSTATUS st = STATUS_SUCCESS;

	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		driverObj->MajorFunction[i] = DispatchPassThough;
	}

	st = DeviceCreate(driverObj);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] DeviceCreate Failed->%lX\n", st);
		return st;
	}

	st = WfpStartup();
	if (!NT_SUCCESS(st)) {
		DeviceDelete();
		DbgPrint("[E] WfpStartup Failed->%lX\n", st);
		return st;
	}

	DbgPrint("[W] Net Filter Start\n");

	driverObj->DriverUnload = DriverUnload;

	return st;
}

VOID DriverUnload(PDRIVER_OBJECT driverObj) {
	WfpShutdown();
	DeviceDelete();
}

NTSTATUS DeviceCreate(PDRIVER_OBJECT driverObj) {
	NTSTATUS st = STATUS_SUCCESS;
	UNICODE_STRING deviceName = { 0 };
	UNICODE_STRING symbolicName = { 0 };

	RtlInitUnicodeString(&deviceName, DEVICE_NAME);
	RtlInitUnicodeString(&symbolicName, SYMBOLIC_NAME);

	st = IoCreateDevice(
		driverObj,
		0,
		&deviceName,
		FILE_DEVICE_NETWORK,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&g_deviceObj
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] IoCreateDevice Failed->%lX\n", st);
		return st;
	}

	st = IoCreateSymbolicLink(
		&symbolicName,
		&deviceName
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] IoCreateSymbolicLink Failed->%lX\n", st);
		IoDeleteDevice(g_deviceObj);
		g_deviceObj = NULL;
		return st;
	}

	DbgPrint("[W] DeviceCreate Success\n");
	return st;
}

NTSTATUS DispatchPassThough(PDEVICE_OBJECT deviceObj, PIRP irp) {
	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

VOID DeviceDelete() {
	UNICODE_STRING symbolicName = { 0 };
	if (g_deviceObj) {
		RtlInitUnicodeString(&symbolicName, SYMBOLIC_NAME);

		IoDeleteSymbolicLink(&symbolicName);
		IoDeleteDevice(g_deviceObj);

		g_deviceObj = NULL;
		DbgPrint("[W] DeviceDelete Success\n");
	}
}

NTSTATUS WfpStartup() {
	NTSTATUS st = STATUS_SUCCESS;
	DbgPrint("[W] WfpStartup Starting...\n");

	st = WfpRegisterCallout(g_deviceObj);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] WfpRegisterCallout Failed->%lX\n", st);
		return st;
	}

	st = WfpAddFilter();
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] WfpAddFilter Failed->%lX\n", st);
		FwpsCalloutUnregisterById(g_calloutId);
		return st;
	}
	DbgPrint("[W] WfpStartup Started...\n");
	return st;
}

VOID WfpShutdown() {
	DbgPrint("[W] WfpShutdown Start\n");

	if (g_engineHandle) {
		if (g_filterId) {
			FwpmFilterDeleteById(g_engineHandle, g_filterId);
			g_filterId = 0;
		}

		if (g_calloutId) FwpmCalloutDeleteById(g_engineHandle,g_calloutId);

		FwpmEngineClose(g_engineHandle);
	}

	if (g_calloutId) FwpsCalloutUnregisterById(g_calloutId);

	DbgPrint("[W] WfpShutdown End\n");
}

NTSTATUS WfpRegisterCallout(PDEVICE_OBJECT deviceObj) {
	NTSTATUS st = STATUS_SUCCESS;
	FWPS_CALLOUT callout = { 0 };

#if (NTDDI_VERSION >= NTDDI_WIN8)
	callout.flags = 0;
#endif
	callout.calloutKey = WFP_SANPLE_CALLOUT_V4;
	
	callout.classifyFn = WfpCalloutClassifyFn;
	callout.notifyFn = WfpCalloutNotifyFn;
	callout.flowDeleteFn = WfpCalloutFlowDeleteNotifyFn;

	st = FwpsCalloutRegister(deviceObj, &callout, &g_calloutId);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] FwpsCalloutRegister Failed->%lX\n", st);
		return st;
	}

	DbgPrint("[W] Callout Register Id -> %u\n", g_calloutId);
	return st;
}

NTSTATUS WfpAddFilter() {
	NTSTATUS st = STATUS_SUCCESS;
	FWPM_SESSION session = { 0 };
	FWPM_CALLOUT callout = { 0 };
	FWPM_FILTER filter = { 0 };

	session.flags = FWPM_SESSION_FLAG_DYNAMIC;

	st = FwpmEngineOpen(
		NULL,
		RPC_C_AUTHN_WINNT,
		NULL,
		&session,
		&g_engineHandle
	);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] FwpmEngineOpen Failed->%lX\n", st);
		return st;
	}

	st = FwpmTransactionBegin(g_engineHandle, 0);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] FwpmTransactionBegin Failed->%lX\n", st);
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = 0;
		return st;
	}

	callout.calloutKey = WFP_SANPLE_CALLOUT_V4;
	callout.displayData.name = L"WFP Network Filter Callout";
	callout.displayData.description = L"Net Filter By ao31";
	callout.applicableLayer = FWPM_LAYER_ALE_AUTH_CONNECT_V4;

	st = FwpmCalloutAdd(g_engineHandle, &callout, NULL, NULL);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] FwpmCalloutAdd Failed->%lX\n", st);
		FwpmTransactionAbort(g_engineHandle);
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = 0;
		return st;
	}

	filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
	filter.displayData.name = L"WFP Network Filter";
	filter.displayData.description = L"Filter outbound connection";
	filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
	filter.action.calloutKey = WFP_SANPLE_CALLOUT_V4;
	filter.subLayerKey = FWPM_SUBLAYER_UNIVERSAL;
	filter.weight.type = FWP_EMPTY;
	filter.numFilterConditions = 0;

	st = FwpmFilterAdd(g_engineHandle, &filter, NULL, &g_filterId);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] FwpmFilterAdd Failed->%lX\n", st);
		FwpmTransactionAbort(g_engineHandle);
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = 0;
		return st;
	}

	st = FwpmTransactionCommit(g_engineHandle);
	if (!NT_SUCCESS(st)) {
		DbgPrint("[E] FwpmTransactionCommit Failed->%lX\n", st);
		FwpmTransactionAbort(g_engineHandle);
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = 0;
		return st;
	}

	DbgPrint("[W] WfpAddFilter Success\n");
	return st;
}

VOID WfpCalloutClassifyFn(const FWPS_INCOMING_VALUES0* inFixedValues, const FWPS_INCOMING_METADATA_VALUES0* inMetaValues, void* layerData, const void* classifyContext, const FWPS_FILTER3* filter, UINT64 flowContext, FWPS_CLASSIFY_OUT0* classifyOut) {
	CONNECT_INFO conInfo = { 0 };
	PWCHAR processPath = NULL;

	g_stat.totalConnections++;

	conInfo.remoteIp = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_REMOTE_ADDRESS].value.uint32;
	conInfo.remotePort = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_REMOTE_PORT].value.uint16;
	conInfo.localPort = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_LOCAL_PORT].value.uint16;
	conInfo.protocol = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_PROTOCOL].value.uint32;

	conInfo.processId = inMetaValues->processId;
	processPath = (PWCHAR)(inMetaValues->processPath ? inMetaValues->processPath->data : NULL);

	classifyOut->actionType = FWP_ACTION_PERMIT;

	ExtractProcessName(processPath, conInfo.processName, 260);

	if (IsProcessBlocked(processPath)) {
		classifyOut->actionType = FWP_ACTION_BLOCK;
		classifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
		return;
	}

	g_stat.allowedConnections++;
}

NTSTATUS WfpCalloutNotifyFn(FWPS_CALLOUT_NOTIFY_TYPE notifyType, const GUID* filterKey, FWPS_FILTER3* filter) {
	return STATUS_SUCCESS;
}

VOID WfpCalloutFlowDeleteNotifyFn(UINT16 layerId, UINT32 calloutId, UINT64 flowContext) {

}

VOID ExtractProcessName(PWCHAR fullPath, PWCHAR processName, SIZE_T maxLength) {
	SIZE_T len = 0;
	SIZE_T startPos = 0;
	
	if (!fullPath || !processName || !maxLength) {
		if (processName && maxLength > 0) processName[0] = L'\0';
		return;
	}

	len = wcslen(fullPath);
	for (int i = len; i > 0; i--) {
		if (fullPath[i - 1] == L"\\" || fullPath[i - 1] == L'/') {
			startPos = i;
			break;
		}
	}

	wcscpy_s(processName, maxLength, &fullPath[startPos]);
}

BOOLEAN IsProcessBlocked(PWCHAR processPath)
{
	if (processPath == NULL) return FALSE;

	if (wcsstr(processPath, L"iexplore")) return TRUE;
	return FALSE;
}
