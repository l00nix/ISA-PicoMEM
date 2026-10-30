# Bounded short MODE SENSE replies

I built this follow-up to the [PPC512 header-only response](USB-MODE10-PREFIX.md#mode-sense10-first-ppc512-result).
**The Kingston hardware test passed the short-reply handling checks; both cache
pages are absent and writes remain blocked. The SanDisk comparison found an initial flush timeout.**

## What changes

Successful, exact 64-byte MODE SENSE(6/10) current caching-page queries may complete
with fewer DATA bytes than requested, even when the device reports zero residue.
I preserve both raw counts and the original CSW. The adapter requires a matching,
successful terminal transfer record and limits parser input to:

```text
min(actual DATA bytes, 64 - bounded CSW residue)
```

This cannot enlarge the received response or turn zero-filled unused buffer space
into a cache page. The exact command form includes LUN 0, DBD=1, page 08, subpage 0,
allocation 64, and zero reserved/control bytes. Signature, tag, status, endpoint,
complete 13-byte CSW, DATA bounds and timeout checks remain. Failed status-1 MODE
commands still require DATA/residue agreement before permitting the alternate.
Sector READ/WRITE and REQUEST SENSE checks are unchanged.

For a successful short reply, `why=0` can now coexist with `ck=1f`: the missing
agreement bit remains visible. This means the bounded query completed; it does
not mean a caching page was found. Header-only data parses as `page` (unknown),
which can permit one alternate query. A missing or inconsistent terminal record
stops parsing and the alternate. Raw prefix bytes remain diagnostic evidence.
The three cached output lines describe the latest completed query; an alternate
can replace the first query's report, while `c6/c10` retain both classifications.

I used [Linux USB storage's actual-length/residue handling](https://github.com/torvalds/linux/blob/d89e1d186ec9271bf78e62771779e9fc03bf0fc3/drivers/usb/storage/transport.c#L1195-L1229)
as a reference for this narrow adjustment. I did not adopt its broader quirks,
reset/retry behavior or any cache-policy defaults. The original flush fault and
write quarantine remain active, even if the query reports WCE=0.

## Validation

- 19,256 actual patched-TinyUSB cases with ASan/UBSan: every DATA/residue length
  0–65 for both MODE forms and statuses 0/1, raw evidence preservation, all CDB
  byte mutations, invalid transfers, late events and unchanged sector/sense guards.
- 191 adapter scenarios with ASan/UBSan, including header-only replies with zero
  residue, short replies with a plausible page beyond received bytes, conservative
  residue bounds, missing/altered terminal records, one alternate and zero sector
  writes after unsupported initial flush.
- Five real FatFs/mount scenarios on disposable FAT32 images, including independent
  long-name/content readback, plus 14 legacy USB tests.
- ARM build and map checks: core 0 reserves 4096 bytes; core 1 reserves 2048 bytes,
  with no scratch overlap. Runtime fault-path peak is still unmeasured.
- Flash usage is 4,809,136 bytes (+168); RAM remains 381,500 bytes. The feature-off
  UF2 is byte-identical to the prior baseline. Dependency patch and hashes are pinned.

[Source/artifact hashes and validation manifest](USB-SHORT-MODE.json).
No new ELKS kernel or utility is required. The upstream metadata draft PR is unchanged.

## Original hardware comparison plan (results below)

I will first test the Kingston DataTraveler 1 GB FAT32 after a fresh firmware backup,
verified flash and normal boot. I will verify the eight retained files through a
read-only mount, confirm a fresh directory name is absent, and make one guarded
mkdir attempt. A failure ends writes for that session; I will save native output,
query status and a screenshot, then unmount.

I will also test the user's SanDisk Cruzer Mini 512 MB FAT32. I will preserve its
existing files and will not format it. Each drive gets a clean MCU session after
any latched fault, and swapping requires a successful unmount. I will record its
identity and read-only baseline before creating a new test directory. If cache
flush succeeds, bounded file writes will be checked byte-for-byte, followed by
sync, remount, independent Mac FAT/content checks and power-cycle persistence.
If it rejects flush, the same bounded sense/cache diagnostics apply and writes stop.
The SanDisk's behavior was unmeasured when this plan was prepared; results follow below.

## Remaining release policy

A drive accepting SYNCHRONIZE CACHE can exercise the existing guarded write path.
A valid WCE=0 reply and a missing cache page are different cases; neither grants
writes in this candidate. Before a release I need an explicit policy distinguishing
verified flush support, reported cache-disabled behavior, and unknown cache state.
Any compatibility assumption must be visible and separately tested, never silently
presented as a guarantee that data survives power loss.

## Kingston short-reply hardware result

I flashed source `8aa9046` after a verified full recovery backup. The new firmware
passed readback verification, and all eight retained files passed full-content
checks through a read-only mount. One guarded `mkdir /mnt/usb/PMSH1029` returned
exit 1 with `cannot create directory`; I did not retry.

```text
?USB fault op=35 f=2 s=1 r=0 d=0 rs=70:5/20/00 n=18 c6=page c10=page
USB query op=1a tag=428 why=0 stage=3 event=0
USB xfer ep=81/81 got=13 want=13 data=4
USB CSW ck=1f s=0 r=0 n=4 b=03000000
```

Both cache queries completed and parsed as missing caching page. The latest
MODE SENSE(6) reply is a four-byte header, `03 00 00 00`. Its zero residue still
disagrees with the actual count, but the bounded short-reply path now handles it
without inventing a phase error or parsing beyond received bytes. The MODE10
classification is retained as `page`; its raw report was replaced by the latest
query, so this run does not independently retain its exact bytes.

**The Kingston's write-cache state remains unknown and writes remain blocked.**
The unsupported initial flush fault remains visible. No sector write was submitted
according to the pre-write guard and `dirty=0`, rather than a physical bus trace.
Initial measured stack usage was 2052/4096 firmware bytes and 418/700 kernel bytes,
with zero flags/invalid counts. Fault-path peak remains unmeasured. USB unmounted
successfully; SD stayed unmounted.

[PPC512 screenshot](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-usb-short-mode-20261029.png)

This screenshot is rendered from actual PPC video memory, with live cached status
and mount output. The saved native mkdir error is explicitly labelled and was not
rerun. The completed SanDisk comparison is recorded below.

## SanDisk comparison result

I tested a second drive, reported as a SanDisk Cruzer Mini 512 MB FAT32. After
booting without a USB stick and inserting it once ELKS was running, PicoMEM
reported a fresh checked-I/O mount with no previous fault. The read-only mount
and directory listing passed. I fully read three existing package files (8,660,
8,720 and 35,508 bytes), saved local copies and recorded their checksums. These
are pre-test baselines, not independent content verification or post-test checks.
I have not published the unrelated personal filenames from the directory listing.

After confirming `PMSD1029` did not exist, I remounted read/write and made one
`mkdir /mnt/usb/PMSD1029` attempt. It failed with exit 1 and `cannot create directory`:

```text
?USB fault op=35 f=6 s=255 r=0 d=0
```

`op=35` is SYNCHRONIZE CACHE(10); `f=6` is the adapter's two-second timeout.
`s=255` is a sentinel, not a status returned by the drive. There was no validated
failed CSW, so REQUEST SENSE and MODE SENSE were not submitted. This does not
establish that SanDisk rejects the opcode or has its cache enabled/disabled.
The precise transport stage and cause of the timeout remain unknown.

No sector write was submitted according to the pre-write guard and `dirty=0`
evidence; this is not a physical bus trace. I did not retry. USB unmounted
successfully, SD stayed unmounted, and ELKS is left running with the fault latched.
Initial stack usage was 2136/4096 firmware bytes and 426/700 kernel bytes, with
zero flags/invalid counts. Fault-path peak remains unmeasured.

[PPC512 SanDisk screenshot](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/screenshots/ppc512-sandisk-flush-timeout-20261029.png)

The screenshot uses actual PPC video memory, live mount/status output and the
explicitly saved native error, without repeating the write.

| Drive | Read-only checks | Initial cache flush | Cache-page queries | Write outcome |
| --- | --- | --- | --- | --- |
| Kingston DataTraveler 1 GB | Eight retained files verified | Valid failure; sense 5/20/00 | Both completed, no caching page | Blocked before sector write |
| SanDisk Cruzer Mini 512 MB | Listing and three file baselines read | Adapter timeout; no validated CSW | Not run | Blocked before sector write |

The second drive exposed a separate timeout path; it does not validate a RW
release. Next I need bounded evidence of the flush transport stage at timeout,
then host tests for any required recovery, alongside an explicit policy for
unsupported flush with unknown cache state. Extending the timeout or bypassing
flush without that evidence is not an established fix.

The user also reported the PPC display-switch shortcut working again after a
boot without the SanDisk. Its behavior after hot-plugging the drive was not
checked. This remains an observation to investigate, not proof of a firmware,
keyboard, power or USB-device cause. No screen-control code changed in this test.
