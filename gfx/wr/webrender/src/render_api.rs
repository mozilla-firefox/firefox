/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![deny(missing_docs)]

use std::cell::Cell;
use std::fmt;
use std::marker::PhantomData;
use std::path::PathBuf;
use std::sync::Arc;
use api::{MinimapData, SnapshotImageKey};
use crate::api::channel::{Sender, SyncSender, single_msg_channel, unbounded_channel};
use crate::api::{BuiltDisplayList, IdNamespace, ExternalScrollId, Parameter, BoolParameter};
use crate::api::{FontKey, FontInstanceKey, NativeFontHandle};
use crate::api::{BlobImageData, BlobImageKey, ImageData, ImageDescriptor, ImageKey, Epoch, QualitySettings};
use crate::api::{BlobImageParams, BlobImageRequest, BlobImageResult, AsyncBlobImageRasterizer, BlobImageHandler};
use crate::api::{DocumentId, EmbedPolicy, PipelineId, PropertyBindingId, PropertyBindingKey, ExternalEvent};
use crate::api::{HitTestResult, HitTesterRequest, ApiHitTester, PropertyValue, DynamicProperties};
use crate::api::{SampledScrollOffset, TileSize, NotificationRequest, DebugFlags};
use crate::api::{GlyphDimensionRequest, GlyphIndexRequest, GlyphIndex, GlyphDimensions};
use crate::api::{FontInstanceOptions, FontInstancePlatformOptions, FontVariation, RenderReasons};
use crate::api::{RenderBackendId, RenderNotifier};
use crate::api::DEFAULT_TILE_SIZE;
use crate::api::units::*;
use crate::api_resources::ApiResources;
use crate::api::ImageFormat;
use crate::bump_allocator::ChunkPool;
use crate::device::{TextureFilter, TextureFormatPair};
use crate::frame_builder::FrameBuilderConfig;
use crate::internal_types::{ResultMsg, SwizzleSettings};
use crate::texture_cache::TextureCacheConfig;
use crate::AsyncPropertySampler;
use glyph_rasterizer::{GlyphRasterThread, SharedFontResources};
use rayon::ThreadPool;
use crate::scene_builder_thread::{SceneBuilderRequest, SceneBuilderResult};
use crate::intern::InterningMemoryReport;
use crate::profiler::{self, TransactionProfile};
#[cfg(feature = "debugger")]
use crate::debugger::{DebugQuery, DebuggerClient};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
struct ResourceId(pub u32);

/// Update of a persistent resource in WebRender.
///
/// ResourceUpdate changes keep theirs effect across display list changes.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub enum ResourceUpdate {
    /// See `AddImage`.
    AddImage(AddImage),
    /// See `UpdateImage`.
    UpdateImage(UpdateImage),
    /// Delete an existing image resource.
    ///
    /// It is invalid to continue referring to the image key in any display list
    /// in the transaction that contains the `DeleteImage` message and subsequent
    /// transactions.
    DeleteImage(ImageKey),
    /// See `AddBlobImage`.
    AddBlobImage(AddBlobImage),
    /// See `UpdateBlobImage`.
    UpdateBlobImage(UpdateBlobImage),
    /// Delete existing blob image resource.
    DeleteBlobImage(BlobImageKey),
    /// See `AddBlobImage::visible_area`.
    SetBlobImageVisibleArea(BlobImageKey, DeviceIntRect),
    /// See `AddSnapshotImage`.
    AddSnapshotImage(AddSnapshotImage),
    /// See `AddSnapshotImage`.
    DeleteSnapshotImage(SnapshotImageKey),
    /// Move a snapshot image, and the pixels already rendered into it, from the
    /// first key to the second. The keys can belong to different id namespaces.
    /// After this, only the second key names the image.
    AdoptSnapshotImage(SnapshotImageKey, SnapshotImageKey),
    /// See `AddFont`.
    AddFont(AddFont),
    /// Deletes an already existing font resource.
    ///
    /// It is invalid to continue referring to the font key in any display list
    /// in the transaction that contains the `DeleteImage` message and subsequent
    /// transactions.
    DeleteFont(FontKey),
    /// See `AddFontInstance`.
    AddFontInstance(AddFontInstance),
    /// Deletes an already existing font instance resource.
    ///
    /// It is invalid to continue referring to the font instance in any display
    /// list in the transaction that contains the `DeleteImage` message and
    /// subsequent transactions.
    DeleteFontInstance(FontInstanceKey),
}

impl fmt::Debug for ResourceUpdate {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ResourceUpdate::AddImage(ref i) => f.write_fmt(format_args!(
                "ResourceUpdate::AddImage size({:?})",
                &i.descriptor.size
            )),
            ResourceUpdate::UpdateImage(ref i) => f.write_fmt(format_args!(
                "ResourceUpdate::UpdateImage size({:?})",
                &i.descriptor.size
            )),
            ResourceUpdate::AddBlobImage(ref i) => f.write_fmt(format_args!(
                "ResourceUFpdate::AddBlobImage size({:?})",
                &i.descriptor.size
            )),
            ResourceUpdate::UpdateBlobImage(i) => f.write_fmt(format_args!(
                "ResourceUpdate::UpdateBlobImage size({:?})",
                &i.descriptor.size
            )),
            ResourceUpdate::DeleteImage(..) => f.write_str("ResourceUpdate::DeleteImage"),
            ResourceUpdate::DeleteBlobImage(..) => f.write_str("ResourceUpdate::DeleteBlobImage"),
            ResourceUpdate::SetBlobImageVisibleArea(..) => f.write_str("ResourceUpdate::SetBlobImageVisibleArea"),
            ResourceUpdate::AddSnapshotImage(..) => f.write_str("ResourceUpdate::AddSnapshotImage"),
            ResourceUpdate::DeleteSnapshotImage(..) => f.write_str("ResourceUpdate::DeleteSnapshotImage"),
            ResourceUpdate::AdoptSnapshotImage(..) => f.write_str("ResourceUpdate::AdoptSnapshotImage"),
            ResourceUpdate::AddFont(..) => f.write_str("ResourceUpdate::AddFont"),
            ResourceUpdate::DeleteFont(..) => f.write_str("ResourceUpdate::DeleteFont"),
            ResourceUpdate::AddFontInstance(..) => f.write_str("ResourceUpdate::AddFontInstance"),
            ResourceUpdate::DeleteFontInstance(..) => f.write_str("ResourceUpdate::DeleteFontInstance"),
        }
    }
}

/// Whether to generate a frame, and if so, an id that allows tracking this
/// transaction through the various frame stages.
#[derive(Clone, Debug)]
pub enum GenerateFrame {
    /// Generate a frame if something changed.
    Yes {
        /// An id that allows tracking the frame transaction through the various
        /// frame stages. Specified by the caller of generate_frame().
        id: u64,
        /// If false, (a subset of) the frame will be rendered, but nothing will
        /// be presented on the window.
        present: bool,
        /// This flag is used by Firefox to differentiate between frames that
        /// participate or not in the frame throttling mechanism.
        tracked: bool,
    },
    /// Don't generate a frame even if something has changed.
    No,
}

impl GenerateFrame {
    ///
    pub fn as_bool(&self) -> bool {
        match self {
            GenerateFrame::Yes { .. } => true,
            GenerateFrame::No => false,
        }
    }

    /// If false, a frame may be (partially) generated but it will not be
    /// presented to the window.
    pub fn present(&self) -> bool {
        match self {
            GenerateFrame::Yes { present, .. } => *present,
            GenerateFrame::No => false,
        }
    }

    /// This flag is used by Gecko to indicate whether the transaction
    /// participates in frame throttling mechanisms.
    pub fn tracked(&self) -> bool {
        match self {
            GenerateFrame::Yes { tracked, .. } => *tracked,
            GenerateFrame::No => false,
        }
    }

    /// Return the frame ID, if a frame is generated.
    pub fn id(&self) -> Option<u64> {
        match self {
            GenerateFrame::Yes { id, .. } => Some(*id),
            GenerateFrame::No => None,
        }
    }
}

/// A Transaction is a group of commands to apply atomically to a document.
///
/// This mechanism ensures that:
///  - no other message can be interleaved between two commands that need to be applied together.
///  - no redundant work is performed if two commands in the same transaction cause the scene or
///    the frame to be rebuilt.
pub struct Transaction {
    /// Operations affecting the scene (applied before scene building).
    scene_ops: Vec<SceneMsg>,
    /// Operations affecting the generation of frames (applied after scene building).
    frame_ops: Vec<FrameMsg>,

    notifications: Vec<NotificationRequest>,

    /// Persistent resource updates to apply as part of this transaction.
    pub resource_updates: Vec<ResourceUpdate>,

