/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BorderRadius, ClipId, ClipMode, ColorF, DebugFlags, PrimitiveFlags, QualitySettings, RasterSpace};
use api::units::*;
use crate::clip::{clamped_radius, ClipItemKeyKind, ClipNodeId, ClipTreeBuilder, SceneClipStore, intersect_rounded_rects};
use crate::internal_types::FastHashMap;
use crate::picture::{PrimitiveList, PictureInstance, Picture3DContext, PictureFlags};
use crate::picture_composite_mode::PictureCompositeMode;
use crate::tile_cache::{SliceId, TileCacheParams};
use crate::prim_store::{PrimitiveInstance, PrimitiveStore, PictureIndex};
use crate::scene_building::SliceFlags;
use crate::spatial_tree::{SpatialNodeIndex, SceneSpatialTree};
use crate::util::VecHelper;
use std::mem;

/*
 Types and functionality related to picture caching. In future, we'll
 move more and more of the existing functionality out of picture.rs
 and into here.
 */

// If the page would create too many slices (an arbitrary definition where
// it's assumed the GPU memory + compositing overhead would be too high)
// then create a single picture cache for the remaining content. This at
// least means that we can cache small content changes efficiently when
// scrolling isn't occurring. Scrolling regions will be handled reasonably
// efficiently by the dirty rect tracking (since it's likely that if the
// page has so many slices there isn't a single major scroll region).
const MAX_CACHE_SLICES: usize = 16;

struct SliceDescriptor {
    prim_list: PrimitiveList,
    scroll_root: SpatialNodeIndex,
}

enum SliceKind {
    Default {
        secondary_slices: Vec<SliceDescriptor>,
    },
    Atomic {
        prim_list: PrimitiveList,
    },
}

impl SliceKind {
    fn default() -> Self {
        SliceKind::Default {
            secondary_slices: Vec::new(),
        }
    }
}

struct PrimarySlice {
    /// Whether this slice is atomic or has secondary slice(s)
    kind: SliceKind,
    /// Optional background color of this slice
    background_color: Option<ColorF>,
    /// Optional root clip for the iframe
    iframe_clip: Option<ClipId>,
    /// Information about how to draw and composite this slice
    slice_flags: SliceFlags,
}

impl PrimarySlice {
    fn new(
        slice_flags: SliceFlags,
        iframe_clip: Option<ClipId>,
        background_color: Option<ColorF>,
    ) -> Self {
        PrimarySlice {
            kind: SliceKind::default(),
            background_color,
            iframe_clip,
            slice_flags,
        }
    }

    fn has_too_many_slices(&self) -> bool {
        match self.kind {
            SliceKind::Atomic { .. } => false,
            SliceKind::Default { ref secondary_slices } => secondary_slices.len() > MAX_CACHE_SLICES,
        }
    }

    fn merge(&mut self) {
        self.slice_flags |= SliceFlags::IS_ATOMIC;

        let old = mem::replace(
            &mut self.kind,
            SliceKind::Default { secondary_slices: Vec::new() },
        );

        self.kind = match old {
            SliceKind::Default { mut secondary_slices } => {
                let mut prim_list = PrimitiveList::empty();

                for descriptor in secondary_slices.drain(..) {
                    prim_list.merge(descriptor.prim_list);
                }

                SliceKind::Atomic {
                    prim_list,
                }
            }
            atomic => atomic,
        }
    }
}

/// Used during scene building to construct the list of pending tile caches.
pub struct TileCacheBuilder {
    /// List of tile caches that have been created so far (last in the list is currently active).
    primary_slices: Vec<PrimarySlice>,
    /// Cache the previous scroll root search for a spatial node, since they are often the same.
    prev_scroll_root_cache: (SpatialNodeIndex, SpatialNodeIndex),
    /// Handle to the root reference frame
    root_spatial_node_index: SpatialNodeIndex,
    /// Debug flags to provide to our TileCacheInstances.
    debug_flags: DebugFlags,
}

