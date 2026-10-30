/* SPDX-License-Identifier: MIT
 * Actual adapter + pinned/patched TinyUSB MSC, joined at the host endpoint API.
 * Only controller completions and device replies are modelled. Golden bytes are
 * constructed independently of msc_cbw_t layout (USB BOT 1.0 sections 5/6).
 * This does not emulate native RP2350 registers or prove physical USB traffic.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../../src/lib/tinyusb/src/class/msc/msc_host.c"
#include "../../../src/USB_Host_Driver/msc/usb-checked.c"

static unsigned scenario, dev, ticks, cbws, reads, writes, flushes, senses, modes;
static uint32_t next_tag;
static uint8_t out_ep, in_ep, opcode;
static char report[80];
static bool claimed, busy_out, busy_in, delivering_late;
static struct { bool pending; uint8_t ep, stage; uint8_t *buf; unsigned len; } transfer;
static uint8_t payload[512];
enum { NORMAL, SYNC_ONLY, SENSE26, SENSE20, TIMEOUT, LATE_OK,
       BAD_TAG, BAD_SIGNATURE, SHORT_STATUS, STATUS_STALL, SHORT_CMD,
       REJECT_CMD, REJECT_STATUS, WRITE_FAIL, DIRTY_SYNC_FAIL, READ_FAIL,
       WRONG_ENDPOINT, PHASE, CASES };

void usb_set_status(uint8_t d,const char *fmt,...)
{
 assert(d==dev); va_list ap; va_start(ap,fmt);
 int n=vsnprintf(report,sizeof(report),fmt,ap);va_end(ap);
 assert(n>=0 && n+3<(int)sizeof(report));
}
uint32_t time_us_32(void) { ticks+=1000; return ticks; }
bool usbh_edpt_busy(uint8_t d,uint8_t ep)
{ assert(d==dev); return ep==out_ep?busy_out:busy_in; }
bool usbh_edpt_claim(uint8_t d,uint8_t ep)
{ assert(d==dev && ep==out_ep && !claimed && !busy_out);claimed=true;return true; }
bool usbh_edpt_release(uint8_t d,uint8_t ep)
{ assert(d==dev && ep==out_ep);claimed=false;return true; }
bool pm_usb_host_snapshot(uint8_t d,uint8_t ep,pm_usb_host_state_t *s)
{ assert(d==dev && (ep==in_ep||ep==out_ep));memset(s,0,sizeof(*s));return true; }

static void le32(uint8_t *b,uint32_t v)
{ for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(v>>(8*i)); }
static void check_cbw(const uint8_t *b,unsigned n)
{
 assert(n==31);opcode=b[15];
 uint8_t golden[31]={0x55,0x53,0x42,0x43};
 le32(golden+4,++next_tag); golden[14]=10;golden[15]=opcode;
 switch(opcode) {
 case 0x35: flushes++;break; /* No DATA, OUT flag=0, LUN=0, all CDB args zero. */
 case 0x28: case 0x2a:
  le32(golden+8,512);golden[12]=opcode==0x28?0x80:0;
  golden[18]=1;golden[19]=0x23;golden[20]=0x45;golden[23]=1;
  if(opcode==0x28)reads++;else writes++;
  break;
 case 0x03:
  senses++;le32(golden+8,18);golden[12]=0x80;golden[14]=6;golden[19]=18;break;
 case 0x1a: case 0x5a:
  modes++;le32(golden+8,64);golden[12]=0x80;
  golden[14]=opcode==0x1a?6:10;golden[16]=8;golden[17]=8;
  golden[opcode==0x1a?19:23]=64;break;
 default: assert(!"unexpected command");
 }
 assert(!memcmp(b,golden,sizeof(golden)));cbws++;
 /* Initial mutation must follow a successful flush. No write after a failed one. */
 if(opcode==0x2a)assert(sync_supported && flushes==1 && !failed);
}
bool usbh_edpt_xfer_with_callback(uint8_t d,uint8_t ep,uint8_t *b,uint16_t n,
                                  tuh_xfer_cb_t cb,uintptr_t arg)
{
 assert(d==dev && !cb && !arg && !transfer.pending);
 unsigned stage=get_itf(d)->stage;
 if(stage==MSC_STAGE_CMD) {
  assert(ep==out_ep && claimed);check_cbw(b,n);
  if(scenario==REJECT_CMD && opcode==0x35)return false;
  claimed=false;
 } else if(stage==MSC_STAGE_DATA) {
  assert(opcode!=0x35); /* No accidental zero-length DATA transfer. */
  assert(ep==(opcode==0x2a?out_ep:in_ep));
  if(opcode==0x2a)assert(n==512 && !memcmp(b,payload,512));
 } else {
  assert(stage==MSC_STAGE_STATUS && ep==in_ep && n==13);
  if(scenario==REJECT_STATUS && opcode==0x35)return false;
 }
 assert(ep==out_ep?!busy_out:!busy_in);
 if(ep==out_ep)busy_out=true;else busy_in=true;
 transfer.pending=true;transfer.ep=ep;transfer.stage=stage;transfer.buf=b;transfer.len=n;
 return true;
}
void tuh_task_ext(uint32_t timeout_ms,bool in_isr)
{
 (void)timeout_ms;assert(!in_isr);
 if(!transfer.pending) { assert(scenario==REJECT_STATUS);return; }
 if(opcode==0x35 && transfer.stage==MSC_STAGE_STATUS &&
    (scenario==TIMEOUT||scenario==LATE_OK) && !delivering_late)return;
 unsigned n=transfer.len;uint8_t ep=transfer.ep;unsigned stage=transfer.stage;
 uint8_t *b=transfer.buf;xfer_result_t event=XFER_RESULT_SUCCESS;
 transfer.pending=false;
 if(ep==out_ep)busy_out=false;else busy_in=false;
 if(stage==MSC_STAGE_CMD) {
  if(scenario==SHORT_CMD && opcode==0x35)n=30;
 } else if(stage==MSC_STAGE_DATA) {
  if(opcode==0x28)memcpy(b,payload,512);
  else if(opcode==0x03) {
   memset(b,0,18);b[0]=0x70;b[2]=5;b[7]=10;b[12]=scenario==SENSE26?0x26:0x20;
  } else if(opcode==0x1a||opcode==0x5a) {
   /* Reproduce a successful header-only response with zero CSW residue. */
   n=opcode==0x1a?4:8;memset(b,0,64);b[opcode==0x1a?0:1]=n-(opcode==0x1a?1:2);
  }
 } else {
  memset(b,0,13);b[0]=0x55;b[1]=0x53;b[2]=0x42;b[3]=0x53;le32(b+4,next_tag);
  if((opcode==0x35 && (scenario==SENSE26||scenario==SENSE20)) ||
     (opcode==0x2a && scenario==WRITE_FAIL) ||
     (opcode==0x35 && writes && scenario==DIRTY_SYNC_FAIL) ||
     (opcode==0x28 && scenario==READ_FAIL)) b[12]=1;
  if(opcode==0x35) {
   if(scenario==BAD_TAG)b[4]^=1;
   if(scenario==BAD_SIGNATURE)b[0]^=1;
   if(scenario==SHORT_STATUS)n=12;
   if(scenario==STATUS_STALL)event=XFER_RESULT_STALLED;
   if(scenario==WRONG_ENDPOINT)ep=in_ep^1;
   if(scenario==PHASE)b[12]=2;
  }
 }
 (void)msch_xfer_cb(dev,ep,event,n);
}
int main(int argc,char **argv)
{
 assert(argc==4);scenario=atoi(argv[1]);dev=atoi(argv[2]);tag=strtoul(argv[3],NULL,0);
 assert(scenario<CASES && (dev==1||dev==4));next_tag=tag;
 out_ep=dev==1?1:2;in_ep=dev==1?0x81:0x83;
 msch_init();msch_interface_t *itf=get_itf(dev);itf->configured=itf->mounted=true;
 itf->ep_in=in_ep;itf->ep_out=out_ep;itf->capacity[0].block_size=512;
 itf->capacity[0].block_count=200000;
 assert(sizeof(msc_cbw_t)==31 && sizeof(msc_csw_t)==13);
 for(unsigned i=0;i<512;i++)payload[i]=(uint8_t)(i*13+7);
 assert(pm_usb_attach(dev));uint8_t readback[512];memset(readback,0xcc,512);
 DRESULT r=usb_disk_read(readback,0x12345,1);
 if(scenario==READ_FAIL) {
  assert(r==RES_ERROR && !flushes && !writes && !senses);
  for(unsigned i=0;i<512;i++)assert(readback[i]==0xcc);
 } else {
  assert(r==RES_OK && !memcmp(readback,payload,512));
  r=scenario==SYNC_ONLY?usb_disk_ioctl(CTRL_SYNC,NULL):usb_disk_write(payload,0x12345,1);
  if(scenario==NORMAL||scenario==SYNC_ONLY||scenario==DIRTY_SYNC_FAIL) {
   assert(r==RES_OK && !failed);
   if(scenario!=SYNC_ONLY) {
    assert(writes==1 && dirty);r=usb_disk_ioctl(CTRL_SYNC,NULL);
    assert((r==RES_OK)==(scenario==NORMAL));
    assert(scenario==NORMAL?!dirty:dirty);
   } else assert(!writes && !dirty);
  } else assert(r==RES_ERROR && failed);
 }
 if(failed) {
  if(scenario==SENSE26)assert(strstr(report,"rs=70:5/26/00") && senses==1 && !modes);
  if(scenario==SENSE20)assert(strstr(report,"c6=page c10=page") && senses==1 && modes==2);
  if(scenario!=SENSE20 && scenario!=SENSE26)assert(!senses && !modes);
  assert(writes==(scenario==WRITE_FAIL||scenario==DIRTY_SYNC_FAIL));
  if(scenario==TIMEOUT||scenario==LATE_OK) {
   assert(transfer.pending && late_observing);
   if(scenario==LATE_OK) {
    delivering_late=true;tuh_task_ext(0,false);
    assert(strstr(report,"obs=csw os=0"));
   } else {ticks+=LATE_WINDOW_US;pm_usb_observe_late();assert(strstr(report,"obs=none"));}
   assert(failed && !late_observing && !sync_supported && !dirty);
  }
  unsigned before=cbws;
  assert(usb_disk_read(readback,0x12345,1)==RES_ERROR);
  assert(usb_disk_write(payload,0x12345,1)==RES_ERROR);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR && cbws==before);
 } else assert(scenario==NORMAL||scenario==SYNC_ONLY);
 printf("PASS wire scenario=%u device=%u commands=%u\n",scenario,dev,cbws);
 return 0;
}