    /// True if the transaction needs the scene building thread's attention.
    /// False for things that can skip the scene builder, like APZ changes and
    /// async images.
    ///
    /// Before this `Transaction` is converted to a `TransactionMsg`, we look
    /// over its contents and set this if we're doing anything the scene builder
    /// needs to know about, so this is only a default.
    use_scene_builder_thread: bool,

    /// Whether to generate a frame, and if so, an id that allows tracking this
    /// transaction through the various frame stages. Specified by the caller of
    /// generate_frame().
    generate_frame: GenerateFrame,

    /// Time when this transaction was constructed.
    creation_time: u64,

    /// Set to true in order to force re-rendering even if WebRender can't internally
    /// detect that something has changed.
    pub invalidate_rendered_frame: bool,

    low_priority: bool,

    ///
    pub render_reasons: RenderReasons,
}

impl Transaction {
    /// Constructor.
    pub fn new() -> Self {
        Transaction {
            scene_ops: Vec::new(),
            frame_ops: Vec::new(),
            resource_updates: Vec::new(),
            notifications: Vec::new(),
            use_scene_builder_thread: true,
            generate_frame: GenerateFrame::No,
            creation_time: zeitstempel::now(),
            invalidate_rendered_frame: false,
            low_priority: false,
            render_reasons: RenderReasons::empty(),
        }
    }

    /// Marks this transaction to allow it to skip going through the scene builder
    /// thread.
    ///
    /// This is useful to avoid jank in transaction associated with animated
    /// property updates, panning and zooming.
    ///
    /// Note that transactions that skip the scene builder thread can race ahead of
    /// transactions that don't skip it.
    pub fn skip_scene_builder(&mut self) {
        self.use_scene_builder_thread = false;
    }

    /// Marks this transaction to enforce going through the scene builder thread.
    pub fn use_scene_builder_thread(&mut self) {
        self.use_scene_builder_thread = true;
    }

    /// Returns true if the transaction has no effect.
    pub fn is_empty(&self) -> bool {
        !self.generate_frame.as_bool() &&
            !self.invalidate_rendered_frame &&
            self.scene_ops.is_empty() &&
            self.frame_ops.is_empty() &&
            self.resource_updates.is_empty() &&
            self.notifications.is_empty()
    }

    /// Update a pipeline's epoch.
    pub fn update_epoch(&mut self, pipeline_id: PipelineId, epoch: Epoch) {
        // We track epochs before and after scene building.
        // This one will be applied to the pending scene right away:
        self.scene_ops.push(SceneMsg::UpdateEpoch(pipeline_id, epoch));
        // And this one will be applied to the currently built scene at the end
        // of the transaction (potentially long after the scene_ops one).
        self.frame_ops.push(FrameMsg::UpdateEpoch(pipeline_id, epoch));
        // We could avoid the duplication here by storing the epoch updates in a
        // separate array and let the render backend schedule the updates at the
        // proper times, but it wouldn't make things simpler.
    }

    /// Sets the root pipeline.
    ///
    /// # Examples
    ///
    /// ```
    /// # use webrender::api::{PipelineId};
    /// # use webrender::api::units::{DeviceIntSize};
    /// # use webrender::render_api::{RenderApiSender, Transaction};
    /// # fn example() {
    /// let pipeline_id = PipelineId(0, 0);
    /// let mut txn = Transaction::new();
    /// txn.set_root_pipeline(pipeline_id);
    /// # }
    /// ```
    pub fn set_root_pipeline(&mut self, pipeline_id: PipelineId) {
        self.scene_ops.push(SceneMsg::SetRootPipeline(pipeline_id));
    }

    /// Removes data associated with a pipeline from the internal data structures.
    /// If the specified `pipeline_id` is for the root pipeline, the root pipeline
    /// is reset back to `None`.
    pub fn remove_pipeline(&mut self, pipeline_id: PipelineId) {
        self.scene_ops.push(SceneMsg::RemovePipeline(pipeline_id));
    }

    /// Sets the pipelines that `pipeline_id` may embed with iframe items.
    ///
    /// The policy must come from trusted code, not from the producer of the
    /// pipeline's display list. It is kept until the pipeline is removed or
    /// the policy is replaced, and it applies to the next scene build.
    pub fn set_embed_policy(&mut self, pipeline_id: PipelineId, policy: EmbedPolicy) {
        self.scene_ops.push(SceneMsg::SetEmbedPolicy { pipeline_id, policy });
    }

    /// Enables or disables the enforcement of embed policies in the document.
    /// While enabled, a pipeline without an embed policy may not embed anything.
    pub fn set_embed_policy_enforcement(&mut self, enabled: bool) {
        self.scene_ops.push(SceneMsg::SetEmbedPolicyEnforcement(enabled));
    }

    /// Supplies a new frame to WebRender.
    ///
    /// Non-blocking, it notifies a worker process which processes the display list.
    ///
    /// Note: Scrolling doesn't require an own Frame.
    ///
    /// Arguments:
    ///
    /// * `epoch`: The unique Frame ID, monotonically increasing.
    /// * `namespace`: The id namespace that owns the resources this display list
    ///   is allowed to reference. It must be provided by the (trusted) code that
    ///   submits the display list, never by whoever built the display list: it is
    ///   what lets the scene builder reject references to resources belonging to
    ///   another namespace.
    /// * `pipeline_id`: The ID of the pipeline that is supplying this display list.
    /// * `display_list`: The root Display list used in this frame.
    pub fn set_display_list(
        &mut self,
        epoch: Epoch,
        namespace: IdNamespace,
        (pipeline_id, mut display_list): (PipelineId, BuiltDisplayList),
    ) {
        display_list.set_send_time_ns(zeitstempel::now());
        self.scene_ops.push(
            SceneMsg::SetDisplayList {
                display_list,
                epoch,
                pipeline_id,
                namespace,
            }
        );
    }

    /// Add a set of persistent resource updates to apply as part of this transaction.
    pub fn update_resources(&mut self, mut resources: Vec<ResourceUpdate>) {
        self.resource_updates.append(&mut resources);
    }

    // Note: Gecko uses this to get notified when a transaction that contains
    // potentially long blob rasterization or scene build is ready to be rendered.
    // so that the tab-switching integration can react adequately when tab
    // switching takes too long. For this use case when matters is that the
    // notification doesn't fire before scene building and blob rasterization.

    /// Trigger a notification at a certain stage of the rendering pipeline.
    ///
    /// Not that notification requests are skipped during serialization, so is is
    /// best to use them for synchronization purposes and not for things that could
    /// affect the WebRender's state.
    pub fn notify(&mut self, event: NotificationRequest) {
        self.notifications.push(event);
    }

    /// Setup the output region in the framebuffer for a given document.
    pub fn set_document_view(
        &mut self,
        device_rect: DeviceIntRect,
    ) {
        let device_rect = DeviceIntRect::from_origin_and_size(
            device_rect.min,
            clamp_window_size(device_rect.size()),
        );
        self.scene_ops.push(
            SceneMsg::SetDocumentView {
                device_rect,
            },
        );
    }

    /// Set multiple scroll offsets with generations to the node identified by
    /// the given external scroll id, the scroll offsets are relative to the
    /// pre-scrolled offset for the scrolling layer.
    pub fn set_scroll_offsets(
        &mut self,
        id: ExternalScrollId,
        sampled_scroll_offsets: Vec<SampledScrollOffset>,
    ) {
        self.frame_ops.push(FrameMsg::SetScrollOffsets(id, sampled_scroll_offsets));
    }

    /// Set the current quality / performance settings for this document.
    pub fn set_quality_settings(&mut self, settings: QualitySettings) {
        self.scene_ops.push(SceneMsg::SetQualitySettings { settings });
    }

    ///
    pub fn set_is_transform_async_zooming(&mut self, is_zooming: bool, animation_id: PropertyBindingId) {
        self.frame_ops.push(FrameMsg::SetIsTransformAsyncZooming(is_zooming, animation_id));
    }

    /// Specify data for APZ minimap debug overlay to be composited
    pub fn set_minimap_data(&mut self, id: ExternalScrollId, minimap_data: MinimapData) {
      self.frame_ops.push(FrameMsg::SetMinimapData(id, minimap_data));
    }

    /// Generate a new frame. When it's done and a RenderNotifier has been set
    /// in `webrender::Renderer`, [new_frame_ready()][notifier] gets called.
    /// Note that the notifier is called even if the frame generation was a
    /// no-op; the arguments passed to `new_frame_ready` will provide information
    /// as to when happened.
    ///
    /// [notifier]: trait.RenderNotifier.html#tymethod.new_frame_ready
    pub fn generate_frame(&mut self, id: u64, present: bool, tracked: bool, reasons: RenderReasons) {
        self.generate_frame = GenerateFrame::Yes{ id, present, tracked };
        self.render_reasons |= reasons;
    }

