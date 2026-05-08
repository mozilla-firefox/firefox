/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Computed types for box properties.

use crate::derives::*;
use crate::values::animated::{Animate, Procedure, ToAnimatedValue};
use crate::values::computed::length::{LengthPercentage, NonNegativeLength};
use crate::values::computed::{Context, Integer, Number, ToComputedValue};
use crate::values::generics::box_::{
    GenericBaselineShift, GenericContainIntrinsicSize, GenericLineClamp, GenericOverflowClipMargin,
    GenericPerspective,
};
use crate::values::specified::box_ as specified;
use std::fmt;
use style_traits::{CssWriter, ToCss};

pub use crate::values::specified::box_::{
    AlignmentBaseline, Appearance, BaselineSource, BreakBetween, BreakWithin, Clear, Contain,
    ContainerName, ContainerType, ContentVisibility, Display, DominantBaseline, Float, Overflow,
    OverflowAnchor, OverscrollBehavior, PositionProperty, ScrollSnapAlign, ScrollSnapAxis,
    ScrollSnapStop, ScrollSnapStrictness, ScrollSnapType, ScrollbarGutter, TouchAction, WillChange,
    WritingModeProperty,
};

/// A computed value for the `baseline-shift` property.
pub type BaselineShift = GenericBaselineShift<LengthPercentage>;

/// A computed value for the `overflow-clip-margin` property.
pub type OverflowClipMargin = GenericOverflowClipMargin<NonNegativeLength>;

/// A computed value for the `contain-intrinsic-size` property.
pub type ContainIntrinsicSize = GenericContainIntrinsicSize<NonNegativeLength>;

impl ContainIntrinsicSize {
    /// Converts contain-intrinsic-size to auto style.
    pub fn add_auto_if_needed(&self) -> Option<Self> {
        Some(match *self {
            Self::None => Self::AutoNone,
            Self::Length(ref l) => Self::AutoLength(*l),
            Self::AutoNone | Self::AutoLength(..) => return None,
        })
    }
}

/// A computed value for the `line-clamp` property.
pub type LineClamp = GenericLineClamp<Integer>;

impl Animate for LineClamp {
    #[inline]
    fn animate(&self, other: &Self, procedure: Procedure) -> Result<Self, ()> {
        if self.is_none() != other.is_none() {
            return Err(());
        }
        if self.is_none() {
            return Ok(Self::none());
        }
        Ok(Self(self.0.animate(&other.0, procedure)?.max(1)))
    }
}

/// A computed value for the `perspective` property.
pub type Perspective = GenericPerspective<NonNegativeLength>;

/// A computed value for the `resize` property.
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone, Copy, Debug, Eq, Hash, MallocSizeOf, Parse, PartialEq, ToCss, ToResolvedValue, ToTyped,
)]
#[repr(u8)]
pub enum Resize {
    None,
    Both,
    Horizontal,
    Vertical,
}

impl ToComputedValue for specified::Resize {
    type ComputedValue = Resize;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Resize {
        let is_vertical = context.style().writing_mode.is_vertical();
        match self {
            specified::Resize::Inline => {
                context
                    .rule_cache_conditions
                    .borrow_mut()
                    .set_writing_mode_dependency(context.builder.writing_mode);
                if is_vertical {
                    Resize::Vertical
                } else {
                    Resize::Horizontal
                }
            },
            specified::Resize::Block => {
                context
                    .rule_cache_conditions
                    .borrow_mut()
                    .set_writing_mode_dependency(context.builder.writing_mode);
                if is_vertical {
                    Resize::Horizontal
                } else {
                    Resize::Vertical
                }
            },
            specified::Resize::None => Resize::None,
            specified::Resize::Both => Resize::Both,
            specified::Resize::Horizontal => Resize::Horizontal,
            specified::Resize::Vertical => Resize::Vertical,
        }
    }

    #[inline]
    fn from_computed_value(computed: &Resize) -> specified::Resize {
        match computed {
            Resize::None => specified::Resize::None,
            Resize::Both => specified::Resize::Both,
            Resize::Horizontal => specified::Resize::Horizontal,
            Resize::Vertical => specified::Resize::Vertical,
        }
    }
}

/// The computed `zoom` property value.
#[derive(
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    PartialEq,
    PartialOrd,
    ToResolvedValue,
    ToTyped,
)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[repr(C)]
pub struct Zoom(f32);

impl ToComputedValue for specified::Zoom {
    type ComputedValue = Zoom;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        let c = match *self {
            Self::Normal => return Zoom::ONE,
            Self::Document => return Zoom::DOCUMENT,
            Self::Value(ref n) => n.0.to_computed_value(context),
        };
        let n = match c {
            super::NumberOrPercentage::Percentage(p) => p.0,
            super::NumberOrPercentage::Number(n) => n,
        };
        if n == 0.0 {
            // For legacy reasons, zoom: 0 (and 0%) computes to 1. ¯\_(ツ)_/¯
            return Zoom::ONE;
        }
        Zoom(n)
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self::new_number(computed.value())
    }
}

impl ToCss for Zoom {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        use std::fmt::Write;
        if *self == Self::DOCUMENT {
            return dest.write_str("document");
        }
        self.value().to_css(dest)
    }
}

impl ToAnimatedValue for Zoom {
    type AnimatedValue = Number;

    #[inline]
    fn to_animated_value(self, _: &crate::values::animated::Context) -> Self::AnimatedValue {
        self.value()
    }

    #[inline]
    fn from_animated_value(animated: Self::AnimatedValue) -> Self {
        Zoom(animated.max(0.0))
    }
}

impl Zoom {
    /// The value 1. This is by far the most common value.
    pub const ONE: Zoom = Zoom(1.0);

    /// The `document` value. This can appear in the computed zoom property value, but not in the
    /// `effective_zoom` field.
    pub const DOCUMENT: Zoom = Zoom(0.0);

    /// Returns whether we're the number 1.
    #[inline]
    pub fn is_one(self) -> bool {
        self == Self::ONE
    }

    /// Returns whether we're the `document` keyword.
    #[inline]
    pub fn is_document(self) -> bool {
        self == Self::DOCUMENT
    }

    /// Returns the inverse of our value.
    #[inline]
    pub fn inverted(&self) -> Option<Self> {
        if self.0 == 0.0 {
            return None;
        }
        Some(Self(1. / self.0))
    }

    /// Returns the value as a float.
    #[inline]
    pub fn value(&self) -> f32 {
        self.0
    }

    /// Computes the effective zoom for a given new zoom value in rhs.
    pub fn compute_effective(self, specified: Self) -> Self {
        if specified == Self::DOCUMENT {
            return Self::ONE;
        }
        if self == Self::ONE {
            return specified;
        }
        if specified == Self::ONE {
            return self;
        }
        Zoom(self.0 * specified.0)
    }

    /// Returns the zoomed value.
    #[inline]
    pub fn zoom(self, value: f32) -> f32 {
        if self == Self::ONE {
            return value;
        }
        value * self.value()
    }

    /// Returns the un-zoomed value.
    #[inline]
    pub fn unzoom(self, value: f32) -> f32 {
        // Avoid division by zero if our effective zoom computation ends up being zero.
        if self == Self::ONE || self.0 == 0.0 {
            return value;
        }
        value / self.value()
    }
}
