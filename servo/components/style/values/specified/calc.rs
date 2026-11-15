/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! [Calc expressions][calc].
//!
//! [calc]: https://drafts.csswg.org/css-values/#calc-notation

use crate::color::AbsoluteColor;
use crate::color::parsing::ChannelKeyword;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::typed_om::{NumericBaseType, NumericType, ToTyped, TypedValue};
use crate::values::DashedIdent;
use crate::values::computed::{self, ToComputedValue};
use crate::values::generics::Optional;
use crate::values::generics::calc::{
    self as generic, CalcNodeLeaf, CalcType, GenericAnchorFunctionFallback, GenericRandomFunction,
    MinMaxOp, ModRemOp, ProgressClampingMode, RoundingStrategy, SimplificationResult, SortKey,
};
use crate::values::generics::length::GenericAnchorSizeFunction;
use crate::values::generics::position::{
    AnchorSideKeyword, GenericAnchorFunction, GenericAnchorSide, TreeScoped,
};
use crate::values::specified::length::NoCalcLength;
use crate::values::specified::random::{RandomFunctionKind, RandomKey};
use crate::values::specified::{
    NoCalcAngle, NoCalcNumber, NoCalcPercentage, NoCalcResolution, NoCalcTime, TreeCountingFunction,
};
use cssparser::{CowRcStr, Parser, Token, match_ignore_ascii_case};
use debug_unreachable::debug_unreachable;
use smallvec::SmallVec;
use std::cmp;
use std::convert::AsRef;
use strum::IntoEnumIterator;
use strum_macros::{AsRefStr, EnumIter};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{ParseError, SpecifiedValueInfo, StyleParseErrorKind};
use thin_vec::ThinVec;

/// The name of the mathematical function that we're parsing.
#[derive(AsRefStr, Clone, Copy, Debug, EnumIter, Parse)]
#[strum(serialize_all = "lowercase")]
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
    /// `progress()`: https://drafts.csswg.org/css-values-5/#funcdef-progress
    Progress,
    /// `random()`: https://drafts.csswg.org/css-values-5/#funcdef-random
    Random,
    /// `sibling-count()`: https://drafts.csswg.org/css-values-5/#funcdef-sibling-count
    #[strum(serialize = "sibling-count")]
    SiblingCount,
    /// `sibling-index()`: https://drafts.csswg.org/css-values-5/#funcdef-sibling-index
    #[strum(serialize = "sibling-index")]
    SiblingIndex,
}

impl MathFunction {
    /// Returns an iterator for the enum variants
    pub fn variants() -> MathFunctionIter {
        MathFunction::iter()
    }
}

pub use crate::values::generics::calc::CalcPercentageLeaf;

/// A leaf node inside a `Calc` expression's AST.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem)]
#[repr(u8)]
pub enum Leaf {
    /// `<length>`
    Length(NoCalcLength),
    /// `<angle>`
    Angle(NoCalcAngle),
    /// `<time>`
    Time(NoCalcTime),
    /// `<resolution>`
    Resolution(NoCalcResolution),
    /// A component of a color.
    ColorComponent(ChannelKeyword),
    /// `<percentage>`
    Percentage(CalcPercentageLeaf),
    /// `<number>`
    Number(NoCalcNumber),
    /// A tree-counting function.
    TreeCountingFunction(TreeCountingFunction),
    /// `<random-key>`
    RandomKey(Box<RandomKey>),
}

impl ToTyped for Leaf {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        // XXX Only supporting Length, Number, Percentage, Angle and Time for now
        match *self {
            Self::Length(ref l) => l.to_typed(dest),
            Self::Number(n) => n.to_typed(dest),
            Self::Percentage(ref p) => p.to_typed(dest),
            Self::Angle(ref a) => a.to_typed(dest),
            Self::Time(t) => t.to_typed(dest),
            _ => Err(()),
        }
    }
}

impl Leaf {
    /// Computes this leaf against the given context (if any), substituting color
    /// channel references with the matching channel of `origin_color` when it is
    /// provided. If no origin color is available, channel references are kept
    /// symbolic so they can be resolved later.
    pub fn to_computed_value(
        &self,
        context: Option<&computed::Context>,
        origin_color: Option<&AbsoluteColor>,
    ) -> Result<Self, ()> {
        Ok(match self {
            Self::Length(l) => {
                let px = match context {
                    Some(context) => l.to_computed_value(context).px(),
                    None => l.to_computed_pixel_length_without_context()?,
                };
                Self::Length(NoCalcLength::from_px(px))
            },
            Self::TreeCountingFunction(f) => match context {
                Some(context) => {
                    Self::Number(NoCalcNumber::new(f.to_computed_value(context) as f32))
                },
                None => return Err(()),
            },
            Self::RandomKey(key) => match context {
                Some(context) => Self::Number(NoCalcNumber::new(*key.to_computed_value(context))),
                None => return Err(()),
            },
            Self::ColorComponent(channel_keyword) => {
                let channel_value = origin_color
                    .and_then(|c| c.get_component_by_channel_keyword(*channel_keyword).ok());
                match channel_value {
                    Some(value) => Self::Number(NoCalcNumber::new(value.unwrap_or(0.0))),
                    // If the color needs to get resolved later (there's no absolute origin color),
                    // or the channel is invalid, we still keep the channel keyword around.
                    None => Self::ColorComponent(*channel_keyword),
                }
            },
            // The remaining leaves are already absolute (and thus
            // context-independent).
            Self::Angle(..)
            | Self::Time(..)
            | Self::Resolution(..)
            | Self::Percentage(..)
            | Self::Number(..) => self.clone(),
        })
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

    /// Resolves this calc expression given a computed context, applying clamping.
    pub fn resolve(
        &self,
        context: &computed::Context,
        leaf_to_f32: impl FnOnce(Result<Leaf, ()>) -> f32,
    ) -> f32 {
        let result = self
            .node
            .to_computed_value(Some(context), None)
            .and_then(|r| r.resolve());
        self.clamping_mode.clamp(leaf_to_f32(result))
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
            Ok(Leaf::Percentage(p)) => Some(NoCalcPercentage::new(p.get())),
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

    /// Gets this calc expression as a resolution
    pub fn as_resolution(&self) -> Option<NoCalcResolution> {
        match self.node.resolve() {
            Ok(Leaf::Resolution(r)) => Some(r),
            _ => None,
        }
    }

    /// Gets this calc expression as an angle
    pub fn as_angle(&self) -> Option<NoCalcAngle> {
        match self.node.resolve() {
            Ok(Leaf::Angle(a)) => Some(a),
            _ => None,
        }
    }
}

impl SpecifiedValueInfo for CalcNumeric {}

/// A `calc()` expression that is known to resolve to a `<length-percentage>`.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem, ToTyped)]
pub struct CalcLengthPercentage(pub CalcNumeric);

impl SpecifiedValueInfo for CalcLengthPercentage {}

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
    pub struct AdditionalFunctions: u8 {
        /// `anchor()` function.
        const ANCHOR = 1 << 0;
        /// `anchor-size()` function.
        const ANCHOR_SIZE = 1 << 1;
    }
}

/// Dictates whether percentages are allowed in the calculation that
/// is parsed using this context, and whether such percentages have a
/// known "percent hint" (the type that they will eventually resolve to).
/// https://drafts.csswg.org/css-values-4/#calc-context
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum PercentageContext {
    /// Percentages are not allowed in this calculation context.
    NotAllowed,
    /// Percentages are allowed with the given pecent hint information.
    Allowed(Optional<NumericBaseType>),
}

#[allow(missing_docs)]
impl PercentageContext {
    pub fn not_allowed() -> Self {
        Self::NotAllowed
    }

