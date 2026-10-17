/*
 * d3dgame11: the reference workload's scene (d3dgame.h, doc 14 P0a) on
 * Direct3D 11 at feature level 10_0, the level the WDDM driver's Direct3D
 * 11 user-mode driver gives (track M25). The same ground, cubes, wave grid,
 * particles, monitor and frame bars as d3dgame9, each drawn with what
 * Direct3D 10 / 11 has and 9 does not:
 *
 *   everything      Shader Model 4.0 vertex, geometry and pixel shaders and
 *                   constant buffers (a dynamic one mapped per draw); no
 *                   fixed function
 *   cubes           one instanced draw (SV_InstanceID, a per-instance
 *                   buffer of world matrices and colours), each cube a slice
 *                   of a texture array
 *   wave grid       computed on the GPU from SV_VertexID and the time,
 *                   captured by stream output, then drawn indexed from the
 *                   captured buffer; DrawAuto draws the same buffer again as
 *                   sparkles on the grid's vertices
 *   particles       a point list a geometry shader expands to camera-facing
 *                   quads (BC1 texture, additive)
 *   monitor         render to texture, then GenerateMips; sampled trilinear
 *   markers         occlusion predicates (SetPredication): a green marker is
 *                   drawn only if its test box is visible, a red one only if
 *                   its box under the ground is (it never is)
 *   the frame       drawn into a 16-bit float target, then tonemapped onto
 *                   the swap chain by a fullscreen triangle with no vertex
 *                   buffer (SV_VertexID) that Loads the texels (SV_Position)
 *
 * Options as d3dgame.h's (-frames, -dump, -w, -h, -novsync, -log; -fs, -bpp16
 * and -shader do nothing here), and:
 *   -warp         run on WARP instead of the hardware adapter
 *   -flip         a flip-model swap chain (FLIP_SEQUENTIAL) instead of DISCARD
 *   -compare N    at frame N, also draw the frame offscreen on our adapter and
 *                 on WARP and compare them pixel by pixel (d3d11-ours.bmp,
 *                 d3d11-warp.bmp beside the log); the last line says
 *                 "d3dgame11: PASS" or "FAIL", and the program exits
 * The adapter is the first one whose name has "2ksbox" in it, else the
 * default one. Log: C:\2KSBOX\D3DGAME11.LOG (guestlog.h).
 *
 * Build (guest-tools/build-wrappers.sh, as d3dgame9; a 32-bit program on
 * x64 Windows runs on the WoW64 copy of the user-mode driver):
 *   i686-w64-mingw32-gcc -O2 -o d3dgame11.exe d3dgame11.c -ld3d11 -ldxgi -luuid -lgdi32 -luser32
 * The shaders are compiled at run time by Windows' d3dcompiler_47.dll.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define COBJMACROS
#define INITGUID
#include "d3dgame.h"
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#define MARKERS 2

static const char hlsl[] =
    "cbuffer Frame : register(b0) {\n"
    "  row_major float4x4 vp; float4 ldir; float4 cam_right; float4 cam_up; float4 tm; float4 screen;\n"
    "};\n"
    "cbuffer Object : register(b1) { row_major float4x4 world; float4 color; };\n"
    "Texture2D t0 : register(t0);\n"
    "Texture2DArray tarr : register(t1);\n"
    "SamplerState s0 : register(s0);\n"
    "float light(float3 n) { return saturate(dot(normalize(n), -ldir.xyz)) * 0.8 + 0.25; }\n"
    /* ground and markers: one world matrix, lit */
    "struct VIn { float3 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VOut { float4 pos : SV_Position; float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
    "VOut vs_lit(VIn v) {\n"
    "  VOut o; float4 w = mul(float4(v.pos, 1), world); o.pos = mul(w, vp);\n"
    "  o.col = float4(color.rgb * light(mul(v.n, (float3x3)world)), color.a); o.uv = v.uv; return o; }\n"
    "float4 ps_tex(VOut i) : SV_Target { return t0.Sample(s0, i.uv) * i.col; }\n"
    "float4 ps_col(VOut i) : SV_Target { return i.col; }\n"
    /* cubes: instanced, a texture array slice each; lit a little over 1 (the HDR target keeps it) */
    "struct CIn { float3 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0;\n"
    "  float4 w0 : WORLD0; float4 w1 : WORLD1; float4 w2 : WORLD2; float4 w3 : WORLD3; float4 icol : COLOR1; };\n"
    "struct COut { float4 pos : SV_Position; float4 col : COLOR0; float3 uvs : TEXCOORD0; };\n"
    "COut vs_cube(CIn v, uint iid : SV_InstanceID) {\n"
    "  COut o; float4x4 w = float4x4(v.w0, v.w1, v.w2, v.w3);\n"
    "  o.pos = mul(mul(float4(v.pos, 1), w), vp);\n"
    "  o.col = float4(v.icol.rgb * light(mul(v.n, (float3x3)w)) * 1.6, 1);\n"
    "  o.uvs = float3(v.uv, (float)iid); return o; }\n"
    "float4 ps_cube(COut i) : SV_Target { return tarr.Sample(s0, i.uvs) * i.col; }\n"
    /* the wave grid, made on the GPU: the same wave as d3dgame.h's geo_grid_fill */
    "struct GV { float4 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; };\n"
    "GV vs_gridgen(uint id : SV_VertexID) {\n"
    "  GV o; uint x = id % 32, z = id / 32;\n"
    "  float fx = (x / 31.0 - 0.5) * 6.0, fz = (z / 31.0 - 0.5) * 6.0, t = tm.x;\n"
    "  float h = 0.25 * sin(fx * 1.5 + t * 2.0) * cos(fz * 1.3 - t * 1.7);\n"
    "  float c = saturate((h + 0.25) * 2.0);\n"
    "  o.pos = float4(fx, h - 0.9, fz + 6.0, 1); o.col = float4(c, 1 - c, 0.5, 1);\n"
    "  o.uv = float2(x / 31.0, z / 31.0) * 4.0; return o; }\n"
    "[maxvertexcount(1)] void gs_so(point GV p[1], inout PointStream<GV> s) { s.Append(p[0]); }\n"
    "VOut vs_grid(GV v) { VOut o; o.pos = mul(v.pos, vp); o.col = v.col; o.uv = v.uv; return o; }\n"
    /* particles: points the geometry shader turns into camera-facing quads */
    "struct PIn { float3 pos : POSITION; float4 col : COLOR; float size : PSIZE; };\n"
    "PIn vs_part(PIn v) { v.col = v.col.zyxw; return v; }\n"           /* a D3DCOLOR's bytes are B, G, R, A */
    "PIn vs_dot(GV v) { PIn o; o.pos = v.pos.xyz + float3(0, 0.05, 0); o.col = float4(1, 1, 0.6, 0.7); o.size = 0.05; return o; }\n"
    "[maxvertexcount(4)] void gs_bill(point PIn p[1], inout TriangleStream<VOut> s) {\n"
    "  static const float2 c[4] = { float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1) };\n"
    "  for (int k = 0; k < 4; k++) {\n"
    "    VOut o; float3 w = p[0].pos + (cam_right.xyz * c[k].x + cam_up.xyz * c[k].y) * p[0].size;\n"
    "    o.pos = mul(float4(w, 1), vp); o.col = p[0].col; o.uv = c[k] * float2(0.5, -0.5) + 0.5; s.Append(o); } }\n"
    "float4 ps_part(VOut i) : SV_Target { return float4(t0.Sample(s0, i.uv).rgb * i.col.rgb, i.col.a); }\n"
    /* the tonemap: a triangle over the screen, no vertex buffer, the HDR texels loaded by position */
    "float4 vs_full(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
    "float4 ps_tone(float4 sp : SV_Position) : SV_Target {\n"
    "  float3 c = t0.Load(int3(sp.xy, 0)).rgb;\n"
    "  c = c * (1 + c / 4.0) / (1 + c);\n"
    "  float2 d = sp.xy / screen.xy - 0.5; c *= 1 - dot(d, d) * 0.5;\n"
    "  return float4(c, 1); }\n"
    /* frame-time bars, in pixels */
    "struct BIn { float4 pos : POSITION; float4 col : COLOR; };\n"
    "VOut vs_bar(BIn v) { VOut o; o.pos = float4(v.pos.x / screen.x * 2 - 1, 1 - v.pos.y / screen.y * 2, 0, 1);\n"
    "  o.col = v.col.zyxw; o.uv = 0; return o; }\n";

