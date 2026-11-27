/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! [Calc expressions][calc].
//!
//! [calc]: https://drafts.csswg.org/css-values/#calc-notation

use crate::derives::*;
use crate::typed_om::{
    MathClamp, MathInvert, MathMax, MathMin, MathNegate, MathProduct, MathSum, MathValue,
    NumericBaseType, NumericType, NumericValue, ToTyped, TypedValue,
};
use crate::values::calc_random;
use crate::values::specified::NoCalcPercentage;
use crate::values::generics::Optional;
use crate::values::generics::length::GenericAnchorSizeFunction;
use crate::values::generics::position::{GenericAnchorFunction, GenericAnchorSide};
use num_traits::Zero;
use smallvec::SmallVec;
use std::convert::AsRef;
use std::fmt::{self, Write};
use std::ops::{Add, Mul, Rem, Sub};
use std::{cmp, mem};
use strum_macros::AsRefStr;
use style_traits::{CssWriter, ToCss};

use thin_vec::ThinVec;

/// Whether we're a `min` or `max` function.
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum MinMaxOp {
    /// `min()`
    Min,
    /// `max()`
    Max,
}

/// Whether we're a `mod` or `rem` function.
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum ModRemOp {
    /// `mod()`
    Mod,
    /// `rem()`
    Rem,
}

impl ModRemOp {
    fn apply(self, dividend: f32, divisor: f32) -> f32 {
        // In mod(A, B) only, if B is infinite and A has opposite sign to B
        // (including an oppositely-signed zero), the result is NaN.
        // https://drafts.csswg.org/css-values/#round-infinities
        if matches!(self, Self::Mod)
            && divisor.is_infinite()
            && dividend.is_sign_negative() != divisor.is_sign_negative()
        {
            return f32::NAN;
        }

        let (r, same_sign_as) = match self {
            Self::Mod => (dividend - divisor * (dividend / divisor).floor(), divisor),
            Self::Rem => (dividend - divisor * (dividend / divisor).trunc(), dividend),
        };
        if r == 0.0 && same_sign_as.is_sign_negative() {
            -0.0
        } else {
            r
        }
    }
}

/// The strategy used in `round()`
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum RoundingStrategy {
    /// `round(nearest, a, b)`
    /// round a to the nearest multiple of b
    Nearest,
    /// `round(up, a, b)`
    /// round a up to the nearest multiple of b
    Up,
    /// `round(down, a, b)`
    /// round a down to the nearest multiple of b
    Down,
    /// `round(to-zero, a, b)`
    /// round a to the nearest multiple of b that is towards zero
    ToZero,
}

/// The clamping mode used in `progress()`
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum ProgressClampingMode {
    /// `progress(value, start, end)`
    /// Progress result is clamped to the range [0, 1}.
    #[css(skip)]
    Clamp,
    /// `progress(no-clamp value, start, end)`
    /// Progress result can be any number.
    NoClamp,
}

impl ProgressClampingMode {
    fn evaluate(self, value: f32, start: f32, end: f32) -> f32 {
        if start == end && self == Self::Clamp {
            return 0.;
        }
        let progress = crate::values::normalize((value - start) / (end - start));
        match self {
            Self::Clamp => progress.max(0.).min(1.),
            Self::NoClamp => progress,
        }
    }
}

/// This determines the order in which we serialize members of a calc() sum.
///
/// See https://drafts.csswg.org/css-values-4/#sort-a-calculations-children
#[derive(
    AsRefStr, Clone, Copy, Debug, Eq, Ord, Parse, PartialEq, PartialOrd, MallocSizeOf, ToShmem,
)]
#[strum(serialize_all = "lowercase")]
#[allow(missing_docs)]
pub enum SortKey {
    #[strum(serialize = "")]
    Number,
    #[css(skip)]
    #[strum(serialize = "%")]
    Percentage,
    Cap,
    Ch,
    Cqb,
    Cqh,
    Cqi,
    Cqmax,
    Cqmin,
    Cqw,
    Deg,
    Dppx,
    Dvb,
    Dvh,
    Dvi,
    Dvmax,
    Dvmin,
    Dvw,
    Em,
    Ex,
    Ic,
    Lh,
    Lvb,
    Lvh,
    Lvi,
    Lvmax,
    Lvmin,
    Lvw,
    Ms,
    Px,
    Rcap,
    Rch,
    Rem,
    Rex,
    Ric,
    Rlh,
    S, // Sec
    Svb,
    Svh,
    Svi,
    Svmax,
    Svmin,
    Svw,
    Vb,
    Vh,
    Vi,
    Vmax,
    Vmin,
    Vw,
    #[css(skip)]
    ColorComponent,
    #[css(skip)]
    Other,
}

/// Fallback type for anchor functions within `calc()`.
/// Ideally, the fallback type is initial type of the property (e.g.
/// `GenericInset` for `left`), but that causes circular reference.
/// TODO(dshin, bug 2034100): Investigate ways to not require this.
/// This handles the parsing of unitless zeros, as well as ensuring
/// that e.g. `calc(anchor(--foo left, 1px) + 10%)` round trips
/// (sorting aside), instead of becoming
/// `calc(anchor(--foo left, calc(1px)) + 10%)`.
#[repr(C)]
#[derive(
    Clone,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToResolvedValue,
    ToShmem,
)]
pub struct GenericAnchorFunctionFallback<L> {
    /// Was this node parsed as a calc node?
    #[animation(constant)]
    is_calc_node: bool,
    /// The parsed fallback value. Stored as a calc node to break
    /// the circular reference.
    pub node: GenericCalcNode<L>,
}

impl<L> GenericAnchorFunctionFallback<L> {
    /// Create a new anchor function fallback value.
    pub fn new(is_calc_node: bool, node: GenericCalcNode<L>) -> Self {
        Self { is_calc_node, node }
    }
}

impl<L: CalcNodeLeaf> ToCss for GenericAnchorFunctionFallback<L> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.node.to_css_impl(
            dest,
            if self.is_calc_node {
                ArgumentLevel::CalculationRoot
            } else {
                ArgumentLevel::ArgumentRoot
            },
        )
    }
}

/// `anchor()` function used in math functions.
pub type GenericCalcAnchorFunction<L> =
    GenericAnchorFunction<Box<GenericCalcNode<L>>, Box<GenericAnchorFunctionFallback<L>>>;
/// `anchor-size()` function used in math functions.
pub type GenericCalcAnchorSizeFunction<L> =
    GenericAnchorSizeFunction<Box<GenericAnchorFunctionFallback<L>>>;

/// A `random()` function.
/// https://drafts.csswg.org/css-values-5/#funcdef-random
#[repr(C)]
#[derive(
    Clone,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToResolvedValue,
    ToShmem,
)]
pub struct GenericRandomFunction<L> {
    /// The `<random-key>`, resolving to the random base value.
    pub key: GenericCalcNode<L>,
    /// The minimum value the function can resolve to, inclusive.
    pub min: GenericCalcNode<L>,
    /// The maximum value the function can resolve to, inclusive.
    pub max: GenericCalcNode<L>,
    /// The optional step value.
    pub step: Optional<GenericCalcNode<L>>,
}

/// A generic node in a calc expression.
///
/// FIXME: This would be much more elegant if we used `Self` in the types below,
/// but we can't because of https://github.com/serde-rs/serde/issues/1565.
///
/// FIXME: The following annotations are to workaround an LLVM inlining bug, see
/// bug 1631929.
///
/// cbindgen:destructor-attributes=MOZ_NEVER_INLINE
/// cbindgen:copy-constructor-attributes=MOZ_NEVER_INLINE
/// cbindgen:eq-attributes=MOZ_NEVER_INLINE
#[repr(u8)]
#[derive(
    Clone,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToResolvedValue,
    ToShmem,
)]
pub enum GenericCalcNode<L> {
    /// A leaf node.
    Leaf(L),
    /// A node that negates its child, e.g. Negate(1) == -1.
    Negate(Box<Self>),
    /// A node that inverts its child, e.g. Invert(10) == 1 / 10 == 0.1. The child must always
    /// resolve to a number unit.
    Invert(Box<Self>),
    /// A sum node, representing `a + b + c` where a, b, and c are the
    /// arguments.
    Sum(crate::OwnedSlice<Self>),
    /// A product node, representing `a * b * c` where a, b, and c are the
    /// arguments.
    Product(crate::OwnedSlice<Self>),
    /// A `min` or `max` function.
    MinMax(crate::OwnedSlice<Self>, MinMaxOp),
    /// A `clamp()` function.
    Clamp {
        /// The minimum value.
        min: Box<Self>,
        /// The central value.
        center: Box<Self>,
        /// The maximum value.
        max: Box<Self>,
    },
    /// A `round()` function.
    Round {
        /// The rounding strategy.
        strategy: RoundingStrategy,
        /// The value to round.
        value: Box<Self>,
        /// The step value.
        step: Box<Self>,
    },
    /// A `mod()` or `rem()` function.
    ModRem {
        /// The dividend calculation.
        dividend: Box<Self>,
        /// The divisor calculation.
        divisor: Box<Self>,
        /// Is the function mod or rem?
        op: ModRemOp,
    },
    /// A `sin()` function.
    Sin(Box<Self>),
    /// A `cos()` function.
    Cos(Box<Self>),
    /// A `tan()` function.
    Tan(Box<Self>),
    /// An `asin()` function.
    Asin(Box<Self>),
    /// An `acos()` function.
    Acos(Box<Self>),
    /// An `atan()` function.
    Atan(Box<Self>),
    /// An `atan2()` function.
    Atan2(Box<Self>, Box<Self>),
    /// A `pow()` function.
    Pow(Box<Self>, Box<Self>),
    /// A `sqrt()` function.
    Sqrt(Box<Self>),
    /// A `hypot()` function
    Hypot(crate::OwnedSlice<Self>),
    /// A `log()` function.
    Log(Box<Self>, Optional<Box<Self>>),
    /// An `exp()` function.
    Exp(Box<Self>),
    /// An `abs()` function.
    Abs(Box<Self>),
    /// A `sign()` function.
    Sign(Box<Self>),
    /// A `progress()` function.
    Progress {
        /// Clamping mode for the result.
        clamping_mode: ProgressClampingMode,
        /// The progress value calculation.
        value: Box<Self>,
        /// The progress start calculation.
        start: Box<Self>,
        /// The progress end calculation.
        end: Box<Self>,
    },
    /// A `random()` function.
    Random(Box<GenericRandomFunction<L>>),
    /// An `anchor()` function.
    Anchor(Box<GenericCalcAnchorFunction<L>>),
    /// An `anchor-size()` function.
    AnchorSize(Box<GenericCalcAnchorSizeFunction<L>>),
}

pub use self::GenericCalcNode as CalcNode;

/// The non-mixed types that a math function can return. Note that
/// <integer> is not represented in this list as a separate type from
/// <number>, as "math functions that resolve to <number> can be used
/// in any place that only accepts <integer>". CSS Typed OM also does
/// not distinguish between numbers and integers.
///
/// https://drafts.csswg.org/css-values-4/#math-function
/// https://drafts.csswg.org/css-values-4/#calc-type-checking
///
/// TODO(Bug 1866236) - Add the <flex> type.
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum CalcType {
    /// <length>
    Length,
    /// <percentage>
    Percentage,
    /// <angle>
    Angle,
    /// <time>
    Time,
    /// <resolution>
    Resolution,
    /// <number>
    Number,
}

/// The value of a percentage leaf node that contains an associated percent hint.
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToAnimatedZero,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
pub struct CalcPercentageLeaf {
    /// The percentage value.
    pub value: NoCalcPercentage,
    /// The base type the percentage resolves against, or None if there is
    /// no specific percent hint (this is used by CSS Typed OM when parsing
    /// an expression without the context of a property).
    #[css(skip)]
    pub hint: Optional<NumericBaseType>,
}

