# USB hub and device inventory

I added explicit hub and downstream-device reporting to the experimental
`pmstat --usb` response. **The candidate is built and host-tested; hardware
validation is pending.** It uses the existing ELKS kernel and utility.

The firmware already enumerated devices behind hubs, but its status command
excluded hub addresses and printed an empty row for an unselected second disk.
The new formatter reads cached TinyUSB metadata on core 0. It does not fetch
USB descriptors, issue storage commands, pump the USB task, reset a device or
change which drive is selected.

The healthy report includes:

- Separate counts for downstream devices and hubs.
- A row for each configured hub, followed by downstream devices in address order.
- USB device address, numeric vendor/product IDs, and parent hub/port (or root).
- `Disk` for a mounted MSC interface; `Device` for other enumerated devices.
- `selected` for the file-service drive, `inactive` for other disks, or `blocked`
  when the USB fault remains latched. These describe firmware state, not an ELKS
  filesystem mount. Numeric IDs are not manufacturer/product strings.

Illustrative layout only; these IDs and port numbers are placeholders:

```text
   USB  : 2 devices, 1 hub
5: Hub 1111:2222 root 0 enumerated
1: Disk 3333:4444 hub 5 port 1 selected
2: Disk 5555:6666 hub 5 port 2 inactive
```

The current configuration supports **one hub and up to four downstream devices**.
The storage adapter still exposes **one USB volume at a time**. This change does
not select the second drive automatically, add a second mount, retry a fault or
change the cache-flush/write policy. An inactive disk must be re-enumerated in a
clean session to become the selected drive; status queries never select it.

## Bounds and fault reporting

The existing CMD 61 counted-NUL-string format is retained: at most eight lines
and 1024 response bytes. Each inventory line is below 80 bytes including its NUL.
The BIOS highlight marker is retained. The ELKS reader already understands this
format. A too-small output buffer receives only complete rows.

During a fault, hub counts remain visible but hub detail rows are omitted to
preserve the previous diagnostic capacity. The selected drive's original fault
line is retained instead of its normal identity row. A single downstream device
still leaves room for all six cached timeout/controller lines. With multiple
downstream devices, optional diagnostic lines can be omitted under the existing
eight-line rule. Detached addresses are excluded; stale messages are not used to
label a newly enumerated healthy or inactive disk.

## Validation

- 10,845 ASan/UBSan inventory scenarios: combinations of device presence, MSC
  class, selection, fault and missing metadata; all buffer capacities through
  600 bytes; address reuse; direct connection; hub addresses never passed to
  MSC helpers. The test links only cached metadata APIs, with no USB-transfer API.
- 19,268 actual TinyUSB cases, 191 adapter cases, 43 late-observer cases, 1,320
  native-getter mock-register cases and five real FatFs/FAT32 cases pass.
- The unchanged ELKS status reader passes its 30 host scenarios.
- Enabled and disabled ARM builds pass. Stack map reserves 4096 bytes on core 0
  and 2048 on core 1, with no overlap. Runtime stack peak is not yet measured.
- Candidate flash usage: 4,813,600 bytes; RAM: 382,088 bytes.

[Source and artifact manifest](USB-INVENTORY.json).

Hardware validation will use a fresh full recovery backup, verified flash, then
cached status checks with no device, hub plus one disk, and hub plus two disks.
Read-only checks will identify the selected volume. No guarded write attempt is
needed to test inventory. The upstream metadata-only draft PR is unchanged.
