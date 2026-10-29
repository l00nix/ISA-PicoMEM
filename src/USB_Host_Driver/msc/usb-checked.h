/* SPDX-License-Identifier: MIT */
#ifndef PICOMEM_USB_CHECKED_H
#define PICOMEM_USB_CHECKED_H
#include <stdbool.h>
#include <stdint.h>
/* Core-0 only, like the existing FatFs/USB task dispatcher. No automatic retry. */
bool pm_usb_accepting(void);
bool pm_usb_attach(uint8_t dev);
void pm_usb_detach(uint8_t dev);
#endif
