/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::AU_PER_DEV_PX;
use euclid::SideOffsets2D;
use gleam::gl;
use image::GenericImageView;
use crate::parse_function::parse_function;
use crate::premultiply::premultiply;
use std::collections::HashMap;
use std::convert::TryInto;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::usize;
use webrender::api::*;
use webrender::render_api::*;
use webrender::api::units::*;
use webrender::api::FillRule;
use crate::wrench::{FontDescriptor, FontInstanceDescriptor, Wrench, WrenchThing, DisplayList};
use crate::yaml_helper::{StringEnum, YamlHelper, make_perspective};
use yaml_rust::{Yaml, YamlLoader};
use crate::PLATFORM_DEFAULT_FACE_NAME;

macro_rules! try_intersect {
    ($first: expr, $second: expr) => {
        if let Some(rect) = ($first).intersection($second) {
            rect
        } else {
            warn!("skipping item with non-intersecting bounds and clip_rect");
            return;
        }
    }
}

fn rsrc_path(item: &Yaml, aux_dir: &Path) -> PathBuf {
    let filename = item.as_str().unwrap();
    let mut file = aux_dir.to_path_buf();
    file.push(filename);
    file
}

impl FontDescriptor {
    fn from_yaml(item: &Yaml, aux_dir: &Path) -> FontDescriptor {
        if !item["family"].is_badvalue() {
            FontDescriptor::Properties {
                family: item["family"].as_str().unwrap().to_owned(),
                weight: item["weight"].as_i64().unwrap_or(400) as u32,
                style: item["style"].as_i64().unwrap_or(0) as u32,
                stretch: item["stretch"].as_i64().unwrap_or(5) as u32,
            }
        } else if !item["font"].is_badvalue() {
            let path = rsrc_path(&item["font"], aux_dir);
            FontDescriptor::Path {
                path,
                font_index: item["font-index"].as_i64().unwrap_or(0) as u32,
            }
        } else {
            FontDescriptor::Family {
                name: PLATFORM_DEFAULT_FACE_NAME.to_string(),
            }
        }
    }
}

struct LocalExternalImageHandler {
    texture_ids: Vec<(gl::GLuint, ImageDescriptor)>,
}

impl LocalExternalImageHandler {
    pub fn new() -> LocalExternalImageHandler {
        LocalExternalImageHandler {
            texture_ids: Vec::new(),
        }
    }

    fn init_gl_texture(
        id: gl::GLuint,
        gl_target: gl::GLuint,
        format_desc: webrender::FormatDesc,
        width: gl::GLint,
        height: gl::GLint,
        bytes: &[u8],
        gl: &dyn gl::Gl,
    ) {
        gl.bind_texture(gl_target, id);
        gl.tex_parameter_i(gl_target, gl::TEXTURE_MAG_FILTER, gl::LINEAR as gl::GLint);
        gl.tex_parameter_i(gl_target, gl::TEXTURE_MIN_FILTER, gl::LINEAR as gl::GLint);
        gl.tex_parameter_i(gl_target, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE as gl::GLint);
        gl.tex_parameter_i(gl_target, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE as gl::GLint);
        gl.tex_image_2d(
            gl_target,
            0,
            format_desc.internal as gl::GLint,
            width,
            height,
            0,
            format_desc.external,
            format_desc.pixel_type,
            Some(bytes),
        );
        gl.bind_texture(gl_target, 0);
    }

    pub fn add_image(&mut self,
        device: &webrender::Device,
        desc: ImageDescriptor,
        target: ImageBufferKind,
        image_data: ImageData,
    ) -> ImageData {
        let (image_id, channel_idx) = match image_data {
            ImageData::Raw(ref data) => {
                let gl = device.gl();
                let texture_ids = gl.gen_textures(1);
                let format_desc = if desc.format == ImageFormat::BGRA8 {
                    // Force BGRA8 data to RGBA8 layout to avoid potential
                    // need for usage of texture-swizzle.
                    webrender::FormatDesc {
                        external: gl::BGRA,
                        .. device.gl_describe_format(ImageFormat::RGBA8)
                    }
                } else {
                    device.gl_describe_format(desc.format)
                };

                LocalExternalImageHandler::init_gl_texture(
                    texture_ids[0],
                    webrender::get_gl_target(target),
                    format_desc,
                    desc.size.width as gl::GLint,
                    desc.size.height as gl::GLint,
                    data,
                    gl,
                );
                self.texture_ids.push((texture_ids[0], desc));
                (ExternalImageId((self.texture_ids.len() - 1) as u64), 0)
            },
            _ => panic!("unsupported!"),
        };

        ImageData::External(
            ExternalImageData {
                id: image_id,
                channel_index: channel_idx,
                image_type: ExternalImageType::TextureHandle(target),
                normalized_uvs: false,
            }
        )
    }
}

impl ExternalImageHandler for LocalExternalImageHandler {
    fn lock(
        &mut self,
        key: ExternalImageId,
        _channel_index: u8,
        _is_composited: bool,
    ) -> ExternalImage<'_> {
        let (id, desc) = self.texture_ids[key.0 as usize];
        ExternalImage {
            uv: TexelRect::new(0.0, 0.0, desc.size.width as f32, desc.size.height as f32),
            source: ExternalImageSource::NativeTexture(id),
        }
    }
    fn unlock(&mut self, _key: ExternalImageId, _channel_index: u8) {}
}

fn broadcast<T: Clone>(base_vals: &[T], num_items: usize) -> Vec<T> {
    if base_vals.len() == num_items {
        return base_vals.to_vec();
    }

    assert_eq!(
        num_items % base_vals.len(),
        0,
        "Cannot broadcast {} elements into {}",
        base_vals.len(),
        num_items
    );

    let mut vals = vec![];
    loop {
        if vals.len() == num_items {
            break;
        }
        vals.extend_from_slice(base_vals);
    }
    vals
}

enum CheckerboardKind {
    BlackGrey,
    BlackTransparent,
}

fn generate_checkerboard_image(
    border: u32,
    tile_x_size: u32,
    tile_y_size: u32,
    tile_x_count: u32,
    tile_y_count: u32,
    kind: CheckerboardKind,
) -> (ImageDescriptor, ImageData) {
    let width = 2 * border + tile_x_size * tile_x_count;
    let height = 2 * border + tile_y_size * tile_y_count;
    let mut pixels = Vec::new();

    for y in 0 .. height {
        for x in 0 .. width {
            if y < border || y >= (height - border) ||
               x < border || x >= (width - border) {
                pixels.push(0);
                pixels.push(0);
                pixels.push(0xff);
                pixels.push(0xff);
            } else {
                let xon = ((x - border) % (2 * tile_x_size)) < tile_x_size;
                let yon = ((y - border) % (2 * tile_y_size)) < tile_y_size;
                match kind {
                    CheckerboardKind::BlackGrey => {
                        let value = if xon ^ yon { 0xff } else { 0x7f };
                        pixels.push(value);
                        pixels.push(value);
                        pixels.push(value);
                        pixels.push(0xff);
                    }
                    CheckerboardKind::BlackTransparent => {
                        let value = if xon ^ yon { 0xff } else { 0x00 };
                        pixels.push(value);
                        pixels.push(value);
                        pixels.push(value);
                        pixels.push(value);
                    }
                }
            }
        }
    }

    let flags = match kind {
        CheckerboardKind::BlackGrey => ImageDescriptorFlags::IS_OPAQUE,
        CheckerboardKind::BlackTransparent => ImageDescriptorFlags::empty(),
    };

    (
        ImageDescriptor::new(width as i32, height as i32, ImageFormat::BGRA8, flags),
        ImageData::new(pixels),
    )
}

fn generate_xy_gradient_image(w: u32, h: u32) -> (ImageDescriptor, ImageData) {
    let mut pixels = Vec::with_capacity((w * h * 4) as usize);
    for y in 0 .. h {
        for x in 0 .. w {
            let grid = if x % 100 < 3 || y % 100 < 3 { 0.9 } else { 1.0 };
            pixels.push((y as f32 / h as f32 * 255.0 * grid) as u8);
            pixels.push(0);
            pixels.push((x as f32 / w as f32 * 255.0 * grid) as u8);
            pixels.push(255);
        }
    }

    (
        ImageDescriptor::new(w as i32, h as i32, ImageFormat::BGRA8, ImageDescriptorFlags::IS_OPAQUE),
        ImageData::new(pixels),
    )
}

fn generate_solid_color_image(
    r: u8,
    g: u8,
    b: u8,
    a: u8,
    w: u32,
    h: u32,
) -> (ImageDescriptor, ImageData) {
    let num_pixels: usize = (w * h).try_into().unwrap();
    let pixels = [b, g, r, a].repeat(num_pixels);

    let mut flags = ImageDescriptorFlags::empty();
    if a == 255 {
        flags |= ImageDescriptorFlags::IS_OPAQUE;
    }

    (
        ImageDescriptor::new(w as i32, h as i32, ImageFormat::BGRA8, flags),
        ImageData::new(pixels),
    )
}



fn is_image_opaque(format: ImageFormat, bytes: &[u8]) -> bool {
    match format {
        ImageFormat::BGRA8 |
        ImageFormat::RGBA8 => {
            let mut is_opaque = true;
            for i in 0 .. (bytes.len() / 4) {
                if bytes[i * 4 + 3] != 255 {
                    is_opaque = false;
                    break;
                }
            }
            is_opaque
        }
        ImageFormat::RG8 => true,
        ImageFormat::RG16 => true,
        ImageFormat::R8 => false,
        ImageFormat::R16 => false,
        ImageFormat::RGBAF32 |
        ImageFormat::RGBAI32 => unreachable!(),
    }
}

