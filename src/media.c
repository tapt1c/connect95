#include "media.h"
#include <string.h>

int media_request(const cJSON *packet)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(packet, "type");
    return cJSON_IsString(type) && !strcmp(type->valuestring, "kdeconnect.mpris.request") &&
        cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(packet, "body"));
}

int media_command(const cJSON *packet)
{
    const cJSON *body, *player, *action;
    if (!media_request(packet)) return MEDIA_NONE;
    body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    player = cJSON_GetObjectItemCaseSensitive(body, "player");
    action = cJSON_GetObjectItemCaseSensitive(body, "action");
    if (!cJSON_IsString(player) || strcmp(player->valuestring, "Windows media keys") || !cJSON_IsString(action)) return MEDIA_NONE;
    if (!strcmp(action->valuestring, "PlayPause") || !strcmp(action->valuestring, "Play") || !strcmp(action->valuestring, "Pause")) return MEDIA_TOGGLE;
    if (!strcmp(action->valuestring, "Stop")) return MEDIA_STOP;
    if (!strcmp(action->valuestring, "Next")) return MEDIA_NEXT;
    if (!strcmp(action->valuestring, "Previous")) return MEDIA_PREVIOUS;
    return MEDIA_NONE;
}

cJSON *media_player_list(void)
{
    cJSON *packet = packet_create("kdeconnect.mpris");
    cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    cJSON *players = body ? cJSON_AddArrayToObject(body, "playerList") : NULL;
    if (!players || !cJSON_AddItemToArray(players, cJSON_CreateString("Windows media keys")) ||
        !cJSON_AddStringToObject(body, "fallbackPlayer", "Windows media keys") ||
        !cJSON_AddBoolToObject(body, "supportAlbumArtPayload", 0)) {
        cJSON_Delete(packet); return NULL;
    }
    return packet;
}

cJSON *media_player_info(void)
{
    cJSON *packet = packet_create("kdeconnect.mpris");
    cJSON *body = cJSON_GetObjectItemCaseSensitive(packet, "body");
    if (!body || !cJSON_AddStringToObject(body, "player", "Windows media keys") ||
        !cJSON_AddBoolToObject(body, "canPlay", 1) || !cJSON_AddBoolToObject(body, "canPause", 1) ||
        !cJSON_AddBoolToObject(body, "canGoNext", 1) || !cJSON_AddBoolToObject(body, "canGoPrevious", 1) ||
        !cJSON_AddBoolToObject(body, "canSeek", 0) || !cJSON_AddBoolToObject(body, "canControl", 1) ||
        !cJSON_AddBoolToObject(body, "isPlaying", 0) || !cJSON_AddStringToObject(body, "title", "Windows media keys")) {
        cJSON_Delete(packet); return NULL;
    }
    return packet;
}
