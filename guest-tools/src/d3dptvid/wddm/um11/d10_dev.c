/*
 * d10_dev.c: Direct3D 10's and 10.1's interfaces (d3d10.dll, d3d10_1.dll;
 * track M25). Their device tables are the Direct3D 11 one's entries under
 * the same names, but for a dozen whose arguments are older: those convert
 * them to 11's and call the 11 entry. Nothing else differs, on the wire
 * either: a Direct3D 10 device is a context on the host's d3d11 like any
 * other.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3dpt11.h"

/* the 11 entries the converters call (d11_device_funcs: every device
 * fills it with the same pointers) */
static D3D11DDI_DEVICEFUNCS E;

/* 10 for one of Direct3D 10.0's interface versions, 101 for 10.1's, 0 for
 * neither (the minor numbers: Vista's, its DXGI 1.1 update's, Windows 7's) */
UINT d10_interface(UINT iface)
{
    switch (iface) {
    case D3D10_0_DDI_INTERFACE_VERSION: case D3D10_0_x_vista_DDI_INTERFACE_VERSION:
    case D3D10_0_x_DDI_INTERFACE_VERSION: case D3D10_0_7_DDI_INTERFACE_VERSION:
        return 10;
    case D3D10_1_DDI_INTERFACE_VERSION: case D3D10_1_x_vista_DDI_INTERFACE_VERSION:
    case D3D10_1_x_DDI_INTERFACE_VERSION: case D3D10_1_7_DDI_INTERFACE_VERSION:
        return 101;
    default:
        return 0;
    }
}

/* a resource: 11's arguments add ByteStride (structured buffers) and
 * later fields, all 0 here */
static void res11(D3D11DDIARG_CREATERESOURCE *o, const D3D10DDIARG_CREATERESOURCE *c)
{
    ZeroMemory(o, sizeof(*o));
    CopyMemory(o, c, sizeof(*c));
}

static SIZE_T APIENTRY d10_calc_res_size(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERESOURCE *c)
{ D11_TRACE("d10_calc_res_size");
    D3D11DDIARG_CREATERESOURCE o;

    res11(&o, c);
    return E.pfnCalcPrivateResourceSize(h, &o);
}

static void APIENTRY d10_create_resource(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERESOURCE *c, D3D10DDI_HRESOURCE hr_,
                                         D3D10DDI_HRTRESOURCE rt)
{ D11_TRACE("d10_create_resource");
    D3D11DDIARG_CREATERESOURCE o;

    res11(&o, c);
    E.pfnCreateResource(h, &o, hr_, rt);
}

/* a shader resource view: 10.0's cube view has no First2DArrayFace and
 * NumCubes (one cube); 10.1's arguments are 11's without BufferEx */
static void srv11(D3D11DDIARG_CREATESHADERRESOURCEVIEW *o, const void *c, SIZE_T size)
{
    ZeroMemory(o, sizeof(*o));
    CopyMemory(o, c, size);
    if (size == sizeof(D3D10DDIARG_CREATESHADERRESOURCEVIEW) && o->ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE) {
        o->TexCube.First2DArrayFace = 0;
        o->TexCube.NumCubes = 1;
    }
}

static SIZE_T APIENTRY d10_calc_srv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *c)
{ D11_TRACE("d10_calc_srv");
    D3D11DDIARG_CREATESHADERRESOURCEVIEW o;

    srv11(&o, c, sizeof(*c));
    return E.pfnCalcPrivateShaderResourceViewSize(h, &o);
}

static void APIENTRY d10_create_srv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *c,
                                    D3D10DDI_HSHADERRESOURCEVIEW hv, D3D10DDI_HRTSHADERRESOURCEVIEW rt)
{ D11_TRACE("d10_create_srv");
    D3D11DDIARG_CREATESHADERRESOURCEVIEW o;

    srv11(&o, c, sizeof(*c));
    E.pfnCreateShaderResourceView(h, &o, hv, rt);
}

static SIZE_T APIENTRY d10_1_calc_srv(D3D10DDI_HDEVICE h, const D3D10_1DDIARG_CREATESHADERRESOURCEVIEW *c)
{ D11_TRACE("d10_1_calc_srv");
    D3D11DDIARG_CREATESHADERRESOURCEVIEW o;

    srv11(&o, c, sizeof(*c));
    return E.pfnCalcPrivateShaderResourceViewSize(h, &o);
}

