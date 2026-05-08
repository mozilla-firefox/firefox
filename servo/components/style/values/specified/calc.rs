/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! [Calc expressions][calc].
//!
//! [calc]: https://drafts.csswg.org/css-values/#calc-notation

use crate::color::parsing::ChannelKeyword;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::{self, ToComputedValue};
use crate::values::generics::calc::{
    self as generic, CalcNodeLeaf, CalcUnits, GenericAnchorFunctionFallback, MinMaxOp, ModRemOp,
    PositivePercentageBasis, RoundingStrategy, SortKey,
};
use crate::values::generics::length::GenericAnchorSizeFunction;
use crate::values::generics::position::{
    AnchorSideKeyword, GenericAnchorFunction, GenericAnchorSide, TreeScoped,
};
use crate::values::specified::length::{AbsoluteLength, FontRelativeLength, NoCalcLength};
use crate::values::specified::length::{ContainerRelativeLength, ViewportPercentageLength};
use crate::values::specified::{
    self, Angle, NoCalcNumber, NoCalcPercentage, NoCalcTime, Resolution,
};
use crate::values::{CSSFloat, DashedIdent};
use cssparser::{match_ignore_ascii_case, CowRcStr, Parser, Token};
use debug_unreachable::debug_unreachable;
use smallvec::SmallVec;
use std::cmp;
use std::fmt::{self, Write};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{
    CssWriter, ParseError, SpecifiedValueInfo, StyleParseErrorKind, ToCss, ToTyped, TypedValue,
};
use thin_vec::ThinVec;

/// The name of the mathematical function that we're parsing.
#[derive(Clone, Copy, Debug, Parse)]
pub enum MathFunction {
    /// `calc()`: https://drafts.csswg.org/css-values-4/#funcdef-calc
    Calc,
    /// `min()`: https://drafts.csswg.org/css-values-4/#funcdef-min
    Min,
    /// `max()`: https://drafts.csswg.org/css-values-4/#funcdef-max
    Max,
    /// `clamp()`: https://drafts.csswg.org/css-values-4/#funcdef-clamp
    Clamp,
    /// `round()`: https://drafts.csswg.org/css-values-4/#funcdef-round
    Round,
    /// `mod()`: https://drafts.csswg.org/css-values-4/#funcdef-mod
    Mod,
    /// `rem()`: https://drafts.csswg.org/css-values-4/#funcdef-rem
    Rem,
    /// `sin()`: https://drafts.csswg.org/css-values-4/#funcdef-sin
    Sin,
    /// `cos()`: https://drafts.csswg.org/css-values-4/#funcdef-cos
    Cos,
    /// `tan()`: https://drafts.csswg.org/css-values-4/#funcdef-tan
    Tan,
    /// `asin()`: https://drafts.csswg.org/css-values-4/#funcdef-asin
    Asin,
    /// `acos()`: https://drafts.csswg.org/css-values-4/#funcdef-acos
    Acos,
    /// `atan()`: https://drafts.csswg.org/css-values-4/#funcdef-atan
    Atan,
    /// `atan2()`: https://drafts.csswg.org/css-values-4/#funcdef-atan2
    Atan2,
    /// `pow()`: https://drafts.csswg.org/css-values-4/#funcdef-pow
    Pow,
    /// `sqrt()`: https://drafts.csswg.org/css-values-4/#funcdef-sqrt
    Sqrt,
    /// `hypot()`: https://drafts.csswg.org/css-values-4/#funcdef-hypot
    Hypot,
    /// `log()`: https://drafts.csswg.org/css-values-4/#funcdef-log
    Log,
    /// `exp()`: https://drafts.csswg.org/css-values-4/#funcdef-exp
    Exp,
    /// `abs()`: https://drafts.csswg.org/css-values-4/#funcdef-abs
    Abs,
    /// `sign()`: https://drafts.csswg.org/css-values-4/#funcdef-sign
    Sign,
}

/// A leaf node inside a `Calc` expression's AST.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
#[repr(u8)]
pub enum Leaf {
    /// `<length>`
    Length(NoCalcLength),
    /// `<angle>`
    Angle(Angle),
    /// `<time>`
    Time(NoCalcTime),
    /// `<resolution>`
    Resolution(Resolution),
    /// A component of a color.
    ColorComponent(ChannelKeyword),
    /// `<percentage>`
    Percentage(NoCalcPercentage),
    /// `<number>`
    Number(NoCalcNumber),
}

impl Leaf {
    fn as_length(&self) -> Option<&NoCalcLength> {
        match *self {
            Self::Length(ref l) => Some(l),
            _ => None,
        }
    }
}

impl ToCss for Leaf {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            Self::Length(ref l) => l.to_css(dest),
            Self::Number(n) => n.to_css(dest),
            Self::Resolution(ref r) => r.to_css(dest),
            Self::Percentage(p) => p.to_css(dest),
            Self::Angle(ref a) => a.to_css(dest),
            Self::Time(ref t) => t.to_css(dest),
            Self::ColorComponent(ref s) => s.to_css(dest),
        }
    }
}

impl ToTyped for Leaf {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        // XXX Only supporting Length, Number,  Percentage and Time for now
        match *self {
            Self::Length(ref l) => l.to_typed(dest),
            Self::Number(n) => n.to_typed(dest),
            Self::Percentage(p) => p.to_typed(dest),
            Self::Time(t) => t.to_typed(dest),
            _ => Err(()),
        }
    }
}

/// A struct to hold a simplified calc expression and associated clamping mode.
///
/// In some cases, e.g. DOMMatrix, we support calc(), but reject all the
/// relative lengths, and to_computed_pixel_length_without_context() handles
/// this case. Therefore, if you want to add a new field, please make sure this
/// function work properly.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem, ToTyped)]
#[allow(missing_docs)]
pub struct CalcNumeric {
    #[css(skip)]
    pub clamping_mode: AllowedNumericType,
    pub node: CalcNode,
}

impl CalcNumeric {
    fn same_unit_length_as(a: &Self, b: &Self) -> Option<(CSSFloat, CSSFloat)> {
        debug_assert_eq!(a.clamping_mode, b.clamping_mode);
        debug_assert_eq!(a.clamping_mode, AllowedNumericType::All);

        let a = a.node.as_leaf()?;
        let b = b.node.as_leaf()?;

        if a.sort_key() != b.sort_key() {
            return None;
        }

        let a = a.as_length()?.unitless_value();
        let b = b.as_length()?.unitless_value();
        return Some((a, b));
    }

