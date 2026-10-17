/*
 * d3dpt11.h: what the Direct3D 11 user-mode driver's files share
 * (d3dpt11.c: the adapter, formats and the command buffer; d11_res.c:
 * resources; d11_pipe.c: views, states, shaders, bindings and work;
 * d11_dev.c: the device and DXGI's functions).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef D3DPT11_H
#define D3DPT11_H

#define D3D_UMD_INTERFACE_VERSION D3D_UMD_INTERFACE_VERSION_WIN7
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WIN7
#include <windows.h>
#include <winternl.h>
#include <stdarg.h>
#include <stdio.h>
#include <d3d10umddi.h>
#include "../../../../../d3dpt/d3dpt_proto.h"
#include "../../../../../d3dpt/d3dpt_fb.h"
#include "../d3dpt_wddm.h"
#include "../umd_log.h"

/* the Direct3D 11 API's values the protocol speaks (d3d11.h is not
 * included beside the DDI's headers) */
#define API_BIND_UNORDERED_ACCESS 0x80u
#define API_CPU_WRITE 0x10000u
#define API_CPU_READ 0x20000u
#define API_MISC_TEXTURECUBE 0x4u
#define DXGI_DDI_ERR_WASSTILLDRAWING_ ((HRESULT)0x887B0001u)

#define D11_TODO(fn) do { static ULONG n_; if (n_ < 4) { n_++; umd_log("d3d11: todo: %s", fn); } } while (0)
/* every entry's first calls in this process: what the runtime asks next;
 * every call in a process HKLM\SOFTWARE\2ksbox's D3D11Trace names */
extern BOOL g_d11_verbose;
#define D11_TRACE(fn) do { static LONG n_; if (InterlockedIncrement(&n_) <= 2 || g_d11_verbose) umd_log("d3d11: > %s", fn); } while (0)

typedef struct D11_ADAPTER {
    D3D10DDI_HRTADAPTER rt;
    D3DDDI_ADAPTERCALLBACKS cb;
    D3DPT_UMD_INFO info;
} D11_ADAPTER;

/* A resource, in the runtime's memory (CalcPrivateResourceSize): its
 * description in the protocol's terms, its allocation and the places of
 * its subresources (sub[], nsub of them) in VRAM (vram) or in the CPU's
 * copy of a dynamic one (shadow). */
typedef struct D11_SUB { UINT offset, row_pitch, depth_pitch, w, h, d; } D11_SUB;
typedef struct D11_RES {
    D3D10DDI_HRTRESOURCE rt;
    D3DKMT_HANDLE kmt;
    d3dpt_d11_res r;                              /* r.res stays 0: patched in each time a token names it */
    D3DPT_ALLOC_DESC a;                           /* the allocation's private data */
    BOOL shared, opened, vram;                    /* vram: the layout is the allocation's */
    BOOL d3d9;                                    /* described to the host as a Direct3D 9 surface too */
    BYTE *shadow, *sent;                          /* a dynamic resource: the CPU's copy, and what the host has */
    UINT shadow_bytes;
    UINT mapped;                                  /* subresource mapped + 1, 0: none */
    BOOL map_write;
    BOOL host_newer;                              /* in VRAM, and the host wrote it since: Map reads it back first */
    UINT nsub;
    D11_SUB sub[1];                               /* nsub of them */
} D11_RES;

/* every other object: the id the host knows it by */
typedef struct D11_OBJ {
    UINT id;
    UINT kind;                                    /* D3DPT_D11K_*; a shader's stage in `extra` */
    UINT extra;
    D11_RES *res;                                 /* a view's resource */
} D11_OBJ;

typedef struct D11_DEV {
    D3D10DDI_HRTDEVICE rt;
    D3D10DDI_HRTCORELAYER core;
    D11_ADAPTER *ad;
    D3DDDI_DEVICECALLBACKS kt;
    const D3D11DDI_CORELAYER_DEVICECALLBACKS *um;
    DXGI_DDI_BASE_CALLBACKS *dxgi;
    HANDLE ctx;                                   /* the kernel context */
    ULONG host_ctx;
    ULONG next_id;
    /* the command buffer, its allocation and patch lists */
    BYTE *buf;
    UINT buf_size, used, count;
    D3DDDI_ALLOCATIONLIST *al;
    UINT al_size, al_n;
    D3DDDI_PATCHLOCATIONLIST *pl;
    UINT pl_size, pl_n;
    UINT rec_off;                                 /* the open D11 record (0: none) */
    D11_RES *qres;                                /* the queries' result page (d11_pipe.c) */
    ULONG errors;
} D11_DEV;

#define DEV(h) ((D11_DEV *)(h).pDrvPrivate)
#define RES(h) ((D11_RES *)(h).pDrvPrivate)
#define OBJ(h) ((D11_OBJ *)(h).pDrvPrivate)

/* d3dpt11.c */
UINT fmt_bytes(UINT f, UINT *block);
UINT fmt_d3d9(UINT f);
UINT fmt_from_d3d9(UINT f);
UINT fmt_support(UINT f);
void cmd_reset(D11_DEV *d);
HRESULT cmd_flush(D11_DEV *d);
void rec_close(D11_DEV *d);
void *cmd_rec(D11_DEV *d, UINT op, UINT body, UINT nal);
void *tok(D11_DEV *d, UINT op, UINT body, UINT nal);
UINT tok_max(D11_DEV *d);
void patch(D11_DEV *d, void *p, D3DKMT_HANDLE kmt);
void patch_res(D11_DEV *d, void *p, const D11_RES *r);
void set_error(D11_DEV *d, HRESULT hr);

/* d11_res.c */
void res_ref(D11_DEV *d, UINT op, D11_RES *r, UINT sub);
BYTE *res_lock(D11_DEV *d, D11_RES *r, BOOL donotwait, HRESULT *hr);
void res_unlock(D11_DEV *d, D11_RES *r);
void d11_res_funcs(D3D11DDI_DEVICEFUNCS *f);

/* d11_pipe.c */
void d11_pipe_funcs(D3D11DDI_DEVICEFUNCS *f);
void d11_pipe_destroy(D11_DEV *d);

#endif
