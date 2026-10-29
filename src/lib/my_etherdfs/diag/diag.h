/* SPDX-License-Identifier: MIT */
#ifndef PM_DIAG_H
#define PM_DIAG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void pm_diag_begin(const unsigned char *frame);
void pm_diag_end(void);
int pm_diag_dispatch(unsigned char *frame,unsigned capacity);
uint32_t pm_diag_clock(void);
void pm_diag_paint(void);
unsigned pm_diag_stack_used(void);
unsigned pm_diag_stack_valid(void);
#ifdef __cplusplus
}
#endif
#endif
