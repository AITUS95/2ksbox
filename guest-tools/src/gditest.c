/*
 * gditest.c: GDI's operations on a window, against the same on a DIB
 * (track M25, the WDDM driver's GDI acceleration)
 *
 * A window's drawing goes where the display driver says: with GDI
 * acceleration (RenderKm) a BitBlt, a ColorFill, AlphaBlend, StretchBlt,
 * TransparentBlt and ClearType text into the window's redirection bitmap
 * are done by the host (protocol v27 and v29), the rest by GDI on the CPU.
 * The same operations into a DIB section are GDI's own software rendering,
 * which this takes for the answer.
 *
 * Each case draws into its own cell of the window and of a DIB of the
 * client's size, both starting from the same background, then the
 * window's client is read back (a BitBlt from its DC) and compared cell by
 * cell, red, green and blue (the window's alpha is DWM's business). The
 * sources are a DIB section (system memory: GDI moves it into a staging
 * surface first) and a compatible bitmap (the display's), each with a
 * pattern and a premultiplied alpha.
 *
 * Then GDI on a Direct3D 11 texture made GDI_COMPATIBLE, as Notepad's
 * text area is (IDXGISurface1::GetDC): Direct3D clears it, GDI fills and
 * writes into it, Direct3D copies a block in, GDI inverts across that
 * block's edge, and the texture is read back through a staging copy and
 * compared with the same steps on a DIB. GDI writes such a texture with
 * the CPU, so its drawing reaches the host's object only if the drivers
 * bring it over, and Direct3D's reaches GDI only the other way round.
 *
 * `gditest: N cases, M failed` is the verdict, in C:\2KSBOX\GDITEST.LOG
 * with a line per case: the largest difference and how many pixels went
 * past the case's tolerance (0 for the copies and ROPs; a few levels where
 * a blend's rounding may differ). Run as the logged-on user on the test
 * machine (D:\vms\win11-test\G11\g11.ps1 GDITEST.EXE); the kernel
 * driver's `RenderKm in 5 s:` lines in the QEMU log say which operations
 * went through RenderKm.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "guestlog.h"

#define CW 96               /* a case's cell */
#define CH 96
#define COLS 8
#define ROWS 5
#define WW (CW * COLS)
#define WH (CH * ROWS)
#define SW 64               /* the sources */
#define SH 64

static FILE *log_file;
static int cases, failed;

static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (log_file) {
        va_start(ap, fmt);
        vfprintf(log_file, fmt, ap);
        va_end(ap);
        fflush(log_file);
    }
}

/* a 32-bit top-down DIB section and its memory DC */
typedef struct Dib { HDC dc; HBITMAP bm, old; DWORD *px; int w, h; } Dib;

static int dib_make(Dib *d, HDC ref, int w, int h)
{
    BITMAPINFO bi;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    d->dc = CreateCompatibleDC(ref);
    d->bm = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, (void **)&d->px, NULL, 0);
    if (!d->dc || !d->bm) {
        return 0;
    }
    d->old = SelectObject(d->dc, d->bm);
    d->w = w;
    d->h = h;
    return 1;
}

/* the sources' picture: colour bands, a diagonal, a key-coloured square
 * (magenta) and an alpha that falls left to right, premultiplied */
static void src_paint(Dib *s)
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            unsigned a = 255 - (unsigned)x * 255 / (s->w - 1);
            unsigned r = (unsigned)(x * 4) & 255, g = (unsigned)(y * 4) & 255, b = ((x + y) & 8) ? 220 : 40;

            if (x > 20 && x < 36 && y > 20 && y < 36) {
                r = 255, g = 0, b = 255, a = 255;      /* TransparentBlt's key */
            }
            if (x == y) {
                r = g = b = 255;
            }
            s->px[y * s->w + x] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
        }
    }
}

/* the background every cell starts from: a gradient, the same in both */
static void bg_paint(Dib *b)
{
    int x, y;

    for (y = 0; y < b->h; y++) {
        for (x = 0; x < b->w; x++) {
            b->px[y * b->w + x] = 0xff000000u | ((unsigned)(x * 255 / b->w) << 16) |
                                  ((unsigned)(y * 255 / b->h) << 8) | (unsigned)(((x / 8 + y / 8) & 1) ? 200 : 90);
        }
    }
}

typedef void (*draw_fn)(HDC dst, int x, int y, HDC src, int arg);

static void op_bitblt(HDC d, int x, int y, HDC s, int rop)
{
    BitBlt(d, x + 8, y + 8, SW, SH, s, 0, 0, (DWORD)rop);
}

