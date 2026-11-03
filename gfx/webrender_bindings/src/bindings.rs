/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(clippy::missing_safety_doc)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use gleam::gl;
use std::cell::RefCell;
#[cfg(not(any(target_os = "macos", target_os = "ios")))]
use std::ffi::OsString;
use std::ffi::{CStr, CString};
use std::io::Cursor;
use std::marker::PhantomData;
use std::ops::Range;
#[cfg(target_os = "android")]
use std::os::raw::c_int;
use std::os::raw::{c_char, c_float, c_void};
#[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
use std::os::unix::ffi::OsStringExt;
#[cfg(target_os = "windows")]
use std::os::windows::ffi::OsStringExt;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;
use std::{env, mem, ptr, slice};
use thin_vec::ThinVec;
use webrender::glyph_rasterizer::{GlyphRasterThread, SharedFontResources};
use webrender::ChunkPool;

use euclid::SideOffsets2D;
use moz2d_renderer::Moz2dBlobImageHandler;
use nsstring::nsAString;
use program_cache::{remove_disk_cache, WrProgramCache};
use tracy_rs::register_thread_with_profiler;
use webrender::render_backend_pool::{PoolMemberSetup, RenderBackendPool};
use webrender::sw_compositor::SwCompositor;
use webrender::{
    api::units::*, api::*, create_webrender_instance, render_api::*, set_profiler_hooks, AsyncPropertySampler,
    AsyncScreenshotHandle, ClipRadius, Compositor, CompositorCapabilities, CompositorConfig, CompositorKind,
    CompositorSurfaceTransform, Device, DeviceOptions, FrameBuilderConfig, GlBackendConfig, GpuBackendConfig,
    MappableCompositor, MappedTileInfo, NativeSurfaceHandle, NativeSurfaceId, NativeSurfaceInfo, NativeTileId,
    PendingShadersToPrecache, PipelineInfo, ProfilerHooks, RecordedFrameHandle, RenderBackendHooks, Renderer,
    RendererStats, SWGLCompositeSurfaceInfo, SceneBuilderHooks, ShaderPrecacheFlags, Shaders, SharedShaders,
    SwapchainMode, TextureCacheConfig, UploadMethod, WebRenderOptions, WindowProperties, WindowVisibility,
    ONE_TIME_USAGE_HINT,
};
use wr_malloc_size_of::MallocSizeOfOps;

extern "C" {
    #[cfg(target_os = "android")]
    fn __android_log_write(prio: c_int, tag: *const c_char, text: *const c_char) -> c_int;
}

/// The unique id for WR resource identification.
static NEXT_NAMESPACE_ID: AtomicUsize = AtomicUsize::new(1);

/// Special value handled in this wrapper layer to signify a redundant clip chain.
pub const ROOT_CLIP_CHAIN: u64 = !0;

fn next_namespace_id() -> IdNamespace {
    IdNamespace(NEXT_NAMESPACE_ID.fetch_add(1, Ordering::Relaxed) as u32)
}

/// Whether a border should be antialiased.
#[repr(C)]
#[derive(Eq, PartialEq, Copy, Clone)]
pub enum AntialiasBorder {
    No = 0,
    Yes,
}

/// Used to indicate if an image is opaque, or has an alpha channel.
#[repr(u8)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum OpacityType {
    Opaque = 0,
    HasAlphaChannel = 1,
}

/// cbindgen:field-names=[mHandle]
/// cbindgen:derive-lt=true
/// cbindgen:derive-lte=true
/// cbindgen:derive-neq=true
type WrEpoch = Epoch;
/// cbindgen:field-names=[mHandle]
/// cbindgen:derive-lt=true
/// cbindgen:derive-lte=true
/// cbindgen:derive-neq=true
pub type WrIdNamespace = IdNamespace;

/// cbindgen:field-names=[mNamespace, mHandle]
type WrDocumentId = DocumentId;
/// cbindgen:field-names=[mNamespace, mHandle]
type WrPipelineId = PipelineId;
/// cbindgen:field-names=[mNamespace, mHandle]
/// cbindgen:derive-neq=true
type WrImageKey = ImageKey;
/// cbindgen:field-names=[mNamespace, mHandle]
pub type WrFontKey = FontKey;
/// cbindgen:field-names=[mNamespace, mHandle]
pub type WrFontInstanceKey = FontInstanceKey;
/// cbindgen:field-names=[mNamespace, mHandle]
type WrYuvColorSpace = YuvColorSpace;
/// cbindgen:field-names=[mNamespace, mHandle]
type WrColorDepth = ColorDepth;
/// cbindgen:field-names=[mNamespace, mHandle]
type WrColorRange = ColorRange;

#[inline]
fn clip_chain_id_to_webrender(id: u64, pipeline_id: WrPipelineId) -> ClipChainId {
    if id == ROOT_CLIP_CHAIN {
        ClipChainId::INVALID
    } else {
        ClipChainId(id, pipeline_id)
    }
}

#[repr(C)]
pub struct WrSpaceAndClipChain {
    space: WrSpatialId,
    clip_chain: u64,
}

impl WrSpaceAndClipChain {
    fn to_webrender(&self, pipeline_id: WrPipelineId) -> SpaceAndClipInfo {
        //Warning: special case here to support dummy clip chain
        SpaceAndClipInfo {
            spatial_id: self.space.to_webrender(pipeline_id),
            clip_chain_id: clip_chain_id_to_webrender(self.clip_chain, pipeline_id),
        }
    }
}

#[repr(C)]
pub enum WrStackingContextClip {
    None,
    ClipChain(u64),
}

impl WrStackingContextClip {
    fn to_webrender(&self, pipeline_id: WrPipelineId) -> Option<ClipChainId> {
        match *self {
            WrStackingContextClip::None => None,
            WrStackingContextClip::ClipChain(id) => {
                if id == ROOT_CLIP_CHAIN {
                    None
                } else {
                    Some(ClipChainId(id, pipeline_id))
                }
            },
        }
    }
}

unsafe fn make_slice<'a, T>(ptr: *const T, len: usize) -> &'a [T] {
    if ptr.is_null() {
        &[]
    } else {
        slice::from_raw_parts(ptr, len)
    }
}

unsafe fn make_slice_mut<'a, T>(ptr: *mut T, len: usize) -> &'a mut [T] {
    if ptr.is_null() {
        &mut []
    } else {
        slice::from_raw_parts_mut(ptr, len)
    }
}

pub struct DocumentHandle {
    api: RenderApi,
    document_id: DocumentId,
    // One of the two options below is Some and the other None at all times.
    // It would be nice to model with an enum, however it is tricky to express
    // moving a variant's content into another variant without moving the
    // containing enum.
    hit_tester_request: Option<HitTesterRequest>,
    hit_tester: Option<Arc<dyn ApiHitTester>>,
}

impl DocumentHandle {
    pub fn new(
        mut api: RenderApi,
        hit_tester: Option<Arc<dyn ApiHitTester>>,
        size: DeviceIntSize,
        id: u32,
    ) -> DocumentHandle {
        let doc = api.add_document_with_id(size, id);
        if static_prefs::pref!("gfx.webrender.enforce-embed-policy") {
            let mut txn = Transaction::new();
            txn.set_embed_policy_enforcement(true);
            api.send_transaction(doc, txn);
        }
        let hit_tester_request = if hit_tester.is_none() {
            // Request the hit tester early to reduce the likelihood of blocking on the
            // first hit testing query.
            Some(api.request_hit_tester(doc))
        } else {
            None
        };

        DocumentHandle {
            api,
            document_id: doc,
            hit_tester_request,
            hit_tester,
        }
    }

    fn ensure_hit_tester(&mut self) -> &Arc<dyn ApiHitTester> {
        if let Some(ref ht) = self.hit_tester {
            return ht;
        }
        self.hit_tester = Some(self.hit_tester_request.take().unwrap().resolve());
        self.hit_tester.as_ref().unwrap()
    }
}

#[repr(C)]
pub struct WrVecU8 {
    /// `data` must always be valid for passing to Vec::from_raw_parts.
    /// In particular, it must be non-null even if capacity is zero.
    data: *mut u8,
    length: usize,
    capacity: usize,
}

impl WrVecU8 {
    fn into_vec(mut self) -> Vec<u8> {
        // Clear self and then drop self.
        self.flush_into_vec()
    }

    // Clears self without consuming self.
    fn flush_into_vec(&mut self) -> Vec<u8> {
        // Create a Vec using Vec::from_raw_parts.
        //
        // Here are the safety requirements, verbatim from the documentation of `from_raw_parts`:
        //
        // > * `ptr` must have been allocated using the global allocator, such as via
        // >   the [`alloc::alloc`] function.
        // > * `T` needs to have the same alignment as what `ptr` was allocated with.
        // >   (`T` having a less strict alignment is not sufficient, the alignment really
        // >   needs to be equal to satisfy the [`dealloc`] requirement that memory must be
        // >   allocated and deallocated with the same layout.)
        // > * The size of `T` times the `capacity` (ie. the allocated size in bytes) needs
        // >   to be the same size as the pointer was allocated with. (Because similar to
        // >   alignment, [`dealloc`] must be called with the same layout `size`.)
        // > * `length` needs to be less than or equal to `capacity`.
        // > * The first `length` values must be properly initialized values of type `T`.
        // > * `capacity` needs to be the capacity that the pointer was allocated with.
        // > * The allocated size in bytes must be no larger than `isize::MAX`.
        // >   See the safety documentation of [`pointer::offset`].
        //
        // These comments don't say what to do for zero-capacity vecs which don't have
        // an allocation. In particular, the requirement "`ptr` must have been allocated"
        // is not met for such vecs.
        //
        // However, the safety requirements of `slice::from_raw_parts` are more explicit
        // about the empty case:
        //
        // > * `data` must be non-null and aligned even for zero-length slices. One
        // >   reason for this is that enum layout optimizations may rely on references
        // >   (including slices of any length) being aligned and non-null to distinguish
        // >   them from other data. You can obtain a pointer that is usable as `data`
        // >   for zero-length slices using [`NonNull::dangling()`].
        //
        // For the empty case we follow this requirement rather than the more stringent
        // requirement from the `Vec::from_raw_parts` docs.
        let vec = unsafe { Vec::from_raw_parts(self.data, self.length, self.capacity) };
        self.data = ptr::NonNull::dangling().as_ptr();
        self.length = 0;
        self.capacity = 0;
        vec
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe { core::slice::from_raw_parts(self.data, self.length) }
    }

    fn from_vec(mut v: Vec<u8>) -> WrVecU8 {
        let w = WrVecU8 {
            data: v.as_mut_ptr(),
            length: v.len(),
            capacity: v.capacity(),
        };
        mem::forget(v);
        w
    }

    fn reserve(&mut self, len: usize) {
        let mut vec = self.flush_into_vec();
        vec.reserve(len);
        *self = Self::from_vec(vec);
    }

    fn push_bytes(&mut self, bytes: &[u8]) {
        let mut vec = self.flush_into_vec();
        vec.extend_from_slice(bytes);
        *self = Self::from_vec(vec);
    }
}

#[no_mangle]
pub extern "C" fn wr_vec_u8_push_bytes(v: &mut WrVecU8, bytes: ByteSlice) {
    v.push_bytes(bytes.as_slice());
}

#[no_mangle]
pub extern "C" fn wr_vec_u8_reserve(v: &mut WrVecU8, len: usize) {
    v.reserve(len);
}

#[no_mangle]
pub extern "C" fn wr_vec_u8_free(v: WrVecU8) {
    v.into_vec();
}

#[repr(C)]
pub struct ByteSlice<'a> {
    buffer: *const u8,
    len: usize,
    _phantom: PhantomData<&'a ()>,
}

impl<'a> ByteSlice<'a> {
    pub fn new(slice: &'a [u8]) -> ByteSlice<'a> {
        ByteSlice {
            buffer: slice.as_ptr(),
            len: slice.len(),
            _phantom: PhantomData,
        }
    }

    pub fn as_slice(&self) -> &'a [u8] {
        unsafe { make_slice(self.buffer, self.len) }
    }
}

#[repr(C)]
pub struct MutByteSlice<'a> {
    buffer: *mut u8,
    len: usize,
    _phantom: PhantomData<&'a ()>,
}