    /// Returns a new CalcNumeric with the same expression but the specified clamping mode
    pub fn with_clamping_mode(&self, clamping_mode: AllowedNumericType) -> Self {
        Self {
            clamping_mode,
            node: self.node.clone(),
        }
    }

    /// Returns a new CalcNumeric with the same clamping mode but a different leaf node
    pub fn with_leaf_node(&self, leaf: Leaf) -> Self {
        Self {
            clamping_mode: self.clamping_mode,
            node: CalcNode::Leaf(leaf),
        }
    }

    /// Gets this calc expression as a number
    pub fn as_number(&self) -> Option<NoCalcNumber> {
        match self.node.resolve() {
            Ok(Leaf::Number(n)) => Some(n),
            _ => None,
        }
    }

    /// Gets this calc expression as a percentage
    pub fn as_percentage(&self) -> Option<NoCalcPercentage> {
        match self.node.resolve() {
            Ok(Leaf::Percentage(p)) => Some(p),
            _ => None,
        }
    }

    /// Gets this calc expression as a time
    pub fn as_time(&self) -> Option<NoCalcTime> {
        match self.node.resolve() {
            Ok(Leaf::Time(t)) => Some(t),
            _ => None,
        }
    }
}

impl SpecifiedValueInfo for CalcNumeric {}

/// Should parsing anchor-positioning functions in `calc()` be allowed?
#[derive(Clone, Copy, PartialEq)]
pub enum AllowAnchorPositioningFunctions {
    /// Don't allow any anchor positioning function.
    No,
    /// Allow `anchor-size()` to be parsed.
    AllowAnchorSize,
    /// Allow `anchor()` and `anchor-size()` to be parsed.
    AllowAnchorAndAnchorSize,
}

bitflags! {
    /// Additional functions within math functions that are permitted to be parsed depending on
    /// the context of parsing (e.g. Parsing `inset` allows use of `anchor()` within `calc()`).
    #[derive(Clone, Copy, PartialEq, Eq)]
    struct AdditionalFunctions: u8 {
        /// `anchor()` function.
        const ANCHOR = 1 << 0;
        /// `anchor-size()` function.
        const ANCHOR_SIZE = 1 << 1;
    }
}

/// What is allowed to be parsed for math functions within in this context?
#[derive(Clone, Copy)]
pub struct AllowParse {
    /// Units allowed to be parsed.
    units: CalcUnits,
    /// Additional functions allowed to be parsed in this context.
    additional_functions: AdditionalFunctions,
}

impl AllowParse {
    /// Allow only specified units to be parsed, without any additional functions.
    pub fn new(units: CalcUnits) -> Self {
        Self {
            units,
            additional_functions: AdditionalFunctions::empty(),
        }
    }

    /// Add new units to the allowed units to be parsed.
    fn new_including(mut self, units: CalcUnits) -> Self {
        self.units |= units;
        self
    }

    /// Should given unit be allowed to parse?
    fn includes(&self, unit: CalcUnits) -> bool {
        self.units.intersects(unit)
    }
}

impl generic::CalcNodeLeaf for Leaf {
    fn unit(&self) -> CalcUnits {
        match self {
            Leaf::Length(_) => CalcUnits::LENGTH,
            Leaf::Angle(_) => CalcUnits::ANGLE,
            Leaf::Time(_) => CalcUnits::TIME,
            Leaf::Resolution(_) => CalcUnits::RESOLUTION,
            Leaf::ColorComponent(_) => CalcUnits::COLOR_COMPONENT,
            Leaf::Percentage(_) => CalcUnits::PERCENTAGE,
            Leaf::Number(_) => CalcUnits::empty(),
        }
    }

    fn unitless_value(&self) -> Option<f32> {
        Some(match *self {
            Self::Length(ref l) => l.unitless_value(),
            Self::Percentage(ref p) => p.get(),
            Self::Number(ref n) => n.value(),
            Self::Resolution(ref r) => r.dppx(),
            Self::Angle(ref a) => a.degrees(),
            Self::Time(ref t) => t.seconds(),
            Self::ColorComponent(_) => return None,
        })
    }

    fn new_number(value: f32) -> Self {
        Self::Number(NoCalcNumber::new(value))
    }

    fn compare(&self, other: &Self, basis: PositivePercentageBasis) -> Option<cmp::Ordering> {
        use self::Leaf::*;

        if std::mem::discriminant(self) != std::mem::discriminant(other) {
            return None;
        }

        if matches!(self, Percentage(..)) && matches!(basis, PositivePercentageBasis::Unknown) {
            return None;
        }

        let self_negative = self.is_negative().unwrap_or(false);
        if self_negative != other.is_negative().unwrap_or(false) {
            return Some(if self_negative {
                cmp::Ordering::Less
            } else {
                cmp::Ordering::Greater
            });
        }

        match (self, other) {
            (&Percentage(ref one), &Percentage(ref other)) => one.get().partial_cmp(&other.get()),
            (&Length(ref one), &Length(ref other)) => one.partial_cmp(other),
            (&Angle(ref one), &Angle(ref other)) => one.degrees().partial_cmp(&other.degrees()),
            (&Time(ref one), &Time(ref other)) => one.seconds().partial_cmp(&other.seconds()),
            (&Resolution(ref one), &Resolution(ref other)) => one.dppx().partial_cmp(&other.dppx()),
            (&Number(ref one), &Number(ref other)) => one.partial_cmp(other),
            (&ColorComponent(ref one), &ColorComponent(ref other)) => one.partial_cmp(other),
            _ => {
                match *self {
                    Length(..) | Percentage(..) | Angle(..) | Time(..) | Number(..)
                    | Resolution(..) | ColorComponent(..) => {},
                }
                unsafe {
                    debug_unreachable!("Forgot a branch?");
                }
            },
        }
    }

    fn as_number(&self) -> Option<f32> {
        match *self {
            Leaf::Length(_)
            | Leaf::Angle(_)
            | Leaf::Time(_)
            | Leaf::Resolution(_)
            | Leaf::Percentage(_)
            | Leaf::ColorComponent(_) => None,
            Leaf::Number(n) => Some(n.value()),
        }
    }

