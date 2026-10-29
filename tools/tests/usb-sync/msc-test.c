/* SPDX-License-Identifier: MIT
 * Compile and exercise the pinned, patched TinyUSB MSC state machine itself.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../src/lib/tinyusb/src/class/msc/msc_host.c"
static unsigned completed,submitted;
static uint8_t status, data[512];
bool usbh_edpt_xfer_with_callback(uint8_t d,uint8_t e,uint8_t*b,uint16_t n,tuh_xfer_cb_t c,uintptr_t u)
{(void)d;(void)e;(void)b;(void)n;(void)c;(void)u;submitted++;return true;}
static bool done(uint8_t d,const tuh_msc_complete_data_t*r)
{assert(d==1);completed++;status=r->csw->status;return true;}
static void setup(unsigned stage)
{
 msch_init();msch_interface_t*i=get_itf(1);msch_epbuf_t*b=get_epbuf(1);
 memset(b,0,sizeof(*b));i->configured=i->mounted=true;i->stage=stage;
 i->ep_in=0x81;i->ep_out=2;i->complete_cb=done;i->buffer=data;
 b->cbw.signature=MSC_CBW_SIGNATURE;b->cbw.tag=123;b->cbw.total_bytes=512;b->cbw.dir=0x80;
 b->csw.signature=MSC_CSW_SIGNATURE;b->csw.tag=123;
 completed=submitted=0;status=99;
}
int main(void)
{
 unsigned stage,n,tests=0;
 for(stage=MSC_STAGE_CMD;stage<=MSC_STAGE_STATUS;stage++) {
  uint8_t ep=stage==MSC_STAGE_CMD?2:0x81;
  unsigned good=stage==MSC_STAGE_CMD?31:stage==MSC_STAGE_DATA?512:13;
  for(n=0;n<good+2;n++)if(n!=good){
   setup(stage);assert(!msch_xfer_cb(1,ep,XFER_RESULT_SUCCESS,n));
   assert(completed==1&&status==2&&submitted==0&&get_itf(1)->stage==MSC_STAGE_IDLE);tests++;
  }
  for(n=XFER_RESULT_FAILED;n<=XFER_RESULT_STALLED;n++){
   setup(stage);assert(!msch_xfer_cb(1,ep,(xfer_result_t)n,good));assert(completed==1&&status==2&&!submitted);tests++;
  }
  setup(stage);assert(!msch_xfer_cb(1,0x83,XFER_RESULT_SUCCESS,good));assert(completed==1&&status==2&&!submitted);tests++;
 }
 for(n=0;n<4;n++){
  setup(MSC_STAGE_STATUS);msc_csw_t*c=&get_epbuf(1)->csw;
  if(n==0)c->signature^=1;if(n==1)c->tag++;if(n==2)c->data_residue=513;if(n==3)c->status=3;
  assert(!msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13));assert(completed==1&&status==2&&!submitted);tests++;
 }
 for(n=0;n<3;n++){
  setup(MSC_STAGE_STATUS);get_epbuf(1)->csw.status=n;
  assert(msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13));assert(completed==1&&status==n&&!submitted);
  assert(!msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13));assert(completed==1);tests++;
 }
 setup(MSC_STAGE_CMD);get_epbuf(1)->cbw.total_bytes=0;
 assert(msch_xfer_cb(1,2,XFER_RESULT_SUCCESS,31)&&submitted==1&&!completed);
 assert(get_itf(1)->stage==MSC_STAGE_STATUS);tests++;
 for(n=0;n<2;n++){
  setup(MSC_STAGE_CMD);get_epbuf(1)->cbw.dir=n?0x80:0;
  assert(msch_xfer_cb(1,2,XFER_RESULT_SUCCESS,31)&&submitted==1&&!completed);
  assert(get_itf(1)->stage==MSC_STAGE_DATA);
  assert(msch_xfer_cb(1,n?0x81:2,XFER_RESULT_SUCCESS,512)&&submitted==2&&!completed);
  assert(get_itf(1)->stage==MSC_STAGE_STATUS);
  assert(msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13)&&completed==1&&status==0);tests++;
 }
 /* Only bounded REQUEST SENSE accepts short DATA, and only matching residue
  * can turn it into a successful completion. Exercise every length/residue. */
 for(unsigned received=0;received<=19;received++)for(unsigned residue=0;residue<=19;residue++){
  setup(MSC_STAGE_CMD);msc_cbw_t*c=&get_epbuf(1)->cbw;
  c->total_bytes=18;c->cmd_len=6;c->command[0]=3;c->command[4]=18;
  assert(msch_xfer_cb(1,2,XFER_RESULT_SUCCESS,31));
  bool accepted=msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,received);
  if(received>18){assert(!accepted&&completed==1&&status==2);}
  else{
   assert(accepted&&!completed&&get_itf(1)->pm_data_bytes==received);
   get_epbuf(1)->csw.data_residue=residue;
   bool ok=msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13);
   assert(ok==(residue<=18&&received==18-residue));
   assert(completed==1&&status==(ok?0:2));
  }tests++;
 }
 for(unsigned variant=0;variant<5;variant++){
  setup(MSC_STAGE_DATA);msc_cbw_t*c=&get_epbuf(1)->cbw;
  c->total_bytes=18;c->cmd_len=6;c->command[0]=3;c->command[4]=18;
  if(variant==0)c->command[0]=0x28;
  if(variant==1)c->cmd_len=10;
  if(variant==2)c->dir=0;
  if(variant==3)c->total_bytes=19;
  if(variant==4)c->command[4]=19;
  assert(!msch_xfer_cb(1,variant==2?2:0x81,XFER_RESULT_SUCCESS,8));
  assert(completed==1&&status==2);tests++;
 }
 /* MODE SENSE(6/10): a clean status 1 may permit the alternate query.
  * Therefore validate DATA/residue agreement for BOTH statuses 0 and 1. */
 for(unsigned ten=0;ten<2;ten++)for(unsigned st=0;st<2;st++)
 for(unsigned received=0;received<=65;received++)for(unsigned residue=0;residue<=65;residue++){
  setup(MSC_STAGE_CMD);msc_cbw_t*c=&get_epbuf(1)->cbw;
  c->total_bytes=64;c->cmd_len=ten?10:6;c->command[0]=ten?0x5a:0x1a;
  c->command[1]=8;c->command[2]=8;c->command[ten?8:4]=64;
  assert(msch_xfer_cb(1,2,XFER_RESULT_SUCCESS,31));
  bool accepted=msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,received);
  if(received>64){assert(!accepted&&completed==1&&status==2);}
  else{
   assert(accepted&&!completed&&get_itf(1)->pm_data_bytes==received);
   get_epbuf(1)->csw.data_residue=residue;get_epbuf(1)->csw.status=st;
   bool ok=msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13);
   assert(ok==(residue<=64&&received==64-residue));
   assert(completed==1&&status==(ok?st:2));
  }tests++;
 }
 for(unsigned ten=0;ten<2;ten++)for(unsigned variant=0;variant<8;variant++){
  setup(MSC_STAGE_DATA);msc_cbw_t*c=&get_epbuf(1)->cbw;
  c->total_bytes=64;c->cmd_len=ten?10:6;c->command[0]=ten?0x5a:0x1a;
  c->command[1]=8;c->command[2]=8;c->command[ten?8:4]=64;
  if(variant==0)c->command[0]=0x28;
  if(variant==1)c->cmd_len=12;
  if(variant==2)c->dir=0;
  if(variant==3)c->total_bytes=65;
  if(variant==4)c->command[ten?8:4]=65;
  if(variant==5)c->command[1]=0;
  if(variant==6)c->command[2]=0x48; /* Changeable, not current. */
  if(variant==7)c->command[3]=1;
  assert(!msch_xfer_cb(1,variant==2?2:0x81,XFER_RESULT_SUCCESS,8));
  assert(completed==1&&status==2);tests++;
 }
 printf("PASS actual TinyUSB MSC: %u completion/length/status cases\n",tests);return 0;
}
