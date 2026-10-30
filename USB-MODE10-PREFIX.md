# MODE SENSE(10)-first and raw response prefix

I am testing a firmware-only follow-up to the [PPC512 DATA/residue mismatch](USB-TRANSPORT-DETAIL.md#ppc512-transport-detail-result).
**The PPC512 test is complete: eight retained files verified, but the guarded
write was rejected. MODE SENSE(10) returned a header without a caching page;
USB write-cache state remains unknown and the write guard remains active.** The existing ELKS kernel
and `pmstat` utility are sufficient.

## Query order and safeguards

After the initial clean pre-write SYNCHRONIZE CACHE failure and validated current
REQUEST SENSE 5/20/00, I now try MODE SENSE(10), opcode 5a, first. It requests the
same current caching page 08, DBD=1, subpage 0 and 64-byte allocation as before.
One MODE SENSE(6) alternate is allowed only after a fully checked status-0/1
completion without usable cache information. Timeout, malformed completion,
submission failure, detach or DATA/residue mismatch stops the sequence.

Each query still owns its persistent buffer, distinct tag and two-second deadline.
The command forms, transfer acceptance predicates, page parser, original flush
fault, write quarantine and no-retry policy are unchanged. A reported WCE=0 still
does not authorize a sector write. There is no cache-setting change, reset recovery,
residue override or flush bypass.

## Captured bytes and cached output

The pinned TinyUSB patch copies at most the first 16 bytes at the accepted DATA
event for the exact bounded MODE SENSE query. It samples only a successful event
on the expected endpoint, with a count within the allocation and a non-null buffer.
It does not sample sector data, REQUEST SENSE, failed or oversized DATA transfers,
or other query forms. The sample is independent of the transfer buffer before CSW
processing, so a later CSW mismatch or buffer change cannot replace it.

The per-command terminal record now includes checked status-0/1 completions as
well as failures. Its new getter validates the device and tag; the old fault getter
still returns failures only. The driver record resets on a new command/device
close. The adapter copies the latest validated cache callback's report into static
text before any alternate command. Late callbacks and detach cannot alter that
text after collection. If the alternate times out or has an invalid callback, the
text can still describe the earlier query: always check `op` alongside `c6/c10`.

`pmstat --usb` keeps three extra lines, within the existing eight-line/1024-byte
limit. The first now starts `USB query`; `why=0` means checked transport completion,
not a valid cache page. Reasons 1–9 retain their [earlier meanings](USB-TRANSPORT-DETAIL.md#field-meanings).
The third line uses shorter labels to leave room for hexadecimal bytes:

```text
USB query op=5a tag=427 why=8 stage=3 event=0
USB xfer ep=81/81 got=13 want=13 data=8
USB CSW ck=1f s=0 r=0 n=8 b=0006000000000000
```

This format was exercised by a host test and then observed on the PPC512 below.
`ck`, `s` and `r` mean CSW check mask, raw status and residue. `n` is the number
of sampled bytes, at most 16; `b` is exactly that many bytes in hexadecimal.
`data` remains the recorded transfer count, which can exceed the sampled prefix.
`n=0` means no captured prefix, possibly zero DATA or unavailable DATA; it does not
by itself prove an empty device response. Each line remains at most 79 characters.

The captured prefix is diagnostic evidence only. It does not repair residue,
authorize the alternate query after an ambiguous transfer, or feed a cache-policy
fallback. It should show whether the short reply resembles a mode header, while
still leaving device-versus-host byte-count errors open without a bus trace.

## Validation

- 19,222 actual patched-TinyUSB cases with ASan/UBSan, including both query forms,
  every DATA length 0–65, invalid events/endpoints/query forms, null buffers,
  CSW mismatches, immutable prefix capture and reset/late-event handling.
- 153 adapter scenarios with ASan/UBSan, checking MODE SENSE(10)-first ordering,
  guarded alternate, no sector writes after rejected flush, four/eight-byte
  mismatch examples, bounded maximum-value formatting and status-buffer limits.
- Five actual FatFs/mount scenarios on disposable FAT32 images, independent
  long-name/content readback, and 14 legacy USB tests.
- 30 existing ELKS status-parser cases including old and new diagnostic formats.
- ARM build and stack-layout checks pass: core 0 reserves 4096 bytes and core 1
  reserves 2048 bytes, with no scratch overlap. Fault-path runtime peak remains
  unmeasured. RAM increases by 160 bytes and flash by 408 bytes from the prior build.
- The feature-disabled UF2 remains byte-identical. Both patched dependency hashes
  are pinned in CMake, and the dependency preparation tool verifies the exact patch.

[Artifact/source hashes and validation manifest](USB-MODE10-PREFIX.json).
The original metadata-only upstream draft PR is unchanged.

## MODE SENSE(10)-first PPC512 result

I flashed the candidate after a fresh full recovery backup; both backup and new
firmware passed readback verification. USB was detected without reconnecting, and
all eight retained files passed full-content checks through a read-only mount.
After confirming `PMMD1029` was absent, I remounted read/write and made exactly
one `mkdir /mnt/usb/PMMD1029` attempt. It returned exit 1 and
`/mnt/usb/PMMD1029: cannot create directory`. I did not retry.

```text
?USB fault op=35 f=2 s=1 r=0 d=0 rs=70:5/20/00 n=18 c6=- c10=phase
USB query op=5a tag=427 why=8 stage=3 event=0
USB xfer ep=81/81 got=13 want=13 data=8
USB CSW ck=1f s=0 r=0 n=8 b=0006000000000000
```

The captured eight bytes form a MODE SENSE(10) header: length field 6 means eight
bytes including that field, with no block descriptors or mode pages. There is no
caching page and no WCE bit. **Cache state remains unknown.** The host recorded
eight DATA bytes for a 64-byte allocation, then a successful 13-byte CSW transfer
with status 0 and residue 0. The checked transport rejected that count mismatch
and synthesized a phase error; the device did not report status 2. MODE SENSE(6)
was skipped. A device residue quirk or host counting issue still needs independent
verification; this is not a USB bus trace.

No sector write was submitted according to the pre-write guard control flow and
`dirty=0` evidence. Initial stack use was 1632/4096 firmware bytes and 418/700
kernel bytes, with zero flags/invalid counts. After the fault, `pmperf /mnt/usb`
returned exit 1, `pmperf ioctl: I/O error`; the fault-path peak remains unavailable.
USB unmounted successfully; SD stayed unmounted. ELKS remains running with the
fault latched until PicoMEM restarts.

[PPC512 screenshot](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-usb-mode10-prefix-20261029.png)

The screenshot is rendered from actual PPC B800 video memory. Mount and cached
status output are live; the saved native mkdir error is explicitly labelled and
was not rerun. The installed firmware is source `ef16c87` / UF2 SHA-256
`48b56652948bacb28ce6988cbdc07799e189de8b9aa04dfbff6bd2752c5cd4b7`.

## Next compatibility work (not implemented)

I will design and host-test bounded short MODE SENSE reply handling, preserving
actual received length and raw CSW evidence while keeping exact sector-transfer
checks. Linux USB storage combines actual transfer residue with the bounded CSW
residue instead of demanding exact agreement; a successful command can have a
short reply. This is a useful implementation reference, not proof of this drive's
persistence behavior. [Linux transport source](https://github.com/torvalds/linux/blob/d89e1d186ec9271bf78e62771779e9fc03bf0fc3/drivers/usb/storage/transport.c#L1195-L1229).

Write policy needs a separate decision: distinguish successful cache flush,
reported cache-disabled operation, and unknown cache state. Linux's disk driver
uses explicit defaults when a caching page is absent, including an assumed
write-through mode unless configured otherwise. An assumption must remain visible;
it cannot be presented as a device-reported WCE=0. I have not enabled that fallback
or relaxed the current write guard. Independent device comparison and persistence
tests remain necessary before a RW release. [Linux cache defaults](https://github.com/torvalds/linux/blob/d89e1d186ec9271bf78e62771779e9fc03bf0fc3/drivers/scsi/sd.c#L3071-L3082).

The [bounded short-reply follow-up](USB-SHORT-MODE.md) is now built and host-tested; hardware validation is pending.
