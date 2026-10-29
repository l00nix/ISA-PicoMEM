/* SPDX-License-Identifier: MIT
 * Real dispatcher + handle pool + FatFs. Faults stay in disposable host images.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ff.h>
#include <diskio.h>
#include "fs.h"
#include "checked-file.h"
extern FIL *get_fobj(uint8_t);
extern FIL *new_fobj(uint8_t *);
extern bool pm_fileopened[16];
extern void ethdfs_docmd(uint8_t *);
volatile uint8_t PM_DP_RAM[16384];
static FILE *image;
static unsigned long sectors;
static unsigned writes,syncs,closes,enabled,restored;
static int fail_write,fail_sync,fail_stat;
DSTATUS disk_initialize(BYTE p){return p==1?0:STA_NOINIT;}
DSTATUS disk_status(BYTE p){return disk_initialize(p);}
DRESULT disk_read(BYTE p,BYTE *b,LBA_t s,UINT n)
{
 if(p!=1||s>=sectors||n>sectors-s)return RES_PARERR;
 return !fseek(image,(long)s*512,SEEK_SET)&&fread(b,512,n,image)==n?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE p,const BYTE *b,LBA_t s,UINT n)
{
 assert(p==1);if(s>=sectors||n>sectors-s)return RES_PARERR;
 writes++;if(fail_write)return RES_ERROR;
 return !fseek(image,(long)s*512,SEEK_SET)&&fwrite(b,512,n,image)==n?RES_OK:RES_ERROR;
}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *out)
{
 (void)out;if(p!=1||cmd!=CTRL_SYNC)return RES_PARERR;
 syncs++;return fail_sync||fflush(image)?RES_ERROR:RES_OK;
}
DWORD get_fattime(void){return (DWORD)(46<<25)|(1<<21)|(1<<16);}
void *ff_memalloc(UINT n){return malloc(n);}
void ff_memfree(void *p){free(p);}
bool PM_EnableSD(void){assert(enabled==restored);enabled++;return true;}
bool PM_PICO_EnablePSRAM(void){assert(enabled==restored+1);restored++;return true;}
FRESULT test_f_stat(const TCHAR *path,FILINFO *info)
{return fail_stat?FR_DISK_ERR:f_stat(path,info);}
FRESULT test_f_close(FIL *file)
{closes++;return f_close(file);}
static uint32_t aligned_frame[79];
static unsigned close_wire(unsigned fid,unsigned length)
{
 unsigned char *p=(unsigned char*)aligned_frame;
 memset(p,0,316);p[0]=0x12;p[1]=0x34;p[2]=0xaa;
 p[12]=0xed;p[13]=0xf5;p[52]=length;p[53]=length>>8;
 p[56]=1;p[57]=219;p[58]=20;p[59]=6;p[60]=fid;p[61]=fid>>8;
 ethdfs_docmd(p);
 assert(p[52]==60&&!p[53]&&p[57]==219&&p[0]==0x12);
 assert(enabled==restored);return p[58]+((unsigned)p[59]<<8);
}
int main(int argc,char **argv)
{
 FATFS fs;struct fileprops props,sentinel;FIL *file;int fid;
 unsigned n,before,old_writes,old_syncs;uint8_t next;
 assert(argc==3);image=fopen(argv[1],"r+b");assert(image);
 fseek(image,0,SEEK_END);sectors=ftell(image)/512;
 assert(f_mount(&fs,"1:",1)==FR_OK);init_fobj();
 assert(pm_file_error(FR_OK)==0&&pm_file_error(FR_DISK_ERR)==31);
 assert(pm_file_error(FR_NOT_READY)==21&&pm_file_error(FR_WRITE_PROTECTED)==19);
 assert(pm_file_error(FR_INVALID_OBJECT)==6);
 assert(close_wire(0,62)==6&&close_wire(256,62)==6&&close_wire(65535,62)==6);
 memset(&props,0xa5,sizeof(props));sentinel=props;
 fail_stat=!strncmp(argv[2],"stat",4);
 if(!strcmp(argv[2],"statclosefail"))fail_sync=1;
 fid=openfile(&props,"1:/CHECK.TXT",FA_READ|FA_WRITE|FA_CREATE_NEW);
 if(fail_stat){
  assert(fid==-31&&!memcmp(&props,&sentinel,sizeof(props))&&closes==1);
  assert(enabled==restored);
  if(fail_sync){assert(pm_fileopened[0]&&pm_file_failed(0)==31&&!get_fobj(0));}
  else assert(!pm_fileopened[0]&&!pm_file_failed(0));
 }else{
  assert(fid==0&&props.fsize==0);file=get_fobj(fid);assert(file);
  before=closes;
  for(n=0;n<65;n++)if(n!=62)assert(close_wire(fid,n)==13);
  assert(close_wire(fid,316)==13&&close_wire(256,62)==6&&closes==before);
  assert(f_write(file,"checked close payload",21,&n)==FR_OK&&n==21);
  fail_write=!strcmp(argv[2],"writefail");fail_sync=!strcmp(argv[2],"syncfail");
  if(fail_write||fail_sync){
   assert(close_wire(fid,62)==31&&pm_fileopened[fid]&&!get_fobj(fid));
   before=closes;old_writes=writes;old_syncs=syncs;
   fail_write=fail_sync=0;
   assert(close_wire(fid,62)==31&&closes==before&&writes==old_writes&&syncs==old_syncs);
   assert(new_fobj(&next)&&next==1); /* Never reuse the failed handle. */
  }else{
   assert(close_wire(fid,62)==0&&!pm_fileopened[fid]);
   assert(close_wire(fid,62)==6&&closes==before+1);
   assert(f_mount(NULL,"1:",0)==FR_OK&&f_mount(&fs,"1:",1)==FR_OK);
   FIL check;char data[32];assert(f_open(&check,"1:/CHECK.TXT",FA_READ)==FR_OK);
   assert(f_read(&check,data,sizeof(data),&n)==FR_OK&&n==21&&!memcmp(data,"checked close payload",21));
   assert(f_close(&check)==FR_OK);
  }
 }
 assert(enabled==restored);
 /* Do not retry or 'repair' failed images. Tests end with quarantined handles. */
 assert(!fclose(image));printf("PASS real dispatcher/handle pool/FatFs: %s\n",argv[2]);
 return 0;
}
