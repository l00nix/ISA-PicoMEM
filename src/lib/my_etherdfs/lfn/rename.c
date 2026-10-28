/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "rename.h"
#include <string.h>
static int source_valid(const char *s,unsigned n)
{
 unsigned i,start=1,parent=1;
 if(n<2||n>PM_LFN_PATH||s[0]!='\\')return 0;
 for(i=1;i<=n;i++)if(i==n||s[i]=='\\') {
  if(!pm_lfn_alias(s+start,i-start))return 0;
  if(i<n)parent=i;
  start=i+1;
 }
 return parent==1||parent+13<=PM_LFN_PATH;
}
int pm_lfn_rename_serve(unsigned drive,const unsigned char *req,unsigned len,
 unsigned char *out,unsigned cap,pm_lfn_rename_fn renamefn)
{
 char source[128],name[27];struct pm_lfn_meta m;
 unsigned op=255,p=0,n=0,j;unsigned char attr=0;unsigned long size=0;
 int status=PM_LFN_INVALID;
 if(cap<52)return -1;
 memset(&m,0,sizeof(m));
 if(len>=13&&len<=PM_LFN_RENAME_REQUEST&&!memcmp(req,"PMLR",4)&&req[4]==1) {
  op=req[5];p=req[6];n=req[7];attr=req[8];
  for(j=0;j<4;j++)size|=(unsigned long)req[9+j]<<(j*8);
  if(drive!=20)status=PM_LFN_DENIED;
  else if(!op&&len==13&&!p&&!n&&!attr&&!size)status=0;
  else if(op==1&&p<=127&&n<=26&&len==13+p+n&&!(attr&~0x26)&&size<=0x7fffffffUL) {
   memcpy(source,req+13,p);source[p]=0;memcpy(name,req+13+p,n);name[n]=0;
   if(source_valid(source,p)&&pm_lfn_ascii(name,n)&&name[0]!=' '&&name[0]!='.') {
    status=renamefn?renamefn(drive,source,name,attr,size,&m):PM_LFN_UNSUPPORTED;
    if(status<0||status>PM_LFN_DENIED)status=PM_LFN_IO;
    if(!status) {
     for(j=0;j<13&&m.alias[j];j++){}
     if(!pm_lfn_alias(m.alias,j)||memcmp(m.name,name,n+1)||m.size!=size||m.attr!=(attr|32))status=PM_LFN_IO;
    }
   }
  }
 }
 memset(out,0,52);memcpy(out,"PMLR",4);out[4]=1;out[5]=op;out[6]=status;
 if(!status) {
  if(!op){out[7]=26;out[8]=1;}
  else {out[7]=n;out[8]=m.attr;for(j=0;j<4;j++)out[9+j]=(m.size>>(j*8))&255;
   memcpy(out+13,m.alias,13);memcpy(out+26,m.name,n);}
 }
 return 52;
}
int pm_lfn_rename_dispatch(unsigned char *f,unsigned cap)
{
 unsigned len;int n;
 if(cap<60||f[59]!=PM_LFN_RENAME_OPCODE)return 0;
 len=f[52]+((unsigned)f[53]<<8);
 if(cap<112||len<73||len>60+PM_LFN_RENAME_REQUEST||len>cap||
    f[0]!=0x12||f[1]!=0x34||f[2]!=0xaa||f[12]!=0xed||f[13]!=0xf5||f[56]!=1)goto bad;
 n=pm_lfn_rename_serve(f[58],f+60,len-60,f+60,cap-60,pm_lfn_rename_fatfs);
 if(n<0)goto bad;
 f[52]=60+n;f[53]=f[58]=f[59]=0;return 1;
 bad:f[52]=60;f[53]=0;f[58]=13;f[59]=0;return 1;
}
