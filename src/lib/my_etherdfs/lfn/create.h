/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PM_LFN_CREATE_H
#define PM_LFN_CREATE_H
#include "lfn.h"
#define PM_LFN_CREATE_OPCODE 0x71
#define PM_LFN_CREATE_REQUEST (8+PM_LFN_PATH+PM_LFN_MAX)
#define PM_LFN_EXISTS 7
#define PM_LFN_DENIED 8
/* op 0: capabilities, 1: exclusive empty file, 2: directory. USB only. */
typedef int (*pm_lfn_create_fn)(unsigned,unsigned,const char *,const char *,struct pm_lfn_meta *);
int pm_lfn_create_serve(unsigned,const unsigned char *,unsigned,unsigned char *,unsigned,pm_lfn_create_fn);
int pm_lfn_create_fatfs(unsigned,unsigned,const char *,const char *,struct pm_lfn_meta *);
int pm_lfn_create_dispatch(unsigned char *,unsigned);
#endif
