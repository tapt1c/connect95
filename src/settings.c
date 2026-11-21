#include "settings.h"
#include <shlobj.h>

static BOOL settings_path(WCHAR path[MAX_PATH], BOOL create)
{
    WCHAR directory[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | (create ? CSIDL_FLAG_CREATE : 0),
                         NULL, 0, directory) != S_OK || lstrlenW(directory) > MAX_PATH - 40) return FALSE;
    lstrcatW(directory, L"\\ConnectXP");
    if (create && !CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    lstrcpyW(path, directory);
    lstrcatW(path, L"\\settings.ini");
    return TRUE;
}

BOOL settings_get_string(const WCHAR *name, WCHAR *value, DWORD capacity)
{
    WCHAR path[MAX_PATH];
    DWORD length;
    if (!value || capacity < 2) return FALSE;
    value[0] = 0;
    if (!settings_path(path, FALSE)) return FALSE;
    length = GetPrivateProfileStringW(L"Settings", name, L"", value, capacity, path);
    return length != 0 && length < capacity - 1;
}

BOOL settings_set_string(const WCHAR *name, const WCHAR *value)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD written;
    const BYTE bom[] = {0xff, 0xfe};
    if (!settings_path(path, TRUE)) return FALSE;
    /* XP's profile API preserves Unicode only when an existing file has a BOM. */
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        BOOL success = WriteFile(file, bom, sizeof(bom), &written, NULL) && written == sizeof(bom);
        CloseHandle(file);
        if (!success) { DeleteFileW(path); return FALSE; }
    } else if (GetLastError() != ERROR_FILE_EXISTS) return FALSE;
    return WritePrivateProfileStringW(L"Settings", name, value, path);
}

BOOL settings_get(const WCHAR *name, BOOL fallback)
{
    WCHAR value[4];
    if (!settings_get_string(name, value, ARRAYSIZE(value))) return fallback;
    if (value[0] == L'0' && value[1] == 0) return FALSE;
    if (value[0] == L'1' && value[1] == 0) return TRUE;
    return fallback;
}

BOOL settings_set(const WCHAR *name, BOOL enabled)
{
    return settings_set_string(name, enabled ? L"1" : L"0");
}

BOOL settings_autostart(void)
{
    HKEY key;
    WCHAR value[MAX_PATH + 32];
    DWORD type = 0, bytes = sizeof(value);
    LONG result;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key)) return FALSE;
    result = RegQueryValueExW(key, L"ConnectXP", NULL, &type, (BYTE *)value, &bytes);
    RegCloseKey(key);
    return !result && type == REG_SZ && bytes >= sizeof(WCHAR);
}

BOOL settings_set_autostart(BOOL enabled)
{
    HKEY key;
    WCHAR path[MAX_PATH], command[MAX_PATH + 32];
    DWORD length;
    LONG result;
    length = GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return FALSE;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result) return FALSE;
    if (enabled) {
        wsprintfW(command, L"\"%s\" --tray", path);
        result = RegSetValueExW(key, L"ConnectXP", 0, REG_SZ, (BYTE *)command, (lstrlenW(command) + 1) * sizeof(WCHAR));
    } else {
        result = RegDeleteValueW(key, L"ConnectXP");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
