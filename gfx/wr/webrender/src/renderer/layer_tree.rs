/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::ImageRendering;
use api::units::{DeviceIntRect, DeviceIntSize};
use crate::composite::{
    ClipRadius, Compositor, CompositorConfig, CompositorInputConfig, CompositorKind,
    CompositorSurfaceTransform, CompositorSurfaceUsage, NativeSurfaceId, WindowProperties,
};
use std::mem;

const FRAME_COUNT_BEFORE_DISABLING_SYNC_COMMIT: u32 = 60;
const MAX_UNUSED_FRAMES_IN_POOL: usize = 3;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum LayerMode {
    /// WebRender composites into the window's default framebuffer: CompositorConfig::Draw,
    /// or CompositorConfig::Layer with SwapchainMode::WindowFramebuffer.
    Framebuffer,
    /// WebRender composites into OS swapchains owned by the embedder:
    /// CompositorConfig::Layer with SwapchainMode::OsLayers.
    Swapchains,
}

pub fn layer_mode(kind: CompositorKind) -> LayerMode {
    if kind.uses_os_layers() {
        LayerMode::Swapchains
    } else {
        LayerMode::Framebuffer
    }
}

/// Only OS layer compositors are asked for their window properties. The window
/// framebuffer is treated as opaque.
pub fn window_properties(config: &CompositorConfig, kind: CompositorKind) -> WindowProperties {
    match config {
        CompositorConfig::Layer { ref compositor } if kind.uses_os_layers() => compositor.get_window_properties(),
        CompositorConfig::Layer { .. } | CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {
            WindowProperties::default()
        }
    }
}

struct TreeLayer {
    id: NativeSurfaceId,
    size: DeviceIntSize,
    is_opaque: bool,
    frames_since_used: usize,
    usage: CompositorSurfaceUsage,
}

/// Maps the per-frame list of compositor input layers to persistent swapchain
/// and external surfaces owned by the embedder.
pub struct LayerTree {
    next_surface_id: u64,
    surface_pool: Vec<TreeLayer>,
    visual_tree: Vec<TreeLayer>,
    enable_screenshot: bool,
    frames_since_using_multiple_layers: Option<u32>,
}

impl LayerTree {
    pub fn new() -> Self {
        LayerTree {
            next_surface_id: 0,
            surface_pool: Vec::new(),
            visual_tree: Vec::new(),
            enable_screenshot: false,
            frames_since_using_multiple_layers: None,
        }
    }

    fn reuse_same_tree(&mut self, compositor: &mut dyn Compositor, input: &CompositorInputConfig) -> bool {
        if input.layers.len() != self.visual_tree.len() {
            return false;
        }

        for (request, layer) in input.layers.iter().zip(self.visual_tree.iter()) {
            if layer.is_opaque != request.is_opaque || !layer.usage.matches(&request.usage) {
                return false;
            }

            match layer.usage {
                CompositorSurfaceUsage::Content => {
                    if layer.size != request.clip_rect.size() {
                        return false;
                    }
                },
                CompositorSurfaceUsage::External { .. } => {},
                CompositorSurfaceUsage::DebugOverlay => {},
            };
        }

        for (request, layer) in input.layers.iter().zip(self.visual_tree.iter_mut()) {
            layer.frames_since_used = 0;

            // Copy across (potentially) updated external image id
            layer.usage = request.usage;
            match layer.usage {
                CompositorSurfaceUsage::Content | CompositorSurfaceUsage::DebugOverlay => {},
                CompositorSurfaceUsage::External { external_image_id, .. } => {
                    compositor.attach_external_image(layer.id, external_image_id);
                },
            }
        }

        true
    }

    fn uses_multiple_layers_except_debug_layer(input: &CompositorInputConfig) -> bool {
        let is_debug_layer = |usage: &CompositorSurfaceUsage| -> bool {
            match usage {
                CompositorSurfaceUsage::DebugOverlay => true,
                CompositorSurfaceUsage::Content | CompositorSurfaceUsage::External { .. } => false,
            }
        };

        let count = input
            .layers
            .iter()
            .filter(|layer| !is_debug_layer(&layer.usage))
            .count();

        count > 1
    }