    fn sort_key(&self) -> SortKey {
        match *self {
            Self::Number(..) => SortKey::Number,
            Self::Percentage(..) => SortKey::Percentage,
            Self::Time(..) => SortKey::S,
            Self::Resolution(..) => SortKey::Dppx,
            Self::Angle(..) => SortKey::Deg,
            Self::Length(ref l) => match *l {
                NoCalcLength::Absolute(..) => SortKey::Px,
                NoCalcLength::FontRelative(ref relative) => match *relative {
                    FontRelativeLength::Em(..) => SortKey::Em,
                    FontRelativeLength::Ex(..) => SortKey::Ex,
                    FontRelativeLength::Rex(..) => SortKey::Rex,
                    FontRelativeLength::Ch(..) => SortKey::Ch,
                    FontRelativeLength::Rch(..) => SortKey::Rch,
                    FontRelativeLength::Cap(..) => SortKey::Cap,
                    FontRelativeLength::Rcap(..) => SortKey::Rcap,
                    FontRelativeLength::Ic(..) => SortKey::Ic,
                    FontRelativeLength::Ric(..) => SortKey::Ric,
                    FontRelativeLength::Rem(..) => SortKey::Rem,
                    FontRelativeLength::Lh(..) => SortKey::Lh,
                    FontRelativeLength::Rlh(..) => SortKey::Rlh,
                },
                NoCalcLength::ViewportPercentage(ref vp) => match *vp {
                    ViewportPercentageLength::Vh(..) => SortKey::Vh,
                    ViewportPercentageLength::Svh(..) => SortKey::Svh,
                    ViewportPercentageLength::Lvh(..) => SortKey::Lvh,
                    ViewportPercentageLength::Dvh(..) => SortKey::Dvh,
                    ViewportPercentageLength::Vw(..) => SortKey::Vw,
                    ViewportPercentageLength::Svw(..) => SortKey::Svw,
                    ViewportPercentageLength::Lvw(..) => SortKey::Lvw,
                    ViewportPercentageLength::Dvw(..) => SortKey::Dvw,
                    ViewportPercentageLength::Vmax(..) => SortKey::Vmax,
                    ViewportPercentageLength::Svmax(..) => SortKey::Svmax,
                    ViewportPercentageLength::Lvmax(..) => SortKey::Lvmax,
                    ViewportPercentageLength::Dvmax(..) => SortKey::Dvmax,
                    ViewportPercentageLength::Vmin(..) => SortKey::Vmin,
                    ViewportPercentageLength::Svmin(..) => SortKey::Svmin,
                    ViewportPercentageLength::Lvmin(..) => SortKey::Lvmin,
                    ViewportPercentageLength::Dvmin(..) => SortKey::Dvmin,
                    ViewportPercentageLength::Vb(..) => SortKey::Vb,
                    ViewportPercentageLength::Svb(..) => SortKey::Svb,
                    ViewportPercentageLength::Lvb(..) => SortKey::Lvb,
                    ViewportPercentageLength::Dvb(..) => SortKey::Dvb,
                    ViewportPercentageLength::Vi(..) => SortKey::Vi,
                    ViewportPercentageLength::Svi(..) => SortKey::Svi,
                    ViewportPercentageLength::Lvi(..) => SortKey::Lvi,
                    ViewportPercentageLength::Dvi(..) => SortKey::Dvi,
                },
                NoCalcLength::ContainerRelative(ref cq) => match *cq {
                    ContainerRelativeLength::Cqw(..) => SortKey::Cqw,
                    ContainerRelativeLength::Cqh(..) => SortKey::Cqh,
                    ContainerRelativeLength::Cqi(..) => SortKey::Cqi,
                    ContainerRelativeLength::Cqb(..) => SortKey::Cqb,
                    ContainerRelativeLength::Cqmin(..) => SortKey::Cqmin,
                    ContainerRelativeLength::Cqmax(..) => SortKey::Cqmax,
                },
                NoCalcLength::ServoCharacterWidth(..) => unreachable!(),
            },
            Self::ColorComponent(..) => SortKey::ColorComponent,
        }
    }

    fn simplify(&mut self) {
        match self {
            Leaf::Length(NoCalcLength::Absolute(ref mut abs)) => {
                *abs = AbsoluteLength::Px(abs.to_px())
            },
            Leaf::Time(ref mut t) => *t = NoCalcTime::from_seconds(t.seconds()),
            _ => (),
        }
    }

    /// Tries to merge one sum to another, that is, perform `x` + `y`.
    ///
    /// Only handles leaf nodes, it's the caller's responsibility to simplify
    /// them before calling this if needed.
    fn try_sum_in_place(&mut self, other: &Self) -> Result<(), ()> {
        use self::Leaf::*;

        if std::mem::discriminant(self) != std::mem::discriminant(other) {
            return Err(());
        }

        match (self, other) {
            (&mut Number(ref mut one), &Number(ref other)) => {
                *one = NoCalcNumber::new(one.value() + other.value());
            },
            (&mut Percentage(ref mut one), &Percentage(ref other)) => {
                *one = NoCalcPercentage::new(one.get() + other.get());
            },
            (&mut Angle(ref mut one), &Angle(ref other)) => {
                *one = specified::Angle::from_calc(one.degrees() + other.degrees());
            },
            (&mut Time(ref mut one), &Time(ref other)) => {
                *one = NoCalcTime::from_seconds(one.seconds() + other.seconds());
            },
            (&mut Resolution(ref mut one), &Resolution(ref other)) => {
                *one = specified::Resolution::from_dppx(one.dppx() + other.dppx());
            },
            (&mut Length(ref mut one), &Length(ref other)) => {
                *one = one.try_op(other, std::ops::Add::add)?;
            },
            (&mut ColorComponent(_), &ColorComponent(_)) => {
                // Can not get the sum of color components, because they haven't been resolved yet.
                return Err(());
            },
            _ => {
                match *other {
                    Number(..) | Percentage(..) | Angle(..) | Time(..) | Resolution(..)
                    | Length(..) | ColorComponent(..) => {},
                }
                unsafe {
                    debug_unreachable!();
                }
            },
        }

        Ok(())
    }

    fn try_product_in_place(&mut self, other: &mut Self) -> bool {
        if let Self::Number(ref mut left) = *self {
            if let Self::Number(ref right) = *other {
                // Both sides are numbers, so we can just modify the left side.
                *left = NoCalcNumber::new(left.value() * right.value());
                true
            } else {
                // The right side is not a number, so the result should be in the units of the right
                // side.
                let left_val = left.value();
                if other.map(|v| v * left_val).is_ok() {
                    std::mem::swap(self, other);
                    true
                } else {
                    false
                }
            }
        } else if let Self::Number(ref right) = *other {
            // The left side is not a number, but the right side is, so the result is the left
            // side unit.
            let right_val = right.value();
            self.map(|v| v * right_val).is_ok()
        } else {
            // Neither side is a number, so a product is not possible.
            false
        }
    }

