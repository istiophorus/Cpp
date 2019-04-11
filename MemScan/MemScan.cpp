// MemScan.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "resource.h"
#include "winuser.h"
#include "stdio.h"
#pragma warning(disable: 4786)
#include <map>

#define DEF_MAX_VALUE_LENGTH				16
#define DEF_MAX_PROCESS_ID_LENGTH			8 
#define DEF_TIMER_STOP						5
#define DEF_TIMER_ELAPSE					100

#define DEF_SHOW_RESULTS_LIMIT				1000
#define DEF_MEM_BUFFER_SIZE					0x10000
#define DEF_MAP_SIZE_MAX					16000000


#define DEF_CLOSE_HANDLE(hh) if (NULL != hh) { CloseHandle(hh); hh = NULL; };

BOOL CALLBACK WaitDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CenterWindow(HWND hWnd);

volatile bool stopFlag = false;
HINSTANCE hInst = NULL;
HANDLE hWorkerThread = NULL;
HANDLE hProcess = NULL;

BOOL bCheckGreater = FALSE;
BOOL bCheckEqual = FALSE;
BOOL bCheckLess = FALSE;

BYTE dataBufferEq[16] = { 0 };
BYTE dataBufferLt[16] = { 0 };
BYTE dataBufferGt[16] = { 0 };
int iDataLength = 0;

BOOL bInt32 = FALSE;
BOOL bInt64 = FALSE;
BOOL bInt8 = FALSE;
BOOL bInt16 = FALSE;
BOOL bFloat8 = FALSE;
BOOL bFloat4 = FALSE;

unsigned short usValueGt = 0;
unsigned __int64 ui64ValueGt = 0;
unsigned int uiValueGt = 0;
double dblValueGt = 0;
float floatValueGt = 0;
BYTE byteValueGt = 0;

unsigned short usValueLt = 0;
unsigned __int64 ui64ValueLt = 0;
unsigned int uiValueLt = 0;
double dblValueLt = 0;
float floatValueLt = 0;
BYTE byteValueLt = 0;

HWND hList = NULL;

std::map<DWORD, DWORD> vAddresses;


#define DEF_SCAN_VALUE(type, scan, varGt,varLt) 		type typeValue = 0; \
		iDataLength = sizeof(type);\
\
		if (bCheckEqual)\
		{\
			if (1 != sscanf(valueEq, scan, &typeValue))\
			{\
				return false;\
			};\
\
			memcpy(dataBufferEq, &typeValue, iDataLength);\
		};\
\
		if (bCheckGreater)\
		{\
			if (1 != sscanf(valueGt, scan, &typeValue))\
			{\
				return false;\
			};\
\
			varGt = typeValue;\
\
			memcpy(dataBufferGt, &typeValue, iDataLength);\
		};\
\
		if (bCheckLess)\
		{\
			if (1 != sscanf(valueLt, scan, &typeValue))\
			{\
				return false;\
			};\
\
			varLt = typeValue;\
\
			memcpy(dataBufferLt, &typeValue, iDataLength);\
		};\
\
		if (bCheckGreater && bCheckLess)\
		{\
			if (varGt > varLt)\
			{\
				return false;\
			};\
		}


#define DEF_CHECK_VALUE(type, varGt, varLt) type value = *((type*)&memBuffer[q]); \
\
					if (bCheckGreater) \
					{\
						if (varGt < value)\
						{\
							bAddGt = true;\
						};\
					}\
					else\
					{\
						bAddGt = true;\
					};\
\
					if (bCheckLess)\
					{\
						if (varLt > value)\
						{\
							bAddLt = true;\
						};\
					}\
					else\
					{\
						bAddLt = true;\
					};


//////////////////////////////////////////////////////////////////////////////////////
//

