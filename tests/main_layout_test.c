#include "main_layout.h"
#include <assert.h>
#include <stdio.h>

static void inside(UiRect r, int width, int height)
{
    assert(r.w > 0 && r.h > 0);
    assert(r.x >= 0 && r.y >= 0 && r.x + r.w <= width && r.y + r.h <= height);
}

static void check(int width, int height, int closed)
{
    MainLayout layout = main_layout(width, height, closed);
    UiRect rects[24];
    int count = 0, i, j;
    assert(layout.compact == (width < 760 || height < 440));
    for (i = 0; i < 3; ++i) rects[count++] = layout.headers[i];
    if (!(closed & 1)) rects[count++] = layout.tree;
    rects[count++] = layout.icon;
    rects[count++] = layout.heading;
    rects[count++] = layout.state;
    rects[count++] = layout.battery;
    rects[count++] = layout.pair;
    for (i = 0; i < 3; ++i) rects[count++] = layout.actions[i];
    rects[count++] = layout.transferText;
    rects[count++] = layout.cancel;
    rects[count++] = layout.progress;
    rects[count++] = layout.recentText;
    for (i = 0; i < 6; ++i) if (!(closed & (i < 3 ? 2 : 4))) rects[count++] = layout.links[i];
    inside(layout.transferFrame, width, height);
    inside(layout.recentFrame, width, height);
    for (i = 0; i < count; ++i) {
        inside(rects[i], width, height);
        for (j = i + 1; j < count; ++j) {
            int disjoint = rects[i].x + rects[i].w <= rects[j].x || rects[j].x + rects[j].w <= rects[i].x ||
                rects[i].y + rects[i].h <= rects[j].y || rects[j].y + rects[j].h <= rects[i].y;
            if (!disjoint) fprintf(stderr, "Overlap at %dx%d, collapsed=%d, elements=%d,%d\n", width, height, closed, i, j);
            assert(disjoint);
        }
    }
}

int main(void)
{
    const int widths[] = {624, 632, 640, 700, 752, 760, 784, 880, 1024, 1920};
    const int heights[] = {384, 392, 400, 420, 440, 460, 480, 700, 1000};
    unsigned int i, j;
    int mask, inset;
    for (i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
        for (j = 0; j < sizeof(heights) / sizeof(heights[0]); ++j)
            for (mask = 0; mask < 8; ++mask) check(widths[i], heights[j], mask);
    /* Taskbar at bottom, top or side, and monitors with a nonzero origin. */
    for (inset = 0; inset <= 48; inset += 24) {
        WindowPlacement window = initial_window(-800, 20, 800 - inset, 600 - 32);
        assert(window.x >= -800 && window.y >= 20);
        assert(window.x + window.w <= -inset && window.y + window.h <= 588);
        assert(window.w >= 640 && window.h >= 480);
    }
    puts("Main layout checks passed: 720 compact/large/collapsed layouts, 800x600 work areas and monitor origins");
    return 0;
}
