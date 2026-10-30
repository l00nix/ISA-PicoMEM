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
static bool mounted=true, ready=true, established_failure;
static uint32_t blocksize=512,ticks;
static unsigned submissions,reads,writes,flushes,pumps,senses,caches[2];
static void *owned;
static const void *caller;
static msc_cbw_t bus;
static bool snapshot_max, snapshot_missing;
static unsigned reply_calls;
static tuh_msc_checked_fault_t model_reply;
static tuh_msc_complete_cb_t callback;
static uintptr_t argument;
static unsigned char disk[1024];
bool tuh_msc_mounted(uint8_t d){assert(d>0&&d<=4);return mounted;}
bool tuh_msc_ready(uint8_t d){assert(d==1);return ready;}
uint32_t tuh_msc_get_block_count(uint8_t d,uint8_t l){assert(d==1&&!l);return 200000;}
uint32_t tuh_msc_get_block_size(uint8_t d,uint8_t l){assert(d==1&&!l);return blocksize;}
static const char *cache_case(unsigned i)
{
 if(!strncmp(mode,"cache6_",7))return i ? "status" : mode+7;
 return !strncmp(mode,"cache_",6) ? mode+6 : "off";
}
bool tuh_msc_get_checked_reply(uint8_t d,uint32_t t,tuh_msc_checked_fault_t *out)
{
 assert(d==1&&t==bus.tag&&out);
 if(snapshot_missing)return false;
 reply_calls++;
 *out=model_reply;
 if(snapshot_max)memset(out,255,sizeof(*out));
 return true;
}
static bool pending_missing, pending_max;
static unsigned pending_calls;
bool tuh_msc_get_pending(uint8_t d,uint32_t t,tuh_msc_pending_t *out)
{
 assert(d==1&&t==bus.tag&&out);pending_calls++;
 if(pending_missing)return false;
 *out=(tuh_msc_pending_t){.tag=t,.opcode=bus.command[0],.stage=3,
  .endpoint=0x81,.expected=13,.submitted=1,.last_stage=1,
  .last_event=0,.last_endpoint=2,.last_actual=31,
  .host_valid=1,.host_start_valid=1,.host_start_frame=12,
  .host={.frame=2012,.slot=1,.active=1,.buffer_ctrl=0x440d,.endpoint_ctrl=0xa0080080}};
 if(pending_max)memset(out,255,sizeof(*out));
 return true;
}
/* Exercise actual status appender at every capacity, with malformed input and
 * maximum-value formatting. It must append atomically and never submit USB. */