bool ScanMemoryBlock(DWORD baseAddress, DWORD size)
{
	static DWORD previousBaseAddress = 0;
	static DWORD previousScanned = 0;

	if (previousScanned + iDataLength == baseAddress)
	{
		size += baseAddress - previousScanned;
		baseAddress = previousScanned;
	};

	bool bRes = true;

	char memBuffer[DEF_MEM_BUFFER_SIZE];

	while (size > iDataLength)
	{
		DWORD cc = (size > DEF_MEM_BUFFER_SIZE) ? DEF_MEM_BUFFER_SIZE : size;
		SIZE_T br = 0;

		if (!ReadProcessMemory(hProcess, (void *)baseAddress, memBuffer, cc, &br))
		{
			break;
		};

		if (br != cc)
		{
			break;
		};

		for (int q = 0; q < cc - iDataLength + 1 && !stopFlag; q++)
		{
			bool bAddEq = false;
			bool bAddGt = false;
			bool bAddLt = false;

			if (bCheckEqual)
			{
				if (memcmp(&memBuffer[q], dataBufferEq, iDataLength) == 0)
				{
					bAddEq = true;
				};
			}
			else
			{
				if(bInt32)
				{
					DEF_CHECK_VALUE(unsigned int, uiValueGt, uiValueLt)
				}
				else if (bInt64)
				{
					DEF_CHECK_VALUE(unsigned __int64, ui64ValueGt, ui64ValueLt)
				}
				else if (bInt8)
				{
					DEF_CHECK_VALUE(unsigned char, byteValueGt, byteValueLt)
				}
				else if (bInt16)
				{
					DEF_CHECK_VALUE(unsigned short, usValueGt, usValueLt)
				}
				else if (bFloat8)
				{
					DEF_CHECK_VALUE(double, dblValueGt, dblValueLt)
				}
				else if (bFloat4)
				{
					DEF_CHECK_VALUE(float, floatValueGt, floatValueLt)
				};
			};

			previousScanned = baseAddress + q;

			if (bAddEq || (bAddGt && bAddLt))
			{
				DWORD dwAddress = baseAddress + q;

				if (vAddresses.find(dwAddress) == vAddresses.end())
				{
					vAddresses.insert(std::map<DWORD, DWORD>::value_type(dwAddress, dwAddress));

					if (vAddresses.size() > DEF_MAP_SIZE_MAX)
					{
						MessageBox(NULL, "Too many items found, scanning will be cancelled.\0", "Warning\0", MB_OK);
						bRes = false;
						break;
					};
				};
			};
		};


		baseAddress += (cc - iDataLength);
		size -= (cc - iDataLength);
	};

	previousBaseAddress = baseAddress;

	return bRes;
};

//////////////////////////////////////////////////////////////////////////////////////

ULONG __stdcall ScanWorkerThread(void *param)
{
	if (NULL == hProcess)
	{
		return -1;
	};

	SYSTEM_INFO si;

	GetSystemInfo(&si);

	DWORD minAddress = (DWORD)si.lpMinimumApplicationAddress;
	DWORD maxAddress = (DWORD)si.lpMaximumApplicationAddress;
	DWORD pageSize = si.dwPageSize;
	DWORD currentAddress = minAddress;

	while (!stopFlag)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQueryEx(hProcess, (void *)currentAddress, &mbi, sizeof(mbi)))
		{
			if (!ScanMemoryBlock((DWORD)mbi.BaseAddress, mbi.RegionSize))
			{
				break;
			};

			currentAddress += mbi.RegionSize;
		}
		else
		{
			currentAddress += pageSize;
		};

		currentAddress = currentAddress - (currentAddress % pageSize);

		if (currentAddress > maxAddress)
		{
			break;
		};
	};

	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////

void OnCommandStop(HWND hWnd)
{
	stopFlag = true;
}

////////////////////////////////////////////////////////////////////////////////////

void InitWaitDialog(HWND hWnd)
{
	CenterWindow(hWnd);

	SetTimer(hWnd, DEF_TIMER_STOP, DEF_TIMER_ELAPSE, NULL);
}

////////////////////////////////////////////////////////////////////////////////////

void FillList()
{
	if (NULL == hList)
	{
		return;
	};

	///////////////////////////////////////////////
	// clear the list

	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	int counter = 0;

	for (std::map<DWORD, DWORD>::iterator it = vAddresses.begin(); it != vAddresses.end(); it++, counter++)
	{
		if (counter > DEF_SHOW_RESULTS_LIMIT)
		{
			MessageBox(GetParent(hList), "Too many items to display\0", "Warning\0", MB_OK);
			return;
		};

		char buffer[1024];

		sprintf(buffer, "%08x %12d\0", it->first, it->first);

		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buffer); 
	};
};

