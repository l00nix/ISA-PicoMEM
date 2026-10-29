# USB long-name creation candidate

I am developing this next experiment as l00nix, with AI coding assistance.
It is separate from the metadata-only draft PR. This candidate adds exclusive
empty-file creation and mkdir, not rename or SD writes. It has now been flashed
and tested on the PPC512 with the paired ELKS kernel.
Long-name creation, writes, deletion, remount and normal reboot tests passed.
Direct Mac content reads and a FAT consistency check also passed. Long-name and
8.3 alias readbacks also passed after returning the stick to PicoMEM. MS-DOS 6.22
alias reads and a temporary legacy create/append/truncate/delete test passed.

## Wire contract

Both `PICOMEM_EXPERIMENTAL_LFN=ON` and the new, default-off
`PICOMEM_EXPERIMENTAL_LFN_CREATE=ON` are required. The metadata descriptor at
ROM offset 0x3fc0 and DFS command 0x70 remain unchanged. A second exact descriptor
at audited FF padding 0x3fd0 is `PMLW 01 71 1a 34 l00nix 00 00` (16 bytes).
The checksum is recomputed from the same audited stock ROM. Command allocation
and discovery remain provisional and need maintainer agreement.

The separate DFS subfunction 0x71 (within outer command 0x89) uses USB drive 20
only. SD drive 18 is denied before filesystem access. Requests are at most
161 bytes, replies exactly 52 bytes. All lengths below exclude the 60-byte DFS
header. Invalid framing never reaches the filesystem.

| Request offset | Meaning |
| --- | --- |
| 0–3 | ASCII `PMLW` |
| 4 | Version 1 |
| 5 | 0 capabilities, 1 exclusive empty file, 2 mkdir |
| 6 | Parent path byte length |
| 7 | New basename byte length |
| 8 onward | Absolute short-alias parent, then new ASCII basename; no terminators |

Capability requests have both lengths zero. Other requests require a root `\`
or validated alias-only parent and 1–26 ASCII basename bytes. Leading dot/space,
trailing dot/space, traversal, separators, control bytes, wildcards and non-ASCII
names are refused. The parent must leave room for the maximum 12-byte alias
within the client's 127-byte path limit. This is intentionally a narrower
creation policy than FAT's complete filename space.

Replies contain `PMLW`, version, echoed operation, status at byte 6 and name
length at byte 7. A successful capability reply has 26 at byte 7 and feature
bits 3 at byte 8 (file + mkdir); all later bytes are zero. A successful creation
reply has FAT attributes at byte 8, zero size at bytes 9–12, a zero-padded
13-byte alias at byte 13, and the exact new name at byte 26. Error replies have
zero bytes from offset 7 onward.

Statuses are 0 success, 1 unsupported/bounded scan refusal, 2 invalid request,
3 missing parent, 6 I/O or ambiguous completion, 7 exists and 8 denied.
Reserved statuses 4/5 are not emitted by this protocol. The ELKS client latches
I/O, malformed replies or unknown status as faults and does not retry mutations.

## Filesystem behaviour

An independent cursor scans the parent to completion, bounded to 256 reads,
before any mutation. Existing long names and aliases are checked case-insensitively.
A bound hit or unsupported alias refuses creation. `FA_CREATE_NEW` provides an
additional no-overwrite check. The file is synced and closed; mkdir's actual
FatFs return is checked. A second independent scan resolves the new alias and
verifies the exact name, type and zero size before success is returned. The
legacy DFS directory cursor is never used. Failures after mutation are not
retried or rolled back, since the object may already exist.

The sector-buffered `FIL` lives in a guarded static workspace because the firmware
has a 2 KiB stack. The create adapter's compiled local frame is 304 bytes rather
than 912 bytes. This reduces stack demand; it is not a measured whole-call-chain
high-water mark. Runtime stack and IRQ-off latency remain hardware checks.
PSRAM access is restored on all filesystem exit paths.

`f_sync`/`f_close` checks are not a USB power-loss durability guarantee. This
candidate remains for disposable, backed-up test media.

## Reproduce validation

```sh
sh src/lib/my_etherdfs/lfn/test.sh
sh src/lib/my_etherdfs/lfn/test-create.sh
python3 src/lib/my_etherdfs/lfn/test-create-fatfs.py .
python3 src/lib/my_etherdfs/lfn/test-create-fatfs.py . --fat32
python3 tools/test-lfn-rom.py
# Add to the SDK/toolchain CMake arguments documented in LFN-EXPERIMENT.md:
# -DPICOMEM_EXPERIMENTAL_LFN=ON -DPICOMEM_EXPERIMENTAL_LFN_CREATE=ON
```

Host sanitizer checks cover malformed requests, SD denial, exclusive creation,
scan limits, filesystem errors, failed sync/close and PSRAM cleanup. Tests with
the actual bundled FatFs code create long files/directories in FAT12/FAT32
images, check alias-only writes, collisions without overwriting, a 26-byte name,
independent directory cursors and remount persistence. Independent mtools reads
then verify the full names and payload. Default, metadata-only and creation
firmware variants are built separately.


## Hardware result

The candidate UF2 with SHA-256
`e8fd1de4f89bec53920a3d9491e0513486d202973f01ecf3d7f5615eef3d80a8`
passed device readback verification after a verified full-flash recovery backup.
The PPC512 booted the paired ELKS kernel and exposed both feature descriptors.
Long-name files and directories, nested paths and a 26-byte basename were created
on the Kingston USB drive. Append, overwrite, truncate, deletion and refusal
checks passed. Three retained payloads (3,073, 1,025 and 73 bytes) matched independent
host-generated bytes through FTP downloads immediately, after remount and after
a clean reboot. SD remained read-only.

[ELKS hardware evidence and screenshot](https://github.com/l00nix/picomem-elks/blob/codex/long-name-create/docs/LONG-NAME-CREATION.md#ppc512-results-28-october-2026)
describe the test scope. After clean unmount and transfer to the Mac, native
filesystem reads matched every expected byte and `fsck_msdos -n` returned 0.
After returning the stick to PicoMEM, all three long paths and their short
aliases returned identical expected bytes in ELKS. DOS compatibility of this
particular build has now passed a bounded smoke test: all three fixture aliases
returned their expected complete contents, a HDD root directory read succeeded,
and a fresh temporary 8.3 file passed create, append, truncate/rewrite and delete
checks. The retained fixtures were unchanged. Runtime stack/latency measurements
and power-loss behavior remain unverified. This does not make the candidate a release candidate
or change the separate metadata-only draft PR.

The next, separate [rename candidate](LFN-RENAME-EXPERIMENT.md) builds on this
creation work. Rename has passed PPC512 file tests; its default-off flag
and descriptor are independent of the creation command.
