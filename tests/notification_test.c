#include "notifications.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char *json, PhoneNotification *notification)
{
    cJSON *packet = packet_parse(json, strlen(json));
    int result = packet && notification_parse(packet, notification);
    cJSON_Delete(packet);
    return result;
}

int main(void)
{
    NotificationStore *store = calloc(1, sizeof(*store));
    PhoneNotification *message = calloc(1, sizeof(*message));
    PhoneNotification *alert = calloc(1, sizeof(*alert));
    unsigned int revision;
    int i;
    assert(store && message && alert);
    assert(parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"chat-1\",\"appName\":\"Messenger\",\"title\":\"Hello\",\"text\":\"Message \\u2603\"}}", message));
    assert(!strcmp(message->text, "Message \xe2\x98\x83"));
    assert(notification_store_apply(store, message) == 1);
    revision = store->revision;
    assert(store->count == 1 && store->alertCount == 1);
    assert(!notification_store_apply(store, message));
    assert(store->revision == revision && store->alertCount == 1);
    strcpy(message->text, "Updated message");
    assert(notification_store_apply(store, message) == 1);
    assert(store->count == 1 && store->alertCount == 1);
    assert(notification_store_alert(store, alert) && !strcmp(alert->text, "Updated message"));
    assert(!notification_store_alert(store, alert));
    strcpy(message->eventTime, "1700000000001");
    assert(notification_store_apply(store, message) && notification_store_alert(store, alert));
    assert(!notification_store_apply(store, message));
    assert(parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"chat-1\",\"isCancel\":true}}", message));
    assert(notification_store_apply(store, message) && !store->count);
    assert(!notification_store_get(store, "chat-1"));
    assert(parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"old\",\"silent\":true,\"ticker\":\"Earlier notification\"}}", message));
    assert(!strcmp(message->text, "Earlier notification"));
    assert(notification_store_apply(store, message) && store->count == 1 && !store->alertCount);
    assert(parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"history\",\"requestAnswer\":true,\"text\":\"Old text\"}}", message));
    assert(notification_store_apply(store, message) && store->count == 2 && !store->alertCount);
    assert(parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"org.kde.kdeconnect_tp::old\",\"isCancel\":true}}", message));
    assert(!strcmp(message->id, "old") && notification_store_apply(store, message) && store->count == 1);
    assert(!parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"text\":\"no id\"}}", message));
    assert(!parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"a\",\"isCancel\":\"true\"}}", message));
    assert(!parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"a\",\"text\":123}}", message));
    assert(!parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"a\",\"id\":\"b\"}}", message));
    assert(!parse("{\"type\":\"kdeconnect.notification\",\"body\":{\"id\":\"a\",\"text\":\"null\\u0000text\"}}", message));
    notification_store_clear(store);
    memset(message, 0, sizeof(*message));
    strcpy(message->app, "Test");
    strcpy(message->title, "Title");
    for (i = 0; i <= NOTIFICATION_LIMIT; ++i) {
        snprintf(message->id, sizeof(message->id), "id-%d", i);
        assert(notification_store_apply(store, message) == 1);
    }
    assert(store->count == NOTIFICATION_LIMIT && store->alertCount == NOTIFICATION_LIMIT);
    assert(!notification_store_get(store, "id-0"));
    assert(notification_store_alert(store, alert) && !strcmp(alert->id, "id-1"));
    strcpy(message->id, "id-2");
    message->cancelled = 1;
    assert(notification_store_apply(store, message) == 1);
    assert(notification_store_alert(store, alert) && !strcmp(alert->id, "id-3"));
    notification_store_clear(store);
    memset(message, 0, sizeof(*message));
    strcpy(message->app, "ConnectXP");
    strcpy(message->title, "Ping from phone");
    strcpy(message->text, "Ping!");
    strcpy(message->id, "connectxp-ping-1-1");
    assert(notification_store_apply(store, message));
    strcpy(message->id, "connectxp-ping-1-2");
    assert(notification_store_apply(store, message));
    assert(store->count == 2 && store->alertCount == 2);
    assert(notification_store_alert(store, alert) && !strcmp(alert->id, "connectxp-ping-1-1"));
    assert(notification_store_alert(store, alert) && !strcmp(alert->id, "connectxp-ping-1-2"));
    notification_store_clear(store);
    assert(!store->count && !store->alertCount);
    free(message);
    free(alert);
    free(store);
    puts("notification tests passed: parsing, updates, cancellation, silent history, alert queue and bounded storage");
    return 0;
}
