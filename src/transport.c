#include "transport.h"
#include "pairing.h"
#include <string.h>

void transport_init(Connection *connection, HANDLE cancel)
{
    ZeroMemory(connection, sizeof(*connection));
    connection->socket = INVALID_SOCKET;
    connection->cancel = cancel;
    mbedtls_ssl_init(&connection->ssl);
    mbedtls_ssl_config_init(&connection->config);
}

int transport_cancelled(Connection *connection)
{
    return WaitForSingleObject(connection->cancel, 0) == WAIT_OBJECT_0 ||
        (connection->transferCancel && WaitForSingleObject(connection->transferCancel, 0) == WAIT_OBJECT_0);
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

int transport_connect(Connection *connection, const char *host, unsigned short port)
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
    address.sin_addr.s_addr = inet_addr(host);
    address.sin_port = htons(port);
    result = connect(connection->socket, (struct sockaddr *)&address, sizeof(address));
    if (!result) return 1;
    if (WSAGetLastError() != WSAEWOULDBLOCK) return 0;
    while (!transport_cancelled(connection) && (DWORD)(GetTickCount() - start) < 10000) {
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

int transport_write(Connection *connection, const char *data, size_t length, BOOL encrypted)
{
    DWORD start = GetTickCount();
    while (length && !transport_cancelled(connection) && (DWORD)(GetTickCount() - start) < 5000) {
        int result = encrypted ? mbedtls_ssl_write(&connection->ssl, (const unsigned char *)data, length) :
                                 net_send(connection, (const unsigned char *)data, length);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            WaitForSingleObject(connection->cancel, 10);
            continue;
        }
        if (result <= 0) return 0;
        data += result;
        length -= (size_t)result;
    }
    return length == 0;
}

int transport_tls(Connection *connection, Credentials *credentials, const char *peerId, int role, BOOL pinned)
{
    const mbedtls_x509_crt *peer;
    DWORD start = GetTickCount();
    int result;
    result = mbedtls_ssl_config_defaults(&connection->config, role,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result) return result;
    mbedtls_ssl_conf_min_tls_version(&connection->config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&connection->config, MBEDTLS_SSL_VERSION_TLS1_2);
    /* Request certificates during enrollment, then check identity and the pin. */
    mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&connection->config, mbedtls_ctr_drbg_random, &credentials->random);
    result = mbedtls_ssl_conf_own_cert(&connection->config, &credentials->certificate, &credentials->key);
    if (!result) result = mbedtls_ssl_setup(&connection->ssl, &connection->config);
    if (result) return result;
    mbedtls_ssl_set_bio(&connection->ssl, connection, net_send, net_receive, NULL);
    do {
        result = mbedtls_ssl_handshake(&connection->ssl);
        if (!result) break;
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return result;
        if (transport_cancelled(connection) || (DWORD)(GetTickCount() - start) > 15000)
            return MBEDTLS_ERR_SSL_TIMEOUT;
        WaitForSingleObject(connection->cancel, 10);
    } while (1);
    peer = mbedtls_ssl_get_peer_cert(&connection->ssl);
    if (!pairing_verify_peer(peer, peerId, mbedtls_ssl_get_verify_result(&connection->ssl)) ||
        (pinned && credentials_pin(credentials, peerId, peer, FALSE) != 1))
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    connection->secure = TRUE;
    return 0;
}

int transport_read(Connection *connection, unsigned char *data, size_t size)
{
    DWORD start = GetTickCount();
    while (!transport_cancelled(connection) && (DWORD)(GetTickCount() - start) < 30000) {
        int result = mbedtls_ssl_read(&connection->ssl, data, size);
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return result;
        WaitForSingleObject(connection->cancel, 10);
    }
    return MBEDTLS_ERR_SSL_TIMEOUT;
}

void transport_close(Connection *connection)
{
    if (connection->socket != INVALID_SOCKET) {
        if (connection->secure && !transport_cancelled(connection)) {
            DWORD start = GetTickCount();
            int result;
            do {
                result = mbedtls_ssl_close_notify(&connection->ssl);
                if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) break;
                WaitForSingleObject(connection->cancel, 10);
            } while (!transport_cancelled(connection) && (DWORD)(GetTickCount() - start) < 1000);
        }
        closesocket(connection->socket);
        connection->socket = INVALID_SOCKET;
    }
    mbedtls_ssl_free(&connection->ssl);
    mbedtls_ssl_config_free(&connection->config);
}

int transport_json(Connection *connection, cJSON *packet, BOOL encrypted)
{
    char *text = cJSON_PrintUnformatted(packet);
    int result;
    if (!text) return 0;
    result = transport_write(connection, text, strlen(text), encrypted) &&
             transport_write(connection, "\n", 1, encrypted);
    cJSON_free(text);
    return result;
}

