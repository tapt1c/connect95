#include "identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int parse(const char *packet)
{
    DiscoveryIdentity result;
    return identity_parse(packet, strlen(packet), &result);
}

int main(void)
{
    const char *valid = "{\"id\":1,\"type\":\"kdeconnect.identity\",\"body\":{"
        "\"deviceId\":\"1234567890abcdef1234567890abcdef\",\"tcpPort\":1716,"
        "\"protocolVersion\":8,\"deviceName\":\"Phone \\u2603\"}}\n";
    char packet[2048];
    DiscoveryIdentity result, original;
    unsigned int seed = 17;
    size_t i, j, length = strlen(valid);
    assert(identity_parse(valid, length, &result));
    assert(result.port == 1716 && result.version == 8);
    assert(!strcmp(result.name, "Phone \xe2\x98\x83"));
    original = result;
    /* Truncated input never leaks partially parsed fields to the caller. */
    for (i = 0; i < length - 2; ++i) {
        assert(!identity_parse(valid, i, &result));
        assert(!memcmp(&result, &original, sizeof(result)));
    }
    assert(!identity_parse(valid, IDENTITY_MAX_PACKET + 1, &result));
    assert(!parse("[]"));
    assert(!parse("{}{}"));
    assert(!parse("{\"type\":\"kdeconnect.pair\",\"body\":{}}"));
    assert(!parse("{\"type\":\"kdeconnect.identity\",\"body\":{\"deviceId\":\"short\"}}"));
    /* v8 can omit the untrusted human-readable name. */
    assert(parse("{\"type\":\"kdeconnect.identity\",\"body\":{\"deviceId\":\"1234567890abcdef1234567890abcdef\",\"tcpPort\":1764,\"protocolVersion\":8}}"));
    for (i = 0; i < 5; ++i) {
        const char *ports[] = {"1715", "1765", "1716.5", "\"1716\"", "null"};
        snprintf(packet, sizeof(packet), "{\"type\":\"kdeconnect.identity\",\"body\":{\"deviceId\":\"1234567890abcdef1234567890abcdef\",\"tcpPort\":%s,\"protocolVersion\":8}}", ports[i]);
        assert(!parse(packet));
    }
    assert(!parse("{\"type\":\"kdeconnect.identity\",\"body\":{\"deviceId\":\"1234567890abcdef1234567890abcdef\",\"tcpPort\":1716,\"protocolVersion\":8,\"deviceName\":\"A\",\"deviceName\":\"B\"}}"));
    assert(!parse("{\"type\":\"kdeconnect.identity\",\"body\":{\"deviceId\":\"1234567890abcdef1234567890abcdef\",\"tcpPort\":1716,\"protocolVersion\":8,\"deviceName\":\"A\\u0000B\"}}"));
    /* Deterministic malformed datagrams, also run with ASan/UBSan. */
    for (i = 0; i < 10000; ++i) {
        memcpy(packet, valid, length);
        for (j = 0; j < 4; ++j) {
            seed = seed * 1664525U + 1013904223U;
            packet[seed % length] = (char)(seed >> 24);
        }
        (void)identity_parse(packet, length, &result);
    }
    puts("identity parser tests passed");
    return 0;
}
