/* SPDX-License-Identifier: GPL-2.0-or-later
 * Separate v1 USB-only creation protocol. Never reuses legacy OPEN buffers.
 */
#include "create.h"
#include <string.h>
static int parent_valid(const char *s,unsigned n)
{
    unsigned i,start=1;
    if(!n||n>PM_LFN_PATH||s[0]!='\\')return 0;
    if(n==1)return 1;
    for(i=1;i<=n;i++)if(i==n||s[i]=='\\') {
        if(!pm_lfn_alias(s+start,i-start))return 0;
        start=i+1;
    }
    /* Reserve the longest possible returned alias before touching media. */
    return n+1+12<=PM_LFN_PATH;
}
int pm_lfn_create_serve(unsigned drive,const unsigned char *req,unsigned len,
    unsigned char *out,unsigned cap,pm_lfn_create_fn create)
{
    char parent[128],name[27];struct pm_lfn_meta meta;
    unsigned op=255,p=0,n=0,j;int status=PM_LFN_INVALID;
    if(cap<PM_LFN_REPLY)return -1;
    memset(&meta,0,sizeof(meta));
    if(len>=8 && len<=PM_LFN_CREATE_REQUEST && !memcmp(req,"PMLW",4) && req[4]==1) {
        op=req[5];p=req[6];n=req[7];
        if(drive!=20)status=PM_LFN_DENIED;
        else if(op==0 && len==8 && !p && !n)status=PM_LFN_OK;
        else if((op==1||op==2)&&p<=127&&n<=26&&len==8+p+n) {
            memcpy(parent,req+8,p);parent[p]=0;memcpy(name,req+8+p,n);name[n]=0;
            if(parent_valid(parent,p)&&pm_lfn_ascii(name,n)&&name[0]!=' '&&name[0]!='.') {
                status=create?create(drive,op,parent,name,&meta):PM_LFN_UNSUPPORTED;
                if(status<0||status>PM_LFN_DENIED)status=PM_LFN_IO;
                if(status==PM_LFN_OK) {
                    for(j=0;j<13&&meta.alias[j];j++) {}
                    if(!pm_lfn_alias(meta.alias,j)||memcmp(meta.name,name,n+1)||
                       meta.size||meta.attr!=(op==1?32:16))status=PM_LFN_IO;
                }
            }
        }
    }
    memset(out,0,PM_LFN_REPLY);memcpy(out,"PMLW",4);out[4]=1;out[5]=op;out[6]=status;
    if(status==PM_LFN_OK) {
        if(!op){out[7]=26;out[8]=3;} /* file + mkdir, no rename */
        else {out[7]=n;out[8]=meta.attr;memcpy(out+13,meta.alias,13);memcpy(out+26,meta.name,n);}
    }
    return PM_LFN_REPLY;
}
int pm_lfn_create_dispatch(unsigned char *f,unsigned cap)
{
    unsigned len;int n;
    if(cap<60||f[59]!=PM_LFN_CREATE_OPCODE)return 0;
    len=f[52]+((unsigned)f[53]<<8);
    if(cap<112||len<68||len>60+PM_LFN_CREATE_REQUEST||len>cap||
       f[0]!=0x12||f[1]!=0x34||f[2]!=0xaa||f[12]!=0xed||f[13]!=0xf5||f[56]!=1) {
        f[52]=60;f[53]=0;f[58]=13;f[59]=0;return 1;
    }
    n=pm_lfn_create_serve(f[58],f+60,len-60,f+60,cap-60,pm_lfn_create_fatfs);
    if(n<0){f[52]=60;f[53]=0;f[58]=13;f[59]=0;return 1;}
    f[52]=60+n;f[53]=0;f[58]=f[59]=0;return 1;
}
