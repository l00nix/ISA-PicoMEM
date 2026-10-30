/* SPDX-License-Identifier: MIT
 * Synchronous FatFs facade with firmware-owned USB storage. A late transfer can
 * touch only this static buffer, never a returned caller buffer. On any ambiguous
 * completion the buffer and device are quarantined until MCU restart. This is
 * containment, not USB cancellation/reset recovery or power-loss atomicity.
 */
#include "tusb.h"
#include "class/msc/msc_host.h"
#include "ff.h"
#include "diskio.h"
#include "usb-checked.h"
#include "../usb.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"

#if defined(PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE) && !defined(PICOMEM_EXPERIMENTAL_USB_SYNC)
#error "Cache probe requires checked USB transport"
#endif
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
static void cache_probe_reset(void);
#endif
#define SECTOR_BYTES 512u
#define COMMAND_TIMEOUT_US 2000000u
static uint8_t transfer_data[SECTOR_BYTES] __attribute__((aligned(4)));
static msc_cbw_t request;
static uint32_t tag;
static uint8_t device;
static bool active, failed, dirty, sync_supported;
static bool waiting;
static uint8_t fault_opcode;
/* Observation only, for the initial clean no-data flush timeout. The main USB
 * task already services the outstanding transfer; this does not pump/retry it. */
#define LATE_WINDOW_US 30000000u
static bool late_observing;
static uint8_t late_device;
static uint32_t late_tag, late_started;
static void late_detached(void);
/* A single diagnostic after a completed, clean pre-write flush failure. These
 * objects have separate lifetimes; neither can be reused after quarantine. */
#define SENSE_BYTES 18u
static uint8_t sense_data[SENSE_BYTES] __attribute__((aligned(4)));
static msc_cbw_t sense_request;
static bool sense_pending, sense_waiting;
static uint8_t sense_device, sense_length;
static const char *sense_result;
/* First fault only, visible through the existing cached CMD_USB_Status query.
 * Formatting does not submit USB work or clear quarantine. */
enum fault_kind { FAULT_REPLY=1, FAULT_STATUS, FAULT_RESIDUE, FAULT_STATE,
                  FAULT_SUBMIT, FAULT_TIMEOUT, FAULT_DETACH, FAULT_IO };
static void fault(unsigned kind, unsigned status, uint32_t residue)
{
    if (failed) return;
    failed = true;
    if (device && device <= CFG_TUH_DEVICE_MAX)
        usb_set_status(device, "?USB fault op=%02x f=%u s=%u r=%lu d=%u",
                       (unsigned)fault_opcode, kind, status,
                       (unsigned long)residue, (unsigned)dirty);
}

static bool present(void)
{
    /* TinyUSB's mounted/capacity helpers index daddr-1 without bounds checks. */
    return device > 0 && device <= CFG_TUH_DEVICE_MAX && tuh_msc_mounted(device);
}

bool pm_usb_accepting(void) { return !device && !active && !failed; }
uint8_t pm_usb_selected_device(void) { return device; }
bool pm_usb_faulted(void) { return failed; }
bool pm_usb_attach(uint8_t dev)
{
    if (!pm_usb_accepting() || !dev || dev > CFG_TUH_DEVICE_MAX ||
        !tuh_msc_mounted(dev) || tuh_msc_get_block_size(dev, 0) != SECTOR_BYTES ||
        !tuh_msc_get_block_count(dev, 0)) return false;
    device = dev;
    dirty = sync_supported = false;
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
    cache_probe_reset();
#endif
    return true;
}
void pm_usb_detach(uint8_t dev)
{
    if (dev != device) return;
    late_detached();
    if (active || dirty) fault(FAULT_DETACH, 255, 0);
    device = 0;
    sync_supported = false;
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
    if (!failed) cache_probe_reset();
#endif
    /* Do not release or repurpose transfer_data, request, or tag while failed. */
}

/* Diagnostics only: each MODE SENSE variant owns storage permanently. No MODE
 * SELECT, cache policy change, original-command retry, or quarantine release. */
