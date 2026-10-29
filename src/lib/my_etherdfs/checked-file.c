/* SPDX-License-Identifier: MIT
 * Keep failed closes allocated and inaccessible until DFS reinitialization.
 * FatFs can clear its modified flag even when sync_fs fails; a repeated close
 * must not turn that earlier flush failure into an apparent success.
 */
#include <stdbool.h>
#include <stdint.h>
#include "checked-file.h"

extern FIL *get_fobj(uint8_t fid);
extern void free_fobj(uint8_t fid);
extern bool PM_EnableSD(void);
extern bool PM_PICO_EnablePSRAM(void);

static unsigned char failed[16];

void pm_file_errors_init(void)
{
    unsigned i;
    for (i = 0; i < 16; ++i) failed[i] = 0;
}

unsigned pm_file_failed(unsigned fid)
{
    return fid < 16 ? failed[fid] : 6;
}

/* DOS error numbers, preserving real failures rather than claiming success. */
unsigned pm_file_error(FRESULT result)
{
    switch (result) {
    case FR_OK: return 0;
    case FR_NO_FILE: return 2;
    case FR_NO_PATH: return 3;
    case FR_TOO_MANY_OPEN_FILES: return 4;
    case FR_DENIED: case FR_EXIST: return 5;
    case FR_INVALID_OBJECT: return 6;
    case FR_NOT_ENOUGH_CORE: return 8;
    case FR_INVALID_NAME: case FR_INVALID_PARAMETER: return 13;
    case FR_INVALID_DRIVE: return 15;
    case FR_WRITE_PROTECTED: return 19;
    case FR_NOT_READY: case FR_NOT_ENABLED: case FR_NO_FILESYSTEM:
    case FR_TIMEOUT: return 21;
    case FR_LOCKED: return 33;
    default: return 31; /* General failure, including disk/internal errors. */
    }
}

unsigned pm_checked_close(unsigned fid)
{
    FIL *file;
    FRESULT result;
    unsigned error;
    if (fid >= 16) return 6; /* Check before narrowing a wire handle. */
    if (failed[fid]) return failed[fid];
    file = get_fobj((uint8_t)fid);
    if (!file) return 6;
    PM_EnableSD();
    result = f_close(file); /* Includes FatFs synchronization; never skip it. */
    PM_PICO_EnablePSRAM();
    error = pm_file_error(result);
    if (error) failed[fid] = error;
    else free_fobj((uint8_t)fid);
    return error;
}