static void op_patblt(HDC d, int x, int y, HDC s, int rop)
{
    HBRUSH br = CreateSolidBrush(RGB(40, 200, 120)), old = SelectObject(d, br);

    (void)s;
    PatBlt(d, x + 8, y + 8, SW, SH, (DWORD)rop);
    SelectObject(d, old);
    DeleteObject(br);
}

static void op_fillrect(HDC d, int x, int y, HDC s, int color)
{
    RECT r = { x + 10, y + 10, x + 70, y + 50 };
    HBRUSH br = CreateSolidBrush((COLORREF)color);

    (void)s;
    FillRect(d, &r, br);
    DeleteObject(br);
}

/* arg: the destination's width and height (negative: mirrored) */
static void op_stretch(HDC d, int x, int y, HDC s, int arg)
{
    int w = (short)(arg & 0xffff), h = (short)(arg >> 16);

    SetStretchBltMode(d, COLORONCOLOR);
    StretchBlt(d, x + 8 + (w < 0 ? -w : 0), y + 8 + (h < 0 ? -h : 0), w, h, s, 0, 0, SW, SH, SRCCOPY);
}

/* arg: the constant alpha | 0x100 per-pixel alpha | 0x200 stretched */
static void op_alpha(HDC d, int x, int y, HDC s, int arg)
{
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(arg & 255), (BYTE)((arg & 0x100) ? AC_SRC_ALPHA : 0) };
    int w = (arg & 0x200) ? 80 : SW, h = (arg & 0x200) ? 40 : SH;

    AlphaBlend(d, x + 8, y + 8, w, h, s, 0, 0, SW, SH, bf);
}

/* arg: stretched */
static void op_transparent(HDC d, int x, int y, HDC s, int arg)
{
    TransparentBlt(d, x + 8, y + 8, arg ? 80 : SW, arg ? 80 : SH, s, 0, 0, SW, SH, RGB(255, 0, 255));
}

/* arg: the text colour; ClearType, transparent (blended over the cell) */
static void op_text(HDC d, int x, int y, HDC s, int arg)
{
    HFONT f = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT old = SelectObject(d, f);
    RECT r = { x + 2, y + 4, x + CW - 2, y + CH - 4 };

    (void)s;
    SetBkMode(d, TRANSPARENT);
    SetTextColor(d, (COLORREF)arg);
    DrawTextA(d, "ClearType gdi Wq 0123", -1, &r, DT_WORDBREAK);
    SelectObject(d, old);
    DeleteObject(f);
}

