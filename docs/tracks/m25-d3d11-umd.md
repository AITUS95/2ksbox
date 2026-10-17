# Track M25: Direct3D 10 and 11, a user-mode driver of our own

Opened 2026-10-16 (user: "the correct would be to implement a real
direct3d 10 or 11 driver, right?", then "let's do it from scratch
ourselves ... first milestone feature level 10_0"). ADR-029. Branch
`track/m25-d3d11-umd`.

## Why

M20 step 5 put M18's WDDM driver on Windows 11: a WDDM 1.1 kernel driver
and a Direct3D 9 user-mode driver. Windows 11's `d3d11.dll` reaches it
through `d3d10level9`, which tops out at feature level 9_3 (M20 got it
there from 9_1). Edge draws in software (`edge://gpu`: "Software only";
Chromium wants 10_0), no Direct3D 10 or 11 program gets a device, and
DWM, XAML and Direct2D all run through the 10level9 layer, where M20's
last open bug (Explorer's XAML that never composes) also lives.

## The shape

- **Guest: `d3dpt11.dll`** (`guest-tools/src/d3dptvid/wddm/um11/`), the
  Direct3D 11 DDI: `OpenAdapter10_2`, the Windows 7 interface
  (`D3D11_0_7_DDI`), the 3D pipeline level from
  `D3D11DDICAPS_3DPIPELINESUPPORT` (10_0 first). Written in C like the
  Direct3D 9 driver, with the same command buffer, allocation list and
  patch locations (`../d3dpt_wddm.h`). The INF names it after the
  Direct3D 9 driver in `UserModeDriverName` (Direct3D 9 reads the first
  name, 10 / 10.1 / 11 the next ones); a 32-bit copy for WoW64.
- **Wire: protocol v25**, one new record, `D3DPT_OP_D11`: a context's
  run of Direct3D 11 tokens, as `D3DPT_OP_DP2` carries a DP2 stream.
  Objects (shaders, states, views, layouts, queries) are the context's,
  by ids the guest picks; resources are allocations, by the host handle
  the kernel driver patches in, so another process (DWM) can name the
  same one.
- **Resources.** Every resource is one allocation. One the CPU may map
  (dynamic, staging) or that leaves the device (shared, a primary) has
  its subresources laid out in it; a default one only names itself and
  lives on the host. The kernel driver registers each with the host as
  before (`VRAM_SURFACE`, a new caps bit `D3DPT_VS_D3D11` for one with
  no Direct3D 9 object behind it).
- **Host: `d3dpt/exec/d3dpt_exec_d11.cpp`**, DXVK's d3d11 through
  `D3D11CoreCreateDevice` with DXVK's own DXGI adapter (on Windows both
  loaded by file name, `dxvk_d3d11.dll` / `dxvk_dxgi.dll`, beside
  `dxvk_d3d9.dll`; the process already holds Windows' own `dxgi.dll`).
  One device for the guest; each guest device an
  `ID3DDeviceContextState`, swapped in when its records run.
- **Shaders** pass through: the DDI gives the program tokens
  (SHDR/SHEX) and the I/O signatures by register; the host writes a
  DXBC container (ISGN/OSGN from the signatures, the code chunk, DXBC's
  own MD5 hash, which DXVK checks) and creates the shader from it.
  Input layouts get the same synthesized semantics.

## Steps

1. **The skeleton.** The DLL loads, `d3d11.dll` opens it, and
   `D3D11CreateDevice` at feature level 10_0 on our adapter succeeds;
   every DDI entry logs its first calls and succeeds. Nothing drawn yet.
2. **Protocol v25 and the host's device.** Resources, views, states,
   shaders, input layouts, the draws, clears, copies, updates, map and
   unmap through VRAM, readback. Checked by an in-guest program that
   renders a scene on our adapter and on WARP and compares the pixels.
3. **Presenting.** A windowed DXGI swap chain through DWM, then full
   screen.
4. **DWM on it**: the desktop composed through our 10_0 device instead
   of 10level9.
5. **10_1, then 11_0**: compute, unordered access views, tessellation.

## Test loop

The PC's Windows 11 test machine (`D:\vms\win11-test`, M20's tools,
outside the repo): `run-m25.sh` (M20's `run-m20.sh` on the overlay pair
`m25`, its own logs), `m25-test.sh` (installs the set staged in
`M25\WDDM64` with a build number of the minute in the INF, restarts, runs
`tools/d3d11-probe.cpp` as the logged-on user, prints its output and its
process's driver lines; `INSTALL=0`, `PROBE_ARGS=present`). The driver
is built with the EWDK (`build-windows.sh wddm`, or `guest-tools/
build-wddm.cmd` alone) and installed by `wddm-install.ps1`.

- **Reinstall with a new DriverVer.** Windows keeps the installed
  package's files when the version is the same, so a rebuilt DLL with
  the old version never lands (the first two cycles ran the old DLLs).
- The user-mode log names the process (`[pid]`) and the kernel driver
  writes a line at a time: Windows 11's shell opens the adapter from a
  dozen processes, and their lines interleaved by the character.
  `HKLM\SOFTWARE\2ksbox` `D3D11Trace` (REG_MULTI_SZ, executable names)
  logs every DDI call of those processes, with each format and
  multisample answer; every process logs each entry's first two calls.
- DXVK writes `player-mitsuami_d3d11.log` / `_dxgi.log` in the player's
  working directory (the checkout): what it refused, by name.
- `New-Item -Force` on a registry key that exists empties it (it took
  the D3D11Exclude value with it once).

## Results

**Step 1, 2026-10-16.** `d3d11.dll` loads `d3dpt11.dll` (the names after
the first in `UserModeDriverName`) and calls `OpenAdapter10_2`;
`D3D11CreateDevice` with every level asked for gives **10_0** on our
adapter (9_3 before, through d3d10level9), the host opens DXVK's d3d11
(feature level 11_0). A process the driver refuses (D3D11Exclude, or no
`CAP_D3D11`) falls back to d3d10level9 and the Direct3D 9 driver, as
before: DWM stays there (`wddm-install.ps1` writes `dwm.exe`).

- Every device the runtime made failed at the end with
  `DXGI_ERROR_DRIVER_INTERNAL_ERROR`, after the driver had answered
  everything: the runtime walks CheckMultisampleQualityLevels per format
  and count, and stopped at the first count claimed (2x, one level, for
  R32G32B32A32_FLOAT). With no multisampling claimed devices are made.
  Which claim it refuses is open.

**Step 2, the same day.** The probe's scene on our adapter at 10_0 against
WARP in the same guest: **0 of 65,536 pixels differ by more than 8** (the
largest difference 1): resources, an immutable and an updated texture, a
dynamic constant buffer, the input layout, shaders through the rebuilt
DXBC container (DXVK checked its hash and took it), sampler, blend,
depth, an indexed draw, a copy into staging read back through VRAM.

**Step 3, presenting.** The scene through a windowed swap chain read off
the screen after DWM: **0 pixels differ** in both models. The blt model
(DISCARD) goes through the kernel driver's Present after a readback into
the buffer's VRAM. The flip model (FLIP_SEQUENTIAL) has DWM open the
app's buffers through the Direct3D 9 driver and sample them: black until
those buffers were described to it as render-target *textures*.

**Windows 11's shell on it.** With DWM excluded, Explorer, Search, the
Start menu, WebView2 and the rest get 10_0 devices from this driver, and
the taskbar draws whole (icons, the search box, the tray, the clock).
Two things it took:

- **Coherence between the two drivers' copies of a shared surface.** The
  host keeps a Direct3D 11 object and a Direct3D 9 one per allocation;
  this side marks a resource in VRAM newer whenever it draws, clears or
  copies into it, and the Direct3D 9 side's surface lookup pulls it back
  through VRAM first (`exec_d11_sync`). Without it every icon was a
  black square.
- **A staging map polled with DONOTWAIT** queued a readback and a flush
  each time, so the allocation was always busy and the runtime's answer
  always "still drawing": Explorer's icons (uploaded through 13x13 to
  46x46 staging textures) never arrived. A staging resource is read back
  only when the host wrote it since (a copy, an update, initial data).

Also: **a driver reinstall restarts the kernel driver's allocation
handles at 1 with no machine reset**, so the host held old objects under
reused handles (DXVK: "Incompatible view dimension for Buffer") and the
player died; a creator's RES_CREATE now replaces whatever had the handle.
**`CAP_D3D11`** (register set v10): the device asks the executor
(`d3dpt_exec_features`) whether a Direct3D 11 device opens, and the
driver refuses to open without it, so a host with no d3d11 (an older
executor, a package without DXVK's d3d11, the Wine executor) leaves the
guest on 9_3: checked with `D3DPT_DXVK_D3D11_LIB` pointing nowhere.

`scripts/test.sh all` on the PC with these changes (2026-10-16): 64
passed, 16 skipped, 3 failed (`cpus`, `clone`, `win11-snapshots`: the
launcher's machine files, with this checkout's `launcherx` older than
`main`; nothing this track touches).

## Open

- The 11.0 DDI's signatures carry no component types, so the host
  declares non-system-value stage interfaces as float (ADR-029's costs).
- Multisampling (above).
- Step 4, DWM on this driver; the packages (DXVK's d3d11 and dxgi beside
  `dxvk_d3d9.dll` in the Windows zip and MSIX, the Linux tarball, the Mac
  app); Linux and macOS builds of `d3dpt_exec_d11.cpp` (written against
  DXVK's native headers, built on Windows only so far).
- A scaled DXGI Blt and a Blt into a surface only Direct3D 9 knows are
  logged and not done.
- Direct3D 10 programs (`d3d10.dll` wants the Direct3D 10 / 10.1 DDI
  versions, which `GetSupportedVersions` does not list yet).
