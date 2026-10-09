/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BlobImageRequest, ImageDescriptorFlags, ImageFormat, RasterizedBlobImage};
use api::{DebugFlags, FontInstanceKey, FontKey, FontTemplate, GlyphIndex};
use api::{ExternalImageData, ExternalImageType, ExternalImageId, BlobImageResult};
use api::{DirtyRect, GlyphDimensions, IdNamespace, DEFAULT_TILE_SIZE};
use api::{ColorF, ImageData, ImageDescriptor, ImageKey, ImageRendering, TileSize};
use api::{BlobImageHandler, BlobImageKey, VoidPtrToSizeFn};
use api::units::*;
use euclid::size2;
use crate::render_target::RenderTargetKind;
use crate::render_task::{RenderTaskLocation, StaticRenderTaskSurface};
use crate::{render_api::{ClearCache, AddFont, ResourceUpdate, MemoryReport}, util::WeakTable};
use crate::prim_store::image::AdjustedImageSource;
use crate::image_tiling::{compute_tile_size, compute_tile_range};
#[cfg(feature = "capture")]
use crate::capture::ExternalCaptureImage;
#[cfg(feature = "replay")]
use crate::capture::PlainExternalImage;
#[cfg(any(feature = "replay", feature = "png", feature="capture"))]
use crate::capture::CaptureConfig;
use crate::composite::{NativeSurfaceId, NativeSurfaceOperation, NativeTileId, NativeSurfaceOperationDetails};
use crate::device::TextureFilter;
use crate::glyph_cache::{GlyphCache, CachedGlyphInfo};
use crate::glyph_cache::GlyphCacheEntry;
use glyph_rasterizer::{GLYPH_FLASHING, FontInstance, GlyphFormat, GlyphKey, GlyphRasterizer, GlyphRasterJob};
use glyph_rasterizer::{SharedFontResources, BaseFontInstance};
use crate::gpu_types::UvRectKind;
use crate::internal_types::{
    CacheTextureId, FastHashMap, FastHashSet, TextureSource, ResourceUpdateList,
    FrameId, FrameStamp,
};
use crate::profiler::{self, TransactionProfile, bytes_to_mb};
use crate::render_task_graph::{RenderTaskId, RenderTaskGraphBuilder};
use crate::render_task_cache::{RenderTaskCache, RenderTaskCacheKey, RenderTaskParent};
use crate::render_task_cache::{RenderTaskCacheEntry, RenderTaskCacheEntryHandle};
use crate::renderer::{GpuBufferAddress, GpuBufferBuilder, GpuBufferBuilderF, GpuBufferHandle};
use crate::surface::SurfaceBuilder;
use euclid::point2;
use smallvec::SmallVec;
use std::collections::hash_map::Entry::{self, Occupied, Vacant};
use std::collections::hash_map::{Iter, IterMut};
use std::collections::VecDeque;
use std::{cmp, mem};
use std::fmt::Debug;
use std::hash::Hash;
use std::os::raw::c_void;
#[cfg(any(feature = "capture", feature = "replay"))]
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::u32;
use crate::texture_cache::{TextureCache, TextureCacheHandle, Eviction, TargetShader};
use crate::picture_textures::PictureTextures;
use peek_poke::PeekPoke;

// Counter for generating unique native surface ids
static NEXT_NATIVE_SURFACE_ID: AtomicUsize = AtomicUsize::new(0);

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct GlyphFetchResult {
    pub index_in_text_run: i32,
    pub uv_rect_address: GpuBufferAddress,
    pub offset: DevicePoint,
    pub size: DeviceIntSize,
    pub scale: f32,
    pub subpx_offset_x: u8,
    pub subpx_offset_y: u8,
    pub is_packed_glyph: bool,
}

// These coordinates are always in texels.
// They are converted to normalized ST
// values in the vertex shader. The reason
// for this is that the texture may change
// dimensions (e.g. the pages in a texture
// atlas can grow). When this happens, by
// storing the coordinates as texel values
// we don't need to go through and update
// various CPU-side structures.
#[derive(Debug, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CacheItem {
    pub texture_id: TextureSource,
    pub uv_rect_handle: GpuBufferHandle,
    pub uv_rect: DeviceIntRect,
    pub user_data: [f32; 4],
}

impl CacheItem {
    pub fn invalid() -> Self {
        CacheItem {
            texture_id: TextureSource::Invalid,
            uv_rect_handle: GpuBufferHandle::INVALID,
            uv_rect: DeviceIntRect::zero(),
            user_data: [0.0; 4],
        }
    }

    pub fn is_valid(&self) -> bool {
        self.texture_id != TextureSource::Invalid
    }
}

/// Represents the backing store of an image in the cache.
/// This storage can take several forms.
#[derive(Clone, Debug)]
pub enum CachedImageData {
    /// A simple series of bytes, provided by the embedding and owned by WebRender.
    /// The format is stored out-of-band, currently in ImageDescriptor.
    Raw(Arc<Vec<u8>>),
    /// An series of commands that can be rasterized into an image via an
    /// embedding-provided callback.
    ///
    /// The commands are stored elsewhere and this variant is used as a placeholder.
    Blob,
    /// A stacking context for which a snapshot has been requested.
    ///
    /// The snapshot is grabbed from GPU-side rasterized pixels so there is no
    /// CPU-side data to store here.
    Snapshot,
    /// An image owned by the embedding, and referenced by WebRender. This may
    /// take the form of a texture or a heap-allocated buffer.
    External(ExternalImageData),
}

impl From<ImageData> for CachedImageData {
    fn from(img_data: ImageData) -> Self {
        match img_data {
            ImageData::Raw(data) => CachedImageData::Raw(data),
            ImageData::External(data) => CachedImageData::External(data),
        }
    }
}

impl CachedImageData {
    /// Returns true if this represents a blob.
    #[inline]
    pub fn is_blob(&self) -> bool {
        match *self {
            CachedImageData::Blob => true,
            _ => false,
        }
    }

    #[inline]
    pub fn is_snapshot(&self) -> bool {
        match *self {
            CachedImageData::Snapshot => true,
            _ => false,
        }
    }

    /// Returns true if this variant of CachedImageData should go through the texture
    /// cache.
    #[inline]
    pub fn uses_texture_cache(&self) -> bool {
        match *self {
            CachedImageData::External(ref ext_data) => match ext_data.image_type {
                ExternalImageType::TextureHandle(_) => false,
                ExternalImageType::Buffer => true,
            },
            CachedImageData::Blob => true,
            CachedImageData::Raw(_) => true,
            CachedImageData::Snapshot => true,
        }
    }
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ImageProperties {
    pub descriptor: ImageDescriptor,
    pub external_image: Option<ExternalImageData>,
    pub tiling: Option<TileSize>,
    // Potentially a subset of the image's total rectangle. This rectangle is what
    // we map to the (layout space) display item bounds.
    pub visible_rect: DeviceIntRect,
    pub adjustment: AdjustedImageSource,
}

#[derive(Debug, Copy, Clone, PartialEq)]
enum State {
    Idle,
    AddResources,
    QueryResources,
}

/// Post scene building state.
type RasterizedBlob = FastHashMap<TileOffset, RasterizedBlobImage>;

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone, PartialEq, PeekPoke, Default)]
pub struct ImageGeneration(pub u32);

impl ImageGeneration {
    pub const INVALID: ImageGeneration = ImageGeneration(u32::MAX);
}

struct ImageResource {
    data: CachedImageData,
    descriptor: ImageDescriptor,
    tiling: Option<TileSize>,
    /// This is used to express images that are virtually very large
    /// but with only a visible sub-set that is valid at a given time.
    visible_rect: DeviceIntRect,
    adjustment: AdjustedImageSource,
    generation: ImageGeneration,
}

#[derive(Default)]
struct ImageTemplates {
    images: FastHashMap<ImageKey, ImageResource>,
}

impl ImageTemplates {
    fn insert(&mut self, key: ImageKey, resource: ImageResource) {
        self.images.insert(key, resource);
    }

    fn remove(&mut self, key: ImageKey) -> Option<ImageResource> {
        self.images.remove(&key)
    }

    fn get(&self, key: ImageKey) -> Option<&ImageResource> {
        self.images.get(&key)
    }

