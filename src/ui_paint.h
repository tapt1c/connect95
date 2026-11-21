#ifndef CONNECTXP_UI_PAINT_H
#define CONNECTXP_UI_PAINT_H
#include <windows.h>
typedef void (*UiDrawItem)(const DRAWITEMSTRUCT *item);
BOOL ui_owner_draw(HWND button);
void ui_draw_item(const DRAWITEMSTRUCT *item, UiDrawItem draw);
#endif
