# Track M26: zero-copy scanout, the guest's frame never leaves the GPU

Opened 2026-10-23 (user: the desktop of Windows 11 on our adapter grows
choppier with the window on a 4K screen; after the dirty-log and
changed-row work, "it doesn't affect anything that changes the whole
screen, like playing a video in full screen. ... That's the real
optimization"). Branch `track/m26-zero-copy`, Windows host first.

## Why

What reached `main` on 2026-10-23 (QEMU patch 99, a pushed flip carrying
only its changed rows, the player's publish copying straight from the
surface) narrows the copies to what changed. A frame that changes
everywhere (the start menu opening, a full-screen video, a game) still
makes the whole trip, 33 MB at 3840x2160:

1. DXVK renders the frame on the host GPU;
2. the executor reads it back into the guest's VRAM (`RES_SCANOUT`,
   protocol v28) and the device waits for the GPU (`fb_settled`);
3. the device copies the page into its shadow (`fb_push_changed`);
4. the player copies the shadow into `front` (`on_refresh_done`);
5. wgpu copies `front` into its staging buffer (`write_texture`);
6. the staging buffer goes up to the GPU again.

About 130-170 MB of memory and bus traffic a frame, 8-10 GB/s at 60
frames a second; at 2560x1440 (all a Linux host here reaches) less than
half. The OpenGL pass-through skips all of it with a ring of GPU images
the player samples (dma-buf on Linux, IOSurface on macOS, doc 12 §4);
the Direct3D executor never had one.

## The shape (Windows first)

QEMU, the executor and the player are one process on one GPU.

- **The player** (wgpu on D3D12, `player-mitsuami`) creates a ring of
  three shared textures (`D3D12_HEAP_FLAG_SHARED`, BGRA8, the mode's
  size) and hands their NT handles down through the embed API at each
  mode switch.
- **The executor** imports them into DXVK's Vulkan device
  (`VK_KHR_external_memory_win32`, handle type `D3D12_RESOURCE`; DXVK
  enables the extension when the driver has it), reaching the device and
  the scanout's `VkImage` through DXVK's interop interfaces. At a flip it
  copies the scanout image into the next slot on the GPU instead of
  reading it back, and the slot is complete once that copy's fence is.
- **The device** (`d3dpt_vga.c`), for a flip the executor put in a slot,
  copies nothing: it tells the embed library which slot holds the frame
  (as `embed_fx_*` does for OpenGL), and the player shows that slot.
- **The player** samples the slot's texture directly: no CPU copy, no
  upload. The CRT chain reads it as it reads the uploaded texture.

What stays on the CPU path: anything the guest's CPU draws into VRAM
(GDI on 98 / XP, a Basic Display or VGA mode, a host without the
extension), the cursor (already separate), and the readback on demand:
QMP `screendump` and anything else that reads the scanout from VRAM
asks the executor for one.

## Steps

1. **The ring in the player and the embed API.** Shared D3D12 textures
   per mode, their handles to QEMU (embed API version bump), a slot
   shown through the existing `ext_slot` path; a test pattern written
   into a slot from the player's side proves the import and display
   with no executor change.
2. **The executor imports and fills a slot.** DXVK's `VkDevice` and the
   scanout image through its interop; the copy and its fence; the
   device names the slot instead of copying, the readback dropped for
   that flip. The A/B a knob (`-global d3dpt-vga.zero-copy=off`).
3. **Readback on demand.** `screendump` and every other VRAM reader of
   the scanout; `test.sh all` and the M25 tools unchanged.
4. **Measure.** `dwm-pace.ps1`, a full-screen video and the start menu at
   3840x2160 under WHPX against `zero-copy=off`; the executor's and
   the player's time a frame.
5. **The Direct3D 9 path** (`d3dpt_exec_ddi.cpp`, XP and 98 games on
   DXVK's d3d9) on the same ring.
6. **Other hosts**: Linux (dma-buf / opaque fd into wgpu's Vulkan, as the
   OpenGL ring), macOS (IOSurface or Metal shared events through
   KosmicKrisp).

## Owns

The ring on both sides: the embed API's new calls (`embed/`), the
player's import (`player-core/src/` beside `dmabuf.rs` / `iosurface.rs`,
`gpu.rs`), the executor's interop and copy (`d3dpt/exec/`, edited
minimally as a shared file), the device's slot path in
`d3dpt/hw/d3dpt_vga.c` (with M7), and this doc.

## Test loop

`D:\vms\win11-test` (`run-m25.sh`, `g11run.sh`, `PACE/`, `SETRES/` with
`3840 2160`), a freeze watchdog armed while it runs; `scripts/test.sh
all` before each commit that touches the device, the embed library or
the executor.
