# ZFS Common V2 migration status — source and release qualification

The independent `/System/Filesystems/ZFS.so` production module exports
**only** `infiltrator_filesystem_api_v2` through the version script in
`module/v2-only.map`. Its compiled source is limited to
`native/zfs_disk.c`, `native/zfs_hash.c` and `module/zfs_module.c`.
The V1 module declaration has been deleted. The original complete ZFS
analysis, MOS, dnode, ZAP and transactional allocation parsers remain
source-owned **non-installed test/analyser code**, not a legacy runtime
filesystem ABI or a substitute Linux VFS implementation.

The Common V2 hosted and freestanding profiles share the exact same
native label, uberblock and committed pool-geometry probe. They validate
SHA-256 on known OpenZFS labels and selected uberblocks; account for
128-entry uberblock rings; propagate failed I/O after finding a pool
instead of inventing a successful match; and refuse unsupported
topologies and unknown metadata. Source-owned unit and real-media tests
cover damaged label replicas, relocated labels, valid and corrupt
uberblocks, 512 B and 4 KiB host sector alignment, and unchanged media.

**The production V2 module intentionally advertises only CORE.PROBE.**
It cannot yet MOUNT, STAT, LIST or READ datasets or files. ZFS has no
qualified ZAP/dnode native **file namespace reader** in the firmware
profile, and must not be claimed BOOT-bootable or file-reader complete.
The existence of source-owned analysis routines is not a substitute
for implementing those operations under Common V2.

The new source version 1.0.11 replaces the previous v1.0.10 published
carrier, whose dynamic symbol table failed the strict all-23 Common V2
release audit. The updated `.deb` and source `.run` may be published
only after exact-head hosted/freestanding unit, independent pool
media, packaging and public-export checks pass. Until publication,
v1.0.10 is **not V2-only qualified**. The dedicated ZFS release
workflow now contains a gated `publish` job, rather than only
uploading transient build artifacts.

Production BOOT itself is not Common V2-capable; no actual BOOT V2
validator/OVMF handoff has been qualified. ZFS never receives a
success designation based on a fabricated empty namespace or disabled
validation. No BOOT repository, VM, ESP, firmware, kernel or physical
medium was changed.
