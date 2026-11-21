#ifndef CONNECTXP_THEME_H
#define CONNECTXP_THEME_H
#include <windows.h>
void theme_refresh(HWND window);
void theme_close(void);
void theme_panel(HDC dc, const RECT *rect, BOOL header);
void theme_sidebar(HDC dc, const RECT *rect);
void theme_button(HDC dc, const RECT *rect, BOOL pressed, BOOL disabled, BOOL focused);
#endif
