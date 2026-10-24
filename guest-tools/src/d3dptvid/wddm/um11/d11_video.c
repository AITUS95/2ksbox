/*
 * d11_video.c: the Direct3D 11.1 interface's video functions (track M25
 * step 4). A device made with D3D11_CREATE_DEVICE_VIDEO_SUPPORT, as DWM
 * makes its own, goes to a driver that has these, so the 11.1 interface
 * brings them: no decoder profiles, no content protection, and a video
 * processor that enumerates and creates for the RGB formats. Its Blt is
 * not done yet (logged).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3dpt11.h"

#define VDEV(h) DEV(h)

/* ------------------------------------------------------------- decoding */

static VOID APIENTRY dec_profile_count(D3D10DDI_HDEVICE h, UINT *n) { D11_TRACE("dec_profile_count"); (void)h; *n = 0; }
static VOID APIENTRY dec_profile(D3D10DDI_HDEVICE h, UINT i, GUID *g) { D11_TRACE("dec_profile"); (void)i; ZeroMemory(g, sizeof(*g)); set_error(VDEV(h), E_INVALIDARG); }
static VOID APIENTRY dec_check_format(D3D10DDI_HDEVICE h, CONST GUID *g, DXGI_FORMAT f, BOOL *ok) { D11_TRACE("dec_check_format"); (void)h; (void)g; (void)f; *ok = FALSE; }
static VOID APIENTRY dec_config_count(D3D10DDI_HDEVICE h, CONST D3D11_1DDI_VIDEO_DECODER_DESC *d, UINT *n) { D11_TRACE("dec_config_count"); (void)h; (void)d; *n = 0; }
static VOID APIENTRY dec_config(D3D10DDI_HDEVICE h, CONST D3D11_1DDI_VIDEO_DECODER_DESC *d, UINT i, D3D11_1DDI_VIDEO_DECODER_CONFIG *c) { D11_TRACE("dec_config"); (void)d; (void)i; (void)c; set_error(VDEV(h), E_INVALIDARG); }
static VOID APIENTRY dec_buffer_type_count(D3D10DDI_HDEVICE h, CONST D3D11_1DDI_VIDEO_DECODER_DESC *d, UINT *n) { D11_TRACE("dec_buffer_type_count"); (void)h; (void)d; *n = 0; }
static VOID APIENTRY dec_buffer_info(D3D10DDI_HDEVICE h, CONST D3D11_1DDI_VIDEO_DECODER_DESC *d, UINT i, D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO *b) { D11_TRACE("dec_buffer_info"); (void)d; (void)i; (void)b; set_error(VDEV(h), E_INVALIDARG); }
static SIZE_T APIENTRY dec_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEODECODER *c) { D11_TRACE("dec_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY dec_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEODECODER *c, D3D11_1DDI_HDECODE d, D3D11_1DDI_HRTDECODE rt) { D11_TRACE("dec_create"); (void)h; (void)c; (void)d; (void)rt; return E_NOTIMPL; }
static VOID APIENTRY dec_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HDECODE d) { D11_TRACE("dec_destroy"); (void)h; (void)d; }
static HRESULT APIENTRY dec_extension(D3D10DDI_HDEVICE h, D3D11_1DDI_HDECODE d, CONST D3D11_1DDIARG_VIDEODECODEREXTENSION *e) { D11_TRACE("dec_extension"); (void)h; (void)d; (void)e; return E_NOTIMPL; }
static HRESULT APIENTRY dec_begin(D3D10DDI_HDEVICE h, D3D11_1DDI_HDECODE d, CONST D3D11_1DDIARG_VIDEODECODERBEGINFRAME *b) { D11_TRACE("dec_begin"); (void)h; (void)d; (void)b; return E_NOTIMPL; }
static VOID APIENTRY dec_end(D3D10DDI_HDEVICE h, D3D11_1DDI_HDECODE d) { D11_TRACE("dec_end"); (void)h; (void)d; }
static HRESULT APIENTRY dec_submit(D3D10DDI_HDEVICE h, D3D11_1DDI_HDECODE d, UINT n, CONST D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC *b) { D11_TRACE("dec_submit"); (void)h; (void)d; (void)n; (void)b; return E_NOTIMPL; }
static HRESULT APIENTRY dec_get_handle(D3D10DDI_HDEVICE h, D3D11_1DDI_HDECODE d, HANDLE *p) { D11_TRACE("dec_get_handle"); (void)h; (void)d; *p = NULL; return E_NOTIMPL; }
static SIZE_T APIENTRY dov_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEODECODEROUTPUTVIEW *c) { D11_TRACE("dov_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY dov_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEODECODEROUTPUTVIEW *c, D3D11_1DDI_HVIDEODECODEROUTPUTVIEW v, D3D11_1DDI_HRTVIDEODECODEROUTPUTVIEW rt) { D11_TRACE("dov_create"); (void)h; (void)c; (void)v; (void)rt; return E_NOTIMPL; }
static VOID APIENTRY dov_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEODECODEROUTPUTVIEW v) { D11_TRACE("dov_destroy"); (void)h; (void)v; }

/* ----------------------------------------------------- the video processor */

/* the RGB formats a processor takes and writes: those a render target can be */
static UINT vp_format(DXGI_FORMAT f)
{
    switch ((UINT)f) {
    case 24: case 28: case 29: case 87: case 88: case 91: case 93: case 10: case 2:
        return D3D11_1DDI_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_1DDI_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT;
    default:
        return 0;
    }
}

static SIZE_T APIENTRY vpe_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORENUM *c) { D11_TRACE("vpe_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY vpe_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORENUM *c, D3D11_1DDI_HVIDEOPROCESSORENUM e, D3D11_1DDI_HRTVIDEOPROCESSORENUM rt)
{ D11_TRACE("vpe_create");
    (void)h; (void)e; (void)rt;
    if (g_d11_verbose) {
        umd_log("d3d11: CreateVideoProcessorEnum %ux%u -> %ux%u", c->Desc.InputWidth, c->Desc.InputHeight, c->Desc.OutputWidth, c->Desc.OutputHeight);
    }
    return S_OK;
}
static VOID APIENTRY vpe_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORENUM e) { D11_TRACE("vpe_destroy"); (void)h; (void)e; }
static VOID APIENTRY vpe_check_format(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORENUM e, DXGI_FORMAT f, UINT *s) { D11_TRACE("vpe_check_format"); (void)h; (void)e; *s = vp_format(f); }
static VOID APIENTRY vpe_caps(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORENUM e, D3D11_1DDI_VIDEO_PROCESSOR_CAPS *c)
{ D11_TRACE("vpe_caps");
    (void)h; (void)e;
    ZeroMemory(c, sizeof(*c));
    c->RateConversionCapsCount = 1;
    c->MaxInputStreams = 1;
    c->MaxStreamStates = 1;
}
static VOID APIENTRY vpe_rate_caps(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORENUM e, UINT i, D3D11_1DDI_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *c) { D11_TRACE("vpe_rate_caps"); (void)h; (void)e; (void)i; ZeroMemory(c, sizeof(*c)); }
static VOID APIENTRY vpe_custom_rate(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORENUM e, UINT i, UINT j, D3D11_1DDI_VIDEO_PROCESSOR_CUSTOM_RATE *r) { D11_TRACE("vpe_custom_rate"); (void)e; (void)i; (void)j; ZeroMemory(r, sizeof(*r)); set_error(VDEV(h), E_INVALIDARG); }
static VOID APIENTRY vpe_filter_range(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORENUM e, D3D11_1DDI_VIDEO_PROCESSOR_FILTER f, D3D11_1DDI_VIDEO_PROCESSOR_FILTER_RANGE *r) { D11_TRACE("vpe_filter_range"); (void)e; (void)f; ZeroMemory(r, sizeof(*r)); set_error(VDEV(h), E_INVALIDARG); }

static SIZE_T APIENTRY vp_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOR *c) { D11_TRACE("vp_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY vp_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOR *c, D3D11_1DDI_HVIDEOPROCESSOR p, D3D11_1DDI_HRTVIDEOPROCESSOR rt) { D11_TRACE("vp_create"); (void)h; (void)c; (void)p; (void)rt; return S_OK; }
static VOID APIENTRY vp_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p) { D11_TRACE("vp_destroy"); (void)h; (void)p; }
static VOID APIENTRY vp_out_rect(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, BOOL e, CONST RECT *r) { D11_TRACE("vp_out_rect"); (void)h; (void)p; (void)e; (void)r; }
static VOID APIENTRY vp_out_bg(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, BOOL y, CONST D3D11_1DDI_VIDEO_COLOR *c) { D11_TRACE("vp_out_bg"); (void)h; (void)p; (void)y; (void)c; }
static VOID APIENTRY vp_out_cs(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, CONST D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE *c) { D11_TRACE("vp_out_cs"); (void)h; (void)p; (void)c; }
static VOID APIENTRY vp_out_alpha(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, D3D11_1DDI_VIDEO_PROCESSOR_ALPHA_FILL_MODE m, UINT s) { D11_TRACE("vp_out_alpha"); (void)h; (void)p; (void)m; (void)s; }
static VOID APIENTRY vp_out_constrict(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, BOOL e, SIZE s) { D11_TRACE("vp_out_constrict"); (void)h; (void)p; (void)e; (void)s; }
static VOID APIENTRY vp_out_stereo(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, BOOL e) { D11_TRACE("vp_out_stereo"); (void)h; (void)p; (void)e; }
static HRESULT APIENTRY vp_out_ext_set(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, CONST GUID *g, UINT n, void *d) { D11_TRACE("vp_out_ext_set"); (void)h; (void)p; (void)g; (void)n; (void)d; return E_NOTIMPL; }
static HRESULT APIENTRY vp_out_ext_get(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, CONST GUID *g, UINT n, void *d) { D11_TRACE("vp_out_ext_get"); (void)h; (void)p; (void)g; (void)n; (void)d; return E_NOTIMPL; }
static VOID APIENTRY vp_st_frame(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, D3D11_1DDI_VIDEO_FRAME_FORMAT f) { D11_TRACE("vp_st_frame"); (void)h; (void)p; (void)s; (void)f; }
static VOID APIENTRY vp_st_cs(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, CONST D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE *c) { D11_TRACE("vp_st_cs"); (void)h; (void)p; (void)s; (void)c; }
static VOID APIENTRY vp_st_rate(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, D3D11_1DDI_VIDEO_PROCESSOR_OUTPUT_RATE r, BOOL rep, CONST DXGI_RATIONAL *c) { D11_TRACE("vp_st_rate"); (void)h; (void)p; (void)s; (void)r; (void)rep; (void)c; }
static VOID APIENTRY vp_st_src(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, CONST RECT *r) { D11_TRACE("vp_st_src"); (void)h; (void)p; (void)s; (void)e; (void)r; }
static VOID APIENTRY vp_st_dst(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, CONST RECT *r) { D11_TRACE("vp_st_dst"); (void)h; (void)p; (void)s; (void)e; (void)r; }
static VOID APIENTRY vp_st_alpha(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, FLOAT a) { D11_TRACE("vp_st_alpha"); (void)h; (void)p; (void)s; (void)e; (void)a; }
static VOID APIENTRY vp_st_palette(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, UINT n, CONST UINT *e) { D11_TRACE("vp_st_palette"); (void)h; (void)p; (void)s; (void)n; (void)e; }
static VOID APIENTRY vp_st_aspect(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, CONST DXGI_RATIONAL *a, CONST DXGI_RATIONAL *b) { D11_TRACE("vp_st_aspect"); (void)h; (void)p; (void)s; (void)e; (void)a; (void)b; }
static VOID APIENTRY vp_st_lumakey(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, FLOAT lo, FLOAT hi) { D11_TRACE("vp_st_lumakey"); (void)h; (void)p; (void)s; (void)e; (void)lo; (void)hi; }
static VOID APIENTRY vp_st_stereo(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT f, BOOL l, BOOL b,
                                  D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FLIP_MODE m, int o) { D11_TRACE("vp_st_stereo"); (void)h; (void)p; (void)s; (void)e; (void)f; (void)l; (void)b; (void)m; (void)o; }
static VOID APIENTRY vp_st_auto(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e) { D11_TRACE("vp_st_auto"); (void)h; (void)p; (void)s; (void)e; }
static VOID APIENTRY vp_st_filter(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, D3D11_1DDI_VIDEO_PROCESSOR_FILTER f, BOOL e, int l) { D11_TRACE("vp_st_filter"); (void)h; (void)p; (void)s; (void)f; (void)e; (void)l; }
static HRESULT APIENTRY vp_st_ext_set(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, CONST GUID *g, UINT n, void *d) { D11_TRACE("vp_st_ext_set"); (void)h; (void)p; (void)s; (void)g; (void)n; (void)d; return E_NOTIMPL; }
static HRESULT APIENTRY vp_st_ext_get(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, CONST GUID *g, UINT n, void *d) { D11_TRACE("vp_st_ext_get"); (void)h; (void)p; (void)s; (void)g; (void)n; (void)d; return E_NOTIMPL; }
static VOID APIENTRY vp_st_rotation(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, UINT s, BOOL e, D3D11_1DDI_VIDEO_PROCESSOR_ROTATION r) { D11_TRACE("vp_st_rotation"); (void)h; (void)p; (void)s; (void)e; (void)r; }
static HRESULT APIENTRY vp_blt(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOR p, D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW v, UINT frame, UINT n,
                               CONST D3D11_1DDI_VIDEO_PROCESSOR_STREAM *s)
{ D11_TRACE("vp_blt");
    (void)h; (void)p; (void)v; (void)frame; (void)n; (void)s;
    D11_TODO("VideoProcessorBlt");
    return S_OK;
}

static SIZE_T APIENTRY vpi_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORINPUTVIEW *c) { D11_TRACE("vpi_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY vpi_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORINPUTVIEW *c, D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW v, D3D11_1DDI_HRTVIDEOPROCESSORINPUTVIEW rt) { D11_TRACE("vpi_create"); (void)h; (void)c; (void)v; (void)rt; return S_OK; }
static VOID APIENTRY vpi_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW v) { D11_TRACE("vpi_destroy"); (void)h; (void)v; }
static SIZE_T APIENTRY vpo_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOROUTPUTVIEW *c) { D11_TRACE("vpo_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY vpo_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOROUTPUTVIEW *c, D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW v, D3D11_1DDI_HRTVIDEOPROCESSOROUTPUTVIEW rt) { D11_TRACE("vpo_create"); (void)h; (void)c; (void)v; (void)rt; return S_OK; }
static VOID APIENTRY vpo_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW v) { D11_TRACE("vpo_destroy"); (void)h; (void)v; }
static VOID APIENTRY vpi_hazard(D3D10DDI_HDEVICE h, D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW v, D3D10DDI_HRESOURCE r) { D11_TRACE("vpi_hazard"); (void)h; (void)v; (void)r; }

/* ------------------------------------------- content protection: none */

static HRESULT APIENTRY cp_caps(D3D10DDI_HDEVICE h, CONST GUID *c, CONST GUID *p, D3D11_1DDI_VIDEO_CONTENT_PROTECTION_CAPS *o) { D11_TRACE("cp_caps"); (void)h; (void)c; (void)p; ZeroMemory(o, sizeof(*o)); return E_NOTIMPL; }
static HRESULT APIENTRY cp_kx_type(D3D10DDI_HDEVICE h, CONST GUID *c, CONST GUID *p, UINT i, GUID *o) { D11_TRACE("cp_kx_type"); (void)h; (void)c; (void)p; (void)i; (void)o; return E_NOTIMPL; }
static SIZE_T APIENTRY cs_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATECRYPTOSESSION *c) { D11_TRACE("cs_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY cs_create(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATECRYPTOSESSION *c, D3D11_1DDI_HCRYPTOSESSION s, D3D11_1DDI_HRTCRYPTOSESSION rt) { D11_TRACE("cs_create"); (void)h; (void)c; (void)s; (void)rt; return E_NOTIMPL; }
static VOID APIENTRY cs_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s) { D11_TRACE("cs_destroy"); (void)h; (void)s; }
static VOID APIENTRY cert_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDI_CERTIFICATE_INFO *i, UINT *n) { D11_TRACE("cert_size"); (void)i; *n = 0; set_error(VDEV(h), E_NOTIMPL); }
static VOID APIENTRY cert(D3D10DDI_HDEVICE h, CONST D3D11_1DDI_CERTIFICATE_INFO *i, UINT n, BYTE *c) { D11_TRACE("cert"); (void)i; (void)n; (void)c; set_error(VDEV(h), E_NOTIMPL); }
static HRESULT APIENTRY cs_kx(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s, UINT n, BYTE *d) { D11_TRACE("cs_kx"); (void)h; (void)s; (void)n; (void)d; return E_NOTIMPL; }
static VOID APIENTRY cs_enc_blt(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s, D3D10DDI_HRESOURCE a, D3D10DDI_HRESOURCE b, UINT n, CONST VOID *iv) { D11_TRACE("cs_enc_blt"); (void)s; (void)a; (void)b; (void)n; (void)iv; set_error(VDEV(h), E_NOTIMPL); }
static VOID APIENTRY cs_dec_blt(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s, D3D10DDI_HRESOURCE a, D3D10DDI_HRESOURCE b, CONST D3D11_1DDI_ENCRYPTED_BLOCK_INFO *i,
                                UINT kn, CONST VOID *k, UINT n, CONST VOID *iv) { D11_TRACE("cs_dec_blt"); (void)s; (void)a; (void)b; (void)i; (void)kn; (void)k; (void)n; (void)iv; set_error(VDEV(h), E_NOTIMPL); }
