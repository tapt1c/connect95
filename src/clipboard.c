#include "clipboard.h"
#include <string.h>

uint64_t clipboard_time(void)
{
    FILETIME time;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&time);
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    if (ticks.QuadPart / 10000 < 11644473600000ULL) return 0;
    return ticks.QuadPart / 10000 - 11644473600000ULL;
}

int clipboard_read(HWND window, char content[CLIPBOARD_MAX_BYTES + 1])
{
    HANDLE data;
    const WCHAR *text;
    WCHAR roundtrip[CLIPBOARD_MAX_BYTES + 1];
    SIZE_T capacity, length = 0;
    int result = -1;
    if (!OpenClipboard(window)) return 0;
    data = GetClipboardData(CF_UNICODETEXT);
    if (!data) goto finish;
    capacity = GlobalSize(data) / sizeof(WCHAR);
    text = GlobalLock(data);
    if (!text) goto finish;
    while (length < capacity && length <= CLIPBOARD_MAX_BYTES && text[length]) ++length;
    if (length < capacity && length <= CLIPBOARD_MAX_BYTES &&
        WideCharToMultiByte(CP_UTF8, 0, text, -1, content, CLIPBOARD_MAX_BYTES + 1, NULL, NULL) &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, content, -1, roundtrip, ARRAYSIZE(roundtrip)) &&
        !wcscmp(text, roundtrip)) result = 1;
    GlobalUnlock(data);
finish:
    CloseClipboard();
    return result;
}

int clipboard_write(HWND window, const char *content)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, content, -1, NULL, 0);
    HGLOBAL data;
    WCHAR *text;
    if (!length || strlen(content) > CLIPBOARD_MAX_BYTES) return -1;
    data = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)length * sizeof(WCHAR));
    if (!data) return -1;
    text = GlobalLock(data);
    if (!text) { GlobalFree(data); return -1; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, content, -1, text, length)) {
        GlobalUnlock(data);
        GlobalFree(data);
        return -1;
    }
    GlobalUnlock(data);
    if (!OpenClipboard(window)) { GlobalFree(data); return 0; }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, data)) {
        CloseClipboard();
        GlobalFree(data);
        return -1;
    }
    CloseClipboard();
    return 1;
}