/// The output of a tile cache builder, containing all details needed to construct the
/// tile cache(s) for the next scene, and retain tiles from the previous frame when sent
/// send to the frame builder.
pub struct TileCacheConfig {
    /// Mapping of slice id to the parameters needed to construct this tile cache.
    pub tile_caches: FastHashMap<SliceId, TileCacheParams>,
    /// Number of picture cache slices that were created (for profiler)
    pub picture_cache_slice_count: usize,
}

impl TileCacheConfig {
    pub fn new(picture_cache_slice_count: usize) -> Self {
        TileCacheConfig {
            tile_caches: FastHashMap::default(),
            picture_cache_slice_count,
        }
    }
}

impl TileCacheBuilder {
    /// Construct a new tile cache builder.
    pub fn new(
        root_spatial_node_index: SpatialNodeIndex,
        background_color: Option<ColorF>,
        debug_flags: DebugFlags,
    ) -> Self {
        TileCacheBuilder {
            primary_slices: vec![PrimarySlice::new(SliceFlags::empty(), None, background_color)],
            prev_scroll_root_cache: (SpatialNodeIndex::INVALID, SpatialNodeIndex::INVALID),
            root_spatial_node_index,
            debug_flags,
        }
    }

    pub fn make_current_slice_atomic(&mut self) {
        self.primary_slices
            .last_mut()
            .unwrap()
            .merge();
    }

    /// Returns true if the current slice has no primitives added yet
    pub fn is_current_slice_empty(&self) -> bool {
        match self.primary_slices.last() {
            Some(slice) => {
                match slice.kind {
                    SliceKind::Default { ref secondary_slices } => {
                        secondary_slices.is_empty()
                    }
                    SliceKind::Atomic { ref prim_list } => {
                        prim_list.is_empty()
                    }
                }
            }
            None => {
                true
            }
        }
    }

    /// Set a barrier that forces a new tile cache next time a prim is added.
    pub fn add_tile_cache_barrier(
        &mut self,
        slice_flags: SliceFlags,
        iframe_clip: Option<ClipId>,
    ) {
        let new_slice = PrimarySlice::new(
            slice_flags,
            iframe_clip,
            None,
        );

        self.primary_slices.push(new_slice);
    }

    /// Create a new tile cache for an existing prim_list
    fn build_tile_cache(
        &mut self,
        prim_list: PrimitiveList,
        spatial_tree: &SceneSpatialTree,
    ) -> Option<SliceDescriptor> {
        if prim_list.is_empty() {
            return None;
        }

        // Iterate the clusters and determine which is the most commonly occurring
        // scroll root. This is a reasonable heuristic to decide which spatial node
        // should be considered the scroll root of this tile cache, in order to
        // minimize the invalidations that occur due to scrolling. It's often the
        // case that a blend container will have only a single scroll root.
        let mut scroll_root_occurrences = FastHashMap::default();

        for cluster in &prim_list.clusters {
            // If we encounter a cluster which has an unknown spatial node,
            // we don't include that in the set of spatial nodes that we
            // are trying to find scroll roots for. Later on, in finalize_picture,
            // the cluster spatial node will be updated to the selected scroll root.
            if cluster.spatial_node_index == SpatialNodeIndex::UNKNOWN {
                continue;
            }

            let scroll_root = find_scroll_root(
                cluster.spatial_node_index,
                &mut self.prev_scroll_root_cache,
                spatial_tree,
                true,
            );

            *scroll_root_occurrences.entry(scroll_root).or_insert(0) += 1;
        }

        // We can't just select the most commonly occurring scroll root in this
        // primitive list. If that is a nested scroll root, there may be
        // primitives in the list that are outside that scroll root, which
        // can cause panics when calculating relative transforms. To ensure
        // this doesn't happen, only retain scroll root candidates that are
        // also ancestors of every other scroll root candidate.
        let scroll_roots: Vec<SpatialNodeIndex> = scroll_root_occurrences
            .keys()
            .cloned()
            .collect();

        scroll_root_occurrences.retain(|parent_spatial_node_index, _| {
            scroll_roots.iter().all(|child_spatial_node_index| {
                parent_spatial_node_index == child_spatial_node_index ||
                spatial_tree.is_ancestor(
                    *parent_spatial_node_index,
                    *child_spatial_node_index,
                )
            })
        });

        // Select the scroll root by finding the most commonly occurring one
        let scroll_root = scroll_root_occurrences
            .iter()
            .max_by_key(|entry | entry.1)
            .map(|(spatial_node_index, _)| *spatial_node_index)
            .unwrap_or(self.root_spatial_node_index);

        Some(SliceDescriptor {
            scroll_root,
            prim_list,
        })
    }

