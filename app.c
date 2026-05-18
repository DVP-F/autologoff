// Build (MSVC):
//   cl app.c /link advapi32.lib user32.lib wtsapi32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <powrprof.h>
#include <stdio.h>
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "advapi32.lib")

#define SERVICE_NAME "GuestLidLogoffService"

// GUID_LIDSWITCH_STATE_CHANGE (from powrprof.h / winuser.h / winnt.h)
// If available, use the definition from the header instead of hard-coding.
// static const GUID GUID_LIDSWITCH_STATE_CHANGE =
// 	{0x31cb1e01, 0x305e, 0x4579,
// 	{0xa5, 0xc9, 0x0c, 0x2f, 0x8b, 0x9d, 0x6b, 0x3c}};

static SERVICE_STATUS        gSvcStatus;
static SERVICE_STATUS_HANDLE gSvcStatusHandle;
static HPOWERNOTIFY          gPowerNotify = NULL;
static HWND                  gHwnd = NULL;

static void WINAPI ServiceMain(int argc, char *argv[]);
static void WINAPI HandlerEx(DWORD ctrl, DWORD eventType, LPVOID eventData, LPVOID context);
static DWORD ParseQuery(void);
static void ProcessPowerMessages(void);
static void LogoffGuestSession(void);
static BOOL InitPowerNotification(void);
static void CleanupPowerNotification(void);

