/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_zfs_module_v1.h"
#include <stdio.h>
#include <string.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "check failed: %s\n", #test); return 1; } } while (0)
int main(void) {
 const IfsZfsModuleV1 *api = infiltrator_filesystem_zfs_v1();
 CHECK(api && api->abi_version == IFS_ZFS_MODULE_V1);
 CHECK(api->structure_bytes >= sizeof(*api));
 CHECK(api->format_id && strcmp(api->format_id, "zfs") == 0);
 CHECK(api->capabilities == IFS_ZFS_CAP_RUN);
 CHECK(api->run != NULL);
 char *args[] = {"defragmenter-zfs-worker", "--version", NULL};
 CHECK(api->run(2, args) == 0);
 return 0;
}
