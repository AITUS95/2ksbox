/*
 * d3dpt-gdi-test: the executor's GDI records (protocol v27's copies and
 * fills, v29's other RenderKm operations) without a guest, against this
 * PC's own GDI (track M25).
 *
 * Windows 11 draws a window with the CPU and sends RenderKm next to
 * nothing (track M25, "GDI's other operations"), so a guest run cannot
 * reach v29's ROPs, StretchBlt, AlphaBlend, TransparentBlt and ClearType.
 * This feeds them to the executor as the kernel driver would, into a
 * malloc'ed VRAM: each case into a plain VRAM surface (the host's CPU
 * path over VRAM) and into a redirection bitmap's object (TEXTURE: read
 * back, computed, written to the object and its VRAM), the object then
 * copied out to check it holds what its VRAM does. The answer is the same
 * operation by GDI on DIB sections (BitBlt, PatBlt, StretchBlt,
 * AlphaBlend, TransparentBlt with the sub-rectangles as the clip), and
 * for what GDI has no call for (a mirrored stretch, the mono stretch
 * modes, ClearType's blend) the DDI's own formula, written out here.
 *
 * Build (scripts/build-windows.sh exec):
 *        g++ -std=c++17 -O2 -static -o build/win/d3dpt-gdi-test.exe tools/d3dpt-gdi-test.cpp -lmsimg32 -lgdi32
 * Run:   D3DPT_EXEC_LIB=build/win/d3dpt/d3dpt_exec.dll D3DPT_DXVK_LIB=build/win/d3dpt/dxvk_d3d9.dll build/win/d3dpt-gdi-test.exe
 */
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../d3dpt/d3dpt_enc.h"
#include "../d3dpt/exec/d3dpt_exec.h"

static d3dpt_exec_t *X;
static uint32_t (*p_submit)(d3dpt_exec_t *, void *, uint32_t);
static uint8_t *vram;
static int cases, failures;

static void cb_log(void *, const char *m) { printf("exec: %s\n", m); }
static void cb_active(void *, int) {}
static void doorbell(d3dpt_enc *e) { p_submit(X, e->shm, D3DPT_SHM_SIZE); }

enum { W = 160, H = 120, SW = 48, SH = 40, PITCH = W * 4, SPITCH = SW * 4 };
enum : uint32_t {
    DST_OFF = 0x000000, SRC_OFF = 0x100000, OBJ_OFF = 0x200000, OUT_OFF = 0x300000, ALPHA_OFF = 0x400000,
    LUT_OFF = 0x500000, BG_OFF = 0x600000, VRAM_SIZE = 0x800000
};
enum { H_OBJ = 100 };
#define FMT_A8R8G8B8 21u
#define FMT_P8 41u

static uint32_t *at(uint32_t off, int pitch, int x, int y) { return (uint32_t *)(vram + off + (size_t)y * pitch + (size_t)x * 4); }