/* opaque text: a fill behind, then the blend */
static void op_text_opaque(HDC d, int x, int y, HDC s, int arg)
{
    HFONT f = CreateFontA(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT old = SelectObject(d, f);

    (void)s;
    SetBkMode(d, OPAQUE);
    SetBkColor(d, RGB(250, 250, 240));
    SetTextColor(d, (COLORREF)arg);
    TextOutA(d, x + 4, y + 30, "Opaque gW", 9);
    SelectObject(d, old);
    DeleteObject(f);
}

/* a copy within the window's own surface, overlapping (a scroll) */
static void op_scroll(HDC d, int x, int y, HDC s, int arg)
{
    (void)s;
    BitBlt(d, x + 4 + arg, y + 4 + arg, 60, 60, d, x + 4, y + 4, SRCCOPY);
}

typedef struct Case {
    const char *name;
    draw_fn fn;
    int arg, src, tol;   /* src: 0 none, 1 the DIB, 2 the compatible bitmap; tol: per channel */
} Case;

static const Case table[] = {
    { "bitblt SRCCOPY dib", op_bitblt, (int)SRCCOPY, 1, 0 },
    { "bitblt SRCCOPY ddb", op_bitblt, (int)SRCCOPY, 2, 0 },
    { "bitblt SRCINVERT", op_bitblt, (int)SRCINVERT, 2, 0 },
    { "bitblt SRCAND", op_bitblt, (int)SRCAND, 2, 0 },
    { "bitblt SRCPAINT", op_bitblt, (int)SRCPAINT, 2, 0 },
    { "bitblt NOTSRCCOPY", op_bitblt, (int)NOTSRCCOPY, 2, 0 },
    { "bitblt SRCERASE", op_bitblt, (int)SRCERASE, 2, 0 },
    { "bitblt MERGEPAINT", op_bitblt, (int)MERGEPAINT, 2, 0 },
    { "bitblt NOTSRCERASE", op_bitblt, (int)NOTSRCERASE, 1, 0 },
    { "bitblt DSTINVERT", op_bitblt, (int)DSTINVERT, 2, 0 },
    { "patblt PATCOPY", op_patblt, (int)PATCOPY, 0, 0 },
    { "patblt PATINVERT", op_patblt, (int)PATINVERT, 0, 0 },
    { "patblt DSTINVERT", op_patblt, (int)DSTINVERT, 0, 0 },
    { "patblt BLACKNESS", op_patblt, (int)BLACKNESS, 0, 0 },
    { "fillrect", op_fillrect, (int)RGB(200, 40, 60), 0, 0 },
    { "stretch grow", op_stretch, 80 | (80 << 16), 2, 0 },
    { "stretch shrink", op_stretch, 40 | (30 << 16), 2, 0 },
    { "stretch mirror x", op_stretch, (-64 & 0xffff) | (64 << 16), 2, 0 },
    { "stretch mirror y", op_stretch, 64 | (-64 << 16), 2, 0 },
    { "stretch dib", op_stretch, 72 | (50 << 16), 1, 0 },
    { "alpha constant", op_alpha, 128, 2, 2 },
    { "alpha per-pixel", op_alpha, 0x100 | 255, 2, 2 },
    { "alpha both", op_alpha, 0x100 | 160, 2, 2 },
    { "alpha stretched", op_alpha, 0x300 | 200, 2, 2 },
    { "alpha dib", op_alpha, 0x100 | 255, 1, 2 },
    { "transparent", op_transparent, 0, 2, 0 },
    { "transparent stretched", op_transparent, 1, 2, 0 },
    { "transparent dib", op_transparent, 0, 1, 0 },
    { "cleartype black", op_text, (int)RGB(0, 0, 0), 0, 3 },
    { "cleartype white", op_text, (int)RGB(255, 255, 255), 0, 3 },
    { "cleartype red", op_text, (int)RGB(220, 20, 20), 0, 3 },
    { "cleartype opaque", op_text_opaque, (int)RGB(20, 20, 120), 0, 3 },
    { "scroll down", op_scroll, 12, 0, 0 },
    { "scroll up", op_scroll, -3, 0, 0 },
};
#define NCASES ((int)(sizeof(table) / sizeof(table[0])))

/* compare a 32-bit picture against the reference, red, green and blue */
static void compare(const char *name, const DWORD *got, int got_pitch, const DWORD *want, int w, int h, int tol)
{
    int x, y, worst = 0, off = 0, fx = -1, fy = -1;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            DWORD a = got[y * got_pitch + x], b = want[y * w + x];
            int k, dmax = 0;

            for (k = 0; k < 24; k += 8) {
                int dd = abs((int)((a >> k) & 255) - (int)((b >> k) & 255));

                dmax = dd > dmax ? dd : dmax;
            }
            worst = dmax > worst ? dmax : worst;
            if (dmax > tol) {
                if (fx < 0) {
                    fx = x, fy = y;
                }
                off++;
            }
        }
    }
    cases++;
    if (off) {
        failed++;
        say("FAIL %-24s max %3d, %4d pixels past %d (first at %d,%d: got %06lx, want %06lx)\n", name, worst, off, tol, fx,
            fy, got[fy * got_pitch + fx] & 0xffffff, want[fy * w + fx] & 0xffffff);
    } else {
        say("ok   %-24s max %3d\n", name, worst);
    }
}

#define TW 160              /* the Direct3D texture */
#define TH 96

typedef HRESULT(WINAPI *pfn_create)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                    UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

/* GDI's steps on the texture's DC (or the reference DIB's) */
static void interop_gdi1(HDC d)
{
    RECT r = { 10, 10, 70, 50 };
    HBRUSH br = CreateSolidBrush(RGB(200, 40, 60));

    FillRect(d, &r, br);
    DeleteObject(br);
    op_text_opaque(d, 76, 0, NULL, (int)RGB(20, 20, 120));
}

static void interop_gdi2(HDC d)
{
    PatBlt(d, 100, 40, 50, 40, DSTINVERT);
}

/* the texture's pixels, through a staging copy, into out (TW x TH) */
static int interop_read(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, DWORD *out)
{
    D3D11_TEXTURE2D_DESC sd;
    ID3D11Texture2D *st;
    D3D11_MAPPED_SUBRESOURCE m;
    int y;

    ID3D11Texture2D_GetDesc(tex, &sd);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &sd, NULL, &st))) {
        return 0;
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)tex);
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m))) {
        ID3D11Texture2D_Release(st);
        return 0;
    }
    for (y = 0; y < TH; y++) {
        memcpy(out + y * TW, (const BYTE *)m.pData + y * m.RowPitch, TW * 4);
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
    ID3D11Texture2D_Release(st);
    return 1;
}

