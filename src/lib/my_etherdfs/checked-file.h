/* SPDX-License-Identifier: MIT */
#ifndef PM_CHECKED_FILE_H
#define PM_CHECKED_FILE_H
#include "ff.h"
void pm_file_errors_init(void);
unsigned pm_file_failed(unsigned fid);
unsigned pm_file_error(FRESULT result);
unsigned pm_checked_close(unsigned fid);
#endif