impl CalcPercentageLeaf {
    /// Builds a percentage leaf with the given percent hint.
    pub fn new(value: f32, hint: Optional<NumericBaseType>) -> Self {
        Self {
            value: NoCalcPercentage::new(value),
            hint,
        }
    }

    /// Returns the percentage value as a float.
    pub fn get(&self) -> f32 {
        self.value.get()
    }

    /// Returns the numeric type of this percentage.
    pub fn numeric_type(&self) -> NumericType {
        match self.hint {
            Optional::Some(hint) => NumericType::percent().with_percent_hint(hint),
            Optional::None => NumericType::percent(),
        }
    }

    /// Returns the percent hint to use when merging two percentages with an arithmetic
    /// operation. Mismatched hints should be impossible after type checking.
    pub fn combined_hint(&self, other: &Self) -> Optional<NumericBaseType> {
        debug_assert_eq!(
            self.hint, other.hint,
            "Merging percentages with mismatched hints"
        );
        self.hint
    }
}

macro_rules! compare_helpers {
    () => {
        /// Return whether a leaf is greater than another.
        #[allow(unused)]
        fn gt(&self, other: &Self) -> bool {
            self.compare(other) == Some(cmp::Ordering::Greater)
        }

        /// Return whether a leaf is less than another.
        fn lt(&self, other: &Self) -> bool {
            self.compare(other) == Some(cmp::Ordering::Less)
        }

        /// Return whether a leaf is smaller or equal than another.
        fn lte(&self, other: &Self) -> bool {
            match self.compare(other) {
                Some(cmp::Ordering::Less) => true,
                Some(cmp::Ordering::Equal) => true,
                Some(cmp::Ordering::Greater) => false,
                None => false,
            }
        }
    };
}

/// A trait that represents all the stuff a valid leaf of a calc expression.
pub trait CalcNodeLeaf: Clone + Sized + PartialEq + ToCss + ToTyped + fmt::Debug {
    /// Returns the type of the leaf.
    fn numeric_type(&self) -> NumericType;

    /// Returns the unitless value of this leaf if one is available.
    fn unitless_value(&self) -> Option<f32>;

    /// Returns the value and percent hint if this leaf is a percentage.
    fn as_percentage(&self) -> Option<(f32, Optional<NumericBaseType>)>;

    /// Returns the canonical value of this leaf in the type's canonical unit,
    /// if there is enough information to determine its numeric value.
    /// https://drafts.csswg.org/css-values-4/#simplify-a-calculation-tree
    fn canonical_value(&self) -> Option<f32>;

    /// Returns the angle value in radians if this leaf is an angle.
    fn as_angle_radians(&self) -> Option<f32>;

    /// Creates a new angle leaf from a value in radians.
    fn new_angle_from_radians(radians: f32) -> Self;

    /// Return true if the units of both leaves are equal. (NOTE: Does not take
    /// the values into account)
    fn is_same_unit_as(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }

    /// Do a partial comparison of these values.
    fn compare(&self, other: &Self) -> Option<cmp::Ordering>;
    compare_helpers!();

    /// Create a new leaf with a number value.
    fn new_number(value: f32) -> Self;

    /// Returns a float value if the leaf is a number.
    fn as_number(&self) -> Option<f32>;

    /// Returns a number or angle radians if the leaf is a number or angle.
    fn as_number_or_angle_radians(&self) -> Option<f32> {
        self.as_number().or_else(|| self.as_angle_radians())
    }

    /// Create a new leaf with `value` in the canonical unit of the given type.
    /// Returns Err(()) if the type cannot be constructed as a leaf.
    fn new_from_typed_value(value: f32, numeric_type: NumericType) -> Result<Self, ()>;

    /// Whether this value is known-negative.
    fn is_negative(&self) -> Result<bool, ()> {
        self.unitless_value()
            .map(|v| Ok(v.is_sign_negative()))
            .unwrap_or_else(|| Err(()))
    }

    /// Whether this value is infinite.
    fn is_infinite(&self) -> Result<bool, ()> {
        self.unitless_value()
            .map(|v| Ok(v.is_infinite()))
            .unwrap_or_else(|| Err(()))
    }

    /// Whether this value is zero.
    fn is_zero(&self) -> Result<bool, ()> {
        self.unitless_value()
            .map(|v| Ok(v.is_zero()))
            .unwrap_or_else(|| Err(()))
    }

    /// Whether this value is NaN.
    fn is_nan(&self) -> Result<bool, ()> {
        self.unitless_value()
            .map(|v| Ok(v.is_nan()))
            .unwrap_or_else(|| Err(()))
    }

    /// Tries to merge one leaf into another using the sum, that is, perform `x` + `y`.
    fn try_sum_in_place(&mut self, other: &Self) -> Result<(), ()>;

    /// Try to merge the right leaf into the left by using a multiplication. Return true if the
    /// merge was successful, otherwise false.
    fn try_product_in_place(&mut self, other: &mut Self) -> bool;

    /// Tries a generic arithmetic operation.
    fn try_op<O>(&self, other: &Self, op: O) -> Result<Self, ()>
    where
        O: Fn(f32, f32) -> f32;

    /// Map the value of this node with the given operation.
    fn map(&mut self, op: impl FnMut(f32) -> f32) -> Result<(), ()>;

    /// Canonicalizes the expression if necessary.
    fn simplify(&mut self) -> SimplificationResult;

    /// Returns the sort key for simplification.
    fn sort_key(&self) -> SortKey;

    /// Create a new leaf containing the sign() result of the given leaf.
    fn sign_from(leaf: &impl CalcNodeLeaf) -> Result<Self, ()> {
        let Some(value) = leaf.canonical_value() else {
            return Err(());
        };

        Ok(Self::new_number(crate::values::calc_sign(value)))
    }

    /// Whether this leaf node should serialize with a `calc()` wrapper
    /// if this node is the root of the calculation tree.
    fn should_serialize_with_root_calc_wrapper(&self) -> bool {
        true
    }
}

/// The level of any argument being serialized in `to_css_impl`.
#[derive(Clone)]
enum ArgumentLevel {
    /// The root of a calculation tree.
    CalculationRoot,
    /// The root of an operand node's argument, e.g. `min(10, 20)`, `10` and `20` will have this
    /// level, but min in this case will have `TopMost`.
    ArgumentRoot,
    /// Any other values serialized in the tree.
    Nested,
}

/// The result of simplify_and_sort_direct_children
#[derive(Clone, Copy)]
pub enum SimplificationResult {
    /// This node (Or some of its descendants, if any) was simplified.
    Simplified,
    /// The children was unchanged.
    Unchanged,
}

impl<L: CalcNodeLeaf> CalcNode<L> {
    /// Create a dummy CalcNode that can be used to do replacements of other nodes.
    fn dummy() -> Self {
        Self::MinMax(Default::default(), MinMaxOp::Max)
    }

    /// Change all the leaf nodes to have the given value. This is useful when
    /// you have `calc(1px * nan)` and you want to replace the product node with
    /// `calc(nan)`, in which case the unit will be retained.
    fn coerce_to_value(&mut self, value: f32) -> Result<(), ()> {
        self.map(|_| value)
    }

    /// Return true if a product is distributive over this node.
    /// Is distributive: (2 + 3) * 4 = 8 + 12
    /// Not distributive: sign(2 + 3) * 4 != sign(8 + 12)
    #[inline]
    pub fn is_product_distributive(&self) -> bool {
        match self {
            // If there's no value, we can't distribute the product.
            Self::Leaf(l) => l.unitless_value().is_some(),
            Self::Sum(children) => children.iter().all(|c| c.is_product_distributive()),
            _ => false,
        }
    }

    /// If the node has a valid type outcome, then return it, otherwise fail. Note that this
    /// type may not represent a valid CSS production and it is the responsibility of the caller
    /// to determine whether this type is acceptable (see NumericType::as_calc_type).
    pub fn numeric_type(&self) -> Result<NumericType, ()> {
        Ok(match self {
            CalcNode::Leaf(l) => l.numeric_type(),
            CalcNode::Negate(child) | CalcNode::Abs(child) => child.numeric_type()?,
            CalcNode::Sum(children) => {
                let mut ty = children.first().unwrap().numeric_type()?;
                for child in children.iter().skip(1) {
                    let child_ty = child.numeric_type()?;
                    ty = NumericType::add_two_types(&ty, &child_ty)?;
                }
                ty
            },
            CalcNode::Product(children) => {
                let mut ty = children.first().unwrap().numeric_type()?;

                for child in children.iter().skip(1) {
                    let child_ty = child.numeric_type()?;
                    ty = NumericType::multiply_two_types(&ty, &child_ty)?;
                }

                ty
            },
            CalcNode::MinMax(children, _) | CalcNode::Hypot(children) => {
                let mut ty = children.first().unwrap().numeric_type()?;
                for child in children.iter().skip(1) {
                    let child_ty = child.numeric_type()?;
                    ty = NumericType::add_two_types(&ty, &child_ty)?;
                }
                ty
            },
            CalcNode::Clamp { min, center, max } => {
                let min_ty = min.numeric_type()?;
                let center_ty = center.numeric_type()?;
                let max_ty = max.numeric_type()?;

                let mut ty = NumericType::add_two_types(&min_ty, &center_ty)?;
                ty = NumericType::add_two_types(&ty, &max_ty)?;
                ty
            },
            CalcNode::Round { value, step, .. } => {
                let value_ty = value.numeric_type()?;
                let step_ty = step.numeric_type()?;
                NumericType::add_two_types(&value_ty, &step_ty)?
            },
            CalcNode::ModRem {
                dividend, divisor, ..
            } => {
                let dividend_ty = dividend.numeric_type()?;
                let divisor_ty = divisor.numeric_type()?;
                NumericType::add_two_types(&dividend_ty, &divisor_ty)?
            },
            CalcNode::Sign(child) => {
                // sign() always resolves to a number, but we still need to make sure that the
                // child units make sense.
                let _ = child.numeric_type()?;
                NumericType::number()
            },
            CalcNode::Anchor(..) | CalcNode::AnchorSize(..) => {
                NumericType::length().with_percent_hint(NumericBaseType::Length)
            },
            CalcNode::Sin(child) | CalcNode::Cos(child) | CalcNode::Tan(child) => {
                let child_ty = child.numeric_type_as_calc_type()?;
                if child_ty != CalcType::Number && child_ty != CalcType::Angle {
                    return Err(());
                }
                NumericType::number()
            },
            CalcNode::Asin(child) | CalcNode::Acos(child) | CalcNode::Atan(child) => {
                if child.numeric_type_as_calc_type()? != CalcType::Number {
                    return Err(());
                }
                NumericType::angle()
            },
            CalcNode::Atan2(a, b) => {
                // Ensure that the types of a and b can be made consistent
                let a_ty = a.numeric_type()?;
                let b_ty = b.numeric_type()?;
                let _ = NumericType::add_two_types(&a_ty, &b_ty)?;
                NumericType::angle()
            },
            CalcNode::Pow(a, b) => {
                let a_ty = a.numeric_type_as_calc_type()?;
                let b_ty = b.numeric_type_as_calc_type()?;
                if a_ty != CalcType::Number || b_ty != CalcType::Number {
                    return Err(());
                }
                NumericType::number()
            },
            CalcNode::Invert(c) => {
                let mut ty = c.numeric_type()?;
                ty.invert();
                ty
            },
            CalcNode::Sqrt(c) | CalcNode::Exp(c) => {
                if c.numeric_type_as_calc_type()? != CalcType::Number {
                    return Err(());
                }
                NumericType::number()
            },
            CalcNode::Log(a, b) => {
                let a_ty = a.numeric_type_as_calc_type()?;
                let b_ty = match b {
                    Optional::Some(b) => b.numeric_type_as_calc_type()?,
                    Optional::None => CalcType::Number,
                };
                if a_ty != CalcType::Number || b_ty != CalcType::Number {
                    return Err(());
                }
                NumericType::number()
            },
            CalcNode::Progress {
                value, start, end, ..
            } => {
                let value_ty = value.numeric_type()?;
                let start_ty = start.numeric_type()?;
                let end_ty = end.numeric_type()?;

                // Ensure that the types of the arguments are consistent.
                let _ = NumericType::add_two_types(&value_ty, &start_ty)?;
                let _ = NumericType::add_two_types(&value_ty, &end_ty)?;
                NumericType::number()
            },
            CalcNode::Random(random) => {
                let min_ty = random.min.numeric_type()?;
                let max_ty = random.max.numeric_type()?;
                let mut ty = NumericType::add_two_types(&min_ty, &max_ty)?;
                if let Some(step) = random.step.as_ref() {
                    let step_ty = step.numeric_type()?;
                    ty = NumericType::add_two_types(&ty, &step_ty)?;
                }
                ty
            },
        })
    }