struct IsRoot(bool);

pub struct Snapshot {
    key: SnapshotImageKey,
    size: LayoutSize,
}

pub struct YamlFrameReader {
    yaml_path: PathBuf,
    aux_dir: PathBuf,
    frame_count: u32,

    display_lists: Vec<DisplayList>,

    watch_source: bool,
    list_resources: bool,

    /// A HashMap of offsets which specify what scroll offsets particular
    /// scroll layers should be initialized with.
    scroll_offsets: HashMap<ExternalScrollId, Vec<SampledScrollOffset>>,
    next_external_scroll_id: u64,

    /// Dynamic transform property values sent with the frame (top-level
    /// `transform-properties` yaml key). Lets a `transform-binding` reference
    /// frame's value change across frames, which is what drives the animating
    /// (has-moved) latch used for text raster space.
    transform_properties: Vec<PropertyValue<LayoutTransform>>,

    image_map: HashMap<(PathBuf, Option<i64>), (ImageKey, LayoutSize)>,

    font_render_mode: Option<FontRenderMode>,
    snapshots: HashMap<String, Snapshot>,
    allow_mipmaps: bool,

    /// Device pixel scale applied to the root pipeline as a scale reference
    /// frame, so that reftests can exercise rendering at different device pixel
    /// ratios (see the `scale(...)` reftest option).
    device_pixel_scale: f32,

    /// When a device pixel scale is emulated, the pipeline's implicit root
    /// reference frame and root scroll node are above the scale reference frame,
    /// so yaml references to them are redirected to scaled equivalents and the
    /// root scroll offset (applied above the scale) is scaled by hand.
    dppx_root_reference_frame: Option<SpatialId>,
    dppx_root_scroll_node: Option<SpatialId>,

    /// A HashMap that allows specifying a numeric id for clip and clip chains in YAML
    /// and having each of those ids correspond to a unique ClipId.
    user_clip_id_map: HashMap<u64, ClipId>,
    user_clipchain_id_map: HashMap<u64, ClipChainId>,
    user_spatial_id_map: HashMap<u64, SpatialId>,

    spatial_id_stack: Vec<SpatialId>,

    requested_frame: usize,
    built_frame: usize,

    yaml_string: String,
    keyframes: Option<Yaml>,

    external_image_handler: Option<Box<LocalExternalImageHandler>>,
}

impl YamlFrameReader {
    pub fn new(yaml_path: &Path) -> YamlFrameReader {
        YamlFrameReader {
            watch_source: false,
            list_resources: false,
            yaml_path: yaml_path.to_owned(),
            aux_dir: yaml_path.parent().unwrap().to_owned(),
            frame_count: 0,
            display_lists: Vec::new(),
            scroll_offsets: HashMap::new(),
            transform_properties: Vec::new(),
            font_render_mode: None,
            snapshots: HashMap::new(),
            allow_mipmaps: false,
            device_pixel_scale: 1.0,
            dppx_root_reference_frame: None,
            dppx_root_scroll_node: None,
            image_map: HashMap::new(),
            user_clip_id_map: HashMap::new(),
            user_clipchain_id_map: HashMap::new(),
            user_spatial_id_map: HashMap::new(),
            spatial_id_stack: Vec::new(),
            yaml_string: String::new(),
            requested_frame: 0,
            built_frame: usize::MAX,
            keyframes: None,
            external_image_handler: Some(Box::new(LocalExternalImageHandler::new())),
            next_external_scroll_id: 1000,      // arbitrary to easily see in logs which are implicit
        }
    }

    // Fonts are not torn down here: they live on `Wrench` so that an identical
    // font keeps its instance key across yaml files, which is what lets content
    // interning dedup a run that two files share.
    pub fn deinit(self, _wrench: &mut Wrench) {
    }

    fn top_space(&self) -> SpatialId {
        *self.spatial_id_stack.last().unwrap()
    }

    pub fn yaml_path(&self) -> &PathBuf {
        &self.yaml_path
    }

    pub fn new_from_args(args: &clap::ArgMatches) -> YamlFrameReader {
        let yaml_file = args.value_of("INPUT").map(PathBuf::from).unwrap();
        YamlFrameReader::new(&yaml_file)
    }

    pub fn new_from_show_args(args: &clap::ArgMatches) -> YamlFrameReader {
        let mut y = YamlFrameReader::new_from_args(args);

        y.keyframes = args.value_of("keyframes").map(|path| {
            let mut file = File::open(&path).unwrap();
            let mut keyframes_string = String::new();
            file.read_to_string(&mut keyframes_string).unwrap();
            YamlLoader::load_from_str(&keyframes_string)
                .expect("Failed to parse keyframes file")
                .pop()
                .unwrap()
        });
        y.list_resources = args.is_present("list-resources");
        y.watch_source = args.is_present("watch");
        y
    }

    pub fn reset(&mut self) {
        self.scroll_offsets.clear();
        self.display_lists.clear();
        self.transform_properties.clear();
    }

    fn parse_transform_properties(&mut self, yaml: &Yaml) {
        if let Some(props) = yaml["transform-properties"].as_vec() {
            for prop in props {
                let id = prop["id"].as_i64().expect("transform-property needs an id") as u64;
                let value = prop["transform"]
                    .as_transform(&LayoutPoint::zero())
                    .unwrap_or_default();
                self.transform_properties.push(PropertyValue {
                    key: PropertyBindingKey::new(id),
                    value,
                });
            }
        }
    }

    fn build(&mut self, wrench: &mut Wrench) {
        let yaml = YamlLoader::load_from_str(&self.yaml_string)
            .map(|mut yaml| {
                assert_eq!(yaml.len(), 1);
                yaml.pop().unwrap()
            })
            .expect("Failed to parse YAML file");

        self.reset();

        self.parse_transform_properties(&yaml);

        if let Some(pipelines) = yaml["pipelines"].as_vec() {
            for pipeline in pipelines {
                let pipeline_id = pipeline["id"].as_pipeline_id().unwrap();
                let mut builder = wrench.take_dl_builder(pipeline_id);
                self.build_pipeline(wrench, &mut builder, pipeline_id, false, pipeline);
                wrench.put_dl_builder(pipeline_id, builder);
            }
        }

        let root_pipeline_id = wrench.root_pipeline_id;
        let mut builder = wrench.take_dl_builder(root_pipeline_id);

        if let Some(frames) = yaml["frames"].as_vec() {
            for frame in frames {
                self.build_pipeline(wrench, &mut builder, root_pipeline_id, true, frame);
            }
        } else {
            let root_stacking_context = &yaml["root"];
            assert_ne!(*root_stacking_context, Yaml::BadValue);
            self.build_pipeline(wrench, &mut builder, root_pipeline_id, true, root_stacking_context);
        }

        wrench.put_dl_builder(root_pipeline_id, builder);

        // If replaying the same frame during interactive use, the frame gets rebuilt,
        // but the external image handler has already been consumed by the renderer.
        if let Some(external_image_handler) = self.external_image_handler.take() {
            wrench.renderer.set_external_image_handler(external_image_handler);
        }
    }

    fn build_pipeline(
        &mut self,
        wrench: &mut Wrench,
        builder: &mut DisplayListBuilder,
        pipeline_id: PipelineId,
        send_transaction: bool,
        yaml: &Yaml
    ) {
        let offscreen = yaml["offscreen"].as_bool().unwrap_or(false);
        // By default, present if send_transaction is set to true. Can be overridden
        // by a field in the pipeline's root.
        let present = !offscreen && yaml["present"].as_bool().unwrap_or(send_transaction);

        // Don't allow referencing clips between pipelines for now.
        self.user_clip_id_map.clear();
        self.user_clipchain_id_map.clear();
        self.user_spatial_id_map.clear();
        self.spatial_id_stack.clear();
        self.spatial_id_stack.push(SpatialId::root_scroll_node(pipeline_id));

        builder.begin(AU_PER_DEV_PX);

        // Apply the requested device pixel scale to the root pipeline by
        // wrapping its content in a scale reference frame. In this architecture
        // the device pixel ratio is expressed through the transform tree, so a
        // uniform root scale renders the scene as if at that device pixel ratio
        // (exercising snapping, raster scale selection, etc.).
        //
        // The pipeline's implicit root reference frame and root scroll node are
        // ancestors of that scale, so a second scale reference frame is created
        // as a sibling for content that the yaml explicitly attaches to the root
        // reference frame (fixed position content), which must be scaled but must
        // not scroll.
        self.dppx_root_reference_frame = None;
        self.dppx_root_scroll_node = None;
        let dppx_reference_frame = if send_transaction && self.device_pixel_scale != 1.0 {
            let scale = self.device_pixel_scale;
            let transform = PropertyBinding::Value(LayoutTransform::scale(scale, scale, 1.0));
            let kind = ReferenceFrameKind::Transform {
                is_2d_scale_translation: true,
                should_snap: false,
                paired_with_perspective: false,
            };

            let fixed_id = builder.push_reference_frame(
                LayoutPoint::zero(),
                SpatialId::root_reference_frame(pipeline_id),
                TransformStyle::Flat,
                transform,
                kind,
            );
            builder.pop_reference_frame();
            self.dppx_root_reference_frame = Some(fixed_id);

            let ref_frame_id = builder.push_reference_frame(
                LayoutPoint::zero(),
                *self.spatial_id_stack.last().unwrap(),
                TransformStyle::Flat,
                transform,
                kind,
            );
            self.dppx_root_scroll_node = Some(ref_frame_id);
            self.spatial_id_stack.push(ref_frame_id);
            true
        } else {
            false
        };

        let mut info = CommonItemProperties {
            clip_rect: LayoutRect::zero(),
            clip_chain_id: ClipChainId::INVALID,
            spatial_id: SpatialId::new(0, PipelineId::dummy()),
            flags: PrimitiveFlags::default(),
        };
        self.add_stacking_context_from_yaml(builder, wrench, yaml, IsRoot(true), &mut info);

        if dppx_reference_frame {
            self.spatial_id_stack.pop().unwrap();
            builder.pop_reference_frame();
        }

        let (pipeline, payload) = builder.end();
        self.display_lists.push(DisplayList {
            pipeline,
            payload,
            present,
            send_transaction,
            render_offscreen: offscreen,
        });

        assert_eq!(self.spatial_id_stack.len(), 1);
    }

