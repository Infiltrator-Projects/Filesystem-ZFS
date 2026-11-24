#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Use immutable data from the OpenZFS pool-upgrade tests. No ZFS driver is used.
set -euo pipefail
probe=$(realpath "${1:?V2 media test}")
shift
modules=()
for module in "$@"; do modules+=("$(realpath "$module")"); done
((${#modules[@]} > 0))
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
if [[ -n ${ZFS_TEST_POOL_IMAGE:-} ]]; then
    cp --sparse=always --reflink=auto "$ZFS_TEST_POOL_IMAGE" clean.img
else
    uri=https://raw.githubusercontent.com/openzfs/zfs/a1daebfec3f5ca46ac261ef339efdf0dbeae997c
    path=tests/zfs-tests/tests/functional/cli_root/zpool_upgrade/blockfiles/zfs-pool-v15.dat.bz2
    curl -fsSL --retry 3 "$uri/$path" -o pool.bz2
    echo '03941b96c88b5e851a89f7e7092f3a62116da7946dbbef315c00f3d26a5e0019  pool.bz2' | sha256sum -c -
    bzip2 -dc pool.bz2 > clean.img
fi
if [[ -n ${ZFS_MOS_TEST:-} ]]; then "$ZFS_MOS_TEST" clean.img; fi
for copy in redundant bad-config bad-ring relocated; do
    cp --sparse=always --reflink=auto clean.img "$copy.img"
done
python3 - <<'PY'
import mmap
from pathlib import Path
label = 256 * 1024
size = Path('clean.img').stat().st_size // label * label
offsets = (0, label, size - 2 * label, size - label)
for name in ('redundant', 'bad-config', 'bad-ring', 'relocated'):
    with open(name + '.img', 'r+b') as f, mmap.mmap(f.fileno(), 0) as disk:
        for i, base in enumerate(offsets):
            config = base + 16 * 1024
            if name in ('redundant', 'bad-config'):
                if name == 'bad-config' or i == 0:
                    disk[config + 75000] ^= 1
            elif name == 'bad-ring':
                # Every real slot starts on a 1 KiB boundary or a multiple.
                # Damage the body while keeping its magic, TXG and trailer.
                for slot in range(base + 128 * 1024, base + label, 1024):
                    disk[slot + 48] ^= 1
            elif i in (0, 2):
                other = offsets[i + 1] + 16 * 1024
                length = 112 * 1024
                a = bytes(disk[config:config + length])
                disk[config:config + length] = disk[other:other + length]
                disk[other:other + length] = a
PY
sha256sum clean.img redundant.img bad-config.img bad-ring.img relocated.img > before
profiles=()
for module in "${modules[@]}"; do
    firmware="$work/reader-${#profiles[@]}.elf"
    objcopy --dump-section ".infiltrator_boot=$firmware" "$module" "$work/carrier-copy.so"
    test -s "$firmware"
    test -z "$(nm -Du "$firmware")"
    if readelf -dW "$firmware" | grep '(NEEDED)' >/dev/null; then exit 1; fi
    profiles+=("$module" "$firmware")
done
for module in "${profiles[@]}"; do
    "$probe" "$module" clean.img valid
    "$probe" "$module" redundant.img valid
    for copy in bad-config bad-ring relocated; do "$probe" "$module" "$copy.img" corrupt; done
done
sha256sum -c before
