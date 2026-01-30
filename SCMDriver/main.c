#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <Windows.h>

#define MAX_INPUT_LENGTH 256

typedef enum {
	MENU_EXIT = 0,
	MENU_CREATE,
	MENU_START,
	MENU_STOP,
	MENU_DELETE,
}MenuStatus;

typedef struct {
	char serviceName[MAX_PATH];
	char displayName[MAX_PATH];
	char driverPath[MAX_PATH];
	DWORD serviceType;
	DWORD startType;
}DriverLoadCfg;

DriverLoadCfg g_config = { 0 };

BOOL ReadIntInput(INT* menuchoice);
BOOL ReadUserInput(PCHAR buffer, int bufferSize);

BOOL CheckDrvierPath(const char* servicePath);

BOOL CreateDriverService();
BOOL StartDriverService();
BOOL StopDriverService();
BOOL DeleteDriverService();

VOID HandleCreateDriver(void);
VOID HandleStartDriver(void);
VOID HandleStopDriver(void);
VOID HandleDeleteDriver(void);

VOID ShowMainMenu();
VOID ShowBanner();

int main()  {
	int menuChoice = 0;
	BOOL continueRunning = 1;
	

	while (continueRunning) {
		system("cls");
		ShowBanner();
		ShowMainMenu();

		if (!ReadIntInput(&menuChoice)) {
			printf("Invalid Input\n");
			system("pause");
			continue;
		}

		switch (menuChoice) {
		case MENU_CREATE: {
			HandleCreateDriver();
			break;
		}
		case MENU_START: {
			HandleStartDriver();
			break;
		}
		case MENU_STOP: {
			HandleStopDriver();
			break;
		}
		case MENU_DELETE: {
			HandleDeleteDriver();
			break;
		}
		case MENU_EXIT: {
			continueRunning = FALSE;
			break;
		}
		default: {
			printf("Invalid Choice\n");
		}
		}
	}
	printf("Thanks For Using!@!\n");
}

BOOL ReadIntInput(INT* menuchoice)
{
	char buffer[MAX_INPUT_LENGTH] = { 0 };
	
	if (!ReadUserInput(buffer, MAX_INPUT_LENGTH)) return 0;
	PCHAR endPtr = NULL;
	int res = strtol(buffer, &endPtr, 10);

	if (*endPtr != '\0') return 0;
	*menuchoice = res;

	return TRUE;
}

BOOL ReadUserInput(PCHAR buffer, int bufferSize)
{
	if (!buffer || !bufferSize) return 0;

	if (!fgets(buffer, bufferSize, stdin)) return 0;
	
	int len = 0;
	len = strlen(buffer);
	if (len > 0 || buffer[len - 1] == '\n') buffer[len - 1] = '\0';

	return TRUE;
}

BOOL CheckDrvierPath(const char* servicePath)
{
	if (!servicePath || !strlen(servicePath)) return FALSE;
	printf(servicePath);
	DWORD attr = GetFileAttributesA(servicePath);

	if (attr == INVALID_FILE_ATTRIBUTES) {
		printf("INVALID_FILE_ATTRIBUTES error code :0x%08X\n",GetLastError());
		return FALSE;
	}
	if (attr == FILE_ATTRIBUTE_DIRECTORY) {
		printf("FILE_ATTRIBUTE_DIRECTORY error code :0x%08X\n", GetLastError());
		return FALSE;
	}

	return TRUE;
}

BOOL CreateDriverService()
{
	SC_HANDLE scManager = NULL;
	SC_HANDLE service = NULL;
	int res = 0;

	scManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (!scManager) {
		printf("OpenSCManager Failed\n");
		return FALSE;
	}

	service = OpenServiceA(scManager, g_config.serviceName, SERVICE_ALL_ACCESS);
	if (service) {
		printf("Service Is Already Exit\n");
		CloseServiceHandle(scManager);
		return FALSE;
	}

	service = CreateServiceA(
		scManager,
		g_config.serviceName,
		g_config.displayName,
		SERVICE_ALL_ACCESS,
		g_config.serviceType,
		g_config.startType,
		SERVICE_ERROR_NORMAL,
		g_config.driverPath,
		0, 0, 0, 0, 0
	);
	if (!service) {
		printf("CreateServiceA Failed 0x%08X\n",GetLastError());
		return FALSE;
	}

	CloseServiceHandle(service);
	CloseServiceHandle(scManager);
	return 0;
}

