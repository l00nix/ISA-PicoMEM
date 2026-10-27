/* SPDX-License-Identifier: GPL-2.0-or-later
 * Build inside a paired PicoMEM firmware, or with the test shim.
 * A separate bounded cursor preserves legacy DFS dir_find. f_stat(short_alias)
 * does NOT recover the LFN in the audited FatFs; directory enumeration does.
 */
#include "lfn.h"
#include "ff.h"
#include <string.h>
#include <stdbool.h>
#define PM_LFN_SCAN_MAX 256
extern bool PM_EnableSD(void);
extern bool PM_PICO_EnablePSRAM(void);
static int copy_info(const FILINFO *info,struct pm_lfn_meta *m)
{
    const char *alias=info->altname[0]?info->altname:info->fname;
    unsigned n;
    for(n=0;n<13 && alias[n];n++) {}
    if(!pm_lfn_alias(alias,n))return PM_LFN_IO;
    memcpy(m->alias,alias,n);m->alias[n]=0;m->size=info->fsize;m->attr=info->fattrib;
    for(n=0;n<sizeof(info->fname) && info->fname[n];n++)
        if((unsigned char)info->fname[n]>=127 || (unsigned char)info->fname[n]<32)return PM_LFN_ENCODING;
    if(n>PM_LFN_MAX)return PM_LFN_TOO_LONG;
    if(!pm_lfn_ascii(info->fname,n))return PM_LFN_ENCODING;
    memcpy(m->name,info->fname,n);m->name[n]=0;return PM_LFN_OK;
}
int pm_lfn_fatfs_lookup(void *ctx,unsigned drive,const char *shortpath,struct pm_lfn_meta *m)
{
    FILINFO info;DIR cursor;
    FRESULT r,closed;
    char path[PM_LFN_PATH+3];
    const char *alias;
    unsigned n=0,leaf=1,j;
    int status=PM_LFN_UNSUPPORTED;
    (void)ctx;
    /* Called only after pm_lfn_serve validates the absolute short path. */
    if(drive!=18 && drive!=20)return PM_LFN_INVALID;
    while(n<=PM_LFN_PATH && shortpath[n]) {if(shortpath[n]=='\\')leaf=n+1;n++;}
    if(n>PM_LFN_PATH || leaf>=n)return PM_LFN_INVALID;
    path[0]=drive==18?'0':'1';path[1]=':';memcpy(path+2,shortpath,leaf);path[leaf+2]=0;
    PM_EnableSD();r=f_opendir(&cursor,path);
    if(r!=FR_OK) {
        PM_PICO_EnablePSRAM();
        return r==FR_NO_FILE || r==FR_NO_PATH ? PM_LFN_MISSING : PM_LFN_IO;
    }
    for(j=0;j<PM_LFN_SCAN_MAX;j++) {
        memset(&info,0,sizeof(info));r=f_readdir(&cursor,&info);
        if(r!=FR_OK){status=PM_LFN_IO;break;}
        if(!info.fname[0]){status=PM_LFN_MISSING;break;}
        alias=info.altname[0]?info.altname:info.fname;
        for(n=0;n<13 && alias[n];n++) {}
        if(pm_lfn_alias(alias,n) && pm_lfn_equal(alias,shortpath+leaf)) {
            status=copy_info(&info,m);break;
        }
    }
    closed=f_closedir(&cursor);PM_PICO_EnablePSRAM();
    return closed==FR_OK?status:PM_LFN_IO;
}
