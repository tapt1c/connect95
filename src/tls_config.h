#ifndef CONNECT95_TLS_CONFIG_H
#define CONNECT95_TLS_CONFIG_H
/* Keep upstream TLS checks. Replace only the OS entropy provider for XP. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#undef MBEDTLS_NET_C
/* MinGW supplies C99 formatting without Vista's _vsnprintf_s. */
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO vsnprintf
#endif
