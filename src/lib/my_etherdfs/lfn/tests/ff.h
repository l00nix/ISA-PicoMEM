#ifndef TEST_FF_H
#define TEST_FF_H
/* Minimum FatFs surface used by the adapter; compiled against real ff.h too. */
typedef int FRESULT;
#define FR_OK 0
#define FR_NO_FILE 4
#define FR_NO_PATH 5
#define FR_DISK_ERR 1
typedef struct { unsigned long fsize; unsigned char fattrib; char altname[13],fname[256]; } FILINFO;
typedef struct { unsigned pos; } DIR;
FRESULT f_opendir(DIR *,const char *);
FRESULT f_readdir(DIR *,FILINFO *);
FRESULT f_closedir(DIR *);
#endif
