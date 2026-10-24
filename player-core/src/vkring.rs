//! A scanout ring on Linux (track M26), as `d3d12ring.rs` is on Windows:
//! images on the GPU that d3dpt-vga's Direct3D executor fills itself and
//! that we sample as they are, so a frame never leaves the GPU. The player
//! makes them on its own Vulkan device and exports their memory as opaque
//! fds, which the executor imports on DXVK's device (DXVK patch 20 enables
//! `VK_KHR_external_memory_fd` there). Both sides create the image with the
//! same parameters (`d3dpt_exec_ring_fd` in `d3dpt/exec/d3dpt_exec.h`), as
//! an opaque fd's import requires.
//!
//! The images are `B8G8R8A8_UNORM` with `MUTABLE_FORMAT`: the executor
//! copies the guest's bytes in as they are and wgpu views them as the
//! swapchain's format (sRGB or not), as for the uploaded texture.
//!
//! Layouts: the executor leaves a filled image in GENERAL, where wgpu
//! believes a sampled texture to be in SHADER_READ_ONLY_OPTIMAL (it is
//! made with that as its state, so wgpu never transitions it from
//! UNDEFINED, which a driver may take as leave to discard the frame). The
//! two are the same memory layout for a sampled colour image on the
//! drivers this runs on (Mesa's), as Direct3D 12's common state is on
//! Windows.

use ash::vk;
use std::ffi::{c_void, CStr};
use wgpu::hal::api::Vulkan;

/// `d3dpt_exec_ring_fd` (`d3dpt/exec/d3dpt_exec.h`): what each handle the
/// device gets points to.
#[repr(C)]
pub struct RingFd {
    pub fd: i32,
    pub memory_type: u32,
    pub size: u64,
    pub device_uuid: [u8; 16],
    pub driver_uuid: [u8; 16],
}

/// One image of the ring: the texture we sample, and the description the
/// device imports it through (its fd ours until the ring goes).
pub struct Slot {
    pub tex: wgpu::Texture,
    info: Box<RingFd>,
}

impl Slot {
    pub fn handle(&self) -> *mut c_void {
        &*self.info as *const RingFd as *mut c_void
    }
}

impl Drop for Slot {
    fn drop(&mut self) {
        // the executor's imports and our texture hold the memory; only our
        // fd closes here
        unsafe { libc::close(self.info.fd) };
    }
}

const EXT_FD: &CStr = c"VK_KHR_external_memory_fd";

/// Whether `device` can make the ring: a Vulkan device opened with
/// `VK_KHR_external_memory_fd` (`dmabuf::create_device` asks for it).
pub fn available(device: &wgpu::Device) -> bool {
    unsafe { device.as_hal::<Vulkan>() }.is_some_and(|hal| hal.enabled_device_extensions().contains(&EXT_FD))
}

