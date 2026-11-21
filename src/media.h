#ifndef CONNECTXP_MEDIA_H
#define CONNECTXP_MEDIA_H
#include "packet.h"
enum { MEDIA_NONE, MEDIA_TOGGLE, MEDIA_STOP, MEDIA_NEXT, MEDIA_PREVIOUS, MEDIA_VOLUME_UP, MEDIA_VOLUME_DOWN };
int media_command(const cJSON *packet);
cJSON *media_player_list(void);
cJSON *media_player_info(void);
int media_request(const cJSON *packet);
#endif
