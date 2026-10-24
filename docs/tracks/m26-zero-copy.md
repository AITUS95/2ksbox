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

## State (2026-10-24)

The three loose ends of the Windows path (user: "go 1, 2 and 3"):

1. **98's driver on the ring.** D3DGAME9 in exclusive fullscreen at
   800x600 on 98 under TCG in `player-mitsuami`
   (`tools/win98-game-test.sh PLAYER=1`, which runs that player on
   Windows since this day, the winit one having no Windows build):
   `scanout ring of 3 at 800x600`, 1603 presents from a slot, 60 flips a
   second, 3 readbacks in 25 s, the scene right in the shots and
   Windows back to its desktop after. test.sh's Win98 checks ran here for
   the first time the same morning: `win98-dx9` passes every line;
   `win98-winetest` is 3 keys worse than `w98-driver.txt`, none of them
   the ring's (they run on a bare QEMU, no player, no ring): a depth
   texture made without `D3DUSAGE_DEPTHSTENCIL` fails with
   `D3DERR_DRIVERINTERNALERROR` in the guest before the host sees it
   (d3d8 `device.c:9939` tests 2D 2 and 9, d3d9 `device.c:8059`, after
   which Wine's test dereferences the texture it did not get and
   crashes). The tests only reach that since 2026-10-19's depth
   textures on the 9x driver (`d32b1212`), after the baseline (09-27).
   The rig fails 9939 too (`rig-98.txt`, 4 times). Open, not M26's.
2. **A release from the player** (embed API v18, patch 101's `held`,
   `d3dpt_exec_ring_held`): the device never writes a slot the player
   may still sample. The player holds a slot from its `on_scanout_slot`
   (under the BQL, before the device's next show) until a newer frame
   replaced it: at once if it was never drawn, else once the GPU has
   finished everything submitted when it went off show
   (`on_submitted_work_done`). The mask goes to the executor under
   `exec_lock` before each batch, and the device shows a slot under the
   same lock (asking the executor again there that the slot still holds
   the page), so no batch runs between a slot's show and its hold. Only
   the ask and the show: the first version also took the page's dirty
   snapshot under the lock, and on 98 under TCG the main loop then waited
   for the vCPU (the BQL let go) while holding the lock the vCPU's next
   flip wanted, a hang four flips in (gdb's stacks: the main loop in a
   condition wait, the vCPU on a mutex). A
   frame with every slot held (a player that takes nothing, its window
   occluded) is read back as without a ring.
3. **The guest cursor over a slot** (a relative mouse or a grab, where
   the host cursor cannot stand in): the player copies the slot on the
   GPU into a texture of its own and blends the sprite into that through
   a byte view, the CPU path's blend; the copy shows instead of the slot
   until the next frame, and a move republishes a slot's frame only while
   the sprite is drawn into it. The OpenGL ring's BGRA slots (Linux,
   macOS) get it too. Checked on Windows 11 at 3840x2160 under WHPX with
   no usb-tablet (the PS/2 mouse is relative, so the sprite is always
   drawn): the arrow over the Start menu's animation in the shots taken
   while a slot was on show, 490 slot presents in the run. Not on 98 or
   XP: there the device hides the hardware cursor while a DirectDraw flip
   chain has the screen (a game draws its own), so a slot frame there
   never has one; D3DGAME9's `-cursor` (the arrow kept in fullscreen)
   showed that, and the 98 run with it (relative mouse moving) is the
   release's check: 1601 slot presents, 60 frames/s, no stall.

`test.sh all` on the PC with all three: 68 passed, 1 failed
(`win98-winetest`, the same three keys as the morning's run before them,
item 1), 16 skipped.

Open: step 6; the Win98 depth-texture failures above (outside M26).

## State (2026-10-24, macOS)

Step 6's macOS half on the Air (user: "implement the zero-copy here"):
the same ring, the same executor code, a Metal heap per slot instead of
a Direct3D 12 resource.

- **What KosmicKrisp imports.** Its `VK_EXT_external_memory_metal`
  takes an `MTLHeap` and nothing else: an `MTLTexture` or `MTLBuffer`
  handle is `VK_ERROR_INVALID_EXTERNAL_HANDLE`, and
  `vkGetPhysicalDeviceImageFormatProperties2` answers only the heap type
  (importable and exportable). Its memory is all heaps: placement,
  shared storage, untracked hazards, default CPU cache; an import puts a
  buffer over the whole heap, so a private heap aborts in Metal's
  validation (`not compatible with the heap's mode`).
