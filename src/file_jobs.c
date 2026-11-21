#include "file_jobs.h"
#include "transfer.h"
#include "language.h"
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
static HWND dialog, nameLabel, detailLabel, meter, cancelButton, folderButton, historyList;
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

static void fill_history(void)
{
    int i;
    WCHAR line[1024];
    if (!historyList) return;
    SendMessageW(historyList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(historyList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < historyCount; ++i) {
        history_line(i, line);
        SendMessageW(historyList, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageW(historyList, LB_SETHORIZONTALEXTENT, 1800, 0);
    SendMessageW(historyList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(historyList, NULL, TRUE);
}

static LRESULT CALLBACK transfer_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CREATE) {
        HINSTANCE instance = GetModuleHandleW(NULL);
        HWND child;
        WCHAR path[MAX_PATH];
        UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
        nameLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 20, 340, 48, window, NULL, instance, NULL);
        animation = CreateWindowW(ANIMATE_CLASSW, L"", WS_CHILD | ACS_TRANSPARENT | ACS_CENTER, 380, 12, 140, 60, window, NULL, instance, NULL);
        if (length && length + 13 < ARRAYSIZE(path)) {
            lstrcatW(path, L"\\shell32.dll");
            shellResources = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
            /* XP shell copy animation. Newer Windows may omit this resource. */
            if (shellResources && FindResourceW(shellResources, MAKEINTRESOURCEW(161), L"AVI") &&
                SendMessageW(animation, ACM_OPENW, (WPARAM)shellResources, (LPARAM)MAKEINTRESOURCEW(161))) ShowWindow(animation, SW_SHOWNA);
        }
        detailLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 82, 500, 66, window, NULL, instance, NULL);
        meter = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 20, 156, 500, 20, window, NULL, instance, NULL);
        folderButton = CreateWindowW(L"BUTTON", tr(L"Open folder"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 20, 190, 140, 26, window, (HMENU)1, instance, NULL);
        cancelButton = CreateWindowW(L"BUTTON", tr(L"Cancel"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 380, 190, 140, 26, window, (HMENU)2, instance, NULL);
        historyList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT,
            20, 238, 500, 140, window, NULL, instance, NULL);
        for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) SendMessageW(child, WM_SETFONT, (WPARAM)dialogFont, TRUE);
        fill_history();
        return 0;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == 1) jobs_open_folder(window);
        if (LOWORD(wParam) == 2) jobs_cancel();
        return 0;
    }
    if (message == WM_CLOSE) { suppressed = TRUE; ShowWindow(window, SW_HIDE); return 0; }
    if (message == WM_DESTROY) {
        if (animation) SendMessageW(animation, ACM_OPENW, 0, 0);
        if (shellResources) FreeLibrary(shellResources);
        shellResources = NULL; animationPlaying = FALSE;
        dialog = animation = nameLabel = detailLabel = meter = historyList = cancelButton = folderButton = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void jobs_show(HWND owner, HFONT font)
{
    if (!dialog) {
        WNDCLASSW cls;
        ZeroMemory(&cls, sizeof(cls));
        cls.hInstance = GetModuleHandleW(NULL);
        cls.lpfnWndProc = transfer_proc;
        cls.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
        cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
        cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        cls.lpszClassName = L"ConnectXP.FileTransfer";
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        dialogFont = font;
        dialog = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, tr(L"File transfer — ConnectXP"),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 560, 440, owner, NULL, cls.hInstance, NULL);
    }
    suppressed = FALSE;
    if (dialog) ShowWindow(dialog, SW_SHOWNOACTIVATE);
}