#define CACHE_BYTES 64u
static uint8_t cache_data[2][CACHE_BYTES] __attribute__((aligned(4)));
static msc_cbw_t cache_request[2];
static const char *cache_result[2] = {"-", "-"};
static unsigned cache_index, cache_length;
static bool cache_waiting, cache_completed;
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
static bool probe_attempted;
static unsigned probe_lines;
static char probe_report[8][80];
static unsigned probe_lengths[2], probe_actual[2], probe_status[2], probe_residue[2];
static unsigned probe_device;
static const char *probe_wce = "unknown";
static uint8_t probe_bytes[2][CACHE_BYTES];
static void cache_probe_reset(void)
{
    /* Only clean attach/detach clears evidence; failed sessions stay latched. */
    probe_attempted = false;
    probe_lines = 0;
    memset(probe_report, 0, sizeof(probe_report));
    memset(probe_lengths, 0, sizeof(probe_lengths));
    memset(probe_bytes, 0, sizeof(probe_bytes));
    for (unsigned i = 0; i < 2; ++i) {
        probe_actual[i] = probe_status[i] = probe_residue[i] = 255;
        cache_result[i] = "-";
    }
}
bool pm_usb_cache_probe_status(char *buffer, size_t capacity)
{
    if (!buffer || !probe_lines) return false;
    snprintf(probe_report[0], sizeof(probe_report[0]),
        "USB cache probe: dev=%u WCE=%s I/O=%s", probe_device, probe_wce,
        failed ? "quarantined" : "read-only");
    size_t needed = 1;
    for (unsigned i = 0; i < probe_lines; ++i) needed += strlen(probe_report[i]) + 1;
    if (capacity < needed) return false; /* Atomic: never emit a truncated frame. */
    buffer[0] = (char)probe_lines;
    size_t end = 1;
    for (unsigned i = 0; i < probe_lines; ++i) {
        size_t n = strlen(probe_report[i]) + 1;
        memcpy(buffer + end, probe_report[i], n);
        end += n;
    }
    return true;
}
#endif

/* Latest completed cache-query text, independent of device close/late callbacks.
 * Three base lines plus three optional HCD lines, each <=79 bytes.
 * Appending respects CMD_USB_Status's existing eight-line/1024B client.
 */
