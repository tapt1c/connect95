#include "pairing.h"
#include "share_info.h"
#include "device_plugins.h"
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <psa/crypto.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test entropy provider for the native Linux test process. */
int mbedtls_hardware_poll(void *context, unsigned char *data, size_t size, size_t *written)
{
    FILE *file = fopen("/dev/urandom", "rb");
    (void)context;
    if (!file) return -1;
    *written = fread(data, 1, size, file);
    fclose(file);
    return *written == size ? 0 : -1;
}

typedef struct {
    unsigned char data[65536];
    size_t used;
} Queue;
typedef struct { Queue *incoming, *outgoing; } Channel;

static int transmit(void *context, const unsigned char *data, size_t size)
{
    Channel *channel = context;
    if (size > sizeof(channel->outgoing->data) - channel->outgoing->used) return MBEDTLS_ERR_SSL_WANT_WRITE;
    memcpy(channel->outgoing->data + channel->outgoing->used, data, size);
    channel->outgoing->used += size;
    return (int)size;
}
static int receive(void *context, unsigned char *data, size_t size)
{
    Channel *channel = context;
    size_t available = channel->incoming->used;
    if (!available) return MBEDTLS_ERR_SSL_WANT_READ;
    if (size > available) size = available;
    memcpy(data, channel->incoming->data, size);
    memmove(channel->incoming->data, channel->incoming->data + size, available - size);
    channel->incoming->used -= size;
    return (int)size;
}

static int message(const char *text) { return pairing_message(text, strlen(text)); }

static int offer(const char *json, ShareInfo *info)
{
    cJSON *packet = packet_parse(json, strlen(json));
    int result = packet ? share_info(packet, info) : -1;
    cJSON_Delete(packet);
    return result;
}

static void test_sharing(void)
{
    ShareInfo info;
    cJSON *generated;
    char json[1600];
    const char *invalidNames[] = {"../photo.jpg", "..", "C:photo.jpg", "a/b", "a\\b", "NUL", "con.txt", "COM1.bin", "LPT9", "photo.", "photo ", "", "a*", "a?"};
    size_t i;
    generated = share_packet("photo.jpg", 4294967297ULL, 1739);
    assert(generated && share_info(generated, &info) == 1);
    assert(info.size == 4294967297ULL && info.port == 1739 && !strcmp(info.filename, "photo.jpg"));
    cJSON_Delete(generated);
    generated = share_packet("empty.txt", 0, 0);
    assert(generated && share_info(generated, &info) == 1 && !info.port && !info.size);
    cJSON_Delete(generated);
    generated = packet_create("kdeconnect.findmyphone.request");
    assert(generated && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(generated, "body")));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(generated, "type")->valuestring, "kdeconnect.findmyphone.request"));
    cJSON_Delete(generated);
    assert(share_filename_valid("photo.jpg"));
    assert(share_filename_valid(".hidden"));
    assert(share_filename_valid("photo \xe2\x98\x83.jpg"));
    for (i = 0; i < sizeof(invalidNames) / sizeof(invalidNames[0]); ++i) assert(!share_filename_valid(invalidNames[i]));
    assert(offer("{\"type\":\"kdeconnect.share.request\",\"body\":{\"filename\":\"empty.txt\"},\"payloadSize\":0}", &info) == 1);
    assert(info.size == 0 && info.port == 0 && !info.unknownSize);
    assert(offer("{\"type\":\"kdeconnect.share.request\",\"body\":{\"filename\":\"data.bin\"},\"payloadSize\":4294967297,\"payloadTransferInfo\":{\"port\":1739}}", &info) == 1);
    assert(info.size == 4294967297ULL && info.port == 1739);
    assert(offer("{\"type\":\"kdeconnect.share.request\",\"body\":{\"filename\":\"data.bin\"},\"payloadSize\":-1,\"payloadTransferInfo\":{\"port\":1739}}", &info) == 1 && info.unknownSize);
    assert(offer("{\"type\":\"kdeconnect.share.request\",\"body\":{\"filename\":\"data.bin\"},\"payloadSize\":1}", &info) == -1);
    assert(offer("{\"type\":\"kdeconnect.share.request\",\"body\":{\"filename\":\"data.bin\"},\"payloadSize\":1,\"payloadTransferInfo\":{\"port\":1739,\"port\":1740}}", &info) == -1);
    for (i = 0; i < 5; ++i) {
        const char *sizes[] = {"-2", "0.5", "9007199254740992", "\"123\"", "null"};
        snprintf(json, sizeof(json), "{\"type\":\"kdeconnect.share.request\",\"body\":{\"filename\":\"a\"},\"payloadSize\":%s,\"payloadTransferInfo\":{\"port\":1739}}", sizes[i]);
        assert(offer(json, &info) == -1);
    }
    assert(offer("{\"type\":\"kdeconnect.findmyphone.request\",\"body\":{}}", &info) == 0);
}

