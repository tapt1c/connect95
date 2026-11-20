#ifndef CONNECT95_SHARE_INFO_H
#define CONNECT95_SHARE_INFO_H
#include "packet.h"
#include <stdint.h>
typedef struct {
    char filename[769];
    uint64_t size;
    int unknownSize;
    unsigned short port;
} ShareInfo;
cJSON *share_packet(const char *name, uint64_t size, unsigned short port);
int share_filename_valid(const char *name);
/* 1 file offer, 0 unrelated packet, -1 invalid file offer. */
int share_info(const cJSON *packet, ShareInfo *info);
#endif