// Service entry table
SERVICE_TABLE_ENTRY ServiceTable[] = {
	{ SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
	{ NULL, NULL }
};

int main(void) {
	if (!StartServiceCtrlDispatcher(ServiceTable)) {
		fprintf(stderr, "StartServiceCtrlDispatcher failed: %lu\n", GetLastError());
		return 1;
	}
	return 0;
}

static void WINAPI ServiceMain(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	gSvcStatusHandle = RegisterServiceCtrlHandlerExA(SERVICE_NAME, (LPHANDLER_FUNCTION_EX)HandlerEx, NULL );
	if (!gSvcStatusHandle) {
		fprintf(stderr, "RegisterServiceCtrlHandlerEx failed: %lu\n", GetLastError());
		return;
	}
	ZeroMemory(&gSvcStatus, sizeof(gSvcStatus));
	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gSvcStatus.dwCurrentState = SERVICE_START_PENDING;
	// Accept session change (lock/unlock/logon/logoff)
	gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_SESSIONCHANGE;
	gSvcStatus.dwWin32ExitCode = NO_ERROR;
	gSvcStatus.dwServiceSpecificExitCode = 0;
	gSvcStatus.dwCheckPoint = 0;
	gSvcStatus.dwWaitHint = 3000;
	if (!SetServiceStatus(gSvcStatusHandle, &gSvcStatus)) {
		fprintf(stderr, "SetServiceStatus (start pending) failed: %lu\n", GetLastError());
		return;
	}
	// Create a hidden message window to receive power notifications
	WNDCLASSA wc = {0};
	wc.lpfnWndProc   = DefWindowProcA;
	wc.lpszClassName = "GuestLidLogoffNotifyWnd";
	wc.hInstance     = GetModuleHandleA(NULL);
	if (!RegisterClassA(&wc)) {
		fprintf(stderr, "RegisterClass failed: %lu\n", GetLastError());
		return;
	}
	// Message-only window
	gHwnd = CreateWindowExA( 0, "GuestLidLogoffNotifyWnd", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL );
	if (!gHwnd) {
		fprintf(stderr, "CreateWindowEx for power notify failed: %lu\n", GetLastError());
		return;
	}
	if (!InitPowerNotification()) {
		fprintf(stderr, "InitPowerNotification failed\n");
		// Continue anyway; we still get lock via session change
	}
	gSvcStatus.dwCurrentState = SERVICE_RUNNING;
	gSvcStatus.dwCheckPoint = 0;
	gSvcStatus.dwWaitHint = 0;
	if (!SetServiceStatus(gSvcStatusHandle, &gSvcStatus)) {
		fprintf(stderr, "SetServiceStatus (running) failed: %lu\n", GetLastError());
		return;
	}
	// Service main loop
	while (gSvcStatus.dwCurrentState == SERVICE_RUNNING) {
		ProcessPowerMessages();
		Sleep(1000);
	}
	CleanupPowerNotification();
}

static void WINAPI HandlerEx(DWORD ctrl, DWORD eventType, LPVOID eventData, LPVOID context) {
	(void)eventData;
	(void)context;
	if (ctrl == SERVICE_CONTROL_SESSIONCHANGE) {
		// eventType is one of the WTS_SESSION_* constants
		if (eventType == WTS_SESSION_LOCK) {
			DWORD sessionId = *(DWORD *)eventData;  // lParam in WM_WTSSESSION_CHANGE
			(void)sessionId; // we log off the known guest session anyway
			fprintf(stdout, "Session locked (WTS_SESSION_LOCK), logging off guest session %d\n", ParseQuery());
			LogoffGuestSession();
		}
	}
}

// Initialize power notification for lid switch
static BOOL InitPowerNotification(void) {
	// Register for power setting notifications via a message window
	gPowerNotify = RegisterPowerSettingNotification( gHwnd, &GUID_LIDSWITCH_STATE_CHANGE, DEVICE_NOTIFY_WINDOW_HANDLE );
	if (!gPowerNotify) {
		fprintf(stderr, "RegisterPowerSettingNotification failed: %lu\n", GetLastError());
		return FALSE;
	}
	return TRUE;
}

static void CleanupPowerNotification(void) {
	if (gPowerNotify) {
		UnregisterPowerSettingNotification(gPowerNotify);
		gPowerNotify = NULL;
	}
	if (gHwnd) {
		DestroyWindow(gHwnd);
		gHwnd = NULL;
	}
}

// Handle power messages in the message loop (if needed)
// For a service, we can also use a simple dedicated thread with GetMessage/DispatchMessage
// Here we integrate into the main loop by checking power messages.
static void ProcessPowerMessages(void) {
	MSG msg;
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_POWERBROADCAST) {
			POWERBROADCAST_SETTING *pbs = (POWERBROADCAST_SETTING *)msg.lParam;
			if (!pbs) continue;
			if (!IsEqualGUID(&pbs->PowerSetting, &GUID_LIDSWITCH_STATE_CHANGE))
				continue;
			DWORD lidState = *((DWORD *)pbs->Data);
			// 0 = closed, 1 = open
			if (lidState == 0) {
				fprintf(stdout, "Lid closed (GUID_LIDSWITCH_STATE_CHANGE), logging off guest session %d\n", ParseQuery());
				LogoffGuestSession();
			}
		}
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
}

static DWORD ParseQuery(void) {
    FILE *pipe;
    char line[512];
    pipe = _popen("query user", "r");
    if (!pipe) {
        return 1;
    }
    while (fgets(line, sizeof(line), pipe)) {
        // Skip header
        if (strstr(line, "USERNAME"))
            continue;
		// Only grab the active user
		if (strstr(line, "Active")) {
			// Tokenize line
			char *token = strtok(line, " \t\r\n");
			while (token) {
				// Skip leading '>'
				if (token[0] == '>')
					token++;
				char *end;
				DWORD sessionId = (DWORD)strtoul(token, &end, 10);
				// If fully numeric, we found the ID
				if (*token != '\0' && *end == '\0') {
					return sessionId;
				}
				token = strtok(NULL, " \t\r\n");
			}
		}
    }
    _pclose(pipe);
	return (DWORD)-1;
}

static void LogoffGuestSession(void) {
	DWORD sessionId = ParseQuery();
	if (sessionId == (DWORD)-1) {
		fprintf(stderr, "Guest session not found\n");
		return;
	}
	if (!WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, sessionId, FALSE)) {
		fprintf(stderr, "WTSLogoffSession failed: %lu\n", GetLastError());
		return;
	}
	fprintf(stdout, "Logged off Guest session %lu\n", sessionId);
}
