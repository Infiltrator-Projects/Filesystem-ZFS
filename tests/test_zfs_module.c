/* SPDX-License-Identifier: GPL-3.0-or-later
 * Legacy worker preservation and genuine Common V2 bounded-host qualification.
 */
#include "ifs_zfs_module_v1.h"
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "ZFS ABI test failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)
typedef struct { uint8_t *image; uint64_t length; uint32_t sector; int fail; } Image;
static int image_read(void *opaque, uint64_t at, void *dst, uint32_t bytes)
{
    Image *image = (Image *)opaque;
    if (image->fail || at % image->sector || bytes % image->sector ||
        bytes > image->sector || at > image->length ||
        bytes > image->length - at) return -1;
    memcpy(dst, image->image + at, bytes);
    return 0;
}
static void *image_allocate(void *p, size_t size)
{ (void)p; return malloc(size); }
static void image_release(void *p, void *value)
{ (void)p; free(value); }
static void put_le64(uint8_t *p, uint64_t value)
{
    unsigned i;
    for (i = 0u; i < 8u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
int main(void)
{
    const IfsZfsModuleV1 *legacy = infiltrator_filesystem_zfs_v1();
    CHECK(legacy && legacy->abi_version == IFS_ZFS_MODULE_V1);
    CHECK(legacy->structure_bytes >= sizeof(*legacy));
    CHECK(legacy->format_id && strcmp(legacy->format_id, "zfs") == 0);
    CHECK(legacy->capabilities == (IFS_ZFS_CAP_RUN | IFS_ZFS_CAP_FIXTURE));
    CHECK(legacy->run != NULL && legacy->fixture != NULL);
    char *args[] = {"defragmenter-zfs-worker", "--version", NULL};
    CHECK(legacy->run(2, args) == 0);

    const IfsFsModuleApiV2 *api = infiltrator_filesystem_api_v2();
    CHECK(ifs_fs_api_v2_valid(api, "zfs"));
    CHECK(api->runtime_profiles == IFS_FS_PROFILE_HOSTED);
    CHECK(api->group_mask == IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE));
    const IfsFsCoreOpsV2 *core =
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api, IFS_FS_GROUP_CORE);
    CHECK(core && core->header.capabilities == IFS_FS_CORE_PROBE);
    CHECK(core->probe && !core->mount && !core->write && !core->read);

    Image image = {0};
    image.length = 2u * 1024u * 1024u;
    image.sector = 512u;
    image.image = (uint8_t *)calloc(1u, (size_t)image.length);
    CHECK(image.image != NULL);
    IfsFsHostRuntimeV2 host = {0};
    host.abi_version = IFS_FS_HOST_ABI_V2;
    host.structure_bytes = sizeof(host);
    host.capabilities = IFS_FS_HOST_READ_ONLY;
    host.volume_bytes = image.length;
    host.logical_sector_bytes = 512u;
    host.physical_sector_bytes = 512u;
    host.maximum_transfer_bytes = 512u;
    host.context = &image;
    host.read_at = image_read;
    host.allocate = image_allocate;
    host.release = image_release;
    CHECK(ifs_fs_host_v2_valid(&host));

    uint32_t variant = 99u;
    CHECK(core->probe(&host, &variant) == IFS_FS_NOT_FOUND && variant == 0u);
    /* Committed ZFS uberblock in the first leaf vdev label's ring. */
    uint8_t *uberblock = image.image + 128u * 1024u;
    put_le64(uberblock, UINT64_C(0x00bab10c));
    put_le64(uberblock + 8u, 5000u);
    put_le64(uberblock + 16u, 42u);
    CHECK(core->probe(&host, &variant) == IFS_FS_OK && variant == 1u);

    /* A 4Kn host must never receive a sub-sector read from the module. */
    host.logical_sector_bytes = 4096u;
    host.physical_sector_bytes = 4096u;
    host.maximum_transfer_bytes = 4096u;
    image.sector = 4096u;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->probe(&host, &variant) == IFS_FS_OK && variant == 1u);
    image.fail = 1;
    CHECK(core->probe(&host, &variant) == IFS_FS_IO_ERROR);
    image.fail = 0;
    host.read_at = NULL;
    CHECK(core->probe(&host, &variant) == IFS_FS_INVALID);
    free(image.image);
    puts("ZFS-COMMON-V2-HOSTED-PROBE-PASS");
    return 0;
}