    pub fn allowed() -> Self {
        Self::Allowed(Optional::None)
    }

    pub fn allowed_with_hint(hint: NumericBaseType) -> Self {
        Self::Allowed(Optional::Some(hint))
    }
}

/// What is allowed to be parsed for math functions within in this context?
#[derive(Clone, Copy)]
pub struct CalcParseFlags {
    /// Whether percentages are allowed in this context, and what numeric type they are relative
    /// to. Used both to control parsing as well as to type check calculation trees.
    pub percentage_context: PercentageContext,
    /// Which relative color components, if any, are allowed.
    pub color_components: ChannelKeyword,
    /// Additional functions allowed to be parsed in this context.
    pub additional_functions: AdditionalFunctions,
    /// Whether or not in place operations should be performed. Normally, we aggressive
    /// simplify via in-place operations, but it is disabled for generating a trace of steps.
    pub in_place_operations: CalcNodeParseInPlaceOperations,
}

impl CalcParseFlags {
    /// Builds parse flags with the given percentage calculation context.
    pub fn new(percentage_context: PercentageContext) -> Self {
        Self {
            percentage_context,
            ..Default::default()
        }
    }
}

impl Default for CalcParseFlags {
    fn default() -> Self {
        Self {
            percentage_context: PercentageContext::not_allowed(),
            color_components: ChannelKeyword::empty(),
            additional_functions: AdditionalFunctions::empty(),
            in_place_operations: CalcNodeParseInPlaceOperations::Yes,
        }
    }
}

impl generic::CalcNodeLeaf for Leaf {
    fn numeric_type(&self) -> NumericType {
        match self {
            Leaf::Length(_) => NumericType::length(),
            Leaf::Angle(_) => NumericType::angle(),
            Leaf::Time(_) => NumericType::time(),
            Leaf::Resolution(_) => NumericType::resolution(),
            Leaf::Percentage(p) => p.numeric_type(),
            Leaf::ColorComponent(_)
            | Leaf::Number(_)
            | Leaf::TreeCountingFunction(_)
            | Leaf::RandomKey(_) => NumericType::number(),
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
            Self::RandomKey(ref k) => {
                return match &**k {
                    RandomKey::Fixed(number) => number.resolve(),
                    RandomKey::CacheKey(_) => None,
                };
            },
            Self::ColorComponent(_) | Self::TreeCountingFunction(_) => return None,
        })
    }

    fn canonical_value(&self) -> Option<f32> {
        Some(match *self {
            Self::Length(ref l) => l.to_px_if_absolute()?,
            Self::Percentage(ref p) => match p.hint {
                // Percentages that are relative to some other value (indicated by a
                // percent hint other than "percent") cannot yet resolve to a numeric
                // value, as the percentage's basis is not available.
                Optional::Some(NumericBaseType::Percent) => p.get(),
                _ => return None,
            },
            Self::Number(ref n) => n.value(),
            Self::Resolution(ref r) => r.dppx(),
            Self::Angle(ref a) => a.degrees(),
            Self::Time(ref t) => t.seconds(),
            Self::RandomKey(ref k) => {
                return match &**k {
                    RandomKey::Fixed(number) => number.resolve(),
                    RandomKey::CacheKey(_) => None,
                };
            },
            Self::ColorComponent(_) | Self::TreeCountingFunction(_) => return None,
        })
    }

    fn is_same_unit_as(&self, other: &Self) -> bool {
        use self::Leaf::*;

        if std::mem::discriminant(self) != std::mem::discriminant(other) {
            return false;
        }

        match (self, other) {
            (Length(a), Length(b)) => a.length_unit() == b.length_unit(),
            (Angle(a), Angle(b)) => a.angle_unit() == b.angle_unit(),
            (Time(a), Time(b)) => a.time_unit() == b.time_unit(),
            (Resolution(a), Resolution(b)) => a.resolution_unit() == b.resolution_unit(),
            (ColorComponent(_), ColorComponent(_))
            | (Percentage(_), Percentage(_))
            | (Number(_), Number(_))
            | (TreeCountingFunction(_), TreeCountingFunction(_))
            | (RandomKey(_), RandomKey(_)) => true,
            _ => {
                match *other {
                    Number(..)
                    | Percentage(..)
                    | Angle(..)
                    | Time(..)
                    | Resolution(..)
                    | Length(..)
                    | ColorComponent(..)
                    | TreeCountingFunction(..)
                    | RandomKey(..) => {},
                }
                unsafe {
                    debug_unreachable!();
                }
            },
        }
    }

