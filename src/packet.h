#ifndef CONNECT95_PACKET_H
#define CONNECT95_PACKET_H
#include <stddef.h>
#define PACKET_MAX_SIZE 65536
#include "vendor/cJSON.h"
cJSON *packet_parse(const char *line, size_t length);
cJSON *packet_create(const char *type);
#endif
