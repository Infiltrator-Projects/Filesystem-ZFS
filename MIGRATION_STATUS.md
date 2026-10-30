# ZFS migration, 2026-10-30
The native ZFS analyser, MOS/space-map engine and worker are source-owned here
in native/ and compiled into a loadable ZFS.so. The original Defragmenter
regression suite is compiled directly against this module. The application
loads the versioned interface from its trusted module host; it no longer
compiles the ZFS engine locally. Exact analysis remains read-only and limited
to independently verified ZFS configurations; no defrag or write support is
advertised. **Test Media ZFS fixture-code extraction is a separate migration
step and must not be counted as completed by this commit.**
