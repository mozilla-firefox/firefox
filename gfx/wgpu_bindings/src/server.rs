/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{
    command::{RecordedComputePass, RecordedRenderPass},
    error::{ErrMsg, ErrorBuffer, ErrorBufferType},
    wgpu_string, AdapterInformation, ByteBuf, CommandEncoderAction, DeviceAction, DropAction,
    QueueWriteAction, SwapChainId, TextureAction,
};

use nsstring::{nsACString, nsCString, nsString};

use wgc::{device::DeviceError, id};
use wgc::{pipeline::CreateShaderModuleError, resource::BufferAccessError};
#[allow(unused_imports)]
use wgh::Instance;

use std::borrow::Cow;
#[allow(unused_imports)]
use std::mem;
#[cfg(target_os = "linux")]
use std::os::fd::{FromRawFd, IntoRawFd, OwnedFd, RawFd};
use std::os::raw::{c_char, c_void};
use std::ptr;
use std::slice;
use std::sync::atomic::{AtomicU32, Ordering};

#[allow(unused_imports)]
use std::ffi::CString;
use std::ffi::{c_long, c_ulong};

#[cfg(target_os = "windows")]
use windows::Win32::{Foundation, Graphics::Direct3D12};

#[cfg(target_os = "linux")]
use ash::{khr, vk};

#[cfg(target_os = "macos")]
use objc::{class, msg_send, sel, sel_impl};

// The seemingly redundant u64 suffixes help cbindgen with generating the right C++ code.
// See https://github.com/mozilla/cbindgen/issues/849.

/// We limit the size of buffer allocations for stability reason.
/// We can reconsider this limit in the future. Note that some drivers (mesa for example),
/// have issues when the size of a buffer, mapping or copy command does not fit into a
/// signed 32 bits integer, so beyond a certain size, large allocations will need some form
/// of driver allow/blocklist.
pub const MAX_BUFFER_SIZE: wgt::BufferAddress = 1u64 << 30u64;
const MAX_BUFFER_SIZE_U32: u32 = MAX_BUFFER_SIZE as u32;

// Mesa has issues with height/depth that don't fit in a 16 bits signed integers.
const MAX_TEXTURE_EXTENT: u32 = std::i16::MAX as u32;
// We have to restrict the number of bindings for any given resource type so that
// the sum of these limits multiplied by the number of shader stages fits
// maxBindingsPerBindGroup (1000). This restriction is arbitrary and is likely to
// change eventually. See github.com/gpuweb/gpuweb/pull/4484
// For now it's impractical for users to have very large numbers of bindings so this
// limit should not be too restrictive until we add support for a bindless API.
// Then we may have to ignore the spec or get it changed.
const MAX_BINDINGS_PER_RESOURCE_TYPE: u32 = 64;

fn restrict_limits(limits: wgt::Limits) -> wgt::Limits {
    wgt::Limits {
        max_buffer_size: limits.max_buffer_size.min(MAX_BUFFER_SIZE),
        max_texture_dimension_1d: limits.max_texture_dimension_1d.min(MAX_TEXTURE_EXTENT),
        max_texture_dimension_2d: limits.max_texture_dimension_2d.min(MAX_TEXTURE_EXTENT),
        max_texture_dimension_3d: limits.max_texture_dimension_3d.min(MAX_TEXTURE_EXTENT),
        max_sampled_textures_per_shader_stage: limits
            .max_sampled_textures_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_samplers_per_shader_stage: limits
            .max_samplers_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_textures_per_shader_stage: limits
            .max_storage_textures_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_uniform_buffers_per_shader_stage: limits
            .max_uniform_buffers_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_storage_buffers_per_shader_stage: limits
            .max_storage_buffers_per_shader_stage
            .min(MAX_BINDINGS_PER_RESOURCE_TYPE),
        max_uniform_buffer_binding_size: limits
            .max_uniform_buffer_binding_size
            .min(MAX_BUFFER_SIZE_U32),
        max_storage_buffer_binding_size: limits
            .max_storage_buffer_binding_size
            .min(MAX_BUFFER_SIZE_U32),
        max_non_sampler_bindings: 10_000,
        ..limits
    }
}

// hide wgc's global in private
pub struct Global {
    global: wgc::global::Global,
    #[allow(dead_code)]
    owner: *mut c_void,
}

