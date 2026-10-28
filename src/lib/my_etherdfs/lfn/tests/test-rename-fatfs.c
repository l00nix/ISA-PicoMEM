#include "../rename.h"
#include <ff.h>
#include <diskio.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static FILE *image;static unsigned long sectors;static unsigned writes,enabled,restored;
DSTATUS disk_initialize(BYTE p){return p==1?0:STA_NOINIT;}
DSTATUS disk_status(BYTE p){return disk_initialize(p);}
DRESULT disk_read(BYTE p,BYTE *b,LBA_t s,UINT n)
{if(p!=1||s>=sectors||n>sectors-s)return RES_PARERR;return !fseek(image,(long)s*512,SEEK_SET)&&fread(b,512,n,image)==n?RES_OK:RES_ERROR;}
DRESULT disk_write(BYTE p,const BYTE *b,LBA_t s,UINT n)
{assert(p==1);if(s>=sectors||n>sectors-s)return RES_PARERR;writes++;return !fseek(image,(long)s*512,SEEK_SET)&&fwrite(b,512,n,image)==n?RES_OK:RES_ERROR;}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *out)
{(void)out;return p==1&&cmd==CTRL_SYNC&&!fflush(image)?RES_OK:RES_PARERR;}
DWORD get_fattime(void){return 0;}
void *ff_memalloc(UINT n){return malloc(n);}
void ff_memfree(void *p){free(p);}
bool PM_EnableSD(void){assert(enabled==restored);enabled++;return true;}
bool PM_PICO_EnablePSRAM(void){assert(enabled==restored+1);restored++;return true;}
static unsigned char buf[316];
static unsigned run(unsigned drive,unsigned op,const char *parent,const char *name,unsigned status)
{
 unsigned p=strlen(parent),n=strlen(name);memset(buf,0,sizeof(buf));
 memcpy(buf,"PMLW",4);buf[4]=1;buf[5]=op;buf[6]=p;buf[7]=n;
 memcpy(buf+8,parent,p);memcpy(buf+8+p,name,n);
 assert(pm_lfn_create_serve(drive,buf,8+p+n,buf,256,pm_lfn_create_fatfs)==52);
 if(buf[6]!=status)fprintf(stderr,"create %s/%s status %u expected %u\n",parent,name,buf[6],status);
 assert(buf[6]==status);assert(enabled==restored);return buf[6];
}
int main(int argc,char **argv)
{
 FATFS fs;DIR cursor;FIL f;FILINFO info;UINT n;char dir[128],alias[128],data[32];unsigned before;DWORD pos;
 assert(argc==2);image=fopen(argv[1],"r+b");assert(image);fseek(image,0,SEEK_END);sectors=ftell(image)/512;
 assert(f_mount(&fs,"1:",1)==FR_OK);assert(f_opendir(&cursor,"1:\\")==FR_OK);pos=cursor.dptr;
 before=writes;run(18,1,"\\","SD Must Stay Untouched.txt",PM_LFN_DENIED);assert(writes==before&&!enabled);
 run(20,2,"\\","Created Long Directory",0);assert(cursor.dptr==pos);
 snprintf(dir,sizeof(dir),"\\%s",buf+13);
 run(20,1,dir,"Created Long Filename.txt",0);snprintf(alias,sizeof(alias),"1:%s\\%s",dir,buf+13);
 assert(f_open(&f,alias,FA_WRITE)==FR_OK);assert(f_write(&f,"new long-name payload",21,&n)==FR_OK&&n==21);assert(f_close(&f)==FR_OK);
 before=writes;run(20,1,dir,"Created Long Filename.txt",PM_LFN_EXISTS);assert(writes==before);
 run(20,1,dir,"created long filename.TXT",PM_LFN_EXISTS);assert(writes==before);
 run(20,1,dir,strrchr(alias,'\\')+1,PM_LFN_EXISTS);assert(writes==before);
 run(20,2,dir,"Created Long Filename.txt",PM_LFN_EXISTS);assert(writes==before);
 run(20,1,"\\MISSING","Absent parent.txt",PM_LFN_MISSING);assert(writes==before);
 assert(f_open(&f,alias,FA_READ)==FR_OK);assert(f_read(&f,data,sizeof(data),&n)==FR_OK&&n==21&&!memcmp(data,"new long-name payload",21));assert(f_close(&f)==FR_OK);
 run(20,1,dir,"N26-xxxxxxxxxxxxxxxxxx.txt",0);
 run(20,2,dir,"Nested Long Directory",0);

 /* Mutations use the short source path; the destination is one bounded name. */
 {
  struct pm_lfn_meta m;char old[128],renamed[128];
  strcpy(old,alias);
  before=writes;
  assert(pm_lfn_rename_fatfs(18,old+2,"Renamed Long Filename.txt",32,21,&m)==PM_LFN_DENIED&&writes==before);
  assert(pm_lfn_rename_fatfs(20,old+2,"N26-xxxxxxxxxxxxxxxxxx.txt",32,21,&m)==PM_LFN_EXISTS&&writes==before);
  assert(pm_lfn_rename_fatfs(20,old+2,"Renamed Long Filename.txt",32,22,&m)==PM_LFN_IO&&writes==before);
  assert(pm_lfn_rename_fatfs(20,dir,"Changed Directory",16,0,&m)==PM_LFN_DENIED&&writes==before);
  assert(pm_lfn_rename_fatfs(20,old+2,"Renamed Long Filename.txt",32,21,&m)==0);
  assert(cursor.dptr==pos);
  snprintf(renamed,sizeof(renamed),"1:%s\\%s",dir,m.alias);
  assert(f_stat(old,&info)==FR_NO_FILE);
  assert(f_open(&f,renamed,FA_READ)==FR_OK);assert(f_read(&f,data,sizeof(data),&n)==FR_OK&&n==21&&!memcmp(data,"new long-name payload",21));assert(f_close(&f)==FR_OK);
  assert(pm_lfn_rename_fatfs(20,renamed+2,"renamed long filename.TXT",32,21,&m)==0);
  snprintf(renamed,sizeof(renamed),"1:%s\\%s",dir,m.alias);
  assert(pm_lfn_rename_fatfs(20,renamed+2,"Renamed Long Filename.txt",32,21,&m)==0);
 }
 assert(f_closedir(&cursor)==FR_OK);assert(f_mount(NULL,"1:",0)==FR_OK);
 assert(f_mount(&fs,"1:",1)==FR_OK);
 assert(f_stat("1:\\Created Long Directory\\Renamed Long Filename.txt",&info)==FR_OK&&info.fsize==21);
 assert(f_stat("1:\\Created Long Directory\\N26-xxxxxxxxxxxxxxxxxx.txt",&info)==FR_OK&&!info.fsize);
 assert(f_stat("1:\\Created Long Directory\\Nested Long Directory",&info)==FR_OK&&(info.fattrib&16));
 assert(f_mount(NULL,"1:",0)==FR_OK);assert(!fclose(image));
 printf("Real FatFs rename: same-parent, case-only, alias and long-path readback, collision refusal, directory/SD denial; create: file/mkdir, 26-byte name, nested alias paths, collisions without overwrite, SD refusal, independent cursor and remount passed (%u disk writes)\n",writes);
}
