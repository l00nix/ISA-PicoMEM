# MODE SENSE(10)-first and raw response prefix

I am testing a firmware-only follow-up to the [PPC512 DATA/residue mismatch](USB-TRANSPORT-DETAIL.md#ppc512-transport-detail-result).
**The candidate is built and host-tested, but not flashed. USB write-cache state
remains unknown, and the write guard remains active.** The existing ELKS kernel
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

This is a **host-test example**, not a PPC512 observation or predicted response.
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

## Physical test plan

After readiness confirmation, I will check USB/SD are unmounted and shut down
ELKS. A fresh full recovery backup and readback verification precede the flash.
After normal boot, I will check USB detection, mount read-only, and verify all eight
retained files. I will confirm the new `PMMD1029` name is absent, unmount, mount
explicitly read/write, and make one `mkdir` attempt. I will save its native result
and complete cached status, then unmount and collect a screenshot. Any failure
ends the write test without a retry or relaxation of the guard.
