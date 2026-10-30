/* SPDX-License-Identifier: GPL-3.0-or-later
 * Adapter to Libraries Common's canonical checked POSIX exact I/O.
 */
#ifndef IFS_ZFS_LD_IO_H
#define IFS_ZFS_LD_IO_H
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "infiltratr/posix_io.h"
static inline ssize_t ld_pread_full(int fd, void *data, size_t length, uint64_t at) {
    if (length > (size_t)SSIZE_MAX) { errno = EOVERFLOW; return -1; }
    return infiltratr_pread_full(fd, data, length, at) == 0 ? (ssize_t)length : -1;
}
#endif
