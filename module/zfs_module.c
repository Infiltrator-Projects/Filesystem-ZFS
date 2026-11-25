/* SPDX-License-Identifier: GPL-3.0-or-later
 * ZFS.so exports the Common V2 filesystem ABI exclusively. Its source-owned
 * read-only ZFS geometry/metadata engine is retained. The historical
 * analyser command-line worker remains in source and is exercised by a
 * separate test-only executable, never embedded as a legacy runtime ABI.
 *
 * Both builds use the same label verification code. The operation mask
 * reflects the pool probe and hosted initial formatter; no mounted namespace.
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
#ifndef IFS_ZFS_FIRMWARE
static const IfsFsVolumeOpsV2 zfs_volume_v2 = {
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,IFS_FS_VOLUME_FORMAT},
    .format=zfs_format_host_v2
};
#endif
static const IfsFsGroupHeaderV2 *zfs_query_v2(
    uint32_t id, uint32_t minimum, uint32_t maximum)
{
    if(minimum>IFS_FS_GROUP_ABI_V2 || maximum<IFS_FS_GROUP_ABI_V2)return NULL;
    if(id==IFS_FS_GROUP_CORE)return &zfs_core_v2.header;
#ifndef IFS_ZFS_FIRMWARE
    if(id==IFS_FS_GROUP_VOLUME)return &zfs_volume_v2.header;
#endif
    return NULL;
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
    .group_mask = IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)
#ifndef IFS_ZFS_FIRMWARE
        | IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)
#endif
    ,
    .query_group = zfs_query_v2
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &zfs_api_v2;
}
