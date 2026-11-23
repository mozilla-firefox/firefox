/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::GPUError;
use crate::{
    cow_label, raw_string_to_string, wgpu_string, AdapterInformation, ByteBuf, DeviceAction,
    FfiDeviceLostReason, FfiErrorFilter, FfiPopErrorScopeResultType, FfiRequestDeviceError,
    FfiSlice, FfiTexelCopyBufferLayout, FfiTextureDescriptor, FfiTextureViewDescriptor,
    QueueWriteAction, RawString, ShaderModuleCompilationMessage, TextureAction,
};

use crate::{BufferMapResult, Message, QueueWriteDataSource, ServerMessage, SwapChainId};

use wgc::naga::front::wgsl::ImplementedLanguageExtension;
use wgpu_core_remote_types::binding_model::{
    BindGroupDescriptor, BindGroupEntry, BindGroupLayoutDescriptor, BindGroupLayoutEntry,
    BindingResource, BufferBinding, BufferBindingLayout, SamplerBindingLayout,
    StorageTextureBindingLayout, TextureBindingLayout,
};
use wgpu_core_remote_types::encoders::{
    BindingCommand, CommandBufferDescriptor, CommandEncoderCommand, ComputePassDescriptor,
    DebugCommand, PassTimestampWrites, RenderBundleDescriptor, RenderBundleEncoderCommand,
    RenderBundleEncoderDescriptor, RenderCommand, RenderPassColorAttachment,
    RenderPassDepthStencilAttachment, RenderPassDescriptor, TexelCopyBufferInfo,
    TexelCopyTextureInfo,
};
use wgpu_core_remote_types::pipelines::{
    ComputePipelineDescriptor, FragmentState, ProgrammableStageDescriptor,
    RenderPipelineDescriptor, VertexBufferLayout, VertexState,
};
use wgpu_core_remote_types::{
    ffi::FfiOption, id, identity::IdentityHub, identity::IdentityManager,
};
use wgpu_core_remote_types::{
    BufferDescriptor, DeviceDescriptor, PipelineLayoutDescriptor, QuerySetDescriptor,
    RequestAdapterOptions, SamplerDescriptor, TextureViewDescriptor,
};
use wgt::{BufferAddress, CommandEncoderDescriptor, DynamicOffset, IndexFormat};

use parking_lot::Mutex;

use nsstring::{nsACString, nsCString, nsString};

use std::array;
use std::borrow::Cow;
use std::fmt::Write;

#[repr(C)]
pub struct ConstantEntry {
    key: RawString,
    value: f64,
}

#[repr(C)]
pub struct FfiProgrammableStageDescriptor<'a> {
    module: id::ShaderModuleId,
    entry_point: RawString,
    constants: FfiSlice<'a, ConstantEntry>,
}

impl FfiProgrammableStageDescriptor<'_> {
    fn to_wgpu(&self) -> ProgrammableStageDescriptor<'_> {
        let constants = unsafe { self.constants.as_slice() }
            .iter()
            .map(|ce| (raw_string_to_string(ce.key), ce.value))
            .collect();
        ProgrammableStageDescriptor {
            module: self.module,
            entry_point: cow_label(&self.entry_point),
            constants,
        }
    }
}

#[repr(C)]
pub struct FfiComputePipelineDescriptor<'a> {
    label: Option<&'a nsACString>,
    layout: Option<id::PipelineLayoutId>,
    stage: FfiProgrammableStageDescriptor<'a>,
}

#[repr(C)]
pub struct FfiVertexBufferLayout<'a> {
    array_stride: wgt::BufferAddress,
    step_mode: wgt::VertexStepMode,
    attributes: FfiSlice<'a, wgt::VertexAttribute>,
}

#[repr(C)]
pub struct FfiVertexState<'a> {
    stage: FfiProgrammableStageDescriptor<'a>,
    buffers: FfiSlice<'a, FfiOption<FfiVertexBufferLayout<'a>>>,
}

impl FfiVertexState<'_> {
    fn to_wgpu(&self) -> VertexState<'_> {
        let buffer_layouts = unsafe { self.buffers.as_slice() }
            .iter()
            .map(|vb| {
                vb.as_ref().map(|vb| VertexBufferLayout {
                    array_stride: vb.array_stride,
                    step_mode: vb.step_mode,
                    attributes: Cow::Borrowed(unsafe { vb.attributes.as_slice() }),
                })
            })
            .collect();
        VertexState {
            stage: self.stage.to_wgpu(),
            buffers: Cow::Owned(buffer_layouts),
        }
    }
}

#[repr(C)]
pub struct ColorTargetState {
    format: wgt::TextureFormat,
    blend: FfiOption<wgt::BlendState>,
    write_mask: wgt::ColorWrites,
}

#[repr(C)]
pub struct FfiFragmentState<'a> {
    stage: FfiProgrammableStageDescriptor<'a>,
    targets: FfiSlice<'a, FfiOption<ColorTargetState>>,
}

impl FfiFragmentState<'_> {
    fn to_wgpu(&self) -> FragmentState<'_> {
        let color_targets = unsafe { self.targets.as_slice() }
            .iter()
            .map(|ct_opt| {
                ct_opt.as_ref().map(|ct| wgt::ColorTargetState {
                    format: ct.format,
                    blend: ct.blend.to_std(),
                    write_mask: ct.write_mask,
                })
            })
            .collect();
        FragmentState {
            stage: self.stage.to_wgpu(),
            targets: Cow::Owned(color_targets),
        }
    }
}

#[repr(C)]
pub struct PrimitiveState<'a> {
    topology: wgt::PrimitiveTopology,
    strip_index_format: Option<&'a wgt::IndexFormat>,
    front_face: wgt::FrontFace,
    cull_mode: Option<&'a wgt::Face>,
    polygon_mode: wgt::PolygonMode,
    unclipped_depth: bool,
}

impl PrimitiveState<'_> {
    fn to_wgpu(&self) -> wgt::PrimitiveState {
        wgt::PrimitiveState {
            topology: self.topology,
            strip_index_format: self.strip_index_format.cloned(),
            front_face: self.front_face.clone(),
            cull_mode: self.cull_mode.cloned(),
            polygon_mode: self.polygon_mode,
            unclipped_depth: self.unclipped_depth,
            conservative: false,
        }
    }
}

#[repr(C)]
pub struct DepthStencilState {
    format: wgt::TextureFormat,
    depth_write_enabled: FfiOption<bool>,
    depth_compare: FfiOption<wgt::CompareFunction>,
    stencil: wgt::StencilState,
    bias: wgt::DepthBiasState,
}

impl DepthStencilState {
    fn to_wgpu(&self) -> wgt::DepthStencilState {
        wgt::DepthStencilState {
            format: self.format,
            depth_write_enabled: self.depth_write_enabled.to_std(),
            depth_compare: self.depth_compare.to_std(),
            stencil: self.stencil.clone(),
            bias: self.bias,
        }
    }
}

#[repr(C)]
pub struct FfiRenderPipelineDescriptor<'a> {
    label: Option<&'a nsACString>,
    layout: Option<id::PipelineLayoutId>,
    vertex: &'a FfiVertexState<'a>,
    primitive: PrimitiveState<'a>,
    fragment: Option<&'a FfiFragmentState<'a>>,
    depth_stencil: Option<&'a DepthStencilState>,
    multisample: wgt::MultisampleState,
}

#[repr(C)]
pub enum RawTextureSampleType {
    Float,
    UnfilterableFloat,
    Uint,
    Sint,
    Depth,
}

#[repr(C)]
pub enum RawBindingType {
    UniformBuffer,
    StorageBuffer,
    ReadonlyStorageBuffer,
    Sampler,
    SampledTexture,
    ReadonlyStorageTexture,
    WriteonlyStorageTexture,
    ReadWriteStorageTexture,
    ExternalTexture,
    Error,
}

/// A [`BindGroupLayoutEntry::error_case`], specified when [`BindGroupLayoutEntry::ty`] is set to
/// [`RawBindingType::Error`].
#[derive(Clone, Copy)]
#[repr(C)]
pub enum BindingTypeError {
    NoneSpecified,
    MultipleSpecified,
}

