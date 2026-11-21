#include "file_jobs.h"
#include "transfer.h"
#include "language.h"
#include "sidebar_layout.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

#define JOB_LIMIT 64
static WCHAR queue[JOB_LIMIT][MAX_PATH];
static int queued, historyCount;
static char queuePeer[39];
static TransferRecord history[JOB_LIMIT];
static HWND dialog, nameLabel, detailLabel, meter, cancelButton, folderButton;
static HWND historyDialog, historyList, historyDetail, historyHint, historyFolder, historyClose;
static BOOL updatingHistory, animationAvailable;
static int lastLayoutMode = -1;
static HWND animation;
static HMODULE shellResources;
static BOOL animationPlaying;
static HFONT dialogFont;
static BOOL suppressed;
static WORD time_field(const cJSON *item, const char *key, int maximum)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, key);
    return cJSON_IsNumber(value) && value->valuedouble >= 0 && value->valuedouble <= maximum &&
        value->valuedouble == value->valueint ? (WORD)value->valueint : 0;
}

static void set_text(HWND control, const WCHAR *text)
{
    WCHAR previous[1024];
    int length = lstrlenW(text);
    if (length < (int)ARRAYSIZE(previous) && GetWindowTextLengthW(control) == length &&
        GetWindowTextW(control, previous, ARRAYSIZE(previous)) == length && !lstrcmpW(previous, text)) return;
    SetWindowTextW(control, text);
}

static BOOL history_path(WCHAR path[MAX_PATH], BOOL temporary)
{
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path) != S_OK || lstrlenW(path) > MAX_PATH - 40) return FALSE;
    lstrcatW(path, temporary ? L"\\Connect95\\transfers.tmp" : L"\\Connect95\\transfers.json");
    return TRUE;
}

static void save_history(void)
{
    WCHAR path[MAX_PATH], temporary[MAX_PATH];
    cJSON *array = cJSON_CreateArray();
    char *text;
    int i;
    if (!array) return;
    for (i = 0; i < historyCount; ++i) {
        char name[1041], result[2049];
        cJSON *item = cJSON_CreateObject();
        if (!WideCharToMultiByte(CP_UTF8, 0, history[i].name, -1, name, sizeof(name), NULL, NULL) ||
            !WideCharToMultiByte(CP_UTF8, 0, history[i].result, -1, result, sizeof(result), NULL, NULL) ||
            !item || !cJSON_AddStringToObject(item, "name", name) || !cJSON_AddStringToObject(item, "result", result) ||
            !cJSON_AddBoolToObject(item, "receiving", history[i].receiving) ||
            !cJSON_AddBoolToObject(item, "succeeded", history[i].succeeded) ||
            !cJSON_AddNumberToObject(item, "year", history[i].time.wYear) ||
            !cJSON_AddNumberToObject(item, "month", history[i].time.wMonth) ||
            !cJSON_AddNumberToObject(item, "day", history[i].time.wDay) ||
            !cJSON_AddNumberToObject(item, "hour", history[i].time.wHour) ||
            !cJSON_AddNumberToObject(item, "minute", history[i].time.wMinute) ||
            !cJSON_AddItemToArray(array, item)) {
            cJSON_Delete(item); cJSON_Delete(array); return;
        }
    }
    text = cJSON_PrintUnformatted(array);
    if (text && history_path(path, FALSE) && history_path(temporary, TRUE)) {
        HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written, length = (DWORD)strlen(text);
            BOOL ok = WriteFile(file, text, length, &written, NULL) && written == length && FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok || !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(temporary);
        }
    }
    cJSON_free(text);
    cJSON_Delete(array);
}