    /// Returns true if the tree changed and a full render is required.
    fn begin_swapchain_frame(&mut self, compositor: &mut dyn Compositor, input: &CompositorInputConfig) -> bool {
        let mut destroy_all_layers = false;
        if self.enable_screenshot != input.enable_screenshot {
            if input.enable_screenshot {
                // Screenshot should not use multiple layers.
                assert!(!Self::uses_multiple_layers_except_debug_layer(input));
                // Force to disable requesting sync dcomp commit.
                self.frames_since_using_multiple_layers = None;
            } else {
                assert!(self.frames_since_using_multiple_layers.is_none());
            }
            self.enable_screenshot = input.enable_screenshot;
            destroy_all_layers = true;
        }

        if Self::uses_multiple_layers_except_debug_layer(input) {
            assert!(!self.enable_screenshot);

            if self.frames_since_using_multiple_layers.is_none() {
                destroy_all_layers = true;
            }
            // Use of multiple layers requests sync dcomp commit.
            self.frames_since_using_multiple_layers = Some(0);
        } else {
            match self.frames_since_using_multiple_layers {
                None => {
                    // Do not request sync dcomp commit.
                },
                Some(count) => {
                    if count < FRAME_COUNT_BEFORE_DISABLING_SYNC_COMMIT {
                        // Keep to requet sync dcomp commit to avoid frequent layers creation.
                        self.frames_since_using_multiple_layers = Some(count + 1);
                    } else {
                        destroy_all_layers = true;
                        // Stop to requet sync dcomp commit.
                        self.frames_since_using_multiple_layers = None;
                    }
                },
            }
        };

        // Request sync dcomp commit if multiple layers are using/used.
        let needs_sync_dcomp_commit = self.frames_since_using_multiple_layers.is_some();

        // Discard all layers to recreate them
        if destroy_all_layers {
            let mut layers_to_destroy = Vec::new();
            self.surface_pool.append(&mut self.visual_tree);
            assert!(self.visual_tree.is_empty());
            mem::swap(&mut self.surface_pool, &mut layers_to_destroy);
            for layer in layers_to_destroy {
                compositor.destroy_surface(layer.id);
            }
        }

        compositor.begin_frame();

        let reuse = self.reuse_same_tree(compositor, input);
        if reuse {
            // Do not request full render.
            return false;
        }

        self.surface_pool.append(&mut self.visual_tree);
        assert!(self.visual_tree.is_empty());

        for request in input.layers {
            let size = request.clip_rect.size();

            let existing_index = self
                .surface_pool
                .iter()
                .position(|layer| layer.is_opaque == request.is_opaque && layer.usage.matches(&request.usage));

            let mut layer = match existing_index {
                Some(existing_index) => {
                    let mut layer = self.surface_pool.swap_remove(existing_index);

                    layer.frames_since_used = 0;

                    // Copy across (potentially) updated external image id
                    layer.usage = request.usage;

                    layer
                },
                None => {
                    let id = NativeSurfaceId(self.next_surface_id);
                    self.next_surface_id += 1;

                    match request.usage {
                        CompositorSurfaceUsage::Content | CompositorSurfaceUsage::DebugOverlay => {
                            compositor.create_swapchain_surface(
                                id,
                                size,
                                request.is_opaque,
                                needs_sync_dcomp_commit,
                            );
                        },
                        CompositorSurfaceUsage::External { .. } => {
                            compositor.create_external_surface(id, request.is_opaque);
                        },
                    }

                    TreeLayer {
                        id,
                        size,
                        is_opaque: request.is_opaque,
                        frames_since_used: 0,
                        usage: request.usage,
                    }
                },
            };

            match layer.usage {
                CompositorSurfaceUsage::Content | CompositorSurfaceUsage::DebugOverlay => {
                    if layer.size.width != size.width || layer.size.height != size.height {
                        compositor.resize_swapchain(layer.id, size);
                        layer.size = size;
                    }
                },
                CompositorSurfaceUsage::External { external_image_id, .. } => {
                    compositor.attach_external_image(layer.id, external_image_id);
                },
            }

            self.visual_tree.push(layer);
        }

        for layer in &mut self.surface_pool {
            layer.frames_since_used += 1;
        }

        // Request full render.
        true
    }

    fn bind_swapchain(&mut self, compositor: &mut dyn Compositor, index: usize, dirty_rects: &[DeviceIntRect]) {
        let layer = &self.visual_tree[index];

        compositor.bind_swapchain(layer.id, dirty_rects);
    }

