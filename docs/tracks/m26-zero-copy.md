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

## State (2026-10-23)

Steps 1 to 4 for Direct3D 11's flips (Windows 11's DWM) work on the PC:
at 3840x2160 under WHPX every present during an animation is a ring
slot, the executor's share is back to 0.04 ms a batch, and the user:
"much smoother". What was found on the way:

- The player's refresh tick published the (older) surface between slot
  frames, and the screen flashed: a tick with no surface update since a
  slot leaves the slot on show (`scanout_shown`, `qemu_vm.rs`).
- `vram_of()` syncs a Direct3D 11 allocation (`exec_d11_sync`): looking
  the page's offset up read the buffer's last frame back, 33 MB a flip
  (the executor at 29 % of the time). The ring takes the offset alone
  (`exec_ddi_vram_offset`).
- A covered player window presents nothing, so a comparison of the
  player's CPU needs the window on screen.

Step 5, the same day (user: "and do the xp/98 direct3d 9 path as
well"): protocol v30's `SCANOUT`. The XP and 98 drivers read a target
back at EndScene, every frame, and at a Flip, a Lock and a blit; EndScene
no longer does, and a Flip sends `SCANOUT`, which with a ring copies the
target into the next slot on DXVK d3d9's own queue (`ring_copy`: d3d9 has
no way to wrap an outside image, so the executor records the barriers and
the copy itself under DXVK's queue lock) and owes VRAM; a Lock or a blit
still reads back, the untracked-writes merge (doc 15) unchanged. A page
the guest's CPU wrote since the copy (GDI text over a 3D frame, the
engine's software cursor) is shown from VRAM, read back with those
writes kept, not from its slot. `test.sh all`: 67 passed (XP's G9, G8,
F9 goldens on v30); D3DGAME9 in exclusive fullscreen at 1600x1200 on XP
under WHPX in the player: 75 flips/s (the mode's rate), 4 readbacks in
16 s, the picture right, the desktop right after.

Open: the guest cursor sprite is not composited into a slot (a grabbed
mouse); three slots and no release from the player (no tearing seen);
98's driver on the ring checked only by its build (test.sh's Win98
Direct3D checks need `build-winetests.sh` output on this PC); step 6.

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
