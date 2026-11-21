/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IFS_ZFS_HASH_H
#define IFS_ZFS_HASH_H
#include <stddef.h>
#include <stdint.h>
/* Hash a block prefix followed by a separate suffix. For ECK the suffix is
 * the physical-offset verifier, so the caller never edits the input block. */
void zfs_sha256(const void *prefix, size_t prefix_bytes, const void *suffix,
                size_t suffix_bytes, uint8_t digest[32]);
#endif