    /// If the node has a valid type outcome that matches one of the types that a calculation
    /// can produce, then return it, otherwise fail.
    pub fn numeric_type_as_calc_type(&self) -> Result<CalcType, ()> {
        self.numeric_type()?.as_calc_type()
    }

    /// Negate the node inline.  If the node is distributive, it is replaced by the result,
    /// otherwise the node is wrapped in a [`Negate`] node.
    pub fn negate(&mut self) {
        /// Node(params) -> Negate(Node(params))
        fn wrap_self_in_negate<L: CalcNodeLeaf>(s: &mut CalcNode<L>) {
            let result = mem::replace(s, CalcNode::dummy());
            *s = CalcNode::Negate(Box::new(result));
        }

        match *self {
            CalcNode::Leaf(ref mut leaf) => {
                if leaf.map(std::ops::Neg::neg).is_err() {
                    wrap_self_in_negate(self)
                }
            },
            CalcNode::Negate(ref mut value) => {
                // Don't negate the value here.  Replace `self` with it's child.
                let result = mem::replace(value.as_mut(), Self::dummy());
                *self = result;
            },
            CalcNode::Invert(_) => {
                // -(1 / -10) == -(-0.1) == 0.1
                wrap_self_in_negate(self)
            },
            CalcNode::Sum(ref mut children) => {
                for child in children.iter_mut() {
                    child.negate();
                }
            },
            CalcNode::Product(_) => {
                // -(2 * 3 / 4) == -(1.5)
                wrap_self_in_negate(self);
            },
            CalcNode::MinMax(ref mut children, ref mut op) => {
                for child in children.iter_mut() {
                    child.negate();
                }

                // Negating min-max means the operation is swapped.
                *op = match *op {
                    MinMaxOp::Min => MinMaxOp::Max,
                    MinMaxOp::Max => MinMaxOp::Min,
                };
            },
            CalcNode::Clamp {
                ref mut min,
                ref mut center,
                ref mut max,
            } => {
                if min.lte(max) {
                    min.negate();
                    center.negate();
                    max.negate();

                    mem::swap(min, max);
                } else {
                    wrap_self_in_negate(self);
                }
            },
            CalcNode::Round {
                ref mut strategy,
                ref mut value,
                ref mut step,
            } => {
                match *strategy {
                    RoundingStrategy::Nearest => {
                        // Nearest is tricky because we'd have to swap the
                        // behavior at the half-way point from using the upper
                        // to lower bound.
                        // Simpler to just wrap self in a negate node.
                        wrap_self_in_negate(self);
                        return;
                    },
                    RoundingStrategy::Up => *strategy = RoundingStrategy::Down,
                    RoundingStrategy::Down => *strategy = RoundingStrategy::Up,
                    RoundingStrategy::ToZero => (),
                }
                value.negate();
                step.negate();
            },
            CalcNode::ModRem {
                ref mut dividend,
                ref mut divisor,
                ..
            } => {
                dividend.negate();
                divisor.negate();
            },
            CalcNode::Sign(ref mut child) => {
                child.negate();
            },
            CalcNode::Sin(..)
            | CalcNode::Cos(..)
            | CalcNode::Tan(..)
            | CalcNode::Asin(..)
            | CalcNode::Acos(..)
            | CalcNode::Atan(..)
            | CalcNode::Atan2(..)
            | CalcNode::Pow(..)
            | CalcNode::Sqrt(..)
            | CalcNode::Log(..)
            | CalcNode::Exp(..)
            | CalcNode::Abs(..)
            | CalcNode::Hypot(..)
            | CalcNode::Progress { .. }
            | CalcNode::Random(..)
            | CalcNode::Anchor(..)
            | CalcNode::AnchorSize(..) => {
                wrap_self_in_negate(self);
            },
        }
    }

    fn sort_key(&self) -> SortKey {
        match *self {
            Self::Leaf(ref l) => l.sort_key(),
            Self::Anchor(..) | Self::AnchorSize(..) => SortKey::Px,
            _ => SortKey::Other,
        }
    }

    /// Returns the leaf if we can (if simplification has allowed it).
    pub fn as_leaf(&self) -> Option<&L> {
        match *self {
            Self::Leaf(ref l) => Some(l),
            _ => None,
        }
    }

    /// Tries to merge one node into another using the sum, that is, perform `x` + `y`.
    pub fn try_sum_in_place(&mut self, other: &Self) -> Result<(), ()> {
        match (self, other) {
            (&mut CalcNode::Leaf(ref mut one), CalcNode::Leaf(other)) => {
                one.try_sum_in_place(other)
            },
            _ => Err(()),
        }
    }

    /// Tries to merge one node into another using the product, that is, perform `x` * `y`.
    pub fn try_product_in_place(&mut self, other: &mut Self) -> bool {
        if let Ok(resolved) = other.resolve()
            && let Some(number) = resolved.as_number()
        {
            if number == 1.0 {
                return true;
            }

            if self.is_product_distributive() {
                if self.map(|v| v * number).is_err() {
                    return false;
                }
                return true;
            }
        }

        if let Ok(resolved) = self.resolve()
            && let Some(number) = resolved.as_number()
        {
            if number == 1.0 {
                std::mem::swap(self, other);
                return true;
            }

            if other.is_product_distributive() {
                if other.map(|v| v * number).is_err() {
                    return false;
                }
                std::mem::swap(self, other);
                return true;
            }
        }

        false
    }

    /// Tries to apply a generic arithmetic operator
    fn try_op<O>(&self, other: &Self, op: O) -> Result<Self, ()>
    where
        O: Fn(f32, f32) -> f32,
    {
        match (self, other) {
            (CalcNode::Leaf(one), CalcNode::Leaf(other)) => {
                Ok(CalcNode::Leaf(one.try_op(other, op)?))
            },
            _ => Err(()),
        }
    }

    /// Map the value of this node with the given operation.
    pub fn map(&mut self, mut op: impl FnMut(f32) -> f32) -> Result<(), ()> {
        fn map_internal<L: CalcNodeLeaf>(
            node: &mut CalcNode<L>,
            op: &mut impl FnMut(f32) -> f32,
        ) -> Result<(), ()> {
            match node {
                CalcNode::Leaf(l) => l.map(op),
                CalcNode::Negate(v) | CalcNode::Invert(v) => map_internal(v, op),
                CalcNode::Sum(children) | CalcNode::Product(children) => {
                    for node in &mut **children {
                        map_internal(node, op)?;
                    }
                    Ok(())
                },
                CalcNode::MinMax(children, _) => {
                    for node in &mut **children {
                        map_internal(node, op)?;
                    }
                    Ok(())
                },
                CalcNode::Clamp { min, center, max } => {
                    map_internal(min, op)?;
                    map_internal(center, op)?;
                    map_internal(max, op)
                },
                CalcNode::Round { value, step, .. } => {
                    map_internal(value, op)?;
                    map_internal(step, op)
                },
                CalcNode::ModRem {
                    dividend, divisor, ..
                } => {
                    map_internal(dividend, op)?;
                    map_internal(divisor, op)
                },
                CalcNode::Hypot(children) => {
                    for node in &mut **children {
                        map_internal(node, op)?;
                    }
                    Ok(())
                },
                CalcNode::Abs(child) | CalcNode::Sign(child) => map_internal(child, op),
                // It is invalid to treat inner `CalcNode`s here - `anchor(--foo 50%) / 2` != `anchor(--foo 25%)`.
                // Same applies to fallback, as we don't know if it will be used. Similar reasoning applies to `anchor-size()`.
                CalcNode::Anchor(_) | CalcNode::AnchorSize(_) => Err(()),
                // Trig functions are nonlinear: 2 * sin(x) != sin(2*x).
                // Similarly for pow/sqrt/log/exp.
                CalcNode::Sin(_)
                | CalcNode::Cos(_)
                | CalcNode::Tan(_)
                | CalcNode::Asin(_)
                | CalcNode::Acos(_)
                | CalcNode::Atan(_)
                | CalcNode::Atan2(..)
                | CalcNode::Pow(..)
                | CalcNode::Sqrt(_)
                | CalcNode::Log(..)
                | CalcNode::Exp(_)
                | CalcNode::Progress { .. }
                | CalcNode::Random(_) => Err(()),
            }
        }

        map_internal(self, &mut op)
    }

    /// Convert this `CalcNode` into a `CalcNode` with a different leaf kind.
    pub fn map_leaves<O, F>(&self, mut map: F) -> CalcNode<O>
    where
        O: CalcNodeLeaf,
        F: FnMut(&L) -> O,
    {
        self.map_leaves_internal(&mut map)
    }

