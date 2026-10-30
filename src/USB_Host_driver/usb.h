/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _DEV_H_
#define _DEV_H_

#include <stdint.h>
#include <stddef.h>

#define DEV_DESC_SIZE 80

extern char dev_message[CFG_TUH_DEVICE_MAX][DEV_DESC_SIZE];

extern uint8_t usb_get_devnb();
extern void usb_print_status();
extern void usb_set_status(uint8_t dev_addr, const char *format, ...);
#ifdef PICOMEM_EXPERIMENTAL_USB_SYNC
/* Core 0, cached enumeration only; counted NUL strings, at most eight lines. */
void usb_build_inventory(char *buffer, size_t capacity);
#endif

#endif /* _DEV_H_ */
