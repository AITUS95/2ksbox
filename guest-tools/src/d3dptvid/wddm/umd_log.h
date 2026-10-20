/*
 * umd_log.h: one line into the QEMU log from a WDDM user-mode driver
 * (umd_log.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UMD_LOG_H
#define UMD_LOG_H

void umd_log(const char *fmt, ...);
/* a traced process (D3D11Trace) logs this many lines instead of the usual cap */
void umd_log_traced(void);
/* this process named in the REG_MULTI_SZ HKLM\SOFTWAREksbox\<value> (or "*") */
BOOL umd_listed(const char *value);

#endif