void jobs_init(void)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD size, read;
    char *text;
    cJSON *array, *item;
    if (!history_path(path, FALSE)) return;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(file, NULL);
    text = size && size < 262144 ? HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
    if (!text || !ReadFile(file, text, size, &read, NULL) || read != size) {
        if (text) HeapFree(GetProcessHeap(), 0, text);
        CloseHandle(file); return;
    }
    CloseHandle(file);
    text[size] = 0;
    array = cJSON_Parse(text);
    if (cJSON_IsArray(array)) cJSON_ArrayForEach(item, array) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(item, "result");
        TransferRecord *record = &history[historyCount];
        if (!cJSON_IsString(name) || !cJSON_IsString(result) ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name->valuestring, -1, record->name, ARRAYSIZE(record->name)) ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result->valuestring, -1, record->result, ARRAYSIZE(record->result))) continue;
        record->receiving = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "receiving"));
        record->succeeded = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "succeeded"));
#define READ_TIME(member, field, maximum) record->time.member = time_field(item, field, maximum)
        READ_TIME(wYear, "year", 9999); READ_TIME(wMonth, "month", 12); READ_TIME(wDay, "day", 31);
        READ_TIME(wHour, "hour", 23); READ_TIME(wMinute, "minute", 59);
#undef READ_TIME
        if (++historyCount == JOB_LIMIT) break;
    }
    cJSON_Delete(array);
    HeapFree(GetProcessHeap(), 0, text);
}

void jobs_open_folder(HWND owner)
{
    WCHAR directory[MAX_PATH];
    if (!transfer_directory(directory) || (INT_PTR)ShellExecuteW(owner, L"open", directory, NULL, NULL, SW_SHOWNORMAL) <= 32)
        MessageBoxW(owner, tr(L"Could not open the received files folder."), L"ConnectXP", MB_OK | MB_ICONERROR);
}

static void enqueue(HWND owner, WCHAR paths[JOB_LIMIT][MAX_PATH], int count)
{
    SessionStatus snapshot;
    int i;
    session_status(&snapshot);
    if (snapshot.phase != SESSION_PAIRED || !count || count > JOB_LIMIT - queued ||
        (queued && strcmp(queuePeer, snapshot.deviceId))) goto failed;
    for (i = 0; i < count; ++i) {
        DWORD attributes = GetFileAttributesW(paths[i]);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) goto failed;
    }
    for (i = 0; i < count; ++i) lstrcpyW(queue[queued++], paths[i]);
    strcpy(queuePeer, snapshot.deviceId);
    suppressed = FALSE;
    return;
failed:
    MessageBoxW(owner, tr(L"Select up to 64 files while connected. Folders and paths longer than 259 characters are not supported."), L"ConnectXP", MB_OK | MB_ICONINFORMATION);
}

void jobs_select(HWND owner)
{
    OPENFILENAMEW picker;
    WCHAR paths[JOB_LIMIT][MAX_PATH];
    WCHAR *selection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 65536 * sizeof(WCHAR));
    WCHAR *name;
    int count = 0, directoryLength;
    if (!selection) return;
    ZeroMemory(&picker, sizeof(picker));
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrFilter = tr(L"All files\0*.*\0\0");
    picker.lpstrFile = selection;
    picker.nMaxFile = 65536;
    picker.lpstrTitle = tr(L"Send file to phone");
    picker.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&picker)) {
        directoryLength = lstrlenW(selection);
        name = selection + directoryLength + 1;
        if (!*name && directoryLength < MAX_PATH) lstrcpyW(paths[count++], selection);
        else while (*name && count < JOB_LIMIT) {
            if (directoryLength + lstrlenW(name) + 1 >= MAX_PATH) break;
            wsprintfW(paths[count++], L"%s\\%s", selection, name);
            name += lstrlenW(name) + 1;
        }
        if (*name) count = 0; /* Reject the entire batch instead of silently truncating. */
        enqueue(owner, paths, count);
    } else if (CommDlgExtendedError())
        MessageBoxW(owner, tr(L"Could not read the file selection. Try selecting fewer files."), L"ConnectXP", MB_OK | MB_ICONERROR);
    HeapFree(GetProcessHeap(), 0, selection);
}

void jobs_drop(HWND owner, HDROP drop)
{
    WCHAR paths[JOB_LIMIT][MAX_PATH];
    UINT count = DragQueryFileW(drop, 0xffffffff, NULL, 0), i;
    if (count <= JOB_LIMIT) for (i = 0; i < count; ++i) {
        if (DragQueryFileW(drop, i, NULL, 0) >= MAX_PATH) { count = 0; break; }
        DragQueryFileW(drop, i, paths[i], MAX_PATH);
    }
    DragFinish(drop);
    enqueue(owner, paths, count <= JOB_LIMIT ? (int)count : 0);
}

