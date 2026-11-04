/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::FontInstanceFlags;
use api::euclid::{Box2D, Scale};
use api::units::*;
use crate::command_buffer::PrimitiveCommand;
use crate::pattern::PatternKind;
use crate::renderer::GpuBufferAddress;
use crate::spatial_tree::{SpatialNodeIndex, SpatialTree};
use glyph_rasterizer::{GlyphFormat, SubpixelDirection};
use crate::gpu_types::{PrimitiveHeaders, ZBufferId, ZBufferIdGenerator};
use crate::gpu_types::SplitCompositeInstance;
use crate::gpu_types::{PrimitiveInstanceData, GlyphInstance};
use crate::gpu_types::{PrimitiveHeader, PrimitiveHeaderIndex};
use crate::gpu_types::{MaskInstance, QuadSegment};
use crate::internal_types::{FastHashMap, FrameAllocator, FrameMemory, FrameVec, Swizzle, TextureSource};
use crate::prim_store::PrimitiveKind;
use crate::prim_store::PrimitiveInstance;
use crate::prim_store::{ClipMaskKind, ClipTaskIndex};
use crate::path_tiler::PathTileInstance;
use crate::quad;
use crate::render_target::RenderTargetContext;
use crate::render_task_graph::{RenderTaskId, RenderTaskGraph};
use crate::render_task::RenderTaskAddress;
use crate::renderer::{BlendMode, GpuBufferBuilder, ShaderColorMode};
use crate::resource_cache::GlyphFetchResult;
use crate::space::SpaceMapper;
use crate::transform::TransformPalette;
use crate::visibility::{PrimitiveVisibilityFlags, DrawState};
use std::fmt;


// Special sentinel value recognized by the shader. It is considered to be
// a dummy task that doesn't mask out anything.
const OPAQUE_TASK_ADDRESS: RenderTaskAddress = RenderTaskAddress(0x7fffffff);

/// Used to signal there are no segments provided with this primitive.
pub const INVALID_SEGMENT_INDEX: i32 = 0xffff;

#[derive(Copy, Clone, PartialEq, Eq, Hash, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BatchKind {
    SplitComposite,
    TextRun(GlyphFormat),
    Quad(PatternKind),
}

/// Input textures for a primitive, without consideration of clip mask
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct TextureSet {
    pub colors: [TextureSource; 3],
}

impl TextureSet {
    const UNTEXTURED: TextureSet = TextureSet {
        colors: [
            TextureSource::Invalid,
            TextureSource::Invalid,
            TextureSource::Invalid,
        ],
    };

    /// A textured primitive
    fn prim_textured(
        color: TextureSource,
    ) -> Self {
        TextureSet {
            colors: [
                color,
                TextureSource::Invalid,
                TextureSource::Invalid,
            ],
        }
    }

    fn is_compatible_with(&self, other: &TextureSet) -> bool {
        self.colors[0].is_compatible(&other.colors[0]) &&
        self.colors[1].is_compatible(&other.colors[1]) &&
        self.colors[2].is_compatible(&other.colors[2])
    }
}

impl TextureSource {
    fn combine(&self, other: TextureSource) -> TextureSource {
        if other == TextureSource::Invalid {
            *self
        } else {
            other
        }
    }
}

/// Optional textures that can be used as a source in the shaders.
/// Textures that are not used by the batch are equal to TextureId::invalid().
#[derive(Copy, Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BatchTextures {
    pub input: TextureSet,
    pub clip_mask: TextureSource,
}

impl BatchTextures {
    /// An empty batch textures (no binding slots set)
    pub fn empty() -> BatchTextures {
        BatchTextures {
            input: TextureSet::UNTEXTURED,
            clip_mask: TextureSource::Invalid,
        }
    }

    /// A textured primitive with optional clip mask
    pub fn prim_textured(
        color: TextureSource,
        clip_mask: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet::prim_textured(color),
            clip_mask,
        }
    }

    /// An untextured primitive with optional clip mask
    pub fn prim_untextured(
        clip_mask: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet::UNTEXTURED,
            clip_mask,
        }
    }

    /// A composite style effect with single input texture
    pub fn composite_rgb(
        texture: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet {
                colors: [
                    texture,
                    TextureSource::Invalid,
                    TextureSource::Invalid,
                ],
            },
            clip_mask: TextureSource::Invalid,
        }
    }

    /// A composite style effect with up to 3 input textures
    pub fn composite_yuv(
        color0: TextureSource,
        color1: TextureSource,
        color2: TextureSource,
    ) -> BatchTextures {
        BatchTextures {
            input: TextureSet {
                colors: [color0, color1, color2],
            },
            clip_mask: TextureSource::Invalid,
        }
    }

    pub fn is_compatible_with(&self, other: &BatchTextures) -> bool {
        if !self.clip_mask.is_compatible(&other.clip_mask) {
            return false;
        }

        self.input.is_compatible_with(&other.input)
    }

    pub fn combine_textures(&self, other: BatchTextures) -> Option<BatchTextures> {
        if !self.is_compatible_with(&other) {
            return None;
        }

        let mut new_textures = BatchTextures::empty();

        new_textures.clip_mask = self.clip_mask.combine(other.clip_mask);

        for i in 0 .. 3 {
            new_textures.input.colors[i] = self.input.colors[i].combine(other.input.colors[i]);
        }

        Some(new_textures)
    }

    fn merge(&mut self, other: &BatchTextures) {
        self.clip_mask = self.clip_mask.combine(other.clip_mask);

        for (s, o) in self.input.colors.iter_mut().zip(other.input.colors.iter()) {
            *s = s.combine(*o);
        }
    }
}

#[derive(Copy, Clone, PartialEq, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct InlineReadback {
    pub src_task_id: RenderTaskId,
    pub readback_task_id: RenderTaskId,
}