    fn map_leaves_internal<O, F>(&self, map: &mut F) -> CalcNode<O>
    where
        O: CalcNodeLeaf,
        F: FnMut(&L) -> O,
    {
        fn map_children<L, O, F>(
            children: &[CalcNode<L>],
            map: &mut F,
        ) -> crate::OwnedSlice<CalcNode<O>>
        where
            L: CalcNodeLeaf,
            O: CalcNodeLeaf,
            F: FnMut(&L) -> O,
        {
            children
                .iter()
                .map(|c| c.map_leaves_internal(map))
                .collect()
        }

        match *self {
            Self::Leaf(ref l) => CalcNode::Leaf(map(l)),
            Self::Negate(ref c) => CalcNode::Negate(Box::new(c.map_leaves_internal(map))),
            Self::Invert(ref c) => CalcNode::Invert(Box::new(c.map_leaves_internal(map))),
            Self::Sum(ref c) => CalcNode::Sum(map_children(c, map)),
            Self::Product(ref c) => CalcNode::Product(map_children(c, map)),
            Self::MinMax(ref c, op) => CalcNode::MinMax(map_children(c, map), op),
            Self::Clamp {
                ref min,
                ref center,
                ref max,
            } => {
                let min = Box::new(min.map_leaves_internal(map));
                let center = Box::new(center.map_leaves_internal(map));
                let max = Box::new(max.map_leaves_internal(map));
                CalcNode::Clamp { min, center, max }
            },
            Self::Round {
                strategy,
                ref value,
                ref step,
            } => {
                let value = Box::new(value.map_leaves_internal(map));
                let step = Box::new(step.map_leaves_internal(map));
                CalcNode::Round {
                    strategy,
                    value,
                    step,
                }
            },
            Self::ModRem {
                ref dividend,
                ref divisor,
                op,
            } => {
                let dividend = Box::new(dividend.map_leaves_internal(map));
                let divisor = Box::new(divisor.map_leaves_internal(map));
                CalcNode::ModRem {
                    dividend,
                    divisor,
                    op,
                }
            },
            Self::Sin(ref c) => CalcNode::Sin(Box::new(c.map_leaves_internal(map))),
            Self::Cos(ref c) => CalcNode::Cos(Box::new(c.map_leaves_internal(map))),
            Self::Tan(ref c) => CalcNode::Tan(Box::new(c.map_leaves_internal(map))),
            Self::Asin(ref c) => CalcNode::Asin(Box::new(c.map_leaves_internal(map))),
            Self::Acos(ref c) => CalcNode::Acos(Box::new(c.map_leaves_internal(map))),
            Self::Atan(ref c) => CalcNode::Atan(Box::new(c.map_leaves_internal(map))),
            Self::Atan2(ref a, ref b) => CalcNode::Atan2(
                Box::new(a.map_leaves_internal(map)),
                Box::new(b.map_leaves_internal(map)),
            ),
            Self::Pow(ref a, ref b) => CalcNode::Pow(
                Box::new(a.map_leaves_internal(map)),
                Box::new(b.map_leaves_internal(map)),
            ),
            Self::Sqrt(ref c) => CalcNode::Sqrt(Box::new(c.map_leaves_internal(map))),
            Self::Hypot(ref c) => CalcNode::Hypot(map_children(c, map)),
            Self::Log(ref a, ref b) => CalcNode::Log(
                Box::new(a.map_leaves_internal(map)),
                b.as_ref()
                    .map(|b| Box::new(b.map_leaves_internal(map)))
                    .into(),
            ),
            Self::Exp(ref c) => CalcNode::Exp(Box::new(c.map_leaves_internal(map))),
            Self::Abs(ref c) => CalcNode::Abs(Box::new(c.map_leaves_internal(map))),
            Self::Sign(ref c) => CalcNode::Sign(Box::new(c.map_leaves_internal(map))),
            Self::Progress {
                clamping_mode,
                ref value,
                ref start,
                ref end,
            } => {
                let value = Box::new(value.map_leaves_internal(map));
                let start = Box::new(start.map_leaves_internal(map));
                let end = Box::new(end.map_leaves_internal(map));
                CalcNode::Progress {
                    clamping_mode,
                    value,
                    start,
                    end,
                }
            },
            Self::Random(ref r) => CalcNode::Random(Box::new(GenericRandomFunction {
                key: r.key.map_leaves_internal(map),
                min: r.min.map_leaves_internal(map),
                max: r.max.map_leaves_internal(map),
                step: r
                    .step
                    .as_ref()
                    .map(|step| step.map_leaves_internal(map))
                    .into(),
            })),
            Self::Anchor(ref f) => CalcNode::Anchor(Box::new(GenericAnchorFunction {
                target_element: f.target_element.clone(),
                side: match &f.side {
                    GenericAnchorSide::Keyword(k) => GenericAnchorSide::Keyword(*k),
                    GenericAnchorSide::Percentage(p) => {
                        GenericAnchorSide::Percentage(Box::new(p.map_leaves_internal(map)))
                    },
                },
                fallback: f
                    .fallback
                    .as_ref()
                    .map(|fb| {
                        Box::new(GenericAnchorFunctionFallback::new(
                            fb.is_calc_node,
                            fb.node.map_leaves_internal(map),
                        ))
                    })
                    .into(),
            })),
            Self::AnchorSize(ref f) => CalcNode::AnchorSize(Box::new(GenericAnchorSizeFunction {
                target_element: f.target_element.clone(),
                size: f.size,
                fallback: f
                    .fallback
                    .as_ref()
                    .map(|fb| {
                        Box::new(GenericAnchorFunctionFallback::new(
                            fb.is_calc_node,
                            fb.node.map_leaves_internal(map),
                        ))
                    })
                    .into(),
            })),
        }
    }

    /// Resolve this node into a value.
    pub fn resolve(&self) -> Result<L, ()> {
        self.resolve_map(|l| Ok(l.clone()))
    }

    /// Resolve this node into a value, given a function that maps the leaf values.
    pub fn resolve_map<F>(&self, mut leaf_to_output_fn: F) -> Result<L, ()>
    where
        F: FnMut(&L) -> Result<L, ()>,
    {
        let (value, ty) = self.resolve_internal(&mut leaf_to_output_fn)?;
        L::new_from_typed_value(value, ty)
    }

    fn resolve_internal<F>(&self, leaf_to_output_fn: &mut F) -> Result<(f32, NumericType), ()>
    where
        F: FnMut(&L) -> Result<L, ()>,
    {
        match self {
            Self::Leaf(l) => {
                let result = leaf_to_output_fn(l)?;
                let value = result.canonical_value().ok_or(())?;
                let ty = result.numeric_type();
                Ok((value, ty))
            },
            Self::Negate(child) => {
                let (value, ty) = child.resolve_internal(leaf_to_output_fn)?;
                Ok((-value, ty))
            },
            Self::Invert(child) => {
                let (value, mut ty) = child.resolve_internal(leaf_to_output_fn)?;
                ty.invert();
                Ok((1.0 / value, ty))
            },
            Self::Sum(children) => {
                let (mut value, mut ty) = children[0].resolve_internal(leaf_to_output_fn)?;

                for child in children.iter().skip(1) {
                    let (right, right_ty) = child.resolve_internal(leaf_to_output_fn)?;
                    value += right;
                    ty = NumericType::add_two_types(&ty, &right_ty)?;
                }

                Ok((value, ty))
            },
            Self::Product(children) => {
                let (mut value, mut ty) = children[0].resolve_internal(leaf_to_output_fn)?;

                for child in children.iter().skip(1) {
                    let (leaf, leaf_ty) = child.resolve_internal(leaf_to_output_fn)?;
                    value *= leaf;
                    ty = NumericType::multiply_two_types(&ty, &leaf_ty)?;
                }

                Ok((value, ty))
            },
            Self::MinMax(children, op) => {
                let (mut value, mut ty) = children[0].resolve_internal(leaf_to_output_fn)?;

                if value.is_nan() {
                    return Ok((value, ty));
                }

                for child in children.iter().skip(1) {
                    let (candidate, candidate_ty) = child.resolve_internal(leaf_to_output_fn)?;

                    // Determine the consistent type (bailing out if the types are not consistent).
                    ty = NumericType::add_two_types(&ty, &candidate_ty)?;

                    if candidate.is_nan() {
                        value = candidate;
                        break;
                    }

                    value = match op {
                        MinMaxOp::Min => crate::values::calc_min(value, candidate),
                        MinMaxOp::Max => crate::values::calc_max(value, candidate),
                    };
                }

                Ok((value, ty))
            },
            Self::Clamp { min, center, max } => {
                let (min, min_ty) = min.resolve_internal(leaf_to_output_fn)?;
                let (center, center_ty) = center.resolve_internal(leaf_to_output_fn)?;
                let (max, max_ty) = max.resolve_internal(leaf_to_output_fn)?;

                let mut ty = NumericType::add_two_types(&min_ty, &center_ty)?;
                ty = NumericType::add_two_types(&ty, &max_ty)?;

                if min.is_nan() {
                    return Ok((min, ty));
                }

                if center.is_nan() {
                    return Ok((center, ty));
                }

                if max.is_nan() {
                    return Ok((max, ty));
                }

                // NOTE: clamp is max(min, min(center, max))
                let value = crate::values::calc_max(min, crate::values::calc_min(center, max));
                Ok((value, ty))
            },
            Self::Round {
                strategy,
                value,
                step,
            } => {
                let (mut value, value_ty) = value.resolve_internal(leaf_to_output_fn)?;
                let (step, step_ty) = step.resolve_internal(leaf_to_output_fn)?;
                let ty = NumericType::add_two_types(&value_ty, &step_ty)?;

                let step = step.abs();

                // TODO(emilio): Seems like at least a few of these
                // special-cases could be removed if we do the math in a
                // particular order.
                if step.is_zero() {
                    value = f32::NAN;
                } else if value.is_infinite() {
                    if step.is_infinite() {
                        value = f32::NAN
                    }
                } else if step.is_infinite() {
                    value = match strategy {
                        RoundingStrategy::Nearest | RoundingStrategy::ToZero => {
                            if value.is_sign_negative() { -0.0 } else { 0.0 }
                        },
                        RoundingStrategy::Up => {
                            if !value.is_sign_negative() && !value.is_zero() {
                                f32::INFINITY
                            } else if !value.is_sign_negative() && value.is_zero() {
                                value
                            } else {
                                -0.0
                            }
                        },
                        RoundingStrategy::Down => {
                            if value.is_sign_negative() && !value.is_zero() {
                                -f32::INFINITY
                            } else if value.is_sign_negative() && value.is_zero() {
                                value
                            } else {
                                0.0
                            }
                        },
                    };
                } else {
                    let div = value / step;
                    let lower_bound = div.floor() * step;
                    let upper_bound = div.ceil() * step;

                    value = match strategy {
                        RoundingStrategy::Nearest => {
                            // In case of a tie, use the upper bound
                            if value - lower_bound < upper_bound - value {
                                lower_bound
                            } else {
                                upper_bound
                            }
                        },
                        RoundingStrategy::Up => upper_bound,
                        RoundingStrategy::Down => lower_bound,
                        RoundingStrategy::ToZero => {
                            // In case of a tie, use the upper bound
                            if lower_bound.abs() < upper_bound.abs() {
                                lower_bound
                            } else {
                                upper_bound
                            }
                        },
                    }
                }

                Ok((value, ty))
            },
            Self::ModRem {
                dividend,
                divisor,
                op,
            } => {
                let (dividend, dividend_ty) = dividend.resolve_internal(leaf_to_output_fn)?;
                let (divisor, divisor_ty) = divisor.resolve_internal(leaf_to_output_fn)?;
                let ty = NumericType::add_two_types(&dividend_ty, &divisor_ty)?;
                let value = op.apply(dividend, divisor);
                Ok((value, ty))
            },
            Self::Sin(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                let radians = match ty.as_calc_type()? {
                    CalcType::Number => value,
                    CalcType::Angle => value.to_radians(),
                    _ => return Err(()),
                };
                Ok((radians.sin(), NumericType::number()))
            },
            Self::Cos(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                let radians = match ty.as_calc_type()? {
                    CalcType::Number => value,
                    CalcType::Angle => value.to_radians(),
                    _ => return Err(()),
                };
                Ok((radians.cos(), NumericType::number()))
            },
            Self::Tan(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                let radians = match ty.as_calc_type()? {
                    CalcType::Number => value,
                    CalcType::Angle => value.to_radians(),
                    _ => return Err(()),
                };
                Ok((radians.tan(), NumericType::number()))
            },
            Self::Asin(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                if !ty.is_number() {
                    return Err(());
                }
                Ok((value.asin().to_degrees(), NumericType::angle()))
            },
            Self::Acos(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                if !ty.is_number() {
                    return Err(());
                }
                Ok((value.acos().to_degrees(), NumericType::angle()))
            },
            Self::Atan(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                if !ty.is_number() {
                    return Err(());
                }
                Ok((value.atan().to_degrees(), NumericType::angle()))
            },
            Self::Atan2(a, b) => {
                let (a, a_ty) = a.resolve_internal(leaf_to_output_fn)?;
                let (b, b_ty) = b.resolve_internal(leaf_to_output_fn)?;
                let _ = NumericType::add_two_types(&a_ty, &b_ty)?;
                Ok((a.atan2(b).to_degrees(), NumericType::angle()))
            },
            Self::Pow(a, b) => {
                let (a, a_ty) = a.resolve_internal(leaf_to_output_fn)?;
                let (b, b_ty) = b.resolve_internal(leaf_to_output_fn)?;
                if !a_ty.is_number() || !b_ty.is_number() {
                    return Err(());
                }
                Ok((a.powf(b), NumericType::number()))
            },
            Self::Sqrt(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                if !ty.is_number() {
                    return Err(());
                }
                Ok((value.sqrt(), NumericType::number()))
            },
            Self::Hypot(children) => {
                let (mut value, mut ty) = children[0].resolve_internal(leaf_to_output_fn)?;
                value = value.powi(2);

                for child in children.iter().skip(1) {
                    let (child_value, child_ty) = child.resolve_internal(leaf_to_output_fn)?;
                    ty = NumericType::add_two_types(&ty, &child_ty)?;
                    value += child_value.powi(2);
                }

                Ok((value.sqrt(), ty))
            },
            Self::Log(a, b) => {
                let (a, a_ty) = a.resolve_internal(leaf_to_output_fn)?;
                if !a_ty.is_number() {
                    return Err(());
                }
                let value = match b {
                    Optional::Some(b) => {
                        let (b, b_ty) = b.resolve_internal(leaf_to_output_fn)?;
                        if !b_ty.is_number() {
                            return Err(());
                        }
                        a.log(b)
                    },
                    Optional::None => a.ln(),
                };
                Ok((value, NumericType::number()))
            },
            Self::Exp(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                if !ty.is_number() {
                    return Err(());
                }
                Ok((value.exp(), NumericType::number()))
            },
            Self::Abs(c) => {
                let (value, ty) = c.resolve_internal(leaf_to_output_fn)?;
                Ok((value.abs(), ty))
            },
            Self::Sign(c) => {
                let (value, _) = c.resolve_internal(leaf_to_output_fn)?;
                let sign = crate::values::calc_sign(value);
                Ok((sign, NumericType::number()))
            },
            Self::Progress {
                clamping_mode,
                value,
                start,
                end,
            } => {
                let (value, value_ty) = value.resolve_internal(leaf_to_output_fn)?;
                let (start, start_ty) = start.resolve_internal(leaf_to_output_fn)?;
                let (end, end_ty) = end.resolve_internal(leaf_to_output_fn)?;

                let _ = NumericType::add_two_types(&value_ty, &start_ty)?;
                let _ = NumericType::add_two_types(&value_ty, &end_ty)?;
                let _ = NumericType::add_two_types(&start_ty, &end_ty)?;

                let progress = clamping_mode.evaluate(value, start, end);
                Ok((progress, NumericType::number()))
            },
            Self::Random(r) => {
                let (base, base_ty) = r.key.resolve_internal(leaf_to_output_fn)?;
                if !base_ty.is_number() {
                    return Err(());
                }

                let (min, min_ty) = r.min.resolve_internal(leaf_to_output_fn)?;
                let (max, max_ty) = r.max.resolve_internal(leaf_to_output_fn)?;
                let mut ty = NumericType::add_two_types(&min_ty, &max_ty)?;

                let step = match r.step.as_ref() {
                    Some(step) => {
                        let (step, step_ty) = step.resolve_internal(leaf_to_output_fn)?;
                        ty = NumericType::add_two_types(&ty, &step_ty)?;
                        Some(step)
                    },
                    None => None,
                };

                let value = calc_random(base, min, max, step);
                Ok((value, ty))
            },
            Self::Anchor(_) | Self::AnchorSize(_) => Err(()),
        }
    }

