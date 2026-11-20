#ifndef CONNECT95_CREDENTIALS_H
#define CONNECT95_CREDENTIALS_H
#include <windows.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

typedef struct {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_pk_context key;
    mbedtls_x509_crt certificate;
    char id[39];
    WCHAR directory[MAX_PATH];
    HANDLE lock;
} Credentials;

void credentials_init(Credentials *credentials);
int credentials_load(Credentials *credentials);
void credentials_free(Credentials *credentials);
int credentials_forget(Credentials *credentials, const char *id);
int credentials_pin(Credentials *credentials, const char *id,
                    const mbedtls_x509_crt *certificate, BOOL save);
#endif
