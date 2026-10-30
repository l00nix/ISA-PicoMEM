/* SPDX-License-Identifier: MIT
 * Read-only probe through the actual adapter and TinyUSB MSC state machine.
 * Endpoint completions/device replies are modelled; no hardware access.
 */
#define PICOMEM_EXPERIMENTAL_USB_CACHE_PROBE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../../src/lib/tinyusb/src/class/msc/msc_host.c"
#include "../../../src/USB_Host_Driver/msc/usb-checked.c"
static unsigned variant, target, ticks, commands, queries[2], read_commands;
static bool pending, busy_out, busy_in, claimed;
static uint8_t ep_now, stage_now, op_now, *buffer_now;
static unsigned length_now;
static char last_fault[80];
static FILE *probe_image;
static bool read_fault, release_timeout;
static unsigned host_snapshots, task_calls;
enum { OFF, ON, HEADER, ZERO_HEADER, SHORT, BLOCK, LONG_BLOCK, ODD_BLOCK,
       MEDIUM, SPF, WRONG_PAGE, PAGE_SHORT, PAGE_LONG, ADVERTISED_LONG,
       BAD_STATUS, PHASE, BAD_TAG, BAD_SIGNATURE, SHORT_CSW, STALL, TIMEOUT,
       DETACH, REJECT, SHORT_MODE10, MODE6_OFF, MODE6_ON, MODE6_BAD,
       BAD_RESIDUE, BAD_COUNT, ZERO_SHORT, PADDED, FULL_UNKNOWN, TIME_DATA, TIME_STATUS, TIME6_CMD, TIME6_DATA,
       TIME6_STATUS, NO_HOST, NO_PENDING, MAX_HOST, CASE_COUNT };
static bool timed_variant(void)
{return variant==TIMEOUT || (variant>=TIME_DATA && variant<CASE_COUNT);}
static unsigned timeout_stage(void)
{return variant==TIME_DATA||variant==TIME6_DATA ? MSC_STAGE_DATA :
        variant==TIME_STATUS||variant==TIME6_STATUS ? MSC_STAGE_STATUS : MSC_STAGE_CMD;}
