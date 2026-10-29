/* SPDX-License-Identifier: MIT
 * Real adapter + real TinyUSB wire types, with an asynchronous transport model.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../../src/USB_Host_Driver/msc/usb-checked.c"
static char status_text[80];
static unsigned reports;
void usb_set_status(uint8_t d,const char *fmt,...)
{
 assert(d==1);va_list a;va_start(a,fmt);
 int n=vsnprintf(status_text,sizeof(status_text),fmt,a);va_end(a);
 assert(n>0&&n+3<(int)sizeof(status_text));reports++;
}
static const char *mode;
static bool mounted=true, ready=true;
static uint32_t blocksize=512,ticks;
static unsigned submissions,reads,writes,flushes,pumps;
static void *owned;
static const void *caller;
static msc_cbw_t bus;
static tuh_msc_complete_cb_t callback;
static uintptr_t argument;
static unsigned char disk[1024];
bool tuh_msc_mounted(uint8_t d){assert(d>0&&d<=4);return mounted;}
bool tuh_msc_ready(uint8_t d){assert(d==1);return ready;}
uint32_t tuh_msc_get_block_count(uint8_t d,uint8_t l){assert(d==1&&!l);return 200000;}
uint32_t tuh_msc_get_block_size(uint8_t d,uint8_t l){assert(d==1&&!l);return blocksize;}
bool tuh_msc_scsi_command(uint8_t d,const msc_cbw_t*c,void*b,tuh_msc_complete_cb_t cb,uintptr_t u)
{
 assert(d==1&&c->signature==MSC_CBW_SIGNATURE&&c->cmd_len==10&&!c->lun);
 assert(c->tag&&c->tag==u);submissions++;bus=*c;owned=b;callback=cb;argument=u;
 if(c->command[0]==0x35){
  assert(!b&&!c->total_bytes&&!c->dir);for(unsigned i=1;i<16;i++)assert(!c->command[i]);flushes++;
 }else{
  assert(b&&b!=caller&&c->total_bytes==512&&c->command[8]==1&&!c->command[7]);
  assert(c->command[0]==0x28||c->command[0]==0x2a);
  if(c->command[0]==0x28){assert(c->dir==0x80);reads++;}else{assert(!c->dir);writes++;}
 }
 return strcmp(mode,"reject")!=0;
}
uint32_t time_us_32(void){ticks+=100000;return ticks;}
static void respond(bool late)
{
 msc_csw_t csw={.signature=MSC_CSW_SIGNATURE,.tag=bus.tag};
 tuh_msc_complete_data_t reply={.cbw=&bus,.csw=&csw,.scsi_data=owned,.user_arg=argument};
 if(!late){
  if(!strcmp(mode,"phase"))csw.status=2;
  if((!strcmp(mode,"dirty_syncfail") && writes && bus.command[0]==0x35)||!strcmp(mode,"scsi")||(!strcmp(mode,"syncfail")&&bus.command[0]==0x35))csw.status=1;
  if(!strcmp(mode,"signature"))csw.signature^=1;
  if(!strcmp(mode,"tag"))csw.tag++;
  if(!strcmp(mode,"residue"))csw.data_residue=1;
  if(!strcmp(mode,"userdata"))reply.user_arg++;
  if(!strcmp(mode,"cbw"))bus.command[9]=1;
  if(!strcmp(mode,"pointer"))reply.scsi_data=(void*)caller;
 }
 assert(callback);callback(!late&&!strcmp(mode,"device")?2:1,!late&&!strcmp(mode,"null")?NULL:&reply);
}
void tuh_task_ext(uint32_t timeout_ms,bool in_isr)
{
 (void)timeout_ms;(void)in_isr;pumps++;
 if(!strcmp(mode,"timeout") || (!strcmp(mode,"write_timeout") && bus.command[0]==0x2a))return;
 if(!strcmp(mode,"disconnect")){mounted=false;pm_usb_detach(1);return;}
 if(!strcmp(mode,"reentrant")){
  BYTE nested[512];assert(usb_disk_read(nested,12,1)==RES_ERROR);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR);
 }
 if(bus.command[0]!=0x35){
  uint32_t lba=((uint32_t)bus.command[2]<<24)|((uint32_t)bus.command[3]<<16)|((uint32_t)bus.command[4]<<8)|bus.command[5];
  assert(lba==12||lba==13);
  if(bus.command[0]==0x28)memcpy(owned,disk+(lba-12)*512,512);
  else memcpy(disk+(lba-12)*512,owned,512);
 }
 respond(false);
}
int main(int argc,char**argv)
{
 assert(argc==3);mode=argv[2];bool wr=!strcmp(argv[1],"write"),sync=!strcmp(argv[1],"sync");
 unsigned char *buf=malloc(1024);assert(buf);caller=buf;
 memset(buf,0x5a,1024);for(unsigned i=0;i<1024;i++)disk[i]=(unsigned char)(i*13+7);
 assert(usb_disk_status()==(STA_NODISK|STA_NOINIT)&&usb_disk_read(buf,12,1)==RES_NOTRDY);
 assert(!pm_usb_attach(0)&&!pm_usb_attach(255));blocksize=4096;assert(!pm_usb_attach(1));blocksize=512;
 assert(pm_usb_attach(1)&&!pm_usb_attach(1)&&usb_disk_status()==0);
 ticks=UINT32_MAX-500000;
 if(!strcmp(mode,"guards")){
  DWORD count;WORD size;
  assert(usb_disk_ioctl(GET_SECTOR_COUNT,&count)==RES_OK&&count==200000);
  assert(usb_disk_ioctl(GET_SECTOR_SIZE,&size)==RES_OK&&size==512);
  assert(usb_disk_ioctl(GET_SECTOR_COUNT,NULL)==RES_PARERR);
  assert(usb_disk_read(NULL,12,1)==RES_PARERR&&usb_disk_write(NULL,12,1)==RES_PARERR);
  assert(usb_disk_read(buf,12,0)==RES_PARERR&&usb_disk_read(buf,12,65536)==RES_PARERR);
  assert(usb_disk_read(buf,199999,2)==RES_PARERR&&usb_disk_write(buf,UINT32_MAX,1)==RES_PARERR);
  assert(!submissions);free(buf);puts("PASS USB geometry/absence/bounds guards");return 0;
 }
 if(!strcmp(mode,"tagwrap"))tag=UINT32_MAX;
 if(!strcmp(mode,"notready"))ready=false;
 DRESULT r=sync?usb_disk_ioctl(CTRL_SYNC,NULL):wr?usb_disk_write(buf,12,2):usb_disk_read(buf,12,2);
 if(!strcmp(mode,"dirty_syncfail")){assert(wr&&r==RES_OK&&dirty&&writes==2);r=usb_disk_ioctl(CTRL_SYNC,NULL);}
 bool success=!strcmp(mode,"normal")||!strcmp(mode,"reentrant")||!strcmp(mode,"dirty_detach");
 if(success){
  assert(r==RES_OK&&!failed&&!active);
  if(wr){assert(flushes==1&&writes==2&&!memcmp(disk,buf,1024));}
  else if(!sync)assert(reads==2&&!memcmp(buf,disk,1024));
  if(!strcmp(mode,"dirty_detach")){
   assert(wr);pm_usb_detach(1);assert(failed&&!pm_usb_attach(1));
  }else{
   assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_OK&&!dirty&&sync_supported);
   unsigned prev=tag;pm_usb_detach(1);assert(pm_usb_attach(1));
   assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_OK&&tag>prev);
  }
  free(buf);
 }else{
  assert(r==RES_ERROR&&failed&&reports==1);unsigned before=submissions;
  unsigned expected_reason=FAULT_REPLY;
  if(!strcmp(mode,"scsi")||!strcmp(mode,"phase")||!strcmp(mode,"syncfail")||!strcmp(mode,"dirty_syncfail"))expected_reason=FAULT_STATUS;
  if(!strcmp(mode,"residue"))expected_reason=FAULT_RESIDUE;
  if(!strcmp(mode,"timeout")||!strcmp(mode,"write_timeout"))expected_reason=FAULT_TIMEOUT;
  if(!strcmp(mode,"disconnect"))expected_reason=FAULT_DETACH;
  if(!strcmp(mode,"reject"))expected_reason=FAULT_SUBMIT;
  if(!strcmp(mode,"tagwrap")||!strcmp(mode,"notready"))expected_reason=FAULT_STATE;
  char reason[16];snprintf(reason,sizeof(reason)," f=%u ",expected_reason);assert(strstr(status_text,reason));
  if(!strcmp(mode,"phase"))assert(strstr(status_text," s=2 "));
  if(!strcmp(mode,"syncfail"))assert(strstr(status_text,"op=35 f=2 s=1 r=0 d=0"));
  if(!strcmp(mode,"write_timeout"))assert(strstr(status_text,"op=2a f=6 s=255 r=0 d=1"));
  if(!strcmp(mode,"dirty_syncfail"))assert(strstr(status_text,"op=35 f=2 s=1 r=0 d=1"));
  char first_fault[80];strcpy(first_fault,status_text);
  if(wr&&!strcmp(mode,"write_timeout"))assert(writes==1&&flushes==1&&owned);
  if(wr&&!strcmp(mode,"syncfail"))assert(writes==0&&flushes==1);
  if(!wr&&!sync)for(unsigned i=0;i<1024;i++)assert(buf[i]==0x5a);
  /* Simulated late DMA after caller free: ASan catches any caller pointer leak. */
  free(buf);caller=NULL;
  if(owned){memset(owned,0xc3,512);assert(((unsigned char*)owned)[511]==0xc3);}
  mounted=true;pm_usb_detach(1);assert(!pm_usb_attach(1));
  if(callback)respond(true);
  BYTE again[512];assert(usb_disk_read(again,12,1)==RES_ERROR);
  assert(usb_disk_write(again,12,1)==RES_ERROR&&usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR);
  assert(submissions==before&&failed&&reports==1&&!strcmp(first_fault,status_text));
 }
 printf("PASS owned USB buffer/cache sync: %s %s\n",argv[1],mode);return 0;
}