    fn as_percentage(&self) -> Option<(f32, Optional<NumericBaseType>)> {
        match *self {
            Self::Percentage(p) => Some((p.get(), p.hint)),
            _ => None,
        }
    }

    fn as_angle_radians(&self) -> Option<f32> {
        if let Self::Angle(ref a) = *self {
            Some(a.radians())
        } else {
            None
        }
    }

    fn new_angle_from_radians(radians: f32) -> Self {
        Self::Angle(NoCalcAngle::from_degrees(radians.to_degrees()))
    }

    fn new_number(value: f32) -> Self {
        Self::Number(NoCalcNumber::new(value))
    }

    fn new_from_typed_value(value: f32, numeric_type: NumericType) -> Result<Self, ()> {
        let calc_type = numeric_type.as_calc_type()?;
        let percent_hint = numeric_type.percent_hint();
        Ok(match calc_type {
            CalcType::Number => Self::new_number(value),
            CalcType::Length => Self::Length(NoCalcLength::from_px(value)),
            CalcType::Angle => Self::Angle(NoCalcAngle::from_degrees(value)),
            CalcType::Time => Self::Time(NoCalcTime::from_seconds(value)),
            CalcType::Resolution => Self::Resolution(NoCalcResolution::from_dppx(value)),
            CalcType::Percentage => Self::Percentage(CalcPercentageLeaf::new(value, percent_hint)),
        })
    }

