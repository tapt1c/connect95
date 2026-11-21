#ifndef CONNECTXP_CLIPBOARD_H
#define CONNECTXP_CLIPBOARD_H
#include <windows.h>
#include "device_plugins.h"
/* 1 success, 0 clipboard busy, -1 unsupported or invalid text. */
int clipboard_read(HWND window, char content[CLIPBOARD_MAX_BYTES + 1]);
int clipboard_write(HWND window, const char *content);
uint64_t clipboard_time(void);
#endif