    fn try_op<O>(&self, other: &Self, op: O) -> Result<Self, ()>
    where
        O: Fn(f32, f32) -> f32,
    {
        use self::Leaf::*;

        if std::mem::discriminant(self) != std::mem::discriminant(other) {
            return Err(());
        }

        match (self, other) {
            (&Number(one), &Number(other)) => {
                return Ok(Leaf::Number(NoCalcNumber::new(op(
                    one.value(),
                    other.value(),
                ))));
            },
            (&Percentage(one), &Percentage(other)) => {
                return Ok(Leaf::Percentage(NoCalcPercentage::new(op(
                    one.get(),
                    other.get(),
                ))));
            },
            (&Angle(ref one), &Angle(ref other)) => {
                return Ok(Leaf::Angle(specified::Angle::from_calc(op(
                    one.degrees(),
                    other.degrees(),
                ))));
            },
            (&Resolution(ref one), &Resolution(ref other)) => {
                return Ok(Leaf::Resolution(specified::Resolution::from_dppx(op(
                    one.dppx(),
                    other.dppx(),
                ))));
            },
            (&Time(ref one), &Time(ref other)) => {
                return Ok(Leaf::Time(NoCalcTime::from_seconds(op(
                    one.seconds(),
                    other.seconds(),
                ))));
            },
            (&Length(ref one), &Length(ref other)) => {
                return Ok(Leaf::Length(one.try_op(other, op)?));
            },
            (&ColorComponent(..), &ColorComponent(..)) => {
                return Err(());
            },
            _ => {
                match *other {
                    Number(..) | Percentage(..) | Angle(..) | Time(..) | Length(..)
                    | Resolution(..) | ColorComponent(..) => {},
                }
                unsafe {
                    debug_unreachable!();
                }
            },
        }
    }

    fn map(&mut self, mut op: impl FnMut(f32) -> f32) -> Result<(), ()> {
        Ok(match self {
            Leaf::Length(one) => *one = one.map(op),
            Leaf::Angle(one) => *one = specified::Angle::from_calc(op(one.degrees())),
            Leaf::Time(one) => *one = NoCalcTime::from_seconds(op(one.seconds())),
            Leaf::Resolution(one) => *one = specified::Resolution::from_dppx(op(one.dppx())),
            Leaf::Percentage(one) => *one = NoCalcPercentage::new(op(one.get())),
            Leaf::Number(one) => *one = NoCalcNumber::new(op(one.value())),
            Leaf::ColorComponent(..) => return Err(()),
        })
    }
}

impl GenericAnchorSide<Box<CalcNode>> {
    fn parse_in_calc<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(k) = input.try_parse(|i| AnchorSideKeyword::parse(i)) {
            return Ok(Self::Keyword(k));
        }
        Ok(Self::Percentage(Box::new(CalcNode::parse_argument(
            context,
            input,
            AllowParse::new(CalcUnits::PERCENTAGE),
        )?)))
    }
}

fn parse_anchor_function_fallback<'i, 't>(
    context: &ParserContext,
    additional_functions: AdditionalFunctions,
    input: &mut Parser<'i, 't>,
) -> Result<Box<GenericAnchorFunctionFallback<Leaf>>, ParseError<'i>> {
    if let Ok(l) = input.try_parse(|i| -> Result<CalcNode, ParseError<'i>> {
        Ok(CalcNode::Leaf(match i.next()? {
            &Token::Number { value, .. } => {
                if value != 0.0 {
                    return Err(i.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                Leaf::Length(NoCalcLength::Absolute(AbsoluteLength::Px(0.0)))
            },
            &Token::Dimension {
                value, ref unit, ..
            } => Leaf::Length(
                NoCalcLength::parse_dimension_with_context(context, value, unit)
                    .map_err(|_| i.new_custom_error(StyleParseErrorKind::UnspecifiedError))?,
            ),
            &Token::Percentage { unit_value, .. } => {
                Leaf::Percentage(NoCalcPercentage::new(unit_value))
            },
            _ => return Err(i.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
        }))
    }) {
        return Ok(Box::new(GenericAnchorFunctionFallback::new(false, l)));
    }
    let node = CalcNode::parse_argument(
        context,
        input,
        AllowParse {
            units: CalcUnits::LENGTH_PERCENTAGE,
            additional_functions,
        },
    )?
    .into_length_or_percentage(AllowedNumericType::All)
    .map_err(|_| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))?
    .node;
    Ok(Box::new(GenericAnchorFunctionFallback::new(true, node)))
}

