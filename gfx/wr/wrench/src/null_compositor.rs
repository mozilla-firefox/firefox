/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use webrender::api::{ColorF, ExternalImageId, ImageRendering};
use webrender::api::units::{DeviceIntRect, DeviceIntSize};
use webrender::{
    ClipRadius, Compositor, CompositorCapabilities, CompositorSurfaceTransform, NativeSurfaceId,
    NativeSurfaceInfo, NativeTileId, WindowProperties, WindowVisibility,
};

/// A layer compositor that draws every layer to the window and drops
/// compositor surfaces. It lets platforms without an OS compositor
/// integration run frame building with `CompositorKind::Layer`, so only
/// scenes without promoted surfaces render correctly. Layers are drawn with
/// a top-left origin, so the output is vertically flipped.
pub struct NullLayerCompositor;

impl Compositor for NullLayerCompositor {
    fn create_surface(&mut self, _id: NativeSurfaceId, _tile_size: DeviceIntSize, _is_opaque: bool) {}

    fn create_external_surface(&mut self, _id: NativeSurfaceId, _is_opaque: bool) {}

    fn create_backdrop_surface(&mut self, _id: NativeSurfaceId, _color: ColorF) {}

    fn destroy_surface(&mut self, _id: NativeSurfaceId) {}

    fn create_tile(&mut self, _id: NativeTileId) {}

    fn destroy_tile(&mut self, _id: NativeTileId) {}

    fn attach_external_image(&mut self, _id: NativeSurfaceId, _external_image: ExternalImageId) {}

    fn bind(
        &mut self,
        _id: NativeTileId,
        _dirty_rect: DeviceIntRect,
        _valid_rect: DeviceIntRect,
    ) -> NativeSurfaceInfo {
        unreachable!("native tiles are not used with the Layer configuration");
    }

    fn unbind(&mut self) {}

    fn begin_frame(&mut self) {}

    fn add_surface(
        &mut self,
        _id: NativeSurfaceId,
        _transform: CompositorSurfaceTransform,
        _clip_rect: DeviceIntRect,
        _image_rendering: ImageRendering,
        _rounded_clip_rect: DeviceIntRect,
        _rounded_clip_radii: ClipRadius,
    ) {}

    fn end_frame(&mut self) {}

    fn deinit(&mut self) {}

    fn get_capabilities(&self) -> CompositorCapabilities {
        Default::default()
    }

    fn get_window_visibility(&self) -> WindowVisibility {
        Default::default()
    }

    fn create_swapchain_surface(
        &mut self,
        _id: NativeSurfaceId,
        _size: DeviceIntSize,
        _is_opaque: bool,
        _needs_sync_commit: bool,
    ) {}

    fn resize_swapchain(&mut self, _id: NativeSurfaceId, _size: DeviceIntSize) {}

    fn bind_swapchain(&mut self, _id: NativeSurfaceId, _dirty_rects: &[DeviceIntRect]) {}

    fn present_swapchain(&mut self, _id: NativeSurfaceId, _dirty_rects: &[DeviceIntRect]) {}

    fn get_window_properties(&self) -> WindowProperties {
        WindowProperties {
            is_opaque: true,
            enable_screenshot: false,
        }
    }
}

/// The inner compositor of a `SwCompositor` that composites into the window
/// itself. It only clears the window before the tiles are composited. Tiles
/// are composited with a top-left origin, so the output is vertically flipped.
#[cfg(feature = "software")]
pub struct NullMappableCompositor {
    pub gl: swgl::Context,
}

#[cfg(feature = "software")]
impl Compositor for NullMappableCompositor {
    fn create_surface(&mut self, _id: NativeSurfaceId, _tile_size: DeviceIntSize, _is_opaque: bool) {}
    fn create_external_surface(&mut self, _id: NativeSurfaceId, _is_opaque: bool) {}
    fn create_backdrop_surface(&mut self, _id: NativeSurfaceId, _color: ColorF) {}
    fn destroy_surface(&mut self, _id: NativeSurfaceId) {}
    fn create_tile(&mut self, _id: NativeTileId) {}
    fn destroy_tile(&mut self, _id: NativeTileId) {}
    fn attach_external_image(&mut self, _id: NativeSurfaceId, _external_image: ExternalImageId) {}
    fn bind(
        &mut self,
        _id: NativeTileId,
        _dirty_rect: DeviceIntRect,
        _valid_rect: DeviceIntRect,
    ) -> NativeSurfaceInfo {
        unreachable!("SwCompositor binds its own tiles");
    }
    fn unbind(&mut self) {}
    fn begin_frame(&mut self) {}
    fn add_surface(
        &mut self,
        _id: NativeSurfaceId,
        _transform: CompositorSurfaceTransform,
        _clip_rect: DeviceIntRect,
        _image_rendering: ImageRendering,
        _rounded_clip_rect: DeviceIntRect,
        _rounded_clip_radii: ClipRadius,
    ) {}
    fn start_compositing(
        &mut self,
        clear_color: ColorF,
        _dirty_rects: &[DeviceIntRect],
        _opaque_rects: &[DeviceIntRect],
    ) {
        let (_, width, height, _) = self.gl.get_color_buffer(0, false);
        self.gl.clear_color_rect(
            0, 0, 0, width, height,
            clear_color.r, clear_color.g, clear_color.b, clear_color.a,
        );
    }
    fn end_frame(&mut self) {}
    fn deinit(&mut self) {}
    fn get_capabilities(&self) -> CompositorCapabilities {
        Default::default()
    }
    fn get_window_visibility(&self) -> WindowVisibility {
        Default::default()
    }
}

#[cfg(feature = "software")]
impl webrender::MappableCompositor for NullMappableCompositor {
    fn map_tile(
        &mut self,
        _id: NativeTileId,
        _dirty_rect: DeviceIntRect,
        _valid_rect: DeviceIntRect,
    ) -> Option<webrender::MappedTileInfo> {
        None
    }
    fn unmap_tile(&mut self) {}
    fn lock_composite_surface(
        &mut self,
        _ctx: *mut std::os::raw::c_void,
        _external_image_id: ExternalImageId,
        _composite_info: *mut webrender::SWGLCompositeSurfaceInfo,
    ) -> bool {
        false
    }
    fn unlock_composite_surface(&mut self, _ctx: *mut std::os::raw::c_void, _external_image_id: ExternalImageId) {}
}
