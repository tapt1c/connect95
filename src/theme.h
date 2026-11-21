#ifndef CONNECTXP_THEME_H
#define CONNECTXP_THEME_H
#include <windows.h>
void theme_refresh(HWND window);
void theme_close(void);
void theme_panel(HDC dc, const RECT *rect, BOOL header);
void theme_sidebar(HDC dc, const RECT *rect);
COLORREF theme_body_color(void);
COLORREF theme_link_color(void);
void theme_link_background(HDC dc, const RECT *rect);
void theme_group_header(HDC dc, const RECT *rect, const WCHAR *text, HFONT font, BOOL collapsed, BOOL focused);
void theme_button(HDC dc, const RECT *rect, BOOL pressed, BOOL disabled, BOOL focused);
#endif
