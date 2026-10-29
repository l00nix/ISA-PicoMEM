# USB cache synchronization and transfer lifetime candidate

I am testing a firmware-only follow-up to [file-service error reporting](FILE-SERVICE-ERRORS.md).
**Host tests and ARM builds pass; PPC512, Kingston and DOS validation is pending.**
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
tags are unique across clean reconnects and cannot wrap. Short replies are rejected;
this strict fixed-size policy needs testing against each supported drive.

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

The candidate uses 4,804,456 FLASH bytes and 380,700 RAM bytes: increases of 416
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

## Hardware validation still required

The PPC is currently in DOS with patched CMTEST/RMTDOS. Return to ELKS first,
verify the installed baseline and existing fixtures, then cleanly unmount USB
and shut down. After physical readiness is confirmed, save and readback-verify a
fresh full 16 MiB PicoMEM flash backup before flashing this candidate. Verify the
candidate's readback too. No kernel change is planned.

Start with detection and read-only checks, then one fresh bounded write to test
Kingston flush support. On any I/O/flush failure, stop; do not retry or unplug a
mounted device. Preserve the logs and use the verified recovery image if needed.
On success, run the 18 workload batches in a fresh `PMSYNC29` directory, measure
timing and observed stack use, compare all retained file bytes before/after clean
remount, perform the independent Mac filename/content and read-only FAT checks,
and test the ELKS return trip plus DOS 8.3 compatibility. SD stays unmounted.
There will be no real-media timeout, disconnect or power-loss fault injection.

Automatic controller cancellation/reset recovery is still open. In the pinned
RP2040/RP2350 host driver, `hcd_edpt_abort_xfer` is unimplemented and returns false.
Restart-based containment is the scope of this candidate. Broader device support,
throughput/stack measurements and power-loss semantics remain release questions.
The original upstream metadata-only draft PR is unchanged.

## Protocol references

- [USB Mass Storage Bulk-Only Transport 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf): command/data/status phases and CSW validity.
- [T10 SBC-3 draft material](https://t10.org/ftp/t10/document.05/05-344r0.pdf): SYNCHRONIZE CACHE(10) command fields.
- [T10 cache synchronization clarification](https://www.t10.org/ftp/t10/document.03/03-388r2.pdf): IMMED and SYNC_NV behavior.
