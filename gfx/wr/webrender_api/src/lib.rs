/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! The `webrender_api` crate contains an assortment types and functions used
//! by WebRender consumers as well as, in many cases, WebRender itself.
//!
//! This separation allows Servo to parallelize compilation across `webrender`
//! and other crates that depend on `webrender_api`. So in practice, we put
//! things in this crate when Servo needs to use them. Firefox depends on the
//! `webrender` crate directly, and so this distinction is not really relevant
//! there.

#![cfg_attr(feature = "nightly", feature(nonzero))]
#![allow(
    clippy::float_cmp,
    clippy::too_many_arguments,
    clippy::unreadable_literal,
    clippy::new_without_default,
    clippy::empty_docs,
    clippy::manual_range_contains,
)]


pub extern crate crossbeam_channel;
pub extern crate euclid;

extern crate app_units;
#[macro_use]
extern crate bitflags;
extern crate byteorder;
#[cfg(feature = "nightly")]
extern crate core;
#[macro_use]
extern crate malloc_size_of_derive;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate time;

extern crate malloc_size_of;
extern crate peek_poke;

pub mod channel;
mod color;
mod display_item;
mod display_item_cache;
mod display_list;
mod font;
mod gradient_builder;
mod image;
mod tile_pool;
pub mod units;

pub use crate::color::*;
pub use crate::display_item::*;
pub use crate::display_item_cache::DisplayItemCache;
pub use crate::display_list::*;
pub use crate::font::*;
pub use crate::gradient_builder::*;
pub use crate::image::*;
pub use crate::tile_pool::*;

use crate::units::*;
use crate::channel::Receiver;
use std::marker::PhantomData;
use std::sync::Arc;
use std::os::raw::c_void;
use peek_poke::PeekPoke;

/// Defined here for cbindgen
pub const MAX_RENDER_TASK_SIZE: i32 = 16384;

/// Width and height in device pixels of image tiles.
pub type TileSize = u16;

/// Various settings that the caller can select based on desired tradeoffs
/// between rendering quality and performance / power usage.
#[derive(Copy, Clone, Deserialize, Serialize)]
pub struct QualitySettings {
    /// If true, disable creating separate picture cache slices when the
    /// scroll root changes. This gives maximum opportunity to find an
    /// opaque background, which enables subpixel AA. However, it is
    /// usually significantly more expensive to render when scrolling.
    pub force_subpixel_aa_where_possible: bool,
}

impl Default for QualitySettings {
    fn default() -> Self {
        QualitySettings {
            // Prefer performance over maximum subpixel AA quality, since WR
            // already enables subpixel AA in more situations than other browsers.
            force_subpixel_aa_where_possible: false,
        }
    }
}

/// An epoch identifies the state of a pipeline in time.
///
/// This is mostly used as a synchronization mechanism to observe how/when particular pipeline
/// updates propagate through WebRender and are applied at various stages.
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize)]
pub struct Epoch(pub u32);

impl Epoch {
    /// Magic invalid epoch value.
    pub fn invalid() -> Epoch {
        Epoch(u32::MAX)
    }
}

/// ID namespaces uniquely identify different users of WebRender's API.
///
/// For example in Gecko each content process uses a separate id namespace.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, MallocSizeOf, PartialEq, Hash, Ord, PartialOrd, PeekPoke)]
#[derive(Deserialize, Serialize)]
pub struct IdNamespace(pub u32);

/// A key uniquely identifying a WebRender document.
///
/// Instances can manage one or several documents (using the same render backend thread).
/// Each document will internally correspond to a single scene, and scenes are made of
/// one or several pipelines.
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct DocumentId {
    ///
    pub namespace_id: IdNamespace,
    ///
    pub id: u32,
}

impl DocumentId {
    ///
    pub fn new(namespace_id: IdNamespace, id: u32) -> Self {
        DocumentId {
            namespace_id,
            id,
        }
    }

    ///
    pub const INVALID: DocumentId = DocumentId { namespace_id: IdNamespace(0), id: 0 };
}

/// This type carries no valuable semantics for WR. However, it reflects the fact that
/// clients (Servo) may generate pipelines by different semi-independent sources.
/// These pipelines still belong to the same `IdNamespace` and the same `DocumentId`.
/// Having this extra Id field enables them to generate `PipelineId` without collision.
pub type PipelineSourceId = u32;

