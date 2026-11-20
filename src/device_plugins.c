#include "device_plugins.h"
#include <string.h>

int plugin_battery(const cJSON *packet, int *charge, int *charging)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    const cJSON *level, *plugged;
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "kdeconnect.battery")) return 0;
    level = cJSON_GetObjectItemCaseSensitive(body, "currentCharge");
    plugged = cJSON_GetObjectItemCaseSensitive(body, "isCharging");
    if (!cJSON_IsNumber(level) || level->valuedouble < -1 || level->valuedouble > 100 ||
        level->valuedouble != level->valueint || !cJSON_IsBool(plugged)) return 0;
    *charge = level->valueint;
    *charging = cJSON_IsTrue(plugged);
    return 1;
}

int plugin_clipboard(const cJSON *packet, ClipboardMessage *message)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    const cJSON *content, *timestamp;
    size_t length;
    int initial;
    if (!cJSON_IsString(type)) return 0;
    initial = !strcmp(type->valuestring, "kdeconnect.clipboard.connect");
    if (!initial && strcmp(type->valuestring, "kdeconnect.clipboard")) return 0;
    content = cJSON_GetObjectItemCaseSensitive(body, "content");
    if (!cJSON_IsString(content)) return 0;
    length = strlen(content->valuestring);
    if (length > CLIPBOARD_MAX_BYTES) return 0;
    message->timestamp = 0;
    if (initial) {
        timestamp = cJSON_GetObjectItemCaseSensitive(body, "timestamp");
        if (!cJSON_IsNumber(timestamp) || !(timestamp->valuedouble > 0 && timestamp->valuedouble <= 9007199254740991.0) ||
            (double)(uint64_t)timestamp->valuedouble != timestamp->valuedouble) return 0;
        message->timestamp = (uint64_t)timestamp->valuedouble;
    }
    memcpy(message->content, content->valuestring, length + 1);
    message->initial = initial;
    message->revision = 0;
    return 1;
}

cJSON *plugin_clipboard_packet(const char *content)
{
    cJSON *packet;
    cJSON *body;
    if (strlen(content) > CLIPBOARD_MAX_BYTES) return NULL;
    packet = packet_create("kdeconnect.clipboard");
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    if (!packet || !cJSON_AddStringToObject(body, "content", content)) {
        cJSON_Delete(packet);
        return NULL;
    }
    return packet;
}
