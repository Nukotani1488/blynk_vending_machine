#include "helpers.h"

#include <Arduino.h>
#include <mbedtls/md.h>

bool constant_time_equal(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t result = 0;
    for (size_t i = 0; i < a.length(); i++) result |= a[i] ^ b[i];
    return result == 0;
}

bool verify_admin_password(const String& password, const String& hash) {
    return constant_time_equal(hash_password(password), hash);
}

String hash_password(const String& password) {
    unsigned char hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)password.c_str(), password.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    String hex_hash = "";
    for (int i = 0; i < 32; i++) {
        char buf[3];
        sprintf(buf, "%02x", hash[i]);
        hex_hash += buf;
    }
    return hex_hash;
}
