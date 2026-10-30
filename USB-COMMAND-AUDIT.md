# USB flush command audit — 30 October 2026

I checked the current adapter against USB Bulk-Only Transport (BOT) 1.0 and
Linux v6.12, then joined the actual adapter and patched TinyUSB MSC implementation
in a host test. **I found no encoding or command-to-status transition defect in
these checks. This is not a hardware fix or a read/write release candidate.**
No firmware, kernel, flash image or hardware state changes in this milestone.

## Command and transport findings

The adapter emits this 31-byte command wrapper for tag 1, LUN 0:

```text
55 53 42 43  01 00 00 00  00 00 00 00  00 00 0a
35 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

The CDB length is 10, opcode is `35`, IMMED and SYNC_NV are clear, and LBA and
block count are zero. The remaining six wrapper CDB bytes are zero padding.
Linux's `sd_sync_cache()` also uses a zero-filled command with this opcode for
whole-medium synchronization. In `scsi_prepare_cmd()`, a zero-byte request gets
`DMA_NONE`; `usb_stor_Bulk_transport()` then sets the wrapper direction to zero.
This agrees with the candidate. The CDB length and wrapper length are different
fields: 10 and 31 respectively.

BOT sections 3.3, 5.1 and 6.7.1 require zero DATA transfers when the wrapper's
data length is zero; the direction bit is then ignored by a compliant device.
The actual TinyUSB code skips DATA and submits a 13-byte status receive on bulk
IN. There is no zero-length data packet in the tested endpoint sequence. Changing
the direction bit or adding a dummy data stage is not a justified repair from
this evidence.

Reference sources:

- [USB BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf), sections 3.3, 5.1, 6.7.1.
- [Linux v6.12 sd.c](https://github.com/torvalds/linux/blob/v6.12/drivers/scsi/sd.c), `sd_sync_cache()` and `sd_read_cache_type()`.
- [Linux v6.12 scsi_lib.c](https://github.com/torvalds/linux/blob/v6.12/drivers/scsi/scsi_lib.c), `scsi_prepare_cmd()`.
- [Linux v6.12 transport.c](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/storage/transport.c), `usb_stor_Bulk_transport()`.

## What differs from a desktop driver

Linux uses cache-mode information and has a write-through fallback when cache
information is unavailable, unless a device setting requests write-back as the
default. It also has transport recovery and retry behavior. The candidate requires
a successful flush before the first sector write and quarantines failed I/O.
That policy can reject devices that a desktop OS accepts; it is separate from
long-name handling. I am not treating Linux's fallback assumption as evidence
that these particular drives have no volatile write cache.

BOT prescribes clear-halt and a further status receive after a stalled status
transfer. The current candidate instead quarantines it. That is a known
compatibility limitation, but the SanDisk observation was a missing completion,
not a delivered STALL event. Implementing recovery without explaining that
observation would not establish its cause or solve the buffer-lifetime problem.

The generic drive's `05/26/00` response is not proof that our CDB is malformed.
It is also not the `05/20/00` unsupported-opcode response that currently permits
cache-mode follow-up. The integration test retains that distinction.

## Joined adapter/TinyUSB tests

`tools/tests/usb-sync/wire-test.c` compiles the real adapter and real patched
TinyUSB MSC code together. Only endpoint scheduling/completions and device replies
are modelled. Independent byte-offset fixtures check the complete wrapper at the
host endpoint boundary, including signature, tag endianness, length, LUN, CDB and
padding. The model checks read/write payloads and endpoint direction, and rejects
any DATA stage for the flush command.

The 72 cases cover 18 scenarios at two device addresses/endpoint pairs and two
tag seeds. They include successful read/write/flush, explicit sync, both observed
failed-flush sense categories, header-only cache replies, timeout, successful late
status that must not release quarantine, malformed status, stalled or rejected
transfers, and dirty write/flush failures. Every failed scenario checks that later
read/write/sync calls submit no new commands. The successful cases verify that a
sector write follows a successful initial flush and that a final flush clears
the dirty state.

```sh
python3 tools/tests/usb-sync/test.py
```

All passed with AddressSanitizer and UndefinedBehaviorSanitizer:

- 72 joined adapter/TinyUSB endpoint cases.
- 19,268 existing TinyUSB completion cases.
- 191 adapter scenarios and 43 late-observation cases.
- Five real FatFs/mount scenarios, including independent content readback.
- 1,320 native snapshot-getter model cases.

These tests do not simulate the native RP2350 controller or physical USB traffic,
measure power-loss durability, or prove drive compliance. The first test run
needed a model correction: an idle task pump must be allowed after rejected
status submission. Production code was not changed. No ARM rebuild was needed
because only host tests and documentation changed.

## Next engineering step

I recommend a read-only cache-capability probe before the first flush, rather than
another speculative change to the flush CDB. It should retain bounded raw MODE
SENSE responses and distinguish validated WCE=0, WCE=1 and unknown results. A
future write-through path can be considered only for a validated disabled write
cache; enabled cache still requires working synchronization. Unknown, truncated,
malformed or failed replies must not silently enable writes. The probe and policy
need their own tests and hardware validation; they are not implemented here.

The existing fault remains latched on the PPC, with USB safely unmounted. No
hardware action is required for this audit. [Machine-readable evidence](USB-COMMAND-AUDIT.json).
