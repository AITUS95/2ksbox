/*
 * umd_log.c: the WDDM user-mode drivers' log line (Direct3D 9's
 * um/d3dptumd.c, Direct3D 11's um11/d3dpt11.c): an escape to the kernel
 * driver, which writes it to the device's DEBUG register, the QEMU log
 * beside its own lines (doc 15's rule).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <windows.h>
#include <winternl.h>
#include <stdarg.h>
#include <stdio.h>
#include <d3dkmthk.h>
#include "d3dpt_wddm.h"
#include "umd_log.h"

static D3DKMT_HANDLE g_kmt_adapter;
static ULONG g_log_lines;

#define UMD_LOG_MAX 4000          /* lines per process; a frame loop must not drown the QEMU log */

static NTSTATUS umd_escape_log(D3DKMT_HANDLE adapter, D3DPT_ESC *m)
{
    D3DKMT_ESCAPE e;

    ZeroMemory(&e, sizeof(e));
    e.hAdapter = adapter;
    e.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    e.pPrivateDriverData = m;
    e.PrivateDriverDataSize = sizeof(*m);
    return D3DKMTEscape(&e);
}

void umd_log(const char *fmt, ...)
{
    D3DPT_ESC m;
    va_list ap;

    if (g_log_lines >= UMD_LOG_MAX) {
        return;
    }
    g_log_lines++;
    ZeroMemory(&m, sizeof(m));
    m.magic = D3DPT_ESC_MAGIC;
    m.op = D3DPT_ESC_LOG;
    /* the process first: a dozen of Windows 11's open the adapter at once */
    {
        int n = _snprintf_s(m.text, sizeof(m.text), _TRUNCATE, "[%lu] ", GetCurrentProcessId());

        va_start(ap, fmt);
        _vsnprintf_s(m.text + n, sizeof(m.text) - n, _TRUNCATE, fmt, ap);
        va_end(ap);
    }
    if (g_kmt_adapter) {
        umd_escape_log(g_kmt_adapter, &m);
        return;
    }
    /* Ours is the adapter that takes the escape: on Windows 11 the
     * standard VGA's Basic Display is a display of its own, often the
     * first, and refuses a private escape (track M20 step 5). */
    for (int i = 1; i <= 16; i++) {
        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME o;
        D3DKMT_CLOSEADAPTER ca;

        ZeroMemory(&o, sizeof(o));
        wsprintfW(o.DeviceName, L"\\\\.\\DISPLAY%d", i);
        if (D3DKMTOpenAdapterFromGdiDisplayName(&o) != 0) {
            continue;
        }
        if (umd_escape_log(o.hAdapter, &m) == 0) {
            g_kmt_adapter = o.hAdapter;
            return;
        }
        ca.hAdapter = o.hAdapter;
        D3DKMTCloseAdapter(&ca);
    }
}