    fn compare(&self, other: &Self) -> Option<cmp::Ordering> {
        use self::Leaf::*;

        if std::mem::discriminant(self) != std::mem::discriminant(other) {
            return None;
        }

        // Percentages that resolve against some other basis value cannot be meaningfully compared.
        if matches!(self, Percentage(p) if p.hint != Optional::Some(NumericBaseType::Percent)) {
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
            (Percentage(one), Percentage(other)) => one.get().partial_cmp(&other.get()),
            (Length(one), Length(other)) => one.partial_cmp(other),
            (Angle(one), Angle(other)) => one.degrees().partial_cmp(&other.degrees()),
            (Time(one), Time(other)) => one.seconds().partial_cmp(&other.seconds()),
            (Resolution(one), Resolution(other)) => one.dppx().partial_cmp(&other.dppx()),
            (Number(one), Number(other)) => one.partial_cmp(other),
            (ColorComponent(one), ColorComponent(other)) => one.partial_cmp(other),
            (TreeCountingFunction(one), TreeCountingFunction(other)) => one.partial_cmp(other),
            (RandomKey(_), RandomKey(_)) => None,
            _ => {
                match *self {
                    Length(..)
                    | Percentage(..)
                    | Angle(..)
                    | Time(..)
                    | Number(..)
                    | Resolution(..)
                    | ColorComponent(..)
                    | TreeCountingFunction(..)
                    | RandomKey(..) => {},
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
            | Leaf::ColorComponent(_)
            | Leaf::TreeCountingFunction(_)
            | Leaf::RandomKey(_) => None,
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
            Self::Length(ref l) => l.sort_key(),
            Self::ColorComponent(..) => SortKey::ColorComponent,
            Self::TreeCountingFunction(..) | Self::RandomKey(..) => SortKey::Other,
        }
    }

    fn simplify(&mut self) -> SimplificationResult {
        match self {
            Leaf::Length(l) => {
                if let Some(px) = l.to_px_if_absolute() {
                    *l = NoCalcLength::from_px(px);
                    return SimplificationResult::Simplified;
                }
            },
            Leaf::Resolution(r) => {
                *r = NoCalcResolution::from_dppx(r.dppx());
                return SimplificationResult::Simplified;
            },
            Leaf::Time(t) => {
                *t = NoCalcTime::from_seconds(t.seconds());
                return SimplificationResult::Simplified;
            },
            Leaf::Angle(a) => {
                *a = NoCalcAngle::from_degrees(a.degrees());
                return SimplificationResult::Simplified;
            },
            _ => (),
        }
        SimplificationResult::Unchanged
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
            (&mut Number(ref mut one), Number(other)) => {
                *one = NoCalcNumber::new(one.value() + other.value());
            },
            (&mut Percentage(ref mut one), Percentage(other)) => {
                *one = CalcPercentageLeaf::new(one.get() + other.get(), one.combined_hint(other));
            },
            (&mut Angle(ref mut one), Angle(other)) => {
                *one = NoCalcAngle::from_degrees(one.degrees() + other.degrees());
            },
            (&mut Time(ref mut one), Time(other)) => {
                *one = NoCalcTime::from_seconds(one.seconds() + other.seconds());
            },
            (&mut Resolution(ref mut one), Resolution(other)) => {
                *one = NoCalcResolution::from_dppx(one.dppx() + other.dppx());
            },
            (&mut Length(ref mut one), Length(other)) => {
                *one = one.try_op(other, std::ops::Add::add)?;
            },
            (&mut ColorComponent(_), &ColorComponent(_)) => {
                // Can not get the sum of color components, because they haven't been resolved yet.
                return Err(());
            },
            (&mut TreeCountingFunction(_), &TreeCountingFunction(_)) => {
                // Can not get the sum of tree counting functions, because they haven't been resolved yet.
                return Err(());
            },
            (&mut RandomKey(_), &RandomKey(_)) => {
                // Can not get the sum of random cache keys.
                return Err(());
            },
            _ => {
                match *other {
                    Number(..)
                    | Percentage(..)
                    | Angle(..)
                    | Time(..)
                    | Resolution(..)
                    | Length(..)
                    | ColorComponent(..)
                    | TreeCountingFunction(..)
                    | RandomKey(..) => {},
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
            (&Number(one), &Number(other)) => Ok(Leaf::Number(NoCalcNumber::new(op(
                one.value(),
                other.value(),
            )))),
            (Percentage(one), Percentage(other)) => Ok(Leaf::Percentage(CalcPercentageLeaf::new(
                op(one.get(), other.get()),
                one.combined_hint(other),
            ))),
            (Angle(one), Angle(other)) => Ok(Leaf::Angle(NoCalcAngle::from_degrees(op(
                one.degrees(),
                other.degrees(),
            )))),
            (Resolution(one), Resolution(other)) => Ok(Leaf::Resolution(
                NoCalcResolution::from_dppx(op(one.dppx(), other.dppx())),
            )),
            (Time(one), Time(other)) => Ok(Leaf::Time(NoCalcTime::from_seconds(op(
                one.seconds(),
                other.seconds(),
            )))),
            (Length(one), Length(other)) => Ok(Leaf::Length(one.try_op(other, op)?)),
            (&ColorComponent(..), &ColorComponent(..)) => Err(()),
            (&TreeCountingFunction(_), &TreeCountingFunction(_)) => Err(()),
            (&RandomKey(_), &RandomKey(_)) => Err(()),
            _ => {
                match *other {
                    Number(..)
                    | Percentage(..)
                    | Angle(..)
                    | Time(..)
                    | Length(..)
                    | Resolution(..)
                    | ColorComponent(..)
                    | TreeCountingFunction(..)
                    | RandomKey(..) => {},
                }
                unsafe {
                    debug_unreachable!();
                }
            },
        }
    }

    fn map(&mut self, mut op: impl FnMut(f32) -> f32) -> Result<(), ()> {
        let _: () = match self {
            Leaf::Length(one) => *one = one.map(op),
            Leaf::Angle(one) => *one = NoCalcAngle::from_degrees(op(one.degrees())),
            Leaf::Time(one) => *one = NoCalcTime::from_seconds(op(one.seconds())),
            Leaf::Resolution(one) => *one = NoCalcResolution::from_dppx(op(one.dppx())),
            Leaf::Percentage(one) => *one = CalcPercentageLeaf::new(op(one.get()), one.hint),
            Leaf::Number(one) => *one = NoCalcNumber::new(op(one.value())),
            Leaf::ColorComponent(..) | Leaf::TreeCountingFunction(..) | Leaf::RandomKey(..) => {
                return Err(());
            },
        };
        Ok(())
    }

    fn should_serialize_with_root_calc_wrapper(&self) -> bool {
        match self {
            Leaf::Length(_)
            | Leaf::Angle(_)
            | Leaf::Time(_)
            | Leaf::Resolution(_)
            | Leaf::ColorComponent(_)
            | Leaf::Percentage(_)
            | Leaf::Number(_) => true,
            Leaf::TreeCountingFunction(_) | Leaf::RandomKey(_) => false,
        }
    }
}

impl GenericAnchorSide<Box<CalcNode>> {
    fn parse_in_calc(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if let Ok(k) = input.try_parse(|i| AnchorSideKeyword::parse(i)) {
            return Ok(Self::Keyword(k));
        }
        Ok(Self::Percentage(Box::new(CalcNode::parse_argument(
            context,
            input,
            CalcParseFlags::new(PercentageContext::allowed_with_hint(
                NumericBaseType::Percent,
            )),
        )?)))
    }
}

fn parse_anchor_function_fallback(
    context: &ParserContext,
    additional_functions: AdditionalFunctions,
    input: &mut Parser,
) -> Result<Box<GenericAnchorFunctionFallback<Leaf>>, ParseError> {
    if let Ok(l) = input.try_parse(|i| -> Result<CalcNode, ParseError> {
        Ok(CalcNode::Leaf(match *(i.next()?) {
            Token::Number { value, .. } => {
                if value != 0.0 {
                    return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                }
                Leaf::Length(NoCalcLength::from_px(0.0))
            },
            Token::Dimension {
                value, ref unit, ..
            } => Leaf::Length(
                NoCalcLength::parse_dimension_with_context(context, value, unit)
                    .map_err(|_| ParseError::custom(StyleParseErrorKind::UnspecifiedError))?,
            ),
            Token::Percentage { unit_value, .. } => Leaf::Percentage(CalcPercentageLeaf::new(
                unit_value,
                Optional::Some(NumericBaseType::Length),
            )),
            _ => return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError)),
        }))
    }) {
        return Ok(Box::new(GenericAnchorFunctionFallback::new(false, l)));
    }
    let node = CalcNode::parse_argument(
        context,
        input,
        CalcParseFlags {
            additional_functions,
            percentage_context: PercentageContext::allowed_with_hint(NumericBaseType::Length),
            ..Default::default()
        },
    )?
    .into_length_or_percentage(AllowedNumericType::All)
    .map_err(|_| ParseError::custom(StyleParseErrorKind::UnspecifiedError))?
    .0
    .node;
    Ok(Box::new(GenericAnchorFunctionFallback::new(true, node)))
}

impl GenericAnchorFunction<Box<CalcNode>, Box<GenericAnchorFunctionFallback<Leaf>>> {
    fn parse_in_calc(
        context: &ParserContext,
        additional_functions: AdditionalFunctions,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        if !crate::pref!("layout.css.anchor-positioning.enabled", gecko = true) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
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
    fn parse_in_calc(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if !crate::pref!("layout.css.anchor-positioning.enabled", gecko = true) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
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

/// Whether in place operations should be done when parsing expressions to create CalcNode
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum CalcNodeParseInPlaceOperations {
    /// Avoid in place operations
    No,
    /// Alow in place operations
    Yes,
}

/// A calc node representation for specified values.
pub type SpecifiedCalcNode = generic::GenericCalcNode<Leaf>;
pub use self::SpecifiedCalcNode as CalcNode;

impl CalcNode {
    /// Tries to parse a single element in the expression, that is, a
    /// `<length>`, `<angle>`, `<time>`, `<percentage>`, `<resolution>`, etc.
    ///
    /// May return a "complex" `CalcNode`, in the presence of a parenthesized
    /// expression, for example.
    fn parse_one(
        context: &ParserContext,
        input: &mut Parser,
        flags: CalcParseFlags,
    ) -> Result<Self, ParseError> {
        match input.next()? {
            &Token::Number { value, .. } => {
                Ok(CalcNode::Leaf(Leaf::Number(NoCalcNumber::new(value))))
            },
            &Token::Dimension {
                value, ref unit, ..
            } => {
                if let Ok(l) = NoCalcLength::parse_dimension_with_context(context, value, unit) {
                    return Ok(CalcNode::Leaf(Leaf::Length(l)));
                }
                if let Ok(a) = NoCalcAngle::parse_dimension(value, unit) {
                    return Ok(CalcNode::Leaf(Leaf::Angle(a)));
                }
                if let Ok(t) = NoCalcTime::parse_dimension(value, unit) {
                    return Ok(CalcNode::Leaf(Leaf::Time(t)));
                }
                if let Ok(t) = NoCalcResolution::parse_dimension(value, unit) {
                    return Ok(CalcNode::Leaf(Leaf::Resolution(t)));
                }
                Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError))
            },
            &Token::Percentage { unit_value, .. } => {
                let hint = match flags.percentage_context {
                    PercentageContext::NotAllowed => {
                        return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                    },
                    PercentageContext::Allowed(hint) => hint,
                };
                Ok(CalcNode::Leaf(Leaf::Percentage(CalcPercentageLeaf::new(
                    unit_value, hint,
                ))))
            },
            &Token::ParenthesisBlock => {
                input.parse_nested_block(|input| CalcNode::parse_argument(context, input, flags))
            },
            Token::Function(name)
                if flags
                    .additional_functions
                    .intersects(AdditionalFunctions::ANCHOR)
                    && name.eq_ignore_ascii_case("anchor") =>
            {
                let anchor_function = GenericAnchorFunction::parse_in_calc(
                    context,
                    flags.additional_functions,
                    input,
                )?;
                Ok(CalcNode::Anchor(Box::new(anchor_function)))
            },
            Token::Function(name)
                if flags
                    .additional_functions
                    .intersects(AdditionalFunctions::ANCHOR_SIZE)
                    && name.eq_ignore_ascii_case("anchor-size") =>
            {
                let anchor_size_function =
                    GenericAnchorSizeFunction::parse_in_calc(context, input)?;
                Ok(CalcNode::AnchorSize(Box::new(anchor_size_function)))
            },
            Token::Function(name) => {
                let function = CalcNode::math_function(context, name)?;
                CalcNode::parse(context, input, function, flags)
            },
            Token::Ident(ident) => {
                let leaf = match_ignore_ascii_case! { &**ident,
                    "e" => Leaf::Number(NoCalcNumber::new(std::f32::consts::E)),
                    "pi" => Leaf::Number(NoCalcNumber::new(std::f32::consts::PI)),
                    "infinity" => Leaf::Number(NoCalcNumber::new(f32::INFINITY)),
                    "-infinity" => Leaf::Number(NoCalcNumber::new(f32::NEG_INFINITY)),
                    "nan" => Leaf::Number(NoCalcNumber::new(f32::NAN)),
                    _ => {
                        match ChannelKeyword::from_ident(ident) {
                            Ok(channel_keyword) if flags.color_components.contains(channel_keyword) => Leaf::ColorComponent(channel_keyword),
                            _ => return Err(ParseError::unexpected_token()),
                        }
                    },
                };
                Ok(CalcNode::Leaf(leaf))
            },
            _ => Err(ParseError::unexpected_token()),
        }
    }

    /// Parse a top-level `calc` expression, with all nested sub-expressions.
    ///
    /// This is in charge of parsing, for example, `2 + 3 * 100%`.
    pub fn parse(
        context: &ParserContext,
        input: &mut Parser,
        function: MathFunction,
        flags: CalcParseFlags,
    ) -> Result<Self, ParseError> {
        input.parse_nested_block(|input| {
            fn consistent_type(a: &CalcNode, b: &CalcNode) -> Result<CalcType, ()> {
                let a_ty = a.numeric_type()?;
                let b_ty = b.numeric_type()?;
                NumericType::add_two_types(&a_ty, &b_ty).and_then(|ty| ty.as_calc_type())
            }

            fn consistent_type_multi(arguments: &[CalcNode]) -> Result<CalcType, ()> {
                let mut ty = arguments.first().unwrap().numeric_type()?;
                for arg in arguments.iter().skip(1) {
                    let arg_ty = arg.numeric_type()?;
                    ty = NumericType::add_two_types(&ty, &arg_ty)?;
                }
                ty.as_calc_type()
            }

            macro_rules! require_consistent_type {
                ($a:expr, $b:expr) => {{
                    let _ = consistent_type(&$a, &$b)
                        .map_err(|_| ParseError::custom(StyleParseErrorKind::UnspecifiedError))?;
                }};
                ($nodes:expr) => {{
                    let _ = consistent_type_multi(&$nodes)
                        .map_err(|_| ParseError::custom(StyleParseErrorKind::UnspecifiedError))?;
                }};
            }

            match function {
                MathFunction::Calc => Self::parse_argument(context, input, flags),
                MathFunction::Clamp => {
                    let min_val = if input
                        .try_parse(|min| min.expect_ident_matching("none"))
                        .ok()
                        .is_none()
                    {
                        Some(Self::parse_argument(context, input, flags)?)
                    } else {
                        None
                    };

                    input.expect_comma()?;
                    let center = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;

                    let max_val = if input
                        .try_parse(|max| max.expect_ident_matching("none"))
                        .ok()
                        .is_none()
                    {
                        Some(Self::parse_argument(context, input, flags)?)
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
                        (None, Some(max)) => {
                            require_consistent_type!(center, max);
                            Self::MinMax(vec![center, max].into(), MinMaxOp::Min)
                        },
                        (Some(min), None) => {
                            require_consistent_type!(min, center);
                            Self::MinMax(vec![min, center].into(), MinMaxOp::Max)
                        },
                        (Some(min), Some(max)) => {
                            require_consistent_type!(min, center);
                            require_consistent_type!(center, max);
                            require_consistent_type!(min, max);
                            Self::Clamp {
                                min: Box::new(min),
                                center: Box::new(center),
                                max: Box::new(max),
                            }
                        },
                    })
                },
                MathFunction::Round => {
                    let strategy = input.try_parse(parse_rounding_strategy);

                    // <rounding-strategy> = nearest | up | down | to-zero
                    // https://drafts.csswg.org/css-values-4/#calc-syntax
                    fn parse_rounding_strategy(
                        input: &mut Parser,
                    ) -> Result<RoundingStrategy, ParseError> {
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

                    let value = Self::parse_argument(context, input, flags)?;

                    // <step> defaults to the number 1 if not provided
                    // https://drafts.csswg.org/css-values-4/#funcdef-round
                    let step = input.try_parse(|input| {
                        input.expect_comma()?;
                        Self::parse_argument(context, input, flags)
                    });

                    let step = step.unwrap_or(Self::Leaf(Leaf::Number(NoCalcNumber::new(1.0))));
                    require_consistent_type!(value, step);

                    Ok(Self::Round {
                        strategy: strategy.unwrap_or(RoundingStrategy::Nearest),
                        value: Box::new(value),
                        step: Box::new(step),
                    })
                },
                MathFunction::Mod | MathFunction::Rem => {
                    let dividend = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;
                    let divisor = Self::parse_argument(context, input, flags)?;
                    require_consistent_type!(dividend, divisor);

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
                        let result = Self::parse_argument(context, input, flags)?;
                        Ok(result)
                    })?;
                    require_consistent_type!(arguments);

                    let op = match function {
                        MathFunction::Min => MinMaxOp::Min,
                        MathFunction::Max => MinMaxOp::Max,
                        _ => unreachable!(),
                    };

                    Ok(Self::MinMax(arguments.into(), op))
                },
                MathFunction::Sin | MathFunction::Cos | MathFunction::Tan => {
                    let node = Self::parse_argument(context, input, flags)?;
                    Ok(match function {
                        MathFunction::Sin => Self::Sin(Box::new(node)),
                        MathFunction::Cos => Self::Cos(Box::new(node)),
                        MathFunction::Tan => Self::Tan(Box::new(node)),
                        _ => unsafe { debug_unreachable!("We just checked!") },
                    })
                },
                MathFunction::Asin | MathFunction::Acos | MathFunction::Atan => {
                    let node = Self::parse_argument(context, input, flags)?;
                    Ok(match function {
                        MathFunction::Asin => Self::Asin(Box::new(node)),
                        MathFunction::Acos => Self::Acos(Box::new(node)),
                        MathFunction::Atan => Self::Atan(Box::new(node)),
                        _ => unsafe { debug_unreachable!("We just checked!") },
                    })
                },
                MathFunction::Atan2 => {
                    let a = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;
                    let b = Self::parse_argument(context, input, flags)?;
                    require_consistent_type!(a, b);
                    Ok(Self::Atan2(Box::new(a), Box::new(b)))
                },
                MathFunction::Pow => {
                    let a = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;
                    let b = Self::parse_argument(context, input, flags)?;
                    Ok(Self::Pow(Box::new(a), Box::new(b)))
                },
                MathFunction::Sqrt => {
                    let a = Self::parse_argument(context, input, flags)?;
                    Ok(Self::Sqrt(Box::new(a)))
                },
                MathFunction::Hypot => {
                    let arguments = input.parse_comma_separated(|input| {
                        let result = Self::parse_argument(context, input, flags)?;
                        Ok(result)
                    })?;
                    require_consistent_type!(arguments);
                    Ok(Self::Hypot(arguments.into()))
                },
                MathFunction::Log => {
                    let a = Self::parse_argument(context, input, flags)?;
                    let b = input
                        .try_parse(|input| {
                            input.expect_comma()?;
                            Self::parse_argument(context, input, flags)
                        })
                        .ok();
                    Ok(Self::Log(Box::new(a), b.map(Box::new).into()))
                },
                MathFunction::Exp => {
                    let a = Self::parse_argument(context, input, flags)?;
                    Ok(Self::Exp(Box::new(a)))
                },
                MathFunction::Abs => {
                    let node = Self::parse_argument(context, input, flags)?;
                    Ok(Self::Abs(Box::new(node)))
                },
                MathFunction::Sign => {
                    let node = Self::parse_argument(context, input, flags)?;
                    Ok(Self::Sign(Box::new(node)))
                },
                MathFunction::Progress => {
                    if !crate::pref!("layout.css.progress-function.enabled") {
                        return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                    }

                    let clamping_mode = input
                        .try_parse(|i| ProgressClampingMode::parse(i))
                        .unwrap_or(ProgressClampingMode::Clamp);

                    let value = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;
                    let start = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;
                    let end = Self::parse_argument(context, input, flags)?;

                    require_consistent_type!(value, start);
                    require_consistent_type!(value, end);
                    require_consistent_type!(start, end);

                    Ok(Self::Progress {
                        clamping_mode,
                        value: Box::new(value),
                        start: Box::new(start),
                        end: Box::new(end),
                    })
                },
                MathFunction::Random => {
                    if !crate::pref!("layout.css.random.enabled") {
                        return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                    }

                    let index = context
                        .property_declaration_context
                        .increment_random_function_count(RandomFunctionKind::Random);
                    let key = RandomKey::parse_optional(context, input, index)?;
                    let min = Self::parse_argument(context, input, flags)?;
                    input.expect_comma()?;
                    let max = Self::parse_argument(context, input, flags)?;
                    let step = input.try_parse(|input| {
                        input.expect_comma()?;
                        Self::parse_argument(context, input, flags)
                    });

                    Ok(Self::Random(Box::new(GenericRandomFunction {
                        key: Self::Leaf(Leaf::RandomKey(Box::new(key))),
                        min,
                        max,
                        step: step.ok().into(),
                    })))
                },
                MathFunction::SiblingCount | MathFunction::SiblingIndex => {
                    if !crate::pref!("layout.css.tree-counting-functions.enabled") {
                        return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                    }

                    if !context.has_element_context() {
                        return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                    }

                    // Tree-counting functions have no arguments
                    input.expect_exhausted()?;

                    Ok(Self::Leaf(Leaf::TreeCountingFunction(match function {
                        MathFunction::SiblingCount => TreeCountingFunction::SiblingCount,
                        MathFunction::SiblingIndex => TreeCountingFunction::SiblingIndex,
                        _ => unsafe { debug_unreachable!("We just checked!") },
                    })))
                },
            }
        })
    }

