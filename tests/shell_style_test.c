#include "shell_style.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *text =
        "<style resid=sectionss>button {background:rcbmp(42,6,#ff00ff,0,0,1,0);foreground:rgb(10,20,30);}"
        "tasklist[id=atom(tasklist)]{background:rgb(90,80,70);}"
        "element[id=atom(arrow)]{content:rcbmp(11,2,255,0,0,0,0);}"
        "element[id=atom(arrow)][selected]{content:rcbmp(12,2,255,0,0,0,0);}"
        "element[id=atom(arrow)][mousefocused]{content:rcbmp(13,2,255,0,0,0,0);}</style>"
        "<style resid='sectiontaskss'>button{foreground:rgb(0,0,0);}button[mousefocused]{foreground:rgb(255,0,0);}</style>"
        "<style resid=taskpane>element[id=atom(sectionlist)]{background:rcbmp(7,5,0,0,0,1,0);}</style>";
    ShellStyle style, untouched;
    const char invalid[] = "<style resid=sectionss>button{foreground:rgb(256,0,0);background:rcbmp(9999999,6);}</style>";
    const char truncated[] = "<style resid=sectionss>button{";
    const char embedded[] = "<style\0resid=sectionss>";
    char fuzz[1024];
    unsigned int state = 37;
    size_t i;
    assert(shell_style_parse(text, strlen(text), &style));
    assert(style.headerBitmap == 42 && style.backgroundBitmap == 7);
    assert(style.expandedBitmap == 12 && style.collapsedBitmap == 11);
    assert(style.colors == 7 && style.bodyColor == 0x46505a && style.linkColor == 0 && style.headerColor == 0x1e140a);
    untouched = style;
    assert(!shell_style_parse(invalid, sizeof(invalid) - 1, &style));
    assert(!memcmp(&style, &untouched, sizeof(style)));
    assert(!shell_style_parse(NULL, 0, &style));
    assert(!shell_style_parse(text, 1048577, &style));
    assert(!shell_style_parse(truncated, sizeof(truncated) - 1, &style));
    assert(!shell_style_parse(embedded, sizeof(embedded) - 1, &style));
    for (i = 0; i < 5000; ++i) {
        size_t at, length = strlen(text);
        memcpy(fuzz, text, length);
        state = state * 1664525u + 1013904223u;
        at = state % length;
        fuzz[at] = (char)(state >> 24);
        shell_style_parse(fuzz, length, &style);
    }
    puts("Shell style checks passed: bitmap IDs, RGB values, hover overrides, bounds and malformed input");
    return 0;
}
