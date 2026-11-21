#ifndef CONNECTXP_DEVICE_NAME_H
#define CONNECTXP_DEVICE_NAME_H
#include <windows.h>
#define DEVICE_NAME_MAX 32
void device_name_load(WCHAR name[DEVICE_NAME_MAX + 1]);
BOOL device_name_dialog(HWND owner);
#endif
