#ifndef CONNECT95_PAIRING_H
#define CONNECT95_PAIRING_H
#include <stddef.h>
#include <mbedtls/x509_crt.h>
int pairing_verify_peer(const mbedtls_x509_crt *certificate, const char *id, unsigned int flags);
int pairing_code(mbedtls_pk_context *local, mbedtls_pk_context *remote, char code[9]);
/* -2 malformed, -1 pair=false, 0 other packet, 1 pair=true. */
int pairing_message(const char *line, size_t length);
#endif