static bool mode6_timeout(void)
{return variant>=TIME6_CMD&&variant<=TIME6_STATUS;}
static bool unsafe(void)
{
 return variant==PHASE || variant==BAD_TAG || variant==BAD_SIGNATURE ||
        variant==SHORT_CSW || variant==STALL || timed_variant() ||
        variant==DETACH || variant==REJECT || variant==MODE6_BAD ||
        variant==BAD_RESIDUE || variant==BAD_COUNT;
}
uint32_t time_us_32(void){ticks+=1000;return ticks;}
void usb_set_status(uint8_t d,const char *fmt,...)
{assert(d==target);va_list a;va_start(a,fmt);vsnprintf(last_fault,80,fmt,a);va_end(a);}
bool usbh_edpt_busy(uint8_t d,uint8_t ep)
{assert(d==target);return ep&0x80?busy_in:busy_out;}
bool usbh_edpt_claim(uint8_t d,uint8_t ep)
{assert(d==target&&ep==1&&!claimed&&!busy_out);claimed=true;return true;}
bool usbh_edpt_release(uint8_t d,uint8_t ep)
{assert(d==target&&ep==1);claimed=false;return true;}
bool pm_usb_host_snapshot(uint8_t d,uint8_t ep,pm_usb_host_state_t *s)
{
 assert(d==target&&(ep==1||ep==0x81));host_snapshots++;
 memset(s,variant==MAX_HOST?255:0,sizeof(*s));
 if(variant!=MAX_HOST){s->frame=host_snapshots;s->buffer_ctrl=0x12345678;}
 return variant!=NO_HOST;
}
static void le32(uint8_t *p,uint32_t n)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
bool usbh_edpt_xfer_with_callback(uint8_t d,uint8_t ep,uint8_t *b,uint16_t n,
                                  tuh_xfer_cb_t cb,uintptr_t arg)
{
 assert(d==target&&!cb&&!arg&&!pending);
 unsigned stage=get_itf(d)->stage;
 if(stage==MSC_STAGE_CMD) {
  assert(ep==1&&n==31&&claimed);op_now=b[15];commands++;
  /* Whitelist: no write, flush, mode-select, sense or reset in this probe. */
  assert(op_now==0x28||op_now==0x5a||op_now==0x1a);
  if(op_now==0x28)read_commands++;
  else {
   unsigned ten=op_now==0x5a;queries[ten]++;
   uint8_t expected[31]={0x55,0x53,0x42,0x43};
   le32(expected+4,tag);le32(expected+8,64);expected[12]=0x80;
   expected[14]=ten?10:6;expected[15]=ten?0x5a:0x1a;
   expected[16]=expected[17]=8;expected[ten?23:19]=64;
   assert(!memcmp(b,expected,31));
   if(variant==REJECT)return false;
  }
  claimed=false;
 }
 if(ep==1){assert(!busy_out);busy_out=true;}else{assert(ep==0x81&&!busy_in);busy_in=true;}
 pending=true;ep_now=ep;stage_now=stage;buffer_now=b;length_now=n;
 return true;
}
static unsigned reply_bytes, reply_residue, reply_status;
static void mode_reply(uint8_t *b,unsigned *n)
{
 bool ten=op_now==0x5a;unsigned h=ten?8:4,offset=h,total=h+20;
 memset(b,0,64);reply_status=reply_residue=0;
 bool fallback=variant==MODE6_OFF||variant==MODE6_ON||variant==MODE6_BAD||variant==SHORT_MODE10||mode6_timeout();
 if(variant==BLOCK){offset+=8;total+=8;b[ten?7:3]=8;}
 b[offset]=8;b[offset+1]=18;b[offset+2]=(variant==ON||(!ten&&variant==MODE6_ON))?4:0;
 if(variant==HEADER||variant==ZERO_HEADER||variant==ZERO_SHORT||(fallback&&ten))total=h;
 b[ten?1:0]=total-(ten?2:1);
 if(variant==ZERO_HEADER||variant==ZERO_SHORT)b[ten?1:0]=0;
 *n=total;reply_residue=64-total;
 if(variant==SHORT)*n=h-1;
 if(variant==ZERO_SHORT){*n=h;reply_residue=0;}
 if(variant==SHORT_MODE10&&ten)*n=h-1;
 if(variant==BAD_STATUS||(fallback&&ten&&variant!=SHORT_MODE10)) {
  reply_status=1;*n=0;reply_residue=64;
 }
 if(variant==LONG_BLOCK&&ten)b[4]=1;
 if(variant==LONG_BLOCK&&!ten)b[3]=255;
 if(variant==ODD_BLOCK)b[ten?7:3]=1;
 if(variant==MEDIUM)b[ten?2:1]=1;
 if(variant==SPF)b[offset]|=0x40;
 if(variant==WRONG_PAGE)b[offset]=6;
 if(variant==PAGE_SHORT)b[offset+1]=1;
 if(variant==PAGE_LONG)b[offset+1]=255;
 if(variant==ADVERTISED_LONG)b[ten?1:0]=63;
 if(variant==PADDED)reply_residue=0;
 if(variant==FULL_UNKNOWN){*n=64;reply_residue=0;b[ten?1:0]=64-(ten?2:1);b[offset]=6;}
 if(variant==BAD_COUNT){*n=65;reply_residue=0;}
 if(variant==BAD_RESIDUE)reply_residue=65;
 reply_bytes=*n;
}
void tuh_task_ext(uint32_t timeout_ms,bool in_isr)
{
 (void)timeout_ms;assert(!in_isr&&pending);task_calls++;
 bool query=op_now!=0x28;
 if(query&&timed_variant()&&!release_timeout&&stage_now==timeout_stage()&&
    (!mode6_timeout()||op_now==0x1a)) {
  if(variant==NO_PENDING)get_itf(target)->pm_pending.tag=0;
  return;
 }
 if(query&&variant==DETACH){get_itf(target)->mounted=false;pm_usb_detach(target);return;}
 unsigned n=length_now;uint8_t *b=buffer_now,ep=ep_now,stage=stage_now;
 xfer_result_t event=XFER_RESULT_SUCCESS;
 pending=false;if(ep==1)busy_out=false;else busy_in=false;
 if(stage==MSC_STAGE_DATA) {
  if(query)mode_reply(b,&n);
  else if(probe_image) {
   const uint8_t *c=get_epbuf(target)->cbw.command;
   uint32_t sector=(uint32_t)c[2]<<24|(uint32_t)c[3]<<16|(uint32_t)c[4]<<8|c[5];
   assert(n==512&&!fseek(probe_image,(long)sector*512,SEEK_SET));
   assert(fread(b,512,1,probe_image)==1);
  } else memset(b,0x5a,n);
 } else if(stage==MSC_STAGE_STATUS) {
  memset(b,0,13);b[0]=0x55;b[1]=0x53;b[2]=0x42;b[3]=0x53;le32(b+4,tag);
  if(!query&&read_fault)b[12]=1;
  if(query) {
   le32(b+8,reply_residue);b[12]=reply_status;
   if(variant==PHASE)b[12]=2;
   if(variant==BAD_TAG)b[4]^=1;
   if(variant==BAD_SIGNATURE)b[0]^=1;
   if(variant==SHORT_CSW)n=12;
   if(variant==STALL||(variant==MODE6_BAD&&op_now==0x1a))event=XFER_RESULT_STALLED;
  }
 }
 (void)msch_xfer_cb(target,ep,event,n);
}
static void check_report(void)
{
 char frame[1026],original[1026];unsigned before=commands, snaps=host_snapshots, tasks=task_calls;
 assert(pm_usb_cache_probe_status(frame,1024));
 assert((unsigned char)frame[0]<=8 && (unsigned char)frame[0]>=4);
 unsigned end=1;
 for(unsigned i=0;i<(unsigned char)frame[0];i++) {assert(strlen(frame+end)<80);end+=strlen(frame+end)+1;}
 if(timed_variant()) {
  const char *row[8];unsigned pos=1;
  for(unsigned k=0;k<(unsigned char)frame[0];k++){row[k]=frame+pos;pos+=strlen(row[k])+1;}
  assert(strstr(row[1],"USB writes blocked; no flush sent."));
  assert(strstr(row[1],mode6_timeout()?"MODE6 time":"MODE10 time MODE6 -"));
  assert(strstr(row[2],mode6_timeout()?"op=1a":"op=5a"));
  if(variant==NO_PENDING) {
   assert((unsigned char)frame[0]==5&&strstr(row[2],"snapshot=unavailable"));
  } else {
   char expected[80];snprintf(expected,80,"stage=%u submit=1",timeout_stage());
   assert(strstr(row[2],expected));
   snprintf(expected,80,"ep=%02x want=%u",timeout_stage()==MSC_STAGE_CMD?1:0x81,
            timeout_stage()==MSC_STAGE_CMD?31:timeout_stage()==MSC_STAGE_DATA?64:13);
   assert(strstr(row[3],expected));
   if(variant==NO_HOST)assert((unsigned char)frame[0]==5);
   else {assert((unsigned char)frame[0]==8);assert(strstr(row[5],"HCD slot="));
         assert(strstr(row[6],variant==MAX_HOST?"bc=ffffffff":"bc=12345678"));}
  }
 }
 assert(strstr(frame+1,unsafe()?"I/O=quarantined":"I/O=read-only"));
 const char *state=variant==ON||variant==MODE6_ON?"enabled":
  variant==OFF||variant==BLOCK||variant==PADDED||variant==MODE6_OFF||variant==SHORT_MODE10?"disabled":"unknown";
 assert(strstr(frame+1,state));
 for(unsigned cap=0;cap<=1024;cap++) {
  memset(frame,'X',sizeof(frame));memcpy(original,frame,sizeof(frame));
  bool ok=pm_usb_cache_probe_status(frame,cap);
  assert(ok==(cap>=end));
  if(!ok)assert(!memcmp(frame,original,sizeof(frame)));
  else assert(!memcmp(frame+end,original+end,sizeof(frame)-end));
 }
 assert(!pm_usb_cache_probe_status(NULL,1024)&&commands==before);
 assert(host_snapshots==snaps&&task_calls==tasks);
}
int main(int argc,char **argv)
{
 assert(argc==3);variant=atoi(argv[1]);target=atoi(argv[2]);assert(variant<CASE_COUNT);
 msch_init();msch_interface_t *i=get_itf(target);
 i->configured=i->mounted=true;i->ep_in=0x81;i->ep_out=1;
 i->capacity[0].block_count=200000;i->capacity[0].block_size=512;
 assert(pm_usb_attach(target));char frame[1024];memset(frame,'X',sizeof(frame));
 assert(!pm_usb_cache_probe_status(frame,sizeof(frame))&&frame[0]=='X');
 uint8_t data[512];assert(usb_disk_read(data,12,1)==RES_OK&&!queries[0]&&!queries[1]);
 assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_WRPRT&&!queries[0]&&!queries[1]);
 DRESULT r=usb_disk_write(data,12,1);
 assert(r==(unsafe()?RES_ERROR:RES_WRPRT));
 assert(probe_attempted&&failed==unsafe()&&!dirty&&!sync_supported);
 assert(queries[1]==1&&queries[0]<=1);check_report();
 unsigned before=commands;
 assert(usb_disk_write(data,12,1)==r&&commands==before);
 assert(usb_disk_ioctl(CTRL_SYNC,NULL)==r&&commands==before);
 if(unsafe()) {
  assert(usb_disk_read(data,12,1)==RES_ERROR&&commands==before);
  assert(!pm_usb_attach(target));
  /* Late completion cannot publish changed evidence or reopen I/O. */
  char prior[1024];memset(prior,0,sizeof(prior));assert(pm_usb_cache_probe_status(prior,sizeof(prior)));
  memset(frame,0,sizeof(frame));cache_waiting=false;
  (void)cache_complete(target,NULL);
  if(timed_variant()) {
   release_timeout=true;unsigned n=0;
   while(pending){assert(n++<3);tuh_task_ext(0,false);}
   assert(commands==before);
  }
  /* Both unrelated report buffers and later register samples may change. */
  memset(transport_lines,'Z',sizeof(transport_lines));
  assert(pm_usb_cache_probe_status(frame,sizeof(frame))&&!memcmp(prior,frame,1024));
 } else {
  assert(usb_disk_read(data,12,1)==RES_OK);
  assert(!pm_usb_attach(target));
  pm_usb_detach(target);assert(!pm_usb_cache_probe_status(frame,sizeof(frame)));
  assert(pm_usb_attach(target));
  assert(!probe_attempted&&!pm_usb_cache_probe_status(frame,sizeof(frame)));
  assert(usb_disk_write(data,12,1)==RES_WRPRT);
  read_fault=true;assert(usb_disk_read(data,12,1)==RES_ERROR);
  assert(pm_usb_cache_probe_status(frame,sizeof(frame))&&strstr(frame+1,"I/O=quarantined"));
 }
 printf("PASS cache probe variant=%u device=%u queries=%u/%u\n",variant,target,queries[1],queries[0]);
 return 0;
}