/// From the point of view of WR, `PipelineId` is completely opaque and generic as long as
/// it's clonable, serializable, comparable, and hashable.
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct PipelineId(pub PipelineSourceId, pub u32);

impl Default for PipelineId {
    fn default() -> Self {
        PipelineId::dummy()
    }
}

impl PipelineId {
    ///
    pub fn dummy() -> Self {
        PipelineId(!0, !0)
    }

    pub const INVALID: Self = PipelineId(!0, !0);
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, PartialOrd)]
pub struct FramePublishId(pub u64);

impl FramePublishId {
    /// Returns a FramePublishId corresponding to the first frame.
    ///
    /// Note that we use 0 as the internal id here because the current code
    /// increments the frame publish id just before ResultMsg::PublishDocument,
    /// and we want the first id to be 1.
    pub fn first() -> Self {
        FramePublishId(0)
    }

    /// Advances this FramePublishId to the next.
    pub fn advance(&mut self) {
        self.0 += 1;
    }

    /// An invalid sentinel FramePublishId, which will always compare less than
    /// any valid FrameId.
    pub const INVALID: Self = FramePublishId(0);
}

impl Default for FramePublishId {
    fn default() -> Self {
        FramePublishId::INVALID
    }
}

/// An opaque pointer-sized value.
#[repr(C)]
#[derive(Clone)]
pub struct ExternalEvent {
    raw: usize,
}

unsafe impl Send for ExternalEvent {}

impl ExternalEvent {
    /// Creates the event from an opaque pointer-sized value.
    pub fn from_raw(raw: usize) -> Self {
        ExternalEvent { raw }
    }
    /// Consumes self to make it obvious that the event should be forwarded only once.
    pub fn unwrap(self) -> usize {
        self.raw
    }
}

pub type APZScrollGeneration = u64;
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, Default)]
pub struct SampledScrollOffset {
    pub offset: LayoutVector2D,
    pub generation: APZScrollGeneration,
}

/// A flag in each scrollable frame to represent whether the owner of the frame document
/// has any scroll-linked effect.
/// See https://firefox-source-docs.mozilla.org/performance/scroll-linked_effects.html
/// for a definition of scroll-linked effect.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum HasScrollLinkedEffect {
    Yes,
    #[default]
    No,
}

#[repr(C)]
pub struct MinimapData {
  pub is_root_content: bool,
  // All rects in local coords relative to the scrolled content's origin.
  pub visual_viewport: LayoutRect,
  pub layout_viewport: LayoutRect,
  pub scrollable_rect: LayoutRect,
  pub displayport: LayoutRect,
  // Populated for root content nodes only, otherwise the identity
  pub zoom_transform: LayoutTransform,
  // Populated for nodes in the subtree of a root content node
  // (outside such subtrees we'll have `root_content_scroll_id == 0`).
  // Stores the enclosing root content node's ExternalScrollId.
  pub root_content_pipeline_id: PipelineId,
  pub root_content_scroll_id: u64
}

#[repr(C)]
pub struct FrameReadyParams {
    pub present: bool,
    pub render: bool,
    pub scrolled: bool,
}

/// A handler to integrate WebRender with the thread that contains the `Renderer`.
pub trait RenderNotifier: Send {
    ///
    fn clone(&self) -> Box<dyn RenderNotifier>;
    /// Wake the thread containing the `Renderer` up (after updates have been put
    /// in the renderer's queue).
    fn wake_up(
        &self,
        composite_needed: bool,
    );
    /// Notify the thread containing the `Renderer` that a new frame is ready.
    fn new_frame_ready(&self, _: DocumentId, publish_id: FramePublishId, params: &FrameReadyParams);
    /// A Gecko-specific notification mechanism to get some code executed on the
    /// `Renderer`'s thread, mostly replaced by `NotificationHandler`. You should
    /// probably use the latter instead.
    fn external_event(&self, _evt: ExternalEvent) {
        unimplemented!()
    }
    /// Notify the thread containing the `Renderer` that the render backend has been
    /// shut down.
    fn shut_down(&self) {}
}

/// A stage of the rendering pipeline.
#[repr(u32)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Checkpoint {
    ///
    SceneBuilt,
    ///
    FrameBuilt,
    ///
    FrameTexturesUpdated,
    ///
    FrameRendered,
    /// NotificationRequests get notified with this if they get dropped without having been
    /// notified. This provides the guarantee that if a request is created it will get notified.
    TransactionDropped,
}

