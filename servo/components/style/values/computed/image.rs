/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the computed value of
//! [`image`][image]s
//!
//! [image]: https://drafts.csswg.org/css-images/#image-values

use crate::derives::*;
use crate::values::computed::percentage::Percentage;
use crate::values::computed::position::Position;
use crate::values::computed::url::ComputedUrl;
use crate::values::computed::{Angle, Color, Context};
use crate::values::computed::{
    AngleOrPercentage, Length, LengthPercentage, NonNegativeLength, NonNegativeLengthPercentage,
    Resolution, ToComputedValue,
};
use crate::values::generics::image::{self as generic, GradientCompatMode};
use crate::values::specified::image as specified;
use crate::values::specified::position::{HorizontalPositionKeyword, VerticalPositionKeyword};
use std::f32::consts::PI;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ToCss};

pub use specified::ImageRendering;

/// Computed values for an image according to CSS-IMAGES.
/// <https://drafts.csswg.org/css-images/#image-values>
pub type Image = generic::GenericImage<Gradient, ComputedUrl, Color, Percentage, Resolution>;

// Images should remain small, see https://github.com/servo/servo/pull/18430
#[cfg(feature = "gecko")]
size_of_test!(Image, 16);
#[cfg(feature = "servo")]
size_of_test!(Image, 24);

/// Computed values for a CSS gradient.
/// <https://drafts.csswg.org/css-images/#gradients>
pub type Gradient = generic::GenericGradient<
    LineDirection,
    Length,
    LengthPercentage,
    Position,
    Angle,
    AngleOrPercentage,
    Color,
>;

/// Computed values for CSS cross-fade
/// <https://drafts.csswg.org/css-images-4/#cross-fade-function>
pub type CrossFade = generic::CrossFade<Image, Color, Percentage>;

/// A computed radial gradient ending shape.
pub type EndingShape = generic::GenericEndingShape<NonNegativeLength, NonNegativeLengthPercentage>;

/// A computed gradient line direction.
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq, ToResolvedValue)]
#[repr(C, u8)]
pub enum LineDirection {
    /// An angle.
    Angle(Angle),
    /// A horizontal direction.
    Horizontal(HorizontalPositionKeyword),
    /// A vertical direction.
    Vertical(VerticalPositionKeyword),
    /// A corner.
    Corner(HorizontalPositionKeyword, VerticalPositionKeyword),
}

/// The computed value for an `image-set()` image.
pub type ImageSet = generic::GenericImageSet<Image, Resolution>;

impl ToComputedValue for specified::ImageSet {
    type ComputedValue = ImageSet;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        let items = self.items.to_computed_value(context);
        let dpr = context.device().device_pixel_ratio().get();

        let mut supported_image = false;
        let mut selected_index = std::usize::MAX;
        let mut selected_resolution = 0.0;

        for (i, item) in items.iter().enumerate() {
            if item.has_mime_type && !context.device().is_supported_mime_type(&item.mime_type) {
                // If the MIME type is not supported, we discard the ImageSetItem.
                continue;
            }

            let candidate_resolution = item.resolution.dppx();
            debug_assert!(
                candidate_resolution >= 0.0,
                "Resolutions should be non-negative"
            );
            if candidate_resolution == 0.0 {
                // If the resolution is 0, we also treat it as an invalid image.
                continue;
            }

            // https://drafts.csswg.org/css-images-4/#image-set-notation:
            //
            //     Make a UA-specific choice of which to load, based on whatever criteria deemed
            //     relevant (such as the resolution of the display, connection speed, etc).
            //
            // For now, select the lowest resolution greater than display density, otherwise the
            // greatest resolution available.
            let better_candidate = || {
                if selected_resolution < dpr && candidate_resolution > selected_resolution {
                    return true;
                }
                if candidate_resolution < selected_resolution && candidate_resolution >= dpr {
                    return true;
                }
                false
            };

            // The first item with a supported MIME type is obviously the current best candidate
            if !supported_image || better_candidate() {
                supported_image = true;
                selected_index = i;
                selected_resolution = candidate_resolution;
            }
        }

        ImageSet {
            selected_index,
            items,
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self {
            selected_index: std::usize::MAX,
            items: ToComputedValue::from_computed_value(&computed.items),
        }
    }
}

