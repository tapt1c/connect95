#ifndef CONNECTXP_TEST_WINDOWS_H
#define CONNECTXP_TEST_WINDOWS_H
/* Minimal host-test API for ui_paint.c/ui_layout.c; never used by the EXE. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef int BOOL;
typedef unsigned int UINT;
typedef uint32_t DWORD;
typedef uintptr_t UINT_PTR, DWORD_PTR, WPARAM;
typedef intptr_t LPARAM, LRESULT;
typedef void *HWND, *HDC, *HBITMAP, *HGDIOBJ, *HBRUSH, *HDWP, *HMONITOR;
typedef struct { int x, y; } POINT;
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct {
    UINT CtlType, CtlID, itemID, itemAction, itemState;
    HWND hwndItem; HDC hDC; RECT rcItem; uintptr_t itemData;
} DRAWITEMSTRUCT;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; } PAINTSTRUCT;
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define WM_ERASEBKGND 0x14
#define WM_PAINT 0x0f
#define WM_PRINTCLIENT 0x318
#define WM_DRAWITEM 0x2b
#define WM_QUERYUISTATE 0x129
#define BM_GETSTATE 0xf2
#define BST_PUSHED 4
#define BST_FOCUS 8
#define ODT_BUTTON 4
#define ODA_DRAWENTIRE 1
#define ODS_SELECTED 1
#define ODS_DISABLED 4
#define ODS_FOCUS 16
#define ODS_NOACCEL 0x100
#define ODS_NOFOCUSRECT 0x200
#define UISF_HIDEFOCUS 1
#define UISF_HIDEACCEL 2
#define COLOR_WINDOW 5
#define SRCCOPY 0xcc0020
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_NOREDRAW 0x0008
#define SWP_NOCOPYBITS 0x0100
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define RDW_INVALIDATE 0x0001
#define RDW_ERASE 0x0004
#define RDW_ALLCHILDREN 0x0080
#define RDW_UPDATENOW 0x0100
#define MONITOR_DEFAULTTOPRIMARY 1
#define SPI_GETWORKAREA 48
#define SW_RESTORE 9
#define SW_SHOW 5
#define ZeroMemory(p,n) memset(p,0,n)
HDC CreateCompatibleDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC,int,int);
HGDIOBJ SelectObject(HDC,HGDIOBJ);
BOOL DeleteDC(HDC);
BOOL DeleteObject(HGDIOBJ);
BOOL SetViewportOrgEx(HDC,int,int,POINT*);
HBRUSH GetSysColorBrush(int);
int FillRect(HDC,const RECT*,HBRUSH);
BOOL BitBlt(HDC,int,int,int,int,HDC,int,int,DWORD);
int SaveDC(HDC);
BOOL RestoreDC(HDC,int);
HDC BeginPaint(HWND,PAINTSTRUCT*);
BOOL EndPaint(HWND,const PAINTSTRUCT*);
HWND GetParent(HWND);
int GetDlgCtrlID(HWND);
BOOL GetClientRect(HWND,RECT*);
BOOL IsWindowEnabled(HWND);
LRESULT SendMessageW(HWND,UINT,WPARAM,LPARAM);
HDWP BeginDeferWindowPos(int);
HDWP DeferWindowPos(HDWP,HWND,HWND,int,int,int,int,UINT);
BOOL EndDeferWindowPos(HDWP);
BOOL SetWindowPos(HWND,HWND,int,int,int,int,UINT);
BOOL RedrawWindow(HWND,const RECT*,void*,UINT);
HMONITOR MonitorFromWindow(HWND,DWORD);
BOOL GetMonitorInfoW(HMONITOR,MONITORINFO*);
BOOL SystemParametersInfoW(UINT,UINT,void*,UINT);
BOOL IsIconic(HWND);
BOOL IsWindowVisible(HWND);
BOOL ShowWindow(HWND,int);
BOOL SetForegroundWindow(HWND);
#endif