static void test_transport_report(void)
{
 char saved[sizeof(transport_lines)];memcpy(saved,transport_lines,sizeof(saved));
 char b[1026],original[1026];
 unsigned before=submissions;
 for(unsigned count=0;count<=8;count++)for(unsigned cap=0;cap<=1024;cap++){
  memset(b,'Z',sizeof(b));b[0]=count;unsigned end=1;
  for(unsigned i=0;i<count;i++){memcpy(b+end,"old",4);end+=4;}
  memcpy(original,b,sizeof(b));pm_usb_append_transport_status(b,cap);
  size_t need=0;for(unsigned i=0;i<3;i++)need+=strlen(transport_lines[i])+1;
  bool append=cap>=end&&count<=5&&transport_lines[0][0]&&cap-end>=need;
  if(!append)assert(!memcmp(b,original,sizeof(b)));
  else{
   size_t extra=0;for(unsigned i=3;i<6;i++)extra+=strlen(transport_lines[i])+1;
   unsigned lines=count<=2&&transport_lines[3][0]&&cap-end-need>=extra?6:3;
   assert((unsigned char)b[0]==count+lines);
   assert(!memcmp(b+1,original+1,end-1));
   for(unsigned i=0;i<lines;i++){assert(!strcmp(b+end,transport_lines[i]));end+=strlen(b+end)+1;}
   assert(!memcmp(b+end,original+end,sizeof(b)-end));
  }
 }
 memset(b,'X',sizeof(b));b[0]=1;memcpy(original,b,sizeof(b));
 pm_usb_append_transport_status(b,1024);assert(!memcmp(b,original,sizeof(b)));
 pm_usb_append_transport_status(NULL,1024);
 for(unsigned i=0;i<6;i++)assert(strlen(transport_lines[i])<80);
 assert(!memcmp(saved,transport_lines,sizeof(saved))&&submissions==before);
}
bool tuh_msc_scsi_command(uint8_t d,const msc_cbw_t*c,void*b,tuh_msc_complete_cb_t cb,uintptr_t u)
{
 assert(d==1&&c->signature==MSC_CBW_SIGNATURE&&!c->lun);
 assert(c->tag&&c->tag==u);submissions++;bus=*c;owned=b;callback=cb;argument=u;
 if(c->command[0]==0x03){
  assert(c->cmd_len==6&&c->total_bytes==18&&c->dir==0x80&&b==sense_data&&b!=transfer_data);
  assert(c->command[4]==18);for(unsigned i=1;i<16;i++)if(i!=4)assert(!c->command[i]);
  assert(failed&&!dirty&&!writes);senses++;
  assert(senses==1&&c->tag>request.tag);
  return strcmp(mode,"sense_reject")!=0;
 }
 if(c->command[0]==0x1a||c->command[0]==0x5a){
  unsigned i=c->command[0]==0x5a;
  assert(c->cmd_len==(i?10:6)&&c->total_bytes==64&&c->dir==0x80);
  assert(c->command[1]==8&&c->command[2]==8&&c->command[i?8:4]==64);
  for(unsigned j=3;j<16;j++)if(j!=(i?8:4))assert(!c->command[j]);
  assert(b==cache_data[i]&&b!=sense_data&&b!=transfer_data&&b!=caller);
  assert(c->tag>sense_request.tag&&failed&&!dirty&&!writes);
  assert(++caches[i]==1);if(!i)assert(caches[1]==1&&c->tag>cache_request[1].tag);
  return strcmp(cache_case(i),"reject")!=0;
 }
 assert(c->cmd_len==10);
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
 if(bus.command[0]==0x1a||bus.command[0]==0x5a){
  bool ten=bus.command[0]==0x5a;const char *v=cache_case(ten);
  uint8_t *b=owned;memset(b,0,64);unsigned header=ten?8:4,offset=header,total=header+20;
  if(!strcmp(v,"block")){offset+=8;total+=8;b[ten?7:3]=8;}
  b[offset]=8;b[offset+1]=18;b[offset+2]=!strcmp(v,"on")?4:0;
  if(!strcmp(v,"ps"))b[offset]|=0x80;
  if(!strcmp(v,"padded"))csw.data_residue=0;else csw.data_residue=64-total;
  b[ten?1:0]=total-(ten?2:1);
  if(!strcmp(v,"status"))csw.status=1;
  if(!strcmp(v,"phase")||!strcmp(v,"mismatch"))csw.status=2;
  if(!strcmp(v,"short"))csw.data_residue=64-(header-1);
  if(!strcmp(v,"empty")){b[ten?1:0]=header-(ten?2:1);csw.data_residue=64-header;}
  if(!strcmp(v,"length"))b[ten?1:0]=63;
  if(!strcmp(v,"total_small"))b[ten?1:0]=0;
  if(!strcmp(v,"descriptor"))b[ten?7:3]=255;
  if(!strcmp(v,"desc_odd"))b[ten?7:3]=1;
  if(!strcmp(v,"media"))b[ten?2:1]=1;
  if(!strcmp(v,"longlba")&&ten)b[4]=1;
  if(!strcmp(v,"page"))b[offset]=6;
  if(!strcmp(v,"spf"))b[offset]|=0x40;
  if(!strcmp(v,"page_short"))b[offset+1]=1;
  if(!strcmp(v,"page_long"))b[offset+1]=255;
  if(!strcmp(v,"signature"))csw.signature^=1;
  if(!strcmp(v,"tag"))csw.tag++;
  if(!strcmp(v,"residue"))csw.data_residue=65;
  if(!strcmp(v,"userdata"))reply.user_arg++;
  if(!strcmp(v,"cbw"))bus.command[3]=1;
  if(!strcmp(v,"pointer"))reply.scsi_data=(void*)caller;
  model_reply=(tuh_msc_checked_fault_t){.tag=bus.tag,.opcode=bus.command[0],
   .stage=3,.event=0,.endpoint=0x81,.expected_endpoint=0x81,.actual=13,.expected=13,
   .data_bytes=csw.data_residue<=64?64-csw.data_residue:0,.residue=csw.data_residue,
   .csw_status=csw.status,.checks=63};
  model_reply.sample_length=model_reply.data_bytes>16?16:model_reply.data_bytes;
  memcpy(model_reply.sample,owned,model_reply.sample_length);
  if(!strcmp(v,"phase")){
   model_reply.reason=1;model_reply.stage=2;model_reply.event=2;model_reply.actual=0;
   model_reply.expected=64;model_reply.data_bytes=0;model_reply.residue=UINT32_MAX;
   model_reply.csw_status=255;model_reply.checks=0;model_reply.sample_length=0;
  }
  if(!strcmp(v,"zero_header")||!strcmp(v,"zero_short")||!strcmp(v,"zero_page")||!strcmp(v,"larger_residue")) {
   csw.data_residue=!strcmp(v,"larger_residue")?64-header:0;
   model_reply.residue=csw.data_residue;
   model_reply.data_bytes=!strcmp(v,"zero_short")?header-1:!strcmp(v,"zero_header")?header:total;
   if(!strcmp(v,"zero_header"))b[ten?1:0]=header-(ten?2:1);
   model_reply.checks=31;
   model_reply.sample_length=model_reply.data_bytes>16?16:model_reply.data_bytes;
   memcpy(model_reply.sample,owned,model_reply.sample_length);
  }
  if(!strcmp(v,"missing_record"))snapshot_missing=true;
  if(!strcmp(v,"record_tag"))model_reply.tag++;
  if(!strcmp(v,"record_opcode"))model_reply.opcode^=1;
  if(!strcmp(v,"record_reason"))model_reply.reason=8;
  if(!strcmp(v,"record_stage"))model_reply.stage=2;
  if(!strcmp(v,"record_event"))model_reply.event=1;
  if(!strcmp(v,"record_actual"))model_reply.actual=12;
  if(!strcmp(v,"record_expected"))model_reply.expected=64;
  if(!strcmp(v,"record_endpoint"))model_reply.endpoint=2;
  if(!strcmp(v,"record_endpoint_pair"))model_reply.expected_endpoint=0x82;
  if(!strcmp(v,"record_checks"))model_reply.checks=29;
  if(!strcmp(v,"record_status"))model_reply.csw_status=1;
  if(!strcmp(v,"record_residue"))model_reply.residue++;
  if(!strcmp(v,"record_count"))model_reply.data_bytes=65;
  if(!strcmp(v,"failed_count")){csw.status=model_reply.csw_status=1;model_reply.data_bytes=0;}
  if(!strcmp(v,"mismatch")){
   model_reply.reason=8;model_reply.data_bytes=header;model_reply.residue=0;
   model_reply.csw_status=0;model_reply.checks=31;model_reply.sample_length=header;
   memset(model_reply.sample,0,16);model_reply.sample[ten?1:0]=header-(ten?2:1);
  }
  callback(!strcmp(v,"device")?2:1,!strcmp(v,"null")?NULL:&reply);
  return;
 }
 if(bus.command[0]==0x03){
  memset(owned,0,18);unsigned char *b=owned;
  b[0]=0x70;b[2]=5;b[7]=10;b[12]=0x20;
  if(!strcmp(mode,"sense_field"))b[12]=0x24;
  if(!strcmp(mode,"sense_unit")){b[2]=6;b[12]=0x29;}
  if(!strcmp(mode,"sense_deferred"))b[0]=0x71;
  if(!strcmp(mode,"sense_validbit"))b[0]=0xf0;
  if(!strcmp(mode,"sense_descriptor")||!strcmp(mode,"sense_desc_deferred")){
   b[0]=!strcmp(mode,"sense_descriptor")?0x72:0x73;b[1]=5;b[2]=0x20;b[3]=0;b[7]=0;csw.data_residue=10;
  }
  if(!strcmp(mode,"sense_short"))csw.data_residue=5;
  if(!strcmp(mode,"sense_short_header"))csw.data_residue=11;
  if(!strcmp(mode,"sense_advertised"))b[7]=5;
  if(!strcmp(mode,"sense_format"))b[0]=0x75;
  if(!strcmp(mode,"sense_status"))csw.status=1;
  if(!strcmp(mode,"sense_phase"))csw.status=2;
  if(!strcmp(mode,"sense_signature"))csw.signature^=1;
  if(!strcmp(mode,"sense_tag"))csw.tag++;
  if(!strcmp(mode,"sense_residue"))csw.data_residue=19;
  if(!strcmp(mode,"sense_userdata"))reply.user_arg++;
  if(!strcmp(mode,"sense_cbw"))bus.command[5]=1;
  if(!strcmp(mode,"sense_pointer"))reply.scsi_data=(void*)caller;
  if(!strcmp(mode,"cache_skip_ready"))ready=false;
  callback(!strcmp(mode,"sense_device")?2:1,!strcmp(mode,"sense_null")?NULL:&reply);
  return;
 }
 if(!late){
  if(!strncmp(mode,"sense_",6)||!strncmp(mode,"cache",5))csw.status=1;
  if(!strcmp(mode,"bad_failed_residue")){csw.status=1;csw.data_residue=1;}
  if(established_failure)csw.status=1;
  if(!strcmp(mode,"sense_skip_ready"))ready=false;

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
 if(bus.command[0]==0x1a||bus.command[0]==0x5a){
  const char *v=cache_case(bus.command[0]==0x5a);
  BYTE nested[512];assert(usb_disk_read(nested,12,1)==RES_ERROR);
  assert(usb_disk_write(nested,12,1)==RES_ERROR&&usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR);
  if(!strcmp(v,"timeout"))return;
  if(!strcmp(v,"disconnect")){mounted=false;pm_usb_detach(1);return;}
  respond(false);return;
 }
 if(bus.command[0]==0x03){
  BYTE nested[512];assert(usb_disk_read(nested,12,1)==RES_ERROR);
  assert(usb_disk_write(nested,12,1)==RES_ERROR&&usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR);
  if(!strcmp(mode,"sense_timeout"))return;
  if(!strcmp(mode,"sense_detach")){mounted=false;pm_usb_detach(1);return;}
  respond(false);return;
 }
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
 assert(argc==3);mode=argv[2];
 if(!strcmp(mode,"report")){
  bus.tag=UINT32_MAX;test_transport_report();
  device=1;tag=bus.tag;pending_missing=true;timeout_transport_report(request.command[0]);
  assert(strstr(transport_lines[0],"snapshot=unavailable"));test_transport_report();
  pending_missing=false;pending_max=true;timeout_transport_report(request.command[0]);
  assert(!strcmp(transport_lines[0],"USB timeout op=ff tag=4294967295 stage=255 submit=255"));
  assert(!strcmp(transport_lines[1],"USB pending ep=ff want=4294967295 deadline_ms=2000"));
  assert(!strcmp(transport_lines[2],"USB last stage=255 event=255 ep=ff got=4294967295"));
  assert(!strcmp(transport_lines[3],"HCD slot=255 act=255 rem=65535 got=65535 sof=4294967295/4294967295 v=255"));
  assert(!strcmp(transport_lines[4],"HCD bc=ffffffff ec=ffffffff poll=ffffffff addr=ffffffff sc=ffffffff"));
  assert(!strcmp(transport_lines[5],"HCD ss=ffffffff bs=ffffffff irq=ffffffff ie=ffffffff sm=ffffffff"));
  test_transport_report();pending_max=false;memset(transport_lines,0,sizeof(transport_lines));
  snapshot_missing=true;cache_transport_report(1,bus.tag);assert(!transport_lines[0][0]);
  snapshot_missing=false;snapshot_max=true;cache_transport_report(1,bus.tag);
  assert(!strcmp(transport_lines[0],"USB query op=ff tag=4294967295 why=255 stage=255 event=255"));
  assert(!strcmp(transport_lines[1],"USB xfer ep=ff/ff got=4294967295 want=4294967295 data=4294967295"));
  assert(!strcmp(transport_lines[2],"USB CSW ck=ff s=255 r=4294967295 n=16 b=ffffffffffffffffffffffffffffffff"));
  test_transport_report();assert(!submissions);puts("PASS bounded immutable diagnostic report; full field ranges");return 0;
 }
 bool wr=!strcmp(argv[1],"write"),sync=!strcmp(argv[1],"sync");
 unsigned char *buf=malloc(1024);assert(buf);caller=buf;
 memset(buf,0x5a,1024);for(unsigned i=0;i<1024;i++)disk[i]=(unsigned char)(i*13+7);
 assert(usb_disk_status()==(STA_NODISK|STA_NOINIT)&&usb_disk_read(buf,12,1)==RES_NOTRDY);
 assert(!pm_usb_attach(0)&&!pm_usb_attach(255));blocksize=4096;assert(!pm_usb_attach(1));blocksize=512;
 assert(!pm_usb_selected_device()&&!pm_usb_faulted());
 assert(pm_usb_attach(1)&&!pm_usb_attach(1)&&usb_disk_status()==0);
 assert(pm_usb_selected_device()==1&&!pm_usb_faulted());
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
 if(!strcmp(mode,"sense_skip_tag"))tag=UINT32_MAX-1;
 if(!strcmp(mode,"cache_skip_tag"))tag=UINT32_MAX-2;
 if(!strcmp(mode,"notready"))ready=false;
 DRESULT r=sync?usb_disk_ioctl(CTRL_SYNC,NULL):wr?usb_disk_write(buf,12,2):usb_disk_read(buf,12,2);
 if(!strcmp(mode,"dirty_syncfail")){assert(wr&&r==RES_OK&&dirty&&writes==2);r=usb_disk_ioctl(CTRL_SYNC,NULL);}
 if(!strcmp(mode,"established_flush")){
  assert(wr&&r==RES_OK&&dirty&&writes==2);
  assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_OK&&!dirty&&sync_supported);
  established_failure=true;r=usb_disk_ioctl(CTRL_SYNC,NULL);
 }
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
  unsigned expected_reports=(!strncmp(mode,"cache",5)||!strncmp(mode,"sense_",6)||
    ((wr||sync)&&(!strcmp(mode,"scsi")||!strcmp(mode,"syncfail"))))?2:1;
  bool observing = (wr||sync)&&!strcmp(mode,"timeout");
  if(observing)expected_reports++;
  assert(r==RES_ERROR&&failed&&reports==expected_reports);unsigned before=submissions;
  assert(senses==(!observing&&expected_reports==2&&strncmp(mode,"sense_skip_",11)?1:0));
  if(!strncmp(mode,"cache",5)){
   unsigned i=strncmp(mode,"cache6_",7)!=0;const char *v=cache_case(i),*want="off";
   bool follow=true;
   if(!strcmp(v,"on"))want="on";
   if(!strcmp(v,"status"))want="s1";
   if(!strcmp(v,"short")||!strcmp(v,"zero_short"))want="short";
   if(!strcmp(v,"zero_header"))want="page";
   if(!strcmp(v,"larger_residue"))want="length";
   if(!strcmp(v,"missing_record")||!strncmp(v,"record_",7)||!strcmp(v,"failed_count")){want="reply";follow=false;}
   if(!strcmp(v,"empty")||!strcmp(v,"page")||!strcmp(v,"spf"))want="page";
   if(!strcmp(v,"length")||!strcmp(v,"total_small")||!strcmp(v,"page_short")||!strcmp(v,"page_long"))want="length";
   if(!strcmp(v,"descriptor")||!strcmp(v,"desc_odd")||!strcmp(v,"media")||!strcmp(v,"longlba"))want="header";
   if(!strcmp(v,"phase")||!strcmp(v,"mismatch")){want="phase";follow=false;}
   if(!strcmp(v,"signature")||!strcmp(v,"tag")||!strcmp(v,"residue")||!strcmp(v,"userdata")||!strcmp(v,"cbw")||!strcmp(v,"pointer")||!strcmp(v,"device")||!strcmp(v,"null")){want="reply";follow=false;}
   if(!strcmp(v,"timeout")){want="time";follow=false;}
   if(!strcmp(v,"disconnect")){want="detach";follow=false;}
   if(!strcmp(v,"reject")){want="submit";follow=false;}
   if(!strncmp(v,"skip_",5)){want="skip";follow=false;}
   if(!strcmp(want,"on")||!strcmp(want,"off"))follow=false;
   char expected[64];snprintf(expected,sizeof(expected),"c6=%s c10=%s",i?(follow?(!strcmp(v,"longlba")?"off":want):"-"):want,i?want:"s1");
   assert(strstr(status_text,expected));
   assert(caches[1]==(strncmp(v,"skip_",5)!=0)&&caches[0]==(!i||follow));
   assert(flushes==1&&writes==0&&senses==1&&!dirty);
  } else {
   bool eligible=((wr||sync)&&(!strcmp(mode,"scsi")||!strcmp(mode,"syncfail")))||
     !strcmp(mode,"sense_fixed")||!strcmp(mode,"sense_validbit")||!strcmp(mode,"sense_descriptor");
   assert(caches[1]==eligible&&!caches[0]);
   if(eligible)assert(strstr(status_text,"c6=- c10=off"));
  }
  if(!strncmp(mode,"sense_",6)){
   assert(wr&&writes==0&&flushes==1&&!dirty);
   const char *expected="rs=70:5/20/00 n=18";
   if(!strcmp(mode,"sense_field"))expected="rs=70:5/24/00";
   if(!strcmp(mode,"sense_unit"))expected="rs=70:6/29/00";
   if(!strcmp(mode,"sense_deferred"))expected="rs=71:5/20/00";
   if(!strcmp(mode,"sense_descriptor"))expected="rs=72:5/20/00 n=8";
   if(!strcmp(mode,"sense_desc_deferred"))expected="rs=73:5/20/00 n=8";
   if(!strcmp(mode,"sense_short")||!strcmp(mode,"sense_short_header")||!strcmp(mode,"sense_advertised"))expected="rs=short";
   if(!strcmp(mode,"sense_format"))expected="rs=format";
   if(!strcmp(mode,"sense_status")||!strcmp(mode,"sense_phase"))expected="rs=status";
   if(!strcmp(mode,"sense_signature")||!strcmp(mode,"sense_tag")||!strcmp(mode,"sense_residue")||!strcmp(mode,"sense_userdata")||!strcmp(mode,"sense_cbw")||!strcmp(mode,"sense_pointer")||!strcmp(mode,"sense_device")||!strcmp(mode,"sense_null"))expected="rs=reply";
   if(!strcmp(mode,"sense_timeout"))expected="rs=timeout";
   if(!strcmp(mode,"sense_detach"))expected="rs=detach";
   if(!strcmp(mode,"sense_reject"))expected="rs=submit";
   if(!strncmp(mode,"sense_skip_",11))expected="rs=skip";
   assert(strstr(status_text,"op=35 f=2 s=1 r=0 d=0")&&strstr(status_text,expected));
  }
  unsigned expected_reason=FAULT_REPLY;
  if(!strcmp(mode,"bad_failed_residue")||!strcmp(mode,"established_flush")||!strncmp(mode,"cache",5)||!strncmp(mode,"sense_",6)||!strcmp(mode,"scsi")||!strcmp(mode,"phase")||!strcmp(mode,"syncfail")||!strcmp(mode,"dirty_syncfail"))expected_reason=FAULT_STATUS;
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
  if (!strcmp(mode,"cache_phase") || !strcmp(mode,"cache6_phase")) {
   assert(strstr(transport_lines[0],"why=1 stage=2 event=2"));
   assert(!strcmp(transport_lines[1],"USB xfer ep=81/81 got=0 want=64 data=0"));
   assert(!strcmp(transport_lines[2],"USB CSW ck=00 s=255 r=4294967295 n=0 b="));
  } else if(!strcmp(mode,"cache_mismatch")||!strcmp(mode,"cache6_mismatch")) {
   assert(strstr(transport_lines[0],"why=8 stage=3 event=0"));
   assert(strstr(transport_lines[2],"ck=1f s=0 r=0"));
   assert(strstr(transport_lines[2],!strcmp(mode,"cache_mismatch")?"n=8 b=0006000000000000":"n=4 b=03000000"));
  } else if(!strcmp(mode,"timeout")||!strcmp(mode,"write_timeout")) {
   assert(pending_calls==1);
   assert(!strcmp(transport_lines[3],"HCD slot=1 act=1 rem=0 got=0 sof=12/2012 v=1"));
   assert(strstr(transport_lines[0],"stage=3 submit=1"));
   assert(!strcmp(transport_lines[1],"USB pending ep=81 want=13 deadline_ms=2000"));
   assert(!strcmp(transport_lines[2],"USB last stage=1 event=0 ep=02 got=31"));
  } else assert(!!transport_lines[0][0]==!!reply_calls);
  test_transport_report();
  char first_transport[sizeof(transport_lines)];memcpy(first_transport,transport_lines,sizeof(first_transport));
  char first_fault[80];strcpy(first_fault,status_text);
  if(wr&&!strcmp(mode,"write_timeout"))assert(writes==1&&flushes==1&&owned);
  if(wr&&!strcmp(mode,"syncfail"))assert(writes==0&&flushes==1);
  if(!wr&&!sync)for(unsigned i=0;i<1024;i++)assert(buf[i]==0x5a);
  /* Simulated late DMA after caller free: ASan catches any caller pointer leak. */
  free(buf);caller=NULL;
  if(owned){memset(owned,0xc3,bus.total_bytes);assert(((unsigned char*)owned)[bus.total_bytes-1]==0xc3);}
  assert(pm_usb_faulted());
  mounted=true;pm_usb_detach(1);assert(!pm_usb_attach(1));
  assert(!pm_usb_selected_device()&&pm_usb_faulted());
  if(observing){expected_reports++;assert(strstr(status_text,"obs=detach"));strcpy(first_fault,status_text);}
  if(callback)respond(true);
  BYTE again[512];assert(usb_disk_read(again,12,1)==RES_ERROR);
  assert(usb_disk_write(again,12,1)==RES_ERROR&&usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR);
  assert(submissions==before&&failed&&reports==expected_reports&&!strcmp(first_fault,status_text));
  assert(!memcmp(first_transport,transport_lines,sizeof(first_transport)));
 }
 printf("PASS owned USB buffer/cache sync: %s %s\n",argv[1],mode);return 0;
}
