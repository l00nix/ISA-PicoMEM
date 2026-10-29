# USB cache synchronization and transfer lifetime candidate

I am testing a firmware-only follow-up to [file-service error reporting](FILE-SERVICE-ERRORS.md).
**The Kingston now reports invalid command operation code for SYNCHRONIZE CACHE(10), confirmed by REQUEST SENSE. Reads pass; USB writes remain blocked while I investigate its cache behavior.**
This is a default-off experiment, not a USB read/write release candidate. The
existing diagnostics ELKS kernel and utilities are sufficient; SD stays read-only.

## What changes

The previous adapter passed a caller's sector buffer to asynchronous TinyUSB I/O.
Its two-second timeout could return while the controller still held that address.
Refusing later requests did not protect a caller that freed or reused its buffer.
The new adapter passes only one aligned, static 512-byte buffer to TinyUSB, and
copies read data to the caller only after a fully checked completion. Writes copy
into that buffer before submission. Multi-sector requests run one sector at a time.

A timeout, rejected submission, disconnect during I/O, failed SCSI response, or
invalid completion permanently stops USB sector I/O until PicoMEM firmware
restarts. The buffer and request remain allocated and are never reused after a
failure. Late completion cannot clear that state. This contains late transfers;
it does not cancel them, repair a filesystem, roll back a partial write, or reset
the USB bus. A warm PC reboot is not assumed to restart PicoMEM.

The pinned TinyUSB patch checks transfer results, endpoint direction, exact byte
counts, CSW signature/tag, residue bounds and status range before reporting a
completion. The adapter additionally requires zero residue, successful status,
the current request, callback identity and the expected data buffer. Its command
tags are unique across clean reconnects and cannot wrap. Sector replies must be
full length. The REQUEST SENSE follow-up below permits a bounded short sense reply
only when its actual transfer length agrees with the CSW residue.

`CTRL_SYNC` now sends SCSI SYNCHRONIZE CACHE(10), with IMMED=0, SYNC_NV=0 and zero
LBA/block count. The intent is to wait for the device to synchronize the whole
medium. The first write also probes this command **before any sector mutation**.
An unsupported or failed flush fails closed; there is no silent fallback. A
successful closing flush clears the dirty state. Disconnecting with dirty data
also latches a fault. The two-second command deadline remains, so a slow flush can
fail safely even on an otherwise functional drive. Device compliance and physical
power-loss behavior remain unproven.

The mount callback attaches one enumerated device synchronously and avoids a
second inquiry into a shared response buffer. Invalid addresses, a second device,
non-512-byte sectors and empty geometry are rejected. Capacity remains 32-bit;
it is no longer narrowed to 16 bits. The firmware's USB status text becomes
`USB Disk mounted (checked I/O)` rather than the extra inquiry's vendor/size line.
The adapter is shared by DFS and USB disk-image access, including DOS.

## Build

Use the pinned SDK/toolchain from the preceding milestone. Prepare dependencies:

```sh
python3 tools/prepare-lfn-deps.py
```

Keep `PICOMEM_EXPERIMENTAL_LFN`, `PICOMEM_EXPERIMENTAL_LFN_CREATE`,
`PICOMEM_EXPERIMENTAL_LFN_RENAME`, `PICOMEM_EXPERIMENTAL_STACK_RESERVE`,
`PICOMEM_EXPERIMENTAL_DIAG` and `PICOMEM_EXPERIMENTAL_FILE_ERRORS` enabled, then add:

```sh
-DPICOMEM_EXPERIMENTAL_USB_SYNC=ON
```

The option defaults OFF and requires the checked-file-error option and its
PicoMEM 2 prerequisites. Configuration checks the exact patched TinyUSB MSC source
hash. The patch and dependency commits are recorded in
[USB-SYNC-LIFETIME.json](USB-SYNC-LIFETIME.json). Run the dependency preparation
script again to verify that the pinned patches are already applied.

