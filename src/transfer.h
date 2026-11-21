#ifndef CONNECTXP_TRANSFER_H
#define CONNECTXP_TRANSFER_H
#include "transport.h"
#include "share_info.h"
typedef void (*TransferProgress)(const WCHAR *text, ULONGLONG done, ULONGLONG total);
typedef struct {
    Connection *control;
    Credentials *credentials;
    const DiscoveryPeer *peer;
    HANDLE cancel;
    TransferProgress progress;
} TransferContext;
const WCHAR *transfer_send(TransferContext *context, const WCHAR *path);
const WCHAR *transfer_receive(TransferContext *context, const ShareInfo *offer);
#endif
