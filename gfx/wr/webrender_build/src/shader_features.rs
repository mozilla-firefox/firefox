/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::HashMap;

bitflags! {
    #[derive(Default, Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct ShaderFeatureFlags: u32 {
        const GL = 1 << 0;
        const GLES = 1 << 1;

        const ADVANCED_BLEND_EQUATION = 1 << 8;
        const DUAL_SOURCE_BLENDING = 1 << 9;
        const DITHERING = 1 << 10;
        const TEXTURE_EXTERNAL = 1 << 11;
        const TEXTURE_EXTERNAL_ESSL1 = 1 << 12;
        const TEXTURE_EXTERNAL_BT709 = 1 << 13;
        const DEBUG = 1 << 14;
        /// Build the analytical variant of cs_path_tile in addition to the
        /// ssaa4 and ssaa8 ones.
        const PATH_AA_ALL_MODES = 1 << 15;
    }
}

pub type ShaderFeatures = HashMap<&'static str, Vec<String>>;

/// Builder for a list of features.
#[derive(Clone)]
struct FeatureList<'a> {
    list: Vec<&'a str>,
}

impl<'a> FeatureList<'a> {
    fn new() -> Self {
        FeatureList {
            list: Vec::new(),
        }
    }

    fn add(&mut self, feature: &'a str) {
        assert!(!feature.contains(','));
        self.list.push(feature);
    }

    fn with(&self, feature: &'a str) -> Self {
        let mut other = self.clone();
        other.add(feature);
        other
    }

    fn concat(&self, other: &Self) -> Self {
        let mut list = self.list.clone();
        list.extend_from_slice(&other.list);
        FeatureList {
            list
        }
    }

    fn finish(&mut self) -> String {
        self.list.sort_unstable();
        self.list.join(",")
    }
}

