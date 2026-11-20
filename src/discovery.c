#include "discovery.h"
#include <stdio.h>
#include <string.h>

static SOCKET udp = INVALID_SOCKET;
static BOOL started;

void discovery_stop(void)
{
    if (udp != INVALID_SOCKET) closesocket(udp);
    udp = INVALID_SOCKET;
    if (started) WSACleanup();
    started = FALSE;
}

int discovery_start(HWND window, UINT message)
{
    WSADATA data;
    struct sockaddr_in local;
    BOOL exclusive = TRUE;
    int error;
    discovery_stop();
    error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error) return error;
    started = TRUE;
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET) goto failed;
    /* Do not silently share discovery traffic with another local client. */
    if (setsockopt(udp, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (const char *)&exclusive, sizeof(exclusive)) == SOCKET_ERROR) goto failed;
    ZeroMemory(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(1716);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udp, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) goto failed;
    if (WSAAsyncSelect(udp, window, message, FD_READ) == SOCKET_ERROR) goto failed;
    return 0;
failed:
    error = WSAGetLastError();
    discovery_stop();
    return error;
}

/* 1 = valid announcement, 2 = ignored datagram, 0 = queue empty, -1 = error. */
int discovery_receive(DiscoveryPeer *peer)
{
    char packet[IDENTITY_MAX_PACKET + 1];
    struct sockaddr_in source;
    int sourceSize = sizeof(source);
    int size, error;
    unsigned long address;
    if (udp == INVALID_SOCKET) return 0;
    size = recvfrom(udp, packet, sizeof(packet), 0, (struct sockaddr *)&source, &sourceSize);
    if (size == SOCKET_ERROR) {
        error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return 0;
        if (error == WSAEMSGSIZE || error == WSAECONNRESET) return 2;
        return -1;
    }
    if (!identity_parse(packet, (size_t)size, &peer->identity)) return 2;
    address = ntohl(source.sin_addr.s_addr);
    if (!address || (address >> 24) >= 224) return 2;
    if (peer->identity.name[0]) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, peer->identity.name,
                                -1, peer->name, ARRAYSIZE(peer->name))) return 2;
    } else {
        lstrcpyW(peer->name, L"KDE Connect device");
    }
    snprintf(peer->address, sizeof(peer->address), "%lu.%lu.%lu.%lu",
             (address >> 24) & 255, (address >> 16) & 255, (address >> 8) & 255, address & 255);
    peer->seen = GetTickCount();
    return 1;
}