    fn to_clip_chain_id(
        &self,
        item: &Yaml,
        builder: &mut DisplayListBuilder,
    ) -> Option<ClipChainId> {
        match *item {
            Yaml::Integer(value) => {
                Some(self.user_clipchain_id_map[&(value as u64)])
            }
            Yaml::Array(ref array) => {
                let clip_ids: Vec<ClipId> = array
                    .iter()
                    .map(|id| {
                        let id = id.as_i64().expect("invalid clip id") as u64;
                        self.user_clip_id_map[&id]
                    })
                    .collect();

                Some(builder.define_clip_chain(None, clip_ids))
            }
            _ => None,
        }
    }

    fn to_spatial_id(&self, item: &Yaml, pipeline_id: PipelineId) -> Option<SpatialId> {
        match *item {
            Yaml::Integer(value) => Some(self.user_spatial_id_map[&(value as u64)]),
            Yaml::String(ref id_string) if id_string == "root-reference-frame" =>
                Some(self.dppx_root_reference_frame
                    .unwrap_or_else(|| SpatialId::root_reference_frame(pipeline_id))),
            Yaml::String(ref id_string) if id_string == "root-scroll-node" =>
                Some(self.dppx_root_scroll_node
                    .unwrap_or_else(|| SpatialId::root_scroll_node(pipeline_id))),
            Yaml::BadValue => None,
            _ => {
                println!("Unable to parse SpatialId {:?}", item);
                None
            }
        }
    }

    fn add_clip_id_mapping(&mut self, numeric_id: u64, real_id: ClipId) {
        assert_ne!(numeric_id, 0, "id=0 is reserved for the root clip");
        self.user_clip_id_map.insert(numeric_id, real_id);
    }

    fn add_clip_chain_id_mapping(&mut self, numeric_id: u64, real_id: ClipChainId) {
        assert_ne!(numeric_id, 0, "id=0 is reserved for the root clip-chain");
        self.user_clipchain_id_map.insert(numeric_id, real_id);
    }

    fn add_spatial_id_mapping(&mut self, numeric_id: u64, real_id: SpatialId) {
        assert_ne!(numeric_id, 0, "id=0 is reserved for the root reference frame");
        assert_ne!(numeric_id, 1, "id=1 is reserved for the root scroll node");
        self.user_spatial_id_map.insert(numeric_id, real_id);
    }

    fn to_hit_testing_tag(&self, item: &Yaml) -> Option<ItemTag> {
        match *item {
            Yaml::Array(ref array) if array.len() == 2 => {
                match (array[0].as_i64(), array[1].as_i64()) {
                    (Some(first), Some(second)) => Some((first as u64, second as u16)),
                    _ => None,
                }
            }
            _ => None,
        }

    }

    fn add_or_get_image(
        &mut self,
        file: &Path,
        tiling: Option<i64>,
        item: &Yaml,
        wrench: &mut Wrench,
    ) -> (ImageKey, LayoutSize) {
        let key = (file.to_owned(), tiling);
        if let Some(k) = self.image_map.get(&key) {
            return *k;
        }

        if self.list_resources { println!("{}", file.to_string_lossy()); }
        let (descriptor, image_data) = match image::open(file) {
            Ok(image) => {
                let (image_width, image_height) = image.dimensions();
                let (format, bytes) = match image {
                    image::DynamicImage::ImageLuma8(_) => {
                        (ImageFormat::R8, image.to_bytes())
                    }
                    image::DynamicImage::ImageLumaA8(_) => {
                        (ImageFormat::RG8, image.to_bytes())
                    }
                    image::DynamicImage::ImageRgba8(_) => {
                        let mut pixels = image.to_bytes();
                        premultiply(pixels.as_mut_slice());
                        (ImageFormat::BGRA8, pixels)
                    }
                    image::DynamicImage::ImageRgb8(_) => {
                        let bytes = image.to_bytes();
                        let mut pixels = Vec::with_capacity(image_width as usize * image_height as usize * 4);
                        for bgr in bytes.chunks(3) {
                            pixels.extend_from_slice(&[
                                bgr[2],
                                bgr[1],
                                bgr[0],
                                0xff
                            ]);
                        }
                        (ImageFormat::BGRA8, pixels)
                    }
                    image::DynamicImage::ImageLuma16(_) => {
                        (ImageFormat::R16, image.to_bytes())
                    }
                    image::DynamicImage::ImageLumaA16(_) => {
                        (ImageFormat::RG16, image.to_bytes())
                    }
                    _ => panic!("We don't support whatever your crazy image type is, come on"),
                };
                let mut flags = ImageDescriptorFlags::empty();
                if is_image_opaque(format, &bytes[..]) {
                    flags |= ImageDescriptorFlags::IS_OPAQUE;
                }
                if self.allow_mipmaps {
                    flags |= ImageDescriptorFlags::ALLOW_MIPMAPS;
                }
                let descriptor = ImageDescriptor::new(
                    image_width as i32,
                    image_height as i32,
                    format,
                    flags,
                );
                let data = ImageData::new(bytes);
                (descriptor, data)
            }
            _ => {
                // This is a hack but it is convenient when generating test cases and avoids
                // bloating the repository.
                match parse_function(
                    file.components()
                        .last()
                        .unwrap()
                        .as_os_str()
                        .to_str()
                        .unwrap(),
                ) {
                    ("xy-gradient", args, _) => generate_xy_gradient_image(
                        args.get(0).unwrap_or(&"1000").parse::<u32>().unwrap(),
                        args.get(1).unwrap_or(&"1000").parse::<u32>().unwrap(),
                    ),
                    ("solid-color", args, _) => generate_solid_color_image(
                        args.get(0).unwrap_or(&"255").parse::<u8>().unwrap(),
                        args.get(1).unwrap_or(&"255").parse::<u8>().unwrap(),
                        args.get(2).unwrap_or(&"255").parse::<u8>().unwrap(),
                        args.get(3).unwrap_or(&"255").parse::<u8>().unwrap(),
                        args.get(4).unwrap_or(&"1000").parse::<u32>().unwrap(),
                        args.get(5).unwrap_or(&"1000").parse::<u32>().unwrap(),
                    ),
                    (name @ "transparent-checkerboard", args, _) |
                    (name @ "checkerboard", args, _) => {
                        let border = args.get(0).unwrap_or(&"4").parse::<u32>().unwrap();

                        let (x_size, y_size, x_count, y_count) = match args.len() {
                            3 => {
                                let size = args.get(1).unwrap_or(&"32").parse::<u32>().unwrap();
                                let count = args.get(2).unwrap_or(&"8").parse::<u32>().unwrap();
                                (size, size, count, count)
                            }
                            5 => {
                                let x_size = args.get(1).unwrap_or(&"32").parse::<u32>().unwrap();
                                let y_size = args.get(2).unwrap_or(&"32").parse::<u32>().unwrap();
                                let x_count = args.get(3).unwrap_or(&"8").parse::<u32>().unwrap();
                                let y_count = args.get(4).unwrap_or(&"8").parse::<u32>().unwrap();
                                (x_size, y_size, x_count, y_count)
                            }
                            _ => {
                                panic!("invalid checkerboard function");
                            }
                        };

                        let kind = if name == "transparent-checkerboard" {
                            CheckerboardKind::BlackTransparent
                        } else {
                            CheckerboardKind::BlackGrey
                        };

                        generate_checkerboard_image(
                            border,
                            x_size,
                            y_size,
                            x_count,
                            y_count,
                            kind,
                        )
                    }
                    ("snapshot", args, _) => {
                        let snapshot = self.snapshots
                            .get(args[0])
                            .expect("Missing snapshot");
                        return (
                            snapshot.key.as_image(),
                            snapshot.size,
                        );
                    }
                    _ => {
                        panic!("Failed to load image {:?}", file.to_str());
                    }
                }
            }
        };
        let tiling = tiling.map(|tile_size| tile_size as u16);
        let image_key = wrench.api.generate_image_key();
        let mut txn = Transaction::new();

        let external = item["external"].as_bool().unwrap_or(false);
        if external {
            // This indicates we want to simulate an external texture,
            // ensure it gets created as such
            let external_target = match item["external-target"].as_str() {
                Some("2d") => ImageBufferKind::Texture2D,
                Some("rect") => ImageBufferKind::TextureRect,
                Some(t) => panic!("Unsupported external texture target: {}", t),
                None => ImageBufferKind::Texture2D,
            };

            let external_image_data =
                self.external_image_handler.as_mut().unwrap().add_image(
                    &wrench.renderer.device,
                    descriptor,
                    external_target,
                    image_data
                );
            txn.add_image(image_key, descriptor, external_image_data, tiling);
        } else {
            txn.add_image(image_key, descriptor, image_data, tiling);
        }

        wrench.api.send_transaction(wrench.document_id, txn);
        let val = (
            image_key,
            LayoutSize::new(descriptor.size.width as f32, descriptor.size.height as f32),
        );
        self.image_map.insert(key, val);
        val
    }

