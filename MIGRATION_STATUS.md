# ZFS/OpenZFS migration — 2026-10-30

Existing Defragmenter ZFS member analyser/worker source copied without modification from Defragmenter commit 30af892d8897d3aeaeefeac1bfcdb9620a0a5c72 to `migration/defragmenter/native/`. Canonical design copied from Filesystems commit ca2fa1210c3c1f2b6f52cd15b3da6dfa83d453ad.

The existing implementation is **read-only, bounded ZFS member analysis**; no ZFS defragmentation, pool mutation, resilient ZFS filesystem .so, user file namespace, qualified BOOT or Linux kernel bridge exists here. It must not be advertised as read/write or full ZFS filesystem support. Existing Defragmenter engine remains active until a tested standalone module is ready.