static char transport_lines[6][80];
/* Freeze a timeout snapshot once. Later USB callbacks cannot rewrite it. */
static void host_transport_report(const tuh_msc_pending_t *p)
{
    if (!p->host_valid) return;
    const pm_usb_host_state_t *h = &p->host;
    snprintf(transport_lines[3], sizeof(transport_lines[3]),
        "HCD slot=%u act=%u rem=%u got=%u sof=%lu/%lu v=%u",
        h->slot, h->active, h->remaining, h->transferred,
        (unsigned long)p->host_start_frame, (unsigned long)h->frame, p->host_start_valid);
    snprintf(transport_lines[4], sizeof(transport_lines[4]),
        "HCD bc=%08lx ec=%08lx poll=%08lx addr=%08lx sc=%08lx",
        (unsigned long)h->buffer_ctrl, (unsigned long)h->endpoint_ctrl,
        (unsigned long)h->poll_ctrl, (unsigned long)h->address_ctrl, (unsigned long)h->sie_ctrl);
    snprintf(transport_lines[5], sizeof(transport_lines[5]),
        "HCD ss=%08lx bs=%08lx irq=%08lx ie=%08lx sm=%08lx",
        (unsigned long)h->sie_status, (unsigned long)h->buffer_status,
        (unsigned long)h->ints, (unsigned long)h->inte, (unsigned long)h->sm_state);
}
static void timeout_transport_report(void)
{
    tuh_msc_pending_t p;
    memset(transport_lines, 0, sizeof(transport_lines));
    if (!tuh_msc_get_pending(device, tag, &p)) {
        snprintf(transport_lines[0], sizeof(transport_lines[0]),
                 "USB timeout op=%02x tag=%lu snapshot=unavailable",
                 request.command[0], (unsigned long)tag);
        strcpy(transport_lines[1], "USB pending stage unknown");
        strcpy(transport_lines[2], "USB no validated completion at deadline");
        return;
    }
    snprintf(transport_lines[0], sizeof(transport_lines[0]),
             "USB timeout op=%02x tag=%lu stage=%u submit=%u",
             p.opcode, (unsigned long)p.tag, p.stage, p.submitted);
    snprintf(transport_lines[1], sizeof(transport_lines[1]),
             "USB pending ep=%02x want=%lu deadline_ms=2000",
             p.endpoint, (unsigned long)p.expected);
    snprintf(transport_lines[2], sizeof(transport_lines[2]),
             "USB last stage=%u event=%u ep=%02x got=%lu",
             p.last_stage, p.last_event, p.last_endpoint, (unsigned long)p.last_actual);
    host_transport_report(&p);
}
static void cache_transport_report(uint8_t dev, uint32_t request_tag)
{
    tuh_msc_checked_fault_t f;
    char hex[33];
    if (!tuh_msc_get_checked_reply(dev, request_tag, &f)) return;
    memset(transport_lines, 0, sizeof(transport_lines));
    unsigned n = f.sample_length;
    if (n > sizeof(f.sample)) n = sizeof(f.sample);
    for (unsigned i = 0; i < n; ++i) {
        static const char digits[] = "0123456789abcdef";
        hex[2*i] = digits[f.sample[i] >> 4];
        hex[2*i+1] = digits[f.sample[i] & 15];
    }
    hex[2*n] = 0;
    snprintf(transport_lines[0], sizeof(transport_lines[0]),
        "USB query op=%02x tag=%lu why=%u stage=%u event=%u",
        (unsigned)f.opcode, (unsigned long)f.tag, (unsigned)f.reason,
        (unsigned)f.stage, (unsigned)f.event);
    snprintf(transport_lines[1], sizeof(transport_lines[1]),
        "USB xfer ep=%02x/%02x got=%lu want=%lu data=%lu",
        (unsigned)f.endpoint, (unsigned)f.expected_endpoint,
        (unsigned long)f.actual, (unsigned long)f.expected, (unsigned long)f.data_bytes);
    snprintf(transport_lines[2], sizeof(transport_lines[2]),
        "USB CSW ck=%02x s=%u r=%lu n=%u b=%s",
        (unsigned)f.checks, (unsigned)f.csw_status, (unsigned long)f.residue, n, hex);
}

void pm_usb_append_transport_status(char *buffer, size_t capacity)
{
    /* Validate the existing count and NUL boundaries before any mutation.
     * Base three lines are atomic. Add all three HCD lines only if they fit. */
    if (!buffer || !capacity || !transport_lines[0][0]) return;
    unsigned count = (unsigned char)buffer[0];
    if (count > 5) return;
    size_t end = 1, needed = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (end >= capacity) return;
        const char *nul = memchr(buffer + end, 0, capacity - end);
        if (!nul) return;
        end = (size_t)(nul - buffer) + 1;
    }
    for (unsigned i = 0; i < 3; ++i) needed += strlen(transport_lines[i]) + 1;
    if (needed > capacity - end) return;
    unsigned lines = 3;
    if (count <= 2 && transport_lines[3][0]) {
        size_t extra = 0;
        for (unsigned i = 3; i < 6; ++i) extra += strlen(transport_lines[i]) + 1;
        if (extra <= capacity - end - needed) lines = 6;
    }
    for (unsigned i = 0; i < lines; ++i) {
        size_t length = strlen(transport_lines[i]) + 1;
        memcpy(buffer + end, transport_lines[i], length);
        end += length;
    }
    buffer[0] = (char)(count + lines);
}

