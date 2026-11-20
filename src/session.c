#include "session.h"
#include "credentials.h"
#include "pairing.h"
#include "vendor/cJSON.h"
#include <mbedtls/ssl.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <psa/crypto.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static CRITICAL_SECTION guard;
static HANDLE worker, cancelEvent, confirmEvent;
static BOOL accepted;
static SessionStatus status;
static DiscoveryPeer target;

typedef struct {
    SOCKET socket;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    Credentials credentials;
} Connection;

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

static int net_send(void *context, const unsigned char *data, size_t size)
{
    Connection *connection = context;
    int result = send(connection->socket, (const char *)data, (int)size, 0);
    if (result >= 0) return result;
    return WSAGetLastError() == WSAEWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int net_receive(void *context, unsigned char *data, size_t size)
{
    Connection *connection = context;
    int result = recv(connection->socket, (char *)data, (int)size, 0);
    if (result >= 0) return result;
    return WSAGetLastError() == WSAEWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int connect_tcp(Connection *connection)
{
    struct sockaddr_in address;
    u_long nonblocking = 1;
    DWORD start = GetTickCount();
    int result, error, size = sizeof(error);
    connection->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection->socket == INVALID_SOCKET ||
        ioctlsocket(connection->socket, FIONBIO, &nonblocking)) return 0;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(target.address);
    address.sin_port = htons(target.identity.port);
    result = connect(connection->socket, (struct sockaddr *)&address, sizeof(address));
    if (!result) return 1;
    if (WSAGetLastError() != WSAEWOULDBLOCK) return 0;
    while (!cancelled() && (DWORD)(GetTickCount() - start) < 10000) {
        fd_set writable, errors;
        struct timeval timeout = {0, 100000};
        FD_ZERO(&writable);
        FD_ZERO(&errors);
        FD_SET(connection->socket, &writable);
        FD_SET(connection->socket, &errors);
        result = select(0, NULL, &writable, &errors, &timeout);
        if (result < 0) return 0;
        if (result > 0) return !getsockopt(connection->socket, SOL_SOCKET, SO_ERROR,
            (char *)&error, &size) && !error;
    }
    return 0;
}

static int write_bytes(Connection *connection, const char *data, size_t length, BOOL encrypted)
{
    DWORD start = GetTickCount();
    while (length && !cancelled() && (DWORD)(GetTickCount() - start) < 5000) {
        int result = encrypted ? mbedtls_ssl_write(&connection->ssl, (const unsigned char *)data, length) :
                                 net_send(connection, (const unsigned char *)data, length);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            WaitForSingleObject(cancelEvent, 10);
            continue;
        }
        if (result <= 0) return 0;
        data += result;
        length -= (size_t)result;
    }
    return length == 0;
}

static int write_json(Connection *connection, cJSON *packet, BOOL encrypted)
{
    char *text = cJSON_PrintUnformatted(packet);
    int result;
    if (!text) return 0;
    result = write_bytes(connection, text, strlen(text), encrypted) &&
             write_bytes(connection, "\n", 1, encrypted);
    cJSON_free(text);
    return result;
}

