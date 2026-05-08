/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Generic types for color properties.

use crate::color::ColorMixItemList;
use crate::color::{mix::ColorInterpolationMethod, AbsoluteColor, ColorFunction};
use crate::derives::*;
use crate::values::{
    computed::ToComputedValue, specified::percentage::ToPercentage, ParseError, Parser,
};
use std::fmt::{self, Write};
use style_traits::{owned_slice::OwnedSlice, CssWriter, ToCss};

/// This struct represents a combined color from a numeric color and
/// the current foreground color (currentcolor keyword).
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToAnimatedValue, ToShmem, ToTyped)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub enum GenericColor<Percentage> {
    /// The actual numeric color.
    Absolute(AbsoluteColor),
    /// A unresolvable color.
    ColorFunction(Box<ColorFunction<Self>>),
    /// The `CurrentColor` keyword.
    CurrentColor,
    /// The color-mix() function.
    ColorMix(Box<GenericColorMix<Self, Percentage>>),
    /// The contrast-color() function.
    ContrastColor(Box<Self>),
}

/// Flags used to modify the calculation of a color mix result.
#[derive(Clone, Copy, Debug, Default, MallocSizeOf, PartialEq, ToShmem)]
#[repr(C)]
pub struct ColorMixFlags(u8);
bitflags! {
    impl ColorMixFlags : u8 {
        /// Normalize the weights of the mix.
        const NORMALIZE_WEIGHTS = 1 << 0;
        /// The result should always be converted to the modern color syntax.
        const RESULT_IN_MODERN_SYNTAX = 1 << 1;
    }
}

/// One `(color, percentage)` component of a `color-mix()` expression.
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[allow(missing_docs)]
#[repr(C)]
pub struct GenericColorMixItem<Color, Percentage> {
    pub color: Color,
    pub percentage: Percentage,
}

/// A restricted version of the css `color-mix()` function, which only supports
/// percentages.
///
/// https://drafts.csswg.org/css-color-5/#color-mix
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[allow(missing_docs)]
#[repr(C)]
pub struct GenericColorMix<Color, Percentage> {
    pub interpolation: ColorInterpolationMethod,
    pub items: OwnedSlice<GenericColorMixItem<Color, Percentage>>,
    pub flags: ColorMixFlags,
}

pub use self::GenericColorMix as ColorMix;

impl<Color: ToCss, Percentage: ToCss + ToPercentage> ToCss for ColorMix<Color, Percentage> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str("color-mix(")?;

        // If the color interpolation method is oklab (which is now the default),
        // it can be omitted.
        // See: https://github.com/web-platform-tests/interop/issues/1166
        if !self.interpolation.is_default() {
            self.interpolation.to_css(dest)?;
            dest.write_str(", ")?;
        }

        let uniform = self
            .items
            .split_first()
            .map(|(first, rest)| {
                rest.iter()
                    .all(|item| item.percentage.to_percentage() == first.percentage.to_percentage())
            })
            .unwrap_or(false);
        let uniform_value = 1.0 / self.items.len() as f32;

        let is_pair = self.items.len() == 2;

        for (index, item) in self.items.iter().enumerate() {
            if index != 0 {
                dest.write_str(", ")?;
            }

            item.color.to_css(dest)?;

            let omit = if is_pair {
                let can_omit = |a: &Percentage, b: &Percentage, is_left| {
                    if a.is_calc() {
                        return false;
                    }
                    // Percentages are enforced to be resolvable at parse time for the specified
                    // colors, and are already resolved for computed colors.
                    let a = a.to_percentage().unwrap();
                    let b = b.to_percentage().unwrap();
                    if a == 0.5 {
                        return b == 0.5;
                    }
                    if is_left {
                        return false;
                    }
                    (1.0 - a - b).abs() <= f32::EPSILON
                };

                let other = &self.items[1 - index].percentage;
                can_omit(&item.percentage, other, index == 0)
            } else {
                !item.percentage.is_calc()
                    && uniform
                    && item.percentage.to_percentage() == Some(uniform_value)
            };

            if !omit {
                dest.write_char(' ')?;
                item.percentage.to_css(dest)?;
            }
        }

        dest.write_char(')')
    }
}