    fn get_or_create_font(&mut self, desc: FontDescriptor, wrench: &mut Wrench) -> FontKey {
        let list_resources = self.list_resources;
        wrench.get_or_create_font(desc, |wrench, desc| match *desc {
            FontDescriptor::Path {
                ref path,
                font_index,
            } => {
                if list_resources { println!("{}", path.to_string_lossy()); }
                let mut file = File::open(path).expect("Couldn't open font file");
                let mut bytes = vec![];
                file.read_to_end(&mut bytes)
                    .expect("failed to read font file");
                wrench.font_key_from_bytes(bytes, font_index)
            }
            FontDescriptor::Family { ref name } => wrench.font_key_from_name(name),
            FontDescriptor::Properties {
                ref family,
                weight,
                style,
                stretch,
            } => wrench.font_key_from_properties(family, weight, style, stretch),
        })
    }

    pub fn allow_mipmaps(&mut self, allow_mipmaps: bool) {
        self.allow_mipmaps = allow_mipmaps;
    }

    pub fn set_device_pixel_scale(&mut self, scale: f32) {
        self.device_pixel_scale = scale;
    }

    pub fn set_font_render_mode(&mut self, render_mode: Option<FontRenderMode>) {
        self.font_render_mode = render_mode;
    }

    fn get_or_create_font_instance(
        &mut self,
        font_key: FontKey,
        size: f32,
        flags: FontInstanceFlags,
        synthetic_italics: SyntheticItalics,
        wrench: &mut Wrench,
    ) -> FontInstanceKey {
        wrench.get_or_create_font_instance(FontInstanceDescriptor {
            font_key,
            size: size.into(),
            flags,
            render_mode: self.font_render_mode,
            synthetic_italics,
        })
    }

    fn as_image_mask(&mut self, item: &Yaml, wrench: &mut Wrench) -> Option<ImageMask> {
        item.as_hash()?;

        let tiling = item["tile-size"].as_i64();

        let (image_key, image_dims) = match item["image"].as_str() {
            Some("invalid") => (ImageKey::DUMMY, LayoutSize::new(100.0, 100.0)),
            Some(filename) => {
                let mut file = self.aux_dir.clone();
                file.push(filename);
                self.add_or_get_image(&file, tiling, item, wrench)
            }
            None => {
                warn!("No image provided for the image-mask!");
                return None;
            }
        };

        let image_rect = item["rect"]
            .as_rect()
            .unwrap_or_else(|| LayoutRect::from_size(image_dims));
        Some(ImageMask {
            image: image_key,
            rect: image_rect,
        })
    }

    fn handle_rect(
        &self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &CommonItemProperties,
    ) {
        let bounds_key = if item["type"].is_badvalue() {
            "rect"
        } else {
            "bounds"
        };

        let bounds = self.resolve_rect(&item[bounds_key]);
        let color = self.resolve_colorf(&item["color"]).unwrap_or(ColorF::BLACK);
        dl.push_rect(info, bounds, color);
    }

    fn handle_hit_test(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        info.clip_rect = try_intersect!(
            item["bounds"].as_rect().expect("hit-test type must have bounds"),
            &info.clip_rect
        );

        if let Some(tag) = self.to_hit_testing_tag(&item["hit-testing-tag"]) {
            dl.push_hit_test(
                info.clip_rect,
                info.clip_chain_id,
                info.spatial_id,
                info.flags,
                tag,
            );
        }
    }

    fn handle_line(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let color = item["color"].as_colorf().unwrap_or(ColorF::BLACK);
        let orientation = item["orientation"]
            .as_str()
            .and_then(LineOrientation::from_str)
            .expect("line must have orientation");
        let style = item["style"]
            .as_str()
            .and_then(LineStyle::from_str)
            .expect("line must have style");

        let wavy_line_thickness = if let LineStyle::Wavy = style {
            item["thickness"].as_f32().expect("wavy lines must have a thickness")
        } else {
            0.0
        };

        let area = if item["baseline"].is_badvalue() {
            let bounds_key = if item["type"].is_badvalue() {
                "rect"
            } else {
                "bounds"
            };

            item[bounds_key]
                .as_rect()
                .expect("line type must have bounds")
        } else {
            // Legacy line representation
            let baseline = item["baseline"].as_f32().expect("line must have baseline");
            let start = item["start"].as_f32().expect("line must have start");
            let end = item["end"].as_f32().expect("line must have end");
            let width = item["width"].as_f32().expect("line must have width");

            match orientation {
                LineOrientation::Horizontal => {
                    LayoutRect::from_origin_and_size(
                        LayoutPoint::new(start, baseline),
                        LayoutSize::new(end - start, width),
                    )
                }
                LineOrientation::Vertical => {
                    LayoutRect::from_origin_and_size(
                        LayoutPoint::new(baseline, start),
                        LayoutSize::new(width, end - start),
                    )
                }
            }
            };

        dl.push_line(
            info,
            &area,
            wavy_line_thickness,
            orientation,
            &color,
            style,
        );
    }

    fn handle_gradient(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let bounds_key = if item["type"].is_badvalue() {
            "gradient"
        } else {
            "bounds"
        };
        let bounds = item[bounds_key]
            .as_rect()
            .expect("gradient must have bounds");

        let (gradient, stops) = item.as_gradient(dl);
        let tile_size = item["tile-size"].as_size().unwrap_or_else(|| bounds.size());
        let tile_spacing = item["tile-spacing"].as_size().unwrap_or_else(LayoutSize::zero);

        dl.push_gradient(
            info,
            bounds,
            gradient,
            tile_size,
            tile_spacing,
            &stops,
        );
    }

    fn handle_radial_gradient(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let bounds_key = if item["type"].is_badvalue() {
            "radial-gradient"
        } else {
            "bounds"
        };
        let bounds = item[bounds_key]
            .as_rect()
            .expect("radial gradient must have bounds");
        let (gradient, stops) = item.as_radial_gradient(dl);
        let tile_size = item["tile-size"].as_size().unwrap_or_else(|| bounds.size());
        let tile_spacing = item["tile-spacing"].as_size().unwrap_or_else(LayoutSize::zero);

        dl.push_radial_gradient(
            info,
            bounds,
            gradient,
            tile_size,
            tile_spacing,
            &stops,
        );
    }

    fn handle_conic_gradient(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let bounds_key = if item["type"].is_badvalue() {
            "conic-gradient"
        } else {
            "bounds"
        };
        let bounds = item[bounds_key]
            .as_rect()
            .expect("conic gradient must have bounds");
        let (gradient, stops) = item.as_conic_gradient(dl);
        let tile_size = item["tile-size"].as_size().unwrap_or_else(|| bounds.size());
        let tile_spacing = item["tile-spacing"].as_size().unwrap_or_else(LayoutSize::zero);

        dl.push_conic_gradient(
            info,
            bounds,
            gradient,
            tile_size,
            tile_spacing,
            &stops,
        );
    }

    fn handle_border(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        // Set by a nine-patch gradient source; the stops travel with the
        // gradient to `push_border` rather than being recorded separately.
        let mut gradient_stops = Vec::new();
        let bounds_key = if item["type"].is_badvalue() {
            "border"
        } else {
            "bounds"
        };
        let bounds = item[bounds_key].as_rect().expect("borders must have bounds");
        let widths = item["width"]
            .as_vec_f32()
            .expect("borders must have width(s)");
        let widths = broadcast(&widths, 4);
        let widths = LayoutSideOffsets::new(widths[0], widths[3], widths[2], widths[1]);
        let border_details = if let Some(border_type) = item["border-type"].as_str() {
            match border_type {
                "normal" => {
                    let colors = item["color"]
                        .as_vec_colorf()
                        .expect("borders must have color(s)");
                    let styles = item["style"]
                        .as_vec_string()
                        .expect("borders must have style(s)");
                    let styles = styles
                        .iter()
                        .map(|s| match s.as_str() {
                            "none" => BorderStyle::None,
                            "solid" => BorderStyle::Solid,
                            "double" => BorderStyle::Double,
                            "dotted" => BorderStyle::Dotted,
                            "dashed" => BorderStyle::Dashed,
                            "hidden" => BorderStyle::Hidden,
                            "ridge" => BorderStyle::Ridge,
                            "inset" => BorderStyle::Inset,
                            "outset" => BorderStyle::Outset,
                            "groove" => BorderStyle::Groove,
                            s => {
                                panic!("Unknown border style '{}'", s);
                            }
                        })
                        .collect::<Vec<BorderStyle>>();
                    let radius = item["radius"]
                        .as_border_radius()
                        .unwrap_or_else(BorderRadius::zero);
                    let inset = item["inset"]
                        .as_side_offsets()
                        .unwrap_or_else(LayoutSideOffsets::zero);

                    let colors = broadcast(&colors, 4);
                    let styles = broadcast(&styles, 4);

                    let top = BorderSide {
                        color: colors[0],
                        style: styles[0],
                    };
                    let right = BorderSide {
                        color: colors[1],
                        style: styles[1],
                    };
                    let bottom = BorderSide {
                        color: colors[2],
                        style: styles[2],
                    };
                    let left = BorderSide {
                        color: colors[3],
                        style: styles[3],
                    };
                    let do_aa = item["do_aa"].as_bool().unwrap_or(true);
                    Some(BorderDetails::Normal(NormalBorder {
                        top,
                        left,
                        bottom,
                        right,
                        radius,
                        inset,
                        do_aa,
                    }))
                }
                "image" | "gradient" | "radial-gradient" | "conic-gradient" => {
                    let image_width = item["image-width"]
                        .as_i64()
                        .unwrap_or(bounds.width() as i64);
                    let image_height = item["image-height"]
                        .as_i64()
                        .unwrap_or(bounds.height() as i64);
                    let fill = item["fill"].as_bool().unwrap_or(false);

                    let slice = if let Some(slice) = item["slice"].as_vec_u32() {
                        broadcast(&slice, 4)
                    } else {
                        vec![widths.top as u32, widths.left as u32, widths.bottom as u32, widths.right as u32]
                    };

                    let repeat_horizontal = match item["repeat-horizontal"]
                        .as_str()
                        .unwrap_or("stretch")
                    {
                        "stretch" => RepeatMode::Stretch,
                        "repeat" => RepeatMode::Repeat,
                        "round" => RepeatMode::Round,
                        "space" => RepeatMode::Space,
                        s => panic!("Unknown box border image repeat mode {}", s),
                    };
                    let repeat_vertical = match item["repeat-vertical"]
                        .as_str()
                        .unwrap_or("stretch")
                    {
                        "stretch" => RepeatMode::Stretch,
                        "repeat" => RepeatMode::Repeat,
                        "round" => RepeatMode::Round,
                        "space" => RepeatMode::Space,
                        s => panic!("Unknown box border image repeat mode {}", s),
                    };
                    let source = match border_type {
                        "image" => {
                            let file = rsrc_path(&item["image-source"], &self.aux_dir);
                            let (image_key, _) = self
                                .add_or_get_image(&file, None, item, wrench);
                            NinePatchBorderSource::Image(image_key, ImageRendering::Auto)
                        }
                        "gradient" => {
                            let (gradient, stops) = item.as_gradient(dl);
                            gradient_stops = stops;
                            NinePatchBorderSource::Gradient(gradient)
                        }
                        "radial-gradient" => {
                            let (gradient, stops) = item.as_radial_gradient(dl);
                            gradient_stops = stops;
                            NinePatchBorderSource::RadialGradient(gradient)
                        }
                        "conic-gradient" => {
                            let (gradient, stops) = item.as_conic_gradient(dl);
                            gradient_stops = stops;
                            NinePatchBorderSource::ConicGradient(gradient)
                        }
                        _ => unreachable!("Unexpected border type"),
                    };

                    Some(BorderDetails::NinePatch(NinePatchBorder {
                        source,
                        width: image_width as i32,
                        height: image_height as i32,
                        slice: SideOffsets2D::new(slice[0] as i32, slice[1] as i32, slice[2] as i32, slice[3] as i32),
                        fill,
                        repeat_horizontal,
                        repeat_vertical,
                    }))
                }
                _ => {
                    println!("Unable to parse border {:?}", item);
                    None
                }
            }
        } else {
            println!("Unable to parse border {:?}", item);
            None
        };
        if let Some(details) = border_details {
            dl.push_border(info, bounds, widths, details, &gradient_stops);
        }
    }