/// A handler to notify when a transaction reaches certain stages of the rendering
/// pipeline.
pub trait NotificationHandler : Send + Sync {
    /// Entry point of the handler to implement. Invoked by WebRender.
    fn notify(&self, when: Checkpoint);
}

/// A request to notify a handler when the transaction reaches certain stages of the
/// rendering pipeline.
///
/// The request is guaranteed to be notified once and only once, even if the transaction
/// is dropped before the requested check-point.
pub struct NotificationRequest {
    handler: Option<Box<dyn NotificationHandler>>,
    when: Checkpoint,
}

impl NotificationRequest {
    /// Constructor.
    pub fn new(when: Checkpoint, handler: Box<dyn NotificationHandler>) -> Self {
        NotificationRequest {
            handler: Some(handler),
            when,
        }
    }

    /// The specified stage at which point the handler should be notified.
    pub fn when(&self) -> Checkpoint { self.when }

    /// Called by WebRender at specified stages to notify the registered handler.
    pub fn notify(mut self) {
        if let Some(handler) = self.handler.take() {
            handler.notify(self.when);
        }
    }
}

/// An object that can perform hit-testing without doing synchronous queries to
/// the RenderBackendThread.
pub trait ApiHitTester: Send + Sync {
    /// Does a hit test on display items in the specified document, at the given
    /// point. The vector of hit results will contain all display items that match,
    /// ordered from front to back.
    fn hit_test(&self, point: WorldPoint) -> HitTestResult;
}

/// A hit tester requested to the render backend thread but not necessarily ready yet.
///
/// The request should be resolved as late as possible to reduce the likelihood of blocking.
pub struct HitTesterRequest {
    #[doc(hidden)]
    pub rx: Receiver<Arc<dyn ApiHitTester>>,
}

impl HitTesterRequest {
    /// Block until the hit tester is available and return it, consuming teh request.
    pub fn resolve(self) -> Arc<dyn ApiHitTester> {
        self.rx.recv().unwrap()
    }
}

/// Describe an item that matched a hit-test query.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct HitTestResultItem {
    /// The pipeline that the display item that was hit belongs to.
    pub pipeline: PipelineId,

    /// The tag of the hit display item.
    pub tag: ItemTag,

    /// The animation id from the stacking context.
    pub animation_id: u64,
}

/// Returned by `RenderApi::hit_test`.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct HitTestResult {
    /// List of items that are match the hit-test query.
    pub items: Vec<HitTestResultItem>,
}

impl Drop for NotificationRequest {
    fn drop(&mut self) {
        if let Some(ref mut handler) = self.handler {
            handler.notify(Checkpoint::TransactionDropped);
        }
    }
}

// This Clone impl yields an "empty" request because we don't want the requests
// to be notified twice so the request is owned by only one of the API messages
// (the original one) after the clone.
// This works in practice because the notifications requests are used for
// synchronization so we don't need to include them in the recording mechanism
// in wrench that clones the messages.
impl Clone for NotificationRequest {
    fn clone(&self) -> Self {
        NotificationRequest {
            when: self.when,
            handler: None,
        }
    }
}


/// A key to identify an animated property binding.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, MallocSizeOf, PartialEq, Serialize, Eq, Hash, PeekPoke)]
pub struct PropertyBindingId {
    pub namespace: IdNamespace,
    pub uid: u32,
}

impl PropertyBindingId {
    /// Constructor.
    pub fn new(value: u64) -> Self {
        PropertyBindingId {
            namespace: IdNamespace((value >> 32) as u32),
            uid: value as u32,
        }
    }

    /// Decompose the ID back into the raw integer.
    pub fn to_u64(&self) -> u64 {
        ((self.namespace.0 as u64) << 32) | self.uid as u64
    }
}

/// A unique key that is used for connecting animated property
/// values to bindings in the display list.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct PropertyBindingKey<T> {
    ///
    pub id: PropertyBindingId,
    #[doc(hidden)]
    pub _phantom: PhantomData<T>,
}

/// Construct a property value from a given key and value.
impl<T: Copy> PropertyBindingKey<T> {
    ///
    pub fn with(self, value: T) -> PropertyValue<T> {
        PropertyValue { key: self, value }
    }
}

impl<T> Into<u64> for PropertyBindingKey<T> {
    fn into(self) -> u64 {
        self.id.to_u64()
    }
}

