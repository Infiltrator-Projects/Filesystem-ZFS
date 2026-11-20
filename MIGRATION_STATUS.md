# ZFS Common V2 migration status

The real ZFS label/uberblock scanner is currently a **hosted probe-only**
Common V2 implementation. It may identify a candidate pool with supported
basic label geometry, but does not mount or traverse the MOS, datasets, object
sets, dnodes, block pointers, checksums, compression, indirect extents,
directories, names or file contents. It is not a BOOT filesystem reader and
must not be presented as one.

The installed ZFS.so now links only its source-owned native probe and Common V2
descriptor, with no historical native analyser or V1 module export. The full
analysis and CLI sources remain in **non-installed regression tests**.
The hosted packaging pipeline creates only a V2 SO plus PDF, and no official
new release is published until the same engine passes freestanding ELF,
unmodified production BOOT validation, OVMF kernel/initramfs reads, feature
refusals, corruption and nonmutation tests. This deliberately avoids inventing
an empty namespace merely to satisfy a validator.
