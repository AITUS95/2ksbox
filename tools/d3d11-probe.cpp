/*
 * d3d11-probe: Direct3D 11 on the WDDM driver's own Direct3D 11 user-mode
 * driver (track M25), checked against WARP in the same guest. For each
 * adapter the feature level D3D11CreateDevice gives with every level
 * asked for, and with 10_0 alone; then one scene drawn on our adapter at
 * 10_0 and on WARP, read back and compared pixel by pixel: clears, an
 * indexed textured quad (an immutable texture, a sampler that wraps), a
 * blended triangle in front of it (depth test, a texture written by
 * UpdateSubresource), a dynamic constant buffer mapped per draw, a copy
 * into a staging texture. Then the scene at 10_1 with shader model 4.1
 * (Gather) on ours and on WARP, and through Direct3D 10's API on ours
 * (d3d10.dll, and d3d10_1.dll at 10_1 and 10_0: the driver's 10.0 and 10.1
 * interfaces) against WARP's 10_0 frame. With "present" also a windowed swap chain's
 * frames, read off the screen (a third argument: how many milliseconds
 * each window stays up before that, 1000 by default). With "flags", for
 * each D3D11_CREATE_DEVICE flag the level our adapter gives and the
 * device made (VIDEO_SUPPORT needs the 11.1 interface's video functions,
 * which DWM's device asks for; track M25 step 4).
 *
 * Output into the file argv[1] (stdout without one), each frame as a PPM
 * beside it (d3d11-ours.ppm, d3d11-warp.ppm). The last line says
 * "d3d11-probe: PASS" or "FAIL".
 *
 * Built on the PC in MSYS2's MINGW64 shell, x64 MSVC:
 *   . scripts/msvc-env.sh && MSYS2_ARG_CONV_EXCL='*' cl -nologo -O1 -MT -EHsc tools/d3d11-probe.cpp
 * Run in the guest's logged-on session (a scheduled task with an
 * interactive principal; docs/testing.md "d3d11-probe").
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <d3d11.h>
#include <d3d10_1.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "d3d10_1.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static FILE *out;
static DWORD hold_ms = 1000;      /* how long a presented window stays before it is read (argv[3]) */
static char dir[MAX_PATH];
static CRITICAL_SECTION out_lock;
#define P(...) do { EnterCriticalSection(&out_lock); fprintf(out, __VA_ARGS__); fflush(out); LeaveCriticalSection(&out_lock); } while (0)

/* The runtime's own words: OutputDebugString lines (DXGI's and Direct3D's
 * reasons for a failure), taken through the DBWIN buffer the way a
 * debug-output viewer takes them, by a thread of this process */
static DWORD WINAPI dbwin(void *)
{
    HANDLE buf_ready = CreateEventA(NULL, FALSE, FALSE, "DBWIN_BUFFER_READY");
    HANDLE data_ready = CreateEventA(NULL, FALSE, FALSE, "DBWIN_DATA_READY");
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
    const char *view = map ? (const char *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 4096) : NULL;

    if (!buf_ready || !data_ready || !view) {
        P("debug output: not listening (error %lu)\n", GetLastError());
        return 0;
    }
    for (;;) {
        SetEvent(buf_ready);
        if (WaitForSingleObject(data_ready, INFINITE) != WAIT_OBJECT_0) return 0;
        char line[4096];
        size_t n;

        strncpy(line, view + 4, sizeof(line) - 1);      /* after the writer's process id */
        line[sizeof(line) - 1] = 0;
        n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        P("debug output: %s\n", line);
    }
}

static const char *fl_name(D3D_FEATURE_LEVEL f)
{
    switch (f) {
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    case D3D_FEATURE_LEVEL_9_3: return "9_3";
    case D3D_FEATURE_LEVEL_9_2: return "9_2";
    case D3D_FEATURE_LEVEL_9_1: return "9_1";
    default: return "?";
    }
}

static const char *src =
    "cbuffer c : register(b0) { float4x4 m; float4 tint; };\n"
    "Texture2D t : register(t0); SamplerState s : register(s0);\n"
    "struct v2p { float4 pos : SV_Position; float4 col : COLOR; float2 uv : TEXCOORD; };\n"
    "v2p vs(float3 pos : POSITION, float4 col : COLOR, float2 uv : TEXCOORD) {\n"
    "  v2p o; o.pos = mul(m, float4(pos, 1)); o.col = col; o.uv = uv; return o; }\n"
    "float4 ps(v2p i) : SV_Target { return t.Sample(s, i.uv) * i.col * tint; }\n"
    /* shader model 4.1 (feature level 10_1): Gather, the red of the four
     * texels a bilinear sample reads */
    "float4 ps41(v2p i) : SV_Target { float4 g = t.Gather(s, i.uv);\n"
    "  return lerp(t.Sample(s, i.uv), float4(g.wzy, 1), 0.25) * i.col * tint; }\n";

