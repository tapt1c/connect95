#include "session.h"
#include "credentials.h"
#include "pairing.h"
#include "transport.h"
#include "transfer.h"
#include "vendor/cJSON.h"
#include <mbedtls/ssl.h>
#include <psa/crypto.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static CRITICAL_SECTION guard;
static HANDLE worker, cancelEvent, confirmEvent;
static BOOL accepted, ringPending;
static HANDLE transferCancel;
static WCHAR pendingPath[MAX_PATH];
static SessionStatus status;
static DiscoveryPeer target;

static void update(int phase, const WCHAR *text)
{
    EnterCriticalSection(&guard);
    status.phase = phase;
    lstrcpynW(status.text, text, ARRAYSIZE(status.text));
    LeaveCriticalSection(&guard);
}

static int cancelled(void)
{
    return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
}

static void transfer_progress(const WCHAR *filename, ULONGLONG done, ULONGLONG total)
{
    char numbers[96];
    WCHAR wideNumbers[96];
    if (total == UINT64_MAX) snprintf(numbers, sizeof(numbers), "%.2f MB", (double)done / 1048576);
    else snprintf(numbers, sizeof(numbers), "%.2f / %.2f MB", (double)done / 1048576, (double)total / 1048576);
    MultiByteToWideChar(CP_ACP, 0, numbers, -1, wideNumbers, ARRAYSIZE(wideNumbers));
    EnterCriticalSection(&guard);
    status.transferPercent = total && total != UINT64_MAX ? (int)((double)done / (double)total * 100) : 0;
    wsprintfW(status.transferText, L"%.200s — %s", filename, wideNumbers);
    LeaveCriticalSection(&guard);
}

static void transfer_finished(const WCHAR *error, const WCHAR *success)
{
    EnterCriticalSection(&guard);
    status.transferActive = FALSE;
    if (!error) status.transferPercent = 100;
    lstrcpynW(status.transferText, error ? error : success, ARRAYSIZE(status.transferText));
    LeaveCriticalSection(&guard);
}

static int process_commands(Connection *connection, Credentials *credentials)
{
    BOOL ring;
    WCHAR path[MAX_PATH];
    TransferContext context = {connection, credentials, &target, transferCancel, transfer_progress};
    EnterCriticalSection(&guard);
    ring = ringPending;
    ringPending = FALSE;
    lstrcpyW(path, pendingPath);
    pendingPath[0] = 0;
    LeaveCriticalSection(&guard);
    if (ring) {
        cJSON *packet = packet_create("kdeconnect.findmyphone.request");
        int result = packet && transport_json(connection, packet, TRUE);
        cJSON_Delete(packet);
        if (!result) return 0;
    }
    if (path[0]) transfer_finished(transfer_send(&context, path), L"File sent to the phone.");
    return 1;
}

static void receive_share(Connection *connection, Credentials *credentials, const ShareInfo *offer)
{
    TransferContext context = {connection, credentials, &target, transferCancel, transfer_progress};
    EnterCriticalSection(&guard);
    if (status.transferActive) {
        LeaveCriticalSection(&guard);
        return;
    }
    status.transferActive = TRUE;
    status.transferPercent = 0;
    ResetEvent(transferCancel);
    LeaveCriticalSection(&guard);
    transfer_finished(transfer_receive(&context, offer), L"File saved in My Documents\\Connect95.");
}

static cJSON *identity_packet(Credentials *credentials)
{
    cJSON *packet = cJSON_CreateObject();
    cJSON *body = cJSON_AddObjectToObject(packet, "body");
    cJSON *incoming, *outgoing;
    if (!packet || !body || !cJSON_AddNumberToObject(packet, "id", (double)time(NULL) * 1000) ||
        !cJSON_AddStringToObject(packet, "type", "kdeconnect.identity") ||
        !cJSON_AddStringToObject(body, "deviceId", credentials->id) ||
        !cJSON_AddStringToObject(body, "deviceName", "Connect95") ||
        !cJSON_AddStringToObject(body, "deviceType", "desktop") ||
        !cJSON_AddNumberToObject(body, "protocolVersion", 7) ||
        !(incoming = cJSON_AddArrayToObject(body, "incomingCapabilities")) ||
        !(outgoing = cJSON_AddArrayToObject(body, "outgoingCapabilities")) ||
        !cJSON_AddItemToArray(incoming, cJSON_CreateString("kdeconnect.share.request")) ||
        !cJSON_AddItemToArray(outgoing, cJSON_CreateString("kdeconnect.share.request")) ||
        !cJSON_AddItemToArray(outgoing, cJSON_CreateString("kdeconnect.findmyphone.request"))) {
        cJSON_Delete(packet);
        return NULL;
    }
    return packet;
}