    fn get_mut(&mut self, key: ImageKey) -> Option<&mut ImageResource> {
        self.images.get_mut(&key)
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct CachedImageInfo {
    texture_cache_handle: TextureCacheHandle,
    dirty_rect: ImageDirtyRect,
    manual_eviction: bool,
}

impl CachedImageInfo {
    fn mark_unused(&mut self, texture_cache: &mut TextureCache) {
        texture_cache.evict_handle(&self.texture_cache_handle);
        self.manual_eviction = false;
    }
}

#[cfg(debug_assertions)]
impl Drop for CachedImageInfo {
    fn drop(&mut self) {
        debug_assert!(!self.manual_eviction, "Manual eviction requires cleanup");
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ResourceClassCache<K: Hash + Eq, V, U: Default> {
    resources: FastHashMap<K, V>,
    pub user_data: U,
}

impl<K, V, U> ResourceClassCache<K, V, U>
where
    K: Clone + Hash + Eq + Debug,
    U: Default,
{
    pub fn new() -> Self {
        ResourceClassCache {
            resources: FastHashMap::default(),
            user_data: Default::default(),
        }
    }

    pub fn get(&self, key: &K) -> &V {
        self.resources.get(key)
            .expect("Didn't find a cached resource with that ID!")
    }

    pub fn try_get(&self, key: &K) -> Option<&V> {
        self.resources.get(key)
    }

    pub fn insert(&mut self, key: K, value: V) {
        self.resources.insert(key, value);
    }

    pub fn remove(&mut self, key: &K) -> Option<V> {
        self.resources.remove(key)
    }

    pub fn get_mut(&mut self, key: &K) -> &mut V {
        self.resources.get_mut(key)
            .expect("Didn't find a cached resource with that ID!")
    }

    pub fn try_get_mut(&mut self, key: &K) -> Option<&mut V> {
        self.resources.get_mut(key)
    }

    pub fn entry(&mut self, key: K) -> Entry<K, V> {
        self.resources.entry(key)
    }

    pub fn iter(&self) -> Iter<K, V> {
        self.resources.iter()
    }

    pub fn iter_mut(&mut self) -> IterMut<K, V> {
        self.resources.iter_mut()
    }

    pub fn is_empty(&mut self) -> bool {
        self.resources.is_empty()
    }

    pub fn clear(&mut self) {
        self.resources.clear();
    }

    pub fn retain<F>(&mut self, f: F)
    where
        F: FnMut(&K, &mut V) -> bool,
    {
        self.resources.retain(f);
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct CachedImageKey {
    pub rendering: ImageRendering,
    pub tile: Option<TileOffset>,
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ImageRequest {
    pub key: ImageKey,
    pub rendering: ImageRendering,
    pub tile: Option<TileOffset>,
}

impl ImageRequest {
    pub fn with_tile(&self, offset: TileOffset) -> Self {
        ImageRequest {
            key: self.key,
            rendering: self.rendering,
            tile: Some(offset),
        }
    }

    pub fn is_untiled_auto(&self) -> bool {
        self.tile.is_none() && self.rendering == ImageRendering::Auto
    }
}

impl Into<BlobImageRequest> for ImageRequest {
    fn into(self) -> BlobImageRequest {
        BlobImageRequest {
            key: BlobImageKey(self.key),
            tile: self.tile.unwrap(),
        }
    }
}

impl Into<CachedImageKey> for ImageRequest {
    fn into(self) -> CachedImageKey {
        CachedImageKey {
            rendering: self.rendering,
            tile: self.tile,
        }
    }
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Clone, Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ImageCacheError {
    OverLimitSize,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
enum ImageResult {
    UntiledAuto(CachedImageInfo),
    Multi(ResourceClassCache<CachedImageKey, CachedImageInfo, ()>),
    Err(ImageCacheError),
}

impl ImageResult {
    /// Releases any texture cache entries held alive by this ImageResult.
    fn drop_from_cache(&mut self, texture_cache: &mut TextureCache) {
        match *self {
            ImageResult::UntiledAuto(ref mut entry) => {
                entry.mark_unused(texture_cache);
            },
            ImageResult::Multi(ref mut entries) => {
                for entry in entries.resources.values_mut() {
                    entry.mark_unused(texture_cache);
                }
            },
            ImageResult::Err(_) => {},
        }
    }
}

type ImageCache = ResourceClassCache<ImageKey, ImageResult, ()>;

struct Resources {
    fonts: SharedFontResources,
    image_templates: ImageTemplates,
    // We keep a set of Weak references to the fonts so that we're able to include them in memory
    // reports even if only the OS is holding on to the Vec<u8>. PtrWeakHashSet will periodically
    // drop any references that have gone dead.
    weak_fonts: WeakTable
}

// We only use this to report glyph dimensions to the user of the API, so using
// the font instance key should be enough. If we start using it to cache dimensions
// for internal font instances we should change the hash key accordingly.
pub type GlyphDimensionsCache = FastHashMap<(FontInstanceKey, GlyphIndex), Option<GlyphDimensions>>;

/// Internal information about allocated render targets in the pool
struct RenderTarget {
    size: DeviceIntSize,
    format: ImageFormat,
    texture_id: CacheTextureId,
    /// If true, this is currently leant out, and not available to other passes
    is_active: bool,
    last_frame_used: FrameId,
}

impl RenderTarget {
    fn size_in_bytes(&self) -> usize {
        let bpp = self.format.bytes_per_pixel() as usize;
        (self.size.width * self.size.height) as usize * bpp
    }

    /// Returns true if this texture was used within `threshold` frames of
    /// the current frame.
    pub fn used_recently(&self, current_frame_id: FrameId, threshold: u64) -> bool {
        self.last_frame_used + threshold >= current_frame_id
    }
}

/// High-level container for resources managed by the `RenderBackend`.
///
/// This includes a variety of things, including images, fonts, and glyphs,
/// which may be stored as memory buffers, GPU textures, or handles to resources
/// managed by the OS or other parts of WebRender.
pub struct ResourceCache {
    cached_glyphs: GlyphCache,
    cached_images: ImageCache,
    cached_render_tasks: RenderTaskCache,

    resources: Resources,
    state: State,
    current_frame_id: FrameId,

    #[cfg(feature = "capture")]
    /// Used for capture sequences. If the resource cache is updated, then we
    /// mark it as dirty. When the next frame is captured in the sequence, we
    /// dump the state of the resource cache.
    capture_dirty: bool,

    pub texture_cache: TextureCache,
    pub picture_textures: PictureTextures,

    /// TODO(gw): We should expire (parts of) this cache semi-regularly!
    cached_glyph_dimensions: GlyphDimensionsCache,
    glyph_rasterizer: GlyphRasterizer,

    /// The set of images that aren't present or valid in the texture cache,
    /// and need to be rasterized and/or uploaded this frame. This includes
    /// both blobs and regular images.
    pending_image_requests: FastHashSet<ImageRequest>,

    rasterized_blob_images: FastHashMap<BlobImageKey, RasterizedBlob>,

    /// A log of the last three frames worth of deleted image keys kept
    /// for debugging purposes.
    deleted_blob_keys: VecDeque<Vec<BlobImageKey>>,

    /// We keep one around to be able to call clear_namespace
    /// after the api object is deleted. For most purposes the
    /// api object's blob handler should be used instead.
    blob_image_handler: Option<Box<dyn BlobImageHandler>>,

    /// A list of queued compositor surface updates to apply next frame.
    pending_native_surface_updates: Vec<NativeSurfaceOperation>,

    image_templates_memory: usize,
    font_templates_memory: usize,

    /// A pool of render targets for use by the render task graph
    render_target_pool: Vec<RenderTarget>,

    /// An empty (1x1 transparent) image used when a stacking context snapshot
    /// is missing.
    ///
    /// For now it acts as a catch-all solution for cases where WebRender fails
    /// to produce a texture cache item for a snapshotted tacking context.
    /// These cases include:
    /// - Empty stacking contexts.
    /// - Stacking contexts that are more aggressively culled out than they
    ///   should, for example when they are in a perspective transform that
    ///   cannot be projected to screen space.
    /// - Likely other cases we have not found yet.
    /// Over time it would be better to handle each of these cases explicitly
    /// and make it a hard error to fail to snapshot a stacking context.
    fallback_handle: TextureCacheHandle,
    debug_fallback_panic: bool,
    debug_fallback_pink: bool,
}

impl ResourceCache {
    pub fn new(
        texture_cache: TextureCache,
        picture_textures: PictureTextures,
        glyph_rasterizer: GlyphRasterizer,
        cached_glyphs: GlyphCache,
        fonts: SharedFontResources,
        blob_image_handler: Option<Box<dyn BlobImageHandler>>,
    ) -> Self {
        ResourceCache {
            cached_glyphs,
            cached_images: ResourceClassCache::new(),
            cached_render_tasks: RenderTaskCache::new(),
            resources: Resources {
                fonts,
                image_templates: ImageTemplates::default(),
                weak_fonts: WeakTable::new(),
            },
            cached_glyph_dimensions: FastHashMap::default(),
            texture_cache,
            picture_textures,
            state: State::Idle,
            current_frame_id: FrameId::INVALID,
            pending_image_requests: FastHashSet::default(),
            glyph_rasterizer,
            rasterized_blob_images: FastHashMap::default(),
            // We want to keep three frames worth of delete blob keys
            deleted_blob_keys: vec![Vec::new(), Vec::new(), Vec::new()].into(),
            blob_image_handler,
            pending_native_surface_updates: Vec::new(),
            #[cfg(feature = "capture")]
            capture_dirty: true,
            image_templates_memory: 0,
            font_templates_memory: 0,
            render_target_pool: Vec::new(),
            fallback_handle: TextureCacheHandle::invalid(),
            debug_fallback_panic: false,
            debug_fallback_pink: false,
        }
    }

    /// Construct a resource cache for use in unit tests.
    #[cfg(test)]
    pub fn new_for_testing() -> Self {
        use rayon::ThreadPoolBuilder;

        let texture_cache = TextureCache::new_for_testing(
            4096,
            ImageFormat::RGBA8,
        );
        let workers = Arc::new(ThreadPoolBuilder::new().build().unwrap());
        let glyph_rasterizer = GlyphRasterizer::new(workers, None, true);
        let cached_glyphs = GlyphCache::new();
        let fonts = SharedFontResources::new(IdNamespace(0));
        let picture_textures = PictureTextures::new(
            crate::tile_cache::TILE_SIZE_DEFAULT,
            TextureFilter::Nearest,
        );

        ResourceCache::new(
            texture_cache,
            picture_textures,
            glyph_rasterizer,
            cached_glyphs,
            fonts,
            None,
        )
    }

    pub fn max_texture_size(&self) -> i32 {
        self.texture_cache.max_texture_size()
    }

    /// Maximum texture size before we consider it preferrable to break the texture
    /// into tiles.
    pub fn tiling_threshold(&self) -> i32 {
        self.texture_cache.tiling_threshold()
    }

    pub fn enable_multithreading(&mut self, enable: bool) {
        self.glyph_rasterizer.enable_multithreading(enable);
    }

    fn should_tile(limit: i32, descriptor: &ImageDescriptor, data: &CachedImageData) -> bool {
        let size_check = descriptor.size.width > limit || descriptor.size.height > limit;
        match *data {
            CachedImageData::Raw(_) | CachedImageData::Blob => size_check,
            CachedImageData::External(info) => {
                // External handles already represent existing textures so it does
                // not make sense to tile them into smaller ones.
                info.image_type == ExternalImageType::Buffer && size_check
            }
            CachedImageData::Snapshot => false,
        }
    }

    /// Request an optionally cacheable render task.
    ///
    /// If the render task cache key is None, the render task is
    /// not cached.
    /// Otherwise, if the item is already cached, the texture cache
    /// handle will be returned. Otherwise, the user supplied
    /// closure will be invoked to generate the render task
    /// chain that is required to draw this task.
    ///
    /// This function takes care of adding the render task as a
    /// dependency to its parent task or surface.
    pub fn request_render_task(
        &mut self,
        key: Option<RenderTaskCacheKey>,
        is_opaque: bool,
        parent: RenderTaskParent,
        gpu_buffer_builder: &mut GpuBufferBuilderF,
        rg_builder: &mut RenderTaskGraphBuilder,
        surface_builder: &mut SurfaceBuilder,
        f: &mut dyn FnMut(&mut RenderTaskGraphBuilder, &mut GpuBufferBuilderF) -> RenderTaskId,
    ) -> RenderTaskId {
        self.cached_render_tasks.request_render_task(
            key.clone(),
            &mut self.texture_cache,
            is_opaque,
            parent,
            gpu_buffer_builder,
            rg_builder,
            surface_builder,
            f
        )
    }

    pub fn render_as_image(
        &mut self,
        image_key: ImageKey,
        size: DeviceIntSize,
        rg_builder: &mut RenderTaskGraphBuilder,
        gpu_buffer_builder: &mut GpuBufferBuilderF,
        is_opaque: bool,
        adjustment: &AdjustedImageSource,
        f: &mut dyn FnMut(&mut RenderTaskGraphBuilder, &mut GpuBufferBuilderF) -> RenderTaskId,
    ) -> RenderTaskId {

        let task_id = f(rg_builder, gpu_buffer_builder);

        let render_task = rg_builder.get_task_mut(task_id);

        // Note: We are defaulting to `ImageRendering::Auto` and only support
        // this mode here, because the desired rendering mode is known later
        // when an image display item will read the produced snapshot. In theory,
        // multiple image items with different rendering modes could refer to
        // the snapshot's image key, or they could first appear in a later frame
        // So delaying snapshotting logic until the we know about the rendering
        // mode would, in addition to adding complexity, only work in certain
        // cases.
        // If supporting more rendering modes is important for snapshots, we could
        // consider specifying it in the stacking context's snapshot params so
        // that we have the information early enough.
        // Here and in other parts of the code, this restriction manifests itself
        // in the expectation that we are dealing with `ImageResult::UntiledAuto`
        // which implicitly specifies the rendering mode.

        // Make sure to update the existing image info and texture cache handle
        // instead of overwriting them if they already exist for this key.
        let image_result = self.cached_images.entry(image_key).or_insert_with(|| {
            ImageResult::UntiledAuto(CachedImageInfo {
                texture_cache_handle: TextureCacheHandle::invalid(),
                dirty_rect: ImageDirtyRect::All,
                manual_eviction: true,
            })
        });

        let ImageResult::UntiledAuto(ref mut info) = *image_result else {
            unreachable!("Expected untiled image with auto filter for snapshot");
        };

        let flags = if is_opaque {
            ImageDescriptorFlags::IS_OPAQUE
        } else {
            ImageDescriptorFlags::empty()
        };

        let descriptor = ImageDescriptor::new(
            size.width,
            size.height,
            self.texture_cache.shared_color_expected_format(),
            flags,
        );

        // TODO(bug 1975123) We currently do not have a way to ensure that an
        // atlas texture used as a destination for the snapshot will not be
        // also used as an input by a primitive of the snapshot.
        // We can't both read and write the same texture in a draw call
        // so we work around it by preventing the snapshot from being placed
        // in a texture atlas.
        let force_standalone_texture = true;

        // Allocate space in the texture cache, but don't supply
        // and CPU-side data to be uploaded.
        let user_data = [0.0; 4];
        self.texture_cache.update(
            &mut info.texture_cache_handle,
            descriptor,
            TextureFilter::Linear,
            None,
            user_data,
            DirtyRect::All,
            gpu_buffer_builder,
            None,
            render_task.uv_rect_kind(),
            Eviction::Manual,
            TargetShader::Default,
            force_standalone_texture,
        );

        // Get the allocation details in the texture cache, and store
        // this in the render task. The renderer will draw this task
        // into the appropriate rect of the texture cache on this frame.
        let (texture_id, uv_rect, _, _, _) =
            self.texture_cache.get_cache_location(&info.texture_cache_handle);

        render_task.location = RenderTaskLocation::Static {
            surface: StaticRenderTaskSurface::TextureCache {
                texture: texture_id,
                target_kind: RenderTargetKind::Color,
            },
            rect: uv_rect.to_i32(),
        };

        self.resources.image_templates
            .get_mut(image_key)
            .unwrap()
            .adjustment = *adjustment;

        task_id
    }

    pub fn post_scene_building_update(
        &mut self,
        updates: Vec<ResourceUpdate>,
        profile: &mut TransactionProfile,
    ) {
        // TODO, there is potential for optimization here, by processing updates in
        // bulk rather than one by one (for example by sorting allocations by size or
        // in a way that reduces fragmentation in the atlas).
        #[cfg(feature = "capture")]
        match updates.is_empty() {
            false => self.capture_dirty = true,
            _ => {},
        }

        for update in updates {
            match update {
                ResourceUpdate::AddImage(img) => {
                    if let ImageData::Raw(ref bytes) = img.data {
                        self.image_templates_memory += bytes.len();
                        profile.set(profiler::IMAGE_TEMPLATES_MEM, bytes_to_mb(self.image_templates_memory));
                    }
                    self.add_image_template(
                        img.key,
                        img.descriptor,
                        img.data.into(),
                        &img.descriptor.size.into(),
                        img.tiling,
                    );
                    profile.set(profiler::IMAGE_TEMPLATES, self.resources.image_templates.images.len());
                }
                ResourceUpdate::UpdateImage(img) => {
                    self.update_image_template(img.key, img.descriptor, img.data.into(), &img.dirty_rect);
                }
                ResourceUpdate::AddBlobImage(img) => {
                    self.add_image_template(
                        img.key.as_image(),
                        img.descriptor,
                        CachedImageData::Blob,
                        &img.visible_rect,
                        Some(img.tile_size),
                    );
                }
                ResourceUpdate::UpdateBlobImage(img) => {
                    self.update_image_template(
                        img.key.as_image(),
                        img.descriptor,
                        CachedImageData::Blob,
                        &to_image_dirty_rect(
                            &img.dirty_rect
                        ),
                    );
                    self.discard_tiles_outside_visible_area(img.key, &img.visible_rect); // TODO: remove?
                    self.set_image_visible_rect(img.key.as_image(), &img.visible_rect);
                }
                ResourceUpdate::DeleteImage(img) => {
                    self.delete_image_template(img);
                    profile.set(profiler::IMAGE_TEMPLATES, self.resources.image_templates.images.len());
                    profile.set(profiler::IMAGE_TEMPLATES_MEM, bytes_to_mb(self.image_templates_memory));
                }
                ResourceUpdate::DeleteBlobImage(img) => {
                    self.delete_image_template(img.as_image());
                }
                ResourceUpdate::AddSnapshotImage(img) => {
                    let format = self.texture_cache.shared_color_expected_format();
                    self.add_image_template(
                        img.key.as_image(),
                        ImageDescriptor {
                            format,
                            // We'll know about the size when creating the render task.
                            size: DeviceIntSize::zero(),
                            stride: None,
                            offset: 0,
                            flags: ImageDescriptorFlags::empty(),
                        },
                        CachedImageData::Snapshot,
                        &DeviceIntRect::zero(),
                        None,
                    );
                }
                ResourceUpdate::DeleteSnapshotImage(img) => {
                    self.delete_image_template(img.as_image());
                }
                ResourceUpdate::DeleteFont(font) => {
                    if let Some(shared_key) = self.resources.fonts.font_keys.delete_key(&font) {
                        self.delete_font_template(shared_key);
                        if let Some(ref mut handler) = &mut self.blob_image_handler {
                            handler.delete_font(shared_key);
                        }
                        profile.set(profiler::FONT_TEMPLATES, self.resources.fonts.templates.len());
                        profile.set(profiler::FONT_TEMPLATES_MEM, bytes_to_mb(self.font_templates_memory));
                    }
                }
                ResourceUpdate::DeleteFontInstance(font) => {
                    if let Some(shared_key) = self.resources.fonts.instance_keys.delete_key(&font) {
                        self.delete_font_instance(shared_key);
                    }
                    if let Some(ref mut handler) = &mut self.blob_image_handler {
                        handler.delete_font_instance(font);
                    }
                }
                ResourceUpdate::SetBlobImageVisibleArea(key, area) => {
                    self.discard_tiles_outside_visible_area(key, &area);
                    self.set_image_visible_rect(key.as_image(), &area);
                }
                ResourceUpdate::AddFont(font) => {
                    // The shared key was already added in ApiResources, but the first time it is
                    // seen on the backend we still need to do some extra initialization here.
                    let (key, template) = match font {
                        AddFont::Raw(key, bytes, index) => {
                            (key, FontTemplate::Raw(bytes, index))
                        }
                        AddFont::Native(key, native_font_handle) => {
                            (key, FontTemplate::Native(native_font_handle))
                        }
                    };
                    let shared_key = self.resources.fonts.font_keys.map_key(&key);
                    if !self.glyph_rasterizer.has_font(shared_key) {
                        self.add_font_template(shared_key, template);
                        profile.set(profiler::FONT_TEMPLATES, self.resources.fonts.templates.len());
                        profile.set(profiler::FONT_TEMPLATES_MEM, bytes_to_mb(self.font_templates_memory));
                    }
                }
                ResourceUpdate::AddFontInstance(..) => {
                    // Already added in ApiResources.
                }
            }
        }
    }

    pub fn add_rasterized_blob_images(
        &mut self,
        images: Vec<(BlobImageRequest, BlobImageResult)>,
        profile: &mut TransactionProfile,
    ) {
        for (request, result) in images {
            let data = match result {
                Ok(data) => data,
                Err(..) => {
                    warn!("Failed to rasterize a blob image");
                    continue;
                }
            };

            profile.add(profiler::RASTERIZED_BLOBS_PX, data.rasterized_rect.area());

            // First make sure we have an entry for this key (using a placeholder
            // if need be).
            let tiles = self.rasterized_blob_images.entry(request.key).or_insert_with(
                || { RasterizedBlob::default() }
            );

            tiles.insert(request.tile, data);

            match self.cached_images.try_get_mut(&request.key.as_image()) {
                Some(&mut ImageResult::Multi(ref mut entries)) => {
                    let cached_key = CachedImageKey {
                        rendering: ImageRendering::Auto, // TODO(nical)
                        tile: Some(request.tile),
                    };
                    if let Some(entry) = entries.try_get_mut(&cached_key) {
                        entry.dirty_rect = DirtyRect::All;
                    }
                }
                _ => {}
            }
        }
    }

    pub fn add_font_template(&mut self, font_key: FontKey, template: FontTemplate) {
        // Push the new font to the font renderer, and also store
        // it locally for glyph metric requests.
        if let FontTemplate::Raw(ref data, _) = template {
            self.resources.weak_fonts.insert(Arc::downgrade(data));
            self.font_templates_memory += data.len();
        }
        self.glyph_rasterizer.add_font(font_key, template.clone());
        self.resources.fonts.templates.add_font(font_key, template);
    }

    pub fn delete_font_template(&mut self, font_key: FontKey) {
        self.glyph_rasterizer.delete_font(font_key);
        if let Some(FontTemplate::Raw(data, _)) = self.resources.fonts.templates.delete_font(&font_key) {
            self.font_templates_memory -= data.len();
        }
        self.cached_glyphs.delete_fonts(&[font_key]);
    }

    pub fn delete_font_instance(&mut self, instance_key: FontInstanceKey) {
        self.resources.fonts.instances.delete_font_instance(instance_key);
    }

    pub fn get_font_instance(&self, instance_key: FontInstanceKey) -> Option<Arc<BaseFontInstance>> {
        self.resources.fonts.instances.get_font_instance(instance_key)
    }

    pub fn get_fonts(&self) -> SharedFontResources {
        self.resources.fonts.clone()
    }

    pub fn add_image_template(
        &mut self,
        image_key: ImageKey,
        descriptor: ImageDescriptor,
        data: CachedImageData,
        visible_rect: &DeviceIntRect,
        mut tiling: Option<TileSize>,
    ) {
        if let Some(ref mut tile_size) = tiling {
            // Sanitize the value since it can be set by a pref.
            *tile_size = (*tile_size).max(16).min(2048);
        }

        if tiling.is_none() && Self::should_tile(self.tiling_threshold(), &descriptor, &data) {
            // We aren't going to be able to upload a texture this big, so tile it, even
            // if tiling was not requested.
            tiling = Some(DEFAULT_TILE_SIZE);
        }

        let resource = ImageResource {
            descriptor,
            data,
            tiling,
            visible_rect: *visible_rect,
            adjustment: AdjustedImageSource::new(),
            generation: ImageGeneration(0),
        };

        self.resources.image_templates.insert(image_key, resource);
    }

    pub fn update_image_template(
        &mut self,
        image_key: ImageKey,
        descriptor: ImageDescriptor,
        data: CachedImageData,
        dirty_rect: &ImageDirtyRect,
    ) {
        let tiling_threshold = self.tiling_threshold();
        let image = match self.resources.image_templates.get_mut(image_key) {
            Some(res) => res,
            None => panic!("Attempt to update non-existent image"),
        };

        let mut tiling = image.tiling;
        if tiling.is_none() && Self::should_tile(tiling_threshold, &descriptor, &data) {
            tiling = Some(DEFAULT_TILE_SIZE);
        }

        // Each cache entry stores its own copy of the image's dirty rect. This allows them to be
        // updated independently.
        match self.cached_images.try_get_mut(&image_key) {
            Some(&mut ImageResult::UntiledAuto(ref mut entry)) => {
                entry.dirty_rect = entry.dirty_rect.union(dirty_rect);
            }
            Some(&mut ImageResult::Multi(ref mut entries)) => {
                for (key, entry) in entries.iter_mut() {
                    // We want the dirty rect relative to the tile and not the whole image.
                    let local_dirty_rect = match (tiling, key.tile) {
                        (Some(tile_size), Some(tile)) => {
                            dirty_rect.map(|mut rect|{
                                let tile_offset = DeviceIntPoint::new(
                                    tile.x as i32,
                                    tile.y as i32,
                                ) * tile_size as i32;
                                rect = rect.translate(-tile_offset.to_vector());

                                let tile_rect = compute_tile_size(
                                    &descriptor.size.into(),
                                    tile_size,
                                    tile,
                                ).into();

                                rect.intersection(&tile_rect).unwrap_or_else(DeviceIntRect::zero)
                            })
                        }
                        (None, Some(..)) => DirtyRect::All,
                        _ => *dirty_rect,
                    };
                    entry.dirty_rect = entry.dirty_rect.union(&local_dirty_rect);
                }
            }
            _ => {}
        }

        if image.descriptor.format != descriptor.format {
            // could be a stronger warning/error?
            trace!("Format change {:?} -> {:?}", image.descriptor.format, descriptor.format);
        }
        *image = ImageResource {
            descriptor,
            data,
            tiling,
            visible_rect: descriptor.size.into(),
            adjustment: AdjustedImageSource::new(),
            generation: ImageGeneration(image.generation.0 + 1),
        };
    }

    pub fn increment_image_generation(&mut self, key: ImageKey) {
        if let Some(image) = self.resources.image_templates.get_mut(key) {
            image.generation.0 += 1;
        }
    }

    pub fn delete_image_template(&mut self, image_key: ImageKey) {
        // Remove the template.
        let value = self.resources.image_templates.remove(image_key);

        // Release the corresponding texture cache entry, if any.
        if let Some(mut cached) = self.cached_images.remove(&image_key) {
            cached.drop_from_cache(&mut self.texture_cache);
        }

        match value {
            Some(image) => if image.data.is_blob() {
                if let CachedImageData::Raw(data) = image.data {
                    self.image_templates_memory -= data.len();
                }

                let blob_key = BlobImageKey(image_key);
                self.deleted_blob_keys.back_mut().unwrap().push(blob_key);
                self.rasterized_blob_images.remove(&blob_key);
            },
            None => {
                warn!("Delete the non-exist key");
                debug!("key={:?}", image_key);
            }
        }
    }

    /// Return the current generation of an image template
    pub fn get_image_generation(&self, key: ImageKey) -> ImageGeneration {
        self.resources
            .image_templates
            .get(key)
            .map_or(ImageGeneration::INVALID, |template| template.generation)
    }

    /// Requests an image to ensure that it will be in the texture cache this frame.
    ///
    /// returns the size in device pixel of the image or tile.
    pub fn request_image(
        &mut self,
        mut request: ImageRequest,
        gpu_buffer: &mut GpuBufferBuilderF,
    ) -> DeviceIntSize {
        debug_assert_eq!(self.state, State::AddResources);

        let template = match self.resources.image_templates.get(request.key) {
            Some(template) => template,
            None => {
                warn!("ERROR: Trying to render deleted / non-existent key");
                debug!("key={:?}", request.key);
                return DeviceIntSize::zero();
            }
        };

        let size = match request.tile {
            Some(tile) => compute_tile_size(&template.visible_rect, template.tiling.unwrap(), tile),
            None => template.descriptor.size,
        };

        // Images that don't use the texture cache can early out.
        if !template.data.uses_texture_cache() {
            return size;
        }

        if template.data.is_snapshot() {
            // We only Support `Auto` for snapshots. This is because we have
            // to make the decision about the filtering mode earlier when
            // producing the snapshot.
            // See the comment at the top of `render_as_image`.
            request.rendering = ImageRendering::Auto;
        }

        let side_size =
            template.tiling.map_or(cmp::max(template.descriptor.size.width, template.descriptor.size.height),
                                   |tile_size| tile_size as i32);
        if side_size > self.texture_cache.max_texture_size() {
            // The image or tiling size is too big for hardware texture size.
            warn!("Dropping image, image:(w:{},h:{}, tile:{}) is too big for hardware!",
                  template.descriptor.size.width, template.descriptor.size.height, template.tiling.unwrap_or(0));
            self.cached_images.insert(request.key, ImageResult::Err(ImageCacheError::OverLimitSize));
            return DeviceIntSize::zero();
        }

        let storage = match self.cached_images.entry(request.key) {
            Occupied(e) => {
                // We might have an existing untiled entry, and need to insert
                // a second entry. In such cases we need to move the old entry
                // out first, replacing it with a dummy entry, and then creating
                // the tiled/multi-entry variant.
                let entry = e.into_mut();
                if !request.is_untiled_auto() {
                    let untiled_entry = match entry {
                        &mut ImageResult::UntiledAuto(ref mut entry) => {
                            Some(mem::replace(entry, CachedImageInfo {
                                texture_cache_handle: TextureCacheHandle::invalid(),
                                dirty_rect: DirtyRect::All,
                                manual_eviction: false,
                            }))
                        }
                        _ => None
                    };

                    if let Some(untiled_entry) = untiled_entry {
                        let mut entries = ResourceClassCache::new();
                        let untiled_key = CachedImageKey {
                            rendering: ImageRendering::Auto,
                            tile: None,
                        };
                        entries.insert(untiled_key, untiled_entry);
                        *entry = ImageResult::Multi(entries);
                    }
                }
                entry
            }
            Vacant(entry) => {
                entry.insert(if request.is_untiled_auto() {
                    ImageResult::UntiledAuto(CachedImageInfo {
                        texture_cache_handle: TextureCacheHandle::invalid(),
                        dirty_rect: DirtyRect::All,
                        manual_eviction: false,
                    })
                } else {
                    ImageResult::Multi(ResourceClassCache::new())
                })
            }
        };

        // If this image exists in the texture cache, *and* the dirty rect
        // in the cache is empty, then it is valid to use as-is.
        let entry = match *storage {
            ImageResult::UntiledAuto(ref mut entry) => entry,
            ImageResult::Multi(ref mut entries) => {
                entries.entry(request.into())
                    .or_insert(CachedImageInfo {
                        texture_cache_handle: TextureCacheHandle::invalid(),
                        dirty_rect: DirtyRect::All,
                        manual_eviction: false,
                    })
            },
            ImageResult::Err(_) => panic!("Errors should already have been handled"),
        };

        let needs_upload = self.texture_cache.request(&entry.texture_cache_handle, gpu_buffer);

        if !needs_upload && entry.dirty_rect.is_empty() {
            return size;
        }

        if !self.pending_image_requests.insert(request) {
            return size;
        }

        if template.data.is_blob() {
            let request: BlobImageRequest = request.into();
            let missing = match self.rasterized_blob_images.get(&request.key) {
                Some(tiles) => !tiles.contains_key(&request.tile),
                _ => true,
            };

            assert!(!missing);
        }

        size
    }

    fn discard_tiles_outside_visible_area(
        &mut self,
        key: BlobImageKey,
        area: &DeviceIntRect
    ) {
        let tile_size = match self.resources.image_templates.get(key.as_image()) {
            Some(template) => template.tiling.unwrap(),
            None => {
                //debug!("Missing image template (key={:?})!", key);
                return;
            }
        };

        let tiles = match self.rasterized_blob_images.get_mut(&key) {
            Some(tiles) => tiles,
            _ => { return; }
        };

        let tile_range = compute_tile_range(
            &area,
            tile_size,
        );

        tiles.retain(|tile, _| { tile_range.contains(*tile) });

        let texture_cache = &mut self.texture_cache;
        match self.cached_images.try_get_mut(&key.as_image()) {
            Some(&mut ImageResult::Multi(ref mut entries)) => {
                entries.retain(|key, entry| {
                    if key.tile.is_none() || tile_range.contains(key.tile.unwrap()) {
                        return true;
                    }
                    entry.mark_unused(texture_cache);
                    return false;
                });
            }
            _ => {}
        }
    }

    fn set_image_visible_rect(&mut self, key: ImageKey, rect: &DeviceIntRect) {
        if let Some(image) = self.resources.image_templates.get_mut(key) {
            // Only blob images are sized by their visible rect; other templates must
            // keep the size that their data and stride were validated against.
            if !image.data.is_blob() {
                return;
            }
            image.visible_rect = *rect;
            image.descriptor.size = rect.size();
        }
    }

    pub fn request_glyphs(
        &mut self,
        mut font: FontInstance,
        glyph_keys: &[GlyphKey],
        gpu_buffer: &mut GpuBufferBuilderF,
    ) {
        debug_assert_eq!(self.state, State::AddResources);

        self.glyph_rasterizer.prepare_font(&mut font);
        let glyph_key_cache = self.cached_glyphs.insert_glyph_key_cache_for_font(&font);
        let texture_cache = &mut self.texture_cache;
        self.glyph_rasterizer.request_glyphs(
            font,
            glyph_keys,
            |key| {
                let cache_key = key.cache_key();
                if let Some(entry) = glyph_key_cache.try_get(&cache_key) {
                    match entry {
                        GlyphCacheEntry::Cached(ref glyph) => {
                            if !texture_cache.request(&glyph.texture_cache_handle, gpu_buffer) {
                                return false;
                            }
                            // This case gets hit when we already rasterized the glyph, but the
                            // glyph has been evicted from the texture cache. Just force it to
                            // pending so it gets rematerialized.
                        }
                        // Otherwise, skip the entry if it is blank or pending.
                        GlyphCacheEntry::Blank | GlyphCacheEntry::Pending => return false,
                    }
                };

                glyph_key_cache.add_glyph(cache_key, GlyphCacheEntry::Pending);

                true
            }
        );
    }

    pub fn pending_updates(&mut self) -> ResourceUpdateList {
        ResourceUpdateList {
            texture_updates: self.texture_cache.pending_updates(),
            native_surface_updates: mem::replace(&mut self.pending_native_surface_updates, Vec::new()),
        }
    }

    pub fn fetch_glyphs<F>(
        &self,
        mut font: FontInstance,
        glyph_keys: &[GlyphKey],
        gpu_buffer: &GpuBufferBuilderF,
        fetch_buffer: &mut Vec<GlyphFetchResult>,
        mut f: F,
    ) where
        F: FnMut(TextureSource, GlyphFormat, &[GlyphFetchResult]),
    {
        debug_assert_eq!(self.state, State::QueryResources);

        self.glyph_rasterizer.prepare_font(&mut font);
        let glyph_key_cache = self.cached_glyphs.get_glyph_key_cache_for_font(&font);

        let mut current_texture_id = TextureSource::Invalid;
        let mut current_glyph_format = GlyphFormat::Subpixel;
        debug_assert!(fetch_buffer.is_empty());

        for (loop_index, key) in glyph_keys.iter().enumerate() {
            let cache_key = key.cache_key();
            let (cache_item, glyph_format, is_packed_glyph) = match *glyph_key_cache.get(&cache_key) {
                GlyphCacheEntry::Cached(ref glyph) => {
                    (self.texture_cache.get(&glyph.texture_cache_handle), glyph.format, glyph.is_packed_glyph)
                }
                GlyphCacheEntry::Blank | GlyphCacheEntry::Pending => continue,
            };
            if current_texture_id != cache_item.texture_id ||
                current_glyph_format != glyph_format {
                if !fetch_buffer.is_empty() {
                    f(current_texture_id, current_glyph_format, fetch_buffer);
                    fetch_buffer.clear();
                }
                current_texture_id = cache_item.texture_id;
                current_glyph_format = glyph_format;
            }
            let (subpx_offset_x, subpx_offset_y) = key.subpixel_offset();
            fetch_buffer.push(GlyphFetchResult {
                index_in_text_run: loop_index as i32,
                uv_rect_address: gpu_buffer.resolve_handle(cache_item.uv_rect_handle),
                offset: DevicePoint::new(cache_item.user_data[0], cache_item.user_data[1]),
                size: cache_item.uv_rect.size(),
                scale: cache_item.user_data[2],
                subpx_offset_x: subpx_offset_x as u8,
                subpx_offset_y: subpx_offset_y as u8,
                is_packed_glyph,
            });
        }

        if !fetch_buffer.is_empty() {
            f(current_texture_id, current_glyph_format, fetch_buffer);
            fetch_buffer.clear();
        }
    }

    pub fn map_font_key(&self, key: FontKey) -> FontKey {
        self.resources.fonts.font_keys.map_key(&key)
    }

    pub fn map_font_instance_key(&self, key: FontInstanceKey) -> FontInstanceKey {
        self.resources.fonts.instance_keys.map_key(&key)
    }

    pub fn get_glyph_dimensions(
        &mut self,
        font: &FontInstance,
        glyph_index: GlyphIndex,
    ) -> Option<GlyphDimensions> {
        match self.cached_glyph_dimensions.entry((font.instance_key, glyph_index)) {
            Occupied(entry) => *entry.get(),
            Vacant(entry) => *entry.insert(
                self.glyph_rasterizer
                    .get_glyph_dimensions(font, glyph_index),
            ),
        }
    }

    pub fn get_glyph_index(&mut self, font_key: FontKey, ch: char) -> Option<u32> {
        self.glyph_rasterizer.get_glyph_index(font_key, ch)
    }

    #[inline]
    pub fn get_cached_image(&self, request: ImageRequest) -> Result<CacheItem, ()> {
        debug_assert_eq!(self.state, State::QueryResources);
        let image_info = self.get_image_info(request)?;

        if let Ok(item) = self.get_texture_cache_item(&image_info.texture_cache_handle) {
            // Common path.
            return Ok(item);
        }

        if self.resources.image_templates
            .get(request.key)
            .map_or(false, |img| img.data.is_snapshot()) {
            if self.debug_fallback_panic {
                panic!("Missing snapshot image");
            }
            return self.get_texture_cache_item(&self.fallback_handle);
        }

        panic!("Requested image missing from the texture cache");
    }

    pub fn get_cached_render_task(
        &self,
        handle: &RenderTaskCacheEntryHandle,
    ) -> &RenderTaskCacheEntry {
        self.cached_render_tasks.get_cache_entry(handle)
    }

    #[inline]
    fn get_image_info(&self, request: ImageRequest) -> Result<&CachedImageInfo, ()> {
        // TODO(Jerry): add a debug option to visualize the corresponding area for
        // the Err() case of CacheItem.
        match *self.cached_images.get(&request.key) {
            ImageResult::UntiledAuto(ref image_info) => Ok(image_info),
            ImageResult::Multi(ref entries) => Ok(entries.get(&request.into())),
            ImageResult::Err(_) => Err(()),
        }
    }

    #[inline]
    pub fn get_texture_cache_item(&self, handle: &TextureCacheHandle) -> Result<CacheItem, ()> {
        if let Some(item) = self.texture_cache.try_get(handle) {
            return Ok(item);
        }

        Err(())
    }

    pub fn get_image_properties(&self, image_key: ImageKey) -> Option<ImageProperties> {
        let image_template = &self.resources.image_templates.get(image_key);

        image_template.map(|image_template| {
            let external_image = match image_template.data {
                CachedImageData::External(ext_image) => match ext_image.image_type {
                    ExternalImageType::TextureHandle(_) => Some(ext_image),
                    // external buffer uses resource_cache.
                    ExternalImageType::Buffer => None,
                },
                // raw and blob image are all using resource_cache.
                CachedImageData::Raw(..)
                | CachedImageData::Blob
                | CachedImageData::Snapshot
                 => None,
            };

            ImageProperties {
                descriptor: image_template.descriptor,
                external_image,
                tiling: image_template.tiling,
                visible_rect: image_template.visible_rect,
                adjustment: image_template.adjustment,
            }
        })
    }

    pub fn begin_frame(&mut self, stamp: FrameStamp, profile: &mut TransactionProfile) {
        profile_scope!("begin_frame");
        debug_assert_eq!(self.state, State::Idle);
        self.state = State::AddResources;
        self.texture_cache.begin_frame(stamp, profile);
        self.picture_textures.begin_frame(stamp, &mut self.texture_cache.pending_updates);

        self.cached_glyphs.begin_frame(
            stamp,
            &mut self.texture_cache,
            &mut self.glyph_rasterizer,
        );
        self.cached_render_tasks.begin_frame(&mut self.texture_cache);
        self.current_frame_id = stamp.frame_id();

        // Pop the old frame and push a new one.
        // Recycle the allocation if any.
        let mut v = self.deleted_blob_keys.pop_front().unwrap_or_else(Vec::new);
        v.clear();
        self.deleted_blob_keys.push_back(v);

        self.texture_cache.run_compaction();
    }

    pub fn block_until_all_resources_added(
        &mut self,
        gpu_buffer: &mut GpuBufferBuilder,
        profile: &mut TransactionProfile,
    ) {
        profile_scope!("block_until_all_resources_added");

        debug_assert_eq!(self.state, State::AddResources);
        self.state = State::QueryResources;

        let cached_glyphs = &mut self.cached_glyphs;
        let texture_cache = &mut self.texture_cache;

        self.glyph_rasterizer.resolve_glyphs(
            |job, can_use_r8_format| {
                let GlyphRasterJob { font, key, result } = job;
                let cache_key = key.cache_key();
                let glyph_key_cache = cached_glyphs.get_glyph_key_cache_for_font_mut(&*font);
                let glyph_info = match result {
                    Err(_) => GlyphCacheEntry::Blank,
                    Ok(ref glyph) if glyph.width == 0 || glyph.height == 0 => {
                        GlyphCacheEntry::Blank
                    }
                    Ok(glyph) => {
                        let mut texture_cache_handle = TextureCacheHandle::invalid();
                        texture_cache.request(&texture_cache_handle, &mut gpu_buffer.f32);
                        texture_cache.update(
                            &mut texture_cache_handle,
                            ImageDescriptor {
                                size: size2(glyph.width, glyph.height),
                                stride: None,
                                format: glyph.format.image_format(can_use_r8_format),
                                flags: ImageDescriptorFlags::empty(),
                                offset: 0,
                            },
                            TextureFilter::Linear,
                            Some(CachedImageData::Raw(Arc::new(glyph.bytes))),
                            [glyph.left, -glyph.top, glyph.scale, 0.0],
                            DirtyRect::All,
                            &mut gpu_buffer.f32,
                            Some(glyph_key_cache.eviction_notice()),
                            UvRectKind::Rect,
                            Eviction::Auto,
                            TargetShader::Text,
                            false,
                        );
                        GlyphCacheEntry::Cached(CachedGlyphInfo {
                            texture_cache_handle,
                            format: glyph.format,
                            is_packed_glyph: glyph.is_packed_glyph,
                        })
                    }
                };
                glyph_key_cache.insert(cache_key, glyph_info);
            },
            profile,
        );

        // Apply any updates of new / updated images (incl. blobs) to the texture cache.
        self.update_texture_cache(gpu_buffer);
    }

    fn update_texture_cache(&mut self, gpu_buffer: &mut GpuBufferBuilder) {
        profile_scope!("update_texture_cache");

        if self.fallback_handle == TextureCacheHandle::invalid() {
            let fallback_color = if self.debug_fallback_pink {
                vec![255, 0, 255, 255]
            } else {
                vec![0, 0, 0, 0]
            };
            self.texture_cache.update(
                &mut self.fallback_handle,
                ImageDescriptor {
                    size: size2(1, 1),
                    stride: None,
                    format: ImageFormat::BGRA8,
                    flags: ImageDescriptorFlags::empty(),
                    offset: 0,
                },
                TextureFilter::Linear,
                Some(CachedImageData::Raw(Arc::new(fallback_color))),
                [0.0; 4],
                DirtyRect::All,
                &mut gpu_buffer.f32,
                None,
                UvRectKind::Rect,
                Eviction::Manual,
                TargetShader::Default,
                false,
            );
        }

        for request in self.pending_image_requests.drain() {
            let image_template = self.resources.image_templates.get_mut(request.key).unwrap();
            debug_assert!(image_template.data.uses_texture_cache());

            let mut updates: SmallVec<[(CachedImageData, Option<DeviceIntRect>); 1]> = SmallVec::new();

            match image_template.data {
                CachedImageData::Snapshot => {
                    // The update is done in ResourceCache::render_as_image.
                }
                CachedImageData::Raw(..)
                | CachedImageData::External(..) => {
                    // Safe to clone here since the Raw image data is an
                    // Arc, and the external image data is small.
                    updates.push((image_template.data.clone(), None));
                }
                CachedImageData::Blob => {
                    let blob_image = self.rasterized_blob_images.get_mut(&BlobImageKey(request.key)).unwrap();
                    let img = &blob_image[&request.tile.unwrap()];
                    updates.push((
                        CachedImageData::Raw(Arc::clone(&img.data)),
                        Some(img.rasterized_rect)
                    ));
                }
            };

            for (image_data, blob_rasterized_rect) in updates {
                let entry = match *self.cached_images.get_mut(&request.key) {
                    ImageResult::UntiledAuto(ref mut entry) => entry,
                    ImageResult::Multi(ref mut entries) => entries.get_mut(&request.into()),
                    ImageResult::Err(_) => panic!("Update requested for invalid entry")
                };

                let mut descriptor = image_template.descriptor.clone();
                let mut dirty_rect = entry.dirty_rect.replace_with_empty();

                if let Some(tile) = request.tile {
                    let tile_size = image_template.tiling.unwrap();
                    let clipped_tile_size = compute_tile_size(&image_template.visible_rect, tile_size, tile);
                    // The tiled image could be stored on the CPU as one large image or be
                    // already broken up into tiles. This affects the way we compute the stride
                    // and offset.
                    let tiled_on_cpu = image_template.data.is_blob();
                    if !tiled_on_cpu {
                        // we don't expect to have partial tiles at the top and left of non-blob
                        // images.
                        debug_assert_eq!(image_template.visible_rect.min, point2(0, 0));
                        let bpp = descriptor.format.bytes_per_pixel();
                        let stride = descriptor.compute_stride();
                        descriptor.stride = Some(stride);
                        descriptor.offset +=
                            tile.y as i32 * tile_size as i32 * stride +
                            tile.x as i32 * tile_size as i32 * bpp;
                    }

                    descriptor.size = clipped_tile_size;
                }

                // If we are uploading the dirty region of a blob image we might have several
                // rects to upload so we use each of these rasterized rects rather than the
                // overall dirty rect of the image.
                if let Some(rect) = blob_rasterized_rect {
                    dirty_rect = DirtyRect::Partial(rect);
                }

                let filter = match request.rendering {
                    ImageRendering::Pixelated => {
                        TextureFilter::Nearest
                    }
                    ImageRendering::Auto | ImageRendering::CrispEdges => {
                        // If the texture uses linear filtering, enable mipmaps and
                        // trilinear filtering, for better image quality. We only
                        // support this for now on textures that are not placed
                        // into the shared cache. This accounts for any image
                        // that is > 512 in either dimension, so it should cover
                        // the most important use cases. We may want to support
                        // mip-maps on shared cache items in the future.
                        if descriptor.allow_mipmaps() &&
                           descriptor.size.width > 512 &&
                           descriptor.size.height > 512 &&
                           !self.texture_cache.is_allowed_in_shared_cache(
                            TextureFilter::Linear,
                            &descriptor,
                        ) {
                            TextureFilter::Trilinear
                        } else {
                            TextureFilter::Linear
                        }
                    }
                };

                let eviction = match &image_template.data {
                    CachedImageData::Blob | CachedImageData::Snapshot => {
                        entry.manual_eviction = true;
                        Eviction::Manual
                    }
                    _ => {
                        Eviction::Auto
                    }
                };

                //Note: at this point, the dirty rectangle is local to the descriptor space
                self.texture_cache.update(
                    &mut entry.texture_cache_handle,
                    descriptor,
                    filter,
                    Some(image_data),
                    [0.0; 4],
                    dirty_rect,
                    &mut gpu_buffer.f32,
                    None,
                    UvRectKind::Rect,
                    eviction,
                    TargetShader::Default,
                    false,
                );
            }
        }
    }

    pub fn create_compositor_backdrop_surface(
        &mut self,
        color: ColorF
    ) -> NativeSurfaceId {
        let id = NativeSurfaceId(NEXT_NATIVE_SURFACE_ID.fetch_add(1, Ordering::Relaxed) as u64);

        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::CreateBackdropSurface {
                    id,
                    color,
                },
            }
        );

        id
    }

    /// Queue up allocation of a new OS native compositor surface with the
    /// specified tile size.
    pub fn create_compositor_surface(
        &mut self,
        virtual_offset: DeviceIntPoint,
        tile_size: DeviceIntSize,
        is_opaque: bool,
    ) -> NativeSurfaceId {
        let id = NativeSurfaceId(NEXT_NATIVE_SURFACE_ID.fetch_add(1, Ordering::Relaxed) as u64);

        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::CreateSurface {
                    id,
                    virtual_offset,
                    tile_size,
                    is_opaque,
                },
            }
        );

        id
    }

    pub fn create_compositor_external_surface(
        &mut self,
        is_opaque: bool,
    ) -> NativeSurfaceId {
        let id = NativeSurfaceId(NEXT_NATIVE_SURFACE_ID.fetch_add(1, Ordering::Relaxed) as u64);

        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::CreateExternalSurface {
                    id,
                    is_opaque,
                },
            }
        );

        id
    }

    /// Queue up destruction of an existing native OS surface. This is used when
    /// a picture cache surface is dropped or resized.
    pub fn destroy_compositor_surface(
        &mut self,
        id: NativeSurfaceId,
    ) {
        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::DestroySurface {
                    id,
                }
            }
        );
    }

    /// Queue construction of a native compositor tile on a given surface.
    pub fn create_compositor_tile(
        &mut self,
        id: NativeTileId,
    ) {
        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::CreateTile {
                    id,
                },
            }
        );
    }

    /// Queue destruction of a native compositor tile.
    pub fn destroy_compositor_tile(
        &mut self,
        id: NativeTileId,
    ) {
        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::DestroyTile {
                    id,
                },
            }
        );
    }

    pub fn attach_compositor_external_image(
        &mut self,
        id: NativeSurfaceId,
        external_image: ExternalImageId,
    ) {
        self.pending_native_surface_updates.push(
            NativeSurfaceOperation {
                details: NativeSurfaceOperationDetails::AttachExternalImage {
                    id,
                    external_image,
                },
            }
        );
    }


    pub fn end_frame(&mut self, profile: &mut TransactionProfile) {
        debug_assert_eq!(self.state, State::QueryResources);
        profile_scope!("end_frame");
        self.state = State::Idle;

        // GC the render target pool, if it's currently > 64 MB in size.
        //
        // We use a simple scheme whereby we drop any texture that hasn't been used
        // in the last 60 frames, until we are below the size threshold. This should
        // generally prevent any sustained build-up of unused textures, unless we don't
        // generate frames for a long period. This can happen when the window is
        // minimized, and we probably want to flush all the WebRender caches in that case [1].
        // There is also a second "red line" memory threshold which prevents
        // memory exhaustion if many render targets are allocated within a small
        // number of frames. For now this is set at 320 MB (10x the normal memory threshold).
        //
        // [1] https://bugzilla.mozilla.org/show_bug.cgi?id=1494099
        self.gc_render_targets(
            64 * 1024 * 1024,
            32 * 1024 * 1024 * 10,
            60,
        );

        self.texture_cache.end_frame(profile);
        self.picture_textures.gc(
            &mut self.texture_cache.pending_updates,
        );

        self.picture_textures.update_profile(profile);
    }

    pub fn set_debug_flags(&mut self, flags: DebugFlags) {
        GLYPH_FLASHING.store(flags.contains(DebugFlags::GLYPH_FLASHING), std::sync::atomic::Ordering::Relaxed);
        self.texture_cache.set_debug_flags(flags);
        self.picture_textures.set_debug_flags(flags);
        self.debug_fallback_panic = flags.contains(DebugFlags::MISSING_SNAPSHOT_PANIC);
        let fallback_pink = flags.contains(DebugFlags::MISSING_SNAPSHOT_PINK);

        if fallback_pink != self.debug_fallback_pink && self.fallback_handle != TextureCacheHandle::invalid() {
            self.texture_cache.evict_handle(&self.fallback_handle);
        }
        self.debug_fallback_pink = fallback_pink;
    }

    pub fn clear(&mut self, what: ClearCache) {
        if what.contains(ClearCache::IMAGES) {
            for (_key, mut cached) in self.cached_images.resources.drain() {
                cached.drop_from_cache(&mut self.texture_cache);
            }
        }
        if what.contains(ClearCache::GLYPHS) {
            self.cached_glyphs.clear();
        }
        if what.contains(ClearCache::GLYPH_DIMENSIONS) {
            self.cached_glyph_dimensions.clear();
        }
        if what.contains(ClearCache::RENDER_TASKS) {
            self.cached_render_tasks.clear();
        }
        if what.contains(ClearCache::TEXTURE_CACHE) {
            self.texture_cache.clear_all();
            self.picture_textures.clear(&mut self.texture_cache.pending_updates);
        }
        if what.contains(ClearCache::RENDER_TARGETS) {
            self.clear_render_target_pool();
        }
    }

    pub fn clear_namespace(&mut self, namespace: IdNamespace) {
        self.clear_images(|k| k.0 == namespace);

        // First clear out any non-shared resources associated with the namespace.
        self.resources.fonts.instances.clear_namespace(namespace);
        let deleted_keys = self.resources.fonts.templates.clear_namespace(namespace);
        self.glyph_rasterizer.delete_fonts(&deleted_keys);
        self.cached_glyphs.clear_namespace(namespace);
        if let Some(handler) = &mut self.blob_image_handler {
            handler.clear_namespace(namespace);
        }

        // Check for any shared instance keys that were remapped from the namespace.
        let shared_instance_keys = self.resources.fonts.instance_keys.clear_namespace(namespace);
        if !shared_instance_keys.is_empty() {
            self.resources.fonts.instances.delete_font_instances(&shared_instance_keys);
            self.cached_glyphs.delete_font_instances(&shared_instance_keys, &mut self.glyph_rasterizer);
            // Blob font instances are not shared across namespaces, so there is no
            // need to call the handler for them individually.
        }

        // Finally check for any shared font keys that were remapped from the namespace.
        let shared_keys = self.resources.fonts.font_keys.clear_namespace(namespace);
        if !shared_keys.is_empty() {
            self.glyph_rasterizer.delete_fonts(&shared_keys);
            self.resources.fonts.templates.delete_fonts(&shared_keys);
            self.cached_glyphs.delete_fonts(&shared_keys);
            if let Some(handler) = &mut self.blob_image_handler {
                for &key in &shared_keys {
                    handler.delete_font(key);
                }
            }
        }
    }

    /// Reports the CPU heap usage of this ResourceCache.
    ///
    /// NB: It would be much better to use the derive(MallocSizeOf) machinery
    /// here, but the Arcs complicate things. The two ways to handle that would
    /// be to either (a) Implement MallocSizeOf manually for the things that own
    /// them and manually avoid double-counting, or (b) Use the "seen this pointer
    /// yet" machinery from the proper malloc_size_of crate. We can do this if/when
    /// more accurate memory reporting on these resources becomes a priority.
    pub fn report_memory(&self, op: VoidPtrToSizeFn) -> MemoryReport {
        let mut report = MemoryReport::default();

        let mut seen_fonts = std::collections::HashSet::new();
        // Measure fonts. We only need the templates here, because the instances
        // don't have big buffers.
        for (_, font) in self.resources.fonts.templates.lock().iter() {
            if let FontTemplate::Raw(ref raw, _) = font {
                report.fonts += unsafe { op(raw.as_ptr() as *const c_void) };
                seen_fonts.insert(raw.as_ptr());
            }
        }

        for font in self.resources.weak_fonts.iter() {
            if !seen_fonts.contains(&font.as_ptr()) {
                report.weak_fonts += unsafe { op(font.as_ptr() as *const c_void) };
            }
        }

        // Measure images.
        for (_, image) in self.resources.image_templates.images.iter() {
            report.images += match image.data {
                CachedImageData::Raw(ref v) => unsafe { op(v.as_ptr() as *const c_void) },
                CachedImageData::Blob
                | CachedImageData::External(..)
                | CachedImageData::Snapshot => 0,
            }
        }

        // Mesure rasterized blobs.
        // TODO(gw): Temporarily disabled while we roll back a crash. We can re-enable
        //           these when that crash is fixed.
        /*
        for (_, image) in self.rasterized_blob_images.iter() {
            let mut accumulate = |b: &RasterizedBlobImage| {
                report.rasterized_blobs += unsafe { op(b.data.as_ptr() as *const c_void) };
            };
            match image {
                RasterizedBlob::Tiled(map) => map.values().for_each(&mut accumulate),
                RasterizedBlob::NonTiled(vec) => vec.iter().for_each(&mut accumulate),
            };
        }
        */

        report
    }

    /// Properly deletes all images matching the predicate.
    fn clear_images<F: Fn(&ImageKey) -> bool>(&mut self, f: F) {
        let keys = self.resources.image_templates.images.keys().filter(|k| f(*k))
            .cloned().collect::<SmallVec<[ImageKey; 16]>>();

        for key in keys {
            self.delete_image_template(key);
        }

        #[cfg(feature="leak_checks")]
        let check_leaks = true;
        #[cfg(not(feature="leak_checks"))]
        let check_leaks = false;

        if check_leaks {
            let blob_f = |key: &BlobImageKey| { f(&key.as_image()) };
            assert!(!self.resources.image_templates.images.keys().any(&f));
            assert!(!self.cached_images.resources.keys().any(&f));
            assert!(!self.rasterized_blob_images.keys().any(&blob_f));
        }
    }

    /// Get a render target from the pool, or allocate a new one if none are
    /// currently available that match the requested parameters.
    pub fn get_or_create_render_target_from_pool(
        &mut self,
        size: DeviceIntSize,
        format: ImageFormat,
    ) -> CacheTextureId {
        for target in &mut self.render_target_pool {
            if target.size == size &&
               target.format == format &&
               !target.is_active {
                // Found a target that's not currently in use which matches. Update
                // the last_frame_used for GC purposes.
                target.is_active = true;
                target.last_frame_used = self.current_frame_id;
                return target.texture_id;
            }
        }

        // Need to create a new render target and add it to the pool

        let texture_id = self.texture_cache.alloc_render_target(
            size,
            format,
        );

        self.render_target_pool.push(RenderTarget {
            size,
            format,
            texture_id,
            is_active: true,
            last_frame_used: self.current_frame_id,
        });

        texture_id
    }

    /// Return a render target to the pool.
    pub fn return_render_target_to_pool(
        &mut self,
        id: CacheTextureId,
    ) {
        let target = self.render_target_pool
            .iter_mut()
            .find(|t| t.texture_id == id)
            .expect("bug: invalid render target id");

        assert!(target.is_active);
        target.is_active = false;
    }

    /// Clear all current render targets (e.g. on memory pressure)
    fn clear_render_target_pool(
        &mut self,
    ) {
        for target in self.render_target_pool.drain(..) {
            debug_assert!(!target.is_active);
            self.texture_cache.free_render_target(target.texture_id);
        }
    }

    /// Garbage collect and remove old render targets from the pool that haven't
    /// been used for some time.
    fn gc_render_targets(
        &mut self,
        total_bytes_threshold: usize,
        total_bytes_red_line_threshold: usize,
        frames_threshold: u64,
    ) {
        // Get the total GPU memory size used by the current render target pool
        let mut rt_pool_size_in_bytes: usize = self.render_target_pool
            .iter()
            .map(|t| t.size_in_bytes())
            .sum();

        // If the total size of the pool is less than the threshold, don't bother
        // trying to GC any targets
        if rt_pool_size_in_bytes <= total_bytes_threshold {
            return;
        }

        // Sort the current pool by age, so that we remove oldest textures first
        self.render_target_pool.sort_by_key(|t| t.last_frame_used);

        // We can't just use retain() because `RenderTarget` requires manual cleanup.
        let mut retained_targets = SmallVec::<[RenderTarget; 8]>::new();

        for target in self.render_target_pool.drain(..) {
            assert!(!target.is_active);

            // Drop oldest textures until we are under the allowed size threshold.
            // However, if it's been used in very recently, it is always kept around,
            // which ensures we don't thrash texture allocations on pages that do
            // require a very large render target pool and are regularly changing.
            let above_red_line = rt_pool_size_in_bytes > total_bytes_red_line_threshold;
            let above_threshold = rt_pool_size_in_bytes > total_bytes_threshold;
            let used_recently = target.used_recently(self.current_frame_id, frames_threshold);
            let used_this_frame = target.last_frame_used == self.current_frame_id;

            if !used_this_frame && (above_red_line || (above_threshold && !used_recently)) {
                rt_pool_size_in_bytes -= target.size_in_bytes();
                self.texture_cache.free_render_target(target.texture_id);
            } else {
                retained_targets.push(target);
            }
        }

        self.render_target_pool.extend(retained_targets);
    }

    #[cfg(test)]
    pub fn validate_surfaces(
        &self,
        expected_surfaces: &[(i32, i32, ImageFormat)],
    ) {
        assert_eq!(expected_surfaces.len(), self.render_target_pool.len());

        for (expected, surface) in expected_surfaces.iter().zip(self.render_target_pool.iter()) {
            assert_eq!(DeviceIntSize::new(expected.0, expected.1), surface.size);
            assert_eq!(expected.2, surface.format);
        }
    }
}

