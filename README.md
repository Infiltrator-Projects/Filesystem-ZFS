# Filesystem-ZFS
Independent filesystem implementation for InfiltratorOS, using the shared Common ABI.

## Installer files

- `/System/Filesystems/ZFS.so`
- `/System/Resources/Documentation/Filesystem-ZFS.pdf`

The Debian and native installer candidates stage the Common V2 module and its manual.

## Common V2 operations

`ZFS.so` provides `CORE.PROBE` using the same native label, XDR and uberblock reader in its hosted and embedded freestanding profiles. It checks the OpenZFS SHA-256 trailer at each physical label and uberblock location and uses bounded, partition-relative Common V2 reads. The released v1.0.10 binary predates these changes.

Pool recognition is available on 512-byte and 4 KiB transfer sectors. The module does not advertise mounting, dataset traversal, file reads or directory enumeration. The historical allocation analyser is built for regression tests separately. BOOT volume qualification remains pending.