static bool cache_complete(uint8_t dev, const tuh_msc_complete_data_t *reply)
{
    const msc_cbw_t *q = &cache_request[cache_index];
    if (!cache_waiting) return true;
    cache_result[cache_index] = "reply";
    if (dev == sense_device && device == sense_device && present() && reply &&
        reply->cbw && reply->csw && reply->user_arg == q->tag &&
        !memcmp(reply->cbw, q, sizeof(*q)) &&
        reply->scsi_data == cache_data[cache_index] &&
        reply->csw->signature == MSC_CSW_SIGNATURE &&
        reply->csw->tag == q->tag && reply->csw->data_residue <= CACHE_BYTES) {
        cache_transport_report(dev, q->tag);
        if (reply->csw->status <= 1) {
            tuh_msc_checked_fault_t f;
            /* Never derive length solely from residue: some successful MODE
             * replies have short DATA and residue zero. Require this command's
             * checked terminal record; parse only bytes both counts permit.
             * Raw status/residue stay intact for diagnostics. Missing/malformed
             * evidence cannot authorize parsing or the alternate query. */
            if (tuh_msc_get_checked_reply(dev, q->tag, &f) &&
                f.tag == q->tag && f.opcode == q->command[0] && !f.reason &&
                f.stage == 3 && f.event == 0 && f.actual == sizeof(msc_csw_t) &&
                f.expected == sizeof(msc_csw_t) && (f.endpoint & 0x80) &&
                f.endpoint == f.expected_endpoint && (f.checks & 31) == 31 &&
                f.csw_status == reply->csw->status &&
                f.residue == reply->csw->data_residue && f.data_bytes <= CACHE_BYTES &&
                (!f.csw_status || f.data_bytes == CACHE_BYTES - f.residue)) {
                cache_completed = true;
                cache_length = CACHE_BYTES - f.residue;
                if (cache_length > f.data_bytes) cache_length = f.data_bytes;
                cache_result[cache_index] = reply->csw->status ? "s1" : "ok";
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
                /* Snapshot only fully validated completion bytes, never a
                 * possibly live controller-owned buffer after a timeout. */
                probe_lengths[cache_index] = cache_length;
                probe_actual[cache_index] = f.data_bytes;
                probe_status[cache_index] = f.csw_status;
                probe_residue[cache_index] = f.residue;
                memcpy(probe_bytes[cache_index], cache_data[cache_index], cache_length);
#endif
            }
        } else {
            cache_result[cache_index] = "phase";
        }
    }
    cache_waiting = false;
    return true;
}

/* Parse one complete caching page, honoring header and descriptor lengths. A
 * missing/short/malformed page never becomes WCE=0. No policy uses this result. */
static const char *cache_parse(const uint8_t *b, unsigned n, bool ten)
{
    unsigned header = ten ? 8 : 4, total, descriptors, offset, page_size;
    if (n < header) return "short";
    total = ten ? ((unsigned)b[0] << 8 | b[1]) + 2 : (unsigned)b[0] + 1;
    if (total < header || total > n) return "length";
    descriptors = ten ? (unsigned)b[6] << 8 | b[7] : b[3];
    /* DBD=1 is requested. If ignored, handle only standard eight-byte block
     * descriptors. Long-LBA descriptors and unexpected media are left unknown. */
    if ((ten && (b[4] & 1)) || b[ten ? 2 : 1] || descriptors % 8 ||
        descriptors > total - header) return "header";
    offset = header + descriptors;
    if (total - offset < 2) return "page";
    if ((b[offset] & 0x7f) != 8) return "page"; /* SPF=0; ignore PS only. */
    page_size = (unsigned)b[offset + 1] + 2;
    if (page_size < 20 || page_size != total - offset) return "length";
    return b[offset + 2] & 4 ? "on" : "off";
}

/* True permits ONE alternate MODE SENSE variant: only after a fully checked
 * completed command, with unknown cache state. Ambiguous transport stops here. */
static bool collect_cache(unsigned index)
{
    uint32_t deadline;
    msc_cbw_t *q = &cache_request[index];
    cache_index = index;
    cache_completed = false;
    cache_result[index] = "skip";
    if (!present() || device != sense_device || !tuh_msc_ready(device) ||
        tag == UINT32_MAX) return false;
    memset(q, 0, sizeof(*q));
    memset(cache_data[index], 0, CACHE_BYTES);
    q->signature = MSC_CBW_SIGNATURE;
    q->tag = ++tag;
    q->total_bytes = CACHE_BYTES;
    q->dir = 0x80;
    q->cmd_len = index ? 10 : 6;
    q->command[0] = index ? 0x5a : 0x1a;
    q->command[1] = 8; /* DBD=1, no block descriptors requested. */
    q->command[2] = 8; /* PC=0 (current), caching page 08, subpage=0. */
    q->command[index ? 8 : 4] = CACHE_BYTES;
    cache_waiting = true;
    deadline = time_us_32() + COMMAND_TIMEOUT_US;
    if (!tuh_msc_scsi_command(device, q, cache_data[index], cache_complete, q->tag)) {
        cache_result[index] = "submit";
        cache_waiting = false;
        return false;
    }
    while (cache_waiting) {
        tuh_task();
        if (!cache_waiting) break;
        if (!present()) { cache_result[index] = "detach"; break; }
        if ((int32_t)(time_us_32() - deadline) >= 0) {
            cache_result[index] = "time";
            break;
        }
    }
    cache_waiting = false;
    if (!cache_completed) return false;
    if (!strcmp(cache_result[index], "ok"))
        cache_result[index] = cache_parse(cache_data[index], cache_length, index != 0);
    return strcmp(cache_result[index], "on") && strcmp(cache_result[index], "off");
}