impl generic::LineDirection for LineDirection {
    fn points_downwards(&self, compat_mode: GradientCompatMode) -> bool {
        match *self {
            LineDirection::Angle(angle) => angle.radians() == PI,
            LineDirection::Vertical(VerticalPositionKeyword::Bottom) => {
                compat_mode == GradientCompatMode::Modern
            },
            LineDirection::Vertical(VerticalPositionKeyword::Top) => {
                compat_mode != GradientCompatMode::Modern
            },
            _ => false,
        }
    }

    fn to_css<W>(&self, dest: &mut CssWriter<W>, compat_mode: GradientCompatMode) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            LineDirection::Angle(ref angle) => angle.to_css(dest),
            LineDirection::Horizontal(x) => {
                if compat_mode == GradientCompatMode::Modern {
                    dest.write_str("to ")?;
                }
                x.to_css(dest)
            },
            LineDirection::Vertical(y) => {
                if compat_mode == GradientCompatMode::Modern {
                    dest.write_str("to ")?;
                }
                y.to_css(dest)
            },
            LineDirection::Corner(x, y) => {
                if compat_mode == GradientCompatMode::Modern {
                    dest.write_str("to ")?;
                }
                x.to_css(dest)?;
                dest.write_char(' ')?;
                y.to_css(dest)
            },
        }
    }
}

impl ToComputedValue for specified::LineDirection {
    type ComputedValue = LineDirection;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            specified::LineDirection::Angle(ref angle) => {
                LineDirection::Angle(angle.to_computed_value(context))
            },
            specified::LineDirection::Horizontal(x) => LineDirection::Horizontal(x),
            specified::LineDirection::Vertical(y) => LineDirection::Vertical(y),
            specified::LineDirection::Corner(x, y) => LineDirection::Corner(x, y),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        match *computed {
            LineDirection::Angle(ref angle) => {
                specified::LineDirection::Angle(ToComputedValue::from_computed_value(angle))
            },
            LineDirection::Horizontal(x) => specified::LineDirection::Horizontal(x),
            LineDirection::Vertical(y) => specified::LineDirection::Vertical(y),
            LineDirection::Corner(x, y) => specified::LineDirection::Corner(x, y),
        }
    }
}

impl ToComputedValue for specified::Image {
    type ComputedValue = Image;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self {
            Self::None => Image::None,
            Self::Url(u) => Image::Url(u.to_computed_value(context)),
            Self::Gradient(g) => Image::Gradient(g.to_computed_value(context)),
            #[cfg(feature = "gecko")]
            Self::Element(e) => Image::Element(e.to_computed_value(context)),
            #[cfg(feature = "gecko")]
            Self::MozSymbolicIcon(e) => Image::MozSymbolicIcon(e.to_computed_value(context)),
            #[cfg(feature = "servo")]
            Self::PaintWorklet(w) => Image::PaintWorklet(w.to_computed_value(context)),
            Self::CrossFade(f) => Image::CrossFade(f.to_computed_value(context)),
            Self::ImageSet(s) => Image::ImageSet(s.to_computed_value(context)),
            Self::Image(color) => Image::Image(color.to_computed_value(context)),
            Self::LightDark(ld) => match ld.compute(context) {
                // none computes to image(transparent) in light-dark(), see
                // https://github.com/w3c/csswg-drafts/issues/13866.
                Image::None => Image::Image(Color::TRANSPARENT_BLACK.into()),
                other => other,
            },
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        match computed {
            Image::None => Self::None,
            Image::Url(u) => Self::Url(ToComputedValue::from_computed_value(u)),
            Image::Gradient(g) => Self::Gradient(ToComputedValue::from_computed_value(g)),
            #[cfg(feature = "gecko")]
            Image::Element(e) => Self::Element(ToComputedValue::from_computed_value(e)),
            #[cfg(feature = "gecko")]
            Image::MozSymbolicIcon(e) => {
                Self::MozSymbolicIcon(ToComputedValue::from_computed_value(e))
            },
            #[cfg(feature = "servo")]
            Image::PaintWorklet(w) => Self::PaintWorklet(ToComputedValue::from_computed_value(w)),
            Image::CrossFade(f) => Self::CrossFade(ToComputedValue::from_computed_value(f)),
            Image::ImageSet(s) => Self::ImageSet(ToComputedValue::from_computed_value(s)),
            Image::Image(color) => Self::Image(ToComputedValue::from_computed_value(color)),
            Image::LightDark(_) => unreachable!("Shouldn't have computed image-set values"),
        }
    }
}