    /// Add a primitive, either to the current tile cache, or a new one, depending on various conditions.
    /// A non-atomic slice picks the tile cache by `placement_node_index`, which
    /// is the primitive's spatial node except for a backdrop-filter chain: that
    /// is in its backdrop root's space, only known once the slice is built, so
    /// it is placed by the backdrop-filter element's spatial node.
    pub fn add_prim(
        &mut self,
        prim_instance: PrimitiveInstance,
        prim_rect: LayoutRect,
        prim_local_clip_rect: LayoutRect,
        spatial_node_index: SpatialNodeIndex,
        placement_node_index: SpatialNodeIndex,
        prim_flags: PrimitiveFlags,
        spatial_tree: &SceneSpatialTree,
        quality_settings: &QualitySettings,
        prim_instances: &mut Vec<PrimitiveInstance>,
        clip_tree_builder: &ClipTreeBuilder,
    ) {
        let primary_slice = self.primary_slices.last_mut().unwrap();

        match primary_slice.kind {
            SliceKind::Atomic { ref mut prim_list } => {
                prim_list.add_prim(
                    prim_instance,
                    prim_rect,
                    prim_local_clip_rect,
                    spatial_node_index,
                    prim_flags,
                    prim_instances,
                );
            }
            SliceKind::Default { ref mut secondary_slices } => {
                assert_ne!(placement_node_index, SpatialNodeIndex::UNKNOWN);

                // Check if we want to create a new slice based on the current / next scroll root
                let scroll_root = find_scroll_root(
                    placement_node_index,
                    &mut self.prev_scroll_root_cache,
                    spatial_tree,
                    // Allow sticky frames as scroll roots, unless our quality settings prefer
                    // subpixel AA over performance.
                    !quality_settings.force_subpixel_aa_where_possible,
                );

                let current_scroll_root = secondary_slices
                    .last()
                    .map(|p| p.scroll_root);

                let mut want_new_tile_cache = secondary_slices.is_empty();

                if let Some(current_scroll_root) = current_scroll_root {
                    want_new_tile_cache |= match (current_scroll_root, scroll_root) {
                        (_, _) if current_scroll_root == self.root_spatial_node_index && scroll_root == self.root_spatial_node_index => {
                            // Both current slice and this cluster are fixed position, no need to cut
                            false
                        }
                        (_, _) if current_scroll_root == self.root_spatial_node_index => {
                            // A scroll root is being established. Give it a cache slice unless
                            // it is a redundant fallback root (no scrollable range, or tiny like
                            // a text input) that is cheaper to keep in the current slice.
                            spatial_tree.is_slice_worthy_scroll_root(scroll_root)
                        }
                        (_, _) if scroll_root == self.root_spatial_node_index => {
                            // If quality settings force subpixel AA over performance, skip creating
                            // a slice for the fixed position element(s) here.
                            if quality_settings.force_subpixel_aa_where_possible {
                                false
                            } else {
                                // A fixed position slice is encountered within a scroll root. Only create
                                // a slice in this case if all the clips referenced by this cluster are also
                                // fixed position. There's no real point in creating slices for these cases,
                                // since we'll have to rasterize them as the scrolling clip moves anyway. It
                                // also allows us to retain subpixel AA in these cases. For these types of
                                // slices, the intra-slice dirty rect handling typically works quite well
                                // (a common case is parallax scrolling effects).
                                let mut create_slice = true;

                                let mut current_node_id = prim_instance.clip_node_id;

                                while current_node_id != ClipNodeId::NONE {
                                    let node = clip_tree_builder.get_node(current_node_id);

                                    let spatial_root = find_scroll_root(
                                        node.spatial_node_index,
                                        &mut self.prev_scroll_root_cache,
                                        spatial_tree,
                                        true,
                                    );

                                    if spatial_root != self.root_spatial_node_index {
                                        create_slice = false;
                                        break;
                                    }

                                    current_node_id = node.parent;
                                }

                                create_slice
                            }
                        }
                        (curr_scroll_root, scroll_root) => {
                            // Two scrolling roots - only need a new slice if they differ
                            curr_scroll_root != scroll_root
                        }
                    };
                }

                if want_new_tile_cache {
                    secondary_slices.push(SliceDescriptor {
                        prim_list: PrimitiveList::empty(),
                        scroll_root,
                    });
                }

                secondary_slices
                    .last_mut()
                    .unwrap()
                    .prim_list
                    .add_prim(
                        prim_instance,
                        prim_rect,
                        prim_local_clip_rect,
                        spatial_node_index,
                        prim_flags,
                        prim_instances,
                    );
            }
        }
    }

