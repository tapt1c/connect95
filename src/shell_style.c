#include "shell_style.h"
#include <string.h>

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch; }
static int space(int ch) { return ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t'; }

static int equal(const char *start, const char *end, const char *word)
{
    size_t i, length = strlen(word);
    if ((size_t)(end - start) != length) return 0;
    for (i = 0; i < length; ++i) if (lower((unsigned char)start[i]) != lower((unsigned char)word[i])) return 0;
    return 1;
}

static const char *find(const char *start, const char *end, const char *word)
{
    size_t length = strlen(word);
    while (start <= end && (size_t)(end - start) >= length) {
        if (equal(start, start + length, word)) return start;
        ++start;
    }
    return NULL;
}

static const char *skip(const char *start, const char *end)
{
    while (start < end && space((unsigned char)*start)) ++start;
    return start;
}

static int number(const char **start, const char *end, unsigned int limit, unsigned int *result)
{
    const char *p = skip(*start, end);
    unsigned int value = 0;
    if (p == end || *p < '0' || *p > '9') return 0;
    while (p < end && *p >= '0' && *p <= '9') {
        unsigned int digit = (unsigned int)(*p++ - '0');
        if (value > (limit - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    *result = value; *start = p;
    return 1;
}

static const char *property(const char *start, const char *end, const char *key)
{
    const char *p = start;
    size_t length = strlen(key);
    while ((p = find(p, end, key)) != NULL) {
        const char *after = skip(p + length, end);
        if ((p == start || space((unsigned char)p[-1]) || p[-1] == ';') && after < end && *after == ':') return skip(after + 1, end);
        p += length;
    }
    return NULL;
}

static int bitmap(const char *start, const char *end, const char *key)
{
    const char *p = property(start, end, key);
    unsigned int id;
    if (!p || end - p < 6 || !equal(p, p + 6, "rcbmp(")) return 0;
    p += 6;
    if (!number(&p, end, 65535, &id) || !id) return 0;
    p = skip(p, end);
    return p < end && *p == ',' ? (int)id : 0;
}

static int color(const char *start, const char *end, const char *key, uint32_t *result)
{
    const char *p = property(start, end, key);
    unsigned int value[3];
    int i;
    if (!p || end - p < 4 || !equal(p, p + 4, "rgb(")) return 0;
    p += 4;
    for (i = 0; i < 3; ++i) {
        if (!number(&p, end, 255, &value[i])) return 0;
        p = skip(p, end);
        if (p == end || *p++ != (i < 2 ? ',' : ')')) return 0;
    }
    *result = value[0] | (value[1] << 8) | (value[2] << 16);
    return 1;
}

static void read_style(const char *start, const char *end, int kind, ShellStyle *style)
{
    const char *selector = start, *open;
    while ((open = find(selector, end, "{")) != NULL) {
        const char *close = find(open + 1, end, "}"), *tail = open;
        const char *head = skip(selector, open);
        if (!close) return;
        while (tail > head && space((unsigned char)tail[-1])) --tail;
        if (kind == 0 && equal(head, tail, "button")) {
            style->headerBitmap = bitmap(open + 1, close, "background");
            if (color(open + 1, close, "foreground", &style->headerColor)) style->colors |= SHELL_HEADER_COLOR;
        } else if (kind == 0 && find(head, tail, "tasklist") && !find(head, tail, "mousefocused")) {
            if (color(open + 1, close, "background", &style->bodyColor)) style->colors |= SHELL_BODY_COLOR;
        } else if (kind == 0 && find(head, tail, "atom(arrow)") && !find(head, tail, "mousefocused") && !find(head, tail, "keyfocused")) {
            int id = bitmap(open + 1, close, "content");
            if (find(head, tail, "[selected]")) style->expandedBitmap = id;
            else style->collapsedBitmap = id;
        } else if (kind == 1 && equal(head, tail, "button")) {
            if (color(open + 1, close, "foreground", &style->linkColor)) style->colors |= SHELL_LINK_COLOR;
        } else if (kind == 2 && find(head, tail, "atom(sectionlist)")) {
            style->backgroundBitmap = bitmap(open + 1, close, "background");
        }
        selector = close + 1;
    }
}

int shell_style_parse(const char *text, size_t length, ShellStyle *style)
{
    const char *p, *end;
    ShellStyle parsed = {0};
    if (!text || !style || !length || length > 1048576) return 0;
    while (length && !text[length - 1]) --length;
    if (memchr(text, 0, length)) return 0;
    p = text; end = text + length;
    while ((p = find(p, end, "<style")) != NULL) {
        const char *header = find(p, end, ">"), *id, *nameEnd, *close;
        char quote = 0;
        int kind = -1;
        if (!header) break;
        close = find(header + 1, end, "</style>");
        if (!close) break;
        id = find(p, header, "resid");
        if (!id || (id = skip(id + 5, header)) == header || *id++ != '=') { p = close + 8; continue; }
        id = skip(id, header);
        if (id < header && (*id == '\'' || *id == '"')) quote = *id++;
        nameEnd = id;
        while (nameEnd < header && !space((unsigned char)*nameEnd) && (!quote || *nameEnd != quote)) ++nameEnd;
        if (equal(id, nameEnd, "sectionss")) kind = 0;
        else if (equal(id, nameEnd, "sectiontaskss")) kind = 1;
        else if (equal(id, nameEnd, "taskpane")) kind = 2;
        if (kind >= 0) read_style(header + 1, close, kind, &parsed);
        p = close + 8;
    }
    if (!parsed.colors && !parsed.backgroundBitmap && !parsed.headerBitmap) return 0;
    *style = parsed;
    return 1;
}
