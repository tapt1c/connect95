#include "theme.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

typedef HTHEME (WINAPI *OpenTheme)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CloseTheme)(HTHEME);
typedef HRESULT (WINAPI *DrawTheme)(HTHEME, HDC, int, int, const RECT *, const RECT *);
typedef HRESULT (WINAPI *ThemeColor)(HTHEME, int, int, int, COLORREF *);
static HMODULE library;
static HTHEME explorer, button;
static OpenTheme openTheme;
static CloseTheme closeTheme;
static DrawTheme drawTheme;
static ThemeColor themeColor;
static COLORREF bodyColor, linkColor;

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
    bodyColor = GetSysColor(COLOR_WINDOW);
    linkColor = GetSysColor(COLOR_HOTLIGHT);
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
        themeColor = (ThemeColor)(void *)GetProcAddress(library, "GetThemeColor");
    }
    if (openTheme && closeTheme && drawTheme) {
        explorer = openTheme(window, L"ExplorerBar");
        button = openTheme(window, L"Button");
        if (explorer) {
            if (themeColor) themeColor(explorer, EBP_NORMALGROUPBACKGROUND, 0, TMT_FILLCOLOR, &bodyColor);
            /* Image-backed parts may inherit a FILLCOLOR they never paint. */
            {
                HDC screen = GetDC(window), sample = CreateCompatibleDC(screen);
                HBITMAP bitmap = CreateCompatibleBitmap(screen, 64, 64);
                HGDIOBJ previous = SelectObject(sample, bitmap);
                RECT area = {0, 0, 64, 64};
                FillRect(sample, &area, GetSysColorBrush(COLOR_WINDOW));
                if (SUCCEEDED(drawTheme(explorer, sample, EBP_NORMALGROUPBACKGROUND, 0, &area, NULL))) {
                    COLORREF pixel = GetPixel(sample, 32, 32);
                    if (pixel != CLR_INVALID) bodyColor = pixel;
                }
                SelectObject(sample, previous); DeleteObject(bitmap); DeleteDC(sample); ReleaseDC(window, screen);
            }
            if (themeColor) themeColor(explorer, EBP_NORMALGROUPHEAD, 0, TMT_TEXTCOLOR, &linkColor);
        }
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
    /* ExplorerBar owns these colors; caption colors are unrelated on XP. */
    if (explorer && SUCCEEDED(drawTheme(explorer, dc, EBP_HEADERBACKGROUND, 0, rect, NULL))) return;
    FillRect(dc, rect, GetSysColorBrush(COLOR_BTNFACE));
}

COLORREF theme_body_color(void)
{
    return bodyColor;
}

COLORREF theme_link_color(void)
{
    return linkColor;
}

void theme_link_background(HDC dc, const RECT *rect)
{
    /* A link fills just the group's interior, never its themed border. */
    HBRUSH brush = CreateSolidBrush(theme_body_color());
    FillRect(dc, rect, brush);
    DeleteObject(brush);
}

void theme_group_header(HDC dc, const RECT *rect, const WCHAR *text, HFONT font, BOOL collapsed, BOOL focused)
{
    RECT label = *rect, arrow = *rect;
    HGDIOBJ previous = SelectObject(dc, font);
    theme_panel(dc, rect, TRUE);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme_link_color());
    label.left += 10;
    label.right -= 30;
    DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    arrow.left = arrow.right - 25;
    InflateRect(&arrow, -4, -4);
    if (!explorer || FAILED(drawTheme(explorer, dc, collapsed ? EBP_NORMALGROUPEXPAND : EBP_NORMALGROUPCOLLAPSE, 1, &arrow, NULL))) {
        POINT points[3];
        HPEN pen = CreatePen(PS_SOLID, 1, theme_link_color());
        HGDIOBJ oldPen = SelectObject(dc, pen);
        int center = (arrow.top + arrow.bottom) / 2, x = (arrow.left + arrow.right) / 2;
        points[0].x = x - 4; points[0].y = center + (collapsed ? -2 : 2);
        points[1].x = x; points[1].y = center + (collapsed ? 2 : -2);
        points[2].x = x + 4; points[2].y = points[0].y;
        Polyline(dc, points, 3);
        SelectObject(dc, oldPen); DeleteObject(pen);
    }
    if (focused) { InflateRect(&label, -2, -3); DrawFocusRect(dc, &label); }
    SelectObject(dc, previous);
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
