/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded same-parent rename; never retry or roll back uncertain completion.
 */
#include "rename.h"
#include "ff.h"
#include <stdbool.h>
#include <string.h>
extern bool PM_EnableSD(void);
extern bool PM_PICO_EnablePSRAM(void);
static int scan(const char *dir,const char *source,const char *name,
 unsigned char attr,unsigned long size,int after,struct pm_lfn_meta *m)
{
 DIR d;FILINFO info;FRESULT r,c;unsigned i,n,found=0,old=0;
 int status=PM_LFN_UNSUPPORTED;
 r=f_opendir(&d,dir);
 if(r!=FR_OK)return r==FR_NO_FILE||r==FR_NO_PATH?PM_LFN_MISSING:PM_LFN_IO;
 for(i=0;i<256;i++) {
  const char *a;int isold;
  memset(&info,0,sizeof(info));r=f_readdir(&d,&info);
  if(r!=FR_OK){status=PM_LFN_IO;break;}
  if(!info.fname[0]) {
   status=found==1?PM_LFN_OK:after?PM_LFN_IO:PM_LFN_MISSING;
   if(after&&old&&!pm_lfn_equal(source,m->alias))status=PM_LFN_IO;
   break;
  }
  a=info.altname[0]?info.altname:info.fname;
  for(n=0;n<13&&a[n];n++){}
  if(!pm_lfn_alias(a,n)){status=PM_LFN_UNSUPPORTED;break;}
  isold=pm_lfn_equal(a,source);if(isold)old++;
  if(!after&&isold) {
   if(info.fattrib!=attr||info.fsize!=size){status=PM_LFN_IO;break;}
   found++;
  }
  if(pm_lfn_equal(name,info.fname)||pm_lfn_equal(name,a)) {
   if(!after&&!isold){status=PM_LFN_EXISTS;break;}
   if(after) {
    if(strcmp(name,info.fname)||info.fattrib!=(attr|32)||info.fsize!=size){status=PM_LFN_IO;break;}
    found++;strcpy(m->alias,a);strcpy(m->name,name);m->attr=info.fattrib;m->size=info.fsize;
   }
  }
 }
 c=f_closedir(&d);return c==FR_OK?status:PM_LFN_IO;
}
int pm_lfn_rename_fatfs(unsigned drive,const char *source,const char *name,
 unsigned char attr,unsigned long size,struct pm_lfn_meta *m)
{
 static struct {char oldpath[130],dir[130],newpath[158];unsigned active;} work;
 char *last;int status;FRESULT r;
 if(drive!=20||attr&~0x26)return PM_LFN_DENIED;
 if(work.active)return PM_LFN_UNSUPPORTED;
 work.active=1;
 strcpy(work.oldpath,"1:");strcat(work.oldpath,source);
 strcpy(work.dir,work.oldpath);last=strrchr(work.dir,'\\');
 if(!last||!last[1]){status=PM_LFN_INVALID;goto release;}
 if(last==work.dir+2)last[1]=0;else *last=0;
 strcpy(work.newpath,work.dir);
 if(work.newpath[strlen(work.newpath)-1]!='\\')strcat(work.newpath,"\\");
 strcat(work.newpath,name);
 PM_EnableSD();
 status=scan(work.dir,strrchr(source,'\\')+1,name,attr,size,0,m);
 if(status)goto done;
 r=f_rename(work.oldpath,work.newpath);
 /* Even FR_EXIST after preflight is treated as uncertain, never retried. */
 if(r!=FR_OK){status=PM_LFN_IO;goto done;}
 status=scan(work.dir,strrchr(source,'\\')+1,name,attr,size,1,m);
 if(status)status=PM_LFN_IO;
 done:PM_PICO_EnablePSRAM();
 release:work.active=0;return status;
}
