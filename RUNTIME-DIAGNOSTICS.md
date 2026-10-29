# Optional file-service timing and stack diagnostics

I am preparing a default-off diagnostic build for PicoMEM 2 to measure the
existing long-name workload. Hardware measurements are pending. The upstream
metadata-only draft PR remains unchanged.

Enable `PICOMEM_EXPERIMENTAL_DIAG=ON` alongside the existing LFN, creation,
rename and `PICOMEM_EXPERIMENTAL_STACK_RESERVE=ON` options. Other boards are
rejected by the stack-reservation gate. The kernel/utility companion is on
[`picomem-elks`'s diagnostics branch](https://github.com/l00nix/picomem-elks/blob/codex/runtime-diagnostics/docs/RUNTIME-DIAGNOSTICS.md).

## Measurement

The core-0 timer surrounds `ethdfs_docmd` and PSRAM restoration inside
`CMD_EthDFS_Send`. It records count, min/max and summed microseconds for each
DFS command and an aggregate. Command `0x73` is excluded. LED operations,
command queueing and PC transport are outside the interval. The unsigned
32-bit microsecond clock handles a wrap between start/end for commands that
complete in less than about 71.6 minutes. Counts/totals saturate with an
explicit flag rather than silently wrapping.

Stack painting uses actual SP under masked interrupts, leaves a 64-byte gap
below the live frame, and touches only the reserved core-0 bank. Querying
scans under masked interrupts. It includes instrumentation and core-0
interrupt stack use since reset; it does not measure core 1. A watermark is
an observed workload peak, not a worst-case bound or overflow prevention.

## Independent experimental protocol

The ROM descriptor at offset `0x3fb0` is exactly 16 bytes:
`50 4d 44 47 01 73 08 34 6c 30 30 6e 69 78 00 00`.
The earlier metadata/create/rename descriptors are unchanged. The ROM stamper
checks the original ROM hash, unused padding and checksum. The descriptor
identifies a private experimental protocol; it is not an upstream allocation.

DFS function `0x73` accepts only USB drive 20, protocol version 1 and a total
request length of 68 bytes. The payload at offset 60 is eight bytes:

| Offset | Field |
| --- | --- |
| 0–3 | `PMDG` |
| 4 | Version 1 |
| 5 | Operation: 0 query, 1 reset |
| 6 | Command selector 0–127, or 255 aggregate |
| 7 | Reserved zero |

Reset requires selector 255 and clears all counters/repaints the stack. It
does not open or modify files. Invalid requests return AX 13 without reset.
Unsupported command numbers above 127 are counted in bucket 127.

Successful replies have total length 112 and AX 0. Their 52-byte payload is:

| Offset | Field (multibyte values little-endian) |
| --- | --- |
| 0–6 | Magic, version, operation and selector echoed |
| 7 | Bit 0: counters saturated; bit 1: stack painting invalid |
| 8–11 | Count |
| 12–15 | Minimum microseconds (zero if no samples) |
| 16–19 | Maximum microseconds |
| 20–23 | Total microseconds |
| 24–25 | Observed core-0 stack bytes used |
| 26–27 | Reserved core-0 stack bytes: 4096 |
| 28–51 | Reserved zero |

The companion ELKS ioctl validates the wire reply before adding its own
kernel metrics to part of the reserved area. Those additions are not sent
by firmware. Query/reset must use the normal serialized PicoMEM command
path; the collector is not a multi-core or nested-dispatch API.

## Checks

```sh
sh src/lib/my_etherdfs/diag/test.sh
sh src/lib/my_etherdfs/lfn/test.sh
sh src/lib/my_etherdfs/lfn/test-create.sh
sh src/lib/my_etherdfs/lfn/test-rename.sh
python3 tools/test-lfn-rom.py
python3 tools/check-stack-reserve.py build-arm-diag/picomem.elf.map
```

With diagnostics disabled, the stack-reservation build reproduces the tested
UF2 SHA-256 `4e144a3ed4f1a40db9da79043fe4660fe9e0c4f8a71253f962622a2e46efef13`.
The diagnostic candidate requires a new verified recovery backup and physical
flash coordination before hardware tests. This is not a release candidate.
