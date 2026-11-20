#include "identity.h"
#include "vendor/cJSON.h"
#include <string.h>

/* Security-relevant fields must be unambiguous. Unknown fields are allowed. */
static const cJSON *field(const cJSON *object, const char *name)
{
    const cJSON *item, *found = NULL;
    cJSON_ArrayForEach(item, object) {
        if (item->string && strcmp(item->string, name) == 0) {
            if (found) return NULL;
            found = item;
        }
    }
    return found;
}

static int unique_keys(const cJSON *object)
{
    const cJSON *item, *other;
    cJSON_ArrayForEach(item, object) {
        for (other = item->next; other; other = other->next)
            if (item->string && other->string && !strcmp(item->string, other->string)) return 0;
    }
    return 1;
}

static int copy_text(const cJSON *item, char *target, size_t capacity)
{
    size_t size;
    if (!cJSON_IsString(item)) return 0;
    size = strlen(item->valuestring);
    if (!size || size >= capacity) return 0;
    memcpy(target, item->valuestring, size + 1);
    return 1;
}

int identity_parse(const char *data, size_t size, DiscoveryIdentity *identity)
{
    cJSON *root;
    const cJSON *body, *type, *port, *version, *name;
    const char *end;
    DiscoveryIdentity parsed = {0};
    size_t i;
    int valid = 0;
    if (!data || !identity || !size || size > IDENTITY_MAX_PACKET) return 0;
    /* Reject embedded NUL, including escaped NUL which C string APIs truncate. */
    for (i = 0; i < size; ++i) {
        if (!data[i]) return 0;
        if (data[i] == '\\') {
            if (i + 5 < size && memcmp(data + i, "\\u0000", 6) == 0) return 0;
            if (i + 1 < size) ++i;
        }
    }
    root = cJSON_ParseWithLengthOpts(data, size, &end, 0);
    if (!root) return 0;
    while (end < data + size && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    body = field(root, "body");
    type = field(root, "type");
    if (end != data + size || !cJSON_IsObject(root) || !cJSON_IsObject(body) ||
        !unique_keys(root) || !unique_keys(body) ||
        !cJSON_IsString(type) || strcmp(type->valuestring, "kdeconnect.identity")) goto done;
    if (!copy_text(field(body, "deviceId"), parsed.id, sizeof(parsed.id))) goto done;
    if (strlen(parsed.id) < 32) goto done;
    for (i = 0; parsed.id[i]; ++i) {
        char c = parsed.id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) goto done;
    }
    port = field(body, "tcpPort");
    version = field(body, "protocolVersion");
    if (!cJSON_IsNumber(port) || port->valuedouble < 1716 || port->valuedouble > 1764 ||
        port->valuedouble != port->valueint) goto done;
    if (!cJSON_IsNumber(version) || version->valuedouble < 7 || version->valuedouble > 8 ||
        version->valuedouble != version->valueint) goto done;
    parsed.port = (unsigned short)port->valueint;
    parsed.version = version->valueint;
    name = field(body, "deviceName");
    if (name && !copy_text(name, parsed.name, sizeof(parsed.name))) goto done;
    for (i = 0; parsed.name[i]; ++i)
        if ((unsigned char)parsed.name[i] < 32) goto done;
    *identity = parsed;
    valid = 1;
done:
    cJSON_Delete(root);
    return valid;
}
