/* SPDX-License-Identifier: MIT
 * Real adapter, asynchronous callback model; no physical USB operations. */
#define main adapter_suite_main
#include "adapter-test.c"
#undef main
int main(int argc,char **argv)
{
 assert(argc==2);unsigned variant=(unsigned)strtoul(argv[1],NULL,10);
 mode="timeout";ticks=UINT32_MAX-500000;
 assert(pm_usb_attach(1));BYTE *buf=malloc(512);assert(buf);caller=buf;
 assert(usb_disk_write(buf,12,1)==RES_ERROR);
 assert(failed&&late_observing&&flushes==1&&!writes&&!reads&&!dirty);
 assert(submissions==1&&!senses&&!caches[0]&&!caches[1]);
 assert(strstr(status_text,"op=35 f=6 s=255 r=0 d=0 obs=wait"));
 free(buf);caller=NULL;
 char frozen[sizeof(transport_lines)];memcpy(frozen,transport_lines,sizeof(frozen));
 msc_csw_t csw={.signature=MSC_CSW_SIGNATURE,.tag=bus.tag};
 msc_cbw_t cbw=bus;
 tuh_msc_complete_data_t reply={.cbw=&cbw,.csw=&csw,.scsi_data=NULL,.user_arg=bus.tag};
 model_reply=(tuh_msc_checked_fault_t){.tag=bus.tag,.opcode=0x35,.stage=3,.event=0,
  .actual=13,.expected=13,.endpoint=0x81,.expected_endpoint=0x81,.checks=63};
 uint32_t elapsed=10000000;uint8_t dev=1;const char *expected="obs=csw";
 bool poll=false,detach=false;const tuh_msc_complete_data_t *arg=&reply;
 switch(variant) {
 case 0: break;
 case 1: csw.status=model_reply.csw_status=1;elapsed=29999999;break;
 case 2: elapsed=30000000;expected="obs=none";break;
 case 3: elapsed=30000001;expected="obs=none";break;
 case 4: elapsed=30000000;poll=true;expected="obs=none";break;
 case 5: ticks=late_started+1000000-100000;pm_usb_observe_late();assert(late_observing&&reports==2);break;
 case 6: detach=true;expected="obs=detach";break;
 case 7: mounted=false;poll=true;expected="obs=detach";break;
 case 8: dev=2;break;
 case 9: arg=NULL;break;
 case 10: reply.cbw=NULL;break;
 case 11: reply.csw=NULL;break;
 case 12: reply.user_arg++;break;
 case 13: cbw.tag++;break;
 case 14: cbw.command[1]=1;break;
 case 15: reply.scsi_data=transfer_data;break;
 case 16: csw.signature++;break;
 case 17: csw.tag++;break;
 case 18: snapshot_missing=true;break;
 case 19: model_reply.tag++;break;
 case 20: model_reply.opcode=0x28;break;
 case 21: model_reply.stage=2;break;
 case 22: model_reply.event=1;break;
 case 23: model_reply.reason=9;break;
 case 24: model_reply.actual=12;break;
 case 25: model_reply.expected=12;break;
 case 26: model_reply.endpoint=0x82;break;
 case 27: model_reply.expected_endpoint=model_reply.endpoint=1;break;
 case 28: model_reply.checks=0;break;
 case 29: model_reply.data_bytes=1;break;
 case 30: model_reply.residue=1;break;
 case 31: csw.status=model_reply.csw_status=2;break;
 case 32: csw.status=1;break;
 case 33: csw.data_residue=1;break;
 case 34: mounted=false;break;
 case 35: elapsed=UINT32_MAX;poll=true;expected="obs=none";break;
 case 41: elapsed=30000000;detach=true;expected="obs=none";break;
 case 42: elapsed=29999999;detach=true;expected="obs=detach";break;
 default: assert(variant>=36&&variant<41);model_reply.checks&=~(1u<<(variant-36));break;
 }
 if((variant>=8&&variant<=34)||(variant>=36&&variant<41))expected="obs=bad";
 ticks=late_started+elapsed-100000;
 if(detach)pm_usb_detach(1);else if(poll)pm_usb_observe_late();else complete(dev,arg);
 assert(!late_observing&&failed&&reports==3&&strstr(status_text,expected));
 assert(strstr(status_text,"op=35 f=6 s=255 r=0 d=0"));
 if(variant==0)assert(strstr(status_text,"os=0 why=0 ms=10000"));
 if(variant==1)assert(strstr(status_text,"os=1 why=0 ms=29999"));
 assert(!memcmp(frozen,transport_lines,sizeof(frozen)));
 assert(submissions==1&&flushes==1&&!writes&&!senses&&!caches[0]&&!caches[1]);
 char observed[80];strcpy(observed,status_text);
 /* Duplicate/out-of-window callbacks, DMA, calls and detach cannot release quarantine. */
 complete(1,&reply);pm_usb_observe_late();pm_usb_detach(1);
 assert(!pm_usb_attach(1));BYTE again[512];
 assert(usb_disk_read(again,12,1)==RES_ERROR&&usb_disk_write(again,12,1)==RES_ERROR);
 assert(usb_disk_ioctl(CTRL_SYNC,NULL)==RES_ERROR);
 assert(!strcmp(observed,status_text)&&reports==3&&submissions==1&&failed);
 test_transport_report();
 printf("PASS late observation case %u\n",variant);return 0;
}