static cJSON *identity_packet(Connection *connection)
{
    cJSON *packet = cJSON_CreateObject();
    cJSON *body = cJSON_AddObjectToObject(packet, "body");
    if (!packet || !body || !cJSON_AddNumberToObject(packet, "id", (double)time(NULL) * 1000) ||
        !cJSON_AddStringToObject(packet, "type", "kdeconnect.identity") ||
        !cJSON_AddStringToObject(body, "deviceId", connection->credentials.id) ||
        !cJSON_AddStringToObject(body, "deviceName", "Connect95") ||
        !cJSON_AddStringToObject(body, "deviceType", "desktop") ||
        !cJSON_AddNumberToObject(body, "protocolVersion", 7) ||
        !cJSON_AddArrayToObject(body, "incomingCapabilities") ||
        !cJSON_AddArrayToObject(body, "outgoingCapabilities")) {
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
        cJSON_AddNumberToObject(body, "timestamp", (double)time(NULL)) && write_json(connection, packet, TRUE);
    cJSON_Delete(packet);
    return result;
}

static unsigned __stdcall run_session(void *argument)
{
    Connection connection;
    WSADATA winsock;
    const mbedtls_x509_crt *peerCertificate;
    cJSON *packet = NULL;
    char line[IDENTITY_MAX_PACKET + 1];
    size_t used = 0;
    DWORD deadline, partialSince = 0;
    int result, pinned, phase = SESSION_BUSY;
    WCHAR detailedFailure[320];
    int tlsError = 0;
    const WCHAR *failure = L"Не удалось установить соединение.";
    (void)argument;
    ZeroMemory(&connection, sizeof(connection));
    connection.socket = INVALID_SOCKET;
    credentials_init(&connection.credentials);
    mbedtls_ssl_init(&connection.ssl);
    mbedtls_ssl_config_init(&connection.config);
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) goto no_winsock;
    if (psa_crypto_init() != PSA_SUCCESS) goto finish;
    update(SESSION_BUSY, L"Подготовка сертификата ПК…");
    failure = L"Не удалось загрузить ключ ПК. Проверьте профиль пользователя и другие экземпляры Connect95.";
    if (!credentials_load(&connection.credentials)) goto finish;
    update(SESSION_BUSY, L"Соединение с телефоном…");
    failure = L"Телефон недоступен по TCP. Обновите поиск на телефоне и повторите.";
    if (!connect_tcp(&connection)) goto finish;
    packet = identity_packet(&connection);
    if (!packet || !write_json(&connection, packet, FALSE)) goto finish;
    cJSON_Delete(packet);
    packet = NULL;
    update(SESSION_BUSY, L"TLS 1.2: проверка сертификата телефона…");
    failure = L"Ошибка TLS или сертификата. Проверьте дату на ПК и телефоне.";
    if (mbedtls_ssl_config_defaults(&connection.config, MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) goto finish;
    mbedtls_ssl_conf_min_tls_version(&connection.config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&connection.config, MBEDTLS_SSL_VERSION_TLS1_2);
    /* Enrollment requests a peer certificate. Trust is checked below before pairing. */
    mbedtls_ssl_conf_authmode(&connection.config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&connection.config, mbedtls_ctr_drbg_random, &connection.credentials.random);
    if (mbedtls_ssl_conf_own_cert(&connection.config, &connection.credentials.certificate,
            &connection.credentials.key) || mbedtls_ssl_setup(&connection.ssl, &connection.config)) goto finish;
    mbedtls_ssl_set_bio(&connection.ssl, &connection, net_send, net_receive, NULL);
    deadline = GetTickCount();
    do {
        result = mbedtls_ssl_handshake(&connection.ssl);
        if (!result) break;
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            tlsError = result;
            goto finish;
        }
        if (cancelled() || (DWORD)(GetTickCount() - deadline) > 15000) goto finish;
        WaitForSingleObject(cancelEvent, 10);
    } while (1);
    peerCertificate = mbedtls_ssl_get_peer_cert(&connection.ssl);
    if (!pairing_verify_peer(peerCertificate, target.identity.id, mbedtls_ssl_get_verify_result(&connection.ssl))) goto finish;
    failure = L"Сертификат телефона изменился или не удалось прочитать сохранённый сертификат. Сопряжение остановлено.";
    pinned = credentials_pin(&connection.credentials, target.identity.id, peerCertificate, FALSE);
    if (pinned < 0) goto finish;
    if (pinned) {
        phase = SESSION_PAIRED;
        update(phase, L"Сопряжено. TLS-соединение установлено.");
    } else {
        failure = L"Не удалось отправить запрос сопряжения.";
        char code[9];
        if (!pairing_code(&connection.credentials.certificate.pk,
                (mbedtls_pk_context *)&peerCertificate->pk, code) || !send_pair(&connection, TRUE)) goto finish;
        EnterCriticalSection(&guard);
        MultiByteToWideChar(CP_ACP, 0, code, -1, status.code, ARRAYSIZE(status.code));
        LeaveCriticalSection(&guard);
        phase = SESSION_WAIT_PHONE;
        update(phase, L"Подтвердите сопряжение на телефоне. Сверьте код.");
    }
    deadline = GetTickCount();
    failure = L"TLS-соединение закрыто. Для повторного подключения выберите «Сопрячь / подключиться».";
    while (!cancelled()) {
        unsigned char byte;
        if (phase == SESSION_WAIT_PHONE && (DWORD)(GetTickCount() - deadline) > 30000) {
            send_pair(&connection, FALSE);
            failure = L"Время ожидания сопряжения истекло. Повторите запрос.";
            goto finish;
        }
        if (used && (DWORD)(GetTickCount() - partialSince) > 10000) {
            failure = L"Телефон прислал незавершённый пакет.";
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
            failure = L"Некорректный или слишком большой пакет телефона.";
            goto finish;
        }
        line[used++] = (char)byte;
        if (byte != '\n') continue;
        line[used] = 0;
        result = pairing_message(line, used);
        if (result == -2) {
            failure = L"Некорректный пакет телефона.";
            goto finish;
        }
        if (result == -1) {
            if (!credentials_forget(&connection.credentials, target.identity.id)) {
                failure = L"Телефон отменил сопряжение, но локальную запись удалить не удалось.";
                goto finish;
            }
            failure = L"Телефон отклонил или отменил сопряжение.";
            goto finish;
        }
        if (result == 1 && phase == SESSION_WAIT_PHONE) {
            HANDLE events[] = {cancelEvent, confirmEvent};
            update(SESSION_CONFIRM, L"Телефон подтвердил запрос. Подтвердите совпадение кода на ПК.");
            if (WaitForMultipleObjects(2, events, FALSE, 60000) != WAIT_OBJECT_0 + 1 || !accepted) {
                send_pair(&connection, FALSE);
                failure = L"Сопряжение отменено на ПК.";
                goto finish;
            }
            if (credentials_pin(&connection.credentials, target.identity.id, peerCertificate, TRUE) != 1) {
                send_pair(&connection, FALSE);
                failure = L"Не удалось сохранить сертификат телефона.";
                goto finish;
            }
            phase = SESSION_PAIRED;
            update(phase, L"Сопряжено. TLS-соединение установлено.");
        }
        used = 0;
    }
finish:
    cJSON_Delete(packet);
    if (connection.socket != INVALID_SOCKET) closesocket(connection.socket);
    WSACleanup();
no_winsock:
    mbedtls_ssl_free(&connection.ssl);
    mbedtls_ssl_config_free(&connection.config);
    credentials_free(&connection.credentials);
    if (cancelled()) update(SESSION_CLOSED, L"Соединение остановлено.");
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
    if (!cancelEvent || !confirmEvent || peer->identity.version != 7) return 0;
    session_stop();
    ResetEvent(cancelEvent);
    ResetEvent(confirmEvent);
    accepted = FALSE;
    target = *peer;
    EnterCriticalSection(&guard);
    ZeroMemory(&status, sizeof(status));
    strcpy(status.deviceId, peer->identity.id);
    status.phase = SESSION_BUSY;
    lstrcpyW(status.text, L"Подключение…");
    LeaveCriticalSection(&guard);
    worker = (HANDLE)_beginthreadex(NULL, 0, run_session, NULL, 0, NULL);
    if (!worker) update(SESSION_ERROR, L"Не удалось запустить сетевой поток.");
    return worker != NULL;
}

void session_free(void)
{
    session_stop();
    if (cancelEvent) CloseHandle(cancelEvent);
    if (confirmEvent) CloseHandle(confirmEvent);
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
    update(SESSION_CLOSED, result ? L"Сопряжение забыто на ПК. Можно отправить новый запрос." :
                                 L"Не удалось удалить сопряжение из профиля пользователя.");
    return result;
}