/// An FFI-friendly representation of a [`wgt::BindGroupLayoutEntry`].
///
/// This is implemented using a "poor person's tagged union". Most fields are expected to be set
/// only with a specific variant of [`Self::ty`], but all are present at all times.
#[repr(C)]
pub struct FfiBindGroupLayoutEntry<'a> {
    binding: u32,
    visibility: wgt::ShaderStagesWebGPU,
    ty: RawBindingType,
    has_dynamic_offset: bool,
    min_binding_size: Option<wgt::BufferSize>,
    view_dimension: Option<&'a wgt::TextureViewDimension>,
    texture_sample_type: Option<&'a RawTextureSampleType>,
    multisampled: bool,
    storage_texture_format: Option<&'a wgt::TextureFormat>,
    sampler_filter: bool,
    sampler_compare: bool,
    /// The error case, for when [`Self::ty`] is set to [`RawBindingType::Error`].
    error_case: BindingTypeError,
}

#[repr(C)]
pub struct FfiBindGroupLayoutDescriptor<'a> {
    label: Option<&'a nsACString>,
    entries: FfiSlice<'a, FfiBindGroupLayoutEntry<'a>>,
}

#[repr(C)]
#[derive(Debug)]
pub struct FfiBindGroupEntry {
    binding: u32,
    buffer: Option<id::BufferId>,
    offset: wgt::BufferAddress,

    // In `wgpu_core::binding_model::BufferBinding`, these are an
    // `Option<BufferAddress>`. But since `BufferAddress` can be zero, that is
    // not a type that cbindgen can express in C++, so we use this pair of
    // values instead.
    size_passed: bool,
    size: wgt::BufferAddress,

    sampler: Option<id::SamplerId>,
    texture_view: Option<id::TextureViewId>,
    external_texture: Option<id::ExternalTextureId>,
}

#[repr(C)]
pub struct FfiBindGroupDescriptor<'a> {
    label: Option<&'a nsACString>,
    layout: id::BindGroupLayoutId,
    entries: FfiSlice<'a, FfiBindGroupEntry>,
}

#[repr(C)]
pub struct FfiPipelineLayoutDescriptor<'a> {
    label: Option<&'a nsACString>,
    bind_group_layouts: FfiSlice<'a, Option<id::BindGroupLayoutId>>,
}

#[repr(C)]
pub struct FfiSamplerDescriptor<'a> {
    label: Option<&'a nsACString>,
    address_modes: [wgt::AddressMode; 3],
    mag_filter: wgt::FilterMode,
    min_filter: wgt::FilterMode,
    mipmap_filter: wgt::MipmapFilterMode,
    lod_min_clamp: f32,
    lod_max_clamp: f32,
    compare: Option<&'a wgt::CompareFunction>,
    max_anisotropy: u16,
}

#[repr(C)]
pub struct FfiRenderBundleEncoderDescriptor<'a> {
    label: Option<&'a nsACString>,
    color_formats: FfiSlice<'a, FfiOption<wgt::TextureFormat>>,
    depth_stencil_format: Option<&'a wgt::TextureFormat>,
    depth_read_only: bool,
    stencil_read_only: bool,
    sample_count: u32,
}

/// Opaque pointer to `mozilla::webgpu::WebGPUChild`.
#[derive(Debug, Clone, Copy)]
#[repr(transparent)]
pub struct WebGPUChildPtr(*mut core::ffi::c_void);

#[derive(Debug)]
pub struct Client {
    owner: WebGPUChildPtr,
    message_queue: Mutex<MessageQueue>,
    identities: Mutex<IdentityHub>,
    external_texture_sources: Mutex<IdentityManager<crate::ExternalTextureSource>>,
}

impl Client {
    pub(crate) fn queue_message(&self, message: &Message) {
        let mut message_queue = self.message_queue.lock();
        message_queue.push(self.owner, message);
    }
    fn get_serialized_messages(&self) -> (u32, Vec<u8>) {
        let mut message_queue = self.message_queue.lock();
        message_queue.flush()
    }
}

#[derive(Debug)]
struct MessageQueue {
    on_message_queued: extern "C" fn(WebGPUChildPtr),

    serialized_messages: std::io::Cursor<Vec<u8>>,
    nr_of_queued_messages: u32,
}

impl MessageQueue {
    fn new(on_message_queued: extern "C" fn(WebGPUChildPtr)) -> Self {
        Self {
            on_message_queued,
            serialized_messages: std::io::Cursor::new(Vec::new()),
            nr_of_queued_messages: 0,
        }
    }

    fn push(&mut self, child: WebGPUChildPtr, message: &Message) {
        use bincode::Options;
        let options = bincode::DefaultOptions::new()
            .with_fixint_encoding()
            .allow_trailing_bytes();
        let mut serializer = bincode::Serializer::new(&mut self.serialized_messages, options);

        use serde::Serialize;
        message.serialize(&mut serializer).unwrap();

        self.nr_of_queued_messages = self.nr_of_queued_messages.checked_add(1).unwrap();
        (self.on_message_queued)(child);

        // Force send when we have queued up at least 4k messages.
        // We must comply with some static limits:
        //   - `IPC::Message::MAX_DESCRIPTORS_PER_MESSAGE` (32767): currently,
        //     no message can refer to more than one shmem handle; 4k is well below 32k.
        //   - `IPC::Channel::kMaximumMessageSize` (256 * 1024 * 1024, when fuzzing):
        //     with a limit of 4k messages, each message can be up to 64KiB; while we have
        //     some messages that can have arbitrary size (ex. `CreateShaderModule`) most
        //     have a static size.
        // If we ever violate the limits, the worst that can happen is that we trigger asserts.
        if self.nr_of_queued_messages >= 4 * 1024 {
            let (nr_of_messages, serialized_messages) = self.flush();
            let serialized_messages = ByteBuf::from_vec(serialized_messages);
            unsafe { wgpu_child_send_messages(child, nr_of_messages, serialized_messages) };
        }
    }

    fn flush(&mut self) -> (u32, Vec<u8>) {
        let nr_of_messages = self.nr_of_queued_messages;
        self.nr_of_queued_messages = 0;
        (
            nr_of_messages,
            core::mem::take(&mut self.serialized_messages).into_inner(),
        )
    }
}

#[no_mangle]
pub extern "C" fn wgpu_client_get_queued_messages(
    client: &Client,
    serialized_messages_bb: &mut ByteBuf,
) -> u32 {
    let (nr_of_messages, serialized_messages) = client.get_serialized_messages();
    *serialized_messages_bb = ByteBuf::from_vec(serialized_messages);
    nr_of_messages
}

#[no_mangle]
pub extern "C" fn wgpu_client_new(
    owner: WebGPUChildPtr,
    on_message_queued: extern "C" fn(WebGPUChildPtr),
) -> *mut Client {
    log::info!("Initializing WGPU client");
    let client = Client {
        owner,
        message_queue: Mutex::new(MessageQueue::new(on_message_queued)),
        identities: Mutex::new(IdentityHub::default()),
        external_texture_sources: Mutex::new(IdentityManager::new()),
    };
    Box::into_raw(Box::new(client))
}

/// # Safety
///
/// This function is unsafe because improper use may lead to memory
/// problems. For example, a double-free may occur if the function is called
/// twice on the same raw pointer.
#[no_mangle]
pub unsafe extern "C" fn wgpu_client_delete(client: *mut Client) {
    log::info!("Terminating WGPU client");
    let _client = unsafe { Box::from_raw(client) };
}

#[no_mangle]
pub extern "C" fn wgpu_client_fill_default_limits(limits: &mut wgt::Limits) {
    *limits = wgt::Limits::default();
}