#[derive(Copy, Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BatchKey {
    pub kind: BatchKind,
    pub blend_mode: BlendMode,
    pub textures: BatchTextures,
    // Prevent instances with different readback sources from going into the
    // same batch. We could relax this if we ensure that they do not overlap.
    pub readback: RenderTaskId,
}

impl BatchKey {
    pub fn new(kind: BatchKind, blend_mode: BlendMode, textures: BatchTextures) -> Self {
        BatchKey {
            kind,
            blend_mode,
            textures,
            readback: RenderTaskId::INVALID,
        }
    }

    pub fn is_compatible_with(&self, other: &BatchKey) -> bool {
        self.kind == other.kind
            && self.blend_mode == other.blend_mode
            && self.textures.is_compatible_with(&other.textures)
            && self.readback == other.readback
    }
}

/// Map a rect into the device space of the surface being batched.
///
/// Mirrors `SurfaceInfo::map_to_device_rect`, which is what produced the device
/// rects the commands carry, so that a rect derived here is comparable to them.
///
/// `bounds`, in raster space, limits how far a projected rect may extend; it
/// matters when the source space reaches the raster node through a perspective
/// transform, where an unbounded projection can blow up near the horizon.
fn map_to_device<F: fmt::Debug>(
    rect: &Box2D<f32, F>,
    from_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    bounds: RasterRect,
    device_pixel_scale: DevicePixelScale,
    spatial_tree: &SpatialTree,
) -> Option<DeviceRect> {
    let map_to_raster: SpaceMapper<F, RasterPixel> = SpaceMapper::new_with_target(
        raster_spatial_node_index,
        from_spatial_node_index,
        bounds,
        spatial_tree,
    );

    Some(map_to_raster.map(rect)? * raster_to_device(device_pixel_scale))
}

fn raster_to_device(device_pixel_scale: DevicePixelScale) -> Scale<f32, RasterPixel, DevicePixel> {
    Scale::new(device_pixel_scale.0)
}

pub struct BatchRects {
    /// Union of all of the batch's item rects.
    ///
    /// Very often we can skip iterating over item rects by testing against
    /// this one first.
    batch: DeviceRect,
    /// When the batch rectangle above isn't a good enough approximation, we
    /// store per item rects.
    items: Option<FrameVec<DeviceRect>>,
    // TODO: batch rects don't need to be part of the frame but they currently
    // are. It may be cleaner to remove them from the frame's final data structure
    // and not use the frame's allocator.
    allocator: FrameAllocator,
}

impl BatchRects {
    fn new(allocator: FrameAllocator) -> Self {
        BatchRects {
            batch: DeviceRect::zero(),
            items: None,
            allocator,
        }
    }

    #[inline]
    fn add_rect(&mut self, rect: &DeviceRect) {
        let union = self.batch.union(rect);
        // If we have already started storing per-item rects, continue doing so.
        // Otherwise, check whether only storing the batch rect is a good enough
        // approximation.
        if let Some(items) = &mut self.items {
            items.push(*rect);
        } else if self.batch.area() + rect.area() < union.area() {
            let mut items = self.allocator.clone().new_vec_with_capacity(16);
            items.push(self.batch);
            items.push(*rect);
            self.items = Some(items);
        }

        self.batch = union;
    }

    #[inline]
    fn intersects(&mut self, rect: &DeviceRect) -> bool {
        if !self.batch.intersects(rect) {
            return false;
        }

        if let Some(items) = &self.items {
            items.iter().any(|item| item.intersects(rect))
        } else {
            // If we don't have per-item rects it means the batch rect is a good
            // enough approximation and we didn't bother storing per-rect items.
            true
        }
    }
}


pub struct AlphaBatchList {
    pub batches: FrameVec<PrimitiveBatch>,
    pub batch_rects: FrameVec<BatchRects>,
    current_batch_index: usize,
    current_z_id: ZBufferId,
    break_advanced_blend_batches: bool,
}

impl AlphaBatchList {
    fn new(break_advanced_blend_batches: bool, preallocate: usize, memory: &FrameMemory) -> Self {
        AlphaBatchList {
            batches: memory.new_vec_with_capacity(preallocate),
            batch_rects: memory.new_vec_with_capacity(preallocate),
            current_z_id: ZBufferId::invalid(),
            current_batch_index: usize::MAX,
            break_advanced_blend_batches,
        }
    }

    /// Clear all current batches in this list. This is typically used
    /// when a primitive is encountered that occludes all previous
    /// content in this batch list.
    fn clear(&mut self) {
        self.current_batch_index = usize::MAX;
        self.current_z_id = ZBufferId::invalid();
        self.batches.clear();
        self.batch_rects.clear();
    }