    fn present_swapchain(&mut self, compositor: &mut dyn Compositor, index: usize, dirty_rects: &[DeviceIntRect]) {
        let layer = &self.visual_tree[index];

        compositor.present_swapchain(layer.id, dirty_rects);
    }

    fn add_swapchain_surface(
        &mut self,
        compositor: &mut dyn Compositor,
        index: usize,
        transform: CompositorSurfaceTransform,
        clip_rect: DeviceIntRect,
        image_rendering: ImageRendering,
        rounded_clip_rect: DeviceIntRect,
        rounded_clip_radii: ClipRadius,
    ) {
        let layer = &self.visual_tree[index];

        compositor.add_surface(
            layer.id,
            transform,
            clip_rect,
            image_rendering,
            rounded_clip_rect,
            rounded_clip_radii,
        );
    }

    fn end_swapchain_frame(&mut self, compositor: &mut dyn Compositor) {
        compositor.end_frame();

        // Destroy any unused surface pool entries
        let mut layers_to_destroy = Vec::new();

        self.surface_pool.retain(|layer| {
            let keep = layer.frames_since_used < MAX_UNUSED_FRAMES_IN_POOL;

            if !keep {
                layers_to_destroy.push(layer.id);
            }

            keep
        });

        for layer_id in layers_to_destroy {
            compositor.destroy_surface(layer_id);
        }
    }

    fn destroy_all_swapchains(&mut self, compositor: &mut dyn Compositor) {
        for layer in self.surface_pool.iter().chain(self.visual_tree.iter()) {
            compositor.destroy_surface(layer.id);
        }

        self.surface_pool.clear();
        self.visual_tree.clear();
    }

    pub fn begin_frame(&mut self, config: &mut CompositorConfig, input: &CompositorInputConfig) -> bool {
        match config {
            CompositorConfig::Layer { ref mut compositor } => self.begin_swapchain_frame(&mut **compositor, input),
            CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => false,
        }
    }

    pub fn bind_layer(&mut self, config: &mut CompositorConfig, index: usize, dirty_rects: &[DeviceIntRect]) {
        match config {
            CompositorConfig::Layer { ref mut compositor } => self.bind_swapchain(&mut **compositor, index, dirty_rects),
            CompositorConfig::Draw { partial_present: Some(ref mut partial_present), .. } => {
                // eglSetDamageRegion must be called exactly once per frame, immediately
                // before rendering to the main framebuffer (bug 1685276).
                if !dirty_rects.is_empty() {
                    partial_present.set_buffer_damage_region(dirty_rects);
                }
            }
            CompositorConfig::Draw { partial_present: None, .. } | CompositorConfig::Native { .. } => {}
        }
    }

    pub fn present_layer(&mut self, config: &mut CompositorConfig, index: usize, dirty_rects: &[DeviceIntRect]) {
        match config {
            CompositorConfig::Layer { ref mut compositor } => self.present_swapchain(&mut **compositor, index, dirty_rects),
            CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {}
        }
    }

    pub fn add_surface(
        &mut self,
        config: &mut CompositorConfig,
        index: usize,
        transform: CompositorSurfaceTransform,
        clip_rect: DeviceIntRect,
        image_rendering: ImageRendering,
        rounded_clip_rect: DeviceIntRect,
        rounded_clip_radii: ClipRadius,
    ) {
        match config {
            CompositorConfig::Layer { ref mut compositor } => self.add_swapchain_surface(
                &mut **compositor,
                index,
                transform,
                clip_rect,
                image_rendering,
                rounded_clip_rect,
                rounded_clip_radii,
            ),
            CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {}
        }
    }

    pub fn end_frame(&mut self, config: &mut CompositorConfig) {
        match config {
            CompositorConfig::Layer { ref mut compositor } => self.end_swapchain_frame(&mut **compositor),
            CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {}
        }
    }

