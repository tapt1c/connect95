#include "network_service.h"
#include "session.h"
#include "saved_devices.h"
#include "credentials.h"
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <psa/crypto.h>

static HANDLE thread, stopEvent, announceEvent;
static HWND recipient;
static UINT incomingMessage;
static volatile LONG listeningPort;
static char localId[39];

static void announce(SOCKET udp, const char *id, unsigned short port)
{
    struct sockaddr_in destination;
    cJSON *packet = session_identity_packet(id, port);
    char *text = packet ? cJSON_PrintUnformatted(packet) : NULL;
    ZeroMemory(&destination, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = INADDR_BROADCAST;
    destination.sin_port = htons(1716);
    if (text) sendto(udp, text, (int)strlen(text), 0, (struct sockaddr *)&destination, sizeof(destination));
    cJSON_free(text);
    cJSON_Delete(packet);
}

static BOOL receive_identity(SOCKET socket, DiscoveryPeer *peer)
{
    char line[IDENTITY_MAX_PACKET + 1];
    DWORD start = GetTickCount();
    size_t used = 0;
    while (used < IDENTITY_MAX_PACKET && WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0 &&
           (DWORD)(GetTickCount() - start) < 5000) {
        int result = recv(socket, line + used, 1, 0);
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            WaitForSingleObject(stopEvent, 10);
            continue;
        }
        if (result != 1 || !line[used]) return FALSE;
        if (line[used++] != '\n') continue;
        line[used] = 0;
        return identity_connection_parse(line, used, &peer->identity, localId) && peer->identity.version == 7 &&
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, peer->identity.name, -1, peer->name, ARRAYSIZE(peer->name));
    }
    return FALSE;
}

static unsigned __stdcall serve(void *argument)
{
    WSADATA data;
    Credentials credentials;
    char id[39];
    SOCKET listener = INVALID_SOCKET, udp = INVALID_SOCKET;
    struct sockaddr_in address;
    BOOL enabled = TRUE;
    u_long nonblocking = 1;
    unsigned short port;
    DWORD lastAnnouncement = 0;
    (void)argument;
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 0;
    credentials_init(&credentials);
    if (!credentials_load(&credentials)) {
        credentials_free(&credentials);
        goto finish;
    }
    strcpy(id, credentials.id);
    strcpy(localId, id);
    credentials_free(&credentials); /* Release the profile lock before sessions start. */
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET || setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (char *)&enabled, sizeof(enabled)) || ioctlsocket(listener, FIONBIO, &nonblocking)) goto finish;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    for (port = 1716; port <= 1738; ++port) {
        address.sin_port = htons(port);
        if (!bind(listener, (struct sockaddr *)&address, sizeof(address))) break;
    }
    if (port > 1738 || listen(listener, 4)) goto finish;
    InterlockedExchange(&listeningPort, port);
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp != INVALID_SOCKET) setsockopt(udp, SOL_SOCKET, SO_BROADCAST, (char *)&enabled, sizeof(enabled));
    while (WaitForSingleObject(stopEvent, 50) != WAIT_OBJECT_0) {
        int length = sizeof(address);
        SOCKET socket;
        if (!lastAnnouncement || (DWORD)(GetTickCount() - lastAnnouncement) >= 30000 ||
            WaitForSingleObject(announceEvent, 0) == WAIT_OBJECT_0) {
            announce(udp, id, port);
            ResetEvent(announceEvent);
            lastAnnouncement = GetTickCount();
        }
        socket = accept(listener, (struct sockaddr *)&address, &length);
        if (socket != INVALID_SOCKET) {
            IncomingConnection *incoming = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*incoming));
            if (incoming && !ioctlsocket(socket, FIONBIO, &nonblocking) && receive_identity(socket, &incoming->peer) &&
                strcmp(incoming->peer.identity.id, id) && saved_device_exists(incoming->peer.identity.id) == 1) {
                unsigned long host = ntohl(address.sin_addr.s_addr);
                snprintf(incoming->peer.address, sizeof(incoming->peer.address), "%lu.%lu.%lu.%lu",
                    (host >> 24) & 255, (host >> 16) & 255, (host >> 8) & 255, host & 255);
                incoming->peer.seen = GetTickCount();
                incoming->socket = socket;
                if (PostMessageW(recipient, incomingMessage, 0, (LPARAM)incoming)) continue;
            }
            if (incoming) HeapFree(GetProcessHeap(), 0, incoming);
            closesocket(socket);
        }
    }
finish:
    InterlockedExchange(&listeningPort, 0);
    if (listener != INVALID_SOCKET) closesocket(listener);
    if (udp != INVALID_SOCKET) closesocket(udp);
    WSACleanup();
    return 0;
}

BOOL network_service_start(HWND owner, UINT message)
{
    network_service_stop();
    recipient = owner;
    incomingMessage = message;
    stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    announceEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!stopEvent || !announceEvent) { network_service_stop(); return FALSE; }
    thread = (HANDLE)_beginthreadex(NULL, 0, serve, NULL, 0, NULL);
    return thread != NULL;
}

void network_service_announce(void) { if (announceEvent) SetEvent(announceEvent); }
unsigned short network_service_port(void) { return (unsigned short)InterlockedCompareExchange(&listeningPort, 0, 0); }
BOOL network_service_is_self(const char *id) { return network_service_port() && !strcmp(localId, id); }

void network_service_stop(void)
{
    if (thread) { SetEvent(stopEvent); WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    if (stopEvent) CloseHandle(stopEvent);
    if (announceEvent) CloseHandle(announceEvent);
    thread = stopEvent = announceEvent = NULL;
}
