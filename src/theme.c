#include "theme.h"
#include <uxtheme.h>
#include <vsstyle.h>

typedef HTHEME (WINAPI *OpenTheme)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CloseTheme)(HTHEME);
typedef HRESULT (WINAPI *DrawTheme)(HTHEME, HDC, int, int, const RECT *, const RECT *);
static HMODULE library;
static HTHEME explorer, button;
static OpenTheme openTheme;
static CloseTheme closeTheme;
static DrawTheme drawTheme;

void theme_close(void)
{
    if (closeTheme) {
        if (explorer) closeTheme(explorer);
        if (button) closeTheme(button);
    }
    explorer = button = NULL;
}

void theme_refresh(HWND window)
{
    theme_close();
    if (!library) {
        WCHAR path[MAX_PATH];
        UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
        if (!length || length + 14 >= ARRAYSIZE(path)) return;
        lstrcatW(path, L"\\uxtheme.dll");
        library = LoadLibraryW(path);
        if (!library) return;
        openTheme = (OpenTheme)(void *)GetProcAddress(library, "OpenThemeData");
        closeTheme = (CloseTheme)(void *)GetProcAddress(library, "CloseThemeData");
        drawTheme = (DrawTheme)(void *)GetProcAddress(library, "DrawThemeBackground");
    }
    if (openTheme && closeTheme && drawTheme) {
        explorer = openTheme(window, L"ExplorerBar");
        button = openTheme(window, L"Button");
    }
}

void theme_panel(HDC dc, const RECT *rect, BOOL header)
{
    if (explorer && SUCCEEDED(drawTheme(explorer, dc, header ? EBP_NORMALGROUPHEAD : EBP_NORMALGROUPBACKGROUND, 0, rect, NULL))) return;
    FillRect(dc, rect, GetSysColorBrush(header ? COLOR_BTNFACE : COLOR_WINDOW));
    if (header) {
        RECT border = *rect;
        DrawEdge(dc, &border, EDGE_ETCHED, BF_BOTTOM);
    }
}

void theme_sidebar(HDC dc, const RECT *rect)
{
    int band;
    COLORREF top = GetSysColor(COLOR_GRADIENTACTIVECAPTION), bottom = GetSysColor(COLOR_ACTIVECAPTION);
    if (!explorer) { FillRect(dc, rect, GetSysColorBrush(COLOR_BTNFACE)); return; }
    for (band = 0; band < 32; ++band) {
        RECT strip = *rect;
        HBRUSH brush;
        strip.top = rect->top + (rect->bottom - rect->top) * band / 32;
        strip.bottom = rect->top + (rect->bottom - rect->top) * (band + 1) / 32;
        brush = CreateSolidBrush(RGB((GetRValue(top) * (31 - band) + GetRValue(bottom) * band) / 31,
            (GetGValue(top) * (31 - band) + GetGValue(bottom) * band) / 31,
            (GetBValue(top) * (31 - band) + GetBValue(bottom) * band) / 31));
        FillRect(dc, &strip, brush);
        DeleteObject(brush);
    }
}

void theme_button(HDC dc, const RECT *rect, BOOL pressed, BOOL disabled, BOOL focused)
{
    if (button && SUCCEEDED(drawTheme(button, dc, BP_PUSHBUTTON,
        disabled ? PBS_DISABLED : pressed ? PBS_PRESSED : focused ? PBS_DEFAULTED : PBS_NORMAL, rect, NULL))) return;
    {
        RECT border = *rect;
        FillRect(dc, rect, GetSysColorBrush(COLOR_BTNFACE));
        DrawFrameControl(dc, &border, DFC_BUTTON, DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0) | (disabled ? DFCS_INACTIVE : 0));
    }
}
