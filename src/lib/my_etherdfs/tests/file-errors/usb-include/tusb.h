/* SPDX-License-Identifier: MIT */
#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#define CFG_TUH_DEVICE_MAX 4
typedef struct { uint8_t lun; } msc_cbw_t;
typedef struct { uint32_t data_residue; uint8_t status; } msc_csw_t;
typedef struct { char vendor_id[8],product_id[16]; } scsi_inquiry_resp_t;
typedef struct { const msc_cbw_t *cbw; const msc_csw_t *csw; } tuh_msc_complete_data_t;
typedef bool (*tuh_msc_complete_cb_t)(uint8_t,const tuh_msc_complete_data_t*);
bool tuh_msc_mounted(uint8_t);
uint32_t tuh_msc_get_block_count(uint8_t,uint8_t);
uint32_t tuh_msc_get_block_size(uint8_t,uint8_t);
bool tuh_msc_inquiry(uint8_t,uint8_t,void*,tuh_msc_complete_cb_t,uintptr_t);
bool tuh_msc_read10(uint8_t,uint8_t,void*,uint32_t,uint16_t,tuh_msc_complete_cb_t,uintptr_t);
bool tuh_msc_write10(uint8_t,uint8_t,const void*,uint32_t,uint16_t,tuh_msc_complete_cb_t,uintptr_t);
uint32_t time_us_32(void);
void tuh_task(void);
#endif
