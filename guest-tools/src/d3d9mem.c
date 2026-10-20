/*
 * d3d9mem: how much texture memory a Direct3D 9 device gives before a
 * CreateTexture fails, and with what. Makes a windowed HAL device, then
 * 1024x1024 A8R8G8B8 textures (4 MiB each, one level) in the pool named
 * on the command line until one fails or the count is reached; each is
 * locked and filled once, as a game's loader does. Prints
 * GetAvailableTextureMem before and after, the count, the failing HRESULT
 * and the process's address space, to stdout and C:\2KSBOX\D3D9MEM.LOG.
 *
 *   d3d9mem [managed|default] [max textures, default 1024]
 *
 * Written for Unigine Heaven in Direct3D 9 on the WDDM driver (track
 * M25, 2026-10-19), whose textures failed "out of video memory" with no
 * call reaching the user-mode driver.
 *
 * Build (mingw-w64, msvcrt, Pentium III floor as the rest of the ISO):
 *   i686-w64-mingw32-gcc -O2 -o d3d9mem.exe d3d9mem.c -ld3d9 -luser32
 */
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "guestlog.h"

static FILE *g_log;

static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
    fflush(stdout);
}

static void address_space(const char *when)
{
    MEMORYSTATUSEX ms;

    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        say("%s: address space %lu of %lu MiB free\n", when, (unsigned long)(ms.ullAvailVirtual >> 20),
            (unsigned long)(ms.ullTotalVirtual >> 20));
    }
}

int main(int argc, char **argv)
{
    D3DPOOL pool = (argc > 1 && !strcmp(argv[1], "default")) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
    int max = argc > 2 ? atoi(argv[2]) : 1024, n;
    WNDCLASSA wc = { 0 };
    HWND hwnd;
    IDirect3D9 *d3d;
    IDirect3DDevice9 *dev = NULL;
    IDirect3DTexture9 **tex;
    D3DADAPTER_IDENTIFIER9 id;
    D3DPRESENT_PARAMETERS pp = { 0 };
    D3DLOCKED_RECT lr;
    HRESULT hr = S_OK;

    g_log = guest_log_open("D3D9MEM.LOG", "w");
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d9mem";
    RegisterClassA(&wc);
    hwnd = CreateWindowA("d3d9mem", "d3d9mem", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320, 240, NULL, NULL, wc.hInstance, NULL);
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        say("Direct3DCreate9 failed\n");
        return 1;
    }
    if (SUCCEEDED(d3d->lpVtbl->GetAdapterIdentifier(d3d, 0, 0, &id))) {
        say("adapter: %s (%s)\n", id.Description, id.Driver);
    }
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = hwnd;
    hr = d3d->lpVtbl->CreateDevice(d3d, 0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) {
        say("CreateDevice: 0x%08lx\n", (unsigned long)hr);
        return 1;
    }
    say("pool %s, at most %d textures of 4 MiB\n", pool == D3DPOOL_MANAGED ? "managed" : "default", max);
    say("available texture memory before: %u MiB\n", dev->lpVtbl->GetAvailableTextureMem(dev) >> 20);
    address_space("before");
    tex = (IDirect3DTexture9 **)calloc((size_t)max, sizeof(*tex));
    for (n = 0; n < max; n++) {
        hr = dev->lpVtbl->CreateTexture(dev, 1024, 1024, 1, 0, D3DFMT_A8R8G8B8, pool, &tex[n], NULL);
        if (FAILED(hr)) {
            break;
        }
        if (pool == D3DPOOL_MANAGED) {
            if (FAILED(hr = tex[n]->lpVtbl->LockRect(tex[n], 0, &lr, NULL, 0))) {
                say("texture %d: LockRect 0x%08lx\n", n, (unsigned long)hr);
                n++;
                break;
            }
            memset(lr.pBits, n & 0xff, (size_t)lr.Pitch * 1024);
            tex[n]->lpVtbl->UnlockRect(tex[n], 0);
            /* the managed texture's video copy: a draw would upload it */
            tex[n]->lpVtbl->PreLoad(tex[n]);
        }
        if (n % 32 == 31) {
            say("%d textures (%d MiB), available %u MiB\n", n + 1, (n + 1) * 4, dev->lpVtbl->GetAvailableTextureMem(dev) >> 20);
        }
    }
    say("result: %d textures (%d MiB), %s 0x%08lx\n", n, n * 4, FAILED(hr) ? "failed with" : "last hr", (unsigned long)hr);
    say("available texture memory after: %u MiB\n", dev->lpVtbl->GetAvailableTextureMem(dev) >> 20);
    address_space("after");
    while (n--) {
        tex[n]->lpVtbl->Release(tex[n]);
    }
    dev->lpVtbl->Release(dev);
    d3d->lpVtbl->Release(d3d);
    return FAILED(hr) ? 2 : 0;
}
