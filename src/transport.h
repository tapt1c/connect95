#ifndef CONNECT95_TRANSPORT_H
#define CONNECT95_TRANSPORT_H
#include "discovery.h"
#include "credentials.h"
#include <mbedtls/ssl.h>
#include "vendor/cJSON.h"
typedef struct {
    SOCKET socket;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    HANDLE cancel;
    HANDLE transferCancel;
    BOOL secure;
} Connection;
void transport_init(Connection *connection, HANDLE cancel);
int transport_cancelled(Connection *connection);
int transport_connect(Connection *connection, const char *address, unsigned short port);
int transport_tls(Connection *connection, Credentials *credentials, const char *peerId, int role, BOOL pinned);
int transport_write(Connection *connection, const char *data, size_t length, BOOL encrypted);
int transport_json(Connection *connection, cJSON *packet, BOOL encrypted);
int transport_read(Connection *connection, unsigned char *data, size_t size);
void transport_close(Connection *connection);
#endif
