/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lfn.h"
#include "dispatch.h"
#include <string.h>
extern int pm_lfn_fatfs_lookup(void *,unsigned,const char *,struct pm_lfn_meta *);
int pm_lfn_dispatch(unsigned char *f,unsigned cap)
{
    unsigned len,drive;
    int n;
    if(cap<60 || f[59]!=PM_LFN_DFS_OPCODE)return 0;
    len=f[52]+((unsigned)f[53]<<8);drive=f[58];
    /* Bound all offsets before payload access; no filesystem call on invalid frames. */
    if(cap<60+PM_LFN_REPLY || len<68 || len>195 || len>cap ||
       f[0]!=0x12 || f[1]!=0x34 || f[2]!=0xaa || f[12]!=0xed || f[13]!=0xf5 ||
       f[56]!=1 || (drive!=18 && drive!=20)) {
        f[52]=60;f[53]=0;f[58]=13;f[59]=0;return 1;
    }
    n=pm_lfn_serve(drive,f+60,len-60,f+60,cap-60,pm_lfn_fatfs_lookup,0);
    if(n<0){f[52]=60;f[53]=0;f[58]=13;f[59]=0;return 1;}
    f[52]=60+n;f[53]=0;f[58]=f[59]=0;return 1;
}
