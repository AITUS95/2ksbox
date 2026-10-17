/*
 * umd_log.h: one line into the QEMU log from a WDDM user-mode driver
 * (umd_log.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UMD_LOG_H
#define UMD_LOG_H

void umd_log(const char *fmt, ...);

#endif
