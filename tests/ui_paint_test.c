#include "ui_paint.h"
#include "ui_layout.h"
#include <commctrl.h>
#include <assert.h>
#include <stdio.h>

#define HANDLE(n) ((void *)(uintptr_t)(n))
static SUBCLASSPROC buttonProc;
static int delegateCalls, dcFail, bitmapFail, liveDCs, liveBitmaps, drawn, committed;
static int expectBuffered, backgroundFilled, viewportX, viewportY, targetState = 17, savedState;
static int deferCalls, failDeferAt, fallbackMoves, ended, redrawn;
static UINT redrawFlags;
static UINT expectedItemState = 5;
static int expectedX = 7, expectedY = 9;
static int paintBegun, paintEnded;
static void draw(const DRAWITEMSTRUCT *item);

HDC BeginPaint(HWND window, PAINTSTRUCT *paint)
{ assert(window == HANDLE(9)); paint->hdc = HANDLE(1); ++paintBegun; return paint->hdc; }
BOOL EndPaint(HWND window, const PAINTSTRUCT *paint)
{ assert(window == HANDLE(9) && paint->hdc == HANDLE(1)); ++paintEnded; return TRUE; }
HWND GetParent(HWND window) { assert(window == HANDLE(9)); return HANDLE(11); }
int GetDlgCtrlID(HWND window) { assert(window == HANDLE(9)); return 210; }
BOOL GetClientRect(HWND window, RECT *rect)
{ assert(window == HANDLE(9)); *rect = (RECT){0, 0, 120, 32}; return TRUE; }
BOOL IsWindowEnabled(HWND window) { assert(window == HANDLE(9)); return FALSE; }
LRESULT SendMessageW(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == BM_GETSTATE) { assert(window == HANDLE(9)); return BST_PUSHED | BST_FOCUS; }
    if (message == WM_QUERYUISTATE) { assert(window == HANDLE(9)); return UISF_HIDEFOCUS; }
    assert(window == HANDLE(11) && message == WM_DRAWITEM && wParam == 210);
    assert(((DRAWITEMSTRUCT *)lParam)->CtlType == ODT_BUTTON);
    ui_draw_item((const DRAWITEMSTRUCT *)lParam, draw);
    return TRUE;
}

BOOL SetWindowSubclass(HWND window, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data)
{ (void)window; assert(id == 2 && data == 0); buttonProc = proc; return TRUE; }
LRESULT DefSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{ (void)window; (void)message; (void)wParam; (void)lParam; ++delegateCalls; return 456; }
HDC CreateCompatibleDC(HDC target)
{ assert(target == HANDLE(1)); if (dcFail) return NULL; ++liveDCs; return HANDLE(2); }
HBITMAP CreateCompatibleBitmap(HDC target, int width, int height)
{ assert(target == HANDLE(1) && width == 120 && height == 32); if (bitmapFail) return NULL; ++liveBitmaps; return HANDLE(3); }
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object)
{ assert(dc == HANDLE(2)); assert(object == HANDLE(3) || object == HANDLE(4)); return HANDLE(4); }
BOOL DeleteDC(HDC dc) { assert(dc == HANDLE(2)); --liveDCs; return TRUE; }
BOOL DeleteObject(HGDIOBJ object) { assert(object == HANDLE(3)); --liveBitmaps; return TRUE; }
BOOL SetViewportOrgEx(HDC dc, int x, int y, POINT *previous)
{ assert(dc == HANDLE(2) && !previous); viewportX = x; viewportY = y; return TRUE; }
HBRUSH GetSysColorBrush(int color) { assert(color == COLOR_WINDOW); return HANDLE(5); }
int FillRect(HDC dc, const RECT *rect, HBRUSH brush)
{ assert(dc == HANDLE(expectBuffered ? 2 : 1) && brush == HANDLE(5)); assert(rect->left == expectedX && rect->top == expectedY); backgroundFilled = TRUE; return 1; }
BOOL BitBlt(HDC target, int x, int y, int width, int height, HDC source, int sx, int sy, DWORD operation)
{
    assert(target == HANDLE(1) && source == HANDLE(2));
    assert(x == expectedX && y == expectedY && sx == expectedX && sy == expectedY && width == 120 && height == 32 && operation == SRCCOPY);
    assert(drawn && backgroundFilled && viewportX == -expectedX && viewportY == -expectedY);
    ++committed;
    return TRUE;
}
int SaveDC(HDC dc) { assert(dc == HANDLE(1)); savedState = targetState; return 3; }
BOOL RestoreDC(HDC dc, int state) { assert(dc == HANDLE(1) && state == 3); targetState = savedState; return TRUE; }

