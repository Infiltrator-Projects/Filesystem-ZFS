/* SPDX-License-Identifier: GPL-3.0-or-later
 * The real ZFS summary parser, checksum-verified MOS analyser, space-map
 * accounting and worker entrypoint are all compiled into this shared object.
 */
#include "ifs_zfs_module_v1.h"
int ifs_zfs_worker_main(int, char **);
static const IfsZfsModuleV1 v1 = {
    IFS_ZFS_MODULE_V1, sizeof(IfsZfsModuleV1), "zfs",
    IFS_ZFS_CAP_RUN, ifs_zfs_worker_main
};
const IfsZfsModuleV1 *infiltrator_filesystem_zfs_v1(void) {
    return &v1;
}