    pub fn deinit(&mut self, config: &mut CompositorConfig) {
        match config {
            CompositorConfig::Layer { ref mut compositor } => self.destroy_all_swapchains(&mut **compositor),
            CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use api::units::DeviceIntPoint;
    use api::{ExternalImageId, IdNamespace, ImageKey};
    use crate::composite::{
        CompositorCapabilities, CompositorInputLayer, CompositorTransformIndex, NativeSurfaceInfo,
        NativeTileId, WindowVisibility,
    };

    #[derive(Debug, Clone, PartialEq)]
    enum Call {
        BeginFrame,
        EndFrame,
        CreateSwapchain { id: u64, size: DeviceIntSize, is_opaque: bool, sync: bool },
        ResizeSwapchain { id: u64, size: DeviceIntSize },
        CreateExternal { id: u64, is_opaque: bool },
        Attach { id: u64, image: u64 },
        Destroy { id: u64 },
        BindSwapchain { id: u64 },
        PresentSwapchain { id: u64 },
        AddSurface { id: u64 },
    }

    struct MockCompositor {
        calls: Vec<Call>,
    }

    impl MockCompositor {
        fn new() -> Self {
            MockCompositor { calls: Vec::new() }
        }
    }

    impl Compositor for MockCompositor {
        fn create_surface(
            &mut self,
            _id: NativeSurfaceId,
            _tile_size: DeviceIntSize,
            _is_opaque: bool,
        ) {
            panic!()
        }

        fn create_external_surface(&mut self, id: NativeSurfaceId, is_opaque: bool) {
            self.calls.push(Call::CreateExternal { id: id.0, is_opaque });
        }

        fn create_backdrop_surface(&mut self, _id: NativeSurfaceId, _color: api::ColorF) {
            panic!()
        }

        fn destroy_surface(&mut self, id: NativeSurfaceId) {
            self.calls.push(Call::Destroy { id: id.0 });
        }

        fn create_tile(&mut self, _id: NativeTileId) {
            panic!()
        }

        fn destroy_tile(&mut self, _id: NativeTileId) {
            panic!()
        }

        fn attach_external_image(&mut self, id: NativeSurfaceId, external_image: ExternalImageId) {
            self.calls.push(Call::Attach { id: id.0, image: external_image.0 });
        }

        fn bind(
            &mut self,
            _id: NativeTileId,
            _dirty_rect: DeviceIntRect,
            _valid_rect: DeviceIntRect,
        ) -> NativeSurfaceInfo {
            panic!()
        }

        fn unbind(&mut self) {
            panic!()
        }

        fn begin_frame(&mut self) {
            self.calls.push(Call::BeginFrame);
        }

        fn add_surface(
            &mut self,
            id: NativeSurfaceId,
            _transform: CompositorSurfaceTransform,
            _clip_rect: DeviceIntRect,
            _image_rendering: ImageRendering,
            _rounded_clip_rect: DeviceIntRect,
            _rounded_clip_radii: ClipRadius,
        ) {
            self.calls.push(Call::AddSurface { id: id.0 });
        }

        fn end_frame(&mut self) {
            self.calls.push(Call::EndFrame);
        }

        fn deinit(&mut self) {}

        fn get_capabilities(&self) -> CompositorCapabilities {
            Default::default()
        }

        fn get_window_visibility(&self) -> WindowVisibility {
            Default::default()
        }

        fn create_swapchain_surface(
            &mut self,
            id: NativeSurfaceId,
            size: DeviceIntSize,
            is_opaque: bool,
            needs_sync_commit: bool,
        ) {
            self.calls.push(Call::CreateSwapchain {
                id: id.0,
                size,
                is_opaque,
                sync: needs_sync_commit,
            });
        }

        fn resize_swapchain(&mut self, id: NativeSurfaceId, size: DeviceIntSize) {
            self.calls.push(Call::ResizeSwapchain { id: id.0, size });
        }

        fn bind_swapchain(&mut self, id: NativeSurfaceId, _dirty_rects: &[DeviceIntRect]) {
            self.calls.push(Call::BindSwapchain { id: id.0 });
        }

        fn present_swapchain(&mut self, id: NativeSurfaceId, _dirty_rects: &[DeviceIntRect]) {
            self.calls.push(Call::PresentSwapchain { id: id.0 });
        }
    }

    fn content_layer(width: i32, height: i32) -> CompositorInputLayer {
        let clip_rect = DeviceIntRect::from_origin_and_size(
            DeviceIntPoint::zero(),
            DeviceIntSize::new(width, height),
        );

        CompositorInputLayer {
            usage: CompositorSurfaceUsage::Content,
            is_opaque: false,
            offset: DeviceIntPoint::zero(),
            clip_rect,
            rounded_clip_rect: clip_rect,
            rounded_clip_radii: ClipRadius::EMPTY,
        }
    }

    fn external_layer(image_key: u32, image_id: u64) -> CompositorInputLayer {
        let clip_rect = DeviceIntRect::from_origin_and_size(
            DeviceIntPoint::zero(),
            DeviceIntSize::new(100, 100),
        );

        CompositorInputLayer {
            usage: CompositorSurfaceUsage::External {
                image_key: ImageKey::new(IdNamespace(0), image_key),
                external_image_id: ExternalImageId(image_id),
                transform_index: CompositorTransformIndex::INVALID,
            },
            is_opaque: false,
            offset: DeviceIntPoint::zero(),
            clip_rect,
            rounded_clip_rect: clip_rect,
            rounded_clip_radii: ClipRadius::EMPTY,
        }
    }

    fn debug_layer() -> CompositorInputLayer {
        let clip_rect = DeviceIntRect::from_origin_and_size(
            DeviceIntPoint::zero(),
            DeviceIntSize::new(100, 100),
        );

        CompositorInputLayer {
            usage: CompositorSurfaceUsage::DebugOverlay,
            is_opaque: false,
            offset: DeviceIntPoint::zero(),
            clip_rect,
            rounded_clip_rect: clip_rect,
            rounded_clip_radii: ClipRadius::EMPTY,
        }
    }

    fn input(layers: &[CompositorInputLayer], enable_screenshot: bool) -> CompositorInputConfig<'_> {
        CompositorInputConfig {
            enable_screenshot,
            layers,
        }
    }

    fn begin(
        tree: &mut LayerTree,
        compositor: &mut MockCompositor,
        layers: &[CompositorInputLayer],
    ) -> bool {
        let config = input(layers, false);
        tree.begin_swapchain_frame(compositor, &config)
    }

    #[test]
    fn first_frame_creates_swapchain() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        assert_eq!(
            compositor.calls,
            vec![
                Call::BeginFrame,
                Call::CreateSwapchain {
                    id: 0,
                    size: DeviceIntSize::new(100, 100),
                    is_opaque: false,
                    sync: false,
                },
            ]
        );
    }