////////////////////////////////////////////////////////////////////////////////////

BOOL CALLBACK WaitDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WORD cmdId = 0;

	switch (uMsg)
	{
	case WM_INITDIALOG:
		InitWaitDialog(hwndDlg);
		return TRUE;
	case WM_COMMAND:
		cmdId = LOWORD(wParam);
		switch (cmdId)
		{
		case IDC_BUTTON_STOP:
			OnCommandStop(hwndDlg);
			return TRUE;
		}
		return FALSE;
	case WM_TIMER:
		if (wParam != DEF_TIMER_STOP)
		{
			return FALSE;
		};

		if (WaitForSingleObject(hWorkerThread, 1) == WAIT_OBJECT_0)
		{
			DEF_CLOSE_HANDLE(hWorkerThread);

			EndDialog(hwndDlg, 0);

			FillList();
		}
		return TRUE;
	}

	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////////

void CenterWindow(HWND hWnd)
{
	////////////////////////////////////////////////////////////////////////////
	// set windows position

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	RECT re = { 0 };
	GetWindowRect(hWnd, &re);

	int windowWidth = re.right - re.left;
	int windowHeight = re.bottom - re.top;

	int posX = (screenWidth - windowWidth) / 2;
	int posY = (screenHeight - windowHeight) / 2;

	SetWindowPos(hWnd, NULL, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

///////////////////////////////////////////////////////////////////////
//

void ShowWaitDialog(HWND hWnd)
{
	DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG_WAIT), hWnd, WaitDialogProc);
}

///////////////////////////////////////////////////////////////////////
//


void SetEditMaxText(HWND hWnd, UINT idc, int maxText)
{
	HWND hDlgItem = GetDlgItem(hWnd, idc);

	if (hDlgItem)
	{
		SendMessage(hDlgItem, EM_SETLIMITTEXT, (WPARAM)maxText, 0);
	}
}


////////////////////////////////////////////////////////////////////////////////////////////
// init message handler

void InitDialog(HWND hWnd)
{
	CenterWindow(hWnd);

	///////////////////////////////////////////////////////////////////////////
	// set max text length

	SetEditMaxText(hWnd, IDC_EDIT_VALUE1, DEF_MAX_VALUE_LENGTH);

	SetEditMaxText(hWnd, IDC_EDIT_VALUE2, DEF_MAX_VALUE_LENGTH);

	SetEditMaxText(hWnd, IDC_EDIT_VALUE3, DEF_MAX_VALUE_LENGTH);

	SetEditMaxText(hWnd, IDC_EDIT_PROCESS_ID, DEF_MAX_PROCESS_ID_LENGTH);

	hList = GetDlgItem(hWnd, IDC_LIST_VALUES);
}

////////////////////////////////////////////////////////////////////////////////////////////

