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
   10_1 done 2026-10-20 (with Direct3D 10's own interfaces for Windows
   7), below.

## Test loop

The PC's Windows 11 test machine (`D:\vms\win11-test`, M20's tools,
outside the repo): `run-m25.sh` (M20's `run-m20.sh` on the overlay pair
`m25`, its own logs; `vgamem_mb=512` since 2026-10-19, so Windows lists
every size up to 3840x2160, `VGAMEM=128` the device's default; the
launcher's Windows 11 machines on our adapter get 512 too, the 32-bit
families keep the default),
`m25-test.sh` (installs the set staged in
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
- The user-mode log stops at 4000 lines a process (400,000 for a
  process `D3D11Trace` names, so a traced DWM still logs when the test
  runs): tracing DWM spends
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
dxgkrnl logs no event. Left for later (user, 2026-10-17); found and
claimed the same day ("WDDM 1.2 claimed", below).

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

Seen on the way: under the full-window load the guest twice hit a TDR
within ~30 s and restarted (below, fixed); and DWM composed on
d3d10level9, the Direct3D 9 driver, not as step 4 found it.

**Why DWM is on d3d10level9: the WDDM version** (user: "could it be
because of the wddm 1.1 / 1.2 difference?"). It is. A `D3D11CreateDevice`
is two opens of the adapter: a probe (`GetCaps` 130, the pipeline
levels; `GetSupportedVersions`) and the device's (the versions again,
the other caps, `CreateDevice`). DWM's first device (flags 0) is made on
the 11.1 interface (`0xb000f` is 11.15, `D3D11_1_DDI_INTERFACE_VERSION`,
not Windows 7's `0xb000b`); for its composition device (flags `0x11`
at the DDI) the runtime makes the second open, asks only the supported
versions and closes it, before any of the user-mode driver's caps, so
the refusal is on the kernel driver's side; after a third probe DWM
opens the Direct3D 9 driver. With `D3DPT_CLAIM_WDDM12` 1 every DWM,
the one at logon included, gets the composition device here (dxdiag:
WDDM 1.2, DDI 11.1, levels 10_0 to 9_1), and the other processes' 0x11
devices land on the 11.1 interface at 1.1 too. So step 4's DWM ran on
the session's 1.2-claiming builds; the commit that turned the claim off
(DWM's device removed whenever a program presents through a window's
swap chain, above) sent DWM back to d3d10level9, and its "either way"
was wrong. Rechecked with QEMU patch 94: at 1.2, `d3d11-probe present
12000` still makes DWM crash (Windows Error Reporting, a new DWM) and
both presented scenes read back black. Not it: DXGI's adapter cache
(`HKLM\SOFTWARE\Microsoft\DirectX\{...}`, which holds 9_1 and driver
version 0 for this adapter; set to 10_0, DWM still fell back).

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

**A game-like scene on Direct3D 10 / 11, 2026-10-17** (user: "create a
mini game-like scene using directx 10/11 features, same way you did for
directx 8/9"). `guest-tools/src/d3dgame11.c`, on d3dgame.h's scene: the
cubes in one instanced draw with a texture array, the wave grid made on
the GPU and captured by stream output, DrawAuto, geometry-shader
billboards, GenerateMips, occlusion predicates and an FP16 target
tonemapped by a fullscreen triangle with no vertex buffer, all at 10_0;
`-compare N` checks frame N against WARP (docs/testing.md). On this PC's
RTX 3090 natively: 9 pixels of 307,200 off. In the guest, first run: 351
off, 342 in one band: **the red marker drew**. DXVK's d3d11 stubs
`SetPredication` (it keeps the predicate and draws), so the executor now
waits for a predicate's answer when it is set and skips the predicated
operations itself (doc 14). Then: 9 off, x64 and the WoW64 32-bit build
alike, the occlusion counts equal to WARP's (182 and 0); on the desktop
through either swap chain model (screendumps), 34-37 frames/s windowed at
640x480 with DISCARD, ~58 with FLIP_SEQUENTIAL. A console window over a
program's own (a scheduled task's `cmd`) reads as the window missing in
a screendump: `G11\g11.ps1` runs it under `conhost --headless`.

**WDDM 1.2 claimed, 2026-10-17** (user, after a session on the desktop:
"everything is laggy and glitchy ... let's take care of the speed
first"). With DWM on d3d10level9 every Direct3D 11 window's frame
crossed to the Direct3D 9 driver through VRAM (the host's d3d11 object
read back, the d3d9 one uploaded again), and DWM's primary went to VRAM
for the display after that: two waits for the GPU a frame, on the one
executor thread, which was 55-60% busy with d3dgame11 windowed. The x64
kernel driver now claims WDDM 1.2 (and `SupportDirectFlip`, mandatory
at 1.2, whose user-mode check already answers no), DWM's composition
device is ours from logon, and the same run keeps the executor 19-24%
busy.

The window swap chain bug, narrowed down on the guest: the blt model
(DISCARD) never broke DWM, the **flip model** always did (d3dgame11
`-flip`, the probe's second window; the probe's "every program" was its
flip half). DWM's own driver log was spent long before (4000 lines a
process), so `D3D11Trace` now lifts a traced process's cap to 400,000;
the trace showed DWM opening the program's two buffers, a flush, and its
device gone. Windows' own ETW (DxgKrnl, DXGI, Direct3D 11 through
`logman`, `G11\etw.ps1`) has no event for it. **cdb attached to DWM**
(`DBG\dbg.ps1`: the EWDK's x64 debugger and the PC's downloaded symbols
on the command CD, the guest having no network; breakpoints on
`d3d11!CDevice::RemoveDevice` and its callers) caught it:
`NDXGI::CDevice::RenderCB` removing the device with
**`DXGI_ERROR_ACCESS_DENIED`**, under our `cmd_flush` in DWM's present.
dxgkrnl refused the submission: all three user-mode drivers listed every
allocation with `WriteOperation`, and a 1.2 kernel refuses a write to an
allocation the device opened read-only, as DWM opens a flip-model swap
chain's buffers (1.1 never checked). Now Direct3D 11's `patch_res` lists
another process's allocation as read and `patch_dst` (a copy's, an
update's, a blt's destination; a readback) as written; the Direct3D 9
driver lists any opened allocation as read (the host runs a submission
to its end before its fence, so the flag orders no wait of ours).

