/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PM_LFN_DISPATCH_H
#define PM_LFN_DISPATCH_H
/* Provisional DFS opcode, NOT the PicoMEM I/O audio command with the same byte. */
#define PM_LFN_DFS_OPCODE 0x70
int pm_lfn_dispatch(unsigned char *frame, unsigned capacity);
#endif
