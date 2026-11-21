#ifndef CONNECTXP_BATTERY_LEVEL_H
#define CONNECTXP_BATTERY_LEVEL_H
/* batmeter.dll bitmap 301: full, medium, low, critical, unknown, absent,
   AC power, critical+AC, battery+AC, and an additional battery image. */
static inline int battery_image_index(int charge, int charging)
{
    if (charge < 0) return 4;
    if (charging) return charge <= 15 ? 7 : 8;
    if (charge <= 15) return 3;
    if (charge <= 33) return 2;
    if (charge <= 66) return 1;
    return 0;
}
#endif
