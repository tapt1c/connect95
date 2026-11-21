#ifndef CONNECTXP_FILE_JOBS_H
#define CONNECTXP_FILE_JOBS_H
#include "session.h"
#include <shellapi.h>
void jobs_init(void);
void jobs_select(HWND owner);
void jobs_drop(HWND owner, HDROP drop);
void jobs_tick(HWND owner, HFONT font, const SessionStatus *snapshot);
void jobs_show(HWND owner, HFONT font);
void jobs_open_folder(HWND owner);
void jobs_recent(WCHAR *text, int capacity);
void jobs_close(void);
void jobs_cancel(void);
BOOL jobs_message(MSG *message);
void jobs_translate(void);
#endif