struct Vtx { float x, y, z; float r, g, b, a; float u, v; };

#define W 256
#define H 256

static ID3DBlob *compile(const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    if (FAILED(D3DCompile(src, strlen(src), "probe", NULL, NULL, entry, target, 0, 0, &code, &err))) {
        P("  compile %s: %s\n", entry, err ? (const char *)err->GetBufferPointer() : "?");
        return NULL;
    }
    return code;
}

static void cb_set(ID3D11DeviceContext *c, ID3D11Buffer *cb, float sx, float sy, float tx, float ty, const float tint[4])
{
    D3D11_MAPPED_SUBRESOURCE m;
    float d[20] = { sx, 0, 0, tx,  0, sy, 0, ty,  0, 0, 1, 0,  0, 0, 0, 1 };
    memcpy(d + 16, tint, 16);
    if (SUCCEEDED(c->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, d, sizeof(d));
        c->Unmap(cb, 0);
    }
}

/* the scene into pixels (B8G8R8A8, W*H), drawn into `target` when given
 * (a swap chain's buffer) or a texture of its own; false when a step failed */
static bool scene(ID3D11Device *dev, ID3D11DeviceContext *c, unsigned *px, ID3D11Texture2D *target = NULL, bool sm41 = false)
{
    bool ok = false;
    ID3DBlob *vsb = compile("vs", sm41 ? "vs_4_1" : "vs_4_0"), *psb = compile(sm41 ? "ps41" : "ps", sm41 ? "ps_4_1" : "ps_4_0");
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11InputLayout *il = NULL;
    ID3D11Buffer *vb = NULL, *ib = NULL, *cb = NULL;
    ID3D11Texture2D *rt = NULL, *ds = NULL, *tex = NULL, *white = NULL, *st = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11ShaderResourceView *srv = NULL, *wsrv = NULL;
    ID3D11SamplerState *smp = NULL;
    ID3D11BlendState *blend = NULL;
    ID3D11DepthStencilState *dss = NULL;
    HRESULT hr;
#define CK(x) do { hr = (x); if (FAILED(hr)) { P("  %s: 0x%08lx\n", #x, (unsigned long)hr); goto done; } } while (0)
    if (!vsb || !psb) goto done;
    CK(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), NULL, &vs));
    CK(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), NULL, &ps));
    {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        CK(dev->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &il));
    }
    {
        /* a quad (4 vertices, 6 indices) and a triangle in front of it */
        Vtx v[] = {
            { -0.8f, -0.8f, 0.5f, 1, 1, 1, 1, 0, 4 }, { -0.8f, 0.8f, 0.5f, 1, 1, 1, 1, 0, 0 },
            { 0.8f, 0.8f, 0.5f, 1, 1, 1, 1, 4, 0 }, { 0.8f, -0.8f, 0.5f, 1, 1, 1, 1, 4, 4 },
            { -0.6f, -0.5f, 0.25f, 1, 0, 0, 0.5f, 0, 0 }, { 0.0f, 0.7f, 0.25f, 0, 1, 0, 0.5f, 0, 0 },
            { 0.6f, -0.5f, 0.25f, 0, 0, 1, 0.5f, 0, 0 } };
        unsigned short idx[] = { 0, 1, 2, 0, 2, 3 };
        D3D11_BUFFER_DESC bd = { sizeof(v), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = { v, 0, 0 };
        CK(dev->CreateBuffer(&bd, &sd, &vb));
        D3D11_BUFFER_DESC ibd = { sizeof(idx), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA isd = { idx, 0, 0 };
        CK(dev->CreateBuffer(&ibd, &isd, &ib));
        D3D11_BUFFER_DESC cbd = { 80, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        CK(dev->CreateBuffer(&cbd, NULL, &cb));
    }
    {
        /* a 64x64 checker, immutable; a 1x1 white written by UpdateSubresource */
        static unsigned checker[64 * 64];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) checker[y * 64 + x] = ((x / 8 + y / 8) & 1) ? 0xffffffffu : 0xff4080c0u;
        D3D11_TEXTURE2D_DESC td = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE,
                                    D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = { checker, 64 * 4, 0 };
        CK(dev->CreateTexture2D(&td, &sd, &tex));
        CK(dev->CreateShaderResourceView(tex, NULL, &srv));
        td.Width = td.Height = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        CK(dev->CreateTexture2D(&td, NULL, &white));
        unsigned w1 = 0xffffffffu;
        c->UpdateSubresource(white, 0, NULL, &w1, 4, 0);
        CK(dev->CreateShaderResourceView(white, NULL, &wsrv));
    }
    {
        D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
        if (target) {
            rt = target;
            rt->AddRef();
        } else {
            CK(dev->CreateTexture2D(&td, NULL, &rt));
        }
        CK(dev->CreateRenderTargetView(rt, NULL, &rtv));
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        CK(dev->CreateTexture2D(&td, NULL, &ds));
        CK(dev->CreateDepthStencilView(ds, NULL, &dsv));
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        CK(dev->CreateTexture2D(&td, NULL, &st));
    }
    {
        D3D11_SAMPLER_DESC s = {};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        CK(dev->CreateSamplerState(&s, &smp));
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        CK(dev->CreateBlendState(&b, &blend));
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = TRUE;
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        d.DepthFunc = D3D11_COMPARISON_LESS;
        CK(dev->CreateDepthStencilState(&d, &dss));
    }
    {
        const float clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f }, one[4] = { 1, 1, 1, 1 }, warm[4] = { 1, 0.9f, 0.8f, 1 };
        D3D11_VIEWPORT vp = { 0, 0, W, H, 0, 1 };
        UINT stride = sizeof(Vtx), off = 0;
        c->OMSetRenderTargets(1, &rtv, dsv);
        c->RSSetViewports(1, &vp);
        c->ClearRenderTargetView(rtv, clear);
        c->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        c->IASetInputLayout(il);
        c->IASetVertexBuffers(0, 1, &vb, &stride, &off);
        c->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs, NULL, 0);
        c->PSSetShader(ps, NULL, 0);
        c->VSSetConstantBuffers(0, 1, &cb);
        c->PSSetConstantBuffers(0, 1, &cb);
        c->PSSetSamplers(0, 1, &smp);
        c->OMSetDepthStencilState(dss, 0);
        cb_set(c, cb, 1, 1, 0, 0, warm);
        c->PSSetShaderResources(0, 1, &srv);
        c->DrawIndexed(6, 0, 0);
        cb_set(c, cb, 1.1f, 1.1f, 0.05f, 0.0f, one);
        c->PSSetShaderResources(0, 1, &wsrv);
        c->OMSetBlendState(blend, NULL, 0xffffffff);
        c->Draw(3, 4);
        c->CopyResource(st, rt);
    }
    {
        D3D11_MAPPED_SUBRESOURCE m;
        CK(c->Map(st, 0, D3D11_MAP_READ, 0, &m));
        for (int y = 0; y < H; y++) memcpy(px + y * W, (const BYTE *)m.pData + y * m.RowPitch, W * 4);
        c->Unmap(st, 0);
    }
    ok = true;