impl GenericAnchorFunction<Box<CalcNode>, Box<GenericAnchorFunctionFallback<Leaf>>> {
    fn parse_in_calc<'i, 't>(
        context: &ParserContext,
        additional_functions: AdditionalFunctions,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if !static_prefs::pref!("layout.css.anchor-positioning.enabled") {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        input.parse_nested_block(|i| {
            let target_element = i.try_parse(|i| DashedIdent::parse(context, i)).ok();
            let side = GenericAnchorSide::parse_in_calc(context, i)?;
            let target_element = if target_element.is_none() {
                i.try_parse(|i| DashedIdent::parse(context, i)).ok()
            } else {
                target_element
            };
            let fallback = i
                .try_parse(|i| {
                    i.expect_comma()?;
                    parse_anchor_function_fallback(context, additional_functions, i)
                })
                .ok();
            Ok(Self {
                target_element: TreeScoped::with_default_level(
                    target_element.unwrap_or_else(DashedIdent::empty),
                ),
                side,
                fallback: fallback.into(),
            })
        })
    }
}

impl GenericAnchorSizeFunction<Box<GenericAnchorFunctionFallback<Leaf>>> {
    fn parse_in_calc<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if !static_prefs::pref!("layout.css.anchor-positioning.enabled") {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        GenericAnchorSizeFunction::parse_inner(context, input, |i| {
            parse_anchor_function_fallback(context, AdditionalFunctions::ANCHOR_SIZE, i)
        })
    }
}

/// Specified `anchor()` function in math functions.
pub type CalcAnchorFunction = generic::GenericCalcAnchorFunction<Leaf>;
/// Specified `anchor-size()` function in math functions.
pub type CalcAnchorSizeFunction = generic::GenericCalcAnchorSizeFunction<Leaf>;

/// A calc node representation for specified values.
pub type CalcNode = generic::GenericCalcNode<Leaf>;
impl CalcNode {
    /// Tries to parse a single element in the expression, that is, a
    /// `<length>`, `<angle>`, `<time>`, `<percentage>`, `<resolution>`, etc.
    ///
    /// May return a "complex" `CalcNode`, in the presence of a parenthesized
    /// expression, for example.
    fn parse_one<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allowed: AllowParse,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        match input.next()? {
            &Token::Number { value, .. } => {
                Ok(CalcNode::Leaf(Leaf::Number(NoCalcNumber::new(value))))
            },
            &Token::Dimension {
                value, ref unit, ..
            } => {
                if allowed.includes(CalcUnits::LENGTH) {
                    if let Ok(l) = NoCalcLength::parse_dimension_with_context(context, value, unit)
                    {
                        return Ok(CalcNode::Leaf(Leaf::Length(l)));
                    }
                }
                if allowed.includes(CalcUnits::ANGLE) {
                    if let Ok(a) = Angle::parse_dimension(value, unit, /* from_calc = */ true) {
                        return Ok(CalcNode::Leaf(Leaf::Angle(a)));
                    }
                }
                if allowed.includes(CalcUnits::TIME) {
                    if let Ok(t) = NoCalcTime::parse_dimension(value, unit) {
                        return Ok(CalcNode::Leaf(Leaf::Time(t)));
                    }
                }
                if allowed.includes(CalcUnits::RESOLUTION) {
                    if let Ok(t) = Resolution::parse_dimension(value, unit) {
                        return Ok(CalcNode::Leaf(Leaf::Resolution(t)));
                    }
                }
                return Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            },
            &Token::Percentage { unit_value, .. } if allowed.includes(CalcUnits::PERCENTAGE) => Ok(
                CalcNode::Leaf(Leaf::Percentage(NoCalcPercentage::new(unit_value))),
            ),
            &Token::ParenthesisBlock => {
                input.parse_nested_block(|input| CalcNode::parse_argument(context, input, allowed))
            },
            &Token::Function(ref name)
                if allowed
                    .additional_functions
                    .intersects(AdditionalFunctions::ANCHOR)
                    && name.eq_ignore_ascii_case("anchor") =>
            {
                let anchor_function = GenericAnchorFunction::parse_in_calc(
                    context,
                    allowed.additional_functions,
                    input,
                )?;
                Ok(CalcNode::Anchor(Box::new(anchor_function)))
            },
            &Token::Function(ref name)
                if allowed
                    .additional_functions
                    .intersects(AdditionalFunctions::ANCHOR_SIZE)
                    && name.eq_ignore_ascii_case("anchor-size") =>
            {
                let anchor_size_function =
                    GenericAnchorSizeFunction::parse_in_calc(context, input)?;
                Ok(CalcNode::AnchorSize(Box::new(anchor_size_function)))
            },
            &Token::Function(ref name) => {
                let function = CalcNode::math_function(context, name, location)?;
                CalcNode::parse(context, input, function, allowed)
            },
            &Token::Ident(ref ident) => {
                let leaf = match_ignore_ascii_case! { &**ident,
                    "e" => Leaf::Number(NoCalcNumber::new(std::f32::consts::E)),
                    "pi" => Leaf::Number(NoCalcNumber::new(std::f32::consts::PI)),
                    "infinity" => Leaf::Number(NoCalcNumber::new(f32::INFINITY)),
                    "-infinity" => Leaf::Number(NoCalcNumber::new(f32::NEG_INFINITY)),
                    "nan" => Leaf::Number(NoCalcNumber::new(f32::NAN)),
                    _ => {
                        if crate::color::parsing::rcs_enabled() &&
                            allowed.includes(CalcUnits::COLOR_COMPONENT)
                        {
                            if let Ok(channel_keyword) = ChannelKeyword::from_ident(&ident) {
                                Leaf::ColorComponent(channel_keyword)
                            } else {
                                return Err(location
                                    .new_unexpected_token_error(Token::Ident(ident.clone())));
                            }
                        } else {
                            return Err(
                                location.new_unexpected_token_error(Token::Ident(ident.clone()))
                            );
                        }
                    },
                };
                Ok(CalcNode::Leaf(leaf))
            },
            t => Err(location.new_unexpected_token_error(t.clone())),
        }
    }

