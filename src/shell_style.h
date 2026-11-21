#ifndef CONNECTXP_SHELL_STYLE_H
#define CONNECTXP_SHELL_STYLE_H
#include <stddef.h>
#include <stdint.h>
enum { SHELL_BODY_COLOR = 1, SHELL_LINK_COLOR = 2, SHELL_HEADER_COLOR = 4 };
typedef struct {
    unsigned int colors;
    uint32_t bodyColor, linkColor, headerColor;
    int backgroundBitmap, headerBitmap, collapsedBitmap, expandedBitmap;
} ShellStyle;
/* Read only numeric bitmap IDs and RGB colors; never evaluate UIFILE code. */
int shell_style_parse(const char *text, size_t length, ShellStyle *style);
#endif
