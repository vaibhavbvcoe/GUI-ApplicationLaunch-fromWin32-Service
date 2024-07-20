#include <Windows.h>
#include <tchar.h>
#include<wtsapi32.h>
#pragma comment(lib,"Wtsapi32.lib")
#include<iostream>
using namespace std;
//#include<string.h>
#include<userenv.h>
#pragma comment(lib,"userenv.lib")
#include <tlhelp32.h>
#define NULL_TOKEN 0

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv);
VOID WINAPI ServiceCtrlHandler (DWORD);
DWORD WINAPI ServiceWorkerThread (LPVOID lpParam);

#define SERVICE_NAME  _T("Calc-Winservice")


BOOL LogError()
{	
	LPSTR messageBuffer = nullptr;
	DWORD dwerr = GetLastError();
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, dwerr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
	OutputDebugStringA(messageBuffer);
	return TRUE;
}
BOOL GetWinLogonPID( DWORD & dwWinLOGONPID)
{
	BOOL bRet = TRUE;
	HANDLE hProcessSnap = NULL;
	HANDLE hProcess = NULL;
	PROCESSENTRY32 pe32;
	DWORD dwPriorityClass = 0;

	// Take a snapshot of all processes in the system.
	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return bRet;
	}

	// Set the size of the structure before using it.
	pe32.dwSize = sizeof(PROCESSENTRY32);

	// Retrieve information about the first process,
	// and exit if unsuccessful
	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);          // clean the snapshot object
		return bRet;
	}

	// Now walk the snapshot of processes, and
	// display information about each process in turn
	do
	{
		// do something with the pe32 struct.
		// pe32.szExeFile -> path of the file

		LPWSTR filename  = pe32.szExeFile;
		if(_tcscmp(filename,_T("winlogon.exe"))==0)
		{
			DWORD dwSessionID = WTSGetActiveConsoleSessionId();  
			DWORD dwWinlogonSessionID=0; 
			ProcessIdToSessionId(pe32.th32ProcessID,&dwWinlogonSessionID);
			if(dwSessionID == dwWinlogonSessionID) 
			{ 
				dwWinLOGONPID =pe32.th32ProcessID; 
				break;
			}
		}

	} while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);

	bRet = ERROR_SUCCESS;

	return bRet ;
}

int _tmain (int argc, TCHAR *argv[])
{
	OutputDebugString(_T("Calc-Winservice: Main: Entry"));

	SERVICE_TABLE_ENTRY ServiceTable[] = 
	{
		{SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain}
	};

	if (StartServiceCtrlDispatcher (ServiceTable) == FALSE)
	{
		OutputDebugString(_T("Calc-Winservice: Main: StartServiceCtrlDispatcher returned error"));
		return GetLastError ();
	}

	OutputDebugString(_T("Calc-Winservice: Main: Exit"));
	return 0;
}


VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv)
{
	DWORD Status = E_FAIL;

	OutputDebugString(_T("Calc-Winservice: ServiceMain: Entry"));

	g_StatusHandle = RegisterServiceCtrlHandler (SERVICE_NAME, ServiceCtrlHandler);

	if (g_StatusHandle == NULL) 
	{
		OutputDebugString(_T("Calc-Winservice: ServiceMain: RegisterServiceCtrlHandler returned error"));
		goto EXIT;
	}

	// Tell the service controller we are starting
	ZeroMemory (&g_ServiceStatus, sizeof (g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE) 
	{
		OutputDebugString(_T("Calc-Winservice: ServiceMain: SetServiceStatus returned error"));
	}

	/* 
	* Perform tasks neccesary to start the service here
	*/
	OutputDebugString(_T("Calc-Winservice: ServiceMain: Performing Service Start Operations"));

	// Create stop event to wait on later.
	g_ServiceStopEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL) 
	{
		OutputDebugString(_T("Calc-Winservice: ServiceMain: CreateEvent(g_ServiceStopEvent) returned error"));

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T("Calc-Winservice: ServiceMain: SetServiceStatus returned error"));
		}
		goto EXIT; 
	}    

	// Tell the service controller we are started
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T("Calc-Winservice: ServiceMain: SetServiceStatus returned error"));
	}

	// Start the thread that will perform the main task of the service
	HANDLE hThread = CreateThread (NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

	OutputDebugString(_T("Calc-Winservice: ServiceMain: Waiting for Worker Thread to complete"));

	// Wait until our worker thread exits effectively signaling that the service needs to stop
	WaitForSingleObject (hThread, INFINITE);

	OutputDebugString(_T("Calc-Winservice: ServiceMain: Worker Thread Stop Event signaled"));


	/* 
	* Perform any cleanup tasks
	*/
	OutputDebugString(_T("Calc-Winservice: ServiceMain: Performing Cleanup Operations"));

	CloseHandle (g_ServiceStopEvent);

	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T("Calc-Winservice: ServiceMain: SetServiceStatus returned error"));
	}

EXIT:
	OutputDebugString(_T("Calc-Winservice: ServiceMain: Exit"));

	return;
}