#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
static void cache_probe(void)
{
    /* Exactly once per clean attachment, on an explicit attempted write.
     * No background USB work, flush, MODE SELECT, retry or sector write. */
    probe_attempted = true;
    active = true;
    sense_device = device;
    probe_device = device;
    bool alternate = collect_cache(1);
    if (!cache_completed) {
        fault_opcode = 0x5a;
        fault(FAULT_REPLY, 255, 0);
    } else if (alternate) {
        (void)collect_cache(0);
        if (!cache_completed) {
            fault_opcode = 0x1a;
            fault(FAULT_REPLY, 255, 0);
        }
    }
    /* A checked completed command can leave cache policy unknown. No result,
     * including validated WCE=0, enables writes in this diagnostic candidate. */
    const char *state = "unknown";
    if (!failed) for (unsigned i = 0; i < 2; ++i) {
        if (!strcmp(cache_result[i], "off")) state = "disabled";
        if (!strcmp(cache_result[i], "on")) state = "enabled";
    }
    probe_wce = state; /* The cached-status function supplies current I/O state. */
    unsigned line = 1;
    for (int i = 1; i >= 0; --i) {
        snprintf(probe_report[line++], 80,
            "MODE%d %s actual=%u used=%u status=%u residue=%u", i ? 10 : 6,
            cache_result[i], probe_actual[i], probe_lengths[i], probe_status[i], probe_residue[i]);
        for (unsigned offset = 0; offset < probe_lengths[i]; offset += 32) {
            char *out = probe_report[line++];
            unsigned prefix = (unsigned)snprintf(out, 80, "M%d +%02u ", i ? 10 : 6, offset);
            unsigned n = probe_lengths[i] - offset;
            if (n > 32) n = 32;
            for (unsigned j = 0; j < n; ++j) {
                static const char hex[] = "0123456789abcdef";
                out[prefix + 2*j] = hex[probe_bytes[i][offset+j] >> 4];
                out[prefix + 2*j+1] = hex[probe_bytes[i][offset+j] & 15];
            }
            out[prefix+2*n] = 0;
        }
    }
    strcpy(probe_report[line++], "Diagnostic only: USB writes blocked; no flush command sent.");
    probe_lines = line; /* <=8; publish only the fully constructed cached frame. */
    if (!failed) active = false;
}
#endif

static bool sense_complete(uint8_t dev, const tuh_msc_complete_data_t *reply)
{
    if (!sense_waiting) return true; /* Late completion cannot change the report. */
    sense_result = "reply";
    if (dev == sense_device && device == sense_device && present() && reply &&
        reply->cbw && reply->csw && reply->user_arg == sense_request.tag &&
        !memcmp(reply->cbw, &sense_request, sizeof(sense_request)) &&
        reply->scsi_data == sense_data &&
        reply->csw->signature == MSC_CSW_SIGNATURE &&
        reply->csw->tag == sense_request.tag &&
        reply->csw->data_residue <= SENSE_BYTES) {
        sense_result = "status";
        if (reply->csw->status == 0) {
            /* The pinned transport checks actual DATA bytes against residue. */
            sense_length = SENSE_BYTES - reply->csw->data_residue;
            sense_result = "ok";
        }
    }
    sense_waiting = false;
    return true;
}