    pub fn set_params_and_get_batch(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        // The bounding box of everything at this Z plane. We expect potentially
        // multiple primitive segments coming with the same `z_id`.
        z_bounding_rect: &DeviceRect,
        z_id: ZBufferId,
    ) -> &mut PrimitiveBatch {
        if z_id != self.current_z_id ||
           self.current_batch_index == usize::MAX ||
           !self.batches[self.current_batch_index].key.is_compatible_with(&key)
        {
            let mut selected_batch_index = None;

            match key.blend_mode {
                BlendMode::Advanced(_) if self.break_advanced_blend_batches => {
                    // don't try to find a batch
                }
                _ => {
                    for (batch_index, batch) in self.batches.iter().enumerate().rev() {
                        // For normal batches, we only need to check for overlaps for batches
                        // other than the first batch we consider. If the first batch
                        // is compatible, then we know there isn't any potential overlap
                        // issues to worry about.
                        if batch.key.is_compatible_with(&key) {
                            selected_batch_index = Some(batch_index);
                            break;
                        }

                        // check for intersections
                        if self.batch_rects[batch_index].intersects(z_bounding_rect) {
                            break;
                        }
                    }
                }
            }

            if selected_batch_index.is_none() {
                // Text runs tend to have a lot of instances per batch, causing a lot of reallocation
                // churn as items are added one by one, so we give it a head start. Ideally we'd start
                // with a larger number, closer to 1k but in some bad cases with lots of batch break
                // we would be wasting a lot of memory.
                // Generally it is safe to preallocate small-ish values for other batch kinds because
                // the items are small and there are no zero-sized batches so there will always be
                // at least one allocation.
                let prealloc = match key.kind {
                    BatchKind::TextRun(..) => 128,
                    _ => 16,
                };
                let mut new_batch = PrimitiveBatch::new(key, self.batches.allocator().clone());
                new_batch.instances.reserve(prealloc);
                selected_batch_index = Some(self.batches.len());
                self.batches.push(new_batch);
                self.batch_rects.push(BatchRects::new(self.batches.allocator().clone()));
            }

            self.current_batch_index = selected_batch_index.unwrap();
            self.batch_rects[self.current_batch_index].add_rect(z_bounding_rect);
            self.current_z_id = z_id;
        }

        let batch = &mut self.batches[self.current_batch_index];
        batch.features |= features;
        batch.key.textures.merge(&key.textures);

        batch
    }
}

pub struct OpaqueBatchList {
    pub pixel_area_threshold_for_new_batch: f32,
    pub batches: FrameVec<PrimitiveBatch>,
    pub current_batch_index: usize,
    lookback_count: usize,
}

impl OpaqueBatchList {
    fn new(pixel_area_threshold_for_new_batch: f32, lookback_count: usize, memory: &FrameMemory) -> Self {
        OpaqueBatchList {
            batches: memory.new_vec(),
            pixel_area_threshold_for_new_batch,
            current_batch_index: usize::MAX,
            lookback_count,
        }
    }

    /// Clear all current batches in this list. This is typically used
    /// when a primitive is encountered that occludes all previous
    /// content in this batch list.
    fn clear(&mut self) {
        self.current_batch_index = usize::MAX;
        self.batches.clear();
    }

    pub fn set_params_and_get_batch(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        // The bounding box of everything at the current Z, whatever it is. We expect potentially
        // multiple primitive segments produced by a primitive, which we allow to check
        // `current_batch_index` instead of iterating the batches.
        z_bounding_rect: &DeviceRect,
    ) -> &mut PrimitiveBatch {
        // If the area of this primitive is larger than the given threshold,
        // then it is large enough to warrant breaking a batch for. In this
        // case we just see if it can be added to the existing batch or
        // create a new one.
        let is_large_occluder = z_bounding_rect.area() > self.pixel_area_threshold_for_new_batch;
        // Since primitives of the same kind tend to come in succession, we keep track
        // of the current batch index to skip the search in some cases. We ignore the
        // current batch index in the case of large occluders to make sure they get added
        // at the top of the bach list.
        if is_large_occluder || self.current_batch_index == usize::MAX ||
           !self.batches[self.current_batch_index].key.is_compatible_with(&key) {
            let mut selected_batch_index = None;
            if is_large_occluder {
                if let Some(batch) = self.batches.last() {
                    if batch.key.is_compatible_with(&key) {
                        selected_batch_index = Some(self.batches.len() - 1);
                    }
                }
            } else {
                // Otherwise, look back through a reasonable number of batches.
                for (batch_index, batch) in self.batches.iter().enumerate().rev().take(self.lookback_count) {
                    if batch.key.is_compatible_with(&key) {
                        selected_batch_index = Some(batch_index);
                        break;
                    }
                }
            }

            if selected_batch_index.is_none() {
                let new_batch = PrimitiveBatch::new(key, self.batches.allocator().clone());
                selected_batch_index = Some(self.batches.len());
                self.batches.push(new_batch);
            }

            self.current_batch_index = selected_batch_index.unwrap();
        }

        let batch = &mut self.batches[self.current_batch_index];
        batch.features |= features;
        batch.key.textures.merge(&key.textures);

        batch
    }

