/* SPDX-License-Identifier: GPL-3.0-or-later
 * The real ZFS summary parser, checksum-verified MOS analyser, space-map
 * accounting and worker entrypoint are all compiled into this shared object.
 */
#include "ifs_zfs_module_v1.h"
int ifs_zfs_worker_main(int, char **);
int ifs_zfs_fixture_run(const void *, const char *, int, const void *);
static const IfsZfsModuleV1 v1 = {
    IFS_ZFS_MODULE_V1, sizeof(IfsZfsModuleV1), "zfs",
    IFS_ZFS_CAP_RUN | IFS_ZFS_CAP_FIXTURE,
    ifs_zfs_worker_main, ifs_zfs_fixture_run
};
const IfsZfsModuleV1 *infiltrator_filesystem_zfs_v1(void) {
    return &v1;
}

/* Canonical Common V2. This stage advertises only proven read-only probing.
 * The old analysis worker is retained until its consumer is migrated. */
#include <infiltratr/filesystem_module_v2.h>
#include "zfs_native.h"

static int zfs_v2_probe(const IfsFsHostRuntimeV2 *host, uint32_t *variant)
{
    LdZfsSummary summary;
    int result;
    if (!variant) return IFS_FS_INVALID;
    *variant = 0u;
    result = zfs_read_summary_host_v2(host, &summary);
    if (result == IFS_FS_OK)
        *variant = (uint32_t)summary.byte_order;
    return result;
}
static const IfsFsCoreOpsV2 zfs_core_v2 = {
    .header = {IFS_FS_GROUP_ABI_V2, sizeof(IfsFsCoreOpsV2),
               IFS_FS_GROUP_CORE, 0u, IFS_FS_CORE_PROBE},
    .probe = zfs_v2_probe
};
static const IfsFsGroupHeaderV2 *zfs_query_v2(
    uint32_t id, uint32_t minimum, uint32_t maximum)
{
    return id == IFS_FS_GROUP_CORE &&
           minimum <= IFS_FS_GROUP_ABI_V2 &&
           maximum >= IFS_FS_GROUP_ABI_V2 ? &zfs_core_v2.header : NULL;
}
static const IfsFsModuleApiV2 zfs_api_v2 = {
    .abi_version = IFS_FS_COMMON_ABI_V2,
    .structure_bytes = sizeof(IfsFsModuleApiV2),
    .format_id = "zfs",
    .runtime_profiles = IFS_FS_PROFILE_HOSTED,
    .group_mask = IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),
    .query_group = zfs_query_v2
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &zfs_api_v2;
}