void jobs_tick(HWND owner, HFONT font, const SessionStatus *snapshot)
{
    TransferRecord record;
    BOOL changed = FALSE;
    while (session_transfer_record(&record)) {
        if (historyCount == JOB_LIMIT) --historyCount;
        memmove(history + 1, history, historyCount * sizeof(history[0]));
        history[0] = record;
        ++historyCount;
        changed = TRUE;
    }
    if (changed) { save_history(); fill_history(); }
    if (changed && !suppressed && owner) jobs_show(owner, font);
    if (queued && (snapshot->phase == SESSION_ERROR || snapshot->phase == SESSION_CLOSED || strcmp(queuePeer, snapshot->deviceId))) queued = 0;
    if (queued && snapshot->phase == SESSION_PAIRED && !snapshot->transferActive && session_send_file(queue[0])) {
        --queued;
        memmove(queue, queue + 1, queued * sizeof(queue[0]));
        if (!suppressed) jobs_show(owner, font);
    }
    if (snapshot->transferActive && !suppressed && (!dialog || !IsWindowVisible(dialog))) jobs_show(owner, font);
    if (dialog) {
        WCHAR details[768], numbers[256], caption[320];
        char stats[256];
        DWORD elapsed = (DWORD)(GetTickCount() - snapshot->transferStarted);
        double seconds = elapsed < 1000 ? 1.0 : elapsed / 1000.0;
        double speed = snapshot->transferDone / seconds;
        BOOL known = snapshot->transferTotal != UINT64_MAX;
        double estimate = speed > 0 && known && snapshot->transferTotal >= snapshot->transferDone ?
            (snapshot->transferTotal - snapshot->transferDone) / speed : 0;
        unsigned int remaining = estimate > 359999 ? 359999 : (unsigned int)estimate;
        snprintf(stats, sizeof(stats), "%.2f / %.2f MB  |  %.1f KB/s", snapshot->transferDone / 1048576.0,
            known ? snapshot->transferTotal / 1048576.0 : 0, speed / 1024.0);
        MultiByteToWideChar(CP_ACP, 0, stats, -1, numbers, ARRAYSIZE(numbers));
        wsprintfW(details, L"%s\r\n%s\r\n%s %u:%02u    %s %d", tr(snapshot->transferReceiving ? L"Receiving from phone" : L"Sending to phone"), numbers,
            tr(L"Time left:"), remaining / 60, remaining % 60, tr(L"Queued:"), queued);
        if (!known || !snapshot->transferDone) wsprintfW(details, L"%s\r\n%s\r\n%s —    %s %d",
            tr(snapshot->transferReceiving ? L"Receiving from phone" : L"Sending to phone"),
            known ? numbers : tr(L"File size is unknown"), tr(L"Time left:"), tr(L"Queued:"), queued);
        set_text(nameLabel, snapshot->transferActive ? snapshot->transferName : tr(snapshot->transferText));
        set_text(detailLabel, snapshot->transferActive ? details : tr(L"Transfer history"));
        set_text(folderButton, tr(L"Open folder"));
        set_text(cancelButton, tr(L"Cancel"));
        if (snapshot->transferActive) wsprintfW(caption, L"%d%% — %.259s", snapshot->transferPercent, snapshot->transferName);
        else lstrcpyW(caption, tr(L"File transfer — ConnectXP"));
        set_text(dialog, caption);
        if (SendMessageW(meter, PBM_GETPOS, 0, 0) != snapshot->transferPercent) SendMessageW(meter, PBM_SETPOS, snapshot->transferPercent, 0);
        if (animationPlaying != snapshot->transferActive) {
            if (snapshot->transferActive) SendMessageW(animation, ACM_PLAY, (WPARAM)-1, MAKELPARAM(0, -1));
            else SendMessageW(animation, ACM_STOP, 0, 0);
            animationPlaying = snapshot->transferActive;
        }
        EnableWindow(cancelButton, snapshot->transferActive || queued);
    }
}

void jobs_close(void) { suppressed = TRUE; queued = 0; if (dialog) DestroyWindow(dialog); }
void jobs_cancel(void) { queued = 0; session_cancel_transfer(); }
BOOL jobs_message(MSG *message) { return dialog && IsDialogMessageW(dialog, message); }
void jobs_translate(void) { fill_history(); }
