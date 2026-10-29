/* SPDX-License-Identifier: MIT
 * Real FatFs + checked USB adapter, with only the transport modeled on a file.
 * Error injection is confined to disposable FAT32 images.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../../src/USB_Host_Driver/msc/usb-checked.c"
#include "../../../src/USB_Host_Driver/msc/fatfs_usb.c"
static char status_text[80];
void usb_set_status(uint8_t d,const char *fmt,...){
 assert(d==1);va_list a;va_start(a,fmt);int n=vsnprintf(status_text,sizeof(status_text),fmt,a);va_end(a);assert(n>0&&n+3<80);
}
static FILE *image;
static uint32_t sectors,ticks;
static msc_cbw_t bus;
static void *owned;
static tuh_msc_complete_cb_t callback;
static uintptr_t arg;
static unsigned writes,flushes,submissions;
static const char *mode;
static bool armed;
bool tuh_msc_mounted(uint8_t d){return d==1;}
bool tuh_msc_ready(uint8_t d){return d==1;}
uint32_t tuh_msc_get_block_count(uint8_t d,uint8_t l){assert(d==1&&!l);return sectors;}
uint32_t tuh_msc_get_block_size(uint8_t d,uint8_t l){assert(d==1&&!l);return 512;}
bool tuh_msc_scsi_command(uint8_t d,const msc_cbw_t*c,void*b,tuh_msc_complete_cb_t cb,uintptr_t u)
{assert(d==1);bus=*c;owned=b;callback=cb;arg=u;submissions++;return true;}
uint32_t time_us_32(void){return ++ticks;}
void tuh_task_ext(uint32_t ms,bool isr)
{
 (void)ms;(void)isr;
 msc_csw_t csw={.signature=MSC_CSW_SIGNATURE,.tag=bus.tag};
 tuh_msc_complete_data_t reply={.cbw=&bus,.csw=&csw,.scsi_data=owned,.user_arg=arg};
 if(bus.command[0]==0x1a){
  assert(failed&&!dirty&&!writes&&bus.total_bytes==64);
  memset(owned,0,64);BYTE *b=owned;b[0]=23;b[4]=8;b[5]=18;csw.data_residue=40;
 }else if(bus.command[0]==0x03){
  assert(failed&&!dirty&&!writes&&bus.total_bytes==18);
  memset(owned,0,18);BYTE *b=owned;b[0]=0x70;b[2]=5;b[7]=10;b[12]=0x20;
 }else if(bus.command[0]==0x35){
  flushes++;assert(!owned&&!bus.total_bytes);
  if((armed&&!strcmp(mode,"syncfail"))||!strcmp(mode,"unsupported"))csw.status=1;
  else assert(!fflush(image));
 }else{
  uint32_t lba=((uint32_t)bus.command[2]<<24)|((uint32_t)bus.command[3]<<16)|((uint32_t)bus.command[4]<<8)|bus.command[5];
  assert(lba<sectors&&bus.total_bytes==512&&bus.command[8]==1);
  assert(!fseek(image,(long)lba*512,SEEK_SET));
  if(bus.command[0]==0x28){
   if(!strcmp(mode,"mountfail"))csw.status=1;
   else assert(fread(owned,512,1,image)==1);
  }
  else{
   assert(bus.command[0]==0x2a&&flushes);writes++;
   if(armed&&!strcmp(mode,"writefail"))csw.status=1;
   else assert(fwrite(owned,512,1,image)==1);
  }
 }
 callback(1,&reply);
}
DSTATUS disk_initialize(BYTE p){assert(p==1);return usb_disk_initialize();}
DSTATUS disk_status(BYTE p){assert(p==1);return usb_disk_status();}
DRESULT disk_read(BYTE p,BYTE*b,LBA_t s,UINT n){assert(p==1);return usb_disk_read(b,s,n);}
DRESULT disk_write(BYTE p,const BYTE*b,LBA_t s,UINT n){assert(p==1);return usb_disk_write(b,s,n);}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void*b){assert(p==1);return usb_disk_ioctl(cmd,b);}
DWORD get_fattime(void){return (DWORD)(46<<25)|(1<<21)|(1<<16);}
void *ff_memalloc(UINT n){return malloc(n);}
void ff_memfree(void*p){free(p);}
int main(int argc,char**argv)
{
 assert(argc==3);mode=argv[2];image=fopen(argv[1],"r+b");assert(image);
 assert(!fseek(image,0,SEEK_END));sectors=ftell(image)/512;
 tuh_msc_mount_cb(0);tuh_msc_mount_cb(255);tuh_msc_mount_cb(2);
 assert(msc_dev_addr==0xff&&!submissions);
 tuh_msc_mount_cb(1);
 if(!strcmp(mode,"mountfail")){
  assert(msc_dev_addr==0xff&&failed&&strstr(status_text,"op=28 f=2 s=1"));unsigned before=submissions;
  tuh_msc_mount_cb(1);assert(submissions==before);
  assert(!fclose(image));puts("PASS actual mount callback: failure quarantine");return 0;
 }
 assert(msc_dev_addr==1&&!failed);
 unsigned before=submissions;tuh_msc_mount_cb(2);tuh_msc_umount_cb(2);
 assert(msc_dev_addr==1&&submissions==before);
 FATFS fs;FIL f;UINT n;BYTE payload[1537],readback[1537];
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(BYTE)(i*13+7);
 assert(f_mount(&fs,"1:",1)==FR_OK);
 FRESULT r=f_open(&f,"1:/Checked USB cache payload.txt",FA_CREATE_NEW|FA_WRITE);
 if(!strcmp(mode,"unsupported")){
  if(r==FR_OK){r=f_write(&f,payload,sizeof(payload),&n);if(r==FR_OK)r=f_close(&f);}
  assert(r==FR_DISK_ERR&&failed&&!writes&&flushes==1);
  assert(strstr(status_text,"rs=70:5/20/00 n=18 c6=off c10=-"));
  assert(strstr(status_text,"op=35 f=2 s=1 r=0 d=0"));
  tuh_msc_umount_cb(1);assert(strstr(status_text,"op=35 f=2 s=1 r=0 d=0"));
 }else{
  assert(r==FR_OK&&f_write(&f,payload,sizeof(payload),&n)==FR_OK&&n==sizeof(payload));
  armed=true;r=f_close(&f);
  if(strcmp(mode,"normal")){
   assert(r==FR_DISK_ERR&&failed&&dirty);
   unsigned before=submissions;
   assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR&&submissions==before);
  }else{
   assert(r==FR_OK&&flushes>=2&&writes&&!dirty&&!failed);
   assert(f_mount(NULL,"1:",0)==FR_OK);pm_usb_detach(1);assert(pm_usb_attach(1));
   assert(f_mount(&fs,"1:",1)==FR_OK);
   assert(f_open(&f,"1:/Checked USB cache payload.txt",FA_READ)==FR_OK);
   assert(f_read(&f,readback,sizeof(readback),&n)==FR_OK&&n==sizeof(payload));
   assert(!memcmp(payload,readback,n)&&f_close(&f)==FR_OK);
  }
 }
 assert(!fclose(image));printf("PASS real FatFs/USB adapter: %s (%u writes, %u flushes)\n",mode,writes,flushes);
 return 0;
}
