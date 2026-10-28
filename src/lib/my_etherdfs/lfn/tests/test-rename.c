#include "../rename.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls,bad;static int status;
static int fn(unsigned drive,const char *src,const char *name,unsigned char attr,unsigned long size,struct pm_lfn_meta *m)
{
 assert(drive==20&&!strcmp(src,"\\DIR\\OLD.TXT")&&attr==32&&size==3073);
 calls++;strcpy(m->alias,bad?"BAD/NAME":"NEWLON~1.TXT");strcpy(m->name,name);m->size=size;m->attr=attr|32;return status;
}
int pm_lfn_rename_fatfs(unsigned d,const char *s,const char *n,unsigned char a,unsigned long z,struct pm_lfn_meta *m)
{return fn(d,s,n,a,z,m);}
static unsigned char b[316];
static unsigned request(unsigned op,const char *s,const char *n)
{unsigned p=strlen(s),l=strlen(n);memset(b,0,sizeof(b));memcpy(b,"PMLR",4);b[4]=1;b[5]=op;b[6]=p;b[7]=l;b[8]=32;b[9]=1;b[10]=12;memcpy(b+13,s,p);memcpy(b+13+p,n,l);return 13+p+l;}
int main(void)
{
 unsigned len,j,before;
 len=request(1,"\\DIR\\OLD.TXT","New Long Name.txt");assert(pm_lfn_rename_serve(20,b,len,b,256,fn)==52&&!b[6]);assert(calls==1&&b[7]==17&&b[9]==1&&b[10]==12);
 for(j=0;j<13;j++){len=request(1,"\\DIR\\OLD.TXT","New Long Name.txt");before=calls;assert(pm_lfn_rename_serve(20,b,j,b,256,fn)==52&&b[6]&&calls==before);}
 const char *invalid[]={"","x","\\","\\DIR\\..","\\LONG NAME.TXT","\\DIR\\OLD.TXT\\","\\DIR\\\\X.TXT"};
 for(j=0;j<sizeof(invalid)/sizeof(*invalid);j++){len=request(1,invalid[j],"New.txt");before=calls;pm_lfn_rename_serve(20,b,len,b,256,fn);assert(b[6]&&calls==before);}
 const char *names[]={"",".",".."," leading",".hidden","trail.","trail ","new/name","123456789012345678901234567","bad\001name"};
 for(j=0;j<sizeof(names)/sizeof(*names);j++){len=request(1,"\\DIR\\OLD.TXT",names[j]);before=calls;pm_lfn_rename_serve(20,b,len,b,256,fn);assert(b[6]&&calls==before);}
 for(j=0;j<3;j++){len=request(1,"\\DIR\\OLD.TXT","New Long Name.txt");before=calls;if(j==0)b[8]=16;if(j==1)b[4]=2;pm_lfn_rename_serve(j==2?18:20,b,len,b,256,fn);assert(b[6]&&calls==before);}
 len=request(0,"","");memset(b+8,0,5);before=calls;pm_lfn_rename_serve(20,b,len,b,256,fn);assert(!b[6]&&b[7]==26&&b[8]==1&&calls==before);
 bad=1;len=request(1,"\\DIR\\OLD.TXT","New Long Name.txt");pm_lfn_rename_serve(20,b,len,b,256,fn);assert(b[6]==6);bad=0;
 for(j=1;j<=8;j++){status=j;len=request(1,"\\DIR\\OLD.TXT","New Long Name.txt");pm_lfn_rename_serve(20,b,len,b,256,fn);assert(b[6]==j);for(unsigned k=7;k<52;k++)assert(!b[k]);}
 status=0;
 len=request(1,"\\DIR\\OLD.TXT","1234567890123456789012.txt");assert(pm_lfn_rename_serve(20,b,len,b,256,fn)==52&&!b[6]&&b[7]==26);
 for(j=0;j<7;j++){
  len=request(1,"\\DIR\\OLD.TXT","New Long Name.txt");memmove(b+60,b,len);memset(b,0,60);
  b[0]=0x12;b[1]=0x34;b[2]=0xaa;b[12]=0xed;b[13]=0xf5;b[52]=60+len;b[56]=1;b[58]=20;b[59]=0x72;
  before=calls;
  if(j==1)b[0]=0;
  if(j==2)b[56]=2;
  if(j==3)b[52]=72;
  if(j==4)b[53]=1;
  if(j==5)b[58]=18;
  if(j==6)b[59]=0x73;
  assert(pm_lfn_rename_dispatch(b,sizeof(b))==(j==6?0:1));
  if(!j)assert(calls==before+1&&b[52]==112&&!b[58]&&!b[66]);else assert(calls==before);
 }
 puts("Rename protocol: bounds, names, SD/directory denial, capabilities, in-place reply and malformed adapter results passed");
}
