#include "sidebar_layout.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const int heights[] = {440, 460, 480, 700, 1000};
    unsigned int i;
    int mask;
    for (i = 0; i < sizeof(heights) / sizeof(heights[0]); ++i) {
        for (mask = 0; mask < 8; ++mask) {
            SidebarLayout layout = sidebar_layout(heights[i], mask & 1, mask & 2, mask & 4);
            assert(layout.devicesTop >= 0);
            assert(layout.filesTop >= layout.devicesTop + 26 + layout.devicesBody + 12);
            assert(layout.otherTop >= layout.filesTop + 26 + layout.filesBody + 12);
            assert(layout.otherTop + 26 + layout.otherBody <= heights[i] - 10);
            assert((layout.devicesBody == 0) == ((mask & 1) != 0));
            assert((layout.filesBody == 0) == ((mask & 2) != 0));
            assert((layout.otherBody == 0) == ((mask & 4) != 0));
            if (!mask) printf("%d %d %d %d %d %d %d\n", heights[i], layout.devicesTop, layout.devicesBody,
                layout.filesTop, layout.filesBody, layout.otherTop, layout.otherBody);
        }
    }
    {
        TransferWindowLayout active = transfer_window_layout(1), done = transfer_window_layout(0);
        assert(active.showProgress && !done.showProgress);
        assert(done.clientHeight < active.clientHeight);
        assert(active.buttonTop >= 144 + 12);
        assert(done.buttonTop >= 116 + 8);
        assert(active.buttonTop + 26 + 12 <= active.clientHeight);
        assert(done.buttonTop + 26 + 12 <= done.clientHeight);
    }
    return 0;
}