    /// Invalidate rendered frame. It ensure that frame will be rendered during
    /// next frame generation. WebRender could skip frame rendering if there
    /// is no update.
    /// But there are cases that needs to force rendering.
    ///  - Content of image is updated by reusing same ExternalImageId.
    ///  - Platform requests it if pixels become stale (like wakeup from standby).
    pub fn invalidate_rendered_frame(&mut self, reasons: RenderReasons) {
        self.invalidate_rendered_frame = true;
        self.render_reasons |= reasons
    }

    /// Reset the list of animated property bindings that should be used to resolve
    /// bindings in the current display list.
    pub fn reset_dynamic_properties(&mut self) {
        self.frame_ops.push(FrameMsg::ResetDynamicProperties);
    }

    /// Add to the list of animated property bindings that should be used to resolve
    /// bindings in the current display list.
    pub fn append_dynamic_properties(&mut self, properties: DynamicProperties) {
        self.frame_ops.push(FrameMsg::AppendDynamicProperties(properties));
    }

    /// Add to the list of animated property bindings that should be used to
    /// resolve bindings in the current display list. This is a convenience method
    /// so the caller doesn't have to figure out all the dynamic properties before
    /// setting them on the transaction but can do them incrementally.
    pub fn append_dynamic_transform_properties(&mut self, transforms: Vec<PropertyValue<LayoutTransform>>) {
        self.frame_ops.push(FrameMsg::AppendDynamicTransformProperties(transforms));
    }

    /// Consumes this object and just returns the frame ops.
    pub fn get_frame_ops(self) -> Vec<FrameMsg> {
        self.frame_ops
    }

    fn finalize(self, document_id: DocumentId) -> Box<TransactionMsg> {
        Box::new(TransactionMsg {
            document_id,
            scene_ops: self.scene_ops,
            frame_ops: self.frame_ops,
            resource_updates: self.resource_updates,
            notifications: self.notifications,
            use_scene_builder_thread: self.use_scene_builder_thread,
            generate_frame: self.generate_frame,
            creation_time: Some(self.creation_time),
            invalidate_rendered_frame: self.invalidate_rendered_frame,
            low_priority: self.low_priority,
            blob_rasterizer: None,
            blob_requests: Vec::new(),
            rasterized_blobs: Vec::new(),
            profile: TransactionProfile::new(),
            render_reasons: self.render_reasons,
        })
    }

    /// See `ResourceUpdate::AddImage`.
    pub fn add_image(
        &mut self,
        key: ImageKey,
        descriptor: ImageDescriptor,
        data: ImageData,
        tiling: Option<TileSize>,
    ) {
        self.resource_updates.push(ResourceUpdate::AddImage(AddImage {
            key,
            descriptor,
            data,
            tiling,
        }));
    }

    /// See `ResourceUpdate::UpdateImage`.
    pub fn update_image(
        &mut self,
        key: ImageKey,
        descriptor: ImageDescriptor,
        data: ImageData,
        dirty_rect: &ImageDirtyRect,
    ) {
        self.resource_updates.push(ResourceUpdate::UpdateImage(UpdateImage {
            key,
            descriptor,
            data,
            dirty_rect: *dirty_rect,
        }));
    }

    /// See `ResourceUpdate::DeleteImage`.
    pub fn delete_image(&mut self, key: ImageKey) {
        self.resource_updates.push(ResourceUpdate::DeleteImage(key));
    }

    /// See `ResourceUpdate::AddBlobImage`.
    pub fn add_blob_image(
        &mut self,
        key: BlobImageKey,
        descriptor: ImageDescriptor,
        data: Arc<BlobImageData>,
        visible_rect: DeviceIntRect,
        tile_size: Option<TileSize>,
    ) {
        self.resource_updates.push(
            ResourceUpdate::AddBlobImage(AddBlobImage {
                key,
                descriptor,
                data,
                visible_rect,
                tile_size: tile_size.unwrap_or(DEFAULT_TILE_SIZE),
            })
        );
    }

    /// See `ResourceUpdate::UpdateBlobImage`.
    pub fn update_blob_image(
        &mut self,
        key: BlobImageKey,
        descriptor: ImageDescriptor,
        data: Arc<BlobImageData>,
        visible_rect: DeviceIntRect,
        dirty_rect: &BlobDirtyRect,
    ) {
        self.resource_updates.push(
            ResourceUpdate::UpdateBlobImage(UpdateBlobImage {
                key,
                descriptor,
                data,
                visible_rect,
                dirty_rect: *dirty_rect,
            })
        );
    }

    /// See `ResourceUpdate::DeleteBlobImage`.
    pub fn delete_blob_image(&mut self, key: BlobImageKey) {
        self.resource_updates.push(ResourceUpdate::DeleteBlobImage(key));
    }

    /// See `ResourceUpdate::SetBlobImageVisibleArea`.
    pub fn set_blob_image_visible_area(&mut self, key: BlobImageKey, area: DeviceIntRect) {
        self.resource_updates.push(ResourceUpdate::SetBlobImageVisibleArea(key, area));
    }

    /// See `ResourceUpdate::AddSnapshotImage`.
    pub fn add_snapshot_image(
        &mut self,
        key: SnapshotImageKey,
    ) {
        self.resource_updates.push(
            ResourceUpdate::AddSnapshotImage(AddSnapshotImage { key })
        );
    }

    /// See `ResourceUpdate::DeleteSnapshotImage`.
    pub fn delete_snapshot_image(&mut self, key: SnapshotImageKey) {
        self.resource_updates.push(ResourceUpdate::DeleteSnapshotImage(key));
    }

    /// See `ResourceUpdate::AdoptSnapshotImage`.
    pub fn adopt_snapshot_image(&mut self, from: SnapshotImageKey, to: SnapshotImageKey) {
        self.resource_updates.push(ResourceUpdate::AdoptSnapshotImage(from, to));
    }

    /// See `ResourceUpdate::AddFont`.
    pub fn add_raw_font(&mut self, key: FontKey, bytes: Vec<u8>, index: u32) {
        self.resource_updates
            .push(ResourceUpdate::AddFont(AddFont::Raw(key, Arc::new(bytes), index)));
    }

    /// See `ResourceUpdate::AddFont`.
    pub fn add_native_font(&mut self, key: FontKey, native_handle: NativeFontHandle) {
        self.resource_updates
            .push(ResourceUpdate::AddFont(AddFont::Native(key, native_handle)));
    }

    /// See `ResourceUpdate::DeleteFont`.
    pub fn delete_font(&mut self, key: FontKey) {
        self.resource_updates.push(ResourceUpdate::DeleteFont(key));
    }

    /// See `ResourceUpdate::AddFontInstance`.
    pub fn add_font_instance(
        &mut self,
        key: FontInstanceKey,
        font_key: FontKey,
        glyph_size: f32,
        options: Option<FontInstanceOptions>,
        platform_options: Option<FontInstancePlatformOptions>,
        variations: Vec<FontVariation>,
    ) {
        self.resource_updates
            .push(ResourceUpdate::AddFontInstance(AddFontInstance {
                key,
                font_key,
                glyph_size,
                options,
                platform_options,
                variations,
            }));
    }

    /// See `ResourceUpdate::DeleteFontInstance`.
    pub fn delete_font_instance(&mut self, key: FontInstanceKey) {
        self.resource_updates.push(ResourceUpdate::DeleteFontInstance(key));
    }

    /// A hint that this transaction can be processed at a lower priority. High-
    /// priority transactions can jump ahead of regular-priority transactions,
    /// but both high- and regular-priority transactions are processed in order
    /// relative to other transactions of the same priority.
    pub fn set_low_priority(&mut self, low_priority: bool) {
        self.low_priority = low_priority;
    }

    /// Returns whether this transaction is marked as low priority.
    pub fn is_low_priority(&self) -> bool {
        self.low_priority
    }

    /// Render a pipeline offscreen immediately without waiting for vsync
    /// and without affecting the state of the current scene.
    ///
    /// Snapshotted stacking contexts will be persisted in the texture cache.
    pub fn render_offscreen(&mut self, pipeline_id: PipelineId) {
        self.scene_ops.push(SceneMsg::RenderOffscreen(pipeline_id));
    }
}

///
pub struct DocumentTransaction {
    ///
    pub document_id: DocumentId,
    ///
    pub transaction: Transaction,
}

