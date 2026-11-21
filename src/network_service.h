#ifndef CONNECTXP_NETWORK_SERVICE_H
#define CONNECTXP_NETWORK_SERVICE_H
#include "discovery.h"
typedef struct { DiscoveryPeer peer; SOCKET socket; } IncomingConnection;
BOOL network_service_start(HWND owner, UINT message);
void network_service_announce(void);
void network_service_stop(void);
unsigned short network_service_port(void);
BOOL network_service_is_self(const char *id);
#endif
