#include "settings.h"

BOOL settings_get(const WCHAR *name, BOOL fallback)
{
    HKEY key;
    DWORD value = 0, type = 0, bytes = sizeof(value);
    LONG result;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Connect95", 0, KEY_QUERY_VALUE, &key)) return fallback;
    result = RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &bytes);
    RegCloseKey(key);
    return !result && type == REG_DWORD && bytes == sizeof(value) && value <= 1 ? value != 0 : fallback;
}

BOOL settings_set(const WCHAR *name, BOOL enabled)
{
    HKEY key;
    DWORD value = enabled != FALSE;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Connect95", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result) return FALSE;
    result = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
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