/// Represents a transaction in the format sent through the channel.
pub struct TransactionMsg {
    ///
    pub document_id: DocumentId,
    /// Changes that require re-building the scene.
    pub scene_ops: Vec<SceneMsg>,
    /// Changes to animated properties that do not require re-building the scene.
    pub frame_ops: Vec<FrameMsg>,
    /// Updates to resources that persist across display lists.
    pub resource_updates: Vec<ResourceUpdate>,
    /// Whether to trigger frame building and rendering if something has changed.
    pub generate_frame: GenerateFrame,
    /// Creation time of this transaction.
    pub creation_time: Option<u64>,
    /// Whether to force frame building and rendering even if no changes are internally
    /// observed.
    pub invalidate_rendered_frame: bool,
    /// Whether to enforce that this transaction go through the scene builder.
    pub use_scene_builder_thread: bool,
    ///
    pub low_priority: bool,

    /// Handlers to notify at certain points of the pipeline.
    pub notifications: Vec<NotificationRequest>,
    ///
    pub blob_rasterizer: Option<Box<dyn AsyncBlobImageRasterizer>>,
    ///
    pub blob_requests: Vec<BlobImageParams>,
    ///
    pub rasterized_blobs: Vec<(BlobImageRequest, BlobImageResult)>,
    /// Collect various data along the rendering pipeline to display it in the embedded profiler.
    pub profile: TransactionProfile,
    /// Keep track of who asks rendering to happen.
    pub render_reasons: RenderReasons,
}

impl fmt::Debug for TransactionMsg {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "threaded={}, genframe={:?}, invalidate={}, low_priority={}",
                        self.use_scene_builder_thread,
                        self.generate_frame,
                        self.invalidate_rendered_frame,
                        self.low_priority,
                    ).unwrap();
        for scene_op in &self.scene_ops {
            writeln!(f, "\t\t{:?}", scene_op).unwrap();
        }

        for frame_op in &self.frame_ops {
            writeln!(f, "\t\t{:?}", frame_op).unwrap();
        }

        for resource_update in &self.resource_updates {
            writeln!(f, "\t\t{:?}", resource_update).unwrap();
        }
        Ok(())
    }
}

impl TransactionMsg {
    /// Returns true if this transaction has no effect.
    pub fn is_empty(&self) -> bool {
        !self.generate_frame.as_bool() &&
            !self.invalidate_rendered_frame &&
            self.scene_ops.is_empty() &&
            self.frame_ops.is_empty() &&
            self.resource_updates.is_empty() &&
            self.notifications.is_empty()
    }
}

/// Creates an image resource with provided parameters.
///
/// Must be matched with a `DeleteImage` at some point to prevent memory leaks.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub struct AddImage {
    /// A key to identify the image resource.
    pub key: ImageKey,
    /// Properties of the image.
    pub descriptor: ImageDescriptor,
    /// The pixels of the image.
    pub data: ImageData,
    /// An optional tiling scheme to apply when storing the image's data
    /// on the GPU. Applies to both width and heights of the tiles.
    ///
    /// Note that WebRender may internally chose to tile large images
    /// even if this member is set to `None`.
    pub tiling: Option<TileSize>,
}

/// Updates an already existing image resource.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub struct UpdateImage {
    /// The key identfying the image resource to update.
    pub key: ImageKey,
    /// Properties of the image.
    pub descriptor: ImageDescriptor,
    /// The pixels of the image.
    pub data: ImageData,
    /// An optional dirty rect that lets WebRender optimize the amount of
    /// data to transfer to the GPU.
    ///
    /// The data provided must still represent the entire image.
    pub dirty_rect: ImageDirtyRect,
}

/// Creates a blob-image resource with provided parameters.
///
/// Must be matched with a `DeleteImage` at some point to prevent memory leaks.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub struct AddBlobImage {
    /// A key to identify the blob-image resource.
    pub key: BlobImageKey,
    /// Properties of the image.
    pub descriptor: ImageDescriptor,
    /// The blob-image's serialized commands.
    pub data: Arc<BlobImageData>,
    /// The portion of the plane in the blob-image's internal coordinate
    /// system that is stretched to fill the image display item.
    ///
    /// Unlike regular images, blob images are not limited in size. The
    /// top-left corner of their internal coordinate system is also not
    /// necessary at (0, 0).
    /// This means that blob images can be updated to insert/remove content
    /// in any direction to support panning and zooming.
    pub visible_rect: DeviceIntRect,
    /// The blob image's tile size to apply when rasterizing the blob-image
    /// and when storing its rasterized data on the GPU.
    /// Applies to both width and heights of the tiles.
    ///
    /// All blob images are tiled.
    pub tile_size: TileSize,
}

/// Updates an already existing blob-image resource.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub struct UpdateBlobImage {
    /// The key identfying the blob-image resource to update.
    pub key: BlobImageKey,
    /// Properties of the image.
    pub descriptor: ImageDescriptor,
    /// The blob-image's serialized commands.
    pub data: Arc<BlobImageData>,
    /// See `AddBlobImage::visible_rect`.
    pub visible_rect: DeviceIntRect,
    /// An optional dirty rect that lets WebRender optimize the amount of
    /// data to to rasterize and transfer to the GPU.
    pub dirty_rect: BlobDirtyRect,
}

/// Creates a snapshot image resource.
///
/// Must be matched with a `DeleteSnapshotImage` at some point to prevent memory leaks.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub struct AddSnapshotImage {
    /// The key identfying the snapshot resource.
    pub key: SnapshotImageKey,
}

/// Creates a font resource.
///
/// Must be matched with a corresponding `ResourceUpdate::DeleteFont` at some point to prevent
/// memory leaks.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub enum AddFont {
    ///
    Raw(FontKey, Arc<Vec<u8>>, u32),
    ///
    Native(FontKey, NativeFontHandle),
}

/// Creates a font instance resource.
///
/// Must be matched with a corresponding `DeleteFontInstance` at some point
/// to prevent memory leaks.
#[derive(Clone)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub struct AddFontInstance {
    /// A key to identify the font instance.
    pub key: FontInstanceKey,
    /// The font resource's key.
    pub font_key: FontKey,
    /// Glyph size in app units.
    pub glyph_size: f32,
    ///
    pub options: Option<FontInstanceOptions>,
    ///
    pub platform_options: Option<FontInstancePlatformOptions>,
    ///
    pub variations: Vec<FontVariation>,
}

/// Frame messages affect building the scene.
pub enum SceneMsg {
    ///
    UpdateEpoch(PipelineId, Epoch),
    ///
    SetRootPipeline(PipelineId),
    ///
    RemovePipeline(PipelineId),
    ///
    SetDisplayList {
        ///
        display_list: BuiltDisplayList,
        ///
        epoch: Epoch,
        ///
        pipeline_id: PipelineId,
        /// The id namespace owning the resources this display list may reference.
        namespace: IdNamespace,
    },
    ///
    SetEmbedPolicy {
        ///
        pipeline_id: PipelineId,
        ///
        policy: EmbedPolicy,
    },
    ///
    SetEmbedPolicyEnforcement(bool),
    /// Build a scene without affecting any retained state.
    ///
    /// Useful to render an offscreen scene in the background without affecting
    /// what is currently displayed.
    RenderOffscreen(PipelineId),
    ///
    SetDocumentView {
        ///
        device_rect: DeviceIntRect,
    },
    /// Set the current quality / performance configuration for this document.
    SetQualitySettings {
        /// The set of available quality / performance config values.
        settings: QualitySettings,
    },
}

/// Frame messages affect frame generation (applied after building the scene).
pub enum FrameMsg {
    ///
    UpdateEpoch(PipelineId, Epoch),
    ///
    HitTest(WorldPoint, SyncSender<HitTestResult>),
    ///
    RequestHitTester(SyncSender<Arc<dyn ApiHitTester>>),
    ///
    SetScrollOffsets(ExternalScrollId, Vec<SampledScrollOffset>),
    ///
    ResetDynamicProperties,
    ///
    AppendDynamicProperties(DynamicProperties),
    ///
    AppendDynamicTransformProperties(Vec<PropertyValue<LayoutTransform>>),
    ///
    SetIsTransformAsyncZooming(bool, PropertyBindingId),
    ///
    SetMinimapData(ExternalScrollId, MinimapData)
}

impl fmt::Debug for SceneMsg {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(match *self {
            SceneMsg::UpdateEpoch(..) => "SceneMsg::UpdateEpoch",
            SceneMsg::SetDisplayList { .. } => "SceneMsg::SetDisplayList",
            SceneMsg::RemovePipeline(..) => "SceneMsg::RemovePipeline",
            SceneMsg::SetDocumentView { .. } => "SceneMsg::SetDocumentView",
            SceneMsg::SetRootPipeline(..) => "SceneMsg::SetRootPipeline",
            SceneMsg::SetEmbedPolicy { .. } => "SceneMsg::SetEmbedPolicy",
            SceneMsg::SetEmbedPolicyEnforcement(..) => "SceneMsg::SetEmbedPolicyEnforcement",
            SceneMsg::SetQualitySettings { .. } => "SceneMsg::SetQualitySettings",
            SceneMsg::RenderOffscreen(..) => "SceneMsg::BuildOffscreen",
        })
    }
}