    #[test]
    fn identical_frame_reuses_tree() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        compositor.calls.clear();
        assert!(!begin(&mut tree, &mut compositor, &layers));
        assert_eq!(compositor.calls, vec![Call::BeginFrame]);
    }

    #[test]
    fn resize_reuses_pooled_swapchain() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        compositor.calls.clear();
        let layers = [content_layer(200, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        assert_eq!(
            compositor.calls,
            vec![
                Call::BeginFrame,
                Call::ResizeSwapchain {
                    id: 0,
                    size: DeviceIntSize::new(200, 100),
                },
            ]
        );
    }

    #[test]
    fn external_image_is_reattached() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [external_layer(1, 10)];
        assert!(begin(&mut tree, &mut compositor, &layers));
        assert_eq!(
            compositor.calls,
            vec![
                Call::BeginFrame,
                Call::CreateExternal { id: 0, is_opaque: false },
                Call::Attach { id: 0, image: 10 },
            ]
        );

        compositor.calls.clear();
        let layers = [external_layer(1, 11)];
        assert!(!begin(&mut tree, &mut compositor, &layers));
        assert_eq!(
            compositor.calls,
            vec![
                Call::BeginFrame,
                Call::Attach { id: 0, image: 11 },
            ]
        );
    }

    #[test]
    fn multiple_layers_enable_sync_commit() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100), external_layer(1, 10), content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        let sync = compositor
            .calls
            .iter()
            .filter_map(|call| match call {
                Call::CreateSwapchain { sync, .. } => Some(*sync),
                _ => None,
            })
            .collect::<Vec<_>>();

        assert_eq!(sync, vec![true, true]);
    }

    #[test]
    fn sync_commit_hysteresis() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100), external_layer(1, 10)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        // The first single-layer frame after a multi-layer one keeps requesting
        // sync commits, so none of these frames destroy and recreate layers.
        for _ in 0 .. FRAME_COUNT_BEFORE_DISABLING_SYNC_COMMIT {
            compositor.calls.clear();
            let layers = [content_layer(100, 100)];
            begin(&mut tree, &mut compositor, &layers);

            assert!(
                !compositor.calls.iter().any(|call| matches!(call, Call::Destroy { .. })),
                "unexpected destroy before the hysteresis expired",
            );
        }

        compositor.calls.clear();
        let layers = [content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        // Every layer is destroyed before the new frame begins, in the tree and
        // in the pool. The relative order of the two destroys is unspecified.
        let mut calls = compositor.calls.clone();
        let tail = calls.split_off(2);
        calls.sort_by_key(|call| format!("{:?}", call));

        assert_eq!(
            calls,
            vec![Call::Destroy { id: 0 }, Call::Destroy { id: 1 }],
        );
        assert_eq!(
            tail,
            vec![
                Call::BeginFrame,
                Call::CreateSwapchain {
                    id: 2,
                    size: DeviceIntSize::new(100, 100),
                    is_opaque: false,
                    sync: false,
                },
            ]
        );
    }

    #[test]
    fn unused_pool_entries_expire() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100)];
        begin(&mut tree, &mut compositor, &layers);
        tree.end_swapchain_frame(&mut compositor);

        let layers = [external_layer(1, 10)];
        begin(&mut tree, &mut compositor, &layers);
        tree.end_swapchain_frame(&mut compositor);

        let layers = [external_layer(2, 20)];
        begin(&mut tree, &mut compositor, &layers);
        tree.end_swapchain_frame(&mut compositor);

        compositor.calls.clear();
        let layers = [external_layer(3, 30)];
        begin(&mut tree, &mut compositor, &layers);
        tree.end_swapchain_frame(&mut compositor);

        assert!(
            compositor.calls.contains(&Call::Destroy { id: 0 }),
            "the unused content swapchain should expire",
        );
    }

    #[test]
    fn screenshot_toggle_recreates_layers() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        compositor.calls.clear();
        let layers = [content_layer(100, 100)];
        let config = input(&layers, true);
        assert!(tree.begin_swapchain_frame(&mut compositor, &config));

        assert_eq!(
            compositor.calls,
            vec![
                Call::Destroy { id: 0 },
                Call::BeginFrame,
                Call::CreateSwapchain {
                    id: 1,
                    size: DeviceIntSize::new(100, 100),
                    is_opaque: false,
                    sync: false,
                },
            ]
        );
    }

    #[test]
    #[should_panic]
    fn screenshot_with_multiple_layers_panics() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100), external_layer(1, 10)];
        let config = input(&layers, true);
        tree.begin_swapchain_frame(&mut compositor, &config);
    }

    #[test]
    fn bind_present_add_use_surface_ids() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [debug_layer(), content_layer(100, 100)];
        assert!(begin(&mut tree, &mut compositor, &layers));

        compositor.calls.clear();
        tree.bind_swapchain(&mut compositor, 1, &[]);
        tree.present_swapchain(&mut compositor, 1, &[]);
        tree.add_swapchain_surface(
            &mut compositor,
            0,
            CompositorSurfaceTransform::identity(),
            DeviceIntRect::from_size(DeviceIntSize::new(100, 100)),
            ImageRendering::Auto,
            DeviceIntRect::from_size(DeviceIntSize::new(100, 100)),
            ClipRadius::EMPTY,
        );

        assert_eq!(
            compositor.calls,
            vec![
                Call::BindSwapchain { id: 1 },
                Call::PresentSwapchain { id: 1 },
                Call::AddSurface { id: 0 },
            ]
        );
    }

    #[test]
    fn destroy_all_swapchains_destroys_pool_and_tree() {
        let mut tree = LayerTree::new();
        let mut compositor = MockCompositor::new();

        let layers = [content_layer(100, 100)];
        begin(&mut tree, &mut compositor, &layers);

        let layers = [external_layer(1, 10)];
        begin(&mut tree, &mut compositor, &layers);

        compositor.calls.clear();
        tree.destroy_all_swapchains(&mut compositor);

        assert!(compositor.calls.contains(&Call::Destroy { id: 0 }));
        assert!(compositor.calls.contains(&Call::Destroy { id: 1 }));
        assert_eq!(compositor.calls.len(), 2);
        assert!(tree.surface_pool.is_empty());
        assert!(tree.visual_tree.is_empty());
    }
}