static VOID APIENTRY cs_refresh_start(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s, UINT n, VOID *r) { D11_TRACE("cs_refresh_start"); (void)s; (void)n; (void)r; set_error(VDEV(h), E_NOTIMPL); }
static VOID APIENTRY cs_refresh_end(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s) { D11_TRACE("cs_refresh_end"); (void)s; set_error(VDEV(h), E_NOTIMPL); }
static VOID APIENTRY cs_blt_key(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s, UINT n, VOID *k) { D11_TRACE("cs_blt_key"); (void)s; (void)n; (void)k; set_error(VDEV(h), E_NOTIMPL); }
static HRESULT APIENTRY cs_get_handle(D3D10DDI_HDEVICE h, D3D11_1DDI_HCRYPTOSESSION s, HANDLE *p) { D11_TRACE("cs_get_handle"); (void)h; (void)s; *p = NULL; return E_NOTIMPL; }
static SIZE_T APIENTRY ac_size(D3D10DDI_HDEVICE h, CONST D3D11_1DDIARG_CREATEAUTHENTICATEDCHANNEL *c) { D11_TRACE("ac_size"); (void)h; (void)c; return 16; }
static HRESULT APIENTRY ac_create(D3D10DDI_HDEVICE h, D3D11_1DDIARG_CREATEAUTHENTICATEDCHANNEL *c, D3D11_1DDI_HAUTHCHANNEL a, D3D11_1DDI_HRTAUTHCHANNEL rt) { D11_TRACE("ac_create"); (void)h; (void)c; (void)a; (void)rt; return E_NOTIMPL; }
static VOID APIENTRY ac_destroy(D3D10DDI_HDEVICE h, D3D11_1DDI_HAUTHCHANNEL a) { D11_TRACE("ac_destroy"); (void)h; (void)a; }
static HRESULT APIENTRY ac_kx(D3D10DDI_HDEVICE h, D3D11_1DDI_HAUTHCHANNEL a, UINT n, VOID *d) { D11_TRACE("ac_kx"); (void)h; (void)a; (void)n; (void)d; return E_NOTIMPL; }
static HRESULT APIENTRY ac_query(D3D10DDI_HDEVICE h, D3D11_1DDI_HAUTHCHANNEL a, UINT n, CONST VOID *i, UINT m, VOID *o) { D11_TRACE("ac_query"); (void)h; (void)a; (void)n; (void)i; (void)m; (void)o; return E_NOTIMPL; }
static HRESULT APIENTRY ac_configure(D3D10DDI_HDEVICE h, D3D11_1DDI_HAUTHCHANNEL a, UINT n, CONST VOID *i, D3D11_1DDI_AUTHENTICATED_CONFIGURE_OUTPUT *o) { D11_TRACE("ac_configure"); (void)h; (void)a; (void)n; (void)i; (void)o; return E_NOTIMPL; }
static VOID APIENTRY capture_handle(D3D10DDI_HDEVICE h, D3D11_1DDI_GETCAPTUREHANDLEDATA *d) { D11_TRACE("capture_handle"); (void)d; set_error(VDEV(h), E_NOTIMPL); }