static void test_ping(void)
{
    cJSON *packet = plugin_ping_packet();
    cJSON *body;
    cJSON *parsed;
    char *encoded;
    char message[64];
    assert(packet && plugin_ping(packet, message, sizeof(message)));
    assert(!strcmp(message, "Ping!"));
    encoded = cJSON_PrintUnformatted(packet);
    parsed = packet_parse(encoded, strlen(encoded));
    assert(parsed && plugin_ping(parsed, message, sizeof(message)));
    cJSON_Delete(parsed);
    cJSON_free(encoded);
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    assert(cJSON_AddStringToObject(body, "message", "Hello \xe2\x98\x83"));
    assert(plugin_ping(packet, message, sizeof(message)) && !strcmp(message, "Hello \xe2\x98\x83"));
    assert(!plugin_ping(packet, message, 2));
    assert(cJSON_ReplaceItemInObjectCaseSensitive(body, "message", cJSON_CreateString("")));
    assert(plugin_ping(packet, message, sizeof(message)) && !strcmp(message, "Ping!"));
    assert(cJSON_ReplaceItemInObjectCaseSensitive(body, "message", cJSON_CreateNumber(1)));
    assert(!plugin_ping(packet, message, sizeof(message)));
    cJSON_Delete(packet);
    packet = packet_create("kdeconnect.pair");
    assert(!plugin_ping(packet, message, sizeof(message)));
    cJSON_Delete(packet);
}

static void test_device_plugins(void)
{
    cJSON *packet, *parsed;
    ClipboardMessage message;
    int charge, charging;
    char text[CLIPBOARD_MAX_BYTES + 2];
    char *encoded;
    const char *battery = "{\"type\":\"kdeconnect.battery\",\"body\":{\"currentCharge\":78,\"isCharging\":true}}";
    packet = packet_parse(battery, strlen(battery));
    assert(packet && plugin_battery(packet, &charge, &charging));
    assert(charge == 78 && charging);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(packet, "body"), "currentCharge"), -1);
    assert(plugin_battery(packet, &charge, &charging) && charge == -1);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(packet, "body"), "currentCharge"), 100.5);
    assert(!plugin_battery(packet, &charge, &charging));
    cJSON_Delete(packet);
    packet = plugin_clipboard_packet("Hello \xe2\x98\x83\n");
    assert(packet && plugin_clipboard(packet, &message));
    assert(!strcmp(message.content, "Hello \xe2\x98\x83\n") && !message.initial);
    cJSON_Delete(packet);
    packet = plugin_clipboard_packet("");
    assert(packet && plugin_clipboard(packet, &message) && !message.content[0]);
    cJSON_Delete(packet);
    packet = plugin_clipboard_packet("literal \\u0000 text");
    encoded = cJSON_PrintUnformatted(packet);
    parsed = packet_parse(encoded, strlen(encoded));
    assert(parsed && plugin_clipboard(parsed, &message) && !strcmp(message.content, "literal \\u0000 text"));
    cJSON_Delete(parsed);
    cJSON_free(encoded);
    cJSON_Delete(packet);
    memset(text, 1, CLIPBOARD_MAX_BYTES);
    text[CLIPBOARD_MAX_BYTES] = 0;
    packet = plugin_clipboard_packet(text);
    assert(packet);
    encoded = cJSON_PrintUnformatted(packet);
    assert(strlen(encoded) < PACKET_MAX_SIZE);
    parsed = packet_parse(encoded, strlen(encoded));
    assert(parsed && plugin_clipboard(parsed, &message) && !strcmp(message.content, text));
    cJSON_Delete(parsed);
    cJSON_free(encoded);
    cJSON_Delete(packet);
    text[CLIPBOARD_MAX_BYTES] = 'a';
    text[CLIPBOARD_MAX_BYTES + 1] = 0;
    assert(!plugin_clipboard_packet(text));
    {
        const char *initial = "{\"type\":\"kdeconnect.clipboard.connect\",\"body\":{\"content\":\"hi\",\"timestamp\":1700000000123}}";
        packet = packet_parse(initial, strlen(initial));
        assert(packet && plugin_clipboard(packet, &message) && message.initial && message.timestamp == 1700000000123ULL);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(packet, "body"), "timestamp"), 0);
        assert(!plugin_clipboard(packet, &message));
        cJSON_Delete(packet);
    }
}