The first candidate used 4,804,456 FLASH bytes and 380,700 RAM bytes: increases of 416
and 512 bytes respectively. SCRATCH_X stays at 3748 bytes and core 0 retains its
4096-byte SCRATCH_Y stack; core 1 retains 2048 bytes. These are link-map checks,
not runtime stack measurements. One-sector transfers may reduce throughput.

A separate option-OFF build has exactly the baseline's binary length and contents
except for the single RTC `__TIME__` string (10:29:16 versus 13:15:49). It is not
byte-identical as a UF2; every byte outside that timestamp matches the installed
file-error baseline. Original tested firmware artifacts are retained for rollback.

## Host validation — October 29, 2026

```sh
python3 tools/tests/usb-sync/test.py
python3 tools/check-stack-reserve.py build-arm-usb-sync/picomem.elf.map
```

The test runner needs a C compiler with ASan/UBSan and mtools. It exercises:

- The actual patched TinyUSB state machine: 578 completion/length/status cases,
  including normal READ, WRITE and zero-data transitions.
- The actual adapter with asynchronous transport models: 56 scenarios covering
  geometry/bounds, reentrancy, timer wrap, read and write timeout, disconnect,
  malformed completion, failed pre-write/closing flush and late completion after
  freeing the caller buffer. Faults stay latched across reconnect attempts.
- Actual FatFs and mount callbacks over the adapter on five disposable FAT32
  images: normal long-name write/remount/readback, unsupported flush with zero
  sector writes, write failure, closing flush failure, and mount failure. A second
  device cannot replace or unmount the selected drive. `mcopy` independently checks
  the normal long filename and all 1537 payload bytes.

The existing ten regression suites also pass: checked DFS/FatFs errors, legacy
USB error handling, LFN/create/rename protocol and adapters, diagnostics, the three
FAT32 LFN suites, and ROM descriptors. Both option-ON and option-OFF ARM builds
pass. Existing linker warnings for unused newlib link/unlink stubs remain.
These tests model transport completion; they do not emulate a USB controller,
prove cache persistence, or establish hardware timing/stack headroom.

## First PPC512 attempt — October 29, 2026

The full-flash backup and candidate readback passed. ELKS booted normally. The
initial USB-status query showed zero devices; after reconnecting the Kingston,
it reported `USB Disk mounted (checked I/O)`. The cause of the initial detection
miss is not established. All eight retained files matched their expected contents
through a read-only mount. The initial observed stack use was 1612/4096 firmware
bytes and 418/700 ELKS bytes, with flags and invalid counts zero.

The first `mkdir` in fresh `PMSYNC29` returned an I/O error. No workload batches
ran. The subsequent `pmperf` ioctl also failed with I/O error because the
file-service session was unusable. USB unmounted successfully, SD stayed unmounted,
and I made no further USB write attempts. The failed command and any prior sector
mutation are unknown: this build does not expose the adapter's first fault.
The first-write flush probe is a possible cause, not a confirmed diagnosis.

