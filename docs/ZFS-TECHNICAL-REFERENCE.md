# ZFS: technical reference

## Scope and layers

ZFS originated at Sun Microsystems in the early 2000s, entered Solaris in 2005 and evolved into OpenZFS across multiple operating systems. It integrates storage pooling, logical volume management and a copy-on-write filesystem. The on-disk pool is built from virtual devices (vdevs); the storage pool allocator manages blocks, the DMU manages objects and the dataset layer organizes filesystems and snapshots. A filesystem is an object set, not a fixed partition inode table. This guide uses OpenZFS 2.2.7 as a pinned layout reference. Feature flags, encryption, RAIDZ, dRAID, gang blocks and indirect/removal mappings need distinct qualification.

Redundancy, snapshots and data verification are integral design concepts rather than add-ons to a conventional inode filesystem.

## Physical labels and placement

Each leaf vdev has four 256 KiB labels: offsets 0, 256 KiB, aligned device size minus 512 KiB and minus 256 KiB. Device size for this calculation is rounded down to label alignment. Label layout: 8 KiB reserved at 0; 8 KiB boot environment at 8192; 112 KiB vdev physical configuration at 16384; 128 KiB uberblock ring at 131072. Configuration includes a packed nvlist and embedded checksum trailer. The first two labels plus a 3.5 MiB boot region occupy 4 MiB. The last two labels reserve 512 KiB. Validate all available labels; do not assume the first label is authoritative.

## Configuration and pool topology

Decode the nvlist serialization according to its encoding and length rules, not as a native pointer-based structure. Configuration includes pool and vdev GUIDs, transaction information, pool state, ashift, top-level vdev identifiers and topology. Match devices by identity, not OS path names. Vdevs can represent leaf disks, mirrors, RAIDZ and other profiles; a DVA names a top-level vdev and a logical offset within it. Use the appropriate topology-specific translation to member reads. Reject incomplete or conflicting configurations where safe reconstruction is not established.

## Uberblocks and committed roots

Uberblock fields are 64-bit: magic 0x00bab10c at 0, version 8, transaction group (txg) 16, GUID sum 24, timestamp 32; 128-byte MOS root block pointer starts 40. Software version 168, multihost magic 176, delay 184, configuration 192 and checkpoint txg 200 follow in the modern structure. Detect byte order from magic and verify embedded checksums with the correct physical-address verifier. Ring slot size follows the version's vdev uberblock shift rule, normally at least 1024 bytes. Select a supported coherent committed txg across valid configuration and root data; a high txg alone does not establish valid ownership or safe import.

## Block pointer byte layout

A normal 128-byte block pointer contains three 16-byte DVAs at offsets 0,16,32, properties word 48, two padding words 56/64, physical birth 72, logical birth 80, fill count 88 and four 64-bit checksum words at 96. These positions are for the ordinary unencrypted pointer representation; embedded and protected forms reuse fields. Parse in the containing metadata block's byte order. Logical birth and physical birth are not necessarily equal. A zero physical birth field may imply logical birth. Recognize holes and embedded pointers before translating DVAs.

## Block pointer properties

For an ordinary block, property bits 0..15 store logical size in 512-byte units minus one; 16..31 store physical size likewise. Bits 32..38 compression, 39 embedded, 40..47 checksum, 48..55 object type, 56..60 indirect level, 61 protected, 62 dedup and 63 byte order. For example logical size encoding 15 means 8192 bytes; physical size encoding 7 means 4096 bytes. Verify supported algorithms and allocation bounds. Embedded pointers have a different size and payload interpretation. The protected bit may denote encryption, authentication or indirect MAC semantics depending on type and level.

## Data virtual addresses and example

Each ordinary DVA consists of two 64-bit words. Word 0 bits 0..23 allocation size in 512-byte units (not minus one), 24..31 grid, 32..63 vdev ID. Word 1 low 63 bits encode offset in 512-byte units; high bit marks gang. For a plain leaf vdev, offset 8192 units gives 4 MiB relative to the allocatable region, or 8 MiB from the leaf start after the 4 MiB leading reservation. A DVA's allocation size can differ from block physical size, especially with parity and padding. Mirrors and RAIDZ require topology-aware reads; a gang address points to a gang header, not directly to all user bytes.

## Block read and integrity

A reader validates the block pointer and available DVA, maps through the vdev topology, reads the stored physical representation, verifies the applicable checksum or authenticated representation, decompresses to logical size and performs object-type-specific byte swapping as required. Checksum, decompression and cryptographic order must follow the exact ZIO pipeline for the active features. Try valid redundant copies on failure rather than silently returning damaged data. Holes synthesize zeros. A checksum mismatch cannot be repaired by recomputing the checksum over corrupted bytes.

## Object sets and dnode slots

