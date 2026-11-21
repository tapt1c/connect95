#include "notifications_ui.h"
#include <commctrl.h>
#include <string.h>

static HWND notificationWindow, notificationList, notificationDetail, notificationStatus;
static HFONT notificationFont;
static NotificationSummary rows[NOTIFICATION_LIMIT];
static int rowCount;
static unsigned int shownRevision = (unsigned int)-1, shownGeneration = (unsigned int)-1;
static BOOL rebuilding;
static char wantedId[NOTIFICATION_ID_BYTES + 1];

static void preview(const char *source, WCHAR *destination, int capacity)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, NULL, 0);
    WCHAR *full;
    int copy;
    destination[0] = 0;
    if (!length || capacity < 2) return;
    full = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length * sizeof(WCHAR));
    if (!full) return;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, full, length)) {
        HeapFree(GetProcessHeap(), 0, full);
        return;
    }
    copy = length - 1 < capacity - 1 ? length - 1 : capacity - 1;
    if (copy && full[copy - 1] >= 0xd800 && full[copy - 1] <= 0xdbff) --copy;
    memcpy(destination, full, (size_t)copy * sizeof(WCHAR));
    destination[copy] = 0;
    HeapFree(GetProcessHeap(), 0, full);
}

void notifications_balloon_text(const PhoneNotification *notification, WCHAR title[64], WCHAR text[256])
{
    int length;
    preview(notification->app, title, 64);
    if (!title[0]) lstrcpyW(title, L"ConnectXP");
    preview(notification->title, text, 256);
    length = lstrlenW(text);
    if (length >= 254) return;
    if (length && notification->text[0]) {
        text[length++] = L'\n';
        text[length] = 0;
    }
    if (length < 255) preview(notification->text, text + length, 256 - length);
    if (!text[0]) lstrcpyW(text, L"New notification from your phone.");
}

static void setDetailText(const WCHAR *text)
{
    int length = lstrlenW(text);
    WCHAR *previous;
    if (GetWindowTextLengthW(notificationDetail) != length) {
        SetWindowTextW(notificationDetail, text);
        return;
    }
    previous = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(length + 1) * sizeof(WCHAR));
    if (!previous) {
        SetWindowTextW(notificationDetail, text);
        return;
    }
    GetWindowTextW(notificationDetail, previous, length + 1);
    if (lstrcmpW(previous, text)) SetWindowTextW(notificationDetail, text);
    HeapFree(GetProcessHeap(), 0, previous);
}

static void showDetails(void)
{
    int selected = ListView_GetNextItem(notificationList, -1, LVNI_SELECTED);
    PhoneNotification *item;
    WCHAR *text;
    size_t capacity;
    int position, converted;
    if (selected < 0 || selected >= rowCount) {
        setDetailText(L"Enable Notification sync and notification access for KDE Connect on your phone.\r\nSelect a notification to read its full text.");
        return;
    }
    item = HeapAlloc(GetProcessHeap(), 0, sizeof(*item));
    if (!item) return;
    if (!session_notification_get(rows[selected].id, item)) {
        HeapFree(GetProcessHeap(), 0, item);
        setDetailText(L"This notification was removed from the phone.");
        return;
    }
    capacity = 2 * (strlen(item->app) + strlen(item->title) + strlen(item->text)) + 10;
    text = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(WCHAR));
    if (text) {
        position = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, item->app, -1, text, (int)capacity) - 1;
        if (position >= 0) {
            text[position++] = L'\r';
            text[position++] = L'\n';
            converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, item->title, -1, text + position, (int)capacity - position);
            if (converted) {
                position += converted - 1;
                text[position++] = L'\r';
                text[position++] = L'\n';
                text[position++] = L'\r';
                text[position++] = L'\n';
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, item->text, -1, text + position, (int)capacity - position)) {
                    size_t read, write, extra = 0, length = (size_t)lstrlenW(text);
                    for (read = 0; read < length; ++read)
                        if (text[read] == L'\n' && (!read || text[read - 1] != L'\r')) ++extra;
                    write = length + extra;
                    text[write] = 0;
                    for (read = length; read > 0;) {
                        --read;
                        text[--write] = text[read];
                        if (text[read] == L'\n' && (!read || text[read - 1] != L'\r')) text[--write] = L'\r';
                    }
                    setDetailText(text);
                }
            }
        }
        HeapFree(GetProcessHeap(), 0, text);
    }
    HeapFree(GetProcessHeap(), 0, item);
}

