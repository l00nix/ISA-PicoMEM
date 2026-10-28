# Experimental same-parent USB file rename

I am developing this as l00nix, with AI coding assistance, on
`codex/dfs-long-name-rename`, after the hardware-tested creation milestone.
**This rename candidate has not yet been flashed or tested on the PPC512.**
The original metadata-only upstream PR remains separate and draft.

Enable `PICOMEM_EXPERIMENTAL_LFN_RENAME=ON` together with
`PICOMEM_EXPERIMENTAL_LFN_CREATE=ON` and `PICOMEM_EXPERIMENTAL_LFN=ON`.
All are off by default. Builds with rename disabled reproduce the earlier
stock, metadata-only and creation UF2 hashes exactly.

The scope is same-parent regular-file rename, including case-only changes,
with no replacement of another entry. Directories, SD writes, Unicode and
names over 26 bytes are excluded. There is no power-loss atomicity guarantee.

## Wire contract

The audited PicoMEM 2 ROM contains a separate exact 16-byte descriptor at
`0x3fe0`: `PMLR 01 72 1a 34 l00nix 00 00`. Existing PMLN/PMLW descriptors
remain unchanged and the ROM checksum is corrected. Clients must match the
whole descriptor before sending a capability query. Opcode `0x72` uses the
existing DFS envelope (outer PicoMEM command `0x89`), USB drive 20 only.

Request length is 13–166 bytes:

| Offset | Meaning |
| --- | --- |
| 0–3 | `PMLR` |
| 4 | Version 1 |
| 5 | Operation: 0 capabilities, 1 rename |
| 6 | Source alias path length, 0–127 |
| 7 | New basename length, 0–26 |
| 8 | Expected source attributes |
| 9–12 | Expected source size, little endian, at most `0x7fffffff` |
| 13 onward | Absolute backslash alias-only source path, then new basename, no terminators |

Capabilities require exactly 13 bytes and zero fields after the operation.
Rename requires a nonempty source and basename. All source components must be
valid 8.3 aliases; the new name follows the creation candidate's ASCII rules
(no leading dot/space, trailing dot/space, separators, controls or wildcards).
The parent must leave space for a returned 12-byte alias within the 127-byte
legacy path. Allowed source attributes are hidden, system and archive (`0x26`).

The response is exactly 52 bytes:

| Offset | Meaning |
| --- | --- |
| 0–5 | Magic, version and echoed operation |
| 6 | Status |
| 7 | Exact name length; capabilities return 26 |
| 8 | Attributes; capabilities return feature bit 0 (regular files only) |
| 9–12 | Size, little endian |
| 13–25 | NUL-terminated, zero-padded 8.3 alias |
| 26–51 | Exact requested name, zero-padded if shorter than 26 bytes |

Capabilities zero all bytes after feature flags. Errors zero all bytes after
status. Status values: 0 success, 1 unsupported/bounded scan, 2 invalid request,
3 missing source, 6 I/O or ambiguous completion, 7 destination exists, 8 denied.
The host treats unrecognized statuses as faults.

## Implementation and checks

A separate adapter owns its DIR cursor and bounded static path workspace. It
requires a complete parent scan within 256 reads, checks the expected source
alias/size/attributes and refuses any other entry with the destination name or
alias. It invokes `f_rename` once. A second complete scan verifies the exact
name, valid alias, unchanged size and expected archive attribute; it also
checks old-alias disappearance if the alias changed. Filesystem exit paths
restore PSRAM. The legacy open/read/write path remains alias-based.

Any `f_rename` error or failed verification after mutation is ambiguous. No
retry or destructive rollback is attempted. Host-side validation must not
interpret this as POSIX overwrite/atomicity support. Unmount before removal.

Sanitizer protocol/adapter tests cover invalid framing/names/bounds, capability
queries, collisions without mutation, stale metadata, scan/read/close failures,
old-alias disappearance and cleanup. Disposable FAT12 and FAT32 tests compile
the actual bundled FatFs code and exercise rename, case-only rename, refusals,
remount and exact payload reads. Independent mtools lookup verifies the new
long filename. Run:

```sh
sh src/lib/my_etherdfs/lfn/test-rename.sh
python3 src/lib/my_etherdfs/lfn/test-rename-fatfs.py .
python3 src/lib/my_etherdfs/lfn/test-rename-fatfs.py . --fat32
python3 tools/test-lfn-rom.py
```

The paired [ELKS candidate](https://github.com/l00nix/picomem-elks/tree/codex/long-name-rename)
adds `pmrename` and a native fixture probe. Hardware rename, independent Mac
FAT checks, DOS regression testing and runtime resource measurements remain
before any release-candidate claim.