struct frame_cb { struct mat4 vp; float ldir[4], cam_right[4], cam_up[4], tm[4], screen[4]; };
struct object_cb { struct mat4 world; float color[4]; };
struct inst { float w[4][4]; float col[4]; };
struct vtx_part { float x, y, z; DWORD color; float size; };
struct vtx_gv { float pos[4], col[4], uv[2]; };          /* what stream output writes: 40 bytes */

typedef HRESULT (WINAPI *pfn_compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob **, ID3DBlob **);
static pfn_compile compile;

/* one device's everything: the game draws on one, -compare on two */
struct gfx {
    const char *name;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    D3D_FEATURE_LEVEL level;
    ID3D11Buffer *cb_frame, *cb_obj, *vb_cube, *ib_cube, *vb_inst, *vb_ground, *ib_grid, *so_grid, *vb_part, *vb_bars;
    ID3D11Texture2D *tex_checker, *tex_arr, *tex_grad, *tex_disc, *tex_rtt, *tex_hdr, *tex_out, *tex_stage;
    ID3D11ShaderResourceView *srv_checker, *srv_arr, *srv_grad, *srv_disc, *srv_rtt, *srv_hdr;
    ID3D11RenderTargetView *rtv_rtt, *rtv_hdr, *rtv_out;
    ID3D11DepthStencilView *dsv_rtt, *dsv_main;
    int ds_w, ds_h;
    ID3D11VertexShader *vs_lit, *vs_cube, *vs_gridgen, *vs_grid, *vs_part, *vs_dot, *vs_full, *vs_bar;
    ID3D11GeometryShader *gs_so, *gs_bill;
    ID3D11PixelShader *ps_tex, *ps_col, *ps_cube, *ps_part, *ps_tone;
    ID3D11InputLayout *il_lit, *il_cube, *il_gv, *il_part, *il_bar;
    ID3D11SamplerState *smp;
    ID3D11BlendState *bs_add;
    ID3D11DepthStencilState *ds_nowrite, *ds_off;
    ID3D11RasterizerState *rs_solid, *rs_wire, *rs_none;
    ID3D11Predicate *pred[MARKERS];
    ID3D11Query *occ[MARKERS];
    UINT64 occ_samples[MARKERS];
    int so_ok, pred_ok;
};

static struct game G;
static struct gfx X, W;                 /* the game's device, and WARP for -compare */
static IDXGISwapChain *swap;
static ID3D11RenderTargetView *rtv_back;
static int compare_frame = -1, use_warp, use_flip;

static const char *hr_str(HRESULT hr) { static char b[4][32]; static int k; k = (k + 1) & 3; sprintf(b[k], "0x%08lx", (unsigned long)hr); return b[k]; }
#define CHK(call) do { HRESULT hr_ = (call); if (FAILED(hr_)) { game_log("d3dgame11 [%s]: %s failed %s", x->name, #call, hr_str(hr_)); return 0; } } while (0)

/* ---------------------------------------------------------------- shaders */

static ID3DBlob *shader(struct gfx *x, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = compile(hlsl, sizeof(hlsl) - 1, "d3dgame11", NULL, NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                         &code, &err);
    if (err) {
        game_log("d3dgame11 [%s]: %s (%s): %s", x->name, entry, target, (const char *)ID3D10Blob_GetBufferPointer(err));
        ID3D10Blob_Release(err);
    }
    if (FAILED(hr)) {
        game_log("d3dgame11 [%s]: D3DCompile %s %s", x->name, entry, hr_str(hr));
        return NULL;
    }
    return code;
}

#define BP(b) ID3D10Blob_GetBufferPointer(b)
#define BS(b) ID3D10Blob_GetBufferSize(b)

static int make_vs(struct gfx *x, const char *entry, ID3D11VertexShader **vs, const D3D11_INPUT_ELEMENT_DESC *el, UINT n,
                   ID3D11InputLayout **il)
{
    ID3DBlob *b = shader(x, entry, "vs_4_0");
    HRESULT hr;
    if (!b) return 0;
    hr = ID3D11Device_CreateVertexShader(x->dev, BP(b), BS(b), NULL, vs);
    if (SUCCEEDED(hr) && el) hr = ID3D11Device_CreateInputLayout(x->dev, el, n, BP(b), BS(b), il);
    ID3D10Blob_Release(b);
    if (FAILED(hr)) { game_log("d3dgame11 [%s]: vertex shader %s / its layout %s", x->name, entry, hr_str(hr)); return 0; }
    return 1;
}

static int make_ps(struct gfx *x, const char *entry, ID3D11PixelShader **ps)
{
    ID3DBlob *b = shader(x, entry, "ps_4_0");
    HRESULT hr;
    if (!b) return 0;
    hr = ID3D11Device_CreatePixelShader(x->dev, BP(b), BS(b), NULL, ps);
    ID3D10Blob_Release(b);
    if (FAILED(hr)) { game_log("d3dgame11 [%s]: pixel shader %s %s", x->name, entry, hr_str(hr)); return 0; }
    return 1;
}

