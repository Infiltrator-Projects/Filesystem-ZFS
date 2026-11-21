/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <infiltratr/filesystem_module_v2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    uint32_t sector;
} Device;

static int read_at(void *ctx, uint64_t offset, void *out, uint32_t bytes)
{
    Device *device = ctx;
    if (!bytes || bytes > device->sector || bytes % device->sector || offset % device->sector)
        return -1;
    return pread(device->fd, out, bytes, (off_t)offset) == (ssize_t)bytes ? 0 : -1;
}
static void *allocate(void *ctx, size_t n) { (void)ctx; return malloc(n); }
static void release(void *ctx, void *p) { (void)ctx; free(p); }

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    const int expected = !strcmp(argv[3], "valid") ? IFS_FS_OK : IFS_FS_CORRUPT;
    if (expected != IFS_FS_OK && strcmp(argv[3], "corrupt")) return 2;
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) { fputs(dlerror(), stderr); return 1; }
    void *symbol = dlsym(module, IFS_FS_COMMON_EXPORT_V2);
    IfsFsModuleApiEntryV2 entry;
    if (!symbol || sizeof(symbol) != sizeof(entry)) return 1;
    memcpy(&entry, &symbol, sizeof(entry));
    const IfsFsModuleApiV2 *api = entry();
    if (!ifs_fs_api_v2_valid(api, "zfs")) return 1;
    const IfsFsCoreOpsV2 *core = (const void *)ifs_fs_group_v2(api, IFS_FS_GROUP_CORE);
    Device device = {.fd = open(argv[2], O_RDONLY)};
    struct stat st;
    if (device.fd < 0 || fstat(device.fd, &st) || !S_ISREG(st.st_mode)) return 1;
    for (device.sector = 512; device.sector <= 4096; device.sector *= 8) {
        IfsFsHostRuntimeV2 host = {
            .abi_version = IFS_FS_HOST_ABI_V2, .structure_bytes = IFS_FS_HOST_V2_BASE_BYTES,
            .capabilities = IFS_FS_HOST_READ_ONLY, .volume_bytes = (uint64_t)st.st_size,
            .logical_sector_bytes = device.sector, .physical_sector_bytes = device.sector,
            .maximum_transfer_bytes = device.sector, .context = &device, .read_at = read_at,
            .allocate = allocate, .release = release};
        uint32_t endian = 0;
        int status = core->probe(&host, &endian);
        if (status != expected || (expected == IFS_FS_OK ? (endian != 1 && endian != 2) : !!endian)) {
            fprintf(stderr, "ZFS probe for %s returned %d, expected %d on %u-byte I/O\n",
                    argv[2], status, expected, device.sector);
            return 1;
        }
    }
    close(device.fd);
    return dlclose(module) != 0;
}
