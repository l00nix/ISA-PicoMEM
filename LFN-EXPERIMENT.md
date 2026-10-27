# Experimental long filenames for ELKS

I am developing this optional extension for my Amstrad PPC512 running ELKS,
with AI coding assistance. This branch adds read-only long-name metadata to
PicoMEM 2's DFS file service. It is not hardware validated or ready for merging.
The paired client lives in [picomem-elks](https://github.com/l00nix/picomem-elks/tree/codex/long-filename-prototype).

## Compatibility and limits

`PICOMEM_EXPERIMENTAL_LFN` defaults OFF. With it OFF, no handler or discovery
marker is built. With it ON, only DFS subfunction **0x70 inside outer command
0x89** is intercepted. This is a provisional experimental number, unrelated to
outer command 0x70; an upstream allocation is still needed. All legacy DFS
commands retain their existing processing.

The matching ELKS client first reads an exact 16-byte descriptor at offset
0x3fc0 of the D000 BIOS ROM: `50 4d 4c 4e 01 70 1a 34 6c 30 30 6e 69 78 00 00`.
It sends no new command when that descriptor is absent. This is a compatibility
marker, not authentication or a standard capability format. The ROM-stamping
script accepts only the audited 16 KiB `pm2bios.bin` SHA-256
`c63ff8fe9a34f43138e55c0c70cab5fe12fe960c9f156363706d2548d4a62c6f`,
checks unused FF padding, and adjusts the BIOS checksum. Source BIOS bytes are
not overwritten. The descriptor location needs maintainer review.

The response carries at most 26 ASCII bytes, matching ELKS's current directory
ABI. Spaces and mixed case are supported. Longer/Unicode names retain their
8.3 aliases; names are never truncated. The ELKS client retains aliases for
all legacy file operations, resolves sibling collisions before publication,
and enables long names only on read-only mounts. USB write mounts keep aliases.
This does not create or rename long filenames.

The adapter uses a separate FatFs directory cursor, scans at most 256 entries,
closes it on all completed-open paths and restores PSRAM access. It does not
change the legacy cursor or write media. Repeated per-entry scans can be slow;
PPC IRQ-off latency and USB/FAT32 timing still require measurement before
routine use. The legacy client caches at most 64 nodes per mount.

## Reproduce builds

The upstream checkout lists TinyUSB and XInput in `.gitmodules` but does not
record their gitlinks. `tools/prepare-lfn-deps.py` pins TinyUSB **0.19.0**
(`331c263400dd5ccf77264cab1fa6bbd9f374b44e`) and XInput
`ae4f3a9fd7c57a6c8c36fdba7f7b1abc9b6a1a56`. Its small, recorded XInput patch
converts the old descriptor-length callback to TinyUSB's bool callback and
removes the redundant `tinyusb` link as the upstream host-driver comment
requires. No dependency is silently upgraded. These USB dependencies also need
hardware validation; the original released firmware's exact dependency set
is not documented by this checkout.

Use Pico SDK **2.2.0**, including its cyw43-driver, lwIP, mbedTLS and BTstack
submodules, an Arm bare-metal GCC toolchain with newlib, CMake and Ninja.
I built both variants using Arm GNU Toolchain **15.3.rel2**, on macOS arm64.

```sh
python3 tools/prepare-lfn-deps.py
cmake -S src -B build-lfn -G Ninja \
  -DPICO_SDK_PATH=/path/to/pico-sdk-2.2.0 \
  -DPICO_TOOLCHAIN_PATH=/path/to/arm-none-eabi-toolchain \
  -DPICOMEM_EXPERIMENTAL_LFN=ON
cmake --build build-lfn
# Repeat in a separate build-default directory with the option OFF.
```

Default target is PicoMEM 2 / RP2350 with 16 MiB flash. Do not use this image on
PicoMEM 1.x. Building produces `picomem.uf2`; it does not flash anything.

## Validation so far

- Both complete default and LFN firmware builds pass. LFN adds 2,336 flash
  bytes; linked RAM remains 377,048 bytes. This is build fit, not a stack or
  runtime measurement.
- 498 payload/adapter checks and 10,000 malformed requests pass with ASan/UBSan.
- 692 dispatcher checks cover bounds, invalid framing, both drives and byte-for-
  byte passthrough of all other DFS command values.
- Tests using the actual FatFs source and a generated FAT12 image pass for
  USB/SD selection, nested alias reads, Unicode/length fallback and independent
  directory cursors. The test block layer records zero writes.
- ROM identity, padding, checksum and unaudited-source refusal tests pass.
- Paired 8086 kernel builds and boots without PicoMEM in QEMU. Its host tests
  cover long-name lookup/readdir, stock-ROM fallback, collisions and faults.

```sh
sh src/lib/my_etherdfs/lfn/test.sh
python3 src/lib/my_etherdfs/lfn/test-fatfs.py .
python3 tools/test-lfn-rom.py
```

Before hardware installation, save the actual installed flash image, preserve
ELKS's current kernel and PPC display extension, and use a small disposable
USB fixture read-only first. Firmware boot, DOS compatibility, real USB/SD
latency, unplug behavior and recovery remain untested. I am seeking review of
the capability advertisement and command allocation before treating this as a
stable interface.

The payload layout and statuses are defined in `src/lib/my_etherdfs/lfn/lfn.h`
and `lfn.c`; the DFS request/reply body is bounded to 135/52 bytes respectively.
