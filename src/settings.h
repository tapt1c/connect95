#ifndef CONNECTXP_SETTINGS_H
#define CONNECTXP_SETTINGS_H
#include <windows.h>
BOOL settings_get(const WCHAR *name, BOOL fallback);
BOOL settings_set(const WCHAR *name, BOOL value);
BOOL settings_get_string(const WCHAR *name, WCHAR *value, DWORD capacity);
BOOL settings_set_string(const WCHAR *name, const WCHAR *value);
BOOL settings_autostart(void);
BOOL settings_set_autostart(BOOL enabled);
#endif
