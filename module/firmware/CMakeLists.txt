# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(ZfsFirmware LANGUAGES C)
add_library(zfs-boot MODULE ../zfs_module.c ../../native/zfs_disk.c ../../native/zfs_hash.c runtime.c)
set_target_properties(zfs-boot PROPERTIES PREFIX "" OUTPUT_NAME ZFS C_VISIBILITY_PRESET hidden)
target_compile_definitions(zfs-boot PRIVATE IFS_ZFS_FIRMWARE=1)
target_compile_features(zfs-boot PRIVATE c_std_11)
target_include_directories(zfs-boot PRIVATE "${COMMON_INCLUDE_DIR}" ../../native)
target_compile_options(zfs-boot PRIVATE -Wall -Wextra -Wpedantic -Werror
    -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone
    -fno-tree-loop-distribute-patterns -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_link_options(zfs-boot PRIVATE -nostdlib -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now)
