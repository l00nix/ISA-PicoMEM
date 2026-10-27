/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PM_LFN_H
#define PM_LFN_H
#include <stddef.h>
#define PM_LFN_MAX 26
#define PM_LFN_PATH 127
#define PM_LFN_REPLY 52
#define PM_LFN_NODES 64
/* Transport-independent prototype: no opcode, hardware access or auto-probe. */
enum { PM_LFN_OK, PM_LFN_UNSUPPORTED, PM_LFN_INVALID, PM_LFN_MISSING,
       PM_LFN_TOO_LONG, PM_LFN_ENCODING, PM_LFN_IO };
struct pm_lfn_meta {
    char alias[13], name[PM_LFN_MAX+1];
    unsigned char attr;
    unsigned long size;
};
typedef int (*pm_lfn_stat)(void *, unsigned, const char *, struct pm_lfn_meta *);
int pm_lfn_ascii(const char *s, unsigned n);
int pm_lfn_alias(const char *s, unsigned n);
int pm_lfn_equal(const char *a, const char *b);
int pm_lfn_request(unsigned char *out, unsigned cap, unsigned op, const char *path);
int pm_lfn_serve(unsigned drive, const unsigned char *req, unsigned len,
                 unsigned char *out, unsigned cap, pm_lfn_stat statfn, void *ctx);
int pm_lfn_capable(const unsigned char *reply, unsigned len);
int pm_lfn_decode(const unsigned char *reply, unsigned len, const char *alias,
                  unsigned char attr, unsigned long size, char *display);
struct pm_lfn_entry { char alias[13], display[27]; unsigned parent; };
void pm_lfn_resolve(struct pm_lfn_entry *e, unsigned count);
int pm_lfn_lookup(const struct pm_lfn_entry *e, unsigned count,
                  unsigned parent, const char *name);
int pm_lfn_path(const struct pm_lfn_entry *e, unsigned count,
                unsigned node, char *out, unsigned cap);
#endif