done:
#define R(p) if (p) p->Release()
    R(vsb); R(psb); R(vs); R(ps); R(il); R(vb); R(ib); R(cb); R(rt); R(ds); R(tex); R(white); R(st);
    R(rtv); R(dsv); R(srv); R(wsrv); R(smp); R(blend); R(dss);
    return ok;
}

/* The same scene through Direct3D 10's API (d3d10.dll or d3d10_1.dll's
 * device), with 10.0's blend state, into R8G8B8A8 */
static bool scene10(ID3D10Device *dev, unsigned *px)
{
    bool ok = false;
    ID3DBlob *vsb = compile("vs", "vs_4_0"), *psb = compile("ps", "ps_4_0");
    ID3D10VertexShader *vs = NULL;
    ID3D10PixelShader *ps = NULL;
    ID3D10InputLayout *il = NULL;
    ID3D10Buffer *vb = NULL, *ib = NULL, *cb = NULL;
    ID3D10Texture2D *rt = NULL, *ds = NULL, *tex = NULL, *white = NULL, *st = NULL;
    ID3D10RenderTargetView *rtv = NULL;
    ID3D10DepthStencilView *dsv = NULL;
    ID3D10ShaderResourceView *srv = NULL, *wsrv = NULL;
    ID3D10SamplerState *smp = NULL;
    ID3D10BlendState *blend = NULL;
    ID3D10DepthStencilState *dss = NULL;
    HRESULT hr;
    if (!vsb || !psb) goto done;
    CK(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), &vs));
    CK(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), &ps));
    {
        D3D10_INPUT_ELEMENT_DESC el[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0 } };
        CK(dev->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &il));
    }
    {
        Vtx v[] = {
            { -0.8f, -0.8f, 0.5f, 1, 1, 1, 1, 0, 4 }, { -0.8f, 0.8f, 0.5f, 1, 1, 1, 1, 0, 0 },
            { 0.8f, 0.8f, 0.5f, 1, 1, 1, 1, 4, 0 }, { 0.8f, -0.8f, 0.5f, 1, 1, 1, 1, 4, 4 },
            { -0.6f, -0.5f, 0.25f, 1, 0, 0, 0.5f, 0, 0 }, { 0.0f, 0.7f, 0.25f, 0, 1, 0, 0.5f, 0, 0 },
            { 0.6f, -0.5f, 0.25f, 0, 0, 1, 0.5f, 0, 0 } };
        unsigned short idx[] = { 0, 1, 2, 0, 2, 3 };
        D3D10_BUFFER_DESC bd = { sizeof(v), D3D10_USAGE_IMMUTABLE, D3D10_BIND_VERTEX_BUFFER, 0, 0 };
        D3D10_SUBRESOURCE_DATA sd = { v, 0, 0 };
        CK(dev->CreateBuffer(&bd, &sd, &vb));
        D3D10_BUFFER_DESC ibd = { sizeof(idx), D3D10_USAGE_DEFAULT, D3D10_BIND_INDEX_BUFFER, 0, 0 };
        D3D10_SUBRESOURCE_DATA isd = { idx, 0, 0 };
        CK(dev->CreateBuffer(&ibd, &isd, &ib));
        D3D10_BUFFER_DESC cbd = { 80, D3D10_USAGE_DYNAMIC, D3D10_BIND_CONSTANT_BUFFER, D3D10_CPU_ACCESS_WRITE, 0 };
        CK(dev->CreateBuffer(&cbd, NULL, &cb));
    }
    {
        static unsigned checker[64 * 64];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) checker[y * 64 + x] = ((x / 8 + y / 8) & 1) ? 0xffffffffu : 0xff4080c0u;
        D3D10_TEXTURE2D_DESC td = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D10_USAGE_IMMUTABLE,
                                    D3D10_BIND_SHADER_RESOURCE, 0, 0 };
        D3D10_SUBRESOURCE_DATA sd = { checker, 64 * 4, 0 };
        CK(dev->CreateTexture2D(&td, &sd, &tex));
        CK(dev->CreateShaderResourceView(tex, NULL, &srv));
        td.Width = td.Height = 1;
        td.Usage = D3D10_USAGE_DEFAULT;
        CK(dev->CreateTexture2D(&td, NULL, &white));
        unsigned w1 = 0xffffffffu;
        dev->UpdateSubresource(white, 0, NULL, &w1, 4, 0);
        CK(dev->CreateShaderResourceView(white, NULL, &wsrv));
    }
    {
        D3D10_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D10_USAGE_DEFAULT, D3D10_BIND_RENDER_TARGET, 0, 0 };
        CK(dev->CreateTexture2D(&td, NULL, &rt));
        CK(dev->CreateRenderTargetView(rt, NULL, &rtv));
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.BindFlags = D3D10_BIND_DEPTH_STENCIL;
        CK(dev->CreateTexture2D(&td, NULL, &ds));
        CK(dev->CreateDepthStencilView(ds, NULL, &dsv));
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.BindFlags = 0;
        td.Usage = D3D10_USAGE_STAGING;
        td.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
        CK(dev->CreateTexture2D(&td, NULL, &st));
    }
    {
        D3D10_SAMPLER_DESC s = {};
        s.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D10_TEXTURE_ADDRESS_WRAP;
        s.MaxLOD = D3D10_FLOAT32_MAX;
        CK(dev->CreateSamplerState(&s, &smp));
        D3D10_BLEND_DESC b = {};
        b.BlendEnable[0] = TRUE;
        b.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        b.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        b.BlendOp = D3D10_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D10_BLEND_ONE;
        b.DestBlendAlpha = D3D10_BLEND_ZERO;
        b.BlendOpAlpha = D3D10_BLEND_OP_ADD;
        for (int i = 0; i < 8; i++) b.RenderTargetWriteMask[i] = D3D10_COLOR_WRITE_ENABLE_ALL;
        CK(dev->CreateBlendState(&b, &blend));
        D3D10_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = TRUE;
        d.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
        d.DepthFunc = D3D10_COMPARISON_LESS;
        CK(dev->CreateDepthStencilState(&d, &dss));
    }
    {
        const float clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f }, one[4] = { 1, 1, 1, 1 }, warm[4] = { 1, 0.9f, 0.8f, 1 };
        D3D10_VIEWPORT vp = { 0, 0, W, H, 0, 1 };
        UINT stride = sizeof(Vtx), off = 0;
        float m[20] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
        void *p;
        dev->OMSetRenderTargets(1, &rtv, dsv);
        dev->RSSetViewports(1, &vp);
        dev->ClearRenderTargetView(rtv, clear);
        dev->ClearDepthStencilView(dsv, D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, 1.0f, 0);
        dev->IASetInputLayout(il);
        dev->IASetVertexBuffers(0, 1, &vb, &stride, &off);
        dev->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
        dev->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dev->VSSetShader(vs);
        dev->PSSetShader(ps);
        dev->VSSetConstantBuffers(0, 1, &cb);
        dev->PSSetConstantBuffers(0, 1, &cb);
        dev->PSSetSamplers(0, 1, &smp);
        dev->OMSetDepthStencilState(dss, 0);
        memcpy(m + 16, warm, 16);
        CK(cb->Map(D3D10_MAP_WRITE_DISCARD, 0, &p));
        memcpy(p, m, sizeof(m));
        cb->Unmap();
        dev->PSSetShaderResources(0, 1, &srv);
        dev->DrawIndexed(6, 0, 0);
        float m2[20] = { 1.1f, 0, 0, 0.05f,  0, 1.1f, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
        memcpy(m2 + 16, one, 16);
        CK(cb->Map(D3D10_MAP_WRITE_DISCARD, 0, &p));
        memcpy(p, m2, sizeof(m2));
        cb->Unmap();
        dev->PSSetShaderResources(0, 1, &wsrv);
        float factor[4] = { 0, 0, 0, 0 };
        dev->OMSetBlendState(blend, factor, 0xffffffff);
        dev->Draw(3, 4);
        dev->CopyResource(st, rt);
    }
    {
        D3D10_MAPPED_TEXTURE2D m;
        CK(st->Map(0, D3D10_MAP_READ, 0, &m));
        for (int y = 0; y < H; y++) memcpy(px + y * W, (const BYTE *)m.pData + y * m.RowPitch, W * 4);
        for (int i = 0; i < W * H; i++)     /* R8G8B8A8 (d3d10.dll's 10.0 has no B8G8R8A8 target) to B8G8R8A8 */
            px[i] = (px[i] & 0xff00ff00u) | (px[i] >> 16 & 0xffu) | (px[i] & 0xffu) << 16;
        st->Unmap(0);
    }
    ok = true;
