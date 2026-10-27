#define main payload_tests
#include "test-lfn.c"
#undef main
#include "../dispatch.h"
static unsigned char frame[316],saved[316];
static void setup(unsigned drive,unsigned op,const char *path)
{
 reset();memset(frame,0,sizeof(frame));frame[0]=0x12;frame[1]=0x34;frame[2]=0xaa;
 frame[12]=0xed;frame[13]=0xf5;frame[56]=1;frame[57]=219;frame[58]=drive;frame[59]=0x70;
 int n=pm_lfn_request(frame+60,256,op,path);assert(n>0);frame[52]=60+n;
}
int main(void)
{
 unsigned i,n=0;
 payload_tests();
 for(i=0;i<256;i++)if(i!=0x70){setup(20,0,"");frame[59]=i;memcpy(saved,frame,316);
 assert(!pm_lfn_dispatch(frame,316));assert(!memcmp(saved,frame,316)&&!calls);n++;}
 for(i=0;i<112;i++){setup(20,0,"");assert(pm_lfn_dispatch(frame,i)==(i>=60));assert(!calls);n++;}
 for(i=0;i<316;i++){setup(20,0,"");frame[52]=i;frame[53]=i>>8;
 assert(pm_lfn_dispatch(frame,316));assert(!calls);assert(frame[52]<=112);n++;}
 for(i=18;i<=20;i+=2){setup(i,1,"\\DIR\\LONGFI~1.TXT");assert(pm_lfn_dispatch(frame,316));
 assert(frame[52]==112&&!frame[53]&&!frame[58]&&!frame[59]&&frame[57]==219);
 assert(pm_lfn_decode(frame+60,52,"LONGFI~1.TXT",32,123,(char[27]){0}));
 assert(!strcmp(seen,i==18?"0:\\DIR\\":"1:\\DIR\\"));n++;}
 const unsigned fields[]={0,1,2,12,13,56,58};
 for(i=0;i<sizeof(fields)/sizeof(*fields);i++) {setup(20,1,"\\LONGFI~1.TXT");frame[fields[i]]^=255;
 assert(pm_lfn_dispatch(frame,316));assert(frame[58]==13&&!calls);n++;}
 puts("DFS dispatcher: legacy passthrough, bounds, framing and real adapter passed");
 printf("DFS dispatcher: %u checks\n",n);return 0;
}