static int make_shaders(struct gfx *x)
{
    static const D3D11_INPUT_ELEMENT_DESC lit[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    static const D3D11_INPUT_ELEMENT_DESC cube[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 } };
    static const D3D11_INPUT_ELEMENT_DESC gv[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    static const D3D11_INPUT_ELEMENT_DESC part[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "PSIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    static const D3D11_INPUT_ELEMENT_DESC bar[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    /* stream output: the grid vertex as the SO buffer holds it */
    static const D3D11_SO_DECLARATION_ENTRY so[] = {
        { 0, "POSITION", 0, 0, 4, 0 }, { 0, "COLOR", 0, 0, 4, 0 }, { 0, "TEXCOORD", 0, 0, 2, 0 } };
    UINT stride = sizeof(struct vtx_gv);
    ID3DBlob *b;
    HRESULT hr;

    if (!make_vs(x, "vs_lit", &x->vs_lit, lit, 3, &x->il_lit) || !make_vs(x, "vs_cube", &x->vs_cube, cube, 8, &x->il_cube) ||
        !make_vs(x, "vs_gridgen", &x->vs_gridgen, NULL, 0, NULL) || !make_vs(x, "vs_grid", &x->vs_grid, gv, 3, &x->il_gv) ||
        !make_vs(x, "vs_part", &x->vs_part, part, 3, &x->il_part) || !make_vs(x, "vs_dot", &x->vs_dot, NULL, 0, NULL) ||
        !make_vs(x, "vs_full", &x->vs_full, NULL, 0, NULL) || !make_vs(x, "vs_bar", &x->vs_bar, bar, 2, &x->il_bar) ||
        !make_ps(x, "ps_tex", &x->ps_tex) || !make_ps(x, "ps_col", &x->ps_col) || !make_ps(x, "ps_cube", &x->ps_cube) ||
        !make_ps(x, "ps_part", &x->ps_part) || !make_ps(x, "ps_tone", &x->ps_tone))
        return 0;
    if (!(b = shader(x, "gs_bill", "gs_4_0"))) return 0;
    hr = ID3D11Device_CreateGeometryShader(x->dev, BP(b), BS(b), NULL, &x->gs_bill);
    ID3D10Blob_Release(b);
    if (FAILED(hr)) { game_log("d3dgame11 [%s]: geometry shader gs_bill %s", x->name, hr_str(hr)); return 0; }
    if (!(b = shader(x, "gs_so", "gs_4_0"))) return 0;
    hr = ID3D11Device_CreateGeometryShaderWithStreamOutput(x->dev, BP(b), BS(b), so, 3, &stride, 1, 0, NULL, &x->gs_so);
    ID3D10Blob_Release(b);
    x->so_ok = SUCCEEDED(hr);
    if (!x->so_ok) game_log("d3dgame11 [%s]: geometry shader with stream output %s: the grid is not drawn", x->name, hr_str(hr));
    return 1;
}

/* ---------------------------------------------------------------- resources */

static int buffer(struct gfx *x, UINT bytes, UINT bind, D3D11_USAGE usage, const void *data, ID3D11Buffer **out)
{
    D3D11_BUFFER_DESC d;
    D3D11_SUBRESOURCE_DATA s;
    memset(&d, 0, sizeof(d));
    d.ByteWidth = bytes;
    d.Usage = usage;
    d.BindFlags = bind;
    d.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
    s.pSysMem = data; s.SysMemPitch = s.SysMemSlicePitch = 0;
    CHK(ID3D11Device_CreateBuffer(x->dev, &d, data ? &s : NULL, out));
    return 1;
}

/* D3DCOLOR texels (B, G, R, A in memory) into R8G8B8A8: every 10_0 driver samples it */
static void bgra_to_rgba(DWORD *p, int n)
{
    int i;
    for (i = 0; i < n; i++) p[i] = (p[i] & 0xff00ff00) | ((p[i] >> 16) & 0xff) | ((p[i] & 0xff) << 16);
}

static int texture(struct gfx *x, int w, int h, int mips, int array, DXGI_FORMAT f, UINT bind, UINT misc,
                   const D3D11_SUBRESOURCE_DATA *init, ID3D11Texture2D **t, ID3D11ShaderResourceView **srv)
{
    D3D11_TEXTURE2D_DESC d;
    memset(&d, 0, sizeof(d));
    d.Width = w; d.Height = h; d.MipLevels = mips; d.ArraySize = array; d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = init && !(bind & D3D11_BIND_RENDER_TARGET) ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
    d.BindFlags = bind; d.MiscFlags = misc;
    CHK(ID3D11Device_CreateTexture2D(x->dev, &d, init, t));
    if (srv) CHK(ID3D11Device_CreateShaderResourceView(x->dev, (ID3D11Resource *)*t, NULL, srv));
    return 1;
}

static int depth(struct gfx *x, int w, int h, ID3D11DepthStencilView **dsv)
{
    ID3D11Texture2D *t;
    D3D11_TEXTURE2D_DESC d;
    memset(&d, 0, sizeof(d));
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    CHK(ID3D11Device_CreateTexture2D(x->dev, &d, NULL, &t));
    CHK(ID3D11Device_CreateDepthStencilView(x->dev, (ID3D11Resource *)t, NULL, dsv));
    ID3D11Texture2D_Release(t);
    return 1;
}

/* the targets the size of the screen: the HDR scene, its depth, the
 * offscreen LDR frame -compare reads back, and its staging copy */
static int make_targets(struct gfx *x, int w, int h)
{
    D3D11_TEXTURE2D_DESC d;
    if (x->ds_w == w && x->ds_h == h) return 1;
    if (!texture(x, w, h, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, NULL,
                 &x->tex_hdr, &x->srv_hdr))
        return 0;
    CHK(ID3D11Device_CreateRenderTargetView(x->dev, (ID3D11Resource *)x->tex_hdr, NULL, &x->rtv_hdr));
    if (!depth(x, w, h, &x->dsv_main)) return 0;
    if (!texture(x, w, h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET, 0, NULL, &x->tex_out, NULL)) return 0;
    CHK(ID3D11Device_CreateRenderTargetView(x->dev, (ID3D11Resource *)x->tex_out, NULL, &x->rtv_out));
    memset(&d, 0, sizeof(d));
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHK(ID3D11Device_CreateTexture2D(x->dev, &d, NULL, &x->tex_stage));
    x->ds_w = w; x->ds_h = h;
    return 1;
}

static int make_resources(struct gfx *x)
{
    struct vtx_pnt cube[24];
    WORD cidx[36], gidx[GRID_INDICES];
    struct vtx_pnt ground[4] = {
        { -12, -1, -9, 0, 1, 0, 0, 0 }, { -12, -1, 15, 0, 1, 0, 0, 8 },
        { 12, -1, -9, 0, 1, 0, 8, 0 }, { 12, -1, 15, 0, 1, 0, 8, 8 } };
    D3D11_SUBRESOURCE_DATA init[8 * NUM_CUBES];
    static DWORD checker[128 * 128 * 2], arr[NUM_CUBES][64 * 64], grad[64 * 64];
    static BYTE disc[64 * 64 / 2];
    int i, level, w, off;

    geo_cube(cube, cidx);
    geo_grid_indices(gidx);
    if (!buffer(x, sizeof(cube), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, cube, &x->vb_cube) ||
        !buffer(x, sizeof(cidx), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, cidx, &x->ib_cube) ||
        !buffer(x, sizeof(ground), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, ground, &x->vb_ground) ||
        !buffer(x, sizeof(gidx), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, gidx, &x->ib_grid) ||
        !buffer(x, sizeof(struct inst) * NUM_CUBES, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, NULL, &x->vb_inst) ||
        !buffer(x, sizeof(struct vtx_part) * NUM_PARTICLES, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, NULL, &x->vb_part) ||
        !buffer(x, sizeof(struct vtx_rhw) * BARS * 6, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, NULL, &x->vb_bars) ||
        !buffer(x, sizeof(struct vtx_gv) * GRID_VERTS, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT, D3D11_USAGE_DEFAULT,
                NULL, &x->so_grid) ||
        !buffer(x, sizeof(struct frame_cb), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, NULL, &x->cb_frame) ||
        !buffer(x, sizeof(struct object_cb), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, NULL, &x->cb_obj))
        return 0;

    /* the ground's checker with its whole mip chain, immutable */
    for (level = 0, w = 128, off = 0; w >= 1; level++, w >>= 1) {
        tex_checker_8888(checker + off, w * 4, w, w, level);
        bgra_to_rgba(checker + off, w * w);
        init[level].pSysMem = checker + off; init[level].SysMemPitch = w * 4; init[level].SysMemSlicePitch = 0;
        off += w * w;
    }
    if (!texture(x, 128, 128, level, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0, init, &x->tex_checker,
                 &x->srv_checker))
        return 0;
    /* the cubes' texture array: the checker's top level, tinted per slice */
    for (i = 0; i < NUM_CUBES; i++) {
        int k;
        tex_checker_8888(arr[i], 64 * 4, 64, 64, 1);
        for (k = 0; k < 64 * 64; k++) {
            DWORD p = arr[i][k];
            if ((k / 64 + k % 64 / 8) % 5 == i) p = (p & 0xff000000) | ((p >> 1) & 0x7f7f7f) | (0x30u << (8 * (i % 3)));
            arr[i][k] = p;
        }
        bgra_to_rgba(arr[i], 64 * 64);
        init[i].pSysMem = arr[i]; init[i].SysMemPitch = 64 * 4; init[i].SysMemSlicePitch = 0;
    }
    if (!texture(x, 64, 64, 1, NUM_CUBES, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0, init, &x->tex_arr, &x->srv_arr))
        return 0;
    /* the grid's gradient (565 in d3dgame9; 10_0 has no 565 texture) */
    {
        static WORD g565[64 * 64];
        int k;
        tex_gradient_565(g565, 64 * 2, 64, 64);
        for (k = 0; k < 64 * 64; k++) {
            WORD p = g565[k];
            grad[k] = 0xff000000u | (DWORD)((p >> 11) << 3) | ((DWORD)((p >> 5 & 63) << 2) << 8) | ((DWORD)((p & 31) << 3) << 16);
        }
        init[0].pSysMem = grad; init[0].SysMemPitch = 64 * 4;
        if (!texture(x, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0, init, &x->tex_grad, &x->srv_grad))
            return 0;
    }
    /* the particles' disc, BC1 (DXT1) */
    tex_disc_dxt1(disc, 16 * 8, 64, 64);
    init[0].pSysMem = disc; init[0].SysMemPitch = 16 * 8;
    if (!texture(x, 64, 64, 1, 1, DXGI_FORMAT_BC1_UNORM, D3D11_BIND_SHADER_RESOURCE, 0, init, &x->tex_disc, &x->srv_disc))
        return 0;
    /* the monitor: a render target with a mip chain GenerateMips fills */
    if (!texture(x, RTT_SIZE, RTT_SIZE, 0, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                 D3D11_RESOURCE_MISC_GENERATE_MIPS, NULL, &x->tex_rtt, &x->srv_rtt))
        return 0;
    CHK(ID3D11Device_CreateRenderTargetView(x->dev, (ID3D11Resource *)x->tex_rtt, NULL, &x->rtv_rtt));
    if (!depth(x, RTT_SIZE, RTT_SIZE, &x->dsv_rtt)) return 0;

    /* states */
    {
        D3D11_SAMPLER_DESC s;
        D3D11_BLEND_DESC b;
        D3D11_DEPTH_STENCIL_DESC ds;
        D3D11_RASTERIZER_DESC r;
        D3D11_QUERY_DESC q;
        memset(&s, 0, sizeof(s));
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        CHK(ID3D11Device_CreateSamplerState(x->dev, &s, &x->smp));
        memset(&b, 0, sizeof(b));
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; b.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        CHK(ID3D11Device_CreateBlendState(x->dev, &b, &x->bs_add));
        memset(&ds, 0, sizeof(ds));
        ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; ds.DepthFunc = D3D11_COMPARISON_LESS;
        CHK(ID3D11Device_CreateDepthStencilState(x->dev, &ds, &x->ds_nowrite));
        ds.DepthEnable = FALSE;
        CHK(ID3D11Device_CreateDepthStencilState(x->dev, &ds, &x->ds_off));
        memset(&r, 0, sizeof(r));
        r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_BACK; r.DepthClipEnable = TRUE;
        CHK(ID3D11Device_CreateRasterizerState(x->dev, &r, &x->rs_solid));
        r.CullMode = D3D11_CULL_NONE;
        CHK(ID3D11Device_CreateRasterizerState(x->dev, &r, &x->rs_none));
        r.FillMode = D3D11_FILL_WIREFRAME;
        CHK(ID3D11Device_CreateRasterizerState(x->dev, &r, &x->rs_wire));
        x->pred_ok = 1;
        for (i = 0; i < MARKERS; i++) {
            q.Query = D3D11_QUERY_OCCLUSION_PREDICATE; q.MiscFlags = 0;
            if (FAILED(ID3D11Device_CreatePredicate(x->dev, &q, &x->pred[i]))) x->pred_ok = 0;
            q.Query = D3D11_QUERY_OCCLUSION;
            CHK(ID3D11Device_CreateQuery(x->dev, &q, &x->occ[i]));
        }
        if (!x->pred_ok) game_log("d3dgame11 [%s]: no occlusion predicate: the markers are drawn unconditionally", x->name);
    }
    return 1;
}

/* ---------------------------------------------------------------- drawing */

static void set_object(struct gfx *x, const struct mat4 *world, float r, float g, float b, float a)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(x->ctx, (ID3D11Resource *)x->cb_obj, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        struct object_cb *o = (struct object_cb *)m.pData;
        o->world = *world;
        o->color[0] = r; o->color[1] = g; o->color[2] = b; o->color[3] = a;
        ID3D11DeviceContext_Unmap(x->ctx, (ID3D11Resource *)x->cb_obj, 0);
    }
}

static void set_frame(struct gfx *x, const struct mat4 *view, const struct mat4 *proj, int w, int h)
{
    struct frame_cb f;
    float l = sqrtf(0.25f + 1.0f + 0.16f);
    memset(&f, 0, sizeof(f));
    m_mul(&f.vp, view, proj);
    f.ldir[0] = -0.5f / l; f.ldir[1] = -1.0f / l; f.ldir[2] = 0.4f / l;
    f.cam_right[0] = view->m[0][0]; f.cam_right[1] = view->m[1][0]; f.cam_right[2] = view->m[2][0];
    f.cam_up[0] = view->m[0][1]; f.cam_up[1] = view->m[1][1]; f.cam_up[2] = view->m[2][1];
    f.tm[0] = G.t;
    f.screen[0] = (float)w; f.screen[1] = (float)h;
    ID3D11DeviceContext_UpdateSubresource(x->ctx, (ID3D11Resource *)x->cb_frame, 0, NULL, &f, 0, 0);
}

static void viewport(struct gfx *x, int w, int h)
{
    D3D11_VIEWPORT v = { 0, 0, (float)w, (float)h, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(x->ctx, 1, &v);
}

static void bind_common(struct gfx *x)
{
    ID3D11Buffer *cbs[2] = { x->cb_frame, x->cb_obj };
    ID3D11DeviceContext_VSSetConstantBuffers(x->ctx, 0, 2, cbs);
    ID3D11DeviceContext_GSSetConstantBuffers(x->ctx, 0, 2, cbs);
    ID3D11DeviceContext_PSSetConstantBuffers(x->ctx, 0, 2, cbs);
    ID3D11DeviceContext_PSSetSamplers(x->ctx, 0, 1, &x->smp);
}

/* the five cubes, one instanced draw */
static void draw_cubes(struct gfx *x)
{
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11Buffer *vbs[2] = { x->vb_cube, x->vb_inst };
    UINT strides[2] = { sizeof(struct vtx_pnt), sizeof(struct inst) }, offs[2] = { 0, 0 };
    int i;
    if (SUCCEEDED(ID3D11DeviceContext_Map(x->ctx, (ID3D11Resource *)x->vb_inst, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        struct inst *in = (struct inst *)m.pData;
        for (i = 0; i < NUM_CUBES; i++) {
            struct mat4 w;
            game_cube_world(&G, i, &w);
            memcpy(in[i].w, w.m, sizeof(in[i].w));
            in[i].col[0] = 0.5f + 0.5f * (i & 1); in[i].col[1] = 0.5f + 0.25f * (i & 2); in[i].col[2] = 1.0f - 0.2f * i;
            in[i].col[3] = 1.0f;
        }
        ID3D11DeviceContext_Unmap(x->ctx, (ID3D11Resource *)x->vb_inst, 0);
    }
    ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_cube);
    ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 2, vbs, strides, offs);
    ID3D11DeviceContext_IASetIndexBuffer(x->ctx, x->ib_cube, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_cube, NULL, 0);
    ID3D11DeviceContext_GSSetShader(x->ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_cube, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 1, 1, &x->srv_arr);
    ID3D11DeviceContext_DrawIndexedInstanced(x->ctx, 36, NUM_CUBES, 0, 0, 0);
}

/* a lit box (the markers and their occlusion test boxes) */
static void draw_box(struct gfx *x, float px, float py, float pz, float s, float r, float g, float b)
{
    struct mat4 sc, tr, w;
    UINT stride = sizeof(struct vtx_pnt), off = 0;
    m_scale(&sc, s);
    m_translate(&tr, px, py, pz);
    m_mul(&w, &sc, &tr);
    set_object(x, &w, r, g, b, 1);
    ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_lit);
    ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->vb_cube, &stride, &off);
    ID3D11DeviceContext_IASetIndexBuffer(x->ctx, x->ib_cube, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_lit, NULL, 0);
    ID3D11DeviceContext_GSSetShader(x->ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_col, NULL, 0);
    ID3D11DeviceContext_DrawIndexed(x->ctx, 36, 0, 0);
}

