#include "ui_paint.h"
#include <commctrl.h>

static void paint_button(HWND window, HDC dc)
{
    DRAWITEMSTRUCT item;
    LRESULT state = SendMessageW(window, BM_GETSTATE, 0, 0);
    LRESULT cues = SendMessageW(window, WM_QUERYUISTATE, 0, 0);
    ZeroMemory(&item, sizeof(item));
    item.CtlType = ODT_BUTTON;
    item.CtlID = (UINT)GetDlgCtrlID(window);
    item.itemAction = ODA_DRAWENTIRE;
    item.itemState = ((state & BST_PUSHED) ? ODS_SELECTED : 0) |
        ((state & BST_FOCUS) ? ODS_FOCUS : 0) | (!IsWindowEnabled(window) ? ODS_DISABLED : 0) |
        ((cues & UISF_HIDEFOCUS) ? ODS_NOFOCUSRECT : 0) | ((cues & UISF_HIDEACCEL) ? ODS_NOACCEL : 0);
    item.hwndItem = window;
    item.hDC = dc;
    GetClientRect(window, &item.rcItem);
    SendMessageW(GetParent(window), WM_DRAWITEM, item.CtlID, (LPARAM)&item);
}

static LRESULT CALLBACK owner_draw_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    /* BUTTON otherwise exposes COLOR_BTNFACE between erase and WM_DRAWITEM.
       The owner always paints the complete rectangle, including its corners. */
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (dc) paint_button(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        paint_button(window, (HDC)wParam);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

BOOL ui_owner_draw(HWND button)
{
    return SetWindowSubclass(button, owner_draw_proc, 2, 0);
}

void ui_draw_item(const DRAWITEMSTRUCT *item, UiDrawItem draw)
{
    int width = item->rcItem.right - item->rcItem.left;
    int height = item->rcItem.bottom - item->rcItem.top;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    DRAWITEMSTRUCT buffered;
    if (width <= 0 || height <= 0) return;
    memory = CreateCompatibleDC(item->hDC);
    bitmap = memory ? CreateCompatibleBitmap(item->hDC, width, height) : NULL;
    if (!bitmap) {
        int saved;
        if (memory) DeleteDC(memory);
        saved = SaveDC(item->hDC);
        FillRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_WINDOW));
        draw(item);
        if (saved) RestoreDC(item->hDC, saved);
        return;
    }
    previous = SelectObject(memory, bitmap);
    SetViewportOrgEx(memory, -item->rcItem.left, -item->rcItem.top, NULL);
    /* UxTheme can leave transparent corners untouched. Initialize them too. */
    FillRect(memory, &item->rcItem, GetSysColorBrush(COLOR_WINDOW));
    buffered = *item;
    buffered.hDC = memory;
    draw(&buffered);
    BitBlt(item->hDC, item->rcItem.left, item->rcItem.top, width, height,
           memory, item->rcItem.left, item->rcItem.top, SRCCOPY);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}