impl<T> PropertyBindingKey<T> {
    /// Constructor.
    pub fn new(value: u64) -> Self {
        PropertyBindingKey {
            id: PropertyBindingId::new(value),
            _phantom: PhantomData,
        }
    }
}

/// A binding property can either be a specific value
/// (the normal, non-animated case) or point to a binding location
/// to fetch the current value from.
/// Note that Binding has also a non-animated value, the value is
/// used for the case where the animation is still in-delay phase
/// (i.e. the animation doesn't produce any animation values).
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum PropertyBinding<T> {
    /// Non-animated value.
    Value(T),
    /// Animated binding.
    Binding(PropertyBindingKey<T>, T),
}

impl<T: Default> Default for PropertyBinding<T> {
    fn default() -> Self {
        PropertyBinding::Value(Default::default())
    }
}

impl<T> From<T> for PropertyBinding<T> {
    fn from(value: T) -> PropertyBinding<T> {
        PropertyBinding::Value(value)
    }
}

impl From<PropertyBindingKey<ColorF>> for PropertyBindingKey<ColorU> {
    fn from(key: PropertyBindingKey<ColorF>) -> PropertyBindingKey<ColorU> {
        PropertyBindingKey {
            id: key.id,
            _phantom: PhantomData,
        }
    }
}

impl From<PropertyBindingKey<ColorU>> for PropertyBindingKey<ColorF> {
    fn from(key: PropertyBindingKey<ColorU>) -> PropertyBindingKey<ColorF> {
        PropertyBindingKey {
            id: key.id,
            _phantom: PhantomData,
        }
    }
}

impl From<PropertyBinding<ColorF>> for PropertyBinding<ColorU> {
    fn from(value: PropertyBinding<ColorF>) -> PropertyBinding<ColorU> {
        match value {
            PropertyBinding::Value(value) => PropertyBinding::Value(value.into()),
            PropertyBinding::Binding(k, v) => {
                PropertyBinding::Binding(k.into(), v.into())
            }
        }
    }
}

impl From<PropertyBinding<ColorU>> for PropertyBinding<ColorF> {
    fn from(value: PropertyBinding<ColorU>) -> PropertyBinding<ColorF> {
        match value {
            PropertyBinding::Value(value) => PropertyBinding::Value(value.into()),
            PropertyBinding::Binding(k, v) => {
                PropertyBinding::Binding(k.into(), v.into())
            }
        }
    }
}

/// The current value of an animated property. This is
/// supplied by the calling code.
#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq)]
pub struct PropertyValue<T> {
    ///
    pub key: PropertyBindingKey<T>,
    ///
    pub value: T,
}

/// When using `generate_frame()`, a list of `PropertyValue` structures
/// can optionally be supplied to provide the current value of any
/// animated properties.
#[derive(Clone, Deserialize, Serialize, Debug, PartialEq, Default)]
pub struct DynamicProperties {
    /// transform list
    pub transforms: Vec<PropertyValue<LayoutTransform>>,
    /// opacity
    pub floats: Vec<PropertyValue<f32>>,
    /// background color
    pub colors: Vec<PropertyValue<ColorF>>,
}

impl DynamicProperties {
    /// Extend the properties.
    pub fn extend(&mut self, other: Self) {
        self.transforms.extend(other.transforms);
        self.floats.extend(other.floats);
        self.colors.extend(other.colors);
    }
}

/// A C function that takes a pointer to a heap allocation and returns its size.
///
/// This is borrowed from the malloc_size_of crate, upon which we want to avoid
/// a dependency from WebRender.
pub type VoidPtrToSizeFn = unsafe extern "C" fn(ptr: *const c_void) -> usize;

/// A configuration option that can be changed at runtime.
///
/// # Adding a new configuration option
///
///  - Add a new enum variant here.
///  - Add the entry in WR_BOOL_PARAMETER_LIST in gfxPlatform.cpp.
///  - React to the parameter change anywhere in WebRender where a SetParam message is received.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum Parameter {
    Bool(BoolParameter, bool),
    Int(IntParameter, i32),
    Float(FloatParameter, f32),
}

/// Boolean configuration option.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum BoolParameter {
    PboUploads = 0,
    Multithreading = 1,
    BatchedUploads = 2,
    DrawCallsForTextureCopy = 3,
}

/// Integer configuration option.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum IntParameter {
    BatchedUploadThreshold = 0,
}

/// Floating point configuration option.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum FloatParameter {
    /// The minimum time for the CPU portion of a frame to be considered slow
    SlowCpuFrameThreshold = 0,
}

