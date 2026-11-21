// SPDX-License-Identifier: GPL-3.0-or-later
#include "zfs_native.h"

#include "ld_io.h"
#include "infiltratr/core.h"
#include "infiltratr/endian.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static void zfs_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0U)
        (void)snprintf(error, error_size, "%s", message);
}

static int size_bytes_for_fd(int fd, const struct stat *status, uint64_t *size_bytes)
{
    if (S_ISREG(status->st_mode)) {
        if (status->st_size < 0) {
            errno = EINVAL;
            return -1;
        }
        *size_bytes = (uint64_t)status->st_size;
        return 0;
    }
    if (S_ISBLK(status->st_mode)) {
        uint64_t bytes = 0U;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
            return -1;
        *size_bytes = bytes;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

static int read_device(void *ctx, uint64_t offset, void *out, uint32_t count)
{
    return ld_pread_full(*(const int *)ctx, out, count, offset) == (ssize_t)count ? 0 : -1;
}
static void *allocate(void *ctx, size_t count) { (void)ctx; return malloc(count); }
static void release(void *ctx, void *p) { (void)ctx; free(p); }

int zfs_read_summary(const char *path, LdZfsSummary *summary,
                     char *error, size_t error_size)
{
    if (path == NULL || summary == NULL) {
        zfs_error(error, error_size, "invalid ZFS summary request");
        return -1;
    }
    memset(summary, 0, sizeof(*summary));

    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (error != NULL && error_size != 0U)
            (void)snprintf(error, error_size, "open: %s", strerror(errno));
        return -1;
    }

    struct stat status;
    if (fstat(fd, &status) != 0 ||
        size_bytes_for_fd(fd, &status, &summary->size_bytes) != 0) {
        const int saved_errno = errno;
        (void)close(fd);
        errno = saved_errno;
        if (error != NULL && error_size != 0U)
            (void)snprintf(error, error_size, "size: %s", strerror(errno));
        return -1;
    }

    IfsFsHostRuntimeV2 host = {
        .abi_version = IFS_FS_HOST_ABI_V2, .structure_bytes = sizeof(host),
        .capabilities = IFS_FS_HOST_READ_ONLY, .volume_bytes = summary->size_bytes,
        .logical_sector_bytes = 512, .physical_sector_bytes = 512,
        .maximum_transfer_bytes = 128 * 1024, .context = (void *)&fd,
        .read_at = read_device, .allocate = allocate, .release = release
    };
    int result = zfs_read_summary_host_v2(&host, summary);

    const int saved_errno = errno;
    (void)close(fd);
    errno = saved_errno;

    if (result == IFS_FS_IO_ERROR) {
        if (error != NULL && error_size != 0U)
            (void)snprintf(error, error_size,
                           "cannot read ZFS vdev labels: %s", strerror(errno));
        return -1;
    }
    if (result != 0) {
        zfs_error(error, error_size,
                  "not a recognised ZFS member with a committed label uberblock");
        return -1;
    }
    if (error != NULL && error_size != 0U)
        error[0] = '\0';
    return 0;
}

bool zfs_probe(const char *path)
{
    LdZfsSummary summary;
    return zfs_read_summary(path, &summary, NULL, 0U) == 0;
}

const char *zfs_byte_order_name(const LdZfsSummary *summary)
{
    if (summary == NULL)
        return "unknown";
    return summary->byte_order == LD_ZFS_BYTE_ORDER_LITTLE ? "little" : "big";
}
