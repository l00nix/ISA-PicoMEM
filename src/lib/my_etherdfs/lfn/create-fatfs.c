/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exclusive USB creation with a separate bounded cursor, alias readback and
 * close/sync checks. An ambiguous failure is never retried or rolled back.
 */
#include "create.h"
#include "ff.h"
#include <string.h>
#include <stdbool.h>
extern bool PM_EnableSD(void);
extern bool PM_PICO_EnablePSRAM(void);
#define SCAN_MAX 256
static int scan(const char *parent,const char *name,int after,unsigned op,struct pm_lfn_meta *m)
{
    DIR dir;FILINFO info;FRESULT r,c;unsigned i,n;int status=PM_LFN_UNSUPPORTED;
    r=f_opendir(&dir,parent);
    if(r!=FR_OK)return r==FR_NO_PATH||r==FR_NO_FILE?PM_LFN_MISSING:PM_LFN_IO;
    for(i=0;i<SCAN_MAX;i++) {
        memset(&info,0,sizeof(info));r=f_readdir(&dir,&info);
        if(r!=FR_OK){status=PM_LFN_IO;break;}
        if(!info.fname[0]){status=after?PM_LFN_IO:PM_LFN_OK;break;}
        const char *alias=info.altname[0]?info.altname:info.fname;
        for(n=0;n<13&&alias[n];n++) {}
        if(!pm_lfn_alias(alias,n)){status=PM_LFN_UNSUPPORTED;break;}
        if(pm_lfn_equal(name,info.fname)||pm_lfn_equal(name,alias)) {
            status=PM_LFN_EXISTS;
            if(after) {
                status=PM_LFN_IO;
                if(!strcmp(name,info.fname)&&!info.fsize&&info.fattrib==(op==1?32:16)) {
                    memcpy(m->alias,alias,n+1);strcpy(m->name,name);m->size=0;m->attr=info.fattrib;
                    status=PM_LFN_OK;
                }
            }
            break;
        }
    }
    c=f_closedir(&dir);
    return c==FR_OK?status:PM_LFN_IO;
}
int pm_lfn_create_fatfs(unsigned drive,unsigned op,const char *parent,const char *name,struct pm_lfn_meta *m)
{
    /* DFS is serialized. Keep the sector-buffered FIL off the 2 KiB stack;
     * refuse re-entry before sharing this workspace. */
    static FIL file;
    static unsigned active;
    char dir[130],full[158];FRESULT r,c;int status;
    /* Protocol validation occurs before entry; repeat the write-drive gate. */
    if(drive!=20||(op!=1&&op!=2))return PM_LFN_DENIED;
    if(active)return PM_LFN_UNSUPPORTED;
    active=1;
    strcpy(dir,"1:");strcat(dir,parent);
    strcpy(full,dir);if(full[strlen(full)-1]!='\\')strcat(full,"\\");strcat(full,name);
    PM_EnableSD();status=scan(dir,name,0,op,m);
    if(status!=PM_LFN_OK)goto done;
    if(op==1) {
        r=f_open(&file,full,FA_WRITE|FA_CREATE_NEW);
        if(r==FR_OK) {
            r=f_sync(&file);c=f_close(&file);
            if(c!=FR_OK)r=c;
        }
    } else r=f_mkdir(full);
    if(r!=FR_OK){status=r==FR_EXIST?PM_LFN_EXISTS:PM_LFN_IO;goto done;}
    status=scan(dir,name,1,op,m);
    if(status!=PM_LFN_OK)status=PM_LFN_IO;
 done:
    PM_PICO_EnablePSRAM();active=0;return status;
}
