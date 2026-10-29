# Experimental file-service error reporting

I am testing a default-off correctness candidate on top of the
[runtime diagnostics branch](RUNTIME-DIAGNOSTICS.md). Host tests and the ARM build
pass. **All 18 PPC512 normal-workload batches and eight full-file checks after
remount passed.** Direct Mac filenames/contents and a read-only FAT check also
passed. Return-trip and DOS checks are pending for this build. It is
not a release candidate. The existing diagnostics ELKS kernel can consume these
errors; no kernel update or new wire opcode is needed.

## Changes

Enable `PICOMEM_EXPERIMENTAL_FILE_ERRORS` together with the existing rename and
4 KiB stack-reservation options. The PicoMEM 2 restriction still applies.

* DFS CLOSE (`0x06`) checks the two-byte payload and full 16-bit handle, then
  returns the actual `f_close` result as a DOS error. Normal success remains zero.
* A failed close leaves its handle allocated but inaccessible until DFS
  reinitialization. Repeated close returns the original error without further
  filesystem calls. Other commands cannot obtain that failed file object.
* OPEN checks `f_stat` before reading its output. A metadata failure closes the
  already-open object and returns an error; a failed cleanup close quarantines
  that object too.
* The USB disk adapter reports rejected submissions, nonzero SCSI completion
  status, nonzero data residue and the existing two-second timeout as errors.
  A failure latches USB I/O off until firmware restart. Reconnection or a late
  successful completion cannot clear that latch. Missing media and invalid
  buffer/count arguments are rejected before submission.

The checked close still calls FatFs `f_close`, including its existing `f_sync`.
No synchronization was removed and no throughput improvement is claimed.
Quarantine matters because this FatFs version can clear `FA_MODIFIED` after a
failed `sync_fs`: simply retrying close could then report success without retrying
that failed flush. The 16 handle slots remain bounded; failure recovery is a
restart, not handle reuse or an automatic retry.

DOS result examples: invalid handle 6, invalid payload 13, write protected 19,
not ready 21, general disk/internal failure 31, locked 33. Readiness/media
absence is distinct from an accepted transfer that later fails. Error returns
are not rollback: data or directory entries may already have changed.

## Scope and remaining limits

The USB adapter is shared by DFS and USB disk-image access, including DOS. Its
failure latch therefore stops all subsequent USB sector operations, not just
one mounted ELKS file. SD sector I/O is unchanged. The checked DFS CLOSE/OPEN
behavior applies to either DFS drive, but ELKS continues to prohibit SD writes.
Both `USB_Host_Driver2` and the older build wiring use this same MSC adapter.

`CTRL_SYNC` now refuses a faulted/busy/absent USB drive, but **still does not send
SCSI SYNCHRONIZE CACHE**. Successful calls establish only the driver's current
completion semantics, not persistence through device-cache loss or power loss.
The existing timeout also does not cancel an outstanding TinyUSB transfer.
Stopping new I/O prevents reuse by this adapter but does not establish safe
buffer ownership for a late transfer. Cancellation, USB transport validation,
device-cache semantics and recovery remain release blockers. No disconnect or
power-loss test is authorized on the real Kingston by these host tests.

The existing mount callback also has an assignment in its single-device check.
That separate multi-device issue is unchanged; this candidate is for the current
single Kingston setup. Host compiler warnings from that existing code are
recorded, not hidden by changes to unrelated behavior.

## Validation

Run from the repository root with a C compiler supporting ASan/UBSan and mtools:

```sh
python3 src/lib/my_etherdfs/tests/file-errors/test.py
sh src/lib/my_etherdfs/tests/file-errors/usb-test.sh
sh src/lib/my_etherdfs/lfn/test.sh
sh src/lib/my_etherdfs/diag/test.sh
```

The FAT32 tests use the actual DFS dispatcher, handle pool and FatFs. Five fresh,
disposable 64 MiB images cover normal close/remount/readback, data-write failure,
CTRL_SYNC failure, post-open metadata failure and failed cleanup close. Tests
also reject malformed CLOSE lengths/handles and confirm no hidden retry or
reuse of failed handles. Normal contents are independently read with `mcopy`.
Only host-image disk functions and the targeted metadata call inject failures.

Fourteen USB-adapter cases compile the production adapter with a deterministic
TinyUSB transport stub: read and write each cover absent/argument/busy guards,
normal completion, SCSI error, short completion, rejected submission, timeout
across timer wrap, and disconnect while waiting. Late-success and reconnect
checks confirm the fault stays latched. These are transport-model tests, not
physical USB/DMA or device-cache tests.

The ELKS host suite additionally tests five CLOSE error codes after a write.
The current kernel returns an error without advancing the file position/size,
marks its file-service session broken, and refuses subsequent writes without
sending another command. These tests do not claim earlier disk writes are undone.

All listed tests passed on October 29, 2026, as did the ARM build and 4 KiB
stack-layout check. The disabled-option build is byte-identical to the installed
diagnostics baseline. See [artifact hashes](FILE-SERVICE-ERRORS.json).

## Build and hardware plan

Use the same pinned SDK/toolchain as the diagnostics build, in a separate build
directory. Add `-DPICOMEM_EXPERIMENTAL_FILE_ERRORS=ON` to its CMake invocation;
keep LFN, creation, rename, stack reservation and diagnostics enabled. The build
uses 4,804,040 bytes of FLASH and 380,188 bytes of RAM, with unchanged scratch
regions (3748 / 4096 bytes). The new persistent state accounts for 20 RAM bytes.

Before flashing, cleanly unmount USB and shut down ELKS, then save and verify a
new full-flash recovery image in BOOT mode. Verify the candidate after writing.
Keep the existing kernel and utilities. Run the bounded workload in a fresh
`PMERR29` directory, record `pmperf` and compare every retained file byte before
and after clean remount. Keep SD unmounted. Follow with independent Mac FAT
checking and DOS legacy alias/create/write/close compatibility. Injected failures
stay on disposable host images; real-media destructive recovery is separate work.

Open and close accounted for 57.4% of the preceding workload's firmware service
time. OPEN still performs both `f_open` and `f_stat`; CLOSE still flushes. Any
optimization needs separate metadata/identity and persistence evidence.

## PPC512 validation — October 29, 2026

I saved/verified the full recovery image, flashed/verified this firmware and kept
the existing diagnostics kernel. The Kingston mounted read/write normally. All
18 bounded workload batches in fresh `PMERR29` passed. Seven old fixtures and the
new 3073-byte payload matched every byte before and after clean remount. SD was
not mounted. No physical fault injection was performed.

The observed firmware stack peak was 2812/4096 bytes; the ELKS workload peak was
354/700 bytes (426 initially before reset). Diagnostic flags and invalid samples
were zero. These observations do not prove worst-case bounds or power-loss
persistence. Direct Mac reads verified all eight exact long filenames and file
contents. After clean unmount, `diskutil verifyVolume` ran `fsck_msdos -n` and
reported exit code 0. The Kingston was safely ejected. Return-trip remount and
DOS compatibility remain pending.

[Hardware evidence and screenshot](https://github.com/l00nix/picomem-elks/blob/codex/file-service-errors/docs/FILE-SERVICE-ERRORS.md#ppc512-results--october-29-2026).