/* GDI and Direct3D 11 on one GDI-compatible texture (Notepad's text area) */
static void interop_cases(HDC ref_dc)
{
    static const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3 };
    static const float clear[4] = { 0.2f, 0.4f, 0.6f, 1.0f };
    HMODULE m = LoadLibraryA("d3d11.dll");
    pfn_create create = m ? (pfn_create)GetProcAddress(m, "D3D11CreateDevice") : NULL;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    ID3D11Texture2D *tex = NULL, *blk = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    IDXGISurface1 *surf = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_BOX box = { 0, 0, 0, 40, 40, 1 };
    DWORD *got = malloc(TW * TH * 4), blk_px[40 * 40];
    Dib ref;
    HDC dc;
    HRESULT hr;
    int i;

    if (!create || !got || !dib_make(&ref, ref_dc, TW, TH)) {
        say("FAIL interop: no d3d11.dll or memory\n");
        cases++, failed++;
        return;
    }
    hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, want, 3, D3D11_SDK_VERSION, &dev,
                NULL, &ctx);
    memset(&td, 0, sizeof(td));
    td.Width = TW;
    td.Height = TH;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    if (SUCCEEDED(hr)) {
        hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &tex);
    }
    if (SUCCEEDED(hr)) {
        hr = ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)tex, NULL, &rtv);
    }
    if (SUCCEEDED(hr)) {
        hr = ID3D11Texture2D_QueryInterface(tex, &IID_IDXGISurface1, (void **)&surf);
    }
    /* a block Direct3D copies in: yellow */
    for (i = 0; i < 40 * 40; i++) {
        blk_px[i] = 0xff20e0f0u;
    }
    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC bd = td;
        D3D11_SUBRESOURCE_DATA sd = { blk_px, 40 * 4, 0 };

        bd.Width = bd.Height = 40;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = 0;
        hr = ID3D11Device_CreateTexture2D(dev, &bd, &sd, &blk);
    }
    if (FAILED(hr)) {
        say("FAIL interop: Direct3D 11 setup 0x%08lx\n", (unsigned long)hr);
        cases++, failed++;
        goto out;
    }

    /* 1: Direct3D's clear, GDI's fill and text over it */
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
    for (i = 0; i < TW * TH; i++) {
        ref.px[i] = 0xff336699u;
    }
    hr = IDXGISurface1_GetDC(surf, FALSE, &dc);
    if (FAILED(hr)) {
        say("FAIL interop: GetDC 0x%08lx\n", (unsigned long)hr);
        cases++, failed++;
        goto out;
    }
    interop_gdi1(dc);
    IDXGISurface1_ReleaseDC(surf, NULL);
    interop_gdi1(ref.dc);
    GdiFlush();
    if (interop_read(dev, ctx, tex, got)) {
        compare("d3d11 clear, gdi draws", got, TW, ref.px, TW, TH, 3);
    }
    /* the same through GDI's eyes: the texture's DC copied into a DIB */
    if (SUCCEEDED(IDXGISurface1_GetDC(surf, FALSE, &dc))) {
        Dib back;

        if (dib_make(&back, ref_dc, TW, TH)) {
            BitBlt(back.dc, 0, 0, TW, TH, dc, 0, 0, SRCCOPY);
            GdiFlush();
            compare("gdi reads it back", back.px, TW, ref.px, TW, TH, 3);
            SelectObject(back.dc, back.old);
            DeleteObject(back.bm);
            DeleteDC(back.dc);
        }
        IDXGISurface1_ReleaseDC(surf, NULL);
    }

    /* 2: Direct3D copies a block in; GDI inverts across its edge */
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)tex, 0, 90, 30, 0, (ID3D11Resource *)blk, 0, &box);
    for (i = 0; i < 40 * 40; i++) {
        ref.px[(30 + i / 40) * TW + 90 + i % 40] = blk_px[i];
    }
    hr = IDXGISurface1_GetDC(surf, FALSE, &dc);
    if (SUCCEEDED(hr)) {
        interop_gdi2(dc);
        IDXGISurface1_ReleaseDC(surf, NULL);
    }
    interop_gdi2(ref.dc);
    GdiFlush();
    if (interop_read(dev, ctx, tex, got)) {
        compare("d3d11 copy, gdi inverts", got, TW, ref.px, TW, TH, 3);
    }

out:
    if (surf) IDXGISurface1_Release(surf);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (blk) ID3D11Texture2D_Release(blk);
    if (tex) ID3D11Texture2D_Release(tex);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dev) ID3D11Device_Release(dev);
    free(got);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) {
        return 1;               /* the test paints everything itself */
    }
    return DefWindowProcA(h, m, w, l);
}

