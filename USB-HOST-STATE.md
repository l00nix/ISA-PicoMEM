# Native USB controller state diagnostic

I built this follow-up to the [SanDisk pending status reply](USB-TIMEOUT-STAGE.md#ppc512-result--30-october-2026).
**Builds and host tests pass. The PPC512 snapshot below captured the pending receive. Read/write release remains blocked.**

## Why this diagnostic

The previous PPC512 run reported a successful 31-byte command-wrapper completion,
then an accepted 13-byte status IN transfer with no completion callback by the
existing two-second deadline. The firmware uses the RP2350 native USB controller
through TinyUSB's `hcd_rp2040.c`, with bulk transfers allocated to the controller's
asynchronous endpoint slots.

The [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
sections 12.7.3.7.4 and 12.7.3.9.4, describes the buffer-control flags and the polling
conditions. In particular, buffer bit 11 records a host STALL, and a polled IN
endpoint needs an enabled slot and an available, non-full buffer. I will inspect
these conditions before choosing any recovery or timeout change.

## What changes

A bounded native-controller getter reads software transfer state and the
associated endpoint's control/buffer registers. It also reads the controller's
polling enables, SIE state, buffer completion bits, masked interrupts, interrupt
enables, frame number and RP2350 internal state register. It never reads payload
buffers or writes USB registers, clears status, submits traffic, resets or retries.
It briefly saves/disables/restores local interrupts to stabilize software state;
hardware continues operating, so the register set is not an atomic bus snapshot.

The frame number is sampled after host-driver submission and at timeout. The
pending command's existing device/tag validation is retained. The adapter freezes
the report at timeout; later events cannot rewrite the cached text. The native
getter rejects absent/unconfigured endpoints, endpoint zero, invalid addresses
and invalid slot indices. There is no change to write policy or the deadline.

The existing three timeout lines remain. Three optional `HCD` lines are appended
as a group when the eight-line/1024-byte client limit permits. With one USB disk,
all six diagnostic lines fit. With additional devices or a smaller buffer, the
three base lines remain and the controller group is omitted. Each line is bounded
to 79 characters. The existing ELKS kernel and `pmstat --usb` are sufficient.

| Field | Meaning |
| --- | --- |
| `slot`, `act`, `rem`, `got` | Controller slot (1–15), software active flag, remaining and transferred bytes |
| `sof=start/end`, `v` | Frame numbers after submission and at timeout; `v` confirms the start sample exists |
| `bc`, `ec` | Endpoint buffer and endpoint control registers |
| `poll`, `addr`, `sc` | Polling enable mask, slot address/direction, SIE control |
| `ss`, `bs` | SIE status and buffer-completion status |
| `irq`, `ie`, `sm` | Masked interrupt status, interrupt enables, RP2350 internal state |

Controller-wide status bits can refer to other traffic or an earlier event. A
NAK bit alone does not prove that this endpoint is currently replying NAK. Frame
numbers wrap; equal samples alone do not prove a stopped controller. This is
software evidence, not a physical USB trace. Interpretation must combine the
endpoint-specific flags, software state and controller state.

## Validation

- 19,268 actual patched TinyUSB cases and 191 adapter scenarios under ASan/UBSan,
  including missing snapshots, immutable timeout output and all status-buffer
  capacities/counts with optional controller output and maximum field values.
- 1,320 native-getter mock-register cases, covering every slot, active/inactive
  state, initially enabled/disabled interrupts, invalid identity/pointers/slots,
  unchanged registers/software state, and restoration of prior interrupt state.
  The actual getter and endpoint lookup are compiled from the source; these are
  not physical-controller or full interrupt-handler tests.
- Five real FatFs/mount tests on disposable FAT32 images with independent
  long-name/content readback, plus 14 legacy USB cases.
- ARM build and stack layout pass: 4096 bytes core 0, 2048 bytes core 1, no overlap.
  Fault-path runtime peak remains unmeasured.
- Flash 4,811,152 bytes; RAM 382,076 bytes. The feature-disabled UF2 is identical
  to the established baseline. The dependency patch and all changed TinyUSB
  source/header hashes are pinned.

[Source and artifact validation manifest](USB-HOST-STATE.json).
The upstream metadata-only draft PR is unchanged.

## Hardware plan

After a fresh full recovery backup and verified flash, I will boot ELKS without
the SanDisk, insert it at login, compare its three existing file baselines through
a read-only mount, and confirm a fresh directory name is absent. I will remount
read/write and make one guarded mkdir attempt. I will save native output, the
full USB/controller status and a screenshot, then unmount. A fault ends writes
for that session; existing media files are preserved.

## PPC512 controller result — 30 October 2026

The full recovery backup and firmware both passed readback verification. The first
ELKS query already detected a USB disk; exact insertion timing was not confirmed.
Read-only mounting and all three saved SanDisk SHA256 baselines passed. A fresh
`PMHC1030` name was absent. One guarded mkdir after read/write remount failed:

```text
?USB fault op=35 f=6 s=255 r=0 d=0
USB timeout op=35 tag=2381 stage=3 submit=1
USB pending ep=81 want=13 deadline_ms=2000
USB last stage=1 event=0 ep=01 got=31
HCD slot=1 act=1 rem=0 got=0 sof=1168/1119 v=1
HCD bc=0000740d ec=a8000200 poll=00000006 addr=00010001 sc=20008604
HCD ss=50000205 bs=00000000 irq=00000000 ie=0000047b sm=00000009
```

The endpoint and polling slot are enabled, and the receive buffer is available,
not full, with a length of 13 and no STALL flag. Software still marks the transfer
active with zero received bytes. `rem=0` is expected after the 13 bytes have been
assigned to the hardware buffer; it does not mean successful completion.
The frame samples differ by 1999 modulo 2048. No buffer-completion flag or masked
interrupt is pending in this snapshot. Internal state 9 is retained raw; I am not
assigning it an undocumented symbolic meaning.

This is consistent with a status receive still waiting, and makes a stopped
controller or an already-pending completion interrupt less likely. The global
ACK and NAK flags are set, but are latched and cannot establish repeated NAKs on
this command. This is not proof of an unsupported opcode, cache state or device
failure. No physical bus trace was taken.

I made no retry and unmounted USB successfully. ELKS remains running with only
its root filesystem mounted. No sector write was submitted according to the
pre-write guard and `d=0`; no post-fault file readback was attempted. Initial stack
readings were 1824/4096 firmware and 418/700 kernel, with zero flags/invalid samples;
the fault-path peak remains unmeasured.

The next experiment should observe late status completion for a separate bounded
window while retaining write quarantine and the normal two-second deadline.
It must not retry the command, enable writes or reset the controller. This is a
proposed next experiment, not a behavior change in this tested build.

[Actual PPC512 screenshot](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-sandisk-host-state-20261030.png).
