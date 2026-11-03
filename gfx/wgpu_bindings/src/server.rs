/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{
    error::{pipeline_error_to_ffi, request_device_error_to_ffi, GPUError},
    make_byte_buf,
    telemetry::build_telemetry_struct,
    wgpu_string, AdapterInformation, BufferMapResult, ByteBuf, DeviceAction, FfiDeviceLostReason,
    FfiErrorFilter, FfiPopErrorScopeResultType, FfiSlice, FfiTextureDescriptor, Message,
    QueueWriteAction, QueueWriteDataSource, RequestDeviceError, ServerMessage,
    ShaderModuleCompilationMessage, SwapChainId, TextureAction,
};

use futures_util::StreamExt;
use nsstring::{nsACString, nsCString};
use scopeguard::{guard, ScopeGuard};

use wgc::{
    error::EmptyErrorScopeStack,
    resource::{BufferAccessError, BufferAccessResult, BufferMapCallback},
};
#[allow(unused_imports)]
use wgh::Instance;
use wgpu_core_remote_types::{
    encoders::{CommandBufferDescriptor, TexelCopyBufferInfo, TexelCopyTextureInfo},
    id, BufferDescriptor, DeviceDescriptor, ShaderModuleDescriptor, TextureDescriptor,
    TextureViewDescriptor,
};
use wgt::{
    error::{ErrorFilter, ErrorType, WebGpuError},
    CommandEncoderDescriptor, TexelCopyBufferLayout,
};

use std::borrow::Cow;
use std::collections::HashMap;
#[allow(unused_imports)]
use std::mem;
#[cfg(target_os = "linux")]
use std::os::fd::{AsRawFd, FromRawFd, IntoRawFd, OwnedFd, RawFd};
use std::os::raw::{c_char, c_void};
use std::ptr;
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

#[allow(unused_imports)]
use std::ffi::CString;

#[cfg(target_os = "windows")]
use windows::Win32::{Foundation, Graphics::Direct3D12};

#[cfg(target_os = "linux")]
use ash::{khr, vk};

// The seemingly redundant u64 suffix helps cbindgen with generating the right C++ code.
// See https://github.com/mozilla/cbindgen/issues/849#issuecomment-4759987548.

/// We limit the size of buffer allocations for stability reason.
/// We can reconsider this limit in the future. Note that some drivers (mesa for example),
/// have issues when the size of a buffer, mapping or copy command does not fit into a
/// signed 32 bits integer, so beyond a certain size, large allocations will need some form
/// of driver allow/blocklist.
///
/// Buffer sizes don't have to be aligned, but storage bindings do, and we clamp
/// maxStorageBufferBindingSize to MAX_BUFFER_SIZE, so use an aligned limit.
pub const MAX_BUFFER_SIZE: wgt::BufferAddress = (1u64 << 31) - 4;

// Mesa has issues with height/depth that don't fit in a 16 bits signed integers.
const MAX_TEXTURE_EXTENT: u32 = i16::MAX as u32;
// We have to restrict the number of bindings for any given resource type so that
// the sum of these limits multiplied by the number of shader stages fits
// maxBindingsPerBindGroup (1000). This restriction is arbitrary and is likely to
// change eventually. See github.com/gpuweb/gpuweb/pull/4484
// For now it's impractical for users to have very large numbers of bindings so this
// limit should not be too restrictive until we add support for a bindless API.
// Then we may have to ignore the spec or get it changed.
const MAX_BINDINGS_PER_RESOURCE_TYPE: u32 = 64;

#[cfg(target_os = "macos")]
fn emit_critical_invalid_note(what: &'static str) {
    // SAFETY: We ensure that the pointer provided is not null.
    let msg = CString::new(format!("{what} is invalid")).unwrap();
    unsafe { gfx_critical_note(msg.as_ptr()) }
}

#[cfg(target_os = "linux")]
fn get_linux_dmabuf_modifiers() -> Option<Vec<u64>> {
    let mut modifiers_ptr: *const u64 = ptr::null();
    let mut modifier_count = 0u32;

    let ok =
        unsafe { wgpu_server_get_linux_dmabuf_modifiers(&mut modifiers_ptr, &mut modifier_count) };
    if !ok {
        return None;
    }
    if modifier_count == 0 {
        return Some(Vec::new());
    }
    if modifiers_ptr.is_null() {
        return None;
    }

    // The pointer is owned by gfxVars storage and copied immediately.
    let modifiers = unsafe { std::slice::from_raw_parts(modifiers_ptr, modifier_count as usize) };
    Some(modifiers.to_vec())
}

#[cfg(target_os = "linux")]
const DMABUF_DEVICE_EXTENSIONS: &[&'static std::ffi::CStr] = &[
    khr::external_memory_fd::NAME,
    ash::ext::external_memory_dma_buf::NAME,
    ash::ext::image_drm_format_modifier::NAME,
    khr::external_semaphore_fd::NAME,
    khr::dedicated_allocation::NAME,
    khr::get_memory_requirements2::NAME,
];

#[cfg(target_os = "linux")]
fn unsupported_dmabuf_extensions(
    hal_adapter: &wgh::vulkan::Adapter,
) -> Vec<&'static std::ffi::CStr> {
    let capabilities = hal_adapter.physical_device_capabilities();
    DMABUF_DEVICE_EXTENSIONS
        .iter()
        .copied()
        .filter(|&extension| !capabilities.supports_extension(extension))
        .collect()
}

fn restrict_limits(limits: wgt::Limits) -> wgt::Limits {
    let wgt::Limits {
        max_texture_dimension_1d,
        max_texture_dimension_2d,
        max_texture_dimension_3d,
        max_texture_array_layers,
        max_bind_groups,
        max_bind_groups_plus_vertex_buffers,
        max_bindings_per_bind_group,
        max_dynamic_uniform_buffers_per_pipeline_layout,
        max_dynamic_storage_buffers_per_pipeline_layout,
        max_sampled_textures_per_shader_stage,
        max_samplers_per_shader_stage,
        max_storage_buffers_per_shader_stage,
        max_storage_buffers_in_vertex_stage,
        max_storage_buffers_in_fragment_stage,
        max_storage_textures_per_shader_stage,
        max_storage_textures_in_vertex_stage,
        max_storage_textures_in_fragment_stage,
        max_uniform_buffers_per_shader_stage,
        max_binding_array_elements_per_shader_stage,
        max_binding_array_acceleration_structure_elements_per_shader_stage,
        max_binding_array_sampler_elements_per_shader_stage,
        max_uniform_buffer_binding_size,
        max_storage_buffer_binding_size,
        max_vertex_buffers,
        max_buffer_size,
        max_vertex_attributes,
        max_vertex_buffer_array_stride,
        max_inter_stage_shader_variables,
        min_uniform_buffer_offset_alignment,
        min_storage_buffer_offset_alignment,
        max_color_attachments,
        max_color_attachment_bytes_per_sample,
        max_compute_workgroup_storage_size,
        max_compute_invocations_per_workgroup,
        max_compute_workgroup_size_x,
        max_compute_workgroup_size_y,
        max_compute_workgroup_size_z,
        max_compute_workgroups_per_dimension,
        max_immediate_size,
        max_non_sampler_bindings: _,
        max_task_workgroup_total_count,
        max_task_workgroups_per_dimension,
        max_mesh_workgroup_total_count,
        max_mesh_workgroups_per_dimension,
        max_task_invocations_per_workgroup,
        max_task_invocations_per_dimension,
        max_mesh_invocations_per_workgroup,
        max_mesh_invocations_per_dimension,
        max_task_payload_size,
        max_mesh_output_vertices,
        max_mesh_output_primitives,
        max_mesh_output_layers,
        max_mesh_multiview_view_count,
        max_blas_primitive_count,
        max_blas_geometry_count,
        max_tlas_instance_count,
        max_acceleration_structures_per_shader_stage,
        max_buffers_and_acceleration_structures_per_shader_stage,
        max_multiview_view_count,
        max_ray_dispatch_count,
        max_ray_recursion_depth,
    } = limits;

    wgt::Limits {
        max_texture_dimension_1d: max_texture_dimension_1d.min(MAX_TEXTURE_EXTENT),
        max_texture_dimension_2d: max_texture_dimension_2d.min(MAX_TEXTURE_EXTENT),
        max_texture_dimension_3d: max_texture_dimension_3d.min(MAX_TEXTURE_EXTENT),
        max_texture_array_layers,
        max_bind_groups,
        max_bind_groups_plus_vertex_buffers,
        max_bindings_per_bind_group,
        max_dynamic_uniform_buffers_per_pipeline_layout,
        max_dynamic_storage_buffers_per_pipeline_layout,
        max_sampled_textures_per_shader_stage: max_sampled_textures_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_samplers_per_shader_stage: max_samplers_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_buffers_per_shader_stage: max_storage_buffers_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_buffers_in_vertex_stage: max_storage_buffers_in_vertex_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_buffers_in_fragment_stage: max_storage_buffers_in_fragment_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_textures_per_shader_stage: max_storage_textures_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_textures_in_vertex_stage: max_storage_textures_in_vertex_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_textures_in_fragment_stage: max_storage_textures_in_fragment_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_uniform_buffers_per_shader_stage: max_uniform_buffers_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_binding_array_elements_per_shader_stage,
        max_binding_array_acceleration_structure_elements_per_shader_stage,
        max_binding_array_sampler_elements_per_shader_stage,
        max_uniform_buffer_binding_size: max_uniform_buffer_binding_size.min(MAX_BUFFER_SIZE),
        max_storage_buffer_binding_size: max_storage_buffer_binding_size.min(MAX_BUFFER_SIZE),
        max_vertex_buffers,
        max_buffer_size: max_buffer_size.min(MAX_BUFFER_SIZE),
        max_vertex_attributes,
        max_vertex_buffer_array_stride,
        max_inter_stage_shader_variables,
        min_uniform_buffer_offset_alignment,
        min_storage_buffer_offset_alignment,
        max_color_attachments,
        max_color_attachment_bytes_per_sample,
        max_compute_workgroup_storage_size,
        max_compute_invocations_per_workgroup,
        max_compute_workgroup_size_x,
        max_compute_workgroup_size_y,
        max_compute_workgroup_size_z,
        max_compute_workgroups_per_dimension,
        max_immediate_size,
        max_non_sampler_bindings: 500_000,
        max_task_workgroup_total_count,
        max_task_workgroups_per_dimension,
        max_mesh_workgroup_total_count,
        max_mesh_workgroups_per_dimension,
        max_task_invocations_per_workgroup,
        max_task_invocations_per_dimension,
        max_mesh_invocations_per_workgroup,
        max_mesh_invocations_per_dimension,
        max_task_payload_size,
        max_mesh_output_vertices,
        max_mesh_output_primitives,
        max_mesh_output_layers,
        max_mesh_multiview_view_count,
        max_blas_primitive_count,
        max_blas_geometry_count,
        max_tlas_instance_count,
        max_acceleration_structures_per_shader_stage,
        max_buffers_and_acceleration_structures_per_shader_stage,
        max_multiview_view_count,
        max_ray_dispatch_count,
        max_ray_recursion_depth,
    }
}

/// Opaque pointer to `mozilla::webgpu::WebGPUParent`.
#[derive(Debug, Clone, Copy)]
#[repr(transparent)]
pub struct WebGPUParentPtr(*mut core::ffi::c_void);

/// An owning handle to a C++ `mozilla::WeakPtr<mozilla::webgpu::WebGPUParent>`.
///
/// Callbacks that outlive the call into the server must not capture a
/// [`WebGPUParentPtr`] directly, since the parent may be destroyed before they
/// run. `mozilla::WeakPtr` is not thread safe, so values of this type must be
/// created, dereferenced and dropped on the thread owning the `WebGPUParent`.
/// The raw pointer this holds makes that the default, as it keeps the type
/// `!Send`.
pub struct WebGPUParentWeakPtr(*mut core::ffi::c_void);

impl WebGPUParentWeakPtr {
    fn new(parent: WebGPUParentPtr) -> Self {
        Self(unsafe { wgpu_parent_weak_ptr_new(parent) })
    }

    /// Returns `None` once the `WebGPUParent` has been destroyed.
    fn get(&self) -> Option<WebGPUParentPtr> {
        let parent = unsafe { wgpu_parent_weak_ptr_get(self.0) };
        (!parent.0.is_null()).then_some(parent)
    }
}

impl Drop for WebGPUParentWeakPtr {
    fn drop(&mut self) {
        unsafe { wgpu_parent_weak_ptr_delete(self.0) };
    }
}

pub struct Global {
    owner: WebGPUParentPtr,
    global: wgpu_core_remote::global::Global,

    /// Swap chain texture descriptors.
    ///
    /// We store the descriptors because they should not change over the life of a swap chain. The
    /// descriptors here are not necessarily valid; they must still only be passed to validating
    /// wgpu-core APIs.
    swap_chain_configs: Mutex<HashMap<SwapChainId, TextureDescriptor<'static>>>,
}

