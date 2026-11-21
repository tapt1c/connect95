#include "theme.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <wchar.h>
#include "shell_style.h"

typedef HTHEME (WINAPI *OpenTheme)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CloseTheme)(HTHEME);
typedef HRESULT (WINAPI *DrawTheme)(HTHEME, HDC, int, int, const RECT *, const RECT *);
typedef HRESULT (WINAPI *ThemeColor)(HTHEME, int, int, int, COLORREF *);
typedef HRESULT (WINAPI *CurrentTheme)(LPWSTR, int, LPWSTR, int, LPWSTR, int);
typedef HRESULT (WINAPI *ThemePartSize)(HTHEME, HDC, int, int, RECT *, THEMESIZE, SIZE *);
static HMODULE library;
static HTHEME explorer, button;
static OpenTheme openTheme;
static CloseTheme closeTheme;
static DrawTheme drawTheme;
static ThemeColor themeColor;
static CurrentTheme currentTheme;
static ThemePartSize themePartSize;
static COLORREF bodyColor, linkColor;
static COLORREF headerColor;
static HBITMAP shellBackground, shellHeader;
static HICON shellCollapse, shellExpand;
static SIZE shellCollapseSize, shellExpandSize;
static BOOL shellStyle;

static HICON bitmap_icon(HMODULE module, int id, SIZE *size)
{
    HBITMAP bitmap, mask;
    DIBSECTION dib;
    ICONINFO info;
    HDC dc;
    HGDIOBJ previous;
    HICON icon;
    int x, y;
    if (!id) return NULL;
    bitmap = LoadImageW(module, MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bitmap) return NULL;
    ZeroMemory(&dib, sizeof(dib));
    if (GetObjectW(bitmap, sizeof(dib), &dib) != sizeof(dib) || !dib.dsBm.bmBits || dib.dsBm.bmBitsPixel != 32 ||
        dib.dsBm.bmWidth <= 0 || dib.dsBm.bmWidth > 128 || dib.dsBm.bmHeight <= 0 || dib.dsBm.bmHeight > 128) {
        DeleteObject(bitmap); return NULL;
    }
    mask = CreateBitmap(dib.dsBm.bmWidth, dib.dsBm.bmHeight, 1, 1, NULL);
    dc = CreateCompatibleDC(NULL);
    previous = SelectObject(dc, mask);
    for (y = 0; y < dib.dsBm.bmHeight; ++y) {
        int row = dib.dsBmih.biHeight > 0 ? dib.dsBm.bmHeight - y - 1 : y;
        const BYTE *pixels = (const BYTE *)dib.dsBm.bmBits + row * dib.dsBm.bmWidthBytes;
        for (x = 0; x < dib.dsBm.bmWidth; ++x) SetPixelV(dc, x, y, pixels[x * 4 + 3] ? RGB(0, 0, 0) : RGB(255, 255, 255));
    }
    SelectObject(dc, previous); DeleteDC(dc);
    ZeroMemory(&info, sizeof(info)); info.fIcon = TRUE; info.hbmColor = bitmap; info.hbmMask = mask;
    icon = CreateIconIndirect(&info);
    if (icon) { size->cx = dib.dsBm.bmWidth; size->cy = dib.dsBm.bmHeight; }
    DeleteObject(bitmap); DeleteObject(mask);
    return icon;
}

