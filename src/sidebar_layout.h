#ifndef CONNECTXP_SIDEBAR_LAYOUT_H
#define CONNECTXP_SIDEBAR_LAYOUT_H
/* Logical client pixels. Extra space stays below the groups, as in Explorer. */
typedef struct { int devicesTop, devicesBody, filesTop, filesBody, otherTop, otherBody; } SidebarLayout;
static inline SidebarLayout sidebar_layout(int height, int devicesClosed, int filesClosed, int otherClosed)
{
    SidebarLayout layout;
    int tree = height - 332;
    if (tree > 150) tree = 150;
    if (tree < 64) tree = 64;
    layout.devicesTop = 16;
    layout.devicesBody = devicesClosed ? 0 : tree;
    layout.filesTop = layout.devicesTop + 26 + layout.devicesBody + 16;
    layout.filesBody = filesClosed ? 0 : 96;
    layout.otherTop = layout.filesTop + 26 + layout.filesBody + 16;
    layout.otherBody = otherClosed ? 0 : 96;
    return layout;
}
typedef struct { int clientHeight, buttonTop, showProgress; } TransferWindowLayout;
static inline TransferWindowLayout transfer_window_layout(int busy)
{
    TransferWindowLayout layout = {busy ? 206 : 166, busy ? 166 : 126, busy != 0};
    return layout;
}
#endif
