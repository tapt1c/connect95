#ifndef CONNECTXP_UI_LAYOUT_H
#define CONNECTXP_UI_LAYOUT_H
#include <windows.h>
#include "main_layout.h"
typedef struct { HWND window; UiRect rect; int visible; } UiPlacement;
void ui_apply_layout(HWND owner, const UiPlacement *items, int count);
WindowPlacement ui_window_placement(HWND owner, int width, int height);
#endif