impl Drop for ResourceCache {
    fn drop(&mut self) {
        self.clear_images(|_| true);
    }
}

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainFontTemplate {
    data: String,
    index: u32,
}

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainImageTemplate {
    data: String,
    descriptor: ImageDescriptor,
    tiling: Option<TileSize>,
    generation: ImageGeneration,
}

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PlainResources {
    font_templates: FastHashMap<FontKey, PlainFontTemplate>,
    font_instances: Vec<BaseFontInstance>,
    image_templates: FastHashMap<ImageKey, PlainImageTemplate>,
}

#[cfg(feature = "capture")]
#[derive(Serialize)]
pub struct PlainCacheRef<'a> {
    current_frame_id: FrameId,
    glyphs: &'a GlyphCache,
    glyph_dimensions: &'a GlyphDimensionsCache,
    images: &'a ImageCache,
    render_tasks: &'a RenderTaskCache,
    textures: &'a TextureCache,
    picture_textures: &'a PictureTextures,
}

#[cfg(feature = "replay")]
#[derive(Deserialize)]
pub struct PlainCacheOwn {
    current_frame_id: FrameId,
    glyphs: GlyphCache,
    glyph_dimensions: GlyphDimensionsCache,
    images: ImageCache,
    render_tasks: RenderTaskCache,
    textures: TextureCache,
    picture_textures: PictureTextures,
}