static void APIENTRY d10_1_create_srv(D3D10DDI_HDEVICE h, const D3D10_1DDIARG_CREATESHADERRESOURCEVIEW *c,
                                      D3D10DDI_HSHADERRESOURCEVIEW hv, D3D10DDI_HRTSHADERRESOURCEVIEW rt)
{ D11_TRACE("d10_1_create_srv");
    D3D11DDIARG_CREATESHADERRESOURCEVIEW o;

    srv11(&o, c, sizeof(*c));
    E.pfnCreateShaderResourceView(h, &o, hv, rt);
}

/* a depth view: 11's adds Flags (read-only depth or stencil) */
static void dsv11(D3D11DDIARG_CREATEDEPTHSTENCILVIEW *o, const D3D10DDIARG_CREATEDEPTHSTENCILVIEW *c)
{
    ZeroMemory(o, sizeof(*o));
    o->hDrvResource = c->hDrvResource;
    o->Format = c->Format;
    o->ResourceDimension = c->ResourceDimension;
    CopyMemory(&o->Tex1D, &c->Tex1D, sizeof(*c) - FIELD_OFFSET(D3D10DDIARG_CREATEDEPTHSTENCILVIEW, Tex1D));
}

static SIZE_T APIENTRY d10_calc_dsv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATEDEPTHSTENCILVIEW *c)
{ D11_TRACE("d10_calc_dsv");
    D3D11DDIARG_CREATEDEPTHSTENCILVIEW o;

    dsv11(&o, c);
    return E.pfnCalcPrivateDepthStencilViewSize(h, &o);
}

static void APIENTRY d10_create_dsv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATEDEPTHSTENCILVIEW *c, D3D10DDI_HDEPTHSTENCILVIEW hv,
                                    D3D10DDI_HRTDEPTHSTENCILVIEW rt)
{ D11_TRACE("d10_create_dsv");
    D3D11DDIARG_CREATEDEPTHSTENCILVIEW o;

    dsv11(&o, c);
    E.pfnCreateDepthStencilView(h, &o, hv, rt);
}

/* 10.0's blend state: one blend for every target, enabled per target */
static void blend11(D3D10_1_DDI_BLEND_DESC *o, const D3D10_DDI_BLEND_DESC *c)
{
    UINT i;

    ZeroMemory(o, sizeof(*o));
    o->AlphaToCoverageEnable = c->AlphaToCoverageEnable;
    o->IndependentBlendEnable = TRUE;
    for (i = 0; i < D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
        D3D10_DDI_RENDER_TARGET_BLEND_DESC1 *b = &o->RenderTarget[i];

        b->BlendEnable = c->BlendEnable[i];
        b->SrcBlend = c->SrcBlend;
        b->DestBlend = c->DestBlend;
        b->BlendOp = c->BlendOp;
        b->SrcBlendAlpha = c->SrcBlendAlpha;
        b->DestBlendAlpha = c->DestBlendAlpha;
        b->BlendOpAlpha = c->BlendOpAlpha;
        b->RenderTargetWriteMask = c->RenderTargetWriteMask[i];
    }
}

static SIZE_T APIENTRY d10_calc_blend(D3D10DDI_HDEVICE h, const D3D10_DDI_BLEND_DESC *c)
{ D11_TRACE("d10_calc_blend");
    D3D10_1_DDI_BLEND_DESC o;

    blend11(&o, c);
    return E.pfnCalcPrivateBlendStateSize(h, &o);
}

static void APIENTRY d10_create_blend(D3D10DDI_HDEVICE h, const D3D10_DDI_BLEND_DESC *c, D3D10DDI_HBLENDSTATE hs,
                                      D3D10DDI_HRTBLENDSTATE rt)
{ D11_TRACE("d10_create_blend");
    D3D10_1_DDI_BLEND_DESC o;

    blend11(&o, c);
    E.pfnCreateBlendState(h, &o, hs, rt);
}

/* Stream output: 10's entries have no stream (all are stream 0, which is
 * rasterized) and one stride, which is the buffer's when every entry goes
 * to slot 0; with several slots each holds one element, whose size is its
 * stride */
typedef struct SO11 {
    D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT a;
    D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY e[64];     /* Direct3D 10's most */
    UINT strides[4];                                        /* its buffers */
} SO11;

