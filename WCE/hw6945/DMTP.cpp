// ----------------------------------------------------------------------------
// Copyright 2006-2007, Martin D. Flynn
// All rights reserved
// ----------------------------------------------------------------------------
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// ----------------------------------------------------------------------------
// Change History:
//  2007/01/28  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h"
#include "custom/defaults.h"

#include <aygshell.h>
#include <windows.h>
#include <windowsx.h>

#include "custom/gps.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "base/propman.h"
#include "base/events.h"

#include "DMTP.h"

// ----------------------------------------------------------------------------

#define MAX_LOADSTRING  100
#define ID_EDIT         1000
#define MENU_HEIGHT     26

// ----------------------------------------------------------------------------

// Global Variables:
static HINSTANCE        hInst = 0;			// The current instance
static HWND             hwndListBox = 0;
static HWND             hwndMain = 0;

// ----------------------------------------------------------------------------

// Foward declarations of functions included in this code module:
ATOM MyRegisterClass(HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ShowStatusInfo(HWND hwnd);

int startupDMTP(int argc, char *argv[], int runInThread);

// ----------------------------------------------------------------------------

#define MAX_ARGC    30

int WINAPI WinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{

    /* parse command line */
    char *argv[MAX_ARGC], *wceCmdLine = (char*)lpCmdLine, wceLineStr[512];
    int argc = 0, wceLineLen = 0;
    argv[argc++] = APPLICATION_NAME;
    for (; *wceCmdLine && (wceLineLen < (sizeof(wceLineStr) - 1));) {
        while (*wceCmdLine == ' ') { wceCmdLine += 2; } // skip prefixing spaces
        if (!*wceCmdLine) { break; } // end of line
        if (argc >= MAX_ARGC) { break; } // maximum args
        argv[argc++] = &wceLineStr[wceLineLen]; // next arg
        for (; *wceCmdLine && (wceLineLen < (sizeof(wceLineStr) - 1));) {
            if (*wceCmdLine == '\\') { // literal
                wceCmdLine += 2;  // wide characters
            } else
            if (*wceCmdLine == ' ') {
                break; // end of argument
            }
            if (*wceCmdLine) {
                wceLineStr[wceLineLen++] = *wceCmdLine;
                wceCmdLine += 2; // wide characters
            }
        }
        wceLineStr[wceLineLen++] = 0;
    }

    /* start DMTP */
    if (startupDMTP(argc,argv,TRUE) != 0) {
        return FALSE;
    }

	/* Perform application initialization */
	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}
	
	/* Main message loop */
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
	}
	
	return msg.wParam;
}

// Register the window class.
// COMMENTS:
//    This function and its usage is only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
ATOM MyRegisterClass(HINSTANCE hInstance, LPTSTR szWindowClass)
{
	WNDCLASS	wc;
	wc.style			= CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc		= (WNDPROC) WndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= 0;
	wc.hInstance		= hInstance;
	wc.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OPENDMTP));
	wc.hCursor			= 0;
	wc.hbrBackground	= (HBRUSH) GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName		= 0;
	wc.lpszClassName	= szWindowClass;
	return RegisterClass(&wc);
}

// Save instance handle and creates main window
// COMMENTS:
//    In this function, we save the instance handle in a global variable and
//    create and display the main program window.
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	
    // Store instance handle in our global variable
	hInst = hInstance;		
    
	/* Window class */
	TCHAR szWindowClass[MAX_LOADSTRING];		// The window class name
	LoadString(hInstance, IDC_OPENDMTP, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance, szWindowClass);
	
    /* app title */
	TCHAR szTitle[MAX_LOADSTRING];			// The title bar text
    const char *version = "OpenDMTP " RELEASE_VERSION; // propGetString(PROP_STATE_FIRMWARE, "OpenDMTP");
    strWideCopy(szTitle, MAX_LOADSTRING, version, -1);
	//LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

	//If it is already running, then focus on the window
	HWND hExistingWnd = FindWindow(szWindowClass, szTitle);	
	if (hExistingWnd) {
		// set focus to foremost child window
		// The "| 0x01" is used to bring any owned windows to the foreground and
		// activate them.
        ShowStatusInfo(hwndListBox);
		SetForegroundWindow((HWND)((ULONG)hExistingWnd | 0x00000001));
        UpdateWindow(hExistingWnd);
		return 0;
	} 

	////////// Setting default main window size
	// This technique allows for you to create the main
	// window to allow for the postion of a menubar and/or
	// the SIP button at the bottom of the screen
	
	//Set default window creation sizd info
	SIPINFO si = {0};
	si.cbSize = sizeof(si);
	SHSipInfo(SPI_GETSIPINFO, 0, &si, 0);
	//Consider the menu at the bottom, please.
	int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	int iDelta = (si.fdwFlags & SIPF_ON) ? 0 : MENU_HEIGHT;
	int cx = si.rcVisibleDesktop.right - si.rcVisibleDesktop.left;
	int cy = si.rcVisibleDesktop.bottom - si.rcVisibleDesktop.top - iDelta;
	hwndMain = CreateWindow(szWindowClass, szTitle, WS_VISIBLE ,
		x, y, cx, cy, NULL, NULL, hInstance, NULL);
	if (!hwndMain) {	
		return FALSE;
	}
	
    /* show window */
	ShowWindow(hwndMain, nCmdShow);
	UpdateWindow(hwndMain);
	
	return TRUE;
}