/* the scene into the HDR target (or, for the monitor, into its texture) */
static void draw_scene(struct gfx *x, int monitor)
{
    struct mat4 world;
    UINT stride, off = 0, so_off = 0;
    ID3D11Buffer *nullb = NULL;
    ID3D11ShaderResourceView *nulls = NULL;
    D3D11_MAPPED_SUBRESOURCE m;
    int i, n;

    ID3D11DeviceContext_RSSetState(x->ctx, G.wireframe && !monitor ? x->rs_wire : x->rs_solid);
    if (monitor) { draw_cubes(x); return; }

    /* ground */
    m_identity(&world);
    set_object(x, &world, 1, 1, 1, 1);
    stride = sizeof(struct vtx_pnt);
    ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_lit);
    ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->vb_ground, &stride, &off);
    ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_lit, NULL, 0);
    ID3D11DeviceContext_GSSetShader(x->ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_tex, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &x->srv_checker);
    ID3D11DeviceContext_Draw(x->ctx, 4, 0);

    draw_cubes(x);

    if (x->so_ok) {
        /* the wave grid: SV_VertexID -> stream output, nothing rasterized
         * (no pixel shader, no depth); then drawn indexed from what was captured */
        ID3D11DeviceContext_IASetInputLayout(x->ctx, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &nullb, &off, &off);
        ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_gridgen, NULL, 0);
        ID3D11DeviceContext_GSSetShader(x->ctx, x->gs_so, NULL, 0);
        ID3D11DeviceContext_PSSetShader(x->ctx, NULL, NULL, 0);
        ID3D11DeviceContext_OMSetDepthStencilState(x->ctx, x->ds_off, 0);
        ID3D11DeviceContext_SOSetTargets(x->ctx, 1, &x->so_grid, &so_off);
        ID3D11DeviceContext_Draw(x->ctx, GRID_VERTS, 0);
        ID3D11DeviceContext_SOSetTargets(x->ctx, 1, &nullb, &so_off);
        ID3D11DeviceContext_OMSetDepthStencilState(x->ctx, NULL, 0);

        stride = sizeof(struct vtx_gv);
        ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_gv);
        ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->so_grid, &stride, &off);
        ID3D11DeviceContext_IASetIndexBuffer(x->ctx, x->ib_grid, DXGI_FORMAT_R16_UINT, 0);
        ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_grid, NULL, 0);
        ID3D11DeviceContext_GSSetShader(x->ctx, NULL, NULL, 0);
        ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_tex, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &x->srv_grad);
        ID3D11DeviceContext_DrawIndexed(x->ctx, GRID_INDICES, 0, 0);
    }

    /* the monitor showing the render target, its mips filled by GenerateMips */
    {
        struct vtx_pnt q[4] = {
            { -3.0f, 2.5f, 9.0f, 0, 0, -1, 0, 0 }, { 3.0f, 2.5f, 9.0f, 0, 0, -1, 1, 0 },
            { -3.0f, -0.5f, 9.0f, 0, 0, -1, 0, 1 }, { 3.0f, -0.5f, 9.0f, 0, 0, -1, 1, 1 } };
        /* the ground's strip buffer is immutable: the quad goes through the dynamic bars buffer */
        if (SUCCEEDED(ID3D11DeviceContext_Map(x->ctx, (ID3D11Resource *)x->vb_bars, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, q, sizeof(q));
            ID3D11DeviceContext_Unmap(x->ctx, (ID3D11Resource *)x->vb_bars, 0);
        }
        m_identity(&world);
        set_object(x, &world, 1.2f, 1.2f, 1.2f, 1);
        stride = sizeof(struct vtx_pnt);
        ID3D11DeviceContext_RSSetState(x->ctx, x->rs_none);
        ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_lit);
        ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->vb_bars, &stride, &off);
        ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_lit, NULL, 0);
        ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_tex, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &x->srv_rtt);
        ID3D11DeviceContext_Draw(x->ctx, 4, 0);
        ID3D11DeviceContext_RSSetState(x->ctx, G.wireframe ? x->rs_wire : x->rs_solid);
    }

    /* the markers: each box's visibility counted, then its marker drawn only
     * if the count is not zero (green: a box in plain view; red: a box under
     * the ground, which never shows) */
    {
        static const float box[MARKERS][3] = { { -1.2f, 3.4f, 3.0f }, { 1.2f, -3.0f, 3.0f } };
        static const float mark[MARKERS][3] = { { -0.4f, 3.4f, 3.0f }, { 0.4f, 3.4f, 3.0f } };
        for (i = 0; i < MARKERS; i++) {
            if (x->pred_ok) ID3D11DeviceContext_Begin(x->ctx, (ID3D11Asynchronous *)x->pred[i]);
            ID3D11DeviceContext_Begin(x->ctx, (ID3D11Asynchronous *)x->occ[i]);
            draw_box(x, box[i][0], box[i][1], box[i][2], 0.3f, 0.6f, 0.6f, 0.6f);
            ID3D11DeviceContext_End(x->ctx, (ID3D11Asynchronous *)x->occ[i]);
            if (x->pred_ok) ID3D11DeviceContext_End(x->ctx, (ID3D11Asynchronous *)x->pred[i]);
        }
        for (i = 0; i < MARKERS; i++) {
            if (x->pred_ok) ID3D11DeviceContext_SetPredication(x->ctx, x->pred[i], FALSE);
            draw_box(x, mark[i][0], mark[i][1], mark[i][2], 0.35f, i ? 2.0f : 0.1f, i ? 0.1f : 2.0f, 0.1f);
        }
        ID3D11DeviceContext_SetPredication(x->ctx, NULL, FALSE);
    }

    /* particles: points, the geometry shader makes the quads; additive, no depth writes */
    n = 0;
    if (SUCCEEDED(ID3D11DeviceContext_Map(x->ctx, (ID3D11Resource *)x->vb_part, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        struct vtx_part *p = (struct vtx_part *)m.pData;
        for (i = 0; i < NUM_PARTICLES; i++, n++) {
            float pos[3], size;
            DWORD c;
            game_particle(&G, i, pos, &c, &size);
            p[i].x = pos[0]; p[i].y = pos[1]; p[i].z = pos[2]; p[i].color = c; p[i].size = size;
        }
        ID3D11DeviceContext_Unmap(x->ctx, (ID3D11Resource *)x->vb_part, 0);
    }
    stride = sizeof(struct vtx_part);
    ID3D11DeviceContext_RSSetState(x->ctx, x->rs_none);
    ID3D11DeviceContext_OMSetBlendState(x->ctx, x->bs_add, NULL, 0xffffffff);
    ID3D11DeviceContext_OMSetDepthStencilState(x->ctx, x->ds_nowrite, 0);
    ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_part);
    ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->vb_part, &stride, &off);
    ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_part, NULL, 0);
    ID3D11DeviceContext_GSSetShader(x->ctx, x->gs_bill, NULL, 0);
    ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_part, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &x->srv_disc);
    ID3D11DeviceContext_Draw(x->ctx, n, 0);
    if (x->so_ok) {
        /* sparkles on the grid's vertices: DrawAuto, as many as the stream output wrote */
        stride = sizeof(struct vtx_gv);
        ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_gv);
        ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->so_grid, &stride, &off);
        ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_dot, NULL, 0);
        ID3D11DeviceContext_DrawAuto(x->ctx);
    }
    ID3D11DeviceContext_GSSetShader(x->ctx, NULL, NULL, 0);
    ID3D11DeviceContext_OMSetBlendState(x->ctx, NULL, NULL, 0xffffffff);
    ID3D11DeviceContext_OMSetDepthStencilState(x->ctx, NULL, 0);
    ID3D11DeviceContext_RSSetState(x->ctx, x->rs_solid);
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &nulls);
}