void d11_video_funcs(D3D11_1DDI_VIDEODEVICEFUNCS *f)
{
    f->pfnGetVideoDecoderProfileCount = dec_profile_count;
    f->pfnGetVideoDecoderProfile = dec_profile;
    f->pfnCheckVideoDecoderFormat = dec_check_format;
    f->pfnGetVideoDecoderConfigCount = dec_config_count;
    f->pfnGetVideoDecoderConfig = dec_config;
    f->pfnGetVideoDecoderBufferTypeCount = dec_buffer_type_count;
    f->pfnGetVideoDecoderBufferInfo = dec_buffer_info;
    f->pfnCalcPrivateVideoDecoderSize = dec_size;
    f->pfnCreateVideoDecoder = dec_create;
    f->pfnDestroyVideoDecoder = dec_destroy;
    f->pfnVideoDecoderExtension = dec_extension;
    f->pfnVideoDecoderBeginFrame = dec_begin;
    f->pfnVideoDecoderEndFrame = dec_end;
    f->pfnVideoDecoderSubmitBuffers = dec_submit;
    f->pfnCalcPrivateVideoProcessorEnumSize = vpe_size;
    f->pfnCreateVideoProcessorEnum = vpe_create;
    f->pfnDestroyVideoProcessorEnum = vpe_destroy;
    f->pfnCheckVideoProcessorFormat = vpe_check_format;
    f->pfnGetVideoProcessorCaps = vpe_caps;
    f->pfnGetVideoProcessorRateConversionCaps = vpe_rate_caps;
    f->pfnGetVideoProcessorCustomRate = vpe_custom_rate;
    f->pfnGetVideoProcessorFilterRange = vpe_filter_range;
    f->pfnCalcPrivateVideoProcessorSize = vp_size;
    f->pfnCreateVideoProcessor = vp_create;
    f->pfnDestroyVideoProcessor = vp_destroy;
    f->pfnVideoProcessorSetOutputTargetRect = vp_out_rect;
    f->pfnVideoProcessorSetOutputBackgroundColor = vp_out_bg;
    f->pfnVideoProcessorSetOutputColorSpace = vp_out_cs;
    f->pfnVideoProcessorSetOutputAlphaFillMode = vp_out_alpha;
    f->pfnVideoProcessorSetOutputConstriction = vp_out_constrict;
    f->pfnVideoProcessorSetOutputStereoMode = vp_out_stereo;
    f->pfnVideoProcessorSetOutputExtension = vp_out_ext_set;
    f->pfnVideoProcessorGetOutputExtension = vp_out_ext_get;
    f->pfnVideoProcessorSetStreamFrameFormat = vp_st_frame;
    f->pfnVideoProcessorSetStreamColorSpace = vp_st_cs;
    f->pfnVideoProcessorSetStreamOutputRate = vp_st_rate;
    f->pfnVideoProcessorSetStreamSourceRect = vp_st_src;
    f->pfnVideoProcessorSetStreamDestRect = vp_st_dst;
    f->pfnVideoProcessorSetStreamAlpha = vp_st_alpha;
    f->pfnVideoProcessorSetStreamPalette = vp_st_palette;
    f->pfnVideoProcessorSetStreamPixelAspectRatio = vp_st_aspect;
    f->pfnVideoProcessorSetStreamLumaKey = vp_st_lumakey;
    f->pfnVideoProcessorSetStreamStereoFormat = vp_st_stereo;
    f->pfnVideoProcessorSetStreamAutoProcessingMode = vp_st_auto;
    f->pfnVideoProcessorSetStreamFilter = vp_st_filter;
    f->pfnVideoProcessorSetStreamExtension = vp_st_ext_set;
    f->pfnVideoProcessorGetStreamExtension = vp_st_ext_get;
    f->pfnVideoProcessorBlt = vp_blt;
    f->pfnCalcPrivateVideoDecoderOutputViewSize = dov_size;
    f->pfnCreateVideoDecoderOutputView = dov_create;
    f->pfnDestroyVideoDecoderOutputView = dov_destroy;
    f->pfnCalcPrivateVideoProcessorInputViewSize = vpi_size;
    f->pfnCreateVideoProcessorInputView = vpi_create;
    f->pfnDestroyVideoProcessorInputView = vpi_destroy;
    f->pfnCalcPrivateVideoProcessorOutputViewSize = vpo_size;
    f->pfnCreateVideoProcessorOutputView = vpo_create;
    f->pfnDestroyVideoProcessorOutputView = vpo_destroy;
    f->pfnVideoProcessorInputViewReadAfterWriteHazard = vpi_hazard;
    f->pfnGetContentProtectionCaps = cp_caps;
    f->pfnGetCryptoKeyExchangeType = cp_kx_type;
    f->pfnCalcPrivateCryptoSessionSize = cs_size;
    f->pfnCreateCryptoSession = cs_create;
    f->pfnDestroyCryptoSession = cs_destroy;
    f->pfnGetCertificateSize = cert_size;
    f->pfnGetCertificate = cert;
    f->pfnNegotiateCryptoSessionKeyExchange = cs_kx;
    f->pfnEncryptionBlt = cs_enc_blt;
    f->pfnDecryptionBlt = cs_dec_blt;
    f->pfnStartSessionKeyRefresh = cs_refresh_start;
    f->pfnFinishSessionKeyRefresh = cs_refresh_end;
    f->pfnGetEncryptionBltKey = cs_blt_key;
    f->pfnCalcPrivateAuthenticatedChannelSize = ac_size;
    f->pfnCreateAuthenticatedChannel = ac_create;
    f->pfnDestroyAuthenticatedChannel = ac_destroy;
    f->pfnNegotiateAuthenticatedChannelKeyExchange = ac_kx;
    f->pfnQueryAuthenticatedChannel = ac_query;
    f->pfnConfigureAuthenticatedChannel = ac_configure;
    f->pfnVideoDecoderGetHandle = dec_get_handle;
    f->pfnCryptoSessionGetHandle = cs_get_handle;
    f->pfnVideoProcessorSetStreamRotation = vp_st_rotation;
    f->pfnGetCaptureHandle = capture_handle;
}