int main(int argc, char **argv)
{
    mbedtls_x509_crt certificates[2], restored;
    mbedtls_pk_context keys[2], restoredKey;
    mbedtls_ssl_context ssl[2];
    mbedtls_ssl_config config[2];
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    Queue queues[2] = {{{0}, 0}, {{0}, 0}};
    Channel channels[2] = {{&queues[0], &queues[1]}, {&queues[1], &queues[0]}};
    int i, iteration, done[2] = {0, 0};
    char code[9], reversed[9];
    unsigned char pem[8192], buffer[256];
    const char *pair = "{\"type\":\"kdeconnect.pair\",\"body\":{\"pair\":true}}\n";
    assert(argc == 6);
    test_sharing();
    test_device_plugins();
    test_ping();
    assert(psa_crypto_init() == PSA_SUCCESS);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    assert(!mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, NULL, 0));
    for (i = 0; i < 2; ++i) {
        mbedtls_x509_crt_init(&certificates[i]);
        mbedtls_pk_init(&keys[i]);
        mbedtls_ssl_init(&ssl[i]);
        mbedtls_ssl_config_init(&config[i]);
        assert(!mbedtls_x509_crt_parse_file(&certificates[i], argv[1 + i * 2]));
        assert(!mbedtls_pk_parse_keyfile(&keys[i], argv[2 + i * 2], NULL, mbedtls_ctr_drbg_random, &random));
    }
    assert(pairing_code(&certificates[0].pk, &certificates[1].pk, code));
    assert(pairing_code(&certificates[1].pk, &certificates[0].pk, reversed));
    assert(!strcmp(code, reversed));
    assert(!strcmp(code, argv[5]));
    assert(pairing_verify_peer(&certificates[0], "1234567890abcdef1234567890abcdef", MBEDTLS_X509_BADCERT_NOT_TRUSTED));
    assert(!pairing_verify_peer(&certificates[0], "ffffffffffffffffffffffffffffffff", 0));
    assert(!pairing_verify_peer(&certificates[0], "1234567890abcdef1234567890abcdef", MBEDTLS_X509_BADCERT_EXPIRED));
    assert(!pairing_verify_peer(NULL, "", 0));
    /* The protected identity payload is key PEM + certificate PEM + NUL. */
    mbedtls_pk_init(&restoredKey);
    mbedtls_x509_crt_init(&restored);
    assert(!mbedtls_pk_write_key_pem(&keys[0], pem, sizeof(pem)));
    {
        size_t size = strlen((char *)pem);
        FILE *file = fopen(argv[1], "rb");
        size_t read;
        assert(file);
        read = fread(pem + size, 1, sizeof(pem) - size - 1, file);
        fclose(file);
        pem[size + read] = 0;
        assert(!mbedtls_pk_parse_key(&restoredKey, pem, size + read + 1, NULL, 0, mbedtls_ctr_drbg_random, &random));
        assert(!mbedtls_x509_crt_parse(&restored, pem, size + read + 1));
        assert(!mbedtls_pk_check_pair(&restored.pk, &restoredKey, mbedtls_ctr_drbg_random, &random));
    }
    for (i = 0; i < 2; ++i) {
        assert(!mbedtls_ssl_config_defaults(&config[i], i ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT));
        mbedtls_ssl_conf_min_tls_version(&config[i], MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&config[i], MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_authmode(&config[i], MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_rng(&config[i], mbedtls_ctr_drbg_random, &random);
        assert(!mbedtls_ssl_conf_own_cert(&config[i], &certificates[i], &keys[i]));
        assert(!mbedtls_ssl_setup(&ssl[i], &config[i]));
        mbedtls_ssl_set_bio(&ssl[i], &channels[i], transmit, receive, NULL);
    }
    for (iteration = 0; iteration < 1000 && !(done[0] && done[1]); ++iteration) {
        for (i = 0; i < 2; ++i) {
            int result;
            if (done[i]) continue;
            result = mbedtls_ssl_handshake(&ssl[i]);
            assert(!result || result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE);
            done[i] = result == 0;
        }
    }
    assert(done[0] && done[1]);
    assert(!strcmp(mbedtls_ssl_get_version(&ssl[0]), "TLSv1.2"));
    assert(pairing_verify_peer(mbedtls_ssl_get_peer_cert(&ssl[1]), "1234567890abcdef1234567890abcdef", mbedtls_ssl_get_verify_result(&ssl[1])));
    assert(mbedtls_ssl_write(&ssl[0], (const unsigned char *)pair, strlen(pair)) == (int)strlen(pair));
    i = mbedtls_ssl_read(&ssl[1], buffer, sizeof(buffer));
    assert(i == (int)strlen(pair) && pairing_message((char *)buffer, (size_t)i) == 1);
    assert(message(pair) == 1);
    assert(message("{\"type\":\"kdeconnect.pair\",\"body\":{\"pair\":false}}") == -1);
    assert(message("{\"type\":\"kdeconnect.battery\",\"body\":{}}") == 0);
    assert(message("{\"type\":\"kdeconnect.pair\",\"body\":{\"pair\":true,\"pair\":false}}") == -2);
    assert(message("{\"type\":\"kdeconnect.pair\",\"body\":{\"pair\":\"true\"}}") == -2);
    assert(message("{\"type\":\"kdeconnect.pair\",\"body\":{\"pair\":true}}garbage") == -2);
    assert(message("{\"type\":\"kdeconnect.pair\\u0000\",\"body\":{\"pair\":true}}") == -2);
    /* Binary payloads across record boundaries in both TLS directions. */
    for (i = 0; i < 2; ++i) {
        unsigned char binary[16384], received[16384];
        int chunk;
        for (chunk = 0; chunk < 20; ++chunk) {
            size_t offset = 0, j;
            for (j = 0; j < sizeof(binary); ++j) binary[j] = (unsigned char)(j + chunk);
            assert(mbedtls_ssl_write(&ssl[i], binary, sizeof(binary)) == (int)sizeof(binary));
            while (offset < sizeof(binary)) {
                int read = mbedtls_ssl_read(&ssl[1 - i], received + offset, sizeof(binary) - offset > 997 ? 997 : sizeof(binary) - offset);
                assert(read > 0 && offset + (size_t)read <= sizeof(binary));
                offset += (size_t)read;
            }
            assert(!memcmp(binary, received, sizeof(binary)));
        }
    }
    assert(!mbedtls_ssl_close_notify(&ssl[0]));
    assert(mbedtls_ssl_read(&ssl[1], buffer, sizeof(buffer)) == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY);
    for (i = 0; i < 2; ++i) {
        mbedtls_ssl_free(&ssl[i]);
        mbedtls_ssl_config_free(&config[i]);
        mbedtls_pk_free(&keys[i]);
        mbedtls_x509_crt_free(&certificates[i]);
    }
    mbedtls_pk_free(&restoredKey);
    mbedtls_x509_crt_free(&restored);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
    puts("pairing tests passed: TLS 1.2, certificates, verification code, PEM reload, pair/share/battery/clipboard/ping packets, bidirectional binary payloads");
    return 0;
}
