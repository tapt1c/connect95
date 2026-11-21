#include "battery_level.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    int charge, previous = 3;
    assert(battery_image_index(-1, 0) == 4);
    assert(battery_image_index(-1, 1) == 4);
    assert(battery_image_index(0, 0) == 3);
    assert(battery_image_index(15, 0) == 3);
    assert(battery_image_index(16, 0) == 2);
    assert(battery_image_index(33, 0) == 2);
    assert(battery_image_index(34, 0) == 1);
    assert(battery_image_index(66, 0) == 1);
    assert(battery_image_index(67, 0) == 0);
    assert(battery_image_index(100, 0) == 0);
    assert(battery_image_index(15, 1) == 7);
    assert(battery_image_index(16, 1) == 8);
    for (charge = 0; charge <= 100; ++charge) {
        int icon = battery_image_index(charge, 0);
        int plugged = battery_image_index(charge, 1);
        assert(icon >= 0 && icon <= previous);
        assert(plugged == 7 || plugged == 8);
        previous = icon;
    }
    puts("Battery icon checks passed: native level boundaries, monotonic charge, charging and unknown state");
    return 0;
}