impl<'a> MutByteSlice<'a> {
    pub fn new(slice: &'a mut [u8]) -> MutByteSlice<'a> {
        let len = slice.len();
        MutByteSlice {
            buffer: slice.as_mut_ptr(),
            len,
            _phantom: PhantomData,
        }
    }

    pub fn as_mut_slice(&mut self) -> &'a mut [u8] {
        unsafe { make_slice_mut(self.buffer, self.len) }
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct WrImageDescriptor {
    pub format: ImageFormat,
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    pub opacity: OpacityType,
    // TODO(gw): Remove this flag (use prim flags instead).
    pub prefer_compositor_surface: bool,
}

impl<'a> From<&'a WrImageDescriptor> for ImageDescriptor {
    fn from(desc: &'a WrImageDescriptor) -> ImageDescriptor {
        let mut flags = ImageDescriptorFlags::empty();

        if desc.opacity == OpacityType::Opaque {
            flags |= ImageDescriptorFlags::IS_OPAQUE;
        }

        ImageDescriptor {
            size: DeviceIntSize::new(desc.width, desc.height),
            stride: if desc.stride != 0 { Some(desc.stride) } else { None },
            format: desc.format,
            offset: 0,
            flags,
        }
    }
}

#[repr(u32)]
#[allow(dead_code)]
enum WrExternalImageType {
    RawData,
    NativeTexture,
    Invalid,
}

#[repr(C)]
struct WrExternalImage {
    image_type: WrExternalImageType,

    // external texture handle
    handle: u64,
    // external texture coordinate
    u0: f32,
    v0: f32,
    u1: f32,
    v1: f32,

    // external image buffer
    buff: *const u8,
    size: usize,
}

extern "C" {
    fn wr_renderer_lock_external_image(
        renderer: *mut c_void,
        external_image_id: ExternalImageId,
        channel_index: u8,
        is_composited: bool,
    ) -> WrExternalImage;
    fn wr_renderer_unlock_external_image(renderer: *mut c_void, external_image_id: ExternalImageId, channel_index: u8);
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct WrExternalImageHandler {
    external_image_obj: *mut c_void,
}

impl ExternalImageHandler for WrExternalImageHandler {
    fn lock(&mut self, id: ExternalImageId, channel_index: u8, is_composited: bool) -> ExternalImage {
        let image =
            unsafe { wr_renderer_lock_external_image(self.external_image_obj, id, channel_index, is_composited) };
        ExternalImage {
            uv: TexelRect::new(image.u0, image.v0, image.u1, image.v1),
            source: match image.image_type {
                WrExternalImageType::NativeTexture => {
                    ExternalImageSource::NativeTexture(ExternalTextureHandle(image.handle))
                },
                WrExternalImageType::RawData => {
                    ExternalImageSource::RawData(unsafe { make_slice(image.buff, image.size) })
                },
                WrExternalImageType::Invalid => ExternalImageSource::Invalid,
            },
        }
    }

    fn unlock(&mut self, id: ExternalImageId, channel_index: u8) {
        unsafe {
            wr_renderer_unlock_external_image(self.external_image_obj, id, channel_index);
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
// Used for ComponentTransfer only
pub struct WrFilterData {
    funcR_type: ComponentTransferFuncType,
    R_values: *mut c_float,
    R_values_count: usize,
    funcG_type: ComponentTransferFuncType,
    G_values: *mut c_float,
    G_values_count: usize,
    funcB_type: ComponentTransferFuncType,
    B_values: *mut c_float,
    B_values_count: usize,
    funcA_type: ComponentTransferFuncType,
    A_values: *mut c_float,
    A_values_count: usize,
}

#[repr(u32)]
#[derive(Debug)]
pub enum WrAnimationType {
    Transform = 0,
    Opacity = 1,
    BackgroundColor = 2,
}

#[repr(C)]
pub struct WrAnimationProperty {
    effect_type: WrAnimationType,
    id: u64,
}

/// cbindgen:derive-eq=false
#[repr(C)]
#[derive(Debug)]
pub struct WrAnimationPropertyValue<T> {
    pub id: u64,
    pub value: T,
}

pub type WrTransformProperty = WrAnimationPropertyValue<LayoutTransform>;
pub type WrOpacityProperty = WrAnimationPropertyValue<f32>;
pub type WrColorProperty = WrAnimationPropertyValue<ColorF>;

/// cbindgen:field-names=[mHandle]
/// cbindgen:derive-lt=true
/// cbindgen:derive-lte=true
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct WrWindowId(u64);

#[repr(C)]
#[derive(Debug)]
pub struct WrComputedTransformData {
    pub scale_from: LayoutSize,
    pub vertical_flip: bool,
    pub rotation: WrRotation,
}

fn get_proc_address(glcontext_ptr: *mut c_void, name: &str) -> *const c_void {
    extern "C" {
        fn get_proc_address_from_glcontext(glcontext_ptr: *mut c_void, procname: *const c_char) -> *const c_void;
    }

    let symbol_name = CString::new(name).unwrap();
    let symbol = unsafe { get_proc_address_from_glcontext(glcontext_ptr, symbol_name.as_ptr()) };
    symbol as *const _
}

#[repr(C)]
pub enum TelemetryProbe {
    SceneBuildTime = 0,
    SceneSwapTime = 1,
    FrameBuildTime = 2,
}

extern "C" {
    fn is_in_compositor_thread() -> bool;
    fn is_in_render_thread() -> bool;
    fn is_in_main_thread() -> bool;
    fn is_glcontext_gles(glcontext_ptr: *mut c_void) -> bool;
    fn is_glcontext_angle(glcontext_ptr: *mut c_void) -> bool;
    fn gfx_wr_resource_path_override() -> *const c_char;
    fn gfx_wr_use_optimized_shaders() -> bool;
    // TODO: make gfx_critical_error() work.
    // We still have problem to pass the error message from render/render_backend
    // thread to main thread now.
    #[allow(dead_code)]
    fn gfx_critical_error(msg: *const c_char);
    fn gfx_critical_note(msg: *const c_char);
    fn gfx_wr_set_crash_annotation(annotation: CrashAnnotation, value: *const c_char);
    fn gfx_wr_clear_crash_annotation(annotation: CrashAnnotation);
}

struct CppNotifier {
    window_id: WrWindowId,
}

unsafe impl Send for CppNotifier {}

extern "C" {
    fn wr_notifier_wake_up(window_id: WrWindowId, composite_needed: bool);
    fn wr_notifier_new_frame_ready(window_id: WrWindowId, publish_id: FramePublishId, params: &FrameReadyParams);
    fn wr_notifier_external_event(window_id: WrWindowId, raw_event: usize);
    fn wr_schedule_render(window_id: WrWindowId, reasons: RenderReasons);
    // NOTE: This moves away from pipeline_info.
    fn wr_schedule_frame_after_scene_build(window_id: WrWindowId, pipeline_info: &mut WrPipelineInfo);

    fn wr_transaction_notification_notified(handler: usize, when: Checkpoint);
}

impl RenderNotifier for CppNotifier {
    fn clone(&self) -> Box<dyn RenderNotifier> {
        Box::new(CppNotifier {
            window_id: self.window_id,
        })
    }

    fn wake_up(&self, composite_needed: bool) {
        unsafe {
            wr_notifier_wake_up(self.window_id, composite_needed);
        }
    }

    fn new_frame_ready(&self, _: DocumentId, publish_id: FramePublishId, params: &FrameReadyParams) {
        unsafe {
            wr_notifier_new_frame_ready(self.window_id, publish_id, params);
        }
    }

    fn external_event(&self, event: ExternalEvent) {
        unsafe {
            wr_notifier_external_event(self.window_id, event.unwrap());
        }
    }
}

struct MozCrashAnnotator;

unsafe impl Send for MozCrashAnnotator {}

impl CrashAnnotator for MozCrashAnnotator {
    fn set(&self, annotation: CrashAnnotation, value: &std::ffi::CStr) {
        unsafe {
            gfx_wr_set_crash_annotation(annotation, value.as_ptr());
        }
    }

    fn clear(&self, annotation: CrashAnnotation) {
        unsafe {
            gfx_wr_clear_crash_annotation(annotation);
        }
    }

    fn box_clone(&self) -> Box<dyn CrashAnnotator> {
        Box::new(MozCrashAnnotator)
    }
}

#[no_mangle]
pub extern "C" fn wr_renderer_set_clear_color(renderer: &mut Renderer, color: ColorF) {
    renderer.set_clear_color(color);
}

#[no_mangle]
pub extern "C" fn wr_renderer_set_external_image_handler(
    renderer: &mut Renderer,
    external_image_handler: &mut WrExternalImageHandler,
) {
    renderer.set_external_image_handler(Box::new(*external_image_handler));
}

#[no_mangle]
pub extern "C" fn wr_renderer_update(renderer: &mut Renderer) {
    renderer.update();
}

#[no_mangle]
pub extern "C" fn wr_renderer_trim_transient_resources(renderer: &mut Renderer, trim_upload_buffers: bool) {
    renderer.trim_transient_resources(trim_upload_buffers);
}

#[no_mangle]
pub extern "C" fn wr_renderer_set_target_frame_publish_id(renderer: &mut Renderer, publish_id: FramePublishId) {
    renderer.set_target_frame_publish_id(publish_id);
}

#[no_mangle]
pub extern "C" fn wr_renderer_render(
    renderer: &mut Renderer,
    width: i32,
    height: i32,
    buffer_age: usize,
    out_stats: &mut RendererStats,
    out_dirty_rects: &mut ThinVec<DeviceIntRect>,
    out_did_rasterize: &mut bool,
) -> bool {
    match renderer.render(DeviceIntSize::new(width, height), buffer_age) {
        Ok(results) => {
            *out_stats = results.stats;
            out_dirty_rects.extend(results.dirty_rects);
            *out_did_rasterize = results.did_rasterize_any_tile;
            true
        },
        Err(errors) => {
            for e in errors {
                warn!(" Failed to render: {:?}", e);
                let msg = CString::new(format!("wr_renderer_render: {:?}", e)).unwrap();
                unsafe {
                    gfx_critical_note(msg.as_ptr());
                }
            }
            false
        },
    }
}

#[no_mangle]
pub extern "C" fn wr_renderer_force_redraw(renderer: &mut Renderer) {
    renderer.force_redraw();
}

#[no_mangle]
pub extern "C" fn wr_renderer_record_frame(
    renderer: &mut Renderer,
    image_format: ImageFormat,
    out_handle: &mut RecordedFrameHandle,
    out_width: &mut i32,
    out_height: &mut i32,
) -> bool {
    if let Some((handle, size)) = renderer.record_frame(image_format) {
        *out_handle = handle;
        *out_width = size.width;
        *out_height = size.height;

        true
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn wr_renderer_map_recorded_frame(
    renderer: &mut Renderer,
    handle: RecordedFrameHandle,
    dst_buffer: *mut u8,
    dst_buffer_len: usize,
    dst_stride: usize,
) -> bool {
    renderer.map_recorded_frame(
        handle,
        unsafe { make_slice_mut(dst_buffer, dst_buffer_len) },
        dst_stride,
    )
}

#[no_mangle]
pub extern "C" fn wr_renderer_release_composition_recorder_structures(renderer: &mut Renderer) {
    renderer.release_composition_recorder_structures();
}

#[no_mangle]
pub extern "C" fn wr_renderer_get_screenshot_async(
    renderer: &mut Renderer,
    window_x: i32,
    window_y: i32,
    window_width: i32,
    window_height: i32,
    buffer_width: i32,
    buffer_height: i32,
    image_format: ImageFormat,
    screenshot_width: *mut i32,
    screenshot_height: *mut i32,
) -> AsyncScreenshotHandle {
    assert!(!screenshot_width.is_null());
    assert!(!screenshot_height.is_null());

    let (handle, size) = renderer.get_screenshot_async(
        DeviceIntRect::from_origin_and_size(
            DeviceIntPoint::new(window_x, window_y),
            DeviceIntSize::new(window_width, window_height),
        ),
        DeviceIntSize::new(buffer_width, buffer_height),
        image_format,
    );

    unsafe {
        *screenshot_width = size.width;
        *screenshot_height = size.height;
    }

    handle
}

#[no_mangle]
pub extern "C" fn wr_renderer_map_and_recycle_screenshot(
    renderer: &mut Renderer,
    handle: AsyncScreenshotHandle,
    dst_buffer: *mut u8,
    dst_buffer_len: usize,
    dst_stride: usize,
    dest_format: ImageFormat,
) -> bool {
    renderer.map_and_recycle_screenshot(
        handle,
        unsafe { make_slice_mut(dst_buffer, dst_buffer_len) },
        dst_stride,
        dest_format,
    )
}

#[no_mangle]
pub extern "C" fn wr_renderer_supports_bgra_readback(renderer: &Renderer) -> bool {
    renderer.supports_bgra_readback()
}

#[no_mangle]
pub extern "C" fn wr_renderer_release_profiler_structures(renderer: &mut Renderer) {
    renderer.release_profiler_structures();
}

// Call wr_renderer_render() before calling this function.
#[no_mangle]
pub unsafe extern "C" fn wr_renderer_readback(
    renderer: &mut Renderer,
    width: i32,
    height: i32,
    format: ImageFormat,
    dst_buffer: *mut u8,
    buffer_size: usize,
) {
    assert!(is_in_render_thread());

    let mut slice = make_slice_mut(dst_buffer, buffer_size);
    renderer.read_pixels_into(FramebufferIntSize::new(width, height).into(), format, &mut slice);
}

#[no_mangle]
pub unsafe extern "C" fn wr_renderer_set_profiler_ui(renderer: &mut Renderer, ui_str: *const u8, ui_str_len: usize) {
    let slice = std::slice::from_raw_parts(ui_str, ui_str_len);
    if let Ok(ui_str) = std::str::from_utf8(slice) {
        renderer.set_profiler_ui(ui_str);
    }
}

#[no_mangle]
pub unsafe extern "C" fn wr_renderer_delete(renderer: *mut Renderer) {
    let renderer = Box::from_raw(renderer);
    renderer.deinit();
    // let renderer go out of scope and get dropped
}

#[no_mangle]
pub unsafe extern "C" fn wr_renderer_accumulate_memory_report(
    renderer: &mut Renderer,
    report: &mut MemoryReport,
    swgl: *mut c_void,
) {
    *report += renderer.report_memory(swgl);
}

// cbindgen doesn't support tuples, so we have a little struct instead, with
// an Into implementation to convert from the tuple to the struct.
#[repr(C)]
pub struct WrPipelineEpoch {
    pipeline_id: WrPipelineId,
    document_id: WrDocumentId,
    epoch: WrEpoch,
}

impl<'a> From<(&'a (WrPipelineId, WrDocumentId), &'a WrEpoch)> for WrPipelineEpoch {
    fn from(tuple: (&(WrPipelineId, WrDocumentId), &WrEpoch)) -> WrPipelineEpoch {
        WrPipelineEpoch {
            pipeline_id: (tuple.0).0,
            document_id: (tuple.0).1,
            epoch: *tuple.1,
        }
    }
}

#[repr(C)]
pub struct WrPipelineIdAndEpoch {
    pipeline_id: WrPipelineId,
    epoch: WrEpoch,
}

impl<'a> From<(&WrPipelineId, &WrEpoch)> for WrPipelineIdAndEpoch {
    fn from(tuple: (&WrPipelineId, &WrEpoch)) -> WrPipelineIdAndEpoch {
        WrPipelineIdAndEpoch {
            pipeline_id: *tuple.0,
            epoch: *tuple.1,
        }
    }
}

#[repr(C)]
pub struct WrRemovedPipeline {
    pipeline_id: WrPipelineId,
    document_id: WrDocumentId,
}

impl<'a> From<&'a (WrPipelineId, WrDocumentId)> for WrRemovedPipeline {
    fn from(tuple: &(WrPipelineId, WrDocumentId)) -> WrRemovedPipeline {
        WrRemovedPipeline {
            pipeline_id: tuple.0,
            document_id: tuple.1,
        }
    }
}

#[repr(C)]
pub struct WrPipelineInfo {
    /// This contains an entry for each pipeline that was rendered, along with
    /// the epoch at which it was rendered. Rendered pipelines include the root
    /// pipeline and any other pipelines that were reachable via IFrame display
    /// items from the root pipeline.
    epochs: ThinVec<WrPipelineEpoch>,
    /// This contains an entry for each pipeline that was removed during the
    /// last transaction. These pipelines would have been explicitly removed by
    /// calling remove_pipeline on the transaction object; the pipeline showing
    /// up in this array means that the data structures have been torn down on
    /// the webrender side, and so any remaining data structures on the caller
    /// side can now be torn down also.
    removed_pipelines: ThinVec<WrRemovedPipeline>,
}

impl WrPipelineInfo {
    fn new(info: &PipelineInfo) -> Self {
        WrPipelineInfo {
            epochs: info.epochs.iter().map(WrPipelineEpoch::from).collect(),
            removed_pipelines: info.removed_pipelines.iter().map(WrRemovedPipeline::from).collect(),
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wr_renderer_flush_pipeline_info(renderer: &mut Renderer, out: &mut WrPipelineInfo) {
    let info = renderer.flush_pipeline_info();
    *out = WrPipelineInfo::new(&info);
}

extern "C" {
    pub fn gecko_profiler_thread_is_being_profiled() -> bool;
}

pub fn gecko_profiler_start_marker(name: &str, text: &str) {
    use gecko_profiler::{gecko_profiler_category, MarkerOptions, MarkerTiming, ProfilerTime, Tracing};
    if text.is_empty() {
        gecko_profiler::add_marker(
            name,
            gecko_profiler_category!(Graphics),
            MarkerOptions {
                timing: MarkerTiming::interval_start(ProfilerTime::now()),
                ..Default::default()
            },
            Tracing::from_static_str("Webrender"),
        );
    } else {
        gecko_profiler::add_text_marker(
            name,
            gecko_profiler_category!(Graphics),
            MarkerOptions {
                timing: MarkerTiming::interval_start(ProfilerTime::now()),
                ..Default::default()
            },
            text,
        );
    }
}
pub fn gecko_profiler_end_marker(name: &str, text: &str) {
    use gecko_profiler::{gecko_profiler_category, MarkerOptions, MarkerTiming, ProfilerTime, Tracing};
    if text.is_empty() {
        gecko_profiler::add_marker(
            name,
            gecko_profiler_category!(Graphics),
            MarkerOptions {
                timing: MarkerTiming::interval_end(ProfilerTime::now()),
                ..Default::default()
            },
            Tracing::from_static_str("Webrender"),
        );
    } else {
        gecko_profiler::add_text_marker(
            name,
            gecko_profiler_category!(Graphics),
            MarkerOptions {
                timing: MarkerTiming::interval_end(ProfilerTime::now()),
                ..Default::default()
            },
            text,
        );
    }
}

pub fn gecko_profiler_event_marker(name: &str) {
    use gecko_profiler::{gecko_profiler_category, Tracing};
    gecko_profiler::add_marker(
        name,
        gecko_profiler_category!(Graphics),
        Default::default(),
        Tracing::from_static_str("Webrender"),
    );
}

pub fn gecko_profiler_add_text_marker(name: &str, text: &str, microseconds: f64) {
    use gecko_profiler::{gecko_profiler_category, MarkerOptions, MarkerTiming, ProfilerTime};
    if !gecko_profiler::current_thread_is_being_profiled_for_markers() {
        return;
    }

    let now = ProfilerTime::now();
    let start = now.clone().subtract_microseconds(microseconds);
    gecko_profiler::add_text_marker(
        name,
        gecko_profiler_category!(Graphics),
        MarkerOptions {
            timing: MarkerTiming::interval(start, now),
            ..Default::default()
        },
        text,
    );
}

/// Simple implementation of the WR ProfilerHooks trait to allow profile
/// markers to be seen in the Gecko profiler.
struct GeckoProfilerHooks;

impl ProfilerHooks for GeckoProfilerHooks {
    fn register_thread(&self, thread_name: &str) {
        gecko_profiler::register_thread(thread_name);
    }

    fn unregister_thread(&self) {
        gecko_profiler::unregister_thread();
    }

    fn begin_marker(&self, label: &str, text: &str) {
        gecko_profiler_start_marker(label, text);
    }

    fn end_marker(&self, label: &str, text: &str) {
        gecko_profiler_end_marker(label, text);
    }

    fn event_marker(&self, label: &str) {
        gecko_profiler_event_marker(label);
    }

    fn add_text_marker(&self, label: &str, text: &str, duration: Duration) {
        let micros = duration.as_micros() as f64;
        gecko_profiler_add_text_marker(label, text, micros);
    }

    fn thread_is_being_profiled(&self) -> bool {
        unsafe { gecko_profiler_thread_is_being_profiled() }
    }
}

static PROFILER_HOOKS: GeckoProfilerHooks = GeckoProfilerHooks {};

#[allow(improper_ctypes)] // this is needed so that rustc doesn't complain about passing the &mut Transaction to an extern function
extern "C" {
    // These callbacks are invoked from the scene builder thread (aka the APZ
    // updater thread)
    fn apz_register_updater(window_id: WrWindowId);
    fn apz_pre_scene_swap(window_id: WrWindowId);
    fn apz_post_scene_swap(window_id: WrWindowId, pipeline_info: &WrPipelineInfo);
    fn apz_run_updater(window_id: WrWindowId);
    fn apz_deregister_updater(window_id: WrWindowId);

    // These callbacks are invoked from the render backend thread (aka the APZ
    // sampler thread)
    fn apz_register_sampler(window_id: WrWindowId);
    fn apz_sample_transforms(window_id: WrWindowId, generated_frame_id: *const u64, transaction: &mut Transaction);
    fn apz_deregister_sampler(window_id: WrWindowId);

    fn omta_register_sampler(window_id: WrWindowId);
    fn omta_sample(window_id: WrWindowId, transaction: &mut Transaction);
    fn omta_deregister_sampler(window_id: WrWindowId);
}

struct APZCallbacks {
    window_id: WrWindowId,
}

impl APZCallbacks {
    pub fn new(window_id: WrWindowId) -> Self {
        APZCallbacks { window_id }
    }
}

impl SceneBuilderHooks for APZCallbacks {
    fn register(&self) {
        unsafe {
            if static_prefs::pref!("gfx.webrender.scene-builder-thread-local-arena") {
                wr_register_thread_local_arena();
            }
            apz_register_updater(self.window_id);
        }
    }

    fn pre_scene_build(&self) {
        gecko_profiler_start_marker("SceneBuilding", "");
    }

    fn pre_scene_swap(&self) {
        unsafe {
            apz_pre_scene_swap(self.window_id);
        }
    }

    fn post_scene_swap(&self, _document_ids: &Vec<DocumentId>, info: PipelineInfo, schedule_frame: bool) {
        let mut info = WrPipelineInfo::new(&info);
        unsafe {
            apz_post_scene_swap(self.window_id, &info);
        }

        if schedule_frame {
            unsafe { wr_schedule_frame_after_scene_build(self.window_id, &mut info) }
        }
        gecko_profiler_end_marker("SceneBuilding", "");
    }

    fn post_resource_update(&self, _document_ids: &Vec<DocumentId>) {
        unsafe { wr_schedule_render(self.window_id, RenderReasons::POST_RESOURCE_UPDATES_HOOK) }
        gecko_profiler_end_marker("SceneBuilding", "");
    }

    fn post_empty_scene_build(&self) {
        gecko_profiler_end_marker("SceneBuilding", "");
    }

    fn poke(&self) {
        unsafe { apz_run_updater(self.window_id) }
    }

    fn deregister(&self) {
        unsafe { apz_deregister_updater(self.window_id) }
    }

    fn embed_policy_violation(&self, _document_id: DocumentId, embedder: PipelineId, child: PipelineId) {
        let msg = CString::new(format!(
            "WebRender ignored an iframe of pipeline {:?} in pipeline {:?}",
            child, embedder
        ))
        .unwrap();
        unsafe { gfx_critical_note(msg.as_ptr()) };
    }
}

struct RenderBackendCallbacks;

impl RenderBackendHooks for RenderBackendCallbacks {
    fn init_thread(&self) {
        if static_prefs::pref!("gfx.webrender.frame-builder-thread-local-arena") {
            unsafe { wr_register_thread_local_arena() };
        }
    }
}

struct SamplerCallback {
    window_id: WrWindowId,
}

impl SamplerCallback {
    pub fn new(window_id: WrWindowId) -> Self {
        SamplerCallback { window_id }
    }
}

impl AsyncPropertySampler for SamplerCallback {
    fn register(&self) {
        unsafe {
            apz_register_sampler(self.window_id);
            omta_register_sampler(self.window_id);
        }
    }

    fn sample(&self, _document_id: DocumentId, generated_frame_id: Option<u64>) -> Vec<FrameMsg> {
        let generated_frame_id_value;
        let generated_frame_id: *const u64 = match generated_frame_id {
            Some(id) => {
                generated_frame_id_value = id;
                &generated_frame_id_value
            },
            None => ptr::null_mut(),
        };
        let mut transaction = Transaction::new();
        // Reset the pending properties first because omta_sample and apz_sample_transforms
        // may be failed to reset them due to null samplers.
        transaction.reset_dynamic_properties();
        unsafe {
            apz_sample_transforms(self.window_id, generated_frame_id, &mut transaction);
            omta_sample(self.window_id, &mut transaction);
        };
        transaction.get_frame_ops()
    }

    fn deregister(&self) {
        unsafe {
            apz_deregister_sampler(self.window_id);
            omta_deregister_sampler(self.window_id);
        }
    }
}

extern "C" {
    fn wr_register_thread_local_arena();
}

pub struct WrThreadPool {
    workers: Arc<rayon::ThreadPool>,
    join_handles: Mutex<Vec<JoinHandle<()>>>,
}

#[no_mangle]
pub extern "C" fn wr_thread_pool_new(low_priority: bool) -> *mut WrThreadPool {
    // Clamp the number of workers between 1 and 4/8. We get diminishing returns
    // with high worker counts and extra overhead because of rayon and font
    // management.

    // We clamp to 4 high priority threads because contention and memory usage
    // make it not worth going higher
    let max = if low_priority { 8 } else { 4 };
    let num_threads = num_cpus::get().min(max);

    let priority_tag = if low_priority { "LP" } else { "" };
    let thread_name = move |idx| format!("WRWorker{}#{}", priority_tag, idx);

    let use_thread_local_arena = static_prefs::pref!("gfx.webrender.worker-thread-local-arena");
    let join_handles = Mutex::new(Vec::new());

    let worker = rayon::ThreadPoolBuilder::new()
        .thread_name(move |idx| thread_name(idx))
        .num_threads(num_threads)
        .spawn_handler(|thread| {
            let mut builder = std::thread::Builder::new();
            if let Some(name) = thread.name() {
                builder = builder.name(name.to_string());
            }
            if let Some(stack_size) = thread.stack_size() {
                builder = builder.stack_size(stack_size);
            }
            let handle = builder.spawn(|| thread.run())?;
            join_handles.lock().unwrap().push(handle);
            Ok(())
        })
        .start_handler(move |idx| {
            if use_thread_local_arena {
                unsafe {
                    wr_register_thread_local_arena();
                }
            }
            let name = thread_name(idx);
            register_thread_with_profiler(name.clone());
            gecko_profiler::register_thread(&name);
        })
        .exit_handler(|_idx| {
            gecko_profiler::unregister_thread();
        })
        .build();

    let workers = Arc::new(worker.unwrap());

    Box::into_raw(Box::new(WrThreadPool { workers, join_handles }))
}

#[no_mangle]
pub unsafe extern "C" fn wr_thread_pool_delete(thread_pool: *mut WrThreadPool, join_workers: bool) {
    let WrThreadPool { workers, join_handles } = *Box::from_raw(thread_pool);

    if !join_workers {
        return;
    }

    let Some(workers) = Arc::into_inner(workers) else {
        panic!("All other references to workers must be dropped before calling wr_thread_pool_delete");
    };
    mem::drop(workers);

    let join_handles = join_handles.into_inner().unwrap();
    for join_handle in join_handles {
        let _ = join_handle.join();
    }
}

pub struct WrChunkPool(Arc<ChunkPool>);

#[no_mangle]
pub unsafe extern "C" fn wr_chunk_pool_new() -> *mut WrChunkPool {
    Box::into_raw(Box::new(WrChunkPool(Arc::new(ChunkPool::new()))))
}

#[no_mangle]
pub unsafe extern "C" fn wr_chunk_pool_delete(pool: *mut WrChunkPool) {
    mem::drop(Box::from_raw(pool));
}

#[no_mangle]
pub unsafe extern "C" fn wr_chunk_pool_purge(pool: &WrChunkPool) {
    pool.0.purge_all_chunks();
}

/// Inner contents of [`WrRenderBackendPool`]. Hidden behind a tuple wrapper
/// so cbindgen exports the outer type as a true opaque forward declaration.
struct RenderBackendPoolHandle {
    pool: Arc<RenderBackendPool>,
    /// Font namespace shared by every window assigned to this pool. The
    /// pool's scene builders use a single `SharedFontResources` keyed by
    /// this namespace, so all windows must register their `ResourceCache`
    /// with the same namespace, otherwise scene-builder font lookups would
    /// miss.
    font_namespace: IdNamespace,
    /// The `SharedFontResources` instance owned by this pool. Every
    /// window assigned to the pool clones this so that fonts registered
    /// from any RenderApi are visible to every scene builder in the pool.
    fonts: SharedFontResources,
}

pub struct WrRenderBackendPool(RenderBackendPoolHandle);

/// Build a placeholder `FrameBuilderConfig` for use in `RenderBackendPool`
/// construction. Real values arrive when the first window registers and
/// `RenderBackend::register_window` propagates them to the scene builder via
/// `SetFrameBuilderConfig`.
fn placeholder_frame_builder_config() -> FrameBuilderConfig {
    FrameBuilderConfig {
        default_font_render_mode: FontRenderMode::Mono,
        dual_source_blending_is_supported: false,
        testing: false,
        gpu_supports_fast_clears: false,
        gpu_supports_advanced_blend: false,
        advanced_blend_is_coherent: false,
        gpu_supports_render_target_partial_update: false,
        external_images_require_copy: false,
        batch_lookback_count: 10,
        background_color: None,
        compositor_kind: CompositorKind::default(),
        tile_size_override: None,
        max_surface_override: None,
        max_depth_ids: 1,
        max_target_size: 2048,
        force_invalidation: false,
        is_software: false,
        low_quality_pinch_zoom: false,
        max_shared_surface_size: 4096,
        enable_dithering: false,
        enable_yuv_overlay_stability: false,
    }
}

/// Create a shared render-backend pool of `size` threads.
///
/// Returns null if `size` is zero or thread creation fails. The returned
/// pointer is owned; release it with `wr_render_backend_pool_delete`.
#[no_mangle]
pub unsafe extern "C" fn wr_render_backend_pool_new(
    size: usize,
    size_of_op: VoidPtrToSizeFn,
    enclosing_size_of_op: VoidPtrToSizeFn,
) -> *mut WrRenderBackendPool {
    if size == 0 {
        return std::ptr::null_mut();
    }

    // Ensure the WR profiler callbacks are hooked up to the Gecko profiler.
    set_profiler_hooks(Some(&PROFILER_HOOKS));

    let font_namespace = next_namespace_id();
    let fonts = SharedFontResources::new(font_namespace);
    let pool_fonts = fonts.clone();
    let config = placeholder_frame_builder_config();

    let pool = match RenderBackendPool::new(size, move |idx| PoolMemberSetup {
        frame_builder_config: config.clone(),
        fonts: pool_fonts.clone(),
        support_low_priority_transactions: true,
        size_of_op: Some(size_of_op),
        enclosing_size_of_op: Some(enclosing_size_of_op),
        render_backend_hooks: Some(Box::new(RenderBackendCallbacks)),
        namespace_alloc_by_client: true,
        thread_name_suffix: idx.to_string(),
    }) {
        Ok(p) => p,
        Err(_) => return std::ptr::null_mut(),
    };

    Box::into_raw(Box::new(WrRenderBackendPool(RenderBackendPoolHandle {
        pool,
        font_namespace,
        fonts,
    })))
}

#[no_mangle]
pub unsafe extern "C" fn wr_render_backend_pool_delete(pool: *mut WrRenderBackendPool) {
    if !pool.is_null() {
        // Dropping the pool shuts its threads down and waits for them to exit.
        mem::drop(Box::from_raw(pool));
    }
}

#[no_mangle]
pub unsafe extern "C" fn wr_program_cache_new(
    prof_path: &nsAString,
    thread_pool: *mut WrThreadPool,
) -> *mut WrProgramCache {
    let workers = &(*thread_pool).workers;
    let program_cache = WrProgramCache::new(prof_path, workers);
    Box::into_raw(Box::new(program_cache))
}

#[no_mangle]
pub unsafe extern "C" fn wr_program_cache_delete(program_cache: *mut WrProgramCache) {
    mem::drop(Box::from_raw(program_cache));
}

#[no_mangle]
pub unsafe extern "C" fn wr_try_load_startup_shaders_from_disk(program_cache: *mut WrProgramCache) {
    (*program_cache).try_load_startup_shaders_from_disk();
}

#[no_mangle]
pub unsafe extern "C" fn remove_program_binary_disk_cache(prof_path: &nsAString) -> bool {
    match remove_disk_cache(prof_path) {
        Ok(_) => true,
        Err(_) => {
            error!("Failed to remove program binary disk cache");
            false
        },
    }
}

// This matches IsEnvSet in gfxEnv.h
fn env_var_to_bool(key: &'static str) -> bool {
    env::var(key).ok().map_or(false, |v| !v.is_empty())
}

// Call MakeCurrent before this.
fn wr_device_new(gl_context: *mut c_void, pc: Option<&mut WrProgramCache>) -> Device {
    assert!(unsafe { is_in_render_thread() });

    let gl;
    if unsafe { is_glcontext_gles(gl_context) } {
        gl = unsafe { gl::GlesFns::load_with(|symbol| get_proc_address(gl_context, symbol)) };
    } else {
        gl = unsafe { gl::GlFns::load_with(|symbol| get_proc_address(gl_context, symbol)) };
    }

    let version = gl.get_string(gl::VERSION);

    info!("WebRender - OpenGL version new {}", version);

    let upload_method = if unsafe { is_glcontext_angle(gl_context) } {
        UploadMethod::Immediate
    } else {
        UploadMethod::PixelBuffer(ONE_TIME_USAGE_HINT)
    };

    let resource_override_path = unsafe {
        let override_charptr = gfx_wr_resource_path_override();
        if override_charptr.is_null() {
            None
        } else {
            match CStr::from_ptr(override_charptr).to_str() {
                Ok(override_str) => Some(PathBuf::from(override_str)),
                _ => None,
            }
        }
    };

    let use_optimized_shaders = unsafe { gfx_wr_use_optimized_shaders() };

    let cached_programs = pc.map(|cached_programs| Rc::clone(cached_programs.rc_get()));

    Device::new(
        GpuBackendConfig::Gl(GlBackendConfig::new(gl)),
        DeviceOptions {
            crash_annotator: Some(Box::new(MozCrashAnnotator)),
            resource_override_path,
            use_optimized_shaders,
            upload_method,
            batched_upload_threshold: 512 * 512,
            cached_programs,
            allow_texture_swizzling: true,
            dump_shader_source: None,
            surface_origin_is_top_left: false,
        },
    )
}

extern "C" {
    fn wr_compositor_create_surface(
        compositor: *mut c_void,
        id: NativeSurfaceId,
        tile_size: DeviceIntSize,
        is_opaque: bool,
    );
    fn wr_compositor_create_swapchain_surface(
        compositor: *mut c_void,
        id: NativeSurfaceId,
        size: DeviceIntSize,
        is_opaque: bool,
        needs_sync_dcomp_commit: bool,
    );
    fn wr_compositor_resize_swapchain(compositor: *mut c_void, id: NativeSurfaceId, size: DeviceIntSize);
    fn wr_compositor_create_external_surface(compositor: *mut c_void, id: NativeSurfaceId, is_opaque: bool);
    fn wr_compositor_create_backdrop_surface(compositor: *mut c_void, id: NativeSurfaceId, color: ColorF);
    fn wr_compositor_destroy_surface(compositor: *mut c_void, id: NativeSurfaceId);
    fn wr_compositor_create_tile(compositor: *mut c_void, id: NativeSurfaceId, x: i32, y: i32);
    fn wr_compositor_destroy_tile(compositor: *mut c_void, id: NativeSurfaceId, x: i32, y: i32);
    fn wr_compositor_attach_external_image(
        compositor: *mut c_void,
        id: NativeSurfaceId,
        external_image: ExternalImageId,
    );
    fn wr_compositor_bind(
        compositor: *mut c_void,
        id: NativeTileId,
        offset: &mut DeviceIntPoint,
        handle: &mut u64,
        dirty_rect: DeviceIntRect,
        valid_rect: DeviceIntRect,
    );
    fn wr_compositor_unbind(compositor: *mut c_void);
    fn wr_compositor_begin_frame(compositor: *mut c_void);
    fn wr_compositor_add_surface(
        compositor: *mut c_void,
        id: NativeSurfaceId,
        transform: &CompositorSurfaceTransform,
        clip_rect: DeviceIntRect,
        image_rendering: ImageRendering,
        rounded_clip_rect: DeviceIntRect,
        rounded_clip_radii: ClipRadius,
    );
    fn wr_compositor_start_compositing(
        compositor: *mut c_void,
        clear_color: ColorF,
        dirty_rects: *const DeviceIntRect,
        num_dirty_rects: usize,
        opaque_rects: *const DeviceIntRect,
        num_opaque_rects: usize,
    );
    fn wr_compositor_end_frame(compositor: *mut c_void);
    fn wr_compositor_deinit(compositor: *mut c_void);
    fn wr_compositor_get_capabilities(compositor: *mut c_void, caps: *mut CompositorCapabilities);
    fn wr_compositor_get_window_visibility(compositor: *mut c_void, caps: *mut WindowVisibility);
    fn wr_compositor_get_window_properties(compositor: *mut c_void, props: *mut WindowProperties);
    fn wr_compositor_bind_swapchain(
        compositor: *mut c_void,
        id: NativeSurfaceId,
        dirty_rects: *const DeviceIntRect,
        num_dirty_rects: usize,
    );
    fn wr_compositor_present_swapchain(
        compositor: *mut c_void,
        id: NativeSurfaceId,
        dirty_rects: *const DeviceIntRect,
        num_dirty_rects: usize,
    );
    fn wr_compositor_map_tile(
        compositor: *mut c_void,
        id: NativeTileId,
        dirty_rect: DeviceIntRect,
        valid_rect: DeviceIntRect,
        data: &mut *mut c_void,
        stride: &mut i32,
    );
    fn wr_compositor_unmap_tile(compositor: *mut c_void);
    fn wr_compositor_get_swapchain_mode(
        compositor: *mut c_void,
        uses_os_layers: &mut bool,
        max_partial_present_rects: &mut usize,
    );
}

pub struct WrCompositor(*mut c_void);

impl Compositor for WrCompositor {
    fn create_surface(&mut self, id: NativeSurfaceId, tile_size: DeviceIntSize, is_opaque: bool) {
        unsafe {
            wr_compositor_create_surface(self.0, id, tile_size, is_opaque);
        }
    }

    fn create_external_surface(&mut self, id: NativeSurfaceId, is_opaque: bool) {
        unsafe {
            wr_compositor_create_external_surface(self.0, id, is_opaque);
        }
    }

    fn create_backdrop_surface(&mut self, id: NativeSurfaceId, color: ColorF) {
        unsafe {
            wr_compositor_create_backdrop_surface(self.0, id, color);
        }
    }

    fn destroy_surface(&mut self, id: NativeSurfaceId) {
        unsafe {
            wr_compositor_destroy_surface(self.0, id);
        }
    }

    fn create_tile(&mut self, id: NativeTileId) {
        unsafe {
            wr_compositor_create_tile(self.0, id.surface_id, id.x, id.y);
        }
    }

    fn destroy_tile(&mut self, id: NativeTileId) {
        unsafe {
            wr_compositor_destroy_tile(self.0, id.surface_id, id.x, id.y);
        }
    }

    fn attach_external_image(&mut self, id: NativeSurfaceId, external_image: ExternalImageId) {
        unsafe {
            wr_compositor_attach_external_image(self.0, id, external_image);
        }
    }

    fn bind(&mut self, id: NativeTileId, dirty_rect: DeviceIntRect, valid_rect: DeviceIntRect) -> NativeSurfaceInfo {
        let mut surface_info = NativeSurfaceInfo {
            origin: DeviceIntPoint::zero(),
            handle: NativeSurfaceHandle::DEFAULT,
        };

        unsafe {
            wr_compositor_bind(
                self.0,
                id,
                &mut surface_info.origin,
                &mut surface_info.handle.0,
                dirty_rect,
                valid_rect,
            );
        }

        surface_info
    }

    fn unbind(&mut self) {
        unsafe {
            wr_compositor_unbind(self.0);
        }
    }

    fn begin_frame(&mut self) {
        unsafe {
            wr_compositor_begin_frame(self.0);
        }
    }

    fn add_surface(
        &mut self,
        id: NativeSurfaceId,
        transform: CompositorSurfaceTransform,
        clip_rect: DeviceIntRect,
        image_rendering: ImageRendering,
        rounded_clip_rect: DeviceIntRect,
        rounded_clip_radii: ClipRadius,
    ) {
        unsafe {
            wr_compositor_add_surface(
                self.0,
                id,
                &transform,
                clip_rect,
                image_rendering,
                rounded_clip_rect,
                rounded_clip_radii,
            );
        }
    }

    fn start_compositing(
        &mut self,
        clear_color: ColorF,
        dirty_rects: &[DeviceIntRect],
        opaque_rects: &[DeviceIntRect],
    ) {
        unsafe {
            wr_compositor_start_compositing(
                self.0,
                clear_color,
                dirty_rects.as_ptr(),
                dirty_rects.len(),
                opaque_rects.as_ptr(),
                opaque_rects.len(),
            );
        }
    }

    fn end_frame(&mut self) {
        unsafe {
            wr_compositor_end_frame(self.0);
        }
    }

    fn deinit(&mut self) {
        unsafe {
            wr_compositor_deinit(self.0);
        }
    }

    fn get_capabilities(&self) -> CompositorCapabilities {
        unsafe {
            let mut caps: CompositorCapabilities = Default::default();
            wr_compositor_get_capabilities(self.0, &mut caps);
            caps
        }
    }

    fn get_window_visibility(&self) -> WindowVisibility {
        unsafe {
            let mut visibility: WindowVisibility = Default::default();
            wr_compositor_get_window_visibility(self.0, &mut visibility);
            visibility
        }
    }

    fn create_swapchain_surface(
        &mut self,
        id: NativeSurfaceId,
        size: DeviceIntSize,
        is_opaque: bool,
        needs_sync_commit: bool,
    ) {
        unsafe {
            wr_compositor_create_swapchain_surface(self.0, id, size, is_opaque, needs_sync_commit);
        }
    }

    fn resize_swapchain(&mut self, id: NativeSurfaceId, size: DeviceIntSize) {
        unsafe {
            wr_compositor_resize_swapchain(self.0, id, size);
        }
    }

    fn bind_swapchain(&mut self, id: NativeSurfaceId, dirty_rects: &[DeviceIntRect]) {
        unsafe {
            wr_compositor_bind_swapchain(self.0, id, dirty_rects.as_ptr(), dirty_rects.len());
        }
    }

    fn present_swapchain(&mut self, id: NativeSurfaceId, dirty_rects: &[DeviceIntRect]) {
        unsafe {
            wr_compositor_present_swapchain(self.0, id, dirty_rects.as_ptr(), dirty_rects.len());
        }
    }

    fn get_window_properties(&self) -> WindowProperties {
        unsafe {
            let mut props: WindowProperties = Default::default();
            wr_compositor_get_window_properties(self.0, &mut props);
            props
        }
    }

    fn get_swapchain_mode(&self) -> SwapchainMode {
        let mut uses_os_layers = false;
        let mut max_partial_present_rects = 0;
        unsafe {
            wr_compositor_get_swapchain_mode(
                self.0,
                &mut uses_os_layers,
                &mut max_partial_present_rects,
            );
        }
        if uses_os_layers {
            SwapchainMode::OsLayers {
                max_partial_present_rects,
            }
        } else {
            SwapchainMode::WindowFramebuffer {
                max_partial_present_rects,
            }
        }
    }
}

extern "C" {
    fn wr_swgl_lock_composite_surface(
        ctx: *mut c_void,
        external_image_id: ExternalImageId,
        composite_info: *mut SWGLCompositeSurfaceInfo,
    ) -> bool;
    fn wr_swgl_unlock_composite_surface(ctx: *mut c_void, external_image_id: ExternalImageId);
}

impl MappableCompositor for WrCompositor {
    /// Map a tile's underlying buffer so it can be used as the backing for
    /// a SWGL framebuffer. This is intended to be a replacement for 'bind'
    /// in any compositors that intend to directly interoperate with SWGL
    /// while supporting some form of native layers.
    fn map_tile(
        &mut self,
        id: NativeTileId,
        dirty_rect: DeviceIntRect,
        valid_rect: DeviceIntRect,
    ) -> Option<MappedTileInfo> {
        let mut tile_info = MappedTileInfo {
            data: ptr::null_mut(),
            stride: 0,
        };

        unsafe {
            wr_compositor_map_tile(
                self.0,
                id,
                dirty_rect,
                valid_rect,
                &mut tile_info.data,
                &mut tile_info.stride,
            );
        }

        if !tile_info.data.is_null() && tile_info.stride != 0 {
            Some(tile_info)
        } else {
            None
        }
    }

    /// Unmap a tile that was was previously mapped via map_tile to signal
    /// that SWGL is done rendering to the buffer.
    fn unmap_tile(&mut self) {
        unsafe {
            wr_compositor_unmap_tile(self.0);
        }
    }

    fn lock_composite_surface(
        &mut self,
        ctx: *mut c_void,
        external_image_id: ExternalImageId,
        composite_info: *mut SWGLCompositeSurfaceInfo,
    ) -> bool {
        unsafe { wr_swgl_lock_composite_surface(ctx, external_image_id, composite_info) }
    }
    fn unlock_composite_surface(&mut self, ctx: *mut c_void, external_image_id: ExternalImageId) {
        unsafe { wr_swgl_unlock_composite_surface(ctx, external_image_id) }
    }
}

/// A wrapper around a strong reference to a Shaders object, and around the
/// Device object that was used to create the shaders.
///
/// We store the device to avoid repeated GL function lookups.
pub struct WrShaders {
    shaders: SharedShaders,
    shaders_to_precache: PendingShadersToPrecache,
    device: Device,
}

pub struct WrGlyphRasterThread(GlyphRasterThread);

#[no_mangle]
pub extern "C" fn wr_glyph_raster_thread_new() -> *mut WrGlyphRasterThread {
    let thread = GlyphRasterThread::new(
        || {
            gecko_profiler::register_thread("WrGlyphRasterizer");
        },
        || {
            gecko_profiler::unregister_thread();
        },
    );

    match thread {
        Ok(thread) => {
            return Box::into_raw(Box::new(WrGlyphRasterThread(thread)));
        },
        Err(..) => {
            return std::ptr::null_mut();
        },
    }
}

#[no_mangle]
pub extern "C" fn wr_glyph_raster_thread_delete(thread: *mut WrGlyphRasterThread) {
    let thread = unsafe { Box::from_raw(thread) };
    thread.0.shut_down();
}

// Call MakeCurrent before this.
#[no_mangle]
pub extern "C" fn wr_window_new(
    window_id: WrWindowId,
    window_width: i32,
    window_height: i32,
    is_main_window: bool,
    support_low_priority_transactions: bool,
    support_low_priority_threadpool: bool,
    allow_texture_swizzling: bool,
    allow_scissored_cache_clears: bool,
    swgl_context: *mut c_void,
    gl_context: *mut c_void,
    surface_origin_is_top_left: bool,
    program_cache: Option<&mut WrProgramCache>,
    shaders: Option<&mut WrShaders>,
    thread_pool: *mut WrThreadPool,
    thread_pool_low_priority: *mut WrThreadPool,
    chunk_pool: &WrChunkPool,
    render_backend_pool: Option<&WrRenderBackendPool>,
    glyph_raster_thread: Option<&WrGlyphRasterThread>,
    size_of_op: VoidPtrToSizeFn,
    enclosing_size_of_op: VoidPtrToSizeFn,
    document_id: u32,
    compositor: *mut c_void,
    use_native_compositor: bool,
    out_handle: &mut *mut DocumentHandle,
    out_renderer: &mut *mut Renderer,
    out_max_texture_size: *mut i32,
    out_err: &mut *mut c_char,
    enable_gpu_markers: bool,
    panic_on_gl_error: bool,
    picture_tile_width: i32,
    picture_tile_height: i32,
    reject_software_rasterizer: bool,
    low_quality_pinch_zoom: bool,
    max_shared_surface_size: i32,
    enable_subpixel_aa: bool,
    limit_sdr_yuv_external_composites: bool,
) -> bool {
    assert!(unsafe { is_in_render_thread() });

    // Ensure the WR profiler callbacks are hooked up to the Gecko profiler.
    set_profiler_hooks(Some(&PROFILER_HOOKS));

    let software = !swgl_context.is_null();
    let (gl, sw_gl) = if software {
        let ctx = swgl::Context::from(swgl_context);
        ctx.make_current();
        (Rc::new(ctx) as Rc<dyn gl::Gl>, Some(ctx))
    } else {
        let gl = unsafe {
            if gl_context.is_null() {
                panic!("Native GL context required when not using SWGL!");
            } else if is_glcontext_gles(gl_context) {
                gl::GlesFns::load_with(|symbol| get_proc_address(gl_context, symbol))
            } else {
                gl::GlFns::load_with(|symbol| get_proc_address(gl_context, symbol))
            }
        };
        (gl, None)
    };

    let version = gl.get_string(gl::VERSION);

    info!("WebRender - OpenGL version new {}", version);

    let workers = unsafe { Arc::clone(&(*thread_pool).workers) };
    let workers_low_priority = unsafe {
        if support_low_priority_threadpool {
            Arc::clone(&(*thread_pool_low_priority).workers)
        } else {
            Arc::clone(&(*thread_pool).workers)
        }
    };

    let upload_method = if !gl_context.is_null() && unsafe { is_glcontext_angle(gl_context) } {
        UploadMethod::Immediate
    } else {
        UploadMethod::PixelBuffer(ONE_TIME_USAGE_HINT)
    };

    let precache_flags = if env_var_to_bool("MOZ_WR_PRECACHE_SHADERS") {
        ShaderPrecacheFlags::FULL_COMPILE
    } else {
        ShaderPrecacheFlags::empty()
    };

    let cached_programs = program_cache.map(|program_cache| Rc::clone(&program_cache.rc_get()));

    let color = if cfg!(target_os = "android") {
        // The color is for avoiding black flash before receiving display list.
        ColorF::new(1.0, 1.0, 1.0, 1.0)
    } else {
        ColorF::new(0.0, 0.0, 0.0, 0.0)
    };

    let compositor_config = if software {
        CompositorConfig::Native {
            compositor: Box::new(SwCompositor::new(
                sw_gl.unwrap(),
                Box::new(WrCompositor(compositor)),
                use_native_compositor,
            )),
        }
    } else if use_native_compositor {
        CompositorConfig::Native {
            compositor: Box::new(WrCompositor(compositor)),
        }
    } else {
        CompositorConfig::Layer {
            compositor: Box::new(WrCompositor(compositor)),
        }
    };

    let picture_tile_size = if picture_tile_width > 0 && picture_tile_height > 0 {
        Some(DeviceIntSize::new(picture_tile_width, picture_tile_height))
    } else {
        None
    };

    let texture_cache_config = if is_main_window {
        TextureCacheConfig::DEFAULT
    } else {
        TextureCacheConfig {
            color8_linear_texture_size: 512,
            color8_nearest_texture_size: 512,
            color8_glyph_texture_size: 512,
            alpha8_texture_size: 512,
            alpha8_glyph_texture_size: 512,
            alpha16_texture_size: 512,
        }
    };

    let enable_dithering = if !software && static_prefs::pref!("gfx.webrender.dithering") {
        true
    } else {
        false
    };

    let enable_yuv_overlay_stability = cfg!(target_os = "windows");

    let enable_shared_instance_buffer = static_prefs::pref!("gfx.webrender.shared-instance-buffer");

    let opts = WebRenderOptions {
        enable_aa: true,
        enable_subpixel_aa,
        support_low_priority_transactions,
        allow_texture_swizzling,
        blob_image_handler: Some(Box::new(Moz2dBlobImageHandler::new(
            workers.clone(),
            workers_low_priority,
        ))),
        crash_annotator: Some(Box::new(MozCrashAnnotator)),
        workers: Some(workers),
        chunk_pool: Some(chunk_pool.0.clone()),
        dedicated_glyph_raster_thread: glyph_raster_thread.map(|grt| grt.0.clone()),
        size_of_op: Some(size_of_op),
        enclosing_size_of_op: Some(enclosing_size_of_op),
        cached_programs,
        resource_override_path: unsafe {
            let override_charptr = gfx_wr_resource_path_override();
            if override_charptr.is_null() {
                None
            } else {
                match CStr::from_ptr(override_charptr).to_str() {
                    Ok(override_str) => Some(PathBuf::from(override_str)),
                    _ => None,
                }
            }
        },
        use_optimized_shaders: unsafe { gfx_wr_use_optimized_shaders() },
        renderer_id: Some(window_id.0),
        upload_method,
        scene_builder_hooks: Some(Box::new(APZCallbacks::new(window_id))),
        render_backend_hooks: Some(Box::new(RenderBackendCallbacks)),
        render_backend_pool: render_backend_pool.map(|p| p.0.pool.clone()),
        sampler: Some(Box::new(SamplerCallback::new(window_id))),
        max_internal_texture_size: Some(8192), // We want to tile if larger than this
        clear_color: color,
        precache_flags,
        namespace_alloc_by_client: true,
        // When a shared backend pool is in use, every window assigned to it
        // must share the pool's font namespace so the pool's scene builders
        // can resolve font keys for any window.
        shared_font_namespace: Some(match render_backend_pool {
            Some(p) => p.0.font_namespace,
            None => next_namespace_id(),
        }),
        // For the shared-pool case we also share the pool's
        // `SharedFontResources` instance itself: every window's RenderApi
        // and every SB on the pool must observe the same font templates
        // and instance maps. Without this, font registrations on one
        // window aren't visible to scene building for another window
        // (or any window, since the SB has the pool's instance).
        shared_fonts: render_backend_pool.as_ref().map(|p| p.0.fonts.clone()),
        // SWGL doesn't support the GL_ALWAYS depth comparison function used by
        // `clear_caches_with_quads`, but scissored clears work well.
        clear_caches_with_quads: !software && !allow_scissored_cache_clears,
        // SWGL supports KHR_blend_equation_advanced safely, but we haven't yet
        // tested other HW platforms determine if it is safe to allow them.
        allow_advanced_blend_equation: software,
        surface_origin_is_top_left,
        compositor_config,
        enable_gpu_markers,
        picture_tile_size,
        texture_cache_config,
        reject_software_rasterizer,
        low_quality_pinch_zoom,
        max_shared_surface_size,
        enable_dithering,
        enable_yuv_overlay_stability,
        limit_sdr_yuv_external_composites,
        enable_shared_instance_buffer,
        ..Default::default()
    };

    let window_size = DeviceIntSize::new(window_width, window_height);
    let notifier = Box::new(CppNotifier { window_id });
    let backend = GpuBackendConfig::Gl(GlBackendConfig {
        panic_on_error: panic_on_gl_error,
        ..GlBackendConfig::new(gl)
    });
    let (renderer, sender) = match create_webrender_instance(backend, notifier, opts, shaders.map(|sh| &sh.shaders)) {
        Ok((renderer, sender)) => (renderer, sender),
        Err(e) => {
            warn!(" Failed to create a Renderer: {:?}", e);
            let msg = CString::new(format!("wr_window_new: {:?}", e)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            *out_err = msg.into_raw();
            return false;
        },
    };

    unsafe {
        *out_max_texture_size = renderer.get_max_texture_size();
    }
    *out_handle = Box::into_raw(Box::new(DocumentHandle::new(
        sender.create_api_by_client(next_namespace_id()),
        None,
        window_size,
        document_id,
    )));
    *out_renderer = Box::into_raw(Box::new(renderer));

    true
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_free_error_msg(msg: *mut c_char) {
    if !msg.is_null() {
        drop(CString::from_raw(msg));
    }
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_delete_document(dh: &mut DocumentHandle) {
    dh.api.delete_document(dh.document_id);
}

#[no_mangle]
pub extern "C" fn wr_api_clone(dh: &mut DocumentHandle, out_handle: &mut *mut DocumentHandle) {
    assert!(unsafe { is_in_compositor_thread() });

    let hit_tester = dh.ensure_hit_tester().clone();

    let handle = DocumentHandle {
        api: dh.api.create_sender().create_api_by_client(next_namespace_id()),
        document_id: dh.document_id,
        hit_tester: Some(hit_tester),
        hit_tester_request: None,
    };
    *out_handle = Box::into_raw(Box::new(handle));
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_delete(dh: *mut DocumentHandle) {
    let _ = Box::from_raw(dh);
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_stop_render_backend(dh: &mut DocumentHandle) {
    dh.api.stop_render_backend();
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_shut_down(dh: &mut DocumentHandle) {
    dh.api.shut_down(true);
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_notify_memory_pressure(dh: &mut DocumentHandle) {
    dh.api.notify_memory_pressure();
}

#[no_mangle]
pub extern "C" fn wr_api_set_debug_flags(dh: &mut DocumentHandle, flags: DebugFlags) {
    dh.api.set_debug_flags(flags);
}

#[no_mangle]
pub extern "C" fn wr_api_set_bool(dh: &mut DocumentHandle, param_name: BoolParameter, val: bool) {
    dh.api.set_parameter(Parameter::Bool(param_name, val));
}

#[no_mangle]
pub extern "C" fn wr_api_set_int(dh: &mut DocumentHandle, param_name: IntParameter, val: i32) {
    dh.api.set_parameter(Parameter::Int(param_name, val));
}

#[no_mangle]
pub extern "C" fn wr_api_set_float(dh: &mut DocumentHandle, param_name: FloatParameter, val: f32) {
    dh.api.set_parameter(Parameter::Float(param_name, val));
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_accumulate_memory_report(
    dh: &mut DocumentHandle,
    report: &mut MemoryReport,
    // we manually expand VoidPtrToSizeFn here because cbindgen otherwise fails to fold the Option<fn()>
    // https://github.com/eqrion/cbindgen/issues/552
    size_of_op: unsafe extern "C" fn(ptr: *const c_void) -> usize,
    enclosing_size_of_op: Option<unsafe extern "C" fn(ptr: *const c_void) -> usize>,
) {
    let ops = MallocSizeOfOps::new(size_of_op, enclosing_size_of_op);
    *report += dh.api.report_memory(ops);
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_clear_all_caches(dh: &mut DocumentHandle) {
    dh.api.send_debug_cmd(DebugCommand::ClearCaches(ClearCache::all()));
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_set_batching_lookback(dh: &mut DocumentHandle, count: u32) {
    dh.api.send_debug_cmd(DebugCommand::SetBatchingLookback(count));
}

fn make_transaction(do_async: bool) -> Transaction {
    let mut transaction = Transaction::new();
    // Ensure that we either use async scene building or not based on the
    // gecko pref, regardless of what the default is. We can remove this once
    // the scene builder thread is enabled everywhere and working well.
    if do_async {
        transaction.use_scene_builder_thread();
    } else {
        transaction.skip_scene_builder();
    }
    transaction
}

#[no_mangle]
pub extern "C" fn wr_transaction_new(do_async: bool) -> *mut Transaction {
    Box::into_raw(Box::new(make_transaction(do_async)))
}

#[no_mangle]
pub extern "C" fn wr_transaction_delete(txn: *mut Transaction) {
    unsafe {
        let _ = Box::from_raw(txn);
    }
}

#[no_mangle]
pub extern "C" fn wr_transaction_set_low_priority(txn: &mut Transaction, low_priority: bool) {
    txn.set_low_priority(low_priority);
}

#[no_mangle]
pub extern "C" fn wr_transaction_is_empty(txn: &Transaction) -> bool {
    txn.is_empty()
}

#[no_mangle]
pub extern "C" fn wr_transaction_resource_updates_is_empty(txn: &Transaction) -> bool {
    txn.resource_updates.is_empty()
}

#[no_mangle]
pub extern "C" fn wr_transaction_is_rendered_frame_invalidated(txn: &Transaction) -> bool {
    txn.invalidate_rendered_frame
}

#[no_mangle]
pub extern "C" fn wr_transaction_notify(txn: &mut Transaction, when: Checkpoint, event: usize) {
    struct GeckoNotification(usize);
    impl NotificationHandler for GeckoNotification {
        fn notify(&self, when: Checkpoint) {
            unsafe {
                wr_transaction_notification_notified(self.0, when);
            }
        }
    }

    let handler = Box::new(GeckoNotification(event));
    txn.notify(NotificationRequest::new(when, handler));
}

#[no_mangle]
pub extern "C" fn wr_transaction_update_epoch(txn: &mut Transaction, pipeline_id: WrPipelineId, epoch: WrEpoch) {
    txn.update_epoch(pipeline_id, epoch);
}

#[no_mangle]
pub extern "C" fn wr_transaction_set_root_pipeline(txn: &mut Transaction, pipeline_id: WrPipelineId) {
    txn.set_root_pipeline(pipeline_id);
}

#[no_mangle]
pub extern "C" fn wr_transaction_remove_pipeline(txn: &mut Transaction, pipeline_id: WrPipelineId) {
    txn.remove_pipeline(pipeline_id);
}

#[no_mangle]
pub unsafe extern "C" fn wr_transaction_set_embed_policy(
    txn: &mut Transaction,
    pipeline_id: WrPipelineId,
    restricted: bool,
    namespaces: *const u32,
    namespace_count: usize,
) {
    let policy = if restricted {
        webrender::api::EmbedPolicy::Restricted(make_slice(namespaces, namespace_count).to_vec())
    } else {
        webrender::api::EmbedPolicy::Unrestricted
    };
    txn.set_embed_policy(pipeline_id, policy);
}

#[no_mangle]
pub extern "C" fn wr_transaction_set_display_list(
    txn: &mut Transaction,
    epoch: WrEpoch,
    namespace: WrIdNamespace,
    pipeline_id: WrPipelineId,
    dl_descriptor: BuiltDisplayListDescriptor,
    dl_items_data: &mut WrVecU8,
    dl_spatial_tree_data: &mut WrVecU8,
    dl_interner_delta: &mut WrVecU8,
) {
    let payload = DisplayListPayload {
        items_data: dl_items_data.flush_into_vec(),
        spatial_tree: dl_spatial_tree_data.flush_into_vec(),
        interner_delta: dl_interner_delta.flush_into_vec(),
    };

    let dl = BuiltDisplayList::from_data(payload, dl_descriptor);

    txn.set_display_list(epoch, namespace, (pipeline_id, dl));
}

#[no_mangle]
pub extern "C" fn wr_transaction_set_document_view(txn: &mut Transaction, doc_rect: &DeviceIntRect) {
    txn.set_document_view(*doc_rect);
}

#[no_mangle]
pub extern "C" fn wr_transaction_generate_frame(
    txn: &mut Transaction,
    id: u64,
    present: bool,
    tracked: bool,
    reasons: RenderReasons,
) {
    txn.generate_frame(id, present, tracked, reasons);
}

#[no_mangle]
pub extern "C" fn wr_transaction_invalidate_rendered_frame(txn: &mut Transaction, reasons: RenderReasons) {
    txn.invalidate_rendered_frame(reasons);
}

#[no_mangle]
pub extern "C" fn wr_transaction_render_offscreen(txn: &mut Transaction, pipeline_id: WrPipelineId) {
    txn.render_offscreen(pipeline_id);
}

fn wr_animation_properties_into_vec<T>(
    animation_array: *const WrAnimationPropertyValue<T>,
    array_count: usize,
    vec: &mut Vec<PropertyValue<T>>,
) where
    T: Copy,
{
    if array_count > 0 {
        debug_assert!(
            vec.capacity() - vec.len() >= array_count,
            "The Vec should have fufficient free capacity"
        );
        let slice = unsafe { make_slice(animation_array, array_count) };

        for element in slice.iter() {
            let prop = PropertyValue {
                key: PropertyBindingKey::new(element.id),
                value: element.value,
            };

            vec.push(prop);
        }
    }
}

#[no_mangle]
pub extern "C" fn wr_transaction_append_dynamic_properties(
    txn: &mut Transaction,
    opacity_array: *const WrOpacityProperty,
    opacity_count: usize,
    transform_array: *const WrTransformProperty,
    transform_count: usize,
    color_array: *const WrColorProperty,
    color_count: usize,
) {
    if opacity_count == 0 && transform_count == 0 && color_count == 0 {
        return;
    }

    let mut properties = DynamicProperties {
        transforms: Vec::with_capacity(transform_count),
        floats: Vec::with_capacity(opacity_count),
        colors: Vec::with_capacity(color_count),
    };

    wr_animation_properties_into_vec(transform_array, transform_count, &mut properties.transforms);

    wr_animation_properties_into_vec(opacity_array, opacity_count, &mut properties.floats);

    wr_animation_properties_into_vec(color_array, color_count, &mut properties.colors);

    txn.append_dynamic_properties(properties);
}

#[no_mangle]
pub extern "C" fn wr_transaction_append_transform_properties(
    txn: &mut Transaction,
    transform_array: *const WrTransformProperty,
    transform_count: usize,
) {
    if transform_count == 0 {
        return;
    }

    let mut transforms = Vec::with_capacity(transform_count);
    wr_animation_properties_into_vec(transform_array, transform_count, &mut transforms);

    txn.append_dynamic_transform_properties(transforms);
}

#[no_mangle]
pub extern "C" fn wr_transaction_scroll_layer(
    txn: &mut Transaction,
    scroll_id: ExternalScrollId,
    sampled_scroll_offsets: &ThinVec<SampledScrollOffset>,
) {
    txn.set_scroll_offsets(scroll_id, sampled_scroll_offsets.to_vec());
}

#[no_mangle]
pub extern "C" fn wr_transaction_set_is_transform_async_zooming(
    txn: &mut Transaction,
    animation_id: u64,
    is_zooming: bool,
) {
    txn.set_is_transform_async_zooming(is_zooming, PropertyBindingId::new(animation_id));
}

#[no_mangle]
pub extern "C" fn wr_transaction_add_minimap_data(
    txn: &mut Transaction,
    scroll_id: ExternalScrollId,
    minimap_data: MinimapData,
) {
    txn.set_minimap_data(scroll_id, minimap_data);
}

#[no_mangle]
pub extern "C" fn wr_transaction_set_quality_settings(txn: &mut Transaction, force_subpixel_aa_where_possible: bool) {
    txn.set_quality_settings(QualitySettings {
        force_subpixel_aa_where_possible,
    });
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_image(
    txn: &mut Transaction,
    image_key: WrImageKey,
    descriptor: &WrImageDescriptor,
    bytes: &mut WrVecU8,
) {
    txn.add_image(
        image_key,
        descriptor.into(),
        ImageData::new(bytes.flush_into_vec()),
        None,
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_blob_image(
    txn: &mut Transaction,
    image_key: BlobImageKey,
    descriptor: &WrImageDescriptor,
    tile_size: u16,
    bytes: &mut WrVecU8,
    visible_rect: DeviceIntRect,
) {
    // If we're at risk of generating an excessive number of tiles, try making
    // them larger so as to reduce the total number. This helps avoid swamping
    // the Moz2dBlobRasterizer with too many parallel requests.
    const TILE_COUNT_LIMIT: i64 = 8192;
    const TILE_SIZE_LIMIT: u16 = 2048;
    let mut adjusted = tile_size;
    // Rather than some tricky computation involving the image dimensions, just
    // keep doubling tile_size until the estimated count is reasonable, or size
    // gets too big. The size limit means this loop won't execute more than a
    // handful of times even in extreme cases.
    while adjusted < TILE_SIZE_LIMIT
        && ((descriptor.height / adjusted as i32 + 1) as i64 * (descriptor.width / adjusted as i32 + 1) as i64)
            > TILE_COUNT_LIMIT
    {
        adjusted = adjusted * 2;
    }

    txn.add_blob_image(
        image_key,
        descriptor.into(),
        Arc::new(bytes.flush_into_vec()),
        visible_rect,
        if descriptor.format == ImageFormat::BGRA8 || adjusted > tile_size {
            Some(adjusted)
        } else {
            None
        },
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_external_image(
    txn: &mut Transaction,
    image_key: WrImageKey,
    descriptor: &WrImageDescriptor,
    external_image_id: ExternalImageId,
    image_type: &ExternalImageType,
    channel_index: u8,
    normalized_uvs: bool,
) {
    txn.add_image(
        image_key,
        descriptor.into(),
        ImageData::External(ExternalImageData {
            id: external_image_id,
            channel_index,
            image_type: *image_type,
            normalized_uvs,
        }),
        None,
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_update_image(
    txn: &mut Transaction,
    key: WrImageKey,
    descriptor: &WrImageDescriptor,
    bytes: &mut WrVecU8,
) {
    txn.update_image(
        key,
        descriptor.into(),
        ImageData::new(bytes.flush_into_vec()),
        &DirtyRect::All,
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_set_blob_image_visible_area(
    txn: &mut Transaction,
    key: BlobImageKey,
    area: &DeviceIntRect,
) {
    txn.set_blob_image_visible_area(key, *area);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_update_external_image(
    txn: &mut Transaction,
    key: WrImageKey,
    descriptor: &WrImageDescriptor,
    external_image_id: ExternalImageId,
    image_type: &ExternalImageType,
    channel_index: u8,
    normalized_uvs: bool,
) {
    txn.update_image(
        key,
        descriptor.into(),
        ImageData::External(ExternalImageData {
            id: external_image_id,
            channel_index,
            image_type: *image_type,
            normalized_uvs,
        }),
        &DirtyRect::All,
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_update_external_image_with_dirty_rect(
    txn: &mut Transaction,
    key: WrImageKey,
    descriptor: &WrImageDescriptor,
    external_image_id: ExternalImageId,
    image_type: &ExternalImageType,
    channel_index: u8,
    normalized_uvs: bool,
    dirty_rect: DeviceIntRect,
) {
    txn.update_image(
        key,
        descriptor.into(),
        ImageData::External(ExternalImageData {
            id: external_image_id,
            channel_index,
            image_type: *image_type,
            normalized_uvs,
        }),
        &DirtyRect::Partial(dirty_rect),
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_update_blob_image(
    txn: &mut Transaction,
    image_key: BlobImageKey,
    descriptor: &WrImageDescriptor,
    bytes: &mut WrVecU8,
    visible_rect: DeviceIntRect,
    dirty_rect: LayoutIntRect,
) {
    txn.update_blob_image(
        image_key,
        descriptor.into(),
        Arc::new(bytes.flush_into_vec()),
        visible_rect,
        &DirtyRect::Partial(dirty_rect),
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_delete_image(txn: &mut Transaction, key: WrImageKey) {
    txn.delete_image(key);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_delete_blob_image(txn: &mut Transaction, key: BlobImageKey) {
    txn.delete_blob_image(key);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_snapshot_image(txn: &mut Transaction, image_key: SnapshotImageKey) {
    txn.add_snapshot_image(image_key);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_delete_snapshot_image(txn: &mut Transaction, key: SnapshotImageKey) {
    txn.delete_snapshot_image(key);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_adopt_snapshot_image(
    txn: &mut Transaction,
    from: SnapshotImageKey,
    to: SnapshotImageKey,
) {
    txn.adopt_snapshot_image(from, to);
}

#[no_mangle]
pub extern "C" fn wr_api_send_transaction(dh: &mut DocumentHandle, transaction: &mut Transaction, is_async: bool) {
    if transaction.is_empty() {
        return;
    }
    let new_txn = make_transaction(is_async);
    let txn = mem::replace(transaction, new_txn);
    dh.api.send_transaction(dh.document_id, txn);
}

#[no_mangle]
pub unsafe extern "C" fn wr_transaction_clear_display_list(
    txn: &mut Transaction,
    epoch: WrEpoch,
    namespace: WrIdNamespace,
    pipeline_id: WrPipelineId,
) {
    let mut frame_builder = WebRenderFrameBuilder::new(pipeline_id);
    // An empty display list: it holds no coordinates, so the grid is irrelevant.
    frame_builder.dl_builder.begin(60.0);

    txn.set_display_list(epoch, namespace, frame_builder.dl_builder.end());
}

#[no_mangle]
pub extern "C" fn wr_api_send_external_event(dh: &mut DocumentHandle, evt: usize) {
    assert!(unsafe { !is_in_render_thread() });

    dh.api.send_external_event(ExternalEvent::from_raw(evt));
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_raw_font(
    txn: &mut Transaction,
    key: WrFontKey,
    bytes: &mut WrVecU8,
    index: u32,
) {
    txn.add_raw_font(key, bytes.flush_into_vec(), index);
}

fn generate_capture_path(moz_revision: *const c_char) -> Option<PathBuf> {
    use std::fs::{create_dir_all, File};
    use std::io::Write;

    let local_dir = PathBuf::from("wr-capture");

    // On Android we need to write into a particular folder on external
    // storage so that (a) it can be written without requiring permissions
    // and (b) it can be pulled off via `adb pull`. This env var is set
    // in GeckoLoader.java.
    // When running in Firefox CI, the MOZ_UPLOAD_DIR variable is set to a path
    // that taskcluster will export artifacts from, so let's put it there.
    let mut path = if let Ok(storage_path) = env::var("PUBLIC_STORAGE") {
        PathBuf::from(storage_path).join(local_dir)
    } else if let Ok(storage_path) = env::var("MOZ_UPLOAD_DIR") {
        PathBuf::from(storage_path).join(local_dir)
    } else if let Some(storage_path) = dirs::home_dir() {
        storage_path.join(local_dir)
    } else {
        local_dir
    };

    // Increment the extension until we find a fresh path
    while path.is_dir() {
        let count: u32 = path
            .extension()
            .and_then(|x| x.to_str())
            .and_then(|x| x.parse().ok())
            .unwrap_or(0);
        path.set_extension((count + 1).to_string());
    }

    // Use warn! so that it gets emitted to logcat on android as well
    let border = "--------------------------\n";
    warn!("{} Capturing WR state to: {:?}\n{}", &border, &path, &border);

    let _ = create_dir_all(&path);
    match File::create(path.join("wr.txt")) {
        Ok(mut file) => {
            // The Gecko HG revision is available at compile time
            if !moz_revision.is_null() {
                if let Ok(moz_revision) = unsafe { CStr::from_ptr(moz_revision) }.to_str() {
                    writeln!(file, "mozilla-central {}", moz_revision).unwrap()
                }
            }
            Some(path)
        },
        Err(e) => {
            warn!("Unable to create path '{:?}' for capture: {:?}", path, e);
            None
        },
    }
}

#[no_mangle]
pub extern "C" fn wr_api_capture(dh: &mut DocumentHandle, moz_revision: *const c_char, bits_raw: u32) {
    if let Some(path) = generate_capture_path(moz_revision) {
        let bits = CaptureBits::from_bits(bits_raw as _).unwrap();
        dh.api.save_capture(path, bits);
    }
}

#[no_mangle]
pub extern "C" fn wr_api_start_capture_sequence(dh: &mut DocumentHandle, moz_revision: *const c_char, bits_raw: u32) {
    if let Some(path) = generate_capture_path(moz_revision) {
        let bits = CaptureBits::from_bits(bits_raw as _).unwrap();
        dh.api.start_capture_sequence(path, bits);
    }
}

#[no_mangle]
pub extern "C" fn wr_api_stop_capture_sequence(dh: &mut DocumentHandle) {
    let border = "--------------------------\n";
    warn!("{} Stop capturing WR state\n{}", &border, &border);
    dh.api.stop_capture_sequence();
}

#[cfg(target_os = "windows")]
fn read_font_descriptor(bytes: &mut WrVecU8, index: u32) -> NativeFontHandle {
    let wchars: Vec<u16> = bytes
        .as_slice()
        .chunks_exact(2)
        .map(|c| u16::from_ne_bytes([c[0], c[1]]))
        .collect();
    NativeFontHandle {
        path: PathBuf::from(OsString::from_wide(&wchars)),
        index,
    }
}

#[cfg(any(target_os = "macos", target_os = "ios"))]
fn read_font_descriptor(bytes: &mut WrVecU8, index: u32) -> NativeFontHandle {
    // On macOS, the descriptor string is a concatenation of the PostScript name
    // and the font file path (to disambiguate cases where there are multiple
    // faces with the same psname present). The index is the length of the psname
    // portion of the descriptor (= starting offset of the path).
    // Here, we split the descriptor into its two components for further use.
    let chars = bytes.flush_into_vec();
    NativeFontHandle {
        name: String::from_utf8(chars[..index as usize].to_vec()).unwrap_or("".to_string()),
        path: String::from_utf8(chars[index as usize..].to_vec()).unwrap_or("".to_string()),
    }
}

#[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
fn read_font_descriptor(bytes: &mut WrVecU8, index: u32) -> NativeFontHandle {
    let chars = bytes.flush_into_vec();
    NativeFontHandle {
        path: PathBuf::from(OsString::from_vec(chars)),
        index,
    }
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_font_descriptor(
    txn: &mut Transaction,
    key: WrFontKey,
    bytes: &mut WrVecU8,
    index: u32,
) {
    let native_font_handle = read_font_descriptor(bytes, index);
    txn.add_native_font(key, native_font_handle);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_delete_font(txn: &mut Transaction, key: WrFontKey) {
    txn.delete_font(key);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_add_font_instance(
    txn: &mut Transaction,
    key: WrFontInstanceKey,
    font_key: WrFontKey,
    glyph_size: f32,
    options: *const FontInstanceOptions,
    platform_options: *const FontInstancePlatformOptions,
    variations: &mut WrVecU8,
) {
    // Deserialize a sequence of FontVariation objects from the raw bytes.
    // Every FontVariation is 8 bytes: one u32 and one f32.
    // The code below would look better with slice::chunk_arrays:
    // https://github.com/rust-lang/rust/issues/74985
    let variations: Vec<FontVariation> = variations
        .as_slice()
        .chunks_exact(8)
        .map(|c| {
            assert_eq!(c.len(), 8);
            let tag = u32::from_ne_bytes([c[0], c[1], c[2], c[3]]);
            let value = f32::from_ne_bytes([c[4], c[5], c[6], c[7]]);
            FontVariation { tag, value }
        })
        .collect();
    txn.add_font_instance(
        key,
        font_key,
        glyph_size,
        unsafe { options.as_ref().cloned() },
        unsafe { platform_options.as_ref().cloned() },
        variations,
    );
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_delete_font_instance(txn: &mut Transaction, key: WrFontInstanceKey) {
    txn.delete_font_instance(key);
}

#[no_mangle]
pub extern "C" fn wr_resource_updates_clear(txn: &mut Transaction) {
    txn.resource_updates.clear();
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_get_namespace(dh: &mut DocumentHandle) -> WrIdNamespace {
    dh.api.get_namespace_id()
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_wake_scene_builder(dh: &mut DocumentHandle) {
    dh.api.wake_scene_builder();
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_flush_scene_builder(dh: &mut DocumentHandle) {
    dh.api.flush_scene_builder();
}

// RenderThread WIP notes:
// In order to separate the compositor thread (or ipc receiver) and the render
// thread, some of the logic below needs to be rewritten. In particular
// the WrWindowState and Notifier implementations aren't designed to work with
// a separate render thread.
// As part of that I am moving the bindings closer to WebRender's API boundary,
// and moving more of the logic in C++ land.
// This work is tracked by bug 1328602.
//
// See RenderThread.h for some notes about how the pieces fit together.

pub struct WebRenderFrameBuilder {
    pub root_pipeline_id: WrPipelineId,
    pub dl_builder: DisplayListBuilder,
}

impl WebRenderFrameBuilder {
    pub fn new(root_pipeline_id: WrPipelineId) -> WebRenderFrameBuilder {
        WebRenderFrameBuilder {
            root_pipeline_id,
            dl_builder: DisplayListBuilder::new(root_pipeline_id),
        }
    }
}

pub struct WrState {
    pipeline_id: WrPipelineId,
    frame_builder: WebRenderFrameBuilder,
}

#[no_mangle]
pub extern "C" fn wr_state_new(pipeline_id: WrPipelineId) -> *mut WrState {
    assert!(unsafe { !is_in_render_thread() });

    let state = Box::new(WrState {
        pipeline_id,
        frame_builder: WebRenderFrameBuilder::new(pipeline_id),
    });

    Box::into_raw(state)
}

#[no_mangle]
pub extern "C" fn wr_state_delete(state: *mut WrState) {
    assert!(unsafe { !is_in_render_thread() });

    unsafe {
        mem::drop(Box::from_raw(state));
    }
}

#[no_mangle]
pub extern "C" fn wr_dp_save(state: &mut WrState) {
    state.frame_builder.dl_builder.save();
}

#[no_mangle]
pub extern "C" fn wr_dp_restore(state: &mut WrState) {
    state.frame_builder.dl_builder.restore();
}

#[no_mangle]
pub extern "C" fn wr_dp_clear_save(state: &mut WrState) {
    state.frame_builder.dl_builder.clear_save();
}

#[repr(u8)]
#[derive(PartialEq, Eq, Debug)]
pub enum WrReferenceFrameKind {
    Transform,
    Perspective,
}

#[repr(u8)]
#[derive(PartialEq, Eq, Debug)]
pub enum WrRotation {
    Degree0,
    Degree90,
    Degree180,
    Degree270,
}

/// IMPORTANT: If you add fields to this struct, you need to also add initializers
/// for those fields in WebRenderAPI.h.
#[repr(C)]
pub struct WrReferenceFrameParams {
    pub transform: LayoutTransform,
    /// Non-zero if the transform is animated, in which case `transform` is the
    /// value used while the animation is not in effect.
    pub animation_id: u64,
    pub transform_style: TransformStyle,
    pub kind: WrReferenceFrameKind,
    pub is_2d_scale_translation: bool,
    pub should_snap: bool,
    pub paired_with_perspective: bool,
    pub scrolling_relative_to: *const u64,
}

#[no_mangle]
pub extern "C" fn wr_dp_define_reference_frame(
    state: &mut WrState,
    parent_spatial_id: WrSpatialId,
    origin: LayoutPoint,
    params: &WrReferenceFrameParams,
) -> WrSpatialId {
    debug_assert!(unsafe { !is_in_render_thread() });

    let transform = if params.animation_id != 0 {
        PropertyBinding::Binding(PropertyBindingKey::new(params.animation_id), params.transform)
    } else {
        PropertyBinding::Value(params.transform)
    };

    let kind = match params.kind {
        WrReferenceFrameKind::Transform => {
            debug_assert!(params.scrolling_relative_to.is_null());
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: params.is_2d_scale_translation,
                should_snap: params.should_snap,
                paired_with_perspective: params.paired_with_perspective,
            }
        },
        WrReferenceFrameKind::Perspective => ReferenceFrameKind::Perspective {
            scrolling_relative_to: unsafe { params.scrolling_relative_to.as_ref() }
                .map(|scroll_id| ExternalScrollId(*scroll_id, state.pipeline_id)),
        },
    };

    let spatial_id = state.frame_builder.dl_builder.define_reference_frame(
        origin,
        parent_spatial_id.to_webrender(state.pipeline_id),
        params.transform_style,
        transform,
        kind,
    );
    WrSpatialId { id: spatial_id.0 }
}

#[no_mangle]
pub extern "C" fn wr_dp_define_computed_frame(
    state: &mut WrState,
    parent_spatial_id: WrSpatialId,
    origin: LayoutPoint,
    data: &WrComputedTransformData,
) -> WrSpatialId {
    debug_assert!(unsafe { !is_in_render_thread() });

    let rotation = match data.rotation {
        WrRotation::Degree0 => Rotation::Degree0,
        WrRotation::Degree90 => Rotation::Degree90,
        WrRotation::Degree180 => Rotation::Degree180,
        WrRotation::Degree270 => Rotation::Degree270,
    };
    let spatial_id = state.frame_builder.dl_builder.define_computed_frame(
        origin,
        parent_spatial_id.to_webrender(state.pipeline_id),
        Some(data.scale_from),
        data.vertical_flip,
        rotation,
    );
    WrSpatialId { id: spatial_id.0 }
}

/// IMPORTANT: If you add fields to this struct, you need to also add initializers
/// for those fields in WebRenderAPI.h.
#[repr(C)]
pub struct WrStackingContextParams {
    pub clip: WrStackingContextClip,
    /// Only opacity animations; transform animations belong to the reference
    /// frame.
    pub animation: *const WrAnimationProperty,
    pub opacity: *const f32,
    pub snapshot: *const SnapshotInfo,
    pub transform_style: TransformStyle,
    pub prim_flags: PrimitiveFlags,
    pub mix_blend_mode: MixBlendMode,
    pub flags: StackingContextFlags,
}

#[no_mangle]
pub extern "C" fn wr_dp_push_stacking_context(
    state: &mut WrState,
    spatial_id: WrSpatialId,
    params: &WrStackingContextParams,
    filters: *const FilterOp,
    filter_count: usize,
    filter_datas: *const WrFilterData,
    filter_datas_count: usize,
    glyph_raster_space: RasterSpace,
) {
    debug_assert!(unsafe { !is_in_render_thread() });

    let c_filters = unsafe { make_slice(filters, filter_count) };
    let mut filters: Vec<FilterOp> = c_filters.iter().copied().collect();

    let c_filter_datas = unsafe { make_slice(filter_datas, filter_datas_count) };
    let r_filter_datas: Vec<FilterData> = c_filter_datas
        .iter()
        .map(|c_filter_data| FilterData {
            func_r_type: c_filter_data.funcR_type,
            r_values: unsafe { make_slice(c_filter_data.R_values, c_filter_data.R_values_count).to_vec() },
            func_g_type: c_filter_data.funcG_type,
            g_values: unsafe { make_slice(c_filter_data.G_values, c_filter_data.G_values_count).to_vec() },
            func_b_type: c_filter_data.funcB_type,
            b_values: unsafe { make_slice(c_filter_data.B_values, c_filter_data.B_values_count).to_vec() },
            func_a_type: c_filter_data.funcA_type,
            a_values: unsafe { make_slice(c_filter_data.A_values, c_filter_data.A_values_count).to_vec() },
        })
        .collect();

    let opacity_ref = unsafe { params.opacity.as_ref() };
    let mut has_opacity_animation = false;
    let anim = unsafe { params.animation.as_ref() };
    if let Some(anim) = anim {
        debug_assert!(anim.id > 0);
        match anim.effect_type {
            WrAnimationType::Opacity => {
                filters.push(FilterOp::Opacity(
                    PropertyBinding::Binding(
                        PropertyBindingKey::new(anim.id),
                        // We have to set the static opacity value as
                        // the value for the case where the animation is
                        // in not in-effect (e.g. in the delay phase
                        // with no corresponding fill mode).
                        opacity_ref.cloned().unwrap_or(1.0),
                    ),
                    1.0,
                ));
                has_opacity_animation = true;
            },
            _ => unreachable!("{:?} should not be a stacking context animation", anim.effect_type),
        }
    }

    if let Some(opacity) = opacity_ref {
        if !has_opacity_animation && *opacity < 1.0 {
            filters.push(FilterOp::Opacity(PropertyBinding::Value(*opacity), *opacity));
        }
    }

    let wr_clip_id = params.clip.to_webrender(state.pipeline_id);

    state.frame_builder.dl_builder.push_stacking_context(
        spatial_id.to_webrender(state.pipeline_id),
        params.prim_flags,
        wr_clip_id,
        params.transform_style,
        params.mix_blend_mode,
        &filters,
        &r_filter_datas,
        glyph_raster_space,
        params.flags,
        unsafe { params.snapshot.as_ref() }.cloned(),
    );
}

#[no_mangle]
pub extern "C" fn wr_dp_push_debug(state: &mut WrState, val: u32) {
    state.frame_builder.dl_builder.push_debug(val);
}

#[no_mangle]
pub extern "C" fn wr_dp_pop_stacking_context(state: &mut WrState) {
    debug_assert!(unsafe { !is_in_render_thread() });
    state.frame_builder.dl_builder.pop_stacking_context();
}

#[no_mangle]
pub extern "C" fn wr_dp_define_clipchain(
    state: &mut WrState,
    parent_clipchain_id: *const u64,
    clips: *const WrClipId,
    clips_count: usize,
) -> u64 {
    debug_assert!(unsafe { is_in_main_thread() });
    let parent = unsafe { parent_clipchain_id.as_ref() }.map(|id| ClipChainId(*id, state.pipeline_id));

    let pipeline_id = state.pipeline_id;
    let clips = unsafe { make_slice(clips, clips_count) }
        .iter()
        .map(|clip_id| clip_id.to_webrender(pipeline_id));

    let clipchain_id = state.frame_builder.dl_builder.define_clip_chain(parent, clips);
    assert!(clipchain_id.1 == state.pipeline_id);
    clipchain_id.0
}

#[no_mangle]
pub extern "C" fn wr_dp_define_image_mask_clip_with_parent_clip_chain(
    state: &mut WrState,
    space: WrSpatialId,
    mask: ImageMask,
    points: *const LayoutPoint,
    point_count: usize,
    fill_rule: FillRule,
) -> WrClipId {
    debug_assert!(unsafe { is_in_main_thread() });

    let c_points = unsafe { make_slice(points, point_count) };
    let points: Vec<LayoutPoint> = c_points.iter().copied().collect();

    let clip_id = state.frame_builder.dl_builder.define_clip_image_mask(
        space.to_webrender(state.pipeline_id),
        mask,
        &points,
        fill_rule,
    );
    WrClipId::from_webrender(clip_id)
}

#[no_mangle]
pub extern "C" fn wr_dp_define_rounded_rect_clip(
    state: &mut WrState,
    space: WrSpatialId,
    complex: ComplexClipRegion,
    anti_aliased: bool,
) -> WrClipId {
    debug_assert!(unsafe { is_in_main_thread() });

    let space = space.to_webrender(state.pipeline_id);
    let dl_builder = &mut state.frame_builder.dl_builder;
    let clip_id = if anti_aliased {
        dl_builder.define_anti_aliased_clip_rounded_rect(space, complex)
    } else {
        dl_builder.define_clip_rounded_rect(space, complex)
    };
    WrClipId::from_webrender(clip_id)
}

#[no_mangle]
pub extern "C" fn wr_dp_define_border_clip(
    state: &mut WrState,
    space: WrSpatialId,
    rect: LayoutRect,
    widths: LayoutSideOffsets,
    top: BorderSide,
    right: BorderSide,
    bottom: BorderSide,
    left: BorderSide,
    radius: BorderRadius,
    inset: LayoutSideOffsets,
) -> WrClipId {
    debug_assert!(unsafe { is_in_main_thread() });
    let border = NormalBorder {
        top,
        right,
        bottom,
        left,
        radius,
        inset,
        do_aa: true,
    };
    let clip_id =
        state
            .frame_builder
            .dl_builder
            .define_clip_border(space.to_webrender(state.pipeline_id), rect, border, widths);
    WrClipId::from_webrender(clip_id)
}

#[no_mangle]
pub extern "C" fn wr_dp_define_rect_clip(
    state: &mut WrState,
    space: WrSpatialId,
    clip_rect: LayoutRect,
    anti_aliased: bool,
) -> WrClipId {
    debug_assert!(unsafe { is_in_main_thread() });

    let space = space.to_webrender(state.pipeline_id);
    let dl_builder = &mut state.frame_builder.dl_builder;
    let clip_id = if anti_aliased {
        dl_builder.define_anti_aliased_clip_rect(space, clip_rect)
    } else {
        dl_builder.define_clip_rect(space, clip_rect)
    };
    WrClipId::from_webrender(clip_id)
}

/// Define a clip to the inside of a path. The commands and points are in the
/// layout of a `Path`, and the points are relative to `rect`'s origin. `aabb`
/// contains every point, and `rect` also clips the path's content.
#[no_mangle]
pub extern "C" fn wr_dp_define_path_clip(
    state: &mut WrState,
    space: WrSpatialId,
    commands: *const PathCommand,
    command_count: usize,
    points: *const LayoutPoint,
    point_count: usize,
    aabb: LayoutRect,
    rect: LayoutRect,
    fill_rule: FillRule,
) -> WrClipId {
    debug_assert!(unsafe { is_in_main_thread() });

    let commands = unsafe { make_slice(commands, command_count) };
    let points = unsafe { make_slice(points, point_count) };
    let space = space.to_webrender(state.pipeline_id);

    let clip_id = state.frame_builder.dl_builder.define_clip_path(
        space,
        &Path::from_slices(commands, points, aabb),
        rect,
        fill_rule,
    );
    WrClipId::from_webrender(clip_id)
}

#[no_mangle]
pub extern "C" fn wr_dp_get_accumulated_scroll_offset(state: &mut WrState, space: WrSpatialId) -> LayoutVector2D {
    debug_assert!(unsafe { is_in_main_thread() });

    let space = space.to_webrender(state.pipeline_id);
    state.frame_builder.dl_builder.accumulated_scroll_offset_px(space)
}

#[no_mangle]
pub extern "C" fn wr_dp_define_sticky_frame(
    state: &mut WrState,
    parent_spatial_id: WrSpatialId,
    content_rect: LayoutRect,
    top_margin: *const f32,
    right_margin: *const f32,
    bottom_margin: *const f32,
    left_margin: *const f32,
    vertical_bounds: StickyOffsetBounds,
    horizontal_bounds: StickyOffsetBounds,
    applied_offset: LayoutVector2D,
    animation: *const WrAnimationProperty,
) -> WrSpatialId {
    assert!(unsafe { is_in_main_thread() });
    let anim = unsafe { animation.as_ref() };
    let transform = anim.map(|anim| {
        debug_assert!(anim.id > 0);
        match anim.effect_type {
            WrAnimationType::Transform => {
                PropertyBinding::Binding(PropertyBindingKey::new(anim.id), LayoutTransform::identity())
            },
            _ => unreachable!("sticky elements can only have a transform animated"),
        }
    });
    let spatial_id = state.frame_builder.dl_builder.define_sticky_frame(
        parent_spatial_id.to_webrender(state.pipeline_id),
        content_rect,
        SideOffsets2D::new(
            unsafe { top_margin.as_ref() }.cloned(),
            unsafe { right_margin.as_ref() }.cloned(),
            unsafe { bottom_margin.as_ref() }.cloned(),
            unsafe { left_margin.as_ref() }.cloned(),
        ),
        vertical_bounds,
        horizontal_bounds,
        applied_offset,
        transform,
    );

    WrSpatialId { id: spatial_id.0 }
}

#[no_mangle]
pub extern "C" fn wr_dp_define_scroll_layer(
    state: &mut WrState,
    external_scroll_id: u64,
    parent: &WrSpatialId,
    content_rect: LayoutRect,
    clip_rect: LayoutRect,
    scroll_offset: LayoutVector2D,
    scroll_offset_generation: APZScrollGeneration,
    has_scroll_linked_effect: HasScrollLinkedEffect,
) -> WrSpatialId {
    assert!(unsafe { is_in_main_thread() });

    let space_and_clip = state.frame_builder.dl_builder.define_scroll_frame(
        parent.to_webrender(state.pipeline_id),
        ExternalScrollId(external_scroll_id, state.pipeline_id),
        content_rect,
        clip_rect,
        scroll_offset,
        scroll_offset_generation,
        has_scroll_linked_effect,
    );

    WrSpatialId::from_webrender(space_and_clip)
}

#[no_mangle]
pub extern "C" fn wr_dp_push_iframe(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    _is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    pipeline_id: WrPipelineId,
    ignore_missing_pipeline: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    state.frame_builder.dl_builder.push_iframe(
        rect,
        clip,
        &parent.to_webrender(state.pipeline_id),
        pipeline_id,
        ignore_missing_pipeline,
    );
}

// A helper fn to construct a PrimitiveFlags
fn prim_flags(is_backface_visible: bool, prefer_compositor_surface: bool) -> PrimitiveFlags {
    let mut flags = PrimitiveFlags::empty();

    if is_backface_visible {
        flags |= PrimitiveFlags::IS_BACKFACE_VISIBLE;
    }

    if prefer_compositor_surface {
        flags |= PrimitiveFlags::PREFER_COMPOSITOR_SURFACE;
    }

    flags
}

fn prim_flags2(
    is_backface_visible: bool,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) -> PrimitiveFlags {
    let mut flags = PrimitiveFlags::empty();

    if supports_external_compositing {
        flags |= PrimitiveFlags::SUPPORTS_EXTERNAL_COMPOSITOR_SURFACE;
    }

    flags | prim_flags(is_backface_visible, prefer_compositor_surface)
}

fn common_item_properties_for_rect(
    state: &mut WrState,
    clip_rect: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
) -> CommonItemProperties {
    let space_and_clip = parent.to_webrender(state.pipeline_id);

    CommonItemProperties {
        // NB: the damp-e10s talos-test will frequently crash on startup if we
        // early-return here for empty rects. I couldn't figure out why, but
        // it's pretty harmless to feed these through, so, uh, we do?
        clip_rect,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    }
}

#[no_mangle]
pub extern "C" fn wr_dp_push_rect(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    force_antialiasing: bool,
    is_checkerboard: bool,
    parent: &WrSpaceAndClipChain,
    color: ColorF,
) {
    debug_assert!(unsafe { !is_in_render_thread() });

    let mut prim_info = common_item_properties_for_rect(state, clip, is_backface_visible, parent);
    if force_antialiasing {
        prim_info.flags |= PrimitiveFlags::ANTIALISED;
    }
    if is_checkerboard {
        prim_info.flags |= PrimitiveFlags::CHECKERBOARD_BACKGROUND;
    }

    state.frame_builder.dl_builder.push_rect(&prim_info, rect, color);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_rect_with_animation(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    color: ColorF,
    animation: *const WrAnimationProperty,
) {
    debug_assert!(unsafe { !is_in_render_thread() });

    let prim_info = common_item_properties_for_rect(state, clip, is_backface_visible, parent);

    let anim = unsafe { animation.as_ref() };
    if let Some(anim) = anim {
        debug_assert!(anim.id > 0);
        match anim.effect_type {
            WrAnimationType::BackgroundColor => state.frame_builder.dl_builder.push_rect_with_animation(
                &prim_info,
                rect,
                PropertyBinding::Binding(PropertyBindingKey::new(anim.id), color),
            ),
            _ => unreachable!("Didn't expect {:?} animation", anim.effect_type),
        }
    }
}

#[no_mangle]
pub extern "C" fn wr_dp_push_backdrop_filter(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    filters: *const FilterOp,
    filter_count: usize,
    filter_datas: *const WrFilterData,
    filter_datas_count: usize,
) {
    debug_assert!(unsafe { !is_in_render_thread() });

    let c_filters = unsafe { make_slice(filters, filter_count) };
    let filters: Vec<FilterOp> = c_filters.iter().copied().collect();

    let c_filter_datas = unsafe { make_slice(filter_datas, filter_datas_count) };
    let filter_datas: Vec<FilterData> = c_filter_datas
        .iter()
        .map(|c_filter_data| FilterData {
            func_r_type: c_filter_data.funcR_type,
            r_values: unsafe { make_slice(c_filter_data.R_values, c_filter_data.R_values_count).to_vec() },
            func_g_type: c_filter_data.funcG_type,
            g_values: unsafe { make_slice(c_filter_data.G_values, c_filter_data.G_values_count).to_vec() },
            func_b_type: c_filter_data.funcB_type,
            b_values: unsafe { make_slice(c_filter_data.B_values, c_filter_data.B_values_count).to_vec() },
            func_a_type: c_filter_data.funcA_type,
            a_values: unsafe { make_slice(c_filter_data.A_values, c_filter_data.A_values_count).to_vec() },
        })
        .collect();

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let clip_rect = clip.intersection(&rect);
    if clip_rect.is_none() {
        return;
    }

    let prim_info = CommonItemProperties {
        clip_rect: clip_rect.unwrap(),
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_backdrop_filter(&prim_info, &filters, &filter_datas);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_hit_test(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    scroll_id: u64,
    hit_info: u16,
) {
    debug_assert!(unsafe { !is_in_render_thread() });

    let clip_rect = clip.intersection(&rect);
    if clip_rect.is_none() {
        return;
    }
    let tag = (scroll_id, hit_info);

    let spatial_id = parent.space.to_webrender(state.pipeline_id);

    let clip_chain_id = if parent.clip_chain == ROOT_CLIP_CHAIN {
        ClipChainId::INVALID
    } else {
        ClipChainId(parent.clip_chain, state.pipeline_id)
    };

    state.frame_builder.dl_builder.push_hit_test(
        clip_rect.unwrap(),
        clip_chain_id,
        spatial_id,
        prim_flags(is_backface_visible, false),
        tag,
    );
}

#[no_mangle]
pub extern "C" fn wr_dp_push_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    force_antialiasing: bool,
    parent: &WrSpaceAndClipChain,
    image_rendering: ImageRendering,
    key: WrImageKey,
    premultiplied_alpha: bool,
    color: ColorF,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
    rasterized_for_rect: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let mut flags = prim_flags2(
        is_backface_visible,
        prefer_compositor_surface,
        supports_external_compositing,
    );

    if force_antialiasing {
        flags |= PrimitiveFlags::ANTIALISED;
    }
    if rasterized_for_rect {
        flags |= PrimitiveFlags::RASTERIZED_FOR_RECT;
    }

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags,
    };

    let alpha_type = if premultiplied_alpha {
        AlphaType::PremultipliedAlpha
    } else {
        AlphaType::Alpha
    };

    state
        .frame_builder
        .dl_builder
        .push_image(&prim_info, bounds, image_rendering, alpha_type, key, color);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_repeating_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    stretch_size: LayoutSize,
    tile_spacing: LayoutSize,
    image_rendering: ImageRendering,
    key: WrImageKey,
    premultiplied_alpha: bool,
    color: ColorF,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    let alpha_type = if premultiplied_alpha {
        AlphaType::PremultipliedAlpha
    } else {
        AlphaType::Alpha
    };

    state.frame_builder.dl_builder.push_repeating_image(
        &prim_info,
        bounds,
        stretch_size,
        tile_spacing,
        image_rendering,
        alpha_type,
        key,
        color,
    );
}

/// Push a 3 planar yuv image.
#[no_mangle]
pub extern "C" fn wr_dp_push_yuv_planar_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    image_key_0: WrImageKey,
    image_key_1: WrImageKey,
    image_key_2: WrImageKey,
    color_depth: WrColorDepth,
    color_space: WrYuvColorSpace,
    color_range: WrColorRange,
    image_rendering: ImageRendering,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags2(
            is_backface_visible,
            prefer_compositor_surface,
            supports_external_compositing,
        ),
    };

    state.frame_builder.dl_builder.push_yuv_image(
        &prim_info,
        bounds,
        YuvData::PlanarYCbCr(image_key_0, image_key_1, image_key_2),
        color_depth,
        color_space,
        color_range,
        image_rendering,
    );
}

/// Push a 2 planar NV12 image.
#[no_mangle]
pub extern "C" fn wr_dp_push_yuv_NV12_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    image_key_0: WrImageKey,
    image_key_1: WrImageKey,
    color_depth: WrColorDepth,
    color_space: WrYuvColorSpace,
    color_range: WrColorRange,
    image_rendering: ImageRendering,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags2(
            is_backface_visible,
            prefer_compositor_surface,
            supports_external_compositing,
        ),
    };

    state.frame_builder.dl_builder.push_yuv_image(
        &prim_info,
        bounds,
        YuvData::NV12(image_key_0, image_key_1),
        color_depth,
        color_space,
        color_range,
        image_rendering,
    );
}

/// Push a 2 planar P010 image.
#[no_mangle]
pub extern "C" fn wr_dp_push_yuv_P010_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    image_key_0: WrImageKey,
    image_key_1: WrImageKey,
    color_depth: WrColorDepth,
    color_space: WrYuvColorSpace,
    color_range: WrColorRange,
    image_rendering: ImageRendering,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags2(
            is_backface_visible,
            prefer_compositor_surface,
            supports_external_compositing,
        ),
    };

    state.frame_builder.dl_builder.push_yuv_image(
        &prim_info,
        bounds,
        YuvData::P010(image_key_0, image_key_1),
        color_depth,
        color_space,
        color_range,
        image_rendering,
    );
}

/// Push a 2 planar NV16 image.
#[no_mangle]
pub extern "C" fn wr_dp_push_yuv_NV16_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    image_key_0: WrImageKey,
    image_key_1: WrImageKey,
    color_depth: WrColorDepth,
    color_space: WrYuvColorSpace,
    color_range: WrColorRange,
    image_rendering: ImageRendering,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags2(
            is_backface_visible,
            prefer_compositor_surface,
            supports_external_compositing,
        ),
    };

    state.frame_builder.dl_builder.push_yuv_image(
        &prim_info,
        bounds,
        YuvData::NV16(image_key_0, image_key_1),
        color_depth,
        color_space,
        color_range,
        image_rendering,
    );
}

/// Push a 2 planar P210 image.
#[no_mangle]
pub extern "C" fn wr_dp_push_yuv_P210_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    image_key_0: WrImageKey,
    image_key_1: WrImageKey,
    color_depth: WrColorDepth,
    color_space: WrYuvColorSpace,
    color_range: WrColorRange,
    image_rendering: ImageRendering,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags2(
            is_backface_visible,
            prefer_compositor_surface,
            supports_external_compositing,
        ),
    };

    state.frame_builder.dl_builder.push_yuv_image(
        &prim_info,
        bounds,
        YuvData::P210(image_key_0, image_key_1),
        color_depth,
        color_space,
        color_range,
        image_rendering,
    );
}

/// Push a yuv interleaved image.
#[no_mangle]
pub extern "C" fn wr_dp_push_yuv_interleaved_image(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    image_key_0: WrImageKey,
    color_depth: WrColorDepth,
    color_space: WrYuvColorSpace,
    color_range: WrColorRange,
    image_rendering: ImageRendering,
    prefer_compositor_surface: bool,
    supports_external_compositing: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() || is_in_compositor_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags2(
            is_backface_visible,
            prefer_compositor_surface,
            supports_external_compositing,
        ),
    };

    state.frame_builder.dl_builder.push_yuv_image(
        &prim_info,
        bounds,
        YuvData::InterleavedYCbCr(image_key_0),
        color_depth,
        color_space,
        color_range,
        image_rendering,
    );
}

#[no_mangle]
pub extern "C" fn wr_dp_push_text(
    state: &mut WrState,
    bounds: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    color: ColorF,
    font_key: WrFontInstanceKey,
    glyphs: *const GlyphInstance,
    glyph_count: u32,
    glyph_options: *const GlyphOptions,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let glyph_slice = unsafe { make_slice(glyphs, glyph_count as usize) };

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        spatial_id: space_and_clip.spatial_id,
        clip_chain_id: space_and_clip.clip_chain_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_text(&prim_info, bounds, &glyph_slice, font_key, color, unsafe {
            glyph_options.as_ref().cloned()
        });
}

#[no_mangle]
pub extern "C" fn wr_dp_push_shadow(
    state: &mut WrState,
    _bounds: LayoutRect,
    _clip: LayoutRect,
    _is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    shadow: Shadow,
    should_inflate: bool,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    state
        .frame_builder
        .dl_builder
        .push_shadow(&parent.to_webrender(state.pipeline_id), shadow, should_inflate);
}

#[no_mangle]
pub extern "C" fn wr_dp_pop_all_shadows(state: &mut WrState) {
    debug_assert!(unsafe { is_in_main_thread() });

    state.frame_builder.dl_builder.pop_all_shadows();
}

#[no_mangle]
pub extern "C" fn wr_dp_push_line(
    state: &mut WrState,
    clip: &LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    bounds: &LayoutRect,
    wavy_line_thickness: f32,
    orientation: LineOrientation,
    color: &ColorF,
    style: LineStyle,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: *clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_line(&prim_info, bounds, wavy_line_thickness, orientation, color, style);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_border(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    force_antialiasing: bool,
    parent: &WrSpaceAndClipChain,
    do_aa: AntialiasBorder,
    widths: LayoutSideOffsets,
    top: BorderSide,
    right: BorderSide,
    bottom: BorderSide,
    left: BorderSide,
    radius: BorderRadius,
    inset: LayoutSideOffsets,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let border_details = BorderDetails::Normal(NormalBorder {
        left,
        right,
        top,
        bottom,
        radius,
        inset,
        do_aa: do_aa == AntialiasBorder::Yes,
    });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let mut prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };
    if force_antialiasing {
        prim_info.flags |= PrimitiveFlags::ANTIALISED;
    }

    state
        .frame_builder
        .dl_builder
        .push_border(&prim_info, rect, widths, border_details, &[]);
}

#[repr(C)]
pub struct WrBorderImage {
    widths: LayoutSideOffsets,
    image: WrImageKey,
    image_rendering: ImageRendering,
    width: i32,
    height: i32,
    fill: bool,
    slice: DeviceIntSideOffsets,
    repeat_horizontal: RepeatMode,
    repeat_vertical: RepeatMode,
}

#[no_mangle]
pub extern "C" fn wr_dp_push_border_image(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    params: &WrBorderImage,
) {
    debug_assert!(unsafe { is_in_main_thread() });
    let border_details = BorderDetails::NinePatch(NinePatchBorder {
        source: NinePatchBorderSource::Image(params.image, params.image_rendering),
        width: params.width,
        height: params.height,
        slice: params.slice,
        fill: params.fill,
        repeat_horizontal: params.repeat_horizontal,
        repeat_vertical: params.repeat_vertical,
    });
    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_border(&prim_info, rect, params.widths, border_details, &[]);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_border_gradient(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    widths: LayoutSideOffsets,
    width: i32,
    height: i32,
    fill: bool,
    slice: DeviceIntSideOffsets,
    start_point: LayoutVector2D,
    end_point: LayoutVector2D,
    stops: *const GradientStop,
    stops_count: usize,
    extend_mode: ExtendMode,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let stops_slice = unsafe { make_slice(stops, stops_count) };
    let stops_vector = stops_slice.to_owned();

    let (gradient, stops_vector) = state.frame_builder.dl_builder.create_gradient(
        start_point,
        end_point,
        stops_vector,
        extend_mode,
        AlphaMode::Premultiplied,
    );

    let border_details = BorderDetails::NinePatch(NinePatchBorder {
        source: NinePatchBorderSource::Gradient(gradient),
        width,
        height,
        slice,
        fill,
        repeat_horizontal: RepeatMode::Stretch,
        repeat_vertical: RepeatMode::Stretch,
    });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_border(&prim_info, rect, widths, border_details, &stops_vector);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_border_radial_gradient(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    widths: LayoutSideOffsets,
    fill: bool,
    center: LayoutVector2D,
    radius: LayoutSize,
    stops: *const GradientStop,
    stops_count: usize,
    extend_mode: ExtendMode,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let stops_slice = unsafe { make_slice(stops, stops_count) };
    let stops_vector = stops_slice.to_owned();

    let slice = SideOffsets2D::new(
        widths.top as i32,
        widths.right as i32,
        widths.bottom as i32,
        widths.left as i32,
    );

    let (gradient, stops_vector) = state.frame_builder.dl_builder.create_radial_gradient(
        center,
        radius,
        stops_vector,
        extend_mode,
        AlphaMode::Premultiplied,
    );

    let border_details = BorderDetails::NinePatch(NinePatchBorder {
        source: NinePatchBorderSource::RadialGradient(gradient),
        width: rect.width() as i32,
        height: rect.height() as i32,
        slice,
        fill,
        repeat_horizontal: RepeatMode::Stretch,
        repeat_vertical: RepeatMode::Stretch,
    });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_border(&prim_info, rect, widths, border_details, &stops_vector);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_border_conic_gradient(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    widths: LayoutSideOffsets,
    fill: bool,
    center: LayoutVector2D,
    angle: f32,
    stops: *const GradientStop,
    stops_count: usize,
    extend_mode: ExtendMode,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let stops_slice = unsafe { make_slice(stops, stops_count) };
    let stops_vector = stops_slice.to_owned();

    let slice = SideOffsets2D::new(
        widths.top as i32,
        widths.right as i32,
        widths.bottom as i32,
        widths.left as i32,
    );

    let (gradient, stops_vector) = state.frame_builder.dl_builder.create_conic_gradient(
        center,
        angle,
        stops_vector,
        extend_mode,
        AlphaMode::Premultiplied,
    );

    let border_details = BorderDetails::NinePatch(NinePatchBorder {
        source: NinePatchBorderSource::ConicGradient(gradient),
        width: rect.width() as i32,
        height: rect.height() as i32,
        slice,
        fill,
        repeat_horizontal: RepeatMode::Stretch,
        repeat_vertical: RepeatMode::Stretch,
    });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state
        .frame_builder
        .dl_builder
        .push_border(&prim_info, rect, widths, border_details, &stops_vector);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_linear_gradient(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    force_antialiasing: bool,
    parent: &WrSpaceAndClipChain,
    start: LayoutVector2D,
    end: LayoutVector2D,
    stops: *const GradientStop,
    stops_count: usize,
    extend_mode: ExtendMode,
    alpha_mode: AlphaMode,
    tile_size: LayoutSize,
    tile_spacing: LayoutSize,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let stops_slice = unsafe { make_slice(stops, stops_count) };
    let stops_vector = stops_slice.to_owned();

    let (gradient, stops_vector) =
        state
            .frame_builder
            .dl_builder
            .create_gradient(start, end, stops_vector, extend_mode, alpha_mode);

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let mut prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };
    if force_antialiasing {
        prim_info.flags |= PrimitiveFlags::ANTIALISED;
    }

    state
        .frame_builder
        .dl_builder
        .push_gradient(&prim_info, rect, gradient, tile_size, tile_spacing, &stops_vector);
}

#[no_mangle]
pub extern "C" fn wr_dp_push_radial_gradient(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    force_antialiasing: bool,
    parent: &WrSpaceAndClipChain,
    center: LayoutVector2D,
    radius: LayoutSize,
    stops: *const GradientStop,
    stops_count: usize,
    extend_mode: ExtendMode,
    alpha_mode: AlphaMode,
    tile_size: LayoutSize,
    tile_spacing: LayoutSize,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let stops_slice = unsafe { make_slice(stops, stops_count) };
    let stops_vector = stops_slice.to_owned();

    let (gradient, stops_vector) =
        state
            .frame_builder
            .dl_builder
            .create_radial_gradient(center, radius, stops_vector, extend_mode, alpha_mode);

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let mut prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };
    if force_antialiasing {
        prim_info.flags |= PrimitiveFlags::ANTIALISED;
    }

    state.frame_builder.dl_builder.push_radial_gradient(
        &prim_info,
        rect,
        gradient,
        tile_size,
        tile_spacing,
        &stops_vector,
    );
}

#[no_mangle]
pub extern "C" fn wr_dp_push_conic_gradient(
    state: &mut WrState,
    rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    center: LayoutVector2D,
    angle: f32,
    stops: *const GradientStop,
    stops_count: usize,
    extend_mode: ExtendMode,
    alpha_mode: AlphaMode,
    tile_size: LayoutSize,
    tile_spacing: LayoutSize,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let stops_slice = unsafe { make_slice(stops, stops_count) };
    let stops_vector = stops_slice.to_owned();

    let (gradient, stops_vector) =
        state
            .frame_builder
            .dl_builder
            .create_conic_gradient(center, angle, stops_vector, extend_mode, alpha_mode);

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state.frame_builder.dl_builder.push_conic_gradient(
        &prim_info,
        rect,
        gradient,
        tile_size,
        tile_spacing,
        &stops_vector,
    );
}

#[no_mangle]
pub extern "C" fn wr_dp_push_box_shadow(
    state: &mut WrState,
    _rect: LayoutRect,
    clip: LayoutRect,
    is_backface_visible: bool,
    parent: &WrSpaceAndClipChain,
    box_bounds: LayoutRect,
    offset: LayoutVector2D,
    color: ColorF,
    blur_radius: f32,
    spread_radius: f32,
    border_radius: BorderRadius,
    shadow_radius: BorderRadius,
    clip_mode: BoxShadowClipMode,
) {
    debug_assert!(unsafe { is_in_main_thread() });

    let space_and_clip = parent.to_webrender(state.pipeline_id);

    let prim_info = CommonItemProperties {
        clip_rect: clip,
        clip_chain_id: space_and_clip.clip_chain_id,
        spatial_id: space_and_clip.spatial_id,
        flags: prim_flags(is_backface_visible, /* prefer_compositor_surface */ false),
    };

    state.frame_builder.dl_builder.push_box_shadow(
        &prim_info,
        box_bounds,
        offset,
        color,
        blur_radius,
        spread_radius,
        border_radius,
        shadow_radius,
        clip_mode,
    );
}

#[no_mangle]
pub extern "C" fn wr_dump_display_list(
    state: &mut WrState,
    indent: usize,
    start: *const usize,
    end: *const usize,
) -> usize {
    let start = unsafe { start.as_ref().cloned() };
    let end = unsafe { end.as_ref().cloned() };
    let range = Range { start, end };
    let mut sink = Cursor::new(Vec::new());
    let index = state
        .frame_builder
        .dl_builder
        .emit_display_list(indent, range, &mut sink);

    // For Android, dump to logcat instead of stderr. This is the same as
    // what printf_stderr does on the C++ side.

    #[cfg(target_os = "android")]
    unsafe {
        let gecko = CString::new("Gecko").unwrap();
        let sink = CString::new(sink.into_inner()).unwrap();
        __android_log_write(4 /* info */, gecko.as_ptr(), sink.as_ptr());
    }

    #[cfg(not(target_os = "android"))]
    eprint!("{}", String::from_utf8(sink.into_inner()).unwrap());

    index
}

#[no_mangle]
pub extern "C" fn wr_dump_serialized_display_list(state: &mut WrState) {
    state.frame_builder.dl_builder.dump_serialized_display_list();
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_begin_builder(state: &mut WrState, au_per_dev_px: i32) {
    state.frame_builder.dl_builder.begin(au_per_dev_px as f32);
}

#[no_mangle]
pub unsafe extern "C" fn wr_api_end_builder(
    state: &mut WrState,
    dl_descriptor: &mut BuiltDisplayListDescriptor,
    dl_items_data: &mut WrVecU8,
    dl_spatial_tree: &mut WrVecU8,
    dl_interner_delta: &mut WrVecU8,
) {
    let (_, dl) = state.frame_builder.dl_builder.end();
    let (payload, descriptor) = dl.into_data();
    *dl_items_data = WrVecU8::from_vec(payload.items_data);
    *dl_spatial_tree = WrVecU8::from_vec(payload.spatial_tree);
    *dl_interner_delta = WrVecU8::from_vec(payload.interner_delta);
    *dl_descriptor = descriptor;
}

#[repr(C)]
pub struct HitResult {
    pipeline_id: WrPipelineId,
    scroll_id: u64,
    animation_id: u64,
    hit_info: u16,
}

#[no_mangle]
pub extern "C" fn wr_api_hit_test(dh: &mut DocumentHandle, point: WorldPoint, out_results: &mut ThinVec<HitResult>) {
    let result = dh.ensure_hit_tester().hit_test(point);
    for item in &result.items {
        out_results.push(HitResult {
            pipeline_id: item.pipeline,
            scroll_id: item.tag.0,
            animation_id: item.animation_id,
            hit_info: item.tag.1,
        });
    }
}

pub type VecU8 = Vec<u8>;
pub type ArcVecU8 = Arc<VecU8>;

#[no_mangle]
pub extern "C" fn wr_add_ref_arc(arc: &ArcVecU8) -> *const VecU8 {
    Arc::into_raw(arc.clone())
}

#[no_mangle]
pub unsafe extern "C" fn wr_dec_ref_arc(arc: *const VecU8) {
    Arc::from_raw(arc);
}

// TODO: nical
// Update for the new blob image interface changes.
//
extern "C" {
    // TODO: figure out the API for tiled blob images.
    pub fn wr_moz2d_render_cb(
        blob: ByteSlice,
        id_namespace: WrIdNamespace,
        format: ImageFormat,
        render_rect: &LayoutIntRect,
        visible_rect: &DeviceIntRect,
        tile_size: u16,
        tile_offset: &TileOffset,
        dirty_rect: Option<&LayoutIntRect>,
        output: MutByteSlice,
    ) -> bool;
}

#[no_mangle]
pub extern "C" fn wr_root_scroll_node_id() -> WrSpatialId {
    // The PipelineId doesn't matter here, since we just want the numeric part of the id
    // produced for any given root reference frame.
    WrSpatialId {
        id: SpatialId::root_scroll_node(PipelineId(0, 0)).0,
    }
}

#[no_mangle]
pub extern "C" fn wr_root_clip_id() -> WrClipId {
    // The PipelineId doesn't matter here, since we just want the numeric part of the id
    // produced for any given root reference frame.
    WrClipId::from_webrender(ClipId::root(PipelineId(0, 0)))
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WrClipId {
    id: usize,
}

impl WrClipId {
    fn to_webrender(&self, pipeline_id: WrPipelineId) -> ClipId {
        ClipId(self.id, pipeline_id)
    }

    fn from_webrender(clip_id: ClipId) -> Self {
        WrClipId { id: clip_id.0 }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WrSpatialId {
    id: usize,
}

impl WrSpatialId {
    fn to_webrender(&self, pipeline_id: WrPipelineId) -> SpatialId {
        SpatialId::new(self.id, pipeline_id)
    }

    fn from_webrender(id: SpatialId) -> Self {
        WrSpatialId { id: id.0 }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wr_device_delete(device: *mut Device) {
    mem::drop(Box::from_raw(device));
}

// Call MakeCurrent before this.
#[no_mangle]
pub extern "C" fn wr_shaders_new(
    gl_context: *mut c_void,
    program_cache: Option<&mut WrProgramCache>,
    precache_shaders: bool,
) -> *mut WrShaders {
    let mut device = wr_device_new(gl_context, program_cache);

    device.begin_frame();

    let mut options = WebRenderOptions::default();
    options.enable_dithering = static_prefs::pref!("gfx.webrender.dithering");

    let mut shaders = match Shaders::new(&mut device, &options) {
        Ok(shaders) => shaders,
        Err(e) => {
            warn!(" Failed to create a Shaders: {:?}", e);
            let msg = CString::new(format!("wr_shaders_new: {:?}", e)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }
            return ptr::null_mut();
        },
    };

    device.end_frame();

    let precache_flags = if precache_shaders || env_var_to_bool("MOZ_WR_PRECACHE_SHADERS") {
        ShaderPrecacheFlags::FULL_COMPILE
    } else {
        ShaderPrecacheFlags::ASYNC_COMPILE
    };

    let shaders_to_precache = shaders.precache_all(precache_flags);

    let shaders = WrShaders {
        shaders: Rc::new(RefCell::new(shaders)),
        shaders_to_precache,
        device,
    };

    Box::into_raw(Box::new(shaders))
}

#[no_mangle]
pub unsafe extern "C" fn wr_shaders_delete(shaders: *mut WrShaders) {
    // Deallocate the box by moving the values out of it.
    let WrShaders {
        shaders, mut device, ..
    } = *Box::from_raw(shaders);
    if let Ok(shaders) = Rc::try_unwrap(shaders) {
        shaders.into_inner().deinit(&mut device);
    }
}

/// Perform one step of shader warmup.
///
/// Returns true if another call is needed, false if warmup is finished.
#[no_mangle]
pub extern "C" fn wr_shaders_resume_warmup(shaders: &mut WrShaders) -> bool {
    shaders.device.begin_frame();

    let need_another_call = match shaders
        .shaders
        .borrow_mut()
        .resume_precache(&mut shaders.device, &mut shaders.shaders_to_precache)
    {
        Ok(need_another_call) => need_another_call,
        Err(e) => {
            warn!(" Failed to create a shader during warmup: {:?}", e);
            let msg = CString::new(format!("wr_shaders_resume_warmup: {:?}", e)).unwrap();
            unsafe {
                gfx_critical_note(msg.as_ptr());
            }

            // Don't ask for another call to resume warmup; if one shader failed
            // to compile it's likely that we will run into similar errors with
            // the rest of the shaders.
            false
        },
    };

    shaders.device.end_frame();

    need_another_call
}

#[no_mangle]
pub unsafe extern "C" fn wr_program_cache_report_memory(
    cache: *const WrProgramCache,
    size_of_op: VoidPtrToSizeFn,
) -> usize {
    (*cache).program_cache.report_memory(size_of_op)
}
