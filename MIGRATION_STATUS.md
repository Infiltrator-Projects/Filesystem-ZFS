# ZFS migration, 2026-10-30
The native ZFS analyser, MOS/space-map engine and worker are source-owned here
in native/ and compiled into a loadable ZFS.so. The original Defragmenter
regression suite is compiled directly against this module. The application
loads the versioned interface from its trusted module host; it no longer
compiles the ZFS engine locally. Exact analysis remains read-only and limited
to independently verified ZFS configurations; no defrag or write support is
advertised. The exact ZFS Test Media formatter and independent verifier are now also compiled
into ZFS.so. The calling application supplies generic corpus planning, I/O and
verification callbacks, while the module owns every ZFS-specific record. The
consumer's real end-to-end fixture and map tests are required before qualifying
this migration. No external mkfs tools are called.


## Missing production capability (2026-11-07)

The physical-disk Defragmenter 1.8.0-288 qualification exposed an actual
incomplete capability: ZFS passed native fixture creation and read-only
analysis, but Test Media deliberately emitted `qualification-skipped`.
This does not qualify ZFS for Defragment or Growth Defrag.

The write-capable implementation belongs **here in ZFS.so**, not as an
independent filesystem parser/writer inside Defragmenter. The next writer
milestone needs an in-process ZFS transaction-group mutation contract with
copy-on-write block-pointer/checksum updates, reference/snapshot/clone policy,
verified durable recovery and clear Stop-safe boundaries. Start with a
strictly bounded, independently verified single-disk pool subset; reject
unqualified feature flags and sharing rather than modifying them unsafely.
Require real post-operation fragmentation analysis plus retained-byte
verification before advertising a write capability.

Rewriting ZFS blocks and guaranteeing Defragmenter's canonical physical
packing are **not interchangeable**: a ZFS allocator may place rewritten
blocks non-contiguously, and the exact 10% adjacent post-file reserve needs
a separate, provable contract. Do not simply mark `IFS_ZFS_CAP_RUN` as
Defragment support or change the GUI to report production qualification
without a native writer and independent fault-injection tests.

## Generic Defragmenter integration target (2026-11-07)

Defragmenter now owns the new generic, **read-only**
`ld_generic_layout_plan` placement policy. ZFS.so should **not** gain its
own competing file-packing/Growth Defrag scheduler.

The missing ZFS translation contract is:

1. Export an exact per-file/object inventory, ordered with stable identifiers,
   allocation units, extents, block-pointer provenance and every relevant
   read-only/clone/snapshot/sharing constraint. The existing native analysis
   supplies pool-wide counters and ranges but not this complete relocatable
   per-object catalogue.
2. Let Defragmenter generate the generic packed or 10%-gap plan. ZFS.so must
   validate that plan against the actual pool allocator and fail closed if its
   constraints cannot be met. A generic plan is **not** permission to write.
3. Supply an in-process, durable ZFS-specific relocation/transaction adapter
   that stages the replacement blocks and changed parent checksums, publishes
   a valid transaction group atomically, supports interruption/recovery and
   independently verifies actual physical placement.
4. Qualify on deliberately fragmented disposable pools with fault injection,
   snapshots and shared-block rejection before advertising any Defrag or
   Growth Defrag capability. Retained-byte checks alone do not count.

Current code is at **step 0 (read-only analysis and fixture creation)**.
The Defragmenter planner is committed but **not wired to any production SO**;
there is no working ZFS writer and no new ZFS capability flag in this commit.
