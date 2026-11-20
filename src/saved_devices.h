#ifndef CONNECT95_SAVED_DEVICES_H
#define CONNECT95_SAVED_DEVICES_H
#include "discovery.h"
int saved_devices_load(DiscoveryPeer *peers, int capacity);
/* 1 saved certificate present, 0 absent, -1 filesystem unavailable. */
int saved_device_exists(const char *id);
int saved_device_remember(const DiscoveryPeer *peer);
int saved_device_forget_details(const char *id);
#endif
