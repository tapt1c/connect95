#include "saved_devices.h"
#include "packet.h"
#include <shlobj.h>
#include <string.h>
#include <stdio.h>

static int valid_id(const char *id)
{
    size_t i, size = strlen(id);
    if (size < 32 || size > 38) return 0;
    for (i = 0; i < size; ++i) {
        char ch = id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

static int directory_path(WCHAR *directory)
{
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, directory) != S_OK ||
        lstrlenW(directory) > MAX_PATH - 80) return 0;
    lstrcatW(directory, L"\\Connect95");
    return 1;
}

static int device_path(const char *id, const WCHAR *extension, WCHAR *path)
{
    WCHAR directory[MAX_PATH];
    if (!valid_id(id) || !directory_path(directory)) return 0;
    wsprintfW(path, L"%s\\peer-%hs.%s", directory, id, extension);
    return 1;
}

int saved_device_exists(const char *id)
{
    WCHAR path[MAX_PATH];
    DWORD attributes, error;
    if (!device_path(id, L"der", path)) return -1;
    attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) return !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
}

static void load_details(DiscoveryPeer *peer)
{
    WCHAR path[MAX_PATH], name[257];
    char data[IDENTITY_MAX_PACKET + 1];
    DiscoveryIdentity identity;
    cJSON *packet;
    const cJSON *body, *address;
    HANDLE file;
    DWORD length, read;
    const char *cursor;
    unsigned int a, b, c, d;
    char extra;
    if (!device_path(peer->identity.id, L"json", path)) return;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    length = GetFileSize(file, NULL);
    if (!length || length > IDENTITY_MAX_PACKET || !ReadFile(file, data, length, &read, NULL) || read != length) {
        CloseHandle(file);
        return;
    }
    CloseHandle(file);
    data[length] = 0;
    if (!identity_parse(data, length, &identity) || strcmp(identity.id, peer->identity.id) ||
        !identity.name[0] || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            identity.name, -1, name, ARRAYSIZE(name))) return;
    packet = packet_parse(data, length);
    if (!packet) return;
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    address = cJSON_GetObjectItemCaseSensitive(body, "address");
    if (!cJSON_IsString(address) || strlen(address->valuestring) >= sizeof(peer->address)) goto finish;
    for (cursor = address->valuestring; *cursor; ++cursor)
        if ((*cursor < '0' || *cursor > '9') && *cursor != '.') goto finish;
    if (sscanf(address->valuestring, "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &extra) != 4 ||
        a >= 224 || b > 255 || c > 255 || d > 255 || !(a | b | c | d)) goto finish;
    peer->identity = identity;
    lstrcpyW(peer->name, name);
    snprintf(peer->address, sizeof(peer->address), "%u.%u.%u.%u", a, b, c, d);
finish:
    cJSON_Delete(packet);
}

int saved_devices_load(DiscoveryPeer *peers, int capacity)
{
    WCHAR directory[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    int count = 0;
    if (capacity <= 0 || !directory_path(directory)) return 0;
    wsprintfW(pattern, L"%s\\peer-*.der", directory);
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        size_t length = (size_t)lstrlenW(entry.cFileName), i;
        DiscoveryPeer peer;
        WCHAR shortId[9];
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || length < 41 || length > 47 ||
            wcsncmp(entry.cFileName, L"peer-", 5) || lstrcmpiW(entry.cFileName + length - 4, L".der")) continue;
        ZeroMemory(&peer, sizeof(peer));
        for (i = 5; i < length - 4; ++i) {
            if (entry.cFileName[i] > 127) break;
            peer.identity.id[i - 5] = (char)entry.cFileName[i];
        }
        if (i != length - 4 || !valid_id(peer.identity.id)) continue;
        MultiByteToWideChar(CP_ACP, 0, peer.identity.id, 8, shortId, 8);
        shortId[8] = 0;
        wsprintfW(peer.name, L"Phone %s", shortId);
        load_details(&peer);
        peers[count++] = peer;
    } while (count < capacity && FindNextFileW(search, &entry));
    FindClose(search);
    return count;
}

int saved_device_remember(const DiscoveryPeer *peer)
{
    WCHAR path[MAX_PATH], temporary[MAX_PATH];
    cJSON *packet = packet_create("kdeconnect.identity");
    cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    char name[257], *text = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written, length;
    int success = 0;
    if (!packet || !device_path(peer->identity.id, L"json", path) || !peer->address[0] ||
        !WideCharToMultiByte(CP_UTF8, 0, peer->name, -1, name, sizeof(name), NULL, NULL) ||
        !cJSON_AddStringToObject(body, "deviceId", peer->identity.id) ||
        !cJSON_AddStringToObject(body, "deviceName", name) ||
        !cJSON_AddStringToObject(body, "address", peer->address) ||
        !cJSON_AddNumberToObject(body, "tcpPort", peer->identity.port) ||
        !cJSON_AddNumberToObject(body, "protocolVersion", peer->identity.version)) goto finish;
    text = cJSON_PrintUnformatted(packet);
    if (!text) goto finish;
    wsprintfW(temporary, L"%s.tmp", path);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto finish;
    length = (DWORD)strlen(text);
    success = WriteFile(file, text, length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (success) success = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!success) DeleteFileW(temporary);
finish:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    cJSON_free(text);
    cJSON_Delete(packet);
    return success;
}

int saved_device_forget_details(const char *id)
{
    WCHAR path[MAX_PATH];
    if (!device_path(id, L"json", path)) return 0;
    return DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
}