#[cfg(feature = "replay")]
const NATIVE_FONT: &'static [u8] = include_bytes!("../res/Proggy.ttf");

// This currently only casts the unit but will soon apply an offset
fn to_image_dirty_rect(blob_dirty_rect: &BlobDirtyRect) -> ImageDirtyRect {
    match *blob_dirty_rect {
        DirtyRect::Partial(rect) => DirtyRect::Partial(rect.cast_unit()),
        DirtyRect::All => DirtyRect::All,
    }
}

impl ResourceCache {
    #[cfg(feature = "capture")]
    pub fn save_capture(
        &mut self, root: &PathBuf
    ) -> (PlainResources, Vec<ExternalCaptureImage>) {
        use std::fs;
        use std::io::Write;

        info!("saving resource cache");
        let res = &self.resources;
        let path_fonts = root.join("fonts");
        if !path_fonts.is_dir() {
            fs::create_dir(&path_fonts).unwrap();
        }
        let path_images = root.join("images");
        if !path_images.is_dir() {
            fs::create_dir(&path_images).unwrap();
        }
        let path_blobs = root.join("blobs");
        if !path_blobs.is_dir() {
            fs::create_dir(&path_blobs).unwrap();
        }
        let path_externals = root.join("externals");
        if !path_externals.is_dir() {
            fs::create_dir(&path_externals).unwrap();
        }

        info!("\tfont templates");
        let mut font_paths = FastHashMap::default();
        for template in res.fonts.templates.lock().values() {
            let data: &[u8] = match *template {
                FontTemplate::Raw(ref arc, _) => arc,
                FontTemplate::Native(_) => continue,
            };
            let font_id = res.fonts.templates.len() + 1;
            let entry = match font_paths.entry(data.as_ptr()) {
                Entry::Occupied(_) => continue,
                Entry::Vacant(e) => e,
            };
            let file_name = format!("{}.raw", font_id);
            let short_path = format!("fonts/{}", file_name);
            fs::File::create(path_fonts.join(file_name))
                .expect(&format!("Unable to create {}", short_path))
                .write_all(data)
                .unwrap();
            entry.insert(short_path);
        }

        info!("\timage templates");
        let mut image_paths = FastHashMap::default();
        let mut other_paths = FastHashMap::default();
        let mut num_blobs = 0;
        let mut external_images = Vec::new();
        for (&key, template) in res.image_templates.images.iter() {
            let desc = &template.descriptor;
            match template.data {
                CachedImageData::Raw(ref arc) => {
                    let image_id = image_paths.len() + 1;
                    let entry = match image_paths.entry(arc.as_ptr()) {
                        Entry::Occupied(_) => continue,
                        Entry::Vacant(e) => e,
                    };

                    #[cfg(feature = "png")]
                    CaptureConfig::save_png(
                        root.join(format!("images/{}.png", image_id)),
                        desc.size,
                        desc.format,
                        desc.stride,
                        &arc,
                    );
                    let file_name = format!("{}.raw", image_id);
                    let short_path = format!("images/{}", file_name);
                    fs::File::create(path_images.join(file_name))
                        .expect(&format!("Unable to create {}", short_path))
                        .write_all(&*arc)
                        .unwrap();
                    entry.insert(short_path);
                }
                CachedImageData::Blob => {
                    warn!("Tiled blob images aren't supported yet");
                    let result = RasterizedBlobImage {
                        rasterized_rect: desc.size.into(),
                        data: Arc::new(vec![0; desc.compute_total_size() as usize])
                    };

                    assert_eq!(result.rasterized_rect.size(), desc.size);
                    assert_eq!(result.data.len(), desc.compute_total_size() as usize);

                    num_blobs += 1;
                    #[cfg(feature = "png")]
                    CaptureConfig::save_png(
                        root.join(format!("blobs/{}.png", num_blobs)),
                        desc.size,
                        desc.format,
                        desc.stride,
                        &result.data,
                    );
                    let file_name = format!("{}.raw", num_blobs);
                    let short_path = format!("blobs/{}", file_name);
                    let full_path = path_blobs.clone().join(&file_name);
                    fs::File::create(full_path)
                        .expect(&format!("Unable to create {}", short_path))
                        .write_all(&result.data)
                        .unwrap();
                    other_paths.insert(key, short_path);
                }
                CachedImageData::Snapshot => {
                    let short_path = format!("snapshots/{}", external_images.len() + 1);
                    other_paths.insert(key, short_path.clone());
                }
                CachedImageData::External(ref ext) => {
                    let short_path = format!("externals/{}", external_images.len() + 1);
                    other_paths.insert(key, short_path.clone());
                    external_images.push(ExternalCaptureImage {
                        short_path,
                        descriptor: desc.clone(),
                        external: ext.clone(),
                    });
                }
            }
        }

        let mut font_templates = FastHashMap::default();
        let mut font_remap = FastHashMap::default();
        // Generate a map from duplicate font keys to their template.
        for key in res.fonts.font_keys.keys() {
            let shared_key = res.fonts.font_keys.map_key(&key);
            let template = match res.fonts.templates.get_font(&shared_key) {
                Some(template) => template,
                None => {
                    debug!("Failed serializing font template {:?}", key);
                    continue;
                }
            };
            let plain_font = match template {
                FontTemplate::Raw(arc, index) => {
                    PlainFontTemplate {
                        data: font_paths[&arc.as_ptr()].clone(),
                        index,
                    }
                }
                #[cfg(not(any(target_os = "macos", target_os = "ios")))]
                FontTemplate::Native(native) => {
                    PlainFontTemplate {
                        data: native.path.to_string_lossy().to_string(),
                        index: native.index,
                    }
                }
                #[cfg(any(target_os = "macos", target_os = "ios"))]
                FontTemplate::Native(native) => {
                    PlainFontTemplate {
                        data: native.name,
                        index: 0,
                    }
                }
            };
            font_templates.insert(key, plain_font);
            // Generate a reverse map from a shared key to a representive key.
            font_remap.insert(shared_key, key);
        }
        let mut font_instances = Vec::new();
        // Build a list of duplicate instance keys.
        for instance_key in res.fonts.instance_keys.keys() {
            let shared_key = res.fonts.instance_keys.map_key(&instance_key);
            let instance = match res.fonts.instances.get_font_instance(shared_key) {
                Some(instance) => instance,
                None => {
                    debug!("Failed serializing font instance {:?}", instance_key);
                    continue;
                }
            };
            // Target the instance towards a representive duplicate font key. The font key will be
            // de-duplicated on load to an appropriate shared key.
            font_instances.push(BaseFontInstance {
                font_key: font_remap.get(&instance.font_key).cloned().unwrap_or(instance.font_key),
                instance_key,
                ..(*instance).clone()
            });
        }
        let resources = PlainResources {
            font_templates,
            font_instances,
            image_templates: res.image_templates.images
                .iter()
                .map(|(key, template)| {
                    (*key, PlainImageTemplate {
                        data: match template.data {
                            CachedImageData::Raw(ref arc) => image_paths[&arc.as_ptr()].clone(),
                            _ => other_paths[key].clone(),
                        },
                        descriptor: template.descriptor.clone(),
                        tiling: template.tiling,
                        generation: template.generation,
                    })
                })
                .collect(),
        };

        (resources, external_images)
    }

