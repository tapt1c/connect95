#ifndef CONNECTXP_DEVICE_PLUGINS_H
#define CONNECTXP_DEVICE_PLUGINS_H
#include "packet.h"
#include <stdint.h>
#define CLIPBOARD_MAX_BYTES 8192
typedef struct {
    char content[CLIPBOARD_MAX_BYTES + 1];
    uint64_t timestamp;
    int initial;
    unsigned int revision;
} ClipboardMessage;
int plugin_battery(const cJSON *packet, int *charge, int *charging);
int plugin_clipboard(const cJSON *packet, ClipboardMessage *message);
int plugin_ping(const cJSON *packet, char *message, size_t capacity);
cJSON *plugin_ping_packet(void);
cJSON *plugin_clipboard_packet(const char *content);
#endif