impl std::ops::Deref for Global {
    type Target = wgpu_core_remote::global::Global;
    fn deref(&self) -> &Self::Target {
        &self.global
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_new(owner: WebGPUParentPtr) -> *mut Global {
    log::info!("Initializing WGPU server");
    let backends_pref = static_prefs::pref!("dom.webgpu.wgpu-backend").to_string();
    let backends = if backends_pref.is_empty() {
        #[cfg(windows)]
        {
            wgt::Backends::DX12
        }
        #[cfg(not(windows))]
        {
            wgt::Backends::PRIMARY
        }
    } else {
        log::info!(
            "Selecting backends based on dom.webgpu.wgpu-backend pref: {:?}",
            backends_pref
        );
        wgt::Backends::from_comma_list(&backends_pref)
    };

    let mut instance_flags = (wgt::InstanceFlags::from_build_config()
        | wgt::InstanceFlags::AUTOMATIC_TIMESTAMP_NORMALIZATION
        | wgt::InstanceFlags::STRICT_WEBGPU_COMPLIANCE)
        .with_env();
    if !static_prefs::pref!("dom.webgpu.hal-labels") {
        instance_flags.insert(wgt::InstanceFlags::DISCARD_HAL_LABELS);
    }

    let dx12_shader_compiler = wgt::Dx12Compiler::DynamicDxc {
        dxc_path: "dxcompiler.dll".into(),
    };

    let global = wgpu_core_remote::global::Global::new(
        "wgpu",
        wgt::InstanceDescriptor {
            backends,
            flags: instance_flags,
            backend_options: wgt::BackendOptions {
                gl: wgt::GlBackendOptions {
                    gles_minor_version: wgt::Gles3MinorVersion::Automatic,
                    fence_behavior: wgt::GlFenceBehavior::Normal,
                    debug_fns: wgt::GlDebugFns::Auto,
                },
                dx12: wgt::Dx12BackendOptions {
                    shader_compiler: dx12_shader_compiler,
                    ..Default::default()
                },
                noop: wgt::NoopBackendOptions::default(),
            },
            memory_budget_thresholds: wgt::MemoryBudgetThresholds {
                for_resource_creation: Some(95),
                for_device_loss: Some(99),
            },
            display: None,
        },
        Some(build_telemetry_struct()),
    );
    let global = Global {
        owner,
        global,
        swap_chain_configs: Mutex::new(HashMap::new()),
    };
    Box::into_raw(Box::new(global))
}

/// # Safety
///
/// This function is unsafe because improper use may lead to memory
/// problems. For example, a double-free may occur if the function is called
/// twice on the same raw pointer.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_delete(global: *mut Global) {
    log::info!("Terminating WGPU server");
    let _ = unsafe { Box::from_raw(global) };
}

#[no_mangle]
pub extern "C" fn wgpu_server_poll_all_devices(global: &Global, force_wait: bool) {
    global.poll_all_devices(force_wait).unwrap();
}

#[no_mangle]
pub extern "C" fn wgpu_server_device_poll(
    global: &Global,
    device_id: id::DeviceId,
    force_wait: bool,
) {
    let maintain = if force_wait {
        wgt::PollType::Wait {
            submission_index: None,
            timeout: Some(Duration::from_secs(60)),
        }
    } else {
        wgt::PollType::Poll
    };
    global.device_poll(device_id, maintain).unwrap();
}

#[cfg(target_os = "macos")]
fn ns_os_version_at_least(
    lhs: &objc2_foundation::NSOperatingSystemVersion,
    rhs_mac_version: (isize, isize),
    rhs_ios_version: (isize, isize),
    is_mac: bool,
) -> bool {
    let rhs = if is_mac {
        rhs_mac_version
    } else {
        rhs_ios_version
    };

    lhs.majorVersion
        .cmp(&rhs.0)
        .then_with(|| lhs.minorVersion.cmp(&rhs.1))
        .is_ge()
}

#[allow(unreachable_code)]
#[allow(unused_variables)]
fn support_use_shared_texture_in_swap_chain(
    global: &Global,
    self_id: id::AdapterId,
    backend: wgt::Backend,
    is_hardware: bool,
) -> bool {
    #[cfg(target_os = "windows")]
    {
        if backend != wgt::Backend::Dx12 {
            log::info!(concat!(
                "WebGPU: disabling SharedTexture swapchain: \n",
                "wgpu backend is not Dx12"
            ));
            return false;
        }
        if !is_hardware {
            log::info!(concat!(
                "WebGPU: disabling SharedTexture swapchain: \n",
                "Dx12 backend is not hardware"
            ));
            return false;
        }
        return true;
    }

    #[cfg(target_os = "linux")]
    {
        if backend != wgt::Backend::Vulkan {
            log::info!(concat!(
                "WebGPU: disabling SharedTexture swapchain: \n",
                "wgpu backend is not Vulkan"
            ));
            return false;
        }

        let adapter = global.resolve_adapter_id(self_id);
        let hal_adapter = unsafe { adapter.as_hal::<wgc::api::Vulkan>() }
            .expect("Vulkan backend adapter on linux");

        if !unsupported_dmabuf_extensions(&hal_adapter).is_empty() {
            return false;
        }

        // We need to be able to export the semaphore that gets signalled
        // when the GPU is done drawing on the ExternalTextureDMABuf.
        let semaphore_info = vk::PhysicalDeviceExternalSemaphoreInfo::default()
            .handle_type(vk::ExternalSemaphoreHandleTypeFlags::OPAQUE_FD);
        let mut semaphore_props = vk::ExternalSemaphoreProperties::default();
        unsafe {
            hal_adapter
                .shared_instance()
                .raw_instance()
                .get_physical_device_external_semaphore_properties(
                    hal_adapter.raw_physical_device(),
                    &semaphore_info,
                    &mut semaphore_props,
                );
        }
        if !semaphore_props
            .external_semaphore_features
            .contains(vk::ExternalSemaphoreFeatureFlags::EXPORTABLE)
        {
            log::info!(
                "WebGPU: disabling ExternalTexture swapchain: \n\
                        device can't export opaque file descriptor semaphores"
            );
            return false;
        }

        return true;
    }

    #[cfg(target_os = "macos")]
    {
        use objc2_foundation::NSProcessInfo;

        if backend != wgt::Backend::Metal {
            log::info!(concat!(
                "WebGPU: disabling SharedTexture swapchain: \n",
                "wgpu backend is not Metal"
            ));
            return false;
        }
        if !is_hardware {
            log::info!(concat!(
                "WebGPU: disabling SharedTexture swapchain: \n",
                "Metal backend is not hardware"
            ));
            return false;
        }

        let version = NSProcessInfo::processInfo().operatingSystemVersion();

        if !ns_os_version_at_least(&version, (10, 14), (12, 0), /* os_is_mac */ true) {
            log::info!(concat!(
                "WebGPU: disabling SharedTexture swapchain:\n",
                "operating system version is not at least 10.14 (macOS) or 12.0 (iOS)\n",
                "shared event not supported"
            ));
            return false;
        }

        return true;
    }

    false
}

fn create_next_numbered_dir(dir: &std::path::Path) -> std::io::Result<std::path::PathBuf> {
    use std::fs;

    loop {
        let next = match fs::read_dir(dir) {
            Ok(entries) => entries
                .filter_map(|entry| entry.ok())
                .filter_map(|entry| entry.file_name().to_str()?.parse::<u64>().ok())
                .max()
                .map(|n| n + 1),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
            Err(e) => return Err(e),
        };

        let path = dir.join(next.unwrap_or(0).to_string());
        match fs::create_dir_all(&path) {
            Ok(()) => return Ok(path),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => return Err(e),
        }
    }
}

fn adapter_request_device(
    global: &Global,
    self_id: id::AdapterId,
    desc: DeviceDescriptor,
    new_device_id: id::DeviceId,
    new_queue_id: id::QueueId,
) -> Result<(), RequestDeviceError> {
    // Note that wgpu-core will generally ignore all required features and limits that
    // are not part of the spec. See docs of `InstanceFlags::STRICT_WEBGPU_COMPLIANCE`
    // for exceptions and actual behavior.

    let mut trace = wgt::Trace::Off;
    if let Some(env_dir) = std::env::var_os("WGPU_TRACE") {
        match create_next_numbered_dir(&std::path::PathBuf::from(env_dir)) {
            Ok(path) => trace = wgt::Trace::Directory(path),
            Err(err) => log::warn!("Failed to create directory for wgpu recording: {err:?}"),
        }
    }

    let mut additional_features = wgt::Features::empty();
    if unsafe { wgpu_parent_is_external_texture_enabled() } {
        // Enable features used for external texture support, if available. We
        // avoid adding unsupported features to required_features so that we
        // can still create a device in their absence, and will only fail when
        // performing an operation that actually requires the feature.
        for feature in [
            wgt::Features::EXTERNAL_TEXTURE,
            wgt::Features::TEXTURE_FORMAT_NV12,
            wgt::Features::TEXTURE_FORMAT_P010,
            wgt::Features::TEXTURE_FORMAT_16BIT_NORM,
        ] {
            if global.adapter_features(self_id).contains(feature) {
                additional_features.insert(feature);
            }
        }
    }

    #[cfg(target_os = "linux")]
    {
        let adapter = global.resolve_adapter_id(self_id);
        let hal_adapter = unsafe { adapter.as_hal::<wgc::api::Vulkan>() }
            .expect("Vulkan backend adapter on linux");

        let support_dma_buf = {
            let unsupported = unsupported_dmabuf_extensions(&hal_adapter);
            if unsupported.is_empty() {
                true
            } else {
                log::info!(
                    concat!(
                        "WebGPU: disabling SharedTexture swapchain: \n",
                        "Vulkan extensions not supported: {:?}",
                    ),
                    unsupported
                );
                false
            }
        };

        match support_dma_buf {
            false => {}
            true => {
                let mut sanitized_desc = wgt::DeviceDescriptor {
                    label: desc.label,
                    required_features: wgt::Features::from_internal_flags(
                        wgt::FeaturesWGPU::empty(),
                        desc.required_features,
                    )
                    .union(additional_features),
                    required_limits: desc.required_limits,
                    default_queue: desc.default_queue,
                    experimental_features: wgt::ExperimentalFeatures::disabled(),
                    memory_hints: wgt::MemoryHints::MemoryUsage,
                    trace,
                };

                global
                    .adapter_validate_device_descriptor(self_id, &mut sanitized_desc)
                    .map_err(request_device_error_to_ffi)?;

                let mut enabled_extensions =
                    hal_adapter.required_device_extensions(sanitized_desc.required_features);
                enabled_extensions.extend_from_slice(DMABUF_DEVICE_EXTENSIONS);

                let mut enabled_phd_features = hal_adapter.physical_device_features(
                    &enabled_extensions,
                    sanitized_desc.required_features,
                );

                let raw_instance = hal_adapter.shared_instance().raw_instance();
                let raw_physical_device = hal_adapter.raw_physical_device();

                let queue_family_index = unsafe {
                    raw_instance
                        .get_physical_device_queue_family_properties(raw_physical_device)
                        .into_iter()
                        .enumerate()
                        .find_map(|(queue_family_index, info)| {
                            if info.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
                                Some(queue_family_index as u32)
                            } else {
                                None
                            }
                        })
                };

                let Some(queue_family_index) = queue_family_index else {
                    let msg = c"Vulkan device has no graphics queue";
                    unsafe { gfx_critical_note(msg.as_ptr()) };
                    return Err(RequestDeviceError::operation_error(
                        "Internal Error: Failed to create ash::Device".to_string(),
                    ));
                };

                let family_info = vk::DeviceQueueCreateInfo::default()
                    .queue_family_index(queue_family_index)
                    .queue_priorities(&[1.0]);
                let family_infos = [family_info];

                let str_pointers = enabled_extensions
                    .iter()
                    .map(|&s| {
                        // Safe because `enabled_extensions` entries have static lifetime.
                        s.as_ptr()
                    })
                    .collect::<Vec<_>>();

                let pre_info = vk::DeviceCreateInfo::default()
                    .queue_create_infos(&family_infos)
                    .enabled_extension_names(&str_pointers);
                let info = enabled_phd_features.add_to_device_create(pre_info);

                let raw_device = unsafe {
                    // SAFETY: We either transfer the returned `raw_device` to `wgpu`, which then
                    // keeps an `Arc` for the parent instance, or if an error occurs before we do
                    // that, we destroy the device.
                    raw_instance
                        .create_device(raw_physical_device, &info, None)
                        .map_err(|err| {
                            let msg =
                                CString::new(format!("create_device() failed: {:?}", err)).unwrap();
                            gfx_critical_note(msg.as_ptr());
                            RequestDeviceError::operation_error(
                                "Internal Error: Failed to create ash::Device".to_string(),
                            )
                        })?
                };

                let hal_device = unsafe {
                    // SAFETY:
                    // - The raw device was created from this adapter.
                    // - We did not remove any extensions.
                    // - On success, `wgpu` takes ownership of the device. We destroy
                    //   it ourselves if and only if this call fails.
                    hal_adapter
                        .device_from_raw(
                            raw_device.clone(),
                            None,
                            &enabled_extensions,
                            sanitized_desc.required_features,
                            &sanitized_desc.required_limits,
                            &sanitized_desc.memory_hints,
                            family_info.queue_family_index,
                            0,
                        )
                        .map_err(move |err| {
                            raw_device.destroy_device(None);
                            let msg = CString::new(format!("device_from_raw() failed: {:?}", err))
                                .unwrap();
                            gfx_critical_note(msg.as_ptr());
                            RequestDeviceError::operation_error(
                                "Internal Error: Failed to create ash::Device".to_string(),
                            )
                        })?
                };

                unsafe {
                    // SAFETY:
                    // - The hal device was created from this adapter.
                    global
                        .create_device_from_hal(
                            self_id,
                            hal_device.into(),
                            &sanitized_desc,
                            new_device_id,
                            new_queue_id,
                        )
                        .map_err(request_device_error_to_ffi)?;
                }

                return Ok(());
            }
        }
    }

    global
        .adapter_request_device(
            self_id,
            &desc,
            trace,
            additional_features,
            new_device_id,
            new_queue_id,
        )
        .map_err(request_device_error_to_ffi)?;

    Ok(())
}

fn set_device_lost_handler(global: &Global, device_id: id::DeviceId) {
    // Create a one-shot channel that the `wgpu_core` callback can use to report
    // the device loss to the calling thread.
    let (device_lost_sender, device_lost_receiver) = futures_channel::oneshot::channel();

    // Spawn a task on the calling thread to wait for such a report.
    let parent = WebGPUParentWeakPtr::new(global.owner);
    moz_task::spawn_local("WebGPU device lost callback", async move {
        // `Err` means the closure was never invoked and `device_lost_sender`
        // was dropped along with the device.
        let Ok((reason, message)) = device_lost_receiver.await else {
            return;
        };
        let Some(parent) = parent.get() else {
            return;
        };
        let reason = match reason {
            wgt::DeviceLostReason::Unknown => FfiDeviceLostReason::Unknown,
            wgt::DeviceLostReason::Destroyed => FfiDeviceLostReason::Destroyed,
        };
        let mut byte_buf = make_byte_buf(&ServerMessage::DeviceLost(
            device_id,
            reason,
            Cow::Owned(message),
        ));
        unsafe { wgpu_parent_send_server_message(parent, &mut byte_buf) };
    })
    .detach();

    global.device_set_device_lost_closure(
        device_id,
        Box::new(move |reason, message| {
            // Sending fails only if the receiving task is already gone.
            let _ = device_lost_sender.send((reason, message));
        }),
    );
}

fn set_uncaptured_error_handler(global: &Global, device_id: id::DeviceId) {
    let (error_sender, mut error_receiver) = futures_channel::mpsc::unbounded();

    let parent = WebGPUParentWeakPtr::new(global.owner);
    moz_task::spawn_local("WebGPU uncaptured error callback", async move {
        // Exits once the device, and with it the handler holding `error_sender`,
        // has been dropped.
        while let Some((ty, message)) = error_receiver.next().await {
            let Some(parent) = parent.get() else {
                return;
            };
            let mut byte_buf = make_byte_buf(&ServerMessage::UncapturedError(
                device_id,
                ty,
                Cow::Owned(message),
            ));
            unsafe { wgpu_parent_send_server_message(parent, &mut byte_buf) };
        }
    })
    .detach();

    global.device_on_uncaptured_error(
        device_id,
        Arc::new(move |error: wgt::error::Error| {
            let ty = match error.webgpu_error_type() {
                ErrorType::Validation => FfiErrorFilter::Validation,
                ErrorType::OutOfMemory => FfiErrorFilter::OutOfMemory,
                ErrorType::Internal => FfiErrorFilter::Internal,
                ErrorType::DeviceLost => unreachable!(),
            };
            // Sending fails only if the receiving task is already gone.
            let _ = error_sender.unbounded_send((ty, format!("{error}")));
        }),
    );
}

