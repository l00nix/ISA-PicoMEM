/* Read-only integration against upstream FatFs and a generated FAT image. */
#include "../lfn.h"
#include <ff.h>
#include <diskio.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static FILE *image;
static unsigned writes,enabled,restored;
DSTATUS disk_initialize(BYTE p){return p<2?0:STA_NOINIT;}
DSTATUS disk_status(BYTE p){return disk_initialize(p);}
DRESULT disk_read(BYTE p,BYTE *b,LBA_t sector,UINT n)
{
 if(p>1 || sector>2880 || n>2880-sector)return RES_PARERR;
 if(fseek(image,(long)sector*512,SEEK_SET))return RES_ERROR;
 return fread(b,512,n,image)==n?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE p,const BYTE *b,LBA_t s,UINT n)
{(void)p;(void)b;(void)s;(void)n;writes++;return RES_WRPRT;}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *out)
{(void)p;(void)out;return cmd==CTRL_SYNC?RES_OK:RES_PARERR;}
DWORD get_fattime(void){return 0;}
void *ff_memalloc(UINT n){return malloc(n);}
void ff_memfree(void *p){free(p);}
bool PM_EnableSD(void){assert(enabled==restored);enabled++;return true;}
bool PM_PICO_EnablePSRAM(void){assert(enabled==restored+1);restored++;return true;}
int pm_lfn_fatfs_lookup(void *,unsigned,const char *,struct pm_lfn_meta *);
static unsigned check_directory(unsigned drive,const char *root,unsigned want)
{
 DIR d;FILINFO f;unsigned count=0,longnames=0,fallback=0;char path[128],display[27];
 unsigned char buf[256];int n;DWORD cursor;
 assert(f_opendir(&d,root)==FR_OK);
 while(1){
  assert(f_readdir(&d,&f)==FR_OK);if(!f.fname[0])break;
  const char *alias=f.altname[0]?f.altname:f.fname;
  snprintf(path,sizeof(path),"\\%s",alias);cursor=d.dptr;
  n=pm_lfn_request(buf,sizeof(buf),1,path);assert(n>0);
  assert(pm_lfn_serve(drive,buf,n,buf,sizeof(buf),pm_lfn_fatfs_lookup,NULL)==52);
  assert(cursor==d.dptr);assert(enabled==restored);
  if(strlen(f.fname)<=26 && pm_lfn_ascii(f.fname,strlen(f.fname))){
   assert(pm_lfn_decode(buf,52,alias,f.fattrib,(unsigned long)f.fsize,display));
   if(strcmp(display,f.fname))fprintf(stderr,"Mismatch: display=%s directory=%s alias=%s\n",display,f.fname,alias);
   assert(!strcmp(display,f.fname));longnames++;
  }else{
   assert(buf[6]==PM_LFN_TOO_LONG || buf[6]==PM_LFN_ENCODING);
   assert(!pm_lfn_decode(buf,52,alias,f.fattrib,(unsigned long)f.fsize,display));
   strcpy(display,alias);fallback++;
  }
  printf("%c: %-26s [alias %s]\n",drive==20?'U':'S',display,alias);count++;
 }
 assert(f_closedir(&d)==FR_OK);assert(count==want);assert(longnames==3&&fallback==2);
 return count;
}
int main(int argc,char **argv)
{
 FATFS sd,usb;FILINFO dirinfo,fileinfo;FIL f;char path[128],shortpath[128],data[32],display[27];
 unsigned char buf[256];UINT got;int n;struct pm_lfn_entry entries[3];
 assert(argc==2);image=fopen(argv[1],"rb");assert(image);
 assert(f_mount(&sd,"0:",1)==FR_OK);assert(f_mount(&usb,"1:",1)==FR_OK);
 check_directory(20,"1:\\",5);check_directory(18,"0:\\",5);
 assert(f_stat("1:\\Long Directory",&dirinfo)==FR_OK);
 assert(f_stat("1:\\Long Directory\\Nested Long Filename.txt",&fileinfo)==FR_OK);
 memset(entries,0,sizeof(entries));strcpy(entries[1].alias,dirinfo.altname);
 strcpy(entries[1].display,dirinfo.fname);entries[1].parent=0;
 strcpy(entries[2].alias,fileinfo.altname);entries[2].parent=1;
 assert(pm_lfn_path(entries,3,2,shortpath,sizeof(shortpath))>0);
 n=pm_lfn_request(buf,sizeof(buf),1,shortpath);assert(n>0);
 assert(pm_lfn_serve(20,buf,n,buf,sizeof(buf),pm_lfn_fatfs_lookup,NULL)==52);
 assert(pm_lfn_decode(buf,52,fileinfo.altname,fileinfo.fattrib,fileinfo.fsize,display));
 strcpy(entries[2].display,display);pm_lfn_resolve(entries,3);
 assert(pm_lfn_lookup(entries,3,1,"Nested Long Filename.txt")==2);
 snprintf(path,sizeof(path),"1:%s",shortpath);
 assert(f_open(&f,path,FA_READ)==FR_OK);assert(f_read(&f,data,sizeof(data),&got)==FR_OK);
 assert(got==12&&!memcmp(data,"LFN fixture\n",12));assert(f_close(&f)==FR_OK);
 assert(!writes);fclose(image);
 puts("Real FatFs: USB/SD names, long/Unicode fallback, independent directory cursor, nested alias-only open passed; zero disk writes.");
 return 0;
}
