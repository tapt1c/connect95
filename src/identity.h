#ifndef CONNECT95_IDENTITY_H
#define CONNECT95_IDENTITY_H
#include <stddef.h>
#define IDENTITY_MAX_PACKET 8192

typedef struct {
    char id[39];
    char name[257];
    unsigned short port;
    int version;
} DiscoveryIdentity;

/* Copies a validated, unauthenticated UDP identity. Never establishes trust. */
int identity_parse(const char *data, size_t size, DiscoveryIdentity *identity);
#endif