BOOL StartDriverService()
{
	SC_HANDLE scManager = 0;
	SC_HANDLE service = 0;
	SERVICE_STATUS serviceStatus = { 0 };
	scManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
	if (!scManager) {
		printf("OpenSCManagerA False\n");
		return 0;
	}

	service = OpenServiceA(scManager, g_config.serviceName, SERVICE_ALL_ACCESS);
	if (!service) {
		printf("OpenServiceA Failed\n");
		CloseServiceHandle(scManager);
		return 0;
	}

	if (!QueryServiceStatus(service, &serviceStatus)) {
		printf("QueryServiceStatus Failed\n");
		CloseServiceHandle(scManager);
		CloseServiceHandle(service);
		return 0;
	}

	if (serviceStatus.dwCurrentState == SERVICE_RUNNING) {
		printf("Service Is Already Running\n");
		CloseServiceHandle(scManager);
		CloseServiceHandle(service);
		return TRUE;
	}

	if (!StartServiceA(service, 0, NULL)) {
		printf("StartServiceA Failed 0x%08X\n",GetLastError());	
	}

	CloseServiceHandle(scManager);
	CloseServiceHandle(service);
	return TRUE;
}

BOOL StopDriverService()
{
	SC_HANDLE scManager = 0;
	SC_HANDLE service = 0;
	SERVICE_STATUS serviceStatus = { 0 };
	scManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
	if (!scManager) {
		printf("OpenSCManagerA False\n");
		return 0;
	}

	service = OpenServiceA(scManager, g_config.serviceName, SERVICE_ALL_ACCESS);
	if (!service) {
		printf("OpenServiceA Failed\n");
		CloseServiceHandle(scManager);
		return 0;
	}

	if (!QueryServiceStatus(service, &serviceStatus)) {
		printf("QueryServiceStatus Failed\n");
		CloseServiceHandle(scManager);
		CloseServiceHandle(service);
		return 0;
	}

	if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
		printf("Service Is Already Stop\n");
		CloseServiceHandle(scManager);
		CloseServiceHandle(service);
	}

	if (!ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus)) {
		printf("Failed To Stop Service\n");
	}

	CloseServiceHandle(scManager);
	CloseServiceHandle(service);

	return TRUE;
}

BOOL DeleteDriverService()
{
	SC_HANDLE scManager = 0;
	SC_HANDLE service = 0;
	SERVICE_STATUS serviceStatus = { 0 };
	scManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
	if (!scManager) {
		printf("OpenSCManagerA False\n");
		return 0;
	}

	service = OpenServiceA(scManager, g_config.serviceName, SERVICE_ALL_ACCESS);
	if (!service) {
		printf("OpenServiceA Failed\n");
		CloseServiceHandle(scManager);
		return 0;
	}

	if (!DeleteService(service)) {
		printf("Failed To Delete Service\n");
	}

	CloseServiceHandle(scManager);
	CloseServiceHandle(service);

	return TRUE;
}
 
VOID HandleCreateDriver(void)
{
	system("cls");
	ShowBanner();
	printf("\n");
	printf("Please Input Service Path:");
	char servicePath[MAX_PATH] = { 0 };
	if (!ReadUserInput(servicePath, MAX_PATH)) return;
	strcpy(g_config.driverPath, servicePath);
	sprintf(g_config.displayName, "%s Drvier", servicePath);
	g_config.startType = SERVICE_DEMAND_START;
	g_config.serviceType = SERVICE_KERNEL_DRIVER;
	strcpy(g_config.serviceName,"service");
	printf("\n");
	printf("Start To Create Service\n");

	if (!CheckDrvierPath(g_config.driverPath)) {
		printf("CheckDrvierPath\n");
		system("pause");
		return;
	}
	if (!CreateDriverService()) {
		printf("CreateDriverService\n");
		system("pause");
		return;
	}
	printf("Successful\n");
	system("pause");
}

VOID HandleStartDriver(void)
{
	system("cls");
	ShowBanner();
	if (!StartDriverService()) {
		printf("StartDriverService Failed\n");
		system("pause");
		return;
	}
	printf("Successful\n");
	system("pause");
}

VOID HandleStopDriver(void)
{
	system("cls");
	ShowBanner();
	if (!StopDriverService()) {
		printf("StopDriverService Failed\n");
		system("pause");
		return;
	}
	printf("Successful\n");
	system("pause");
}

VOID HandleDeleteDriver(void)
{
	system("cls");
	ShowBanner();
	if (!DeleteDriverService()) {
		printf("StopDriverService Failed\n");
		system("pause"); 
		return;
	}
	printf("Successful\n");
	system("pause");
}

VOID ShowMainMenu()
{
	printf("\n");
	printf("====================================================\n");
	printf("[1] Create Service\n");
	printf("[2] Start Service\n");
	printf("[3] Stop Service\n");
	printf("[4] Delete Service\n");
	printf("[0] Exit\n");
	printf("====================================================\n");
	printf("\nPlease Input:");
}

VOID ShowBanner()
{
	printf("\n");
	printf("====================================================\n");
	printf("Windows Driver Loader\n");
	printf("====================================================\n");
}
