#include "transfer.h"
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

static SOCKET listen_payload(Connection *control, unsigned short *port)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    SOCKET listener;
    BOOL exclusive = TRUE;
    u_long nonblocking = 1;
    if (getsockname(control->socket, (struct sockaddr *)&address, &length)) return INVALID_SOCKET;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return listener;
    if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char *)&exclusive, sizeof(exclusive)) || ioctlsocket(listener, FIONBIO, &nonblocking)) goto failed;
    for (*port = 1739; *port <= 1764; ++*port) {
        address.sin_port = htons(*port);
        if (!bind(listener, (struct sockaddr *)&address, sizeof(address))) {
            if (!listen(listener, 4)) return listener;
            break;
        }
    }
failed:
    closesocket(listener);
    return INVALID_SOCKET;
}

static int accept_payload(Connection *payload, SOCKET listener, const char *host)
{
    DWORD start = GetTickCount();
    while (!transport_cancelled(payload) && (DWORD)(GetTickCount() - start) < 30000) {
        struct sockaddr_in address;
        int length = sizeof(address);
        SOCKET incoming = accept(listener, (struct sockaddr *)&address, &length);
        if (incoming != INVALID_SOCKET) {
            u_long nonblocking = 1;
            if (address.sin_addr.s_addr == inet_addr(host) && !ioctlsocket(incoming, FIONBIO, &nonblocking)) {
                payload->socket = incoming;
                return 1;
            }
            closesocket(incoming);
        } else if (WSAGetLastError() != WSAEWOULDBLOCK) return 0;
        WaitForSingleObject(payload->cancel, 20);
    }
    return 0;
}

const WCHAR *transfer_send(TransferContext *context, const WCHAR *path)
{
    Connection payload;
    HANDLE file = INVALID_HANDLE_VALUE;
    SOCKET listener = INVALID_SOCKET;
    LARGE_INTEGER size;
    ULONGLONG done = 0;
    unsigned short port = 0;
    unsigned char buffer[16384];
    char name[769];
    const WCHAR *filename = wcsrchr(path, L'\\');
    const WCHAR *error = L"Cannot open the selected file.";
    cJSON *packet = NULL;
    transport_init(&payload, context->control->cancel);
    payload.transferCancel = context->cancel;
    filename = filename ? filename + 1 : path;
    {
        WCHAR roundtrip[MAX_PATH];
        if (!WideCharToMultiByte(CP_UTF8, 0, filename, -1, name, sizeof(name), NULL, NULL) ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, roundtrip, ARRAYSIZE(roundtrip)) ||
            wcscmp(filename, roundtrip)) {
            error = L"This file name cannot be encoded as UTF-8.";
            goto finish;
        }
    }
    if (!share_filename_valid(name)) {
        error = L"This file name is not supported.";
        goto finish;
    }
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) || size.QuadPart < 0) goto finish;
    if ((ULONGLONG)size.QuadPart > 9007199254740991ULL) {
        error = L"The file is too large for the protocol.";
        goto finish;
    }
    context->progress(filename, 0, (ULONGLONG)size.QuadPart);
    if (size.QuadPart) {
        error = L"Cannot listen for file transfer on TCP 1739-1764.";
        listener = listen_payload(context->control, &port);
        if (listener == INVALID_SOCKET) goto finish;
    }
    error = L"Cannot announce the file to the phone.";
    packet = share_packet(name, (ULONGLONG)size.QuadPart, port);
    if (!packet || transport_cancelled(&payload) || !transport_json(context->control, packet, TRUE)) goto finish;
    if (size.QuadPart) {
        error = L"The phone did not connect. Allow inbound TCP 1739-1764 for ConnectXP.";
        if (!accept_payload(&payload, listener, context->peer->address)) goto finish;
        closesocket(listener);
        listener = INVALID_SOCKET;
        error = L"The file connection certificate did not match the paired phone, or TLS failed.";
        if (transport_tls(&payload, context->credentials, context->peer->identity.id, MBEDTLS_SSL_IS_SERVER, TRUE)) goto finish;
    }
    error = L"File sending failed or timed out.";
    while (done < (ULONGLONG)size.QuadPart) {
        DWORD read;
        DWORD want = (ULONGLONG)size.QuadPart - done > sizeof(buffer) ? sizeof(buffer) :
                      (DWORD)((ULONGLONG)size.QuadPart - done);
        if (transport_cancelled(&payload) || !ReadFile(file, buffer, want, &read, NULL) || !read ||
            !transport_write(&payload, (const char *)buffer, read, TRUE)) goto finish;
        done += read;
        context->progress(filename, done, (ULONGLONG)size.QuadPart);
    }
    error = NULL;
finish:
    if (error && transport_cancelled(&payload)) error = L"Transfer cancelled.";
    cJSON_Delete(packet);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (listener != INVALID_SOCKET) closesocket(listener);
    transport_close(&payload);
    return error;
}

