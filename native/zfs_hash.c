/* SPDX-License-Identifier: GPL-3.0-or-later
 * SHA-256 per FIPS 180-4 sections 5 and 6. All word operations are modulo
 * 2^32. Labels and uberblocks may originate on a host of either byte order.
 */
#include "zfs_hash.h"
#include <infiltratr/endian.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint8_t pending[64];
    size_t used;
} Sha256;

static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void compress(Sha256 *hash, const uint8_t block[64])
{
    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i)
        words[i] = infiltratr_load_be32(block + 4 * i);
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x = words[i - 15], y = words[i - 2];
        uint32_t s0 = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
        uint32_t s1 = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a = hash->state[0], b = hash->state[1], c = hash->state[2], d = hash->state[3];
    uint32_t e = hash->state[4], f = hash->state[5], g = hash->state[6], h = hash->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        uint32_t t1 = h + s1 + ((e & f) ^ (~e & g)) + constants[i] + words[i];
        uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        uint32_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    hash->state[0] += a; hash->state[1] += b; hash->state[2] += c; hash->state[3] += d;
    hash->state[4] += e; hash->state[5] += f; hash->state[6] += g; hash->state[7] += h;
}

static void update(Sha256 *hash, const uint8_t *data, size_t n)
{
    while (n) {
        if (!hash->used && n >= 64) {
            compress(hash, data);
            data += 64;
            n -= 64;
            continue;
        }
        const size_t room = sizeof(hash->pending) - hash->used;
        const size_t take = n < room ? n : room;
        memcpy(hash->pending + hash->used, data, take);
        hash->used += take;
        data += take;
        n -= take;
        if (hash->used == 64) {
            compress(hash, hash->pending);
            hash->used = 0;
        }
    }
}

void zfs_sha256(const void *prefix, size_t prefix_bytes, const void *suffix,
                size_t suffix_bytes, uint8_t digest[32])
{
    Sha256 hash = {.state = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                             0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    update(&hash, prefix, prefix_bytes);
    update(&hash, suffix, suffix_bytes);
    uint8_t pad[64] = {0x80}, bits[8];
    infiltratr_store_be64(bits, ((uint64_t)prefix_bytes + suffix_bytes) * 8);
    update(&hash, pad, hash.used < 56 ? 56 - hash.used : 120 - hash.used);
    update(&hash, bits, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i)
        infiltratr_store_be32(digest + 4 * i, hash.state[i]);
}
