#include "share_info.h"
#include <string.h>

int share_filename_valid(const char *name)
{
    size_t length = strlen(name), i, stem;
    char upper[10] = {0};
    if (!length || length > 768 || name[length - 1] == '.' || name[length - 1] == ' ') return 0;
    for (i = 0; i < length; ++i)
        if ((unsigned char)name[i] < 32 || strchr("/\\:*?\"<>|", name[i])) return 0;
    stem = strcspn(name, ".");
    if (stem < sizeof(upper)) {
        for (i = 0; i < stem; ++i) {
            char ch = name[i];
            upper[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
        }
        if (!strcmp(upper, "CON") || !strcmp(upper, "PRN") || !strcmp(upper, "AUX") ||
            !strcmp(upper, "NUL") || !strcmp(upper, "CLOCK$") || !strcmp(upper, "CONIN$") ||
            !strcmp(upper, "CONOUT$")) return 0;
        if (stem == 4 && (!memcmp(upper, "COM", 3) || !memcmp(upper, "LPT", 3)) &&
            upper[3] >= '1' && upper[3] <= '9') return 0;
    }
    return 1;
}

int share_info(const cJSON *packet, ShareInfo *info)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    const cJSON *name, *size, *transfer, *port;
    ShareInfo parsed = {{0}, 0, 0, 0};
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "kdeconnect.share.request")) return 0;
    name = cJSON_GetObjectItemCaseSensitive(body, "filename");
    if (!name) return 0; /* Text and URL sharing are separate plugin functions. */
    if (!cJSON_IsString(name) || !share_filename_valid(name->valuestring)) return -1;
    strcpy(parsed.filename, name->valuestring);
    size = cJSON_GetObjectItemCaseSensitive(packet, "payloadSize");
    if (size) {
        if (!cJSON_IsNumber(size) || !(size->valuedouble >= -1 && size->valuedouble <= 9007199254740991.0)) return -1;
        if (size->valuedouble == -1) parsed.unknownSize = 1;
        else {
            if (size->valuedouble < 0) return -1;
            parsed.size = (uint64_t)size->valuedouble;
            if ((double)parsed.size != size->valuedouble) return -1;
        }
    }
    transfer = cJSON_GetObjectItemCaseSensitive(packet, "payloadTransferInfo");
    if (transfer) {
        const cJSON *entry;
        int count = 0;
        if (!cJSON_IsObject(transfer)) return -1;
        cJSON_ArrayForEach(entry, transfer)
            if (entry->string && !strcmp(entry->string, "port")) ++count;
        port = cJSON_GetObjectItemCaseSensitive(transfer, "port");
        if (count != 1 || !cJSON_IsNumber(port) || port->valuedouble < 1024 ||
            port->valuedouble > 65535 || port->valuedouble != port->valueint) return -1;
        parsed.port = (unsigned short)port->valueint;
    }
    if (!size && parsed.port) parsed.unknownSize = 1;
    if ((parsed.size || parsed.unknownSize) && !parsed.port) return -1;
    *info = parsed;
    return 1;
}

cJSON *share_packet(const char *name, uint64_t size, unsigned short port)
{
    cJSON *packet = packet_create("kdeconnect.share.request");
    cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    cJSON *transfer;
    if (!packet || !cJSON_AddStringToObject(body, "filename", name) ||
        !cJSON_AddNumberToObject(body, "numberOfFiles", 1) ||
        !cJSON_AddNumberToObject(body, "totalPayloadSize", (double)size) ||
        !cJSON_AddNumberToObject(packet, "payloadSize", (double)size)) goto failed;
    if (port) {
        transfer = cJSON_AddObjectToObject(packet, "payloadTransferInfo");
        if (!transfer || !cJSON_AddNumberToObject(transfer, "port", port)) goto failed;
    }
    return packet;
failed:
    cJSON_Delete(packet);
    return NULL;
}