/// Writes the single `WGSLLanguageFeature` associated with `index`, appending its identifier to the
/// provided `buffer`. If `index` does not correspond to a valid feature index, then do nothing.
///
/// This function enables an FFI consumer to extract all implemented features in a loop, like so:
///
/// ```rust
/// let mut buffer = nsstring::nsCString::new();
/// for index in 0usize.. {
///     buffer.truncate();
///     wgpu_client_instance_get_wgsl_language_feature(&mut buffer, index);
///     if buffer.is_empty() {
///         break;
///     }
///     // Handle the identifier in `buffer`…
/// }
/// ```
#[no_mangle]
pub extern "C" fn wgpu_client_instance_get_wgsl_language_feature(
    buffer: &mut nsstring::nsCString,
    index: usize,
) {
    // TODO(Bug 2005059): Exclude `ImmediateAddressSpace` until we expose the
    // rest of the immediates API.
    let extensions = ImplementedLanguageExtension::all()
        .iter()
        .filter(|ext| !matches!(ext, ImplementedLanguageExtension::ImmediateAddressSpace))
        .collect::<Vec<_>>();
    match extensions.get(index) {
        Some(some) => buffer.write_str(some.to_ident()).unwrap(),
        None => (),
    }
}

#[repr(C)]
pub struct FfiDeviceDescriptor<'a> {
    pub label: Option<&'a nsACString>,
    pub required_features: wgt::FeaturesWebGPU,
    pub required_limits: wgt::Limits,
}

#[repr(C)]
pub struct DeviceQueueId {
    device: id::DeviceId,
    queue: id::QueueId,
}

#[no_mangle]
pub extern "C" fn wgpu_client_request_device(
    client: &Client,
    adapter_id: id::AdapterId,
    desc: &FfiDeviceDescriptor,
) -> DeviceQueueId {
    let identities = client.identities.lock();
    let device_id = identities.devices.process();
    let queue_id = identities.queues.process();
    drop(identities);

    let label = wgpu_string(desc.label);
    let desc = DeviceDescriptor {
        label,
        required_features: desc.required_features,
        required_limits: desc.required_limits.clone(),
        // The content process is untrusted, so values set here in fields of the device descriptor
        // not intended to be set by content are ignored, and are overridden in
        // `server::request_device`.
        ..DeviceDescriptor::default()
    };
    let message = Message::RequestDevice {
        adapter_id,
        device_id,
        queue_id,
        desc,
    };
    client.queue_message(&message);
    DeviceQueueId {
        device: device_id,
        queue: queue_id,
    }
}

#[rustfmt::skip]
mod drop {
    use super::*;

    #[no_mangle] pub extern "C" fn wgpu_client_destroy_buffer(client: &Client, id: id::BufferId) { client.queue_message(&Message::DestroyBuffer(id)); }
    #[no_mangle] pub extern "C" fn wgpu_client_destroy_texture(client: &Client, id: id::TextureId) { client.queue_message(&Message::DestroyTexture(id)); }
    #[no_mangle] pub extern "C" fn wgpu_client_destroy_external_texture(client: &Client, id: id::ExternalTextureId) { client.queue_message(&Message::DestroyExternalTexture(id)); }
    #[no_mangle] pub extern "C" fn wgpu_client_destroy_external_texture_source(client: &Client, id: crate::ExternalTextureSourceId) { client.queue_message(&&Message::DestroyExternalTextureSource(id)); }
    #[no_mangle] pub extern "C" fn wgpu_client_destroy_device(client: &Client, id: id::DeviceId) { client.queue_message(&Message::DestroyDevice(id)); }

    #[no_mangle] pub extern "C" fn wgpu_client_drop_adapter(client: &Client, id: id::AdapterId) { client.queue_message(&Message::DropAdapter(id)); client.identities.lock().adapters.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_device(client: &Client, id: id::DeviceId) { client.queue_message(&Message::DropDevice(id)); client.identities.lock().devices.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_queue(client: &Client, id: id::QueueId) { client.queue_message(&Message::DropQueue(id)); client.identities.lock().queues.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_buffer(client: &Client, id: id::BufferId) { client.queue_message(&Message::DropBuffer(id)); client.identities.lock().buffers.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_command_encoder(client: &Client, id: id::CommandEncoderId) { client.queue_message(&Message::DropCommandEncoder(id)); client.identities.lock().command_encoders.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_render_pass_encoder(client: &Client, id: id::RenderPassEncoderId) { client.queue_message(&Message::DropRenderPassEncoder(id)); client.identities.lock().render_passes.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_compute_pass_encoder(client: &Client, id: id::ComputePassEncoderId) { client.queue_message(&Message::DropComputePassEncoder(id)); client.identities.lock().compute_passes.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_render_bundle_encoder(client: &Client, id: id::RenderBundleEncoderId) { client.queue_message(&Message::DropRenderBundleEncoder(id)); client.identities.lock().render_bundle_encoders.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_command_buffer(client: &Client, id: id::CommandBufferId) { client.queue_message(&Message::DropCommandBuffer(id)); client.identities.lock().command_buffers.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_render_bundle(client: &Client, id: id::RenderBundleId) { client.queue_message(&Message::DropRenderBundle(id)); client.identities.lock().render_bundles.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_bind_group_layout(client: &Client, id: id::BindGroupLayoutId) { client.queue_message(&Message::DropBindGroupLayout(id)); client.identities.lock().bind_group_layouts.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_pipeline_layout(client: &Client, id: id::PipelineLayoutId) { client.queue_message(&Message::DropPipelineLayout(id)); client.identities.lock().pipeline_layouts.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_bind_group(client: &Client, id: id::BindGroupId) { client.queue_message(&Message::DropBindGroup(id)); client.identities.lock().bind_groups.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_shader_module(client: &Client, id: id::ShaderModuleId) { client.queue_message(&Message::DropShaderModule(id)); client.identities.lock().shader_modules.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_compute_pipeline(client: &Client, id: id::ComputePipelineId) { client.queue_message(&Message::DropComputePipeline(id)); client.identities.lock().compute_pipelines.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_render_pipeline(client: &Client, id: id::RenderPipelineId) { client.queue_message(&Message::DropRenderPipeline(id)); client.identities.lock().render_pipelines.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_texture(client: &Client, id: id::TextureId) { client.queue_message(&Message::DropTexture(id)); client.identities.lock().textures.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_texture_view(client: &Client, id: id::TextureViewId) { client.queue_message(&Message::DropTextureView(id)); client.identities.lock().texture_views.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_external_texture(client: &Client, id: id::ExternalTextureId) { client.queue_message(&Message::DropExternalTexture(id)); client.identities.lock().external_textures.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_external_texture_source(client: &Client, id: crate::ExternalTextureSourceId) { client.queue_message(&Message::DropExternalTextureSource(id)); client.external_texture_sources.lock().free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_sampler(client: &Client, id: id::SamplerId) { client.queue_message(&Message::DropSampler(id)); client.identities.lock().samplers.free(id); }
    #[no_mangle] pub extern "C" fn wgpu_client_drop_query_set(client: &Client, id: id::QuerySetId) { client.queue_message(&Message::DropQuerySet(id)); client.identities.lock().query_sets.free(id); }
}

#[repr(C)]
pub struct FfiShaderModuleCompilationMessage {
    pub line_number: u64,
    pub line_pos: u64,
    pub utf16_offset: u64,
    pub utf16_length: u64,
    pub message: nsString,
    pub message_type: wgt::CompilationMessageType,
}

extern "C" {
    fn wgpu_child_send_messages(
        child: WebGPUChildPtr,
        nr_of_messages: u32,
        serialized_messages: ByteBuf,
    );
    fn wgpu_child_resolve_request_adapter_promise(
        child: WebGPUChildPtr,
        adapter_id: id::AdapterId,
        adapter_info: Option<&AdapterInformation<nsString>>,
    );
    fn wgpu_child_resolve_request_device_promise(
        child: WebGPUChildPtr,
        device_id: id::DeviceId,
        queue_id: id::QueueId,
        limits: Option<&wgt::Limits>,
        error: Option<&FfiRequestDeviceError>,
    );
    fn wgpu_child_resolve_pop_error_scope_promise(
        child: WebGPUChildPtr,
        device_id: id::DeviceId,
        ty: FfiPopErrorScopeResultType,
        message: &nsCString,
    );
    fn wgpu_child_resolve_create_pipeline_promise(
        child: WebGPUChildPtr,
        pipeline_id: id::RawId,
        is_render_pipeline: bool,
        is_validation_error: bool,
        error: Option<&nsCString>,
    );
    fn wgpu_child_resolve_create_shader_module_promise(
        child: WebGPUChildPtr,
        shader_module_id: id::ShaderModuleId,
        messages: FfiSlice<FfiShaderModuleCompilationMessage>,
    );
    fn wgpu_child_resolve_buffer_map_promise(
        child: WebGPUChildPtr,
        buffer_id: id::BufferId,
        is_writable: bool,
        offset: u64,
        size: u64,
        error: Option<&nsCString>,
    );
    fn wgpu_child_resolve_on_submitted_work_done_promise(
        child: WebGPUChildPtr,
        queue_id: id::QueueId,
    );
    fn wgpu_child_handle_uncaptured_error(
        child: WebGPUChildPtr,
        device_id: id::DeviceId,
        ty: FfiErrorFilter,
        message: &nsCString,
    );
    fn wgpu_child_handle_device_lost(
        child: WebGPUChildPtr,
        device_id: id::DeviceId,
        reason: FfiDeviceLostReason,
        message: &nsCString,
    );
}

