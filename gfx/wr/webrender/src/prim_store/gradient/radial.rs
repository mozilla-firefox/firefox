/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Radial gradients
//!
//! Specification: https://drafts.csswg.org/css-images-4/#radial-gradients
//!
//! Radial gradients are rendered as quads with the gradient pattern (ps_quad_gradient).

use api::{AlphaMode, ExtendMode, GradientStop};
use api::units::*;
use crate::pattern::gradient::{radial_gradient_pattern};
use crate::pattern::{Pattern, PatternBuilder, PatternBuilderState};
use crate::dl_interner::{DlResolve, DlResolveContext};
use crate::prim_store::PrimitiveOpacity;
use crate::prim_store::PrimTemplateCommonData;
use crate::prim_store::NinePatchDescriptor;

use std::ops::{Deref, DerefMut};
use super::stops_and_min_alpha;

// `RadialGradientParams` now lives in `webrender_api::key_types` so builder-side
// interning keys can reference it. Re-exported to keep existing references
// working.
pub use api::key_types::RadialGradientParams;

// `RadialGradientKey` lives in `webrender_api::interned_prims` (alongside the
// `RadialGradient` value), since the content display list builder interns
// them. The frame-time `RadialGradientTemplate` stays here.
pub use api::interned_prims::RadialGradientKey;

impl crate::prim_store::PrimKeyCommon for RadialGradientKey {
    fn key_common(&self) -> &crate::prim_store::PrimKeyCommonData {
        &self.common
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf, Clone)]
#[derive(Debug)]
pub struct RadialGradientTemplate {
    pub common: PrimTemplateCommonData,
    pub extend_mode: ExtendMode,
    pub alpha_mode: AlphaMode,
    pub params: RadialGradientParams,
    pub center: LayoutVector2D,
    /// Per-axis fraction of `common.prim_size` covered by one tile of the
    /// gradient pattern. Multiply by `common.prim_size` at use to recover the
    /// absolute stretch_size.
    pub stretch_ratio: LayoutSize,
    pub tile_spacing: LayoutSize,
    pub border_nine_patch: Option<Box<NinePatchDescriptor>>,
    pub stops_opacity: PrimitiveOpacity,
    pub stops: Vec<GradientStop>,
}

impl PatternBuilder for RadialGradientTemplate {
    fn build(
        &self,
        pattern_rect: &LayoutRect,
        state: &mut PatternBuilderState,
    ) -> Pattern {
        radial_gradient_pattern(
            pattern_rect.min + self.center,
            self.params.start_radius,
            self.params.end_radius,
            self.params.ratio_xy,
            self.extend_mode,
            self.alpha_mode,
            &self.stops,
            state.frame_gpu_data,
        )
    }
}

impl Deref for RadialGradientTemplate {
    type Target = PrimTemplateCommonData;
    fn deref(&self) -> &Self::Target {
        &self.common
    }
}

impl DerefMut for RadialGradientTemplate {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.common
    }
}

/// Nothing to validate: a gradient names no resource.
impl DlResolve<RadialGradientKey> for RadialGradientTemplate {
    fn resolve(item: &RadialGradientKey, _ctx: &DlResolveContext) -> Self {
        let common = PrimTemplateCommonData::with_key_common(item.common);

        let (stops, min_alpha) = stops_and_min_alpha(&item.stops);

        // Save opacity of the stops for use in
        // selecting which pass this gradient
        // should be drawn in.
        let stops_opacity = PrimitiveOpacity::from_alpha(min_alpha);

        RadialGradientTemplate {
            common,
            center: item.center.into(),
            extend_mode: item.extend_mode,
            alpha_mode: item.alpha_mode,
            params: item.params.clone(),
            stretch_ratio: item.stretch_ratio.into(),
            tile_spacing: item.tile_spacing.into(),
            border_nine_patch: item.nine_patch.clone(),
            stops_opacity,
            stops,
        }
    }
}