impl fmt::Debug for FrameMsg {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(match *self {
            FrameMsg::UpdateEpoch(..) => "FrameMsg::UpdateEpoch",
            FrameMsg::HitTest(..) => "FrameMsg::HitTest",
            FrameMsg::RequestHitTester(..) => "FrameMsg::RequestHitTester",
            FrameMsg::SetScrollOffsets(..) => "FrameMsg::SetScrollOffsets",
            FrameMsg::ResetDynamicProperties => "FrameMsg::ResetDynamicProperties",
            FrameMsg::AppendDynamicProperties(..) => "FrameMsg::AppendDynamicProperties",
            FrameMsg::AppendDynamicTransformProperties(..) => "FrameMsg::AppendDynamicTransformProperties",
            FrameMsg::SetIsTransformAsyncZooming(..) => "FrameMsg::SetIsTransformAsyncZooming",
            FrameMsg::SetMinimapData(..) => "FrameMsg::SetMinimapData",
        })
    }
}

bitflags!{
    /// Bit flags for WR stages to store in a capture.
    // Note: capturing `FRAME` without `SCENE` is not currently supported.
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct CaptureBits: u8 {
        ///
        const SCENE = 0x1;
        ///
        const FRAME = 0x2;
        ///
        const TILE_CACHE = 0x4;
        ///
        const EXTERNAL_RESOURCES = 0x8;
    }
}

bitflags!{
    /// Mask for clearing caches in debug commands.
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct ClearCache: u8 {
        ///
        const IMAGES = 0b1;
        ///
        const GLYPHS = 0b10;
        ///
        const GLYPH_DIMENSIONS = 0b100;
        ///
        const RENDER_TASKS = 0b1000;
        ///
        const TEXTURE_CACHE = 0b10000;
        /// Clear render target pool
        const RENDER_TARGETS = 0b100000;
    }
}

/// Information about a loaded capture of each document
/// that is returned by `RenderBackend`.
#[derive(Clone, Debug)]
pub struct CapturedDocument {
    ///
    pub document_id: DocumentId,
    ///
    pub root_pipeline_id: Option<PipelineId>,
}

/// Update of the state of built-in debugging facilities.
pub enum DebugCommand {
    /// Sets the provided debug flags.
    SetFlags(DebugFlags),
    /// Get current debug flags
    GetDebugFlags(Sender<DebugFlags>),
    /// Enable/Disable render command logging.
    SetRenderCommandLog(bool),
    /// Save a capture of all the documents state.
    SaveCapture(PathBuf, CaptureBits),
    /// Load a capture of all the documents state.
    LoadCapture(PathBuf, Option<(u32, u32)>, Sender<CapturedDocument>),
    /// Start capturing a sequence of scene/frame changes.
    StartCaptureSequence(PathBuf, CaptureBits),
    /// Stop capturing a sequence of scene/frame changes.
    StopCaptureSequence,
    /// Clear cached resources, forcing them to be re-uploaded from templates.
    ClearCaches(ClearCache),
    /// Sets the maximum amount of existing batches to visit before creating a new one.
    SetBatchingLookback(u32),
    /// Causes the scene builder to pause for a given amount of milliseconds each time it
    /// processes a transaction.
    SimulateLongSceneBuild(u32),
    /// Set an override tile size to use for picture caches
    SetPictureTileSize(Option<DeviceIntSize>),
    /// Set an override for max off-screen surface size
    SetMaximumSurfaceSize(Option<usize>),
    /// Generate a frame to force a redraw / recomposite
    GenerateFrame,
    #[cfg(feature = "debugger")]
    /// Query internal information about WR
    Query(DebugQuery),
    #[cfg(feature = "debugger")]
    /// Add a new profiler consumer
    AddDebugClient(DebuggerClient),
    #[cfg(feature = "debugger")]
    /// Capture the next composited frame with RenderDoc, replying with the
    /// written .rdc path (or an error message).
    CaptureRenderDoc(Sender<crate::api::debugger::RenderDocReply>),
    #[cfg(feature = "debugger")]
    /// Replace the per-primitive debug override (disabled / highlighted
    /// primitives) of the window's documents. Replies with an error message
    /// if the override targets a stale scene generation.
    SetSceneDebugOverride(crate::api::debugger::SceneDebugOverride, Sender<Result<(), String>>),
    #[cfg(feature = "debugger")]
    /// Replace the source of one `.glsl` file, or drop the override when the
    /// source is `None`, and rebuild the shaders it affects. Replies with the
    /// compile diagnostics if any affected shader failed to build.
    SetShaderSource(String, Option<String>, Sender<crate::api::debugger::ShaderReloadReply>),
}

/// Initial state handed to `RenderBackend::register_window`.
///
/// Sent across the api channel via `ApiMsg::RegisterWindow`, so all fields
/// must be `Send`. The `ResourceCache` itself isn't carried here — instead
/// we ship the [`ResourceCacheInit`] params so that the backend thread
/// constructs `ResourceCache` (and its sub-caches) locally. This keeps the
/// allocations attributed to the thread that ultimately owns them.
pub struct WindowRegistration {
    /// The id assigned to the window. Used to route subsequent messages.
    pub id: RenderBackendId,
    /// Channel the backend uses to publish frames / texture updates back
    /// to the `Renderer` that owns this window.
    pub result_tx: Sender<ResultMsg>,
    /// Notifier used to wake up the renderer when frames are ready.
    pub notifier: Box<dyn RenderNotifier>,
    /// Optional sampler invoked just before frame building.
    pub sampler: Option<Box<dyn AsyncPropertySampler + Send>>,
    /// Params used to construct the window's `ResourceCache` on the
    /// render-backend thread.
    pub resource_cache: ResourceCacheInit,
    /// Pool of large memory chunks used by per-frame allocators.
    pub chunk_pool: Arc<ChunkPool>,
    /// Initial frame-builder configuration.
    pub frame_config: FrameBuilderConfig,
    /// Initial debug flags.
    pub debug_flags: DebugFlags,
}

/// Parameters needed to construct a window's `ResourceCache` (plus its
/// sub-caches) on the render backend thread.
pub struct ResourceCacheInit {
    /// Maximum texture size for the texture cache.
    pub max_internal_texture_size: i32,
    /// Threshold above which images get tiled.
    pub image_tiling_threshold: i32,
    /// Preferred color formats reported by the device.
    pub color_cache_formats: TextureFormatPair<ImageFormat>,
    /// Swizzle settings reported by the device.
    pub swizzle_settings: Option<SwizzleSettings>,
    /// Per-window texture cache budget configuration.
    pub texture_cache_config: TextureCacheConfig,
    /// Tile size used for picture caches.
    pub picture_tile_size: api::units::DeviceIntSize,
    /// Filter used for picture-cache textures.
    pub picture_texture_filter: TextureFilter,
    /// Worker thread pool used by the glyph rasterizer.
    pub workers: Arc<ThreadPool>,
    /// Optional dedicated glyph raster thread handle.
    pub dedicated_glyph_raster_thread: Option<GlyphRasterThread>,
    /// Whether the device supports r8 texture uploads.
    pub supports_r8_texture_upload: bool,
    /// Shared font resources used by the scene builder and frame builder.
    pub fonts: SharedFontResources,
    /// Optional blob image handler used to rasterize blob images for this window.
    pub blob_image_handler: Option<Box<dyn crate::api::BlobImageHandler>>,
    /// Whether the resource cache may use parallel work.
    pub enable_multithreading: bool,
}

/// Message sent by the `RenderApi` to the render backend thread.
pub enum ApiMsg {
    /// Adds a new document namespace.
    CloneApi(SyncSender<IdNamespace>),
    /// Adds a new document namespace.
    CloneApiByClient(IdNamespace),
    /// Register a new window on this backend thread. Sent once per window,
    /// before any `AddDocument` referencing the same `RenderBackendId`.
    RegisterWindow(Box<WindowRegistration>),
    /// Unregister a window from this render backend thread.
    ///
    /// If a channel is passed via the second argument, an empty message will
    /// be sent on it after the window is unregistered. It can be used for
    /// synchronzation.
    UnregisterWindow(RenderBackendId, Option<SyncSender<()>>),
    /// Adds a new document with given initial size, owned by the given window.
    AddDocument(DocumentId, DeviceIntSize, RenderBackendId),
    /// A message targeted at a particular document.
    UpdateDocuments(Vec<Box<TransactionMsg>>),
    /// Releases inactive render targets and, optionally, transient upload
    /// buffers without clearing persistent caches.
    TrimTransientResources {
        /// Window whose transient resources should be released.
        backend_id: RenderBackendId,
        /// Also release transient renderer upload buffers.
        trim_upload_buffers: bool,
    },
    /// Flush from the caches anything that isn't necessary, to free some memory.
    MemoryPressure,
    /// Collects a memory report.
    ReportMemory(SyncSender<Box<MemoryReport>>),
    /// Change debugging options, scoped to a specific window. The command
    /// only affects documents that belong to the given `RenderBackendId`,
    /// so callers can target one window in a shared backend pool without
    /// disturbing others.
    DebugCommand(RenderBackendId, DebugCommand),
    /// Message from the scene builder thread.
    SceneBuilderResult(SceneBuilderResult),
}