static void so11(SO11 *o, const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *c)
{
    UINT i, n = c->NumEntries < ARRAYSIZE(o->e) ? c->NumEntries : ARRAYSIZE(o->e), slots = 1;
    BOOL one = TRUE;

    ZeroMemory(o, sizeof(*o));
    for (i = 0; i < n; i++) {
        const D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *s = &c->pOutputStreamDecl[i];
        UINT m = s->RegisterMask, comps = (m & 1) + (m >> 1 & 1) + (m >> 2 & 1) + (m >> 3 & 1);

        o->e[i].OutputSlot = s->OutputSlot;
        o->e[i].RegisterIndex = s->RegisterIndex;
        o->e[i].RegisterMask = s->RegisterMask;
        if (s->OutputSlot < ARRAYSIZE(o->strides)) {
            o->strides[s->OutputSlot] += comps * 4;
            if (s->OutputSlot + 1 > slots) {
                slots = s->OutputSlot + 1;
            }
        }
        if (s->OutputSlot) {
            one = FALSE;
        }
    }
    if (one && c->StreamOutputStrideInBytes) {
        o->strides[0] = c->StreamOutputStrideInBytes;
    }
    o->a.pShaderCode = c->pShaderCode;
    o->a.pOutputStreamDecl = o->e;
    o->a.NumEntries = n;
    o->a.BufferStridesInBytes = o->strides;
    o->a.NumStrides = slots;
    o->a.RasterizedStream = 0;
}

static SIZE_T APIENTRY d10_calc_gs_so(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *c,
                                      const D3D10DDIARG_STAGE_IO_SIGNATURES *s)
{ D11_TRACE("d10_calc_gs_so");
    SO11 o;

    so11(&o, c);
    return E.pfnCalcPrivateGeometryShaderWithStreamOutput(h, &o.a, s);
}

static void APIENTRY d10_create_gs_so(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *c, D3D10DDI_HSHADER hs,
                                      D3D10DDI_HRTSHADER rt, const D3D10DDIARG_STAGE_IO_SIGNATURES *s)
{ D11_TRACE("d10_create_gs_so");
    SO11 o;

    so11(&o, c);
    E.pfnCreateGeometryShaderWithStreamOutput(h, &o.a, hs, rt, s);
}

/* render targets with no unordered access views */
static void APIENTRY d10_set_targets(D3D10DDI_HDEVICE h, const D3D10DDI_HRENDERTARGETVIEW *rtv, UINT n, UINT clear,
                                     D3D10DDI_HDEPTHSTENCILVIEW dsv)
{ D11_TRACE("d10_set_targets");
    E.pfnSetRenderTargets(h, rtv, n, clear, dsv, NULL, NULL, n, 0, 0, 0);
}

static void APIENTRY d10_relocate(D3D10DDI_HDEVICE h, struct D3D10DDI_DEVICEFUNCS *f)
{ D11_TRACE("d10_relocate");
    (void)h; (void)f;                             /* nothing keeps the table's address */
}

static void APIENTRY d10_1_relocate(D3D10DDI_HDEVICE h, struct D3D10_1DDI_DEVICEFUNCS *f)
{ D11_TRACE("d10_1_relocate");
    (void)h; (void)f;
}

