# USB current cache-mode diagnostic

I am testing a firmware-only follow-up to the [Kingston sense result](USB-SYNC-LIFETIME.md#request-sense-hardware-result).
**PPC512 reads pass, but MODE SENSE(6) hit a phase/transport failure and MODE SENSE(10) was skipped. The Kingston cache state remains unknown; USB writes remain blocked.**
It reports the drive's current write-cache enable bit when available. It does not
change cache settings, bypass synchronization or make the failed write succeed.
The existing diagnostics ELKS kernel and utilities are sufficient.

## Query sequence

The checked USB adapter still probes SYNCHRONIZE CACHE(10) before its first sector
write. Only a validated initial clean flush failure, followed by current-format
REQUEST SENSE 70/72 with key/ASC/ASCQ 5/20/00, enables this diagnostic. Other sense
codes, deferred sense, dirty/established flush failure, failed READ/WRITE and
ambiguous transport do not trigger cache queries.

1. MODE SENSE(6), opcode 1a, requests current values (PC=0), caching page 08,
   subpage 0, DBD=1 and a 64-byte allocation.
2. If that command completes with validated status 0 or 1 but supplies no usable
   cache-enable result, one MODE SENSE(10), opcode 5a, requests the same page and
   allocation. A valid result from the first query ends the sequence.
3. Timeout, rejected submission, disconnect, malformed completion, phase error,
   unavailable device or exhausted tag space ends the sequence immediately.

Each variant has a separate persistent 64-byte buffer and request, a unique tag
and a two-second deadline. There are no retries, resets, MODE SELECT commands or
writes in these queries. The original flush fault remains latched regardless of
the result. A diagnostic may take up to four additional seconds; its final status
is available through cached `pmstat --usb` even if the ELKS file operation fails.

The pinned TinyUSB patch permits short DATA only for these exact bounded query
forms (and the preceding REQUEST SENSE form). MODE SENSE actual byte count must
agree with CSW residue for both successful and command-failed status. A malformed
transfer cannot authorize the alternate query. Sector commands retain exact byte
counts. The patch hash is checked at configuration time.

## Parsing and status

The parser requires the complete mode header, an advertised length within the
received data, and a complete page 08 with SPF=0 and at least 20 total page bytes.
It honors standard eight-byte block descriptors if the drive ignores DBD. Missing
pages, unexpected media, long-LBA descriptors, invalid lengths and truncated data
remain unknown. Older shortened caching pages are not accepted by this experiment.
Only WCE, bit 2 of page byte 2, is decoded; it is a device report, not a measured
power-loss guarantee. The current mode page and WCE interpretation are described
in [T10's caching-page material](https://www.t10.org/ftp/t10/document.05/05-239r0.pdf)
and used by [Linux's SCSI disk driver](https://github.com/torvalds/linux/blob/master/drivers/scsi/sd.c).

The existing original fault/sense fields remain and gain `c6=VALUE c10=VALUE`:

| Value | Meaning |
| --- | --- |
| `on`, `off` | Complete current caching page reports WCE=1 or WCE=0. |
| `-` | Variant not attempted. |
| `s1` | Validated command-failed CSW; its detailed sense was not queried. |
| `short`, `length`, `header`, `page` | Completed reply lacks an acceptable caching page. |
| `reply`, `phase` | Invalid completion or phase/transport error; stop. |
| `submit`, `time`, `detach`, `skip` | Submission failure, deadline, disconnect, or unavailable/precondition failure; stop. |

For example, `c6=s1 c10=off` would report a failed six-byte query and a valid
ten-byte query with write caching disabled. **This is a host-test example, not a
Kingston observation.** Every value except `on`/`off` means cache state is unknown.
Even `off` does not release quarantine or permit the original write in this build.
A missing or unsupported page must never be interpreted as absence of volatile
cache. No compatibility policy has been selected.

## Validation

The actual patched TinyUSB state machine passes 18,423 completion/length/status
cases, including all received-length/residue pairs around both 64-byte queries,
for status 0 and 1, plus checks that other request forms remain strict. The real
adapter passes 150 scenarios under ASan/UBSan, including enabled/disabled cache,
six-/ten-byte replies, ignored DBD, padding, malformed headers/pages, every failure
category, bounded formatting, no second query after ambiguous failure and late
completion after caller memory is freed. Five real FatFs/FAT32 scenarios and 14
legacy USB scenarios also pass. A simulated unsupported flush still submits zero
sector writes even when the cache page reports `off`.

Both ARM builds pass. The option-OFF UF2 is byte-identical to the preceding OFF
artifact. The candidate uses 4,807,168 FLASH bytes and 380,988 RAM bytes, an increase
of 208 RAM bytes over the installed sense diagnostic. Core 0 retains 4096 stack
bytes and core 1 retains 2048, with no scratch overlap. Fault-path runtime stack peaks remain unmeasured; the hardware result is below. [Artifact/source hashes](USB-CACHE-MODE.json).

Build with the same pinned dependencies and `PICOMEM_EXPERIMENTAL_USB_SYNC=ON`
configuration described in [the base experiment](USB-SYNC-LIFETIME.md#build).
Run `python3 tools/tests/usb-sync/test.py` and the stack-layout checker against the
new link map. No ELKS build or upstream PR change is part of this milestone.

## PPC512 hardware result — October 29, 2026

The installed firmware matched the prior sense diagnostic. The fresh full 16 MiB
recovery backup and candidate readback both passed. ELKS booted with its existing
kernel, USB appeared immediately, and all eight retained files passed full-content
checks through a read-only mount. Initial stack readings were 1736/4096 firmware
bytes and 426/700 kernel bytes, with flags and invalid counts zero. These do not
measure the later fault-path peak.

After unmounting and explicitly mounting read/write, I made one fresh
`mkdir /mnt/usb/PMCCH29` attempt. The name was absent from the preceding directory
listing. It returned exit 1 and `/mnt/usb/PMCCH29: cannot create directory`.
The cached status was:

```text
?USB fault op=35 f=2 s=1 r=0 d=0 rs=70:5/20/00 n=18 c6=phase c10=-
```

The original flush still reports unsupported-opcode sense. The six-byte cache
query yielded status 2. That may be a device-reported phase error or the checked
TinyUSB layer's synthetic error for a stall, invalid transfer, bad CSW or length
mismatch. This build does not retain the underlying transport details, so none of
those causes is established. It does not prove MODE SENSE is unsupported.
The alternate query was correctly skipped, and WCE remains unknown.

The first-write guard returned before WRITE(10) submission, inferred from the
firmware control flow and cached result; no physical USB trace was taken. There
was no retry or cache-setting change. USB unmounted successfully, SD stayed
unmounted and ELKS remains running with the adapter fault latched until restart.

[Native screenshot and evidence](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/USB-SYNC-LIFETIME.md#cache-mode-ppc512-result).

## Next diagnostic

I need to preserve the first transport fault's stage, event, actual/expected byte
counts and available CSW validation fields before the checked layer substitutes
status 2. This should add cached evidence without issuing more USB commands or
weakening the write guard. No such follow-up is implemented or staged yet.
The result will determine whether transport handling needs correction or another
query is justified. Missing cache information cannot justify a flush bypass.
Full workload, Mac/FAT verification and DOS compatibility remain pending.