static void collect_sense(void)
{
    uint32_t deadline;
    uint8_t format, key, asc, ascq;
    sense_pending = false; /* Exactly one attempt; never recursive or retried. */
    sense_device = device;
    sense_result = "skip";
    if (!present() || !tuh_msc_ready(device) || tag == UINT32_MAX) goto report;
    memset(&sense_request, 0, sizeof(sense_request));
    memset(sense_data, 0, sizeof(sense_data));
    sense_request.signature = MSC_CBW_SIGNATURE;
    sense_request.tag = ++tag;
    sense_request.total_bytes = SENSE_BYTES;
    sense_request.dir = 0x80;
    sense_request.cmd_len = 6;
    sense_request.command[0] = 0x03; /* REQUEST SENSE, DESC=0, LUN=0. */
    sense_request.command[4] = SENSE_BYTES;
    sense_waiting = true;
    deadline = time_us_32() + COMMAND_TIMEOUT_US;
    if (!tuh_msc_scsi_command(device, &sense_request, sense_data,
                             sense_complete, sense_request.tag)) {
        sense_result = "submit";
        goto report;
    }
    while (sense_waiting) {
        tuh_task();
        if (!sense_waiting) break;
        if (!present()) { sense_result = "detach"; break; }
        if ((int32_t)(time_us_32() - deadline) >= 0) {
            sense_result = "timeout";
            break;
        }
    }
    if (strcmp(sense_result, "ok")) goto report;
    sense_result = "short";
    if (sense_length < 8) goto report;
    format = sense_data[0] & 0x7f;
    if (format == 0x70 || format == 0x71) {
        /* Both received and advertised length must include ASC/ASCQ. Only
         * this header is decoded; any trailing sense descriptors are ignored. */
        if (sense_length < 14 || sense_data[7] < 6) goto report;
        key = sense_data[2] & 15; asc = sense_data[12]; ascq = sense_data[13];
    } else if (format == 0x72 || format == 0x73) {
        key = sense_data[1] & 15; asc = sense_data[2]; ascq = sense_data[3];
    } else { sense_result = "format"; goto report; }
    sense_waiting = false;
    /* Query only current unsupported-opcode sense. Deferred errors or any
     * other reason do not authorize follow-up. Original failure stays latched. */
    if ((format == 0x70 || format == 0x72) && key == 5 && asc == 0x20 && !ascq) {
        /* Try MODE SENSE(10) first; never follow an ambiguous completion. */
        if (collect_cache(1)) (void)collect_cache(0);
        usb_set_status(sense_device,
            "?USB fault op=35 f=2 s=1 r=0 d=0 rs=%02x:%x/%02x/%02x n=%u c6=%s c10=%s",
            (unsigned)format, (unsigned)key, (unsigned)asc, (unsigned)ascq,
            (unsigned)sense_length, cache_result[0], cache_result[1]);
        return;
    }
    usb_set_status(sense_device,
        "?USB fault op=35 f=2 s=1 r=0 d=0 rs=%02x:%x/%02x/%02x n=%u",
        (unsigned)format, (unsigned)key, (unsigned)asc, (unsigned)ascq,
        (unsigned)sense_length);
    return;
report:
    sense_waiting = false;
    if (sense_device && sense_device <= CFG_TUH_DEVICE_MAX)
        usb_set_status(sense_device,
            "?USB fault op=35 f=2 s=1 r=0 d=0 rs=%s", sense_result);
}

