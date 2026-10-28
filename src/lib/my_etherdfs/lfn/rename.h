/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PM_LFN_RENAME_H
#define PM_LFN_RENAME_H
#include "create.h"
#define PM_LFN_RENAME_OPCODE 0x72
#define PM_LFN_RENAME_REQUEST (13+PM_LFN_PATH+PM_LFN_MAX)
/* v1: same-parent regular files, no replacement. Expected size/attr checked. */
typedef int (*pm_lfn_rename_fn)(unsigned,const char *,const char *,unsigned char,unsigned long,struct pm_lfn_meta *);
int pm_lfn_rename_serve(unsigned,const unsigned char *,unsigned,unsigned char *,unsigned,pm_lfn_rename_fn);
int pm_lfn_rename_fatfs(unsigned,const char *,const char *,unsigned char,unsigned long,struct pm_lfn_meta *);
int pm_lfn_rename_dispatch(unsigned char *,unsigned);
#endif
