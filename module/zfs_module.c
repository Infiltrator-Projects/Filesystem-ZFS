/* SPDX-License-Identifier: GPL-3.0-or-later
 * ZFS.so exports the Common V2 filesystem ABI exclusively. Its source-owned
 * read-only ZFS geometry/metadata engine is retained. The historical
 * analyser command-line worker remains in source and is exercised by a
 * separate test-only executable, never embedded as a legacy runtime ABI.
 *
 * Both builds use the same label verification code. The operation mask
 * reflects the pool probe and does not advertise a dataset namespace.
 */
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
#ifdef IFS_ZFS_FIRMWARE
    .runtime_profiles = IFS_FS_PROFILE_FREESTANDING | IFS_FS_PROFILE_FIRMWARE,
#else
    .runtime_profiles = IFS_FS_PROFILE_HOSTED,
#endif
    .group_mask = IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),
    .query_group = zfs_query_v2
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &zfs_api_v2;
}