static void history_line(int i, WCHAR text[1024])
{
    const TransferRecord *record = &history[i];
    wsprintfW(text, L"%02u.%02u.%04u %02u:%02u  %s  %.259s — %.500s", record->time.wDay, record->time.wMonth,
        record->time.wYear, record->time.wHour, record->time.wMinute,
        tr(record->receiving ? L"Received" : L"Sent"), record->name, tr(record->result));
}

void jobs_recent(WCHAR *text, int capacity)
{
    if (!historyCount) lstrcpynW(text, tr(L"No completed transfers"), capacity);
    else {
        WCHAR line[1024];
        history_line(0, line);
        lstrcpynW(text, line, capacity);
    }
}

static void history_details(void)
{
    WCHAR text[1100];
    int selected = ListView_GetNextItem(historyList, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= historyCount) {
        set_text(historyDetail, tr(historyCount ? L"Select a transfer to view its details." : L"No completed transfers"));
        return;
    }
    wsprintfW(text, L"%s\r\n%s", history[selected].name, tr(history[selected].result));
    set_text(historyDetail, text);
}

static void fill_history(void)
{
    int i;
    if (!historyList) return;
    updatingHistory = TRUE;
    SendMessageW(historyList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(historyList);
    for (i = 0; i < historyCount; ++i) {
        LVITEMW item;
        WCHAR date[40];
        const TransferRecord *record = &history[i];
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = history[i].name;
        SendMessageW(historyList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1;
        item.pszText = (LPWSTR)tr(record->receiving ? L"Received" : L"Sent");
        SendMessageW(historyList, LVM_SETITEMTEXTW, i, (LPARAM)&item);
        wsprintfW(date, L"%02u.%02u.%04u %02u:%02u", record->time.wDay, record->time.wMonth,
            record->time.wYear, record->time.wHour, record->time.wMinute);
        item.iSubItem = 2; item.pszText = date;
        SendMessageW(historyList, LVM_SETITEMTEXTW, i, (LPARAM)&item);
        item.iSubItem = 3; item.pszText = (LPWSTR)tr(record->succeeded ? L"Completed" : L"Failed or cancelled");
        SendMessageW(historyList, LVM_SETITEMTEXTW, i, (LPARAM)&item);
    }
    if (historyCount) ListView_SetItemState(historyList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(historyList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(historyList, NULL, TRUE);
    updatingHistory = FALSE;
    history_details();
}

static void history_layout(HWND window)
{
    RECT area;
    int width, height, available;
    GetClientRect(window, &area);
    width = area.right; height = area.bottom;
    available = width - 24;
    MoveWindow(historyHint, 12, 12, available, 20, TRUE);
    MoveWindow(historyList, 12, 40, available, height - 148, TRUE);
    MoveWindow(historyDetail, 12, height - 96, available, 52, TRUE);
    MoveWindow(historyFolder, 12, height - 32, 140, 24, TRUE);
    MoveWindow(historyClose, width - 102, height - 32, 90, 24, TRUE);
    ListView_SetColumnWidth(historyList, 0, available * 34 / 100);
    ListView_SetColumnWidth(historyList, 1, available * 16 / 100);
    ListView_SetColumnWidth(historyList, 2, 128);
    ListView_SetColumnWidth(historyList, 3, available - available * 34 / 100 - available * 16 / 100 - 150);
}

static LRESULT CALLBACK history_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == DM_GETDEFID) return MAKELRESULT(3, DC_HASDEFID);
    if (message == WM_CREATE) {
        HINSTANCE instance = GetModuleHandleW(NULL);
        HWND child;
        LVCOLUMNW column;
        const WCHAR *columns[] = {L"File", L"Direction", L"Date", L"Result"};
        int i;
        historyHint = CreateWindowW(L"STATIC", tr(L"Select a transfer to view its details."), WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, NULL, instance, NULL);
        historyList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, window, NULL, instance, NULL);
        historyDetail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0, window, NULL, instance, NULL);
        historyFolder = CreateWindowW(L"BUTTON", tr(L"Open folder"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window, (HMENU)1, instance, NULL);
        historyClose = CreateWindowW(L"BUTTON", tr(L"Close"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window, (HMENU)3, instance, NULL);
        ListView_SetExtendedListViewStyle(historyList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessageW(child, WM_SETFONT, (WPARAM)dialogFont, TRUE);
        ZeroMemory(&column, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 140;
        for (i = 0; i < 4; ++i) {
            column.pszText = (LPWSTR)tr(columns[i]);
            SendMessageW(historyList, LVM_INSERTCOLUMNW, i, (LPARAM)&column);
        }
        fill_history();
        return 0;
    }
    if (message == WM_SIZE) { if (wParam != SIZE_MINIMIZED && historyList) history_layout(window); return 0; }
    if (message == WM_GETMINMAXINFO) {
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 640;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 340;
        return 0;
    }
    if (message == WM_NOTIFY && !updatingHistory && ((NMHDR *)lParam)->hwndFrom == historyList &&
        ((NMHDR *)lParam)->code == LVN_ITEMCHANGED) { history_details(); return 0; }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == 1) jobs_open_folder(window);
        else if (LOWORD(wParam) == 3 || LOWORD(wParam) == IDCANCEL) DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        historyDialog = historyList = historyDetail = historyHint = historyFolder = historyClose = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void transfer_layout(BOOL busy)
{
    TransferWindowLayout layout = transfer_window_layout(busy);
    RECT outer = {0, 0, 540, layout.clientHeight};
    RECT client;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int buttonY = layout.buttonTop;
    BOOL animated = busy && animationAvailable;
    GetClientRect(dialog, &client);
    if (lastLayoutMode == (int)busy && client.bottom == outer.bottom) return;
    lastLayoutMode = busy;
    MoveWindow(nameLabel, 20, 16, animated ? 340 : 500, 34, TRUE);
    MoveWindow(detailLabel, 20, 60, 500, 56, TRUE);
    MoveWindow(meter, 20, 126, 500, 18, TRUE);
    MoveWindow(folderButton, 20, buttonY, 140, 26, TRUE);
    MoveWindow(cancelButton, 380, buttonY, 140, 26, TRUE);
    ShowWindow(meter, layout.showProgress ? SW_SHOWNA : SW_HIDE);
    ShowWindow(animation, animated ? SW_SHOWNA : SW_HIDE);
    GetClientRect(dialog, &client);
    if (client.bottom != outer.bottom && !IsIconic(dialog)) {
        AdjustWindowRectEx(&outer, style, FALSE, WS_EX_CONTROLPARENT);
        SetWindowPos(dialog, NULL, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static LRESULT CALLBACK transfer_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == DM_GETDEFID) return MAKELRESULT(2, DC_HASDEFID);
    if (message == WM_CREATE) {
        HINSTANCE instance = GetModuleHandleW(NULL);
        HWND child;
        WCHAR path[MAX_PATH];
        UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
        nameLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 20, 16, 340, 34, window, NULL, instance, NULL);
        animation = CreateWindowW(ANIMATE_CLASSW, L"", WS_CHILD | ACS_TRANSPARENT | ACS_CENTER, 380, 4, 140, 50, window, NULL, instance, NULL);
        if (length && length + 13 < ARRAYSIZE(path)) {
            lstrcatW(path, L"\\shell32.dll");
            shellResources = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
            animationAvailable = shellResources && FindResourceW(shellResources, MAKEINTRESOURCEW(161), L"AVI") &&
                SendMessageW(animation, ACM_OPENW, (WPARAM)shellResources, (LPARAM)MAKEINTRESOURCEW(161));
        }
        detailLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 60, 500, 56, window, NULL, instance, NULL);
        meter = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 20, 126, 500, 18, window, NULL, instance, NULL);
        folderButton = CreateWindowW(L"BUTTON", tr(L"Open folder"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 20, 166, 140, 26, window, (HMENU)1, instance, NULL);
        cancelButton = CreateWindowW(L"BUTTON", tr(L"Cancel"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 380, 166, 140, 26, window, (HMENU)2, instance, NULL);
        for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessageW(child, WM_SETFONT, (WPARAM)dialogFont, TRUE);
        return 0;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == 1) jobs_open_folder(window);
        else if (LOWORD(wParam) == 2) {
            SessionStatus snapshot;
            session_status(&snapshot);
            if (snapshot.transferActive || queued) jobs_cancel();
            else { suppressed = TRUE; ShowWindow(window, SW_HIDE); }
        } else if (LOWORD(wParam) == IDCANCEL) SendMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_CLOSE) { suppressed = TRUE; ShowWindow(window, SW_HIDE); return 0; }
    if (message == WM_DESTROY) {
        if (animation) SendMessageW(animation, ACM_OPENW, 0, 0);
        if (shellResources) FreeLibrary(shellResources);
        shellResources = NULL; animationPlaying = animationAvailable = FALSE; lastLayoutMode = -1;
        dialog = animation = nameLabel = detailLabel = meter = cancelButton = folderButton = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL register_jobs_class(HWND owner, const WCHAR *name, WNDPROC procedure)
{
    WNDCLASSW cls;
    ZeroMemory(&cls, sizeof(cls));
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpfnWndProc = procedure;
    cls.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    cls.lpszClassName = name;
    return RegisterClassW(&cls) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void jobs_show(HWND owner, HFONT font)
{
    dialogFont = font;
    if (!historyDialog) {
        if (!register_jobs_class(owner, L"ConnectXP.TransferHistory", history_proc)) return;
        historyDialog = CreateWindowExW(WS_EX_CONTROLPARENT, L"ConnectXP.TransferHistory", tr(L"Transfer history — ConnectXP"),
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 420, owner, NULL, GetModuleHandleW(NULL), NULL);
    }
    if (historyDialog) { ShowWindow(historyDialog, SW_RESTORE); SetForegroundWindow(historyDialog); }
}

static void show_transfer(HWND owner, HFONT font)
{
    if (!dialog) {
        RECT outer = {0, 0, 540, 206};
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (!register_jobs_class(owner, L"ConnectXP.FileTransfer", transfer_proc)) return;
        dialogFont = font;
        AdjustWindowRectEx(&outer, style, FALSE, WS_EX_CONTROLPARENT);
        dialog = CreateWindowExW(WS_EX_CONTROLPARENT, L"ConnectXP.FileTransfer", tr(L"File transfer — ConnectXP"),
            style, CW_USEDEFAULT, CW_USEDEFAULT, outer.right - outer.left, outer.bottom - outer.top, owner, NULL, GetModuleHandleW(NULL), NULL);
    }
    if (dialog) ShowWindow(dialog, SW_SHOWNOACTIVATE);
}

void jobs_tick(HWND owner, HFONT font, const SessionStatus *snapshot)
{
    TransferRecord record;
    SessionStatus current = *snapshot;
    BOOL changed = FALSE;
    while (session_transfer_record(&record)) {
        if (historyCount == JOB_LIMIT) --historyCount;
        memmove(history + 1, history, historyCount * sizeof(history[0]));
        history[0] = record;
        ++historyCount;
        changed = TRUE;
    }
    if (changed) { save_history(); fill_history(); }
    if (queued && (current.phase == SESSION_ERROR || current.phase == SESSION_CLOSED || strcmp(queuePeer, current.deviceId))) queued = 0;
    if (queued && current.phase == SESSION_PAIRED && !current.transferActive && session_send_file(queue[0])) {
        --queued;
        memmove(queue, queue + 1, queued * sizeof(queue[0]));
        session_status(&current);
    }
    if ((current.transferActive || changed) && !suppressed && owner && (!dialog || !IsWindowVisible(dialog))) show_transfer(owner, font);
    if (dialog) {
        WCHAR details[768], numbers[256], caption[320];
        char stats[256];
        BOOL busy = current.transferActive || queued;
        DWORD elapsed = (DWORD)(GetTickCount() - current.transferStarted);
        double seconds = elapsed < 1000 ? 1.0 : elapsed / 1000.0;
        double speed = current.transferDone / seconds;
        BOOL known = current.transferTotal != UINT64_MAX;
        double estimate = speed > 0 && known && current.transferTotal >= current.transferDone ?
            (current.transferTotal - current.transferDone) / speed : 0;
        unsigned int remaining = estimate > 359999 ? 359999 : (unsigned int)estimate;
        transfer_layout(busy);
        snprintf(stats, sizeof(stats), "%.2f / %.2f MB  |  %.1f KB/s", current.transferDone / 1048576.0,
            known ? current.transferTotal / 1048576.0 : 0, speed / 1024.0);
        MultiByteToWideChar(CP_ACP, 0, stats, -1, numbers, ARRAYSIZE(numbers));
        wsprintfW(details, L"%s\r\n%s\r\n%s %u:%02u    %s %d", tr(current.transferReceiving ? L"Receiving from phone" : L"Sending to phone"), numbers,
            tr(L"Time left:"), remaining / 60, remaining % 60, tr(L"Queued:"), queued);
        if (!known || !current.transferDone) wsprintfW(details, L"%s\r\n%s\r\n%s —    %s %d",
            tr(current.transferReceiving ? L"Receiving from phone" : L"Sending to phone"),
            known ? numbers : tr(L"File size is unknown"), tr(L"Time left:"), tr(L"Queued:"), queued);
        set_text(nameLabel, current.transferName[0] ? current.transferName : tr(L"File transfers"));
        set_text(detailLabel, busy ? details : current.transferText[0] ? tr(current.transferText) : tr(L"No active transfers"));
        set_text(folderButton, tr(L"Open folder"));
        set_text(cancelButton, tr(busy ? L"Cancel" : L"Close"));
        if (busy) wsprintfW(caption, L"%d%% — %.259s", current.transferPercent, current.transferName);
        else lstrcpyW(caption, tr(current.transferSucceeded ? L"Transfer complete — ConnectXP" : L"File transfer — ConnectXP"));
        set_text(dialog, caption);
        if (SendMessageW(meter, PBM_GETPOS, 0, 0) != current.transferPercent) SendMessageW(meter, PBM_SETPOS, current.transferPercent, 0);
        if (animationPlaying != busy) {
            if (busy) SendMessageW(animation, ACM_PLAY, (WPARAM)-1, MAKELPARAM(0, -1));
            else SendMessageW(animation, ACM_STOP, 0, 0);
            animationPlaying = busy;
        }
        EnableWindow(cancelButton, TRUE);
    }
    /* Hiding a finished batch must not suppress future incoming transfers. */
    if (!current.transferActive && !queued) suppressed = FALSE;
}

void jobs_close(void)
{
    suppressed = TRUE; queued = 0;
    if (dialog) DestroyWindow(dialog);
    if (historyDialog) DestroyWindow(historyDialog);
}
void jobs_cancel(void) { queued = 0; session_cancel_transfer(); }
BOOL jobs_message(MSG *message)
{
    return (dialog && IsDialogMessageW(dialog, message)) || (historyDialog && IsDialogMessageW(historyDialog, message));
}
void jobs_translate(void)
{
    if (historyDialog) {
        LVCOLUMNW column;
        const WCHAR *columns[] = {L"File", L"Direction", L"Date", L"Result"};
        int i;
        set_text(historyDialog, tr(L"Transfer history — ConnectXP"));
        set_text(historyHint, tr(L"Select a transfer to view its details."));
        set_text(historyFolder, tr(L"Open folder"));
        set_text(historyClose, tr(L"Close"));
        ZeroMemory(&column, sizeof(column)); column.mask = LVCF_TEXT;
        for (i = 0; i < 4; ++i) {
            column.pszText = (LPWSTR)tr(columns[i]);
            SendMessageW(historyList, LVM_SETCOLUMNW, i, (LPARAM)&column);
        }
        fill_history();
    }
}