    /// Consume this object and build the list of tile cache primitives
    pub fn build(
        mut self,
        prim_store: &mut PrimitiveStore,
        spatial_tree: &SceneSpatialTree,
        prim_instances: &[PrimitiveInstance],
        clip_tree_builder: &mut ClipTreeBuilder,
        clips: &SceneClipStore,
    ) -> (TileCacheConfig, Vec<PictureIndex>) {
        let mut result = TileCacheConfig::new(self.primary_slices.len());
        let mut tile_cache_pictures = Vec::new();
        let primary_slices = std::mem::replace(&mut self.primary_slices, Vec::new());

        for mut primary_slice in primary_slices {

            if primary_slice.has_too_many_slices() {
                primary_slice.merge();
            }

            match primary_slice.kind {
                SliceKind::Atomic { prim_list } => {
                    if let Some(descriptor) = self.build_tile_cache(
                        prim_list,
                        spatial_tree,
                    ) {
                        create_tile_cache(
                            self.debug_flags,
                            primary_slice.slice_flags,
                            descriptor.scroll_root,
                            primary_slice.iframe_clip,
                            descriptor.prim_list,
                            primary_slice.background_color,
                            prim_store,
                            prim_instances,
                            &mut result.tile_caches,
                            &mut tile_cache_pictures,
                            clip_tree_builder,
                            clips,
                            spatial_tree,
                        );
                    }
                }
                SliceKind::Default { secondary_slices } => {
                    for descriptor in secondary_slices {
                        create_tile_cache(
                            self.debug_flags,
                            primary_slice.slice_flags,
                            descriptor.scroll_root,
                            primary_slice.iframe_clip,
                            descriptor.prim_list,
                            primary_slice.background_color,
                            prim_store,
                            prim_instances,
                            &mut result.tile_caches,
                            &mut tile_cache_pictures,
                            clip_tree_builder,
                            clips,
                            spatial_tree,
                        );
                    }
                }
            }
        }

        (result, tile_cache_pictures)
    }
}

/// Find the scroll root for a given spatial node
fn find_scroll_root(
    spatial_node_index: SpatialNodeIndex,
    prev_scroll_root_cache: &mut (SpatialNodeIndex, SpatialNodeIndex),
    spatial_tree: &SceneSpatialTree,
    allow_sticky_frames: bool,
) -> SpatialNodeIndex {
    if prev_scroll_root_cache.0 == spatial_node_index {
        return prev_scroll_root_cache.1;
    }

    let scroll_root = spatial_tree.find_scroll_root(spatial_node_index, allow_sticky_frames);
    *prev_scroll_root_cache = (spatial_node_index, scroll_root);

    scroll_root
}