    /// Mutate nodes within this calc node tree using given the mapping function.
    pub fn map_node<F>(&mut self, mut mapping_fn: F) -> Result<(), ()>
    where
        F: FnMut(&CalcNode<L>) -> Result<Option<CalcNode<L>>, ()>,
    {
        self.map_node_internal(&mut mapping_fn)
    }

    fn map_node_internal<F>(&mut self, mapping_fn: &mut F) -> Result<(), ()>
    where
        F: FnMut(&CalcNode<L>) -> Result<Option<CalcNode<L>>, ()>,
    {
        if let Some(node) = mapping_fn(self)? {
            *self = node;
            // Assume that any sub-nodes don't need to be mutated.
            return Ok(());
        }
        match self {
            Self::Leaf(_) | Self::Anchor(_) | Self::AnchorSize(_) => (),
            Self::Negate(child)
            | Self::Invert(child)
            | Self::Abs(child)
            | Self::Sign(child)
            | Self::Sin(child)
            | Self::Cos(child)
            | Self::Tan(child)
            | Self::Asin(child)
            | Self::Acos(child)
            | Self::Atan(child)
            | Self::Sqrt(child)
            | Self::Exp(child) => {
                child.map_node_internal(mapping_fn)?;
            },
            Self::Atan2(a, b) => {
                a.map_node_internal(mapping_fn)?;
                b.map_node_internal(mapping_fn)?;
            },
            Self::Pow(a, b) => {
                a.map_node_internal(mapping_fn)?;
                b.map_node_internal(mapping_fn)?;
            },
            Self::Log(a, b) => {
                a.map_node_internal(mapping_fn)?;
                if let Optional::Some(b) = b {
                    b.map_node_internal(mapping_fn)?;
                }
            },
            Self::Sum(children)
            | Self::Product(children)
            | Self::Hypot(children)
            | Self::MinMax(children, _) => {
                for child in children.iter_mut() {
                    child.map_node_internal(mapping_fn)?;
                }
            },
            Self::Clamp { min, center, max } => {
                min.map_node_internal(mapping_fn)?;
                center.map_node_internal(mapping_fn)?;
                max.map_node_internal(mapping_fn)?;
            },
            Self::Round { value, step, .. } => {
                value.map_node_internal(mapping_fn)?;
                step.map_node_internal(mapping_fn)?;
            },
            Self::ModRem {
                dividend, divisor, ..
            } => {
                dividend.map_node_internal(mapping_fn)?;
                divisor.map_node_internal(mapping_fn)?;
            },
            Self::Progress {
                value, start, end, ..
            } => {
                value.map_node_internal(mapping_fn)?;
                start.map_node_internal(mapping_fn)?;
                end.map_node_internal(mapping_fn)?;
            },
            Self::Random(random) => {
                random.key.map_node_internal(mapping_fn)?;
                random.min.map_node_internal(mapping_fn)?;
                random.max.map_node_internal(mapping_fn)?;
                if let Some(step) = random.step.as_mut() {
                    step.map_node_internal(mapping_fn)?;
                }
            },
        };
        Ok(())
    }

    fn is_negative_leaf(&self) -> Result<bool, ()> {
        Ok(match *self {
            Self::Leaf(ref l) => l.is_negative()?,
            _ => false,
        })
    }

    fn is_zero_leaf(&self) -> Result<bool, ()> {
        Ok(match *self {
            Self::Leaf(ref l) => l.is_zero()?,
            _ => false,
        })
    }

    fn is_infinite_leaf(&self) -> Result<bool, ()> {
        Ok(match *self {
            Self::Leaf(ref l) => l.is_infinite()?,
            _ => false,
        })
    }

    fn is_nan_leaf(&self) -> Result<bool, ()> {
        Ok(match *self {
            Self::Leaf(ref l) => l.is_nan()?,
            _ => false,
        })
    }

    /// Visits all the nodes in this calculation tree recursively, starting by
    /// the leaves and bubbling all the way up.
    ///
    /// This is useful for simplification, but can also be used for validation
    /// and such.
    pub fn visit_depth_first(&mut self, mut f: impl FnMut(&mut Self)) {
        self.visit_depth_first_internal(&mut f)
    }

    fn visit_depth_first_internal(&mut self, f: &mut impl FnMut(&mut Self)) {
        match *self {
            Self::Clamp {
                ref mut min,
                ref mut center,
                ref mut max,
            } => {
                min.visit_depth_first_internal(f);
                center.visit_depth_first_internal(f);
                max.visit_depth_first_internal(f);
            },
            Self::Round {
                ref mut value,
                ref mut step,
                ..
            } => {
                value.visit_depth_first_internal(f);
                step.visit_depth_first_internal(f);
            },
            Self::ModRem {
                ref mut dividend,
                ref mut divisor,
                ..
            } => {
                dividend.visit_depth_first_internal(f);
                divisor.visit_depth_first_internal(f);
            },
            Self::Sum(ref mut children)
            | Self::Product(ref mut children)
            | Self::MinMax(ref mut children, _)
            | Self::Hypot(ref mut children) => {
                for child in &mut **children {
                    child.visit_depth_first_internal(f);
                }
            },
            Self::Negate(ref mut value) | Self::Invert(ref mut value) => {
                value.visit_depth_first_internal(f);
            },
            Self::Sin(ref mut value)
            | Self::Cos(ref mut value)
            | Self::Tan(ref mut value)
            | Self::Asin(ref mut value)
            | Self::Acos(ref mut value)
            | Self::Atan(ref mut value)
            | Self::Sqrt(ref mut value)
            | Self::Exp(ref mut value) => {
                value.visit_depth_first_internal(f);
            },
            Self::Atan2(ref mut a, ref mut b) => {
                a.visit_depth_first_internal(f);
                b.visit_depth_first_internal(f);
            },
            Self::Pow(ref mut a, ref mut b) => {
                a.visit_depth_first_internal(f);
                b.visit_depth_first_internal(f);
            },
            Self::Log(ref mut a, ref mut b) => {
                a.visit_depth_first_internal(f);
                if let Optional::Some(b) = b {
                    b.visit_depth_first_internal(f);
                }
            },
            Self::Abs(ref mut value) | Self::Sign(ref mut value) => {
                value.visit_depth_first_internal(f);
            },
            Self::Progress {
                ref mut value,
                ref mut start,
                ref mut end,
                ..
            } => {
                value.visit_depth_first_internal(f);
                start.visit_depth_first_internal(f);
                end.visit_depth_first_internal(f);
            },
            Self::Random(ref mut random) => {
                random.key.visit_depth_first_internal(f);
                random.min.visit_depth_first_internal(f);
                random.max.visit_depth_first_internal(f);
                if let Some(step) = random.step.as_mut() {
                    step.visit_depth_first_internal(f);
                }
            },
            Self::Leaf(..) | Self::Anchor(..) | Self::AnchorSize(..) => {},
        }
        f(self);
    }