impl fmt::Debug for ApiMsg {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(match *self {
            ApiMsg::CloneApi(..) => "ApiMsg::CloneApi",
            ApiMsg::CloneApiByClient(..) => "ApiMsg::CloneApiByClient",
            ApiMsg::RegisterWindow(..) => "ApiMsg::RegisterWindow",
            ApiMsg::UnregisterWindow(..) => "ApiMsg::UnregisterWindow",
            ApiMsg::AddDocument(..) => "ApiMsg::AddDocument",
            ApiMsg::UpdateDocuments(..) => "ApiMsg::UpdateDocuments",
            ApiMsg::TrimTransientResources { .. } => "ApiMsg::TrimTransientResources",
            ApiMsg::MemoryPressure => "ApiMsg::MemoryPressure",
            ApiMsg::ReportMemory(..) => "ApiMsg::ReportMemory",
            ApiMsg::DebugCommand(..) => "ApiMsg::DebugCommand",
            ApiMsg::SceneBuilderResult(..) => "ApiMsg::SceneBuilderResult",
        })
    }
}

/// Allows the API to communicate with WebRender.
///
/// This object is created along with the `Renderer` and it's main use from a
/// user perspective is to create one or several `RenderApi` objects.
pub struct RenderApiSender {
    api_sender: Sender<ApiMsg>,
    scene_sender: Sender<SceneBuilderRequest>,
    low_priority_scene_sender: Sender<SceneBuilderRequest>,
    /// The render backend window this sender (and any `RenderApi` cloned from
    /// it) routes documents to.
    backend_id: RenderBackendId,
    blob_image_handler: Option<Box<dyn BlobImageHandler>>,
    fonts: SharedFontResources,
    /// Keeps the `RenderBackendPool` (and therefore its backend / scene
    /// builder threads) alive for the lifetime of this sender and every
    /// `RenderApi` cloned from it. Without this the pool can drop while
    /// `RenderApi` instances still hold sender clones, and any `send` they
    /// issue during their own destructor (e.g. `delete_document` from
    /// `WebRenderAPI::~WebRenderAPI`) would panic on a receiver-closed
    /// channel.
    render_backend_pool: Arc<crate::render_backend_pool::RenderBackendPool>,
}

impl RenderApiSender {
    /// Used internally by the `Renderer`.
    pub fn new(
        api_sender: Sender<ApiMsg>,
        scene_sender: Sender<SceneBuilderRequest>,
        low_priority_scene_sender: Sender<SceneBuilderRequest>,
        backend_id: RenderBackendId,
        blob_image_handler: Option<Box<dyn BlobImageHandler>>,
        fonts: SharedFontResources,
        render_backend_pool: Arc<crate::render_backend_pool::RenderBackendPool>,
    ) -> Self {
        RenderApiSender {
            api_sender,
            scene_sender,
            low_priority_scene_sender,
            backend_id,
            blob_image_handler,
            fonts,
            render_backend_pool,
        }
    }

    /// Returns the `RenderBackendId` of the window this sender routes to.
    pub fn backend_id(&self) -> RenderBackendId {
        self.backend_id
    }

    /// Creates a new resource API object with a dedicated namespace.
    pub fn create_api(&self) -> RenderApi {
        let (sync_tx, sync_rx) = single_msg_channel();
        let msg = ApiMsg::CloneApi(sync_tx);
        self.api_sender.send(msg).expect("Failed to send CloneApi message");
        let namespace_id = sync_rx.recv().expect("Failed to receive CloneApi reply");
        RenderApi {
            api_sender: self.api_sender.clone(),
            scene_sender: self.scene_sender.clone(),
            low_priority_scene_sender: self.low_priority_scene_sender.clone(),
            backend_id: self.backend_id,
            namespace_id,
            next_id: Cell::new(ResourceId(0)),
            resources: ApiResources::new(
                self.blob_image_handler.as_ref().map(|handler| handler.create_similar()),
                self.fonts.clone(),
            ),
            render_backend_pool: self.render_backend_pool.clone(),
        }
    }

    /// Creates a new resource API object with a dedicated namespace.
    /// Namespace id is allocated by client.
    ///
    /// The function could be used only when WebRenderOptions::namespace_alloc_by_client is true.
    /// When the option is true, create_api() could not be used to prevent namespace id conflict.
    pub fn create_api_by_client(&self, namespace_id: IdNamespace) -> RenderApi {
        let msg = ApiMsg::CloneApiByClient(namespace_id);
        self.api_sender.send(msg).expect("Failed to send CloneApiByClient message");
        RenderApi {
            api_sender: self.api_sender.clone(),
            scene_sender: self.scene_sender.clone(),
            low_priority_scene_sender: self.low_priority_scene_sender.clone(),
            backend_id: self.backend_id,
            namespace_id,
            next_id: Cell::new(ResourceId(0)),
            resources: ApiResources::new(
                self.blob_image_handler.as_ref().map(|handler| handler.create_similar()),
                self.fonts.clone(),
            ),
            render_backend_pool: self.render_backend_pool.clone(),
        }
    }
}

/// The main entry point to interact with WebRender.
pub struct RenderApi {
    api_sender: Sender<ApiMsg>,
    scene_sender: Sender<SceneBuilderRequest>,
    low_priority_scene_sender: Sender<SceneBuilderRequest>,
    backend_id: RenderBackendId,
    namespace_id: IdNamespace,
    next_id: Cell<ResourceId>,
    resources: ApiResources,
    /// Keeps the `RenderBackendPool` alive as long as this `RenderApi` is
    /// alive. The pool's `Drop` impl tears down the backend / scene-builder
    /// threads, so until every `RenderApi` is gone we must keep those
    /// threads (and their receivers) running — otherwise the senders here
    /// would panic on a closed channel.
    render_backend_pool: Arc<crate::render_backend_pool::RenderBackendPool>,
}

impl RenderApi {
    /// Returns the namespace ID used by this API object.
    pub fn get_namespace_id(&self) -> IdNamespace {
        self.namespace_id
    }

    /// Returns the `RenderBackendId` of the window this API targets.
    pub fn backend_id(&self) -> RenderBackendId {
        self.backend_id
    }

    /// Returns a clone of the API message sender for internal use
    #[allow(unused)]
    pub(crate) fn get_api_sender(&self) -> Sender<ApiMsg> {
        self.api_sender.clone()
    }

    ///
    pub fn create_sender(&self) -> RenderApiSender {
        RenderApiSender::new(
            self.api_sender.clone(),
            self.scene_sender.clone(),
            self.low_priority_scene_sender.clone(),
            self.backend_id,
            self.resources.blob_image_handler.as_ref().map(|handler| handler.create_similar()),
            self.resources.get_fonts(),
            self.render_backend_pool.clone(),
        )
    }

    /// Add a document to the WebRender instance.
    ///
    /// Instances can manage one or several documents (using the same render backend thread).
    /// Each document will internally correspond to a single scene, and scenes are made of
    /// one or several pipelines.
    pub fn add_document(&self, initial_size: DeviceIntSize) -> DocumentId {
        let new_id = self.next_unique_id();
        self.add_document_with_id(initial_size, new_id)
    }

    /// See `add_document`
    pub fn add_document_with_id(&self,
                                initial_size: DeviceIntSize,
                                id: u32) -> DocumentId {
        let initial_size = clamp_window_size(initial_size);

        let document_id = DocumentId::new(self.namespace_id, id);

        // We send this message to both the render backend and the scene builder instead of having
        // the scene builder thread forward it to the render backend as we do elswhere. This is because
        // some transactions can skip the scene builder thread and we want to avoid them arriving before
        // the render backend knows about the existence of the corresponding document id.
        // It may not be necessary, though.
        self.api_sender.send(
            ApiMsg::AddDocument(document_id, initial_size, self.backend_id)
        ).unwrap();
        self.scene_sender.send(
            SceneBuilderRequest::AddDocument(document_id, initial_size, self.backend_id)
        ).unwrap();

        document_id
    }

    /// Delete a document.
    pub fn delete_document(&self, document_id: DocumentId) {
        self.low_priority_scene_sender.send(
            SceneBuilderRequest::DeleteDocument(document_id)
        ).unwrap();
    }