/// Given a PrimitiveList and scroll root, construct a tile cache primitive instance
/// that wraps the primitive list.
fn create_tile_cache(
    debug_flags: DebugFlags,
    slice_flags: SliceFlags,
    scroll_root: SpatialNodeIndex,
    iframe_clip: Option<ClipId>,
    prim_list: PrimitiveList,
    background_color: Option<ColorF>,
    prim_store: &mut PrimitiveStore,
    prim_instances: &[PrimitiveInstance],
    tile_caches: &mut FastHashMap<SliceId, TileCacheParams>,
    tile_cache_pictures: &mut Vec<PictureIndex>,
    clip_tree_builder: &mut ClipTreeBuilder,
    clips: &SceneClipStore,
    spatial_tree: &SceneSpatialTree,
) {
    // Accumulate any clip instances from the iframe_clip into the shared clips
    // that will be applied by this tile cache during compositing.
    let mut additional_clips = Vec::new();

    if let Some(clip_id) = iframe_clip {
        additional_clips.push(clip_id);
    }

    // Find the best shared clip node that we can apply while compositing tiles,
    // rather than applying to each item individually.

    // Step 1: Walk the primitive list, and find the LCA of the clip-tree that
    //         matches all primitives. This gives us our "best-case" shared
    //         clip node that moves as many clips as possible to compositing.
    let mut shared_clip_node_id = None;

    for cluster in &prim_list.clusters {
        for prim_instance in &prim_instances[cluster.prim_range()] {
            let node_id = prim_instance.clip_node_id;

            // TODO(gw): Need to cache last clip-node id here?
            shared_clip_node_id = match shared_clip_node_id {
                Some(current) => {
                    Some(clip_tree_builder.find_lowest_common_ancestor(current, node_id))
                }
                None => {
                    Some(node_id)
                }
            }
        }
    }

    // Step 2: Now we need to walk up the shared clip node hierarchy, and remove clips
    //         that we can't handle during compositing, such as:
    //         (a) Non axis-aligned clips
    //         (b) Box-shadow or image-mask clips
    //         (c) More than one rounded-rect clip (unless they can be intersected
    //             into a single rounded-rect clip).
    let mut shared_clip_node_id = shared_clip_node_id.unwrap_or(ClipNodeId::NONE);
    let mut current_node_id = shared_clip_node_id;
    let mut rounded_rect_count = 0;

    // Track accumulated rounded rect info so we can attempt to combine
    // multiple rounded rects into a single compositing clip.
    let mut accumulated_rounded_rect: Option<(LayoutRect, BorderRadius)> = None;

    // SNAPTODO: Scene-build slice partitioning reads `node.unsnapped_clip_rect`
    // (and feeds it through `clamped_radius` / `intersect_rounded_rects` /
    // `accumulated_rounded_rect`) to decide whether clips can be promoted
    // into a shared compositing clip. Snapping isn't available at scene-build
    // time, so audit whether pixel-aligned vs. sub-pixel clip rects can flip
    // the can_use_fast_path / intersect decisions once per-frame snapping
    // is real.
    // Walk up the hierarchy to the root of the clip-tree
    while current_node_id != ClipNodeId::NONE {
        let node = clip_tree_builder.get_node(current_node_id);
        let clip_node_data = &clips[node.handle];

        // Check if this clip is in the root coord system (i.e. is axis-aligned with tile-cache)
        let is_rcs = spatial_tree.is_root_coord_system(node.spatial_node_index);

        let node_valid = if is_rcs {
            match clip_node_data.key.kind {
                ClipItemKeyKind::ImageMask(..) |
                ClipItemKeyKind::Rectangle(ClipMode::ClipOut) |
                ClipItemKeyKind::RoundedRectangle(_, _, ClipMode::ClipOut) => {
                    // Has an image-mask or clip-out clip, we can't handle this as a shared clip
                    false
                }
                ClipItemKeyKind::RoundedRectangle(radius, _, ClipMode::Clip) => {
                    // The shader and CoreAnimation rely on certain constraints such
                    // as uniform radii to be able to apply the clip during compositing.
                    let br = clamped_radius(&BorderRadius::from(radius), node.unsnapped_clip_rect.size());
                    if !debug_flags.contains(DebugFlags::DISABLE_COMPOSITOR_CLIPS) &&
                       br.can_use_fast_path_in(&node.unsnapped_clip_rect) {
                        rounded_rect_count += 1;

                        if accumulated_rounded_rect.is_none() {
                            accumulated_rounded_rect = Some((node.unsnapped_clip_rect, br));
                        }

                        true
                    } else {
                        false
                    }
                }
                ClipItemKeyKind::Rectangle(ClipMode::Clip) => {
                    // We can apply multiple (via combining) axis-aligned rectangle
                    // clips to the shared compositing clip.
                    true
                }
            }
        } else {
            // Has a complex transform, we can't handle this as a shared clip
            false
        };

        if node_valid {
            // This node was found to be one we can apply during compositing.
            if rounded_rect_count > 1 {
                // Check if the two rounded rects can be combined. Both clips are in
                // the root coordinate system (is_rcs). The actual intersection with
                // correct spatial transforms is performed in pre_update; here we just
                // verify the clips are geometrically compatible in their local spaces
                // to decide whether to keep both in the shared clip chain.
                let can_combine = match (accumulated_rounded_rect, clip_node_data.key.kind) {
                    (
                        Some((acc_rect, acc_radius)),
                        ClipItemKeyKind::RoundedRectangle(radius, _, ClipMode::Clip),
                    ) => {
                        let radius = clamped_radius(&BorderRadius::from(radius), node.unsnapped_clip_rect.size());
                        intersect_rounded_rects(
                            acc_rect, acc_radius,
                            node.unsnapped_clip_rect, radius,
                        )
                    }
                    _ => None,
                };

                if let Some((combined_rect, combined_radius)) = can_combine {
                    // Successfully combined — keep both clips in the shared
                    // set and update the accumulated state for potential
                    // further combinations.
                    rounded_rect_count = 1;
                    accumulated_rounded_rect = Some((combined_rect, combined_radius));
                } else {
                    // Can't combine, drop children and keep only this clip.
                    shared_clip_node_id = current_node_id;
                    rounded_rect_count = 1;
                    if let ClipItemKeyKind::RoundedRectangle(radius, _, ClipMode::Clip) = clip_node_data.key.kind {
                        let radius = clamped_radius(&BorderRadius::from(radius), node.unsnapped_clip_rect.size());
                        accumulated_rounded_rect = Some((node.unsnapped_clip_rect, radius));
                    }
                }
            }
        } else {
            // Node was invalid, due to transform / clip type. Drop this clip
            // and reset the rounded rect count to 0, since we drop children
            // from here too.
            shared_clip_node_id = node.parent;
            rounded_rect_count = 0;
            accumulated_rounded_rect = None;
        }

        current_node_id = node.parent;
    }

    let tile_clip_node_id = Some(clip_tree_builder.build_for_tile_cache(
        shared_clip_node_id,
        &additional_clips,
    ));

    // Build a clip-chain for the tile cache, that contains any of the shared clips
    // we will apply when drawing the tiles. In all cases provided by Gecko, these
    // are rectangle clips with a scale/offset transform only, and get handled as
    // a simple local clip rect in the vertex shader. However, this should in theory
    // also work with any complex clips, such as rounded rects and image masks, by
    // producing a clip mask that is applied to the picture cache tiles.

    let slice = tile_cache_pictures.len();

    let background_color = if slice == 0 {
        background_color
    } else {
        None
    };

    let slice_id = SliceId::new(slice);

    // Store some information about the picture cache slice. This is used when we swap the
    // new scene into the frame builder to either reuse existing slices, or create new ones.
    tile_caches.insert(slice_id, TileCacheParams {
        debug_flags,
        slice,
        slice_flags,
        spatial_node_index: scroll_root,
        background_color,
        shared_clip_node_id,
        tile_clip_node_id,
        image_surface_count: prim_list.image_surface_count,
        yuv_image_surface_count: prim_list.yuv_image_surface_count,
    });

    let pic_index = prim_store.pictures.alloc().init(PictureInstance::new_image(
        Some(PictureCompositeMode::TileCache { slice_id }),
        Picture3DContext::Out,
        PrimitiveFlags::IS_BACKFACE_VISIBLE,
        prim_list,
        scroll_root,
        RasterSpace::Screen,
        PictureFlags::empty(),
        None,
    ));

    tile_cache_pictures.push(PictureIndex(pic_index as u32));
}