static int send_pair(Connection *connection, BOOL pair)
{
    cJSON *packet = cJSON_CreateObject();
    cJSON *body = cJSON_AddObjectToObject(packet, "body");
    int result = packet && body && cJSON_AddNumberToObject(packet, "id", (double)time(NULL) * 1000) &&
        cJSON_AddStringToObject(packet, "type", "kdeconnect.pair") &&
        cJSON_AddBoolToObject(body, "pair", pair) &&
        cJSON_AddNumberToObject(body, "timestamp", (double)time(NULL)) && transport_json(connection, packet, TRUE);
    cJSON_Delete(packet);
    return result;
}

static unsigned __stdcall run_session(void *argument)
{
    Connection connection;
    Credentials credentials;
    WSADATA winsock;
    const mbedtls_x509_crt *peerCertificate;
    cJSON *packet = NULL;
    char line[IDENTITY_MAX_PACKET + 1];
    size_t used = 0;
    DWORD deadline, partialSince = 0;
    int result, pinned, phase = SESSION_BUSY;
    WCHAR detailedFailure[320];
    int tlsError = 0;
    const WCHAR *failure = L"Could not establish the connection.";
    (void)argument;
    transport_init(&connection, cancelEvent);
    credentials_init(&credentials);
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) goto no_winsock;
    if (psa_crypto_init() != PSA_SUCCESS) goto finish;
    update(SESSION_BUSY, L"Preparing the PC certificate…");
    failure = L"Cannot load the PC key. Check the user profile and other running copies of Connect95.";
    if (!credentials_load(&credentials)) goto finish;
    update(SESSION_BUSY, L"Connecting to the phone…");
    failure = L"Cannot reach the phone over TCP. Refresh discovery on the phone and retry.";
    if (!transport_connect(&connection, target.address, target.identity.port)) goto finish;
    packet = identity_packet(&credentials);
    if (!packet || !transport_json(&connection, packet, FALSE)) goto finish;
    cJSON_Delete(packet);
    packet = NULL;
    update(SESSION_BUSY, L"TLS 1.2 — checking the phone certificate…");
    failure = L"TLS or certificate error. Check the date on both devices.";
    tlsError = transport_tls(&connection, &credentials, target.identity.id, MBEDTLS_SSL_IS_SERVER, FALSE);
    if (tlsError) goto finish;
    peerCertificate = mbedtls_ssl_get_peer_cert(&connection.ssl);
    failure = L"The phone certificate changed or its saved certificate is unreadable. Pairing stopped.";
    pinned = credentials_pin(&credentials, target.identity.id, peerCertificate, FALSE);
    if (pinned < 0) goto finish;
    if (pinned) {
        phase = SESSION_PAIRED;
        update(phase, L"Paired. TLS connection established.");
    } else {
        failure = L"Could not send the pairing request.";
        char code[9];
        if (!pairing_code(&credentials.certificate.pk,
                (mbedtls_pk_context *)&peerCertificate->pk, code) || !send_pair(&connection, TRUE)) goto finish;
        EnterCriticalSection(&guard);
        MultiByteToWideChar(CP_ACP, 0, code, -1, status.code, ARRAYSIZE(status.code));
        LeaveCriticalSection(&guard);
        phase = SESSION_WAIT_PHONE;
        update(phase, L"Accept pairing on the phone and compare the code.");
    }
    deadline = GetTickCount();
    failure = L"TLS connection closed. Select Pair / connect to reconnect.";
    while (!cancelled()) {
        unsigned char byte;
        if (!used && phase == SESSION_PAIRED && !process_commands(&connection, &credentials)) goto finish;
        if (phase == SESSION_WAIT_PHONE && (DWORD)(GetTickCount() - deadline) > 30000) {
            send_pair(&connection, FALSE);
            failure = L"Pairing timed out. Please try again.";
            goto finish;
        }
        if (used && (DWORD)(GetTickCount() - partialSince) > 10000) {
            failure = L"The phone sent an incomplete packet.";
            goto finish;
        }
        result = mbedtls_ssl_read(&connection.ssl, &byte, 1);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            WaitForSingleObject(cancelEvent, 25);
            continue;
        }
        if (result <= 0) goto finish;
        if (!used) partialSince = GetTickCount();
        if (!byte || used == IDENTITY_MAX_PACKET) {
            failure = L"The phone sent an invalid or oversized packet.";
            goto finish;
        }
        line[used++] = (char)byte;
        if (byte != '\n') continue;
        line[used] = 0;
        result = pairing_message(line, used);
        if (result == -2) {
            failure = L"The phone sent an invalid packet.";
            goto finish;
        }
        if (result == -1) {
            if (!credentials_forget(&credentials, target.identity.id)) {
                failure = L"The phone unpaired, but its local trust record could not be removed.";
                goto finish;
            }
            failure = L"The phone rejected or cancelled pairing.";
            goto finish;
        }
        if (result == 1 && phase == SESSION_WAIT_PHONE) {
            HANDLE events[] = {cancelEvent, confirmEvent};
            update(SESSION_CONFIRM, L"The phone accepted. Confirm the matching code on this PC.");
            if (WaitForMultipleObjects(2, events, FALSE, 60000) != WAIT_OBJECT_0 + 1 || !accepted) {
                send_pair(&connection, FALSE);
                failure = L"Pairing cancelled on this PC.";
                goto finish;
            }
            if (credentials_pin(&credentials, target.identity.id, peerCertificate, TRUE) != 1) {
                send_pair(&connection, FALSE);
                failure = L"Could not save the phone certificate.";
                goto finish;
            }
            phase = SESSION_PAIRED;
            update(phase, L"Paired. TLS connection established.");
        }
        if (result == 0 && phase == SESSION_PAIRED) {
            ShareInfo offer;
            cJSON *received = packet_parse(line, used);
            int sharing = share_info(received, &offer);
            cJSON_Delete(received);
            if (sharing > 0) receive_share(&connection, &credentials, &offer);
            else if (sharing < 0) transfer_finished(L"The incoming file announcement is invalid or unsupported.", NULL);
        }
        used = 0;
    }
