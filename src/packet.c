#include "packet.h"
#include <string.h>
#include <time.h>

static int unique_keys(const cJSON *object)
{
    const cJSON *item, *other;
    cJSON_ArrayForEach(item, object) {
        for (other = item->next; other; other = other->next)
            if (item->string && other->string && !strcmp(item->string, other->string)) return 0;
    }
    return 1;
}

cJSON *packet_parse(const char *line, size_t length)
{
    cJSON *packet;
    const cJSON *type, *body;
    const char *end;
    size_t i;
    if (!line || !length || length > PACKET_MAX_SIZE) return NULL;
    for (i = 0; i < length; ++i) {
        if (!line[i]) return NULL;
        if (line[i] == '\\') {
            if (i + 5 < length && !memcmp(line + i, "\\u0000", 6)) return NULL;
            if (i + 1 < length) ++i;
        }
    }
    packet = cJSON_ParseWithLengthOpts(line, length, &end, 0);
    if (!packet) return NULL;
    while (end < line + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    if (end != line + length || !cJSON_IsObject(packet) || !unique_keys(packet) ||
        !cJSON_IsObject(body) || !unique_keys(body) || !cJSON_IsString(type)) {
        cJSON_Delete(packet);
        return NULL;
    }
    return packet;
}

cJSON *packet_create(const char *type)
{
    cJSON *packet = cJSON_CreateObject();
    if (!packet || !cJSON_AddStringToObject(packet, "type", type) ||
        !cJSON_AddNumberToObject(packet, "id", (double)time(NULL) * 1000) ||
        !cJSON_AddObjectToObject(packet, "body")) {
        cJSON_Delete(packet);
        return NULL;
    }
    return packet;
}