    fn parse_argument(
        context: &ParserContext,
        input: &mut Parser,
        flags: CalcParseFlags,
    ) -> Result<Self, ParseError> {
        let mut sum = SmallVec::<[CalcNode; 1]>::new();
        let first = Self::parse_product(context, input, flags)?;
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
                            let rhs = Self::parse_product(context, input, flags)?;
                            if flags.in_place_operations == CalcNodeParseInPlaceOperations::No
                                || sum.last_mut().unwrap().try_sum_in_place(&rhs).is_err()
                            {
                                sum.push(rhs);
                            }
                        },
                        Token::Delim('-') => {
                            let mut rhs = Self::parse_product(context, input, flags)?;
                            rhs.negate();
                            if flags.in_place_operations == CalcNodeParseInPlaceOperations::No
                                || sum.last_mut().unwrap().try_sum_in_place(&rhs).is_err()
                            {
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
    fn parse_product(
        context: &ParserContext,
        input: &mut Parser,
        flags: CalcParseFlags,
    ) -> Result<Self, ParseError> {
        let mut product = SmallVec::<[CalcNode; 1]>::new();
        let first = Self::parse_one(context, input, flags)?;
        product.push(first);

        loop {
            let start = input.state();
            match input.next() {
                Ok(&Token::Delim('*')) => {
                    let mut rhs = Self::parse_one(context, input, flags)?;

                    // We can unwrap here, because we start the function by adding a node to
                    // the list.
                    if flags.in_place_operations == CalcNodeParseInPlaceOperations::No
                        || !product.last_mut().unwrap().try_product_in_place(&mut rhs)
                    {
                        product.push(rhs);
                    }
                },
                Ok(&Token::Delim('/')) => {
                    let rhs = Self::parse_one(context, input, flags)?;

                    enum InPlaceDivisionResult {
                        /// The right was merged into the left.
                        Merged,
                        /// The right is not a number or could not be resolved, so the left is
                        /// unchanged.
                        Unchanged,
                        /// The division should have been applied in-place, but could not due
                        /// to an error, making the calculation invalid.
                        Invalid,
                    }

                    fn try_division_in_place(
                        left: &mut CalcNode,
                        right: &CalcNode,
                        in_place_operations: CalcNodeParseInPlaceOperations,
                    ) -> InPlaceDivisionResult {
                        if in_place_operations == CalcNodeParseInPlaceOperations::No {
                            return InPlaceDivisionResult::Unchanged;
                        }

                        if let Ok(resolved) = right.resolve()
                            && let Some(number) = resolved.as_number()
                            && number != 1.0
                            && left.is_product_distributive()
                        {
                            if left.map(|l| l / number).is_err() {
                                return InPlaceDivisionResult::Invalid;
                            }
                            return InPlaceDivisionResult::Merged;
                        }
                        InPlaceDivisionResult::Unchanged
                    }

                    // If the left-hand side supported in-place division and the right-hand
                    // side was a resolved number, then the division was already applied
                    // and merged, so no further work is required. Otherwise, the right-hand
                    // side is emitted as an Invert node.
                    match try_division_in_place(
                        product.last_mut().unwrap(),
                        &rhs,
                        flags.in_place_operations,
                    ) {
                        InPlaceDivisionResult::Merged => {},
                        InPlaceDivisionResult::Unchanged => {
                            product.push(Self::Invert(Box::new(rhs)))
                        },
                        InPlaceDivisionResult::Invalid => {
                            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
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

    /// Computes this calc tree against the given context (if any), resolving
    /// context-dependent leaves (e.g. lengths) and substituting color channel
    /// references against `origin_color` when provided.
    pub fn to_computed_value(
        &self,
        context: Option<&computed::Context>,
        origin_color: Option<&AbsoluteColor>,
    ) -> Result<Self, ()> {
        let mut failed = false;
        let result = self.map_leaves(|leaf| {
            match leaf.to_computed_value(context, origin_color) {
                Ok(c) => c,
                Err(()) => {
                    // XXX a bit hacky way of propagating the error...
                    failed = true;
                    leaf.clone()
                },
            }
        });
        if failed {
            return Err(());
        }
        return Ok(result);
    }

    /// Tries to simplify this expression into a `<length>` value. Used for properties that
    /// accept `<length>` but not `<length-percentage>`.
    pub fn into_length(
        mut self,
        clamping_mode: AllowedNumericType,
    ) -> Result<CalcLengthPercentage, ()> {
        self.simplify_and_sort();

        if self.numeric_type_as_calc_type()? != CalcType::Length {
            return Err(());
        }

        Ok(CalcLengthPercentage(CalcNumeric {
            clamping_mode,
            node: self,
        }))
    }

    /// Tries to simplify this expression into a `<length>` or `<percentage>`
    /// value.
    pub fn into_length_or_percentage(
        mut self,
        clamping_mode: AllowedNumericType,
    ) -> Result<CalcLengthPercentage, ()> {
        self.simplify_and_sort();

        let ty = self.numeric_type_as_calc_type()?;
        if ty != CalcType::Length && ty != CalcType::Percentage {
            return Err(());
        }

        Ok(CalcLengthPercentage(CalcNumeric {
            clamping_mode,
            node: self,
        }))
    }

    /// Tries to simplify this expression into a `<time>` value.
    fn into_time(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        if self.numeric_type_as_calc_type()? != CalcType::Time {
            return Err(());
        }

        Ok(CalcNumeric {
            clamping_mode,
            node: self,
        })
    }

    /// Tries to simplify this expression into a `<resolution>` value.
    fn into_resolution(mut self) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        if self.numeric_type_as_calc_type()? != CalcType::Resolution {
            return Err(());
        }

        Ok(CalcNumeric {
            clamping_mode: AllowedNumericType::NonNegative,
            node: self,
        })
    }

    /// Tries to simplify this expression into a `CalcNumeric` value.
    fn into_angle(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        if self.numeric_type_as_calc_type()? != CalcType::Angle {
            return Err(());
        }

        Ok(CalcNumeric {
            clamping_mode,
            node: self,
        })
    }

    /// Tries to convert this expression into a `CalcNumeric`, keeping the
    /// AST for later evaluation at computed-value time.
    fn into_number(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        if self.numeric_type_as_calc_type()? != CalcType::Number {
            return Err(());
        }

        Ok(CalcNumeric {
            clamping_mode,
            node: self,
        })
    }

    /// Tries to convert this expression into a `CalcNumeric`, keeping the
    /// AST for later evaluation at computed-value time.
    fn into_percentage(mut self, clamping_mode: AllowedNumericType) -> Result<CalcNumeric, ()> {
        self.simplify_and_sort();

        if self.numeric_type_as_calc_type()? != CalcType::Percentage {
            return Err(());
        }

        Ok(CalcNumeric {
            clamping_mode,
            node: self,
        })
    }

    /// Given a function name, and the location from where the token came from,
    /// return a mathematical function corresponding to that name or an error.
    #[inline]
    pub fn math_function<'i>(
        _: &ParserContext,
        name: &CowRcStr<'i>,
    ) -> Result<MathFunction, ParseError> {
        let function = match MathFunction::from_ident(name) {
            Ok(f) => f,
            Err(()) => return Err(ParseError::unexpected_token()),
        };

        Ok(function)
    }

    /// Convenience parsing function for `<length> | <percentage>`, and, optionally, `anchor()`.
    pub fn parse_length_or_percentage(
        context: &ParserContext,
        input: &mut Parser,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
        allow_anchor: AllowAnchorPositioningFunctions,
    ) -> Result<CalcLengthPercentage, ParseError> {
        let percentage_context = PercentageContext::allowed_with_hint(NumericBaseType::Length);
        let additional_functions = match allow_anchor {
            AllowAnchorPositioningFunctions::No => AdditionalFunctions::empty(),
            AllowAnchorPositioningFunctions::AllowAnchorSize => AdditionalFunctions::ANCHOR_SIZE,
            AllowAnchorPositioningFunctions::AllowAnchorAndAnchorSize => {
                AdditionalFunctions::ANCHOR | AdditionalFunctions::ANCHOR_SIZE
            },
        };
        let flags = CalcParseFlags {
            additional_functions,
            percentage_context,
            ..Default::default()
        };
        Self::parse(context, input, function, flags)?
            .into_length_or_percentage(clamping_mode)
            .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for percentages.
    pub fn parse_percentage(
        context: &ParserContext,
        input: &mut Parser,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
    ) -> Result<CalcNumeric, ParseError> {
        Self::parse(
            context,
            input,
            function,
            CalcParseFlags::new(PercentageContext::allowed_with_hint(
                NumericBaseType::Percent,
            )),
        )?
        .into_percentage(clamping_mode)
        .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<length>`.
    pub fn parse_length(
        context: &ParserContext,
        input: &mut Parser,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
        percentage_context: PercentageContext,
    ) -> Result<CalcLengthPercentage, ParseError> {
        Self::parse(
            context,
            input,
            function,
            CalcParseFlags::new(percentage_context),
        )?
        .into_length(clamping_mode)
        .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<number>`.
    pub fn parse_number(
        context: &ParserContext,
        input: &mut Parser,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
        percentage_context: PercentageContext,
    ) -> Result<CalcNumeric, ParseError> {
        Self::parse(
            context,
            input,
            function,
            CalcParseFlags::new(percentage_context),
        )?
        .into_number(clamping_mode)
        .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<angle>`.
    pub fn parse_angle(
        context: &ParserContext,
        input: &mut Parser,
        function: MathFunction,
        percentage_context: PercentageContext,
    ) -> Result<CalcNumeric, ParseError> {
        Self::parse(
            context,
            input,
            function,
            CalcParseFlags::new(percentage_context),
        )?
        .into_angle(AllowedNumericType::All)
        .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<time>`.
    pub fn parse_time(
        context: &ParserContext,
        input: &mut Parser,
        clamping_mode: AllowedNumericType,
        function: MathFunction,
        percentage_context: PercentageContext,
    ) -> Result<CalcNumeric, ParseError> {
        Self::parse(
            context,
            input,
            function,
            CalcParseFlags::new(percentage_context),
        )?
        .into_time(clamping_mode)
        .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }

    /// Convenience parsing function for `<resolution>`.
    pub fn parse_resolution(
        context: &ParserContext,
        input: &mut Parser,
        function: MathFunction,
        percentage_context: PercentageContext,
    ) -> Result<CalcNumeric, ParseError> {
        Self::parse(
            context,
            input,
            function,
            CalcParseFlags::new(percentage_context),
        )?
        .into_resolution()
        .map_err(|()| ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }
}
