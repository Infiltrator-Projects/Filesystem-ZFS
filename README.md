# Filesystem-ZFS

## 1. Purpose and format scope

ZFS pool identification and native metadata inspection. This repository owns the format-specific implementation of `ZFS.so` for InfiltratorOS.

## 2. On-disk architecture and invariants

Physical labels, XDR configuration records, uberblocks and checksummed metadata describe pool identity. Verify SHA-256 trailers using the correct physical locations; bound all partition-relative reads. The documented probe supports 512-byte and 4 KiB transfer sectors.

## 3. Common V2 implementation and capabilities

The provider declares CORE probe and a VOLUME format callback; consult exact hosted masks and qualification for format availability. Hosted and freestanding pool probes share the native label and uberblock reader. The regression advertises cancellation capability. The canonical entry point is `infiltrator_filesystem_api_v2`. Consumers must negotiate the actual profile and operation masks and use bounded, partition-relative host I/O. A source-level callback does not by itself establish that every format variant or host profile is qualified.

## 4. Integrity, mutation, allocation and recovery

Do not infer dataset mounting, namespace traversal, file reads, arbitrary pool mutation, vdev topology support, transaction replay or BOOT eligibility. Historical analyser code is not a public capability contract. For destructive operations, require the applicable exclusive writable host, durability contract and format-specific validation. Never silently substitute a kernel or FUSE driver or treat a rejected or unavailable operation as success. Preserve original disk-format semantics; relocation and recovery are separate from ordinary file I/O.

## 5. Build, installation and release

The target installation is `/System/Filesystems/ZFS.so` and `/System/Resources/Documentation/Filesystem-ZFS.pdf`. The project release policy is a portable amd64 DEB and a CPU-native source-first RUN, with GitHub's automatic source archive; do not treat this policy statement as verification of the current installer payload. The release workflow is the authoritative executable build and package specification. Historical adapters and test artifacts are not additional production filesystem authorities.

## 6. Qualification and limitations

[Recorded qualification workflow](https://github.com/Infiltrator-Projects/Filesystem-ZFS/actions/runs/45526919027) · [Current workflows](https://github.com/Infiltrator-Projects/Filesystem-ZFS/actions?query=branch%3Amain). A successful workflow establishes the tested scope only, not complete original-format parity, physical-media crash safety, or production BOOT/kernel integration. Use disposable images for destructive qualification.

## 7. Technical references and provenance

- [Provider implementation](module/zfs_module.c)
- [Native design or source provenance](native/filesystems/zfs/DESIGN.md)
- [Migration and source history](MIGRATION_STATUS.md)
- [Original-format reference source](docs/ZFS.pages) and [PDF](docs/ZFS.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

The original-format reference and historical migration notes describe different evidence from the current production provider. When descriptions disagree, the current exported descriptor, implementation, executable tests and exact-revision release evidence take precedence.