/// Flags to track why we are rendering.
#[repr(C)]
#[derive(Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash, Default, Deserialize, MallocSizeOf, Serialize)]
pub struct RenderReasons(u32);

bitflags! {
    impl RenderReasons: u32 {
        /// Equivalent of empty() for the C++ side.
        const NONE                          = 0;
        const SCENE                         = 1 << 0;
        const ANIMATED_PROPERTY             = 1 << 1;
        const RESOURCE_UPDATE               = 1 << 2;
        const ASYNC_IMAGE                   = 1 << 3;
        const CLEAR_RESOURCES               = 1 << 4;
        const APZ                           = 1 << 5;
        /// Window resize
        const RESIZE                        = 1 << 6;
        /// Various widget-related reasons
        const WIDGET                        = 1 << 7;
        /// See Frame::must_be_drawn
        const TEXTURE_CACHE_FLUSH           = 1 << 8;
        const SNAPSHOT                      = 1 << 9;
        const POST_RESOURCE_UPDATES_HOOK    = 1 << 10;
        const CONFIG_CHANGE                 = 1 << 11;
        const CONTENT_SYNC                  = 1 << 12;
        const FLUSH                         = 1 << 13;
        const TESTING                       = 1 << 14;
        const OTHER                         = 1 << 15;
        /// Vsync isn't actually "why" we render but it can be useful
        /// to see which frames were driven by the vsync scheduler so
        /// we store a bit for it.
        const VSYNC                         = 1 << 16;
        const SKIPPED_COMPOSITE             = 1 << 17;
        /// Gecko does some special things when it starts observing vsync
        /// so it can be useful to know what frames are associated with it.
        const START_OBSERVING_VSYNC         = 1 << 18;
        const ASYNC_IMAGE_COMPOSITE_UNTIL   = 1 << 19;
    }
}

impl core::fmt::Debug for RenderReasons {
    fn fmt(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
        if self.is_empty() {
            write!(f, "{:#x}", Self::empty().bits())
        } else {
            bitflags::parser::to_writer(self, f)
        }
    }
}

impl RenderReasons {
    pub const NUM_BITS: u32 = 17;
}

/// Flags to enable/disable various builtin debugging tools.
#[repr(C)]
#[derive(Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash, Default, Deserialize, MallocSizeOf, Serialize)]
pub struct DebugFlags(u64);

