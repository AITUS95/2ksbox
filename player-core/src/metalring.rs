//! A scanout ring on macOS (track M26): images on the GPU that a display
//! device whose frames are rendered there (d3dpt-vga's Direct3D executor,
//! DXVK on KosmicKrisp) fills itself, and that we sample as they are, so a
//! frame never leaves the GPU. The Windows ring is `d3d12ring.rs`.
//!
//! The player makes them, as one Metal heap per slot with a texture at its
//! start, and hands the device the heaps: KosmicKrisp's
//! `VK_EXT_external_memory_metal` imports a heap (not a texture or a
//! buffer), and the executor binds a `VkImage` at its offset 0, which
//! KosmicKrisp backs with a texture of its own made there from the image's
//! description. Ours and that one are the same pixels only while both
//! describe them alike: BGRA8, shader read and render target (KosmicKrisp's
//! for a copy's source and destination) and pixel-format views (its
//! `MUTABLE_FORMAT`, which the executor asks for). Without that last one
//! Metal compresses the texture and the two layouts differ (every pixel
//! wrong, measured on the M1 at 640x480 to 3840x2160). The heap is what
//! KosmicKrisp makes its own memory as: placement, shared storage, no
//! hazard tracking (it puts a buffer over the whole heap as well, which a
//! private heap refuses).

use objc2::rc::Retained;
use objc2::runtime::ProtocolObject;
use objc2_metal::{
    MTLCPUCacheMode, MTLDevice, MTLHazardTrackingMode, MTLHeap, MTLHeapDescriptor, MTLHeapType, MTLPixelFormat,
    MTLStorageMode, MTLTexture, MTLTextureDescriptor, MTLTextureType, MTLTextureUsage,
};
use wgpu::hal::api::Metal;

/// One image of the ring: the texture we sample, and the heap the device
/// imports its own reference to (ours until the ring goes).
pub struct Slot {
    pub tex: wgpu::Texture,
    heap: Retained<ProtocolObject<dyn MTLHeap>>,
}

impl Slot {
    /// The `id<MTLHeap>` for the device (embed API v17's handle).
    pub fn handle(&self) -> *mut std::ffi::c_void {
        Retained::as_ptr(&self.heap) as *mut std::ffi::c_void
    }
}

// SAFETY: Metal heaps are thread-safe objects; the heap is only retained
// here and released when the slot goes.
unsafe impl Send for Slot {}

/// `count` images of `w` x `h` on `device` (a Metal one), sampled as sRGB
/// when `srgb` (the swapchain's choice, as for the uploaded texture).
pub fn alloc(device: &wgpu::Device, w: u32, h: u32, count: usize, srgb: bool) -> Result<Vec<Slot>, String> {
    let hal = unsafe { device.as_hal::<Metal>() }.ok_or("not a Metal device")?;
    let raw = hal.raw_device();
    let wgpu_format = if srgb {
        wgpu::TextureFormat::Bgra8UnormSrgb
    } else {
        wgpu::TextureFormat::Bgra8Unorm
    };
    let desc = unsafe {
        MTLTextureDescriptor::texture2DDescriptorWithPixelFormat_width_height_mipmapped(
            MTLPixelFormat::BGRA8Unorm,
            w as usize,
            h as usize,
            false,
        )
    };
    desc.setUsage(MTLTextureUsage::ShaderRead | MTLTextureUsage::RenderTarget | MTLTextureUsage::PixelFormatView);
    desc.setStorageMode(MTLStorageMode::Shared);
    desc.setAllowGPUOptimizedContents(true);
    let size = raw.heapTextureSizeAndAlignWithDescriptor(&desc);
    let hd = MTLHeapDescriptor::new();
    hd.setType(MTLHeapType::Placement);
    hd.setStorageMode(MTLStorageMode::Shared);
    hd.setCpuCacheMode(MTLCPUCacheMode::DefaultCache);
    hd.setHazardTrackingMode(MTLHazardTrackingMode::Untracked);
    hd.setSize(size.size);
    let mut slots = Vec::with_capacity(count);
    for i in 0..count {
        let heap = raw.newHeapWithDescriptor(&hd).ok_or_else(|| format!("slot {i}: newHeapWithDescriptor gave nil"))?;
        desc.setResourceOptions(heap.resourceOptions());
        // SAFETY: offset 0 of a heap sized for this very descriptor
        let base = unsafe { heap.newTextureWithDescriptor_offset(&desc, 0) }
            .ok_or_else(|| format!("slot {i}: no texture on the heap"))?;
        let tex = if srgb {
            base.newTextureViewWithPixelFormat(MTLPixelFormat::BGRA8Unorm_sRGB)
                .ok_or_else(|| format!("slot {i}: no sRGB view"))?
        } else {
            base
        };
        let hal_tex = unsafe {
            wgpu::hal::metal::Device::texture_from_raw(
                tex,
                wgpu_format,
                MTLTextureType::Type2D,
                1,
                1,
                wgpu::hal::CopyExtent { width: w, height: h, depth: 1 },
                None,
            )
        };
        let tex = unsafe {
            device.create_texture_from_hal::<Metal>(
                hal_tex,
                &wgpu::TextureDescriptor {
                    label: Some("scanout ring"),
                    size: wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format: wgpu_format,
                    // COPY_SRC: Ctrl+Alt+S reads the frame on show back
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
                    view_formats: &[],
                },
                wgpu::TextureUses::UNINITIALIZED,
            )
        };
        slots.push(Slot { tex, heap });
    }
    Ok(slots)
}
