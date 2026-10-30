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
