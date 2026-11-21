/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent test writer for an OpenZFS location-bound embedded checksum.
 */
#ifndef ZFS_LABEL_FIXTURE_H
#define ZFS_LABEL_FIXTURE_H
#include <openssl/evp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void fixture_word(uint8_t *out, uint64_t value, int big)
{
    for (unsigned i = 0; i < 8; ++i)
        out[big ? 7u - i : i] = (uint8_t)(value >> (8u * i));
}

static void seal_label(uint8_t *block, size_t size, uint64_t offset, int big)
{
    uint8_t *sum = block + size - 32;
    uint8_t digest[32];
    unsigned length = 0;
    fixture_word(sum - 8, UINT64_C(0x0210da7ab10c7a11), big);
    memset(sum, 0, 32);
    fixture_word(sum, offset, big);
    if (!EVP_Digest(block, size, digest, &length, EVP_sha256(), NULL) || length != 32)
        abort();
    for (unsigned i = 0; i < 32; ++i)
        sum[i] = digest[big ? i : (i & ~7u) + 7u - (i & 7u)];
}
#endif