finish:
    EnterCriticalSection(&guard);
    if (status.transferActive) lstrcpyW(status.transferText, L"Transfer interrupted.");
    status.transferActive = FALSE;
    pendingPath[0] = 0;
    ringPending = FALSE;
    LeaveCriticalSection(&guard);
    cJSON_Delete(packet);
    transport_close(&connection);
    WSACleanup();
no_winsock:
    credentials_free(&credentials);
    if (cancelled()) update(SESSION_CLOSED, L"Disconnected.");
    else {
        if (tlsError) {
            wsprintfW(detailedFailure, L"%s (TLS -0x%04X)", failure, -tlsError);
            failure = detailedFailure;
        }
        update(SESSION_ERROR, failure);
    }
    return 0;
}

void session_init(void)
{
    InitializeCriticalSection(&guard);
    cancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    confirmEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    transferCancel = CreateEventW(NULL, TRUE, FALSE, NULL);
}

void session_status(SessionStatus *snapshot)
{
    EnterCriticalSection(&guard);
    *snapshot = status;
    LeaveCriticalSection(&guard);
}

void session_confirm(BOOL consent)
{
    accepted = consent;
    SetEvent(confirmEvent);
}

void session_stop(void)
{
    if (!worker) return;
    SetEvent(cancelEvent);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    worker = NULL;
}

int session_start(const DiscoveryPeer *peer)
{
    if (!cancelEvent || !confirmEvent || !transferCancel || peer->identity.version != 7) return 0;
    session_stop();
    ResetEvent(cancelEvent);
    ResetEvent(confirmEvent);
    accepted = FALSE;
    target = *peer;
    EnterCriticalSection(&guard);
    ZeroMemory(&status, sizeof(status));
    strcpy(status.deviceId, peer->identity.id);
    status.phase = SESSION_BUSY;
    lstrcpyW(status.text, L"Connecting…");
    LeaveCriticalSection(&guard);
    worker = (HANDLE)_beginthreadex(NULL, 0, run_session, NULL, 0, NULL);
    if (!worker) update(SESSION_ERROR, L"Could not start the network worker.");
    return worker != NULL;
}

void session_free(void)
{
    session_stop();
    if (cancelEvent) CloseHandle(cancelEvent);
    if (confirmEvent) CloseHandle(confirmEvent);
    if (transferCancel) CloseHandle(transferCancel);
    DeleteCriticalSection(&guard);
}

int session_forget(const DiscoveryPeer *peer)
{
    Credentials credentials;
    int result;
    session_stop();
    credentials_init(&credentials);
    result = credentials_load(&credentials) && credentials_forget(&credentials, peer->identity.id);
    credentials_free(&credentials);
    EnterCriticalSection(&guard);
    strcpy(status.deviceId, peer->identity.id);
    LeaveCriticalSection(&guard);
    update(SESSION_CLOSED, result ? L"Pairing forgotten on this PC. You can pair again." :
                                 L"Could not remove pairing from the user profile.");
    return result;
}

int session_ring(void)
{
    int ready;
    EnterCriticalSection(&guard);
    ready = status.phase == SESSION_PAIRED;
    if (ready) ringPending = TRUE;
    LeaveCriticalSection(&guard);
    return ready;
}

int session_send_file(const WCHAR *path)
{
    int ready;
    if (!path[0] || lstrlenW(path) >= MAX_PATH) return 0;
    EnterCriticalSection(&guard);
    ready = status.phase == SESSION_PAIRED && !status.transferActive && !pendingPath[0];
    if (ready) {
        lstrcpyW(pendingPath, path);
        status.transferActive = TRUE;
        status.transferPercent = 0;
        lstrcpyW(status.transferText, L"Preparing to send the file…");
        ResetEvent(transferCancel);
    }
    LeaveCriticalSection(&guard);
    return ready;
}

void session_cancel_transfer(void)
{
    EnterCriticalSection(&guard);
    if (status.transferActive) SetEvent(transferCancel);
    LeaveCriticalSection(&guard);
}
