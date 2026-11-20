#ifndef CONNECT95_DISCOVERY_H
#define CONNECT95_DISCOVERY_H
#include <winsock2.h>
#include <windows.h>
#include "identity.h"

typedef struct {
    DiscoveryIdentity identity;
    WCHAR name[257];
    char address[16];
    DWORD seen;
} DiscoveryPeer;

int discovery_start(HWND window, UINT message);
int discovery_receive(DiscoveryPeer *peer);
void discovery_stop(void);
#endif