impl std::ops::Deref for Global {
    type Target = wgc::global::Global;
    fn deref(&self) -> &Self::Target {
        &self.global
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_new(owner: *mut c_void, use_dxc: bool) -> *mut Global {
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

    let mut instance_flags = wgt::InstanceFlags::from_build_config().with_env();
    if !static_prefs::pref!("dom.webgpu.hal-labels") {
        instance_flags.insert(wgt::InstanceFlags::DISCARD_HAL_LABELS);
    }

    let dx12_shader_compiler = if use_dxc {
        wgt::Dx12Compiler::DynamicDxc {
            dxc_path: "dxcompiler.dll".into(),
            max_shader_model: wgt::DxcShaderModel::V6_6,
        }
    } else {
        wgt::Dx12Compiler::Fxc
    };

    let global = wgc::global::Global::new(
        "wgpu",
        &wgt::InstanceDescriptor {
            backends,
            flags: instance_flags,
            backend_options: wgt::BackendOptions {
                gl: wgt::GlBackendOptions {
                    gles_minor_version: wgt::Gles3MinorVersion::Automatic,
                    fence_behavior: wgt::GlFenceBehavior::Normal,
                },
                dx12: wgt::Dx12BackendOptions {
                    shader_compiler: dx12_shader_compiler,
                },
                noop: wgt::NoopBackendOptions { enable: false },
            },
            memory_budget_thresholds: wgt::MemoryBudgetThresholds {
                for_resource_creation: Some(95),
                for_device_loss: Some(99),
            },
        },
    );
    let global = Global { global, owner };
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
    let _ = Box::from_raw(global);
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
        wgt::PollType::Wait
    } else {
        wgt::PollType::Poll
    };
    global.device_poll(device_id, maintain).unwrap();
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct FfiLUID {
    low_part: c_ulong,
    high_part: c_long,
}

/// Request an adapter according to the specified options.
///
/// Returns true if we successfully found an adapter.
#[allow(unused_variables)]
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_instance_request_adapter(
    global: &Global,
    desc: &wgc::instance::RequestAdapterOptions,
    adapter_id: id::AdapterId,
    adapter_luid: Option<&FfiLUID>,
    mut error_buf: ErrorBuffer,
) -> bool {
    // Prefer to use the dx12 backend, if one exists, and use the same DXGI adapter as WebRender.
    // If wgpu uses a different adapter than WebRender, textures created by
    // webgpu::ExternalTexture do not work with wgpu.
    #[cfg(target_os = "windows")]
    if adapter_luid.is_some() && !desc.force_fallback_adapter {
        if let Some(instance) = global.global.instance_as_hal::<wgc::api::Dx12>() {
            for adapter in instance.enumerate_adapters(None) {
                let raw_adapter = adapter.adapter.raw_adapter();
                let desc = unsafe { raw_adapter.GetDesc() };
                if let Ok(desc) = desc {
                    if desc.AdapterLuid.LowPart == adapter_luid.unwrap().low_part
                        && desc.AdapterLuid.HighPart == adapter_luid.unwrap().high_part
                    {
                        global.create_adapter_from_hal(
                            wgh::DynExposedAdapter::from(adapter),
                            Some(adapter_id),
                        );
                        return true;
                    }
                }
            }
            error_buf.init(ErrMsg {
                message: "Failed to create adapter for dx12",
                r#type: ErrorBufferType::Internal,
            });
            return false;
        }
    }

    match global.request_adapter(desc, wgt::Backends::PRIMARY, Some(adapter_id)) {
        Ok(id) => return true,
        Err(e) => {
            error_buf.init(e);
            return false;
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
#[allow(clippy::upper_case_acronyms)]
#[cfg(target_os = "macos")]
struct NSOperatingSystemVersion {
    major: usize,
    minor: usize,
    patch: usize,
}

#[cfg(target_os = "macos")]
impl NSOperatingSystemVersion {
    fn at_least(
        &self,
        mac_version: (usize, usize),
        ios_version: (usize, usize),
        is_mac: bool,
    ) -> bool {
        let version = if is_mac { mac_version } else { ios_version };

        self.major
            .cmp(&version.0)
            .then_with(|| self.minor.cmp(&version.1))
            .is_ge()
    }
}

#[allow(unreachable_code)]
#[allow(unused_variables)]
fn support_use_external_texture_in_swap_chain(
    global: &Global,
    self_id: id::AdapterId,
    backend: wgt::Backend,
    is_hardware: bool,
) -> bool {
    #[cfg(target_os = "windows")]
    {
        return backend == wgt::Backend::Dx12 && is_hardware;
    }

    #[cfg(target_os = "linux")]
    {
        let support = if backend != wgt::Backend::Vulkan {
            false
        } else {
            unsafe {
                global.adapter_as_hal::<wgc::api::Vulkan, _, bool>(self_id, |hal_adapter| {
                    let hal_adapter = match hal_adapter {
                        None => {
                            let msg = c"Vulkan adapter is invalid";
                            gfx_critical_note(msg.as_ptr());
                            return false;
                        }
                        Some(hal_adapter) => hal_adapter,
                    };

                    let capabilities = hal_adapter.physical_device_capabilities();

                    capabilities.supports_extension(khr::external_memory_fd::NAME)
                        && capabilities.supports_extension(ash::ext::external_memory_dma_buf::NAME)
                        && capabilities
                            .supports_extension(ash::ext::image_drm_format_modifier::NAME)
                        && capabilities.supports_extension(khr::external_semaphore_fd::NAME)
                })
            }
        };
        return support;
    }

    #[cfg(target_os = "macos")]
    {
        if backend != wgt::Backend::Metal || !is_hardware {
            return false;
        }

        let version: NSOperatingSystemVersion = unsafe {
            let process_info: *mut objc::runtime::Object =
                msg_send![class!(NSProcessInfo), processInfo];
            msg_send![process_info, operatingSystemVersion]
        };

        let supports_shared_event = version.at_least((10, 14), (12, 0), /* os_is_mac */ true);

        return supports_shared_event;
    }

    false
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_adapter_pack_info(
    global: &Global,
    self_id: Option<id::AdapterId>,
    byte_buf: &mut ByteBuf,
) {
    let mut data = Vec::new();
    match self_id {
        Some(id) => {
            let wgt::AdapterInfo {
                name,
                vendor,
                device,
                device_type,
                driver,
                driver_info,
                backend,
            } = global.adapter_get_info(id);

            let is_hardware = match device_type {
                wgt::DeviceType::IntegratedGpu | wgt::DeviceType::DiscreteGpu => true,
                _ => false,
            };

            if static_prefs::pref!("dom.webgpu.testing.assert-hardware-adapter") {
                assert!(
                    is_hardware,
                    "Expected a hardware gpu adapter, got {:?}",
                    device_type
                );
            }

            let support_use_external_texture_in_swap_chain =
                support_use_external_texture_in_swap_chain(global, id, backend, is_hardware);

            let info = AdapterInformation {
                id,
                limits: restrict_limits(global.adapter_limits(id)),
                features: global.adapter_features(id).features_webgpu,
                name,
                vendor,
                device,
                device_type,
                driver,
                driver_info,
                backend,
                support_use_external_texture_in_swap_chain,
            };
            bincode::serialize_into(&mut data, &info).unwrap();
        }
        None => {
            bincode::serialize_into(&mut data, &0u64).unwrap();
        }
    }
    *byte_buf = ByteBuf::from_vec(data);
}

static TRACE_IDX: AtomicU32 = AtomicU32::new(0);

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_adapter_request_device(
    global: &Global,
    self_id: id::AdapterId,
    byte_buf: &ByteBuf,
    new_device_id: id::DeviceId,
    new_queue_id: id::QueueId,
    mut error_buf: ErrorBuffer,
) {
    let mut desc: wgc::device::DeviceDescriptor =
        bincode::deserialize(byte_buf.as_slice()).unwrap();

    desc.trace = match desc.trace {
        wgt::Trace::Directory(s) => {
            let idx = TRACE_IDX.fetch_add(1, Ordering::Relaxed);
            let path = s.join(idx.to_string());

            if std::fs::create_dir_all(&path).is_err() {
                log::warn!("Failed to create directory {:?} for wgpu recording.", path);
            }

            wgt::Trace::Directory(path)
        }
        other => other,
    };

    // TODO: in https://github.com/gfx-rs/wgpu/pull/3626/files#diff-033343814319f5a6bd781494692ea626f06f6c3acc0753a12c867b53a646c34eR97
    // which introduced the queue id parameter, the queue id is also the device id. I don't know how applicable this is to
    // other situations (this one in particular).

    #[cfg(target_os = "linux")]
    {
        let support_dma_buf =
            global.adapter_as_hal::<wgc::api::Vulkan, _, bool>(self_id, |hal_adapter| {
                let hal_adapter = match hal_adapter {
                    None => {
                        let msg = c"Vulkan adapter is invalid";
                        gfx_critical_note(msg.as_ptr());
                        return false;
                    }
                    Some(hal_adapter) => hal_adapter,
                };

                let capabilities = hal_adapter.physical_device_capabilities();

                capabilities.supports_extension(khr::external_memory_fd::NAME)
                    && capabilities.supports_extension(ash::ext::external_memory_dma_buf::NAME)
                    && capabilities.supports_extension(ash::ext::image_drm_format_modifier::NAME)
                    && capabilities.supports_extension(khr::external_semaphore_fd::NAME)
            });

        if support_dma_buf {
            let hal_device = global
                .adapter_as_hal::<wgc::api::Vulkan, _, Option<wgh::OpenDevice<wgh::api::Vulkan>>>(
                    self_id,
                    |hal_adapter| {
                        let hal_adapter = match hal_adapter {
                            None => {
                                let msg = c"Vulkan adapter is invalid";
                                gfx_critical_note(msg.as_ptr());
                                return None;
                            }
                            Some(hal_adapter) => hal_adapter,
                        };

                        let mut enabled_extensions =
                            hal_adapter.required_device_extensions(desc.required_features);
                        enabled_extensions.push(khr::external_memory_fd::NAME);
                        enabled_extensions.push(ash::ext::external_memory_dma_buf::NAME);
                        enabled_extensions.push(ash::ext::image_drm_format_modifier::NAME);
                        enabled_extensions.push(khr::external_semaphore_fd::NAME);

                        let mut enabled_phd_features = hal_adapter
                            .physical_device_features(&enabled_extensions, desc.required_features);

                        let raw_instance = hal_adapter.shared_instance().raw_instance();
                        let raw_physical_device = hal_adapter.raw_physical_device();

                        let queue_family_index = raw_instance
                            .get_physical_device_queue_family_properties(raw_physical_device)
                            .into_iter()
                            .enumerate()
                            .find_map(|(queue_family_index, info)| {
                                if info.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
                                    Some(queue_family_index as u32)
                                } else {
                                    None
                                }
                            });

                        let queue_family_index = match queue_family_index {
                            None => {
                                let msg = c"Vulkan device has no graphics queue";
                                gfx_critical_note(msg.as_ptr());
                                return None;
                            }
                            Some(queue_family_index) => queue_family_index,
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

                        let raw_device =
                            match raw_instance.create_device(raw_physical_device, &info, None) {
                                Err(err) => {
                                    let msg =
                                        CString::new(format!("create_device() failed: {:?}", err))
                                            .unwrap();
                                    gfx_critical_note(msg.as_ptr());
                                    return None;
                                }
                                Ok(raw_device) => raw_device,
                            };

                        let hal_device = hal_adapter.device_from_raw(
                            raw_device,
                            None,
                            &enabled_extensions,
                            desc.required_features,
                            &desc.memory_hints,
                            family_info.queue_family_index,
                            0,
                        );
                        Some(hal_device.unwrap())
                    },
                );

            let hal_device = match hal_device {
                None => {
                    error_buf.init(ErrMsg {
                        message: "Failed to create ash::Device",
                        r#type: ErrorBufferType::Internal,
                    });
                    return;
                }
                Some(hal_device) => hal_device,
            };

            let res = global.create_device_from_hal(
                self_id,
                hal_device.into(),
                &desc,
                Some(new_device_id),
                Some(new_queue_id),
            );
            if let Err(err) = res {
                error_buf.init(err);
            }
            return;
        }
    }

    let res =
        global.adapter_request_device(self_id, &desc, Some(new_device_id), Some(new_queue_id));
    if let Err(err) = res {
        error_buf.init(err);
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_adapter_drop(global: &Global, adapter_id: id::AdapterId) {
    global.adapter_drop(adapter_id)
}

#[no_mangle]
pub extern "C" fn wgpu_server_device_destroy(global: &Global, self_id: id::DeviceId) {
    global.device_destroy(self_id)
}

#[no_mangle]
pub extern "C" fn wgpu_server_device_drop(global: &Global, self_id: id::DeviceId) {
    global.device_drop(self_id)
}

#[repr(C)]
pub struct DeviceLostClosure {
    pub callback: unsafe extern "C" fn(user_data: *mut u8, reason: u8, message: *const c_char),
    pub cleanup_callback: unsafe extern "C" fn(user_data: *mut u8),
    pub user_data: *mut u8,
}
unsafe impl Send for DeviceLostClosure {}

impl DeviceLostClosure {
    fn call(self, reason: wgt::DeviceLostReason, message: String) {
        // Ensure message is structured as a null-terminated C string. It only
        // needs to live as long as the callback invocation.
        let message = std::ffi::CString::new(message).unwrap();
        unsafe {
            (self.callback)(self.user_data, reason as u8, message.as_ptr());
        }
        core::mem::forget(self);
    }
}

impl Drop for DeviceLostClosure {
    fn drop(&mut self) {
        unsafe {
            (self.cleanup_callback)(self.user_data);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_set_device_lost_callback(
    global: &Global,
    self_id: id::DeviceId,
    closure: DeviceLostClosure,
) {
    let closure = Box::new(move |reason, message| closure.call(reason, message));
    global.device_set_device_lost_closure(self_id, closure);
}

impl ShaderModuleCompilationMessage {
    fn set_error(&mut self, error: &CreateShaderModuleError, source: &str) {
        // The WebGPU spec says that if the message doesn't point to a particular position in
        // the source, the line number, position, offset and lengths should be zero.
        let line_number;
        let line_pos;
        let utf16_offset;
        let utf16_length;

        let location = match error {
            CreateShaderModuleError::Parsing(e) => e.inner.location(source),
            CreateShaderModuleError::Validation(e) => e.inner.location(source),
            _ => None,
        };

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

        let message = nsString::from(&error.to_string());

        *self = Self {
            line_number,
            line_pos,
            utf16_offset,
            utf16_length,
            message,
        };
    }
}

/// A compilation message representation for the ffi boundary.
/// the message is immediately copied into an equivalent C++
/// structure that owns its strings.
#[repr(C)]
#[derive(Clone)]
pub struct ShaderModuleCompilationMessage {
    pub line_number: u64,
    pub line_pos: u64,
    pub utf16_offset: u64,
    pub utf16_length: u64,
    pub message: nsString,
}

/// Creates a shader module and returns an object describing the errors if any.
///
/// If there was no error, the returned pointer is nil.
#[no_mangle]
pub extern "C" fn wgpu_server_device_create_shader_module(
    global: &Global,
    self_id: id::DeviceId,
    module_id: id::ShaderModuleId,
    label: Option<&nsACString>,
    code: &nsCString,
    out_message: &mut ShaderModuleCompilationMessage,
    mut error_buf: ErrorBuffer,
) -> bool {
    let utf8_label = label.map(|utf16| utf16.to_string());
    let label = utf8_label.as_ref().map(|s| Cow::from(&s[..]));

    let source_str = code.to_utf8();

    let source = wgc::pipeline::ShaderModuleSource::Wgsl(Cow::from(&source_str[..]));

    let desc = wgc::pipeline::ShaderModuleDescriptor {
        label,
        runtime_checks: Default::default(),
    };

    let (_, error) = global.device_create_shader_module(self_id, &desc, source, Some(module_id));

    if let Some(err) = error {
        out_message.set_error(&err, &source_str[..]);
        let err_type = match &err {
            CreateShaderModuleError::Device(DeviceError::OutOfMemory) => {
                ErrorBufferType::OutOfMemory
            }
            CreateShaderModuleError::Device(DeviceError::Lost) => ErrorBufferType::DeviceLost,
            _ => ErrorBufferType::Validation,
        };

        // Per spec: "User agents should not include detailed compiler error messages or
        // shader text in the message text of validation errors arising here: these details
        // are accessible via getCompilationInfo()"
        let message = match &err {
            CreateShaderModuleError::Parsing(_) => "Parsing error".to_string(),
            CreateShaderModuleError::Validation(_) => "Shader validation error".to_string(),
            CreateShaderModuleError::Device(device_err) => format!("{device_err:?}"),
            _ => format!("{err:?}"),
        };

        error_buf.init(ErrMsg {
            message: &format!("Shader module creation failed: {message}"),
            r#type: err_type,
        });
        return false;
    }

    // Avoid allocating the structure that holds errors in the common case (no errors).
    return true;
}

#[no_mangle]
pub extern "C" fn wgpu_server_device_create_buffer(
    global: &Global,
    self_id: id::DeviceId,
    buffer_id: id::BufferId,
    label: Option<&nsACString>,
    size: wgt::BufferAddress,
    usage: u32,
    mapped_at_creation: bool,
    shm_allocation_failed: bool,
    mut error_buf: ErrorBuffer,
) {
    let utf8_label = label.map(|utf16| utf16.to_string());
    let label = utf8_label.as_ref().map(|s| Cow::from(&s[..]));
    let usage = wgt::BufferUsages::from_bits_retain(usage);

    let desc = wgc::resource::BufferDescriptor {
        label,
        size,
        usage,
        mapped_at_creation,
    };

    // Don't trust the graphics driver with buffer sizes larger than our conservative max texture size.
    if shm_allocation_failed || size > MAX_BUFFER_SIZE {
        error_buf.init(ErrMsg {
            message: "Out of memory",
            r#type: ErrorBufferType::OutOfMemory,
        });
        global.create_buffer_error(Some(buffer_id), &desc);
        return;
    }

    let (_, error) = global.device_create_buffer(self_id, &desc, Some(buffer_id));
    if let Some(err) = error {
        error_buf.init(err);
    }
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

#[repr(C)]
pub struct BufferMapClosure {
    pub callback: unsafe extern "C" fn(user_data: *mut u8, status: BufferMapAsyncStatus),
    pub user_data: *mut u8,
}
unsafe impl Send for BufferMapClosure {}

/// # Safety
///
/// Callers are responsible for ensuring `closure` is well-formed.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_buffer_map(
    global: &Global,
    buffer_id: id::BufferId,
    start: wgt::BufferAddress,
    size: wgt::BufferAddress,
    map_mode: wgc::device::HostMap,
    closure: BufferMapClosure,
    mut error_buf: ErrorBuffer,
) {
    let closure = Box::new(move |result| {
        let _ = &closure;
        let status = match result {
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
            Err(BufferAccessError::OutOfBoundsUnderrun { .. })
            | Err(BufferAccessError::OutOfBoundsOverrun { .. })
            | Err(BufferAccessError::NegativeRange { .. }) => BufferMapAsyncStatus::InvalidRange,
            Err(BufferAccessError::Failed)
            | Err(BufferAccessError::NotMapped)
            | Err(BufferAccessError::MapAborted) => BufferMapAsyncStatus::Error,
            Err(_) => BufferMapAsyncStatus::Invalid,
        };

        (closure.callback)(closure.user_data, status)
    });
    let operation = wgc::resource::BufferMapOperation {
        host: map_mode,
        callback: Some(closure),
    };
    let result = global.buffer_map_async(buffer_id, start, Some(size), operation);

    if let Err(error) = result {
        error_buf.init(error);
    }
}

#[repr(C)]
pub struct MappedBufferSlice {
    pub ptr: *mut u8,
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
    mut error_buf: ErrorBuffer,
) -> MappedBufferSlice {
    let result = global.buffer_get_mapped_range(buffer_id, start, Some(size));

    let (ptr, length) = result
        .map(|(ptr, len)| (ptr.as_ptr(), len))
        .unwrap_or_else(|error| {
            error_buf.init(error);
            (std::ptr::null_mut(), 0)
        });
    MappedBufferSlice { ptr, length }
}

#[no_mangle]
pub extern "C" fn wgpu_server_buffer_unmap(
    global: &Global,
    buffer_id: id::BufferId,
    mut error_buf: ErrorBuffer,
) {
    if let Err(e) = global.buffer_unmap(buffer_id) {
        match e {
            // NOTE: This is presumed by CTS test cases, and was even formally specified in the
            // WebGPU spec. previously, but this doesn't seem formally specified now. :confused:
            //
            // TODO: upstream this; see <https://bugzilla.mozilla.org/show_bug.cgi?id=1842297>.
            BufferAccessError::InvalidResource(_) => (),
            other => error_buf.init(other),
        }
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_buffer_destroy(global: &Global, self_id: id::BufferId) {
    // Per spec, there is no need for the buffer or even device to be in a valid state,
    // even calling calling destroy multiple times is fine, so no error to push into
    // an error scope.
    let _ = global.buffer_destroy(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_buffer_drop(global: &Global, self_id: id::BufferId) {
    global.buffer_drop(self_id);
}

#[allow(unused_variables)]
#[no_mangle]
pub extern "C" fn wgpu_server_get_device_fence_handle(
    global: &Global,
    device_id: id::DeviceId,
) -> *mut c_void {
    #[cfg(target_os = "windows")]
    {
        let dx12_device = unsafe {
            global.device_as_hal::<wgc::api::Dx12, _, Option<Direct3D12::ID3D12Device>>(
                device_id,
                |hal_device| hal_device.map(|device| device.raw_device().clone()),
            )
        };
        let dx12_device = match dx12_device {
            Some(device) => device,
            None => {
                return ptr::null_mut();
            }
        };

        let dx12_fence = unsafe {
            global.device_fence_as_hal::<wgc::api::Dx12, _, Option<Direct3D12::ID3D12Fence>>(
                device_id,
                |hal_fence| hal_fence.map(|fence| fence.raw_fence().clone()),
            )
        };
        let dx12_fence = match dx12_fence {
            Some(fence) => fence,
            None => {
                return ptr::null_mut();
            }
        };

        let res = unsafe {
            dx12_device.CreateSharedHandle(&dx12_fence, None, Foundation::GENERIC_ALL.0, None)
        };

        return match res {
            Ok(handle) => handle.0,
            Err(_) => ptr::null_mut(),
        };
    }
    #[cfg(not(target_os = "windows"))]
    ptr::null_mut()
}

#[derive(Debug)]
#[repr(C)]
pub struct DMABufInfo {
    pub is_valid: bool,
    pub modifier: u64,
    pub plane_count: u32,
    pub offsets: [u64; 3],
    pub strides: [u64; 3],
}

#[derive(Debug)]
#[cfg(target_os = "linux")]
pub struct VkImageHandle {
    pub device: vk::Device,
    pub image: vk::Image,
    pub memory: vk::DeviceMemory,
    pub memory_size: u64,
    pub memory_type_index: u32,
    pub modifier: u64,
    pub layouts: Vec<vk::SubresourceLayout>,
}

#[cfg(target_os = "linux")]
impl VkImageHandle {
    fn destroy(&self, global: &Global, device_id: id::DeviceId) {
        unsafe {
            global.device_as_hal::<wgc::api::Vulkan, _, ()>(device_id, |hal_device| {
                let hal_device = match hal_device {
                    None => {
                        return;
                    }
                    Some(hal_device) => hal_device,
                };
                let device = hal_device.raw_device();

                (device.fp_v1_0().destroy_image)(self.device, self.image, ptr::null());
                (device.fp_v1_0().free_memory)(self.device, self.memory, ptr::null());
            })
        };
    }
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub extern "C" fn wgpu_vkimage_create_with_dma_buf(
    global: &Global,
    device_id: id::DeviceId,
    width: u32,
    height: u32,
    out_memory_size: *mut u64,
) -> *mut VkImageHandle {
    let image_handle = unsafe {
        global.device_as_hal::<wgc::api::Vulkan, _, Option<VkImageHandle>>(
            device_id,
            |hal_device| {
                let hal_device = match hal_device {
                    None => {
                        let msg = c"Vulkan device is invalid";
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }
                    Some(hal_device) => hal_device,
                };

                let device = hal_device.raw_device();
                let physical_device = hal_device.raw_physical_device();
                let instance = hal_device.shared_instance().raw_instance();

                let count = {
                    let mut drm_format_modifier_props_list =
                        vk::DrmFormatModifierPropertiesListEXT::default();
                    let mut format_properties_2 = vk::FormatProperties2::default()
                        .push_next(&mut drm_format_modifier_props_list);

                    instance.get_physical_device_format_properties2(
                        physical_device,
                        vk::Format::R8G8B8A8_UNORM,
                        &mut format_properties_2,
                    );
                    drm_format_modifier_props_list.drm_format_modifier_count
                };

                if count == 0 {
                    let msg = c"get_physical_device_format_properties2() failed";
                    gfx_critical_note(msg.as_ptr());
                    return None;
                }

                let mut modifier_props =
                    vec![vk::DrmFormatModifierPropertiesEXT::default(); count as usize];

                let mut drm_format_modifier_props_list =
                    vk::DrmFormatModifierPropertiesListEXT::default()
                        .drm_format_modifier_properties(&mut modifier_props);
                let mut format_properties_2 =
                    vk::FormatProperties2::default().push_next(&mut drm_format_modifier_props_list);

                instance.get_physical_device_format_properties2(
                    physical_device,
                    vk::Format::R8G8B8A8_UNORM,
                    &mut format_properties_2,
                );

                let mut usage_flags = vk::ImageUsageFlags::empty();
                usage_flags |= vk::ImageUsageFlags::COLOR_ATTACHMENT;

                modifier_props.retain(|modifier_prop| {
                    let support = is_dmabuf_supported(
                        instance,
                        physical_device,
                        vk::Format::R8G8B8A8_UNORM,
                        modifier_prop.drm_format_modifier,
                        usage_flags,
                    );
                    support
                });

                if modifier_props.is_empty() {
                    let msg = c"format not supported for dmabuf import";
                    gfx_critical_note(msg.as_ptr());
                    return None;
                }

                let modifiers: Vec<u64> = modifier_props
                    .iter()
                    .map(|modifier_prop| modifier_prop.drm_format_modifier)
                    .collect();

                let mut modifier_list = vk::ImageDrmFormatModifierListCreateInfoEXT::default()
                    .drm_format_modifiers(&modifiers);

                let extent = vk::Extent3D {
                    width,
                    height,
                    depth: 1,
                };

                let mut external_image_create_info = vk::ExternalMemoryImageCreateInfo::default()
                    .handle_types(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

                let mut export_memory_alloc_info = vk::ExportMemoryAllocateInfo::default()
                    .handle_types(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

                let flags = vk::ImageCreateFlags::empty();

                let vk_info = vk::ImageCreateInfo::default()
                    .flags(flags)
                    .image_type(vk::ImageType::TYPE_2D)
                    .format(vk::Format::R8G8B8A8_UNORM)
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
                        let msg =
                            CString::new(format!("create_image() failed: {:?}", err)).unwrap();
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }
                    Ok(image) => image,
                };

                let mut image_modifier_properties =
                    vk::ImageDrmFormatModifierPropertiesEXT::default();
                let image_drm_format_modifier =
                    ash::ext::image_drm_format_modifier::Device::new(instance, device);
                let ret = image_drm_format_modifier.get_image_drm_format_modifier_properties(
                    image,
                    &mut image_modifier_properties,
                );
                if ret.is_err() {
                    let msg = CString::new(format!(
                        "get_image_drm_format_modifier_properties() failed: {:?}",
                        ret
                    ))
                    .unwrap();
                    gfx_critical_note(msg.as_ptr());
                    return None;
                }

                let memory_req = device.get_image_memory_requirements(image);

                let mem_properties =
                    instance.get_physical_device_memory_properties(physical_device);

                let index = mem_properties
                    .memory_types
                    .iter()
                    .enumerate()
                    .position(|(i, t)| {
                        ((1 << i) & memory_req.memory_type_bits) != 0
                            && t.property_flags
                                .contains(vk::MemoryPropertyFlags::DEVICE_LOCAL)
                    });

                let index = match index {
                    None => {
                        let msg = c"Failed to get DEVICE_LOCAL memory index";
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }
                    Some(index) => index,
                };

                let mut dedicated_memory_info =
                    vk::MemoryDedicatedAllocateInfo::default().image(image);

                let memory_allocate_info = vk::MemoryAllocateInfo::default()
                    .allocation_size(memory_req.size)
                    .memory_type_index(index as u32)
                    .push_next(&mut dedicated_memory_info)
                    .push_next(&mut export_memory_alloc_info);

                let memory = match device.allocate_memory(&memory_allocate_info, None) {
                    Err(err) => {
                        let msg =
                            CString::new(format!("allocate_memory() failed: {:?}", err)).unwrap();
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }
                    Ok(memory) => memory,
                };

                let result = device.bind_image_memory(image, memory, /* offset */ 0);
                if result.is_err() {
                    let msg =
                        CString::new(format!("bind_image_memory() failed: {:?}", result)).unwrap();
                    gfx_critical_note(msg.as_ptr());
                    return None;
                }

                *out_memory_size = memory_req.size;

                let modifier_prop = modifier_props.iter().find(|prop| {
                    prop.drm_format_modifier == image_modifier_properties.drm_format_modifier
                });
                let modifier_prop = match modifier_prop {
                    None => {
                        let msg = c"failed to find modifier_prop";
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }
                    Some(modifier_prop) => modifier_prop,
                };

                let plane_count = modifier_prop.drm_format_modifier_plane_count;

                let mut layouts = Vec::new();
                for i in 0..plane_count {
                    let flag = match i {
                        0 => vk::ImageAspectFlags::PLANE_0,
                        1 => vk::ImageAspectFlags::PLANE_1,
                        2 => vk::ImageAspectFlags::PLANE_2,
                        _ => unreachable!(),
                    };
                    let subresource = vk::ImageSubresource::default().aspect_mask(flag);
                    let layout = device.get_image_subresource_layout(image, subresource);
                    layouts.push(layout);
                }

                Some(VkImageHandle {
                    device: device.handle(),
                    image,
                    memory,
                    memory_size: memory_req.size,
                    memory_type_index: index as u32,
                    modifier: image_modifier_properties.drm_format_modifier,
                    layouts,
                })
            },
        )
    };

    let image_handle = match image_handle {
        None => {
            return ptr::null_mut();
        }
        Some(image_handle) => image_handle,
    };

    Box::into_raw(Box::new(image_handle))
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub unsafe extern "C" fn wgpu_vkimage_destroy(
    global: &Global,
    device_id: id::DeviceId,
    handle: &VkImageHandle,
) {
    handle.destroy(global, device_id);
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub unsafe extern "C" fn wgpu_vkimage_delete(handle: *mut VkImageHandle) {
    let _ = Box::from_raw(handle);
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub extern "C" fn wgpu_vkimage_get_file_descriptor(
    global: &Global,
    device_id: id::DeviceId,
    handle: &VkImageHandle,
) -> i32 {
    unsafe {
        global.device_as_hal::<wgc::api::Vulkan, _, i32>(device_id, |hal_device| {
            let hal_device = match hal_device {
                None => {
                    let msg = c"Vulkan device is invalid";
                    gfx_critical_note(msg.as_ptr());
                    return -1;
                }
                Some(hal_device) => hal_device,
            };

            let device = hal_device.raw_device();
            let instance = hal_device.shared_instance().raw_instance();

            let get_fd_info = vk::MemoryGetFdInfoKHR::default()
                .memory(handle.memory)
                .handle_type(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

            let loader = khr::external_memory_fd::Device::new(instance, device);

            return match loader.get_memory_fd(&get_fd_info) {
                Err(..) => -1,
                Ok(fd) => fd,
            };
        })
    }
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub extern "C" fn wgpu_vkimage_get_dma_buf_info(handle: &VkImageHandle) -> DMABufInfo {
    let mut offsets: [u64; 3] = [0; 3];
    let mut strides: [u64; 3] = [0; 3];
    let plane_count = handle.layouts.len();
    for i in 0..plane_count {
        offsets[i] = handle.layouts[i].offset;
        strides[i] = handle.layouts[i].row_pitch;
    }

    DMABufInfo {
        is_valid: true,
        modifier: handle.modifier,
        plane_count: plane_count as u32,
        offsets,
        strides,
    }
}

#[cfg(target_os = "macos")]
pub struct MetalSharedEventHandle(metal::SharedEvent);
#[cfg(not(target_os = "macos"))]
pub struct MetalSharedEventHandle;

#[no_mangle]
#[allow(unreachable_code)]
#[allow(unused_variables)]
pub extern "C" fn wgpu_server_get_device_fence_metal_shared_event(
    global: &Global,
    device_id: id::DeviceId,
) -> *mut MetalSharedEventHandle {
    #[cfg(target_os = "macos")]
    {
        let shared_event = unsafe {
            global.device_fence_as_hal::<wgc::api::Metal, _, Option<metal::SharedEvent>>(
                device_id,
                |hal_fence| hal_fence.map(|fence| fence.raw_shared_event().unwrap().clone()),
            )
        };
        let shared_event = match shared_event {
            Some(shared_event) => shared_event,
            None => {
                return ptr::null_mut();
            }
        };
        return Box::into_raw(Box::new(MetalSharedEventHandle(shared_event)));
    }

    ptr::null_mut()
}

#[no_mangle]
#[allow(unreachable_code)]
#[allow(unused_variables)]
pub extern "C" fn wgpu_server_metal_shared_event_signaled_value(
    shared_event: &mut MetalSharedEventHandle,
) -> u64 {
    #[cfg(target_os = "macos")]
    {
        return shared_event.0.signaled_value();
    }

    u64::MAX
}

#[no_mangle]
#[allow(unreachable_code)]
#[allow(unused_variables)]
pub extern "C" fn wgpu_server_delete_metal_shared_event(shared_event: *mut MetalSharedEventHandle) {
    #[cfg(target_os = "macos")]
    {
        let _ = unsafe { Box::from_raw(shared_event) };
    }
}

extern "C" {
    #[allow(dead_code)]
    fn gfx_critical_note(msg: *const c_char);
    #[allow(dead_code)]
    fn wgpu_server_use_external_texture_for_swap_chain(
        param: *mut c_void,
        swap_chain_id: SwapChainId,
    ) -> bool;
    #[allow(dead_code)]
    fn wgpu_server_disable_external_texture_for_swap_chain(
        param: *mut c_void,
        swap_chain_id: SwapChainId,
    );
    #[allow(dead_code)]
    fn wgpu_server_ensure_external_texture_for_swap_chain(
        param: *mut c_void,
        swap_chain_id: SwapChainId,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        width: u32,
        height: u32,
        format: wgt::TextureFormat,
        usage: wgt::TextureUsages,
    ) -> bool;
    #[allow(dead_code)]
    fn wgpu_server_ensure_external_texture_for_readback(
        param: *mut c_void,
        swap_chain_id: SwapChainId,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        width: u32,
        height: u32,
        format: wgt::TextureFormat,
        usage: wgt::TextureUsages,
    );
    #[allow(dead_code)]
    fn wgpu_server_get_external_texture_handle(
        param: *mut c_void,
        id: id::TextureId,
    ) -> *mut c_void;
    #[allow(improper_ctypes)]
    #[allow(dead_code)]
    #[cfg(target_os = "linux")]
    fn wgpu_server_get_vk_image_handle(
        param: *mut c_void,
        texture_id: id::TextureId,
    ) -> *const VkImageHandle;
    #[allow(dead_code)]
    fn wgpu_server_get_dma_buf_fd(param: *mut c_void, id: id::TextureId) -> i32;
    #[allow(dead_code)]
    fn wgpu_server_get_external_io_surface_id(param: *mut c_void, id: id::TextureId) -> u32;
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

    match instance.get_physical_device_image_format_properties2(
        physical_device,
        &format_info,
        &mut props,
    ) {
        Ok(_) => (),
        Err(_) => {
            //debug!(?format, ?modifier, "format not supported for dma import");
            return false;
        }
    }

    drm_props
        .external_memory_properties
        .compatible_handle_types
        .contains(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT)
}

#[cfg(target_os = "linux")]
pub fn select_memory_type(
    props: &vk::PhysicalDeviceMemoryProperties,
    flags: vk::MemoryPropertyFlags,
    memory_type_bits: Option<u32>,
) -> Option<u32> {
    for i in 0..props.memory_type_count {
        if let Some(mask) = memory_type_bits {
            if mask & (1 << i) == 0 {
                continue;
            }
        }

        if flags.is_empty()
            || props.memory_types[i as usize]
                .property_flags
                .contains(flags)
        {
            return Some(i);
        }
    }

    None
}

#[cfg(target_os = "linux")]
struct VkImageHolder {
    pub device: vk::Device,
    pub image: vk::Image,
    pub memory: vk::DeviceMemory,
    pub fn_destroy_image: vk::PFN_vkDestroyImage,
    pub fn_free_memory: vk::PFN_vkFreeMemory,
}

#[cfg(target_os = "linux")]
impl VkImageHolder {
    fn destroy(&self) {
        unsafe {
            (self.fn_destroy_image)(self.device, self.image, ptr::null());
            (self.fn_free_memory)(self.device, self.memory, ptr::null());
        }
    }
}

impl Global {
    #[cfg(target_os = "windows")]
    fn create_texture_with_external_texture_d3d11(
        &self,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        desc: &wgc::resource::TextureDescriptor,
        swap_chain_id: Option<SwapChainId>,
    ) -> bool {
        let dx12_device = unsafe {
            self.device_as_hal::<wgc::api::Dx12, _, Option<Direct3D12::ID3D12Device>>(
                device_id,
                |hal_device| {
                    if hal_device.is_none() {
                        return None;
                    }
                    hal_device.map(|hal_device| hal_device.raw_device().clone())
                },
            )
        };

        if dx12_device.is_none() {
            let msg = c"dx12 device is none";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let dx12_device = dx12_device.unwrap();
        let ret = unsafe {
            wgpu_server_ensure_external_texture_for_swap_chain(
                self.owner,
                swap_chain_id.unwrap(),
                device_id,
                texture_id,
                desc.size.width,
                desc.size.height,
                desc.format,
                desc.usage,
            )
        };
        if ret != true {
            let msg = c"Failed to create external texture";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let handle = unsafe { wgpu_server_get_external_texture_handle(self.owner, texture_id) };
        if handle.is_null() {
            let msg = c"Failed to get external texture handle";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }
        let mut resource: Option<Direct3D12::ID3D12Resource> = None;
        let res =
            unsafe { dx12_device.OpenSharedHandle(Foundation::HANDLE(handle), &mut resource) };
        if res.is_err() || resource.is_none() {
            let msg = c"Failed to open shared handle";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let hal_texture = unsafe {
            <wgh::api::Dx12 as wgh::Api>::Device::texture_from_raw(
                resource.unwrap(),
                wgt::TextureFormat::Bgra8Unorm,
                wgt::TextureDimension::D2,
                desc.size,
                1,
                1,
            )
        };
        let (_, error) = unsafe {
            self.create_texture_from_hal(Box::new(hal_texture), device_id, &desc, Some(texture_id))
        };
        if let Some(err) = error {
            let msg = CString::new(format!("create_texture_from_hal() failed: {:?}", err)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        true
    }

    #[allow(dead_code)]
    #[cfg(target_os = "linux")]
    fn create_texture_with_external_texture_dmabuf(
        &self,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        desc: &wgc::resource::TextureDescriptor,
        swap_chain_id: Option<SwapChainId>,
    ) -> bool {
        let ret = unsafe {
            wgpu_server_ensure_external_texture_for_swap_chain(
                self.owner,
                swap_chain_id.unwrap(),
                device_id,
                texture_id,
                desc.size.width,
                desc.size.height,
                desc.format,
                desc.usage,
            )
        };
        if ret != true {
            let msg = c"Failed to create external texture";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let handle = unsafe { wgpu_server_get_vk_image_handle(self.owner, texture_id) };
        if handle.is_null() {
            let msg = c"Failed to get VkImageHandle";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let vk_image_wrapper = unsafe { &*handle };

        let fd = unsafe { wgpu_server_get_dma_buf_fd(self.owner, texture_id) };
        if fd < 0 {
            let msg = c"Failed to get DMABuf fd";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        // Ensure to close file descriptor
        let owned_fd = unsafe { OwnedFd::from_raw_fd(fd as RawFd) };

        let image_holder = unsafe {
            self.device_as_hal::<wgc::api::Vulkan, _, Option<VkImageHolder>>(
                device_id,
                |hal_device| {
                    let hal_device = match hal_device {
                        None => {
                            let msg = c"Vulkan device is invalid";
                            gfx_critical_note(msg.as_ptr());
                            return None;
                        }
                        Some(hal_device) => hal_device,
                    };

                    let device = hal_device.raw_device();

                    let extent = vk::Extent3D {
                        width: desc.size.width,
                        height: desc.size.height,
                        depth: 1,
                    };
                    let mut usage_flags = vk::ImageUsageFlags::empty();
                    usage_flags |= vk::ImageUsageFlags::COLOR_ATTACHMENT;

                    let mut external_image_create_info =
                        vk::ExternalMemoryImageCreateInfo::default()
                            .handle_types(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT);

                    let vk_info = vk::ImageCreateInfo::default()
                        .flags(vk::ImageCreateFlags::ALIAS)
                        .image_type(vk::ImageType::TYPE_2D)
                        .format(vk::Format::R8G8B8A8_UNORM)
                        .extent(extent)
                        .mip_levels(1)
                        .array_layers(1)
                        .samples(vk::SampleCountFlags::TYPE_1)
                        .tiling(vk::ImageTiling::OPTIMAL)
                        .usage(usage_flags)
                        .sharing_mode(vk::SharingMode::EXCLUSIVE)
                        .initial_layout(vk::ImageLayout::UNDEFINED)
                        .push_next(&mut external_image_create_info);

                    let image = match device.create_image(&vk_info, None) {
                        Err(err) => {
                            let msg =
                                CString::new(format!("create_image() failed: {:?}", err)).unwrap();
                            gfx_critical_note(msg.as_ptr());
                            return None;
                        }
                        Ok(image) => image,
                    };

                    let memory_req = device.get_image_memory_requirements(image);
                    if memory_req.size > vk_image_wrapper.memory_size {
                        let msg = c"Invalid memory size";
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }

                    let mut dedicated_memory_info =
                        vk::MemoryDedicatedAllocateInfo::default().image(image);

                    let mut import_memory_fd_info = vk::ImportMemoryFdInfoKHR::default()
                        .handle_type(vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT)
                        .fd(owned_fd.into_raw_fd());

                    let memory_allocate_info = vk::MemoryAllocateInfo::default()
                        .allocation_size(vk_image_wrapper.memory_size)
                        .memory_type_index(vk_image_wrapper.memory_type_index)
                        .push_next(&mut dedicated_memory_info)
                        .push_next(&mut import_memory_fd_info);

                    let memory = match device.allocate_memory(&memory_allocate_info, None) {
                        Err(err) => {
                            let msg = CString::new(format!("allocate_memory() failed: {:?}", err))
                                .unwrap();
                            gfx_critical_note(msg.as_ptr());
                            return None;
                        }
                        Ok(memory) => memory,
                    };

                    let result = device.bind_image_memory(image, memory, /* offset */ 0);
                    if result.is_err() {
                        let msg = CString::new(format!("bind_image_memory() failed: {:?}", result))
                            .unwrap();
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }

                    Some(VkImageHolder {
                        device: device.handle(),
                        image,
                        memory,
                        fn_destroy_image: device.fp_v1_0().destroy_image,
                        fn_free_memory: device.fp_v1_0().free_memory,
                    })
                },
            )
        };

        let image_holder = match image_holder {
            None => {
                let msg = c"Failed to get vk::Image";
                unsafe {
                    gfx_critical_note(msg.as_ptr());
                }
                return false;
            }
            Some(image_holder) => image_holder,
        };

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

        let image = image_holder.image;

        let hal_texture = unsafe {
            <wgh::api::Vulkan as wgh::Api>::Device::texture_from_raw(
                image,
                &hal_desc,
                Some(Box::new(move || {
                    image_holder.destroy();
                })),
            )
        };

        let (_, error) = unsafe {
            self.create_texture_from_hal(Box::new(hal_texture), device_id, &desc, Some(texture_id))
        };
        if let Some(err) = error {
            let msg = CString::new(format!("create_texture_from_hal() failed: {:?}", err)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        true
    }

    #[cfg(target_os = "macos")]
    fn create_texture_with_external_texture_iosurface(
        &self,
        device_id: id::DeviceId,
        texture_id: id::TextureId,
        desc: &wgc::resource::TextureDescriptor,
        swap_chain_id: Option<SwapChainId>,
    ) -> bool {
        let ret = unsafe {
            wgpu_server_ensure_external_texture_for_swap_chain(
                self.owner,
                swap_chain_id.unwrap(),
                device_id,
                texture_id,
                desc.size.width,
                desc.size.height,
                desc.format,
                desc.usage,
            )
        };
        if ret != true {
            let msg = c"Failed to create external texture";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let io_surface_id =
            unsafe { wgpu_server_get_external_io_surface_id(self.owner, texture_id) };
        if io_surface_id == 0 {
            let msg = c"Failed to get io surface id";
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        let io_surface = io_surface::lookup(io_surface_id);

        let desc_ref = &desc;

        let raw = unsafe {
            self.device_as_hal::<wgc::api::Metal, _, Option<metal::Texture>>(
                device_id,
                |hal_device| {
                    let hal_device = match hal_device {
                        None => {
                            let msg = c"metal device is invalid";
                            gfx_critical_note(msg.as_ptr());
                            return None;
                        }
                        Some(hal_device) => hal_device,
                    };

                    use metal::foreign_types::ForeignType as _;
                    let device = hal_device.raw_device();

                    objc::rc::autoreleasepool(|| {
                        let descriptor = metal::TextureDescriptor::new();
                        let usage = metal::MTLTextureUsage::RenderTarget
                            | metal::MTLTextureUsage::ShaderRead
                            | metal::MTLTextureUsage::PixelFormatView;

                        descriptor.set_texture_type(metal::MTLTextureType::D2);
                        descriptor.set_width(desc_ref.size.width as u64);
                        descriptor.set_height(desc_ref.size.height as u64);
                        descriptor.set_mipmap_level_count(desc_ref.mip_level_count as u64);
                        descriptor.set_pixel_format(metal::MTLPixelFormat::BGRA8Unorm);
                        descriptor.set_usage(usage);
                        descriptor.set_storage_mode(metal::MTLStorageMode::Private);

                        let raw_device = device.lock();
                        let raw_texture: metal::Texture = msg_send![*raw_device, newTextureWithDescriptor: descriptor
                        iosurface:io_surface.obj
                        plane:0];

                        if raw_texture.as_ptr().is_null() {
                            let msg = c"Failed to create metal::Texture for swap chain";
                            gfx_critical_note(msg.as_ptr());
                            return None;
                        }

                        if let Some(label) = &desc_ref.label {
                            raw_texture.set_label(&label);
                        }

                        Some(raw_texture)
                    })
                },
            )
        };

        let hal_texture = unsafe {
            <wgh::api::Metal as wgh::Api>::Device::texture_from_raw(
                raw.unwrap(),
                wgt::TextureFormat::Bgra8Unorm,
                metal::MTLTextureType::D2,
                1,
                1,
                wgh::CopyExtent {
                    width: desc.size.width,
                    height: desc.size.height,
                    depth: 1,
                },
            )
        };

        let (_, error) = unsafe {
            self.create_texture_from_hal(Box::new(hal_texture), device_id, &desc, Some(texture_id))
        };
        if let Some(err) = error {
            let msg = CString::new(format!("create_texture_from_hal() failed: {:?}", err)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return false;
        }

        true
    }

    fn device_action(
        &self,
        self_id: id::DeviceId,
        action: DeviceAction,
        mut error_buf: ErrorBuffer,
    ) {
        match action {
            #[allow(unused_variables)]
            DeviceAction::CreateTexture(id, desc, swap_chain_id) => {
                let max = MAX_TEXTURE_EXTENT;
                if desc.size.width > max
                    || desc.size.height > max
                    || desc.size.depth_or_array_layers > max
                {
                    self.create_texture_error(Some(id), &desc);
                    error_buf.init(ErrMsg {
                        message: "Out of memory",
                        r#type: ErrorBufferType::OutOfMemory,
                    });
                    return;
                }

                if [
                    desc.size.width,
                    desc.size.height,
                    desc.size.depth_or_array_layers,
                ]
                .contains(&0)
                {
                    self.create_texture_error(Some(id), &desc);
                    error_buf.init(ErrMsg {
                        message: "size is zero",
                        r#type: ErrorBufferType::Validation,
                    });
                    return;
                }

                let use_external_texture = if let Some(id) = swap_chain_id {
                    unsafe { wgpu_server_use_external_texture_for_swap_chain(self.owner, id) }
                } else {
                    false
                };

                if use_external_texture {
                    let limits = self.device_limits(self_id);
                    if desc.size.width > limits.max_texture_dimension_2d
                        || desc.size.height > limits.max_texture_dimension_2d
                    {
                        self.create_texture_error(Some(id), &desc);
                        error_buf.init(ErrMsg {
                            message: "size exceeds limits.max_texture_dimension_2d",
                            r#type: ErrorBufferType::Validation,
                        });
                        return;
                    }

                    let features = self.device_features(self_id);
                    if desc.format == wgt::TextureFormat::Bgra8Unorm
                        && desc.usage.contains(wgt::TextureUsages::STORAGE_BINDING)
                        && !features.contains(wgt::Features::BGRA8UNORM_STORAGE)
                    {
                        self.create_texture_error(Some(id), &desc);
                        error_buf.init(ErrMsg {
                            message: "Bgra8Unorm with GPUStorageBinding usage with BGRA8UNORM_STORAGE disabled",
                            r#type: ErrorBufferType::Validation,
                        });
                        return;
                    }

                    #[cfg(target_os = "windows")]
                    {
                        let is_created = self.create_texture_with_external_texture_d3d11(
                            self_id,
                            id,
                            &desc,
                            swap_chain_id,
                        );
                        if is_created {
                            return;
                        }
                    }

                    #[cfg(target_os = "linux")]
                    {
                        let is_created = self.create_texture_with_external_texture_dmabuf(
                            self_id,
                            id,
                            &desc,
                            swap_chain_id,
                        );
                        if is_created {
                            return;
                        }
                    }

                    #[cfg(target_os = "macos")]
                    {
                        let is_created = self.create_texture_with_external_texture_iosurface(
                            self_id,
                            id,
                            &desc,
                            swap_chain_id,
                        );
                        if is_created {
                            return;
                        }
                    }

                    unsafe {
                        wgpu_server_disable_external_texture_for_swap_chain(
                            self.owner,
                            swap_chain_id.unwrap(),
                        )
                    };
                }

                if let Some(swap_chain_id) = swap_chain_id {
                    unsafe {
                        wgpu_server_ensure_external_texture_for_readback(
                            self.owner,
                            swap_chain_id,
                            self_id,
                            id,
                            desc.size.width,
                            desc.size.height,
                            desc.format,
                            desc.usage,
                        )
                    };
                }

                let (_, error) = self.device_create_texture(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateSampler(id, desc) => {
                let (_, error) = self.device_create_sampler(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateBindGroupLayout(id, desc) => {
                let (_, error) = self.device_create_bind_group_layout(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::RenderPipelineGetBindGroupLayout(pipeline_id, index, bgl_id) => {
                let (_, error) =
                    self.render_pipeline_get_bind_group_layout(pipeline_id, index, Some(bgl_id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::ComputePipelineGetBindGroupLayout(pipeline_id, index, bgl_id) => {
                let (_, error) =
                    self.compute_pipeline_get_bind_group_layout(pipeline_id, index, Some(bgl_id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreatePipelineLayout(id, desc) => {
                let (_, error) = self.device_create_pipeline_layout(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateBindGroup(id, desc) => {
                let (_, error) = self.device_create_bind_group(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateShaderModule(id, desc, code) => {
                let source = wgc::pipeline::ShaderModuleSource::Wgsl(code);
                let (_, error) = self.device_create_shader_module(self_id, &desc, source, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateComputePipeline(id, desc, implicit) => {
                let implicit_ids = implicit
                    .as_ref()
                    .map(|imp| wgc::device::ImplicitPipelineIds {
                        root_id: imp.pipeline,
                        group_ids: &imp.bind_groups,
                    });
                let (_, error) =
                    self.device_create_compute_pipeline(self_id, &desc, Some(id), implicit_ids);
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateRenderPipeline(id, desc, implicit) => {
                let implicit_ids = implicit
                    .as_ref()
                    .map(|imp| wgc::device::ImplicitPipelineIds {
                        root_id: imp.pipeline,
                        group_ids: &imp.bind_groups,
                    });
                let (_, error) =
                    self.device_create_render_pipeline(self_id, &desc, Some(id), implicit_ids);
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateRenderBundle(id, encoder, desc) => {
                let (_, error) = self.render_bundle_encoder_finish(encoder, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateRenderBundleError(buffer_id, label) => {
                self.create_render_bundle_error(
                    Some(buffer_id),
                    &wgt::RenderBundleDescriptor { label },
                );
            }
            DeviceAction::CreateQuerySet(id, desc) => {
                let (_, error) = self.device_create_query_set(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::CreateCommandEncoder(id, desc) => {
                let (_, error) = self.device_create_command_encoder(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
            DeviceAction::Error { message, r#type } => {
                error_buf.init(ErrMsg {
                    message: &message,
                    r#type,
                });
            }
        }
    }

    fn texture_action(
        &self,
        self_id: id::TextureId,
        action: TextureAction,
        mut error_buf: ErrorBuffer,
    ) {
        match action {
            TextureAction::CreateView(id, desc) => {
                let (_, error) = self.texture_create_view(self_id, &desc, Some(id));
                if let Some(err) = error {
                    error_buf.init(err);
                }
            }
        }
    }

    fn command_encoder_action(
        &self,
        self_id: id::CommandEncoderId,
        action: CommandEncoderAction,
        mut error_buf: ErrorBuffer,
    ) {
        match action {
            CommandEncoderAction::CopyBufferToBuffer {
                src,
                src_offset,
                dst,
                dst_offset,
                size,
            } => {
                if let Err(err) = self.command_encoder_copy_buffer_to_buffer(
                    self_id, src, src_offset, dst, dst_offset, size,
                ) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::CopyBufferToTexture { src, dst, size } => {
                if let Err(err) =
                    self.command_encoder_copy_buffer_to_texture(self_id, &src, &dst, &size)
                {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::CopyTextureToBuffer { src, dst, size } => {
                if let Err(err) =
                    self.command_encoder_copy_texture_to_buffer(self_id, &src, &dst, &size)
                {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::CopyTextureToTexture { src, dst, size } => {
                if let Err(err) =
                    self.command_encoder_copy_texture_to_texture(self_id, &src, &dst, &size)
                {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::RunComputePass {
                base,
                timestamp_writes,
            } => {
                if let Err(err) = self.compute_pass_end_with_unresolved_commands(
                    self_id,
                    base,
                    timestamp_writes.as_ref(),
                ) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::WriteTimestamp {
                query_set_id,
                query_index,
            } => {
                if let Err(err) =
                    self.command_encoder_write_timestamp(self_id, query_set_id, query_index)
                {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::ResolveQuerySet {
                query_set_id,
                start_query,
                query_count,
                destination,
                destination_offset,
            } => {
                if let Err(err) = self.command_encoder_resolve_query_set(
                    self_id,
                    query_set_id,
                    start_query,
                    query_count,
                    destination,
                    destination_offset,
                ) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::RunRenderPass {
                base,
                target_colors,
                target_depth_stencil,
                timestamp_writes,
                occlusion_query_set_id,
            } => {
                if let Err(err) = self.render_pass_end_with_unresolved_commands(
                    self_id,
                    base,
                    &target_colors,
                    target_depth_stencil.as_ref(),
                    timestamp_writes.as_ref(),
                    occlusion_query_set_id,
                ) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::ClearBuffer { dst, offset, size } => {
                if let Err(err) = self.command_encoder_clear_buffer(self_id, dst, offset, size) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::ClearTexture {
                dst,
                ref subresource_range,
            } => {
                if let Err(err) =
                    self.command_encoder_clear_texture(self_id, dst, subresource_range)
                {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::PushDebugGroup(marker) => {
                if let Err(err) = self.command_encoder_push_debug_group(self_id, &marker) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::PopDebugGroup => {
                if let Err(err) = self.command_encoder_pop_debug_group(self_id) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::InsertDebugMarker(marker) => {
                if let Err(err) = self.command_encoder_insert_debug_marker(self_id, &marker) {
                    error_buf.init(err);
                }
            }
            CommandEncoderAction::BuildAccelerationStructuresUnsafeTlas { .. }
            | CommandEncoderAction::BuildAccelerationStructures { .. } => {
                unreachable!("internal error: attempted to build acceleration structures")
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_device_action(
    global: &Global,
    self_id: id::DeviceId,
    byte_buf: &ByteBuf,
    error_buf: ErrorBuffer,
) {
    let action = bincode::deserialize(byte_buf.as_slice()).unwrap();
    global.device_action(self_id, action, error_buf);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_texture_action(
    global: &Global,
    self_id: id::TextureId,
    byte_buf: &ByteBuf,
    error_buf: ErrorBuffer,
) {
    let action = bincode::deserialize(byte_buf.as_slice()).unwrap();
    global.texture_action(self_id, action, error_buf);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_command_encoder_action(
    global: &Global,
    self_id: id::CommandEncoderId,
    byte_buf: &ByteBuf,
    error_buf: ErrorBuffer,
) {
    let action = bincode::deserialize(byte_buf.as_slice()).unwrap();
    global.command_encoder_action(self_id, action, error_buf);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_render_pass(
    global: &Global,
    encoder_id: id::CommandEncoderId,
    byte_buf: &ByteBuf,
    error_buf: ErrorBuffer,
) {
    let pass = bincode::deserialize(byte_buf.as_slice()).unwrap();

    trait ReplayRenderPass {
        fn replay_render_pass(
            &self,
            encoder_id: id::CommandEncoderId,
            src_pass: &RecordedRenderPass,
            error_buf: ErrorBuffer,
        );
    }
    impl ReplayRenderPass for Global {
        fn replay_render_pass(
            &self,
            encoder_id: id::CommandEncoderId,
            src_pass: &RecordedRenderPass,
            error_buf: ErrorBuffer,
        ) {
            crate::command::replay_render_pass(self, encoder_id, src_pass, error_buf);
        }
    }

    global.replay_render_pass(encoder_id, &pass, error_buf);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_compute_pass(
    global: &Global,
    encoder_id: id::CommandEncoderId,
    byte_buf: &ByteBuf,
    error_buf: ErrorBuffer,
) {
    let src_pass = bincode::deserialize(byte_buf.as_slice()).unwrap();

    trait ReplayComputePass {
        fn replay_compute_pass(
            &self,
            encoder_id: id::CommandEncoderId,
            src_pass: &RecordedComputePass,
            error_buf: ErrorBuffer,
        );
    }
    impl ReplayComputePass for Global {
        fn replay_compute_pass(
            &self,
            encoder_id: id::CommandEncoderId,
            src_pass: &RecordedComputePass,
            error_buf: ErrorBuffer,
        ) {
            crate::command::replay_compute_pass(self, encoder_id, src_pass, error_buf);
        }
    }

    global.replay_compute_pass(encoder_id, &src_pass, error_buf);
}

#[no_mangle]
pub extern "C" fn wgpu_server_device_create_encoder(
    global: &Global,
    self_id: id::DeviceId,
    desc: &wgt::CommandEncoderDescriptor<Option<&nsACString>>,
    new_id: id::CommandEncoderId,
    mut error_buf: ErrorBuffer,
) {
    let utf8_label = desc.label.map(|utf16| utf16.to_string());
    let label = utf8_label.as_ref().map(|s| Cow::from(&s[..]));

    let desc = desc.map_label(|_| label);
    let (_, error) = global.device_create_command_encoder(self_id, &desc, Some(new_id));
    if let Some(err) = error {
        error_buf.init(err);
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_encoder_finish(
    global: &Global,
    self_id: id::CommandEncoderId,
    desc: &wgt::CommandBufferDescriptor<Option<&nsACString>>,
    mut error_buf: ErrorBuffer,
) {
    let label = wgpu_string(desc.label);
    let desc = desc.map_label(|_| label);
    let (_, error) = global.command_encoder_finish(self_id, &desc);
    if let Some(err) = error {
        error_buf.init(err);
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_encoder_drop(global: &Global, self_id: id::CommandEncoderId) {
    global.command_encoder_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_render_bundle_drop(global: &Global, self_id: id::RenderBundleId) {
    global.render_bundle_drop(self_id);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_encoder_copy_texture_to_buffer(
    global: &Global,
    self_id: id::CommandEncoderId,
    source: &wgc::command::TexelCopyTextureInfo,
    dst_buffer: wgc::id::BufferId,
    dst_layout: &crate::TexelCopyBufferLayout,
    size: &wgt::Extent3d,
    mut error_buf: ErrorBuffer,
) {
    let destination = wgc::command::TexelCopyBufferInfo {
        buffer: dst_buffer,
        layout: dst_layout.into_wgt(),
    };
    if let Err(err) =
        global.command_encoder_copy_texture_to_buffer(self_id, source, &destination, size)
    {
        error_buf.init(err);
    }
}

/// # Safety
///
/// This function is unsafe as there is no guarantee that the given pointer is
/// valid for `command_buffer_id_length` elements.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_queue_submit(
    global: &Global,
    self_id: id::QueueId,
    command_buffer_ids: *const id::CommandBufferId,
    command_buffer_id_length: usize,
    mut error_buf: ErrorBuffer,
) -> u64 {
    let command_buffers = slice::from_raw_parts(command_buffer_ids, command_buffer_id_length);
    let result = global.queue_submit(self_id, command_buffers);

    match result {
        Err((_index, err)) => {
            error_buf.init(err);
            return 0;
        }
        Ok(wrapped_index) => wrapped_index,
    }
}

#[repr(C)]
pub struct SubmittedWorkDoneClosure {
    pub callback: unsafe extern "C" fn(user_data: *mut u8),
    pub user_data: *mut u8,
}
unsafe impl Send for SubmittedWorkDoneClosure {}

#[derive(Debug)]
#[cfg(target_os = "linux")]
pub struct VkSemaphoreHandle {
    pub semaphore: vk::Semaphore,
}

#[allow(dead_code)]
fn emit_critical_invalid_note_if_none<T>(what: &'static str, t: Option<T>) -> Option<T> {
    if t.is_none() {
        // SAFETY: We ensure that the pointer provided is not null.
        let msg = CString::new(format!("{what} is invalid")).unwrap();
        unsafe { gfx_critical_note(msg.as_ptr()) }
    }
    t
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub extern "C" fn wgpu_vksemaphore_create_signal_semaphore(
    global: &Global,
    queue_id: id::QueueId,
) -> *mut VkSemaphoreHandle {
    let semaphore_handle = unsafe {
        global.queue_as_hal::<wgc::api::Vulkan, _, Option<VkSemaphoreHandle>>(
            queue_id,
            |hal_queue| {
                let hal_queue = emit_critical_invalid_note_if_none("Vulkan queue", hal_queue)?;
                let device = hal_queue.raw_device();

                let mut export_semaphore_create_info = vk::ExportSemaphoreCreateInfo::default()
                    .handle_types(vk::ExternalSemaphoreHandleTypeFlags::OPAQUE_FD);
                let create_info =
                    vk::SemaphoreCreateInfo::default().push_next(&mut export_semaphore_create_info);
                let semaphore = match device.create_semaphore(&create_info, None) {
                    Err(err) => {
                        let msg =
                            CString::new(format!("create_semaphore() failed: {:?}", err)).unwrap();
                        gfx_critical_note(msg.as_ptr());
                        return None;
                    }
                    Ok(semaphore) => semaphore,
                };

                hal_queue.add_signal_semaphore(semaphore, None);

                Some(VkSemaphoreHandle { semaphore })
            },
        )
    };

    match semaphore_handle {
        None => ptr::null_mut(),
        Some(semaphore_handle) => Box::into_raw(Box::new(semaphore_handle)),
    }
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub unsafe extern "C" fn wgpu_vksemaphore_get_file_descriptor(
    global: &Global,
    device_id: id::DeviceId,
    handle: &VkSemaphoreHandle,
) -> i32 {
    let file_descriptor = unsafe {
        global.device_as_hal::<wgc::api::Vulkan, _, Option<i32>>(device_id, |hal_device| {
            let hal_device = emit_critical_invalid_note_if_none("Vulkan device", hal_device)?;
            let device = hal_device.raw_device();
            let instance = hal_device.shared_instance().raw_instance();

            let external_semaphore_fd = khr::external_semaphore_fd::Device::new(instance, device);
            let get_fd_info = vk::SemaphoreGetFdInfoKHR::default()
                .semaphore(handle.semaphore)
                .handle_type(vk::ExternalSemaphoreHandleTypeFlags::OPAQUE_FD);

            external_semaphore_fd.get_semaphore_fd(&get_fd_info).ok()
        })
    };

    // From [Wikipedia](https://en.wikipedia.org/wiki/File_descriptor):
    //
    // > File descriptors typically have non-negative integer values, with negative values
    // > being reserved to indicate "no value" or error conditions.
    file_descriptor.unwrap_or(-1)
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub unsafe extern "C" fn wgpu_vksemaphore_destroy(
    global: &Global,
    device_id: id::DeviceId,
    handle: &VkSemaphoreHandle,
) {
    unsafe {
        global.device_as_hal::<wgc::api::Vulkan, _, ()>(device_id, |hal_device| {
            let hal_device = emit_critical_invalid_note_if_none("Vulkan device", hal_device);
            let hal_device = match hal_device {
                None => {
                    return;
                }
                Some(hal_device) => hal_device,
            };
            let device = hal_device.raw_device();
            device.destroy_semaphore(handle.semaphore, None);
        })
    };
}

#[no_mangle]
#[cfg(target_os = "linux")]
pub unsafe extern "C" fn wgpu_vksemaphore_delete(handle: *mut VkSemaphoreHandle) {
    let _ = Box::from_raw(handle);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_server_on_submitted_work_done(
    global: &Global,
    self_id: id::QueueId,
    closure: SubmittedWorkDoneClosure,
) {
    let closure = Box::new(move || {
        let _ = &closure;
        (closure.callback)(closure.user_data)
    });
    global.queue_on_submitted_work_done(self_id, closure);
}

/// # Safety
///
/// This function is unsafe as there is no guarantee that the given pointer is
/// valid for `data_length` elements.
#[no_mangle]
pub unsafe extern "C" fn wgpu_server_queue_write_action(
    global: &Global,
    self_id: id::QueueId,
    byte_buf: &ByteBuf,
    data: *const u8,
    data_length: usize,
    mut error_buf: ErrorBuffer,
) {
    let action: QueueWriteAction = bincode::deserialize(byte_buf.as_slice()).unwrap();
    // It is undefined behavior to pass a null pointer to `slice::from_raw_parts`, so in the case
    // of a null pointer (which occurs if `data_length` is 0), we use a dangling pointer.
    let data = ptr::NonNull::new(data as *mut u8).unwrap_or_else(|| {
        assert!(data_length == 0);
        ptr::NonNull::dangling()
    });
    let data = slice::from_raw_parts(data.as_ptr(), data_length);
    let result = match action {
        QueueWriteAction::Buffer { dst, offset } => {
            global.queue_write_buffer(self_id, dst, offset, data)
        }
        QueueWriteAction::Texture { dst, layout, size } => {
            global.queue_write_texture(self_id, &dst, data, &layout, &size)
        }
    };
    if let Err(err) = result {
        error_buf.init(err);
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_bind_group_layout_drop(
    global: &Global,
    self_id: id::BindGroupLayoutId,
) {
    global.bind_group_layout_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_pipeline_layout_drop(global: &Global, self_id: id::PipelineLayoutId) {
    global.pipeline_layout_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_bind_group_drop(global: &Global, self_id: id::BindGroupId) {
    global.bind_group_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_shader_module_drop(global: &Global, self_id: id::ShaderModuleId) {
    global.shader_module_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_compute_pipeline_drop(
    global: &Global,
    self_id: id::ComputePipelineId,
) {
    global.compute_pipeline_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_render_pipeline_drop(global: &Global, self_id: id::RenderPipelineId) {
    global.render_pipeline_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_texture_destroy(global: &Global, self_id: id::TextureId) {
    let _ = global.texture_destroy(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_texture_drop(global: &Global, self_id: id::TextureId) {
    global.texture_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_texture_view_drop(global: &Global, self_id: id::TextureViewId) {
    global.texture_view_drop(self_id).unwrap();
}

#[no_mangle]
pub extern "C" fn wgpu_server_sampler_drop(global: &Global, self_id: id::SamplerId) {
    global.sampler_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_query_set_drop(global: &Global, self_id: id::QuerySetId) {
    global.query_set_drop(self_id);
}

#[no_mangle]
pub extern "C" fn wgpu_server_compute_pipeline_get_bind_group_layout(
    global: &Global,
    self_id: id::ComputePipelineId,
    index: u32,
    assign_id: id::BindGroupLayoutId,
    mut error_buf: ErrorBuffer,
) {
    let (_, error) = global.compute_pipeline_get_bind_group_layout(self_id, index, Some(assign_id));
    if let Some(err) = error {
        error_buf.init(err);
    }
}

#[no_mangle]
pub extern "C" fn wgpu_server_render_pipeline_get_bind_group_layout(
    global: &Global,
    self_id: id::RenderPipelineId,
    index: u32,
    assign_id: id::BindGroupLayoutId,
    mut error_buf: ErrorBuffer,
) {
    let (_, error) = global.render_pipeline_get_bind_group_layout(self_id, index, Some(assign_id));
    if let Some(err) = error {
        error_buf.init(err);
    }
}

/// Encode the freeing of the selected ID into a byte buf.
#[no_mangle]
pub extern "C" fn wgpu_server_adapter_free(id: id::AdapterId, drop_byte_buf: &mut ByteBuf) {
    *drop_byte_buf = DropAction::Adapter(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_device_free(id: id::DeviceId, drop_byte_buf: &mut ByteBuf) {
    *drop_byte_buf = DropAction::Device(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_shader_module_free(
    id: id::ShaderModuleId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::ShaderModule(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_pipeline_layout_free(
    id: id::PipelineLayoutId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::PipelineLayout(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_bind_group_layout_free(
    id: id::BindGroupLayoutId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::BindGroupLayout(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_bind_group_free(id: id::BindGroupId, drop_byte_buf: &mut ByteBuf) {
    *drop_byte_buf = DropAction::BindGroup(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_command_buffer_free(
    id: id::CommandBufferId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::CommandBuffer(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_render_bundle_free(
    id: id::RenderBundleId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::RenderBundle(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_render_pipeline_free(
    id: id::RenderPipelineId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::RenderPipeline(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_compute_pipeline_free(
    id: id::ComputePipelineId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::ComputePipeline(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_buffer_free(id: id::BufferId, drop_byte_buf: &mut ByteBuf) {
    *drop_byte_buf = DropAction::Buffer(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_texture_free(id: id::TextureId, drop_byte_buf: &mut ByteBuf) {
    *drop_byte_buf = DropAction::Texture(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_texture_view_free(
    id: id::TextureViewId,
    drop_byte_buf: &mut ByteBuf,
) {
    *drop_byte_buf = DropAction::TextureView(id).to_byte_buf();
}
#[no_mangle]
pub extern "C" fn wgpu_server_sampler_free(id: id::SamplerId, drop_byte_buf: &mut ByteBuf) {
    *drop_byte_buf = DropAction::Sampler(id).to_byte_buf();
}
