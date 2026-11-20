#include "notifications.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int text_field(const cJSON *body, const char *key, char *out, size_t size, const char *fallback)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(body, key);
    const char *text = fallback;
    if (value) {
        if (!cJSON_IsString(value)) return 0;
        text = value->valuestring;
    }
    if (strlen(text) >= size) return 0;
    strcpy(out, text);
    return 1;
}

int notification_parse(const cJSON *packet, PhoneNotification *notification)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    const cJSON *cancel, *silent, *history;
    const char *prefix = "org.kde.kdeconnect_tp::";
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "kdeconnect.notification") || !cJSON_IsObject(body)) return 0;
    memset(notification, 0, sizeof(*notification));
    if (!text_field(body, "id", notification->id, sizeof(notification->id), "") || !notification->id[0]) return 0;
    if (!strncmp(notification->id, prefix, strlen(prefix)))
        memmove(notification->id, notification->id + strlen(prefix), strlen(notification->id + strlen(prefix)) + 1);
    if (!notification->id[0]) return 0;
    cancel = cJSON_GetObjectItemCaseSensitive(body, "isCancel");
    silent = cJSON_GetObjectItemCaseSensitive(body, "silent");
    history = cJSON_GetObjectItemCaseSensitive(body, "requestAnswer");
    if ((cancel && !cJSON_IsBool(cancel)) || (silent && !cJSON_IsBool(silent)) || (history && !cJSON_IsBool(history))) return 0;
    notification->cancelled = cJSON_IsTrue(cancel);
    notification->silent = cJSON_IsTrue(silent) || cJSON_IsTrue(history);
    if (notification->cancelled) return 1;
    {
        const cJSON *time = cJSON_GetObjectItemCaseSensitive(body, "time");
        if (cJSON_IsString(time) && strlen(time->valuestring) < sizeof(notification->eventTime))
            strcpy(notification->eventTime, time->valuestring);
        else if (cJSON_IsNumber(time) && time->valuedouble >= 0 && time->valuedouble <= 9007199254740991.0)
            snprintf(notification->eventTime, sizeof(notification->eventTime), "%.0f", time->valuedouble);
    }
    if (!text_field(body, "appName", notification->app, sizeof(notification->app), "Phone") ||
        !text_field(body, "title", notification->title, sizeof(notification->title), "") ||
        !text_field(body, "text", notification->text, sizeof(notification->text), "")) return 0;
    if (!notification->title[0] && !notification->text[0] &&
        !text_field(body, "ticker", notification->text, sizeof(notification->text), "")) return 0;
    return 1;
}

static void remove_alert(NotificationStore *store, const char *id)
{
    int i;
    for (i = 0; i < store->alertCount; ++i) {
        if (!strcmp(store->alerts[i], id)) {
            memmove(store->alerts + i, store->alerts + i + 1,
                    (size_t)(store->alertCount - i - 1) * sizeof(store->alerts[0]));
            --store->alertCount;
            --i;
        }
    }
}

static void remove_item(NotificationStore *store, int index)
{
    remove_alert(store, store->items[index]->id);
    free(store->items[index]);
    memmove(store->items + index, store->items + index + 1,
            (size_t)(store->count - index - 1) * sizeof(store->items[0]));
    --store->count;
}

void notification_store_clear(NotificationStore *store)
{
    while (store->count) remove_item(store, store->count - 1);
    store->alertCount = 0;
    ++store->revision;
}

const PhoneNotification *notification_store_get(const NotificationStore *store, const char *id)
{
    int i;
    for (i = 0; i < store->count; ++i)
        if (!strcmp(store->items[i]->id, id)) return store->items[i];
    return NULL;
}

int notification_store_apply(NotificationStore *store, const PhoneNotification *notification)
{
    PhoneNotification *copy;
    int i, index = -1;
    for (i = 0; i < store->count; ++i)
        if (!strcmp(store->items[i]->id, notification->id)) { index = i; break; }
    if (notification->cancelled) {
        if (index < 0) return 0;
        remove_item(store, index);
        ++store->revision;
        return 1;
    }
    if (index >= 0 && !strcmp(store->items[index]->app, notification->app) &&
        !strcmp(store->items[index]->title, notification->title) &&
        !strcmp(store->items[index]->text, notification->text) &&
        !strcmp(store->items[index]->eventTime, notification->eventTime)) return 0;
    copy = malloc(sizeof(*copy));
    if (!copy) return -1;
    *copy = *notification;
    if (index >= 0) {
        free(store->items[index]);
        memmove(store->items + index, store->items + index + 1,
                (size_t)(store->count - index - 1) * sizeof(store->items[0]));
        --store->count;
    } else if (store->count == NOTIFICATION_LIMIT) remove_item(store, 0);
    store->items[store->count++] = copy;
    ++store->revision;
    if (!notification->silent) {
        for (i = 0; i < store->alertCount; ++i)
            if (!strcmp(store->alerts[i], notification->id)) return 1;
        if (store->alertCount == NOTIFICATION_LIMIT) {
            memmove(store->alerts, store->alerts + 1, (NOTIFICATION_LIMIT - 1) * sizeof(store->alerts[0]));
            --store->alertCount;
        }
        strcpy(store->alerts[store->alertCount++], notification->id);
    }
    return 1;
}

int notification_store_alert(NotificationStore *store, PhoneNotification *notification)
{
    const PhoneNotification *item;
    if (!store->alertCount) return 0;
    item = notification_store_get(store, store->alerts[0]);
    if (item) *notification = *item;
    memmove(store->alerts, store->alerts + 1, (size_t)(store->alertCount - 1) * sizeof(store->alerts[0]));
    --store->alertCount;
    return item != NULL;
}