static void late_report(const char *result, unsigned status, unsigned why, uint32_t elapsed)
{
    /* Preserve every original fault field. Observation never clears quarantine. */
    if (late_device && late_device <= CFG_TUH_DEVICE_MAX)
        usb_set_status(late_device,
            "?USB fault op=35 f=6 s=255 r=0 d=0 obs=%s os=%u why=%u ms=%lu",
            result, status, why, (unsigned long)(elapsed / 1000u));
}
static void late_finish(const char *result, unsigned status, unsigned why, uint32_t elapsed)
{
    late_observing = false;
    late_report(result, status, why, elapsed);
}
static void late_detached(void)
{
    if (late_observing) {
        uint32_t elapsed = time_us_32() - late_started;
        late_finish(elapsed >= LATE_WINDOW_US ? "none" : "detach", 255, 0, elapsed);
    }
}
void pm_usb_observe_late(void)
{
    if (!late_observing) return;
    uint32_t elapsed = time_us_32() - late_started;
    if (elapsed >= LATE_WINDOW_US) late_finish("none", 255, 0, elapsed);
    else if (device != late_device || !present()) late_finish("detach", 255, 0, elapsed);
}
static void late_complete(uint8_t dev, const tuh_msc_complete_data_t *reply)
{
    if (!late_observing) return;
    uint32_t elapsed = time_us_32() - late_started;
    if (elapsed >= LATE_WINDOW_US) {
        late_finish("none", 255, 0, elapsed);
        return;
    }
    tuh_msc_checked_fault_t f;
    memset(&f, 0, sizeof(f));
    /* A late callback is evidence only. Require original identity and a checked
     * terminal record, including raw status/residue agreement, to call it CSW. */
    bool valid = dev == late_device && device == late_device && present() && reply &&
        reply->cbw && reply->csw && reply->user_arg == late_tag &&
        !memcmp(reply->cbw, &request, sizeof(request)) && !reply->scsi_data &&
        reply->csw->signature == MSC_CSW_SIGNATURE && reply->csw->tag == late_tag &&
        tuh_msc_get_checked_reply(dev, late_tag, &f) && f.tag == late_tag &&
        f.opcode == 0x35 && f.stage == 3 && f.event == 0 && !f.reason &&
        f.actual == 13 && f.expected == 13 && (f.expected_endpoint & 0x80) &&
        f.endpoint == f.expected_endpoint && (f.checks & 31) == 31 &&
        !f.data_bytes && !f.residue && f.csw_status <= 1 &&
        f.csw_status == reply->csw->status && f.residue == reply->csw->data_residue;
    late_finish(valid ? "csw" : "bad", valid ? f.csw_status : 255, f.reason, elapsed);
}

static bool complete(uint8_t dev, const tuh_msc_complete_data_t *reply)
{
    if (failed) { late_complete(dev, reply); return true; } /* Never undo quarantine. */
    if (!active || !waiting || dev != device || !present() || !reply ||
        !reply->cbw || !reply->csw || reply->user_arg != tag ||
        memcmp(reply->cbw, &request, sizeof(request)) ||
        reply->scsi_data != (request.total_bytes ? transfer_data : NULL) ||
        reply->csw->signature != MSC_CSW_SIGNATURE || reply->csw->tag != tag)
        fault(FAULT_REPLY, 255, 0);
    else if (reply->csw->status != 0) {
        /* Only a completed no-data flush, before any writes, permits follow-up.
         * Failed READ/WRITE, dirty flush, phase errors and malformed transport
         * never submit another command. Keep the original fault latched. */
        sense_pending = request.command[0] == 0x35 && !dirty && !sync_supported &&
                        reply->csw->status == 1 && reply->csw->data_residue == 0;
        fault(FAULT_STATUS, reply->csw->status, reply->csw->data_residue);
    }
    else if (reply->csw->data_residue != 0)
        fault(FAULT_RESIDUE, 0, reply->csw->data_residue);
    waiting = false;
    return true;
}

static DRESULT command(uint8_t opcode, uint32_t sector)
{
    uint32_t deadline;
    if (failed) return RES_ERROR;
    fault_opcode = opcode;
    if (!present()) return RES_NOTRDY;
    if (!active || waiting || !tuh_msc_ready(device) || tag == UINT32_MAX) {
        fault(FAULT_STATE, 255, 0);
        return RES_ERROR;
    }
    memset(&request, 0, sizeof(request));
    request.signature = MSC_CBW_SIGNATURE;
    request.tag = ++tag; /* Unique across detach/reconnect; never wrap/reuse. */
    request.cmd_len = 10;
    request.command[0] = opcode;
    if (opcode != 0x35) {
        request.total_bytes = SECTOR_BYTES;
        request.dir = opcode == 0x28 ? 0x80 : 0;
        request.command[2] = sector >> 24;
        request.command[3] = sector >> 16;
        request.command[4] = sector >> 8;
        request.command[5] = sector;
        request.command[8] = 1; /* One sector: never exceed fixed owned buffer. */
    }
    /* 35h: IMMED=0, SYNC_NV=0, LBA=0, blocks=0: wait for whole-medium sync. */
    waiting = true;
    deadline = time_us_32() + COMMAND_TIMEOUT_US;
    if (!tuh_msc_scsi_command(device, &request,
            request.total_bytes ? transfer_data : NULL, complete, tag)) {
        fault(FAULT_SUBMIT, 255, 0); /* Never recycle a rejected request. */
        return RES_ERROR;
    }
    while (waiting && !failed) {
        tuh_task();
        if (!waiting || failed) break; /* A completed command wins at deadline. */
        if (!present()) fault(FAULT_DETACH, 255, 0);
        else if ((int32_t)(time_us_32() - deadline) >= 0) {
            timeout_transport_report();
            fault(FAULT_TIMEOUT, 255, 0);
            if (opcode == 0x35 && !dirty && !sync_supported) {
                late_device = device;
                late_tag = tag;
                late_started = time_us_32();
                late_observing = true;
                late_report("wait", 255, 0, 0);
            }
        }
    }
    if (sense_pending) collect_sense();
    return failed ? RES_ERROR : RES_OK;
}