done:
    R(vsb); R(psb); R(vs); R(ps); R(il); R(vb); R(ib); R(cb); R(rt); R(ds); R(tex); R(white); R(st);
    R(rtv); R(dsv); R(srv); R(wsrv); R(smp); R(blend); R(dss);
    return ok;
}

/* pixel differences beyond 8 in a channel, and the largest */
static unsigned diff(const unsigned *a, const unsigned *b, unsigned *maxd)
{
    unsigned off = 0;

    *maxd = 0;
    for (int i = 0; i < W * H; i++) {
        unsigned dm = 0;
        for (int k = 0; k < 24; k += 8) {
            int dd = (int)((a[i] >> k) & 0xff) - (int)((b[i] >> k) & 0xff);
            if (dd < 0) dd = -dd;
            if ((unsigned)dd > dm) dm = dd;
        }
        if (dm > *maxd) *maxd = dm;
        if (dm > 8) off++;
    }
    return off;
}

/* The scene presented: a window with a swap chain on our adapter (a
 * blt-model DISCARD one, then a flip-model FLIP_SEQUENTIAL one), thirty
 * presents, then the window's client area read off the screen, which is
 * what DWM composed */
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

static bool presented(IDXGIAdapter1 *adapter, DXGI_SWAP_EFFECT effect, unsigned *px)
{
    static const D3D_FEATURE_LEVEL ten[] = { D3D_FEATURE_LEVEL_10_0 };
    WNDCLASSA wc = {};
    RECT r = { 0, 0, W, H };
    HWND wnd;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *c = NULL;
    IDXGIFactory1 *fac = NULL;
    IDXGISwapChain *sc = NULL;
    ID3D11Texture2D *bb = NULL;
    static unsigned drawn[W * H];
    bool ok = false;
    HRESULT hr;

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d11probe";
    RegisterClassA(&wc);
    AdjustWindowRect(&r, WS_POPUP, FALSE);
    wnd = CreateWindowExA(WS_EX_TOPMOST, "d3d11probe", "d3d11-probe", WS_POPUP | WS_VISIBLE, 100, 100, W, H, NULL, NULL,
                          wc.hInstance, NULL);
    if (!wnd) { P("  no window\n"); return false; }
    hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, ten, 1, D3D11_SDK_VERSION, &dev, NULL, &c);
    if (FAILED(hr)) { P("  device 0x%08lx\n", (unsigned long)hr); goto done; }
    adapter->GetParent(__uuidof(IDXGIFactory1), (void **)&fac);
    {
        DXGI_SWAP_CHAIN_DESC d = {};
        d.BufferDesc.Width = W;
        d.BufferDesc.Height = H;
        d.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = effect == DXGI_SWAP_EFFECT_DISCARD ? 1 : 2;
        d.OutputWindow = wnd;
        d.Windowed = TRUE;
        d.SwapEffect = effect;
        hr = fac->CreateSwapChain(dev, &d, &sc);
        if (FAILED(hr)) { P("  swap chain 0x%08lx\n", (unsigned long)hr); goto done; }
    }
    for (int f = 0; f < 30; f++) {
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
        if (bb) { bb->Release(); bb = NULL; }
        sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb);
        if (!scene(dev, c, drawn, bb)) goto done;
        hr = sc->Present(1, 0);
        if (FAILED(hr)) { P("  present 0x%08lx\n", (unsigned long)hr); goto done; }
    }
    Sleep(hold_ms);
    {
        HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        void *bits = NULL;
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, W, H, screen, 100, 100, SRCCOPY);
        memcpy(px, bits, W * H * 4);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        for (int i = 0; i < W * H; i++) px[i] |= 0xff000000u;
    }
    ok = true;