static uint32_t bg_px(int x, int y) { return 0xff000000u | ((uint32_t)(x * 255 / W) << 16) | ((uint32_t)(y * 255 / H) << 8) | (((x / 6 + y / 6) & 1) ? 200u : 70u); }
/* the source: bands, a diagonal, a magenta square (the key), alpha falling left to right, premultiplied */
static uint32_t src_px(int x, int y)
{
    uint32_t a = 255 - (uint32_t)x * 255 / (SW - 1), r = (uint32_t)(x * 5) & 255, g = (uint32_t)(y * 6) & 255, b = ((x + y) & 4) ? 230 : 30;
    if (x >= 12 && x < 24 && y >= 10 && y < 22) r = 255, g = 0, b = 255, a = 255;
    if (x == y) r = g = b = 255;
    return (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
}
/* ClearType's coverage: three ramps, zero and full spans included */
static uint32_t alpha_px(int x, int y)
{
    uint32_t r = (uint32_t)((x * 7 + y) % 300), g = (uint32_t)((x * 5 + y * 3) % 300), b = (uint32_t)((x * 3 + y * 5) % 300);
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    if ((x / 10) % 4 == 0) r = g = b = 0;
    return (r << 16) | (g << 8) | b;
}

static d3dpt_gdi_side side(uint32_t handle, uint32_t flags, uint32_t off, uint32_t pitch, uint32_t w, uint32_t h, uint32_t fmt = FMT_A8R8G8B8)
{
    d3dpt_gdi_side s = {};
    s.handle = handle; s.flags = flags; s.offset = off; s.pitch = pitch; s.width = w; s.height = h; s.format = fmt;
    return s;
}

static void gdi_copy(d3dpt_enc *e, const d3dpt_gdi_side &src, const d3dpt_gdi_side &dst, int w, int h)
{
    d3dpt_gdi *g = (d3dpt_gdi *)d3dpt_enc_cmd(e, D3DPT_OP_GDI, sizeof(d3dpt_gdi), sizeof(d3dpt_gdi_rect));
    memset(g, 0, sizeof(*g));
    g->op = D3DPT_GDI_COPY;
    g->n = 1;
    g->src = src;
    g->dst = dst;
    d3dpt_gdi_rect r = { 0, 0, 0, 0, w, h };
    memcpy(g + 1, &r, sizeof r);
}

/* a case: the op, its parameters and the sub-rectangles */
struct Case {
    const char *name;
    d3dpt_gdi2 g;
    std::vector<RECT> sub;
    int tol;          /* per channel */
    int mode;         /* the answer: 0 GDI, 1 the formula */
    DWORD gdirop;     /* ROP: GDI's code */
};

/* --- the answer --- */
struct Dib {
    HDC dc = nullptr; HBITMAP bm = nullptr; uint32_t *px = nullptr; int w = 0, h = 0;
    bool make(int ww, int hh)
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = ww;
        bi.bmiHeader.biHeight = -hh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr);
        bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, nullptr, 0);
        if (!dc || !bm) return false;
        SelectObject(dc, bm);
        w = ww; h = hh;
        return true;
    }
};

static int map_(int xd, int dl, int dr, int sl, int sr)
{
    double v = (xd - dl + 0.5) * (double)(sr - sl) / (double)(dr - dl) + sl;
    int i = (int)v;
    return i < sl ? sl : i >= sr ? sr - 1 : i;
}
static int ch(uint32_t v, int k) { return (int)((v >> (8 * k)) & 255); }

