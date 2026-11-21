#ifndef CONNECTXP_MAIN_LAYOUT_H
#define CONNECTXP_MAIN_LAYOUT_H
#include "sidebar_layout.h"

typedef struct { int x, y, w, h; } UiRect;
typedef struct {
    int paneWidth, compact, contentHeight;
    SidebarLayout sidebar;
    UiRect headers[3], tree, links[6], icon;
    UiRect heading, state, battery, pair, actions[3];
    UiRect transferFrame, transferText, cancel, progress, recentFrame, recentText;
} MainLayout;

static inline MainLayout main_layout(int width, int height, int closed)
{
    MainLayout layout;
    int right, available, button, i, transferTop, recentTop;
    layout.compact = width < 760 || height < 440;
    layout.contentHeight = height;
    layout.paneWidth = layout.compact ? 190 : 220;
    layout.sidebar = sidebar_layout(height, closed & 1, closed & 2, closed & 4);
    right = layout.paneWidth + (layout.compact ? 12 : 20);
    available = width - right - 12;
    button = (available - 12) / 3;
    layout.headers[0] = (UiRect){12, layout.sidebar.devicesTop, layout.paneWidth - 24, layout.sidebar.headerHeight};
    layout.headers[1] = (UiRect){12, layout.sidebar.filesTop, layout.paneWidth - 24, layout.sidebar.headerHeight};
    layout.headers[2] = (UiRect){12, layout.sidebar.otherTop, layout.paneWidth - 24, layout.sidebar.headerHeight};
    layout.tree = (UiRect){14, layout.sidebar.devicesTop + layout.sidebar.headerHeight + 2, layout.paneWidth - 28, layout.sidebar.devicesBody - 4};
    for (i = 0; i < 6; ++i) layout.links[i] = (UiRect){22,
        (i < 3 ? layout.sidebar.filesTop : layout.sidebar.otherTop) + layout.sidebar.headerHeight + 8 + (i % 3) * layout.sidebar.linkStep,
        layout.paneWidth - 44, layout.sidebar.linkHeight};
    layout.icon = (UiRect){right, 20, 32, 32};
    layout.heading = (UiRect){right + 44, 16, available - 44, 26};
    layout.state = (UiRect){right + 44, 48, available - 44, 32};
    layout.battery = (UiRect){right + 44, 88, available - 226, 32};
    layout.pair = (UiRect){right + available - 170, 88, 170, 26};
    for (i = 0; i < 3; ++i) layout.actions[i] = (UiRect){right + i * (button + 6), 128,
        i == 2 ? available - 2 * (button + 6) : button, layout.compact ? 38 : 48};
    transferTop = layout.compact ? 182 : 196;
    recentTop = layout.compact ? 302 : 340;
    layout.transferFrame = (UiRect){right, transferTop, available, layout.compact ? 104 : 128};
    layout.transferText = (UiRect){right + 12, transferTop + 24, available - 110, layout.compact ? 32 : 40};
    layout.cancel = (UiRect){right + available - 88, transferTop + 24, 76, 26};
    layout.progress = (UiRect){right + 12, transferTop + (layout.compact ? 70 : 86), available - 24, layout.compact ? 16 : 18};
    layout.recentFrame = (UiRect){right, recentTop, available, height - recentTop - 12};
    layout.recentText = (UiRect){right + 12, recentTop + 24, available - 24, height - recentTop - 48};
    return layout;
}

typedef struct { int x, y, w, h; } WindowPlacement;
static inline WindowPlacement initial_window(int x, int y, int width, int height)
{
    WindowPlacement window;
    window.w = width - 24;
    window.h = height - 24;
    if (window.w > 880) window.w = 880;
    if (window.h > 560) window.h = 560;
    if (window.w < 1) window.w = 1;
    if (window.h < 1) window.h = 1;
    window.x = x + (width - window.w) / 2;
    window.y = y + (height - window.h) / 2;
    return window;
}
#endif
