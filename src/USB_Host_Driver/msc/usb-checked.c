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

#define SECTOR_BYTES 512u
#define COMMAND_TIMEOUT_US 2000000u
static uint8_t transfer_data[SECTOR_BYTES] __attribute__((aligned(4)));
static msc_cbw_t request;
static uint32_t tag;
static uint8_t device;
static bool active, failed, dirty, sync_supported;
static bool waiting;
static uint8_t fault_opcode;
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
bool pm_usb_attach(uint8_t dev)
{
    if (!pm_usb_accepting() || !dev || dev > CFG_TUH_DEVICE_MAX ||
        !tuh_msc_mounted(dev) || tuh_msc_get_block_size(dev, 0) != SECTOR_BYTES ||
        !tuh_msc_get_block_count(dev, 0)) return false;
    device = dev;
    dirty = sync_supported = false;
    return true;
}
void pm_usb_detach(uint8_t dev)
{
    if (dev != device) return;
    if (active || dirty) fault(FAULT_DETACH, 255, 0);
    device = 0;
    sync_supported = false;
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

/* Latest completed cache-query text, independent of device close/late callbacks.
 * Three <=79-byte lines fit CMD_USB_Status's existing eight-line/1024B client.
 */
static char transport_lines[3][80];
static void cache_transport_report(uint8_t dev, uint32_t request_tag)
{
    tuh_msc_checked_fault_t f;
    char hex[33];
    if (!tuh_msc_get_checked_reply(dev, request_tag, &f)) return;
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
     * Append all three or none, never truncate or exceed the client limits. */
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
    for (unsigned i = 0; i < 3; ++i) {
        size_t length = strlen(transport_lines[i]) + 1;
        memcpy(buffer + end, transport_lines[i], length);
        end += length;
    }
    buffer[0] = (char)(count + 3);
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

static bool complete(uint8_t dev, const tuh_msc_complete_data_t *reply)
{
    if (failed) return true; /* Late success cannot undo quarantine. */
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
        else if ((int32_t)(time_us_32() - deadline) >= 0) fault(FAULT_TIMEOUT, 255, 0);
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
}
DRESULT usb_disk_ioctl(BYTE cmd, void *buf)
{
    DRESULT r;
    if (failed || active) return RES_ERROR;
    if (!present()) return RES_NOTRDY;
    if (cmd == CTRL_SYNC) {
        r = begin();
        if (r != RES_OK) return r;
        r = command(0x35, 0);
        if (r == RES_OK) { dirty = false; sync_supported = true; }
        return end(r);
    }
    if (!buf) return RES_PARERR;
    switch (cmd) {
    case GET_SECTOR_COUNT: *(DWORD *)buf = tuh_msc_get_block_count(device, 0); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buf = SECTOR_BYTES; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buf = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