    fn handle_box_shadow(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let bounds_key = if item["type"].is_badvalue() {
            "box-shadow"
        } else {
            "bounds"
        };
        let bounds = item[bounds_key]
            .as_rect()
            .expect("box shadow must have bounds");
        let box_bounds = item["box-bounds"].as_rect().unwrap_or(bounds);
        let offset = self.resolve_vector(&item["offset"], LayoutVector2D::zero());
        let color = item["color"]
            .as_colorf()
            .unwrap_or_else(|| ColorF::new(0.0, 0.0, 0.0, 1.0));
        let blur_radius = item["blur-radius"].as_force_f32().unwrap_or(0.0);
        let spread_radius = item["spread-radius"].as_force_f32().unwrap_or(0.0);
        let border_radius = item["border-radius"]
            .as_border_radius()
            .unwrap_or_else(BorderRadius::zero);
        let clip_mode = if let Some(mode) = item["clip-mode"].as_str() {
            match mode {
                "outset" => BoxShadowClipMode::Outset,
                "inset" => BoxShadowClipMode::Inset,
                s => panic!("Unknown box shadow clip mode {}", s),
            }
        } else {
            BoxShadowClipMode::Outset
        };
        let shadow_radius = item["shadow-radius"].as_border_radius().unwrap_or_else(|| {
            let adjust_radius = |border_radius: f32, spread_amount: f32| {
                if border_radius > 0.0 {
                    (border_radius + spread_amount).max(0.0)
                } else {
                    0.0
                }
            };
            let adjust_corner = |corner: LayoutSize, spread_amount: f32| {
                LayoutSize::new(
                    adjust_radius(corner.width, spread_amount),
                    adjust_radius(corner.height, spread_amount),
                )
            };
            let spread_amount = match clip_mode {
                BoxShadowClipMode::Outset => spread_radius,
                BoxShadowClipMode::Inset => -spread_radius,
            };
            BorderRadius {
                top_left: adjust_corner(border_radius.top_left, spread_amount),
                top_right: adjust_corner(border_radius.top_right, spread_amount),
                bottom_right: adjust_corner(border_radius.bottom_right, spread_amount),
                bottom_left: adjust_corner(border_radius.bottom_left, spread_amount),
                shape_top_left: border_radius.shape_top_left,
                shape_top_right: border_radius.shape_top_right,
                shape_bottom_left: border_radius.shape_bottom_left,
                shape_bottom_right: border_radius.shape_bottom_right,
            }
        });

        dl.push_box_shadow(
            info,
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

    fn handle_yuv_image(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        // TODO(gw): Support other YUV color spaces.
        let color_space = YuvColorSpace::Rec709;
        let color_range = ColorRange::Limited;

        let yuv_data = match item["format"].as_str().expect("no format supplied") {
            "planar" => {
                let y_path = rsrc_path(&item["src-y"], &self.aux_dir);
                let (y_key, _) = self.add_or_get_image(&y_path, None, item, wrench);

                let u_path = rsrc_path(&item["src-u"], &self.aux_dir);
                let (u_key, _) = self.add_or_get_image(&u_path, None, item, wrench);

                let v_path = rsrc_path(&item["src-v"], &self.aux_dir);
                let (v_key, _) = self.add_or_get_image(&v_path, None, item, wrench);

                YuvData::PlanarYCbCr(y_key, u_key, v_key)
            }
            "nv12" => {
                let y_path = rsrc_path(&item["src-y"], &self.aux_dir);
                let (y_key, _) = self.add_or_get_image(&y_path, None, item, wrench);

                let uv_path = rsrc_path(&item["src-uv"], &self.aux_dir);
                let (uv_key, _) = self.add_or_get_image(&uv_path, None, item, wrench);

                YuvData::NV12(y_key, uv_key)
            }
            "p010" => {
                let y_path = rsrc_path(&item["src-y"], &self.aux_dir);
                let (y_key, _) = self.add_or_get_image(&y_path, None, item, wrench);

                let uv_path = rsrc_path(&item["src-uv"], &self.aux_dir);
                let (uv_key, _) = self.add_or_get_image(&uv_path, None, item, wrench);

                YuvData::P010(y_key, uv_key)
            }
            "nv16" => {
                let y_path = rsrc_path(&item["src-y"], &self.aux_dir);
                let (y_key, _) = self.add_or_get_image(&y_path, None, item, wrench);

                let uv_path = rsrc_path(&item["src-uv"], &self.aux_dir);
                let (uv_key, _) = self.add_or_get_image(&uv_path, None, item, wrench);

                YuvData::NV16(y_key, uv_key)
            }
            "p210" => {
                let y_path = rsrc_path(&item["src-y"], &self.aux_dir);
                let (y_key, _) = self.add_or_get_image(&y_path, None, item, wrench);

                let uv_path = rsrc_path(&item["src-uv"], &self.aux_dir);
                let (uv_key, _) = self.add_or_get_image(&uv_path, None, item, wrench);

                YuvData::P210(y_key, uv_key)
            }
            "interleaved" => {
                let yuv_path = rsrc_path(&item["src"], &self.aux_dir);
                let (yuv_key, _) = self.add_or_get_image(&yuv_path, None, item, wrench);

                YuvData::InterleavedYCbCr(yuv_key)
            }
            _ => {
                panic!("unexpected yuv format");
            }
        };

        let color_depth = match yuv_data.get_format() {
            YuvFormat::NV12 |
            YuvFormat::NV16 |
            YuvFormat::PlanarYCbCr |
            YuvFormat::InterleavedYCbCr => ColorDepth::Color8,
            YuvFormat::P010 |
            YuvFormat::P210 => ColorDepth::Color10,
        };

        let bounds = item["bounds"].as_vec_f32().unwrap();
        let bounds = LayoutRect::from_origin_and_size(
            LayoutPoint::new(bounds[0], bounds[1]),
            LayoutSize::new(bounds[2], bounds[3]),
        );

        dl.push_yuv_image(
            info,
            bounds,
            yuv_data,
            color_depth,
            color_space,
            color_range,
            ImageRendering::Auto,
        );
    }

    fn handle_image(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let filename = &item[if item["type"].is_badvalue() {
                                 "image"
                             } else {
                                 "src"
                             }];
        let tiling = item["tile-size"].as_i64();

        let file = rsrc_path(filename, &self.aux_dir);
        let (image_key, image_dims) =
            self.add_or_get_image(&file, tiling, item, wrench);

        let bounds_raws = item["bounds"].as_vec_f32().unwrap();
        let bounds = if bounds_raws.len() == 2 {
            LayoutRect::from_origin_and_size(LayoutPoint::new(bounds_raws[0], bounds_raws[1]), image_dims)
        } else if bounds_raws.len() == 4 {
            LayoutRect::from_origin_and_size(
                LayoutPoint::new(bounds_raws[0], bounds_raws[1]),
                LayoutSize::new(bounds_raws[2], bounds_raws[3]),
            )
        } else {
            panic!(
                "image expected 2 or 4 values in bounds, got '{:?}'",
                item["bounds"]
            );
        };
        let rendering = match item["rendering"].as_str() {
            Some("auto") | None => ImageRendering::Auto,
            Some("crisp-edges") => ImageRendering::CrispEdges,
            Some("pixelated") => ImageRendering::Pixelated,
            Some(_) => panic!(
                "ImageRendering can be auto, crisp-edges, or pixelated -- got {:?}",
                item
            ),
        };
        let alpha_type = match item["alpha-type"].as_str() {
            Some("premultiplied-alpha") | None => AlphaType::PremultipliedAlpha,
            Some("alpha") => AlphaType::Alpha,
            Some(_) => panic!(
                "AlphaType can be premultiplied-alpha or alpha -- got {:?}",
                item
            ),
        };
        let color = item["color"]
            .as_colorf()
            .unwrap_or_else(|| ColorF::WHITE);
        let stretch_size = item["stretch-size"].as_size();
        let tile_spacing = item["tile-spacing"].as_size();
        if stretch_size.is_none() && tile_spacing.is_none() {
            dl.push_image(
                info,
                bounds,
                rendering,
                alpha_type,
                image_key,
                color,
           );
        } else {
            dl.push_repeating_image(
                info,
                bounds,
                stretch_size.unwrap_or(image_dims),
                tile_spacing.unwrap_or_else(LayoutSize::zero),
                rendering,
                alpha_type,
                image_key,
                color,
           );
        }
    }

    fn handle_text(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let size = item["size"].as_pt_to_f32().unwrap_or(16.0);
        let color = item["color"].as_colorf().unwrap_or(ColorF::BLACK);
        let synthetic_italics = if let Some(angle) = item["synthetic-italics"].as_f32() {
            SyntheticItalics::from_degrees(angle)
        } else if item["synthetic-italics"].as_bool().unwrap_or(false) {
            SyntheticItalics::enabled()
        } else {
            SyntheticItalics::disabled()
        };

        let mut flags = FontInstanceFlags::empty();
        if item["synthetic-bold"].as_bool().unwrap_or(false) {
            flags |= FontInstanceFlags::SYNTHETIC_BOLD;
        }
        if item["embedded-bitmaps"].as_bool().unwrap_or(false) {
            flags |= FontInstanceFlags::EMBEDDED_BITMAPS;
        }
        if item["transpose"].as_bool().unwrap_or(false) {
            flags |= FontInstanceFlags::TRANSPOSE;
        }
        if item["flip-x"].as_bool().unwrap_or(false) {
            flags |= FontInstanceFlags::FLIP_X;
        }
        if item["flip-y"].as_bool().unwrap_or(false) {
            flags |= FontInstanceFlags::FLIP_Y;
        }

        assert!(
            item["blur-radius"].is_badvalue(),
            "text no longer has a blur radius, use PushShadow and PopAllShadows"
        );

        let desc = FontDescriptor::from_yaml(item, &self.aux_dir);
        let font_key = self.get_or_create_font(desc, wrench);
        let font_instance_key = self.get_or_create_font_instance(font_key,
                                                                 size,
                                                                 flags,
                                                                 synthetic_italics,
                                                                 wrench);

        assert!(
            !(item["glyphs"].is_badvalue() && item["text"].is_badvalue()),
            "text item had neither text nor glyphs!"
        );

        let (glyphs, rect) = if item["text"].is_badvalue() {
            // if glyphs are specified, then the glyph positions can have the
            // origin baked in.
            let origin = item["origin"]
                .as_point()
                .unwrap_or(LayoutPoint::new(0.0, 0.0));
            let glyph_indices = item["glyphs"].as_vec_u32().unwrap();
            let glyph_offsets = item["offsets"].as_vec_f32().unwrap();
            assert_eq!(glyph_offsets.len(), glyph_indices.len() * 2);

            let glyphs = glyph_indices
                .iter()
                .enumerate()
                .map(|k| {
                    GlyphInstance {
                        index: *k.1,
                        // In the future we want to change the API to be relative, eliminating this
                        point: LayoutPoint::new(
                            origin.x + glyph_offsets[k.0 * 2],
                            origin.y + glyph_offsets[k.0 * 2 + 1],
                        ),
                    }
                })
                .collect::<Vec<_>>();
            // TODO(gw): We could optionally use the WR API to query glyph dimensions
            //           here and calculate the bounding region here if we want to.
            let rect = item["bounds"]
                .as_rect()
                .expect("Text items with glyphs require bounds [for now]");
            (glyphs, rect)
        } else {
            let text = item["text"].as_str().unwrap();
            let origin = item["origin"]
                .as_point()
                .expect("origin required for text without glyphs");
            let (glyph_indices, glyph_positions, bounds) = wrench.layout_simple_ascii(
                font_key,
                font_instance_key,
                text,
                size,
                origin,
                flags,
            );

            let glyphs = glyph_indices
                .iter()
                .zip(glyph_positions)
                .map(|arg| {
                    GlyphInstance {
                        index: *arg.0 as u32,
                        point: arg.1,
                    }
                })
                .collect::<Vec<_>>();
            (glyphs, bounds)
        };

        dl.push_text(
            info,
            rect,
            &glyphs,
            font_instance_key,
            color,
            None,
        );
    }

    fn handle_iframe(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let bounds = item["bounds"].as_rect().expect("iframe must have bounds");
        let pipeline_id = item["id"].as_pipeline_id().unwrap();
        let ignore = item["ignore_missing_pipeline"].as_bool().unwrap_or(true);
        dl.push_iframe(
            bounds,
            info.clip_rect,
            &SpaceAndClipInfo {
                spatial_id: info.spatial_id,
                clip_chain_id: info.clip_chain_id
            },
            pipeline_id,
            ignore
        );
    }

    fn get_item_type_from_yaml(item: &Yaml) -> &str {
        let shorthands = [
            "rect",
            "image",
            "text",
            "glyphs",
            "box-shadow", // Note: box_shadow shorthand check has to come before border.
            "border",
            "gradient",
            "radial-gradient",
            "conic-gradient"
        ];

        for shorthand in shorthands.iter() {
            if !item[*shorthand].is_badvalue() {
                return shorthand;
            }
        }
        item["type"].as_str().unwrap_or("unknown")
    }

    fn add_display_list_items_from_yaml(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        yaml_items: &[Yaml],
    ) {
        // A very large number (but safely far away from finite limits of f32)
        let big_number = 1.0e30;
        // A rect that should in practical terms serve as a no-op for clipping
        let full_clip = LayoutRect::from_origin_and_size(
            LayoutPoint::new(-big_number / 2.0, -big_number / 2.0),
            LayoutSize::new(big_number, big_number));

        for item in yaml_items {
            let item_type = Self::get_item_type_from_yaml(item);

            let spatial_id = self.to_spatial_id(&item["spatial-id"], dl.pipeline_id);

            if let Some(spatial_id) = spatial_id {
                self.spatial_id_stack.push(spatial_id);
            }

            let clip_rect = item["clip-rect"].as_rect().unwrap_or(full_clip);
            let clip_chain_id = self.to_clip_chain_id(&item["clip-chain"], dl).unwrap_or(ClipChainId::INVALID);

            let mut flags = PrimitiveFlags::default();
            for (key, flag) in [
                ("backface-visible", PrimitiveFlags::IS_BACKFACE_VISIBLE),
                ("scrollbar-container", PrimitiveFlags::IS_SCROLLBAR_CONTAINER),
                ("prefer-compositor-surface", PrimitiveFlags::PREFER_COMPOSITOR_SURFACE),
                ("checkerboard-background", PrimitiveFlags::CHECKERBOARD_BACKGROUND),
            ] {
                if let Some(value) = item[key].as_bool() {
                    flags.set(flag, value);
                }
            }


            let mut info = CommonItemProperties {
                clip_rect,
                clip_chain_id,
                spatial_id: self.top_space(),
                flags,
            };

            match item_type {
                "rect" => self.handle_rect(dl, item, &info),
                "hit-test" => self.handle_hit_test(dl, item, &mut info),
                "line" => self.handle_line(dl, item, &mut info),
                "image" => self.handle_image(dl, wrench, item, &mut info),
                "yuv-image" => self.handle_yuv_image(dl, wrench, item, &mut info),
                "text" | "glyphs" => self.handle_text(dl, wrench, item, &mut info),
                "scroll-frame" => self.handle_scroll_frame(dl, wrench, item),
                "sticky-frame" => self.handle_sticky_frame(dl, wrench, item),
                "clip" => self.handle_clip(dl, wrench, item),
                "clip-chain" => self.handle_clip_chain(dl, item),
                "border" => self.handle_border(dl, wrench, item, &mut info),
                "gradient" => self.handle_gradient(dl, item, &mut info),
                "radial-gradient" => self.handle_radial_gradient(dl, item, &mut info),
                "conic-gradient" => self.handle_conic_gradient(dl, item, &mut info),
                "box-shadow" => self.handle_box_shadow(dl, item, &mut info),
                "iframe" => self.handle_iframe(dl, item, &mut info),
                "stacking-context" => {
                    self.add_stacking_context_from_yaml(dl, wrench, item, IsRoot(false), &mut info)
                }
                "reference-frame" => self.handle_reference_frame(dl, wrench, item),
                "computed-frame" => self.handle_computed_frame(dl, wrench, item),
                "shadow" => self.handle_push_shadow(dl, item, &mut info),
                "pop-all-shadows" => self.handle_pop_all_shadows(dl),
                "backdrop-filter" => self.handle_backdrop_filter(dl, item, &mut info),
                _ => println!("Skipping unknown item type: {:?}", item),
            }

            if spatial_id.is_some() {
                self.spatial_id_stack.pop().unwrap();
            }
        }
    }

    fn handle_scroll_frame(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        yaml: &Yaml,
    ) {
        let clip_rect = yaml["bounds"]
            .as_rect()
            .expect("scroll frame must have a bounds");
        let content_size = yaml["content-size"].as_size().unwrap_or_else(|| clip_rect.size());
        let content_rect = LayoutRect::from_origin_and_size(clip_rect.min, content_size);
        let external_scroll_offset = yaml["external-scroll-offset"].as_vector().unwrap_or_else(LayoutVector2D::zero);
        let scroll_generation = yaml["scroll-generation"].as_i64().map_or(APZScrollGeneration::default(), |v| v as u64);
        let has_scroll_linked_effect =
        yaml["has-scroll-linked-effect"].as_bool().map_or(HasScrollLinkedEffect::default(),
            |v| if v { HasScrollLinkedEffect::Yes } else { HasScrollLinkedEffect::No }
        );

        let numeric_id = yaml["id"].as_i64().map(|id| id as u64);

        let external_id = ExternalScrollId(self.next_external_scroll_id, dl.pipeline_id);
        self.next_external_scroll_id += 1;

        if let Some(vector) = yaml["scroll-offset"].as_vector() {
            self.scroll_offsets.insert(
                external_id,
                vec![SampledScrollOffset {
                    offset: vector,
                    generation: APZScrollGeneration::default(),
                }],
            );
        }

        if !yaml["scroll-offsets"].is_badvalue() {
            let mut offsets = Vec::new();
            for entry in yaml["scroll-offsets"].as_vec().unwrap() {
                let offset = entry["offset"].as_vector().unwrap_or(LayoutVector2D::zero());
                let generation = entry["generation"].as_i64().map_or(APZScrollGeneration::default(), |v| v as u64);
                offsets.push(SampledScrollOffset { offset, generation });
            }
            self.scroll_offsets.insert(external_id, offsets);
        }

        let clip_to_frame = yaml["clip-to-frame"].as_bool().unwrap_or(false);

        let clip_id = if clip_to_frame {
            Some(dl.define_clip_rect(
                self.top_space(),
                clip_rect,
            ))
        } else {
            None
        };

        let spatial_id = dl.define_scroll_frame(
            self.top_space(),
            external_id,
            content_rect,
            clip_rect,
            external_scroll_offset,
            scroll_generation,
            has_scroll_linked_effect,
        );
        if let Some(numeric_id) = numeric_id {
            self.add_spatial_id_mapping(numeric_id, spatial_id);
            if let Some(clip_id) = clip_id {
                self.add_clip_id_mapping(numeric_id, clip_id);
            }
        }

        if let Some(yaml_items) = yaml["items"].as_vec() {
            self.spatial_id_stack.push(spatial_id);
            self.add_display_list_items_from_yaml(dl, wrench, yaml_items);
            self.spatial_id_stack.pop().unwrap();
        }
    }

    fn handle_sticky_frame(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        yaml: &Yaml,
    ) {
        let bounds = yaml["bounds"].as_rect().expect("sticky frame must have a bounds");
        let numeric_id = yaml["id"].as_i64().map(|id| id as u64);

        let real_id = dl.define_sticky_frame(
            *self.spatial_id_stack.last().unwrap(),
            bounds,
            SideOffsets2D::new(
                yaml["margin-top"].as_f32(),
                yaml["margin-right"].as_f32(),
                yaml["margin-bottom"].as_f32(),
                yaml["margin-left"].as_f32(),
            ),
            yaml["vertical-offset-bounds"].as_sticky_offset_bounds(),
            yaml["horizontal-offset-bounds"].as_sticky_offset_bounds(),
            yaml["previously-applied-offset"].as_vector().unwrap_or_else(LayoutVector2D::zero),
            None,
        );

        if let Some(numeric_id) = numeric_id {
            self.add_spatial_id_mapping(numeric_id, real_id);
        }

        if let Some(yaml_items) = yaml["items"].as_vec() {
            self.spatial_id_stack.push(real_id);
            self.add_display_list_items_from_yaml(dl, wrench, yaml_items);
            self.spatial_id_stack.pop().unwrap();
        }
    }

    fn resolve_binding<'a>(
        &'a self,
        yaml: &'a Yaml,
    ) -> &'a Yaml {
        if let Some(keyframes) = &self.keyframes {
            if let Some(s) = yaml.as_str() {
                const PREFIX: &str = "key(";
                const SUFFIX: &str = ")";
                if let Some(key) = s.strip_prefix(PREFIX).and_then(|s| s.strip_suffix(SUFFIX)) {
                    return &keyframes[key][self.requested_frame];
                }
            }
        }

        yaml
    }

