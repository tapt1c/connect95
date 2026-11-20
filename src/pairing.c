#include "pairing.h"
#include "packet.h"
#include "identity.h"
#include "vendor/cJSON.h"
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <stdio.h>

int pairing_verify_peer(const mbedtls_x509_crt *certificate, const char *id, unsigned int flags)
{
    const mbedtls_x509_name *name;
    int count = 0;
    /* A self-signed enrollment certificate has no CA. All other errors fail. */
    if (!certificate || (flags & ~MBEDTLS_X509_BADCERT_NOT_TRUSTED)) return 0;
    name = &certificate->subject;
    while (name) {
        if (!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid)) {
            if (strlen(id) != name->val.len || memcmp(id, name->val.p, name->val.len)) return 0;
            ++count;
        }
        name = name->next;
    }
    return count == 1;
}

int pairing_code(mbedtls_pk_context *local, mbedtls_pk_context *remote, char code[9])
{
    unsigned char a[2048], b[2048], hash[32];
    unsigned char *first, *second;
    int aSize, bSize, firstSize, secondSize, comparison, i, result;
    mbedtls_sha256_context sha;
    aSize = mbedtls_pk_write_pubkey_der(local, a, sizeof(a));
    bSize = mbedtls_pk_write_pubkey_der(remote, b, sizeof(b));
    if (aSize <= 0 || bSize <= 0) return 0;
    first = a + sizeof(a) - aSize;
    second = b + sizeof(b) - bSize;
    firstSize = aSize;
    secondSize = bSize;
    comparison = memcmp(first, second, (size_t)(aSize < bSize ? aSize : bSize));
    /* KDE protocol 7 hashes the two SPKI DER public keys, descending order. */
    if (comparison < 0 || (!comparison && aSize < bSize)) {
        first = b + sizeof(b) - bSize;
        second = a + sizeof(a) - aSize;
        firstSize = bSize;
        secondSize = aSize;
    }
    mbedtls_sha256_init(&sha);
    result = !mbedtls_sha256_starts(&sha, 0) &&
             !mbedtls_sha256_update(&sha, first, (size_t)firstSize) &&
             !mbedtls_sha256_update(&sha, second, (size_t)secondSize) &&
             !mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    if (!result) return 0;
    for (i = 0; i < 4; ++i) sprintf(code + i * 2, "%02X", hash[i]);
    return 1;
}

int pairing_message(const char *line, size_t length)
{
    cJSON *packet = packet_parse(line, length);
    const cJSON *type, *body, *pair;
    int result = -2;
    if (!packet) return -2;
    type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    if (strcmp(type->valuestring, "kdeconnect.pair")) result = 0;
    else {
        pair = cJSON_GetObjectItemCaseSensitive(body, "pair");
        if (cJSON_IsBool(pair)) result = cJSON_IsTrue(pair) ? 1 : -1;
    }
    cJSON_Delete(packet);
    return result;
}