#[no_mangle]
pub extern "C" fn wgpu_client_receive_server_message(client: &Client, byte_buf: &ByteBuf) {
    let message: ServerMessage = bincode::deserialize(unsafe { byte_buf.as_slice() }).unwrap();
    match message {
        ServerMessage::RequestAdapterResponse(adapter_id, adapter_information) => {
            if let Some(AdapterInformation {
                backend,
                device_type,
                device,
                driver_info,
                driver,
                features,
                id,
                limits,
                name,
                vendor,
                support_use_shared_texture_in_swap_chain,
                subgroup_min_size,
                subgroup_max_size,
            }) = adapter_information
            {
                let nss = |s: &str| {
                    let mut ns_string = nsString::new();
                    ns_string.assign_str(s);
                    ns_string
                };
                let adapter_info = AdapterInformation {
                    backend,
                    device_type,
                    device,
                    driver_info: nss(&driver_info),
                    driver: nss(&driver),
                    features,
                    id,
                    limits,
                    name: nss(&name),
                    vendor,
                    support_use_shared_texture_in_swap_chain,
                    subgroup_min_size,
                    subgroup_max_size,
                };
                unsafe {
                    wgpu_child_resolve_request_adapter_promise(
                        client.owner,
                        adapter_id,
                        Some(&adapter_info),
                    );
                }
            } else {
                unsafe {
                    wgpu_child_resolve_request_adapter_promise(client.owner, adapter_id, None);
                }
                client.identities.lock().adapters.free(adapter_id)
            }
        }
        ServerMessage::RequestDeviceResponse(device_id, queue_id, res) => match res {
            Err(error) => {
                let message = nsCString::from(error.message);
                let error = FfiRequestDeviceError {
                    ty: error.ty,
                    message: &message,
                };
                unsafe {
                    wgpu_child_resolve_request_device_promise(
                        client.owner,
                        device_id,
                        queue_id,
                        None,
                        Some(&error),
                    );
                }
                let identities = client.identities.lock();
                identities.devices.free(device_id);
                identities.queues.free(queue_id);
            }
            Ok(limits) => unsafe {
                wgpu_child_resolve_request_device_promise(
                    client.owner,
                    device_id,
                    queue_id,
                    Some(&limits),
                    None,
                );
            },
        },
        ServerMessage::PopErrorScopeResponse(device_id, ty, message) => {
            let message = nsCString::from(message.as_ref());
            unsafe {
                wgpu_child_resolve_pop_error_scope_promise(client.owner, device_id, ty, &message);
            }
        }
        ServerMessage::CreateRenderPipelineResponse { pipeline_id, error } => {
            let is_render_pipeline = true;
            if let Some(error) = error {
                let ns_error = nsCString::from(error.error);
                unsafe {
                    wgpu_child_resolve_create_pipeline_promise(
                        client.owner,
                        pipeline_id.into_raw(),
                        is_render_pipeline,
                        error.is_validation_error,
                        Some(&ns_error),
                    );
                }
                client.identities.lock().render_pipelines.free(pipeline_id);
            } else {
                unsafe {
                    wgpu_child_resolve_create_pipeline_promise(
                        client.owner,
                        pipeline_id.into_raw(),
                        is_render_pipeline,
                        false,
                        None,
                    );
                }
            }
        }
        ServerMessage::CreateComputePipelineResponse { pipeline_id, error } => {
            let is_render_pipeline = false;
            if let Some(error) = error {
                let ns_error = nsCString::from(error.error);
                unsafe {
                    wgpu_child_resolve_create_pipeline_promise(
                        client.owner,
                        pipeline_id.into_raw(),
                        is_render_pipeline,
                        error.is_validation_error,
                        Some(&ns_error),
                    );
                }
                client.identities.lock().compute_pipelines.free(pipeline_id);
            } else {
                unsafe {
                    wgpu_child_resolve_create_pipeline_promise(
                        client.owner,
                        pipeline_id.into_raw(),
                        is_render_pipeline,
                        false,
                        None,
                    );
                }
            }
        }
        ServerMessage::CreateShaderModuleResponse(shader_module_id, compilation_messages) => {
            let ffi_compilation_messages: Vec<_> = compilation_messages
                .iter()
                .map(|m| {
                    let ShaderModuleCompilationMessage {
                        line_number,
                        line_pos,
                        utf16_offset,
                        utf16_length,
                        message,
                        message_type,
                    } = m;

                    let message = nsString::from(message);

                    FfiShaderModuleCompilationMessage {
                        line_number: *line_number,
                        line_pos: *line_pos,
                        utf16_offset: *utf16_offset,
                        utf16_length: *utf16_length,
                        message,
                        message_type: *message_type,
                    }
                })
                .collect();

            unsafe {
                wgpu_child_resolve_create_shader_module_promise(
                    client.owner,
                    shader_module_id,
                    FfiSlice::from_slice(&ffi_compilation_messages),
                )
            }
        }
        ServerMessage::BufferMapResponse(buffer_id, buffer_map_result) => {
            match buffer_map_result {
                BufferMapResult::Success {
                    is_writable,
                    offset,
                    size,
                } => unsafe {
                    wgpu_child_resolve_buffer_map_promise(
                        client.owner,
                        buffer_id,
                        is_writable,
                        offset,
                        size,
                        None,
                    );
                },
                BufferMapResult::Error(error) => {
                    let ns_error = nsCString::from(error.as_ref());
                    unsafe {
                        wgpu_child_resolve_buffer_map_promise(
                            client.owner,
                            buffer_id,
                            false,
                            0,
                            0,
                            Some(&ns_error),
                        );
                    }
                }
            };
        }
        ServerMessage::QueueOnSubmittedWorkDoneResponse(queue_id) => unsafe {
            wgpu_child_resolve_on_submitted_work_done_promise(client.owner, queue_id);
        },
        ServerMessage::UncapturedError(device_id, ty, message) => {
            let message = nsCString::from(message.as_ref());
            unsafe {
                wgpu_child_handle_uncaptured_error(client.owner, device_id, ty, &message);
            }
        }
        ServerMessage::DeviceLost(device_id, reason, message) => {
            let message = nsCString::from(message.as_ref());
            unsafe {
                wgpu_child_handle_device_lost(client.owner, device_id, reason, &message);
            }
        }

        ServerMessage::FreeSwapChainBufferIds(buffer_ids) => {
            let identities = client.identities.lock();
            for id in buffer_ids {
                identities.buffers.free(id);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn wgpu_client_request_adapter(
    client: &Client,
    power_preference: wgt::PowerPreference,
    force_fallback_adapter: bool,
) -> id::AdapterId {
    let adapter_id = client.identities.lock().adapters.process();
    let desc = RequestAdapterOptions {
        power_preference,
        force_fallback_adapter,
    };
    let message = Message::RequestAdapter { adapter_id, desc };
    client.queue_message(&message);
    adapter_id
}

#[no_mangle]
pub extern "C" fn wgpu_client_pop_error_scope(client: &Client, device_id: id::DeviceId) {
    let message = Message::Device(device_id, DeviceAction::PopErrorScope);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_shader_module(
    client: &Client,
    device_id: id::DeviceId,
    label: Option<&nsACString>,
    code: &nsACString,
) -> id::ShaderModuleId {
    let shader_module_id = client.identities.lock().shader_modules.process();
    let label = wgpu_string(label);
    let action =
        DeviceAction::CreateShaderModule(shader_module_id, label, Cow::Owned(code.to_string()));
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    shader_module_id
}

#[no_mangle]
pub extern "C" fn wgpu_client_on_submitted_work_done(
    client: &Client,
    device_id: id::DeviceId,
    queue_id: id::QueueId,
) {
    let message = Message::QueueOnSubmittedWorkDone {
        device_id,
        queue_id,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_swap_chain(
    client: &Client,
    device_id: id::DeviceId,
    queue_id: id::QueueId,
    desc: &FfiTextureDescriptor,
    format: crate::SurfaceFormat,
    remote_texture_owner_id: crate::RemoteTextureOwnerId,
    use_shared_texture_in_swap_chain: bool,
) {
    let identities = client.identities.lock();
    let buffer_ids: [id::BufferId; crate::MAX_SWAPCHAIN_BUFFER_COUNT] =
        array::from_fn(|_| identities.buffers.process());
    drop(identities);

    let message = Message::CreateSwapChain {
        device_id,
        queue_id,
        format,
        desc: desc.to_wgpu(),
        buffer_ids,
        remote_texture_owner_id,
        use_shared_texture_in_swap_chain,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_swap_chain_present(
    client: &Client,
    texture_id: id::TextureId,
    command_encoder_id: id::CommandEncoderId,
    command_buffer_id: id::CommandBufferId,
    remote_texture_id: crate::RemoteTextureId,
    remote_texture_owner_id: crate::RemoteTextureOwnerId,
) {
    let message = Message::SwapChainPresent {
        texture_id,
        command_encoder_id,
        command_buffer_id,
        remote_texture_id,
        remote_texture_owner_id,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_swap_chain_drop(
    client: &Client,
    remote_texture_owner_id: crate::RemoteTextureOwnerId,
    txn_type: crate::RemoteTextureTxnType,
    txn_id: crate::RemoteTextureTxnId,
) {
    let message = Message::SwapChainDrop {
        remote_texture_owner_id,
        txn_type,
        txn_id,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_queue_submit(
    client: &Client,
    queue_id: id::QueueId,
    command_buffers: FfiSlice<'_, id::CommandBufferId>,
    swap_chain_textures: FfiSlice<'_, id::TextureId>,
    external_texture_sources: FfiSlice<'_, crate::ExternalTextureSourceId>,
) {
    let message = Message::QueueSubmit(
        queue_id,
        Cow::Borrowed(unsafe { command_buffers.as_slice() }),
        Cow::Borrowed(unsafe { swap_chain_textures.as_slice() }),
        Cow::Borrowed(unsafe { external_texture_sources.as_slice() }),
    );
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_buffer_map(
    client: &Client,
    device_id: id::DeviceId,
    buffer_id: id::BufferId,
    mode: u32,
    offset: u64,
    size: u64,
) {
    let message = Message::BufferMap {
        device_id,
        buffer_id,
        mode,
        offset,
        size,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_buffer_unmap(
    client: &Client,
    device_id: id::DeviceId,
    buffer_id: id::BufferId,
    flush: bool,
) {
    let message = Message::BufferUnmap(device_id, buffer_id, flush);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_push_error_scope(
    client: &Client,
    device_id: id::DeviceId,
    filter: FfiErrorFilter,
) {
    let action = DeviceAction::PushErrorScope(filter);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
}

#[repr(C)]
pub struct FfiBufferDescriptor<'a> {
    label: Option<&'a nsACString>,
    size: u64,
    usage: wgt::BufferUsagesWebGPU,
    mapped_at_creation: bool,
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_buffer(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiBufferDescriptor,
    shmem_handle_index: usize,
) -> id::BufferId {
    let buffer_id = client.identities.lock().buffers.process();
    let label = wgpu_string(desc.label);
    let desc = BufferDescriptor {
        label,
        size: desc.size,
        usage: desc.usage,
        mapped_at_creation: desc.mapped_at_creation,
    };
    let action = DeviceAction::CreateBuffer {
        buffer_id,
        desc,
        shmem_handle_index,
    };
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    buffer_id
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_texture(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiTextureDescriptor,
    swap_chain_id: Option<&SwapChainId>,
) -> id::TextureId {
    let id = client.identities.lock().textures.process();

    let action = DeviceAction::CreateTexture(id, desc.to_wgpu(), swap_chain_id.copied());
    let message = Message::Device(device_id, action);
    client.queue_message(&message);

    id
}

#[no_mangle]
pub extern "C" fn wgpu_client_make_texture_id(client: &Client) -> id::TextureId {
    client.identities.lock().textures.process()
}

#[no_mangle]
pub extern "C" fn wgpu_client_free_texture_id(client: &Client, id: id::TextureId) {
    client.identities.lock().textures.free(id)
}

impl FfiTextureViewDescriptor<'_> {
    fn to_wgpu(&'_ self) -> TextureViewDescriptor<'_> {
        let s = self.swizzle.map(|c| {
            match c {
                b'0' => wgt::ComponentSwizzle::Zero,
                b'1' => wgt::ComponentSwizzle::One,
                b'r' => wgt::ComponentSwizzle::R,
                b'g' => wgt::ComponentSwizzle::G,
                b'b' => wgt::ComponentSwizzle::B,
                b'a' => wgt::ComponentSwizzle::A,
                // `FfiTextureViewDescriptor` must be valid on construction.
                _ => unreachable!(),
            }
        });
        let swizzle = wgt::TextureComponentSwizzle {
            r: s[0],
            g: s[1],
            b: s[2],
            a: s[3],
        };

        TextureViewDescriptor {
            label: wgpu_string(self.label),
            format: self.format.cloned(),
            dimension: self.dimension.cloned(),
            range: wgt::ImageSubresourceRange {
                aspect: self.aspect,
                base_mip_level: self.base_mip_level,
                mip_level_count: self.mip_level_count.map(|ptr| *ptr),
                base_array_layer: self.base_array_layer,
                array_layer_count: self.array_layer_count.map(|ptr| *ptr),
            },
            usage: Some(self.usage),
            swizzle,
        }
    }
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_texture_view(
    client: &Client,
    texture_id: id::TextureId,
    desc: &crate::FfiTextureViewDescriptor,
) -> id::TextureViewId {
    let id = client.identities.lock().texture_views.process();

    let action = TextureAction::CreateView(id, desc.to_wgpu());
    let message = Message::Texture(texture_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub extern "C" fn wgpu_client_make_texture_view_id(client: &Client) -> id::TextureViewId {
    client.identities.lock().texture_views.process()
}

#[no_mangle]
pub extern "C" fn wgpu_client_free_texture_view_id(client: &Client, id: id::TextureViewId) {
    client.identities.lock().texture_views.free(id)
}

#[no_mangle]
pub extern "C" fn wgpu_client_make_external_texture_source_id(
    client: &Client,
) -> crate::ExternalTextureSourceId {
    client.external_texture_sources.lock().process()
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_external_texture(
    client: &Client,
    device_id: id::DeviceId,
    desc: &crate::ExternalTextureDescriptor<Option<&nsACString>>,
) -> id::ExternalTextureId {
    let desc = desc.map_label(|l| wgpu_string(*l));
    let id = client.identities.lock().external_textures.process();

    let action = DeviceAction::CreateExternalTexture(id, desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_sampler(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiSamplerDescriptor,
) -> id::SamplerId {
    let label = wgpu_string(desc.label);

    let id = client.identities.lock().samplers.process();

    let wgpu_desc = SamplerDescriptor {
        label,
        address_modes: desc.address_modes,
        mag_filter: desc.mag_filter,
        min_filter: desc.min_filter,
        mipmap_filter: desc.mipmap_filter,
        lod_min_clamp: desc.lod_min_clamp,
        lod_max_clamp: desc.lod_max_clamp,
        compare: desc.compare.cloned(),
        anisotropy_clamp: desc.max_anisotropy,
    };
    let action = DeviceAction::CreateSampler(id, wgpu_desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub extern "C" fn wgpu_client_make_command_encoder_id(client: &Client) -> id::CommandEncoderId {
    client.identities.lock().command_encoders.process()
}

#[no_mangle]
pub extern "C" fn wgpu_client_free_command_encoder_id(client: &Client, id: id::CommandEncoderId) {
    client.identities.lock().command_encoders.free(id)
}

#[no_mangle]
pub extern "C" fn wgpu_client_make_command_buffer_id(client: &Client) -> id::CommandBufferId {
    client.identities.lock().command_buffers.process()
}

#[no_mangle]
pub extern "C" fn wgpu_client_free_command_buffer_id(client: &Client, id: id::CommandBufferId) {
    client.identities.lock().command_buffers.free(id)
}

#[repr(C)]
pub struct FfiCommandEncoderDescriptor<'a> {
    label: Option<&'a nsACString>,
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_command_encoder(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiCommandEncoderDescriptor,
) -> id::CommandEncoderId {
    let label = wgpu_string(desc.label);

    let id = client.identities.lock().command_encoders.process();

    let desc = CommandEncoderDescriptor { label };

    let action = DeviceAction::CreateCommandEncoder(id, desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_render_bundle_encoder(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiRenderBundleEncoderDescriptor,
) -> id::RenderBundleEncoderId {
    let label = wgpu_string(desc.label);

    let id = client.identities.lock().render_bundle_encoders.process();

    let color_formats: Vec<_> = unsafe { desc.color_formats.as_slice() }
        .iter()
        .map(|format_opt| format_opt.to_std())
        .collect();
    let desc = RenderBundleEncoderDescriptor {
        label,
        color_formats: Cow::Owned(color_formats),
        depth_stencil: desc
            .depth_stencil_format
            .map(|&format| wgt::RenderBundleDepthStencil {
                format,
                depth_read_only: desc.depth_read_only,
                stencil_read_only: desc.stencil_read_only,
            }),
        sample_count: desc.sample_count,
    };

    let action = DeviceAction::CreateRenderBundleEncoder(id, desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_render_bundle_encoder_set_bind_group(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    index: u32,
    bind_group: Option<id::BindGroupId>,
    dynamic_offsets: FfiSlice<'_, DynamicOffset>,
) {
    let command = RenderBundleEncoderCommand::BindingCommand(BindingCommand::SetBindGroup {
        index,
        bind_group,
        dynamic_offsets: unsafe { dynamic_offsets.as_slice() }.to_vec(),
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_set_pipeline(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    pipeline_id: id::RenderPipelineId,
) {
    let command =
        RenderBundleEncoderCommand::RenderCommand(RenderCommand::SetPipeline(pipeline_id));
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_set_index_buffer(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    buffer: id::BufferId,
    index_format: IndexFormat,
    offset: BufferAddress,
    size: FfiOption<BufferAddress>,
) {
    let command = RenderBundleEncoderCommand::RenderCommand(RenderCommand::SetIndexBuffer {
        buffer,
        index_format,
        offset,
        size: size.to_std(),
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_set_vertex_buffer(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    slot: u32,
    buffer: Option<id::BufferId>,
    offset: BufferAddress,
    size: FfiOption<BufferAddress>,
) {
    let command = RenderBundleEncoderCommand::RenderCommand(RenderCommand::SetVertexBuffer {
        slot,
        buffer,
        offset,
        size: size.to_std(),
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_draw(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    vertex_count: u32,
    instance_count: u32,
    first_vertex: u32,
    first_instance: u32,
) {
    let command = RenderBundleEncoderCommand::RenderCommand(RenderCommand::Draw {
        vertex_count,
        instance_count,
        first_vertex,
        first_instance,
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_draw_indexed(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    index_count: u32,
    instance_count: u32,
    first_index: u32,
    base_vertex: i32,
    first_instance: u32,
) {
    let command = RenderBundleEncoderCommand::RenderCommand(RenderCommand::DrawIndexed {
        index_count,
        instance_count,
        first_index,
        base_vertex,
        first_instance,
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_draw_indirect(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    indirect_buffer: id::BufferId,
    indirect_offset: BufferAddress,
) {
    let command = RenderBundleEncoderCommand::RenderCommand(RenderCommand::DrawIndirect {
        indirect_buffer,
        indirect_offset,
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_draw_indexed_indirect(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    indirect_buffer: id::BufferId,
    indirect_offset: BufferAddress,
) {
    let command = RenderBundleEncoderCommand::RenderCommand(RenderCommand::DrawIndexedIndirect {
        indirect_buffer,
        indirect_offset,
    });
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_render_bundle_encoder_push_debug_group(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    label: RawString,
) {
    let command = RenderBundleEncoderCommand::DebugCommand(DebugCommand::PushDebugGroup(
        raw_string_to_string(label),
    ));
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_render_bundle_encoder_pop_debug_group(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
) {
    let command = RenderBundleEncoderCommand::DebugCommand(DebugCommand::PopDebugGroup);
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_render_bundle_encoder_insert_debug_marker(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    label: RawString,
) {
    let command = RenderBundleEncoderCommand::DebugCommand(DebugCommand::InsertDebugMarker(
        raw_string_to_string(label),
    ));
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
}

#[repr(C)]
pub struct FfiRenderBundleDescriptor<'a> {
    label: Option<&'a nsACString>,
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_render_bundle_encoder_finish(
    client: &Client,
    encoder_id: id::RenderBundleEncoderId,
    desc: &FfiRenderBundleDescriptor,
) -> id::RenderBundleId {
    let label = wgpu_string(desc.label);

    let render_bundle_id = client.identities.lock().render_bundles.process();

    let desc = RenderBundleDescriptor { label };
    let command = RenderBundleEncoderCommand::Finish {
        desc: desc,
        render_bundle_id,
    };
    let message = Message::RenderBundleEncoder(encoder_id, command);
    client.queue_message(&message);
    render_bundle_id
}

#[repr(C)]
pub struct RawQuerySetDescriptor<'a> {
    label: Option<&'a nsACString>,
    ty: RawQueryType,
    count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum RawQueryType {
    Occlusion,
    Timestamp,
}

#[no_mangle]
pub extern "C" fn wgpu_client_create_query_set(
    client: &Client,
    device_id: id::DeviceId,
    desc: &RawQuerySetDescriptor,
) -> id::QuerySetId {
    let &RawQuerySetDescriptor { label, ty, count } = desc;

    let label = wgpu_string(label);
    let ty = match ty {
        RawQueryType::Occlusion => wgt::QueryType::Occlusion,
        RawQueryType::Timestamp => wgt::QueryType::Timestamp,
    };

    let desc = QuerySetDescriptor { label, ty, count };

    let id = client.identities.lock().query_sets.process();

    let action = DeviceAction::CreateQuerySet(id, desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);

    id
}

#[repr(C)]
pub struct FfiComputePassDescriptor<'a> {
    pub label: Option<&'a nsACString>,
    pub timestamp_writes: Option<&'a PassTimestampWrites>,
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_begin_compute_pass(
    client: &Client,
    encoder_id: id::CommandEncoderId,
    desc: &FfiComputePassDescriptor,
) -> id::ComputePassEncoderId {
    let &FfiComputePassDescriptor {
        label,
        timestamp_writes,
    } = desc;

    let label = wgpu_string(label);

    let desc = ComputePassDescriptor {
        label,
        timestamp_writes: timestamp_writes.cloned(),
    };

    let compute_pass_encoder_id = client.identities.lock().compute_passes.process();

    let command = CommandEncoderCommand::BeginComputePass {
        desc,
        compute_pass_encoder_id,
    };
    let message = Message::CommandEncoder(encoder_id, command);
    client.queue_message(&message);
    compute_pass_encoder_id
}

#[repr(C)]
pub struct FfiRenderPassDescriptor<'a> {
    pub label: Option<&'a nsACString>,
    pub color_attachments: FfiSlice<'a, FfiOption<RenderPassColorAttachment>>,
    pub depth_stencil_attachment: Option<&'a RenderPassDepthStencilAttachment>,
    pub timestamp_writes: Option<&'a PassTimestampWrites>,
    pub occlusion_query_set: Option<id::QuerySetId>,
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_begin_render_pass(
    client: &Client,
    encoder_id: id::CommandEncoderId,
    desc: &FfiRenderPassDescriptor,
) -> id::RenderPassEncoderId {
    let &FfiRenderPassDescriptor {
        label,
        color_attachments,
        depth_stencil_attachment,
        timestamp_writes,
        occlusion_query_set,
    } = desc;

    let label = wgpu_string(label);

    let color_attachments: Vec<_> = unsafe { color_attachments.as_slice() }
        .iter()
        .map(|att_opt| att_opt.as_ref().map(|att| att.clone()))
        .collect();
    let depth_stencil_attachment = depth_stencil_attachment.cloned();

    let desc = RenderPassDescriptor {
        label,
        color_attachments: Cow::Owned(color_attachments),
        depth_stencil_attachment,
        timestamp_writes: timestamp_writes.cloned(),
        occlusion_query_set,
    };

    let render_pass_encoder_id = client.identities.lock().render_passes.process();

    let command = CommandEncoderCommand::BeginRenderPass {
        desc,
        render_pass_encoder_id,
    };
    let message = Message::CommandEncoder(encoder_id, command);
    client.queue_message(&message);
    render_pass_encoder_id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_create_bind_group_layout(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiBindGroupLayoutDescriptor,
) -> id::BindGroupLayoutId {
    let label = wgpu_string(desc.label);

    let id = client.identities.lock().bind_group_layouts.process();

    let entries = unsafe { desc.entries.as_slice() }
        .iter()
        .enumerate()
        .map(|(idx, entry)| {
            let buffer = |ty| {
                FfiOption::Some(BufferBindingLayout {
                    ty,
                    has_dynamic_offset: entry.has_dynamic_offset,
                    min_binding_size: entry.min_binding_size,
                })
            };
            let storage_texture = |access| {
                FfiOption::Some(StorageTextureBindingLayout {
                    access,
                    view_dimension: *entry.view_dimension.unwrap(),
                    format: *entry.storage_texture_format.unwrap(),
                })
            };

            let mut out = BindGroupLayoutEntry {
                binding: entry.binding,
                visibility: entry.visibility,
                buffer: FfiOption::None,
                sampler: FfiOption::None,
                texture: FfiOption::None,
                storage_texture: FfiOption::None,
                external_texture: false,
            };
            match entry.ty {
                RawBindingType::UniformBuffer => {
                    out.buffer = buffer(wgt::BufferBindingType::Uniform)
                }
                RawBindingType::StorageBuffer => {
                    out.buffer = buffer(wgt::BufferBindingType::Storage { read_only: false })
                }
                RawBindingType::ReadonlyStorageBuffer => {
                    out.buffer = buffer(wgt::BufferBindingType::Storage { read_only: true })
                }
                RawBindingType::Sampler => {
                    out.sampler = FfiOption::Some(SamplerBindingLayout {
                        ty: if entry.sampler_compare {
                            wgt::SamplerBindingType::Comparison
                        } else if entry.sampler_filter {
                            wgt::SamplerBindingType::Filtering
                        } else {
                            wgt::SamplerBindingType::NonFiltering
                        },
                    })
                }
                RawBindingType::SampledTexture => {
                    out.texture = FfiOption::Some(TextureBindingLayout {
                        //TODO: the spec has a bug here
                        view_dimension: *entry
                            .view_dimension
                            .unwrap_or(&wgt::TextureViewDimension::D2),
                        sample_type: match entry.texture_sample_type {
                            None | Some(RawTextureSampleType::Float) => {
                                wgt::TextureSampleType::Float { filterable: true }
                            }
                            Some(RawTextureSampleType::UnfilterableFloat) => {
                                wgt::TextureSampleType::Float { filterable: false }
                            }
                            Some(RawTextureSampleType::Uint) => wgt::TextureSampleType::Uint,
                            Some(RawTextureSampleType::Sint) => wgt::TextureSampleType::Sint,
                            Some(RawTextureSampleType::Depth) => wgt::TextureSampleType::Depth,
                        },
                        multisampled: entry.multisampled,
                    })
                }
                RawBindingType::ReadonlyStorageTexture => {
                    out.storage_texture = storage_texture(wgt::StorageTextureAccess::ReadOnly)
                }
                RawBindingType::WriteonlyStorageTexture => {
                    out.storage_texture = storage_texture(wgt::StorageTextureAccess::WriteOnly)
                }
                RawBindingType::ReadWriteStorageTexture => {
                    out.storage_texture = storage_texture(wgt::StorageTextureAccess::ReadWrite)
                }
                RawBindingType::ExternalTexture => out.external_texture = true,
                RawBindingType::Error => return Err((idx, entry.error_case)),
            }
            Ok(out)
        })
        .collect::<Result<_, _>>();

    let action = match entries {
        Ok(entries) => {
            let wgpu_desc = BindGroupLayoutDescriptor {
                label,
                entries: Cow::Owned(entries),
            };
            DeviceAction::CreateBindGroupLayout(id, wgpu_desc)
        }
        Err((idx, error_case)) => {
            let initial_msg = match error_case {
                BindingTypeError::NoneSpecified => "no type specified",
                BindingTypeError::MultipleSpecified => "multiple types specified",
            };
            let mut message = format!("{initial_msg} for entry {idx} of bind group layout");
            if let Some(label) = label.as_deref() {
                write!(&mut message, "\"{label}\"").unwrap();
            }

            client.queue_message(&Message::Device(
                device_id,
                DeviceAction::Error(GPUError {
                    message: message.into(),
                    r#type: wgt::error::ErrorType::Validation,
                }),
            ));
            DeviceAction::CreateBindGroupLayoutError(id, label)
        }
    };
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_render_pipeline_get_bind_group_layout(
    client: &Client,
    device_id: id::DeviceId,
    pipeline_id: id::RenderPipelineId,
    index: u32,
) -> id::BindGroupLayoutId {
    let bgl_id = client.identities.lock().bind_group_layouts.process();

    let action = DeviceAction::RenderPipelineGetBindGroupLayout(pipeline_id, index, bgl_id);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);

    bgl_id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_compute_pipeline_get_bind_group_layout(
    client: &Client,
    device_id: id::DeviceId,
    pipeline_id: id::ComputePipelineId,
    index: u32,
) -> id::BindGroupLayoutId {
    let bgl_id = client.identities.lock().bind_group_layouts.process();

    let action = DeviceAction::ComputePipelineGetBindGroupLayout(pipeline_id, index, bgl_id);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);

    bgl_id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_create_pipeline_layout(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiPipelineLayoutDescriptor,
) -> id::PipelineLayoutId {
    let label = wgpu_string(desc.label);

    let id = client.identities.lock().pipeline_layouts.process();

    let wgpu_desc = PipelineLayoutDescriptor {
        label,
        bind_group_layouts: Cow::Borrowed(unsafe { desc.bind_group_layouts.as_slice() }),
        immediate_size: 0,
    };

    let action = DeviceAction::CreatePipelineLayout(id, wgpu_desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_create_bind_group(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiBindGroupDescriptor,
) -> id::BindGroupId {
    let label = wgpu_string(desc.label);

    let id = client.identities.lock().bind_groups.process();

    let entries = unsafe { desc.entries.as_slice() }
        .iter()
        .map(|entry| BindGroupEntry {
            binding: entry.binding,
            resource: if let Some(id) = entry.buffer {
                BindingResource::Buffer(BufferBinding {
                    buffer: id,
                    offset: entry.offset,
                    size: if entry.size_passed {
                        FfiOption::Some(entry.size)
                    } else {
                        FfiOption::None
                    },
                })
            } else if let Some(id) = entry.sampler {
                BindingResource::Sampler(id)
            } else if let Some(id) = entry.texture_view {
                BindingResource::TextureView(id)
            } else if let Some(id) = entry.external_texture {
                BindingResource::ExternalTexture(id)
            } else {
                panic!("Unexpected binding entry {:?}", entry);
            },
        })
        .collect();
    let wgpu_desc = BindGroupDescriptor {
        label,
        layout: desc.layout,
        entries: Cow::Owned(entries),
    };

    let action = DeviceAction::CreateBindGroup(id, wgpu_desc);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_create_compute_pipeline(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiComputePipelineDescriptor,
    is_async: bool,
) -> id::ComputePipelineId {
    let label = wgpu_string(desc.label);

    let identities = client.identities.lock();
    let id = identities.compute_pipelines.process();

    let wgpu_desc = ComputePipelineDescriptor {
        label,
        layout: desc.layout,
        stage: desc.stage.to_wgpu(),
    };

    let action = DeviceAction::CreateComputePipeline(id, wgpu_desc, is_async);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_create_render_pipeline(
    client: &Client,
    device_id: id::DeviceId,
    desc: &FfiRenderPipelineDescriptor,
    is_async: bool,
) -> id::RenderPipelineId {
    let label = wgpu_string(desc.label);

    let identities = client.identities.lock();
    let id = identities.render_pipelines.process();

    let wgpu_desc = RenderPipelineDescriptor {
        label,
        layout: desc.layout,
        vertex: desc.vertex.to_wgpu(),
        fragment: desc.fragment.map(FfiFragmentState::to_wgpu),
        primitive: desc.primitive.to_wgpu(),
        depth_stencil: desc.depth_stencil.map(DepthStencilState::to_wgpu),
        multisample: desc.multisample.clone(),
    };

    let action = DeviceAction::CreateRenderPipeline(id, wgpu_desc, is_async);
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
    id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_copy_buffer_to_buffer(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    source: id::BufferId,
    source_offset: wgt::BufferAddress,
    destination: id::BufferId,
    destination_offset: wgt::BufferAddress,
    size: wgt::BufferAddress,
) {
    // In Javascript, `size === undefined` means "copy from src_offset to end of
    // buffer". The `size` argument to this function uses a value of
    // `wgt::BufferAddress::MAX` to encode that case. (Valid copy
    // sizes must be multiples of four, so in the case that the application
    // really asked to copy BufferAddress::MAX bytes,
    // CommandEncoder::CopyBufferToBuffer decrements it by four, which
    // will still fail for mis-alignment.)
    let size = (size != wgt::BufferAddress::MAX).then_some(size);
    let command = CommandEncoderCommand::CopyBufferToBuffer {
        source,
        source_offset,
        destination,
        destination_offset,
        size,
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_copy_buffer_to_texture(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    src_buffer: id::BufferId,
    src_layout: &FfiTexelCopyBufferLayout,
    destination: TexelCopyTextureInfo,
    copy_size: wgt::Extent3d,
) {
    let command = CommandEncoderCommand::CopyBufferToTexture {
        source: TexelCopyBufferInfo {
            buffer: src_buffer,
            layout: src_layout.into_wgt(),
        },
        destination,
        copy_size,
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_copy_texture_to_buffer(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    source: TexelCopyTextureInfo,
    dst_buffer: id::BufferId,
    dst_layout: &FfiTexelCopyBufferLayout,
    copy_size: wgt::Extent3d,
) {
    let command = CommandEncoderCommand::CopyTextureToBuffer {
        source,
        destination: TexelCopyBufferInfo {
            buffer: dst_buffer,
            layout: dst_layout.into_wgt(),
        },
        copy_size,
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_copy_texture_to_texture(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    source: TexelCopyTextureInfo,
    destination: TexelCopyTextureInfo,
    copy_size: wgt::Extent3d,
) {
    let command = CommandEncoderCommand::CopyTextureToTexture {
        source,
        destination,
        copy_size,
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_clear_buffer(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    buffer: id::BufferId,
    offset: u64,
    size: Option<&u64>,
) {
    let command = CommandEncoderCommand::ClearBuffer {
        buffer,
        offset,
        size: size.cloned(),
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_resolve_query_set(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    query_set_id: id::QuerySetId,
    first_query: u32,
    query_count: u32,
    destination: id::BufferId,
    destination_offset: wgt::BufferAddress,
) {
    let command = CommandEncoderCommand::ResolveQuerySet {
        query_set: query_set_id,
        first_query,
        query_count,
        destination,
        destination_offset,
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub extern "C" fn wgpu_client_command_encoder_push_debug_group(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    marker: &nsACString,
) {
    let string = marker.to_string();
    let command = CommandEncoderCommand::DebugCommand(DebugCommand::PushDebugGroup(string));
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_pop_debug_group(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
) {
    let command = CommandEncoderCommand::DebugCommand(DebugCommand::PopDebugGroup);
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_insert_debug_marker(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    marker: &nsACString,
) {
    let string = marker.to_string();
    let command = CommandEncoderCommand::DebugCommand(DebugCommand::InsertDebugMarker(string));
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
}

#[repr(C)]
pub struct FfiCommandBufferDescriptor<'a> {
    label: Option<&'a nsACString>,
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_client_command_encoder_finish(
    client: &Client,
    command_encoder_id: id::CommandEncoderId,
    desc: &FfiCommandBufferDescriptor,
) -> id::CommandBufferId {
    let command_buffer_id = client.identities.lock().command_buffers.process();
    let label = wgpu_string(desc.label);
    let desc = CommandBufferDescriptor { label };
    let command = CommandEncoderCommand::Finish {
        desc,
        command_buffer_id,
    };
    let message = Message::CommandEncoder(command_encoder_id, command);
    client.queue_message(&message);
    command_buffer_id
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_report_validation_error(
    client: &Client,
    device_id: id::DeviceId,
    message: *const core::ffi::c_char,
) {
    let action = DeviceAction::Error(GPUError {
        message: unsafe { core::ffi::CStr::from_ptr(message) }
            .to_str()
            .unwrap()
            .into(),
        r#type: wgt::error::ErrorType::Validation,
    });
    let message = Message::Device(device_id, action);
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_queue_write_buffer_inline(
    client: &Client,
    queue_id: id::QueueId,
    dst: id::BufferId,
    offset: wgt::BufferAddress,
    data_buffer_index: usize,
) {
    let data_source = QueueWriteDataSource::DataBuffer(data_buffer_index);

    let action = QueueWriteAction::Buffer { dst, offset };
    let message = Message::QueueWrite {
        queue_id,
        data_source,
        action,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_queue_write_buffer_via_shmem(
    client: &Client,
    queue_id: id::QueueId,
    dst: id::BufferId,
    offset: wgt::BufferAddress,
    shmem_handle_index: usize,
) {
    let data_source = QueueWriteDataSource::Shmem(shmem_handle_index);

    let action = QueueWriteAction::Buffer { dst, offset };
    let message = Message::QueueWrite {
        queue_id,
        data_source,
        action,
    };
    client.queue_message(&message);
}

#[no_mangle]
pub unsafe extern "C" fn wgpu_queue_write_texture_via_shmem(
    client: &Client,
    queue_id: id::QueueId,
    dst: TexelCopyTextureInfo,
    layout: FfiTexelCopyBufferLayout,
    size: wgt::Extent3d,
    shmem_handle_index: usize,
) {
    let data_source = QueueWriteDataSource::Shmem(shmem_handle_index);

    let layout = layout.into_wgt();
    let action = QueueWriteAction::Texture { dst, layout, size };
    let message = Message::QueueWrite {
        queue_id,
        data_source,
        action,
    };
    client.queue_message(&message);
}

#[repr(C)]
pub struct TextureFormatBlockInfo {
    copy_size: u32,
    width: u32,
    height: u32,
}

/// Obtain the block size and dimensions for a single aspect.
///
/// Populates `info` and returns true on success. Returns false if `format` has
/// multiple aspects and `aspect` is `All`.
#[no_mangle]
pub extern "C" fn wgpu_texture_format_get_block_info(
    format: wgt::TextureFormat,
    aspect: wgt::TextureAspect,
    info: &mut TextureFormatBlockInfo,
) -> bool {
    let (width, height) = format.block_dimensions();
    let (copy_size, ret) = match format.block_copy_size(Some(aspect)) {
        Some(size) => (size, true),
        None => (0, false),
    };
    *info = TextureFormatBlockInfo {
        width,
        height,
        copy_size,
    };
    ret
}

#[no_mangle]
pub extern "C" fn wgpu_client_use_shared_texture_in_swapChain(format: wgt::TextureFormat) -> bool {
    let supported = match format {
        wgt::TextureFormat::Bgra8Unorm => true,
        _ => false,
    };

    supported
}
