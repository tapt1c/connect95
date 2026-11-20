#ifndef CONNECT95_SESSION_H
#define CONNECT95_SESSION_H
#include "discovery.h"
enum { SESSION_IDLE, SESSION_BUSY, SESSION_WAIT_PHONE, SESSION_CONFIRM,
       SESSION_PAIRED, SESSION_ERROR, SESSION_CLOSED };
typedef struct {
    int phase;
    char deviceId[39];
    WCHAR text[256];
    WCHAR code[9];
    BOOL transferActive;
    int transferPercent;
    WCHAR transferText[512];
} SessionStatus;
void session_init(void);
int session_start(const DiscoveryPeer *peer);
void session_status(SessionStatus *status);
void session_confirm(BOOL accepted);
void session_stop(void);
int session_forget(const DiscoveryPeer *peer);
void session_free(void);
int session_ring(void);
int session_send_file(const WCHAR *path);
void session_cancel_transfer(void);
#endif
