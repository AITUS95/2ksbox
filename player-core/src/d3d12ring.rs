//! A scanout ring on Windows (track M26): images on the GPU that a display
//! device whose frames are rendered there (d3dpt-vga's Direct3D executor)
//! fills itself, and that we sample as they are, so a frame never leaves
//! the GPU. The player makes them, as shared Direct3D 12 resources, because
//! that is the direction every driver supports: Vulkan imports a D3D12
//! resource's NT handle (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT`,
//! the executor on DXVK's device), where D3D12 opening Vulkan's exports is
//! not promised.
//!
//! The resources are BGRA8 typeless: the executor views them as
//! `B8G8R8A8_UNORM` and wgpu as the swapchain's format (sRGB on D3D12 by
//! default), so the bytes the guest drew pass through as they are.

use windows::Win32::Foundation::{CloseHandle, GENERIC_ALL, HANDLE};
use windows::Win32::Graphics::Direct3D12::*;
use windows::Win32::Graphics::Dxgi::Common::{DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_SAMPLE_DESC};
use wgpu::hal::api::Dx12;

/// One image of the ring: the texture we sample, and the NT handle the
/// device opens its own reference through (ours until the ring goes).
pub struct Slot {
    pub tex: wgpu::Texture,
    handle: HANDLE,
}

impl Slot {
    pub fn handle(&self) -> *mut std::ffi::c_void {
        self.handle.0
    }
}

impl Drop for Slot {
    fn drop(&mut self) {
        // the device's imports hold the memory; only our handle closes
        unsafe {
            let _ = CloseHandle(self.handle);
        }
    }
}

// SAFETY: a HANDLE is a kernel object reference, usable from any thread.
unsafe impl Send for Slot {}

/// `count` images of `w` x `h` on `device` (a Direct3D 12 one), sampled as
/// sRGB when `srgb` (the swapchain's choice, as for the uploaded texture).
pub fn alloc(device: &wgpu::Device, w: u32, h: u32, count: usize, srgb: bool) -> Result<Vec<Slot>, String> {
    let hal = unsafe { device.as_hal::<Dx12>() }.ok_or("not a Direct3D 12 device")?;
    let raw = hal.raw_device();
    let format = if srgb {
        wgpu::TextureFormat::Bgra8UnormSrgb
    } else {
        wgpu::TextureFormat::Bgra8Unorm
    };
    let size = wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 };
    let heap = D3D12_HEAP_PROPERTIES { Type: D3D12_HEAP_TYPE_DEFAULT, ..Default::default() };
    let desc = D3D12_RESOURCE_DESC {
        Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        Alignment: 0,
        Width: w as u64,
        Height: h,
        DepthOrArraySize: 1,
        MipLevels: 1,
        Format: DXGI_FORMAT_B8G8R8A8_TYPELESS,
        SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
        Layout: D3D12_TEXTURE_LAYOUT_UNKNOWN,
        // written by another API's queue while we may sample the one
        // before: no barriers between the two can be shared
        Flags: D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
    };
    let mut slots = Vec::with_capacity(count);
    for i in 0..count {
        let mut res: Option<ID3D12Resource> = None;
        unsafe {
            raw.CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_SHARED,
                &desc,
                D3D12_RESOURCE_STATE_COMMON,
                None,
                &mut res,
            )
        }
        .map_err(|e| format!("slot {i}: CreateCommittedResource: {e}"))?;
        let res = res.ok_or("CreateCommittedResource gave nothing")?;
        let handle = unsafe { raw.CreateSharedHandle(&res, None, GENERIC_ALL.0, None) }
            .map_err(|e| format!("slot {i}: CreateSharedHandle: {e}"))?;
        let hal_tex = unsafe {
            wgpu::hal::dx12::Device::texture_from_raw(res, format, wgpu::TextureDimension::D2, size, 1, 1)
        };
        let tex = unsafe {
            device.create_texture_from_hal::<Dx12>(
                hal_tex,
                &wgpu::TextureDescriptor {
                    label: Some("scanout ring"),
                    size,
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format,
                    // COPY_SRC: Ctrl+Alt+S reads the frame on show back
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
                    view_formats: &[],
                },
                // the D3D12 state it was made in, COMMON, which a texture
                // with simultaneous access decays to between any two uses
                wgpu::TextureUses::UNINITIALIZED,
            )
        };
        slots.push(Slot { tex, handle });
    }
    Ok(slots)
}