/// `count` images of `w` x `h` on `device`, sampled as sRGB when `srgb`
/// (the swapchain's choice, as for the uploaded texture).
pub fn alloc(device: &wgpu::Device, w: u32, h: u32, count: usize, srgb: bool) -> Result<Vec<Slot>, String> {
    let hal = unsafe { device.as_hal::<Vulkan>() }.ok_or("not a Vulkan device")?;
    if !hal.enabled_device_extensions().contains(&EXT_FD) {
        return Err("the device has no VK_KHR_external_memory_fd".into());
    }
    let raw = hal.raw_device().clone();
    let instance = hal.shared_instance().raw_instance().clone();
    let phd = hal.raw_physical_device();
    // the guard goes before wgpu takes the textures (it holds the device)
    drop(hal);
    let fd_loader = ash::khr::external_memory_fd::Device::new(&instance, &raw);

    let mut id = vk::PhysicalDeviceIDProperties::default();
    let mut props = vk::PhysicalDeviceProperties2::default().push_next(&mut id);
    unsafe { instance.get_physical_device_properties2(phd, &mut props) };
    let (device_uuid, driver_uuid) = (id.device_uuid, id.driver_uuid);
    let mem_props = unsafe { instance.get_physical_device_memory_properties(phd) };

    let format = if srgb {
        wgpu::TextureFormat::Bgra8UnormSrgb
    } else {
        wgpu::TextureFormat::Bgra8Unorm
    };
    let usage = vk::ImageUsageFlags::TRANSFER_SRC | vk::ImageUsageFlags::TRANSFER_DST | vk::ImageUsageFlags::SAMPLED;
    let handle_type = vk::ExternalMemoryHandleTypeFlags::OPAQUE_FD;

    // exportable at all, as this image
    let mut ext_info = vk::PhysicalDeviceExternalImageFormatInfo::default().handle_type(handle_type);
    let fmt_info = vk::PhysicalDeviceImageFormatInfo2::default()
        .format(vk::Format::B8G8R8A8_UNORM)
        .ty(vk::ImageType::TYPE_2D)
        .tiling(vk::ImageTiling::OPTIMAL)
        .usage(usage)
        .flags(vk::ImageCreateFlags::MUTABLE_FORMAT)
        .push_next(&mut ext_info);
    let mut ext_props = vk::ExternalImageFormatProperties::default();
    let mut fmt_props = vk::ImageFormatProperties2::default().push_next(&mut ext_props);
    unsafe { instance.get_physical_device_image_format_properties2(phd, &fmt_info, &mut fmt_props) }
        .map_err(|e| format!("the image's format with an opaque fd: {e}"))?;
    if !ext_props
        .external_memory_properties
        .external_memory_features
        .contains(vk::ExternalMemoryFeatureFlags::EXPORTABLE)
    {
        return Err("the image cannot be exported as an opaque fd".into());
    }

    let size = wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 };
    let mut slots = Vec::with_capacity(count);
    for i in 0..count {
        let mut ext_mem = vk::ExternalMemoryImageCreateInfo::default().handle_types(handle_type);
        let info = vk::ImageCreateInfo::default()
            .flags(vk::ImageCreateFlags::MUTABLE_FORMAT)
            .image_type(vk::ImageType::TYPE_2D)
            .format(vk::Format::B8G8R8A8_UNORM)
            .extent(vk::Extent3D { width: w, height: h, depth: 1 })
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL)
            .usage(usage)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .initial_layout(vk::ImageLayout::UNDEFINED)
            .push_next(&mut ext_mem);
        let image = unsafe { raw.create_image(&info, None) }.map_err(|e| format!("slot {i}: vkCreateImage: {e}"))?;
        let req = unsafe { raw.get_image_memory_requirements(image) };
        // device-local, as the executor writes it and we sample it there
        let mem_type = (0..mem_props.memory_type_count)
            .filter(|t| req.memory_type_bits & (1 << t) != 0)
            .min_by_key(|t| {
                let flags = mem_props.memory_types[*t as usize].property_flags;
                !flags.contains(vk::MemoryPropertyFlags::DEVICE_LOCAL)
            });
        let Some(mem_type) = mem_type else {
            unsafe { raw.destroy_image(image, None) };
            return Err(format!("slot {i}: no memory type for the image"));
        };
        let mut export = vk::ExportMemoryAllocateInfo::default().handle_types(handle_type);
        let mut dedicated = vk::MemoryDedicatedAllocateInfo::default().image(image);
        let alloc = vk::MemoryAllocateInfo::default()
            .allocation_size(req.size)
            .memory_type_index(mem_type)
            .push_next(&mut export)
            .push_next(&mut dedicated);
        let memory = match unsafe { raw.allocate_memory(&alloc, None) } {
            Ok(m) => m,
            Err(e) => {
                unsafe { raw.destroy_image(image, None) };
                return Err(format!("slot {i}: vkAllocateMemory: {e}"));
            }
        };
        let fd = unsafe { raw.bind_image_memory(image, memory, 0) }
            .map_err(|e| format!("vkBindImageMemory: {e}"))
            .and_then(|()| {
                let get = vk::MemoryGetFdInfoKHR::default().memory(memory).handle_type(handle_type);
                unsafe { fd_loader.get_memory_fd(&get) }.map_err(|e| format!("vkGetMemoryFdKHR: {e}"))
            });
        let fd = match fd {
            Ok(fd) => fd,
            Err(e) => {
                unsafe {
                    raw.destroy_image(image, None);
                    raw.free_memory(memory, None);
                }
                return Err(format!("slot {i}: {e}"));
            }
        };
        let hal_desc = wgpu::hal::TextureDescriptor {
            label: Some("scanout ring"),
            size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format,
            usage: wgpu::TextureUses::RESOURCE | wgpu::TextureUses::COPY_SRC,
            memory_flags: wgpu::hal::MemoryFlags::empty(),
            view_formats: vec![],
        };
        let raw2 = raw.clone();
        let hal = unsafe { device.as_hal::<Vulkan>() }.ok_or("not a Vulkan device")?;
        let hal_tex = unsafe {
            hal.texture_from_raw(
                image,
                &hal_desc,
                Some(Box::new(move || {
                    raw2.destroy_image(image, None);
                    raw2.free_memory(memory, None);
                })),
                wgpu::hal::vulkan::TextureMemory::External,
            )
        };
        drop(hal);
        let tex = unsafe {
            device.create_texture_from_hal::<Vulkan>(
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
                // sampled as it is: see the module's note on layouts
                wgpu::TextureUses::RESOURCE,
            )
        };
        slots.push(Slot {
            tex,
            info: Box::new(RingFd {
                fd,
                memory_type: mem_type,
                size: req.size,
                device_uuid,
                driver_uuid,
            }),
        });
    }
    Ok(slots)
}