/* the entries both tables take from 11's as they are */
#define D10_SAME(f)                                                                                \
    do {                                                                                           \
        (f)->pfnDefaultConstantBufferUpdateSubresourceUP = E.pfnDefaultConstantBufferUpdateSubresourceUP; \
        (f)->pfnVsSetConstantBuffers = E.pfnVsSetConstantBuffers;                                  \
        (f)->pfnPsSetShaderResources = E.pfnPsSetShaderResources;                                  \
        (f)->pfnPsSetShader = E.pfnPsSetShader;                                                    \
        (f)->pfnPsSetSamplers = E.pfnPsSetSamplers;                                                \
        (f)->pfnVsSetShader = E.pfnVsSetShader;                                                    \
        (f)->pfnDrawIndexed = E.pfnDrawIndexed;                                                    \
        (f)->pfnDraw = E.pfnDraw;                                                                  \
        (f)->pfnDynamicIABufferMapNoOverwrite = E.pfnDynamicIABufferMapNoOverwrite;                \
        (f)->pfnDynamicIABufferUnmap = E.pfnDynamicIABufferUnmap;                                  \
        (f)->pfnDynamicConstantBufferMapDiscard = E.pfnDynamicConstantBufferMapDiscard;            \
        (f)->pfnDynamicIABufferMapDiscard = E.pfnDynamicIABufferMapDiscard;                        \
        (f)->pfnDynamicConstantBufferUnmap = E.pfnDynamicConstantBufferUnmap;                      \
        (f)->pfnPsSetConstantBuffers = E.pfnPsSetConstantBuffers;                                  \
        (f)->pfnIaSetInputLayout = E.pfnIaSetInputLayout;                                          \
        (f)->pfnIaSetVertexBuffers = E.pfnIaSetVertexBuffers;                                      \
        (f)->pfnIaSetIndexBuffer = E.pfnIaSetIndexBuffer;                                          \
        (f)->pfnDrawIndexedInstanced = E.pfnDrawIndexedInstanced;                                  \
        (f)->pfnDrawInstanced = E.pfnDrawInstanced;                                                \
        (f)->pfnDynamicResourceMapDiscard = E.pfnDynamicResourceMapDiscard;                        \
        (f)->pfnDynamicResourceUnmap = E.pfnDynamicResourceUnmap;                                  \
        (f)->pfnGsSetConstantBuffers = E.pfnGsSetConstantBuffers;                                  \
        (f)->pfnGsSetShader = E.pfnGsSetShader;                                                    \
        (f)->pfnIaSetTopology = E.pfnIaSetTopology;                                                \
        (f)->pfnStagingResourceMap = E.pfnStagingResourceMap;                                      \
        (f)->pfnStagingResourceUnmap = E.pfnStagingResourceUnmap;                                  \
        (f)->pfnVsSetShaderResources = E.pfnVsSetShaderResources;                                  \
        (f)->pfnVsSetSamplers = E.pfnVsSetSamplers;                                                \
        (f)->pfnGsSetShaderResources = E.pfnGsSetShaderResources;                                  \
        (f)->pfnGsSetSamplers = E.pfnGsSetSamplers;                                                \
        (f)->pfnShaderResourceViewReadAfterWriteHazard = E.pfnShaderResourceViewReadAfterWriteHazard; \
        (f)->pfnResourceReadAfterWriteHazard = E.pfnResourceReadAfterWriteHazard;                  \
        (f)->pfnSetBlendState = E.pfnSetBlendState;                                                \
        (f)->pfnSetDepthStencilState = E.pfnSetDepthStencilState;                                  \
        (f)->pfnSetRasterizerState = E.pfnSetRasterizerState;                                      \
        (f)->pfnQueryEnd = E.pfnQueryEnd;                                                          \
        (f)->pfnQueryBegin = E.pfnQueryBegin;                                                      \
        (f)->pfnResourceCopyRegion = E.pfnResourceCopyRegion;                                      \
        (f)->pfnResourceUpdateSubresourceUP = E.pfnResourceUpdateSubresourceUP;                    \
        (f)->pfnSoSetTargets = E.pfnSoSetTargets;                                                  \
        (f)->pfnDrawAuto = E.pfnDrawAuto;                                                          \
        (f)->pfnSetViewports = E.pfnSetViewports;                                                  \
        (f)->pfnSetScissorRects = E.pfnSetScissorRects;                                            \
        (f)->pfnClearRenderTargetView = E.pfnClearRenderTargetView;                                \
        (f)->pfnClearDepthStencilView = E.pfnClearDepthStencilView;                                \
        (f)->pfnSetPredication = E.pfnSetPredication;                                              \
        (f)->pfnQueryGetData = E.pfnQueryGetData;                                                  \
        (f)->pfnFlush = E.pfnFlush;                                                                \
        (f)->pfnGenMips = E.pfnGenMips;                                                            \
        (f)->pfnResourceCopy = E.pfnResourceCopy;                                                  \
        (f)->pfnResourceResolveSubresource = E.pfnResourceResolveSubresource;                      \
        (f)->pfnResourceMap = E.pfnResourceMap;                                                    \
        (f)->pfnResourceUnmap = E.pfnResourceUnmap;                                                \
        (f)->pfnResourceIsStagingBusy = E.pfnResourceIsStagingBusy;                                \
        (f)->pfnCalcPrivateOpenedResourceSize = E.pfnCalcPrivateOpenedResourceSize;                \
        (f)->pfnOpenResource = E.pfnOpenResource;                                                  \
        (f)->pfnDestroyResource = E.pfnDestroyResource;                                            \
        (f)->pfnDestroyShaderResourceView = E.pfnDestroyShaderResourceView;                        \
        (f)->pfnCalcPrivateRenderTargetViewSize = E.pfnCalcPrivateRenderTargetViewSize;            \
        (f)->pfnCreateRenderTargetView = E.pfnCreateRenderTargetView;                              \
        (f)->pfnDestroyRenderTargetView = E.pfnDestroyRenderTargetView;                            \
        (f)->pfnDestroyDepthStencilView = E.pfnDestroyDepthStencilView;                            \
        (f)->pfnCalcPrivateElementLayoutSize = E.pfnCalcPrivateElementLayoutSize;                  \
        (f)->pfnCreateElementLayout = E.pfnCreateElementLayout;                                    \
        (f)->pfnDestroyElementLayout = E.pfnDestroyElementLayout;                                  \
        (f)->pfnDestroyBlendState = E.pfnDestroyBlendState;                                        \
        (f)->pfnCalcPrivateDepthStencilStateSize = E.pfnCalcPrivateDepthStencilStateSize;          \
        (f)->pfnCreateDepthStencilState = E.pfnCreateDepthStencilState;                            \
        (f)->pfnDestroyDepthStencilState = E.pfnDestroyDepthStencilState;                          \
        (f)->pfnCalcPrivateRasterizerStateSize = E.pfnCalcPrivateRasterizerStateSize;              \
        (f)->pfnCreateRasterizerState = E.pfnCreateRasterizerState;                                \
        (f)->pfnDestroyRasterizerState = E.pfnDestroyRasterizerState;                              \
        (f)->pfnCalcPrivateShaderSize = E.pfnCalcPrivateShaderSize;                                \
        (f)->pfnCreateVertexShader = E.pfnCreateVertexShader;                                      \
        (f)->pfnCreateGeometryShader = E.pfnCreateGeometryShader;                                  \
        (f)->pfnCreatePixelShader = E.pfnCreatePixelShader;                                        \
        (f)->pfnDestroyShader = E.pfnDestroyShader;                                                \
        (f)->pfnCalcPrivateSamplerSize = E.pfnCalcPrivateSamplerSize;                              \
        (f)->pfnCreateSampler = E.pfnCreateSampler;                                                \
        (f)->pfnDestroySampler = E.pfnDestroySampler;                                              \
        (f)->pfnCalcPrivateQuerySize = E.pfnCalcPrivateQuerySize;                                  \
        (f)->pfnCreateQuery = E.pfnCreateQuery;                                                    \
        (f)->pfnDestroyQuery = E.pfnDestroyQuery;                                                  \
        (f)->pfnCheckFormatSupport = E.pfnCheckFormatSupport;                                      \
        (f)->pfnCheckMultisampleQualityLevels = E.pfnCheckMultisampleQualityLevels;                \
        (f)->pfnCheckCounterInfo = E.pfnCheckCounterInfo;                                          \
        (f)->pfnCheckCounter = E.pfnCheckCounter;                                                  \
        (f)->pfnDestroyDevice = E.pfnDestroyDevice;                                                \
        (f)->pfnSetTextFilterSize = E.pfnSetTextFilterSize;                                        \
        (f)->pfnCalcPrivateResourceSize = d10_calc_res_size;                                       \
        (f)->pfnCreateResource = d10_create_resource;                                              \
        (f)->pfnCalcPrivateDepthStencilViewSize = d10_calc_dsv;                                    \
        (f)->pfnCreateDepthStencilView = d10_create_dsv;                                           \
        (f)->pfnCalcPrivateGeometryShaderWithStreamOutput = d10_calc_gs_so;                        \
        (f)->pfnCreateGeometryShaderWithStreamOutput = d10_create_gs_so;                           \
        (f)->pfnSetRenderTargets = d10_set_targets;                                                \
    } while (0)