    #[cfg(feature = "capture")]
    pub fn save_caches(&self, _root: &PathBuf) -> PlainCacheRef {
        PlainCacheRef {
            current_frame_id: self.current_frame_id,
            glyphs: &self.cached_glyphs,
            glyph_dimensions: &self.cached_glyph_dimensions,
            images: &self.cached_images,
            render_tasks: &self.cached_render_tasks,
            textures: &self.texture_cache,
            picture_textures: &self.picture_textures,
        }
    }

    #[cfg(feature = "replay")]
    pub fn load_capture(
        &mut self,
        resources: PlainResources,
        caches: Option<PlainCacheOwn>,
        config: &CaptureConfig,
    ) -> Vec<PlainExternalImage> {
        use std::{fs, path::Path};
        use crate::texture_cache::TextureCacheConfig;

        info!("loading resource cache");
        //TODO: instead of filling the local path to Arc<data> map as we process
        // each of the resource types, we could go through all of the local paths
        // and fill out the map as the first step.
        let mut raw_map = FastHashMap::<String, Arc<Vec<u8>>>::default();

        self.clear(ClearCache::all());
        self.clear_images(|_| true);

        match caches {
            Some(cached) => {
                self.current_frame_id = cached.current_frame_id;
                self.cached_glyphs = cached.glyphs;
                self.cached_glyph_dimensions = cached.glyph_dimensions;
                self.cached_images = cached.images;
                self.cached_render_tasks = cached.render_tasks;
                self.texture_cache = cached.textures;
                self.picture_textures = cached.picture_textures;
            }
            None => {
                self.current_frame_id = FrameId::INVALID;
                self.texture_cache = TextureCache::new(
                    self.texture_cache.max_texture_size(),
                    self.texture_cache.tiling_threshold(),
                    self.texture_cache.color_formats(),
                    self.texture_cache.swizzle_settings(),
                    &TextureCacheConfig::DEFAULT,
                );
                self.picture_textures = PictureTextures::new(
                    self.picture_textures.default_tile_size(),
                    self.picture_textures.filter(),
                );
            }
        }

        self.glyph_rasterizer.reset();
        let res = &mut self.resources;
        res.fonts.templates.clear();
        res.fonts.instances.clear();
        res.image_templates.images.clear();

        info!("\tfont templates...");
        let root = config.resource_root();
        let native_font_replacement = Arc::new(NATIVE_FONT.to_vec());
        for (key, plain_template) in resources.font_templates {
            let arc = match raw_map.entry(plain_template.data) {
                Entry::Occupied(e) => {
                    e.get().clone()
                }
                Entry::Vacant(e) => {
                    let file_path = if Path::new(e.key()).is_absolute() {
                        PathBuf::from(e.key())
                    } else {
                        root.join(e.key())
                    };
                    let arc = match fs::read(file_path) {
                        Ok(buffer) => Arc::new(buffer),
                        Err(err) => {
                            error!("Unable to open font template {:?}: {:?}", e.key(), err);
                            Arc::clone(&native_font_replacement)
                        }
                    };
                    e.insert(arc).clone()
                }
            };

            let template = FontTemplate::Raw(arc, plain_template.index);
            // Only add the template if this is the first time it has been seen.
            if let Some(shared_key) = res.fonts.font_keys.add_key(&key, &template) {
                self.glyph_rasterizer.add_font(shared_key, template.clone());
                res.fonts.templates.add_font(shared_key, template);
            }
        }

        info!("\tfont instances...");
        for instance in resources.font_instances {
            // Target the instance to a shared font key.
            let base = BaseFontInstance {
                font_key: res.fonts.font_keys.map_key(&instance.font_key),
                ..instance
            };
            if let Some(shared_instance) = res.fonts.instance_keys.add_key(base) {
                res.fonts.instances.add_font_instance(shared_instance);
            }
        }

        info!("\timage templates...");
        let mut external_images = Vec::new();
        for (key, template) in resources.image_templates {
            let data = if template.data.starts_with("snapshots/") {
                // TODO(nical): If a snapshot was captured in a previous frame,
                // we have to serialize/deserialize the image itself.
                CachedImageData::Snapshot
            } else {
                match config.deserialize_for_resource::<PlainExternalImage, _>(&template.data) {
                    Some(plain) => {
                        let ext_data = plain.external;
                        external_images.push(plain);
                        CachedImageData::External(ext_data)
                    }
                    None => {
                        let arc = match raw_map.entry(template.data) {
                            Entry::Occupied(e) => e.get().clone(),
                            Entry::Vacant(e) => {
                                match fs::read(root.join(e.key())) {
                                    Ok(buffer) => {
                                        e.insert(Arc::new(buffer)).clone()
                                    }
                                    Err(err) => {
                                        log::warn!("Unable to open {}: {err:?}", e.key());
                                        continue;
                                    }
                                }
                            }
                        };
                        CachedImageData::Raw(arc)
                    }
                }
            };

            res.image_templates.images.insert(key, ImageResource {
                data,
                descriptor: template.descriptor,
                tiling: template.tiling,
                visible_rect: template.descriptor.size.into(),
                adjustment: AdjustedImageSource::new(), // TODO(nical)
                generation: template.generation,
            });
        }

        external_images
    }

    #[cfg(feature = "capture")]
    pub fn save_capture_sequence(&mut self, config: &mut CaptureConfig) -> Vec<ExternalCaptureImage> {
        if self.capture_dirty {
            self.capture_dirty = false;
            config.prepare_resource();
            let (resources, deferred) = self.save_capture(&config.resource_root());
            config.serialize_for_resource(&resources, "plain-resources.ron");
            deferred
        } else {
            Vec::new()
        }
    }
}
