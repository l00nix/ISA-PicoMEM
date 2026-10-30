/* SPDX-License-Identifier: MIT */
#ifndef PICOMEM_USB_CHECKED_H
#define PICOMEM_USB_CHECKED_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Core-0 only, like the existing FatFs/USB task dispatcher. No automatic retry. */
bool pm_usb_accepting(void);
bool pm_usb_attach(uint8_t dev);
void pm_usb_detach(uint8_t dev);
/* Core-0 loop hook: bounded late observation only, no USB calls or waits. */
void pm_usb_observe_late(void);
/* Append cached diagnostic lines only; does not submit USB commands. */
void pm_usb_append_transport_status(char *buffer, size_t capacity);
#endif