void notifications_ui_tick(const SessionStatus *snapshot)
{
    char selectedId[NOTIFICATION_ID_BYTES + 1] = "";
    WCHAR statusText[128];
    int i, selected, restore = -1;
    if (!notificationWindow) return;
    if (shownRevision != snapshot->notificationRevision || shownGeneration != snapshot->generation) {
        selected = ListView_GetNextItem(notificationList, -1, LVNI_SELECTED);
        if (shownGeneration == snapshot->generation && selected >= 0 && selected < rowCount)
            strcpy(selectedId, rows[selected].id);
        if (wantedId[0]) strcpy(selectedId, wantedId);
        rowCount = session_notification_list(rows, NOTIFICATION_LIMIT, &shownRevision);
        shownGeneration = snapshot->generation;
        rebuilding = TRUE;
        SendMessageW(notificationList, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(notificationList);
        for (i = 0; i < rowCount; ++i) {
            LVITEMW item;
            WCHAR app[NOTIFICATION_APP_BYTES + 1], title[NOTIFICATION_TITLE_BYTES + 1];
            preview(rows[i].app, app, ARRAYSIZE(app));
            preview(rows[i].title, title, ARRAYSIZE(title));
            ZeroMemory(&item, sizeof(item));
            item.mask = LVIF_TEXT;
            item.iItem = i;
            item.pszText = app;
            SendMessageW(notificationList, LVM_INSERTITEMW, 0, (LPARAM)&item);
            item.iSubItem = 1;
            item.pszText = title;
            SendMessageW(notificationList, LVM_SETITEMTEXTW, i, (LPARAM)&item);
            if (!strcmp(rows[i].id, selectedId)) restore = i;
        }
        if (restore < 0 && rowCount) restore = 0;
        if (restore >= 0) {
            ListView_SetItemState(notificationList, restore, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(notificationList, restore, FALSE);
        }
        SendMessageW(notificationList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(notificationList, NULL, TRUE);
        rebuilding = FALSE;
        wantedId[0] = 0;
        showDetails();
    }
    wsprintfW(statusText, snapshot->phase == SESSION_PAIRED ? L"Phone notifications — %d" :
              L"Disconnected — %d received notifications", rowCount);
    SetWindowTextW(notificationStatus, statusText);
}

static void layout(HWND window)
{
    RECT area;
    int listHeight;
    GetClientRect(window, &area);
    listHeight = area.bottom / 3;
    if (listHeight < 100) listHeight = 100;
    MoveWindow(notificationStatus, 10, 10, area.right - 20, 20, TRUE);
    MoveWindow(notificationList, 10, 36, area.right - 20, listHeight, TRUE);
    MoveWindow(notificationDetail, 10, listHeight + 46, area.right - 20, area.bottom - listHeight - 56, TRUE);
    ListView_SetColumnWidth(notificationList, 0, 150);
    ListView_SetColumnWidth(notificationList, 1, area.right - 195);
}

static LRESULT CALLBACK notificationProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        LVCOLUMNW column;
        notificationStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, NULL, GetModuleHandleW(NULL), NULL);
        notificationList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, window, NULL, GetModuleHandleW(NULL), NULL);
        notificationDetail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, window, NULL, GetModuleHandleW(NULL), NULL);
        if (!notificationStatus || !notificationList || !notificationDetail) return -1;
        SendMessageW(notificationStatus, WM_SETFONT, (WPARAM)notificationFont, TRUE);
        SendMessageW(notificationList, WM_SETFONT, (WPARAM)notificationFont, TRUE);
        SendMessageW(notificationDetail, WM_SETFONT, (WPARAM)notificationFont, TRUE);
        SendMessageW(notificationDetail, EM_SETLIMITTEXT, 1000000, 0);
        ListView_SetExtendedListViewStyle(notificationList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ZeroMemory(&column, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 150;
        column.pszText = L"Application";
        SendMessageW(notificationList, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
        column.pszText = L"Title";
        SendMessageW(notificationList, LVM_INSERTCOLUMNW, 1, (LPARAM)&column);
        DeleteMenu(GetSystemMenu(window, FALSE), SC_MAXIMIZE, MF_BYCOMMAND);
        return 0;
    }
    case WM_SIZE:
        if (wParam == SIZE_MAXIMIZED) ShowWindow(window, SW_RESTORE);
        else if (wParam != SIZE_MINIMIZED && notificationList) layout(window);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MAXIMIZE) return 0;
        break;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 520;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 360;
        return 0;
    case WM_NOTIFY:
        if (!rebuilding && ((NMHDR *)lParam)->hwndFrom == notificationList && ((NMHDR *)lParam)->code == LVN_ITEMCHANGED)
            showDetails();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        notificationWindow = NULL;
        notificationList = NULL;
        notificationDetail = NULL;
        notificationStatus = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void notifications_ui_show(HWND owner, HFONT font, const char *id)
{
    SessionStatus snapshot;
    if (id) lstrcpynA(wantedId, id, ARRAYSIZE(wantedId));
    if (!notificationWindow) {
        WNDCLASSW cls;
        ZeroMemory(&cls, sizeof(cls));
        cls.hInstance = GetModuleHandleW(NULL);
        cls.lpfnWndProc = notificationProc;
        cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
        cls.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        if (!cls.hIcon) cls.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICONSM);
        if (!cls.hIcon) cls.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
        if (!cls.hIcon) cls.hIcon = LoadIconW(NULL, IDI_INFORMATION);
        cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        cls.lpszClassName = L"ConnectXP.Notifications";
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        notificationFont = font;
        shownRevision = (unsigned int)-1;
        shownGeneration = (unsigned int)-1;
        notificationWindow = CreateWindowExW(0, cls.lpszClassName, L"Notifications — ConnectXP",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 660, 470,
            owner, NULL, cls.hInstance, NULL);
        if (!notificationWindow) {
            MessageBoxW(owner, L"Could not open the notifications window.", L"ConnectXP", MB_OK | MB_ICONERROR);
            return;
        }
    }
    if (id && id[0]) shownRevision = (unsigned int)-1;
    session_status(&snapshot);
    notifications_ui_tick(&snapshot);
    ShowWindow(notificationWindow, SW_RESTORE);
    SetForegroundWindow(notificationWindow);
}

void notifications_ui_close(void)
{
    if (notificationWindow) DestroyWindow(notificationWindow);
}