    fn finalize(&mut self) {
        // Reverse the instance arrays in the opaque batches
        // to get maximum z-buffer efficiency by drawing
        // front-to-back.
        // TODO(gw): Maybe we can change the batch code to
        //           build these in reverse and avoid having
        //           to reverse the instance array here.
        for batch in &mut self.batches {
            batch.instances.reverse();
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveBatch {
    pub key: BatchKey,
    pub instances: FrameVec<PrimitiveInstanceData>,
    pub readback: Option<InlineReadback>,
    pub features: BatchFeatures,
}

bitflags! {
    /// Features of the batch that, if not requested, may allow a fast-path.
    ///
    /// Rather than breaking batches when primitives request different features,
    /// we always request the minimum amount of features to satisfy all items in
    /// the batch.
    /// The goal is to let the renderer be optionally select more specialized
    /// versions of a shader if the batch doesn't require code certain code paths.
    /// Not all shaders necessarily implement all of these features.
    #[cfg_attr(feature = "capture", derive(Serialize))]
    #[cfg_attr(feature = "replay", derive(Deserialize))]
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct BatchFeatures: u8 {
        const ALPHA_PASS = 1 << 0;
        const ANTIALIASING = 1 << 1;
        const REPETITION = 1 << 2;
        /// Indicates a primitive in this batch may use a clip mask.
        const CLIP_MASK = 1 << 3;
    }
}

impl PrimitiveBatch {
    fn new(key: BatchKey, allocator: FrameAllocator) -> PrimitiveBatch {
        PrimitiveBatch {
            key,
            instances: FrameVec::new_in(allocator),
            readback: None,
            features: BatchFeatures::empty(),
        }
    }

    fn merge(&mut self, other: PrimitiveBatch) {
        self.instances.extend(other.instances);
        self.features |= other.features;
        self.key.textures.merge(&other.key.textures);
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct AlphaBatchContainer {
    pub opaque_batches: FrameVec<PrimitiveBatch>,
    pub alpha_batches: FrameVec<PrimitiveBatch>,
    /// The overall scissor rect for this render task, if one
    /// is required.
    pub task_scissor_rect: Option<DeviceIntRect>,
    /// The rectangle of the owning render target that this
    /// set of batches affects.
    pub task_rect: DeviceIntRect,
}

impl AlphaBatchContainer {
    pub fn new(
        task_scissor_rect: Option<DeviceIntRect>,
        memory: &FrameMemory,
    ) -> AlphaBatchContainer {
        AlphaBatchContainer {
            opaque_batches: memory.new_vec(),
            alpha_batches: memory.new_vec(),
            task_scissor_rect,
            task_rect: DeviceIntRect::zero(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.opaque_batches.is_empty() &&
        self.alpha_batches.is_empty()
    }

    fn merge(&mut self, builder: AlphaBatchBuilder, task_rect: &DeviceIntRect) {
        self.task_rect = self.task_rect.union(task_rect);

        for other_batch in builder.opaque_batch_list.batches {
            let batch_index = self.opaque_batches.iter().position(|batch| {
                batch.key.is_compatible_with(&other_batch.key)
            });

            match batch_index {
                Some(batch_index) => {
                    self.opaque_batches[batch_index].merge(other_batch);
                }
                None => {
                    self.opaque_batches.push(other_batch);
                }
            }
        }

        let mut min_batch_index = 0;

        for other_batch in builder.alpha_batch_list.batches {
            let batch_index = self.alpha_batches.iter().skip(min_batch_index).position(|batch| {
                batch.key.is_compatible_with(&other_batch.key)
            });

            match batch_index {
                Some(batch_index) => {
                    let index = batch_index + min_batch_index;
                    self.alpha_batches[index].merge(other_batch);
                    min_batch_index = index;
                }
                None => {
                    self.alpha_batches.push(other_batch);
                    min_batch_index = self.alpha_batches.len();
                }
            }
        }
    }
}

/// Encapsulates the logic of building batches for items that are blended.
pub struct AlphaBatchBuilder {
    pub alpha_batch_list: AlphaBatchList,
    pub opaque_batch_list: OpaqueBatchList,
    pub render_task_id: RenderTaskId,
    render_task_address: RenderTaskAddress,
}

impl AlphaBatchBuilder {
    pub fn new(
        screen_size: DeviceIntSize,
        break_advanced_blend_batches: bool,
        lookback_count: usize,
        render_task_id: RenderTaskId,
        render_task_address: RenderTaskAddress,
        memory: &FrameMemory,
    ) -> Self {
        // The threshold for creating a new batch is one quarter of the screen
        // area, in device pixels - the same units the item rects are in.
        let batch_area_threshold = (screen_size.width * screen_size.height) as f32 / 4.0;

        AlphaBatchBuilder {
            alpha_batch_list: AlphaBatchList::new(break_advanced_blend_batches, 128, memory),
            opaque_batch_list: OpaqueBatchList::new(batch_area_threshold, lookback_count, memory),
            render_task_id,
            render_task_address,
        }
    }

    /// Clear all current batches in this builder. This is typically used
    /// when a primitive is encountered that occludes all previous
    /// content in this batch list.
    fn clear(&mut self) {
        self.alpha_batch_list.clear();
        self.opaque_batch_list.clear();
    }

    pub fn build(
        mut self,
        batch_containers: &mut FrameVec<AlphaBatchContainer>,
        merged_batches: &mut AlphaBatchContainer,
        task_rect: DeviceIntRect,
        task_scissor_rect: Option<DeviceIntRect>,
    ) {
        self.opaque_batch_list.finalize();

        if task_scissor_rect.is_none() {
            merged_batches.merge(self, &task_rect);
        } else {
            batch_containers.push(AlphaBatchContainer {
                alpha_batches: self.alpha_batch_list.batches,
                opaque_batches: self.opaque_batch_list.batches,
                task_scissor_rect,
                task_rect,
            });
        }
    }

    pub fn push_single_instance(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        bounding_rect: &DeviceRect,
        z_id: ZBufferId,
        instance: PrimitiveInstanceData,
    ) {
        self.set_params_and_get_batch(key, features, None, bounding_rect, z_id)
            .push(instance);
    }

    pub fn set_params_and_get_batch(
        &mut self,
        key: BatchKey,
        features: BatchFeatures,
        readback: Option<&InlineReadback>,
        bounding_rect: &DeviceRect,
        z_id: ZBufferId,
    ) -> &mut FrameVec<PrimitiveInstanceData> {
        let batch = match key.blend_mode {
            BlendMode::None if readback.is_none() => {
                self.opaque_batch_list
                    .set_params_and_get_batch(key, features, bounding_rect)
            }
            _ => {
                let batch = self.alpha_batch_list
                    .set_params_and_get_batch(key, features, bounding_rect, z_id);

                if let Some(readback) = readback {
                    // Sanity check. A batch can only have a one or no inline readback.
                    debug_assert!(
                        // Either it doesn't have one (a new batch)...
                        (batch.readback.is_none() && batch.instances.is_empty())
                        // ... or it has one and it is the same.
                        || batch.readback == Some(*readback)
                    );
                    batch.readback = Some(*readback);
                }

                batch
            }
        };

        &mut batch.instances
    }
}

/// Supports (recursively) adding a list of primitives and pictures to an alpha batch
/// builder. In future, it will support multiple dirty regions / slices, allowing the
/// contents of a picture to be spliced into multiple batch builders.
pub struct BatchBuilder {
    /// A temporary buffer that is used during glyph fetching, stored here
    /// to reduce memory allocations.
    glyph_fetch_buffer: Vec<GlyphFetchResult>,

    batcher: AlphaBatchBuilder,
}

impl BatchBuilder {
    pub fn new(batcher: AlphaBatchBuilder) -> Self {
        BatchBuilder {
            glyph_fetch_buffer: Vec::new(),
            batcher,
        }
    }

    pub fn finalize(self) -> AlphaBatchBuilder {
        self.batcher
    }

    fn add_split_composite_instance_to_batches(
        &mut self,
        batch_key: BatchKey,
        features: BatchFeatures,
        bounding_rect: &DeviceRect,
        z_id: ZBufferId,
        prim_header_index: PrimitiveHeaderIndex,
        polygons_address: i32,
    ) {
        let render_task_address = self.batcher.render_task_address;

        self.batcher.push_single_instance(
            batch_key,
            features,
            bounding_rect,
            z_id,
            PrimitiveInstanceData::from(SplitCompositeInstance {
                prim_header_index,
                render_task_address,
                polygons_address,
                z: z_id,
            }),
        );
    }

    /// Clear all current batchers. This is typically used when a primitive
    /// is encountered that occludes all previous content in this batch list.
    fn clear_batches(&mut self) {
        self.batcher.clear();
    }

    // Adds a primitive to a batch.
    // It can recursively call itself in some situations, for
    // example if it encounters a picture where the items
    // in that picture are being drawn into the same target.
    pub fn add_prim_to_batch(
        &mut self,
        cmd: &PrimitiveCommand,
        prim_spatial_node_index: SpatialNodeIndex,
        ctx: &RenderTargetContext,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
        transforms: &mut TransformPalette,
        root_spatial_node_index: SpatialNodeIndex,
        surface_spatial_node_index: SpatialNodeIndex,
        device_pixel_scale: DevicePixelScale,
        z_generator: &mut ZBufferIdGenerator,
        prim_instances: &[PrimitiveInstance],
        gpu_buffer_builder: &mut GpuBufferBuilder,
        segments: &[QuadSegment],
    ) {
        let (draw_index, bounding_rect) = match cmd {
            PrimitiveCommand::Simple { draw_index, device_rect } => {
                (draw_index, device_rect)
            }
            PrimitiveCommand::SplitComposite { draw_index, device_rect, polygons_address, transform_id, src_task_id, pattern_rect } => {
                let prim_info = ctx.scratch.frame.draw(*draw_index);

                let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                let (uv_rect_address, texture) = render_tasks.resolve_location(*src_task_id).unwrap();
                let textures = BatchTextures::prim_textured(texture, clip_mask_texture_id);

                let z_id = z_generator.next();

                let prim_header = PrimitiveHeader {
                    pattern_rect: *pattern_rect,
                    bounds: prim_info.clip_chain.local_clip_rect,
                    specific_prim_address: GpuBufferAddress::INVALID.as_int(),
                    transform_id: *transform_id,
                    z: z_id,
                    render_task_address: self.batcher.render_task_address,
                    user_data: [
                        uv_rect_address.as_int(),
                        0,
                        0,
                        clip_task_address.0 as i32,
                    ],
                };
                let prim_header_index = prim_headers.push(&prim_header);

                let key = BatchKey::new(
                    BatchKind::SplitComposite,
                    BlendMode::PremultipliedAlpha,
                    textures,
                );

                self.add_split_composite_instance_to_batches(
                    key,
                    BatchFeatures::CLIP_MASK,
                    device_rect,
                    z_id,
                    prim_header_index,
                    polygons_address.as_int(),
                );

                return;
            }
            PrimitiveCommand::Instance { draw_index, device_rect, .. } => {
                (draw_index, device_rect)
            }
            PrimitiveCommand::Quad { pattern, pattern_input, device_rect, gpu_buffer_address, quad_flags, edge_flags, transform_id, src_color_task_ids, blend_mode } => {
                let bounding_rect = device_rect;
                let render_task_address = self.batcher.render_task_address;

                let mut readback = None;
                if pattern.requires_backdrop_readback() {
                    readback = Some(InlineReadback {
                        src_task_id: self.batcher.render_task_id,
                        readback_task_id: src_color_task_ids[0],
                    })
                }

                if segments.is_empty() {
                    let z_id = z_generator.next();

                    quad::add_to_batch(
                        *pattern,
                        *pattern_input,
                        render_task_address,
                        *transform_id,
                        *gpu_buffer_address,
                        *quad_flags,
                        *edge_flags,
                        INVALID_SEGMENT_INDEX as u8,
                        *src_color_task_ids,
                        z_id,
                        *blend_mode,
                        readback.map(|rb| rb.readback_task_id),
                        render_tasks,
                        gpu_buffer_builder,
                        |key, instance| {
                            let batch = self.batcher.set_params_and_get_batch(
                                key,
                                BatchFeatures::empty(),
                                readback.as_ref(),
                                bounding_rect,
                                z_id,
                            );
                            batch.push(instance);
                        },
                    );
                } else {
                    for (i, segment) in segments.iter().enumerate() {
                        // TODO(gw): edge_flags should be per-segment, when used for more than composites
                        debug_assert!(edge_flags.is_empty());

                        let z_id = z_generator.next();

                        // Each segment covers only its own part of the quad, so batch
                        // it against that rather than the whole quad: segments of one
                        // quad routinely fit between the segments of another.
                        // An empty rect would never report an overlap, so fall back to
                        // the quad's own rect rather than let the instance sink into a
                        // batch it is drawn on top of.
                        let segment_rect = if segment.rect.is_empty() {
                            *bounding_rect
                        } else {
                            segment.rect.cast_unit()
                        };

                        quad::add_to_batch(
                            *pattern,
                            *pattern_input,
                            render_task_address,
                            *transform_id,
                            *gpu_buffer_address,
                            *quad_flags,
                            *edge_flags,
                            i as u8,
                            [segment.task_id, src_color_task_ids[1], src_color_task_ids[2]],
                            z_id,
                            *blend_mode,
                            readback.map(|rb| rb.readback_task_id),
                            render_tasks,
                            gpu_buffer_builder,
                            |key, instance| {
                                let batch = self.batcher.set_params_and_get_batch(
                                    key,
                                    BatchFeatures::empty(),
                                    readback.as_ref(),
                                    &segment_rect,
                                    z_id,
                                );
                                batch.push(instance);
                            },
                        );
                    }
                }

                return;
            }
        };

        let vis_flags = match ctx.scratch.frame.draw(*draw_index).state {
            DrawState::Culled => {
                return;
            }
            DrawState::PassThrough |
            DrawState::Unset => {
                panic!("bug: invalid visibility state");
            }
            DrawState::Visible { vis_flags, .. } => {
                vis_flags
            }
        };

        // If this primitive is a backdrop, that means that it is known to cover
        // the entire picture cache background. In that case, the renderer will
        // use the backdrop color as a clear color, and so we can drop this
        // primitive and any prior primitives from the batch lists for this
        // picture cache slice.
        // TODO: This isn't reachable anymore, and the optimization was lost in
        // the transition to quad shaders.
        if vis_flags.contains(PrimitiveVisibilityFlags::IS_BACKDROP) {
            self.clear_batches();
            return;
        }

        let transform_id = transforms.gpu.get_id(
            prim_spatial_node_index,
            root_spatial_node_index,
            ctx.spatial_tree,
        );

        let prim_info = ctx.scratch.frame.draw(*draw_index);

        let z_id = z_generator.next();

        let mut batch_features = BatchFeatures::empty();
        // Check if the primitive might require a clip mask.
        if prim_info.clip_task_index != ClipTaskIndex::INVALID {
            batch_features |= BatchFeatures::CLIP_MASK;
        }

        if !bounding_rect.is_empty() {
            // The command's device rect was mapped out of the picture-space
            // coverage rect, so the clip chain has to have been built in the
            // surface's picture space for the two to describe the same region.
            debug_assert_eq!(prim_info.clip_chain.pic_spatial_node_index, surface_spatial_node_index,
                "The primitive's bounding box is specified in a different coordinate system from the current batch!");
        }

        // A draw index is not a primitive instance index; the header carries the
        // back-reference.
        let prim_instance = &prim_instances[prim_info.prim_instance_index.0 as usize];

        match prim_instance.kind {
            PrimitiveKind::TextRun { handle } => {
                let text_run_scratch_handle = prim_info.kind_scratch.unwrap_text_run();
                let run_scratch = &ctx.scratch.frame.text_runs[text_run_scratch_handle];
                let subpx_dir = run_scratch.used_font.get_subpx_dir();
                // A run that resolved to nothing never made it here: `prepare`
                // pushes no kind scratch for it, so it has no draw to batch.
                let prim_data = ctx.dl_stores.text_runs[handle]
                    .kind
                    .as_ref()
                    .expect("bug: batching a text run that resolved to nothing");

                let glyph_keys = &ctx.scratch.frame.glyph_keys[run_scratch.glyph_keys_range];

                // `local_rect.p0` is the run anchor (the normalized prim rect
                // origin). In device mode the shader transforms it to device
                // space and adds the per-glyph device offsets stored at
                // `gpu_address`. `user_data` carries the raster scale (for
                // local-raster mode's raster -> local mapping) and the mode flag
                // (0 = device, 1 = local raster).
                let prim_header = PrimitiveHeader {
                    // A text run's prim rect situates the glyphs but does not
                    // bound them: the shader only reads `pattern_rect.min` as
                    // the run anchor, and glyph ink routinely extends past the
                    // authored rect. Do not fold the prim rect into `bounds` -
                    // that would start clipping glyphs by it.
                    pattern_rect: run_scratch.pattern_rect,
                    // Quantised to the glyph grid on the snapped axes - see
                    // `TextRunScratch::snapped_clip_rect`. The shader clamps the
                    // glyph quad to this, and that clamp is a hard pixel-centre
                    // test, so it is only lossless when the clip and the glyphs
                    // share a grid.
                    bounds: run_scratch.snapped_clip_rect,
                    transform_id,
                    z: z_id,
                    render_task_address: self.batcher.render_task_address,
                    specific_prim_address: run_scratch.gpu_address.as_int(),
                    user_data: [
                        (run_scratch.raster_scale * 65535.0).round() as i32,
                        run_scratch.local_raster as i32,
                        0,
                        0,
                    ],
                };
                let prim_header_index = prim_headers.push(&prim_header);
                let base_instance = GlyphInstance::new(
                    prim_header_index,
                );
                let batcher = &mut self.batcher;

                let (clip_task_address, clip_mask_texture_id) = ctx.get_prim_clip_task_and_texture(
                    prim_info.clip_task_index,
                    render_tasks,
                ).unwrap();

                // The run_scratch.used_font.clone() is here instead of inline in the `fetch_glyph`
                // function call to work around a miscompilation.
                // https://github.com/rust-lang/rust/issues/80111
                let font = run_scratch.used_font.clone();
                ctx.resource_cache.fetch_glyphs(
                    font,
                    &glyph_keys,
                    &gpu_buffer_builder.f32,
                    &mut self.glyph_fetch_buffer,
                    |texture_id, glyph_format, glyphs| {
                        debug_assert_ne!(texture_id, TextureSource::Invalid);

                        let subpx_dir = subpx_dir.limit_by(glyph_format);

                        // A glyph that rasterized from a bitmap strike ignored the
                        // sub-pixel offset its key asked for and landed on the device
                        // grid, so its pen rounds to nearest rather than flooring with
                        // the sub-pixel bias (bug 2056856). Only known once the glyph is
                        // rasterized, which is why it is resolved here rather than in
                        // `request_resources`. Batches are keyed by glyph format, so this
                        // is uniform across the batch.
                        let is_bitmap_strike = matches!(
                            glyph_format,
                            GlyphFormat::Bitmap | GlyphFormat::ColorBitmap
                        );

                        let textures = BatchTextures::prim_textured(
                            texture_id,
                            clip_mask_texture_id,
                        );

                        let kind = BatchKind::TextRun(glyph_format);

                        let (blend_mode, color_mode) = match glyph_format {
                            GlyphFormat::Subpixel |
                            GlyphFormat::TransformedSubpixel => {
                                debug_assert!(ctx.use_dual_source_blending);
                                (
                                    BlendMode::SubpixelDualSource,
                                    ShaderColorMode::SubpixelDualSource,
                                )
                            }
                            GlyphFormat::Alpha |
                            GlyphFormat::TransformedAlpha |
                            GlyphFormat::Bitmap => {
                                (
                                    BlendMode::PremultipliedAlpha,
                                    ShaderColorMode::Alpha,
                                )
                            }
                            GlyphFormat::ColorBitmap => {
                                (
                                    BlendMode::PremultipliedAlpha,
                                    if prim_data.shadow {
                                        // Ignore color and only sample alpha when shadowing.
                                        ShaderColorMode::BitmapShadow
                                    } else {
                                        ShaderColorMode::ColorBitmap
                                    },
                                )
                            }
                        };

                        // Calculate a tighter bounding rect of just the glyphs passed to this
                        // callback from request_glyphs(), rather than using the bounds of the
                        // entire text run. This improves batching when glyphs are fragmented
                        // over multiple textures in the texture cache.
                        // This mirrors the glyph positioning in the ps_text_run shader. The
                        // TRANSFORM_GLYPHS branch covers device mode for 2D rotated/skewed
                        // glyphs; the other branch covers device-mode axis-aligned and
                        // local-raster mode (distinguished by `run_scratch.raster_scale`).
                        // `text_offset` is zero because glyph positions are stored absolutely
                        // (relative to the prim origin via `local_rect.min`), not relative to
                        // a separate snapped reference-frame offset; the TRANSFORM_GLYPHS
                        // branch's `raster_text_offset` then reduces to the reference-frame
                        // device snap that `request_resources` applies.
                        let tight_bounding_rect = {
                            let snap_bias = if is_bitmap_strike {
                                DeviceVector2D::new(0.5, 0.5)
                            } else {
                                match subpx_dir {
                                    SubpixelDirection::None => DeviceVector2D::new(0.5, 0.5),
                                    SubpixelDirection::Horizontal => DeviceVector2D::new(0.125, 0.5),
                                    SubpixelDirection::Vertical => DeviceVector2D::new(0.5, 0.125),
                                    SubpixelDirection::Mixed => DeviceVector2D::new(0.125, 0.125),
                                }
                            };
                            let text_offset = LayoutVector2D::zero();

                            let device_tight_rect = if run_scratch.used_font.flags.contains(FontInstanceFlags::TRANSFORM_GLYPHS) {
                                let mut device_bounding_rect = DeviceRect::default();

                                // The relative transform lands in raster space, so fold in
                                // the raster -> device scale: the snapping below floors
                                // against the device pixel grid, not the raster one.
                                let glyph_transform = ctx.spatial_tree.get_relative_transform(
                                    prim_spatial_node_index,
                                    root_spatial_node_index,
                                )
                                    .into_transform()
                                    .then_scale(device_pixel_scale.0, device_pixel_scale.0, 1.0)
                                    .with_destination::<DevicePixel>();

                                let glyph_translation = DeviceVector2D::new(glyph_transform.m41, glyph_transform.m42);

                                let mut use_tight_bounding_rect = true;
                                for glyph in glyphs {
                                    let glyph_offset = prim_data.glyphs[glyph.index_in_text_run as usize].point + prim_header.pattern_rect.min.to_vector();

                                    let transformed_offset = match glyph_transform.transform_point2d(glyph_offset) {
                                        Some(transformed_offset) => transformed_offset,
                                        None => {
                                            use_tight_bounding_rect = false;
                                            break;
                                        }
                                    };
                                    let raster_glyph_offset = (transformed_offset + snap_bias).floor();
                                    let raster_text_offset = (
                                        glyph_transform.transform_vector2d(text_offset) +
                                        glyph_translation +
                                        DeviceVector2D::new(0.5, 0.5)
                                    ).floor() - glyph_translation;

                                    let device_glyph_rect = DeviceRect::from_origin_and_size(
                                        glyph.offset + raster_glyph_offset.to_vector() + raster_text_offset,
                                        glyph.size.to_f32(),
                                    );

                                    device_bounding_rect = device_bounding_rect.union(&device_glyph_rect);
                                }

                                if use_tight_bounding_rect {
                                    Some(device_bounding_rect)
                                } else {
                                    None
                                }
                            } else {
                                let mut local_bounding_rect = LayoutRect::default();

                                let glyph_raster_scale = run_scratch.raster_scale;

                                for glyph in glyphs {
                                    let glyph_offset = prim_data.glyphs[glyph.index_in_text_run as usize].point + prim_header.pattern_rect.min.to_vector();
                                    let glyph_scale = LayoutToDeviceScale::new(glyph_raster_scale / glyph.scale);
                                    let raster_glyph_offset = (glyph_offset * LayoutToDeviceScale::new(glyph_raster_scale) + snap_bias).floor() / glyph.scale;
                                    let local_glyph_rect = LayoutRect::from_origin_and_size(
                                        (glyph.offset + raster_glyph_offset.to_vector()) / glyph_scale + text_offset,
                                        glyph.size.to_f32() / glyph_scale,
                                    );

                                    local_bounding_rect = local_bounding_rect.union(&local_glyph_rect);
                                }

                                map_to_device(
                                    &local_bounding_rect,
                                    prim_spatial_node_index,
                                    root_spatial_node_index,
                                    *bounding_rect / raster_to_device(device_pixel_scale),
                                    device_pixel_scale,
                                    ctx.spatial_tree,
                                )
                            };

                            match device_tight_rect {
                                // The text run may have been clipped, for example if part of it is offscreen.
                                // So intersect our result with the original bounding rect.
                                Some(rect) => rect.intersection(bounding_rect).unwrap_or_else(DeviceRect::zero),
                                // If space mapping went off the rails, fall back to the old behavior.
                                //TODO: consider skipping the glyph run completely in this case.
                                None => *bounding_rect,
                            }
                        };

                        let key = BatchKey::new(kind, blend_mode, textures);

                        let batch = &mut batcher.alpha_batch_list.set_params_and_get_batch(
                            key,
                            batch_features,
                            &tight_bounding_rect,
                            z_id,
                        ).instances;

                        batch.reserve(glyphs.len());
                        for glyph in glyphs {
                            batch.push(base_instance.build(
                                clip_task_address,
                                subpx_dir,
                                glyph.index_in_text_run,
                                glyph.uv_rect_address,
                                color_mode,
                                glyph.subpx_offset_x,
                                glyph.subpx_offset_y,
                                glyph.is_packed_glyph,
                                is_bitmap_strike,
                            ));
                        }
                    },
                );
            }
            _ => {}
        }
    }
}

/// A list of clip instances to be drawn into a target.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipMaskInstanceList {
    pub mask_instances_fast: FrameVec<MaskInstance>,
    pub mask_instances_superellipse: FrameVec<MaskInstance>,
    pub mask_instances_slow: FrameVec<MaskInstance>,

    pub mask_instances_fast_with_scissor: FastHashMap<DeviceIntRect, FrameVec<MaskInstance>>,
    pub mask_instances_superellipse_with_scissor: FastHashMap<DeviceIntRect, FrameVec<MaskInstance>>,
    pub mask_instances_slow_with_scissor: FastHashMap<DeviceIntRect, FrameVec<MaskInstance>>,

    pub image_mask_instances: FastHashMap<TextureSource, FrameVec<PrimitiveInstanceData>>,
    pub image_mask_instances_with_scissor: FastHashMap<(DeviceIntRect, TextureSource), FrameVec<PrimitiveInstanceData>>,

    /// Drawn with the premultiplied dest-out blend mode. Positioned relative
    /// to their render task by the shader, so they never need a scissor rect.
    pub path_tiles: FrameVec<PathTileInstance>,
}

impl ClipMaskInstanceList {
    pub fn new(memory: &FrameMemory) -> Self {
        ClipMaskInstanceList {
            mask_instances_fast: memory.new_vec(),
            mask_instances_superellipse: memory.new_vec(),
            mask_instances_slow: memory.new_vec(),
            mask_instances_fast_with_scissor: FastHashMap::default(),
            mask_instances_superellipse_with_scissor: FastHashMap::default(),
            mask_instances_slow_with_scissor: FastHashMap::default(),
            image_mask_instances: FastHashMap::default(),
            image_mask_instances_with_scissor: FastHashMap::default(),
            path_tiles: memory.new_vec(),
        }
    }

    pub fn is_empty(&self) -> bool {
        // Destructure self to make sure we don't forget to update this method if
        // a new member is added.
        let ClipMaskInstanceList {
            mask_instances_fast,
            mask_instances_superellipse,
            mask_instances_slow,
            mask_instances_fast_with_scissor,
            mask_instances_superellipse_with_scissor,
            mask_instances_slow_with_scissor,
            image_mask_instances,
            image_mask_instances_with_scissor,
            path_tiles,
        } = self;

        mask_instances_fast.is_empty()
            && mask_instances_superellipse.is_empty()
            && mask_instances_slow.is_empty()
            && mask_instances_fast_with_scissor.is_empty()
            && mask_instances_superellipse_with_scissor.is_empty()
            && mask_instances_slow_with_scissor.is_empty()
            && image_mask_instances.is_empty()
            && image_mask_instances_with_scissor.is_empty()
            && path_tiles.is_empty()
    }
}


impl<'a, 'rc> RenderTargetContext<'a, 'rc> {
    /// Retrieve the GPU task address for a given clip task instance.
    /// Returns None if the segment was completely clipped out.
    /// Returns Some(OPAQUE_TASK_ADDRESS) if no clip mask is needed.
    /// Returns Some(task_address) if there was a valid clip mask.
    fn get_clip_task_and_texture(
        &self,
        clip_task_index: ClipTaskIndex,
        offset: i32,
        render_tasks: &RenderTaskGraph,
    ) -> Option<(RenderTaskAddress, TextureSource)> {
        match self.scratch.frame.clip_mask_instances[clip_task_index.0 as usize + offset as usize] {
            ClipMaskKind::Mask(task_id) => {
                Some((
                    task_id.into(),
                    TextureSource::TextureCache(
                        render_tasks[task_id].get_target_texture(),
                        Swizzle::default(),
                    )
                ))
            }
            ClipMaskKind::None => {
                Some((OPAQUE_TASK_ADDRESS, TextureSource::Invalid))
            }
            ClipMaskKind::Clipped => {
                None
            }
        }
    }

    /// Helper function to get the clip task address for a
    /// non-segmented primitive.
    fn get_prim_clip_task_and_texture(
        &self,
        clip_task_index: ClipTaskIndex,
        render_tasks: &RenderTaskGraph,
    ) -> Option<(RenderTaskAddress, TextureSource)> {
        self.get_clip_task_and_texture(
            clip_task_index,
            0,
            render_tasks,
        )
    }
}
