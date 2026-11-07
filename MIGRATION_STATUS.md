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
