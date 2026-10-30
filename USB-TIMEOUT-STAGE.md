# USB timeout-stage diagnostic candidate

I built this diagnostic after the [SanDisk comparison](USB-SHORT-MODE.md) timed
out during its initial SYNCHRONIZE CACHE(10), before any sector write was submitted
according to the pre-write guard. **Host tests and builds pass; the PPC512 test captured the pending status reply.
This is not a read/write release candidate.**

## What changes

At the existing two-second READ/WRITE/flush command deadline, I save the pending
MSC stage, endpoint, expected transfer length, host-driver submission result and
last completion callback. The snapshot is keyed by device and command tag. It
reads only tracked metadata, never an incomplete CSW or sector buffer, and sends
no USB transaction. The cached report stays unchanged if a late callback arrives.
The normal terminal MODE SENSE report remains available on its existing path.

The report uses three bounded lines in the existing USB status response. Existing
ELKS `pmstat --usb` can display it; no kernel or utility update is required.
For illustration only, a pending status reply could look like:

```text
USB timeout op=35 tag=123 stage=3 submit=1
USB pending ep=81 want=13 deadline_ms=2000
USB last stage=1 event=0 ep=02 got=31
```

Stage 1 is the command wrapper, 2 is DATA, and 3 is the status wrapper. Submission
1 means the host driver accepted the transfer; 2 means it rejected it. Acceptance
does not prove a transaction occurred on the physical USB bus. Last stage 0/event
255 means no completion callback was recorded. An unavailable matching snapshot
is reported explicitly. These fields do not establish the device's cache policy.

The existing timeout, write quarantine and validation rules remain unchanged.
There are no new retries, resets or command submissions. In particular, this
candidate cannot enable writes on the Kingston with absent caching pages, and
does not assume that the SanDisk timeout means an unsupported opcode.

## Validation

- 19,268 actual TinyUSB state-machine cases with ASan/UBSan, including accepted
  and rejected submissions at command, DATA and status stages, both DATA
  directions, wrong identity, inactive interfaces and stale CSW exclusion.
- 191 adapter scenarios with ASan/UBSan, including timeout report capture,
  missing snapshots, maximum field values, bounded atomic status appending,
  unchanged quarantine and immutable cached output after late completion.
- Five real FatFs/mount cases on disposable FAT32 images with independent
  long-name/content readback; 14 legacy USB cases.
- ARM build and stack map checks pass: 4096 bytes core 0, 2048 bytes core 1,
  no scratch overlap. Fault-path runtime stack peak remains unmeasured.
- Flash 4,810,024 bytes; RAM 381,596 bytes. Feature-disabled UF2 is byte-identical
  to the established baseline. Dependency patch and source hashes are pinned.

[Source and artifact validation manifest](USB-TIMEOUT-STAGE.json).
The upstream metadata-only draft PR is unchanged.

## Hardware plan

After a fresh verified full recovery backup and firmware flash, I will boot ELKS
without the SanDisk, then insert it at login to match the previously working
startup sequence. I will check read-only access and the three saved file baselines,
confirm a fresh test directory is absent, and make one guarded mkdir attempt.
I will capture native output, cached USB status and a screenshot, then unmount.
Any failure ends writes for that session. The display-switch observation remains
separate and unresolved; no causal link to the USB timeout has been established.

## PPC512 result — 30 October 2026

The full recovery backup and new firmware passed readback verification. ELKS
returned at its usual address, and the first status query already detected the
USB drive without an old fault. Exact insertion timing was not confirmed.
Read-only mounting passed, and all three prior SanDisk file baselines matched
byte counts and SHA256. A fresh `PMTS1030` directory was absent. After remounting
read/write, one `mkdir` failed with `cannot create directory`.

```text
?USB fault op=35 f=6 s=255 r=0 d=0
USB timeout op=35 tag=2381 stage=3 submit=1
USB pending ep=81 want=13 deadline_ms=2000
USB last stage=1 event=0 ep=01 got=31
```

The host reported a successful 31-byte command-wrapper completion on OUT endpoint
01, then accepted the 13-byte status IN transfer on endpoint 81. That status
transfer had not delivered a completion callback before the two-second deadline.
This narrows the investigation to status-transfer progress/completion delivery;
it does not distinguish device delay/NAK from a host-controller or scheduling
problem, and it does not prove an unsupported command. There is no physical bus
trace. No sector write was submitted according to the pre-write guard and `d=0`.

I made no retry, unmounted USB successfully, and left ELKS running with only its
root filesystem mounted. The cached evidence remained identical during the screen
capture. Initial stack readings were 1888/4096 firmware and 418/700 kernel, with
zero flags/invalid samples; the fault-path peak remains unmeasured. The display
shortcut after hotplug remains unconfirmed. I will investigate the host-controller
status-IN path before changing timeout or recovery behavior.

[Actual PPC512 screenshot](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-sandisk-timeout-stage-20261030.png).
