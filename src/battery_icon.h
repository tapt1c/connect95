#ifndef CONNECTXP_BATTERY_ICON_H
#define CONNECTXP_BATTERY_ICON_H
#include <windows.h>
/* Returns an owned 16x16 icon; the caller releases it with DestroyIcon. */
HICON battery_icon_load(int charge, BOOL charging);
void battery_icon_free(void);
#endif
