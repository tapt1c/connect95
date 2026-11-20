#ifndef CONNECT95_NOTIFICATIONS_H
#define CONNECT95_NOTIFICATIONS_H
#include "packet.h"
#define NOTIFICATION_LIMIT 128
#define NOTIFICATION_ID_BYTES 1024
#define NOTIFICATION_APP_BYTES 1024
#define NOTIFICATION_TITLE_BYTES 4096
#define NOTIFICATION_TEXT_BYTES 65535

typedef struct {
    char id[NOTIFICATION_ID_BYTES + 1];
    char app[NOTIFICATION_APP_BYTES + 1];
    char title[NOTIFICATION_TITLE_BYTES + 1];
    char text[NOTIFICATION_TEXT_BYTES + 1];
    char eventTime[64];
    int cancelled;
    int silent;
} PhoneNotification;

typedef struct {
    char id[NOTIFICATION_ID_BYTES + 1];
    char app[NOTIFICATION_APP_BYTES + 1];
    char title[NOTIFICATION_TITLE_BYTES + 1];
} NotificationSummary;

typedef struct {
    PhoneNotification *items[NOTIFICATION_LIMIT];
    int count;
    unsigned int revision;
    char alerts[NOTIFICATION_LIMIT][NOTIFICATION_ID_BYTES + 1];
    int alertCount;
} NotificationStore;

int notification_parse(const cJSON *packet, PhoneNotification *notification);
void notification_store_clear(NotificationStore *store);
int notification_store_apply(NotificationStore *store, const PhoneNotification *notification);
const PhoneNotification *notification_store_get(const NotificationStore *store, const char *id);
int notification_store_alert(NotificationStore *store, PhoneNotification *notification);
#endif