    fn resolve_colorf(
        &self,
        yaml: &Yaml,
    ) -> Option<ColorF> {
        self.resolve_binding(yaml)
            .as_colorf()
    }

    fn resolve_rect(
        &self,
        yaml: &Yaml,
    ) -> LayoutRect {
        self.resolve_binding(yaml)
            .as_rect()
            .unwrap_or_else(|| panic!("invalid rect {:?}", yaml))
    }

    fn resolve_vector(
        &self,
        yaml: &Yaml,
        default: LayoutVector2D,
    ) -> LayoutVector2D {
        self.resolve_binding(yaml)
            .as_vector()
            .unwrap_or(default)
    }

    fn handle_push_shadow(
        &mut self,
        dl: &mut DisplayListBuilder,
        yaml: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        let blur_radius = yaml["blur-radius"].as_f32().unwrap_or(0.0);
        let offset = yaml["offset"].as_vector().unwrap_or_else(LayoutVector2D::zero);
        let color = yaml["color"].as_colorf().unwrap_or(ColorF::BLACK);

        dl.push_shadow(
            &SpaceAndClipInfo { spatial_id: info.spatial_id, clip_chain_id: info.clip_chain_id },
            Shadow {
                blur_radius,
                offset,
                color,
            },
            true,
        );
    }

