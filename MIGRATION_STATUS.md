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

## 21 November 2026 — Common V2 label-config I/O errors

The shared native ZFS label scanner now distinguishes an unreadable or
short label-configuration read from a missing/unsupported NVLIST. Its
hosted Common V2 CORE.PROBE returns `IFS_FS_IO_ERROR` when the real host
fails the configuration read after locating a committed uberblock; previously
it silently returned a successful probe. A targeted Common V2 test forces
this late I/O failure on aligned 4 KiB host requests, restores media I/O,
then confirms the same source image is still recognized. No write callbacks
are used. The ZFS provider remains probe-only: no MOS/dataset namespace,
freestanding reader, production BOOT acceptance or OVMF qualification.
