/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "diag.c"
static uint32_t now;static unsigned painted;
uint32_t pm_diag_clock(void){return now;}
void pm_diag_paint(void){painted++;}
unsigned pm_diag_stack_used(void){return 2400;}
unsigned pm_diag_stack_valid(void){return 1;}
static unsigned char f[316];
static uint32_t get32(unsigned n){return (uint32_t)f[n]|((uint32_t)f[n+1]<<8)|((uint32_t)f[n+2]<<16)|((uint32_t)f[n+3]<<24);}
static void request(unsigned op,unsigned selector)
{memset(f,0,sizeof(f));f[0]=0x12;f[1]=0x34;f[2]=0xaa;f[12]=0xed;f[13]=0xf5;f[52]=68;f[56]=1;f[58]=20;f[59]=0x73;memcpy(f+60,"PMDG",4);f[64]=1;f[65]=op;f[66]=selector;}
int main(void)
{
 unsigned n;
 memset(f,0,sizeof(f));f[59]=9;now=UINT32_MAX-9;pm_diag_begin(f);now=20;pm_diag_end();
 assert(samples[9].count==1&&samples[9].total==30);assert(painted==1);
 now=100;pm_diag_begin(f);now=150;pm_diag_end();
 request(0,9);pm_diag_begin(f);assert(pm_diag_dispatch(f,316));pm_diag_end();
 assert(f[52]==112&&!f[58]&&!f[59]&&!f[67]);
 assert(get32(68)==2&&get32(72)==30&&get32(76)==50&&get32(80)==80);
 for(n=88;n<112;n++)assert(!f[n]);
 request(0,255);assert(pm_diag_dispatch(f,316)&&get32(68)==2);
 request(1,9);assert(pm_diag_dispatch(f,316)&&f[58]==13&&painted==1);
 request(1,255);f[58]=18;assert(pm_diag_dispatch(f,316)&&f[58]==13&&painted==1);
 request(1,255);f[52]=69;assert(pm_diag_dispatch(f,316)&&f[58]==13&&painted==1);
 request(1,255);assert(pm_diag_dispatch(f,111)&&f[58]==13&&painted==1);
 request(1,255);assert(pm_diag_dispatch(f,316)&&get32(68)==0&&painted==2);
 samples[9].total=UINT32_MAX-1;f[59]=9;now=1;pm_diag_begin(f);now=10;pm_diag_end();
 assert(samples[9].total==UINT32_MAX&&saturated);
 samples[9].count=UINT32_MAX;add(&samples[9],100);assert(samples[9].count==UINT32_MAX);
 for(n=0;n<68;n++){request(1,255);f[52]=n;assert(pm_diag_dispatch(f,316)&&f[58]==13&&painted==2);}
 request(0,128);assert(pm_diag_dispatch(f,316)&&f[58]==13);
 f[59]=8;assert(!pm_diag_dispatch(f,316));assert(!pm_diag_dispatch(f,59));
 puts("PASS diagnostics framing, drive/reset guards, statistics, timer wrap, saturation and query exclusion");return 0;
}
