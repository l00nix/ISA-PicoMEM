#!/usr/bin/env python3
"""Compile the actual native getter/lookup against fake registers (no hardware).
The IRQ/controller implementation itself is validated by the ARM build, not this test.
"""
from pathlib import Path
import os, subprocess, tempfile
k=Path(__file__).resolve().parent;r=k.parents[2];t=r/'src/lib/tinyusb/src'
s=(t/'portable/raspberrypi/rp2040/hcd_rp2040.c').read_text()
lookup=s[s.index('static struct hw_endpoint *get_dev_ep('):s.index('#ifdef PICOMEM_EXPERIMENTAL_USB_SYNC\n/* PM_HOST_SNAPSHOT_BEGIN')]
getter=s.split('/* PM_HOST_SNAPSHOT_BEGIN: exercised against fake registers in host tests. */')[1].split('/* PM_HOST_SNAPSHOT_END */')[0]
pre=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "host/hcd.h"
#define PICO_USB_HOST_INTERRUPT_ENDPOINTS 15
struct hw_endpoint { bool configured,active; uint8_t dev_addr,ep_addr,interrupt_num;
 uint16_t remaining_len,xferred_len; uint32_t *buffer_control,*endpoint_control; };
static struct hw_endpoint ep_pool[16];
#define epx (ep_pool[0])
static struct { uint32_t int_ep_addr_ctrl[15],int_ep_ctrl,sie_ctrl,sie_status,
 buf_status,ints,inte,sof_rd,sm_state; } hw;
#define usb_hw (&hw)
static unsigned disabled,restored;static uint32_t irq_state;
uint32_t save_and_disable_interrupts(void){uint32_t old=irq_state;irq_state=1;disabled++;return old;}
void restore_interrupts(uint32_t old){assert(irq_state==1);irq_state=old;restored++;}
'''
post=r'''
int main(void) {
 unsigned tests=0;
 for(unsigned slot=0;slot<15;slot++)for(unsigned active=0;active<2;active++)
 for(unsigned blocked=0;blocked<2;blocked++) {
  memset(ep_pool,0,sizeof(ep_pool));memset(&hw,0x5a,sizeof(hw));
  for(unsigned j=0;j<15;j++)hw.int_ep_addr_ctrl[j]=0xa0000000+j;
  hw.int_ep_ctrl=1u<<(slot+1);hw.sie_ctrl=0x12345678;hw.sie_status=0x34567890;
  hw.buf_status=0x40;hw.ints=0x80;hw.inte=0x90;hw.sof_rd=1987;hw.sm_state=0xabcdef;
  uint32_t bc=0x440d,ec=0xa0080080;
  struct hw_endpoint *ep=&ep_pool[slot+1];
  *ep=(struct hw_endpoint){.configured=true,.active=active,.dev_addr=3,.ep_addr=0x81,
   .interrupt_num=slot,.remaining_len=17,.xferred_len=19,.buffer_control=&bc,.endpoint_control=&ec};
  pm_usb_host_state_t out,before;memset(&out,0xa5,sizeof(out));before=out;
  struct hw_endpoint saved[16];memcpy(saved,ep_pool,sizeof(saved));
  unsigned char hw_saved[sizeof(hw)];memcpy(hw_saved,&hw,sizeof(hw));
  irq_state=blocked;unsigned n=disabled;
  assert(pm_usb_host_snapshot(3,0x81,&out));
  assert(disabled==n+1&&disabled==restored&&irq_state==blocked);
  assert(out.slot==slot+1&&out.active==active&&out.remaining==17&&out.transferred==19);
  assert(out.buffer_ctrl==bc&&out.endpoint_ctrl==ec&&out.address_ctrl==0xa0000000+slot);
  assert(out.poll_ctrl==hw.int_ep_ctrl&&out.sie_ctrl==hw.sie_ctrl);
  assert(out.sie_status==hw.sie_status&&out.buffer_status==hw.buf_status);
  assert(out.ints==hw.ints&&out.inte==hw.inte&&out.frame==hw.sof_rd);
#if PICO_RP2350
  assert(out.sm_state==hw.sm_state);
#else
  assert(out.sm_state==0);
#endif
  assert(!memcmp(saved,ep_pool,sizeof(saved))&&!memcmp(hw_saved,&hw,sizeof(hw)));
  assert(bc==0x440d&&ec==0xa0080080);
  for(unsigned bad=0;bad<10;bad++) {
   *ep=saved[slot+1];out=before;uint8_t dev=3,addr=0x81;
   if(bad==0)dev=0;if(bad==1)dev=128;if(bad==2)dev=4;
   if(bad==3)addr=0x80;if(bad==4)addr=0xf1;if(bad==5)addr=0x82;
   if(bad==6)ep->configured=false;if(bad==7)ep->buffer_control=NULL;
   if(bad==8)ep->endpoint_control=NULL;if(bad==9)ep->interrupt_num=15;
   assert(!pm_usb_host_snapshot(dev,addr,&out));assert(!memcmp(&out,&before,sizeof(out)));
   assert(disabled==restored&&irq_state==blocked);tests++;
  }
  assert(!pm_usb_host_snapshot(3,0x81,NULL));tests++;
 }
 printf("PASS native HCD getter: %u invalid/readonly/IRQ cases (RP2350=%d)\n",tests,PICO_RP2350);
}
'''
with tempfile.TemporaryDirectory(prefix='pm-host-state-') as d:
 p=Path(d);(p/'test.c').write_text(pre+lookup+getter+post)
 for rp2350 in (0,1):
  subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-g','-fsanitize=address,undefined','-DPICOMEM_EXPERIMENTAL_USB_SYNC=1',f'-DPICO_RP2350={rp2350}','-I'+str(k),'-I'+str(t),str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