int transfer_directory(WCHAR *directory)
{
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, 0, directory) != S_OK ||
        lstrlenW(directory) + 10 >= MAX_PATH) return 0;
    lstrcatW(directory, L"\\ConnectXP");
    return CreateDirectoryW(directory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static HANDLE temporary_file(TransferContext *context, const WCHAR *directory, WCHAR *path)
{
    unsigned char random[8];
    WCHAR hex[17], candidate[MAX_PATH];
    int attempt, i;
    if (lstrlenW(directory) + 34 >= MAX_PATH) return INVALID_HANDLE_VALUE;
    for (attempt = 0; attempt < 10; ++attempt) {
        HANDLE file;
        if (mbedtls_ctr_drbg_random(&context->credentials->random, random, sizeof(random))) return INVALID_HANDLE_VALUE;
        for (i = 0; i < 8; ++i) wsprintfW(hex + i * 2, L"%02x", random[i]);
        wsprintfW(candidate, L"%s\\.connectxp-%s.part", directory, hex);
        file = CreateFileW(candidate, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            lstrcpyW(path, candidate);
            return file;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    path[0] = 0;
    return INVALID_HANDLE_VALUE;
}

static int publish_file(const WCHAR *temporary, const WCHAR *directory, const WCHAR *filename)
{
    WCHAR destination[MAX_PATH];
    int counter;
    for (counter = 0; counter < 1000; ++counter) {
        if (lstrlenW(directory) + lstrlenW(filename) + 16 >= MAX_PATH) return 0;
        if (!counter) wsprintfW(destination, L"%s\\%s", directory, filename);
        else wsprintfW(destination, L"%s\\(%d) %s", directory, counter, filename);
        /* No replacement flag, including when another process creates the destination. */
        if (MoveFileExW(temporary, destination, MOVEFILE_WRITE_THROUGH)) return 1;
        if (GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS) return 0;
    }
    return 0;
}

const WCHAR *transfer_receive(TransferContext *context, const ShareInfo *offer)
{
    Connection payload;
    WCHAR directory[MAX_PATH], temporary[MAX_PATH] = L"", filename[MAX_PATH];
    unsigned char buffer[16384];
    HANDLE file = INVALID_HANDLE_VALUE;
    ULONGLONG done = 0;
    const WCHAR *error = L"Cannot create the received file in My Documents\\ConnectXP.";
    transport_init(&payload, context->control->cancel);
    payload.transferCancel = context->cancel;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, offer->filename, -1, filename, ARRAYSIZE(filename)) ||
        !transfer_directory(directory) || lstrlenW(directory) + lstrlenW(filename) + 16 >= MAX_PATH) goto finish;
    file = temporary_file(context, directory, temporary);
    if (file == INVALID_HANDLE_VALUE) goto finish;
    context->progress(filename, 0, offer->unknownSize ? UINT64_MAX : offer->size);
    if (offer->port) {
        error = L"Cannot connect to the phone's file transfer port.";
        if (!transport_connect(&payload, context->peer->address, offer->port)) goto finish;
        error = L"The file connection certificate did not match the paired phone, or TLS failed.";
        if (transport_tls(&payload, context->credentials, context->peer->identity.id, MBEDTLS_SSL_IS_CLIENT, TRUE)) goto finish;
    }
    error = L"File receiving failed, timed out, or the phone sent fewer bytes than announced.";
    while (offer->unknownSize || done < offer->size) {
        DWORD written;
        size_t want = offer->unknownSize || offer->size - done > sizeof(buffer) ? sizeof(buffer) : (size_t)(offer->size - done);
        int read = transport_read(&payload, buffer, want);
        if (offer->unknownSize && read == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        if (read <= 0 || done > UINT64_MAX - (unsigned int)read || !WriteFile(file, buffer, (DWORD)read, &written, NULL) || written != (DWORD)read) goto finish;
        done += (unsigned int)read;
        context->progress(filename, done, offer->unknownSize ? UINT64_MAX : offer->size);
    }
    if (offer->port && !offer->unknownSize) {
        int extra = transport_read(&payload, buffer, 1);
        if (extra != 0 && extra != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY && extra != MBEDTLS_ERR_SSL_CONN_EOF) {
            error = L"The received file size did not match its announcement, or the transfer did not finish.";
            goto finish;
        }
    }
    error = L"Cannot finish saving the received file.";
    if (transport_cancelled(&payload) || !FlushFileBuffers(file)) goto finish;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (!publish_file(temporary, directory, filename)) goto finish;
    temporary[0] = 0;
    error = NULL;
finish:
    if (error && transport_cancelled(&payload)) error = L"Transfer cancelled.";
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (temporary[0]) DeleteFileW(temporary);
    transport_close(&payload);
    return error;
}
