// Host tests only (test_hold.c): yaykit_session.c itself on these; test_hold.c answers the calls.
#pragma once
#include <stdint.h>
#define MBEDTLS_PRIVATE(member) member
typedef struct mbedtls_x509_crt mbedtls_x509_crt;
typedef struct {
    int (*f_vrfy)(void *, mbedtls_x509_crt *, int, uint32_t *);
    void *p_vrfy;
} mbedtls_ssl_config;
void mbedtls_ssl_conf_verify(mbedtls_ssl_config *conf, int (*f)(void *, mbedtls_x509_crt *, int, uint32_t *), void *p);