    fn handle_pop_all_shadows(&mut self, dl: &mut DisplayListBuilder) {
        dl.pop_all_shadows();
    }

    fn handle_clip_chain(&mut self, builder: &mut DisplayListBuilder, yaml: &Yaml) {
        let numeric_id = yaml["id"].as_i64().expect("clip chains must have an id");
        let clip_ids: Vec<ClipId> = yaml["clips"]
            .as_vec_u64()
            .unwrap_or_default()
            .iter()
            .map(|id| self.user_clip_id_map[id])
            .collect();

        let parent = self.to_clip_chain_id(&yaml["parent"], builder);
        let real_id = builder.define_clip_chain(parent, clip_ids);
        self.add_clip_chain_id_mapping(numeric_id as u64, real_id);
    }

    fn handle_clip(&mut self, dl: &mut DisplayListBuilder, wrench: &mut Wrench, yaml: &Yaml) {
        let numeric_id = yaml["id"].as_i64();
        let spatial_id = self.top_space();
        let complex_clips = yaml["complex"].as_complex_clip_regions();
        let mut clip_id = None;

        if let Some(clip_rect) = yaml["bounds"].as_rect() {
            clip_id = Some(dl.define_clip_rect(
                spatial_id,
                clip_rect,
            ));
        }

        if let Some(image_mask) = self.as_image_mask(&yaml["image-mask"], wrench) {
            assert!(clip_id.is_none(), "invalid clip definition");

            clip_id = Some(dl.define_clip_image_mask(
                spatial_id,
                image_mask,
                &[],
                FillRule::Nonzero,
            ));
        }

        if !complex_clips.is_empty() {
            // Only 1 complex clip is supported per clip (todo: change yaml format)
            assert_eq!(complex_clips.len(), 1);
            assert!(clip_id.is_none(), "invalid clip definition");

            clip_id = Some(dl.define_clip_rounded_rect(
                spatial_id,
                complex_clips[0],
            ));
        }

        if let Some(clip_id) = clip_id {
            if let Some(numeric_id) = numeric_id {
                self.add_clip_id_mapping(numeric_id as u64, clip_id);
            }
        }
    }

    fn push_reference_frame(
        &mut self,
        dl: &mut DisplayListBuilder,
        default_bounds: impl Fn() -> LayoutRect,
        yaml: &Yaml,
    ) -> SpatialId {
        let bounds = yaml["bounds"].as_rect().unwrap_or_else(default_bounds);
        let default_transform_origin = LayoutPoint::new(
            bounds.min.x + bounds.width() * 0.5,
            bounds.min.y + bounds.height() * 0.5,
        );

        let transform_style = yaml["transform-style"]
            .as_transform_style()
            .unwrap_or(TransformStyle::Flat);

        let transform_origin = yaml["transform-origin"]
            .as_point()
            .unwrap_or(default_transform_origin);

        assert!(
            yaml["transform"].is_badvalue() ||
            yaml["perspective"].is_badvalue(),
            "Should have one of either transform or perspective"
        );

        let perspective_origin = yaml["perspective-origin"]
            .as_point()
            .unwrap_or(default_transform_origin);

        let is_2d = yaml["is-2d"].as_bool().unwrap_or(false);
        let should_snap = yaml["should-snap"].as_bool().unwrap_or(false);

        let reference_frame_kind = if !yaml["perspective"].is_badvalue() {
            ReferenceFrameKind::Perspective { scrolling_relative_to: None }
        } else {
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: is_2d,
                should_snap,
                paired_with_perspective: yaml["paired-with-perspective"].as_bool().unwrap_or(false),
            }
        };

        let transform = yaml["transform"]
            .as_transform(&transform_origin);

        let perspective = match yaml["perspective"].as_f32() {
            Some(value) if value != 0.0 => {
                Some(make_perspective(perspective_origin, value as f32))
            }
            Some(..) => None,
            _ => yaml["perspective"].as_matrix4d(),
        };

