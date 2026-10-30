# Bounded late status observation

I built this follow-up to the [SanDisk controller snapshot](USB-HOST-STATE.md#ppc512-controller-result--30-october-2026).
**Host tests and builds pass; hardware testing is pending. Read/write release remains blocked.**

## Behavior

The normal command still fails after two seconds and quarantines USB. Only an
initial, clean, no-data SYNCHRONIZE CACHE(10) timeout arms this observer. For 30
additional seconds, it records whether the outstanding transfer delivers a late
completion callback. It uses the existing USB task loop and does not block the
file-service caller, submit another command, retry, reset or clear the fault.

A candidate late CSW must match the original device, tag, command wrapper and
callback pointers, and have a matching checked terminal record with a complete
13-byte status IN, valid signature/tag, zero residue and status 0 or 1. Malformed
or mismatched callbacks are recorded as invalid evidence. There is no REQUEST
SENSE or cache query after a late reply. Even a validated status-0 reply cannot
enable writes. Read/write requests and new device attachment remain rejected.

The original six timeout/controller lines stay frozen. Only the existing device
fault line gains an observation suffix, preserving its original fault fields:

```text
?USB fault op=35 f=6 s=255 r=0 d=0 obs=wait os=255 why=0 ms=0
```

| Field/value | Meaning |
| --- | --- |
| `obs=wait` | Additional observation window is active |
| `obs=csw`, `os=0/1` | Matching checked late CSW, success/command-failed status; original operation still failed |
| `obs=bad` | Callback or terminal record failed validation |
| `obs=detach` | Device disappeared during the observation window |
| `obs=none` | No accepted observation before the window expired |
| `os=255` | Sentinel: no validated late CSW status |
| `why` | Terminal-record rejection reason when available; 0 alone does not prove validity |
| `ms` | Elapsed milliseconds since observation started after the original timeout |

`obs=none` also wins when a callback or detach is first processed at/after the
30-second boundary. Callback timing measures software delivery, not physical bus
arrival. If the main task is delayed, the final check can occur after 30 seconds;
its elapsed value is retained. Timers handle 32-bit wraparound. Late duplicates
and callbacks after the observation ends cannot change the final result.

This bounds diagnostic observation, not the lifetime of the outstanding hardware
transfer. No cancellation is implemented; owned buffers remain quarantined until
MCU restart, and existing USB service continues as before. The six frozen lines
show the original two-second state, not a later controller snapshot. The existing
ELKS kernel and `pmstat --usb` display the new suffix without an update.

## Validation

- 43 dedicated ASan/UBSan observer cases: late status 0/1, invalid callback and
  terminal fields, missing record, exact window boundaries, disconnects, timer
  wraparound, duplicate events, caller-buffer release, immutable timeout evidence,
  zero additional USB commands and persistent quarantine after every outcome.
- Existing 19,268 actual TinyUSB cases, 191 adapter scenarios, 1,320 native-getter
  mock-register cases, five real FatFs/FAT32 cases with independent readback and
  14 legacy USB cases pass.
- ARM build and stack map pass: core 0 reserves 4096 bytes, core 1 reserves 2048,
  with no overlap. Runtime fault-path stack peak remains unmeasured.
- Flash 4,812,568 bytes; RAM 382,088 bytes. Feature-disabled UF2 remains identical
  to the established baseline. [Source and artifact manifest](USB-LATE-STATUS.json).

## Hardware plan

After a fresh verified full backup and flash, I will boot ELKS, verify the three
SanDisk file baselines read-only, confirm a fresh name is absent, and make one
guarded mkdir attempt after read/write remount. The usual two-second error remains.
I will query cached status during/after the additional observation window, save
the final result and a screenshot, then unmount. There will be no write retry,
reset or media change during observation. The upstream metadata draft PR is unchanged.