    /// Generate a new font key
    pub fn generate_font_key(&self) -> FontKey {
        let new_id = self.next_unique_id();
        FontKey::new(self.namespace_id, new_id)
    }

    /// Generate a new font instance key
    pub fn generate_font_instance_key(&self) -> FontInstanceKey {
        let new_id = self.next_unique_id();
        FontInstanceKey::new(self.namespace_id, new_id)
    }

    /// Gets the dimensions for the supplied glyph keys
    ///
    /// Note: Internally, the internal texture cache doesn't store
    /// 'empty' textures (height or width = 0)
    /// This means that glyph dimensions e.g. for spaces (' ') will mostly be None.
    pub fn get_glyph_dimensions(
        &self,
        key: FontInstanceKey,
        glyph_indices: Vec<GlyphIndex>,
    ) -> Vec<Option<GlyphDimensions>> {
        let (sender, rx) = single_msg_channel();
        let msg = SceneBuilderRequest::GetGlyphDimensions(self.backend_id, GlyphDimensionRequest {
            key,
            glyph_indices,
            sender
        });
        self.low_priority_scene_sender.send(msg).unwrap();
        rx.recv().unwrap()
    }

    /// Gets the glyph indices for the supplied string. These
    /// can be used to construct GlyphKeys.
    pub fn get_glyph_indices(&self, key: FontKey, text: &str) -> Vec<Option<u32>> {
        let (sender, rx) = single_msg_channel();
        let msg = SceneBuilderRequest::GetGlyphIndices(self.backend_id, GlyphIndexRequest {
            key,
            text: text.to_string(),
            sender,
        });
        self.low_priority_scene_sender.send(msg).unwrap();
        rx.recv().unwrap()
    }

    /// Creates an `ImageKey`.
    pub fn generate_image_key(&self) -> ImageKey {
        let new_id = self.next_unique_id();
        ImageKey::new(self.namespace_id, new_id)
    }

    /// Creates a `BlobImageKey`.
    pub fn generate_blob_image_key(&self) -> BlobImageKey {
        BlobImageKey(self.generate_image_key())
    }

    /// A Gecko-specific notification mechanism to get some code executed on the
    /// `Renderer`'s thread, mostly replaced by `NotificationHandler`. You should
    /// probably use the latter instead.
    pub fn send_external_event(&self, evt: ExternalEvent) {
        let msg = SceneBuilderRequest::ExternalEvent(self.backend_id, evt);
        self.low_priority_scene_sender.send(msg).unwrap();
    }

    /// Notify WebRender that now is a good time to flush caches and release
    /// as much memory as possible.
    pub fn notify_memory_pressure(&self) {
        self.api_sender.send(ApiMsg::MemoryPressure).unwrap();
    }

    /// Synchronously requests memory report.
    pub fn report_memory(&self, _ops: malloc_size_of::MallocSizeOfOps) -> MemoryReport {
        let (tx, rx) = single_msg_channel();
        self.api_sender.send(ApiMsg::ReportMemory(tx)).unwrap();
        *rx.recv().unwrap()
    }

    /// Update debugging flags.
    pub fn set_debug_flags(&mut self, flags: DebugFlags) {
        self.resources.set_debug_flags(flags);
        let cmd = DebugCommand::SetFlags(flags);
        self.api_sender.send(ApiMsg::DebugCommand(self.backend_id, cmd)).unwrap();
        self.scene_sender.send(SceneBuilderRequest ::SetFlags(flags)).unwrap();
        self.low_priority_scene_sender.send(SceneBuilderRequest ::SetFlags(flags)).unwrap();
    }

    /// Drain barrier for shutdown.
    ///
    /// Synchronously round-trips a stop request along the slowest path a
    /// transaction can take -- low-priority scene builder, then scene builder,
    /// then out on the api channel to the render backend -- so that by the time
    /// the backend acks it, everything submitted before this call has been
    /// processed and written to `result_tx`, and the window has been marked
    /// stopped so nothing further will be. After this returns it is safe for the
    /// caller to drop the `Renderer` (and its `result_rx`).
    ///
    /// Taking the slow path is what makes this a barrier: a request sent on the
    /// low-priority channel cannot overtake work queued earlier on either scene
    /// channel, and the ack cannot overtake api-channel messages queued earlier,
    /// because the backend answers it in FIFO order with those.
    ///
    /// The window stays registered. The render backend thread keeps
    /// running so `RunOnRenderThread` (which delivers events via
    /// `notifier.external_event` *through* the backend) still works. Call
    /// `shut_down(true)` afterwards to actually unregister the window.
    pub fn stop_render_backend(&self) {
        let (tx, rx) = single_msg_channel();
        if self.low_priority_scene_sender
            .send(SceneBuilderRequest::StopWindow(self.backend_id, tx))
            .is_ok()
        {
            let _ = rx.recv();
        }
    }

    /// Shut the WebRender instance down.
    ///
    /// `synchronously` controls whether the call blocks for the backend
    /// to fully drop the window's state. When `true` this provides the
    /// same drain-before-destroy guarantee as `stop_render_backend`.
    ///
    /// Calling `stop_render_backend` followed by `shut_down(true)` is
    /// safe — by the time `shut_down` runs the backend may have already
    /// exited (and closed the api channel). In that case the send is a
    /// no-op and the function returns immediately.
    pub fn shut_down(&self, synchronously: bool) {
        if synchronously {
            let (tx, rx) = single_msg_channel();
            if self.api_sender
                .send(ApiMsg::UnregisterWindow(self.backend_id, Some(tx)))
                .is_ok()
            {
                let _ = rx.recv();
            }
        } else {
            // Fire-and-forget: the caller opted out of the drain barrier.
            let _ = self.api_sender.send(
                ApiMsg::UnregisterWindow(self.backend_id, None),
            );
        }
    }

    /// Create a new unique key that can be used for
    /// animated property bindings.
    pub fn generate_property_binding_key<T: Copy>(&self) -> PropertyBindingKey<T> {
        let new_id = self.next_unique_id();
        PropertyBindingKey {
            id: PropertyBindingId {
                namespace: self.namespace_id,
                uid: new_id,
            },
            _phantom: PhantomData,
        }
    }

    #[inline]
    fn next_unique_id(&self) -> u32 {
        let ResourceId(id) = self.next_id.get();
        self.next_id.set(ResourceId(id + 1));
        id
    }

    // For use in Wrench only
    #[doc(hidden)]
    pub fn send_message(&self, msg: ApiMsg) {
        self.api_sender.send(msg).unwrap();
    }

    /// Creates a transaction message from a single frame message.
    fn frame_message(&self, msg: FrameMsg, document_id: DocumentId) -> Box<TransactionMsg> {
        Box::new(TransactionMsg {
            document_id,
            scene_ops: Vec::new(),
            frame_ops: vec![msg],
            resource_updates: Vec::new(),
            notifications: Vec::new(),
            generate_frame: GenerateFrame::No,
            creation_time: None,
            invalidate_rendered_frame: false,
            use_scene_builder_thread: false,
            low_priority: false,
            blob_rasterizer: None,
            blob_requests: Vec::new(),
            rasterized_blobs: Vec::new(),
            profile: TransactionProfile::new(),
            render_reasons: RenderReasons::empty(),
        })
    }

    /// A helper method to send document messages.
    fn send_frame_msg(&self, document_id: DocumentId, msg: FrameMsg) {
        // This assertion fails on Servo use-cases, because it creates different
        // `RenderApi` instances for layout and compositor.
        //assert_eq!(document_id.0, self.namespace_id);
        self.api_sender
            .send(ApiMsg::UpdateDocuments(vec![self.frame_message(msg, document_id)]))
            .unwrap()
    }

    /// Send a transaction to WebRender.
    pub fn send_transaction(&mut self, document_id: DocumentId, transaction: Transaction) {
        let mut transaction = transaction.finalize(document_id);

        self.resources.update(&mut transaction);

        if transaction.generate_frame.as_bool() {
            transaction.profile.start_time(profiler::API_SEND_TIME);
            transaction.profile.start_time(profiler::TOTAL_FRAME_CPU_TIME);
        }

        if transaction.use_scene_builder_thread {
            let sender = if transaction.low_priority {
                &mut self.low_priority_scene_sender
            } else {
                &mut self.scene_sender
            };

            sender.send(SceneBuilderRequest::Transactions(vec![transaction]))
                .expect("send by scene sender failed");
        } else {
            self.api_sender.send(ApiMsg::UpdateDocuments(vec![transaction]))
                .expect("send by api sender failed");
        }
    }

