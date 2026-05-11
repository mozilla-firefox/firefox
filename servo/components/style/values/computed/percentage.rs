/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Computed percentages.

use crate::derives::*;
use crate::values::generics::{ClampToNonNegative, NonNegative};
use crate::values::specified::percentage::ToPercentage;
use crate::values::{reify_percentage, serialize_normalized_percentage, CSSFloat};
use crate::Zero;
use std::fmt;
use style_traits::{CssWriter, ToCss, ToTyped, TypedValue};
use thin_vec::ThinVec;

/// A computed percentage.
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Default,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    PartialOrd,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct Percentage(pub CSSFloat);

impl ClampToNonNegative for Percentage {
    #[inline]
    fn clamp_to_non_negative(self) -> Self {
        Percentage(self.0.max(0.))
    }
}

impl Percentage {
    /// 100%
    #[inline]
    pub fn hundred() -> Self {
        Percentage(1.)
    }

    /// Returns the absolute value for this percentage.
    #[inline]
    pub fn abs(&self) -> Self {
        Percentage(self.0.abs())
    }
}

impl Zero for Percentage {
    fn zero() -> Self {
        Percentage(0.)
    }

    fn is_zero(&self) -> bool {
        self.0 == 0.
    }
}

impl ToPercentage for Percentage {
    fn to_percentage(&self) -> Option<CSSFloat> {
        Some(self.0)
    }
}

impl std::ops::AddAssign for Percentage {
    fn add_assign(&mut self, other: Self) {
        self.0 += other.0
    }
}

impl std::ops::Add for Percentage {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Percentage(self.0 + other.0)
    }
}

impl std::ops::Sub for Percentage {
    type Output = Self;

    fn sub(self, other: Self) -> Self {
        Percentage(self.0 - other.0)
    }
}

impl std::ops::Rem for Percentage {
    type Output = Self;

    fn rem(self, other: Self) -> Self {
        Percentage(self.0 % other.0)
    }
}

impl ToCss for Percentage {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        serialize_normalized_percentage(self.0, dest)
    }
}

impl ToTyped for Percentage {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        reify_percentage(self.0, /* was_calc = */ false, dest)
    }
}

/// A wrapper over a `Percentage`, whose value should be clamped to 0.
pub type NonNegativePercentage = NonNegative<Percentage>;

impl NonNegativePercentage {
    /// 100%
    #[inline]
    pub fn hundred() -> Self {
        NonNegative(Percentage::hundred())
    }
}