[Hardware evidence and native screen](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/USB-SYNC-LIFETIME.md#first-ppc512-attempt).

## Diagnostic follow-up

A small follow-up preserves the first adapter fault in `pmstat --usb`, so it can
be read even after the ELKS file-service session refuses `pmperf`. It retains all
flush checks, error responses and quarantine rules. There are no retries, sense
requests, fallback writes or new USB commands. Mount/removal status messages cannot
overwrite a fault. A clean restart is still required to resume USB I/O.

Captured on the PPC512 by the diagnostic follow-up:
`?USB fault op=35 f=2 s=1 r=0 d=0`.

- `op`: attempted SCSI opcode in hex (`28` read, `2a` write, `35` flush).
- `f`: fault category: 1 invalid reply, 2 failed status, 3 residue, 4 adapter state,
  5 submission failure, 6 timeout, 7 disconnect/dirty detach, 8 other I/O failure.
- `s`: SCSI status, or 255 when unavailable/untrusted. Status 1 means command
  failed; it does not alone establish unsupported-command sense data. Status 2
  may be a real phase error or the checked TinyUSB layer's synthetic failure.
- `r`: residue from a structurally validated reply; zero when unavailable.
- `d`: adapter dirty flag; it is not a count of sectors actually changed.

The follow-up passes 578 TinyUSB cases, 59 adapter scenarios, five real FAT32/mount
scenarios and the 14 legacy USB scenarios. Tests check bounded status formatting,
correct fault categories, and preservation through late completions, unmount and
reconnect attempts. ARM build and stack layout pass. It uses 4,805,672 FLASH bytes
and 380,704 RAM bytes, four more RAM bytes than the first candidate. Runtime fault
reporting stack usage still needs measurement. See [follow-up hashes](USB-SYNC-FAULT-STATUS.json).

## Diagnostic PPC512 result — October 29, 2026

The recovery backup and diagnostic firmware readback passed. ELKS booted and
PicoMEM detected the Kingston without another reconnect. All eight existing files
again passed full-content checks through a read-only mount. The fresh `PMSYD29`
attempt failed at its initial `mkdir`; no workload batch ran.

`pmstat --usb` returned `op=35 f=2 s=1 r=0 d=0`. This identifies a structurally
validated completion of SYNCHRONIZE CACHE(10) with command-failed status, zero
residue and a clear dirty flag. The pre-write flush probe returns on that error
before submitting WRITE(10), so this attempt stopped before sector writes. That
last conclusion follows from the validated firmware control flow and fault
record; no physical USB trace was taken. It does not retroactively establish the
exact sector history of the earlier candidate's attempt.

Status 1 is a command failure, without the detailed sense reason. It does not
prove that the opcode is unsupported. No REQUEST SENSE response was collected,
and the write safeguard has not been bypassed. USB unmounted successfully; ELKS
remains running. SD stayed unmounted. The initial stack readings remained
1612/4096 firmware bytes and 418/700 ELKS bytes with flags/invalid counts zero;
the stack peak during failure reporting was not measured.

[Native screenshot and full evidence](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/USB-SYNC-LIFETIME.md#diagnostic-ppc512-result).

The next diagnostic is implemented and hardware tested, as described below. Sense data can distinguish
invalid opcode, invalid command fields and other conditions. Missing or unsupported
sense/cache queries must not be treated as proof that the drive has no volatile
write cache. No fallback that reports a failed flush as success is justified by
this result.

The [USB BOT status definitions](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
identify status 1 as command failure. The [T10 sense-code list](https://www.t10.org/lists/asc-num.txt)
distinguishes invalid opcode (20/00) from invalid command fields (24/00). The
REQUEST SENSE follow-up below captured 20/00 on this Kingston.

## REQUEST SENSE follow-up

I added one read-only REQUEST SENSE(6), allocation length 18, DESC=0, LUN=0,
after the initial clean SYNCHRONIZE CACHE failure. Eligibility requires a fully
validated status-1 completion with zero residue, no dirty sectors and no previous
successful flush on this attachment. Failed READ/WRITE, established or dirty flush,
timeout, phase error, malformed reply and rejected submission never trigger it.
The query runs after the original completion callback returns, has its own
persistent request and aligned data buffer, a unique nonwrapping tag and a
two-second deadline. There is no retry, reset or continuation of the failed write.
The original fault remains latched even if the sense query succeeds.

The pinned TinyUSB patch permits 0–18 actual DATA bytes only for an 18-byte
REQUEST SENSE(6) IN request. A successful CSW must report residue equal to 18 minus
the received length. All other commands retain exact DATA-length checks. The
adapter checks callback/request/buffer/device identity and CSW signature/tag/status
before parsing. Fixed formats 70/71 require at least 14 received bytes and an
advertised length that includes ASC/ASCQ; descriptor formats 72/73 require the
complete eight-byte header. Only those header fields are decoded. Trailing sense
descriptors are neither requested in full nor interpreted.

`pmstat --usb` retains the original fault fields and appends
`rs=FORMAT:KEY/ASC/ASCQ n=BYTES` (hex codes, decimal byte count). The Kingston returned
`rs=70:5/20/00 n=18`: current fixed-format illegal-request / invalid opcode. Formats
71/73 identify deferred errors and must not be described as current-command errors.
If trustworthy header fields are unavailable, the suffix is `rs=short`, `format`,
`reply`, `status`, `submit`, `timeout`, `detach` or `skip`; it does not invent sense
codes. Late callbacks, reconnects and later I/O cannot overwrite the final report.

Validation passes 983 actual TinyUSB length/status cases, 89 adapter scenarios
under ASan/UBSan, five real FatFs/FAT32 scenarios and 14 legacy USB cases. These
cover fixed/descriptor and deferred responses, invalid-field/unit-attention codes,
short headers, advertised length, malformed replies, failed sense, submission
rejection, timeout, disconnect, tag exhaustion and late completion after caller
free. The unsupported-flush FAT32 case still submits zero writes. Both ARM builds
pass; the option-OFF UF2 is byte-identical to the preceding option-OFF artifact.
The candidate uses 4,805,808 FLASH bytes and 380,780 RAM bytes. Core 0 retains 4096
stack bytes and core 1 retains 2048, with no scratch overlap. Fault-path runtime
stack use remains unmeasured. [Candidate hashes and tests](USB-REQUEST-SENSE.json).

## REQUEST SENSE hardware result

The fresh full 16 MiB recovery backup and firmware readback passed. ELKS booted
with the existing kernel; USB appeared immediately without reconnecting. All eight
retained files matched their expected full contents through a read-only mount.
Initial stack use was 1624/4096 firmware bytes and 426/700 ELKS bytes, with zero
flags/invalid counts. The peak during fault reporting was not measured.

After a clean unmount and explicit read/write mount, one `mkdir /mnt/usb/PMSEN29`
returned exit 1 and `/mnt/usb/PMSEN29: cannot create directory`. The fresh name was
absent from the preceding root listing. `pmstat --usb` then returned:

```text
?USB fault op=35 f=2 s=1 r=0 d=0 rs=70:5/20/00 n=18
```

The validated 18-byte sense reply reports ILLEGAL REQUEST / INVALID COMMAND
OPERATION CODE, confirming the drive rejects SYNCHRONIZE CACHE(10). The first-write
flush guard returned before WRITE(10) submission. That conclusion follows from
the firmware control flow and cached response, not a physical USB trace.
I made no retry; USB unmounted successfully. SD stayed unmounted. ELKS remains
running with the adapter fault latched until PicoMEM restarts.

[Native screenshot and evidence](https://github.com/l00nix/picomem-elks/blob/codex/usb-sync-lifetime/docs/USB-SYNC-LIFETIME.md#request-sense-ppc512-result).

## Next investigation

The next step is a bounded read-only query of the drive's current caching mode
before considering compatibility behavior. Unsupported flush alone does not prove
that writes bypass volatile cache, and an unsupported/missing cache-mode page must
remain unknown. No fallback or cache-setting change is implemented by this result.
The broader workload, Mac filename/content/FAT check, return trip, DOS compatibility,
and timing/stack checks remain gated on resolving this failure. This diagnostic
alone does not make the firmware a read/write release candidate.

Automatic controller cancellation/reset recovery is still open. In the pinned
RP2040/RP2350 host driver, `hcd_edpt_abort_xfer` is unimplemented and returns false.
Restart-based containment is the scope of this candidate. Broader device support,
throughput/stack measurements and power-loss semantics remain release questions.
The original upstream metadata-only draft PR is unchanged.

## Protocol references

- [USB Mass Storage Bulk-Only Transport 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf): command/data/status phases and CSW validity.
- [T10 SBC-3 draft material](https://t10.org/ftp/t10/document.05/05-344r0.pdf): SYNCHRONIZE CACHE(10) command fields.
- [T10 cache synchronization clarification](https://www.t10.org/ftp/t10/document.03/03-388r2.pdf): IMMED and SYNC_NV behavior.