/// Debug information about a set of picture cache slices, exposed via RenderResults
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PictureCacheDebugInfo {
    pub slices: FastHashMap<usize, SliceDebugInfo>,
}

impl PictureCacheDebugInfo {
    pub fn new() -> Self {
        PictureCacheDebugInfo {
            slices: FastHashMap::default(),
        }
    }

    /// Convenience method to retrieve a given slice. Deliberately panics
    /// if the slice isn't present.
    pub fn slice(&self, slice: usize) -> &SliceDebugInfo {
        &self.slices[&slice]
    }
}

impl Default for PictureCacheDebugInfo {
    fn default() -> PictureCacheDebugInfo {
        PictureCacheDebugInfo::new()
    }
}

/// Debug information about the compositor clip applied to a picture cache slice
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CompositorClipDebugInfo {
    pub rect: DeviceRect,
    pub radius: BorderRadius,
}

/// Debug information about a set of picture cache tiles, exposed via RenderResults
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SliceDebugInfo {
    pub tiles: FastHashMap<TileOffset, TileDebugInfo>,
    pub compositor_clip: Option<CompositorClipDebugInfo>,
}

impl SliceDebugInfo {
    pub fn new() -> Self {
        SliceDebugInfo {
            tiles: FastHashMap::default(),
            compositor_clip: None,
        }
    }

