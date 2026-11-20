#ifndef CONNECT95_NOTIFICATIONS_UI_H
#define CONNECT95_NOTIFICATIONS_UI_H
#include "session.h"
void notifications_ui_show(HWND owner, HFONT font, const char *id);
void notifications_ui_tick(const SessionStatus *snapshot);
void notifications_ui_close(void);
void notifications_balloon_text(const PhoneNotification *notification, WCHAR title[64], WCHAR text[256]);
#endif