        let transform_value = transform.or(perspective).unwrap_or_default();

        // A `transform-binding` id makes the reference frame's transform a
        // property Binding rather than a static Value, so it is treated as
        // animating (is_ancestor_or_self_animating). The bound value is the
        // computed transform above; nothing needs to update it for the binding
        // to count as animating.
        let transform_binding = match yaml["transform-binding"].as_i64() {
            Some(id) => PropertyBinding::Binding(
                PropertyBindingKey::new(id as u64),
                transform_value,
            ),
            None => PropertyBinding::Value(transform_value),
        };

        let reference_frame_id = dl.push_reference_frame(
            bounds.min,
            *self.spatial_id_stack.last().unwrap(),
            transform_style,
            transform_binding,
            reference_frame_kind,
        );

        let numeric_id = yaml["id"].as_i64();
        if let Some(numeric_id) = numeric_id {
            self.add_spatial_id_mapping(numeric_id as u64, reference_frame_id);
        }

        reference_frame_id
    }

    fn handle_reference_frame(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        yaml: &Yaml,
    ) {
        let default_bounds = || LayoutRect::from_size(wrench.window_size_f32());
        let real_id = self.push_reference_frame(dl, default_bounds, yaml);
        self.spatial_id_stack.push(real_id);

        if let Some(yaml_items) = yaml["items"].as_vec() {
            self.add_display_list_items_from_yaml(dl, wrench, yaml_items);
        }

        self.spatial_id_stack.pop().unwrap();
        dl.pop_reference_frame();
    }

    fn push_computed_frame(
        &mut self,
        dl: &mut DisplayListBuilder,
        default_bounds: impl Fn() -> LayoutRect,
        yaml: &Yaml,
    ) -> SpatialId {
        let bounds = yaml["bounds"].as_rect().unwrap_or_else(default_bounds);

        let scale_from = yaml["scale-from"].as_size();
        let vertical_flip = yaml["vertical-flip"].as_bool().unwrap_or(false);
        let rotation = yaml["rotation"].as_rotation().unwrap_or(Rotation::Degree0);

        let reference_frame_id = dl.push_computed_frame(
            bounds.min,
            *self.spatial_id_stack.last().unwrap(),
            scale_from,
            vertical_flip,
            rotation,
        );

        let numeric_id = yaml["id"].as_i64();
        if let Some(numeric_id) = numeric_id {
            self.add_spatial_id_mapping(numeric_id as u64, reference_frame_id);
        }

        reference_frame_id
    }

    fn handle_computed_frame(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        yaml: &Yaml,
    ) {
        let default_bounds = || LayoutRect::from_size(wrench.window_size_f32());
        let real_id = self.push_computed_frame(dl, default_bounds, yaml);
        self.spatial_id_stack.push(real_id);

        if let Some(yaml_items) = yaml["items"].as_vec() {
            self.add_display_list_items_from_yaml(dl, wrench, yaml_items);
        }

        self.spatial_id_stack.pop().unwrap();
        dl.pop_reference_frame();
    }

    fn add_stacking_context_from_yaml(
        &mut self,
        dl: &mut DisplayListBuilder,
        wrench: &mut Wrench,
        yaml: &Yaml,
        IsRoot(is_root): IsRoot,
        info: &mut CommonItemProperties,
    ) {
        let default_bounds = || LayoutRect::from_size(wrench.window_size_f32());
        let mut bounds = yaml["bounds"].as_rect().unwrap_or_else(default_bounds);

        let transform_style = yaml["transform-style"]
            .as_transform_style()
            .unwrap_or(TransformStyle::Flat);

        let has_transform = !yaml["transform"].is_badvalue() || !yaml["perspective"].is_badvalue();
        let pushed_reference_frame = if has_transform || bounds.min != LayoutPoint::zero() {
            let reference_frame_id = if has_transform {
                self.push_reference_frame(dl, default_bounds, yaml)
            } else {
                let parent_spatial_id = *self.spatial_id_stack.last().unwrap();
                dl.push_reference_frame(
                    bounds.min,
                    parent_spatial_id,
                    transform_style,
                    PropertyBinding::Value(LayoutTransform::identity()),
                    ReferenceFrameKind::Transform {
                        is_2d_scale_translation: true,
                        should_snap: false,
                        paired_with_perspective: false,
                    },
                )
            };
            self.spatial_id_stack.push(reference_frame_id);
            bounds.max -= bounds.min.to_vector();
            bounds.min = LayoutPoint::zero();
            true
        } else {
            false
        };

        let clip_chain_id = self.to_clip_chain_id(&yaml["clip-chain"], dl);
        let mix_blend_mode = yaml["mix-blend-mode"]
            .as_mix_blend_mode()
            .unwrap_or(MixBlendMode::Normal);
        let raster_space = yaml["raster-space"]
            .as_raster_space()
            .unwrap_or(RasterSpace::Screen);
        let is_blend_container = yaml["blend-container"].as_bool().unwrap_or(false);
        let wraps_backdrop_filter = yaml["wraps-backdrop-filter"].as_bool().unwrap_or(false);

        if is_root {
            if let Some(vector) = yaml["scroll-offset"].as_vector() {
                let external_id = ExternalScrollId(0, dl.pipeline_id);
                // The root scroll node is an ancestor of the emulated device
                // pixel scale, so unlike scroll frames declared in the yaml its
                // offset is not scaled by the transform tree.
                let vector = if self.dppx_root_scroll_node.is_some() {
                    vector * self.device_pixel_scale
                } else {
                    vector
                };
                self.scroll_offsets.insert(
                    external_id,
                    vec![SampledScrollOffset {
                        offset: vector,
                        generation: APZScrollGeneration::default(),
                    }],
                );
            }
        }

        let filters = yaml["filters"].as_vec_filter_op().unwrap_or_default();
        let filter_datas = yaml["filter-datas"].as_vec_filter_data().unwrap_or_default();

        let snapshot = if !yaml["snapshot"].is_badvalue() {
            let yaml = &yaml["snapshot"];
            let name = yaml["name"].as_str().unwrap_or("snapshot");
            let area = yaml["area"].as_rect().unwrap_or(bounds);
            let detached = yaml["detached"].as_bool().unwrap_or(false);

            let key = SnapshotImageKey(wrench.api.generate_image_key());
            self.snapshots.insert(name.to_string(), Snapshot {
                key,
                size: bounds.size(),
            });

            let mut txn = Transaction::new();
            txn.add_snapshot_image(key);
            wrench.api.send_transaction(wrench.document_id, txn);

            Some(SnapshotInfo { key, area, detached })
        } else {
            None
        };

        let mut flags = StackingContextFlags::empty();
        flags.set(StackingContextFlags::IS_BLEND_CONTAINER, is_blend_container);
        flags.set(StackingContextFlags::WRAPS_BACKDROP_FILTER, wraps_backdrop_filter);

        dl.push_stacking_context(
            *self.spatial_id_stack.last().unwrap(),
            info.flags,
            clip_chain_id,
            transform_style,
            mix_blend_mode,
            &filters,
            &filter_datas,
            raster_space,
            flags,
            snapshot,
        );

        if let Some(yaml_items) = yaml["items"].as_vec() {
            self.add_display_list_items_from_yaml(dl, wrench, yaml_items);
        }

        dl.pop_stacking_context();

        if pushed_reference_frame {
            self.spatial_id_stack.pop().unwrap();
            dl.pop_reference_frame();
        }
    }

    fn handle_backdrop_filter(
        &mut self,
        dl: &mut DisplayListBuilder,
        item: &Yaml,
        info: &mut CommonItemProperties,
    ) {
        info.clip_rect = try_intersect!(
            self.resolve_rect(&item["bounds"]),
            &info.clip_rect
        );

        let filters = item["filters"].as_vec_filter_op().unwrap_or_default();
        let filter_datas = item["filter-datas"].as_vec_filter_data().unwrap_or_default();

        dl.push_backdrop_filter(
            info,
            &filters,
            &filter_datas,
        );
    }
}

impl WrenchThing for YamlFrameReader {
    fn do_frame(&mut self, wrench: &mut Wrench) -> u32 {
        let mut should_build_yaml = false;

        // If YAML isn't read yet, or watching source file, reload from disk.
        if self.yaml_string.is_empty() || self.watch_source {
            self.yaml_string = std::fs::read_to_string(&self.yaml_path)
                 .unwrap_or_else(|_| panic!("YAML '{:?}' doesn't exist", self.yaml_path));
            should_build_yaml = true;
        }

        // Evaluate conditions that require parsing the YAML.
        if self.built_frame != self.requested_frame {
            // Requested frame has changed
            should_build_yaml = true;
        }

        // Build the DL from YAML if required
        if should_build_yaml {
            self.build(wrench);
        }

        // Determine whether to send a new DL, or just refresh.
        if should_build_yaml || wrench.should_rebuild_display_lists() {
            wrench.begin_frame();
            wrench.send_lists(
                &mut self.frame_count,
                self.display_lists.clone(),
                &self.scroll_offsets,
                &self.transform_properties,
            );
        } else {
            wrench.refresh();
        }

        self.frame_count
    }

    fn next_frame(&mut self) {
        let mut max_frame_count = 0;
        if let Some(ref keyframes) = self.keyframes {
            for (_, values) in keyframes.as_hash().unwrap() {
                max_frame_count = max_frame_count.max(values.as_vec().unwrap().len());
            }
        }
        if self.requested_frame + 1 < max_frame_count {
            self.requested_frame += 1;
        }
    }

    fn prev_frame(&mut self) {
        if self.requested_frame > 0 {
            self.requested_frame -= 1;
        }
    }
}