- **One texture, two descriptions.** A `VkImage` bound at offset 0 is a
  texture KosmicKrisp makes on the heap from the image's description
  (`kk_image_layout.c`: transfer source / sampled → shader read,
  transfer destination → render target, `MUTABLE_FORMAT` → pixel-format
  view, GPU-optimised contents). The player (`metalring.rs`) places its
  own texture there with the same description, and the two are the same
  pixels only because they agree. Measured with a probe (a pattern
  uploaded on Vulkan, read back through the player's texture) at
  640x480, 800x600, 1366x768, 1600x1200 and 3840x2160 on the M1: with
  `MUTABLE_FORMAT` / `PixelFormatView` every pixel right (Metal's heap
  size and Vulkan's requirement equal); without it every pixel wrong,
  Metal sizing the heap larger (lossless compression, which a
  pixel-format view turns off). The executor already asked for
  `MUTABLE_FORMAT` (the Direct3D 12 resource is typeless), so its
  `exec_vk_ring_image` differs only in the handle type and the import
  structure. The player's sRGB view of the slot is a Metal texture view.
- **DXVK enables the extension** (DXVK patch 19), as it enables
  `VK_KHR_external_memory_win32` for itself; the executor finds the
  Vulkan loader DXVK runs on among the process's images (`_dyld_*`),
  whatever path DXVK opened it by.
- **The player's slot numbers.** On macOS (and Linux) the OpenGL
  pass-through's ring is in the same table of imported slots, numbered 0
  to 2 like the scanout ring's, and an OpenGL title on d3dpt-vga can have
  both up. The scanout ring's slots now sit at `SCANOUT_BASE` (8) and
  up in the player (`qemu_vm.rs`), and `slot_bit` gives the OpenGL ring's
  none, so one ring's slot going off show never releases the other's.
  The device's numbering is unchanged.

D3DGAME9 in exclusive fullscreen at 800x600 on 98 under TCG in
`player-mitsuami` on the Air (`tools/win98-game-test.sh PLAYER=1`
`PLAYER_BIN=player-mitsuami/...`, the launcher's `base98-us` with its
Voodoo 2 in `EXTRA`, `PLAYER_PUBLISH_LOG=1`): `scanout ring: 3 images
of 800x600` on both DXVK devices, 1500 frames in 26.5 s at 60.0 fps,
1539 frames published from a slot, 4 readbacks in the run, the scene
right in the player's shots (taken from the slot on show) and the
desktop right after. A first run without the card in `EXTRA` lost the
device after 36 frames: 3dfx's login job put its "Voodoo 2 driver is
loading" box over the fullscreen game, then a Glide error box that held
the power button (the image's, not the ring's).

Windows 11 on Arm under HVF on the same ring, the user's machine
(the Start menu's animation "has echoes of previous frames"): a Direct3D
11 flip read back because the player held every slot (on the Mac its
GPU gives a slot back later than the PC's, so this happens several times
an animation) left the page's entry in `scans`, which named the slot of
DWM's frame from two or three flips before, and the page's next show
took that slot. The readback forgets the page's slots now
(`res_readback`), as the Direct3D 9 side's always did. A count added for
one run (the machine on a copy, logged in, the Start menu opened four
times): 13 such entries, 12 at a flip, each an old frame shown before.
Less, but still there (the user): a second cause. A page's newest
frame copied into a slot (VRAM older, on purpose) and its flip not shown
yet (the copy not done, the device's retry) lost that slot to the next
frame of another page: the player held the other two, the one free was
it. The page's entry went with it, and the flip found no slot and showed
VRAM's older frame. A count for the run: 31 times in eight Start menus.
`exec_ring_pick` now takes a mask of the slots holding another page's
newest frame and gives one only when no other is free, and the
overwrite puts that page's frame back into VRAM first (Direct3D 11:
owed, the display waits; Direct3D 9: read back at once). The same run
afterwards: 0; the user on the machine itself: "now it works". More
frames now come through VRAM than before (a page
whose slot was taken is read back), the ring's price on a host whose
player gives slots back late.
On the way: KosmicKrisp's `vkCmdSetEvent2` ignores its dependency (a
one-thread compute write), suspected of showing a slot before its copy
was done; a probe (40 copies of a 3840x2160 image, then the event) had
it set after the copies every time, 0.05 ms before the fence, so the
Direct3D 11 event query stays.

Smoothness at a large size (the user: the desktop "not smooth enough
when using a large resolution here"), measured on the user's machine
(a copy) at 2560x1676, the Start menu opened and closed twelve times,
DWM's own flips timed at the device's register:

| | flips/s in an animation | interval p90 | frames from a slot |
|---|---|---|---|
| ring off (readback) | 38.0 | 33 ms | 0 % |
| ring, 3 slots | 39.6 | 33 ms | 49 % |
| ring, 5 slots | 42.6 | 31 ms | 100 % |

The ring is not what holds the desktop back: DWM makes about 40 frames
a second at that size either way, its GPU work through DXVK and
KosmicKrisp a median 5 to 8 ms a frame and over 30 ms on heavy ones (the
Start menu's acrylic blur), which a flip's show waits for. Five slots
are the default since (`ring-slots`, 2 to 8): with three, half the
frames were read back (every slot held, or the free one another page's
newest), with five none. Tried and dropped on the way: DXVK's tiler mode
off (`DXVK_CONFIG="dxvk.tilerMode = False"`, which ignores an explicit
flush after a multisampled pass): no change; a flip that never waits for
its copy (retrying on the timer past the 20 tries): the guest queued
frames faster than the GPU finished them, p90 111 ms and one flip a
second late, so the wait stays as the GPU's back-pressure. The ring-off
run's own pace looked perfect only because that path republishes the
surface at every tick: compare flips, not publishes.
`PLAYER_PUBLISH_LOG` lines carry milliseconds since.

Open: nothing of step 6 (Linux below).

## State (2026-10-24, Linux)

Step 6's Linux half (user: "implement the zero-copy here on linux"),
alongside the macOS half above and on the same executor code. The player
makes the ring on its own Vulkan device (`player-core/src/vkring.rs`):
three `B8G8R8A8_UNORM` images with `MUTABLE_FORMAT`, each in a dedicated
allocation exported as an opaque fd. Each handle the device gets points
to a `d3dpt_exec_ring_fd` (`d3dpt/exec/d3dpt_exec.h`): the fd, the
allocation's memory type and size, and the device's and driver's UUIDs.
`exec_vk_ring_image`'s Linux branch refuses when DXVK runs on another GPU
than the player (an opaque fd is good only on the device that made it),
dups the fd and imports it with the exporter's memory type and size.
DXVK enabled only `VK_KHR_external_memory_win32`; DXVK patch 20 adds
`VK_KHR_external_memory_fd`. `dmabuf::create_device` opens the player's
device with that extension even where the dma-buf ones are missing.

Layouts, which Direct3D 12's simultaneous access made moot: a filled slot
rests in GENERAL (DXVK's d3d11 keeps a wrapped image there; `ring_copy`
ends there too), and wgpu is told at creation that the texture is already
in its sampled state, so it never transitions it from UNDEFINED (a driver
may discard the frame then). It then believes SHADER_READ_ONLY_OPTIMAL
where the image is GENERAL: the same memory for a sampled colour image on
Mesa's drivers. The slot offset (`SCANOUT_BASE`) keeps the OpenGL
pass-through's dma-buf ring apart, as the IOSurface one on macOS.

Checked on the Linux box (RX 9060 XT, RADV) in `player-mitsuami`:

- 98 under TCG, D3DGAME9 in exclusive fullscreen at 800x600
  (`tools/win98-game-test.sh PLAYER=1 PLAYER_BIN=…/player-mitsuami`): both
  DXVK devices import the ring, flips show their slots, 1500 frames at
  60 frames/s, 3 readbacks in the run, the executor at 0.03 ms a batch,
  the scene right in the shots.
- Windows 11 x64 under KVM (the launcher's `win11` machine on an
  overlay): DWM's Direct3D 11 flips go through the ring, which is made
  again as the guest follows the window (1920x1200, 2048x1368,
  1272x1314); the lock screen and the pointer right in the shots.

These runs were before the rebase onto the macOS half; the merged code
built (`build-d3dpt-exec.sh`, the player) but was not run again, and
`scripts/test.sh all` was not run (user: "no need for tests"), nor the
A/B against `PLAYER_ZERO_COPY=0` at 4K.

Seen on the way, not the ring's: Windows 11 turning the screen off shows
garbage (00-status "Open threads", M25's track doc).

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
6. **Other hosts**: Linux done 2026-10-24 (opaque fds from wgpu's Vulkan
   into DXVK's, `vkring.rs`, DXVK patch 20, above); macOS done 2026-10-24 (Metal heaps through
   KosmicKrisp's `VK_EXT_external_memory_metal`, above).

## Owns

The ring on both sides: the embed API's new calls (`embed/`), the
player's import (`player-core/src/` beside `dmabuf.rs` / `iosurface.rs`:
`d3d12ring.rs`, `metalring.rs`, `vkring.rs`, `gpu.rs`), DXVK patches 19 and 20, the executor's interop and copy (`d3dpt/exec/`, edited
minimally as a shared file), the device's slot path in
`d3dpt/hw/d3dpt_vga.c` (with M7), and this doc.

## Test loop

`D:\vms\win11-test` (`run-m25.sh`, `g11run.sh`, `PACE/`, `SETRES/` with
`3840 2160`), a freeze watchdog armed while it runs; `scripts/test.sh
all` before each commit that touches the device, the embed library or
the executor.