bool GetValues(HWND hWnd)
{
	HWND hDlgItem = GetDlgItem(hWnd, IDC_EDIT_PROCESS_ID);

	char buffer[1020] = { 0 };

	if (hDlgItem)
	{
		GetWindowText(hDlgItem, buffer, 1020);
	}
	else
	{
		MessageBox(hWnd, "Unexpected error [A]\0", "ERROR\0", MB_OK);
		return false;
	}

	hDlgItem = GetDlgItem(hWnd, IDC_RADIO_INT32);

	if (hDlgItem)
	{
		GetWindowText(hDlgItem, buffer, 1020);
	}
	else
	{
		MessageBox(hWnd, "Unexpected error [A]\0", "ERROR\0", MB_OK);
		return false;
	}


	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////
// get SCAN parameters

bool GetParameters(HWND hWnd)
{
	char valueEq[1024] = { 0 };
	char valueGt[1024] = { 0 };
	char valueLt[1024] = { 0 };

	///////////////////////////////////////////////////////////////////////
	// get values (as text)

	GetDlgItemText(hWnd, IDC_EDIT_VALUE1, valueEq, sizeof(valueEq) - 1);
	GetDlgItemText(hWnd, IDC_EDIT_VALUE2, valueGt, sizeof(valueGt) - 1);
	GetDlgItemText(hWnd, IDC_EDIT_VALUE3, valueLt, sizeof(valueLt) - 1);

	////////////////////////////////////////////////////////////////////////
	// get Compare options

	bCheckEqual = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_CHECK_EQUAL), BM_GETSTATE, 0, 0);
	bCheckGreater = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_CHECK_GREATER), BM_GETSTATE, 0, 0);
	bCheckLess =(BOOL)::SendMessage(GetDlgItem(hWnd, IDC_CHECK_LESS), BM_GETSTATE, 0, 0);


	if (bCheckEqual)
	{
		if (bCheckGreater || bCheckLess)
		{
			return false;
		};

		if (strlen(valueEq) <= 0)
		{
			return false;
		};
	}
	else
	{
		if (!bCheckGreater && !bCheckLess)
		{
			return false;
		};

		if (bCheckGreater)
		{
			if (strlen(valueGt) <= 0)
			{
				return false;
			};
		};

		if (bCheckLess)
		{
			if (strlen(valueLt) <= 0)
			{
				return false;
			};
		};
	};


	bInt32 = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_RADIO_INT32), BM_GETSTATE, 0, 0);
	bInt64 = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_RADIO_INT64), BM_GETSTATE, 0, 0);
	bInt8 = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_RADIO_INT08), BM_GETSTATE, 0, 0);
	bInt16 = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_RADIO_INT16), BM_GETSTATE, 0, 0);
	bFloat8 = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_RADIO_FLOAT8), BM_GETSTATE, 0, 0);
	bFloat4 = (BOOL)::SendMessage(GetDlgItem(hWnd, IDC_RADIO_FLOAT4), BM_GETSTATE, 0, 0);

	if (bInt32)
	{
		DEF_SCAN_VALUE(unsigned int, "%u\0", uiValueGt, uiValueLt)
	};

	if (bInt64)
	{
		DEF_SCAN_VALUE(unsigned __int64, "%I64u\0", ui64ValueGt, ui64ValueLt)
	};

	if (bInt16)
	{
		DEF_SCAN_VALUE(unsigned short, "%hu\0", usValueGt, usValueLt)
	};

	if (bInt8)
	{
		DEF_SCAN_VALUE(unsigned char, "%hi\0", byteValueGt, byteValueLt)
	};

	if (bFloat8)
	{
		DEF_SCAN_VALUE(double, "%lf\0", dblValueGt, dblValueLt)
	};

	if (bFloat4)
	{
		return false;
	};

	return (bInt32 || bInt64 || bInt8 || bInt16 || bFloat8 || bFloat4);
};

////////////////////////////////////////////////////////////////////////////////////////////
// SCAN command handler

void OnCommandScan(HWND hWnd)
{
	if (!GetValues(hWnd))
	{
		return;
	}

	if (NULL != hWorkerThread)
	{
		Beep(1000, 100);
		MessageBox(hWnd, "Worker thread is still running\0", "ERROR\0", MB_OK);
		return;
	};

	if (!GetParameters(hWnd))
	{
		MessageBox(hWnd, "Invalid options selected\0", "Error", MB_OK);
		return;
	};

	DWORD dwProcessId = 0;

	char value[1024] = { 0 };

	GetDlgItemText(hWnd, IDC_EDIT_PROCESS_ID, value, sizeof(value) - 1);

	dwProcessId = atoi(value);

	DEF_CLOSE_HANDLE(hProcess);

	vAddresses.clear();

	hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_WRITE, FALSE, dwProcessId);

	if (NULL == hProcess)
	{
		DWORD dwErr = GetLastError();

		char buff[4096] = { 0 };

		sprintf(buff, "Could not open process %u %x. Error code: %u %x\0", dwProcessId, dwProcessId, dwErr, dwErr);

		MessageBox(hWnd, buff, "Error", MB_OK | MB_ICONEXCLAMATION);

		return;
	};

	DWORD dwThreadId = 0;

	stopFlag = false;

	hWorkerThread = CreateThread(NULL, 0, ScanWorkerThread, NULL, 0, &dwThreadId);

	if (NULL == hWorkerThread)
	{
		MessageBox(hWnd, "Could not create worker thread\0", "ERROR\0", MB_OK);
		return;
	}

	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	ShowWaitDialog(hWnd);
}

////////////////////////////////////////////////////////////////////////////////////////////
// RESET command handler