bitflags! {
    impl DebugFlags: u64 {
        /// Display the frame profiler on screen.
        const PROFILER_DBG          = 1 << 0;
        /// Display intermediate render targets on screen.
        const RENDER_TARGET_DBG     = 1 << 1;
        /// Display all texture cache pages on screen.
        const TEXTURE_CACHE_DBG     = 1 << 2;
        /// Display GPU timing results.
        const GPU_TIME_QUERIES      = 1 << 3;
        /// Query the number of pixels that pass the depth test divided and show it
        /// in the profiler as a percentage of the number of pixels in the screen
        /// (window width times height).
        const GPU_SAMPLE_QUERIES    = 1 << 4;
        /// Render each quad with their own draw call.
        ///
        /// Terrible for performance but can help with understanding the drawing
        /// order when inspecting renderdoc or apitrace recordings.
        const DISABLE_BATCHING      = 1 << 5;
        /// Display the pipeline epochs.
        const EPOCHS                = 1 << 6;
        /// Print driver messages to stdout.
        const ECHO_DRIVER_MESSAGES  = 1 << 7;
        /// Show an overlay displaying overdraw amount.
        const SHOW_OVERDRAW         = 1 << 8;
        /// Display the contents of GPU cache.
        const GPU_CACHE_DBG         = 1 << 9;
        /// Clear evicted parts of the texture cache for debugging purposes.
        const TEXTURE_CACHE_DBG_CLEAR_EVICTED = 1 << 10;
        /// Show picture caching debug overlay
        const PICTURE_CACHING_DBG   = 1 << 11;
        /// Highlight all primitives with colors based on kind.
        const PRIMITIVE_DBG = 1 << 12;
        /// Draw a zoom widget showing part of the framebuffer zoomed in.
        const ZOOM_DBG = 1 << 13;
        /// Scale the debug renderer down for a smaller screen. This will disrupt
        /// any mapping between debug display items and page content, so shouldn't
        /// be used with overlays like the picture caching or primitive display.
        const SMALL_SCREEN = 1 << 14;
        /// Disable various bits of the WebRender pipeline, to help narrow
        /// down where slowness might be coming from.
        const DISABLE_OPAQUE_PASS = 1 << 15;
        ///
        const DISABLE_ALPHA_PASS = 1 << 16;
        ///
        const DISABLE_CLIP_MASKS = 1 << 17;
        ///
        const DISABLE_TEXT_PRIMS = 1 << 18;
        ///
        const DISABLE_GRADIENT_PRIMS = 1 << 19;
        ///
        const OBSCURE_IMAGES = 1 << 20;
        /// Taint the transparent area of the glyphs with a random opacity to easily
        /// see when glyphs are re-rasterized.
        const GLYPH_FLASHING = 1 << 21;
        /// The profiler only displays information that is out of the ordinary.
        const SMART_PROFILER        = 1 << 22;
        /// If set, dump picture cache invalidation debug to console.
        const INVALIDATION_DBG = 1 << 23;
        /// Collect and dump profiler statistics to captures.
        const PROFILER_CAPTURE = 1 << 25;
        /// Invalidate picture tiles every frames (useful when inspecting GPU work in external tools).
        const FORCE_PICTURE_INVALIDATION = 1 << 26;
        /// Display window visibility on screen.
        const WINDOW_VISIBILITY_DBG     = 1 << 27;
        /// Render large blobs with at a smaller size (incorrectly). This is a temporary workaround for
        /// fuzzing.
        const RESTRICT_BLOB_SIZE        = 1 << 28;
        /// Enable surface promotion logging.
        const SURFACE_PROMOTION_LOGGING = 1 << 29;
        /// Show picture caching debug overlay.
        const PICTURE_BORDERS           = 1 << 30;
        /// Panic when a attempting to display a missing stacking context snapshot.
        const MISSING_SNAPSHOT_PANIC    = (1 as u64) << 31; // need "as u32" until we have cbindgen#556
        /// Panic when a attempting to display a missing stacking context snapshot.
        const MISSING_SNAPSHOT_PINK     = (1 as u64) << 32;
    }
}

impl core::fmt::Debug for DebugFlags {
    fn fmt(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
        if self.is_empty() {
            write!(f, "{:#x}", Self::empty().bits())
        } else {
            bitflags::parser::to_writer(self, f)
        }
    }
}

/// Information specific to a primitive type that
/// uniquely identifies a primitive template by key.
#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub enum PrimitiveKeyKind {
    /// Clear an existing rect, used for special effects on some platforms.
    Clear,
    ///
    Rectangle {
        ///
        color: PropertyBinding<ColorU>,
    },
}

///
#[derive(Clone, Copy, Debug)]
pub enum ScrollLocation {
    /// Scroll by a certain amount.
    Delta(LayoutVector2D),
    /// Scroll to very top of element.
    Start,
    /// Scroll to very bottom of element.
    End,
}

/// Crash annotations included in crash reports.
#[repr(C)]
#[derive(Clone, Copy)]
pub enum CrashAnnotation {
    CompileShader = 0,
    DrawShader = 1,
}

/// Handler to expose support for annotating crash reports.
pub trait CrashAnnotator : Send {
    fn set(&self, annotation: CrashAnnotation, value: &std::ffi::CStr);
    fn clear(&self, annotation: CrashAnnotation);
    fn box_clone(&self) -> Box<dyn CrashAnnotator>;
}

impl Clone for Box<dyn CrashAnnotator> {
    fn clone(&self) -> Box<dyn CrashAnnotator> {
        self.box_clone()
    }
}

/// Guard to add a crash annotation at creation, and clear it at destruction.
pub struct CrashAnnotatorGuard<'a> {
    annotator: &'a Option<Box<dyn CrashAnnotator>>,
    annotation: CrashAnnotation,
}

impl<'a> CrashAnnotatorGuard<'a> {
    pub fn new(
        annotator: &'a Option<Box<dyn CrashAnnotator>>,
        annotation: CrashAnnotation,
        value: &std::ffi::CStr,
    ) -> Self {
        if let Some(ref annotator) = annotator {
            annotator.set(annotation, value);
        }
        Self {
            annotator,
            annotation,
        }
    }
}

impl<'a> Drop for CrashAnnotatorGuard<'a> {
    fn drop(&mut self) {
        if let Some(ref annotator) = self.annotator {
            annotator.clear(self.annotation);
        }
    }
}
