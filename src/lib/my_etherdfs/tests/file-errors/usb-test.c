/* SPDX-License-Identifier: MIT
 * Exercise the production USB disk adapter with a deterministic MSC transport.
 * This models responses, not DMA ownership, device caches or a physical bus.
 */
#include <stdio.h>
#include <string.h>
#include "../../../../USB_Host_Driver/msc/fatfs_usb.c"
static bool present=true,reject,stall,disconnect;
static uint32_t ticks;
static unsigned submitted,pumped;
static tuh_msc_complete_cb_t completion;
static msc_csw_t csw;
static msc_cbw_t cbw;
static const tuh_msc_complete_data_t reply={&cbw,&csw};
bool tuh_msc_mounted(uint8_t d){return present&&d==1;}
uint32_t tuh_msc_get_block_count(uint8_t d,uint8_t l){(void)d;(void)l;return 32768;}
uint32_t tuh_msc_get_block_size(uint8_t d,uint8_t l){(void)d;(void)l;return 512;}
bool tuh_msc_inquiry(uint8_t d,uint8_t l,void*b,tuh_msc_complete_cb_t c,uintptr_t a)
{(void)d;(void)l;(void)b;(void)c;(void)a;return false;}
bool tuh_msc_read10(uint8_t d,uint8_t l,void*b,uint32_t s,uint16_t n,tuh_msc_complete_cb_t c,uintptr_t a)
{assert(d==1&&l==0&&b&&s==12&&n==1&&!a);submitted++;completion=c;return !reject;}
bool tuh_msc_write10(uint8_t d,uint8_t l,const void*b,uint32_t s,uint16_t n,tuh_msc_complete_cb_t c,uintptr_t a)
{return tuh_msc_read10(d,l,(void*)b,s,n,c,a);}
uint32_t time_us_32(void){ticks+=100000;return ticks;}
void tuh_task(void)
{
 pumped++;
 if(disconnect){present=false;tuh_msc_umount_cb(1);}
 if(!stall){assert(completion);completion(1,&reply);}
}
void usb_set_status(uint8_t d,const char *f,...){(void)d;(void)f;}
FRESULT f_mount(FATFS *f,const TCHAR *p,BYTE o){(void)f;(void)p;(void)o;return FR_OK;}
FRESULT f_getcwd(TCHAR*b,UINT n){(void)b;(void)n;return FR_OK;}
FRESULT f_chdrive(const TCHAR*p){(void)p;return FR_OK;}
FRESULT f_chdir(const TCHAR*p){(void)p;return FR_OK;}
int main(int argc,char**argv)
{
 BYTE data[512]={0};unsigned before;DRESULT result;
 assert(argc==3);msc_dev_addr=1;
 bool write=!strcmp(argv[1],"write");const char*mode=argv[2];
 if(!strcmp(mode,"guards")){
  present=false;
  assert(usb_disk_read(data,12,1)==RES_NOTRDY&&usb_disk_write(data,12,1)==RES_NOTRDY);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_NOTRDY);present=true;
  assert(usb_disk_read(NULL,12,1)==RES_PARERR&&usb_disk_write(NULL,12,1)==RES_PARERR);
  assert(usb_disk_read(data,12,0)==RES_PARERR&&usb_disk_write(data,12,0)==RES_PARERR);
  assert(usb_disk_read(data,12,65536)==RES_PARERR&&usb_disk_write(data,12,65536)==RES_PARERR);
  msc_volume_busy=true;
  assert(usb_disk_read(data,12,1)==RES_ERROR&&usb_disk_write(data,12,1)==RES_ERROR);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR&&submitted==0);
  puts("PASS USB absent/argument/busy guards");return 0;
 }
 reject=!strcmp(mode,"reject");stall=!strcmp(mode,"timeout")||!strcmp(mode,"disconnect");
 disconnect=!strcmp(mode,"disconnect");
 if(!strcmp(mode,"scsi"))csw.status=1;
 if(!strcmp(mode,"short"))csw.data_residue=512;
 ticks=UINT32_MAX-500000; /* Exercise deadline wrap on every transfer. */
 result=write?usb_disk_write(data,12,1):usb_disk_read(data,12,1);
 assert(submitted==1&&!msc_volume_busy);
 if(!strcmp(mode,"normal")){
  assert(result==RES_OK&&!msc_io_fault&&pumped==1);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_OK);
 }else{
  assert(result==RES_ERROR&&msc_io_fault);
  if(reject)assert(pumped==0);else assert(pumped>0&&pumped<=20);
  before=submitted;
  /* Neither a late successful callback nor reconnect may clear the fault. */
  csw.status=0;csw.data_residue=0;present=true;msc_dev_addr=1;
  if(stall)completion(1,&reply);
  assert(usb_disk_read(data,12,1)==RES_ERROR&&usb_disk_write(data,12,1)==RES_ERROR);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR&&submitted==before);
 }
 printf("PASS USB %s %s\n",argv[1],mode);return 0;
}