void OnCommandReset(HWND hWnd)
{
	DEF_CLOSE_HANDLE(hProcess);

	vAddresses.clear();

	SendMessage(hList, LB_RESETCONTENT, 0, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////
// CHECK command handler

void OnCommandCheck(HWND hWnd)
{
}

////////////////////////////////////////////////////////////////////////////////////////////
// RESCAN command handler

void OnCommandRescan(HWND hWnd)
{
	if (!GetValues(hWnd))
	{
		return;
	}

	if (NULL != hWorkerThread)
	{
		Beep(1000, 100);
		MessageBox(hWnd, "Worker thread is still running\0", "ERROR\0", MB_OK);
		return;
	};

	if (!GetParameters(hWnd))
	{
		MessageBox(hWnd, "Invalid options selected\0", "Error", MB_OK);
		return;
	};

	DWORD dwProcessId = 0;

	char value[1024] = { 0 };

	GetDlgItemText(hWnd, IDC_EDIT_PROCESS_ID, value, sizeof(value) - 1);

	if (NULL == hProcess)
	{
		dwProcessId = atoi(value);

		hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_WRITE, FALSE, dwProcessId);
	};

	if (NULL == hProcess)
	{
		DWORD dwErr = GetLastError();

		char buff[4096] = { 0 };

		sprintf(buff, "Could not open process %u %x. Error code: %u %x\0", dwProcessId, dwProcessId, dwErr, dwErr);

		MessageBox(hWnd, buff, "Error", MB_OK | MB_ICONEXCLAMATION);

		return;
	};

	DWORD dwThreadId = 0;

	stopFlag = false;

	hWorkerThread = CreateThread(NULL, 0, ScanWorkerThread, NULL, 0, &dwThreadId);

	if (NULL == hWorkerThread)
	{
		MessageBox(hWnd, "Could not create worker thread\0", "ERROR\0", MB_OK);
		return;
	}

	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	ShowWaitDialog(hWnd);
}

////////////////////////////////////////////////////////////////////////////////////////////
// main dialog procedure

BOOL CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WORD cmdId = 0;

	switch (uMsg)
	{
	case WM_INITDIALOG:
		InitDialog(hwndDlg);
		return TRUE;
	case WM_COMMAND:
		cmdId = LOWORD(wParam);
		switch (cmdId)
		{
		case IDC_BUTTON_SCAN:
			OnCommandScan(hwndDlg);
			return TRUE;
		case IDC_BUTTON_CHECK:
			OnCommandCheck(hwndDlg);
			return TRUE;
		case IDC_BUTTON_RESET:
			OnCommandReset(hwndDlg);
			return TRUE;
		}
		return FALSE;
	case WM_CLOSE:
		EndDialog(hwndDlg, IDOK);
		return TRUE;
	}

	return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////////

void AdjustProcessPrivileges(HWND hWnd)
{
	DWORD access = TOKEN_ADJUST_PRIVILEGES | TOKEN_READ | TOKEN_WRITE | TOKEN_QUERY;
	HANDLE hToken = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), access, &hToken))
	{
		MessageBox(hWnd, "Error in OpenProcessToken.\0", "Error\0", MB_OK);
		return;
	};

	TOKEN_PRIVILEGES tkp; 

	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid))
	{
		MessageBox(hWnd, "Error in LookupPrivilegeValue.\0", "Error\0", MB_OK);
	};

	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; 

	BOOL res = AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
	if (!res)
	{
		MessageBox(hWnd, "Error in AdjustTokenPrivileges.\0", "Error\0", MB_OK);
	};

	LookupPrivilegeValue(NULL, SE_SECURITY_NAME, &tkp.Privileges[0].Luid); 
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; 

	BOOL resB = AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
	if (!resB)
	{
		MessageBox(hWnd, "Error in AdjustTokenPrivileges.\0", "Error\0", MB_OK);
	};

	CloseHandle(hToken);
	hToken = NULL;
};


////////////////////////////////////////////////////////////////////////////////////////////
// main application function

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{

	hInst = hInstance;

	AdjustProcessPrivileges(NULL);

	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, DialogProc);

	DEF_CLOSE_HANDLE(hProcess);

	DEF_CLOSE_HANDLE(hWorkerThread);

	return 0;
}

