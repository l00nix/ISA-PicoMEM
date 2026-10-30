# Read-only USB cache-capability candidate

I am preparing a hardware diagnostic for the cache-flush blocker identified in
[the command audit](USB-COMMAND-AUDIT.md). **This candidate blocks all USB sector
writes and sends no SYNCHRONIZE CACHE commands. It is not a read/write release.**
It is built and host-tested. The first physical test is recorded below; it did
not establish the drive's cache policy.

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
  stack bytes and core 1 retains 2048, without scratch overlap. Runtime stack peak remains unmeasured; the first physical probe result is below.

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


## First physical probe: generic drive through powered hub

I flashed the candidate after saving and readback-verifying a fresh full 16 MiB
recovery image. The new firmware also passed readback verification. On the PPC512,
I confirmed the generic drive `0011:7788` selected at address 1, port 3 of hub
`1a40:0101` (address 5). A read-only mount succeeded. All nine non-hidden files
(913,703 bytes) matched their prior Mac SHA256 baseline, and `PMCP1030` was absent.

After remounting for the diagnostic trigger, I made exactly one
`mkdir /mnt/usb/PMCP1030` attempt. It failed as expected, but the capability query
timed out rather than returning a usable response:

```text
USB cache probe: dev=1 WCE=unknown I/O=quarantined
MODE10 time actual=255 used=0 status=255 residue=255
MODE6 - actual=255 used=0 status=255 residue=255
Diagnostic only: USB writes blocked; no flush command sent.
```

MODE SENSE(6) was not attempted. The `255` values mark unavailable metadata; they
are not device-returned status values. I made no retry and successfully unmounted
USB. No post-fault media reads were attempted. Repeated cached status reads kept
the same result. ELKS remains running; PicoMEM must restart before further USB I/O.

This establishes a timeout on a read-only MODE SENSE command independently of the
flush command. It does not identify the drive, hub, controller or transport as the
cause, and it does not establish whether the drive has a write cache. The verified
diagnostic build blocks sector writes and issues no flush; this is software
evidence, not a physical USB bus trace. The next controlled comparison is this
same firmware and drive connected directly to PicoMEM after a fresh restart.

[Hardware record](USB-CACHE-PROBE-HARDWARE.json) ·
[Actual PPC screen capture](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-usb-cache-probe-20261030.png).


## Direct-connection comparison

After a user-confirmed physical power cycle, I repeated the test with the same
firmware and generic drive connected directly to PicoMEM. Inventory confirmed
`0011:7788` selected at address 1 on root port 0 and **zero hubs**. The read-only
mount succeeded, all nine files (913,703 bytes) again matched the Mac baseline,
and neither the previous hub-test directory nor the new `PMCD1030` directory existed.

Exactly one `mkdir /mnt/usb/PMCD1030` attempt reproduced the same result:
MODE SENSE(10) timed out, WCE remained unknown, USB I/O was quarantined, and
MODE SENSE(6) was not attempted. USB unmounted successfully. I made no retry and
no post-fault media reads; ELKS remains running.

The hub is therefore not required to reproduce this timeout. This does not yet
distinguish drive behavior from the host-controller or transport implementation.
The current probe report does not reveal which transfer stage stalled. Source
review shows that `collect_cache()` records `time` without collecting the pending
transfer snapshot used by the main command timeout path. The next diagnostic
should preserve that snapshot for the cache query and expose the command, data
or status stage, with controller details, without sending another USB command or
changing the write policy. This reporting gap is not itself evidence of the cause.

[Direct hardware record](USB-CACHE-PROBE-DIRECT.json) ·
[Actual PPC screen capture](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-usb-cache-direct-20261030.png).


The [cache-query transfer-stage candidate](USB-CACHE-STAGE.md) now implements this
snapshot reporting and has passed host tests and ARM builds. It has not yet been
flashed; the hardware results above remain from the earlier firmware.