VOID WINAPI ServiceCtrlHandler (DWORD CtrlCode)
{
	OutputDebugString(_T("Calc-Winservice: ServiceCtrlHandler: Entry"));

	switch (CtrlCode) 
	{
	case SERVICE_CONTROL_STOP :

		OutputDebugString(_T("Calc-Winservice: ServiceCtrlHandler: SERVICE_CONTROL_STOP Request"));

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;
		
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T("Calc-Winservice: ServiceCtrlHandler: SetServiceStatus returned error"));
		}

		// This will signal the worker thread to start shutting down
		SetEvent (g_ServiceStopEvent);

		break;

	default:
		break;
	}

	OutputDebugString(_T("Calc-Winservice: ServiceCtrlHandler: Exit"));
}
BOOL GetWinLogonToken(PHANDLE hWinlogonDupToken)
{
	DWORD dwWinlogonPID = 0;
	if(GetWinLogonPID(dwWinlogonPID) != ERROR_SUCCESS )
	{
		return LogError();
		
	}

	HANDLE hWinLOGON = OpenProcess(PROCESS_QUERY_INFORMATION,TRUE,dwWinlogonPID);
	if(hWinLOGON == INVALID_HANDLE_VALUE)
	{
		return LogError();
	}
	
	HANDLE hWinlogonPToken = 0;
	
	if(!OpenProcessToken(hWinLOGON,TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE | TOKEN_QUERY,&hWinlogonPToken))
	{
		CloseHandle(hWinLOGON);
		return LogError();
	}

	if(!DuplicateTokenEx(hWinlogonPToken, MAXIMUM_ALLOWED, 0, SecurityImpersonation, TokenPrimary,hWinlogonDupToken))
	{
		CloseHandle(hWinLOGON);
		CloseHandle(hWinlogonPToken); 
		return LogError();
	}
	return ERROR_SUCCESS;
}
//below code is not used any more.This is not a tested function
PHANDLE GetCurrentUserToken()
{
	PHANDLE currentToken = 0;
	PHANDLE primaryToken = 0;

	int dwSessionId = 0;
	PHANDLE hUserToken = 0;
	PHANDLE hTokenDup = 0;

	PWTS_SESSION_INFO pSessionInfo = 0;
	DWORD dwCount = 0;

	// Get the list of all terminal sessions    

	WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &dwCount);

	int dataSize = sizeof(WTS_SESSION_INFO);

	// look over obtained list in search of the active session
	for (DWORD i = 0; i < dwCount; ++i)
	{
		WTS_SESSION_INFO si = pSessionInfo[i];
		if (WTSActive == si.State)
		{
			// If the current session is active – store its ID
			dwSessionId = si.SessionId;
			break;
		}
	}

	// Get token of the logged in user by the active session ID
	BOOL bRet = WTSQueryUserToken(dwSessionId, currentToken);
	if (bRet == false)
	{
		return 0;
	}

	bRet = DuplicateTokenEx(currentToken, TOKEN_ASSIGN_PRIMARY | TOKEN_ALL_ACCESS, 0, SecurityImpersonation, TokenPrimary, primaryToken);
	if (bRet == false)
	{
		return 0;
	}
	return primaryToken;
}
BOOL Run(LPCWSTR applicationname, LPWSTR arguments)
{
	BOOL bResult = ERROR_SUCCESS;
	// Get token of the current user
	HANDLE hWinLogonToken = NULL;
	if( GetWinLogonToken(&hWinLogonToken) != ERROR_SUCCESS)
	{
		return LogError();
	}

	void* lpEnvironment = NULL;
	if(!CreateEnvironmentBlock(&lpEnvironment, hWinLogonToken,FALSE))
	{
		return LogError();
	}
	
	STARTUPINFO si = { sizeof (si) } ;
	PROCESS_INFORMATION pi = { } ;
	si.lpDesktop =TEXT("winsta0\\default");

	// Do NOT want to inherit handles here
	DWORD dwCreationFlags = CREATE_UNICODE_ENVIRONMENT  | CREATE_NEW_CONSOLE;
	if(	!CreateProcessAsUser(hWinLogonToken,
								applicationname,
								arguments,
								NULL,
								NULL,
								FALSE,
								dwCreationFlags, 
								lpEnvironment, 
								NULL, 
								&si, 
								&pi))
	{
		bResult = LogError();
	}
	DestroyEnvironmentBlock (lpEnvironment);
	CloseHandle(hWinLogonToken);
	return bResult;
}
DWORD WINAPI ServiceWorkerThread (LPVOID lpParam)
{
	OutputDebugString(_T("Calc-Winservice: ServiceWorkerThread: Entry"));

	//  Periodically check if the service has been requested to stop

	LPCWSTR appname= L"notepad.exe";
	LPWSTR arg = L"7sample.txt"; 
	Run(appname,arg);

	while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0)
	{     
		Sleep(5000);
		OutputDebugString(TEXT("I am in Service worker thread"));
	}

	OutputDebugString(_T("Calc-Winservice: ServiceWorkerThread: Exit"));

	return ERROR_SUCCESS;
}