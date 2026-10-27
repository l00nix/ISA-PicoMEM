#include "../lfn.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
int pm_lfn_fatfs_lookup(void *,unsigned,const char *,struct pm_lfn_meta *);
static FILINFO fixture;
static unsigned calls,enabled,restored;
static int result,read_error,close_error;
static unsigned match_at,reads,closes;
static char seen[132];
bool PM_EnableSD(void){assert(enabled==restored);enabled++;return true;}
bool PM_PICO_EnablePSRAM(void){assert(enabled==restored+1);restored++;return true;}
FRESULT f_opendir(DIR *d,const char *path)
{ assert(enabled==restored+1);calls++;strcpy(seen,path);d->pos=0;return result; }
FRESULT f_readdir(DIR *d,FILINFO *out)
{assert(enabled==restored+1);reads++;
 if(read_error)return FR_DISK_ERR;
 if(d->pos<match_at){*out=fixture;strcpy(out->altname,"OTHER.TXT");}
 else if(d->pos==match_at)*out=fixture;else memset(out,0,sizeof(*out));
 d->pos++;return FR_OK;}
FRESULT f_closedir(DIR *d){(void)d;assert(enabled==restored+1);closes++;return close_error?FR_DISK_ERR:FR_OK;}
static void reset(void)
{
 memset(&fixture,0,sizeof(fixture));strcpy(fixture.altname,"LONGFI~1.TXT");
 strcpy(fixture.fname,"Long Filename.txt");fixture.fsize=123;fixture.fattrib=32;
 calls=enabled=restored=reads=closes=match_at=0;result=read_error=close_error=0;seen[0]=0;
}
static unsigned char packet[256],reply[PM_LFN_REPLY];
static int query(unsigned drive,const char *path)
{
 int n=pm_lfn_request(packet,sizeof(packet),1,path);assert(n>0);
 assert(pm_lfn_serve(drive,packet,n,reply,sizeof(reply),pm_lfn_fatfs_lookup,NULL)==PM_LFN_REPLY);
 assert(enabled==restored);return reply[6];
}
static void entry(struct pm_lfn_entry *e,const char *a,const char *d,unsigned p)
{ strcpy(e->alias,a);strcpy(e->display,d);e->parent=p; }
int main(void)
{
 unsigned i,j,n,tests=0;char display[27],path[128];
 struct pm_lfn_entry entries[5];
 reset();n=pm_lfn_request(packet,sizeof(packet),0,"");assert(n==8);
 assert(pm_lfn_serve(20,packet,n,packet,sizeof(packet),pm_lfn_fatfs_lookup,NULL)==52);
 assert(pm_lfn_capable(packet,52));assert(!calls);tests++;
 for(i=0;i<52;i++) {packet[i]^=128;assert(!pm_lfn_capable(packet,52));packet[i]^=128;tests++;}
 assert(!pm_lfn_capable(packet,51));tests++;
 reset();assert(query(20,"\\LONGFI~1.TXT")==0);assert(!strcmp(seen,"1:\\"));
 assert(pm_lfn_decode(reply,52,"longfi~1.txt",32,123,display));assert(!strcmp(display,"Long Filename.txt"));tests++;
 assert(query(18,"\\DIR\\LONGFI~1.TXT")==0);assert(!strcmp(seen,"0:\\DIR\\"));tests++;
 /* The serialized result retains size, alias, attributes and original case. */
 for(i=0;i<52;i++) {unsigned char saved=reply[i];reply[i]=255;
  assert(!pm_lfn_decode(reply,52,"LONGFI~1.TXT",32,123,display));assert(!display[0]);reply[i]=saved;tests++;}
 for(i=0;i<52;i++){assert(!pm_lfn_decode(reply,i,"LONGFI~1.TXT",32,123,display));tests++;}
 assert(!pm_lfn_decode(reply,52,"OTHER.TXT",32,123,display));tests++;
 assert(!pm_lfn_decode(reply,52,"LONGFI~1.TXT",16,123,display));tests++;
 assert(!pm_lfn_decode(reply,52,"LONGFI~1.TXT",32,124,display));tests++;
 reset();read_error=1;assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_IO);assert(closes==1&&restored==1);tests++;
 reset();close_error=1;assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_IO);assert(closes==1&&restored==1);tests++;
 reset();match_at=255;assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_OK);assert(reads==256&&closes==1);tests++;
 reset();match_at=256;assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_UNSUPPORTED);assert(reads==256&&closes==1);tests++;
 reset();assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_OK);
 /* Request/reply overlap, including metadata queries. */
 n=pm_lfn_request(packet,sizeof(packet),1,"\\LONGFI~1.TXT");
 assert(pm_lfn_serve(20,packet,n,packet,sizeof(packet),pm_lfn_fatfs_lookup,NULL)==52);
 assert(pm_lfn_decode(packet,52,"LONGFI~1.TXT",32,123,display));tests++;
 /* Size boundaries never truncate to an ambiguous name. */
 for(i=12;i<=27;i++) {
  reset();memset(fixture.fname,'a',i);fixture.fname[i]=0;
  assert(query(20,"\\LONGFI~1.TXT")== (i<=26?PM_LFN_OK:PM_LFN_TOO_LONG));
  assert(pm_lfn_decode(reply,52,"LONGFI~1.TXT",32,123,display)==(i<=26));
  if(i<=26)assert(strlen(display)==i);tests++;
 }
 reset();memset(fixture.fname,'x',255);fixture.fname[255]=0;
 assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_TOO_LONG);tests++;
 reset();strcpy(fixture.fname,"Caf\xc3\xa9.txt");assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_ENCODING);tests++;
 const char *invalid_names[]={".","..","bad/name","bad\\name","x:y","x*y","x?y","x\"y","x<y","x>y","x|y","trailing.","trailing ","x\ny","x\177y"};
 for(i=0;i<sizeof(invalid_names)/sizeof(*invalid_names);i++){
  reset();strcpy(fixture.fname,invalid_names[i]);assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_ENCODING);tests++;
 }
 reset();strcpy(fixture.altname,"OTHER.TXT");assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_MISSING);tests++;
 reset();fixture.fsize=0x80000000UL;assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_IO);tests++;
 reset();fixture.fattrib=8;assert(query(20,"\\LONGFI~1.TXT")==PM_LFN_IO);tests++;
 reset();fixture.altname[0]=0;strcpy(fixture.fname,"Short.txt");assert(query(20,"\\SHORT.TXT")==PM_LFN_OK);tests++;
 for(i=0;i<3;i++) {reset();result=i==0?FR_NO_FILE:i==1?FR_NO_PATH:FR_DISK_ERR;
  assert(query(20,"\\LONGFI~1.TXT")== (i<2?PM_LFN_MISSING:PM_LFN_IO));assert(enabled==1&&restored==1);tests++;}
 const char *invalid_paths[]={"", "\\", "relative.txt","\\..\\A.TXT","\\.\\A.TXT","\\A\\","\\A\\\\B","C:\\A.TXT","\\A/B.TXT","\\LONG FILENAME.TXT","\\123456789.TXT","\\A.1234","\\*.TXT","\\A..B"};
 for(i=0;i<sizeof(invalid_paths)/sizeof(*invalid_paths);i++) {
  const char *s=invalid_paths[i];n=strlen(s);reset();
  assert(pm_lfn_request(packet,sizeof(packet),1,s)<0);
  memcpy(packet,"PMLN",4);packet[4]=1;packet[5]=1;packet[6]=n;packet[7]=0;memcpy(packet+8,s,n);
  assert(pm_lfn_serve(20,packet,8+n,reply,52,pm_lfn_fatfs_lookup,NULL)==52);
  assert(reply[6]==PM_LFN_INVALID&&!calls);tests++;
 }
 /* All request lengths are bounded; no malformed size reaches FatFs. */
 for(i=0;i<256;i++) {
  reset();n=pm_lfn_request(packet,sizeof(packet),1,"\\LONGFI~1.TXT");
  assert(pm_lfn_serve(20,packet,i,reply,52,pm_lfn_fatfs_lookup,NULL)==52);
  assert(calls==(i==n));tests++;
 }
 reset();n=pm_lfn_request(packet,sizeof(packet),1,"\\LONGFI~1.TXT");
 for(i=0;i<8;i++) {unsigned char saved=packet[i];packet[i]=255;
  assert(pm_lfn_serve(20,packet,n,reply,52,pm_lfn_fatfs_lookup,NULL)==52);assert(reply[6]!=0&&!calls);packet[i]=saved;tests++;}
 assert(pm_lfn_serve(2,packet,n,reply,52,pm_lfn_fatfs_lookup,NULL)==52);assert(reply[6]==PM_LFN_INVALID&&!calls);tests++;
 assert(pm_lfn_serve(20,packet,n,reply,51,pm_lfn_fatfs_lookup,NULL)<0&&!calls);tests++;
 assert(pm_lfn_serve(20,packet,n,reply,52,NULL,NULL)==52);assert(reply[6]==PM_LFN_UNSUPPORTED);tests++;
 memset(entries,0,sizeof(entries));entry(entries+1,"LONGDI~1","Long Directory",0);
 entry(entries+2,"LONGFI~1.TXT","Long Filename.txt",1);
 entry(entries+3,"OTHER.TXT","Other name.txt",1);
 entry(entries+4,"ROOT.TXT","Other name.txt",0);
 pm_lfn_resolve(entries,5);assert(pm_lfn_lookup(entries,5,0,"long directory")==1);
 assert(pm_lfn_lookup(entries,5,1,"LONG FILENAME.TXT")==2);
 assert(pm_lfn_lookup(entries,5,1,"longfi~1.txt")==2);
 assert(pm_lfn_path(entries,5,2,path,sizeof(path))>0);assert(!strcmp(path,"\\LONGDI~1\\LONGFI~1.TXT"));
 assert(entries[3].display[0]&&entries[4].display[0]);tests++;
 strcpy(entries[2].display,"other.txt");pm_lfn_resolve(entries,5);assert(!entries[2].display[0]);tests++;
 strcpy(entries[2].display,"Other NAME.txt");pm_lfn_resolve(entries,5);assert(!entries[2].display[0]&&!entries[3].display[0]);tests++;
 assert(pm_lfn_lookup(entries,5,1,"longfi~1.txt")==2);tests++;
 entries[1].parent=2;assert(pm_lfn_path(entries,5,2,path,sizeof(path))<0);tests++;
 entries[1].parent=0;assert(pm_lfn_path(entries,5,2,path,5)<0);tests++;
 /* Unterminated/invalid display candidates must never enter string comparison. */
 memset(entries[2].display,'x',27);pm_lfn_resolve(entries,5);assert(!entries[2].display[0]);tests++;
 /* Longest bounded short path and one byte beyond it. */
 memset(path,0,sizeof(path));
 for(i=0;i<14;i++){path[i*9]='\\';memset(path+i*9+1,'A',8);}
 path[126]=0;assert(pm_lfn_request(packet,sizeof(packet),1,path)==134);tests++;
 assert(pm_lfn_request(packet,7,0,"")<0);tests++;
 /* Deterministic arbitrary request bytes under address/undefined sanitizers. */
 for(i=0;i<10000;i++) {
  unsigned seed=i+1;
  for(j=0;j<sizeof(packet);j++){seed=seed*1664525u+1013904223u;packet[j]=seed>>24;}
  assert(pm_lfn_serve(20,packet,i%256,reply,52,pm_lfn_fatfs_lookup,NULL)==52);
 }
 printf("LFN: %u checks + 10000 malformed requests passed\n",tests);
 return 0;
}
