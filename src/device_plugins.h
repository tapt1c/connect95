#ifndef CONNECT95_DEVICE_PLUGINS_H
#define CONNECT95_DEVICE_PLUGINS_H
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
cJSON *plugin_clipboard_packet(const char *content);
#endif