/* the DDI's formulas, for what GDI has no call for */
static void formula(const Case &c, std::vector<uint32_t> &d, const std::vector<uint32_t> &s, const std::vector<uint32_t> &a,
                    const uint8_t *lut)
{
    const d3dpt_gdi2 &g = c.g;
    for (const RECT &r : c.sub)
        for (int y = r.top; y < r.bottom; y++)
            for (int x = r.left; x < r.right; x++) {
                uint32_t &o = d[(size_t)y * W + x], dv = o;
                if (g.op == D3DPT_GDI_STRETCH) {
                    int xs = map_(x, g.dl, g.dr, g.sl, g.sr), ys = map_(y, g.dt, g.db, g.st, g.sb);
                    if (g.flags & 1) xs = g.sr - 1 - (xs - g.sl);
                    if (g.flags & 2) ys = g.sb - 1 - (ys - g.st);
                    uint32_t v = s[(size_t)ys * SW + xs];
                    if ((g.rop == 1 || g.rop == 2) && !(g.flags & 3)) {
                        int xs1 = g.sl + (int)((long long)(x + 1 - g.dl) * (g.sr - g.sl) / (g.dr - g.dl));
                        int ys1 = g.st + (int)((long long)(y + 1 - g.dt) * (g.sb - g.st) / (g.db - g.dt));
                        if (xs1 <= xs) xs1 = xs + 1;
                        if (ys1 <= ys) ys1 = ys + 1;
                        for (int yy = ys; yy < ys1 && yy < g.sb; yy++)
                            for (int xx = xs; xx < xs1 && xx < g.sr; xx++)
                                v = g.rop == 1 ? (v & s[(size_t)yy * SW + xx]) : (v | s[(size_t)yy * SW + xx]);
                    }
                    o = v;
                } else if (g.op == D3DPT_GDI_CLEARTYPE) {
                    uint32_t av = a[(size_t)(y + g.ay) * W + (x + g.ax)], out = dv & 0xff000000u;
                    for (int k = 0; k < 3; k++) {
                        int D = ch(dv, k), C = ch(g.color, k), A = ch(av, k), v;
                        if (lut) {
                            const uint8_t *gt = lut + g.gamma * 512, *it = gt + 256;
                            int t = gt[D];
                            v = A == 0 ? D : A == 255 ? ch(g.color2, k) : it[(int)std::lround(t + (C - t) * A / 255.0)];
                        } else {
                            int f = C >= D ? ch(av, 2) : ch(av, 1);
                            v = (int)std::lround(D + (C - D) * f / 255.0);
                        }
                        out |= (uint32_t)v << (8 * k);
                    }
                    o = out;
                }
            }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *lib = getenv("D3DPT_EXEC_LIB");
    if (!lib) lib = "build/win/d3dpt/d3dpt_exec.dll";
    HMODULE h = LoadLibraryA(lib);
    if (!h) { fprintf(stderr, "LoadLibrary %s failed\n", lib); return 1; }
    auto p_version = (uint32_t (*)(void))GetProcAddress(h, "d3dpt_exec_version");
    auto p_create = (d3dpt_exec_t *(*)(const d3dpt_exec_ops *))GetProcAddress(h, "d3dpt_exec_create");
    auto p_attach = (void (*)(d3dpt_exec_t *, int))GetProcAddress(h, "d3dpt_exec_attach");
    auto p_destroy = (void (*)(d3dpt_exec_t *))GetProcAddress(h, "d3dpt_exec_destroy");
    auto p_set_vram = (void (*)(d3dpt_exec_t *, void *, uint32_t))GetProcAddress(h, "d3dpt_exec_set_vram");
    auto p_features = (uint32_t (*)(d3dpt_exec_t *))GetProcAddress(h, "d3dpt_exec_features");
    p_submit = (uint32_t (*)(d3dpt_exec_t *, void *, uint32_t))GetProcAddress(h, "d3dpt_exec_submit");
    if (!p_version || !p_create || !p_attach || !p_destroy || !p_submit || !p_set_vram || !p_features) {
        fprintf(stderr, "bad executor library\n");
        return 1;
    }
    if (p_version() != D3DPT_PROTO_VERSION) { fprintf(stderr, "protocol mismatch (%u, header %u)\n", p_version(), D3DPT_PROTO_VERSION); return 1; }
    d3dpt_exec_ops ops = { nullptr, cb_log, cb_active, nullptr };
    X = p_create(&ops);
    if (!X) { printf("no executor (no DXVK / Vulkan device)\n"); return 77; }
    p_attach(X, 1);
    vram = (uint8_t *)calloc(1, VRAM_SIZE);
    p_set_vram(X, vram, VRAM_SIZE);
    if (!(p_features(X) & D3DPT_EXEC_FEAT_D3D11)) { printf("no Direct3D 11 on this host: skipped\n"); return 77; }
    uint8_t *shm = (uint8_t *)calloc(1, D3DPT_SHM_SIZE);
    d3dpt_enc enc;
    d3dpt_enc_init(&enc, shm, doorbell);

    std::vector<uint32_t> bg((size_t)W * H), src((size_t)SW * SH), alpha((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) bg[(size_t)y * W + x] = bg_px(x, y), alpha[(size_t)y * W + x] = alpha_px(x, y);
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) src[(size_t)y * SW + x] = src_px(x, y);
    for (int y = 0; y < H; y++) memcpy(at(BG_OFF, PITCH, 0, y), &bg[(size_t)y * W], PITCH);
    for (int y = 0; y < SH; y++) memcpy(at(SRC_OFF, SPITCH, 0, y), &src[(size_t)y * SW], SPITCH);
    for (int y = 0; y < H; y++) memcpy(at(ALPHA_OFF, PITCH, 0, y), &alpha[(size_t)y * W], PITCH);
    /* the gamma table: 16 rows of 512, a gamma of 1.0 + row / 8 and its inverse */
    uint8_t lut[16 * 512];
    for (int r = 0; r < 16; r++)
        for (int i = 0; i < 256; i++) {
            double gm = 1.0 + r / 8.0;
            lut[r * 512 + i] = (uint8_t)std::lround(255.0 * std::pow(i / 255.0, gm));
            lut[r * 512 + 256 + i] = (uint8_t)std::lround(255.0 * std::pow(i / 255.0, 1.0 / gm));
        }
    memcpy(vram + LUT_OFF, lut, sizeof lut);

    const d3dpt_gdi_side S = side(0, 0, SRC_OFF, SPITCH, SW, SH);
    auto mk = [](uint32_t op, uint32_t rop, uint32_t color, uint32_t flags, RECT sr, RECT dr) {
        d3dpt_gdi2 g = {};
        g.op = op; g.rop = rop; g.color = color; g.flags = flags;
        g.sl = sr.left; g.st = sr.top; g.sr = sr.right; g.sb = sr.bottom;
        g.dl = dr.left; g.dt = dr.top; g.dr = dr.right; g.db = dr.bottom;
        return g;
    };
    const RECT s0 = { 0, 0, SW, SH }, d0 = { 20, 15, 20 + SW, 15 + SH };
    /* sub-rectangles never overlap (a clip region's) */
    const std::vector<RECT> one = { d0 }, two = { { 20, 15, 50, 40 }, { 50, 15, 20 + SW, 15 + SH }, { 24, 40, 50, 15 + SH } };
    std::vector<Case> cs;
    struct { const char *n; uint32_t r3; DWORD rop; } rops[] = {
        { "rop SRCCOPY", 0xcc, SRCCOPY }, { "rop SRCINVERT", 0x66, SRCINVERT }, { "rop SRCAND", 0x88, SRCAND },
        { "rop SRCPAINT", 0xee, SRCPAINT }, { "rop NOTSRCCOPY", 0x33, NOTSRCCOPY }, { "rop SRCERASE", 0x44, SRCERASE },
        { "rop MERGEPAINT", 0xbb, MERGEPAINT }, { "rop NOTSRCERASE", 0x11, NOTSRCERASE }, { "rop DSTINVERT (blt)", 0x55, DSTINVERT },
    };
    for (auto &r : rops) cs.push_back({ r.n, mk(D3DPT_GDI_ROP, r.r3, 0, 0, s0, d0), two, 0, 0, r.rop });
    struct { const char *n; uint32_t r3; DWORD rop; } pats[] = {
        { "fill PATCOPY", 0xf0, PATCOPY }, { "fill PATINVERT", 0x5a, PATINVERT }, { "fill DSTINVERT", 0x55, DSTINVERT },
        { "fill PDXN", 0xa5, 0x00a50065 }, { "fill PATAND (DPa)", 0xa0, 0x00a000c9 }, { "fill PATOR (DPo)", 0xfa, 0x00fa0089 },
    };
    for (auto &p : pats) {
        Case c = { p.n, mk(D3DPT_GDI_ROP, p.r3, 0x00c84030u, 0, s0, d0), two, 0, 0, p.rop };
        cs.push_back(c);
    }
    cs.push_back({ "stretch grow", mk(D3DPT_GDI_STRETCH, 3, 0, 0, s0, { 10, 10, 10 + 100, 10 + 90 }), { { 10, 10, 110, 100 } }, 0, 0, 0 });
    cs.push_back({ "stretch shrink", mk(D3DPT_GDI_STRETCH, 3, 0, 0, s0, { 30, 30, 30 + 20, 30 + 17 }), { { 30, 30, 50, 47 } }, 0, 0, 0 });
    cs.push_back({ "stretch part", mk(D3DPT_GDI_STRETCH, 3, 0, 0, { 8, 4, 40, 36 }, { 5, 5, 5 + 70, 5 + 50 }), { { 5, 5, 60, 40 }, { 60, 5, 75, 55 }, { 5, 40, 60, 55 } }, 0, 0, 0 });
    cs.push_back({ "stretch mirror x", mk(D3DPT_GDI_STRETCH, 3, 0, 1, s0, d0), one, 0, 1, 0 });
    cs.push_back({ "stretch mirror xy", mk(D3DPT_GDI_STRETCH, 3, 0, 3, s0, { 10, 10, 10 + 70, 10 + 60 }), { { 10, 10, 80, 70 } }, 0, 1, 0 });
    cs.push_back({ "stretch AND shrink", mk(D3DPT_GDI_STRETCH, 1, 0, 0, s0, { 30, 30, 30 + 20, 30 + 13 }), { { 30, 30, 50, 43 } }, 0, 1, 0 });
    cs.push_back({ "stretch OR shrink", mk(D3DPT_GDI_STRETCH, 2, 0, 0, s0, { 30, 30, 30 + 17, 30 + 20 }), { { 30, 30, 47, 50 } }, 0, 1, 0 });
    cs.push_back({ "alpha constant", mk(D3DPT_GDI_ALPHA, 0, 0, 128, s0, d0), two, 1, 0, 0 });
    cs.push_back({ "alpha per-pixel", mk(D3DPT_GDI_ALPHA, 0, 0, 0x100 | 255, s0, d0), two, 1, 0, 0 });
    cs.push_back({ "alpha both", mk(D3DPT_GDI_ALPHA, 0, 0, 0x100 | 150, s0, d0), one, 1, 0, 0 });
    cs.push_back({ "alpha stretched", mk(D3DPT_GDI_ALPHA, 0, 0, 0x100 | 220, s0, { 10, 10, 10 + 90, 10 + 30 }), { { 10, 10, 100, 40 } }, 1, 0, 0 });
    cs.push_back({ "transparent", mk(D3DPT_GDI_TRANSPARENT, 0, 0x00ff00ffu, 0, s0, d0), two, 0, 0, 0 });
    cs.push_back({ "transparent stretched", mk(D3DPT_GDI_TRANSPARENT, 0, 0x00ff00ffu, 0, s0, { 10, 10, 10 + 96, 10 + 80 }), { { 10, 10, 106, 90 } }, 0, 0, 0 });
    for (int gm = 0; gm < 2; gm++) {
        Case c = { gm ? "cleartype gamma 5" : "cleartype no gamma", mk(D3DPT_GDI_CLEARTYPE, 0, 0x00203080u, 0, s0, { 0, 0, W, H }),
                   { { 10, 10, 150, 60 }, { 30, 70, 120, 110 } }, 1, 1, 0 };
        c.g.color2 = 0x00405090u;
        c.g.gamma = gm ? 5 : ~0u;
        c.g.ax = 3;
        c.g.ay = 2;
        cs.push_back(c);
    }

    Dib dd, ds;
    if (!dd.make(W, H) || !ds.make(SW, SH)) { fprintf(stderr, "no DIB sections\n"); return 1; }
    memcpy(ds.px, src.data(), src.size() * 4);

    for (const Case &c : cs) {
        const bool has_src = c.g.op != D3DPT_GDI_CLEARTYPE && (c.g.op != D3DPT_GDI_ROP || c.name[0] == 'r');
        /* the answer */
        std::vector<uint32_t> want = bg;
        if (c.mode == 0) {
            memcpy(dd.px, bg.data(), bg.size() * 4);
            HRGN rgn = CreateRectRgn(0, 0, 0, 0);
            for (const RECT &r : c.sub) {
                HRGN one_ = CreateRectRgnIndirect(&r);
                CombineRgn(rgn, rgn, one_, RGN_OR);
                DeleteObject(one_);
            }
            SelectClipRgn(dd.dc, rgn);
            const d3dpt_gdi2 &g = c.g;
            int dw = g.dr - g.dl, dh = g.db - g.dt, sw = g.sr - g.sl, sh = g.sb - g.st;
            if (g.op == D3DPT_GDI_ROP && has_src) {
                BitBlt(dd.dc, g.dl, g.dt, dw, dh, ds.dc, g.sl, g.st, c.gdirop);
            } else if (g.op == D3DPT_GDI_ROP) {
                HBRUSH br = CreateSolidBrush(RGB((g.color >> 16) & 255, (g.color >> 8) & 255, g.color & 255));
                HGDIOBJ old = SelectObject(dd.dc, br);
                PatBlt(dd.dc, g.dl, g.dt, dw, dh, c.gdirop);
                SelectObject(dd.dc, old);
                DeleteObject(br);
            } else if (g.op == D3DPT_GDI_STRETCH) {
                SetStretchBltMode(dd.dc, COLORONCOLOR);
                StretchBlt(dd.dc, g.dl, g.dt, dw, dh, ds.dc, g.sl, g.st, sw, sh, SRCCOPY);
            } else if (g.op == D3DPT_GDI_ALPHA) {
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(g.flags & 255), (BYTE)((g.flags & 0x100) ? AC_SRC_ALPHA : 0) };
                AlphaBlend(dd.dc, g.dl, g.dt, dw, dh, ds.dc, g.sl, g.st, sw, sh, bf);
            } else if (g.op == D3DPT_GDI_TRANSPARENT) {
                TransparentBlt(dd.dc, g.dl, g.dt, dw, dh, ds.dc, g.sl, g.st, sw, sh, RGB(255, 0, 255));
            }
            GdiFlush();
            SelectClipRgn(dd.dc, nullptr);
            DeleteObject(rgn);
            memcpy(want.data(), dd.px, want.size() * 4);
        } else {
            formula(c, want, src, alpha, c.g.gamma != ~0u ? lut : nullptr);
        }

        /* the executor: into plain VRAM, and into an object */
        for (int obj = 0; obj < 2; obj++) {
            d3dpt_gdi_side D = obj ? side(H_OBJ, D3DPT_GDI_TEXTURE, OBJ_OFF, PITCH, W, H) : side(0, 0, DST_OFF, PITCH, W, H);
            const d3dpt_gdi_side BG = side(0, 0, BG_OFF, PITCH, W, H);
            if (obj) gdi_copy(&enc, BG, D, W, H);      /* the object and its VRAM start as the background */
            else for (int y = 0; y < H; y++) memcpy(at(DST_OFF, PITCH, 0, y), &bg[(size_t)y * W], PITCH);
            d3dpt_gdi2 *g = (d3dpt_gdi2 *)d3dpt_enc_cmd(&enc, D3DPT_OP_GDI, sizeof(d3dpt_gdi2), (uint32_t)(c.sub.size() * sizeof(d3dpt_gdi_rect)));
            *g = c.g;
            g->n = (uint32_t)c.sub.size();
            g->dst = D;
            if (has_src) g->src = S;
            if (c.g.op == D3DPT_GDI_CLEARTYPE) {
                g->alpha = side(0, 0, ALPHA_OFF, PITCH, W, H);
                g->lut = side(0, 0, LUT_OFF, 512, 512, 16, FMT_P8);
            }
            d3dpt_gdi_rect *r = (d3dpt_gdi_rect *)(g + 1);
            for (size_t i = 0; i < c.sub.size(); i++) r[i] = { 0, 0, c.sub[i].left, c.sub[i].top, c.sub[i].right, c.sub[i].bottom };
            if (obj) gdi_copy(&enc, D, side(0, 0, OUT_OFF, PITCH, W, H), W, H);   /* the object, read back */
            d3dpt_enc_flush(&enc);

            for (int pass = 0; pass < (obj ? 2 : 1); pass++) {
                uint32_t off = obj ? (pass ? OUT_OFF : OBJ_OFF) : DST_OFF;
                int worst = 0, bad = 0, fx = -1, fy = -1, opaque_bad = 0;
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) {
                        uint32_t got = *at(off, PITCH, x, y), w = want[(size_t)y * W + x];
                        int m = 0;
                        for (int k = 0; k < 3; k++) m = std::max(m, std::abs(ch(got, k) - ch(w, k)));
                        worst = std::max(worst, m);
                        if (m > c.tol) { if (fx < 0) fx = x, fy = y; bad++; }
                        if (obj && c.g.op == D3DPT_GDI_ROP && (got >> 24) != 255) opaque_bad++;
                    }
                const char *where = obj ? (pass ? "object" : "bitmap VRAM") : "VRAM";
                cases++;
                if (bad || opaque_bad) {
                    failures++;
                    printf("FAIL: %-22s %-11s max %3d, %5d pixels past %d, %d not opaque", c.name, where, worst, bad, c.tol, opaque_bad);
                    if (fx >= 0) printf(" (first %d,%d: got %06x, want %06x)", fx, fy, *at(off, PITCH, fx, fy) & 0xffffff, want[(size_t)fy * W + fx] & 0xffffff);
                    printf("\n");
                } else {
                    printf("ok:   %-22s %-11s max %3d\n", c.name, where, worst);
                }
            }
        }
    }
    printf("d3dpt-gdi-test: %d checks, %d failed\n", cases, failures);
    p_destroy(X);
    return failures ? 1 : 0;
}
