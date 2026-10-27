/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lfn.h"
#include <string.h>
static unsigned bounded(const char *s,unsigned cap)
{ unsigned n; for(n=0;n<cap && s[n];n++) {} return n; }
static unsigned char upper(unsigned char c)
{ return c>='a' && c<='z' ? c-32 : c; }
int pm_lfn_equal(const char *a,const char *b)
{ while(*a && upper(*a)==upper(*b)) {a++;b++;} return upper(*a)==upper(*b); }
int pm_lfn_ascii(const char *s,unsigned n)
{
    unsigned i;
    if(!n || n>PM_LFN_MAX || s[n-1]==' ' || s[n-1]=='.') return 0;
    for(i=0;i<n;i++)
        if((unsigned char)s[i]<32 || (unsigned char)s[i]>126 ||
           strchr("\\/:*?\"<>|",s[i])) return 0;
    return !(n==1 && s[0]=='.') && !(n==2 && s[0]=='.' && s[1]=='.');
}
int pm_lfn_alias(const char *s,unsigned n)
{
    unsigned i,base=0,ext=0,dot=0;
    if(!n || n>12) return 0;
    for(i=0;i<n;i++) {
        unsigned char c=s[i];
        if(c=='.') {if(dot || !base) return 0;dot=1;continue;}
        if(c<=32 || c>=127 || strchr("\\/:*?\"<>|+[],;=",c)) return 0;
        if(dot) {if(++ext>3)return 0;} else if(++base>8)return 0;
    }
    return !dot || ext;
}
static int valid_path(const char *s,unsigned n)
{
    unsigned i,start=1;
    if(n<2 || n>PM_LFN_PATH || s[0]!='\\')return 0;
    for(i=1;i<=n;i++) if(i==n || s[i]=='\\') {
        if(!pm_lfn_alias(s+start,i-start))return 0;
        start=i+1;
    }
    return 1;
}
int pm_lfn_request(unsigned char *out,unsigned cap,unsigned op,const char *path)
{
    unsigned n=bounded(path,PM_LFN_PATH+1);
    if(op>1 || (op==0 && n) || (op==1 && !valid_path(path,n)) || cap<8+n)return -1;
    memcpy(out,"PMLN",4);out[4]=1;out[5]=op;out[6]=n;out[7]=0;
    memcpy(out+8,path,n);return 8+n;
}
static void put32(unsigned char *p,unsigned long n)
{ unsigned i;for(i=0;i<4;i++){p[i]=n&255;n>>=8;} }
static unsigned long get32(const unsigned char *p)
{ return (unsigned long)p[0]|((unsigned long)p[1]<<8)|((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24); }
int pm_lfn_serve(unsigned drive,const unsigned char *req,unsigned len,
                 unsigned char *out,unsigned cap,pm_lfn_stat statfn,void *ctx)
{
    /* Parse/copy before writing: DFS uses the same request/reply buffer. */
    char path[PM_LFN_PATH+1];
    struct pm_lfn_meta meta;
    unsigned op=255,n=0,j;
    int status=PM_LFN_INVALID;
    if(cap<PM_LFN_REPLY)return -1;
    if(len>=8 && len<=8+PM_LFN_PATH && !memcmp(req,"PMLN",4) &&
       req[4]==1 && req[7]==0 && req[6]==len-8 && (drive==18 || drive==20)) {
        op=req[5];n=req[6];memcpy(path,req+8,n);path[n]=0;
        if(op==0 && !n) status=PM_LFN_OK;
        else if(op==1 && valid_path(path,n)) {
            memset(&meta,0,sizeof(meta));
            status=statfn ? statfn(ctx,drive,path,&meta) : PM_LFN_UNSUPPORTED;
            if(status<0 || status>PM_LFN_IO)status=PM_LFN_IO;
            if(status==PM_LFN_OK) {
                j=n;while(j && path[j-1]!='\\')j--;
                if(!pm_lfn_alias(meta.alias,bounded(meta.alias,13)) ||
                   !pm_lfn_equal(meta.alias,path+j) || meta.size>0x7fffffffUL ||
                   (meta.attr&0xc8))status=PM_LFN_IO;
                else if(bounded(meta.name,27)>26)status=PM_LFN_TOO_LONG;
                else if(!pm_lfn_ascii(meta.name,bounded(meta.name,27)))status=PM_LFN_ENCODING;
            }
        }
    }
    memset(out,0,PM_LFN_REPLY);memcpy(out,"PMLN",4);
    out[4]=1;out[5]=op;out[6]=status;
    if(status==PM_LFN_OK) {
        if(op==0)out[7]=PM_LFN_MAX;
        else {
            out[7]=bounded(meta.name,27);out[8]=meta.attr;put32(out+9,meta.size);
            memcpy(out+13,meta.alias,13);memcpy(out+26,meta.name,out[7]);
        }
    }
    return PM_LFN_REPLY;
}
int pm_lfn_capable(const unsigned char *r,unsigned len)
{
    unsigned i;
    if(len!=PM_LFN_REPLY || memcmp(r,"PMLN",4) || r[4]!=1 || r[5] || r[6] || r[7]!=26)return 0;
    for(i=8;i<len;i++)if(r[i])return 0;
    return 1;
}
int pm_lfn_decode(const unsigned char *r,unsigned len,const char *alias,
                  unsigned char attr,unsigned long size,char *display)
{
    unsigned n,i;
    display[0]=0;
    if(len!=PM_LFN_REPLY || memcmp(r,"PMLN",4) || r[4]!=1 || r[5]!=1 || r[6])return 0;
    n=r[7];
    if(!n || n>26 || r[8]!=attr || get32(r+9)!=size ||
       !pm_lfn_alias((const char *)r+13,bounded((const char *)r+13,13)) ||
       !pm_lfn_equal((const char *)r+13,alias) || !pm_lfn_ascii((const char *)r+26,n))return 0;
    for(i=26+n;i<len;i++)if(r[i])return 0;
    memcpy(display,r+26,n);display[n]=0;return 1;
}
void pm_lfn_resolve(struct pm_lfn_entry *e,unsigned count)
{
    unsigned i,j;unsigned char bad[PM_LFN_NODES];
    if(count>PM_LFN_NODES)return;
    memset(bad,0,sizeof(bad));
    for(i=1;i<count;i++)
        if(!pm_lfn_ascii(e[i].display,bounded(e[i].display,27)))e[i].display[0]=0;
    for(i=1;i<count;i++) {
        for(j=1;j<count;j++)if(i!=j && e[i].parent==e[j].parent && e[i].display[0]) {
            if(pm_lfn_equal(e[i].display,e[j].alias))bad[i]=1;
            if(e[j].display[0] && pm_lfn_equal(e[i].display,e[j].display))bad[i]=bad[j]=1;
        }
    }
    /* Decide over the full snapshot before clearing any candidate. */
    for(i=1;i<count;i++)if(bad[i])e[i].display[0]=0;
}
int pm_lfn_lookup(const struct pm_lfn_entry *e,unsigned count,unsigned parent,const char *name)
{
    unsigned i;
    if(count>PM_LFN_NODES || parent>=count)return -1;
    for(i=1;i<count;i++)if(e[i].parent==parent && pm_lfn_equal(e[i].alias,name))return i;
    for(i=1;i<count;i++)if(e[i].parent==parent && e[i].display[0] && pm_lfn_equal(e[i].display,name))return i;
    return -1;
}
int pm_lfn_path(const struct pm_lfn_entry *e,unsigned count,unsigned node,char *out,unsigned cap)
{
    unsigned n=0,k,depth=0,len;
    if(!cap || count>PM_LFN_NODES)return -1;
    out[0]=0;
    while(node) {
        if(node>=count || ++depth>=count)return -1;
        len=bounded(e[node].alias,13);
        if(!pm_lfn_alias(e[node].alias,len) || len+n+1>PM_LFN_PATH || len+n+1>=cap)return -1;
        for(k=n+1;k;k--)out[k+len]=out[k-1];
        out[0]='\\';memcpy(out+1,e[node].alias,len);n+=len+1;node=e[node].parent;
    }
    if(!n){if(cap<2)return -1;out[0]='\\';out[1]=0;n=1;}
    return n;
}
