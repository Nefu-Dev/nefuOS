# NVFS Journaling Plan

Status: **layout fixed, journal repaired, tests green (75 checks, 0 failures)**.
Audience: nefuOS maintainers. Language: English (project convention).

## 1. On-disk layout (authoritative)

Single-partition image, 512-byte blocks, default 4 MiB. All constants are
defined once in `core/vfs/nvfs.h` (`NVFS_*`) and referenced by `nvfs.cpp`.

| Region | Blocks | Size | Contents |
|---|---|---|---|
| Superblock | 0 | 512 B | `NVFS_MAGIC`, version, geometry, `reserved[440]` |
| Inode table | 1 .. 222 | 222 blocks | 1024 inodes x 111 bytes (packed) |
| Block bitmap | 223 .. 224 | 2 blocks | 8192 bits |
| Journal | 225 .. 352 | 128 blocks = 64 KiB | redo log |
| User data | 353 .. 8191 | 7839 blocks | files and directories |

`sizeof(Superblock) == 512` is asserted by the test suite; the previous
`reserved[468]` made the struct 540 bytes and pushed the documented layout
out of sync — this is the "reserved must be 440" finding from the NVFS audit.

## 2. Journal record format

Header is `sizeof(JournalEntry)` (17 bytes, packed: `seq u32 | type u8 |
block u32 | size u32 | checksum u32`) padded to `NVFS_JREC_HEADER = 24`
bytes. A `JOURNAL_WRITE` record carries up to 512 bytes of payload
(`NVFS_JREC_MAX = 24 + 512 = 536`).

A transaction is written as:

```
JOURNAL_START (seq = N)
JOURNAL_WRITE (block, payload, checksum)   // zero or more
JOURNAL_COMMIT (seq = N)
```

The checksum is FNV-1a over the payload. `seq` is the transaction sequence
number; **`seq == 0` is the "clean region" sentinel** — the replay loop stops
when it sees `seq == 0`, so a transaction must never journal with `seq == 0`.

## 3. Latent bugs fixed in this pass

Three real defects were found while landing the plan:

1. **Superblock journal pointer was wrong.** `format_image()` wrote
   `sb.journal_start = JOURNAL_START;` which resolved to the *enum value 1*
   (record type), not the layout constant 225. The runtime never noticed
   because `jptr()` used the compile-time constant, but any external tool
   reading the superblock (fsck, host-side mount) would have looked at block 1.
   Fixed: `sb.journal_start = JRN_START_BLOCK;`.

2. **Recovery never actually replayed anything.** `journal_start()` never
   incremented `s_journal_seq`, so every record carried `seq == 0` — which the
   replay loop treats as "clean region" and stops at immediately. Crash
   recovery was therefore a no-op (and worse: it then cleared the journal).
   Fixed: `s_journal_seq++` at the start of each top-level transaction.

3. **Mid-transaction checkpoint rolled the live image back.** The image buffer
   is both live state and "disk"; writes are applied immediately and snapshots
   are journaled for redo. When a large write (e.g. 96 KiB) filled the journal
   mid-transaction, `replay_and_clear()` copied *old snapshots* back over
   blocks the current transaction had already updated — corrupting the block
   bitmap and indirect tables (observed: `tbl[108]` reset to 0, file read back
   truncated). Fixed by tracking blocks written since the last replay
   (`s_txn_blocks` bitmap): replay skips those blocks (their image content is
   already newer) and clears the tracking set afterwards. `recover()` — the
   crash path — clears the tracking set first, because after a crash the
   journal is the authoritative copy and every checksum-valid record must be
   applied.

## 4. Test coverage (tests/nvfs_test.cpp)

- `[1a]` superblock size == 512, inode size == 111, all layout constants,
  superblock geometry on disk.
- `[1b]/[2b]` journal region contains `START -> WRITE* -> COMMIT` in order
  after a transaction.
- `[2c]` replay is idempotent (a second replay is a no-op).
- `[4]` crash recovery restores a deliberately corrupted block from the
  journal.
- `[9]` 96 KiB (192-block) write exceeds journal capacity, forces automatic
  rollover mid-transaction, and the file still reads back byte-identical —
  this is the test that caught defect 3.

Run: `tests/build_nvfs_test.ps1` (headless, no kernel).

## 5. Remaining roadmap

1. **fsck tool** — a host-side checker validating superblock geometry, bitmap
   vs inode consistency, and directory entry integrity; reads the same
   `NVFS_*` constants.
2. **Torn-write hardening** — today a checksum mismatch stops replay and the
   remaining records are discarded; add a `JOURNAL_COMMIT`-counted
   two-phase-clear so an interrupted checkpoint cannot lose the tail of a
   committed transaction.
3. **Format tool** — a host utility to build/extend NVFS images with the
   documented layout (currently `format_image()` is in-kernel only).
4. **Journal sizing** — 128 blocks is comfortable for normal operations;
   re-evaluate if `write_file` is ever called with multi-megabyte buffers
   (the current fix already makes oversized transactions safe, but at the cost
   of replaying in place).
5. **Mount-time self-check** — surface journal replay statistics (records
   replayed, bytes recovered, torn records discarded) to the `fsinfo`
   interface so operators can verify recovery actually ran.

## 6. Acceptance

The plan is accepted when: (a) all `NVFS_*` constants live in `nvfs.h`;
(b) `sizeof(Superblock) == 512`; (c) the three defects above are fixed with
regression tests; (d) `build_nvfs_test.ps1` reports 0 failures.