The MOS root pointer resolves an object set whose first 512 bytes are its meta-dnode. The meta-dnode maps the array of object slots; object number times 512 identifies the nominal slot position. Dnode fields: type 0, indirect shift 1, levels 2, pointer count 3, bonus type 4, checksum 5, compression 6, flags 7, data block size in 512-byte sectors at 8 (16), bonus length 10 (16), extra slots 12, maximum block ID 16 (64), used 24 (64). Block pointers begin at 64. A dnode may consume 1+extra_slots slots; do not treat a continuation slot as a separate object. Bonus follows the active pointer array; a spill pointer, if present, occupies the end of the allocated dnode.

## Indirect object lookup

Let D be the dnode data block size and P=2^indblkshift/128 the pointers per indirect block. For a requested byte offset, logical block ID=floor(offset/D), in-block offset=offset mod D. With L levels, a top-level pointer spans P^(L-1) data blocks. Divide block ID by that span to select the top pointer; take successive quotient/remainder indices through lower levels. With 16 KiB indirect blocks, P=128. For two levels, block ID 130 selects top pointer 1 then index 2, if the dnode has enough pointers. Validate maxblkid, level, expected size and every child pointer. Avoid overflow on exponentiation.

## Dataset hierarchy and snapshots

The MOS includes dataset and directory objects and ZAP mappings. Resolve the pool directory to the root DSL directory; use child-directory maps to navigate named datasets. A dataset's physical bonus contains directory object 0, previous snapshot object 8, previous snapshot txg 16, next snapshot 24, snapshot-name map 32, creation time 48, creation txg 56, deadlist object 64, referenced/compressed/uncompressed byte totals 72/80/88, unique bytes 96, GUIDs 104/112, flags 120 and object-set root pointer 128. Dataset object IDs belong to the MOS; file objects belong to a dataset object set. Snapshots and clones share block ancestry, so a referenced-byte total is not necessarily unique allocated space.

## ZAP and filesystem namespace

The ZFS Attribute Processor maps names or integer keys to typed values. MicroZAP uses a compact fixed-entry representation; FatZAP uses a pointer table, hash-indexed leaves and variable key/value chunks. Select the correct block type and validate table growth, collision chains and array chunks. Use native hash and normalization rules. The filesystem master-node ZAP locates the root directory and other objects. Directory ZAP entries encode object identity and possibly type; resolve file attributes through legacy znode bonus or System Attributes (SA) layout registries and optional spill blocks. Do not assume every modern inode uses the historical znode byte layout.

## Allocation, metaslabs and space maps

Top-level vdevs divide their address space into metaslabs. Space-map objects encode allocation and free ranges using version-specific records; replay those records to reconstruct allocated space, accounting for condensed maps, log space maps and deferred frees. Allocation is constrained by ashift, topology and reservation policy. A file's block pointers alone do not enumerate all metadata, snapshots, pool-wide allocations or deduplicated references. Defragmentation cannot safely relocate a DVA without updating all owning references and preserving birth, checksum and allocation semantics.

## Transaction groups and recovery

ZFS batches changes in transaction groups. Sync writes new blocks and metadata before publishing a new uberblock referring to the coherent MOS. Old referenced blocks must not be freed prematurely. The ZFS Intent Log provides recovery for applicable synchronous operations not yet committed in the main txg; it is not a general redo log for every metadata write. Replay requires record-type-specific validation and safe import ownership. A checkpoint is a retained pool state, distinct from a dataset snapshot. Scrub checks and repairs through verified redundancy; it is not the same as arbitrary offline fsck reconstruction.

## Implementation and qualification boundary

Historical revision ee1683008e85 implemented native pool label and uberblock probing, with checksummed XDR configurations and 512-byte/4 KiB transfer support. It declared CORE probe and hosted VOLUME format; cancellation was documented. Dataset mounting, namespace traversal, file reads, arbitrary pool writes and vdev topology/recovery were not established by those declarations.

Common V2 is the independent provider interface: https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md . Negotiate real operation masks and media profiles. Use bounded host I/O, exclusive unmounted ownership and durable flush for mutation; distinguish unsupported, corrupt, I/O and stale-handle results. Test Media and Defragmenter use the same engine only for supported operations. Hosted, freestanding, BOOT, kernel, recovery and package qualification are independent. Current capability and release status belongs to the repository README and exact-source qualification records; historical evidence is not a current status claim.

Historical source reference: https://github.com/Infiltrator-Projects/Filesystem-ZFS/tree/ee1683008e85 .

## Sources and verification

Primary pinned source: https://github.com/openzfs/zfs/tree/zfs-2.2.7/include/sys (vdev_impl.h, uberblock_impl.h, spa.h, dnode.h, dmu_objset.h, dsl_dataset.h, zap_impl.h). Algorithms in https://github.com/openzfs/zfs/tree/zfs-2.2.7/module/zfs . Validate native pools with multiple txgs, different ashift, fragmented objects, holes, snapshots, corrupted redundant blocks and feature admission. Compare interpretation against zdb and native read-only import of disposable images. Full RAIDZ/dRAID, ZAP/SA variant layouts, allocator log formats, protected blocks and writer recovery remain separate research and test requirements. Historical evidence is identified in Implementation and qualification boundary.