    /// This function simplifies and sorts the calculation of the specified node. It simplifies
    /// directly nested nodes while assuming that all nodes below it have already been simplified.
    /// It is recommended to use this function in combination with `visit_depth_first()`.
    ///
    /// This function is necessary only if the node needs to be preserved after parsing,
    /// specifically for `<length-percentage>` cases where the calculation contains percentages or
    /// relative units. Otherwise, the node can be evaluated using `resolve()`, which will
    /// automatically provide a simplified value.
    ///
    /// <https://drafts.csswg.org/css-values-4/#calc-simplification>
    pub fn simplify_and_sort_direct_children(&mut self) -> SimplificationResult {
        macro_rules! replace_self_with {
            ($slot:expr) => {{
                let result = mem::replace($slot, Self::dummy());
                *self = result;
            }};
        }

        macro_rules! value_or_stop {
            ($op:expr) => {{
                match $op {
                    Ok(value) => value,
                    Err(_) => return SimplificationResult::Unchanged,
                }
            }};
        }

        match *self {
            Self::Clamp {
                ref mut min,
                ref mut center,
                ref mut max,
            } => {
                // NOTE: clamp() is max(min, min(center, max))
                let min_cmp_center = match min.compare(center) {
                    Some(o) => o,
                    None => return SimplificationResult::Unchanged,
                };

                // So if we can prove that min is more than center, then we won,
                // as that's what we should always return.
                if matches!(min_cmp_center, cmp::Ordering::Greater) {
                    replace_self_with!(&mut **min);
                    return SimplificationResult::Simplified;
                }

                // Otherwise try with max.
                let max_cmp_center = match max.compare(center) {
                    Some(o) => o,
                    None => return SimplificationResult::Unchanged,
                };

                if matches!(max_cmp_center, cmp::Ordering::Less) {
                    // max is less than center, so we need to return effectively
                    // `max(min, max)`.
                    let max_cmp_min = match max.compare(min) {
                        Some(o) => o,
                        None => return SimplificationResult::Unchanged,
                    };

                    if matches!(max_cmp_min, cmp::Ordering::Less) {
                        replace_self_with!(&mut **min);
                        return SimplificationResult::Simplified;
                    }

                    replace_self_with!(&mut **max);
                    return SimplificationResult::Simplified;
                }

                // Otherwise we're the center node.
                replace_self_with!(&mut **center);
                SimplificationResult::Simplified
            },
            Self::Round {
                strategy,
                ref mut value,
                ref mut step,
            } => {
                if value_or_stop!(step.is_zero_leaf()) {
                    value_or_stop!(value.coerce_to_value(f32::NAN));
                    replace_self_with!(&mut **value);
                    return SimplificationResult::Simplified;
                }

                if value_or_stop!(value.is_infinite_leaf())
                    && value_or_stop!(step.is_infinite_leaf())
                {
                    value_or_stop!(value.coerce_to_value(f32::NAN));
                    replace_self_with!(&mut **value);
                    return SimplificationResult::Simplified;
                }

                if value_or_stop!(value.is_infinite_leaf()) {
                    replace_self_with!(&mut **value);
                    return SimplificationResult::Simplified;
                }

                if value_or_stop!(step.is_infinite_leaf()) {
                    match strategy {
                        RoundingStrategy::Nearest | RoundingStrategy::ToZero => {
                            value_or_stop!(value.coerce_to_value(0.0));
                            replace_self_with!(&mut **value);
                            return SimplificationResult::Simplified;
                        },
                        RoundingStrategy::Up => {
                            if !value_or_stop!(value.is_negative_leaf())
                                && !value_or_stop!(value.is_zero_leaf())
                            {
                                value_or_stop!(value.coerce_to_value(f32::INFINITY));
                                replace_self_with!(&mut **value);
                                return SimplificationResult::Simplified;
                            } else if !value_or_stop!(value.is_negative_leaf())
                                && value_or_stop!(value.is_zero_leaf())
                            {
                                replace_self_with!(&mut **value);
                                return SimplificationResult::Simplified;
                            } else {
                                value_or_stop!(value.coerce_to_value(0.0));
                                replace_self_with!(&mut **value);
                                return SimplificationResult::Simplified;
                            }
                        },
                        RoundingStrategy::Down => {
                            if value_or_stop!(value.is_negative_leaf())
                                && !value_or_stop!(value.is_zero_leaf())
                            {
                                value_or_stop!(value.coerce_to_value(-f32::INFINITY));
                                replace_self_with!(&mut **value);
                                return SimplificationResult::Simplified;
                            } else if value_or_stop!(value.is_negative_leaf())
                                && value_or_stop!(value.is_zero_leaf())
                            {
                                replace_self_with!(&mut **value);
                                return SimplificationResult::Simplified;
                            } else {
                                value_or_stop!(value.coerce_to_value(0.0));
                                replace_self_with!(&mut **value);
                                return SimplificationResult::Simplified;
                            }
                        },
                    }
                }

                if value_or_stop!(step.is_negative_leaf()) {
                    step.negate();
                }

                let remainder = value_or_stop!(value.try_op(step, Rem::rem));
                if value_or_stop!(remainder.is_zero_leaf()) {
                    replace_self_with!(&mut **value);
                    return SimplificationResult::Simplified;
                }

                let (mut lower_bound, mut upper_bound) = if value_or_stop!(value.is_negative_leaf())
                {
                    let upper_bound = value_or_stop!(value.try_op(&remainder, Sub::sub));
                    let lower_bound = value_or_stop!(upper_bound.try_op(step, Sub::sub));

                    (lower_bound, upper_bound)
                } else {
                    let lower_bound = value_or_stop!(value.try_op(&remainder, Sub::sub));
                    let upper_bound = value_or_stop!(lower_bound.try_op(step, Add::add));

                    (lower_bound, upper_bound)
                };

                match strategy {
                    RoundingStrategy::Nearest => {
                        let lower_diff = value_or_stop!(value.try_op(&lower_bound, Sub::sub));
                        let upper_diff = value_or_stop!(upper_bound.try_op(value, Sub::sub));
                        // In case of a tie, use the upper bound
                        if lower_diff.lt(&upper_diff) {
                            replace_self_with!(&mut lower_bound);
                        } else {
                            replace_self_with!(&mut upper_bound);
                        }
                    },
                    RoundingStrategy::Up => {
                        replace_self_with!(&mut upper_bound);
                    },
                    RoundingStrategy::Down => {
                        replace_self_with!(&mut lower_bound);
                    },
                    RoundingStrategy::ToZero => {
                        let mut lower_diff = lower_bound.clone();
                        let mut upper_diff = upper_bound.clone();

                        if value_or_stop!(lower_diff.is_negative_leaf()) {
                            lower_diff.negate();
                        }

                        if value_or_stop!(upper_diff.is_negative_leaf()) {
                            upper_diff.negate();
                        }

                        // In case of a tie, use the upper bound
                        if lower_diff.lt(&upper_diff) {
                            replace_self_with!(&mut lower_bound);
                        } else {
                            replace_self_with!(&mut upper_bound);
                        }
                    },
                };
                SimplificationResult::Simplified
            },
            Self::ModRem {
                ref dividend,
                ref divisor,
                op,
            } => {
                let mut result = value_or_stop!(dividend.try_op(divisor, |a, b| op.apply(a, b)));
                replace_self_with!(&mut result);
                SimplificationResult::Simplified
            },
            Self::MinMax(ref mut children, op) => {
                let winning_order = match op {
                    MinMaxOp::Min => cmp::Ordering::Less,
                    MinMaxOp::Max => cmp::Ordering::Greater,
                };

                if value_or_stop!(children[0].is_nan_leaf()) {
                    replace_self_with!(&mut children[0]);
                    return SimplificationResult::Simplified;
                }

                let mut result = 0;
                for i in 1..children.len() {
                    if value_or_stop!(children[i].is_nan_leaf()) {
                        replace_self_with!(&mut children[i]);
                        return SimplificationResult::Simplified;
                    }
                    let o = match children[i].compare(&children[result]) {
                        // We can't compare all the children, so we can't
                        // know which one will actually win. Bail out and
                        // keep ourselves as a min / max function.
                        //
                        // TODO: Maybe we could simplify compatible children,
                        // see https://github.com/w3c/csswg-drafts/issues/4756
                        None => return SimplificationResult::Unchanged,
                        Some(o) => o,
                    };

                    if o == winning_order {
                        result = i;
                    }
                }

                replace_self_with!(&mut children[result]);
                SimplificationResult::Simplified
            },
            Self::Sum(ref mut children_slot) => {
                let mut sums_to_merge = SmallVec::<[_; 3]>::new();
                let mut extra_kids = 0;
                for (i, child) in children_slot.iter().enumerate() {
                    if let Self::Sum(ref children) = *child {
                        extra_kids += children.len();
                        sums_to_merge.push(i);
                    }
                }

                // If we only have one kid, we've already simplified it, and it
                // doesn't really matter whether it's a sum already or not, so
                // lift it up and continue.
                if children_slot.len() == 1 {
                    replace_self_with!(&mut children_slot[0]);
                    return SimplificationResult::Simplified;
                }

                let mut children = mem::take(children_slot).into_vec();

                if !sums_to_merge.is_empty() {
                    children.reserve(extra_kids - sums_to_merge.len());
                    // Merge all our nested sums, in reverse order so that the
                    // list indices are not invalidated.
                    for i in sums_to_merge.drain(..).rev() {
                        let kid_children = match children.swap_remove(i) {
                            Self::Sum(c) => c,
                            _ => unreachable!(),
                        };

                        // This would be nicer with
                        // https://github.com/rust-lang/rust/issues/59878 fixed.
                        children.extend(kid_children.into_vec());
                    }
                }

                let children_len = children.len();
                debug_assert!(children_len >= 2, "Should still have multiple kids!");

                // Sort by spec order.
                children.sort_unstable_by_key(|c| c.sort_key());

                // NOTE: if the function returns true, by the docs of dedup_by,
                // a is removed.
                children.dedup_by(|a, b| b.try_sum_in_place(a).is_ok());

                let updated_children_len = children.len();
                if updated_children_len == 1 {
                    // If only one children remains, lift it up, and carry on.
                    replace_self_with!(&mut children[0]);
                } else {
                    // Else put our simplified children back.
                    *children_slot = children.into_boxed_slice().into();
                }

                if updated_children_len != children_len {
                    SimplificationResult::Simplified
                } else {
                    SimplificationResult::Unchanged
                }
            },
            Self::Product(ref mut children_slot) => {
                let mut products_to_merge = SmallVec::<[_; 3]>::new();
                let mut extra_kids = 0;
                for (i, child) in children_slot.iter().enumerate() {
                    if let Self::Product(ref children) = *child {
                        extra_kids += children.len();
                        products_to_merge.push(i);
                    }
                }

                // If we only have one kid, we've already simplified it, and it
                // doesn't really matter whether it's a product already or not,
                // so lift it up and continue.
                if children_slot.len() == 1 {
                    replace_self_with!(&mut children_slot[0]);
                    return SimplificationResult::Unchanged;
                }

                let mut children = mem::take(children_slot).into_vec();
                if !products_to_merge.is_empty() {
                    children.reserve(extra_kids - products_to_merge.len());
                    // Merge all our nested sums, in reverse order so that the
                    // list indices are not invalidated.
                    for i in products_to_merge.drain(..).rev() {
                        let kid_children = match children.swap_remove(i) {
                            Self::Product(c) => c,
                            _ => unreachable!(),
                        };

                        // This would be nicer with
                        // https://github.com/rust-lang/rust/issues/59878 fixed.
                        children.extend(kid_children.into_vec());
                    }
                }

                debug_assert!(children.len() >= 2, "Should still have multiple kids!");

                // Sort by spec order.
                children.sort_unstable_by_key(|c| c.sort_key());

                // NOTE: if the function returns true, by the docs of dedup_by,
                // a is removed.
                children.dedup_by(|right, left| left.try_product_in_place(right));

                if children.len() == 1 {
                    // If only one children remains, lift it up, and carry on.
                    replace_self_with!(&mut children[0]);
                    return SimplificationResult::Simplified;
                }

                // "If root contains only numeric values and/or Invert nodes containing numeric values,
                // and multiplying the types of all the children (noting that the type of an Invert
                // node is the inverse of its child’s type) results in a type that matches any of the
                // types that a math function can resolve to, return the result of multiplying all the
                // values of the children (noting that the value of an Invert node is the reciprocal of
                // its child’s value), expressed in the result’s canonical unit."
                //
                // https://drafts.csswg.org/css-values-4/#simplify-a-calculation-tree
                let mut result = 1.0;
                let mut ty = Ok(NumericType::number());

                for child in children.iter() {
                    let (leaf, is_inverted) = match child {
                        Self::Leaf(leaf) => (leaf, false),
                        Self::Invert(inner) if inner.as_leaf().is_some() => {
                            (inner.as_leaf().unwrap(), true)
                        },
                        _ => {
                            ty = Err(());
                            break;
                        },
                    };

                    // Only multiply values that are in that type's canonical unit.
                    let Some(value) = leaf.canonical_value() else {
                        ty = Err(());
                        break;
                    };
                    let (multiplicand, child_ty) = if is_inverted {
                        let mut ty = leaf.numeric_type();
                        ty.invert();
                        (1.0 / value, ty)
                    } else {
                        (value, leaf.numeric_type())
                    };

                    result *= multiplicand;
                    ty = ty.and_then(|ty| NumericType::multiply_two_types(&ty, &child_ty));
                }

                if let Ok(leaf) = ty.and_then(|ty| L::new_from_typed_value(result, ty)) {
                    let mut result = Self::Leaf(leaf);
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }

                // Else put our simplified children back.
                *children_slot = children.into_boxed_slice().into();
                SimplificationResult::Unchanged
            },
            Self::Sin(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(radians) = leaf.as_number_or_angle_radians()
                {
                    let mut result = Self::Leaf(L::new_number(radians.sin()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Cos(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(radians) = leaf.as_number_or_angle_radians()
                {
                    let mut result = Self::Leaf(L::new_number(radians.cos()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Tan(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(radians) = leaf.as_number_or_angle_radians()
                {
                    let mut result = Self::Leaf(L::new_number(radians.tan()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Asin(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(value) = leaf.as_number()
                {
                    let mut result = Self::Leaf(L::new_angle_from_radians(value.asin()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Acos(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(value) = leaf.as_number()
                {
                    let mut result = Self::Leaf(L::new_angle_from_radians(value.acos()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Atan(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(value) = leaf.as_number()
                {
                    let mut result = Self::Leaf(L::new_angle_from_radians(value.atan()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Atan2(ref mut a, ref mut b) => {
                if let (CalcNode::Leaf(la), CalcNode::Leaf(lb)) = (&**a, &**b)
                    && la.is_same_unit_as(lb)
                    && let (Some(a_val), Some(b_val)) = (la.unitless_value(), lb.unitless_value())
                {
                    let mut result = Self::Leaf(L::new_angle_from_radians(a_val.atan2(b_val)));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Pow(ref mut a, ref mut b) => {
                if let (CalcNode::Leaf(la), CalcNode::Leaf(lb)) = (&**a, &**b)
                    && let (Some(a_val), Some(b_val)) = (la.as_number(), lb.as_number())
                {
                    let mut result = Self::Leaf(L::new_number(a_val.powf(b_val)));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Sqrt(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(value) = leaf.as_number()
                {
                    let mut result = Self::Leaf(L::new_number(value.sqrt()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Hypot(ref children) => {
                let mut result = value_or_stop!(children[0].try_op(&children[0], Mul::mul));

                for child in children.iter().skip(1) {
                    let square = value_or_stop!(child.try_op(child, Mul::mul));
                    result = value_or_stop!(result.try_op(&square, Add::add));
                }

                result = value_or_stop!(result.try_op(&result, |a, _| a.sqrt()));

                replace_self_with!(&mut result);
                SimplificationResult::Simplified
            },
            Self::Log(ref mut a, ref mut b) => {
                if let CalcNode::Leaf(ref la) = **a
                    && let Some(a_val) = la.as_number()
                {
                    let folded = match b {
                        &mut Optional::Some(ref b) => {
                            if let CalcNode::Leaf(ref lb) = **b {
                                lb.as_number().map(|b_val| a_val.log(b_val))
                            } else {
                                None
                            }
                        },
                        Optional::None => Some(a_val.ln()),
                    };
                    if let Some(number) = folded {
                        let mut result = Self::Leaf(L::new_number(number));
                        replace_self_with!(&mut result);
                        return SimplificationResult::Simplified;
                    }
                }
                SimplificationResult::Unchanged
            },
            Self::Exp(ref mut child) => {
                if let CalcNode::Leaf(ref leaf) = **child
                    && let Some(value) = leaf.as_number()
                {
                    let mut result = Self::Leaf(L::new_number(value.exp()));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Abs(ref mut child) => {
                if let CalcNode::Leaf(leaf) = child.as_mut() {
                    value_or_stop!(leaf.map(|v| v.abs()));
                    replace_self_with!(&mut **child);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Sign(ref mut child) => {
                if let CalcNode::Leaf(leaf) = child.as_mut() {
                    let mut result = Self::Leaf(value_or_stop!(L::sign_from(leaf)));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Negate(ref mut child) => {
                // Step 6.
                match &mut **child {
                    CalcNode::Leaf(_) => {
                        // 1. If root’s child is a numeric value, return an equivalent numeric value, but
                        // with the value negated (0 - value).
                        child.negate();
                        replace_self_with!(&mut **child);
                        SimplificationResult::Simplified
                    },
                    CalcNode::Negate(value) => {
                        // 2. If root’s child is a Negate node, return the child’s child.
                        replace_self_with!(&mut **value);
                        SimplificationResult::Simplified
                    },
                    _ => {
                        // 3. Return root.
                        SimplificationResult::Unchanged
                    },
                }
            },
            Self::Invert(ref mut child) => {
                // Step 7.
                match &mut **child {
                    CalcNode::Leaf(leaf) => {
                        // 1. If root’s child is a number (not a percentage or dimension) return the
                        // reciprocal of the child’s value.
                        if leaf.numeric_type().is_number() {
                            value_or_stop!(child.map(|v| 1.0 / v));
                            replace_self_with!(&mut **child);
                            return SimplificationResult::Simplified;
                        }
                        SimplificationResult::Unchanged
                    },
                    CalcNode::Invert(value) => {
                        // 2. If root’s child is an Invert node, return the child’s child.
                        replace_self_with!(&mut **value);
                        SimplificationResult::Simplified
                    },
                    _ => {
                        // 3. Return root.
                        SimplificationResult::Unchanged
                    },
                }
            },
            Self::Progress {
                clamping_mode,
                ref mut value,
                ref mut start,
                ref mut end,
            } => {
                if let (CalcNode::Leaf(value), CalcNode::Leaf(start), CalcNode::Leaf(end)) =
                    (&**value, &**start, &**end)
                    && value.is_same_unit_as(start)
                    && value.is_same_unit_as(end)
                    && let (Some(value), Some(start), Some(end)) = (
                        value.unitless_value(),
                        start.unitless_value(),
                        end.unitless_value(),
                    )
                {
                    let mut result =
                        Self::Leaf(L::new_number(clamping_mode.evaluate(value, start, end)));
                    replace_self_with!(&mut result);
                    return SimplificationResult::Simplified;
                }
                SimplificationResult::Unchanged
            },
            Self::Random(ref mut r) => {
                let (
                    CalcNode::Leaf(key_leaf),
                    CalcNode::Leaf(min_leaf),
                    CalcNode::Leaf(max_leaf),
                ) = (&r.key, &r.min, &r.max)
                else {
                    return SimplificationResult::Unchanged;
                };

                if !min_leaf.is_same_unit_as(max_leaf) {
                    return SimplificationResult::Unchanged;
                }

                let (Some(base), Some(min), Some(max)) = (
                    key_leaf.canonical_value(),
                    min_leaf.canonical_value(),
                    max_leaf.canonical_value(),
                ) else {
                    return SimplificationResult::Unchanged;
                };

                let step = match &r.step {
                    Optional::Some(step) => {
                        let CalcNode::Leaf(step_leaf) = step else {
                            return SimplificationResult::Unchanged;
                        };
                        if !min_leaf.is_same_unit_as(step_leaf) {
                            return SimplificationResult::Unchanged;
                        }
                        let Some(step) = step_leaf.unitless_value() else {
                            return SimplificationResult::Unchanged;
                        };
                        Some(step)
                    },
                    Optional::None => None,
                };

                let result = calc_random(base, min, max, step);
                if r.min.coerce_to_value(result).is_err() {
                    return SimplificationResult::Unchanged;
                }
                replace_self_with!(&mut r.min);
                SimplificationResult::Simplified
            },
            Self::Leaf(ref mut l) => l.simplify(),
            Self::Anchor(ref mut f) => {
                if let GenericAnchorSide::Percentage(ref mut n) = f.side {
                    n.simplify_and_sort();
                    return SimplificationResult::Simplified;
                }
                if let Some(fallback) = f.fallback.as_mut() {
                    return fallback.node.simplify_and_sort();
                }
                SimplificationResult::Unchanged
            },
            Self::AnchorSize(ref mut f) => {
                if let Some(fallback) = f.fallback.as_mut() {
                    return fallback.node.simplify_and_sort();
                }
                SimplificationResult::Unchanged
            },
        }
    }

    /// Simplifies and sorts the kids in the whole calculation subtree.
    pub fn simplify_and_sort(&mut self) -> SimplificationResult {
        let mut res = SimplificationResult::Unchanged;
        self.visit_depth_first(|node| {
            if let SimplificationResult::Simplified = node.simplify_and_sort_direct_children() {
                res = SimplificationResult::Simplified;
            }
        });
        res
    }

    fn to_css_impl<W>(&self, dest: &mut CssWriter<W>, level: ArgumentLevel) -> fmt::Result
    where
        W: Write,
    {
        let write_closing_paren = match self {
            Self::MinMax(_, op) => {
                dest.write_str(match op {
                    MinMaxOp::Max => "max(",
                    MinMaxOp::Min => "min(",
                })?;
                true
            },
            Self::Clamp { .. } => {
                dest.write_str("clamp(")?;
                true
            },
            Self::Round { strategy, .. } => {
                match strategy {
                    RoundingStrategy::Nearest => dest.write_str("round("),
                    RoundingStrategy::Up => dest.write_str("round(up, "),
                    RoundingStrategy::Down => dest.write_str("round(down, "),
                    RoundingStrategy::ToZero => dest.write_str("round(to-zero, "),
                }?;

                true
            },
            Self::ModRem { op, .. } => {
                dest.write_str(match op {
                    ModRemOp::Mod => "mod(",
                    ModRemOp::Rem => "rem(",
                })?;

                true
            },
            Self::Sin(_) => {
                dest.write_str("sin(")?;
                true
            },
            Self::Cos(_) => {
                dest.write_str("cos(")?;
                true
            },
            Self::Tan(_) => {
                dest.write_str("tan(")?;
                true
            },
            Self::Asin(_) => {
                dest.write_str("asin(")?;
                true
            },
            Self::Acos(_) => {
                dest.write_str("acos(")?;
                true
            },
            Self::Atan(_) => {
                dest.write_str("atan(")?;
                true
            },
            Self::Atan2(..) => {
                dest.write_str("atan2(")?;
                true
            },
            Self::Pow(..) => {
                dest.write_str("pow(")?;
                true
            },
            Self::Sqrt(_) => {
                dest.write_str("sqrt(")?;
                true
            },
            Self::Hypot(_) => {
                dest.write_str("hypot(")?;
                true
            },
            Self::Log(..) => {
                dest.write_str("log(")?;
                true
            },
            Self::Exp(_) => {
                dest.write_str("exp(")?;
                true
            },
            Self::Abs(_) => {
                dest.write_str("abs(")?;
                true
            },
            Self::Sign(_) => {
                dest.write_str("sign(")?;
                true
            },
            Self::Progress { .. } => {
                dest.write_str("progress(")?;
                true
            },
            Self::Random(_) => {
                dest.write_str("random(")?;
                true
            },
            Self::Negate(_) => {
                // We never generate a [`Negate`] node as the root of a calculation, only inside
                // [`Sum`] nodes as a child. Because negate nodes are handled by the [`Sum`] node
                // directly (see below), this node will never be serialized.
                debug_assert!(
                    false,
                    "We never serialize Negate nodes as they are handled inside Sum nodes."
                );
                dest.write_str("(-1 * ")?;
                true
            },
            Self::Invert(_) => {
                if matches!(level, ArgumentLevel::CalculationRoot) {
                    dest.write_str("calc")?;
                }
                dest.write_str("(1 / ")?;
                true
            },
            Self::Sum(_) | Self::Product(_) => match level {
                ArgumentLevel::CalculationRoot => {
                    dest.write_str("calc(")?;
                    true
                },
                ArgumentLevel::ArgumentRoot => false,
                ArgumentLevel::Nested => {
                    dest.write_str("(")?;
                    true
                },
            },
            Self::Leaf(leaf) => match level {
                ArgumentLevel::CalculationRoot => {
                    if leaf.should_serialize_with_root_calc_wrapper() {
                        dest.write_str("calc(")?;
                        true
                    } else {
                        false
                    }
                },
                ArgumentLevel::ArgumentRoot | ArgumentLevel::Nested => false,
            },
            Self::Anchor(_) | Self::AnchorSize(_) => false,
        };

        match *self {
            Self::MinMax(ref children, _) | Self::Hypot(ref children) => {
                let mut first = true;
                for child in &**children {
                    if !first {
                        dest.write_str(", ")?;
                    }
                    first = false;
                    child.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                }
            },
            Self::Negate(ref value) | Self::Invert(ref value) => {
                value.to_css_impl(dest, ArgumentLevel::Nested)?
            },
            Self::Sum(ref children) => {
                let mut first = true;
                for child in &**children {
                    if !first {
                        match child {
                            Self::Leaf(l) => {
                                if let Ok(true) = l.is_negative() {
                                    dest.write_str(" - ")?;
                                    let mut negated = l.clone();
                                    // We can unwrap here, because we already
                                    // checked if the value inside is negative.
                                    negated.map(std::ops::Neg::neg).unwrap();
                                    negated.to_css(dest)?;
                                } else {
                                    dest.write_str(" + ")?;
                                    l.to_css(dest)?;
                                }
                            },
                            Self::Negate(n) => {
                                dest.write_str(" - ")?;
                                n.to_css_impl(dest, ArgumentLevel::Nested)?;
                            },
                            _ => {
                                dest.write_str(" + ")?;
                                child.to_css_impl(dest, ArgumentLevel::Nested)?;
                            },
                        }
                    } else {
                        first = false;
                        child.to_css_impl(dest, ArgumentLevel::Nested)?;
                    }
                }
            },
            Self::Product(ref children) => {
                let mut first = true;
                for child in &**children {
                    if !first {
                        match child {
                            Self::Invert(n) => {
                                dest.write_str(" / ")?;
                                n.to_css_impl(dest, ArgumentLevel::Nested)?;
                            },
                            _ => {
                                dest.write_str(" * ")?;
                                child.to_css_impl(dest, ArgumentLevel::Nested)?;
                            },
                        }
                    } else {
                        first = false;
                        child.to_css_impl(dest, ArgumentLevel::Nested)?;
                    }
                }
            },
            Self::Clamp {
                ref min,
                ref center,
                ref max,
            } => {
                min.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                center.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                max.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
            },
            Self::Round {
                ref value,
                ref step,
                ..
            } => {
                value.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                step.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
            },
            Self::ModRem {
                ref dividend,
                ref divisor,
                ..
            } => {
                dividend.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                divisor.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
            },
            Self::Sin(ref v)
            | Self::Cos(ref v)
            | Self::Tan(ref v)
            | Self::Asin(ref v)
            | Self::Acos(ref v)
            | Self::Atan(ref v) => v.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?,
            Self::Atan2(ref a, ref b) => {
                a.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                b.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
            },
            Self::Pow(ref a, ref b) => {
                a.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                b.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
            },
            Self::Sqrt(ref v) | Self::Exp(ref v) => {
                v.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?
            },
            Self::Log(ref a, ref b) => {
                a.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                if let Optional::Some(b) = b {
                    dest.write_str(", ")?;
                    b.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                }
            },
            Self::Abs(ref v) | Self::Sign(ref v) => {
                v.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?
            },
            Self::Progress {
                clamping_mode,
                ref value,
                ref start,
                ref end,
            } => {
                if clamping_mode == ProgressClampingMode::NoClamp {
                    clamping_mode.to_css(dest)?;
                    dest.write_char(' ')?;
                }
                value.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                start.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                end.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
            },
            Self::Random(ref r) => {
                // If the random key was computed to a number, serialized into the "fixed" form.
                if r.key.as_leaf().and_then(|l| l.as_number()).is_some() {
                    dest.write_str("fixed ")?;
                }
                r.key.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                r.min.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                dest.write_str(", ")?;
                r.max.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                if let Some(step) = r.step.as_ref() {
                    dest.write_str(", ")?;
                    step.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?;
                }
            },
            Self::Leaf(ref l) => l.to_css(dest)?,
            Self::Anchor(ref f) => f.to_css(dest)?,
            Self::AnchorSize(ref f) => f.to_css(dest)?,
        }

        if write_closing_paren {
            dest.write_char(')')?;
        }
        Ok(())
    }

    fn to_typed_impl(
        &self,
        dest: &mut ThinVec<TypedValue>,
        level: ArgumentLevel,
    ) -> Result<(), ()> {
        // Note: Naturally, only nodes that can be reified into CSSUnitValue
        // and CSSMathValue objects are supported here:
        // Leaf, Negate, Invert, Sum, Product, MinMax, and Clamp.
        match *self {
            Self::Leaf(ref l) => match l.to_typed_value() {
                Some(TypedValue::Numeric(inner)) => {
                    match level {
                        ArgumentLevel::CalculationRoot => {
                            dest.push(TypedValue::Numeric(NumericValue::Math(MathValue::Sum(
                                MathSum::try_from_numeric_values(ThinVec::from([inner]))?,
                            ))));
                        },
                        ArgumentLevel::ArgumentRoot | ArgumentLevel::Nested => {
                            dest.push(TypedValue::Numeric(inner));
                        },
                    }
                    Ok(())
                },
                _ => Err(()),
            },
            Self::Negate(_) => {
                // We never generate a [`Negate`] node as the root of a calculation, only inside
                // [`Sum`] nodes as a child. Because negate nodes are handled by the [`Sum`] node
                // directly (see below), this node will never be reified.
                debug_assert!(
                    false,
                    "We never reify Negate nodes as they are handled inside Sum nodes."
                );

                Err(())
            },
            Self::Invert(ref value) => {
                let inner = CalcNodeWithLevel::nested(value)
                    .to_numeric_value()
                    .ok_or(())?;

                dest.push(TypedValue::Numeric(NumericValue::Math(MathValue::Invert(
                    MathInvert::from_numeric_value(inner),
                ))));
                Ok(())
            },
            Self::Sum(ref children) => {
                let mut values = ThinVec::new();
                let mut first = true;

                for child in &**children {
                    if !first {
                        match child {
                            Self::Leaf(l) => {
                                if let Ok(true) = l.is_negative() {
                                    let mut negated = l.clone();

                                    // We can unwrap here, because we already
                                    // checked if the value inside is negative.
                                    negated.map(std::ops::Neg::neg).unwrap();

                                    let inner = negated.to_numeric_value().ok_or(())?;

                                    values.push(NumericValue::Math(MathValue::Negate(
                                        MathNegate::from_numeric_value(inner),
                                    )));
                                } else {
                                    let inner = l.to_numeric_value().ok_or(())?;

                                    values.push(inner);
                                }
                            },
                            Self::Negate(n) => {
                                let inner = CalcNodeWithLevel::nested(n.as_ref())
                                    .to_numeric_value()
                                    .ok_or(())?;

                                values.push(NumericValue::Math(MathValue::Negate(
                                    MathNegate::from_numeric_value(inner),
                                )));
                            },
                            _ => {
                                let inner = CalcNodeWithLevel::nested(child)
                                    .to_numeric_value()
                                    .ok_or(())?;

                                values.push(inner);
                            },
                        }
                    } else {
                        first = false;

                        let inner = CalcNodeWithLevel::nested(child)
                            .to_numeric_value()
                            .ok_or(())?;

                        values.push(inner);
                    }
                }

                dest.push(TypedValue::Numeric(NumericValue::Math(MathValue::Sum(
                    MathSum::try_from_numeric_values(values)?,
                ))));
                Ok(())
            },
            Self::Product(ref children) => {
                let mut values = ThinVec::new();
                let mut first = true;

                for child in &**children {
                    if !first {
                        match child {
                            Self::Invert(n) => {
                                let inner = CalcNodeWithLevel::nested(n.as_ref())
                                    .to_numeric_value()
                                    .ok_or(())?;

                                values.push(NumericValue::Math(MathValue::Invert(
                                    MathInvert::from_numeric_value(inner),
                                )));
                            },
                            _ => {
                                let inner = CalcNodeWithLevel::nested(child)
                                    .to_numeric_value()
                                    .ok_or(())?;

                                values.push(inner);
                            },
                        }
                    } else {
                        first = false;

                        let inner = CalcNodeWithLevel::nested(child)
                            .to_numeric_value()
                            .ok_or(())?;

                        values.push(inner);
                    }
                }

                dest.push(TypedValue::Numeric(NumericValue::Math(MathValue::Product(
                    MathProduct::try_from_numeric_values(values)?,
                ))));
                Ok(())
            },
            Self::MinMax(ref children, op) => {
                let mut values = ThinVec::new();

                for child in &**children {
                    let inner = CalcNodeWithLevel::argument_root(child)
                        .to_numeric_value()
                        .ok_or(())?;

                    values.push(inner);
                }

                let math_value = match op {
                    MinMaxOp::Min => MathValue::Min(MathMin::try_from_numeric_values(values)?),
                    MinMaxOp::Max => MathValue::Max(MathMax::try_from_numeric_values(values)?),
                };

                dest.push(TypedValue::Numeric(NumericValue::Math(math_value)));
                Ok(())
            },
            Self::Clamp {
                ref min,
                ref center,
                ref max,
            } => {
                let lower = CalcNodeWithLevel::argument_root(min)
                    .to_numeric_value()
                    .ok_or(())?;

                let value = CalcNodeWithLevel::argument_root(center)
                    .to_numeric_value()
                    .ok_or(())?;

                let upper = CalcNodeWithLevel::argument_root(max)
                    .to_numeric_value()
                    .ok_or(())?;

                dest.push(TypedValue::Numeric(NumericValue::Math(MathValue::Clamp(
                    MathClamp::try_from_numeric_values([lower, value, upper].into())?,
                ))));
                Ok(())
            },
            _ => Err(()),
        }
    }

    fn compare(&self, other: &Self) -> Option<cmp::Ordering> {
        match (self, other) {
            (CalcNode::Leaf(one), CalcNode::Leaf(other)) => one.compare(other),
            _ => None,
        }
    }

    compare_helpers!();
}

impl<L: CalcNodeLeaf> ToCss for CalcNode<L> {
    /// <https://drafts.csswg.org/css-values/#calc-serialize>
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.to_css_impl(dest, ArgumentLevel::CalculationRoot)
    }
}

impl<L: CalcNodeLeaf> ToTyped for CalcNode<L> {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        CalcNodeWithLevel::calculation_root(self).to_typed(dest)
    }
}

struct CalcNodeWithLevel<'a, L> {
    node: &'a CalcNode<L>,
    level: ArgumentLevel,
}

impl<'a, L> CalcNodeWithLevel<'a, L> {
    #[inline]
    fn new(node: &'a CalcNode<L>, level: ArgumentLevel) -> Self {
        Self { node, level }
    }

    #[inline]
    fn calculation_root(node: &'a CalcNode<L>) -> Self {
        Self::new(node, ArgumentLevel::CalculationRoot)
    }

    #[inline]
    fn argument_root(node: &'a CalcNode<L>) -> Self {
        Self::new(node, ArgumentLevel::ArgumentRoot)
    }

    #[inline]
    fn nested(node: &'a CalcNode<L>) -> Self {
        Self::new(node, ArgumentLevel::Nested)
    }
}

impl<'a, L: CalcNodeLeaf> ToTyped for CalcNodeWithLevel<'a, L> {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        self.node.to_typed_impl(dest, self.level.clone())
    }
}
