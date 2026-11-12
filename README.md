# Filesystem-ZFS
Independent filesystem implementation for InfiltratorOS, using the shared Common ABI.

## Installer files

- `/System/Filesystems/ZFS.so`
- `/System/Resources/Documentation/Filesystem-ZFS.pdf`

The current package builds the shared object and generates the PDF. A standalone kernel module is pending.

InfiltratorOS calls `--system-package-mode` and decides which outputs need updating. Its worker builds a missing or older SO, repairs the PDF independently, and stores the matching released kernel module when available. Use `--build-only --output DIRECTORY` to build a Debian package.

## Common filesystem ABI migration

`ZFS.so` now exposes the canonical Common ABI V2 `CORE.PROBE` operation
using bounded, partition-relative host reads through the existing native ZFS
uberblock/label decoder. The advertised profile is **HOSTED only**; probing
supports aligned 512-byte and 4-KiB-sector hosts and does not write media.

**Not yet a completed V2 migration:** the existing read-only analyser worker
still uses the dedicated analysis interface while the Defragmenter consumer is
being converted. V2 does not yet expose mount, directory reads, allocation
mapping, format, repair, relocation, kernel or UEFI capabilities. Do not treat
an advertised V2 probe as evidence that these operations exist. The V2 path has no fallback.