void d10_device_funcs(UINT ten, D3D10DDIARG_CREATEDEVICE *c)
{
    d11_device_funcs(&E);
    if (ten == 101) {
        D3D10_1DDI_DEVICEFUNCS *f = c->p10_1DeviceFuncs;

        D10_SAME(f);
        f->pfnCalcPrivateShaderResourceViewSize = d10_1_calc_srv;
        f->pfnCreateShaderResourceView = d10_1_create_srv;
        f->pfnCalcPrivateBlendStateSize = E.pfnCalcPrivateBlendStateSize;   /* 10.1's blend state is 11's */
        f->pfnCreateBlendState = E.pfnCreateBlendState;
        f->pfnResourceConvert = E.pfnResourceConvert;
        f->pfnResourceConvertRegion = E.pfnResourceConvertRegion;
        f->pfnRelocateDeviceFuncs = d10_1_relocate;
    } else {
        D3D10DDI_DEVICEFUNCS *f = c->pDeviceFuncs;

        D10_SAME(f);
        f->pfnCalcPrivateShaderResourceViewSize = d10_calc_srv;
        f->pfnCreateShaderResourceView = d10_create_srv;
        f->pfnCalcPrivateBlendStateSize = d10_calc_blend;
        f->pfnCreateBlendState = d10_create_blend;
        f->pfnRelocateDeviceFuncs = d10_relocate;
    }
}
