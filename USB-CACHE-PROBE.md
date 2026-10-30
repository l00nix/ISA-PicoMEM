# Read-only USB cache-capability candidate

I am preparing a hardware diagnostic for the cache-flush blocker identified in
[the command audit](USB-COMMAND-AUDIT.md). **This candidate blocks all USB sector
writes and sends no SYNCHRONIZE CACHE commands. It is not a read/write release.**
It is built and host-tested; physical validation is pending.

## Trigger and results

The option `PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE` defaults OFF and requires
`PICOMEM_EXPERIMENTAL_USB_SYNC` plus its existing prerequisites. With it enabled,
the first valid attempted USB sector write per clean attachment performs a
read-only MODE SENSE(10) query for the current caching page. A fully validated
completion with unknown cache state permits at most one MODE SENSE(6) alternative.
Each query retains the existing two-second deadline and separately owned buffers.
No MODE SELECT, reset, flush, sector write or automatic retry is sent.

The attempted write always fails. Results mean:

| Result | Meaning | USB writes in this candidate |
| --- | --- | --- |
| `WCE=disabled` | A complete, validated caching page reports WCE=0 | Blocked |
| `WCE=enabled` | A complete, validated caching page reports WCE=1 | Blocked |
| `WCE=unknown` | No usable caching page was established | Blocked |

A completed response with absent/malformed page content leaves cache policy
unknown. Ambiguous transport, timeout, rejected submission or disconnect during
the query quarantines USB I/O until PicoMEM restarts. A late callback cannot
change the saved report or reopen I/O. A later read failure also changes the
cached report's I/O state to `quarantined`.

The probe does not run during enumeration, read-only mounting, or status queries.
`CTRL_SYNC` is rejected without sending a command in this diagnostic build. Repeated
write attempts do not rerun the probe. Clean detach/reattach clears its evidence
and permits a new probe; a failed session cannot be cleared by reattachment.

## Existing ELKS interface

No new kernel or ELKS utility is required. Before probing, `pmstat --usb` retains
the existing inventory. Afterward, CMD 61 returns the cached probe report instead:

```text
USB cache probe: dev=1 WCE=disabled I/O=read-only
MODE10 off actual=28 used=28 status=0 residue=36
M10 +00 001a0000000000000812000000000000000000000000000000000000
MODE6 - actual=255 used=0 status=255 residue=255
Diagnostic only: USB writes blocked; no flush command sent.
```

This is an illustrative valid response, not a hardware capture. `actual` is the
validated DATA byte count; `used` is the smaller of that count and allocation
minus residue. Hex rows contain only those usable bytes, copied after a validated
completion. There are at most 64 bytes per variant. `255` marks unavailable
metadata, not a validated status/residue. `-` means the variant was not attempted.
Other result labels distinguish header/page errors, status failure and transport
failure. The command-status format remains at most eight counted NUL-terminated
rows, each below 80 bytes including NUL; an undersized buffer is left unchanged.
After the probe, the report takes precedence over inventory so both bounded
responses fit. Status reads perform no USB transfer.

## Validation

- 64 new ASan/UBSan scenarios run the actual adapter and patched TinyUSB MSC
  code together. They cover enabled/disabled/unknown results, both variants,
  short and header-only replies, malformed pages and descriptors, failed status,
  transport faults, timeout, detach, repeated attempts, reattachment and later
  read failure. Every output-buffer capacity through 1024 bytes is checked.
- Four real FAT32 scenarios perform a failed directory creation through the real
  adapter and TinyUSB code. The endpoint model permits only READ and MODE SENSE;
  no WRITE or flush is accepted. Every complete FAT image has the same SHA256
  before and after, including timeout and unknown-policy cases.
- Existing suites pass: 72 joined transport cases, 19,268 TinyUSB cases, 191 adapter
  scenarios, 43 late-observation cases, five FatFs cases, 1,320 native snapshot
  models, 10,845 inventory scenarios and 30 ELKS status-reader scenarios.
- ARM builds with the probe ON and OFF pass. The OFF BIN differs from the installed
  inventory baseline only in five bytes of the RTC build-time string.
- Candidate uses 4,814,120 FLASH bytes and 382,904 RAM bytes. Core 0 retains 4096
  stack bytes and core 1 retains 2048, without scratch overlap. Runtime stack peak
  and physical USB behavior are not yet measured.

[Candidate hashes and validation manifest](USB-CACHE-PROBE.json).

## Planned PPC test

After readiness confirmation, I will shut down ELKS and make a fresh full recovery
backup with readback verification before flashing. The first test uses the generic
drive alone through the powered hub in a fresh PicoMEM session. I will verify its
identity and read-only contents, confirm that `PMCP1030` does not exist, and make
one mkdir attempt through the USB write mount. Failure is expected: the purpose
is the read-only capability report. I will capture that report, unmount USB, and
compare the drive's contents as appropriate. A transport fault stops all further
I/O until restart. Other drives are separate comparisons, not simultaneous mounts.

A validated WCE=0 result could inform a future write-through implementation. This
candidate implements no such policy, and unknown cache behavior never enables
writes. Reported cache settings alone do not prove power-loss durability.