static void draw(const DRAWITEMSTRUCT *item)
{
    assert(item->hDC == HANDLE(expectBuffered ? 2 : 1));
    assert(item->CtlID == 210 && item->itemAction == 1 && item->itemState == expectedItemState);
    assert(item->hwndItem == HANDLE(9) && item->rcItem.left == expectedX && item->rcItem.top == expectedY);
    if (expectBuffered) assert(backgroundFilled && !committed);
    else targetState = 99;
    ++drawn;
}

static void flags(UINT value)
{
    assert((value & (SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS)) ==
        (SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS));
}
HDWP BeginDeferWindowPos(int count) { assert(count == 2); return HANDLE(6); }
HDWP DeferWindowPos(HDWP batch, HWND window, HWND after, int x, int y, int width, int height, UINT value)
{
    (void)window; (void)x; (void)y;
    assert(batch == HANDLE(6) && !after && width > 0 && height > 0); flags(value);
    ++deferCalls;
    return failDeferAt == deferCalls ? NULL : batch;
}
BOOL EndDeferWindowPos(HDWP batch) { assert(batch == HANDLE(6)); ++ended; return TRUE; }
BOOL SetWindowPos(HWND window, HWND after, int x, int y, int width, int height, UINT value)
{ (void)window; (void)x; (void)y; assert(!after && width > 0 && height > 0); flags(value); ++fallbackMoves; return TRUE; }
BOOL RedrawWindow(HWND window, const RECT *rect, void *region, UINT value)
{
    (void)window; assert(!rect && !region);
    assert(ended || fallbackMoves == 2); /* Painting follows the entire layout. */
    redrawFlags = value; ++redrawn;
    return TRUE;
}
HMONITOR MonitorFromWindow(HWND window, DWORD fallback) { (void)window; (void)fallback; return HANDLE(7); }
BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO *info)
{ assert(monitor == HANDLE(7)); info->rcWork = (RECT){0, 0, 800, 570}; return TRUE; }
BOOL SystemParametersInfoW(UINT action, UINT parameter, void *value, UINT flagsValue)
{ (void)action; (void)parameter; (void)value; (void)flagsValue; return FALSE; }

int main(void)
{
    DRAWITEMSTRUCT item = {0};
    UiPlacement positions[] = {{HANDLE(9), {10, 20, 120, 32}, 1}, {HANDLE(10), {20, 60, 0, 0}, 0}};
    int i;
    assert(ui_owner_draw(HANDLE(9)) && buttonProc);
    assert(buttonProc(HANDLE(9), WM_ERASEBKGND, 0, 0, 2, 0) == 1 && !delegateCalls);
    assert(buttonProc(HANDLE(9), 0x100, 0, 0, 2, 0) == 456 && delegateCalls == 1);
    expectedItemState = ODS_SELECTED | ODS_DISABLED | ODS_FOCUS | ODS_NOFOCUSRECT;
    expectedX = expectedY = 0;
    expectBuffered = TRUE;
    assert(buttonProc(HANDLE(9), WM_PAINT, 0, 0, 2, 0) == 0);
    assert(paintBegun == 1 && paintEnded == 1 && committed == 1 && delegateCalls == 1);
    drawn = committed = backgroundFilled = 0;
    assert(buttonProc(HANDLE(9), WM_PRINTCLIENT, (WPARAM)HANDLE(1), 0, 2, 0) == 0);
    assert(paintBegun == 1 && committed == 1 && delegateCalls == 1);
    expectedItemState = 5;
    expectedX = 7; expectedY = 9;
    item.CtlID = 210; item.itemAction = 1; item.itemState = 5;
    item.hwndItem = HANDLE(9); item.hDC = HANDLE(1); item.rcItem = (RECT){7, 9, 127, 41};
    expectBuffered = TRUE;
    for (i = 0; i < 1000; ++i) {
        drawn = committed = backgroundFilled = 0;
        ui_draw_item(&item, draw);
        assert(drawn == 1 && committed == 1 && !liveDCs && !liveBitmaps && targetState == 17);
    }
    expectBuffered = FALSE; bitmapFail = TRUE; drawn = committed = 0;
    ui_draw_item(&item, draw);
    assert(drawn == 1 && !committed && !liveDCs && !liveBitmaps && targetState == 17);
    bitmapFail = FALSE; dcFail = TRUE; drawn = 0;
    ui_draw_item(&item, draw);
    assert(drawn == 1 && !liveDCs && targetState == 17);
    for (i = 0; i < 2; ++i) {
        deferCalls = fallbackMoves = ended = redrawn = 0;
        failDeferAt = i ? 2 : 0;
        ui_apply_layout(HANDLE(11), positions, 2);
        assert(redrawn == 1 && (redrawFlags & RDW_UPDATENOW) && (redrawFlags & RDW_ALLCHILDREN));
        assert(i ? fallbackMoves == 2 && !ended : ended == 1 && !fallbackMoves);
    }
    puts("UI paint checks passed (mock Win32): suppressed erase, buffered full frames, GDI cleanup, allocation fallbacks and synchronous redraw");
    return 0;
}
