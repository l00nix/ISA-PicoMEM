/* SPDX-License-Identifier: MIT
 * Paint only inactive core-0 stack, with interrupts masked and a 64-byte gap.
 * Measurements include instrumentation and interrupt use since reset.
 */
#include "diag.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
extern uint32_t __StackBottom,__StackTop;
static unsigned valid;
#define PAINT 0xa55a7e19u
uint32_t pm_diag_clock(void){return time_us_32();}
void pm_diag_paint(void)
{
 uint32_t irq=save_and_disable_interrupts();uintptr_t sp,low=(uintptr_t)&__StackBottom,top=(uintptr_t)&__StackTop;
 __asm volatile("mov %0, sp":"=r"(sp)::"memory");
 valid=get_core_num()==0&&top-low==4096&&sp>=low+64&&sp<=top;
 if(valid){volatile uint32_t *p=(volatile uint32_t*)low;uintptr_t end=(sp-64)&~(uintptr_t)3;
  while((uintptr_t)p<end)*p++=PAINT;
 }
 restore_interrupts(irq);
}
unsigned pm_diag_stack_used(void)
{
 uint32_t irq=save_and_disable_interrupts();unsigned used;
 volatile uint32_t *p=&__StackBottom;uintptr_t top=(uintptr_t)&__StackTop;
 if(!valid){restore_interrupts(irq);return 4096;}
 while((uintptr_t)p<top&&*p==PAINT)p++;
 used=top-(uintptr_t)p;restore_interrupts(irq);return used;
}
unsigned pm_diag_stack_valid(void){return valid;}
