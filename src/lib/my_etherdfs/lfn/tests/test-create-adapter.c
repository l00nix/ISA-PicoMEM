#include "../create.h"
#include "ff.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static unsigned on,off,mutations,reads;static int failure,created,full,conflict;
static char target[158];
bool PM_EnableSD(void){assert(on==off);on++;return true;}
bool PM_PICO_EnablePSRAM(void){assert(on==off+1);off++;return true;}
FRESULT f_opendir(DIR *d,const char *s)
{assert(!strcmp(s,"1:\\DIR"));d->pos=0;return failure==1?FR_DISK_ERR:failure==2?FR_NO_PATH:FR_OK;}
FRESULT f_readdir(DIR *d,FILINFO *f)
{
 reads++;memset(f,0,sizeof(*f));
 if(failure==3|| (failure==7&&created))return FR_DISK_ERR;
 if(full){strcpy(f->fname,"OTHER.TXT");return FR_OK;}
 if(d->pos++==0&&(created||conflict)) {
  strcpy(f->fname,"New Long Name.txt");strcpy(f->altname,"NEWLON~1.TXT");f->fattrib=created==2?16:32;
 }
 return FR_OK;
}
FRESULT f_closedir(DIR *d){(void)d;return failure==4?FR_DISK_ERR:FR_OK;}
FRESULT f_open(FIL *f,const char *p,unsigned char mode)
{assert(mode==(FA_WRITE|FA_CREATE_NEW));assert(!strcmp(p,"1:\\DIR\\New Long Name.txt"));strcpy(target,p);mutations++;f->open=1;created=1;return FR_OK;}
FRESULT f_sync(FIL *f){assert(f->open);return failure==5?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *f){assert(f->open);f->open=0;return failure==6?FR_DISK_ERR:FR_OK;}
FRESULT f_mkdir(const char *p){assert(!strcmp(p,"1:\\DIR\\New Long Name.txt"));created=2;mutations++;return failure==8?FR_DISK_ERR:FR_OK;}
static void reset(void){on=off=mutations=reads=0;failure=created=full=conflict=0;}
int main(void)
{
 struct pm_lfn_meta m;unsigned j;
 for(j=1;j<=8;j++){
  reset();failure=j;assert(pm_lfn_create_fatfs(20,j==8?2:1,"\\DIR","New Long Name.txt",&m)==(j==2?PM_LFN_MISSING:PM_LFN_IO));
  assert(on==1&&off==1&&mutations==(j>=5));
 }
 reset();full=1;assert(pm_lfn_create_fatfs(20,1,"\\DIR","New Long Name.txt",&m)==PM_LFN_UNSUPPORTED);assert(reads==256&&!mutations&&on==off);
 reset();conflict=1;assert(pm_lfn_create_fatfs(20,1,"\\DIR","New Long Name.txt",&m)==PM_LFN_EXISTS);assert(!mutations&&on==off);
 reset();assert(pm_lfn_create_fatfs(18,1,"\\DIR","New Long Name.txt",&m)==PM_LFN_DENIED);assert(!on&&!mutations);
 for(j=1;j<=2;j++){
  reset();assert(pm_lfn_create_fatfs(20,j,"\\DIR","New Long Name.txt",&m)==0);
  assert(mutations==1&&on==off&&!strcmp(m.alias,"NEWLON~1.TXT")&&!strcmp(m.name,"New Long Name.txt"));
 }
 puts("Create adapter: directory bounds, preflight failures, sync/close errors, ambiguous completion, collisions and PSRAM cleanup passed");
}