    /// Does a hit test on display items in the specified document, at the given
    /// point. If a pipeline_id is specified, it is used to further restrict the
    /// hit results so that only items inside that pipeline are matched. The vector
    /// of hit results will contain all display items that match, ordered from
    /// front to back.
    pub fn hit_test(&self,
        document_id: DocumentId,
        point: WorldPoint,
    ) -> HitTestResult {
        let (tx, rx) = single_msg_channel();

        self.send_frame_msg(
            document_id,
            FrameMsg::HitTest(point, tx)
        );
        rx.recv().unwrap()
    }

    /// Synchronously request an object that can perform fast hit testing queries.
    pub fn request_hit_tester(&self, document_id: DocumentId) -> HitTesterRequest {
        let (tx, rx) = single_msg_channel();
        self.send_frame_msg(
            document_id,
            FrameMsg::RequestHitTester(tx)
        );

        HitTesterRequest { rx }
    }

    // Some internal scheduling magic that leaked into the API.
    // Buckle up and see APZUpdater.cpp for more info about what this is about.
    #[doc(hidden)]
    pub fn wake_scene_builder(&self) {
        self.scene_sender.send(SceneBuilderRequest::WakeUp).unwrap();
    }

    /// Block until a round-trip to the scene builder thread has completed. This
    /// ensures that any transactions (including ones deferred to the scene
    /// builder thread) have been processed.
    pub fn flush_scene_builder(&self) {
        let (tx, rx) = single_msg_channel();
        self.low_priority_scene_sender.send(SceneBuilderRequest::Flush(tx)).unwrap();
        rx.recv().unwrap(); // Block until done.
    }

    /// Save a capture of the current frame state for debugging.
    pub fn save_capture(&self, path: PathBuf, bits: CaptureBits) {
        let msg = ApiMsg::DebugCommand(self.backend_id, DebugCommand::SaveCapture(path, bits));
        self.send_message(msg);
    }

    /// Load a capture of the current frame state for debugging.
    pub fn load_capture(&self, path: PathBuf, ids: Option<(u32, u32)>) -> Vec<CapturedDocument> {
        // First flush the scene builder otherwise async scenes might clobber
        // the capture we are about to load.
        self.flush_scene_builder();

        let (tx, rx) = unbounded_channel();
        let msg = ApiMsg::DebugCommand(self.backend_id, DebugCommand::LoadCapture(path, ids, tx));
        self.send_message(msg);

        let mut documents = Vec::new();
        while let Ok(captured_doc) = rx.recv() {
            documents.push(captured_doc);
        }
        documents
    }

    /// Start capturing a sequence of frames.
    pub fn start_capture_sequence(&self, path: PathBuf, bits: CaptureBits) {
        let msg = ApiMsg::DebugCommand(self.backend_id, DebugCommand::StartCaptureSequence(path, bits));
        self.send_message(msg);
    }

    /// Stop capturing sequences of frames.
    pub fn stop_capture_sequence(&self) {
        let msg = ApiMsg::DebugCommand(self.backend_id, DebugCommand::StopCaptureSequence);
        self.send_message(msg);
    }

    /// Get the current debug flags
    pub fn get_debug_flags(&self) -> DebugFlags {
        let (tx, rx) = unbounded_channel();
        let msg = ApiMsg::DebugCommand(self.backend_id, DebugCommand::GetDebugFlags(tx));
        self.send_message(msg);
        rx.recv().unwrap()
    }

    /// Update the state of builtin debugging facilities.
    pub fn send_debug_cmd(&self, cmd: DebugCommand) {
        let msg = ApiMsg::DebugCommand(self.backend_id, cmd);
        self.send_message(msg);
    }

    /// Update a instance-global parameter.
    pub fn set_parameter(&mut self, parameter: Parameter) {
        if let Parameter::Bool(BoolParameter::Multithreading, enabled) = parameter {
            self.resources.enable_multithreading(enabled);
        }

        let _ = self.low_priority_scene_sender.send(
            SceneBuilderRequest::SetParameter(self.backend_id, parameter)
        );
    }
}

impl Drop for RenderApi {
    fn drop(&mut self) {
        let msg = SceneBuilderRequest::ClearNamespace(self.backend_id, self.namespace_id);
        let _ = self.low_priority_scene_sender.send(msg);
    }
}


fn clamp_window_size(size: DeviceIntSize) -> DeviceIntSize {
    // Anything bigger than this will crash later when attempting to create
    // a render task.
    use crate::api::MAX_RENDER_TASK_SIZE;
    if size.width > MAX_RENDER_TASK_SIZE || size.height > MAX_RENDER_TASK_SIZE {
        warn!("Clamping a {}x{} window/document", size.width, size.height);
    }
    DeviceIntSize::new(
        size.width.min(MAX_RENDER_TASK_SIZE),
        size.height.min(MAX_RENDER_TASK_SIZE),
    )
}

/// Collection of heap sizes, in bytes.
/// cbindgen:derive-eq=false
/// cbindgen:derive-ostream=false
#[repr(C)]
#[allow(missing_docs)]
#[derive(AddAssign, Clone, Debug, Default)]
pub struct MemoryReport {
    //
    // CPU Memory.
    //
    pub clip_stores: usize,
    pub hit_testers: usize,
    pub fonts: usize,
    pub weak_fonts: usize,
    pub images: usize,
    pub rasterized_blobs: usize,
    pub shader_cache: usize,
    pub interning: InterningMemoryReport,
    pub display_list: usize,
    pub upload_staging_memory: usize,
    pub swgl: usize,
    pub frame_allocator: usize,
    pub render_tasks: usize,

    //
    // GPU memory.
    //
    pub vertex_data_textures: usize,
    pub render_target_textures: usize,
    pub picture_tile_textures: usize,
    pub atlas_textures: usize,
    pub standalone_textures: usize,
    pub texture_cache_structures: usize,
    pub depth_target_textures: usize,
    pub texture_upload_pbos: usize,
    pub swap_chain: usize,
    pub render_texture_hosts: usize,
    pub upload_staging_textures: usize,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::api::{BlobImageResources, ImageDescriptorFlags};

    struct NullRasterizer;
    impl AsyncBlobImageRasterizer for NullRasterizer {
        fn rasterize(
            &mut self,
            _requests: &[BlobImageParams],
            _low_priority: bool,
            _tile_pool: &mut crate::api::BlobTilePool,
        ) -> Vec<(BlobImageRequest, BlobImageResult)> {
            Vec::new()
        }
    }

    struct NullBlobHandler;
    impl BlobImageHandler for NullBlobHandler {
        fn create_blob_rasterizer(&mut self) -> Box<dyn AsyncBlobImageRasterizer> {
            Box::new(NullRasterizer)
        }
        fn create_similar(&self) -> Box<dyn BlobImageHandler> {
            Box::new(NullBlobHandler)
        }
        fn prepare_resources(&mut self, _: &dyn BlobImageResources, _: &[BlobImageParams]) {}
        fn add(&mut self, _: BlobImageKey, _: Arc<BlobImageData>, _: &DeviceIntRect, _: TileSize) {}
        fn update(&mut self, _: BlobImageKey, _: Arc<BlobImageData>, _: &DeviceIntRect, _: &BlobDirtyRect) {}
        fn delete(&mut self, _: BlobImageKey) {}
        fn delete_font(&mut self, _: FontKey) {}
        fn delete_font_instance(&mut self, _: FontInstanceKey) {}
        fn clear_namespace(&mut self, _: IdNamespace) {}
        fn enable_multithreading(&mut self, _: bool) {}
    }

    #[test]
    fn delete_blob_image_in_same_transaction_as_add() {
        let namespace = IdNamespace(1);
        let mut resources = ApiResources::new(
            Some(Box::new(NullBlobHandler)),
            SharedFontResources::new(namespace),
        );
        let key = BlobImageKey(ImageKey::new(namespace, 1));
        let rect = DeviceIntRect::from_size(DeviceIntSize::new(64, 64));
        let descriptor = ImageDescriptor::new(64, 64, ImageFormat::BGRA8, ImageDescriptorFlags::empty());

        let mut txn = Transaction::new();
        txn.add_blob_image(key, descriptor, Arc::new(vec![0; 16]), rect, None);
        txn.delete_blob_image(key);
        let mut msg = txn.finalize(DocumentId::new(namespace, 0));
        resources.update(&mut msg);
        assert!(msg.blob_requests.is_empty());
    }
}

#[cfg(test)]
mod window_size_tests {
    use super::*;
    use crate::api::MAX_RENDER_TASK_SIZE;

    #[test]
    fn clamp_oversized_window() {
        assert_eq!(
            clamp_window_size(DeviceIntSize::new(470287, 29)),
            DeviceIntSize::new(MAX_RENDER_TASK_SIZE, 29),
        );
        assert_eq!(
            clamp_window_size(DeviceIntSize::new(1024, 768)),
            DeviceIntSize::new(1024, 768),
        );
    }
}
