# Cache-query timeout stage diagnostic

I built this firmware-only diagnostic after the generic USB drive reproduced a
MODE SENSE(10) timeout both through a powered hub and directly on PicoMEM.
[Previous hardware comparison](USB-CACHE-PROBE.md#direct-connection-comparison).
**This candidate is built and tested, but has not been flashed or validated on
hardware. USB writes remain blocked. It is not a read/write release.**

## What changes

At the cache query's existing two-second deadline, I now capture the matching
pending-transfer snapshot and the existing native RP USB-controller snapshot.
The getter reads state and registers; it sends no command, services no USB task,
reads no endpoint payload, and performs no reset or retry. IRQ exclusion stabilizes
software state while registers are read, but hardware continues progressing; this
is not an atomic physical-bus trace.

The existing `pmstat --usb` returns a frozen report of at most eight rows. On a
cache-query timeout, the report contains:

1. Cache policy (`unknown`) and quarantined I/O state.
2. MODE SENSE(10)/(6) result labels and the writes-blocked/no-flush policy.
3. Opcode, tag, pending stage and submission state.
4. Endpoint, expected byte count and the 2000 ms deadline.
5. Last completion stage, event, endpoint and byte count.
6. Controller slot, active state, remaining/transferred counts and frame counters.
7. Buffer, endpoint, polling, address and SIE control registers.
8. SIE/buffer status, interrupt masks/status and RP2350 state-machine register.

Stages are `1` = command wrapper, `2` = data, `3` = status wrapper. Submission `1`
means the host driver accepted the transfer, not proof of physical traffic.
Last stage `0` / event `255` means there was no completion callback. Missing pending
or controller evidence is explicitly omitted/reported unavailable. No partial
DATA bytes or invented CSW status values are displayed on timeout.

Each row stays below 80 bytes including its NUL, and insufficient output capacity
leaves the destination untouched. The report is copied into independent storage
before returning. Late callbacks and unrelated diagnostic updates cannot change
it; later status reads perform no USB or register access. Valid completed replies
retain the previous raw-byte report.

This change does not alter the query order, deadline, command fields or alternate
query rules. A timeout still prevents MODE SENSE(6) fallback and quarantines USB
until PicoMEM restarts. All sector writes and flush commands remain blocked in
the diagnostic build, including after a completed cache-policy result. The probe
option remains OFF by default. No kernel or ELKS utility changes are needed.

## Validation

- 80 ASan/UBSan cache-probe scenarios cover both device addresses, command/data/status
  timeouts for both MODE SENSE variants, unavailable evidence and maximum register
  values. They test all output capacities through 1024 bytes, frozen reports after
  actual late model completions, and zero USB/register side effects from status reads.
- 12 real FAT32 scenarios use the actual adapter and TinyUSB code with modelled
  endpoints. No write or flush opcode is permitted, and every complete image retains
  its original SHA256 after the failed directory-creation attempt.
- Existing suites pass: 19,268 TinyUSB cases, 72 joined transport cases, 191 adapter
  cases, 43 late-observation cases, five FatFs cases, 1,320 native-controller getter
  cases, 10,845 inventory cases and 30 ELKS status-query cases.
- ARM builds with the probe ON and OFF pass. The shared timeout reporter now accepts
  the actual opcode for unavailable-snapshot reports; the OFF binary is not identical
  to its predecessor and uses eight fewer FLASH bytes. Its behavior is covered by
  the existing adapter/transport tests.
- Probe-ON memory: 4,814,320 FLASH bytes, 382,904 RAM bytes. Core 0 retains 4096 stack
  bytes and core 1 retains 2048, without scratch overlap. Runtime stack peak remains
  unmeasured. Existing newlib syscall-stub linker warnings remain.

[Source revision, artifact hashes and validation manifest](USB-CACHE-STAGE.json).

## Planned hardware test

After physical readiness confirmation, I will shut down ELKS, save and readback-
verify a fresh full recovery image, then flash and verify this candidate. The same
kernel and generic drive will be used, directly connected without the hub. After
read-only identity/content checks, one directory-creation attempt will trigger
the probe. I will capture the report and actual PPC screen, then unmount USB.
A timeout stops further media I/O until restart; status reads use only the saved
snapshot. Controller registers and stage information will narrow the investigation
but do not by themselves establish the electrical or protocol-level cause.