/// Computes available shaders and their features for the given feature flags.
pub fn get_shader_features(flags: ShaderFeatureFlags) -> ShaderFeatures {
    let mut shaders = ShaderFeatures::new();

    // Cache shaders
    shaders.insert("cs_blur", vec!["ALPHA_TARGET".to_string(), "COLOR_TARGET".to_string()]);

    shaders.insert("ps_quad_mask", vec![String::new(), "FAST_PATH".to_string(), "SUPERELLIPSE".to_string()]);

    for name in &[
        "cs_line_decoration",
        "cs_svg_filter_node",
    ] {
        shaders.insert(name, vec![String::new()]);
    }

    let mut path_tile_features = vec!["PATH_SSAA4".to_string(), "PATH_SSAA8".to_string()];
    if flags.contains(ShaderFeatureFlags::PATH_AA_ALL_MODES) {
        path_tile_features.push(String::new());
    }
    shaders.insert("cs_path_tile", path_tile_features);

    for name in &[
        "cs_border_segment",
        "cs_border_solid",
    ] {
        shaders.insert(name, vec![String::new(), "SUPERELLIPSE".to_string()]);
    }

    let mut base_prim_features = FeatureList::new();

    let mut texture_types = vec!["TEXTURE_2D"];
    if flags.contains(ShaderFeatureFlags::GL) {
        texture_types.push("TEXTURE_RECT");
    }
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL) {
        texture_types.push("TEXTURE_EXTERNAL");
    }
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL_BT709) {
        texture_types.push("TEXTURE_EXTERNAL_BT709");
    }

    let mut composite_texture_types = texture_types.clone();
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL_ESSL1) {
        composite_texture_types.push("TEXTURE_EXTERNAL_ESSL1");
    }
    let mut composite_features: Vec<String> = Vec::new();
    for texture_type in &composite_texture_types {
        let base = texture_type.to_string();
        composite_features.push(base);
    }
    shaders.insert("cs_scale", composite_features.clone());

    // YUV composite shaders
    for texture_type in &texture_types {
        let mut list = FeatureList::new();
        if !texture_type.is_empty() {
            list.add(texture_type);
        }
        list.add("YUV");
        composite_features.push(list.finish());
    }

    // Fast path composite shaders
    for texture_type in &composite_texture_types {
        let mut list = FeatureList::new();
        if !texture_type.is_empty() {
            list.add(texture_type);
        }
        list.add("FAST_PATH");
        composite_features.push(list.finish());

        // YUV shaders are not compatible with ESSL1
        if *texture_type == "TEXTURE_EXTERNAL_ESSL1" {
            continue;
        }

        list.add("YUV");
        composite_features.push(list.finish());
    }
    shaders.insert("composite", composite_features);

    // Prim shaders
    let mut text_types = vec![""];
    if flags.contains(ShaderFeatureFlags::DUAL_SOURCE_BLENDING) {
        text_types.push("DUAL_SOURCE_BLENDING");
    }
    let mut text_features: Vec<String> = Vec::new();
    for text_type in &text_types {
        let mut list = base_prim_features.with("TEXTURE_2D");
        if !text_type.is_empty() {
            list.add(text_type);
        }
        let mut alpha_list = list.with("ALPHA_PASS");
        text_features.push(alpha_list.finish());
        text_features.push(alpha_list.with("GLYPH_TRANSFORM").finish());
        text_features.push(list.with("DEBUG_OVERDRAW").finish());
    }
    shaders.insert("ps_text_run", text_features);

    shaders.insert("ps_split_composite", vec![base_prim_features.finish()]);

    // ps_quad_textured needs per-texture-kind variants so that external image
    // sources (e.g. ANGLE DXGI textures) are sampled with the correct sampler
    // type.
    let mut ps_quad_textured_features: Vec<String> = vec!["TEXTURE_2D".to_string()];
    if flags.contains(ShaderFeatureFlags::GL) {
        ps_quad_textured_features.push("TEXTURE_RECT".to_string());
    }
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL) {
        ps_quad_textured_features.push("TEXTURE_EXTERNAL".to_string());
    }
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL_BT709) {
        ps_quad_textured_features.push("TEXTURE_EXTERNAL_BT709".to_string());
    }
    shaders.insert("ps_quad_textured", ps_quad_textured_features);

    shaders.insert("ps_quad_repeat", vec![base_prim_features.finish()]);

    shaders.insert("ps_quad_box_shadow", vec![base_prim_features.finish(), "SUPERELLIPSE".to_string()]);

    // Like ps_quad_textured, ps_quad_yuv needs per-texture-kind variants so that
    // the YUV planes are sampled with the correct sampler type.
    let mut ps_quad_yuv_features: Vec<String> = vec!["TEXTURE_2D".to_string()];
    if flags.contains(ShaderFeatureFlags::GL) {
        ps_quad_yuv_features.push("TEXTURE_RECT".to_string());
    }
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL) {
        ps_quad_yuv_features.push("TEXTURE_EXTERNAL".to_string());
    }
    if flags.contains(ShaderFeatureFlags::TEXTURE_EXTERNAL_BT709) {
        ps_quad_yuv_features.push("TEXTURE_EXTERNAL_BT709".to_string());
    }
    shaders.insert("ps_quad_yuv", ps_quad_yuv_features);

    shaders.insert("ps_quad_backdrop", vec!["TEXTURE_2D".to_string()]);

    shaders.insert("ps_quad_blend", vec!["TEXTURE_2D".to_string()]);

    shaders.insert("ps_quad_mix_blend", vec!["TEXTURE_2D".to_string()]);

    let mut maybe_dithering = FeatureList::new();
    if flags.contains(ShaderFeatureFlags::DITHERING) {
        maybe_dithering.add("DITHERING");
    }

    shaders.insert("ps_quad_gradient", vec![base_prim_features.concat(&maybe_dithering).finish()]);

    shaders.insert("ps_clear", vec![base_prim_features.finish()]);

    shaders.insert("ps_copy", vec![base_prim_features.finish()]);

    if flags.contains(ShaderFeatureFlags::DEBUG) {
        for name in &["debug_color", "debug_font"] {
            shaders.insert(name, vec![String::new()]);
        }
    }

    shaders
}
