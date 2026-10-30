/* Real inventory formatter; cached enumeration models only. No transport API. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../src/USB_Host_Driver/usb-status.c"
char dev_message[CFG_TUH_DEVICE_MAX][DEV_DESC_SIZE];
static unsigned mask, disks, selected, blocked, unavailable;
static unsigned msc_calls;
static bool direct;
bool tuh_mounted(uint8_t d) { assert(d>=1&&d<=5); return mask&(1u<<d); }
bool tuh_vid_pid_get(uint8_t d,uint16_t *v,uint16_t *p) {
 assert(tuh_mounted(d)); *v=0xffff;*p=0xffff;return !unavailable;
}
bool tuh_bus_info_get(uint8_t d,tuh_bus_info_t *b) {
 assert(tuh_mounted(d));*b=(tuh_bus_info_t){255,(d==5||direct)?0:5,255,0};return !unavailable;
}
bool tuh_msc_mounted(uint8_t d) { assert(d>=1&&d<=4);++msc_calls;return disks&(1u<<d); }
uint8_t pm_usb_selected_device(void) { return selected; }
bool pm_usb_faulted(void) { return blocked; }
static unsigned count(unsigned bits) { unsigned n=0;for(;bits;bits>>=1)n+=bits&1;return n; }
static bool contains(const char *buf,const char *needle) {
 const char *s=buf+1;for(unsigned n=0;n<(unsigned char)buf[0];++n) {
  if(strstr(s,needle))return true;s+=strlen(s)+1;
 }return false;
}
int main(void) {
 unsigned cases=0;
 for(mask=0;mask<64;mask+=2) for(disks=0;disks<32;disks+=2)
 for(selected=0;selected<=4;++selected) for(blocked=0;blocked<2;++blocked)
 for(unavailable=0;unavailable<2;++unavailable) {
  char b[1024];memset(b,0xa5,sizeof(b));msc_calls=0;
  memset(dev_message,0,sizeof(dev_message));
  if(selected)snprintf(dev_message[selected-1],80,"%u: ?USB fault op=35 f=6",selected);
  usb_build_inventory(b,sizeof(b));
  assert((unsigned char)b[0]==1+count(mask&30)+(blocked?0:count(mask&32)));
  size_t pos=1;
  for(unsigned n=0;n<(unsigned char)b[0];++n) {
   size_t len=strlen(b+pos);assert(len<80);pos+=len+1;
  }
  assert(pos<sizeof(b));for(size_t i=pos;i<sizeof(b);++i)assert((unsigned char)b[i]==0xa5);
  if((mask&32)&&!blocked)assert(contains(b,"5: Hub"));
  if((mask&2)&&(disks&2)&&!(selected==1&&blocked)) {
   assert(contains(b,"1: Disk"));
   assert(contains(b,blocked?"blocked":selected==1?"selected":"inactive"));
  }
  if(selected&&blocked&&(mask&(1u<<selected))) assert(contains(b,"?USB fault op=35 f=6"));
  ++cases;
 }
 /* Exhaust each capacity, including empty output, with all devices mounted. */
 mask=62;disks=30;selected=1;blocked=0;unavailable=0;
 for(size_t cap=0;cap<=600;++cap) {
  unsigned char b[604];memset(b,0xa5,sizeof(b));
  usb_build_inventory((char*)b+1,cap);assert(b[0]==0xa5);
  for(size_t i=cap+1;i<sizeof(b);++i)assert(b[i]==0xa5);
  if(cap){size_t pos=1;assert(b[1]<=6);for(unsigned n=0;n<b[1];++n){
    assert(pos<cap);const void *end=memchr(b+1+pos,0,cap-pos);assert(end);pos=(const unsigned char*)end-b;
  }}
  ++cases;
 }
 usb_build_inventory(NULL,1024);usb_build_inventory(NULL,0);
 /* Address reuse must not expose a stale disk message on an inactive disk. */
 mask=6;disks=6;selected=2;blocked=0;
 strcpy(dev_message[0],"1: ?USB fault stale");
 char b[1024];usb_build_inventory(b,sizeof(b));assert(!contains(b,"stale"));
 assert(contains(b,"inactive")&&contains(b,"selected"));
 direct=true;mask=2;disks=2;selected=1;blocked=0;
 usb_build_inventory(b,sizeof(b));assert(contains(b,"root 255 selected"));
 puts("PASS cached hub/device inventory, fault preservation, address reuse and bounded buffers");
 printf("PASS %u inventory scenarios\n",cases+4);
}