    /// Parse a top-level `calc` expression, with all nested sub-expressions.
    ///
    /// This is in charge of parsing, for example, `2 + 3 * 100%`.
    pub fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        function: MathFunction,
        allowed: AllowParse,
    ) -> Result<Self, ParseError<'i>> {
        input.parse_nested_block(|input| {
            match function {
                MathFunction::Calc => Self::parse_argument(context, input, allowed),
                MathFunction::Clamp => {
                    let min_val = if input
                        .try_parse(|min| min.expect_ident_matching("none"))
                        .ok()
                        .is_none()
                    {
                        Some(Self::parse_argument(context, input, allowed)?)
                    } else {
                        None
                    };

                    input.expect_comma()?;
                    let center = Self::parse_argument(context, input, allowed)?;
                    input.expect_comma()?;

                    let max_val = if input
                        .try_parse(|max| max.expect_ident_matching("none"))
                        .ok()
                        .is_none()
                    {
                        Some(Self::parse_argument(context, input, allowed)?)
                    } else {
                        None
                    };

                    // Specification does not state how serialization should occur for clamp
                    // https://github.com/w3c/csswg-drafts/issues/13535
                    // tentatively partially serialize to min/max
                    // clamp(MIN, VAL, none) is equivalent to max(MIN, VAL)
                    // clamp(none, VAL, MAX) is equivalent to min(VAL, MAX)
                    // clamp(none, VAL, none) is equivalent to just calc(VAL)
                    Ok(match (min_val, max_val) {
                        (None, None) => center,
                        (None, Some(max)) => Self::MinMax(vec![center, max].into(), MinMaxOp::Min),
                        (Some(min), None) => Self::MinMax(vec![min, center].into(), MinMaxOp::Max),
                        (Some(min), Some(max)) => Self::Clamp {
                            min: Box::new(min),
                            center: Box::new(center),
                            max: Box::new(max),
                        },
                    })
                },
                MathFunction::Round => {
                    let strategy = input.try_parse(parse_rounding_strategy);

                    // <rounding-strategy> = nearest | up | down | to-zero
                    // https://drafts.csswg.org/css-values-4/#calc-syntax
                    fn parse_rounding_strategy<'i, 't>(
                        input: &mut Parser<'i, 't>,
                    ) -> Result<RoundingStrategy, ParseError<'i>> {
                        Ok(try_match_ident_ignore_ascii_case! { input,
                            "nearest" => RoundingStrategy::Nearest,
                            "up" => RoundingStrategy::Up,
                            "down" => RoundingStrategy::Down,
                            "to-zero" => RoundingStrategy::ToZero,
                        })
                    }

                    if strategy.is_ok() {
                        input.expect_comma()?;
                    }

                    let value = Self::parse_argument(context, input, allowed)?;

                    // <step> defaults to the number 1 if not provided
                    // https://drafts.csswg.org/css-values-4/#funcdef-round
                    let step = input.try_parse(|input| {
                        input.expect_comma()?;
                        Self::parse_argument(context, input, allowed)
                    });

                    let step = step.unwrap_or(Self::Leaf(Leaf::Number(NoCalcNumber::new(1.0))));

                    Ok(Self::Round {
                        strategy: strategy.unwrap_or(RoundingStrategy::Nearest),
                        value: Box::new(value),
                        step: Box::new(step),
                    })
                },
                MathFunction::Mod | MathFunction::Rem => {
                    let dividend = Self::parse_argument(context, input, allowed)?;
                    input.expect_comma()?;
                    let divisor = Self::parse_argument(context, input, allowed)?;

                    let op = match function {
                        MathFunction::Mod => ModRemOp::Mod,
                        MathFunction::Rem => ModRemOp::Rem,
                        _ => unreachable!(),
                    };
                    Ok(Self::ModRem {
                        dividend: Box::new(dividend),
                        divisor: Box::new(divisor),
                        op,
                    })
                },
                MathFunction::Min | MathFunction::Max => {
                    // TODO(emilio): The common case for parse_comma_separated
                    // is just one element, but for min / max is two, really...
                    //
                    // Consider adding an API to cssparser to specify the
                    // initial vector capacity?
                    let arguments = input.parse_comma_separated(|input| {
                        let result = Self::parse_argument(context, input, allowed)?;
                        Ok(result)
                    })?;

                    let op = match function {
                        MathFunction::Min => MinMaxOp::Min,
                        MathFunction::Max => MinMaxOp::Max,
                        _ => unreachable!(),
                    };

                    Ok(Self::MinMax(arguments.into(), op))
                },
                MathFunction::Sin | MathFunction::Cos | MathFunction::Tan => {
                    let a = Self::parse_angle_argument(context, input)?;

                    let number = match function {
                        MathFunction::Sin => a.sin(),
                        MathFunction::Cos => a.cos(),
                        MathFunction::Tan => a.tan(),
                        _ => unsafe {
                            debug_unreachable!("We just checked!");
                        },
                    };

                    Ok(Self::Leaf(Leaf::Number(NoCalcNumber::new(number))))
                },
                MathFunction::Asin | MathFunction::Acos | MathFunction::Atan => {
                    let a = Self::parse_number_argument(context, input)?;

                    let radians = match function {
                        MathFunction::Asin => a.asin(),
                        MathFunction::Acos => a.acos(),
                        MathFunction::Atan => a.atan(),
                        _ => unsafe {
                            debug_unreachable!("We just checked!");
                        },
                    };

                    Ok(Self::Leaf(Leaf::Angle(Angle::from_radians(radians))))
                },
                MathFunction::Atan2 => {
                    let allow_all = allowed.new_including(CalcUnits::ALL);
                    let a = Self::parse_argument(context, input, allow_all)?;
                    input.expect_comma()?;
                    let b = Self::parse_argument(context, input, allow_all)?;

                    let radians = Self::try_resolve(input, || {
                        if let Ok(a) = a.to_number() {
                            let b = b.to_number()?;
                            return Ok(a.value().atan2(b.value()));
                        }

                        if let Ok(a) = a.to_percentage() {
                            let b = b.to_percentage()?;
                            return Ok(a.get().atan2(b.get()));
                        }

                        if let Ok(a) = a.to_time() {
                            let b = b.to_time()?;
                            return Ok(a.seconds().atan2(b.seconds()));
                        }

                        if let Ok(a) = a.to_angle() {
                            let b = b.to_angle()?;
                            return Ok(a.radians().atan2(b.radians()));
                        }

                        if let Ok(a) = a.to_resolution() {
                            let b = b.to_resolution()?;
                            return Ok(a.dppx().atan2(b.dppx()));
                        }

                        let a = a.into_length_or_percentage(AllowedNumericType::All)?;
                        let b = b.into_length_or_percentage(AllowedNumericType::All)?;
                        let (a, b) = CalcNumeric::same_unit_length_as(&a, &b).ok_or(())?;

                        Ok(a.atan2(b))
                    })?;

                    Ok(Self::Leaf(Leaf::Angle(Angle::from_radians(radians))))
                },
                MathFunction::Pow => {
                    let a = Self::parse_number_argument(context, input)?;
                    input.expect_comma()?;
                    let b = Self::parse_number_argument(context, input)?;

                    let number = a.powf(b);

                    Ok(Self::Leaf(Leaf::Number(NoCalcNumber::new(number))))
                },
                MathFunction::Sqrt => {
                    let a = Self::parse_number_argument(context, input)?;

                    let number = a.sqrt();

                    Ok(Self::Leaf(Leaf::Number(NoCalcNumber::new(number))))
                },
                MathFunction::Hypot => {
                    let arguments = input.parse_comma_separated(|input| {
                        let result = Self::parse_argument(context, input, allowed)?;
                        Ok(result)
                    })?;

                    Ok(Self::Hypot(arguments.into()))
                },
                MathFunction::Log => {
                    let a = Self::parse_number_argument(context, input)?;
                    let b = input
                        .try_parse(|input| {
                            input.expect_comma()?;
                            Self::parse_number_argument(context, input)
                        })
                        .ok();

                    let number = match b {
                        Some(b) => a.log(b),
                        None => a.ln(),
                    };

                    Ok(Self::Leaf(Leaf::Number(NoCalcNumber::new(number))))
                },
                MathFunction::Exp => {
                    let a = Self::parse_number_argument(context, input)?;
                    let number = a.exp();
                    Ok(Self::Leaf(Leaf::Number(NoCalcNumber::new(number))))
                },
                MathFunction::Abs => {
                    let node = Self::parse_argument(context, input, allowed)?;
                    Ok(Self::Abs(Box::new(node)))
                },
                MathFunction::Sign => {
                    // The sign of a percentage is dependent on the percentage basis, so if
                    // percentages aren't allowed (so there's no basis) we shouldn't allow them in
                    // sign(). The rest of the units are safe tho.
                    let node = Self::parse_argument(
                        context,
                        input,
                        allowed.new_including(CalcUnits::ALL - CalcUnits::PERCENTAGE),
                    )?;
                    Ok(Self::Sign(Box::new(node)))
                },
            }
        })
    }

    fn parse_angle_argument<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<CSSFloat, ParseError<'i>> {
        let argument = Self::parse_argument(context, input, AllowParse::new(CalcUnits::ANGLE))?;
        argument
            .to_number()
            .map(|n| n.value())
            .or_else(|()| Ok(argument.to_angle()?.radians()))
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    fn parse_number_argument<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<CSSFloat, ParseError<'i>> {
        Self::parse_argument(context, input, AllowParse::new(CalcUnits::empty()))?
            .to_number()
            .map(|n| n.value())
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    fn parse_argument<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allowed: AllowParse,
    ) -> Result<Self, ParseError<'i>> {
        let mut sum = SmallVec::<[CalcNode; 1]>::new();
        let first = Self::parse_product(context, input, allowed)?;
        sum.push(first);
        loop {
            let start = input.state();
            match input.next_including_whitespace() {
                Ok(&Token::WhiteSpace(_)) => {
                    if input.is_exhausted() {
                        break; // allow trailing whitespace
                    }
                    match *input.next()? {
                        Token::Delim('+') => {
                            let rhs = Self::parse_product(context, input, allowed)?;
                            if sum.last_mut().unwrap().try_sum_in_place(&rhs).is_err() {
                                sum.push(rhs);
                            }
                        },
                        Token::Delim('-') => {
                            let mut rhs = Self::parse_product(context, input, allowed)?;
                            rhs.negate();
                            if sum.last_mut().unwrap().try_sum_in_place(&rhs).is_err() {
                                sum.push(rhs);
                            }
                        },
                        _ => {
                            input.reset(&start);
                            break;
                        },
                    }
                },
                _ => {
                    input.reset(&start);
                    break;
                },
            }
        }

        Ok(if sum.len() == 1 {
            sum.drain(..).next().unwrap()
        } else {
            Self::Sum(sum.into_boxed_slice().into())
        })
    }

    /// Parse a top-level `calc` expression, and all the products that may
    /// follow, and stop as soon as a non-product expression is found.
    ///
    /// This should parse correctly:
    ///
    /// * `2`
    /// * `2 * 2`
    /// * `2 * 2 + 2` (but will leave the `+ 2` unparsed).
    ///
    fn parse_product<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allowed: AllowParse,
    ) -> Result<Self, ParseError<'i>> {
        let mut product = SmallVec::<[CalcNode; 1]>::new();
        let first = Self::parse_one(context, input, allowed)?;
        product.push(first);

        loop {
            let start = input.state();
            match input.next() {
                Ok(&Token::Delim('*')) => {
                    let mut rhs = Self::parse_one(context, input, allowed)?;

                    // We can unwrap here, becuase we start the function by adding a node to
                    // the list.
                    if !product.last_mut().unwrap().try_product_in_place(&mut rhs) {
                        product.push(rhs);
                    }
                },
                Ok(&Token::Delim('/')) => {
                    let rhs = Self::parse_one(context, input, allowed)?;

                    enum InPlaceDivisionResult {
                        /// The right was merged into the left.
                        Merged,
                        /// The right is not a number or could not be resolved, so the left is
                        /// unchanged.
                        Unchanged,
                        /// The right was resolved, but was not a number, so the calculation is
                        /// invalid.
                        Invalid,
                    }

                    fn try_division_in_place(
                        left: &mut CalcNode,
                        right: &CalcNode,
                    ) -> InPlaceDivisionResult {
                        if let Ok(resolved) = right.resolve() {
                            if let Some(number) = resolved.as_number() {
                                if number != 1.0 && left.is_product_distributive() {
                                    if left.map(|l| l / number).is_err() {
                                        return InPlaceDivisionResult::Invalid;
                                    }
                                    return InPlaceDivisionResult::Merged;
                                }
                            } else {
                                // Color components are valid denominators, but they can't resolve
                                // at parse time.
                                return if resolved.unit().contains(CalcUnits::COLOR_COMPONENT) {
                                    InPlaceDivisionResult::Unchanged
                                } else {
                                    InPlaceDivisionResult::Invalid
                                };
                            }
                        }
                        InPlaceDivisionResult::Unchanged
                    }

                    // The right hand side of a division *must* be a number, so if we can
                    // already resolve it, then merge it with the last node on the product list.
                    // We can unwrap here, becuase we start the function by adding a node to
                    // the list.
                    match try_division_in_place(&mut product.last_mut().unwrap(), &rhs) {
                        InPlaceDivisionResult::Merged => {},
                        InPlaceDivisionResult::Unchanged => {
                            product.push(Self::Invert(Box::new(rhs)))
                        },
                        InPlaceDivisionResult::Invalid => {
                            return Err(
                                input.new_custom_error(StyleParseErrorKind::UnspecifiedError)
                            )
                        },
                    }
                },
                _ => {
                    input.reset(&start);
                    break;
                },
            }
        }

        Ok(if product.len() == 1 {
            product.drain(..).next().unwrap()
        } else {
            Self::Product(product.into_boxed_slice().into())
        })
    }

    fn try_resolve<'i, 't, F>(
        input: &Parser<'i, 't>,
        closure: F,
    ) -> Result<CSSFloat, ParseError<'i>>
    where
        F: FnOnce() -> Result<CSSFloat, ()>,
    {
        closure().map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Tries to simplify this expression into a `<length>` or `<percentage>`
    /// value.
    pub fn into_length_or_percentage(
        mut self,
        clamping_mode: AllowedNumericType,
    ) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        // Although we allow numbers inside CalcNumeric, calculations that resolve to a
        // number result is still not allowed.
        let unit = self.unit()?;
        if !CalcUnits::LENGTH_PERCENTAGE.intersects(unit) {
            Err(())
        } else {
            Ok(CalcNumeric {
                clamping_mode,
                node: self,
            })
        }
    }

    /// Tries to simplify this expression into a `NoCalcTime` value.
    fn to_time(&self) -> Result<NoCalcTime, ()> {
        if let Leaf::Time(time) = self.resolve()? {
            Ok(time)
        } else {
            Err(())
        }
    }

    /// Tries to simplify this expression into a `<time>` value.
    fn into_time(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        let unit: CalcUnits = self.unit()?;
        if !CalcUnits::TIME.intersects(unit) {
            Err(())
        } else {
            Ok(CalcNumeric {
                clamping_mode,
                node: self,
            })
        }
    }

    /// Tries to simplify the expression into a `<resolution>` value.
    fn to_resolution(&self) -> Result<Resolution, ()> {
        let dppx = if let Leaf::Resolution(resolution) = self.resolve()? {
            resolution.dppx()
        } else {
            return Err(());
        };

        Ok(Resolution::from_dppx_calc(dppx))
    }

    /// Tries to simplify this expression into an `Angle` value.
    fn to_angle(&self) -> Result<Angle, ()> {
        let degrees = if let Leaf::Angle(angle) = self.resolve()? {
            angle.degrees()
        } else {
            return Err(());
        };

        let result = Angle::from_calc(degrees);
        Ok(result)
    }

    /// Tries to simplify this expression into a `<number>` value.
    fn to_number(&self) -> Result<NoCalcNumber, ()> {
        let number = if let Leaf::Number(number) = self.resolve()? {
            number
        } else {
            return Err(());
        };

        Ok(number)
    }

    /// Tries to convert this expression into a `CalcNumeric`, keeping the
    /// AST for later evaluation at computed-value time.
    fn into_number(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        let unit: CalcUnits = self.unit()?;
        if !unit.is_empty() {
            Err(())
        } else {
            Ok(CalcNumeric {
                clamping_mode,
                node: self,
            })
        }
    }

    /// Tries to simplify this expression into a `NoCalcAngle` value.
    fn to_percentage(&self) -> Result<NoCalcPercentage, ()> {
        let value = if let Leaf::Percentage(percentage) = self.resolve()? {
            percentage.get()
        } else {
            return Err(());
        };

        Ok(NoCalcPercentage::new(value))
    }

    /// Tries to convert this expression into a `CalcNumeric`, keeping the
    /// AST for later evaluation at computed-value time.
    fn into_percentage(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        let unit: CalcUnits = self.unit()?;
        if !CalcUnits::PERCENTAGE.intersects(unit) {
            Err(())
        } else {
            Ok(CalcNumeric {
                clamping_mode,
                node: self,
            })
        }
    }

    /// Given a function name, and the location from where the token came from,
    /// return a mathematical function corresponding to that name or an error.
    #[inline]
    pub fn math_function<'i>(
        _: &ParserContext,
        name: &CowRcStr<'i>,
        location: cssparser::SourceLocation,
    ) -> Result<MathFunction, ParseError<'i>> {
        let function = match MathFunction::from_ident(&*name) {
            Ok(f) => f,
            Err(()) => {
                return Err(location.new_unexpected_token_error(Token::Function(name.clone())))
            },
        };

        Ok(function)
    }

    /// Convenience parsing function for `<length> | <percentage>`, and, optionally, `anchor()`.
    pub fn parse_length_or_percentage<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
        allow_anchor: AllowAnchorPositioningFunctions,
    ) -> Result<CalcNumeric, ParseError<'i>> {
        let allowed = if allow_anchor == AllowAnchorPositioningFunctions::No {
            AllowParse::new(CalcUnits::LENGTH_PERCENTAGE)
        } else {
            AllowParse {
                units: CalcUnits::LENGTH_PERCENTAGE,
                additional_functions: match allow_anchor {
                    AllowAnchorPositioningFunctions::No => unreachable!(),
                    AllowAnchorPositioningFunctions::AllowAnchorSize => {
                        AdditionalFunctions::ANCHOR_SIZE
                    },
                    AllowAnchorPositioningFunctions::AllowAnchorAndAnchorSize => {
                        AdditionalFunctions::ANCHOR | AdditionalFunctions::ANCHOR_SIZE
                    },
                },
            }
        };
        Self::parse(context, input, function, allowed)?
            .into_length_or_percentage(clamping_mode)
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for percentages.
    pub fn parse_percentage<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
    ) -> Result<CalcNumeric, ParseError<'i>> {
        Self::parse(
            context,
            input,
            function,
            AllowParse::new(CalcUnits::PERCENTAGE),
        )?
        .into_percentage(clamping_mode)
        .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<length>`.
    pub fn parse_length<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
    ) -> Result<CalcNumeric, ParseError<'i>> {
        Self::parse(context, input, function, AllowParse::new(CalcUnits::LENGTH))?
            .into_length_or_percentage(clamping_mode)
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<number>`.
    pub fn parse_number<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
    ) -> Result<CalcNumeric, ParseError<'i>> {
        Self::parse(
            context,
            input,
            function,
            AllowParse::new(CalcUnits::empty()),
        )?
        .into_number(clamping_mode)
        .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<angle>`.
    pub fn parse_angle<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        function: MathFunction,
    ) -> Result<Angle, ParseError<'i>> {
        Self::parse(context, input, function, AllowParse::new(CalcUnits::ANGLE))?
            .to_angle()
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<time>`.
    pub fn parse_time<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
    ) -> Result<CalcNumeric, ParseError<'i>> {
        Self::parse(context, input, function, AllowParse::new(CalcUnits::TIME))?
            .into_time(clamping_mode)
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<resolution>`.
    pub fn parse_resolution<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        function: MathFunction,
    ) -> Result<Resolution, ParseError<'i>> {
        Self::parse(
            context,
            input,
            function,
            AllowParse::new(CalcUnits::RESOLUTION),
        )?
        .to_resolution()
        .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }

    /// Constructs a new calculation tree that replaces leaf nodes with their computed values
    /// if they can only be resolved at computed value time.
    pub fn with_computed_context(&self, context: &computed::Context) -> Self {
        self.map_leaves(|leaf| match leaf {
            // Lengths can contain relative units that can only resolve at computed value time
            Leaf::Length(length) => Leaf::Length(NoCalcLength::from_px(
                length.to_computed_value(context).px(),
            )),
            // Other nodes have been resolved eagerly at parse time
            _ => leaf.clone(),
        })
    }
}