// Processe messages for the main window.
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT rc;
	
	switch (message) {
        
		case WM_CREATE: 
			//Create a MenuBar
			SHMENUBARINFO mbi;
			memset(&mbi, 0, sizeof(SHMENUBARINFO));
			mbi.cbSize     = sizeof(SHMENUBARINFO);
			mbi.hwndParent = hWnd;
			mbi.nToolBarId = IDM_MAIN_MENU;
			mbi.hInstRes   = hInst;
			mbi.nBmpId     = 0;
			mbi.cBmpImages = 0;	
			SHCreateMenuBar(&mbi);
			if (!SHCreateMenuBar(&mbi))  {
				MessageBox(hWnd, L"SHCreateMenuBar Failed", L"Error", MB_OK);
			}
			//Create Edit control
			GetClientRect(hWnd, &rc);
			hwndListBox = CreateWindow(_T("listbox") , NULL, WS_CHILD | WS_VSCROLL | WS_HSCROLL, 
			rc.left, rc.top, rc.right, rc.bottom , 
			hWnd, (HMENU)ID_EDIT, hInst, NULL);
			if (hwndListBox){
				ShowStatusInfo(hwndListBox);
				ShowWindow(hwndListBox, SW_SHOW);
				UpdateWindow(hWnd);
			}
            break;
			
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
            
		case WM_COMMAND:
			if (LOWORD(wParam) == IDM_ITEM_QUIT) {
				DestroyWindow(hWnd);
			} else
			if (LOWORD(wParam) == IDM_ITEM_REFRESH) {
				ShowStatusInfo(hwndListBox);
				UpdateWindow(hWnd);
			}
			break;
            
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
            
	}
    
	return 0;
}

// ----------------------------------------------------------------------------

static TCHAR *DIRECTION[] = { L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW" };

void ShowStatusInfo(HWND hwnd) 
{
    YMDHMS_t yh;
	TCHAR szbufW[MAX_PATH];

    if (!hwnd) {
        return;
    }
    
    /* need to clear what's here */
    SendMessage(hwnd, LB_RESETCONTENT, 0L, 0L);

    /* header */
    wsprintf(szbufW, _T("OpenDMTP Status:"));
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);

    /* blank line */
    wsprintf(szbufW, _T(" "));
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);

    /* aquire latest GPS */
    GPS_t gps;
    gpsAquire(&gps, 0L);
    utBool hasGPS = ((gps.point.latitude != 0.0) || (gps.point.longitude != 0.0))? utTrue : utFalse;
    
    /* last fix time */
    if (gps.fixtime > 0L) {
        TIME_ZONE_INFORMATION tz;
        memset(&tz, 0, sizeof(tz));
        DWORD rtn = GetTimeZoneInformation(&tz);
        UInt32 bias = tz.Bias + ((rtn == TIME_ZONE_ID_DAYLIGHT)? tz.DaylightBias : tz.StandardBias);
        UInt32 fixtime = gps.fixtime - (bias * 60L);
        WCHAR *tzName = (rtn == TIME_ZONE_ID_DAYLIGHT)? tz.DaylightName : tz.StandardName;
        utcSecondsToYmdHms(&yh, fixtime);
        wsprintf(szbufW, _T("FixTime:  %02d/%02d/%02d %02d:%02d:%02d"), 
            yh.wYear, yh.wMonth , yh.wDay,
            yh.wHour, yh.wMinute, yh.wSecond);
    } else {
        wsprintf(szbufW, _T("FixTime:  n/a"));
    }
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);
    
    /* latitude/longitude */
    if (hasGPS) {
        wsprintf(szbufW, _T("Lat/Lon:  %.4lf / %.4lf"), gps.point.latitude, gps.point.longitude);
    } else {
        wsprintf(szbufW, _T("Lat/Lon:  n/a"));
    }
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);

    /* speed/heading */
    if (hasGPS) {
        double minKPH = propGetDouble(PROP_GPS_MIN_SPEED, 10.0);
        double mph = (gps.speedKPH >= minKPH)? (gps.speedKPH * MILES_PER_KILOMETER) : 0.0;
        const TCHAR *hd = (mph > 0.0)? DIRECTION[(int)((gps.heading / 45.0) + 0.5) % 8] : L"";
        wsprintf(szbufW, _T("Speed  :  %.1lf mph  %s"), mph, hd);
    } else {
        wsprintf(szbufW, _T("Speed  :  n/a"));
    }
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);
    
    /* altitude */
    if (hasGPS) {
        double feet = gps.altitude * FEET_PER_METER;
        wsprintf(szbufW, _T("Altitude:  %.0lf ft"), feet);
    } else {
        wsprintf(szbufW, _T("Altitude:  n/a"));
    }
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);

    /* blank line */
    wsprintf(szbufW, _T(" "));
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);

    /* GPS diagnostics? */

    /* pending event count */
    Int32 queuedEvents = evGetPacketCount();
    Int32 totalEvents = evGetTotalPacketCount();
    wsprintf(szbufW, _T("Events  :  %ld / %ld"), queuedEvents, totalEvents);
    SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)(LPTSTR)szbufW);

    /* last connection time */
    
}
