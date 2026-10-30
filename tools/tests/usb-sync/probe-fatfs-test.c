/* SPDX-License-Identifier: MIT
 * Real FAT32 + adapter + TinyUSB: attempted mkdir never submits sector writes.
 * Only USB endpoints are modelled, backed by a read-only disposable image.
 */
#define main probe_model_main
#include "probe-test.c"
#undef main
DSTATUS disk_initialize(BYTE p){assert(p==1);return usb_disk_initialize();}
DSTATUS disk_status(BYTE p){assert(p==1);return usb_disk_status();}
DRESULT disk_read(BYTE p,BYTE*b,LBA_t s,UINT n){assert(p==1);return usb_disk_read(b,s,n);}
DRESULT disk_write(BYTE p,const BYTE*b,LBA_t s,UINT n){assert(p==1);return usb_disk_write(b,s,n);}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void*b){assert(p==1);return usb_disk_ioctl(cmd,b);}
DWORD get_fattime(void){return (DWORD)(46<<25)|(1<<21)|(1<<16);}
void *ff_memalloc(UINT n){return malloc(n);}
void ff_memfree(void*p){free(p);}
int main(int argc,char **argv)
{
 assert(argc==3);variant=atoi(argv[2]);target=1;
 probe_image=fopen(argv[1],"rb");assert(probe_image);
 assert(!fseek(probe_image,0,SEEK_END));uint32_t sectors=ftell(probe_image)/512;
 msch_init();msch_interface_t *i=get_itf(1);
 i->configured=i->mounted=true;i->ep_in=0x81;i->ep_out=1;
 i->capacity[0].block_count=sectors;i->capacity[0].block_size=512;
 assert(pm_usb_attach(1));FATFS fs;FILINFO info;
 assert(f_mount(&fs,"1:",1)==FR_OK&&!probe_attempted);
 assert(f_stat("1:/Probe Long Directory",&info)==FR_NO_FILE);
 FRESULT r=f_mkdir("1:/Probe Long Directory");
 assert(r!=FR_OK&&probe_attempted&&!dirty&&!sync_supported&&queries[1]==1);
 assert(queries[0]<=1);check_report();
 assert(f_mount(NULL,"1:",0)==FR_OK);
 if(!failed) {
  assert(f_mount(&fs,"1:",1)==FR_OK);
  assert(f_stat("1:/Probe Long Directory",&info)==FR_NO_FILE);
 }
 fclose(probe_image);
 printf("PASS read-only cache probe FAT32 variant=%u result=%u; no sector write or flush\n",variant,r);
 return 0;
}
