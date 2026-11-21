#ifndef CONNECTXP_LANGUAGE_H
#define CONNECTXP_LANGUAGE_H
#include <windows.h>

enum { LANGUAGE_ENGLISH, LANGUAGE_RUSSIAN };
void language_load(void);
int language_current(void);
BOOL language_select(int language);
const WCHAR *tr(const WCHAR *english);
const WCHAR *language_status(const WCHAR *english, WCHAR *buffer, int capacity);
void language_about(HWND owner, HICON icon);

#endif
