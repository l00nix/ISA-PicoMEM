/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#include "main.h"
#include "tusb.h"
#include "../usb.h"
#include "ff.h"
#include "diskio.h"
#include <math.h>
#include "class/msc/msc_host.h"

// We are an 8-bit computer, confirm fatfs is too
static_assert(sizeof(TCHAR) == sizeof(char));

static FATFS msc_fatfs_volume;
static volatile bool msc_volume_busy=false;
static volatile bool msc_volume_error=false;
static scsi_inquiry_resp_t msc_inquiry_resp;
static uint8_t msc_dev_addr=0xFF;   // 0xFF no disk mounted
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
/* Do not continue using potentially inconsistent media after an I/O failure.
 * Deliberately not cleared by reconnect or a late completion; restart required.
 * This does not cancel a timed-out TinyUSB transfer or guarantee durability. */
static volatile bool msc_io_fault;
#endif

bool inquiry_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
    if (cb_data->csw->status != 0)
    {
        usb_set_status(dev_addr, "?MSC SCSI inquiry failed");
        return false;
    }

    const double block_count = tuh_msc_get_block_count(dev_addr, cb_data->cbw->lun);
    const double block_size = tuh_msc_get_block_size(dev_addr, cb_data->cbw->lun);
    const char *xb = "MB";
    double size = block_count * block_size / (1024 * 1024);
    if (size >= 1000)
    {
        xb = "GB";
        size /= 1024;
    }
    if (size >= 1000)
    {
        xb = "TB";
        size /= 1024;
    }
    size = ceil(size * 10) / 10;
    if (msc_dev_addr=0xFF)
    {
    usb_set_status(dev_addr, "USB Disk %.1f %s %.8s %.16s",
                   size, xb,
                   msc_inquiry_resp.vendor_id,
                   msc_inquiry_resp.product_id);
    char drive_path[3] = "1:";
    msc_dev_addr=dev_addr;
    FRESULT mount_result = f_mount(&msc_fatfs_volume, drive_path, 1);
    if (mount_result != FR_OK)
    {
        usb_set_status(dev_addr, "?MSC filesystem mount failed (%d)", mount_result);
        msc_dev_addr=0xFF;
        return false;
    }
    // If current directory invalid, change to root of this drive
    char s[2];
    if (FR_OK != f_getcwd(s, 2))
    {
        f_chdrive(drive_path);
        f_chdir("/");
    }
    //printf(">>Mount completed and successfull !!! :)\n");
    return true;
    }
    else 
    {
    usb_set_status(dev_addr, "USB %.1f %s Not Mounted",size, xb);        
    return true;
    }
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    uint8_t const lun = 0;
    usb_set_status(dev_addr, "USB Disk mounted, inquiring");
    tuh_msc_inquiry(dev_addr, lun, &msc_inquiry_resp, inquiry_complete_cb, 0);
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    /* Only tear down the filesystem for the drive we actually mounted.
     * A second MSC device (e.g. a USB hub with two drives) disconnecting
     * must not unmount the still-active drive. */
    if (dev_addr != msc_dev_addr)
        return;
            
    char drive_path[3] = "1:";
    f_unmount(drive_path);
    usb_set_status(dev_addr, "USB Disk Removed");
    msc_dev_addr=0xFF;
}

static void wait_for_disk_io(BYTE pdrv)
{
    /* 2-second timeout — prevents a hung or disconnected USB drive from
     * locking the firmware forever. 2 s is far longer
     * than any legitimate sector read should take over USB Full Speed. */
    uint32_t deadline = time_us_32() + 2000000u;    
//    printf(">> wait_for_disk_io \n");
    while (msc_volume_busy) {
        tuh_task();
        if ((int32_t)(time_us_32() - deadline) >= 0) {
       //     printf("disk_io: timeout waiting for USB transfer\n");
            msc_volume_busy = false;
            msc_volume_error = true;
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
            msc_io_fault = true;
#endif
            return;
        }        
    }
}

static bool disk_io_complete(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
    (void)cb_data;
    /* Propagate SCSI command status — non-zero CSW status means the drive
     * reported an error (e.g. medium error, illegal request). */
    msc_volume_error = (cb_data->csw->status != 0);
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    /* A nominally successful but short command is not a full sector transfer. */
    if (cb_data->csw->data_residue != 0) msc_volume_error = true;
    if (msc_volume_error) msc_io_fault = true;
#endif
 //   if (msc_volume_error)
 //       printf("disk_io: SCSI error status %u\n", cb_data->csw->status);    
    msc_volume_busy = false; 
    return true;
}

DSTATUS usb_disk_status()
{
    return tuh_msc_mounted(msc_dev_addr) ? 0 : STA_NODISK;
}

DSTATUS usb_disk_initialize()
{
    return 0;
}

DRESULT usb_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    uint8_t const lun = 0;
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    if (msc_io_fault || msc_volume_busy) return RES_ERROR;
    if (!tuh_msc_mounted(msc_dev_addr)) return RES_NOTRDY;
    if (!buff || !count || count > UINT16_MAX) return RES_PARERR;
#endif
  //  printf(">> usb_disk_read s%d c%d\n",sector,count);
    msc_volume_busy = true;
    msc_volume_error = false;
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    if (!tuh_msc_read10(msc_dev_addr, lun, buff, sector, (uint16_t)count, disk_io_complete, 0)) {
        msc_volume_busy = false;
        msc_io_fault = true;
        return RES_ERROR;
    }
#else
    tuh_msc_read10(msc_dev_addr, lun, buff, sector, (uint16_t)count, disk_io_complete, 0);
#endif
    wait_for_disk_io(0); //wait_for_disk_io(pdrv);

#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    return msc_io_fault || msc_volume_error ? RES_ERROR : RES_OK;
#endif
    return RES_OK;
}

DRESULT usb_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    uint8_t const lun = 0;
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    if (msc_io_fault || msc_volume_busy) return RES_ERROR;
    if (!tuh_msc_mounted(msc_dev_addr)) return RES_NOTRDY;
    if (!buff || !count || count > UINT16_MAX) return RES_PARERR;
#endif
    msc_volume_busy = true;
    msc_volume_error = false;
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    if (!tuh_msc_write10(msc_dev_addr, lun, buff, sector, (uint16_t)count, disk_io_complete, 0)) {
        msc_volume_busy = false;
        msc_io_fault = true;
        return RES_ERROR;
    }
#else
    tuh_msc_write10(msc_dev_addr, lun, buff, sector, (uint16_t)count, disk_io_complete, 0);
#endif
    wait_for_disk_io(0);  //wait_for_disk_io(pdrv);

#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    return msc_io_fault || msc_volume_error ? RES_ERROR : RES_OK;
#endif
    return RES_OK;
}

DRESULT usb_disk_ioctl(BYTE cmd, void *buff)
{
    uint8_t const lun = 0;
#ifdef PICOMEM_EXPERIMENTAL_FILE_ERRORS
    if (msc_io_fault || msc_volume_busy) return RES_ERROR;
    if (!tuh_msc_mounted(msc_dev_addr)) return RES_NOTRDY;
#endif
    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *((DWORD *)buff) = (WORD)tuh_msc_get_block_count(msc_dev_addr, lun);
        return RES_OK;
    case GET_SECTOR_SIZE:
        *((WORD *)buff) = (WORD)tuh_msc_get_block_size(msc_dev_addr, lun);
        return RES_OK;
    case GET_BLOCK_SIZE:
        *((DWORD *)buff) = 1; // 1 sector
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
