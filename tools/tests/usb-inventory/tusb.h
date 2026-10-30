#ifndef INVENTORY_TEST_TUSB_H
#define INVENTORY_TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
#define CFG_TUH_DEVICE_MAX 4
#define CFG_TUH_HUB 1
typedef struct { uint8_t rhport, hub_addr, hub_port, speed; } tuh_bus_info_t;
bool tuh_mounted(uint8_t);
bool tuh_vid_pid_get(uint8_t, uint16_t *, uint16_t *);
bool tuh_bus_info_get(uint8_t, tuh_bus_info_t *);
bool tuh_msc_mounted(uint8_t);
#endif