/* one whole frame onto `out` (the swap chain's buffer or the offscreen one) */
static void render(struct gfx *x, ID3D11RenderTargetView *out, int w, int h)
{
    static const float rtt_clear[4] = { 0.25f, 0.125f, 0.0625f, 1 }, sky[4] = { 0.0625f, 0.125f, 0.19f, 1 };   /* d3dgame9's 0x402010, 0x102030 */
    static const float reye[3] = { 0.0f, 4.0f, -6.0f }, rat[3] = { 0.0f, 0.5f, 3.0f };
    struct mat4 view, proj, rview, rproj;
    float eye[3], at[3];
    ID3D11ShaderResourceView *nulls = NULL;
    UINT stride = sizeof(struct vtx_rhw), off = 0;
    D3D11_MAPPED_SUBRESOURCE m;
    struct vtx_rhw bars[BARS * 6];
    int n;

    game_camera(&G, eye, at);
    m_lookat(&view, eye, at);
    m_perspective(&proj, 1.1f, w / (float)h, 0.5f, 60.0f);
    m_lookat(&rview, reye, rat);
    m_perspective(&rproj, 0.9f, 1.0f, 0.5f, 60.0f);
    bind_common(x);

    /* 1. the monitor's picture: the cubes from a fixed camera, then its mips */
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &nulls);
    ID3D11DeviceContext_OMSetRenderTargets(x->ctx, 1, &x->rtv_rtt, x->dsv_rtt);
    ID3D11DeviceContext_ClearRenderTargetView(x->ctx, x->rtv_rtt, rtt_clear);
    ID3D11DeviceContext_ClearDepthStencilView(x->ctx, x->dsv_rtt, D3D11_CLEAR_DEPTH, 1.0f, 0);
    viewport(x, RTT_SIZE, RTT_SIZE);
    set_frame(x, &rview, &rproj, RTT_SIZE, RTT_SIZE);
    draw_scene(x, 1);
    ID3D11DeviceContext_OMSetRenderTargets(x->ctx, 0, NULL, NULL);
    ID3D11DeviceContext_GenerateMips(x->ctx, x->srv_rtt);

    /* 2. the scene into the HDR target */
    ID3D11DeviceContext_OMSetRenderTargets(x->ctx, 1, &x->rtv_hdr, x->dsv_main);
    ID3D11DeviceContext_ClearRenderTargetView(x->ctx, x->rtv_hdr, sky);
    ID3D11DeviceContext_ClearDepthStencilView(x->ctx, x->dsv_main, D3D11_CLEAR_DEPTH, 1.0f, 0);
    viewport(x, w, h);
    set_frame(x, &view, &proj, w, h);
    draw_scene(x, 0);

    /* 3. tonemapped onto the output by one triangle and no vertex buffer */
    ID3D11DeviceContext_OMSetRenderTargets(x->ctx, 1, &out, NULL);
    ID3D11DeviceContext_IASetInputLayout(x->ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(x->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_full, NULL, 0);
    ID3D11DeviceContext_GSSetShader(x->ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_tone, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &x->srv_hdr);
    ID3D11DeviceContext_Draw(x->ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(x->ctx, 0, 1, &nulls);

    /* 4. the frame-time bars, in pixels, over the tonemapped frame */
    n = game_bars(&G, bars, w, h);
    if (SUCCEEDED(ID3D11DeviceContext_Map(x->ctx, (ID3D11Resource *)x->vb_bars, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, bars, n * sizeof(bars[0]));
        ID3D11DeviceContext_Unmap(x->ctx, (ID3D11Resource *)x->vb_bars, 0);
    }
    ID3D11DeviceContext_IASetInputLayout(x->ctx, x->il_bar);
    ID3D11DeviceContext_IASetVertexBuffers(x->ctx, 0, 1, &x->vb_bars, &stride, &off);
    ID3D11DeviceContext_VSSetShader(x->ctx, x->vs_bar, NULL, 0);
    ID3D11DeviceContext_PSSetShader(x->ctx, x->ps_col, NULL, 0);
    ID3D11DeviceContext_RSSetState(x->ctx, x->rs_none);
    ID3D11DeviceContext_Draw(x->ctx, n, 0);
    ID3D11DeviceContext_RSSetState(x->ctx, x->rs_solid);
}

/* the occlusion queries' answers (waited for: they decide the markers) */
static void read_queries(struct gfx *x)
{
    int i, k;
    for (i = 0; i < MARKERS; i++) {
        x->occ_samples[i] = ~0ull;
        for (k = 0; k < 2000; k++) {
            UINT64 v;
            if (ID3D11DeviceContext_GetData(x->ctx, (ID3D11Asynchronous *)x->occ[i], &v, sizeof(v), 0) == S_OK) {
                x->occ_samples[i] = v;
                break;
            }
            Sleep(1);
        }
    }
}

/* ---------------------------------------------------------------- devices */

static IDXGIAdapter *pick_adapter(void)
{
    IDXGIFactory *f;
    IDXGIAdapter *a, *pick = NULL;
    UINT i;
    if (FAILED(CreateDXGIFactory(&IID_IDXGIFactory, (void **)&f))) return NULL;
    for (i = 0; IDXGIFactory_EnumAdapters(f, i, &a) == S_OK; i++) {
        DXGI_ADAPTER_DESC d;
        char name[128];
        IDXGIAdapter_GetDesc(a, &d);
        WideCharToMultiByte(CP_ACP, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
        game_log("d3dgame11: adapter %u \"%s\" (%04x:%04x)", i, name, d.VendorId, d.DeviceId);
        if (!pick && strstr(name, "2ksbox")) {
            pick = a;
            continue;
        }
        IDXGIAdapter_Release(a);
    }
    IDXGIFactory_Release(f);
    return pick;
}

static int open_device(struct gfx *x, const char *name, int warp)
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    IDXGIAdapter *a = warp ? NULL : pick_adapter();
    HRESULT hr;
    x->name = name;
    hr = D3D11CreateDevice(a, warp ? D3D_DRIVER_TYPE_WARP : a ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           levels, 3, D3D11_SDK_VERSION, &x->dev, &x->level, &x->ctx);
    if (a) IDXGIAdapter_Release(a);
    game_log("d3dgame11 [%s]: D3D11CreateDevice %s, feature level %d_%d", name, hr_str(hr), x->level >> 12, (x->level >> 8) & 15);
    return SUCCEEDED(hr);
}

static int setup(struct gfx *x)
{
    return make_shaders(x) && make_resources(x) && make_targets(x, G.o.w, G.o.h);
}

/* the offscreen frame read back as B, G, R, A */
static DWORD *read_frame(struct gfx *x, int w, int h)
{
    D3D11_MAPPED_SUBRESOURCE m;
    DWORD *px;
    int y;
    ID3D11DeviceContext_CopyResource(x->ctx, (ID3D11Resource *)x->tex_stage, (ID3D11Resource *)x->tex_out);
    if (FAILED(ID3D11DeviceContext_Map(x->ctx, (ID3D11Resource *)x->tex_stage, 0, D3D11_MAP_READ, 0, &m))) return NULL;
    px = (DWORD *)malloc((size_t)w * h * 4);
    for (y = 0; y < h; y++) memcpy(px + (size_t)y * w, (BYTE *)m.pData + (size_t)y * m.RowPitch, (size_t)w * 4);
    ID3D11DeviceContext_Unmap(x->ctx, (ID3D11Resource *)x->tex_stage, 0);
    return px;
}

/* -compare: this frame on our adapter and on WARP, offscreen, pixel by pixel */
static int compare(void)
{
    int w = G.o.w, h = G.o.h, i, bad = 0, big = 0, rows[8] = { 0 };
    DWORD *a, *b;
    char path[MAX_PATH];
    if (!open_device(&W, "WARP", 1) || !setup(&W)) return 0;
    render(&X, X.rtv_out, w, h);
    render(&W, W.rtv_out, w, h);
    read_queries(&X);
    read_queries(&W);
    a = read_frame(&X, w, h);
    b = read_frame(&W, w, h);
    if (!a || !b) { game_log("d3dgame11: compare: a frame was not read back"); return 0; }
    for (i = 0; i < w * h; i++) {
        int c, d = 0;
        for (c = 0; c < 24; c += 8) {
            int e = abs((int)((a[i] >> c) & 0xff) - (int)((b[i] >> c) & 0xff));
            if (e > d) d = e;
        }
        if (d > big) big = d;
        if (d > 8) { bad++; rows[(i / w) * 8 / h]++; }
    }
    game_out_path(path, sizeof(path), "d3d11-ours.bmp");
    bmp_write(path, a, w * 4, w, h, 0);
    game_out_path(path, sizeof(path), "d3d11-warp.bmp");
    bmp_write(path, b, w * 4, w, h, 0);
    game_log("d3dgame11: occlusion samples: visible box ours %llu WARP %llu, hidden box ours %llu WARP %llu",
             X.occ_samples[0], W.occ_samples[0], X.occ_samples[1], W.occ_samples[1]);
    game_log("d3dgame11: frame %u: %d of %d pixels differ by more than 8 (largest %d); by eighths of the height %d %d %d %d %d %d %d %d",
             G.frame, bad, w * h, big, rows[0], rows[1], rows[2], rows[3], rows[4], rows[5], rows[6], rows[7]);
    game_log("d3dgame11: pixel (%d,%d) ours 0x%08lx WARP 0x%08lx", w / 2, h / 2, (unsigned long)a[(h / 2) * w + w / 2],
             (unsigned long)b[(h / 2) * w + w / 2]);
    free(a);
    free(b);
    /* PASS: the frames agree (under 0.5% of the pixels off by more than 8:
     * two rasterisers and filtering units round apart) and the predicates
     * agree with the counts (the hidden box counted nothing) */
    return bad < w * h / 200 && X.occ_samples[0] && X.occ_samples[0] != ~0ull && X.occ_samples[1] == 0;
}

static void dump_frame(void)
{
    ID3D11Texture2D *bb = NULL;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&bb))) return;
    ID3D11DeviceContext_CopyResource(X.ctx, (ID3D11Resource *)X.tex_stage, (ID3D11Resource *)bb);
    if (SUCCEEDED(ID3D11DeviceContext_Map(X.ctx, (ID3D11Resource *)X.tex_stage, 0, D3D11_MAP_READ, 0, &m))) {
        int ok = bmp_write(G.o.dump_file, m.pData, m.RowPitch, G.o.w, G.o.h, 0);
        ID3D11DeviceContext_Unmap(X.ctx, (ID3D11Resource *)X.tex_stage, 0);
        game_log("d3dgame11: frame %u -> %s (%s)", G.frame, G.o.dump_file, ok ? "written" : "write failed");
    }
    ID3D11Texture2D_Release(bb);
}

