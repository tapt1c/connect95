#include "ui_layout.h"

WindowPlacement ui_window_placement(HWND owner, int width, int height)
{
    MONITORINFO monitor;
    RECT work = {0, 0, 800, 570};
    WindowPlacement placement;
    ZeroMemory(&monitor, sizeof(monitor)); monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &monitor)) work = monitor.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    placement = initial_window(work.left, work.top, work.right - work.left, work.bottom - work.top);
    if (placement.w > width) placement.w = width;
    if (placement.h > height) placement.h = height;
    placement.x = work.left + (work.right - work.left - placement.w) / 2;
    placement.y = work.top + (work.bottom - work.top - placement.h) / 2;
    return placement;
}

void ui_apply_layout(HWND owner, const UiPlacement *items, int count)
{
    HDWP batch = BeginDeferWindowPos(count);
    int i;
    for (i = 0; batch && i < count; ++i) {
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
        if (items[i].visible >= 0) flags |= items[i].visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
        batch = DeferWindowPos(batch, items[i].window, NULL, items[i].rect.x, items[i].rect.y,
            items[i].rect.w > 0 ? items[i].rect.w : 1, items[i].rect.h > 0 ? items[i].rect.h : 1, flags);
    }
    if (!batch || !EndDeferWindowPos(batch)) {
        /* DeferWindowPos can fail after queuing earlier windows. Reapply all. */
        for (i = 0; i < count; ++i) {
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
            if (items[i].visible >= 0) flags |= items[i].visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
            SetWindowPos(items[i].window, NULL, items[i].rect.x, items[i].rect.y,
                items[i].rect.w > 0 ? items[i].rect.w : 1, items[i].rect.h > 0 ? items[i].rect.h : 1, flags);
        }
    }
    /* The sizing modal loop can postpone queued child paints. Finish this
       frame before another WM_SIZE is allowed to display erased controls. */
    RedrawWindow(owner, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
