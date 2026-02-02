#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Windows.h>

#define DEVICE_NAME			"\\\\.\\MyFirstDevice"

#define IOCTL_SEND_DATA		CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_RECEIVE_DATA	CTL_CODE(FILE_DEVICE_UNKNOWN,0x801,METHOD_BUFFERED,FILE_ANY_ACCESS)

typedef struct {
	HANDLE DeviceHandle;
	CHAR SendBuffer[0xFF];
	CHAR ReceiveBuffer[0xFF];
	ULONG byteReturn;
}AppContext;

BOOL CreateDevice(AppContext* ctx) {
	if (!ctx) {
		printf("[-] Wrong Param\n");
		return FALSE;
	}

	HANDLE hDevice = CreateFileA(
		DEVICE_NAME,
		GENERIC_READ | GENERIC_WRITE,
		0,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		0
	);
	if (hDevice == INVALID_HANDLE_VALUE) {
		ctx->DeviceHandle = hDevice;
		printf("[-] CreateFileA Failed\n");
		return FALSE;
	}

	ctx->DeviceHandle = hDevice;
}

VOID CloseDevice(AppContext* ctx) {
	if (!ctx || ctx->DeviceHandle == INVALID_HANDLE_VALUE) {
		printf("[-] Wrong Param\n");
		return;
	}
	CloseHandle(ctx->DeviceHandle);
}

BOOL SendData(AppContext* ctx) {
	if (!ctx) {
		return FALSE;
	}

	if (!DeviceIoControl(
		ctx->DeviceHandle,
		IOCTL_SEND_DATA,
		ctx->SendBuffer,
		0xFF,
		NULL,
		0,
		&ctx->byteReturn,
		NULL
	)) {
		printf("[-] DeviceIoControl Failed->0x%08X\n", GetLastError());
		return FALSE;
	}

	return TRUE;
}

BOOL ReceiveData(AppContext* ctx) {
	if (!ctx) return FALSE;

	if (!DeviceIoControl(
		ctx->DeviceHandle,
		IOCTL_RECEIVE_DATA,
		NULL,
		0,
		ctx->ReceiveBuffer,
		0xFF,
		&ctx->byteReturn,
		NULL
	)) {
		printf("[-] DeviceIoControl Failed->0x%08X\n",GetLastError());
		return FALSE;
	}

	return TRUE;
}

int main() {
	CHAR IoMessage[] = "Hello Io Device Control\n";
	AppContext ctx = { 0 };

	if (!CreateDevice(&ctx) || ctx.DeviceHandle == INVALID_HANDLE_VALUE || ctx.DeviceHandle == 0) {
		printf("[-] CreateDevice Failed\n");
		return 1;
	}

	printf("[+] CreateDevice Success\n");
	
	ZeroMemory(ctx.SendBuffer, 0xFF);
	CopyMemory(ctx.SendBuffer, IoMessage, strlen(IoMessage) + 1);

	if (!SendData(&ctx)) {
		printf("[-] SendData Failed\n");
		system("pause");
		return 1;
	}

	if (!ReceiveData(&ctx)) {
		printf("[-] ReceiveData Failed\n");
		system("pause");
		return 1;
	}

	printf("byteReturn->%d\n", ctx.byteReturn);
	printf("%s",ctx.ReceiveBuffer);
	printf("[+] Test DeviceIoControl Success\n");

	CloseDevice(&ctx);
	printf("[+] CloseDevice\n");

	system("pause");
	return 0;
}