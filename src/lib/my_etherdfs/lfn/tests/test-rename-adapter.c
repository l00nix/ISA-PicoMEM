#include "../rename.h"
#include "ff.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static unsigned on,off,mutations,reads,renamed;static int fault,collision,full;
bool PM_EnableSD(void){assert(on==off);on++;return true;}
bool PM_PICO_EnablePSRAM(void){assert(on==off+1);off++;return true;}
FRESULT f_opendir(DIR *d,const char *s){assert(!strcmp(s,"1:\\DIR"));d->pos=0;return fault==1?FR_DISK_ERR:fault==2?FR_NO_PATH:FR_OK;}
FRESULT f_readdir(DIR *d,FILINFO *f)
{
 unsigned i=d->pos++;reads++;memset(f,0,sizeof(*f));
 if(fault==3||(fault==6&&renamed))return FR_DISK_ERR;
 if(full){strcpy(f->fname,"OTHER.TXT");return 0;}
 if(i==0) {
  strcpy(f->fname,renamed?"New Long Name.txt":"Old Long Name.txt");strcpy(f->altname,renamed?"NEWLON~1.TXT":"OLD.TXT");f->fsize=3073;f->fattrib=32;
  if(fault==7&&renamed)f->fsize++;
  if(fault==8&&!renamed)f->fsize++;
  if(fault==9)strcpy(f->altname,"BAD/NAME");
 } else if(i==1&&collision){strcpy(f->fname,"New Long Name.txt");strcpy(f->altname,"OTHER.TXT");}
 else if(i==1&&fault==10&&renamed){strcpy(f->fname,"Old Long Name.txt");strcpy(f->altname,"OLD.TXT");}
 return 0;
}
FRESULT f_closedir(DIR *d){(void)d;return fault==4?FR_DISK_ERR:0;}
FRESULT f_rename(const char *a,const char *b)
{assert(!strcmp(a,"1:\\DIR\\OLD.TXT")&&!strcmp(b,"1:\\DIR\\New Long Name.txt"));mutations++;renamed=1;return fault==5?FR_DISK_ERR:FR_OK;}
static void reset(void){on=off=mutations=reads=renamed=0;fault=collision=full=0;}
int main(void)
{
 struct pm_lfn_meta m;int r;
 reset();r=pm_lfn_rename_fatfs(20,"\\DIR\\OLD.TXT","New Long Name.txt",32,3073,&m);assert(!r&&mutations==1&&on==off&&m.size==3073);
 for(int i=1;i<=10;i++){reset();fault=i;r=pm_lfn_rename_fatfs(20,"\\DIR\\OLD.TXT","New Long Name.txt",32,3073,&m);assert(r&&on==off);assert(mutations==(i>=5&&i!=8&&i!=9));}
 reset();full=1;r=pm_lfn_rename_fatfs(20,"\\DIR\\OLD.TXT","New Long Name.txt",32,3073,&m);assert(r==1&&reads==256&&!mutations&&on==off);
 reset();collision=1;r=pm_lfn_rename_fatfs(20,"\\DIR\\OLD.TXT","New Long Name.txt",32,3073,&m);assert(r==7&&!mutations&&on==off);
 reset();assert(pm_lfn_rename_fatfs(18,"\\DIR\\OLD.TXT","New Long Name.txt",32,3073,&m)==8&&!on);
 assert(pm_lfn_rename_fatfs(20,"\\DIR\\OLD.TXT","New Long Name.txt",16,0,&m)==8&&!on);
 puts("Rename adapter: complete bounded scans, collision/no mutation, metadata mismatch, filesystem failures, old alias disappearance and PSRAM cleanup passed");
}
