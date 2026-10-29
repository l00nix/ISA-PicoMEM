# Runtime validation preparation

I am testing the LFN rename candidate's stack use and latency before considering
a release candidate. This branch adds an opt-in stack reservation for PicoMEM 2;
it is not a new file-service protocol. It has been flashed with verified
readback after saving a verified full recovery image. ELKS boot and the first
18-batch runtime workload passed, including full-file checks after remounting.
The upstream metadata-only draft PR is unchanged.

## Compiler audit

An ARM GCC 15.3.1 release build with `-fstack-usage` found the following nested
legacy file-delete call path. These are individual compiled frames, not measured
runtime peaks:

| Function | Frame bytes |
| --- | ---: |
| `main` | 64 |
| `main_core0` | 40 |
| `PM_ExecCMD` | 152 |
| `ethdfs_docmd` | 8 |
| `process` | 616 |
| `delfiles` | 816 |
| `f_unlink` | 264 |
| `follow_path` | 152 |
| **Partial nested total** | **2112** |

The disassembled instruction listings for all eight functions match the
previously installed rename ELF exactly; the audit did not change their code.

That partial path exceeds the 2048-byte core-0 reservation before deeper FatFs,
USB calls or interrupt frames are included. The large `delfiles` frame serves
the legacy wildcard implementation even when deleting one exact filename.
The installed link map has unused space below the reserved core-0 stack in
SCRATCH_Y, so this finding alone does **not** establish observed corruption.
Successful earlier file tests also do not establish adequate stack margin.

The separate LFN handlers run before the legacy `process` function. Its
616-byte frame must not be added to their call paths. A complete bound requires
call-path analysis, indirect calls and interrupt nesting; `.su` files alone
cannot supply a runtime high-water measurement.

## Opt-in reservation

Configure the existing rename build with:

```sh
-DPICOMEM_EXPERIMENTAL_STACK_RESERVE=ON
```

This defines `PICO_STACK_SIZE=0x1000` and explicitly keeps
`PICO_CORE1_STACK_SIZE=0x800`. Core 0 uses the otherwise empty 4 KiB SCRATCH_Y
bank; core 1's SCRATCH_X also contains ISA code/data and must not grow with it.
No file-service or ROM descriptor changes are made. The option defaults off
and refuses other PicoMEM board variants.

Check the built map with:

```sh
python3 tools/check-stack-reserve.py build-arm-stack-reserve/picomem.elf.map
```

The candidate builds and passes this map check. With the option disabled, the
existing rename build still produces its tested UF2 SHA-256:
`79055e63295aad3901d23ff17a93882ec3cf3cc8c5dd4702c36a5205626e31c8`.
Reserving 4 KiB is not proof that every firmware path fits. Runtime stack
high-water/guard checks remain required before a release-candidate claim.

## PPC512 results — October 29, 2026

The firmware booted ELKS with the existing rename kernel. The USB drive mounted
read/write and passed 18 host-timed batches: reads, writes, rename pairs,
create/write/delete and directory create/remove, plus no-op baseline batches.
The new payload and five earlier fixtures passed full host reads afterward
and again after clean unmount/remount. No SD writes or kernel changes were
made. Firmware UF2 SHA-256:
`4e144a3ed4f1a40db9da79043fe4660fe9e0c4f8a71253f962622a2e46efef13`.

[PPC timings, screenshot and validation data](https://github.com/l00nix/picomem-elks/blob/codex/runtime-validation/docs/RUNTIME-VALIDATION.md).
These are end-to-end timings including telnet/TTY overhead; the no-op batch
alone took about one second. Actual peak stack use and firmware-only service
timings are still unmeasured. This is not a release candidate or a power-loss
recovery test.

## Latency

The ELKS file-service transport masks interrupts while waiting for PicoMEM.
Its tick-based clock may therefore undercount elapsed time. The companion
ELKS `pmbench` tool uses host-timed batches, with no-op batches to expose
transport overhead. These give end-to-end latency, not pure firmware service
time. Firmware-clock service timing remains a separate measurement to add.

Hardware tests and any flash require the saved recovery image and physical
coordination. No SD writes are part of this work.
