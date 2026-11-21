#include "media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *type, const char *player, const char *action, int expected)
{
    cJSON *packet = packet_create(type);
    cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    assert(cJSON_AddStringToObject(body, "player", player));
    assert(cJSON_AddStringToObject(body, "action", action));
    assert(media_command(packet) == expected);
    cJSON_Delete(packet);
}

int main(void)
{
    cJSON *packet, *body, *list;
    check("kdeconnect.mpris.request", "Windows media keys", "PlayPause", MEDIA_TOGGLE);
    check("kdeconnect.mpris.request", "Windows media keys", "Play", MEDIA_TOGGLE);
    check("kdeconnect.mpris.request", "Windows media keys", "Pause", MEDIA_TOGGLE);
    check("kdeconnect.mpris.request", "Windows media keys", "Stop", MEDIA_STOP);
    check("kdeconnect.mpris.request", "Windows media keys", "Next", MEDIA_NEXT);
    check("kdeconnect.mpris.request", "Windows media keys", "Previous", MEDIA_PREVIOUS);
    check("kdeconnect.mpris.request", "Other player", "Next", MEDIA_NONE);
    check("kdeconnect.ping", "Windows media keys", "Next", MEDIA_NONE);
    check("kdeconnect.mpris.request", "Windows media keys", "Run", MEDIA_NONE);
    assert(!media_request(NULL));
    assert(media_command(NULL) == MEDIA_NONE);
    packet = packet_create("kdeconnect.mpris.request");
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    cJSON_AddNumberToObject(body, "action", 1);
    assert(media_command(packet) == MEDIA_NONE);
    cJSON_Delete(packet);
    packet = media_player_list(); assert(packet);
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    list = cJSON_GetObjectItemCaseSensitive(body, "playerList");
    assert(cJSON_IsArray(list) && cJSON_GetArraySize(list) == 1);
    assert(!strcmp(cJSON_GetArrayItem(list, 0)->valuestring, "Windows media keys"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(body, "fallbackPlayer")->valuestring, "Windows media keys"));
    cJSON_Delete(packet);
    packet = media_player_info(); assert(packet);
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "canGoNext")));
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(body, "canSeek")));
    cJSON_Delete(packet);
    puts("Media protocol checks passed: advertised player, supported keys and rejection of unrelated requests");
    return 0;
}
