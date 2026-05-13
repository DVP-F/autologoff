// Build (MSVC):
//   cl monitor_idle_sessions.c /link wtsapi32.lib user32.lib

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <wtsapi32.h>
#include <string.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")

#define MAX_INACTVE_MINUTES 2

void monitor_sessions(DWORD sessionId, size_t flag) {
	LPWSTR userName = NULL;
	DWORD bytes = 0;
	if (!WTSQuerySessionInformationW( WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &userName, &bytes )) {
		return;
	}
	if (!userName || userName[0] == L'\0') {
		if (userName) WTSFreeMemory(userName);
		return;
	}
	DWORD idleMs = 0;
	DWORD idleBytes = 0;
	if (WTSQuerySessionInformationW( WTS_CURRENT_SERVER_HANDLE, sessionId, WTSIdleTime, (LPWSTR*)&idleMs, &idleBytes )) {
		ULONGLONG idleMinutes = idleMs / 60000ULL;
		char command[64];
		snprintf(command, sizeof(command), "logoff %lu", (unsigned long)sessionId);
		if (flag == 0) {
			if (idleMinutes >= MAX_INACTVE_MINUTES) {
				system(command);
			}
		}
		else {
			printf(idleMinutes);
			system("query user");
		}
		WTSFreeMemory(&idleMs);
	}
	WTSFreeMemory(userName);
}

// global stop event (optional but correct)
HANDLE gStopEvent = NULL;

DWORD WINAPI WorkerThread(LPVOID param) {
	while (1) {
		PWTS_SESSION_INFOW pSessions = NULL;
		DWORD count = 0;
		if (WTSEnumerateSessionsW( WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessions, &count )) {
			for (DWORD i = 0; i < count; ++i) {
				if (pSessions[i].State == WTSActive || pSessions[i].State == WTSDisconnected) {
					monitor_sessions(pSessions[i].SessionId, 0);
				}
			}
			WTSFreeMemory(pSessions);
		}
		Sleep(5000);
	}
	return 0;
}

SERVICE_STATUS gStatus;
SERVICE_STATUS_HANDLE gStatusHandle;

void SetStatus(DWORD state) {
	gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gStatus.dwCurrentState = state;
	gStatus.dwControlsAccepted = 0;
	SetServiceStatus(gStatusHandle, &gStatus);
}

void WINAPI ServiceCtrlHandler(DWORD ctrl) {
	if (ctrl == SERVICE_CONTROL_STOP) {
		SetStatus(SERVICE_STOPPED);
		ExitProcess(0);
	}
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
	gStatusHandle = RegisterServiceCtrlHandlerW(L"ALO", ServiceCtrlHandler);
	SetStatus(SERVICE_START_PENDING);
	gStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
	SetStatus(SERVICE_RUNNING);
	Sleep(INFINITE);
}

int wmain(int argc, wchar_t* argv[]) {
	if (argc == 1) {
		SERVICE_TABLE_ENTRYW table[] = {
			{ L"ALO", ServiceMain }, 
			{ NULL, NULL }
		};
		StartServiceCtrlDispatcherW(table);
	}
	else {
		if (wcscmp(argv[1], L"-") == 0) {
			while (1) {
				PWTS_SESSION_INFOW pSessions = NULL;
				DWORD count = 0;
				if (WTSEnumerateSessionsW( WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessions, &count )) {
					for (DWORD i = 0; i < count; ++i) {
						if (pSessions[i].State == WTSActive || pSessions[i].State == WTSDisconnected) {
							monitor_sessions(pSessions[i].SessionId, 1);
						}
					}
					WTSFreeMemory(pSessions);
				}
				Sleep(5000);
			}
		}
	}
	return 0;
}
