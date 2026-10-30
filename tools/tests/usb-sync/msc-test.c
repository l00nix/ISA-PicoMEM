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
bool usbh_edpt_claim(uint8_t d,uint8_t ep){assert(d==1&&ep==2);return true;}
bool usbh_edpt_release(uint8_t d,uint8_t ep){assert(d==1&&ep==2);return true;}
static bool done(uint8_t d,const tuh_msc_complete_data_t*r)
{
 assert(d==1);completed++;status=r->csw->status;
 tuh_msc_checked_fault_t f;
 assert(!tuh_msc_get_checked_fault(0,123,&f));
 assert(!tuh_msc_get_checked_fault(255,123,&f));
 assert(!tuh_msc_get_checked_fault(1,124,&f));
 assert(!tuh_msc_get_checked_fault(1,123,NULL));
 assert(tuh_msc_get_checked_fault(1,123,&f)==(status==2));
 if(status==2){assert(f.tag==123&&f.reason>=1&&f.reason<=9);}
 return true;
}
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
   assert(completed==1&&status==2&&submitted==0&&get_itf(1)->stage==MSC_STAGE_IDLE);
   tuh_msc_checked_fault_t f;assert(tuh_msc_get_checked_fault(1,123,&f));
   assert(f.reason==3&&f.stage==stage&&f.actual==n&&f.expected==good);
   assert(f.endpoint==ep&&f.expected_endpoint==ep&&f.event==0);
   assert(f.checks==0&&f.csw_status==255&&f.residue==UINT32_MAX);
   assert(!msch_xfer_cb(1,ep,XFER_RESULT_STALLED,good));
   tuh_msc_checked_fault_t again;assert(tuh_msc_get_checked_fault(1,123,&again));
   assert(!memcmp(&f,&again,sizeof(f)));tests++;
  }
  for(n=XFER_RESULT_FAILED;n<=XFER_RESULT_INVALID;n++){
   setup(stage);assert(!msch_xfer_cb(1,ep,(xfer_result_t)n,good));assert(completed==1&&status==2&&!submitted);
   tuh_msc_checked_fault_t f;assert(tuh_msc_get_checked_fault(1,123,&f));
   assert(f.reason==1&&f.event==n&&f.stage==stage&&f.actual==good&&f.expected==good);
   assert(!f.checks&&f.csw_status==255&&f.residue==UINT32_MAX);tests++;
  }
  setup(stage);assert(!msch_xfer_cb(1,0x83,XFER_RESULT_SUCCESS,good));assert(completed==1&&status==2&&!submitted);
  tuh_msc_checked_fault_t f;assert(tuh_msc_get_checked_fault(1,123,&f));
  assert(f.reason==2&&f.endpoint==0x83&&f.expected_endpoint==ep&&!f.checks);tests++;
 }
 for(n=0;n<4;n++){
  setup(MSC_STAGE_STATUS);msc_csw_t*c=&get_epbuf(1)->csw;
  if(n==0)c->signature^=1;if(n==1)c->tag++;if(n==2)c->data_residue=513;if(n==3)c->status=3;
  assert(!msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13));assert(completed==1&&status==2&&!submitted);
  tuh_msc_checked_fault_t f;assert(tuh_msc_get_checked_fault(1,123,&f));
  assert(f.reason==n+4&&f.stage==3&&(f.checks&1));
  assert(!(f.checks&(2<<n))&&f.csw_status==(n==3?3:0)&&f.residue==(n==2?513:0));tests++;
 }
 for(n=0;n<3;n++){
  setup(MSC_STAGE_STATUS);get_epbuf(1)->csw.status=n;
  assert(msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13));assert(completed==1&&status==n&&!submitted);
  tuh_msc_checked_fault_t f;assert(tuh_msc_get_checked_fault(1,123,&f)==(n==2));
  if(n==2)assert(f.reason==9&&f.checks==31&&f.csw_status==2&&f.residue==0&&f.actual==13);
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
 /* MODE SENSE(6/10): status 0 accepts independently bounded actual/residue;
  * status 1 still needs agreement. Raw evidence must never be normalized. */
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
   assert(ok==(residue<=64&&(!st||received==64-residue)));
   assert(completed==1&&status==(ok?st:2));
   tuh_msc_checked_fault_t f;
   assert(tuh_msc_get_checked_fault(1,123,&f)==!ok);
   assert(tuh_msc_get_checked_reply(1,123,&f));
   assert(f.data_bytes==received&&f.residue==residue&&f.csw_status==st);
   if(ok)assert(!f.reason&&f.checks==(received==64-residue?63:31));
   if(!ok){assert(f.reason==(residue>64?6:8)&&f.data_bytes==received&&f.residue==residue&&f.opcode==(ten?0x5a:0x1a));}
  }tests++;
 }
 for(unsigned ten=0;ten<2;ten++)for(unsigned variant=0;variant<25;variant++){
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
  if(variant==8)c->lun=1;
  if(variant>=9)c->command[variant-9]^=1; /* every CDB byte */
  assert(!msch_xfer_cb(1,variant==2?2:0x81,XFER_RESULT_SUCCESS,8));
  assert(completed==1&&status==2);tests++;
 }
 /* MODE SENSE snapshots: every bounded length, independent of CSW residue,
  * plus wrong endpoint/event/oversize/null-buffer exclusions and late overwrite. */
 for(unsigned ten=0;ten<2;ten++)for(unsigned received=0;received<=65;received++)
 for(unsigned variant=0;variant<6;variant++) {
  setup(MSC_STAGE_DATA);msc_cbw_t*c=&get_epbuf(1)->cbw;
  c->total_bytes=64;c->cmd_len=ten?10:6;c->command[0]=ten?0x5a:0x1a;
  c->command[1]=8;c->command[2]=8;c->command[ten?8:4]=64;
  for(unsigned j=0;j<sizeof(data);j++)data[j]=(uint8_t)(j+1);
  if(variant==3)get_itf(1)->buffer=NULL;
  if(variant==4)c->command[2]=0x48; /* Not this bounded current-page query. */
  unsigned ep=variant==1?0x82:0x81;
  xfer_result_t event=variant==2?XFER_RESULT_FAILED:XFER_RESULT_SUCCESS;
  bool ok=msch_xfer_cb(1,ep,event,received);
  bool sampled=variant!=1&&variant!=2&&variant!=3&&variant!=4&&received<=64;
  if(ok){
   memset(data,0xee,sizeof(data)); /* No read from this changed buffer at CSW. */
   get_epbuf(1)->csw.data_residue=variant==5?0:64-received;
   (void)msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,13);
  }
  tuh_msc_checked_fault_t f;
  assert(tuh_msc_get_checked_reply(1,123,&f));
  assert(!tuh_msc_get_checked_reply(0,123,&f));
  assert(!tuh_msc_get_checked_reply(255,123,&f));
  assert(!tuh_msc_get_checked_reply(1,124,&f));
  assert(!tuh_msc_get_checked_reply(1,123,NULL));
  unsigned n=sampled?(received>16?16:received):0;
  assert(f.sample_length==n);
  for(unsigned j=0;j<16;j++)assert(f.sample[j]==(j<n?j+1:0));
  if(variant==5&&received<64)assert(!f.reason&&f.checks==31&&f.data_bytes==received&&f.residue==0);
  if(variant==0&&received<=64)assert(f.reason==0&&f.checks==63);
  assert(!msch_xfer_cb(1,0x81,XFER_RESULT_SUCCESS,64));
  tuh_msc_checked_fault_t after;assert(tuh_msc_get_checked_reply(1,123,&after));
  assert(!memcmp(&after,&f,sizeof(f)));tests++;
 }
 /* The next command resets only the driver snapshot; no additional command
  * is submitted by the getter. The adapter owns its permanent copy. */
 setup(MSC_STAGE_DATA);assert(!msch_xfer_cb(1,0x81,XFER_RESULT_STALLED,0));
 tuh_msc_checked_fault_t f;assert(tuh_msc_get_checked_fault(1,123,&f));
 msc_cbw_t q=get_epbuf(1)->cbw;q.tag=456;
 assert(tuh_msc_scsi_command(1,&q,data,done,456));
 assert(!tuh_msc_get_checked_fault(1,123,&f)&&!tuh_msc_get_checked_fault(1,456,&f));
 assert(submitted==1);
 assert(!tuh_msc_get_checked_reply(1,456,&f));
 assert(!get_itf(1)->pm_sample_length);tests++;
 printf("PASS actual TinyUSB MSC: %u completion/length/status cases\n",tests);return 0;
}