done:
    if (bb) bb->Release();
    if (sc) sc->Release();
    if (fac) fac->Release();
    if (c) c->Release();
    if (dev) dev->Release();
    DestroyWindow(wnd);
    return ok;
}

static void ppm(const char *name, const unsigned *px)
{
    char path[MAX_PATH];
    FILE *f;
    snprintf(path, sizeof(path), "%s%s", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = { (unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static unsigned ours[W * H], warp[W * H];
    IDXGIFactory1 *fac = NULL;
    IDXGIAdapter1 *a = NULL, *mine = NULL;
    bool pass = true;

    /* the screen is read at the window's coordinates: a process that is
     * not DPI-aware gets its window scaled (175% on a 1080p desktop) */
    SetProcessDPIAware();
    InitializeCriticalSection(&out_lock);
    out = argc > 1 ? fopen(argv[1], "w") : stdout;
    if (!out) out = stdout;
    CloseHandle(CreateThread(NULL, 0, dbwin, NULL, 0, NULL));
    Sleep(100);
    if (argc > 1) {
        strncpy(dir, argv[1], sizeof(dir) - 1);
        char *s = strrchr(dir, '\\');
        if (s) s[1] = 0; else dir[0] = 0;
    }
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac))) {
        P("no DXGI factory\nd3d11-probe: FAIL\n");
        return 1;
    }
    for (UINT i = 0; fac->EnumAdapters1(i, &a) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d;
        a->GetDesc1(&d);
        P("adapter %u: %ls (%04x:%04x)\n", i, d.Description, d.VendorId, d.DeviceId);
        static const D3D_FEATURE_LEVEL all[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                 D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
        static const D3D_FEATURE_LEVEL ten[] = { D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL got;
        HRESULT hr = D3D11CreateDevice(a, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, all, 7, D3D11_SDK_VERSION, NULL, &got, NULL);
        P("  every level: 0x%08lx, %s\n", (unsigned long)hr, SUCCEEDED(hr) ? fl_name(got) : "-");
        hr = D3D11CreateDevice(a, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, ten, 1, D3D11_SDK_VERSION, NULL, &got, NULL);
        P("  10_0 alone: 0x%08lx\n", (unsigned long)hr);
        if (d.VendorId == 0x1234 && !mine) {
            mine = a;
            mine->AddRef();
        }
        a->Release();
    }
    if (!mine) {
        P("no d3dpt-vga adapter (1234:xxxx)\nd3d11-probe: FAIL\n");
        return 1;
    }
    if (argc > 2 && !strcmp(argv[2], "flags")) {
        /* the level each creation flag gets on ours with every level asked
         * for: 10_0 is this driver, 9_x the runtime's d3d10level9 */
        static const D3D_FEATURE_LEVEL all[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                 D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
        static const struct { UINT flag; const char *name; } fl[] = {
            { 0, "none" }, { D3D11_CREATE_DEVICE_SINGLETHREADED, "SINGLETHREADED" }, { D3D11_CREATE_DEVICE_BGRA_SUPPORT, "BGRA_SUPPORT" },
            { D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, "PREVENT_INTERNAL_THREADING" },
            { D3D11_CREATE_DEVICE_DEBUGGABLE, "DEBUGGABLE" }, { D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT, "DISABLE_GPU_TIMEOUT" },
            { D3D11_CREATE_DEVICE_VIDEO_SUPPORT, "VIDEO_SUPPORT" }, { 0x20000, "0x20000" }, { 0x40000, "0x40000" }, { 0x80000, "0x80000" },
        };
        for (UINT i = 0; i < ARRAYSIZE(fl); i++) {
            D3D_FEATURE_LEVEL got;
            ID3D11Device *dev = NULL;
            HRESULT hr = D3D11CreateDevice(mine, D3D_DRIVER_TYPE_UNKNOWN, NULL, fl[i].flag, all, 7, D3D11_SDK_VERSION, NULL, &got, NULL);
            P("  flag %s: 0x%08lx, %s", fl[i].name, (unsigned long)hr, SUCCEEDED(hr) ? fl_name(got) : "-");
            /* and made: asking the level alone makes no device */
            hr = D3D11CreateDevice(mine, D3D_DRIVER_TYPE_UNKNOWN, NULL, fl[i].flag, all, 7, D3D11_SDK_VERSION, &dev, &got, NULL);
            P("; made: 0x%08lx, %s\n", (unsigned long)hr, SUCCEEDED(hr) ? fl_name(got) : "-");
            if (dev) dev->Release();
        }
        return 0;
    }
    {
        static const D3D_FEATURE_LEVEL ten[] = { D3D_FEATURE_LEVEL_10_0 };
        ID3D11Device *dev = NULL, *wdev = NULL;
        ID3D11DeviceContext *c = NULL, *wc = NULL;
        HRESULT hr = D3D11CreateDevice(mine, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, ten, 1, D3D11_SDK_VERSION, &dev, NULL, &c);
        P("scene on ours (10_0): device 0x%08lx\n", (unsigned long)hr);
        bool a_ok = SUCCEEDED(hr) && scene(dev, c, ours);
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, ten, 1, D3D11_SDK_VERSION, &wdev, NULL, &wc);
        P("scene on WARP (10_0): device 0x%08lx\n", (unsigned long)hr);
        bool b_ok = SUCCEEDED(hr) && scene(wdev, wc, warp);
        if (!a_ok || !b_ok) {
            P("scene: ours %s, WARP %s\n", a_ok ? "drawn" : "failed", b_ok ? "drawn" : "failed");
            pass = false;
        } else {
            unsigned maxd, off = diff(ours, warp, &maxd);
            P("scene: %u of %u pixels differ by more than 8 (largest %u); pixel (128,128) ours 0x%08x WARP 0x%08x, (4,4) 0x%08x / 0x%08x\n",
              off, W * H, maxd, ours[128 * W + 128], warp[128 * W + 128], ours[4 * W + 4], warp[4 * W + 4]);
            pass = off < 200;
        }
        if (argc > 1) {
            ppm("d3d11-ours.ppm", ours);
            ppm("d3d11-warp.ppm", warp);
        }
        /* feature level 10_1: the scene with shader model 4.1 (Gather) */
        {
            static const D3D_FEATURE_LEVEL ten1[] = { D3D_FEATURE_LEVEL_10_1 };
            static unsigned ours41[W * H], warp41[W * H];
            ID3D11Device *d41 = NULL, *w41 = NULL;
            ID3D11DeviceContext *c41 = NULL, *wc41 = NULL;
            hr = D3D11CreateDevice(mine, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, ten1, 1, D3D11_SDK_VERSION, &d41, NULL, &c41);
            P("scene on ours (10_1, shader model 4.1): device 0x%08lx\n", (unsigned long)hr);
            bool o41 = SUCCEEDED(hr) && scene(d41, c41, ours41, NULL, true);
            hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, ten1, 1, D3D11_SDK_VERSION, &w41, NULL, &wc41);
            bool k41 = SUCCEEDED(hr) && scene(w41, wc41, warp41, NULL, true);
            if (!o41 || !k41) {
                P("scene 10_1: ours %s, WARP %s\n", o41 ? "drawn" : "failed", k41 ? "drawn" : "failed");
                pass = false;
            } else {
                unsigned maxd, off = diff(ours41, warp41, &maxd);
                P("scene 10_1: %u of %u pixels differ by more than 8 (largest %u)\n", off, W * H, maxd);
                pass = pass && off < 200;
            }
            if (argc > 1) ppm("d3d11-ours-10_1.ppm", ours41);
            R(c41); R(d41); R(wc41); R(w41);
        }
        /* Direct3D 10 programs: d3d10.dll's device, and d3d10_1.dll's at
         * 10_1 and at 10_0, against the 10_0 scene on WARP */
        if (b_ok) {
            static unsigned ten_px[W * H];
            static const struct { int lib; D3D10_FEATURE_LEVEL1 fl; const char *name, *file; } d10[] = {
                { 0, D3D10_FEATURE_LEVEL_10_0, "d3d10.dll", "d3d10-ours.ppm" },
                { 1, D3D10_FEATURE_LEVEL_10_1, "d3d10_1.dll at 10_1", "d3d10_1-ours.ppm" },
                { 1, D3D10_FEATURE_LEVEL_10_0, "d3d10_1.dll at 10_0", "d3d10_1-ours-10_0.ppm" } };
            for (int k = 0; k < 3; k++) {
                ID3D10Device *d10dev = NULL;
                ID3D10Device1 *d101 = NULL;
                if (d10[k].lib == 0) {
                    hr = D3D10CreateDevice(mine, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &d10dev);
                } else {
                    hr = D3D10CreateDevice1(mine, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, d10[k].fl, D3D10_1_SDK_VERSION, &d101);
                    d10dev = d101;
                }
                P("Direct3D 10, %s: device 0x%08lx\n", d10[k].name, (unsigned long)hr);
                if (FAILED(hr) || !scene10(d10dev, ten_px)) {
                    P("Direct3D 10, %s: failed\n", d10[k].name);
                    pass = false;
                } else {
                    unsigned maxd, off = diff(ten_px, warp, &maxd);
                    P("Direct3D 10, %s: %u of %u pixels differ by more than 8 (largest %u)\n", d10[k].name, off, W * H, maxd);
                    pass = pass && off < 200;
                    if (argc > 1) ppm(d10[k].file, ten_px);
                }
                R(d10dev);
            }
        }
        /* "present": the scene through a swap chain and DWM, both models */
        if (argc > 3) hold_ms = (DWORD)atoi(argv[3]);
        if (b_ok && argc > 2 && !strcmp(argv[2], "present")) {
            static unsigned shown[W * H];
            static const struct { DXGI_SWAP_EFFECT e; const char *name, *file; } models[] = {
                { DXGI_SWAP_EFFECT_DISCARD, "blt model (DISCARD)", "d3d11-present-blt.ppm" },
                { DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, "flip model (FLIP_SEQUENTIAL)", "d3d11-present-flip.ppm" } };
            for (int m = 0; m < 2; m++) {
                if (!presented(mine, models[m].e, shown)) {
                    P("present, %s: failed\n", models[m].name);
                    pass = false;
                    continue;
                }
                unsigned maxd, off = diff(shown, warp, &maxd);
                P("present, %s: %u of %u pixels differ by more than 8 (largest %u); (128,128) 0x%08x, (4,4) 0x%08x\n", models[m].name,
                  off, W * H, maxd, shown[128 * W + 128], shown[4 * W + 4]);
                pass = pass && off < 200;
                if (argc > 1) ppm(models[m].file, shown);
            }
        }
    }
    P("d3d11-probe: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