impl ShaderModuleCompilationMessage {
    fn new(value: wgt::CompilationMessage, source: &str) -> Self {
        // The WebGPU spec says that if the message doesn't point to a particular position in
        // the source, the line number, position, offset and lengths should be zero.
        let line_number;
        let line_pos;
        let utf16_offset;
        let utf16_length;

        let wgt::CompilationMessage {
            message,
            message_type,
            location,
        } = value;

        if let Some(location) = location {
            let len_utf16 = |s: &str| s.chars().map(|c| c.len_utf16() as u64).sum();
            let start = location.offset as usize;
            let end = start + location.length as usize;
            utf16_offset = len_utf16(&source[0..start]);
            utf16_length = len_utf16(&source[start..end]);

            line_number = location.line_number as u64;
            // Naga reports a `line_pos` using UTF-8 bytes, so we cannot use it.
            let line_start = source[0..start].rfind('\n').map(|pos| pos + 1).unwrap_or(0);
            line_pos = len_utf16(&source[line_start..start]) + 1;
        } else {
            line_number = 0;
            line_pos = 0;
            utf16_offset = 0;
            utf16_length = 0;
        }

        Self {
            line_number,
            line_pos,
            utf16_offset,
            utf16_length,
            message,
            message_type,
        }
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_device_create_buffer(
    global: &Global,
    device_id: id::DeviceId,
    buffer_id: id::BufferId,
    size: wgt::BufferAddress,
    usage: wgt::BufferUsagesWebGPU,
) -> bool {
    let desc = BufferDescriptor {
        label: None,
        size,
        usage,
        mapped_at_creation: false,
    };

    global.device_push_error_scope(device_id, ErrorFilter::Validation);
    global.device_push_error_scope(device_id, ErrorFilter::OutOfMemory);
    global.device_create_buffer(device_id, &desc, buffer_id);
    let oom_error = global.device_pop_error_scope(device_id).unwrap();
    let validation_error = global.device_pop_error_scope(device_id).unwrap();
    if let Some(validation_error) = validation_error {
        panic!("{validation_error}");
    }
    if oom_error.is_some() {
        global.buffer_remove(buffer_id);
    }
    oom_error.is_none()
}

/// The status code provided to the buffer mapping closure.
///
/// This is very similar to `BufferAccessResult`, except that this is FFI-friendly.
#[repr(C)]
pub enum BufferMapAsyncStatus {
    /// The Buffer is successfully mapped, `get_mapped_range` can be called.
    ///
    /// All other variants of this enum represent failures to map the buffer.
    Success,
    /// The buffer is already mapped.
    ///
    /// While this is treated as an error, it does not prevent mapped range from being accessed.
    AlreadyMapped,
    /// Mapping was already requested.
    MapAlreadyPending,
    /// An unknown error.
    Error,
    /// The context is Lost.
    ContextLost,
    /// The buffer is in an invalid state.
    Invalid,
    /// The range isn't fully contained in the buffer.
    InvalidRange,
    /// The range isn't properly aligned.
    InvalidAlignment,
    /// Incompatible usage flags.
    InvalidUsageFlags,
}

impl From<BufferAccessResult> for BufferMapAsyncStatus {
    fn from(result: BufferAccessResult) -> Self {
        match result {
            Ok(_) => BufferMapAsyncStatus::Success,
            Err(BufferAccessError::Device(_)) => BufferMapAsyncStatus::ContextLost,
            Err(BufferAccessError::InvalidResource(_))
            | Err(BufferAccessError::DestroyedResource(_)) => BufferMapAsyncStatus::Invalid,
            Err(BufferAccessError::AlreadyMapped) => BufferMapAsyncStatus::AlreadyMapped,
            Err(BufferAccessError::MapAlreadyPending) => BufferMapAsyncStatus::MapAlreadyPending,
            Err(BufferAccessError::MissingBufferUsage(_)) => {
                BufferMapAsyncStatus::InvalidUsageFlags
            }
            Err(BufferAccessError::UnalignedRange)
            | Err(BufferAccessError::UnalignedRangeSize { .. })
            | Err(BufferAccessError::UnalignedOffset { .. }) => {
                BufferMapAsyncStatus::InvalidAlignment
            }
            Err(BufferAccessError::OutOfBoundsStartOffsetUnderrun { .. })
            | Err(BufferAccessError::OutOfBoundsStartOffsetOverrun { .. })
            | Err(BufferAccessError::OutOfBoundsEndOffsetOverrun { .. })
            | Err(BufferAccessError::MapStartOffsetOverrun { .. })
            | Err(BufferAccessError::MapEndOffsetOverrun { .. }) => {
                BufferMapAsyncStatus::InvalidRange
            }
            Err(BufferAccessError::Failed)
            | Err(BufferAccessError::NotMapped)
            | Err(BufferAccessError::MapAborted) => BufferMapAsyncStatus::Error,
            Err(_) => BufferMapAsyncStatus::Invalid,
        }
    }
}

impl From<Result<(), wgc::device::WaitIdleError>> for BufferMapAsyncStatus {
    fn from(result: Result<(), wgc::device::WaitIdleError>) -> Self {
        match result {
            Ok(()) => BufferMapAsyncStatus::Success,
            Err(err) => match err {
                wgc::device::WaitIdleError::Device(_) => BufferMapAsyncStatus::ContextLost,
                wgc::device::WaitIdleError::WrongSubmissionIndex(_, _)
                | wgc::device::WaitIdleError::Timeout => BufferMapAsyncStatus::Error,
                _ => BufferMapAsyncStatus::Error,
            },
        }
    }
}

#[repr(C)]
pub struct BufferMapClosure {
    pub callback: unsafe extern "C" fn(user_data: *mut u8, status: BufferMapAsyncStatus),
    pub user_data: *mut u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HostMap {
    Read,
    Write,
}

impl HostMap {
    fn to_wgrt(&self) -> wgpu_core_remote_types::MapMode {
        match self {
            HostMap::Read => wgpu_core_remote_types::MapMode::READ,
            HostMap::Write => wgpu_core_remote_types::MapMode::WRITE,
        }
    }
}

/// # Safety
///
/// Callers are responsible for ensuring `closure` is well-formed.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_buffer_map(
    global: &Global,
    buffer_id: id::BufferId,
    start: wgt::BufferAddress,
    size: wgt::BufferAddress,
    map_mode: HostMap,
    closure: BufferMapClosure,
) {
    // Create a one-shot channel to carry the map result from the
    // `buffer_map_async` callback to this thread.
    let (map_result_sender, map_result_receiver) = futures_channel::oneshot::channel();

    // Spawn a task on this thread to wait for that map result.
    moz_task::spawn_local("wgpu_server_buffer_map callback", async move {
        let result = map_result_receiver.await.unwrap();
        unsafe {
            (closure.callback)(closure.user_data, BufferMapAsyncStatus::from(result));
        }
    })
    .detach();

    let mode = map_mode.to_wgrt();
    let callback: Option<BufferMapCallback> = Some(Box::new(move |result| {
        // Send the map result from whatever thread this callback is running
        // on to the task we spawned above.
        map_result_sender.send(result).unwrap();
    }));
    let _submission_idx_opt = global.buffer_map_async(buffer_id, mode, start, Some(size), callback);
}

/// Map a buffer, blocking until it is ready for access.
///
/// Map the `size` bytes starting at `offset` in `buffer_id` to be accessed
/// according to `map_mode`, blocking the calling thread until the mapping is
/// ready.
///
/// This function actually blocks the calling thread until the GPU has completed
/// all previously submitted work, even if the buffer is actually available
/// right now. In practice, this function is generally used immediately after
/// submitted work that writes data to the buffer, so this shouldn't be much of
/// a problem.
///
/// All ids are looked up using `global`. The buffer `buffer_id` must belong to
/// `device_id`.
///
/// Return a `BufferMapAsyncStatus` to indicate success or failure.
#[no_mangle]
pub extern "C" fn wgpu_server_buffer_map_blocking(
    global: &Global,
    device_id: id::DeviceId,
    buffer_id: id::BufferId,
    offset: wgt::BufferAddress,
    size: wgt::BufferAddress,
    map_mode: HostMap,
) -> BufferMapAsyncStatus {
    // Arrange to pass the map status back to this function. The whole Arc/OnceLock
    // song and dance is required because `buffer_map_async` assumes that the
    // poll might happen on another thread.
    let status_passback = Arc::new(OnceLock::new());
    let mode = map_mode.to_wgrt();
    let callback: Option<BufferMapCallback> = Some(Box::new({
        let status_passback = Arc::clone(&status_passback);
        move |status| {
            // unwrap: This callback is the only place that initializes the
            // `OnceLock`, and it should only be invoked once.
            status_passback.set(status).unwrap();
        }
    }));

    // Submit the map request, and note its submission index.
    let submission_index;
    match global.buffer_map_async(buffer_id, mode, offset, Some(size), callback) {
        Some(i) => {
            submission_index = i;
        }
        None => {
            return BufferMapAsyncStatus::Error;
        }
    }

    // Wait until the map request submission is done.
    let poll_type = wgt::PollType::Wait {
        submission_index: Some(submission_index),
        timeout: Some(Duration::from_secs(60)),
    };
    if let Err(err) = global.device_poll(device_id, poll_type) {
        return BufferMapAsyncStatus::from(Err(err));
    }

    // We could just lock the mutex and unwrap the status, but let's take it
    // step by step and check everything is as we expect.

    // unwrap: `status_passback` should be the only owner of the `Arc`.
    let status_oncelock = Arc::into_inner(status_passback).unwrap();

    // unwrap: the `OnceLock` should have been initialized.
    let status_result = status_oncelock.into_inner().unwrap();

    BufferMapAsyncStatus::from(status_result)
}

/// The result of getting a mapped range.
///
/// On success, this returns the range's start address and size, as
/// well as the [`BufferMapping`] tracking its lifetime. Unmapping or
/// destroying the buffer will block until the `BufferMapping` is
/// dropped.
///
/// On failure, `core_mapping` and `ptr` are null.
///
/// [`BufferMapping`]: wgc::resource::BufferMapping
#[repr(C)]
pub struct BufferMappingResult {
    /// A `Box<BufferMapping>` as a raw pointer, or `null` if the mapping was cancelled.
    pub core_mapping: *mut wgc::resource::BufferMapping,

    /// The start of the mapped range, or `null` if the mapping was cancelled.
    pub ptr: *mut u8,

    /// The length of the mapped range in bytes.
    pub length: u64,
}

/// # Safety
///
/// This function is unsafe as there is no guarantee that the given pointer is
/// valid for `size` elements.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_buffer_get_mapped_range(
    global: &Global,
    buffer_id: id::BufferId,
    start: wgt::BufferAddress,
    size: wgt::BufferAddress,
) -> BufferMappingResult {
    match global.buffer_get_mapped_range(buffer_id, start, Some(size)) {
        Ok(mapping) => BufferMappingResult {
            ptr: mapping.ptr().as_ptr(),
            length: mapping.len(),
            core_mapping: Box::into_raw(Box::new(mapping)),
        },
        Err(error) => match error {
            // The map may have been cancelled before the caller got here:
            // `buffer.destroy()` destroys the resource and `buffer.unmap()`
            // returns it to the idle state. Report an empty slice so the
            // caller can turn it into a map error.
            BufferAccessError::DestroyedResource(_) | BufferAccessError::NotMapped => {
                BufferMappingResult {
                    core_mapping: core::ptr::null_mut(),
                    ptr: core::ptr::null_mut(),
                    length: 0,
                }
            }
            _ => panic!("{error}"),
        },
    }
}

/// # Safety
///
/// This function must be passed a valid pointer that was created by
/// calling `Box::into_raw` on a `Box<wgpu_core::resource::BufferMapping>`.
/// (`BufferMappingResult::core_mapping`, when non-null, is such a
/// pointer.)
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_buffer_mapping_destroy(
    mapping: *mut wgc::resource::BufferMapping,
) {
    assert!(!mapping.is_null());
    // Safety: this function's contract requires that `mapping` is
    // valid and created by `Box::into_raw`.
    drop(unsafe { Box::from_raw(mapping) });
}

#[no_mangle]
pub extern "C" fn wgpu_server_buffer_unmap(global: &Global, buffer_id: id::BufferId) {
    global.buffer_unmap(buffer_id)
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_device_create_texture(
    global: &Global,
    device_id: id::DeviceId,
    id_in: id::TextureId,
    desc: &FfiTextureDescriptor,
) {
    global.device_create_texture(device_id, &desc.to_wgpu(), id_in);
}

#[no_mangle]
pub extern "C" fn wgpu_server_texture_destroy(global: &Global, id: id::TextureId) {
    global.texture_destroy(id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_texture_drop(global: &Global, id: id::TextureId) {
    global.texture_remove(id);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_texture_create_view(
    global: &Global,
    _device_id: id::DeviceId,
    texture_id: id::TextureId,
    id_in: id::TextureViewId,
    desc: &crate::FfiTextureViewDescriptor,
) {
    let desc = TextureViewDescriptor {
        label: wgpu_string(desc.label),
        format: desc.format.cloned(),
        dimension: desc.dimension.cloned(),
        range: wgt::ImageSubresourceRange {
            aspect: desc.aspect,
            base_mip_level: desc.base_mip_level,
            mip_level_count: desc.mip_level_count.map(|ptr| *ptr),
            base_array_layer: desc.base_array_layer,
            array_layer_count: desc.array_layer_count.map(|ptr| *ptr),
        },
        usage: Some(desc.usage),
        swizzle: Default::default(),
    };
    global.texture_create_view(texture_id, &desc, id_in);
}

#[no_mangle]
pub extern "C" fn wgpu_server_texture_view_drop(global: &Global, id: id::TextureViewId) {
    global.texture_view_remove(id);
}

#[allow(unused_variables)]
#[no_mangle]
#[cfg(target_os = "windows")]
pub extern "C" fn wgpu_server_get_device_fence_handle(
    global: &Global,
    device_id: id::DeviceId,
) -> *mut core::ffi::c_void {
    let device = global.resolve_device_id(device_id);

    let dx12_device = unsafe { device.clone().as_hal::<wgc::api::Dx12>() }
        .expect("D3D12 backend device on windows")
        .raw_device()
        .clone();

    let dx12_fence = unsafe { device.fence_as_hal::<wgc::api::Dx12>() }
        .expect("D3D12 backend fence on windows")
        .raw_fence()
        .clone();

    match unsafe {
        dx12_device.CreateSharedHandle(&dx12_fence, None, Foundation::GENERIC_ALL.0, None)
    } {
        Ok(handle) => handle.0,
        Err(_) => ptr::null_mut(),
    }
}

#[derive(Debug, Default)]
#[repr(C)]
pub struct DMABufInfo {
    pub is_valid: bool,
    pub modifier: u64,
    pub plane_count: u32,
    pub offsets: [u64; 3],
    pub strides: [u64; 3],
}

/// Destroys a `VkImage` and its dedicated `VkDeviceMemory` unless ownership has
/// been handed over with [`ScopedVkImage::release`].
#[cfg(target_os = "linux")]
struct ScopedVkImage<'a> {
    device: &'a ash::Device,
    image: vk::Image,
    memory: vk::DeviceMemory,
}

#[cfg(target_os = "linux")]
impl<'a> ScopedVkImage<'a> {
    fn new(device: &'a ash::Device, image: vk::Image) -> Self {
        Self {
            device,
            image,
            memory: vk::DeviceMemory::null(),
        }
    }

    fn release(&mut self) {
        self.image = vk::Image::null();
        self.memory = vk::DeviceMemory::null();
    }
}

#[cfg(target_os = "linux")]
impl Drop for ScopedVkImage<'_> {
    fn drop(&mut self) {
        if self.image != vk::Image::null() {
            unsafe {
                self.device.destroy_image(self.image, None);
            }
        }
        if self.memory != vk::DeviceMemory::null() {
            unsafe {
                self.device.free_memory(self.memory, None);
            }
        }
    }
}

/// Queries the memory requirements of `image` through
/// `vkGetImageMemoryRequirements2`. The caller always makes a dedicated
/// allocation, so the dedicated requirements are only reported, never acted on.
#[cfg(target_os = "linux")]
unsafe fn dedicated_image_memory_requirements(
    instance: &ash::Instance,
    device: &ash::Device,
    image: vk::Image,
) -> vk::MemoryRequirements {
    let loader = khr::get_memory_requirements2::Device::new(instance, device);

    let mut dedicated_requirements = vk::MemoryDedicatedRequirements::default();
    let requirements = {
        let mut requirements2 =
            vk::MemoryRequirements2::default().push_next(&mut dedicated_requirements);
        let info = vk::ImageMemoryRequirementsInfo2::default().image(image);
        unsafe {
            loader.get_image_memory_requirements2(&info, &mut requirements2);
        }
        requirements2.memory_requirements
    };

    if dedicated_requirements.requires_dedicated_allocation == vk::FALSE
        && dedicated_requirements.prefers_dedicated_allocation == vk::FALSE
    {
        let msg = c"dmabuf image neither requires nor prefers a dedicated allocation";
        unsafe {
            gfx_critical_note(msg.as_ptr());
        }
    }

    requirements
}

/// Allocates a dma-buf backed `VkImage`, exports the dma-buf file descriptor to
/// `out_fd` (-1 on failure) and returns its layout.
///
/// The `VkImage` and its memory are destroyed before returning: the exported
/// file descriptor keeps the underlying dma-buf alive on its own, and the image
/// is never used in a submission, so there is nothing for
/// `VUID-vkDestroyImage-image-01000` or `VUID-vkFreeMemory-memory-00677` to
/// wait on.
#[no_mangle]
#[cfg(target_os = "linux")]
pub extern "C" fn wgpu_vkimage_create_with_dma_buf(
    global: &Global,
    device_id: id::DeviceId,
    width: u32,
    height: u32,
    out_fd: *mut i32,
) -> DMABufInfo {
    match create_dma_buf(global, device_id, width, height) {
        Ok((info, fd)) => {
            unsafe { *out_fd = fd };
            info
        }
        Err(msg) => {
            unsafe { gfx_critical_note(msg.as_ptr()) };
            unsafe { *out_fd = -1 };
            DMABufInfo::default()
        }
    }
}

#[cfg(target_os = "linux")]
fn create_dma_buf(
    global: &Global,
    device_id: id::DeviceId,
    width: u32,
    height: u32,
) -> Result<(DMABufInfo, i32), CString> {
    let device = global.resolve_device_id(device_id);
    unsafe {
        let hal_device = device
            .as_hal::<wgc::api::Vulkan>()
            .expect("Vulkan backend device on linux");

        let device = hal_device.raw_device();
        let physical_device = hal_device.raw_physical_device();
        let instance = hal_device.shared_instance().raw_instance();

        let count = {
            let mut drm_format_modifier_props_list =
                vk::DrmFormatModifierPropertiesListEXT::default();
            let mut format_properties_2 =
                vk::FormatProperties2::default().push_next(&mut drm_format_modifier_props_list);

            instance.get_physical_device_format_properties2(
                physical_device,
                vk::Format::B8G8R8A8_UNORM,
                &mut format_properties_2,
            );
            drm_format_modifier_props_list.drm_format_modifier_count
        };

        if count == 0 {
            return Err(c"get_physical_device_format_properties2() failed".to_owned());
        }

        let mut modifier_props =
            vec![vk::DrmFormatModifierPropertiesEXT::default(); count as usize];

        let mut drm_format_modifier_props_list = vk::DrmFormatModifierPropertiesListEXT::default()
            .drm_format_modifier_properties(&mut modifier_props);
        let mut format_properties_2 =
            vk::FormatProperties2::default().push_next(&mut drm_format_modifier_props_list);

        instance.get_physical_device_format_properties2(
            physical_device,
            vk::Format::B8G8R8A8_UNORM,
            &mut format_properties_2,
        );

        let mut usage_flags = vk::ImageUsageFlags::empty();
        usage_flags |= vk::ImageUsageFlags::COLOR_ATTACHMENT;

        modifier_props.retain(|modifier_prop| {
            is_dmabuf_supported(
                instance,
                physical_device,
                vk::Format::B8G8R8A8_UNORM,
                modifier_prop.drm_format_modifier,
                usage_flags,
            )
        });

        if modifier_props.is_empty() {
            return Err(c"format not supported for dmabuf import".to_owned());
        }

        let Some(consumer_modifiers) = get_linux_dmabuf_modifiers() else {
            return Err(c"failed to get consumer dmabuf modifiers".to_owned());
        };

        modifier_props.retain(|modifier_prop| {
            consumer_modifiers.contains(&modifier_prop.drm_format_modifier)
        });

        if modifier_props.is_empty() {
            let msg =
                c"no common dmabuf modifier found for WebGPU shared-texture swapchain".to_owned();
            return Err(msg);
        }

        let modifiers: Vec<u64> = modifier_props
            .iter()
            .map(|modifier_prop| modifier_prop.drm_format_modifier)
            .collect();

        let mut modifier_list =
            vk::ImageDrmFormatModifierListCreateInfoEXT::default().drm_format_modifiers(&modifiers);

        let extent = vk::Extent3D {
            width,
            height,
            depth: 1,
        };

        let mut external_image_create_info = vk::ExternalMemoryImageCreateInfo::default()
            .handle_types(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

        let mut export_memory_alloc_info = vk::ExportMemoryAllocateInfo::default()
            .handle_types(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

        let vk_info = vk::ImageCreateInfo::default()
            .flags(vk::ImageCreateFlags::ALIAS)
            .image_type(vk::ImageType::TYPE_2D)
            // Bug 1971883: Rather than hard-coding this format, we should use
            // whatever format was negotiated between `GPUCanvasContext.configure`
            // and the GPU process.
            .format(vk::Format::B8G8R8A8_UNORM)
            .extent(extent)
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::DRM_FORMAT_MODIFIER_EXT)
            .usage(usage_flags)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .initial_layout(vk::ImageLayout::UNDEFINED)
            .push_next(&mut modifier_list)
            .push_next(&mut external_image_create_info);

        let image = match device.create_image(&vk_info, None) {
            Err(err) => {
                let msg = CString::new(format!("create_image() failed: {:?}", err)).unwrap();
                return Err(msg);
            }
            Ok(image) => image,
        };

        // From here on, every early return destroys the image (and, once
        // allocated, its memory) through this guard. The success path destroys
        // them too, after the dma-buf fd has been exported.
        let mut scoped_image = ScopedVkImage::new(device, image);

        let mut image_modifier_properties = vk::ImageDrmFormatModifierPropertiesEXT::default();
        let image_drm_format_modifier =
            ash::ext::image_drm_format_modifier::Device::new(instance, device);
        let ret = image_drm_format_modifier
            .get_image_drm_format_modifier_properties(image, &mut image_modifier_properties);
        if ret.is_err() {
            let msg = CString::new(format!(
                "get_image_drm_format_modifier_properties() failed: {:?}",
                ret
            ))
            .unwrap();
            return Err(msg);
        }

        let memory_req = dedicated_image_memory_requirements(instance, device, image);
        let mem_properties = instance.get_physical_device_memory_properties(physical_device);
        let memory_type_index = select_memory_type(
            &mem_properties,
            vk::MemoryPropertyFlags::DEVICE_LOCAL,
            memory_req.memory_type_bits,
        );
        let Some(memory_type_index) = memory_type_index else {
            let msg = c"Failed to get DEVICE_LOCAL memory index";
            return Err(msg.to_owned());
        };

        let mut dedicated_memory_info = vk::MemoryDedicatedAllocateInfo::default().image(image);

        let memory_allocate_info = vk::MemoryAllocateInfo::default()
            .allocation_size(memory_req.size)
            .memory_type_index(memory_type_index)
            .push_next(&mut dedicated_memory_info)
            .push_next(&mut export_memory_alloc_info);

        let memory = match device.allocate_memory(&memory_allocate_info, None) {
            Err(err) => {
                let msg = CString::new(format!("allocate_memory() failed: {:?}", err)).unwrap();
                return Err(msg);
            }
            Ok(memory) => memory,
        };
        scoped_image.memory = memory;

        let result = device.bind_image_memory(image, memory, /* offset */ 0);
        if result.is_err() {
            let msg = CString::new(format!("bind_image_memory() failed: {:?}", result)).unwrap();
            return Err(msg);
        }

        let modifier_prop = modifier_props
            .iter()
            .find(|prop| prop.drm_format_modifier == image_modifier_properties.drm_format_modifier);
        let Some(modifier_prop) = modifier_prop else {
            let msg = c"failed to find modifier_prop".to_owned();
            return Err(msg);
        };

        let plane_count = modifier_prop.drm_format_modifier_plane_count;
        if plane_count as usize > 3 {
            let msg = c"too many dmabuf memory planes".to_owned();
            return Err(msg);
        }

        let mut offsets: [u64; 3] = [0; 3];
        let mut strides: [u64; 3] = [0; 3];
        for i in 0..plane_count {
            // VUID-vkGetImageSubresourceLayout-tiling-09433: For
            // `DMA_BUF` images, the planes must be identified using the
            // `MEMORY_PLANE_i_EXT bits, not the `PLANE_i` bits.
            let flag = match i {
                0 => vk::ImageAspectFlags::MEMORY_PLANE_0_EXT,
                1 => vk::ImageAspectFlags::MEMORY_PLANE_1_EXT,
                2 => vk::ImageAspectFlags::MEMORY_PLANE_2_EXT,
                _ => unreachable!(),
            };
            let subresource = vk::ImageSubresource::default().aspect_mask(flag);
            let layout = device.get_image_subresource_layout(image, subresource);
            offsets[i as usize] = layout.offset;
            strides[i as usize] = layout.row_pitch;
        }

        // `vkGetMemoryFdKHR` returns a new file descriptor holding a reference
        // to the memory object's payload, so the dma-buf outlives the
        // `VkDeviceMemory` we free below. This must happen before the free.
        let get_fd_info = vk::MemoryGetFdInfoKHR::default()
            .memory(memory)
            .handle_type(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);
        let loader = khr::external_memory_fd::Device::new(instance, device);
        let fd = match loader.get_memory_fd(&get_fd_info) {
            Err(err) => {
                let msg = CString::new(format!("get_memory_fd() failed: {:?}", err)).unwrap();
                return Err(msg);
            }
            Ok(fd) => fd,
        };

        Ok((
            DMABufInfo {
                is_valid: true,
                modifier: image_modifier_properties.drm_format_modifier,
                plane_count,
                offsets,
                strides,
            },
            fd,
        ))
    }
}

/// The caller assumes responsibility for one reference to the returned shared
/// event, and must release it when no longer required.
#[no_mangle]
#[allow(unreachable_code)]
#[allow(unused_variables)]
pub extern "C" fn wgpu_server_get_device_fence_metal_shared_event(
    global: &Global,
    device_id: id::DeviceId,
) -> *mut c_void {
    #[cfg(target_os = "macos")]
    {
        use objc2::Message as _;

        let device = global.resolve_device_id(device_id);
        let shared_event = unsafe { device.fence_as_hal::<wgc::api::Metal>() }
            .map(|fence| fence.raw_shared_event().unwrap().retain());
        let shared_event = match shared_event {
            Some(shared_event) => shared_event,
            None => {
                return ptr::null_mut();
            }
        };
        return objc2::rc::Retained::into_raw(shared_event).cast();
    }

    ptr::null_mut()
}

extern "C" {
    #[allow(dead_code)]
    fn gfx_critical_note(msg: *const c_char);
    fn wgpu_server_use_shared_texture_for_swap_chain(
        parent: WebGPUParentPtr,
        swap_chain_id: SwapChainId,
    ) -> bool;
    fn wgpu_server_disable_shared_texture_for_swap_chain(
        parent: WebGPUParentPtr,
        swap_chain_id: SwapChainId,
    );
    #[allow(dead_code)]
    fn wgpu_server_ensure_shared_texture_for_swap_chain(
        parent: WebGPUParentPtr,
        swap_chain_id: SwapChainId,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        width: u32,
        height: u32,
        format: wgt::TextureFormat,
        usage: wgt::TextureUsages,
    ) -> bool;
    fn wgpu_server_ensure_shared_texture_for_readback(
        parent: WebGPUParentPtr,
        swap_chain_id: SwapChainId,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        width: u32,
        height: u32,
        format: wgt::TextureFormat,
        usage: wgt::TextureUsages,
    );
    #[cfg(target_os = "windows")]
    fn wgpu_server_get_shared_texture_handle(
        parent: WebGPUParentPtr,
        id: id::TextureId,
    ) -> *mut core::ffi::c_void;
    #[cfg(target_os = "linux")]
    fn wgpu_server_get_dma_buf_info(parent: WebGPUParentPtr, id: id::TextureId) -> DMABufInfo;
    #[cfg(target_os = "linux")]
    fn wgpu_server_get_dma_buf_fd(parent: WebGPUParentPtr, id: id::TextureId) -> i32;
    #[cfg(target_os = "linux")]
    fn wgpu_server_get_linux_dmabuf_modifiers(
        modifiers: *mut *const u64,
        modifier_count: *mut u32,
    ) -> bool;
    #[cfg(target_os = "macos")]
    fn wgpu_server_get_external_io_surface_id(parent: WebGPUParentPtr, id: id::TextureId) -> u32;
    fn wgpu_server_remove_shared_texture(parent: WebGPUParentPtr, id: id::TextureId);
    fn wgpu_parent_is_external_texture_enabled() -> bool;
    fn wgpu_parent_external_texture_source_get_external_texture_descriptor<'a>(
        parent: WebGPUParentPtr,
        id: crate::ExternalTextureSourceId,
        dest_color_space: crate::PredefinedColorSpace,
    ) -> crate::ExternalTextureDescriptorFromSource<'a>;
    fn wgpu_parent_destroy_external_texture_source(
        parent: WebGPUParentPtr,
        id: crate::ExternalTextureSourceId,
    );
    fn wgpu_parent_drop_external_texture_source(
        parent: WebGPUParentPtr,
        id: crate::ExternalTextureSourceId,
    );
    fn wgpu_server_dealloc_buffer_shmem(parent: WebGPUParentPtr, id: id::BufferId);
    fn wgpu_server_pre_device_drop(parent: WebGPUParentPtr, id: id::DeviceId);
    fn wgpu_server_set_buffer_map_data(
        parent: WebGPUParentPtr,
        device_id: id::DeviceId,
        buffer_id: id::BufferId,
        has_map_flags: bool,
        mapped_offset: u64,
        mapped_size: u64,
        is_mapped: bool,
        shmem_index: usize,
    );
    fn wgpu_parent_buffer_unmap(
        parent: WebGPUParentPtr,
        device_id: id::DeviceId,
        buffer_id: id::BufferId,
        flush: bool,
    );
    fn wgpu_parent_queue_submit(
        parent: WebGPUParentPtr,
        queue_id: id::QueueId,
        command_buffer_ids: *const id::CommandBufferId,
        command_buffer_ids_length: usize,
        texture_ids: *const id::TextureId,
        texture_ids_length: usize,
        external_texture_source_ids: *const crate::ExternalTextureSourceId,
        external_texture_source_ids_length: usize,
    );
    fn wgpu_parent_create_swap_chain(
        parent: WebGPUParentPtr,
        device_id: id::DeviceId,
        queue_id: id::QueueId,
        width: u32,
        height: u32,
        format: crate::SurfaceFormat,
        buffer_ids: *const id::BufferId,
        buffer_ids_length: usize,
        remote_texture_owner_id: crate::RemoteTextureOwnerId,
        use_shared_texture_in_swap_chain: bool,
    );
    fn wgpu_parent_swap_chain_present(
        parent: WebGPUParentPtr,
        texture_id: id::TextureId,
        command_encoder_id: id::CommandEncoderId,
        command_buffer_id: id::CommandBufferId,
        remote_texture_id: crate::RemoteTextureId,
        remote_texture_owner_id: crate::RemoteTextureOwnerId,
    );
    fn wgpu_parent_swap_chain_drop(
        parent: WebGPUParentPtr,
        remote_texture_owner_id: crate::RemoteTextureOwnerId,
        txn_type: crate::RemoteTextureTxnType,
        txn_id: crate::RemoteTextureTxnId,
    );
    #[cfg(target_os = "windows")]
    fn wgpu_parent_get_compositor_device_luid(out_luid: *mut crate::FfiLUID) -> bool;
    fn wgpu_parent_post_request_device(parent: WebGPUParentPtr, device_id: id::DeviceId);
    fn wgpu_parent_build_buffer_map_closure(
        parent: WebGPUParentPtr,
        device_id: id::DeviceId,
        buffer_id: id::BufferId,
        mode: HostMap,
        offset: u64,
        size: u64,
    ) -> BufferMapClosure;
    fn wgpu_parent_build_submitted_work_done_closure(
        parent: WebGPUParentPtr,
        queue_id: id::QueueId,
    ) -> SubmittedWorkDoneClosure;
    fn wgpu_parent_send_server_message(parent: WebGPUParentPtr, message: &mut ByteBuf);
    fn wgpu_parent_weak_ptr_new(parent: WebGPUParentPtr) -> *mut core::ffi::c_void;
    fn wgpu_parent_weak_ptr_get(weak_ptr: *mut core::ffi::c_void) -> WebGPUParentPtr;
    fn wgpu_parent_weak_ptr_delete(weak_ptr: *mut core::ffi::c_void);
    fn wgpu_texture_format_is_valid_for_webidl(format: *const nsCString) -> bool;
}

#[cfg(target_os = "linux")]
pub unsafe fn is_dmabuf_supported(
    instance: &ash::Instance,
    physical_device: vk::PhysicalDevice,
    format: vk::Format,
    modifier: u64,
    usage: vk::ImageUsageFlags,
) -> bool {
    let mut drm_props = vk::ExternalImageFormatProperties::default();
    let mut props = vk::ImageFormatProperties2::default().push_next(&mut drm_props);

    let mut modifier_info =
        vk::PhysicalDeviceImageDrmFormatModifierInfoEXT::default().drm_format_modifier(modifier);

    let mut external_format_info = vk::PhysicalDeviceExternalImageFormatInfo::default()
        .handle_type(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

    let format_info = vk::PhysicalDeviceImageFormatInfo2::default()
        .format(format)
        .ty(vk::ImageType::TYPE_2D)
        .usage(usage)
        .tiling(vk::ImageTiling::DRM_FORMAT_MODIFIER_EXT)
        .push_next(&mut external_format_info)
        .push_next(&mut modifier_info);

    let res = unsafe {
        instance.get_physical_device_image_format_properties2(
            physical_device,
            &format_info,
            &mut props,
        )
    };
    match res {
        Ok(_) => (),
        Err(_) => {
            //debug!(?format, ?modifier, "format not supported for dma import");
            return false;
        }
    }

    if !drm_props
        .external_memory_properties
        .compatible_handle_types
        .contains(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT)
    {
        return false;
    }

    drm_props
        .external_memory_properties
        .external_memory_features
        .contains(
            vk::ExternalMemoryFeatureFlags::EXPORTABLE | vk::ExternalMemoryFeatureFlags::IMPORTABLE,
        )
}

#[cfg(target_os = "linux")]
pub fn select_memory_type(
    props: &vk::PhysicalDeviceMemoryProperties,
    flags: vk::MemoryPropertyFlags,
    memory_type_bits: u32,
) -> Option<u32> {
    for i in 0..props.memory_type_count {
        if memory_type_bits & (1 << i) == 0 {
            continue;
        }

        if props.memory_types[i as usize]
            .property_flags
            .contains(flags)
        {
            return Some(i);
        }
    }

    None
}

impl Global {
    /// Import a D3D texture (`ID3D11Resource`) shared handle to a wgpu texture.
    ///
    /// Three outcomes are possible:
    /// - The texture is imported successfully. Return value is `None`.
    /// - The import fails before the call to `create_texture_from_hal`.
    ///   Return value is `Some`, containing the provided `ScopeGuard`,
    ///   which should populate the texture as invalid.
    /// - The import failed within the call to `create_texture_from_hal`.
    ///   In this case `create_texture_from_hal` populates an invalid
    ///   texture. The `ScopeGuard` is defused and the return value is
    ///   `None`.
    ///
    /// This function only supports plain 2D color textures. Multisampled,
    /// depth/stencil, layered, mipmapped, and volume textures are not
    /// supported.
    #[cfg(target_os = "windows")]
    fn create_texture_with_imported_d3d11_resource<'a, F: FnOnce(()) + 'a>(
        &self,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        desc: &TextureDescriptor,
        handle: *mut core::ffi::c_void,
        guard: ScopeGuard<(), F>,
    ) -> Option<ScopeGuard<(), F>> {
        assert!(desc.dimension == wgt::TextureDimension::D2);
        assert!(desc.sample_count == 1);
        assert!(desc.size.depth_or_array_layers == 1);
        assert!(desc.mip_level_count == 1);

        let device = self.resolve_device_id(device_id);
        let dx12_device = unsafe { device.as_hal::<wgc::api::Dx12>() }
            .expect("D3D12 backend device on windows")
            .raw_device()
            .clone();
        let mut resource: Option<Direct3D12::ID3D12Resource> = None;
        let res =
            unsafe { dx12_device.OpenSharedHandle(Foundation::HANDLE(handle), &mut resource) };
        if res.is_err() || resource.is_none() {
            let msg = c"Failed to open shared handle";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return Some(guard);
        }

        let hal_texture = unsafe {
            <wgh::api::Dx12 as wgh::Api>::Device::texture_from_raw(
                resource.unwrap(),
                desc.format,
                wgt::TextureDimension::D2,
                desc.size,
                1,
                1,
                None,
            )
        };

        // `create_texture_from_hal` populates an invalid texture on failure, so
        // defuse the guard now.
        ScopeGuard::into_inner(guard);

        let (_, error) = unsafe {
            self.create_texture_from_hal(
                Box::new(hal_texture),
                device_id,
                &desc,
                wgt::TextureUses::UNINITIALIZED,
                texture_id,
                // NOTE: For swapchain textures (but not external textures), we want `false`,
                // but we can't yet, because upstream's API isn't quite right yet.
                /* cleared */
                true,
            )
        };
        if let Some(err) = error {
            let msg = CString::new(format!("create_texture_from_hal() failed: {:?}", err)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            // Texture was already populated as invalid.
        }

        None
    }

    #[cfg(target_os = "linux")]
    fn create_texture_with_imported_dmabuf<'a, F: FnOnce(()) + 'a>(
        &self,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        desc: &TextureDescriptor,
        guard: ScopeGuard<(), F>,
    ) -> Option<ScopeGuard<(), F>> {
        unsafe {
            let info = wgpu_server_get_dma_buf_info(self.owner, texture_id);
            if !info.is_valid {
                let msg = c"Failed to get DMABufInfo";
                gfx_critical_note(msg.as_ptr());
                return Some(guard);
            }
            if info.plane_count as usize > info.offsets.len() {
                let msg = c"Invalid dmabuf plane count";
                gfx_critical_note(msg.as_ptr());
                return Some(guard);
            }

            let fd = wgpu_server_get_dma_buf_fd(self.owner, texture_id);
            if fd < 0 {
                let msg = c"Failed to get DMABuf fd";
                gfx_critical_note(msg.as_ptr());
                return Some(guard);
            }

            // Ensure to close file descriptor
            let owned_fd = OwnedFd::from_raw_fd(fd as RawFd);

            let device = self.resolve_device_id(device_id);
            let hal_device = device
                .as_hal::<wgc::api::Vulkan>()
                .expect("Vulkan backend device on linux");

            let device = hal_device.raw_device();
            let instance = hal_device.shared_instance().raw_instance();
            let physical_device = hal_device.raw_physical_device();

            let extent = vk::Extent3D {
                width: desc.size.width,
                height: desc.size.height,
                depth: 1,
            };
            let mut usage_flags = vk::ImageUsageFlags::empty();
            usage_flags |= vk::ImageUsageFlags::COLOR_ATTACHMENT;

            let mut external_image_create_info = vk::ExternalMemoryImageCreateInfo::default()
                .handle_types(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

            // Surprising rule:
            //
            // > VUID-VkImageDrmFormatModifierExplicitCreateInfoEXT-size-02267:
            // > For each element of pPlaneLayouts, size must be 0
            //
            // Rationale:
            //
            // > In each element of pPlaneLayouts, the implementation must ignore
            // > size. The implementation calculates the size of each plane, which
            // > the application can query with vkGetImageSubresourceLayout.
            //
            // So, the plane layouts we rebuild here leave `size` at 0.
            let memory_plane_layouts: Vec<_> = (0..info.plane_count as usize)
                .map(|i| vk::SubresourceLayout {
                    offset: info.offsets[i],
                    size: 0,
                    row_pitch: info.strides[i],
                    array_pitch: 0,
                    depth_pitch: 0,
                })
                .collect();

            // VUID-VkImageCreateInfo-pNext-00990
            //
            // Since `wgpu_vkimage_create_with_dma_buf` above succeeded in
            // creating the original DMABuf image, if we pass the same
            // parameters, including the DRM format modifier and plane layouts,
            // we can assume that this call will succeed too.
            //
            // The only thing we're adding is the `ALIAS` flag, because this
            // aliases the original image.
            let mut modifier_list = vk::ImageDrmFormatModifierExplicitCreateInfoEXT::default()
                .drm_format_modifier(info.modifier)
                .plane_layouts(&memory_plane_layouts);

            let vk_info = vk::ImageCreateInfo::default()
                .flags(vk::ImageCreateFlags::ALIAS)
                .image_type(vk::ImageType::TYPE_2D)
                // Bug 1971883: Rather than hard-coding this format, we should use
                // whatever format was negotiated between `GPUCanvasContext.configure`
                // and the GPU process.
                .format(vk::Format::B8G8R8A8_UNORM)
                .extent(extent)
                .mip_levels(1)
                .array_layers(1)
                .samples(vk::SampleCountFlags::TYPE_1)
                .tiling(vk::ImageTiling::DRM_FORMAT_MODIFIER_EXT)
                .usage(usage_flags)
                .sharing_mode(vk::SharingMode::EXCLUSIVE)
                .initial_layout(vk::ImageLayout::UNDEFINED)
                .push_next(&mut modifier_list)
                .push_next(&mut external_image_create_info);

            let image = match device.create_image(&vk_info, None) {
                Err(err) => {
                    let msg = CString::new(format!(
                        "Failed to get vk::Image: create_image() failed: {:?}",
                        err
                    ))
                    .unwrap();
                    gfx_critical_note(msg.as_ptr());
                    return Some(guard);
                }
                Ok(image) => image,
            };

            // Every early return below destroys the image, and its memory once
            // allocated, through this guard.
            let mut scoped_image = ScopedVkImage::new(device, image);

            // The memory requirements of this image are not necessarily the
            // ones of the image the dma-buf was allocated from, so they must be
            // queried afresh: `VUID-VkMemoryAllocateInfo-memoryTypeIndex-00648`
            // and `VUID-VkMemoryDedicatedAllocateInfo-image-02964`.
            let memory_req = dedicated_image_memory_requirements(instance, device, image);

            let mut fd_props = vk::MemoryFdPropertiesKHR::default();
            let external_memory_fd = khr::external_memory_fd::Device::new(instance, device);
            if let Err(err) = external_memory_fd.get_memory_fd_properties(
                vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT,
                owned_fd.as_raw_fd(),
                &mut fd_props,
            ) {
                let msg =
                    CString::new(format!("get_memory_fd_properties() failed: {:?}", err)).unwrap();
                gfx_critical_note(msg.as_ptr());
                return Some(guard);
            }

            let mem_properties = instance.get_physical_device_memory_properties(physical_device);
            let memory_type_index = select_memory_type(
                &mem_properties,
                vk::MemoryPropertyFlags::DEVICE_LOCAL,
                memory_req.memory_type_bits & fd_props.memory_type_bits,
            );
            let Some(memory_type_index) = memory_type_index else {
                let msg = c"Failed to find a memory type for the imported dmabuf";
                gfx_critical_note(msg.as_ptr());
                return Some(guard);
            };

            let mut dedicated_memory_info = vk::MemoryDedicatedAllocateInfo::default().image(image);

            let mut import_memory_fd_info = vk::ImportMemoryFdInfoKHR::default()
                .handle_type(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT)
                .fd(owned_fd.as_raw_fd());

            let memory_allocate_info = vk::MemoryAllocateInfo::default()
                .allocation_size(memory_req.size)
                .memory_type_index(memory_type_index)
                .push_next(&mut dedicated_memory_info)
                .push_next(&mut import_memory_fd_info);

            // `vkAllocateMemory` takes ownership of the fd on success only, so
            // `owned_fd` closes it for us on every failure path above and here.
            let memory = match device.allocate_memory(&memory_allocate_info, None) {
                Err(err) => {
                    let msg = CString::new(format!(
                        "Failed to get vk::Image: allocate_memory() failed: {:?}",
                        err
                    ))
                    .unwrap();
                    gfx_critical_note(msg.as_ptr());
                    return Some(guard);
                }
                Ok(memory) => {
                    let _ = owned_fd.into_raw_fd();
                    memory
                }
            };
            scoped_image.memory = memory;

            match device.bind_image_memory(image, memory, /* offset */ 0) {
                Ok(()) => {}
                Err(err) => {
                    let msg = CString::new(format!(
                        "Failed to get vk::Image: bind_image_memory() failed: {:?}",
                        err
                    ))
                    .unwrap();
                    gfx_critical_note(msg.as_ptr());
                    return Some(guard);
                }
            }

            let hal_desc = wgh::TextureDescriptor {
                label: None,
                size: desc.size,
                mip_level_count: desc.mip_level_count,
                sample_count: desc.sample_count,
                dimension: desc.dimension,
                format: desc.format,
                usage: wgt::TextureUses::COPY_DST | wgt::TextureUses::COLOR_TARGET,
                memory_flags: wgh::MemoryFlags::empty(),
                view_formats: vec![],
            };

            let hal_texture = <wgh::api::Vulkan as wgh::Api>::Device::texture_from_raw(
                &hal_device,
                image,
                &hal_desc,
                None,
                wgh::vulkan::TextureMemory::Dedicated(memory),
            );

            // `create_texture_from_hal` populates an invalid texture on failure, so
            // defuse the guard now.
            ScopeGuard::into_inner(guard);

            let (_, error) = self.create_texture_from_hal(
                Box::new(hal_texture),
                device_id,
                &desc,
                wgt::TextureUses::UNINITIALIZED,
                texture_id,
                // NOTE: For swapchain textures (but not external textures), we want `false`,
                // but we can't yet, because upstream's API isn't quite right yet.
                /* cleared */
                true,
            );
            if let Some(err) = error {
                let msg =
                    CString::new(format!("create_texture_from_hal() failed: {:?}", err)).unwrap();
                gfx_critical_note(msg.as_ptr());
                // Texture was already populated as invalid.
            }

            // We've successfully imported the texture. wgpu-core now owns
            // the image and its memory.
            scoped_image.release();

            None
        }
    }

    /// Create a shared texture for use in a no-readback swap chain.
    ///
    /// Three outcomes are possible:
    /// - The texture is created successfully. Return value is `None`.
    /// - The import fails without populating the texture as invalid. Return value is
    ///   `Some`, containing a `ScopeGuard` that would populate the texture as invalid.
    ///   However, the caller instead defuses the guard and falls back to creating a
    ///   regular texture to be presented via readback.
    /// - The import fails within a call to `create_texture_from_hal`. In this case
    ///   `create_texture_from_hal` populates an invalid texture. The `ScopeGuard` is
    ///   defused and the return value is `None`.
    fn create_shared_texture_for_swap_chain<'a>(
        &'a self,
        device_id: id::DeviceId,
        swap_chain_id: SwapChainId,
        texture_id: id::TextureId,
        desc: &'a TextureDescriptor,
    ) -> Option<ScopeGuard<(), impl FnOnce(()) + 'a>> {
        if let Some(err) = self.device_validate_texture_descriptor(device_id, &desc) {
            self.device_handle_error(device_id, err, None, "Device::create_texture");
            self.create_texture_error(device_id, texture_id, &desc);
            return None;
        }

        let guard = guard(
            (),
            Box::new(move |()| {
                self.create_texture_error(device_id, texture_id, desc);
            }),
        );

        let ret = unsafe {
            wgpu_server_ensure_shared_texture_for_swap_chain(
                self.owner,
                swap_chain_id,
                device_id,
                texture_id,
                desc.size.width,
                desc.size.height,
                desc.format,
                desc.usage,
            )
        };
        if ret != true {
            let msg = c"Failed to create shared texture";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return Some(guard);
        }

        // TODO: Use `cfg_select!` when we adopt Rust 1.95+.

        #[cfg(target_os = "windows")]
        {
            let handle = unsafe { wgpu_server_get_shared_texture_handle(self.owner, texture_id) };
            if handle.is_null() {
                let msg = c"Failed to get shared texture handle";
                unsafe {
                    gfx_critical_note(msg.as_ptr());
                }
                return Some(guard);
            }

            self.create_texture_with_imported_d3d11_resource(
                device_id, texture_id, &desc, handle, guard,
            )
        }

        #[cfg(target_os = "linux")]
        {
            self.create_texture_with_imported_dmabuf(device_id, texture_id, &desc, guard)
        }

        #[cfg(target_os = "macos")]
        {
            let io_surface_id =
                unsafe { wgpu_server_get_external_io_surface_id(self.owner, texture_id) };
            if io_surface_id == 0 {
                let msg = c"Failed to get io surface id";
                unsafe {
                    gfx_critical_note(msg.as_ptr());
                }
                return Some(guard);
            }

            self.create_texture_with_imported_iosurface(
                device_id,
                texture_id,
                &desc,
                io_surface_id,
                0,
                guard,
            )
        }

        #[cfg(not(any(target_os = "windows", target_os = "linux", target_os = "macos")))]
        {
            unreachable!()
        }

        // Our return value comes directly from whichever `create_texture_with_imported_*` we
        // called, and is `None`, if the texture was created (either successfully or as an
        // invalid texture), or is `Some`, containing a `ScopeGuard` that will populate an
        // invalid texture, if there was an error.
    }

    fn device_action(
        &self,
        device_id: id::DeviceId,
        action: DeviceAction<'static>,
        shmem_mappings: FfiSlice<'_, FfiSlice<'_, u8>>,
        response_byte_buf: &mut ByteBuf,
    ) {
        match action {
            DeviceAction::CreateBuffer {
                buffer_id,
                desc,
                shmem_handle_index,
            } => {
                let has_map_flags = desc.usage.intersects(
                    wgt::BufferUsagesWebGPU::MAP_READ | wgt::BufferUsagesWebGPU::MAP_WRITE,
                );
                let needs_shmem = has_map_flags || desc.mapped_at_creation;

                let shmem_data =
                    unsafe { shmem_mappings.as_slice()[shmem_handle_index].as_slice() };

                let shmem_size = shmem_data.len();

                // If we requested a non-zero mappable buffer and get a size of zero, it
                // indicates that the shmem allocation failed on the client side or
                // mapping failed in the parent process.
                let shmem_allocation_failed = needs_shmem && (shmem_size as u64) < desc.size;
                if shmem_allocation_failed {
                    assert_eq!(shmem_size, 0);
                }

                // Don't trust the graphics driver with buffer sizes larger than our conservative max buffer size.
                if shmem_allocation_failed || desc.size > MAX_BUFFER_SIZE {
                    GPUError::oom().report(self, device_id);
                    self.create_buffer_error(device_id, buffer_id, &desc);
                    return;
                }

                self.device_create_buffer(device_id, &desc, buffer_id);

                if needs_shmem {
                    // A `mapped_at_creation` buffer starts out mapped, so the
                    // parent must know to flush the shmem contents back into it
                    // on `unmap()`. If creation failed there is nothing mapped,
                    // which `buffer_get_mapped_range` reports as an error.
                    let is_mapped = desc.mapped_at_creation
                        && self.buffer_get_mapped_range(buffer_id, 0, None).is_ok();
                    unsafe {
                        wgpu_server_set_buffer_map_data(
                            self.owner,
                            device_id,
                            buffer_id,
                            has_map_flags,
                            0,
                            if is_mapped { desc.size } else { 0 },
                            is_mapped,
                            shmem_handle_index,
                        );
                    }
                }
            }
            #[allow(unused_variables)]
            DeviceAction::CreateTexture(id, desc, swap_chain_id) => {
                let desc = if let Some(swap_chain_id) = swap_chain_id {
                    // This should have been checked on the content timeline.
                    assert!(!desc
                        .usage
                        .contains(wgt::TextureUsages::TRANSIENT_ATTACHMENT));

                    // n.b. Just because a swap chain is known, does not mean that the configuration
                    // is valid.
                    self.swap_chain_configs
                        .lock()
                        .unwrap()
                        .get(&swap_chain_id)
                        .cloned()
                        .expect("CreateTexture for unknown swap chain {swap_chain_id:?}")
                        .map_label(|_| None)
                } else {
                    desc
                };

                unsafe {
                    assert!(wgpu_texture_format_is_valid_for_webidl(&nsCString::from(
                        serde_json::to_value(&desc.format)
                            .unwrap()
                            .as_str()
                            .unwrap(),
                    ),));
                }

                let max = MAX_TEXTURE_EXTENT;
                if desc.size.width > max
                    || desc.size.height > max
                    || desc.size.depth_or_array_layers > max
                {
                    self.create_texture_error(device_id, id, &desc);
                    GPUError::oom().report(self, device_id);
                    return;
                }

                let use_shared_texture = if let Some(id) = swap_chain_id {
                    unsafe { wgpu_server_use_shared_texture_for_swap_chain(self.owner, id) }
                } else {
                    false
                };

                if use_shared_texture {
                    match self.create_shared_texture_for_swap_chain(
                        device_id,
                        swap_chain_id.unwrap(),
                        id,
                        &desc,
                    ) {
                        None => return,
                        Some(guard) => {
                            // `guard` is a scope guard that would populate an invalid texture, but
                            // we defuse the guard and fall back to the regular texture creation
                            // path (and will present by manual readback). If there is also a
                            // problem creating the regular texture, `device_create_texture`
                            // populates it as an invalid texture.
                            ScopeGuard::into_inner(guard)
                        }
                    }

                    unsafe {
                        wgpu_server_disable_shared_texture_for_swap_chain(
                            self.owner,
                            swap_chain_id.unwrap(),
                        )
                    };
                }

                if let Some(swap_chain_id) = swap_chain_id {
                    unsafe {
                        wgpu_server_ensure_shared_texture_for_readback(
                            self.owner,
                            swap_chain_id,
                            device_id,
                            id,
                            desc.size.width,
                            desc.size.height,
                            desc.format,
                            desc.usage,
                        )
                    };
                }

                self.device_create_texture(device_id, &desc, id);
            }
            DeviceAction::CreateExternalTexture(id, desc) => {
                // Obtain the descriptor from the source. A source ID of `None`
                // indicates the client-side encountered an error when
                // importing the source.
                let source_desc = desc.source.and_then(|source| {
                    let source_desc = unsafe {
                        wgpu_parent_external_texture_source_get_external_texture_descriptor(
                            self.owner,
                            source,
                            desc.color_space,
                        )
                    };
                    let planes = unsafe { source_desc.planes.as_slice() };
                    // The source having no planes indicates we encountered an
                    // error on the server side when importing the source
                    if planes.is_empty() {
                        None
                    } else {
                        Some(source_desc)
                    }
                });
                match source_desc {
                    Some(source_desc) => {
                        let planes = unsafe { source_desc.planes.as_slice() };
                        let desc = wgt::ExternalTextureDescriptor {
                            label: desc.label,
                            width: source_desc.width,
                            height: source_desc.height,
                            format: source_desc.format,
                            yuv_conversion_matrix: source_desc.yuv_conversion_matrix,
                            gamut_conversion_matrix: source_desc.gamut_conversion_matrix,
                            src_transfer_function: source_desc.src_transfer_function,
                            dst_transfer_function: source_desc.dst_transfer_function,
                            sample_transform: source_desc.sample_transform,
                            load_transform: source_desc.load_transform,
                        };
                        self.device_create_external_texture(device_id, &desc, planes, id);
                    }
                    None => {
                        // Create the external texture in an error state.
                        let desc = wgt::ExternalTextureDescriptor {
                            label: desc.label,
                            width: 0,
                            height: 0,
                            format: wgt::ExternalTextureFormat::Rgba,
                            yuv_conversion_matrix: Default::default(),
                            gamut_conversion_matrix: Default::default(),
                            src_transfer_function: Default::default(),
                            dst_transfer_function: Default::default(),
                            sample_transform: Default::default(),
                            load_transform: Default::default(),
                        };
                        self.create_external_texture_error(device_id, id, &desc);
                    }
                }
            }
            DeviceAction::CreateSampler(id, desc) => {
                self.device_create_sampler(device_id, &desc, id);
            }
            DeviceAction::CreateBindGroupLayout(id, desc) => {
                self.device_create_bind_group_layout(device_id, &desc, id);
            }
            DeviceAction::CreateBindGroupLayoutError(id, label) => {
                self.create_bind_group_layout_error(device_id, id, label);
            }
            DeviceAction::RenderPipelineGetBindGroupLayout(pipeline_id, index, bgl_id) => {
                self.render_pipeline_get_bind_group_layout(pipeline_id, index, bgl_id);
            }
            DeviceAction::ComputePipelineGetBindGroupLayout(pipeline_id, index, bgl_id) => {
                self.compute_pipeline_get_bind_group_layout(pipeline_id, index, bgl_id);
            }
            DeviceAction::CreatePipelineLayout(id, desc) => {
                self.device_create_pipeline_layout(device_id, &desc, id);
            }
            DeviceAction::CreateBindGroup(id, desc) => {
                self.device_create_bind_group(device_id, &desc, id);
            }
            DeviceAction::CreateShaderModule(id, label, code) => {
                let desc = ShaderModuleDescriptor {
                    label,
                    code: code.as_ref().into(),
                };
                self.device_create_shader_module(device_id, &desc, id);

                let wgt::CompilationInfo {
                    messages: compilation_messages,
                } = self.shader_module_compilation_info(id);

                let compilation_messages = compilation_messages
                    .into_iter()
                    .map(|m| ShaderModuleCompilationMessage::new(m, &code))
                    .collect();

                *response_byte_buf = make_byte_buf(&ServerMessage::CreateShaderModuleResponse(
                    id,
                    compilation_messages,
                ));
            }
            DeviceAction::CreateComputePipeline(id, desc, is_async) => {
                if is_async {
                    let result = self.device_create_compute_pipeline_or_error(device_id, &desc, id);
                    let error = result.err().map(pipeline_error_to_ffi);
                    *response_byte_buf =
                        make_byte_buf(&ServerMessage::CreateComputePipelineResponse {
                            pipeline_id: id,
                            error,
                        });
                } else {
                    self.device_create_compute_pipeline(device_id, &desc, id);
                }
            }
            DeviceAction::CreateRenderPipeline(id, desc, is_async) => {
                if is_async {
                    let result = self.device_create_render_pipeline_or_error(device_id, &desc, id);
                    let error = result.err().map(pipeline_error_to_ffi);
                    *response_byte_buf =
                        make_byte_buf(&ServerMessage::CreateRenderPipelineResponse {
                            pipeline_id: id,
                            error,
                        });
                } else {
                    self.device_create_render_pipeline(device_id, &desc, id);
                }
            }
            DeviceAction::CreateRenderBundleEncoder(id, desc) => {
                self.device_create_render_bundle_encoder(device_id, &desc, id);
            }
            DeviceAction::CreateQuerySet(id, desc) => {
                self.device_create_query_set(device_id, &desc, id);
            }
            DeviceAction::CreateCommandEncoder(id, desc) => {
                self.device_create_command_encoder(device_id, &desc, id);
            }
            DeviceAction::Error(error) => {
                error.report(self, device_id);
            }
            DeviceAction::PushErrorScope(filter) => {
                let filter = match filter {
                    FfiErrorFilter::Validation => ErrorFilter::Validation,
                    FfiErrorFilter::OutOfMemory => ErrorFilter::OutOfMemory,
                    FfiErrorFilter::Internal => ErrorFilter::Internal,
                };
                self.device_push_error_scope(device_id, filter);
            }
            DeviceAction::PopErrorScope => {
                let (ty, message) = match self.device_pop_error_scope(device_id) {
                    Ok(None) => (FfiPopErrorScopeResultType::NoError, "".into()),
                    Err(EmptyErrorScopeStack) => (
                        FfiPopErrorScopeResultType::EmptyErrorScopeStack,
                        "popErrorScope on empty stack".into(),
                    ),
                    Ok(Some(error)) => (
                        match error.webgpu_error_type() {
                            ErrorType::Validation => FfiPopErrorScopeResultType::Validation,
                            ErrorType::OutOfMemory => FfiPopErrorScopeResultType::OutOfMemory,
                            ErrorType::Internal => FfiPopErrorScopeResultType::Internal,
                            ErrorType::DeviceLost => unreachable!(),
                        },
                        format!("{error}").into(),
                    ),
                };
                *response_byte_buf = make_byte_buf(&ServerMessage::PopErrorScopeResponse(
                    device_id, ty, message,
                ));
            }
        }
    }

    fn texture_action(&self, self_id: id::TextureId, action: TextureAction) {
        match action {
            TextureAction::CreateView(id, desc) => {
                self.texture_create_view(self_id, &desc, id);
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_send_buffer_map_success(
    parent: WebGPUParentPtr,
    buffer_id: id::BufferId,
    is_writable: bool,
    offset: u64,
    size: u64,
) {
    let result = BufferMapResult::Success {
        is_writable,
        offset,
        size,
    };
    let mut byte_buf = make_byte_buf(&ServerMessage::BufferMapResponse(buffer_id, result));
    unsafe { wgpu_parent_send_server_message(parent, &mut byte_buf) };
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_send_buffer_map_error(
    parent: WebGPUParentPtr,
    buffer_id: id::BufferId,
    error: &nsACString,
) {
    let error = error.to_utf8();
    let result = BufferMapResult::Error(error);
    let mut byte_buf = make_byte_buf(&ServerMessage::BufferMapResponse(buffer_id, result));
    unsafe { wgpu_parent_send_server_message(parent, &mut byte_buf) };
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_pack_work_done(bb: &mut ByteBuf, queue_id: id::QueueId) {
    *bb = make_byte_buf(&ServerMessage::QueueOnSubmittedWorkDoneResponse(queue_id));
}

/// # Panics
///
/// If the size of `buffer_ids` is not [`crate::MAX_SWAPCHAIN_BUFFER_COUNT`].
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_pack_free_swap_chain_buffer_ids(
    bb: &mut ByteBuf,
    buffer_ids: FfiSlice<'_, id::BufferId>,
) {
    *bb = make_byte_buf(&ServerMessage::FreeSwapChainBufferIds(
        unsafe { buffer_ids.as_slice() }.try_into().unwrap(),
    ));
}

// Process WebGPU messages
//
// # Safety
//
// - `data_buffers` and `shmem_mappings` must be valid.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_messages(
    global: &Global,
    nr_of_messages: u32,
    serialized_messages: &ByteBuf,
    data_buffers: FfiSlice<'_, ByteBuf>,
    shmem_mappings: FfiSlice<'_, FfiSlice<'_, u8>>,
) {
    let serialized_messages = unsafe { serialized_messages.as_slice() };
    let data_buffers = unsafe { data_buffers.as_slice() };

    use bincode::Options;
    let options = bincode::DefaultOptions::new()
        .with_fixint_encoding()
        .allow_trailing_bytes();
    let mut deserializer = bincode::Deserializer::from_slice(serialized_messages, options);

    for _ in 0..nr_of_messages {
        let message: Message = serde::Deserialize::deserialize(&mut deserializer).unwrap();
        unsafe {
            process_message(global, data_buffers, shmem_mappings, message);
        }
    }
}

fn process_buffer_map(global: &Global, msg: Message, response_byte_buf: &mut ByteBuf) {
    let Message::BufferMap {
        device_id,
        buffer_id,
        mode,
        offset,
        size,
    } = msg
    else {
        unreachable!();
    };
    let mode = match mode {
        /* GPUMapMode.READ */ 1 => HostMap::Read,
        /* GPUMapMode.WRITE */ 2 => HostMap::Write,
        _ => {
            #[derive(Debug, thiserror::Error)]
            #[error(
                "GPUBuffer.mapAsync 'mode' argument must be either GPUMapMode.READ or GPUMapMode.WRITE"
            )]
            struct ModeError;

            impl WebGpuError for ModeError {
                fn webgpu_error_type(&self) -> ErrorType {
                    ErrorType::Validation
                }
            }

            global.device_handle_error(device_id, ModeError, None, "Buffer::map_async");

            // Synthesize the `BufferMapResponse` that is normally
            // generated in the callback set up below.
            let response = BufferMapResult::Error(format!("{}", ModeError).into());
            *response_byte_buf =
                make_byte_buf(&ServerMessage::BufferMapResponse(buffer_id, response));
            return;
        }
    };

    let closure = unsafe {
        wgpu_parent_build_buffer_map_closure(global.owner, device_id, buffer_id, mode, offset, size)
    };

    // Create a one-shot channel to carry the map result from the
    // `buffer_map_async` callback to this thread.
    let (map_result_sender, map_result_receiver) = futures_channel::oneshot::channel();

    // Spawn a task on this thread to wait for that map result.
    moz_task::spawn_local("process_buffer_map callback", async move {
        let result = map_result_receiver.await.unwrap();
        unsafe {
            (closure.callback)(closure.user_data, BufferMapAsyncStatus::from(result));
        }
    })
    .detach();

    let mode = mode.to_wgrt();
    let callback: Option<BufferMapCallback> = Some(Box::new(move |result| {
        // Send the map result from whatever thread this callback is running
        // on to the task we spawned above.
        map_result_sender.send(result).unwrap();
    }));
    global.buffer_map_async(buffer_id, mode, offset, Some(size), callback);
}

// Process one WebGPU message
//
// # Safety
//
// - `shmem_mappings` must be valid.
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn process_message(
    global: &Global,
    data_buffers: &[ByteBuf],
    shmem_mappings: FfiSlice<'_, FfiSlice<'_, u8>>,
    message: Message<'static>,
) {
    let response_byte_buf = &mut ByteBuf::new();

    match message {
        Message::RequestAdapter { adapter_id, desc } => {
            let mut result = None;

            // Prefer to use the dx12 backend, if one exists, and use the same DXGI adapter as WebRender.
            // If wgpu uses a different adapter than WebRender, textures created by
            // webgpu::SharedTexture do not work with wgpu.
            #[cfg(target_os = "windows")]
            {
                let mut adapter_luid = crate::FfiLUID {
                    low_part: 0,
                    high_part: 0,
                };
                if wgpu_parent_get_compositor_device_luid(&mut adapter_luid)
                    && !desc.force_fallback_adapter
                {
                    // We can't unwrap here since instance creation could have failed.
                    if let Some(instance) = global.instance().as_hal::<wgc::api::Dx12>() {
                        for adapter in instance.enumerate_adapters(None) {
                            let raw_adapter = adapter.adapter.raw_adapter();
                            let desc = unsafe { raw_adapter.GetDesc() };
                            if let Ok(desc) = desc {
                                if desc.AdapterLuid.LowPart == adapter_luid.low_part
                                    && desc.AdapterLuid.HighPart == adapter_luid.high_part
                                {
                                    global.create_adapter_from_hal(
                                        wgh::DynExposedAdapter::from(adapter),
                                        adapter_id,
                                    );
                                    result = Some(true);
                                    break;
                                }
                            }
                        }
                        if result.is_none() {
                            log::error!(concat!(
                                "Failed to find D3D12 adapter with the same LUID ",
                                "that the compositor is using!"
                            ));
                            result = Some(false);
                        }
                    }
                }
            }

            if result.is_none() {
                let created = match global.request_adapter(
                    &desc,
                    false,
                    wgt::Backends::PRIMARY,
                    adapter_id,
                ) {
                    Ok(_) => true,
                    Err(e) => {
                        log::warn!("{e}");
                        false
                    }
                };
                result = Some(created);
            }

            let response = if result.unwrap() {
                let wgt::AdapterInfo {
                    name,
                    vendor,
                    device,
                    device_type,
                    driver,
                    driver_info,
                    backend,
                    transient_saves_memory: _,
                    device_pci_bus_id: _,
                    subgroup_min_size,
                    subgroup_max_size,
                    limit_bucket: _,
                } = global.adapter_get_info(adapter_id);

                let is_hardware = match device_type {
                    wgt::DeviceType::IntegratedGpu | wgt::DeviceType::DiscreteGpu => true,
                    _ => false,
                };

                if static_prefs::pref!("dom.webgpu.testing.assert-hardware-adapter")
                    && !desc.force_fallback_adapter
                {
                    assert!(
                        is_hardware,
                        "Expected a hardware gpu adapter, got {:?}",
                        device_type
                    );
                }

                let support_use_shared_texture_in_swap_chain =
                    support_use_shared_texture_in_swap_chain(
                        global,
                        adapter_id,
                        backend,
                        is_hardware,
                    );

                let info = AdapterInformation {
                    id: adapter_id,
                    limits: restrict_limits(global.adapter_limits(adapter_id)),
                    features: global.adapter_features(adapter_id).features_webgpu,
                    name: Cow::Owned(name),
                    vendor,
                    device,
                    device_type,
                    driver: Cow::Owned(driver),
                    driver_info: Cow::Owned(driver_info),
                    backend,
                    support_use_shared_texture_in_swap_chain,
                    subgroup_min_size,
                    subgroup_max_size,
                };
                Some(info)
            } else {
                None
            };

            *response_byte_buf =
                make_byte_buf(&ServerMessage::RequestAdapterResponse(adapter_id, response));
        }
        Message::RequestDevice {
            adapter_id,
            device_id,
            queue_id,
            desc,
        } => {
            let res =
                adapter_request_device(global, adapter_id, desc, device_id, queue_id).map(|()| {
                    set_uncaptured_error_handler(global, device_id);
                    set_device_lost_handler(global, device_id);
                    wgpu_parent_post_request_device(global.owner, device_id);
                    // Not the limits we were asked for: wgpu-core adjusts them
                    // per <https://www.w3.org/TR/webgpu/#a-new-device>.
                    global.device_limits(device_id)
                });

            *response_byte_buf = make_byte_buf(&ServerMessage::RequestDeviceResponse(
                device_id, queue_id, res,
            ));
        }
        Message::Device(id, action) => {
            global.device_action(id, action, shmem_mappings, response_byte_buf)
        }
        Message::Texture(id, action) => {
            global.texture_action(id, action);
        }
        Message::RenderBundleEncoder(id, cmd) => {
            global.handle_render_bundle_encoder_command(id, cmd);
        }
        Message::CommandEncoder(id, cmd) => {
            global.handle_command_encoder_command(id, cmd);
        }
        Message::RenderPassEncoder(id, cmd) => {
            global.handle_render_pass_command(id, cmd);
        }
        Message::ComputePassEncoder(id, cmd) => {
            global.handle_compute_pass_command(id, cmd);
        }
        Message::QueueWrite {
            queue_id,
            data_source,
            action,
        } => {
            let data = match data_source {
                QueueWriteDataSource::DataBuffer(data_buffer_index) => {
                    data_buffers[data_buffer_index].as_slice()
                }
                QueueWriteDataSource::Shmem(shmem_handle_index) => {
                    shmem_mappings.as_slice()[shmem_handle_index].as_slice()
                }
            };
            match action {
                QueueWriteAction::Buffer { dst, offset } => {
                    global.queue_write_buffer(queue_id, dst, offset, data);
                }
                QueueWriteAction::Texture { dst, layout, size } => {
                    global.queue_write_texture(queue_id, &dst, data, &layout, &size);
                }
            }
        }
        msg @ Message::BufferMap { .. } => {
            process_buffer_map(global, msg, response_byte_buf);
        }
        Message::BufferUnmap(device_id, buffer_id, flush) => {
            wgpu_parent_buffer_unmap(global.owner, device_id, buffer_id, flush);
        }
        Message::QueueSubmit(
            queue_id,
            command_buffer_ids,
            texture_ids,
            external_texture_source_ids,
        ) => wgpu_parent_queue_submit(
            global.owner,
            queue_id,
            command_buffer_ids.as_ptr(),
            command_buffer_ids.len(),
            texture_ids.as_ptr(),
            texture_ids.len(),
            external_texture_source_ids.as_ptr(),
            external_texture_source_ids.len(),
        ),
        Message::QueueOnSubmittedWorkDone {
            device_id: _,
            queue_id,
        } => {
            let closure = wgpu_parent_build_submitted_work_done_closure(global.owner, queue_id);
            let (work_done_sender, work_done_receiver) = futures_channel::oneshot::channel();
            moz_task::spawn_local("WebGPU onSubmittedWorkDone callback", async move {
                work_done_receiver.await.unwrap();
                (closure.callback)(closure.user_data)
            })
            .detach();
            global.queue_on_submitted_work_done(
                queue_id,
                Box::new(move || {
                    work_done_sender.send(()).unwrap();
                }),
            );
        }

        Message::CreateSwapChain {
            device_id,
            queue_id,
            desc,
            format,
            buffer_ids,
            remote_texture_owner_id,
            use_shared_texture_in_swap_chain,
        } => {
            let (sanitized_desc, size) = {
                let TextureDescriptor {
                    size: wgt::Extent3d { width, height, .. },
                    format,
                    usage,
                    view_formats,
                    ..
                } = desc;

                let size = wgt::Extent3d {
                    width,
                    height,
                    ..wgt::Extent3d::default()
                };

                (
                    TextureDescriptor {
                        label: None,
                        size,
                        mip_level_count: 1,
                        sample_count: 1,
                        dimension: wgt::TextureDimension::D2,
                        format,
                        usage,
                        view_formats: Vec::from(view_formats),
                    },
                    size,
                )
            };

            unsafe {
                assert!(wgpu_texture_format_is_valid_for_webidl(&nsCString::from(
                    serde_json::to_value(&sanitized_desc.format)
                        .unwrap()
                        .as_str()
                        .unwrap(),
                ),));
            }

            let res = global.device_validate_texture_descriptor(device_id, &sanitized_desc);

            // In order to support `getCurrentTexture()` on invalid canvas configurations,
            // we store the configuration even when it is not valid.
            global
                .swap_chain_configs
                .lock()
                .unwrap()
                .insert(SwapChainId(remote_texture_owner_id.0), sanitized_desc);

            if let Some(err) = res {
                // This is a validation error from the device timeline
                // steps of `GPUCanvasContext.configure`.
                global.device_handle_error(device_id, err, None, "GPUCanvasContext.configure");
            } else {
                wgpu_parent_create_swap_chain(
                    global.owner,
                    device_id,
                    queue_id,
                    size.width,
                    size.height,
                    format,
                    buffer_ids.as_ptr(),
                    buffer_ids.len(),
                    remote_texture_owner_id,
                    use_shared_texture_in_swap_chain,
                );
            }
        }
        Message::SwapChainPresent {
            texture_id,
            command_encoder_id,
            command_buffer_id,
            remote_texture_id,
            remote_texture_owner_id,
        } => {
            wgpu_parent_swap_chain_present(
                global.owner,
                texture_id,
                command_encoder_id,
                command_buffer_id,
                remote_texture_id,
                remote_texture_owner_id,
            );
        }
        Message::SwapChainDrop {
            remote_texture_owner_id,
            txn_type,
            txn_id,
        } => {
            global
                .swap_chain_configs
                .lock()
                .unwrap()
                .remove(&SwapChainId(remote_texture_owner_id.0));
            wgpu_parent_swap_chain_drop(global.owner, remote_texture_owner_id, txn_type, txn_id);
        }

        Message::DestroyBuffer(id) => {
            wgpu_server_dealloc_buffer_shmem(global.owner, id);
            global.buffer_destroy(id);
        }
        Message::DestroyTexture(id) => {
            wgpu_server_remove_shared_texture(global.owner, id);
            global.texture_destroy(id);
        }
        Message::DestroyExternalTexture(id) => global.external_texture_destroy(id),
        Message::DestroyExternalTextureSource(id) => {
            wgpu_parent_destroy_external_texture_source(global.owner, id);
        }
        Message::DestroyDevice(id) => global.device_destroy(id),

        Message::DropAdapter(id) => {
            global.adapter_remove(id);
        }
        Message::DropDevice(id) => {
            wgpu_server_pre_device_drop(global.owner, id);
            global.device_remove(id);
        }
        Message::DropQueue(id) => {
            global.queue_remove(id);
        }
        Message::DropBuffer(id) => {
            wgpu_server_dealloc_buffer_shmem(global.owner, id);
            global.buffer_remove(id);
        }
        Message::DropCommandEncoder(id) => {
            global.command_encoder_remove(id);
        }
        Message::DropRenderPassEncoder(id) => {
            global.render_pass_remove(id);
        }
        Message::DropComputePassEncoder(id) => {
            global.compute_pass_remove(id);
        }
        Message::DropRenderBundleEncoder(id) => {
            global.render_bundle_encoder_remove(id);
        }
        Message::DropCommandBuffer(id) => {
            global.command_buffer_remove(id);
        }
        Message::DropRenderBundle(id) => {
            global.render_bundle_remove(id);
        }
        Message::DropBindGroupLayout(id) => {
            global.bind_group_layout_remove(id);
        }
        Message::DropPipelineLayout(id) => {
            global.pipeline_layout_remove(id);
        }
        Message::DropBindGroup(id) => {
            global.bind_group_remove(id);
        }
        Message::DropShaderModule(id) => {
            global.shader_module_remove(id);
        }
        Message::DropComputePipeline(id) => {
            global.compute_pipeline_remove(id);
        }
        Message::DropRenderPipeline(id) => {
            global.render_pipeline_remove(id);
        }
        Message::DropTexture(id) => {
            wgpu_server_remove_shared_texture(global.owner, id);
            global.texture_remove(id);
        }
        Message::DropTextureView(id) => {
            global.texture_view_remove(id);
        }
        Message::DropExternalTexture(id) => {
            global.external_texture_remove(id);
        }
        Message::DropExternalTextureSource(id) => {
            wgpu_parent_drop_external_texture_source(global.owner, id);
        }
        Message::DropSampler(id) => {
            global.sampler_remove(id);
        }
        Message::DropQuerySet(id) => {
            global.query_set_remove(id);
        }
    }

    if !response_byte_buf.is_empty() {
        wgpu_parent_send_server_message(global.owner, response_byte_buf);
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_submit_copy_texture_to_buffer(
    global: &Global,
    device_id: id::DeviceId,
    queue_id: id::QueueId,
    command_encoder_id: id::CommandEncoderId,
    command_buffer_id: id::CommandBufferId,
    src_texture_id: id::TextureId,
    src_texture_width: u32,
    src_texture_height: u32,
    dst_buffer_id: id::BufferId,
    dst_buffer_bytes_per_row: u32,
) -> bool {
    global.device_push_error_scope(device_id, ErrorFilter::Validation);
    global.device_create_command_encoder(
        device_id,
        &CommandEncoderDescriptor { label: None },
        command_encoder_id,
    );
    let source = TexelCopyTextureInfo {
        texture: src_texture_id,
        mip_level: 0,
        origin: wgt::Origin3d::ZERO,
        aspect: wgt::TextureAspect::All,
    };
    let destination = TexelCopyBufferInfo {
        buffer: dst_buffer_id,
        layout: TexelCopyBufferLayout {
            offset: 0,
            bytes_per_row: Some(dst_buffer_bytes_per_row),
            rows_per_image: None,
        },
    };
    let copy_size = wgt::Extent3d {
        width: src_texture_width,
        height: src_texture_height,
        depth_or_array_layers: 1,
    };
    global.command_encoder_copy_texture_to_buffer(
        command_encoder_id,
        &source,
        &destination,
        &copy_size,
    );
    global.command_encoder_finish(
        command_encoder_id,
        &CommandBufferDescriptor { label: None },
        command_buffer_id,
    );
    global.queue_submit(queue_id, &[command_buffer_id]);

    global.command_encoder_remove(command_encoder_id);
    global.command_buffer_remove(command_buffer_id);

    let error = global.device_pop_error_scope(device_id).unwrap();
    if let Some(err) = &error {
        let msg = CString::new(format!(
            "wgpu_server_submit_copy_texture_to_buffer() failed: {:?}",
            err
        ))
        .unwrap();
        unsafe {
            gfx_critical_note(msg.as_ptr());
        }
    }
    error.is_none()
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_queue_write_texture(
    global: &Global,
    device_id: id::DeviceId,
    queue_id: id::QueueId,
    destination: &TexelCopyTextureInfo,
    data: FfiSlice<u8>,
    data_layout: &crate::FfiTexelCopyBufferLayout,
    size: &wgt::Extent3d,
) {
    let data = unsafe { data.as_slice() };
    let data_layout = data_layout.into_wgt();
    global.device_push_error_scope(device_id, ErrorFilter::Validation);
    global.queue_write_texture(queue_id, destination, data, &data_layout, size);
    let error = global.device_pop_error_scope(device_id).unwrap();
    if let Some(err) = error {
        let msg = CString::new(format!("write_texture() failed: {:?}", err)).unwrap();
        unsafe { gfx_critical_note(msg.as_ptr()) };
    }
}

/// A raw `VkSemaphore`, or 0 for null. Defined on all platforms so that
/// `wgpu_server_queue_submit` keeps a single signature.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct VkSemaphoreHandle(pub u64);

/// Queues the destruction of the semaphores that
/// `wgpu_vksemaphore_create_signal_semaphore` registered on the queue, now that
/// the submission they belong to has been attempted. The actual
/// `vkDestroySemaphore` calls happen later, from an `on_submitted_work_done`
/// closure.
#[cfg(target_os = "linux")]
fn enqueue_signal_semaphores_destruction(
    queue: &Arc<wgc::device::queue::Queue>,
    device: Arc<wgc::device::Device>,
    handles: &[VkSemaphoreHandle],
) {
    use ash::vk::Handle as _;

    if handles.is_empty() {
        return;
    }

    let semaphores: Vec<vk::Semaphore> = handles
        .iter()
        .map(|handle| vk::Semaphore::from_raw(handle.0))
        .collect();

    // There may be some semaphores still left in the HAL queue if an error was
    // encountered, and we must ensure they aren't signalled after destruction.
    // Since attempting to remove a semaphore that was already consumed is a
    // no-op, just ask the queue to remove all of the ones we know were part of
    // the submission.
    {
        let queue = queue.clone();
        let hal_queue =
            unsafe { queue.as_hal::<wgc::api::Vulkan>() }.expect("Vulkan backend queue on linux");
        for &semaphore in &semaphores {
            hal_queue.remove_signal_semaphore(semaphore);
        }
    }

    // Destroying a semaphore has to wait for any submission referencing it to
    // complete (VUID-vkDestroySemaphore-semaphore-05149). This must therefore
    // run after `queue_submit`, so that the closure attaches to the submission
    // that signals these semaphores. A failed submit that still reached the
    // driver is tracked as well, so deferring is correct on both paths.
    //
    // The closure is owned by the queue's own `LifetimeTracker`, so it must not
    // capture an `Arc<Queue>`, which would be an unbreakable cycle. Capturing
    // `Arc<Device>` is fine because `Device::queue` is only a `Weak`, and it
    // lets us reach the hal device without a registry lookup that would panic
    // once the device id has been dropped.
    queue.on_submitted_work_done(Box::new(move || {
        let hal_device =
            unsafe { device.as_hal::<wgc::api::Vulkan>() }.expect("Vulkan backend device on linux");
        let raw_device = hal_device.raw_device();
        for semaphore in semaphores {
            unsafe {
                raw_device.destroy_semaphore(semaphore, None);
            }
        }
    }));
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_queue_submit(
    global: &Global,
    queue_id: id::QueueId,
    command_buffer_ids: FfiSlice<'_, id::CommandBufferId>,
    signal_semaphores: FfiSlice<'_, VkSemaphoreHandle>,
) -> u64 {
    let submission_index_opt =
        global.queue_submit(queue_id, unsafe { command_buffer_ids.as_slice() });

    #[cfg(target_os = "linux")]
    {
        use wgc::resource::ParentDevice as _;

        let queue = global.resolve_queue_id(queue_id);
        let device = queue.device();
        enqueue_signal_semaphores_destruction(&queue, device.clone(), unsafe {
            signal_semaphores.as_slice()
        });
    }
    #[cfg(not(target_os = "linux"))]
    let _ = signal_semaphores;

    // NOTE: `0` means "no index to follow up with".
    submission_index_opt.unwrap_or(0)
}

#[repr(C)]
pub struct SubmittedWorkDoneClosure {
    pub callback: unsafe extern "C" fn(user_data: *mut u8),
    pub user_data: *mut u8,
}

/// Creates a binary semaphore, registers it as a signal semaphore of the next
/// submission on `queue_id`, and exports it to `out_fd` (-1 on failure).
///
/// Ownership of the semaphore itself stays here: `wgpu_server_queue_submit`
/// destroys it once the submission it signals has completed.
#[no_mangle]
#[cfg(target_os = "linux")]
pub extern "C" fn wgpu_vksemaphore_create_signal_semaphore(
    global: &Global,
    queue_id: id::QueueId,
    out_fd: *mut i32,
) -> VkSemaphoreHandle {
    use ash::vk::Handle as _;
    use wgc::resource::ParentDevice as _;

    let queue = global.resolve_queue_id(queue_id);
    let device = queue.device().clone();

    let hal_device =
        unsafe { device.as_hal::<wgc::api::Vulkan>() }.expect("Vulkan backend device on linux");
    let hal_queue =
        unsafe { queue.as_hal::<wgc::api::Vulkan>() }.expect("Vulkan backend queue on linux");

    unsafe {
        *out_fd = -1;

        let device = hal_device.raw_device();

        let mut export_semaphore_create_info = vk::ExportSemaphoreCreateInfo::default()
            .handle_types(vk::ExternalSemaphoreHandleTypeFlags::OPAQUE_FD);
        let create_info =
            vk::SemaphoreCreateInfo::default().push_next(&mut export_semaphore_create_info);
        let semaphore = match device.create_semaphore(&create_info, None) {
            Err(err) => {
                let msg = CString::new(format!("create_semaphore() failed: {:?}", err)).unwrap();
                gfx_critical_note(msg.as_ptr());
                return VkSemaphoreHandle(0);
            }
            Ok(semaphore) => semaphore,
        };

        let instance = hal_device.shared_instance().raw_instance();
        let external_semaphore_fd = khr::external_semaphore_fd::Device::new(instance, device);
        let get_fd_info = vk::SemaphoreGetFdInfoKHR::default()
            .semaphore(semaphore)
            .handle_type(vk::ExternalSemaphoreHandleTypeFlags::OPAQUE_FD);

        match external_semaphore_fd.get_semaphore_fd(&get_fd_info) {
            Err(err) => {
                let msg = CString::new(format!("vkGetSemaphoreFdKHR failed: {:?}", err)).unwrap();
                gfx_critical_note(msg.as_ptr());

                device.destroy_semaphore(semaphore, None);
                VkSemaphoreHandle(0)
            }
            Ok(fd) => {
                *out_fd = fd;
                hal_queue.add_signal_semaphore(semaphore, None);
                VkSemaphoreHandle(semaphore.as_raw())
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_buffer_drop(global: &Global, self_id: id::BufferId) {
    global.buffer_remove(self_id);
}

/// Imports a Direct3D texture from a shared handle.
#[cfg(target_os = "windows")]
#[no_mangle]
#[allow(unsafe_op_in_unsafe_fn)]
pub unsafe extern "C" fn wgpu_server_device_import_texture_from_d3d11_resource(
    global: &Global,
    device_id: id::DeviceId,
    id_in: id::TextureId,
    desc: &FfiTextureDescriptor,
    handle: *mut core::ffi::c_void,
) {
    let desc = desc.to_wgpu();

    global.create_texture_with_imported_d3d11_resource(
        device_id,
        id_in,
        &desc,
        handle,
        guard((), |_| {
            global.create_texture_error(device_id, id_in, &desc);
        }),
    );
}

/// Imports a fence from a shared handle and queues a GPU-side wait on the
/// specified queue for the fence to reach a specific value.
#[cfg(target_os = "windows")]
#[no_mangle]
#[allow(unsafe_op_in_unsafe_fn)]
pub unsafe extern "C" fn wgpu_server_device_wait_fence_from_shared_handle(
    global: &Global,
    queue_id: id::QueueId,
    fence_handle: *mut core::ffi::c_void,
    fence_value: u64,
) -> bool {
    use wgc::resource::ParentDevice as _;

    let queue = global.resolve_queue_id(queue_id);
    let device = queue.device().clone();

    let hal_device =
        unsafe { device.as_hal::<wgc::api::Dx12>() }.expect("D3D12 backend device on windows");
    let hal_queue =
        unsafe { queue.as_hal::<wgc::api::Dx12>() }.expect("D3D12 backend queue on windows");

    let mut fence: Option<Direct3D12::ID3D12Fence> = None;
    let res = hal_device
        .raw_device()
        .OpenSharedHandle(Foundation::HANDLE(fence_handle), &mut fence);
    let fence = match (res, fence) {
        (Ok(_), Some(fence)) => fence,
        _ => return false,
    };

    let res = hal_queue.as_raw().Wait(&fence, fence_value);
    res.is_ok()
}

#[cfg(target_os = "macos")]
mod macos {
    use std::ffi::CString;

    use super::{emit_critical_invalid_note, gfx_critical_note, Global};
    use crate::FfiTextureDescriptor;

    use objc2::{rc::autoreleasepool, runtime::ProtocolObject};
    use objc2_foundation::NSString;
    use objc2_io_surface::IOSurfaceRef;
    use objc2_metal::{
        MTLDevice as _, MTLPixelFormat, MTLResource, MTLStorageMode, MTLTextureDescriptor,
        MTLTextureType, MTLTextureUsage,
    };
    use scopeguard::{guard, ScopeGuard};
    use wgpu_core_remote_types::{id, TextureDescriptor};

    /// Imports a Metal texture from the specified plane of an IOSurface.
    ///
    /// This is used for external textures.
    #[no_mangle]
    #[allow(unsafe_op_in_unsafe_fn)]
    pub unsafe extern "C" fn wgpu_server_device_import_texture_from_iosurface(
        global: &Global,
        device_id: id::DeviceId,
        id_in: id::TextureId,
        desc: &FfiTextureDescriptor,
        io_surface_id: u32,
        plane: usize,
    ) {
        let desc = desc.to_wgpu();

        global.create_texture_with_imported_iosurface(
            device_id,
            id_in,
            &desc,
            io_surface_id,
            plane,
            guard((), |_| {
                global.create_texture_error(device_id, id_in, &desc);
            }),
        );
    }

    impl super::Global {
        /// Import an `IOSurface` as a `wgpu` texture.
        ///
        /// Three outcomes are possible:
        /// - The texture is imported successfully. Return value is `None`.
        /// - The import fails before the call to `create_texture_from_hal`.
        ///   Return value is `Some`, containing the provided `ScopeGuard`,
        ///   which should populate the texture as invalid.
        /// - The import failed within the call to `create_texture_from_hal`.
        ///   In this case `create_texture_from_hal` populates an invalid
        ///   texture. The `ScopeGuard` is defused and the return value is
        ///   `None`.
        ///
        /// This function only supports plain 2D color textures. Multisampled,
        /// depth/stencil, layered, mipmapped, and volume textures are not
        /// supported.
        ///
        /// This function also only handles a basic set of color formats.
        pub(super) fn create_texture_with_imported_iosurface<'a, F: FnOnce(()) + 'a>(
            &self,
            device_id: id::DeviceId,
            texture_id: id::TextureId,
            desc: &'a TextureDescriptor,
            io_surface_id: u32,
            plane: usize,
            guard: ScopeGuard<(), F>,
        ) -> Option<ScopeGuard<(), F>> {
            let Some(io_surface) = IOSurfaceRef::lookup(io_surface_id) else {
                emit_critical_invalid_note("IOSurface");
                return Some(guard);
            };

            assert!(desc.dimension == wgt::TextureDimension::D2);
            assert!(desc.sample_count == 1);
            assert!(desc.size.depth_or_array_layers == 1);
            assert!(desc.mip_level_count == 1);

            let raw_texture = unsafe {
                let device = self.resolve_device_id(device_id);
                let Some(hal_device) = device.as_hal::<wgc::api::Metal>() else {
                    emit_critical_invalid_note("metal device");
                    return Some(guard);
                };

                let device = hal_device.raw_device();

                let texture = autoreleasepool(|_| {
                    use wgt::TextureUsages as Tu;

                    let descriptor = MTLTextureDescriptor::new();

                    let format = match desc.format {
                        wgt::TextureFormat::Rgba8Unorm => MTLPixelFormat::RGBA8Unorm,
                        wgt::TextureFormat::Bgra8Unorm => MTLPixelFormat::BGRA8Unorm,
                        wgt::TextureFormat::R8Unorm => MTLPixelFormat::R8Unorm,
                        wgt::TextureFormat::Rg8Unorm => MTLPixelFormat::RG8Unorm,
                        wgt::TextureFormat::R16Unorm => MTLPixelFormat::R16Unorm,
                        wgt::TextureFormat::Rg16Unorm => MTLPixelFormat::RG16Unorm,
                        _ => unimplemented!(),
                    };

                    let mut usage = MTLTextureUsage::Unknown;
                    usage.set(
                        MTLTextureUsage::RenderTarget,
                        desc.usage.intersects(Tu::RENDER_ATTACHMENT),
                    );
                    usage.set(
                        MTLTextureUsage::ShaderRead,
                        desc.usage
                            .intersects(Tu::TEXTURE_BINDING | Tu::STORAGE_BINDING),
                    );
                    usage.set(
                        MTLTextureUsage::ShaderWrite,
                        desc.usage.intersects(Tu::STORAGE_BINDING),
                    );

                    descriptor.setWidth(desc.size.width as usize);
                    descriptor.setHeight(desc.size.height as usize);
                    descriptor.setPixelFormat(format);
                    descriptor.setUsage(usage);
                    descriptor.setStorageMode(MTLStorageMode::Private);

                    device.newTextureWithDescriptor_iosurface_plane(&descriptor, &io_surface, plane)
                });

                let Some(texture) = texture else {
                    let msg = c"Failed to create MTLTexture from imported IOSurface";
                    gfx_critical_note(msg.as_ptr());
                    return Some(guard);
                };

                texture
            };

            if let Some(ref label) = desc.label {
                ProtocolObject::<dyn MTLResource>::from_ref(&*raw_texture)
                    .setLabel(Some(&NSString::from_str(label)));
            }

            let hal_texture = unsafe {
                <wgh::api::Metal as wgh::Api>::Device::texture_from_raw(
                    raw_texture,
                    desc.format,
                    MTLTextureType::Type2D,
                    1,
                    1,
                    wgh::CopyExtent::map_extent_to_copy_size(&desc.size, desc.dimension),
                    None,
                )
            };

            // `create_texture_from_hal` populates an invalid texture on failure, so
            // defuse the guard now.
            ScopeGuard::into_inner(guard);

            let (_, error) = unsafe {
                self.create_texture_from_hal(
                    Box::new(hal_texture),
                    device_id,
                    &desc,
                    wgt::TextureUses::UNINITIALIZED,
                    texture_id,
                    // NOTE: For swapchain textures (but not external textures), we want `false`,
                    // but we can't yet, because upstream's API isn't quite right yet.
                    /* cleared */
                    true,
                )
            };
            if let Some(err) = error {
                let msg =
                    CString::new(format!("create_texture_from_hal() failed: {:?}", err)).unwrap();
                unsafe {
                    gfx_critical_note(msg.as_ptr());
                }
                // Texture was already populated as invalid.
            }

            None
        }
    }
}
