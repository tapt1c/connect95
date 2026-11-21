#ifndef CONNECTXP_SIDEBAR_LAYOUT_H
#define CONNECTXP_SIDEBAR_LAYOUT_H
/* Logical client pixels. Extra space stays below the groups, as in Explorer. */
typedef struct {
    int devicesTop, devicesBody, filesTop, filesBody, otherTop, otherBody;
    int headerHeight, linkStep, linkHeight;
} SidebarLayout;
static inline SidebarLayout sidebar_layout(int height, int devicesClosed, int filesClosed, int otherClosed)
{
    SidebarLayout layout;
    int compact = height < 440;
    int gap = compact ? 12 : 16, body = compact ? 84 : 96;
    int tree = height - (compact ? 288 : 332);
    if (tree > 150) tree = 150;
    if (tree < 64) tree = 64;
    layout.headerHeight = compact ? 24 : 26;
    layout.linkStep = compact ? 24 : 28;
    layout.linkHeight = compact ? 20 : 22;
    layout.devicesTop = compact ? 12 : 16;
    layout.devicesBody = devicesClosed ? 0 : tree;
    layout.filesTop = layout.devicesTop + layout.headerHeight + layout.devicesBody + gap;
    layout.filesBody = filesClosed ? 0 : body;
    layout.otherTop = layout.filesTop + layout.headerHeight + layout.filesBody + gap;
    layout.otherBody = otherClosed ? 0 : body;
    return layout;
}
typedef struct { int clientHeight, buttonTop, showProgress; } TransferWindowLayout;
static inline TransferWindowLayout transfer_window_layout(int busy)
{
    TransferWindowLayout layout = {busy ? 206 : 166, busy ? 166 : 126, busy != 0};
    return layout;
}
#endif
