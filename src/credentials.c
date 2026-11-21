#include "credentials.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/rsa.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_util.h>

/* The default Mbed TLS Windows source uses BCrypt, absent on XP. */
int mbedtls_hardware_poll(void *context, unsigned char *output, size_t size, size_t *written)
{
    HCRYPTPROV provider;
    BOOL result;
    (void)context;
    *written = 0;
    if (size > MAXDWORD || !CryptAcquireContextW(&provider, NULL, NULL,
            PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) return -1;
    result = CryptGenRandom(provider, (DWORD)size, output);
    CryptReleaseContext(provider, 0);
    if (!result) return -1;
    *written = size;
    return 0;
}

static int read_file(const WCHAR *path, unsigned char *data, DWORD capacity, DWORD *length)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, read;
    BOOL success;
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    size = GetFileSize(file, NULL);
    success = size && size < capacity && ReadFile(file, data, size, &read, NULL) && read == size;
    CloseHandle(file);
    if (!success) return -1;
    *length = size;
    data[size] = 0;
    return 1;
}

static int write_file(const WCHAR *path, const unsigned char *data, DWORD length)
{
    WCHAR temporary[MAX_PATH];
    HANDLE file;
    DWORD written;
    BOOL success;
    if (lstrlenW(path) + 5 >= MAX_PATH) return 0;
    lstrcpyW(temporary, path);
    lstrcatW(temporary, L".tmp");
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    success = WriteFile(file, data, length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (success) success = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!success) DeleteFileW(temporary);
    return success;
}

static int cert_id(const mbedtls_x509_crt *certificate, char *id)
{
    const mbedtls_x509_name *name = &certificate->subject;
    while (name) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
            if (name->val.len != 32) return 0;
            memcpy(id, name->val.p, 32);
            id[32] = 0;
            return 1;
        }
        name = name->next;
    }
    return 0;
}

void credentials_init(Credentials *c)
{
    ZeroMemory(c, sizeof(*c));
    c->lock = INVALID_HANDLE_VALUE;
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->random);
    mbedtls_pk_init(&c->key);
    mbedtls_x509_crt_init(&c->certificate);
}

int credentials_load(Credentials *c)
{
    WCHAR path[MAX_PATH];
    unsigned char fileData[16384], pem[8192], randomId[16];
    DATA_BLOB input, output;
    DWORD length = 0;
    int result, i, keyLength, success = 0;
    char subject[80], expiry[16];
    SYSTEMTIME now;
    mbedtls_x509write_cert writer;
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, c->directory) != S_OK ||
        lstrlenW(c->directory) > MAX_PATH - 80) return 0;
    lstrcatW(c->directory, L"\\Connect95");
    if (!CreateDirectoryW(c->directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    wsprintfW(path, L"%s\\identity.lock", c->directory);
    c->lock = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (c->lock == INVALID_HANDLE_VALUE) return 0;
    if (mbedtls_ctr_drbg_seed(&c->random, mbedtls_entropy_func, &c->entropy,
                              (const unsigned char *)"ConnectXP", 9)) return 0;
    wsprintfW(path, L"%s\\identity.dat", c->directory);
    result = read_file(path, fileData, sizeof(fileData), &length);
    if (result < 0) return 0;
    if (result == 1) {
        input.pbData = fileData;
        input.cbData = length;
        if (!CryptUnprotectData(&input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output)) return 0;
        if (output.cbData && output.pbData[output.cbData - 1] == 0 &&
            !mbedtls_pk_parse_key(&c->key, output.pbData, output.cbData, NULL, 0,
                                  mbedtls_ctr_drbg_random, &c->random) &&
            !mbedtls_x509_crt_parse(&c->certificate, output.pbData, output.cbData) &&
            !mbedtls_pk_check_pair(&c->certificate.pk, &c->key, mbedtls_ctr_drbg_random, &c->random))
            success = cert_id(&c->certificate, c->id);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        return success;
    }
    if (mbedtls_ctr_drbg_random(&c->random, randomId, sizeof(randomId))) return 0;
    for (i = 0; i < 16; ++i) sprintf(c->id + i * 2, "%02x", randomId[i]);
    if (mbedtls_pk_setup(&c->key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) ||
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(c->key), mbedtls_ctr_drbg_random, &c->random, 2048, 65537)) return 0;
    mbedtls_x509write_crt_init(&writer);
    snprintf(subject, sizeof(subject), "CN=%s,O=ConnectXP,OU=KDE Connect", c->id);
    GetSystemTime(&now);
    snprintf(expiry, sizeof(expiry), "%04u0101000000", (unsigned int)now.wYear + 10);
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &c->key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &c->key);
    randomId[0] &= 0x7f;
    if (mbedtls_x509write_crt_set_serial_raw(&writer, randomId, sizeof(randomId)) ||
        mbedtls_x509write_crt_set_subject_name(&writer, subject) ||
        mbedtls_x509write_crt_set_issuer_name(&writer, subject) ||
        mbedtls_x509write_crt_set_validity(&writer, "20000101000000", expiry) ||
        mbedtls_pk_write_key_pem(&c->key, pem, sizeof(pem))) goto generated;
    keyLength = (int)strlen((char *)pem);
    if (mbedtls_x509write_crt_pem(&writer, pem + keyLength, sizeof(pem) - keyLength,
                                 mbedtls_ctr_drbg_random, &c->random) ||
        mbedtls_x509_crt_parse(&c->certificate, pem + keyLength, strlen((char *)pem + keyLength) + 1)) goto generated;
    input.pbData = pem;
    input.cbData = (DWORD)strlen((char *)pem) + 1;
    if (!CryptProtectData(&input, L"ConnectXP identity", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output)) goto generated;
    success = write_file(path, output.pbData, output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
generated:
    mbedtls_platform_zeroize(pem, sizeof(pem));
    mbedtls_x509write_crt_free(&writer);
    return success;
}

static int peer_path(Credentials *c, const char *id, WCHAR *path)
{
    size_t i, size = strlen(id);
    /* IDs become filenames. Validate independently of discovery callers. */
    if (size < 32 || size > 38) return 0;
    for (i = 0; i < size; ++i) {
        char ch = id[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '-')) return 0;
    }
    wsprintfW(path, L"%s\\peer-%hs.der", c->directory, id);
    return 1;
}

int credentials_forget(Credentials *c, const char *id)
{
    WCHAR path[MAX_PATH];
    if (!peer_path(c, id, path)) return 0;
    return DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

int credentials_pin(Credentials *c, const char *id, const mbedtls_x509_crt *certificate, BOOL save)
{
    WCHAR path[MAX_PATH];
    unsigned char data[16384];
    DWORD length = 0;
    int result;
    if (!certificate || certificate->raw.len >= sizeof(data) || !peer_path(c, id, path)) return -1;
    result = read_file(path, data, sizeof(data), &length);
    if (result < 0) return -1;
    if (result == 1) return length == certificate->raw.len &&
        !memcmp(data, certificate->raw.p, length) ? 1 : -1;
    if (!save) return 0;
    return write_file(path, certificate->raw.p, (DWORD)certificate->raw.len) ? 1 : -1;
}

void credentials_free(Credentials *c)
{
    mbedtls_x509_crt_free(&c->certificate);
    mbedtls_pk_free(&c->key);
    mbedtls_ctr_drbg_free(&c->random);
    mbedtls_entropy_free(&c->entropy);
    if (c->lock != INVALID_HANDLE_VALUE) CloseHandle(c->lock);
}
