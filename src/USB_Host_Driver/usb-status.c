/* SPDX-License-Identifier: MIT */
#ifdef PICOMEM_EXPERIMENTAL_USB_SYNC
#include "tusb.h"
#include "usb.h"
#include "msc/usb-checked.h"
#include <stdio.h>
#include <string.h>

/* TinyUSB assigns hubs addresses above CFG_TUH_DEVICE_MAX. Enumerate only
 * configured addresses, on core 0 without pumping USB or fetching descriptors.
 * Keep the existing CMD 61 framing and its eight-line/80-column limits. */
_Static_assert(CFG_TUH_DEVICE_MAX + CFG_TUH_HUB <= 7,
               "USB inventory must fit seven device rows plus its header");

static bool append_line(char *buffer, size_t capacity, size_t *end,
                        const char *line)
{
    size_t length = strlen(line) + 1;
    if ((unsigned char)buffer[0] >= 8 || length > capacity - *end)
        return false;
    memcpy(buffer + *end, line, length);
    *end += length;
    ++buffer[0];
    return true;
}

void usb_build_inventory(char *buffer, size_t capacity)
{
    if (!buffer || !capacity) return;
    buffer[0] = 0;
    bool mounted[CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1] = {false};
    unsigned devices = 0, hubs = 0;
    for (unsigned d = 1; d <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; ++d) {
        mounted[d] = tuh_mounted(d);
        if (mounted[d]) {
            if (d > CFG_TUH_DEVICE_MAX) ++hubs;
            else ++devices;
        }
    }
    char line[DEV_DESC_SIZE];
    size_t end = 1;
    snprintf(line, sizeof(line), "   USB  : %u device%s, %u hub%s",
             devices, devices == 1 ? "" : "s", hubs, hubs == 1 ? "" : "s");
    line[1] = (char)0xfe; /* Existing BIOS highlight marker. */
    if (!append_line(buffer, capacity, &end, line)) return;

    uint8_t selected = pm_usb_selected_device();
    bool blocked = pm_usb_faulted();
    /* Hubs first, then downstream devices in address order. During a fault,
     * retain hub counts in the header but omit hub detail rows, reserving the
     * same diagnostic capacity as the old device-only report. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        if (!pass && blocked) continue;
        unsigned first = pass ? 1 : CFG_TUH_DEVICE_MAX + 1;
        unsigned last = pass ? CFG_TUH_DEVICE_MAX : CFG_TUH_DEVICE_MAX + CFG_TUH_HUB;
        for (unsigned d = first; d <= last; ++d) {
            if (!mounted[d]) continue;
            /* Preserve the selected device's bounded fault text verbatim.
             * VID/PID and port are shown while healthy, not squeezed into a
             * fault line that may already use the full display width. */
            if (blocked && selected == d && d <= CFG_TUH_DEVICE_MAX &&
                !strncmp(dev_message[d - 1] + 3, "?USB fault ", 11)) {
                snprintf(line, sizeof(line), "%.79s", dev_message[d - 1]);
            } else {
                uint16_t vid = 0, pid = 0;
                bool ids = tuh_vid_pid_get(d, &vid, &pid);
                tuh_bus_info_t bus = {0};
                bool route = tuh_bus_info_get(d, &bus);
                bool hub = d > CFG_TUH_DEVICE_MAX;
                /* MSC helpers must never receive hub addresses. */
                bool disk = !hub && tuh_msc_mounted(d);
                const char *kind = hub ? "Hub" : disk ? "Disk" : "Device";
                const char *state = disk ? (blocked ? "blocked" :
                    selected == d ? "selected" : "inactive") : "enumerated";
                char id[10], location[24];
                if (ids) snprintf(id, sizeof(id), "%04x:%04x", vid, pid);
                else strcpy(id, "????:????");
                if (!route) strcpy(location, "route unknown");
                else if (bus.hub_addr)
                    snprintf(location, sizeof(location), "hub %u port %u",
                             bus.hub_addr, bus.hub_port);
                else snprintf(location, sizeof(location), "root %u", bus.rhport);
                snprintf(line, sizeof(line), "%u: %s %s %s %s",
                         d, kind, id, location, state);
            }
            if (!append_line(buffer, capacity, &end, line)) return;
        }
    }
}
#endif
