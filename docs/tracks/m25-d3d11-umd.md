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
  Direct3D 11 DDI: `OpenAdapter10_2`, Windows 8's 11.1 interface
  (`D3D11_1_DDI`, with its video functions, `d11_video.c`; step 4) and
  the Windows 7 one (`D3D11_0_7_DDI`), the 3D pipeline level from
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
   of 10level9 (done 2026-10-17, below).
5. **10_1, then 11_0**: compute, unordered access views, tessellation.

## Test loop

The PC's Windows 11 test machine (`D:\vms\win11-test`, M20's tools,
outside the repo): `run-m25.sh` (M20's `run-m20.sh` on the overlay pair
`m25`, its own logs), `m25-test.sh` (installs the set staged in
`M25\WDDM64` with a build number of the minute in the INF, restarts, runs
`tools/d3d11-probe.cpp` as the logged-on user, prints its output and its
process's driver lines; `INSTALL=0`, `PROBE_ARGS=present`, `"present
12000"` to hold each window 12 s, `flags`). Step 4 added `stage25.sh`
(this checkout's `build/wddm/x64` into `M25\WDDM64`), `inst25.sh`
(install, wait for the restart, detach Basic Display's screen as the
guest agent would, `M25\oneshot.ps1`), `dwm25.sh <tag>` (DWM off
`D3D11Exclude`, restarted, its driver lines into `m25-dwm.log`, a
screendump; `EXCLUDE=1` puts it back on Direct3D 9's driver, `TRACE=1`
traces it) and `shot25.sh <tag>` (a screendump of d3dpt0). The driver
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
- The user-mode log stops at 4000 lines a process: tracing DWM spends
  them in seconds (the format and multisample answers are logged once
  even when traced, `D11_TRACE_FEW`).
- `D3D11Win7` (the same kind of list) offers a process only the Windows
  7 interface: an A/B for the 11.1 one.
- **DWM's crashes**: `HKLM\SOFTWARE\Microsoft\Windows\Windows Error
  Reporting\LocalDumps\dwm.exe` (`DumpFolder` `C:\2KSBOX\dumps`,
  `DumpType` 1) keeps a minidump of each; one comes out zipped and base64
  through `guest-run.sh` (~250 KB of text) and is read on the PC with the
  EWDK's `cdb` (`E:\Program Files\Windows Kits\10\Debuggers\x64`,
  `_NT_SYMBOL_PATH=srv*<cache>*https://msdl.microsoft.com/download/
  symbols`): `!analyze -v`, then the frames' registers (`.frame /r`).

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

**Step 4, DWM on it, 2026-10-17.** Windows 11 composes through this
driver: DWM's devices are 11.1 ones on our adapter from logon, nothing
of it goes to the Direct3D 9 driver, and the desktop, the Start menu
(its acrylic too), the taskbar and File Explorer (its XAML header and
Home page as well, M20's open bug) draw whole, with one screen or with
Basic Display's extended beside it, and through live switches between
the two. `d3d11-probe` passes offscreen and through both swap chain
models (0 pixels off). What it took:

- **The 11.1 interface.** With D3D11Exclude empty DWM made one device
  on us at 10_0 and composed through a second one on d3d10level9 anyway.
  `d3d11-probe flags` found why: every `D3D11CreateDevice` flag gets 10_0
  on our adapter except `VIDEO_SUPPORT`, which got 9_3, and DWM's
  composition device asks for it; the runtime gives such a device only
  to a driver with the 11.1 interface's video functions. The runtime
  takes the 11.1 interface over our WDDM 1.1 kernel driver (tested with
  the version offered to the probe alone first). `d11_video.c`: no
  decoder profiles, no content protection, a video processor for the RGB
  formats (its Blt logged, not done). The 11.1 table is the 11.0 one
  entry for entry plus five: the entries whose arguments changed (copy
  flags, `SetConstantBuffers1`, 11.1's blend and rasterizer descriptions,
  shaders with 11.1's signatures, which carry the component types, a
  flush that answers) and Discard, ClearView, CheckDirectFlipSupport (no),
  AssignDebugBinary, the constant-buffer map; DXGI 1.2's Blt1 and
  offer/reclaim. **Protocol v26**: a signature entry's component type,
  `SET_CB1`, `CLEAR_VIEW`.
- **Primaries the display scans out.** The driver marked DXGI's
  primaries `NO_SCANOUT` (step 1's choice); then DXGI never sets the
  display mode on DWM's primary, and its frames went where the screen
  never looked (a black desktop). They are scanned out now, as the
  Direct3D 9 driver's are.
- **Views follow a flip-model swap chain's rotation.** DWM renders
  through render target views it makes once per buffer; DXGI rotates the
  buffers' identities at each present, which the driver did to its
  resources but not to the host's views, so DWM drew into one host
  buffer while the screen flipped through the others (only the regions
  redrawn showed). Each view keeps its creation; after a rotation the
  views of the rotated buffers are made again under their ids and bound
  again where they were.
- **The fix-up reserve.** A kernel driver fix-up entry is three ULONGs
  and a pointer, 24 bytes on x64 (also for a 32-bit process under
  WoW64), and both user-mode drivers reserved 16 per patch location.
  DWM filled a command buffer to 251,616 bytes with 392 patches and the
  kernel driver refused it ("Render does not fit one DMA buffer"):
  every object it created was lost (`resource N unknown` on the host,
  the screen black after a topology change). Both reserve 24 now.

**WDDM 1.2** (user: "also implement wddm 1.2"). The x64 kernel driver
has Windows 8's interface and WDDM 1.2's DDIs: the per-engine TDR
(QueryDependentEngineGroup, QueryEngineStatus, ResetEngine), handing the
screen to Basic Display (StopDeviceAndReleasePostDisplayOwnership) and
the bugcheck screen (SystemDisplayEnable / Write); with them reported
Windows lists the adapter as **WDDM 1.2, DDI 11.1** (dxdiag). It does
not report 1.2 yet (`D3DPT_CLAIM_WDDM12`): with it, every program that
presents through a window's swap chain made DWM fail fast, again and
again while the window lived. Bisected on the PC: the 11.1 user-mode
driver on a WDDM 1.1 kernel driver passes; Windows 8's interface with
the new DDIs and the 1.0 caps passes; `WDDMVersion = DXGKDDI_WDDMv1_2`
alone (no preemption caps, no per-engine TDR) fails, whether the program
is on the 11.1 or the Windows 7 interface and whether DWM is on this
driver or on d3d10level9. In DWM's dumps the error is
`DXGI_ERROR_DEVICE_REMOVED`, met in `CD3DDevice::CopyTexture2D` (from
`CSecondarySysmemBitmap::Update`, DWM's copy of a swap chain's buffer
through system memory, with a WARP lock) or `CreateTexture`; neither
driver logs an error, the kernel driver refuses no present, and
dxgkrnl logs no event. Left for later (user, 2026-10-17).

`scripts/test.sh all` on the PC with step 4 (2026-10-17): 67 passed, 16
skipped, none failed.

**The desktop at high resolutions, 2026-10-17** (user: "laggy on win 11
... works well on low resolutions, but higher resolutions are slow"). A
maximized window repainted every 10 ms (`LOAD\anim.ps1` on the test
machine, run as the logged-on user): DWM composed **~31 frames/s at
1600x1200 against ~58 at 1024x768**, with the guest mostly idle (17% of
four CPUs: System, where the executor runs inside the submission, 34% of
one, DWM 13%) and the host executor 22% busy. A latency problem, then:
at 1600x1200 DWM's frame is ~17 Direct3D 9 submissions (the window into a
full-screen intermediate, the taskbar acrylic's blur chain, the primary)
against ~4 at 1024x768, and the Direct3D 9 user-mode driver flushed its
command buffer at every Lock, so each draw after a write to DWM's dynamic
vertex ring was a submission of its own and the lock then waited for that
submission's fence (dxgkrnl's scheduler, the device's interrupt, the
DPC). DWM missed every other vertical blank. A NOOVERWRITE lock no
longer flushes (the caller writes only bytes no queued command reads, and
dxgkrnl, which knows of no work on the allocation, locks it at once): the
same load at 1600x1200 now composes at **54-58 frames/s**, ~900 batches
in 5 s instead of ~6,300. The Direct3D 11 driver had no such cost (a
dynamic resource maps a CPU copy and its unmap sends the bytes that
changed). Not it, measured on the way: the segment (`vgamem_mb=512` gave
the same 31) and the readbacks' size (~3.8 ms at 1600x1200, ~3.4 ms at
1024x768, most of it the wait for the GPU).

Seen on the way, both open below: in this overlay DWM made one device on
this driver through the Windows 7 interface (`0xb000f`) and composed on
d3d10level9, the Direct3D 9 driver, after logon and after a restart of
DWM alike, not as step 4 found it; and under the full-window load the
guest twice hit a TDR within ~30 s and restarted.

**Widescreen modes** (user: "add more resolution options to our driver
on windows 10/11"). The host's mode table is the XP-era one (640x480 to
1600x1200, 4:3 and 5:4, shared with the XP miniport); the WDDM kernel
driver now adds 1280x720, 1280x800, 1366x768, 1440x900, 1600x900,
1680x1050, 1920x1080, 1920x1200, 2560x1440, 2560x1600 and 3840x2160 at
60 Hz (`wide_sizes`), each where six of its frames fit the segment
(DWM's primaries, its intermediate and the windows' surfaces): the
default 63 MiB stops at 1920x1200, `vgamem_mb=256` adds 2560x1600,
`vgamem_mb=512` 3840x2160. The device takes any linear mode. XP and 9x
keep their lists. The same full-window load with the NOOVERWRITE change,
each from a fresh boot: **1920x1080 57-59 frames/s** on the default
segment (a run right after switching modes in the same session gave 30,
not seen again from a boot); **2560x1440 ~32** (`vgamem_mb=256`), with
the guest mostly idle and the executor 28% busy: per frame the primary's
readback (~6.5 ms, ~3.8 of it waiting for the GPU and the copy across)
and the untracked-pixel passes over the same 14.7 MB (the frame's first
draw compares the target with its shadow, ~1.5 ms; the readback compares
again and copies twice) run inside DWM's present, on the vCPU, and the
flip misses its blank. What would take it further: the readback only of
the rectangles drawn since the last one (a desktop that changes a little
changes a little), and the readback's copy done at the flip instead of
inside the present.

## Open

- ~~**A TDR under a full-screen repaint**~~ (fixed by QEMU patch 94) (`LOAD\anim.ps1` maximized at
  1600x1200, before the NOOVERWRITE change; not seen in one run after
  it): `CollectDbgInfo` with every fence done on the host and the last
  one never reported (`fences submitted 0xbb37 done 0xbb37 notified
  0xbb36`), the device's `IRQ_STATUS` 3 (both events pending, enabled)
  while no interrupt service ran, then `ResetFromTimeout` and a guest
  restart. **The cause is QEMU's**: WHPX's APIC EOI exit called
  `ioapic_eoi_broadcast()` without the BQL from the vCPU loop while the
  device raised its line under it from another vCPU, and the two
  interleave so that neither delivers (patch 94, `94-whpx-eoi-bql`, which
  takes the BQL; every guest's level-triggered INTx under WHPX could lose
  its line so, an XP's sound or USB card on an APIC HAL included; KVM's
  IOAPIC is in the kernel and TCG's EOI holds the BQL). Checked
  2026-10-17: the old Direct3D 9 driver (a submission per draw, the most
  interrupts), which hit the TDR within ~30-40 s three runs of three,
  ran the same load on the patched QEMU three times 180 s with no TDR and
  no restart. Fixed.
- **DWM back on d3d10level9** in the `m25` overlay (above): which of its
  devices the runtime refuses on this driver.

- **Claiming WDDM 1.2** (above): what Windows does differently with a
  window's swap chain on a 1.2 driver.
- Multisampling (above).
- The packages (DXVK's d3d11 and dxgi beside
  `dxvk_d3d9.dll` in the Windows zip and MSIX, the Linux tarball, the Mac
  app); Linux and macOS builds of `d3dpt_exec_d11.cpp` (written against
  DXVK's native headers, built on Windows only so far).
- A scaled DXGI Blt, a Blt1 of part of its source, a Blt into a surface
  only Direct3D 9 knows, the video processor's Blt, ClearView of a view
  that is not a render target's, a blend state's logic op and a
  rasterizer state's forced sample count (DWM's make one, ignored) are
  logged and not done.
- Direct3D 10 programs (`d3d10.dll` wants the Direct3D 10 / 10.1 DDI
  versions, which `GetSupportedVersions` does not list yet).