impl<Percentage> ColorMix<GenericColor<Percentage>, Percentage> {
    /// Mix the colors so that we get a single color. If any of the 2 colors are
    /// not mixable (perhaps not absolute?), then return None.
    pub fn mix_to_absolute(&self) -> Option<AbsoluteColor>
    where
        Percentage: ToPercentage,
    {
        use crate::color::mix;

        let mut items = ColorMixItemList::with_capacity(self.items.len());
        for item in self.items.iter() {
            items.push(mix::ColorMixItem::new(
                *item.color.as_absolute()?,
                item.percentage.to_percentage()?,
            ))
        }

        Some(mix::mix_many(self.interpolation, items, self.flags))
    }
}

pub use self::GenericColor as Color;

impl<Percentage> Color<Percentage> {
    /// If this color is absolute return it's value, otherwise return None.
    pub fn as_absolute(&self) -> Option<&AbsoluteColor> {
        match *self {
            Self::Absolute(ref absolute) => Some(absolute),
            _ => None,
        }
    }

    /// Returns a color value representing currentcolor.
    pub fn currentcolor() -> Self {
        Self::CurrentColor
    }

    /// Whether it is a currentcolor value (no numeric color component).
    pub fn is_currentcolor(&self) -> bool {
        matches!(*self, Self::CurrentColor)
    }

    /// Whether this color is an absolute color.
    pub fn is_absolute(&self) -> bool {
        matches!(*self, Self::Absolute(..))
    }
}

/// Either `<color>` or `auto`.
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    PartialEq,
    Parse,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToCss,
    ToShmem,
    ToTyped,
)]
#[repr(C, u8)]
pub enum GenericColorOrAuto<C> {
    /// A `<color>`.
    Color(C),
    /// `auto`
    Auto,
}

pub use self::GenericColorOrAuto as ColorOrAuto;

/// Caret color is effectively a ColorOrAuto, but resolves `auto` to
/// currentColor.
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToShmem,
    ToTyped,
)]
#[repr(transparent)]
pub struct GenericCaretColor<C>(pub GenericColorOrAuto<C>);

impl<C> GenericCaretColor<C> {
    /// Returns the `auto` value.
    pub fn auto() -> Self {
        GenericCaretColor(GenericColorOrAuto::Auto)
    }
}

pub use self::GenericCaretColor as CaretColor;

/// A light-dark(<light>, <dark>) function.
#[derive(
    Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToShmem, ToCss, ToResolvedValue,
)]
#[css(function = "light-dark", comma)]
#[repr(C)]
pub struct GenericLightDark<T> {
    /// The value returned when using a light theme.
    pub light: T,
    /// The value returned when using a dark theme.
    pub dark: T,
}

impl<T> GenericLightDark<T> {
    /// Parse the arguments of the light-dark() function.
    pub fn parse_args_with<'i>(
        input: &mut Parser<'i, '_>,
        mut parse_one: impl FnMut(&mut Parser<'i, '_>) -> Result<T, ParseError<'i>>,
    ) -> Result<Self, ParseError<'i>> {
        let light = parse_one(input)?;
        input.expect_comma()?;
        let dark = parse_one(input)?;
        Ok(Self { light, dark })
    }

    /// Parse the light-dark() function.
    pub fn parse_with<'i>(
        input: &mut Parser<'i, '_>,
        parse_one: impl FnMut(&mut Parser<'i, '_>) -> Result<T, ParseError<'i>>,
    ) -> Result<Self, ParseError<'i>> {
        input.expect_function_matching("light-dark")?;
        input.parse_nested_block(|input| Self::parse_args_with(input, parse_one))
    }
}

impl<T: ToComputedValue> GenericLightDark<T> {
    /// Choose the light or dark version of this value for computation purposes, and compute it.
    pub fn compute(&self, cx: &crate::values::computed::Context) -> T::ComputedValue {
        let dark = cx.device().is_dark_color_scheme(cx.builder.color_scheme);
        if cx.for_non_inherited_property {
            cx.rule_cache_conditions
                .borrow_mut()
                .set_color_scheme_dependency(cx.builder.color_scheme);
        }
        let chosen = if dark { &self.dark } else { &self.light };
        chosen.to_computed_value(cx)
    }
}