static void pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG msg;

    do {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            DispatchMessageA(&msg);
        }
        Sleep(10);
    } while ((LONG)(end - GetTickCount()) > 0);
}

int main(int argc, char **argv)
{
    WNDCLASSA wc;
    RECT wr = { 0, 0, WW, WH };
    HWND hwnd;
    HDC wdc, ddb_dc;
    HBITMAP ddb, ddb_old;
    Dib src, ref, bg, back;
    int i, keep = argc > 1 && !strcmp(argv[1], "-keep");

    log_file = guest_log_open("GDITEST.LOG", "w");
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "gditest";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA("gditest", "gditest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                         wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        say("gditest: no window (%lu)\n", GetLastError());
        return 1;
    }
    SetForegroundWindow(hwnd);
    pump(500);
    wdc = GetDC(hwnd);
    if (!dib_make(&src, wdc, SW, SH) || !dib_make(&ref, wdc, WW, WH) || !dib_make(&bg, wdc, WW, WH) ||
        !dib_make(&back, wdc, WW, WH)) {
        say("gditest: no DIB sections\n");
        return 1;
    }
    src_paint(&src);
    bg_paint(&bg);
    /* the display's own bitmap with the same picture */
    ddb = CreateCompatibleBitmap(wdc, SW, SH);
    ddb_dc = CreateCompatibleDC(wdc);
    ddb_old = SelectObject(ddb_dc, ddb);
    BitBlt(ddb_dc, 0, 0, SW, SH, src.dc, 0, 0, SRCCOPY);

    /* the same start in both */
    BitBlt(wdc, 0, 0, WW, WH, bg.dc, 0, 0, SRCCOPY);
    BitBlt(ref.dc, 0, 0, WW, WH, bg.dc, 0, 0, SRCCOPY);
    GdiFlush();
    pump(200);

    for (i = 0; i < NCASES; i++) {
        const Case *c = &table[i];
        int x = (i % COLS) * CW, y = (i / COLS) * CH;
        HDC s = c->src == 1 ? src.dc : c->src == 2 ? ddb_dc : NULL;
        HRGN clip = CreateRectRgn(x, y, x + CW, y + CH);

        SelectClipRgn(wdc, clip);
        SelectClipRgn(ref.dc, clip);
        c->fn(wdc, x, y, s, c->arg);
        c->fn(ref.dc, x, y, c->src == 2 ? src.dc : s, c->arg);
        SelectClipRgn(wdc, NULL);
        SelectClipRgn(ref.dc, NULL);
        DeleteObject(clip);
    }
    GdiFlush();
    pump(300);

    /* the window as it is now */
    BitBlt(back.dc, 0, 0, WW, WH, wdc, 0, 0, SRCCOPY);
    GdiFlush();
    for (i = 0; i < NCASES; i++) {
        const Case *c = &table[i];
        int x0 = (i % COLS) * CW, y0 = (i / COLS) * CH, x, y, worst = 0, off = 0, fx = -1, fy = -1;

        for (y = y0; y < y0 + CH; y++) {
            for (x = x0; x < x0 + CW; x++) {
                DWORD a = back.px[y * WW + x], b = ref.px[y * WW + x];
                int k, dmax = 0;

                for (k = 0; k < 24; k += 8) {
                    int dd = abs((int)((a >> k) & 255) - (int)((b >> k) & 255));

                    dmax = dd > dmax ? dd : dmax;
                }
                if (dmax > worst) {
                    worst = dmax;
                }
                if (dmax > c->tol) {
                    if (fx < 0) {
                        fx = x - x0, fy = y - y0;
                    }
                    off++;
                }
            }
        }
        cases++;
        if (off) {
            failed++;
            say("FAIL %-24s max %3d, %4d pixels past %d (first at %d,%d: window %06lx, DIB %06lx)\n", c->name, worst,
                off, c->tol, fx, fy, back.px[(y0 + fy) * WW + x0 + fx] & 0xffffff, ref.px[(y0 + fy) * WW + x0 + fx] & 0xffffff);
        } else {
            say("ok   %-24s max %3d\n", c->name, worst);
        }
    }
    interop_cases(wdc);
    say("gditest: %d cases, %d failed\n", cases, failed);
    if (keep) {
        pump(60000);
    }
    SelectObject(ddb_dc, ddb_old);
    DeleteObject(ddb);
    DeleteDC(ddb_dc);
    ReleaseDC(hwnd, wdc);
    DestroyWindow(hwnd);
    if (log_file) {
        fclose(log_file);
    }
    return failed ? 2 : 0;
}
