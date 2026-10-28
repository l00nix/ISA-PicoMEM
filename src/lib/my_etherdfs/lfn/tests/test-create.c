/* Protocol/dispatch tests; backend is never reached for invalid requests. */
#include "../create.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;static int result,corrupt;static unsigned char b[316],saved[316];
int pm_lfn_create_fatfs(unsigned drive,unsigned op,const char *parent,const char *name,struct pm_lfn_meta *m)
{
    assert(drive==20&&(op==1||op==2));assert(parent[0]=='\\');calls++;
    strcpy(m->alias,"NEWLON~1.TXT");strcpy(m->name,name);m->attr=op==1?32:16;
    if(corrupt==1)m->size=1;
    if(corrupt==2)m->alias[0]='/';
    if(corrupt==3)m->name[0]='?';
    return result;
}
static unsigned request(unsigned op,const char *parent,const char *name)
{
 unsigned p=strlen(parent),n=strlen(name);memset(b,0,sizeof(b));
 memcpy(b,"PMLW",4);b[4]=1;b[5]=op;b[6]=p;b[7]=n;memcpy(b+8,parent,p);memcpy(b+8+p,name,n);
 return 8+p+n;
}
static void check(unsigned drive,unsigned len,unsigned status,unsigned reached)
{
 unsigned before=calls;assert(pm_lfn_create_serve(drive,b,len,b,256,pm_lfn_create_fatfs)==52);
 assert(b[6]==status&&calls-before==reached);
 if(status)for(unsigned j=7;j<52;j++)assert(!b[j]);
}
int main(void)
{
 unsigned n,j;result=corrupt=0;
 n=request(0,"","");check(20,n,0,0);assert(b[7]==26&&b[8]==3);
 for(j=1;j<=2;j++){n=request(j,"\\DIR","New Long Name.txt");check(20,n,0,1);assert(b[8]==(j==1?32:16));}
 n=request(1,"\\","12345678901234567890123456");check(20,n,0,1);
 const char *bad[]={"",".","..","../x","x/y","x\\y","*.txt","a\001b","Caf\303\251.txt","name.","name "," leading",".hidden","123456789012345678901234567"};
 for(j=0;j<sizeof(bad)/sizeof(*bad);j++){n=request(1,"\\",bad[j]);check(20,n,PM_LFN_INVALID,0);}
 const char *parents[]={"","/","\\..","\\DIR\\","\\LONG DIRECTORY","\\A\\..\\B","\\A//B","\\A\0B"};
 for(j=0;j<7;j++){n=request(1,parents[j],"New Long Name.txt");check(20,n,PM_LFN_INVALID,0);}
 n=request(1,"\\A","Name.txt");b[9]=0;check(20,n,PM_LFN_INVALID,0);
 for(j=0;j<256;j++)if(j!=20){n=request(1,"\\","Name.txt");check(j,n,PM_LFN_DENIED,0);}
 for(j=1;j<=8;j++){result=j;n=request(1,"\\","Name.txt");check(20,n,j,1);}result=0;
 for(corrupt=1;corrupt<=3;corrupt++){n=request(1,"\\","Name.txt");check(20,n,PM_LFN_IO,1);}corrupt=0;
 for(j=0;j<256;j++)if(j!=0x71){memset(b,0,316);b[59]=j;memcpy(saved,b,316);assert(!pm_lfn_create_dispatch(b,316));assert(!memcmp(saved,b,316));}
 unsigned seed=31,before=calls;
 for(j=0;j<10000;j++){
  for(n=0;n<256;n++){seed=seed*1664525u+1013904223u;b[n]=seed>>24;}
  n=seed%256;assert(pm_lfn_create_serve(20,b,n,b,256,pm_lfn_create_fatfs)==52);
 }
 assert(calls==before);
 for(j=0;j<316;j++){
  memset(b,0,316);b[59]=0x71;b[52]=j;b[53]=j>>8;
  assert(pm_lfn_create_dispatch(b,316));assert(calls==before);
 }
 puts("LFN create: bounds, USB-only gate, names, errors, malformed replies, passthrough and 10000 malformed requests passed");
 return 0;
}
