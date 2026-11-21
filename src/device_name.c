#include "device_name.h"
#include "../resources/resource.h"
#include <wchar.h>

static BOOL valid_name(const WCHAR *name)
{
    int i, length = lstrlenW(name);
    WORD kinds[DEVICE_NAME_MAX];
    if (!length || length > DEVICE_NAME_MAX) return FALSE;
    if (!GetStringTypeW(CT_CTYPE1, name, length, kinds) ||
        (kinds[0] & C1_SPACE) || (kinds[length - 1] & C1_SPACE)) return FALSE;
    for (i = 0; i < length; ++i) {
        WCHAR ch = name[i];
        if (ch < 32 || ch == 127 || wcschr(L"\"',;:.!?()[]<>", ch)) return FALSE;
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (i + 1 == length || name[i + 1] < 0xdc00 || name[i + 1] > 0xdfff) return FALSE;
            ++i;
        } else if (ch >= 0xdc00 && ch <= 0xdfff) return FALSE;
    }
    return TRUE;
}

void device_name_load(WCHAR name[DEVICE_NAME_MAX + 1])
{
    HKEY key;
    WCHAR stored[DEVICE_NAME_MAX + 1];
    DWORD type = 0, size = sizeof(stored);
    LONG result;
    lstrcpyW(name, L"Connect95");
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Connect95", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    ZeroMemory(stored, sizeof(stored));
    result = RegQueryValueExW(key, L"DeviceName", NULL, &type, (BYTE *)stored, &size);
    RegCloseKey(key);
    if (result == ERROR_SUCCESS && type == REG_SZ && size >= sizeof(WCHAR) &&
        size <= sizeof(stored) && size % sizeof(WCHAR) == 0 && stored[size / sizeof(WCHAR) - 1] == 0 && valid_name(stored))
        lstrcpyW(name, stored);
}

static INT_PTR CALLBACK name_dialog_proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (message) {
    case WM_INITDIALOG: {
        WCHAR name[DEVICE_NAME_MAX + 1];
        device_name_load(name);
        SendDlgItemMessageW(dialog, IDC_DEVICE_NAME, EM_LIMITTEXT, DEVICE_NAME_MAX, 0);
        SetDlgItemTextW(dialog, IDC_DEVICE_NAME, name);
        SendDlgItemMessageW(dialog, IDC_DEVICE_NAME, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dialog, IDC_DEVICE_NAME));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            WCHAR name[DEVICE_NAME_MAX + 1] = L"", previous[DEVICE_NAME_MAX + 1];
            HKEY key;
            LONG result;
            GetDlgItemTextW(dialog, IDC_DEVICE_NAME, name, ARRAYSIZE(name));
            if (!valid_name(name)) {
                MessageBoxW(dialog, L"Enter 1–32 characters without leading or trailing spaces.\nKDE Connect filters these characters: \" ' , ; : . ! ? ( ) [ ] < >", L"Connect95", MB_OK | MB_ICONINFORMATION);
                SetFocus(GetDlgItem(dialog, IDC_DEVICE_NAME));
                return TRUE;
            }
            device_name_load(previous);
            if (!wcscmp(name, previous)) { EndDialog(dialog, IDCANCEL); return TRUE; }
            result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Connect95", 0, NULL, 0,
                                     KEY_SET_VALUE, NULL, &key, NULL);
            if (result == ERROR_SUCCESS) {
                result = RegSetValueExW(key, L"DeviceName", 0, REG_SZ, (const BYTE *)name,
                                       (DWORD)(lstrlenW(name) + 1) * sizeof(WCHAR));
                RegCloseKey(key);
            }
            if (result != ERROR_SUCCESS) {
                MessageBoxW(dialog, L"Could not save the device name in your Windows profile.", L"Connect95", MB_OK | MB_ICONERROR);
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

BOOL device_name_dialog(HWND owner)
{
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_DEVICE_NAME), owner, name_dialog_proc, 0);
    if (result == -1) MessageBoxW(owner, L"Could not open the device name dialog.", L"Connect95", MB_OK | MB_ICONERROR);
    return result == IDOK;
}
