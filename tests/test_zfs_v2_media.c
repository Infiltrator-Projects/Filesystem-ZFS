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
    uint64_t fail_begin,fail_end;
    int fail_io, fail_alloc, outstanding;
} Device;

static int read_at(void *ctx, uint64_t offset, void *out, uint32_t bytes)
{
    Device *device = ctx;
    if (device->fail_io ||
        (offset>=device->fail_begin && offset<device->fail_end) ||
        !bytes || bytes > device->sector || bytes % device->sector || offset % device->sector)
        return -1;
    return pread(device->fd, out, bytes, (off_t)offset) == (ssize_t)bytes ? 0 : -1;
}
static void *allocate(void *ctx, size_t n)
{
    Device *device = ctx;
    if (device->fail_alloc) return NULL;
    void *p = malloc(n);
    if (p) ++device->outstanding;
    return p;
}
static void release(void *ctx, void *p)
{
    Device *device = ctx;
    if (p) --device->outstanding;
    free(p);
}

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
    const char *suffix = strrchr(argv[1], '.');
    uint64_t profile = suffix && !strcmp(suffix, ".elf") ?
        IFS_FS_PROFILE_FIRMWARE | IFS_FS_PROFILE_FREESTANDING : IFS_FS_PROFILE_HOSTED;
    if (api->runtime_profiles != profile || !core || !core->probe ||
        core->header.capabilities != IFS_FS_CORE_PROBE || core->mount || core->read) return 1;
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
        if (device.outstanding) return 1;
        if (expected==IFS_FS_OK) {
            /* An unreadable primary 256 KiB label must not hide a real
             * pool with independent committed copies in its other labels. */
            device.fail_begin=0;
            device.fail_end=256u*1024u;
            endian=0;
            if (core->probe(&host,&endian)!=IFS_FS_OK ||
                (endian!=1 && endian!=2) || device.outstanding) return 1;
            device.fail_begin=device.fail_end=0;
        }
        device.fail_alloc = 1;
        if (core->probe(&host, &endian) != IFS_FS_NO_MEMORY || endian || device.outstanding) return 1;
        device.fail_alloc = 0;
        device.fail_io = 1;
        if (core->probe(&host, &endian) != IFS_FS_IO_ERROR || endian || device.outstanding) return 1;
        device.fail_io = 0;
    }
    close(device.fd);
    return dlclose(module) != 0;
}