int main(int argc, char **argv)
{
    HWND hwnd;
    HMODULE dc;
    DXGI_SWAP_CHAIN_DESC sd;
    IDXGIDevice *xd = NULL;
    IDXGIAdapter *xa = NULL;
    IDXGIFactory *xf = NULL;
    ID3D11Texture2D *bb = NULL;
    HRESULT hr;
    int i, result = 0;

    game_init(&G, argc, argv);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-warp")) use_warp = 1;
        else if (!strcmp(argv[i], "-flip")) use_flip = 1;
        else if (!strcmp(argv[i], "-compare") && i + 1 < argc) compare_frame = atoi(argv[++i]);
    }
    if (compare_frame >= 0 && !G.o.frames) G.o.frames = compare_frame + 1;   /* a fixed step: both frames the same */
    game_log_open(G.o.log_file[0] ? G.o.log_file : "D3DGAME11.LOG", argc, argv);
    dc = LoadLibraryA("d3dcompiler_47.dll");
    compile = dc ? (pfn_compile)GetProcAddress(dc, "D3DCompile") : NULL;
    if (!compile) { game_log("d3dgame11: no d3dcompiler_47.dll"); return 1; }
    hwnd = game_window(&G, "d3dgame11");
    if (!open_device(&X, use_warp ? "WARP" : "ours", use_warp)) return 1;
    if (X.level < D3D_FEATURE_LEVEL_10_0) { game_log("d3dgame11: feature level 10_0 or above wanted"); return 1; }
    if (!setup(&X)) return 1;

    memset(&sd, 0, sizeof(sd));
    sd.BufferDesc.Width = G.o.w;
    sd.BufferDesc.Height = G.o.h;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = use_flip ? 2 : 1;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = use_flip ? (DXGI_SWAP_EFFECT)3 /* FLIP_SEQUENTIAL */ : DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device_QueryInterface(X.dev, &IID_IDXGIDevice, (void **)&xd);
    if (xd) IDXGIDevice_GetAdapter(xd, &xa);
    if (xa) IDXGIAdapter_GetParent(xa, &IID_IDXGIFactory, (void **)&xf);
    hr = xf ? IDXGIFactory_CreateSwapChain(xf, (IUnknown *)X.dev, &sd, &swap) : E_FAIL;
    game_log("d3dgame11: swap chain %dx%d %s: %s", G.o.w, G.o.h, use_flip ? "FLIP_SEQUENTIAL" : "DISCARD", hr_str(hr));
    if (xf) IDXGIFactory_Release(xf);
    if (xa) IDXGIAdapter_Release(xa);
    if (xd) IDXGIDevice_Release(xd);
    if (FAILED(hr)) return 1;
    game_log("d3dgame11: stream output %s, occlusion predicates %s", X.so_ok ? "on" : "off", X.pred_ok ? "on" : "off");

    while (!G.quit && game_pump()) {
        float dt = game_step(&G);
        if (!rtv_back) {
            if (FAILED(IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&bb))) break;
            hr = ID3D11Device_CreateRenderTargetView(X.dev, (ID3D11Resource *)bb, NULL, &rtv_back);
            ID3D11Texture2D_Release(bb);
            if (FAILED(hr)) { game_log("d3dgame11: the back buffer's view %s", hr_str(hr)); break; }
        }
        if (compare_frame >= 0 && (int)G.frame == compare_frame) {
            result = compare();
            game_log("d3dgame11: %s", result ? "PASS" : "FAIL");
            break;
        }
        render(&X, rtv_back, G.o.w, G.o.h);
        if (G.o.dump_frame >= 0 && (int)G.frame == G.o.dump_frame) dump_frame();
        hr = IDXGISwapChain_Present(swap, G.o.novsync ? 0 : 1, 0);
        if (FAILED(hr)) { game_log("d3dgame11: Present %s", hr_str(hr)); break; }
        if (use_flip && rtv_back) { ID3D11RenderTargetView_Release(rtv_back); rtv_back = NULL; }
        if (dt < 0) {
            char title[96];
            snprintf(title, sizeof(title), "d3dgame11: %.1f fps, frame %u", G.fps, G.frame);
            SetWindowTextA(hwnd, title);
            game_log("%s", title);
        }
        if (G.o.frames && (int)G.frame >= G.o.frames) break;
    }
    game_log("d3dgame11: %u frames, %lu ms", G.frame, (unsigned long)(GetTickCount() - G.start_ms));
    game_log("d3dgame11: exit");
    return compare_frame >= 0 ? !result : 0;
}