static void load_shell_style(void)
{
    WCHAR path[MAX_PATH], scheme[64], size[64], *slash;
    HMODULE module;
    HRSRC resource;
    HGLOBAL data;
    const BYTE *raw;
    DWORD bytes, i;
    char *text;
    ShellStyle style;
    if (!currentTheme || FAILED(currentTheme(path, ARRAYSIZE(path), scheme, ARRAYSIZE(scheme), size, ARRAYSIZE(size)))) return;
    slash = wcsrchr(path, L'\\');
    if (!slash || wcspbrk(scheme, L"\\/:") || wcsstr(scheme, L"..")) return;
    slash[1] = 0;
    if (lstrlenW(path) + lstrlenW(scheme) + 22 >= MAX_PATH) return;
    lstrcatW(path, L"Shell\\"); lstrcatW(path, scheme); lstrcatW(path, L"\\shellstyle.dll");
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) return;
    resource = FindResourceW(module, MAKEINTRESOURCEW(1), L"UIFILE");
    bytes = resource ? SizeofResource(module, resource) : 0;
    data = resource ? LoadResource(module, resource) : NULL;
    raw = data ? LockResource(data) : NULL;
    text = raw && bytes && bytes <= 1048576 ? HeapAlloc(GetProcessHeap(), 0, bytes + 1) : NULL;
    if (text) {
        DWORD length = 0;
        if (bytes >= 2 && ((raw[0] == 0xff && raw[1] == 0xfe) || (raw[0] == '<' && raw[1] == 0))) {
            for (i = raw[0] == 0xff ? 2 : 0; i + 1 < bytes; i += 2) text[length++] = raw[i + 1] || raw[i] > 127 ? '?' : (char)raw[i];
        } else { CopyMemory(text, raw, bytes); length = bytes; }
        text[length] = 0;
        if (shell_style_parse(text, length, &style)) {
            shellStyle = TRUE;
            if (style.colors & SHELL_BODY_COLOR) bodyColor = style.bodyColor;
            if (style.colors & SHELL_LINK_COLOR) linkColor = style.linkColor;
            headerColor = style.colors & SHELL_HEADER_COLOR ? style.headerColor : linkColor;
            if (style.backgroundBitmap) shellBackground = LoadImageW(module, MAKEINTRESOURCEW(style.backgroundBitmap), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (style.headerBitmap) shellHeader = LoadImageW(module, MAKEINTRESOURCEW(style.headerBitmap), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            shellCollapse = bitmap_icon(module, style.expandedBitmap, &shellCollapseSize);
            shellExpand = bitmap_icon(module, style.collapsedBitmap, &shellExpandSize);
        }
        HeapFree(GetProcessHeap(), 0, text);
    }
    FreeLibrary(module);
}

static BOOL draw_bitmap(HDC dc, HBITMAP bitmap, const RECT *rect, BOOL transparent)
{
    BITMAP details;
    HDC source;
    HGDIOBJ previous;
    BOOL result;
    if (!bitmap || !GetObjectW(bitmap, sizeof(details), &details)) return FALSE;
    source = CreateCompatibleDC(dc);
    if (!source) return FALSE;
    previous = SelectObject(source, bitmap);
    if (transparent) result = TransparentBlt(dc, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
        source, 0, 0, details.bmWidth, details.bmHeight, RGB(255, 0, 255));
    else result = StretchBlt(dc, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
        source, 0, 0, details.bmWidth, details.bmHeight, SRCCOPY);
    SelectObject(source, previous); DeleteDC(source);
    return result;
}

void theme_close(void)
{
    if (shellBackground) DeleteObject(shellBackground);
    if (shellHeader) DeleteObject(shellHeader);
    if (shellCollapse) DestroyIcon(shellCollapse);
    if (shellExpand) DestroyIcon(shellExpand);
    shellBackground = shellHeader = NULL; shellCollapse = shellExpand = NULL; shellStyle = FALSE;
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
        currentTheme = (CurrentTheme)(void *)GetProcAddress(library, "GetCurrentThemeName");
        themePartSize = (ThemePartSize)(void *)GetProcAddress(library, "GetThemePartSize");
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
    headerColor = linkColor;
    if (explorer) load_shell_style();
}

void theme_panel(HDC dc, const RECT *rect, BOOL header)
{
    if (shellStyle) {
        if (header && draw_bitmap(dc, shellHeader, rect, TRUE)) return;
        if (!header) {
            HBRUSH brush = CreateSolidBrush(bodyColor);
            FillRect(dc, rect, brush); DeleteObject(brush);
            FrameRect(dc, rect, GetSysColorBrush(COLOR_3DSHADOW));
            return;
        }
    }
    if (explorer && SUCCEEDED(drawTheme(explorer, dc, header ? EBP_NORMALGROUPHEAD : EBP_NORMALGROUPBACKGROUND, 0, rect, NULL))) return;
    FillRect(dc, rect, GetSysColorBrush(header ? COLOR_BTNFACE : COLOR_WINDOW));
    if (header) {
        RECT border = *rect;
        DrawEdge(dc, &border, EDGE_ETCHED, BF_BOTTOM);
    }
}

void theme_sidebar(HDC dc, const RECT *rect)
{
    if (draw_bitmap(dc, shellBackground, rect, FALSE)) return;
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
    HICON glyph = collapsed ? shellExpand : shellCollapse;
    int part = collapsed ? EBP_NORMALGROUPEXPAND : EBP_NORMALGROUPCOLLAPSE;
    SIZE natural = {21, 21};
    int width, height, availableHeight = rect->bottom - rect->top, availableWidth = rect->right - rect->left - 8;
    HGDIOBJ previous = SelectObject(dc, font);
    if (glyph) natural = collapsed ? shellExpandSize : shellCollapseSize;
    else if (!shellStyle && explorer && themePartSize) {
        SIZE native;
        if (SUCCEEDED(themePartSize(explorer, dc, part, 1, NULL, TS_TRUE, &native)) &&
            native.cx > 0 && native.cy > 0 && native.cx <= 128 && native.cy <= 128) natural = native;
    }
    width = natural.cx; height = natural.cy;
    /* Keep native pixels whenever they fit, and preserve the aspect ratio in
       compact headers. In particular, never squeeze a round glyph to 17x18. */
    if (height > availableHeight) { width = width * availableHeight / height; height = availableHeight; }
    if (width > availableWidth) { height = height * availableWidth / width; width = availableWidth; }
    arrow.right = rect->right - 4;
    arrow.left = arrow.right - width;
    arrow.top = rect->top + (availableHeight - height) / 2;
    arrow.bottom = arrow.top + height;
    theme_panel(dc, rect, TRUE);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, headerColor);
    label.left += 10;
    label.right = arrow.left - 4;
    DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (glyph) {
        DrawIconEx(dc, arrow.left, arrow.top, glyph,
            arrow.right - arrow.left, arrow.bottom - arrow.top, 0, NULL, DI_NORMAL);
    } else if (shellStyle || !explorer || FAILED(drawTheme(explorer, dc, part, 1, &arrow, NULL))) {
        POINT points[3];
        HPEN pen = CreatePen(PS_SOLID, 1, headerColor);
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
