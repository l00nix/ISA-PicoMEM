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
