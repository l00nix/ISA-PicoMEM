/* SPDX-License-Identifier: MIT
 * Opt-in core-0 DFS metrics. Query/reset never opens or modifies media.
 */
#include "diag.h"
#include <string.h>
#include <limits.h>
struct sample {uint32_t count,min,max,total;};
static struct sample samples[129]; /* 128 command buckets + aggregate */
static uint32_t started;
static unsigned command,initialized,saturated;
static void put16(unsigned char *p,unsigned x){p[0]=x;p[1]=x>>8;}
static void put32(unsigned char *p,uint32_t x){put16(p,x);put16(p+2,x>>16);}
static void reset(void)
{memset(samples,0,sizeof(samples));saturated=0;pm_diag_paint();initialized=1;}
void pm_diag_begin(const unsigned char *frame)
{
 if(!initialized)reset();
 command=frame[59];started=pm_diag_clock();
}
static void add(struct sample *s,uint32_t us)
{
 if(s->count==UINT32_MAX){saturated=1;return;}
 if(!s->count||us<s->min)s->min=us;
 if(us>s->max)s->max=us;
 s->count++;
 if(UINT32_MAX-s->total<us){s->total=UINT32_MAX;saturated=1;}else s->total+=us;
}
void pm_diag_end(void)
{
 uint32_t elapsed=pm_diag_clock()-started;
 if(command==0x73)return; /* Instrumentation is not file-service workload. */
 add(&samples[command<128?command:127],elapsed);add(&samples[128],elapsed);
}
int pm_diag_dispatch(unsigned char *f,unsigned cap)
{
 unsigned op,selector,len;struct sample *s;
 if(cap<60||f[59]!=0x73)return 0;
 len=f[52]+((unsigned)f[53]<<8);
 if(cap<112||len!=68||f[0]!=0x12||f[1]!=0x34||f[2]!=0xaa||
    f[12]!=0xed||f[13]!=0xf5||f[56]!=1||f[58]!=20||
    memcmp(f+60,"PMDG",4)||f[64]!=1||f[65]>1||f[67]||
    (f[66]>=128&&f[66]!=255)||(f[65]&&f[66]!=255)) {
  f[52]=60;f[53]=0;f[58]=13;f[59]=0;return 1;
 }
 op=f[65];selector=f[66];if(op)reset();
 s=&samples[selector==255?128:selector];
 memset(f+60,0,52);memcpy(f+60,"PMDG",4);f[64]=1;f[65]=op;f[66]=selector;
 f[67]=(saturated?1:0)|(pm_diag_stack_valid()?0:2);
 put32(f+68,s->count);put32(f+72,s->min);put32(f+76,s->max);put32(f+80,s->total);
 put16(f+84,pm_diag_stack_used());put16(f+86,4096);
 f[52]=112;f[53]=0;f[58]=f[59]=0;return 1;
}