static DRESULT begin(void)
{
    if (failed || active) return RES_ERROR;
    if (!present()) return RES_NOTRDY;
    active = true;
    return RES_OK;
}
static DRESULT end(DRESULT result)
{
    if (result == RES_OK) active = false;
    else fault(FAULT_IO, 255, 0); /* Preserve any earlier, more specific fault. */
    return result;
}
static DRESULT validate(const void *buf, LBA_t sector, UINT count)
{
    uint32_t capacity;
    if (failed || active) return RES_ERROR;
    if (!present()) return RES_NOTRDY;
    capacity = tuh_msc_get_block_count(device, 0);
    if (!buf || !count || count > UINT16_MAX || sector >= capacity ||
        count > capacity - sector) return RES_PARERR;
    return RES_OK;
}
DSTATUS usb_disk_status(void)
{
    if (!present()) return STA_NODISK | STA_NOINIT;
    return failed ? STA_NOINIT : 0;
}
DSTATUS usb_disk_initialize(void) { return usb_disk_status(); }
DRESULT usb_disk_read(BYTE *buf, LBA_t sector, UINT count)
{
    DRESULT r = validate(buf, sector, count);
    if (r != RES_OK || (r = begin()) != RES_OK) return r;
    while (count--) {
        r = command(0x28, sector++);
        if (r != RES_OK) break;
        memcpy(buf, transfer_data, SECTOR_BYTES);
        buf += SECTOR_BYTES;
    }
    return end(r);
}
DRESULT usb_disk_write(const BYTE *buf, LBA_t sector, UINT count)
{
    DRESULT r = validate(buf, sector, count);
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
    if (r != RES_OK) return r;
    if (!probe_attempted) cache_probe();
    return failed ? RES_ERROR : RES_WRPRT;
#else
    if (r != RES_OK || (r = begin()) != RES_OK) return r;
    /* Refuse unsupported flush before the first sector mutation on this device. */
    if (!sync_supported) {
        r = command(0x35, 0);
        if (r != RES_OK) return end(r);
        sync_supported = true;
    }
    while (count--) {
        memcpy(transfer_data, buf, SECTOR_BYTES);
        dirty = true;
        r = command(0x2a, sector++);
        if (r != RES_OK) break;
        buf += SECTOR_BYTES;
    }
    return end(r);
#endif
}
DRESULT usb_disk_ioctl(BYTE cmd, void *buf)
{
    DRESULT r;
    if (failed || active) return RES_ERROR;
    if (!present()) return RES_NOTRDY;
    if (cmd == CTRL_SYNC) {
#ifdef PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE
        return RES_WRPRT; /* No flush from any path in this diagnostic build. */
#else
        r = begin();
        if (r != RES_OK) return r;
        r = command(0x35, 0);
        if (r == RES_OK) { dirty = false; sync_supported = true; }
        return end(r);
#endif
    }
    if (!buf) return RES_PARERR;
    switch (cmd) {
    case GET_SECTOR_COUNT: *(DWORD *)buf = tuh_msc_get_block_count(device, 0); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buf = SECTOR_BYTES; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buf = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