    /// Convenience method to retrieve a given tile. Deliberately panics
    /// if the tile isn't present.
    pub fn tile(&self, x: i32, y: i32) -> &TileDebugInfo {
        &self.tiles[&TileOffset::new(x, y)]
    }
}

/// Debug information about a tile that was dirty and was rasterized
#[derive(Debug, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct DirtyTileDebugInfo {
    pub local_valid_rect: PictureRect,
    pub local_dirty_rect: PictureRect,
}

/// Debug information about the state of a tile
#[derive(Debug, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum TileDebugInfo {
    /// Tile was occluded by a tile in front of it
    Occluded,
    /// Tile was culled (not visible in current display port)
    Culled,
    /// Tile was valid (no rasterization was done) and visible
    Valid,
    /// Tile was dirty, and was updated
    Dirty(DirtyTileDebugInfo),
}

impl TileDebugInfo {
    pub fn is_occluded(&self) -> bool {
        match self {
            TileDebugInfo::Occluded => true,
            TileDebugInfo::Culled |
            TileDebugInfo::Valid |
            TileDebugInfo::Dirty(..) => false,
        }
    }

    pub fn is_valid(&self) -> bool {
        match self {
            TileDebugInfo::Valid => true,
            TileDebugInfo::Culled |
            TileDebugInfo::Occluded |
            TileDebugInfo::Dirty(..) => false,
        }
    }

    pub fn is_culled(&self) -> bool {
        match self {
            TileDebugInfo::Culled => true,
            TileDebugInfo::Valid |
            TileDebugInfo::Occluded |
            TileDebugInfo::Dirty(..) => false,
        }
    }

    pub fn as_dirty(&self) -> &DirtyTileDebugInfo {
        match self {
            TileDebugInfo::Occluded |
            TileDebugInfo::Culled |
            TileDebugInfo::Valid => {
                panic!("not a dirty tile!");
            }
            TileDebugInfo::Dirty(ref info) => {
                info
            }
        }
    }
}
