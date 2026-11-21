/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "zfs_hash.h"
#include <openssl/evp.h>
#include <string.h>

/* The OpenSSL oracle is test-only. Every split and every padding boundary
 * through three SHA blocks exercises the same two-piece path used for ECK. */
int main(void)
{
    uint8_t bytes[192], reference[32], actual[32];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (uint8_t)(7 + 19 * i);
    for (size_t length = 0; length <= sizeof(bytes); ++length) {
        unsigned produced = 0;
        if (!EVP_Digest(bytes, length, reference, &produced, EVP_sha256(), NULL) || produced != 32)
            return 1;
        for (size_t split = 0; split <= length; ++split) {
            zfs_sha256(bytes, split, bytes + split, length - split, actual);
            if (memcmp(actual, reference, sizeof(reference))) return 1;
        }
    }
    return 0;
}
