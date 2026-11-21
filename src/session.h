#ifndef CONNECTXP_SESSION_H
#define CONNECTXP_SESSION_H
#include "discovery.h"
#include "device_plugins.h"
#include "notifications.h"
enum { SESSION_IDLE, SESSION_BUSY, SESSION_WAIT_PHONE, SESSION_CONFIRM,
       SESSION_PAIRED, SESSION_ERROR, SESSION_CLOSED };
typedef struct {
    WCHAR name[MAX_PATH], result[512];
    SYSTEMTIME time;
    BOOL receiving, succeeded;
} TransferRecord;
typedef struct {
    int phase;
    unsigned int generation;
    unsigned int notificationRevision;
    char deviceId[39];
    WCHAR text[256];
    WCHAR code[9];
    int batteryCharge;
    BOOL batteryCharging;
    BOOL clipboardSync;
    unsigned int clipboardRevision;
    SYSTEMTIME clipboardReceived;
    BOOL transferActive;
    int transferPercent;
    WCHAR transferText[512];
    WCHAR transferName[MAX_PATH];
    ULONGLONG transferDone, transferTotal;
    DWORD transferStarted;
    unsigned int transferRevision;
    BOOL transferReceiving, transferSucceeded;
} SessionStatus;
int session_init(void);
int session_start(const DiscoveryPeer *peer);
int session_start_saved(const DiscoveryPeer *peer);
int session_accept(const DiscoveryPeer *peer, SOCKET socket);
cJSON *session_identity_packet(const char *id, unsigned short port);
void session_status(SessionStatus *status);
void session_confirm(BOOL accepted);
void session_stop(void);
int session_forget(const DiscoveryPeer *peer);
void session_free(void);
int session_ring(void);
int session_ping(void);
int session_send_file(const WCHAR *path);
void session_cancel_transfer(void);
int session_transfer_record(TransferRecord *record);
int session_send_clipboard(const char *content, BOOL automatic);
int session_received_clipboard(ClipboardMessage *message);
void session_clipboard_sync(BOOL enabled);

int session_notification_list(NotificationSummary *items, int capacity, unsigned int *revision);
int session_notification_get(const char *id, PhoneNotification *notification);
int session_notification_alert(PhoneNotification *notification);

#endif