Checked at 1920x1200 (the user's mode): d3dgame11 through either model
with no DWM crash, `-compare 60` PASS (4 of 307,200 off, the occlusion
counts WARP's), `d3d11-probe present` 0 pixels off in both models (it is
DPI-aware now: Windows scaled its unaware window 175% and the screen was
read where the window was not), File Explorer whole, d3dgame9 ~35
frames/s, the maximized repaint 54-56 frames/s. d3dgame11 windowed: ~57
frames/s with FLIP_SEQUENTIAL, still ~37 with DISCARD (both as before;
below). The reference scenes are DPI-aware too (`d3dgame.h`): their
640x480 was shown at 1120x840, every pixel resampled. The user, on the
desktop by hand with this driver: "the desktop is actually feeling much
snappier, no visual glitches anymore and the demo games are slow but
don't have terrible frame pacing. the desktop is already feeling better
than the viogpudo one". `scripts/test.sh all` on the PC with these
changes: 67 passed, 16 skipped, none failed.

**Flips that hold nothing, 2026-10-18** (the user, watching d3dgame11
windowed with DISCARD: "it oscilates between fast and slow. like,
several frames fast, then several slow"). Not the VRAM crossings: a
per-frame trace (`d3dgame11 -trace N`) had runs of ~33 ms frames and
runs of ~17 ms, nearly all of each inside `Present`, the executor and
DWM mostly idle (`D3DPT_D11_PROF=1`). The kernel driver held a flip's
fence until the vertical blank (the 600 frames/s fix of M20), and fences
complete in order, so **every process's later submissions** waited behind
DWM's flip: a present just before DWM's took one blank, one just after
it two, and the phase between the two clocks drifted.

- `FlipOnVSyncWithNoWait` alone (the flip's fence done at once, the
  scanout and a CRTC_VSYNC at the blank) was bugcheck 0x113, subtype
  0x35, at the adapter's next stop (the restart ~9-14 s into each boot,
  or a reinstall): `VIDMM_FLIP_QUEUE_REFERENCES::ReleaseReference` on a
  destroyed allocation with 0x5a / 0x65 / 0xb3 flips still counted on
  source 0. Read in dxgmms1 with cdb: a DMA flip's queue entry is
  retired only when a vertical blank's cookie matches the address the
  entry holds, which none of ours ever did. Reporting the shown flip
  when the blank's interrupt went off did not help. The guest sat in
  recovery (QMP `send-key ret` twice gets past it).
- **`FlipOnVSyncMmIo`** (with `FlipOnVSyncWithNoWait`, x64 only): a flip
  on the blank is no DMA buffer (`Present` gets a NULL one) but
  `SetVidPnSourceAddress` with `FlipOnNextVSync`, called once the
  frame's rendering is done; the driver latches it and the blank shows it
  and reports **the very 64-bit `PrimaryAddress`** it was given
  (`shown_pa`), which is what the cookie is compared with. No fence
  waits for a blank any more; dxgkrnl paces the flips itself.

d3dgame11 windowed at 640x480, `-novsync`: DISCARD 70-115 frames/s
(30-45 before), FLIP_SEQUENTIAL ~400 (~57); with vsync, ~50-57, the
frame intervals one spread around 17-20 ms instead of two runs;
full-screen d3dgame9 still exactly 60. The driver reinstalled and the
boot-time restart stopped the adapter with no bugcheck after ~19,700
blanks of DWM's MMIO flips. A redirection bitmap preferring the aperture
(the CPU reads it, and VRAM is an uncached BAR under WHPX) measured no
gain (84-89 against 79-93) and was left out. `scripts/test.sh all` on
the PC: 67 passed, 16 skipped, none failed (run from the PowerShell
tool: `win.ps1` started from Git Bash gets `pwd` as `C:/...`, whose
colon breaks `PATH`, and the player's two checks fail with no output).

Also: `D3D11Trace` / `D3D11Exclude` never reached a 32-bit program (its
`RegGetValueA` read `WOW6432Node`); it asks for the 64-bit view now
(`RRF_SUBKEY_WOW6464KEY`, defined by hand: the EWDK's headers predate it).
Windows turning the screen off (`SetVidPnSourceVisibility 0`, the adapter
to D3) leaves the device's last frame up, which reads as a frozen guest;
the test machine's display and sleep timeouts are off (`powercfg`).

**The host's vertical blank, again, 2026-10-18** (the user: "first fix
vblank syncing with host"; M24's design, `screen::HostVBlank`, QEMU patch
90, `d3dpt-vga`'s `host-vblank`). Two faults:

- **The host's blanks had stopped coming.** The device's last summary
  read `16284 the host's, 20413 the timer's`, and not one came after: the
  frame traces above ran on the device's own 60 Hz timer. On the PC's new
  screen (an LG 4K at 60 Hz, the Meta Quest runtime installed)
  `D3DKMTWaitForVerticalBlankEvent` failed at once with
  `STATUS_GRAPHICS_PRESENT_OCCLUDED` (0xC01E0006), as did
  `DCompositionWaitForCompositorClock`, while `DwmFlush` kept DWM's
  16.7 ms. The player's thread took every failure for a monitor gone,
  reopened it, and said nothing. Now an occluded wait is answered by DWM's
  next composition (never sooner than three quarters of the screen's
  period after the last tick, as an idle DWM returns at once), and the
  player says which: `[display] the host screen's vertical blank is
  refused (occluded): DWM's composition stands in for it`, `... is back`,
  `... failed (0x...)`. The refusal comes and goes with the host's display
  (the next boot waited on the real blank again).
- **The modes said 60 Hz whatever the blank's rate.** QEMU patch 96 keeps
  the rate the player gives the guest's blank (`qemu_embed_set_refresh_rate`,
  72 Hz on a 144 Hz screen), `d3dpt-vga` reads it out of `HOST_MHZ`
  (register set v11), and the WDDM driver advertises every mode at it, a
  rational (59,997/1000 here; DWM's `rateRefresh` now 16.665 ms, 16.819
  before). Not through the console's UI info, as virtio-gpu has it: a
  device that takes UI info is one whose guest follows the window's size.
  The made-up EDID's range limits are 24-240 Hz, 15-255 kHz and 2550 MHz
  (50-75 Hz, 80 kHz and 170 MHz before: 72 Hz on a 144 Hz screen is
  86 kHz at 1920x1200 and 155 kHz at 3840x2160 with no blanking). A
  changed EDID is a new monitor to an installed guest, which starts it at
  1024x768 once (the user: no problem, for now).

On the way: `ChangeDisplaySettingsEx` with `CDS_UPDATEREGISTRY` from a
scheduled task fails (-1) for a monitor Windows has stored nothing for,
while the same change without the registry succeeds: the test's tool, not
the driver. `SETRES\setres.ps1` on the test machine (`size.txt` beside it,
"W H") now makes the change without the registry, then saves the active
configuration as the Settings app does (`SetDisplayConfig` with
`SDC_SAVE_TO_DATABASE`); the new monitor came back at 1920x1200 after a
restart.

Measured with the host's blank: DWM composes 223 of 300 frames in its
16-17 ms bucket (`guest-tools/viogpudo/dwm-pace.ps1`, 5th to 95th
percentile 14.6-18.0 ms, none doubled); d3dgame11 windowed with vsync,
flip model, 149 of 201 frames at 16.5-17.5 ms, 12 doubled. Timestamps at
both ends (removed) put the ±2 ms between the player and the device: the
host's blanks reached QEMU 13.8-19.5 ms apart (87 of 600 off by over
1 ms), the guest's interrupt 13.6-19.4. A blank waits for the main loop,
and the main loop for the BQL, which a vCPU holds through a whole batch:
the DOORBELL write runs the executor inside it, 1-3 ms for a DWM frame.
Releasing the BQL around `submit` alone (a prototype) bugchecked the guest
at boot twice: the batch needs the device's state guarded properly first.
`scripts/test.sh all` on the PC with these changes: 67 passed, 16 skipped,
none failed.

**Batches without the BQL, 2026-10-18** (user: "continue with driver
optimization"). Under a hardware accelerator `d3dpt-vga` now drops the
BQL around the executor's `submit` (its own `exec_lock` serialises the
executor's calls; TCG keeps the BQL). The executor reads only RAM (the
window, VRAM) and calls back only the log and `memory_region_set_dirty`;
a reset pauses the vCPUs and waits for this one to hold the BQL again.
The earlier prototype's bugcheck, and this one's first boots, were **0x119
subtype 1, fence 0 reported** (minidumps read with cdb in the guest; the
driver's fence reports logged): QEMU's re-entrancy guard is the device's,
not the thread's, so while one vCPU sat in the DOORBELL write another
vCPU's register read was refused (`Blocked re-entrant IO on
MemoryRegion: d3dpt-vga.regs at addr: 0xBC`, FENCE_DONE), read 0, and
the interrupt service reported fence 0. The doorbell lowers the guard
while the BQL is away and raises it again under it (the executor makes
no DMA). One run each on the same build, the BQL held / released: d3dgame11
windowed, flip model, unsynced **253 / 323 frames/s**, DISCARD 83 / 93;
with vsync, flip 296 / 311 of 380 frames at 15.5-17.8 ms (13 / 5
doubled), DISCARD 73 / 57 (47 / 56 doubled, within one run's noise).
**DWM's pace did not change** (`dwm-pace.ps1` p5-p95 14.4-18.1 / 14.4-18.2
ms): the ±2 ms on the guest's blank is not the batch under the BQL, as
guessed above.

**Where a DISCARD window's frame goes.** The kernel driver's Present of a
blt-model window copies the buffer into a **STAGING** standard allocation
(no host handle), not into a surface DWM samples: the driver claims no
GDI hardware acceleration, so every window's redirection surface is in
system memory, and Windows reads the staging copy with the CPU and DWM
uploads it again. Each present waits for a DWM composition (unsynced it
measures 16-17 ms in `Present`, ~60-90 frames/s). A copy on the host GPU
has no destination object to copy into, so `PRESENT_BLT` (a host-side
present copy, drafted) was dropped. What would change it: redirection
surfaces in video memory, `SupportSoftwareDeviceBitmaps` (CPU-visible
texture redirection bitmaps) or full GDI acceleration, both of which
change every window and not only Direct3D ones.

**`SupportSoftwareDeviceBitmaps`, tried the same day** (user: "do the
experiment"). Claimed, Windows 11 asks `GetStandardAllocationDriverData`
for `GDISURFACE` type 6 only (`TEXTURE_CPUVISIBLE`, A8R8G8B8: 1x1, the
taskbar's 1920x60, 1920x1200 surfaces, each window with its frame);
refused, it falls back to the system-memory bitmaps with the desktop
whole. Taken (an allocation in VRAM with a host handle, a render-target
texture), the desktop stayed whole and DWM never opened one through the
Direct3D 11 driver, and **a DISCARD present still copied into a 640x480
STAGING surface** while its window had a 646x515 bitmap of this kind:
Windows moves the frame into the bitmap on the CPU either way. So
CPU-visible bitmaps change where GDI draws (into VRAM, an uncached BAR
under WHPX, with no way for the host to see the writes) and nothing
about the blt-model present; reverted. What is left is GDI acceleration
proper (`SupportKernelModeCommandBuffer`, `DxgkDdiRenderKm`, type 1
`TEXTURE` bitmaps the CPU never touches).

**GDI acceleration, logged only, the same day** (user: "yes, go on").
`SupportKernelModeCommandBuffer` claimed, `DxgkDdiRenderKm` logging its
commands and emitting an empty DMA buffer. Windows 11 asks for
`STAGING_CPUVISIBLE` (type 2, 1920x1200 and the lookup table's 512x16),
`STAGING` (3, 1920x64) and `LOOKUPTABLE` (4, 512x16 A8: ClearType's
gamma); while the lookup table is refused it retries and stays on
software GDI (no RenderKm at all). With all three taken it asks for
**`TEXTURE` bitmaps** (type 1: 1920x1200, 1920x60, a window's 646x515,
1x1), and RenderKm brings ColorFills into them and SRCCOPY BitBlts from a
CPU-visible staging surface. Then **a DISCARD present copies straight
from the swap chain's buffer into its window's `TEXTURE` bitmap** (no
staging, no CPU copy), and d3dgame11 with vsync held 60.0 frames/s,
16.61 ms in each present, over 1800 frames (median ~20 ms with doubled
frames before). DWM opens the bitmaps through the Direct3D 11 driver
(`OpenResource` 646x515, 1920x1200, 1920x60), so what it samples is a
host object: a present's copy can be a copy on the host GPU. With
nothing drawn the wallpaper and the window's inside were black; DWM's
own title bars and taskbar drew. Reverted. Doing it for real: each
RenderKm operation run on the host into the bitmap's object (BitBlt with
its ROPs, ColorFill, AlphaBlend, StretchBlt, TransparentBlt, ClearType
blend against the lookup table), the staging surfaces uploaded from VRAM
where they are sources and read back where they are destinations, and
the present's Blt into a bitmap as a host copy.

**GDI acceleration, copies, fills and the present, 2026-10-18** (user:
"start with copies, fills and the present"; then "this branch is
experimental, you can leave it on"). On by default on this branch: the
x64 kernel driver claims `SupportKernelModeCommandBuffer` when the
executor is present, unless the service key's `GdiAcceleration` is 0
(`HKLM\SYSTEM\CurrentControlSet\Services\d3dptkmd`, read at
DriverEntry; a restart applies it). Then:

- **GDI surfaces** (`GetStandardAllocationDriverData`'s `GDISURFACE`):
  `TEXTURE` bitmaps (a host handle, a render-target texture there, not
  CPU-visible), the two staging types and ClearType's lookup table, all
  in VRAM (kind `D3DPT_ALLOC_GDI`, the type in `source`).
- **RenderKm** (`d3dpt_render_km`): a `PKT_REG` of the allocation list,
  as Render's, then a `PKT_GDI` per SRCCOPY BitBlt and PATCOPY ColorFill
  (their sub-rectangles; `MultipassOffset` the next command's offset
  when the DMA buffer is full); the submission hands each to the host as
  protocol v27's `GDI` record. The other operations are counted and
  logged every 5 s (`RenderKm in 5 s, not done yet`).
- **The host** (`exec_d11_gdi`): each side is the bitmap's Direct3D 11
  object (made at its first use; DWM's `RES_CREATE` then finds it) or
  VRAM (the staging surfaces; the host's pixels synced there first):
  `CopySubresourceRegion` (through a temporary within one bitmap when the
  rectangles overlap), `UpdateSubresource` from VRAM, a staged readback
  into VRAM, `memmove`; fills by `ClearView` with alpha 255 (GDI leaves a
  fill's alpha 0, and DWM composes the bitmap with it: a white fill came
  out black).
- **The present** into a `TEXTURE` bitmap is the same record, a copy on
  the host GPU (`gdi_present`), where a blt-model window's frame went
  through a staging surface the CPU read.
- **Paging**: a bitmap transferred out of VRAM is written back from its
  object first, and re-read after a transfer brings it in (`gdi_paged`,
  `D3DPT_GDI_VRAM`): GDI drawing on the CPU pages it through system
  memory (the logon's 1920x1200 bitmap went out and in once).

d3dgame11 windowed, DISCARD, with it on: with vsync 304 of 380 frames at
15.5-17.8 ms and 1 doubled (median 16.7 ms; 57 and 56 before), unsynced
**340 frames/s** (93 before; the flip model's 323), 300 host copies in
each 5 s, the scene right on the screen. **GDI's own content is not
there yet**: the wallpaper and a classic dialog's inside (`winver`) stay
black, Notepad's text area blank. For `winver` the host got one fill of
a 403x20 band (and, another boot, a white fill of the whole window) and
nothing else: no copy, no paging, and its bitmap's VRAM was black when
re-read (a destructive test, removed), so its drawing reaches the window
some way not found yet; ColorFill's PATINVERT (the caret) and the other
operations are not done. Off (`GdiAcceleration` = 0) nothing changes:
Windows asks for no GDI surface.

**GDI's own drawing, the same day.** GDI draws most of a window (text,
the wallpaper, a dialog's logo and background) with the CPU straight into
the redirection bitmap, and only into one that is CPU-visible: with the
bitmaps not CPU-visible that drawing was simply lost (dxgkrnl does not
page them for it), which was the black. Now the bitmaps are CPU-visible,
and the host keeps a **shadow** of each bitmap's VRAM: a Direct3D
submission naming a bitmap (DWM about to sample it) sends protocol v27's
`GDI` REFRESH first, and the host uploads the spans of VRAM that differ
from the shadow (row by row, rows of one span together). A bitmap's
first touch by the host (a GDI operation, a present) uploads its VRAM
and starts the shadow first; the host's fills and copies from VRAM go
into the bitmap's VRAM and shadow too; a copy between objects (a
present) stays in the object until the CPU draws over it. The refresh
costs under 1 ms of the executor every 5 s (`D3DPT_D11_PROF`, then token 63).
The desktop, `winver` (logo, text, the OK button) and a DISCARD window's
scene all draw.

**The price: a present into a CPU-visible bitmap is held.** d3dgame11
windowed, DISCARD, unsynced: ~85 frames/s in runs of ~3 ms and ~15-20 ms
presents (the user saw the fast/slow oscillation again), against ~430
frames/s with the bitmaps not CPU-visible (with or without the refresh:
it is the flag). ETW in the guest (`G11\etw2.ps1` on the test machine,
DxgKrnl's ids read from the manifest): the present is a plain Blt
(`bRedirectedPresent` false), and the game thread's per-frame Lock
(event 41) waits 13-17 ms (event 103) behind it, about one composition;
the title is set once a second, so it is not GDI drawing the caption.
What holds a GPU write into a CPU-visible bitmap is not found yet.

**Where the present really goes, and readbacks that wait, the same day**
(user: "continue with driver optimization"). ETW again (`G11\etw3.ps1`
on the test machine: every process's queue and DMA packets, the game's
Lock and DXGI's Present, merged over 120 ms of the steady state), and the
kernel driver logging a present's destination: with the bitmaps
CPU-visible **the present is not into the redirection bitmap at all**.
The game's window has its 646x515 `TEXTURE` bitmap, but its frame goes
into a 640x480 STAGING standard allocation, and the game thread waits
for that packet and locks the staging surface (read-only) for Windows'
CPU copy into the bitmap, as with `SupportSoftwareDeviceBitmaps`
(above): a CPU-visible bitmap is filled by the CPU whatever its type.
So the ~85 frames/s are the old system-memory path, and the fast host
copies of "GDI acceleration, copies, fills and the present" need
bitmaps that are not CPU-visible; how GDI's CPU drawing reaches those
is what is open.

The profile (`D3DPT_D11_PROF`, now with GDI records as 64 + their op and
a readback's two halves as 72 and 73) found the other cost: **a
readback waits for the host GPU**, 2-4 ms in `Map` (the GPU finishing the
frame) against ~1 ms copying into VRAM, and the user-mode driver read
the buffer back at every present: ~1 s of the executor's in each 5 s,
mostly DWM's 1920x1200 flips, every guest submission queued behind each.
Protocol v28:

- A **blt-model present** reads nothing back in the user-mode driver.
  The kernel driver's present into a redirection bitmap is a host copy
  (`gdi_present`); one through VRAM (a STAGING destination, as now)
  first sends v28's GDI `SYNC` for its source (the host's pixels into
  VRAM, nothing if VRAM has them: the executor's `newer`), and so does a
  paging transfer out of VRAM of a Direct3D 11 allocation.
- A **flip's readback** is `RES_SCANOUT`: the host copies the buffer
  into its staging twin and leaves the copy into VRAM owed. It is paid
  at the next batch if the GPU is done (`DO_NOT_WAIT`), before anything
  else reads that allocation's VRAM (`exec_d11_sync`, a later readback,
  `SYNC`), and by the display: `d3dpt-vga`'s refresh and its flip-push
  (the page copied at the flip's register write) call the new optional
  `d3dpt_exec_settle` under `exec_lock`, waiting for a running batch.
  The first version settled only at the refresh, with a try-lock: the
  flip-push copied the page before it was paid, and everything that moved
  on the desktop flickered with its own past (the user: "like reverb"),
  the frame of two or three flips before. An executor
  never asked to settle (the remote one under Wine) defers nothing. A
  guest CPU write to that VRAM (`RES_DIRTY`) drops what is owed. The
  first build recursed: the VRAM lookup syncs the allocation, which
  settled the same entry again (a stack overflow in the vCPU thread);
  an entry leaves the list before it is paid.

d3dgame11 windowed, one run each, the profile on: flip model unsynced
560 frames/s with the flicker (the try-locked settle), **~350-440 with
it fixed** (900 frames in 2.0-2.6 s, three runs; 323 before), with vsync
60.0 frames/s at 16.4 ms in each present (with the flicker); the user
then saw the desktop right ("it looks good now"); a readback's wait for the GPU 0.03-0.5 ms where it was 2-4;
the desktop draws, the clock current. DISCARD unchanged, as above (~80
unsynced, 47 with vsync): its present still goes through STAGING, now
with a `SYNC` (~1.1 ms, the GPU's frame) where the readback was.

**Bitmaps the CPU cannot see, again, the same day** (user: "go on").
The kernel driver gained the service key's `GdiCpuVisible` (REG_DWORD,
default 1; 0 makes the `TEXTURE` bitmaps not CPU-visible, read at
DriverEntry) and a census of RenderKm: every 5 s a `RenderKm in 5 s:`
line per operation, ROP and surface kinds (kind * 16 + GDI type: 0x51 a
redirection bitmap, 0x52 / 0x53 the staging ones, 0x54 the lookup
table, 0x40 a Direct3D allocation), with the first one's sizes and host
handles; and a line per GDI surface paged (the first 64).

- **CPU-visible** (the default): RenderKm carries almost nothing (two
  copies in 5 s, into ClearType's lookup table). GDI draws with the CPU,
  in effect software GDI on bitmaps in VRAM.
- **Not CPU-visible**: the wallpaper and `winver`'s inside black as
  before, and now Notepad's text area too. `winver` gets its 591x529
  `TEXTURE` bitmap and then **no RenderKm command and no paging at all**:
  its drawing goes nowhere the driver sees. Notepad's is a BitBlt from
  the bitmap into a 944x508 Direct3D allocation and back (GDI drawing on
  a Direct3D surface with the CPU), which the host copied from its object
  and not from the VRAM the CPU drew into. Windows 11 draws GDI with the
  CPU into a CPU-visible redirection surface and has no other route for
  it, so the bitmaps stay CPU-visible, and what is left is making the
  STAGING path fast.

So the blt-model present into a STAGING surface is now a host copy as
well (`gdi_present`, a Direct3D 11 buffer of 32 bits a pixel): the host
reads the frame straight into the staging surface's VRAM, where `SYNC`
put it into the buffer's own VRAM and this CPU copied it on through the
BAR. The scene is right (a screendump of the window); the rate is not
moved, ~80 frames/s unsynced and ~45 with vsync (one run each), so the
guest's copy was not what held the frame.

**Where the STAGING frame went, the same day** (user: "proceed"). An
ETW timeline (`etw3.ps1`, d3dgame11 DISCARD unsynced, median present
interval 11.8 ms) and the executor's profile showed two costs:

- **The display waited for the flip's readback under the BQL.** At every
  one of DWM's flips (~57/s) the flip-push and the refresh settled the
  owed readback with a blocking Map: 2.8 ms of GPU wait (slot 72) plus
  1 ms of copy on a vCPU or the main loop, holding the BQL, ~20% of the
  time with the whole guest stopped. `d3dpt_exec_settle` now takes
  `wait` and returns how many readbacks are still owed; the device tries
  it (`fb_settled`: a trylock of the executor's lock and a Map with
  `DO_NOT_WAIT`), and a flip-push that finds its page owed comes back
  from a 1 ms realtime timer (the newest page then; after 20 tries it
  waits), a refresh skips (after 3 it waits). Nothing stale is shown, so
  no flicker. The executor flushes once an owed copy is queued, so the
  GPU has it. Slot 72 went from 796 ms to 9 ms per 5 s; the rate from
  ~85 to ~97-119 frames/s (median interval 8.9 ms).
- **The game's CPU copy out of the STAGING surface read write-combined
  memory.** Between the game's Lock of the staging surface and its
  Present, 1.6-13 ms: segment 0 was not `CacheCoherent`, so Windows
  mapped the Lock write-combined and every read was uncached. Both
  segments are now `CacheCoherent` and a staging allocation `Cached`
  (the service key's `Cached` = 0 goes back; both are the host's RAM or
  the guest's, which the host reads and writes coherently, and WHPX does
  not trap VRAM: its `log_sync` marks it all dirty). The copy is
  0.1-0.4 ms.

DISCARD unsynced is now **~210-240 frames/s** (600 frames in 2.9 s,
1200 in 5.0 s; ~85 before), with vsync 58.3 (~45 before); the flip
model unsynced 515 (~350-440), with vsync 58.8. The scene and the
desktop are right (screendumps mid-run).

**GDI's other operations, 2026-10-19** (user: "proceed until you
implement all the remaining GDI operations"). RenderKm's every operation
now reaches the host (protocol v29, `d3dpt_gdi2`): a BitBlt or a
ColorFill with any ROP (the DDI's named ones mapped to their ROP3, P the
fill's colour), StretchBlt (COLORONCOLOR, MirrorX / MirrorY, and the mono
modes, whose shrink ANDs or ORs the pixels it drops), AlphaBlend
(constant alpha, premultiplied per-pixel alpha, both, stretched),
TransparentBlt (the key with or without its alpha) and ClearTypeBlend
(the DDI's own formula, with the lookup table's gamma row or without).
The kernel driver turns each into a `PKT_GDI2` (`gdi_render2`, its
allocations' places filled at submit time as the copies' are) and claims
`SupportAllBltRops`, `SupportMirrorStretchBlt` and
`SupportMonoStretchBltModes`; the host does them on the CPU (`gdi_ex`):
the destination's sub-rectangles and the source rectangle read (an
object's through a staging copy, VRAM's in place), computed, and written
back as a copy's are (the object, its VRAM and shadow). A ROP into a
redirection bitmap comes out opaque, as a fill does.

Windows 11 does not send them. `gditest.exe` (new: each operation on a
window against the same on a DIB section) is 34 of 34 exact on the test
machine, and the census shows why: GDI drew all of it with the CPU, and
RenderKm carried one copy into ClearType's lookup table. With
`GdiCpuVisible=0` every case fails as `winver` did, nothing coming through
RenderKm. So the host's half is proved without a guest:
`tools/d3dpt-gdi-test.cpp` (`d3dpt-gdi` in `test.sh`) feeds the records
to the executor, into plain VRAM and into a redirection bitmap's object,
against this PC's GDI on DIB sections and, where GDI has no call (a
mirrored stretch, the mono modes, ClearType's blend), the DDI's formula:
90 checks, all exact but AlphaBlend's rounding (within 1). It found a
vertical mirror applied to AlphaBlend whenever its constant alpha had
bit 1 set (the flags share a word). Windows 7's GDI does use RenderKm,
but its WDDM 1.1 build has no GDI acceleration (track M18's).

**GDI-compatible textures, and GDI acceleration off again, the same
day** (user: "just continue"). Opening common windows one by one on the
test machine (Notepad, `winver`, Character Map, a console in Terminal):
all drew but **Notepad's text area, black with the default settings**,
not only with `GdiCpuVisible=0`. Its rich-edit control draws with GDI into
a Direct3D 11 texture made `GDI_COMPATIBLE` (`IDXGISurface1::GetDC`; the
runtime keeps the flag from the driver, `d3d10umddi.h` reserves it, and
makes the texture shared: Notepad's 944x508 `misc 0x2`). `gditest` gained
three cases that do the same (a clear, GDI's fill and opaque text, GDI
reading the texture back, a Direct3D copy, a GDI invert across it) and
failed them: the texture kept Direct3D's clear, and GDI's own read gave
nothing (the DIB's zeros). The only RenderKm the census saw was the invert
(a ColorFill DSTINVERT on the Direct3D allocation); `GetDC` reaches the
user-mode driver only as DXGI's `ResolveSharedResource`, and nothing marks
`ReleaseDC`. With GDI acceleration on, GDI opens the texture itself and
draws on it with the CPU where RenderKm does not, and a shared Direct3D
allocation is not CPU-visible (Windows 7's dxgmms1 takes a shared
CPU-visible allocation only in an aperture segment, M18 step 7). Made
CPU-visible on the x64 build, Windows 11 refused it as well: every shared
render target's `pfnAllocateCb` failed `E_INVALIDARG` and the taskbar
went (reverted). An aperture segment would need the host to reach the
guest's aperture pages, which it cannot.

With **GDI acceleration off** Notepad draws, and `gditest` is 37 of 37
exact: Windows then moves a GDI-compatible texture's pixels itself. The
frame rates do not depend on it any more (the readback and cached-staging
work above helps either way): d3dgame11 windowed, unsynced, DISCARD 1200
frames in 4.8 s (~250 frames/s; ~210-240 with it on) and the flip model
1200 in 2.5 s (~470; 515), with vsync 58.2 and 58.8 (58.3, 58.8). So the
kernel driver's default is **off** (`GdiAcceleration` absent or 0; 1 turns
it on, a restart applies it), as "Open" asked before the track merges;
the host's GDI operations and `d3dpt-gdi` stay.

**Feature level 10_1, and Direct3D 10 programs** (2026-10-20, user: "Do
d3d10 and feature level 10_1"). `GetCaps`' 3D pipeline level claims 10_1
beside 10_0: shader model 4.1, cube arrays and a blend state per target,
which the driver already passed through (the host's DXVK device is 11_0;
no wire change). On Windows 11, `D3D11CreateDevice` with every level now
gives **10_1** on our adapter, and DWM's device is 10_1 with the desktop
composing as before.

Direct3D 10 programs on Windows 8 and later need nothing else:
`d3d10.dll` and `d3d10_1.dll` run on Direct3D 11's runtime there, and
every device the probe made through them reached the driver through the
11.1 interface (`0xb000f`). (The "Open" note that `d3d10.dll` wanted the
10 / 10.1 DDI versions was wrong for Windows 11.) Windows 7's runtime is
another: it opens a driver's own Direct3D 10.0 and 10.1 interfaces. So
`GetSupportedVersions` lists them (Windows 7's and those of Vista's DXGI
1.1 update), and `d10_dev.c` gives each its device table: the 11
entries, but for the dozen whose arguments are older (a resource without
ByteStride, 10.0's cube view and blend state, a depth view without
flags, stream output with one stride and no streams, render targets with
no UAVs), which convert to 11's and call the 11 entry. 32-bit Windows 7
gets `d3dpt11.dll` now too (the INF's x86 section, the guest disc's
`WDDM\`).

`d3d11-probe` grew the checks: the scene at 10_1 with shader model 4.1
(a Gather) on ours and WARP, and through Direct3D 10's API (`d3d10.dll`,
and `d3d10_1.dll` at 10_1 and 10_0, with 10.0's blend state and
R8G8B8A8, since `d3d10.dll` has no B8G8R8A8 target) against WARP's 10_0
frame. On the PC's Windows 11 all parts **PASS, 0 pixels off**, `present`
too. On 32-bit Windows 7 (the M18 machine, the probe built x86 with the
SDK's `d3dcompiler_47.dll` beside it, on a stick hot-plugged by QMP):
**PASS, 0 pixels off** in every part, through Windows 7's 11.0 interface
(`0xb000b`), 10.0 (`0xa0009`: `d3d10.dll`, and `d3d10_1.dll` at 10_0)
and 10.1 (`0xa000a`), each converter called. Its "every level" line is
`E_INVALIDARG` there: Windows 7's runtime refuses 11_1 in the list asked
for.

**Windows 7's DWM stays on Direct3D 9's driver** (through
`d3d10level9`): it never opens `d3dpt11.dll`, while `dxdiag` does (DDI
10.1, its device made through the 10.1 interface). `dxdiag` rates the
32-bit kernel driver **WDDM 1.0**, and Windows 7's DWM puts a 1.0 driver
on 10level9, as Windows 11's did at 1.1 (step 4). The likeliest reason
for 1.0 is the missing GDI acceleration, which WDDM 1.1 requires (the
x64 driver's RenderKm, off by default and not in the 32-bit build); not
checked. `win7-aero-test.sh`'s `dwm` verdict accepts either DLL and says
which.

## The packages, and the driver for Windows 11 on Arm

**Every package carries DXVK's d3d11 and dxgi** beside its d3d9
(2026-10-19): the Windows zip and MSIX as `dxvk_d3d11.dll` and
`dxvk_dxgi.dll`, the Linux tarball and the Flatpak (which is the tarball's
tree) as `lib/2ksbox/libdxvk_d3d11.so.0` and `libdxvk_dxgi.so.0`, the Mac
app as `lib/2ksbox/libdxvk_d3d11.0.dylib` and `libdxvk_dxgi.0.dylib`
(App Store and community builds; not the Intel one, which has no DXVK).
The packaged player names them to the executor as it names the d3d9
(`player-core/src/companions.rs`: `D3DPT_DXVK_D3D11_LIB`,
`D3DPT_DXVK_DXGI_LIB`, printed by `--companions` as `dxvk-d3d11` and
`dxvk-dxgi`), and each packager checks that the staged player found the
staged copies, as it does for the d3d9. Where the environment names only
the d3d9 (a harness), the executor looks beside it, or in DXVK's build
tree's `src/d3d11/` and `src/dxgi/` when the d3d9 is in its `src/d3d9/`.
`build.sh` configures a Linux or Mac `build/dxvk` from before d3d11 was
enabled afresh. The executor (`d3dpt_exec_d11.cpp` with the rest) compiles
for Linux with g++ against DXVK's native headers, warning-free and with no
unresolved symbol (checked in WSL's Ubuntu on the PC), and its Wine build
with mingw. **On a Linux host Windows 11 on the driver works as on the
Windows one** (the user, by hand, 2026-10-20: under KVM, `d3dgame8`,
`d3dgame9` and `d3dgame11` run as on the Windows host); the macOS build and a run
there are untested.

**The driver builds for Windows 11 on Arm** into `build/wddm/arm64`
(`build-wddm.cmd`, the `wddm` stage), and the guest-tools ISO carries it
as `WDDMARM\` with the same INF (now with an `NTarm64` decoration) and
installer:

- `d3dptkmd.sys` ARM64, from the EWDK as the others. The only x86-only
  line in it was the debug console's port write before the register BAR
  is mapped; on Arm those lines are dropped.
- `d3dptumd.dll` and `d3dpt11.dll` **ARM64X**: one DLL with an ARM64 half
  for Arm processes (DWM) and an ARM64EC half for the x64 programs Windows
  emulates, under the one `UserModeDriverName`. VS 2019, the EWDK's, has
  no ARM64EC, so they are VS 2022's (17.8 or later, its ARM64EC build
  tools, `BuildAsX`) on its own Windows SDK, with the EWDK's display
  headers copied beside them (`build/wddm/arm64/wdkinc`: the SDK has no
  `d3d10umddi.h`, and the EWDK's include path predates ARM64EC). Without
  VS 2022 the Arm set is skipped with a note.
- `d3dptumd32.dll` and `d3dpt11_32.dll`, the x86 builds, for the x86
  programs Windows on Arm emulates under WoW64 (`UserModeDriverNameWow`,
  SysWOW64), as on x64.
- All three Arm builds compile with `/volatile:ms`: MSVC's Arm default
  orders no volatile access, and the drivers start the device with a
  register write after their writes to memory.

`windows-drivers.sh` publishes and fetches the Arm set with the rest.

**The launcher offers it, experimental** (2026-10-19, user: "a
warning-gated experimental driver option that needs test mode", as the
drivers disc's experimental viogpudo is). The aarch64 QEMU builds
`d3dpt-vga` and its executor loader too (`d3dpt/hw/meson.build`,
`arm_system_ss`; the device has nothing x86 in it, and the virt board's
PCI selects VGA_PCI, so the VGA core). A Windows 11 machine on Arm with
`video = "d3dpt"` gets `ramfb` (the firmware's screen) then
`d3dpt-vga,irq=on,vgamem_mb=512` in place of virtio-gpu (`arm_args`,
checked with `launcherx --print-args` on a scratch bundle). In the form
our adapter on Windows 11, either architecture, is "2ksbox adapter
(experimental)"; picking it asks first in a warning alert
(`Form::video_question`: early build, test mode, Secure Boot off, the
watermark, anti-cheat; Cancel the default), and while it is the pick a
warning under the picker says test mode (`video_warning`); the note names
`WDDMARM\` or `WDDM64\`. **Not run**: the aarch64 QEMU is built only on an
Arm host, so neither its compile with the device nor a guest on it has
been tried; the Mac is next. The executor there is DXVK on KosmicKrisp,
whose Direct3D 11 side has not run either.

## Open

- **A present into a CPU-visible redirection bitmap is a STAGING copy**
  the CPU moves ("Where the present really goes"); it is fast now
  ("Where the STAGING frame went", ~240 frames/s against the flip
  model's ~515). Bitmaps that are not CPU-visible lose GDI's drawing
  with no RenderKm to bring it ("Bitmaps the CPU cannot see, again").
  Every RenderKm operation is done since 2026-10-19 ("GDI's other
  operations"; Windows 11 sends next to none of them). GDI acceleration
  is off by default again since the same day ("GDI-compatible
  textures"): with it on, a Direct3D 11 GDI-compatible texture loses
  GDI's drawing (Notepad's text area black), which needs a shared
  allocation GDI can reach with the CPU; turning it on by default again
  waits for that.
- **A blt-model window** without it waits for DWM's composition through
  Windows' system-memory redirection (above); CPU-visible texture
  bitmaps do not change that.
- DISCARD with vsync without it still spreads (median ~19-20 ms, ~50 of
  380 frames doubled; the flip model holds 17); the same path.
- The vertical blank's ±2 ms between the player and the device: not the
  executor's batch (above); where it comes from is open.
- The screen Windows turned off still shows the last frame (above); the
  device could show black while the source is invisible.

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
- ~~**DWM on d3d10level9 at WDDM 1.1**~~, ~~**claiming WDDM 1.2**~~
  (both done 2026-10-17, above).
- ~~Explorer's missing text, icons and clipped shadows~~ the user saw by
  hand (M20's): gone on the user's own look at the desktop on the 10_0
  device (2026-10-19), most likely Direct3D 9 on WDDM 1.0 / 1.1, which
  DWM composed on before.
- Multisampling (above).
- ~~The packages~~ (done 2026-10-19, "The packages, and the driver for
  Windows 11 on Arm"); a Linux host runs it (KVM, `d3dgame8` / `9` / `11`; the user, 2026-10-20); the
  Mac build of the executor and a run there remain.
- **Windows 11 on Arm**: the driver builds, the aarch64 QEMU has the
  device and the launcher offers it (same section); the Mac's build and
  a run there remain.
- A scaled DXGI Blt, a Blt1 of part of its source, a Blt into a surface
  only Direct3D 9 knows, the video processor's Blt, ClearView of a view
  that is not a render target's, a blend state's logic op and a
  rasterizer state's forced sample count (DWM's make one, ignored) are
  logged and not done.
- ~~Direct3D 10 programs~~ (done 2026-10-20, "Feature level 10_1, and
  Direct3D 10 programs": through the 11.1 interface on Windows 11, the
  10.0 / 10.1 ones on Windows 7). Windows 7's DWM on `d3dpt11.dll`
  waits for the 32-bit kernel driver to be WDDM 1.1 (same section).
- **Depth textures on the Direct3D 9 driver** (2026-10-19, the user: a
  Direct3D 9 game on Windows 11 said "hardware shadows are not
  supported"). The WDDM Direct3D 9 driver claimed D16 / D24X8 / D24S8 as
  depth buffers only, so `CheckDeviceFormat(D3DUSAGE_DEPTHSTENCIL,
  D3DRTYPE_TEXTURE)` failed. It now claims them as textures too (the
  core's format list, `core_caps.c`, so the XP and 9x drivers as well:
  their surface with `DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER` reaches the host
  with both caps), and the executor makes a resource that is
  both texture and depth buffer a `D3DUSAGE_DEPTHSTENCIL` texture, level 0
  the depth buffer, sampled with DXVK's depth compare (NVIDIA's shadow
  maps), never uploaded from VRAM; a host that refuses one gets the plain
  depth surface. No new wire: the host reads the caps it already had.
  The user ran the game's hardware shadows on Windows 11 and on XP
  (2026-10-20); open: the vendors' raw-depth FOURCCs
  (INTZ, DF24, DF16; not Direct3D 9's own formats), a probe of our own.
- **Unigine Heaven in Direct3D 9** (2026-10-19, on an overlay of the
  user's machine, High, 1920x1200 full screen). With the depth textures
  above it loaded and crashed before its first frame (an access violation
  in `Unigine_x86.dll`); its log said every texture after a point failed
  "out of video memory", and none of those calls reached the user-mode
  driver. `D3D9MEM.EXE` showed why: Direct3D 9 counts its textures
  against the dedicated memory plus the shared system memory the
  adapter reports, and refuses past that itself; at a 64 MiB aperture
  that was 512 MiB (127 default-pool textures of 4 MiB). The x64 kernel
  driver's aperture is 1 GiB now: the budget is 1472 MiB, 367 such
  textures are made (three times the VRAM segment; the memory manager
  evicts them to system memory), and Heaven runs with its shadows, at 1
  to 3 frames a second (the executor busy 28% of the time; open).
  Also open: each texture still takes its size of the process's address
  space (CPU-visible allocations), so a 32-bit game has ~1.9 GiB for
  textures and its own memory together; and the XP and 9x drivers'
  memory has no paging at all.
- `HKLM\SOFTWARE\2ksbox` `D3D9Trace` (executables, as `D3D11Trace`)
  lifts the Direct3D 9 driver's 4000-line log cap for those processes.
