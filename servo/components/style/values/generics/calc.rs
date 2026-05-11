/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! [Calc expressions][calc].
//!
//! [calc]: https://drafts.csswg.org/css-values/#calc-notation

use crate::derives::*;
use crate::values::generics::length::GenericAnchorSizeFunction;
use crate::values::generics::position::{GenericAnchorFunction, GenericAnchorSide};
use num_traits::Zero;
use smallvec::SmallVec;
use std::convert::AsRef;
use std::fmt::{self, Write};
use std::ops::{Add, Mul, Neg, Rem, Sub};
use std::{cmp, mem};
use strum_macros::AsRefStr;
use style_traits::{CssWriter, MathSum, NumericValue, ToCss, ToTyped, TypedValue};

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
    Negate(Box<GenericCalcNode<L>>),
    /// A node that inverts its child, e.g. Invert(10) == 1 / 10 == 0.1. The child must always
    /// resolve to a number unit.
    Invert(Box<GenericCalcNode<L>>),
    /// A sum node, representing `a + b + c` where a, b, and c are the
    /// arguments.
    Sum(crate::OwnedSlice<GenericCalcNode<L>>),
    /// A product node, representing `a * b * c` where a, b, and c are the
    /// arguments.
    Product(crate::OwnedSlice<GenericCalcNode<L>>),
    /// A `min` or `max` function.
    MinMax(crate::OwnedSlice<GenericCalcNode<L>>, MinMaxOp),
    /// A `clamp()` function.
    Clamp {
        /// The minimum value.
        min: Box<GenericCalcNode<L>>,
        /// The central value.
        center: Box<GenericCalcNode<L>>,
        /// The maximum value.
        max: Box<GenericCalcNode<L>>,
    },
    /// A `round()` function.
    Round {
        /// The rounding strategy.
        strategy: RoundingStrategy,
        /// The value to round.
        value: Box<GenericCalcNode<L>>,
        /// The step value.
        step: Box<GenericCalcNode<L>>,
    },
    /// A `mod()` or `rem()` function.
    ModRem {
        /// The dividend calculation.
        dividend: Box<GenericCalcNode<L>>,
        /// The divisor calculation.
        divisor: Box<GenericCalcNode<L>>,
        /// Is the function mod or rem?
        op: ModRemOp,
    },
    /// A `hypot()` function
    Hypot(crate::OwnedSlice<GenericCalcNode<L>>),
    /// An `abs()` function.
    Abs(Box<GenericCalcNode<L>>),
    /// A `sign()` function.
    Sign(Box<GenericCalcNode<L>>),
    /// An `anchor()` function.
    Anchor(Box<GenericCalcAnchorFunction<L>>),
    /// An `anchor-size()` function.
    AnchorSize(Box<GenericCalcAnchorSizeFunction<L>>),
}

pub use self::GenericCalcNode as CalcNode;

bitflags! {
    /// Expected units we allow parsing within a `calc()` expression.
    ///
    /// This is used as a hint for the parser to fast-reject invalid
    /// expressions. Numbers are always allowed because they multiply other
    /// units.
    #[derive(Clone, Copy, PartialEq, Eq)]
    pub struct CalcUnits: u8 {
        /// <length>
        const LENGTH = 1 << 0;
        /// <percentage>
        const PERCENTAGE = 1 << 1;
        /// <angle>
        const ANGLE = 1 << 2;
        /// <time>
        const TIME = 1 << 3;
        /// <resolution>
        const RESOLUTION = 1 << 4;
        /// A component of a color (r, g, b, h, s, l, alpha, etc.)
        const COLOR_COMPONENT = 1 << 5;

        /// <length-percentage>
        const LENGTH_PERCENTAGE = Self::LENGTH.bits() | Self::PERCENTAGE.bits();
        // NOTE: When you add to this, make sure to make Atan2 deal with these.
        /// Allow all units.
        const ALL = Self::LENGTH.bits() | Self::PERCENTAGE.bits() | Self::ANGLE.bits() |
            Self::TIME.bits() | Self::RESOLUTION.bits() | Self::COLOR_COMPONENT.bits();
    }
}

impl CalcUnits {
    /// Returns whether the flags only represent a single unit. This will return true for 0, which
    /// is a "number" this is also fine.
    #[inline]
    fn is_single_unit(&self) -> bool {
        self.bits() == 0 || self.bits() & (self.bits() - 1) == 0
    }

    /// Returns true if this unit is allowed to be summed with the given unit, otherwise false.
    #[inline]
    fn can_sum_with(&self, other: Self) -> bool {
        match *self {
            Self::LENGTH => other.intersects(Self::LENGTH | Self::PERCENTAGE),
            Self::PERCENTAGE => other.intersects(Self::LENGTH | Self::PERCENTAGE),
            Self::LENGTH_PERCENTAGE => other.intersects(Self::LENGTH | Self::PERCENTAGE),
            u => u.is_single_unit() && other == u,
        }
    }
}

/// For percentage resolution, sometimes we can't assume that the percentage basis is positive (so
/// we don't know whether a percentage is larger than another).
pub enum PositivePercentageBasis {
    /// The percent basis is not known-positive, we can't compare percentages.
    Unknown,
    /// The percent basis is known-positive, we assume larger percentages are larger.
    Yes,
}

macro_rules! compare_helpers {
    () => {
        /// Return whether a leaf is greater than another.
        #[allow(unused)]
        fn gt(&self, other: &Self, basis_positive: PositivePercentageBasis) -> bool {
            self.compare(other, basis_positive) == Some(cmp::Ordering::Greater)
        }

        /// Return whether a leaf is less than another.
        fn lt(&self, other: &Self, basis_positive: PositivePercentageBasis) -> bool {
            self.compare(other, basis_positive) == Some(cmp::Ordering::Less)
        }

        /// Return whether a leaf is smaller or equal than another.
        fn lte(&self, other: &Self, basis_positive: PositivePercentageBasis) -> bool {
            match self.compare(other, basis_positive) {
                Some(cmp::Ordering::Less) => true,
                Some(cmp::Ordering::Equal) => true,
                Some(cmp::Ordering::Greater) => false,
                None => false,
            }
        }
    };
}

/// A trait that represents all the stuff a valid leaf of a calc expression.
pub trait CalcNodeLeaf: Clone + Sized + PartialEq + ToCss + ToTyped {
    /// Returns the unit of the leaf.
    fn unit(&self) -> CalcUnits;

    /// Returns the unitless value of this leaf if one is available.
    fn unitless_value(&self) -> Option<f32>;

    /// Return true if the units of both leaves are equal. (NOTE: Does not take
    /// the values into account)
    fn is_same_unit_as(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }

    /// Do a partial comparison of these values.
    fn compare(
        &self,
        other: &Self,
        base_is_positive: PositivePercentageBasis,
    ) -> Option<cmp::Ordering>;
    compare_helpers!();

    /// Create a new leaf with a number value.
    fn new_number(value: f32) -> Self;

    /// Returns a float value if the leaf is a number.
    fn as_number(&self) -> Option<f32>;

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
    fn simplify(&mut self);

    /// Returns the sort key for simplification.
    fn sort_key(&self) -> SortKey;

    /// Create a new leaf containing the sign() result of the given leaf.
    fn sign_from(leaf: &impl CalcNodeLeaf) -> Result<Self, ()> {
        let Some(value) = leaf.unitless_value() else {
            return Err(());
        };

        Ok(Self::new_number(if value.is_nan() {
            f32::NAN
        } else if value.is_zero() {
            value
        } else if value.is_sign_negative() {
            -1.0
        } else {
            1.0
        }))
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
            Self::Leaf(l) => l.unit() != CalcUnits::COLOR_COMPONENT,
            Self::Sum(children) => children.iter().all(|c| c.is_product_distributive()),
            _ => false,
        }
    }

    /// If the node has a valid unit outcome, then return it, otherwise fail.
    pub fn unit(&self) -> Result<CalcUnits, ()> {
        Ok(match self {
            CalcNode::Leaf(l) => l.unit(),
            CalcNode::Negate(child) | CalcNode::Invert(child) | CalcNode::Abs(child) => {
                child.unit()?
            },
            CalcNode::Sum(children) => {
                let mut unit = children.first().unwrap().unit()?;
                for child in children.iter().skip(1) {
                    let child_unit = child.unit()?;
                    if !child_unit.can_sum_with(unit) {
                        return Err(());
                    }
                    unit |= child_unit;
                }
                unit
            },
            CalcNode::Product(children) => {
                // Only one node is allowed to have a unit, the rest must be numbers.
                let mut unit = None;
                for child in children.iter() {
                    let child_unit = child.unit()?;
                    if child_unit.is_empty() {
                        // Numbers are always allowed in a product, so continue with the next.
                        continue;
                    }

                    if unit.is_some() {
                        // We already have a unit for the node, so another unit node is invalid.
                        return Err(());
                    }

                    // We have the unit for the node.
                    unit = Some(child_unit);
                }
                // We only keep track of specified units, so if we end up with a None and no failure
                // so far, then we have a number.
                unit.unwrap_or(CalcUnits::empty())
            },
            CalcNode::MinMax(children, _) | CalcNode::Hypot(children) => {
                let mut unit = children.first().unwrap().unit()?;
                for child in children.iter().skip(1) {
                    let child_unit = child.unit()?;
                    if !child_unit.can_sum_with(unit) {
                        return Err(());
                    }
                    unit |= child_unit;
                }
                unit
            },
            CalcNode::Clamp { min, center, max } => {
                let min_unit = min.unit()?;
                let center_unit = center.unit()?;

                if !min_unit.can_sum_with(center_unit) {
                    return Err(());
                }

                let max_unit = max.unit()?;

                if !center_unit.can_sum_with(max_unit) {
                    return Err(());
                }

                min_unit | center_unit | max_unit
            },
            CalcNode::Round { value, step, .. } => {
                let value_unit = value.unit()?;
                let step_unit = step.unit()?;
                if !step_unit.can_sum_with(value_unit) {
                    return Err(());
                }
                value_unit | step_unit
            },
            CalcNode::ModRem {
                dividend, divisor, ..
            } => {
                let dividend_unit = dividend.unit()?;
                let divisor_unit = divisor.unit()?;
                if !divisor_unit.can_sum_with(dividend_unit) {
                    return Err(());
                }
                dividend_unit | divisor_unit
            },
            CalcNode::Sign(ref child) => {
                // sign() always resolves to a number, but we still need to make sure that the
                // child units make sense.
                let _ = child.unit()?;
                CalcUnits::empty()
            },
            CalcNode::Anchor(..) | CalcNode::AnchorSize(..) => CalcUnits::LENGTH_PERCENTAGE,
        })
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
                if min.lte(max, PositivePercentageBasis::Unknown) {
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
            CalcNode::Hypot(ref mut children) => {
                for child in children.iter_mut() {
                    child.negate();
                }
            },
            CalcNode::Abs(_) => {
                wrap_self_in_negate(self);
            },
            CalcNode::Sign(ref mut child) => {
                child.negate();
            },
            CalcNode::Anchor(_) | CalcNode::AnchorSize(_) => {
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
            (&mut CalcNode::Leaf(ref mut one), &CalcNode::Leaf(ref other)) => {
                one.try_sum_in_place(other)
            },
            _ => Err(()),
        }
    }

    /// Tries to merge one node into another using the product, that is, perform `x` * `y`.
    pub fn try_product_in_place(&mut self, other: &mut Self) -> bool {
        if let Ok(resolved) = other.resolve() {
            if let Some(number) = resolved.as_number() {
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
        }

        if let Ok(resolved) = self.resolve() {
            if let Some(number) = resolved.as_number() {
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
        }

        false
    }

    /// Tries to apply a generic arithmetic operator
    fn try_op<O>(&self, other: &Self, op: O) -> Result<Self, ()>
    where
        O: Fn(f32, f32) -> f32,
    {
        match (self, other) {
            (&CalcNode::Leaf(ref one), &CalcNode::Leaf(ref other)) => {
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
            Self::Hypot(ref c) => CalcNode::Hypot(map_children(c, map)),
            Self::Abs(ref c) => CalcNode::Abs(Box::new(c.map_leaves_internal(map))),
            Self::Sign(ref c) => CalcNode::Sign(Box::new(c.map_leaves_internal(map))),
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
        self.resolve_internal(&mut leaf_to_output_fn)
    }

    fn resolve_internal<F>(&self, leaf_to_output_fn: &mut F) -> Result<L, ()>
    where
        F: FnMut(&L) -> Result<L, ()>,
    {
        match self {
            Self::Leaf(l) => leaf_to_output_fn(l),
            Self::Negate(child) => {
                let mut result = child.resolve_internal(leaf_to_output_fn)?;
                result.map(|v| v.neg())?;
                Ok(result)
            },
            Self::Invert(child) => {
                let mut result = child.resolve_internal(leaf_to_output_fn)?;
                result.map(|v| 1.0 / v)?;
                Ok(result)
            },
            Self::Sum(children) => {
                let mut result = children[0].resolve_internal(leaf_to_output_fn)?;

                for child in children.iter().skip(1) {
                    let right = child.resolve_internal(leaf_to_output_fn)?;
                    // try_op will make sure we only sum leaves with the same type.
                    result = result.try_op(&right, |left, right| left + right)?;
                }

                Ok(result)
            },
            Self::Product(children) => {
                let mut result = children[0].resolve_internal(leaf_to_output_fn)?;

                for child in children.iter().skip(1) {
                    let right = child.resolve_internal(leaf_to_output_fn)?;
                    // Mutliply only allowed when either side is a number.
                    match result.as_number() {
                        Some(left) => {
                            // Left side is a number, so we use the right node as the result.
                            result = right;
                            result.map(|v| v * left)?;
                        },
                        None => {
                            // Left side is not a number, so check if the right side is.
                            match right.as_number() {
                                Some(right) => {
                                    result.map(|v| v * right)?;
                                },
                                None => {
                                    // Multiplying with both sides having units.
                                    return Err(());
                                },
                            }
                        },
                    }
                }

                Ok(result)
            },
            Self::MinMax(children, op) => {
                let mut result = children[0].resolve_internal(leaf_to_output_fn)?;

                if result.is_nan()? {
                    return Ok(result);
                }

                for child in children.iter().skip(1) {
                    let candidate = child.resolve_internal(leaf_to_output_fn)?;

                    // Leaf types must match for each child.
                    if !result.is_same_unit_as(&candidate) {
                        return Err(());
                    }

                    if candidate.is_nan()? {
                        result = candidate;
                        break;
                    }

                    let candidate_wins = match op {
                        MinMaxOp::Min => candidate.lt(&result, PositivePercentageBasis::Yes),
                        MinMaxOp::Max => candidate.gt(&result, PositivePercentageBasis::Yes),
                    };

                    if candidate_wins {
                        result = candidate;
                    }
                }

                Ok(result)
            },
            Self::Clamp { min, center, max } => {
                let min = min.resolve_internal(leaf_to_output_fn)?;
                let center = center.resolve_internal(leaf_to_output_fn)?;
                let max = max.resolve_internal(leaf_to_output_fn)?;

                if !min.is_same_unit_as(&center) || !max.is_same_unit_as(&center) {
                    return Err(());
                }

                if min.is_nan()? {
                    return Ok(min);
                }

                if center.is_nan()? {
                    return Ok(center);
                }

                if max.is_nan()? {
                    return Ok(max);
                }

                let mut result = center;
                if result.gt(&max, PositivePercentageBasis::Yes) {
                    result = max;
                }
                if result.lt(&min, PositivePercentageBasis::Yes) {
                    result = min
                }

                Ok(result)
            },
            Self::Round {
                strategy,
                value,
                step,
            } => {
                let mut value = value.resolve_internal(leaf_to_output_fn)?;
                let step = step.resolve_internal(leaf_to_output_fn)?;

                if !value.is_same_unit_as(&step) {
                    return Err(());
                }

                let Some(step) = step.unitless_value() else {
                    return Err(());
                };
                let step = step.abs();

                value.map(|value| {
                    // TODO(emilio): Seems like at least a few of these
                    // special-cases could be removed if we do the math in a
                    // particular order.
                    if step.is_zero() {
                        return f32::NAN;
                    }

                    if value.is_infinite() {
                        if step.is_infinite() {
                            return f32::NAN;
                        }
                        return value;
                    }

                    if step.is_infinite() {
                        match strategy {
                            RoundingStrategy::Nearest | RoundingStrategy::ToZero => {
                                return if value.is_sign_negative() { -0.0 } else { 0.0 }
                            },
                            RoundingStrategy::Up => {
                                return if !value.is_sign_negative() && !value.is_zero() {
                                    f32::INFINITY
                                } else if !value.is_sign_negative() && value.is_zero() {
                                    value
                                } else {
                                    -0.0
                                }
                            },
                            RoundingStrategy::Down => {
                                return if value.is_sign_negative() && !value.is_zero() {
                                    -f32::INFINITY
                                } else if value.is_sign_negative() && value.is_zero() {
                                    value
                                } else {
                                    0.0
                                }
                            },
                        }
                    }

                    let div = value / step;
                    let lower_bound = div.floor() * step;
                    let upper_bound = div.ceil() * step;

                    match strategy {
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
                })?;

                Ok(value)
            },
            Self::ModRem {
                dividend,
                divisor,
                op,
            } => {
                let mut dividend = dividend.resolve_internal(leaf_to_output_fn)?;
                let divisor = divisor.resolve_internal(leaf_to_output_fn)?;

                if !dividend.is_same_unit_as(&divisor) {
                    return Err(());
                }

                let Some(divisor) = divisor.unitless_value() else {
                    return Err(());
                };
                dividend.map(|dividend| op.apply(dividend, divisor))?;
                Ok(dividend)
            },
            Self::Hypot(children) => {
                let mut result = children[0].resolve_internal(leaf_to_output_fn)?;
                result.map(|v| v.powi(2))?;

                for child in children.iter().skip(1) {
                    let child_value = child.resolve_internal(leaf_to_output_fn)?;

                    if !result.is_same_unit_as(&child_value) {
                        return Err(());
                    }

                    let Some(child_value) = child_value.unitless_value() else {
                        return Err(());
                    };
                    result.map(|v| v + child_value.powi(2))?;
                }

                result.map(|v| v.sqrt())?;
                Ok(result)
            },
            Self::Abs(ref c) => {
                let mut result = c.resolve_internal(leaf_to_output_fn)?;

                result.map(|v| v.abs())?;

                Ok(result)
            },
            Self::Sign(ref c) => {
                let result = c.resolve_internal(leaf_to_output_fn)?;
                Ok(L::sign_from(&result)?)
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
            Self::Negate(child) | Self::Invert(child) | Self::Abs(child) | Self::Sign(child) => {
                child.map_node_internal(mapping_fn)?;
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
            Self::Abs(ref mut value) | Self::Sign(ref mut value) => {
                value.visit_depth_first_internal(f);
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
    pub fn simplify_and_sort_direct_children(&mut self) {
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
                    Err(_) => return,
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
                let min_cmp_center = match min.compare(&center, PositivePercentageBasis::Unknown) {
                    Some(o) => o,
                    None => return,
                };

                // So if we can prove that min is more than center, then we won,
                // as that's what we should always return.
                if matches!(min_cmp_center, cmp::Ordering::Greater) {
                    replace_self_with!(&mut **min);
                    return;
                }

                // Otherwise try with max.
                let max_cmp_center = match max.compare(&center, PositivePercentageBasis::Unknown) {
                    Some(o) => o,
                    None => return,
                };

                if matches!(max_cmp_center, cmp::Ordering::Less) {
                    // max is less than center, so we need to return effectively
                    // `max(min, max)`.
                    let max_cmp_min = match max.compare(&min, PositivePercentageBasis::Unknown) {
                        Some(o) => o,
                        None => return,
                    };

                    if matches!(max_cmp_min, cmp::Ordering::Less) {
                        replace_self_with!(&mut **min);
                        return;
                    }

                    replace_self_with!(&mut **max);
                    return;
                }

                // Otherwise we're the center node.
                replace_self_with!(&mut **center);
            },
            Self::Round {
                strategy,
                ref mut value,
                ref mut step,
            } => {
                if value_or_stop!(step.is_zero_leaf()) {
                    value_or_stop!(value.coerce_to_value(f32::NAN));
                    replace_self_with!(&mut **value);
                    return;
                }

                if value_or_stop!(value.is_infinite_leaf())
                    && value_or_stop!(step.is_infinite_leaf())
                {
                    value_or_stop!(value.coerce_to_value(f32::NAN));
                    replace_self_with!(&mut **value);
                    return;
                }

                if value_or_stop!(value.is_infinite_leaf()) {
                    replace_self_with!(&mut **value);
                    return;
                }

                if value_or_stop!(step.is_infinite_leaf()) {
                    match strategy {
                        RoundingStrategy::Nearest | RoundingStrategy::ToZero => {
                            value_or_stop!(value.coerce_to_value(0.0));
                            replace_self_with!(&mut **value);
                            return;
                        },
                        RoundingStrategy::Up => {
                            if !value_or_stop!(value.is_negative_leaf())
                                && !value_or_stop!(value.is_zero_leaf())
                            {
                                value_or_stop!(value.coerce_to_value(f32::INFINITY));
                                replace_self_with!(&mut **value);
                                return;
                            } else if !value_or_stop!(value.is_negative_leaf())
                                && value_or_stop!(value.is_zero_leaf())
                            {
                                replace_self_with!(&mut **value);
                                return;
                            } else {
                                value_or_stop!(value.coerce_to_value(0.0));
                                replace_self_with!(&mut **value);
                                return;
                            }
                        },
                        RoundingStrategy::Down => {
                            if value_or_stop!(value.is_negative_leaf())
                                && !value_or_stop!(value.is_zero_leaf())
                            {
                                value_or_stop!(value.coerce_to_value(-f32::INFINITY));
                                replace_self_with!(&mut **value);
                                return;
                            } else if value_or_stop!(value.is_negative_leaf())
                                && value_or_stop!(value.is_zero_leaf())
                            {
                                replace_self_with!(&mut **value);
                                return;
                            } else {
                                value_or_stop!(value.coerce_to_value(0.0));
                                replace_self_with!(&mut **value);
                                return;
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
                    return;
                }

                let (mut lower_bound, mut upper_bound) = if value_or_stop!(value.is_negative_leaf())
                {
                    let upper_bound = value_or_stop!(value.try_op(&remainder, Sub::sub));
                    let lower_bound = value_or_stop!(upper_bound.try_op(&step, Sub::sub));

                    (lower_bound, upper_bound)
                } else {
                    let lower_bound = value_or_stop!(value.try_op(&remainder, Sub::sub));
                    let upper_bound = value_or_stop!(lower_bound.try_op(&step, Add::add));

                    (lower_bound, upper_bound)
                };

                match strategy {
                    RoundingStrategy::Nearest => {
                        let lower_diff = value_or_stop!(value.try_op(&lower_bound, Sub::sub));
                        let upper_diff = value_or_stop!(upper_bound.try_op(value, Sub::sub));
                        // In case of a tie, use the upper bound
                        if lower_diff.lt(&upper_diff, PositivePercentageBasis::Unknown) {
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
                        if lower_diff.lt(&upper_diff, PositivePercentageBasis::Unknown) {
                            replace_self_with!(&mut lower_bound);
                        } else {
                            replace_self_with!(&mut upper_bound);
                        }
                    },
                };
            },
            Self::ModRem {
                ref dividend,
                ref divisor,
                op,
            } => {
                let mut result = value_or_stop!(dividend.try_op(divisor, |a, b| op.apply(a, b)));
                replace_self_with!(&mut result);
            },
            Self::MinMax(ref mut children, op) => {
                let winning_order = match op {
                    MinMaxOp::Min => cmp::Ordering::Less,
                    MinMaxOp::Max => cmp::Ordering::Greater,
                };

                if value_or_stop!(children[0].is_nan_leaf()) {
                    replace_self_with!(&mut children[0]);
                    return;
                }

                let mut result = 0;
                for i in 1..children.len() {
                    if value_or_stop!(children[i].is_nan_leaf()) {
                        replace_self_with!(&mut children[i]);
                        return;
                    }
                    let o = match children[i]
                        .compare(&children[result], PositivePercentageBasis::Unknown)
                    {
                        // We can't compare all the children, so we can't
                        // know which one will actually win. Bail out and
                        // keep ourselves as a min / max function.
                        //
                        // TODO: Maybe we could simplify compatible children,
                        // see https://github.com/w3c/csswg-drafts/issues/4756
                        None => return,
                        Some(o) => o,
                    };

                    if o == winning_order {
                        result = i;
                    }
                }

                replace_self_with!(&mut children[result]);
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
                    return;
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

                debug_assert!(children.len() >= 2, "Should still have multiple kids!");

                // Sort by spec order.
                children.sort_unstable_by_key(|c| c.sort_key());

                // NOTE: if the function returns true, by the docs of dedup_by,
                // a is removed.
                children.dedup_by(|a, b| b.try_sum_in_place(a).is_ok());

                if children.len() == 1 {
                    // If only one children remains, lift it up, and carry on.
                    replace_self_with!(&mut children[0]);
                } else {
                    // Else put our simplified children back.
                    *children_slot = children.into_boxed_slice().into();
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
                    return;
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
                } else {
                    // Else put our simplified children back.
                    *children_slot = children.into_boxed_slice().into();
                }
            },
            Self::Hypot(ref children) => {
                let mut result = value_or_stop!(children[0].try_op(&children[0], Mul::mul));

                for child in children.iter().skip(1) {
                    let square = value_or_stop!(child.try_op(&child, Mul::mul));
                    result = value_or_stop!(result.try_op(&square, Add::add));
                }

                result = value_or_stop!(result.try_op(&result, |a, _| a.sqrt()));

                replace_self_with!(&mut result);
            },
            Self::Abs(ref mut child) => {
                if let CalcNode::Leaf(leaf) = child.as_mut() {
                    value_or_stop!(leaf.map(|v| v.abs()));
                    replace_self_with!(&mut **child);
                }
            },
            Self::Sign(ref mut child) => {
                if let CalcNode::Leaf(leaf) = child.as_mut() {
                    let mut result = Self::Leaf(value_or_stop!(L::sign_from(leaf)));
                    replace_self_with!(&mut result);
                }
            },
            Self::Negate(ref mut child) => {
                // Step 6.
                match &mut **child {
                    CalcNode::Leaf(_) => {
                        // 1. If root’s child is a numeric value, return an equivalent numeric value, but
                        // with the value negated (0 - value).
                        child.negate();
                        replace_self_with!(&mut **child);
                    },
                    CalcNode::Negate(value) => {
                        // 2. If root’s child is a Negate node, return the child’s child.
                        replace_self_with!(&mut **value);
                    },
                    _ => {
                        // 3. Return root.
                    },
                }
            },
            Self::Invert(ref mut child) => {
                // Step 7.
                match &mut **child {
                    CalcNode::Leaf(leaf) => {
                        // 1. If root’s child is a number (not a percentage or dimension) return the
                        // reciprocal of the child’s value.
                        if leaf.unit().is_empty() {
                            value_or_stop!(child.map(|v| 1.0 / v));
                            replace_self_with!(&mut **child);
                        }
                    },
                    CalcNode::Invert(value) => {
                        // 2. If root’s child is an Invert node, return the child’s child.
                        replace_self_with!(&mut **value);
                    },
                    _ => {
                        // 3. Return root.
                    },
                }
            },
            Self::Leaf(ref mut l) => {
                l.simplify();
            },
            Self::Anchor(ref mut f) => {
                if let GenericAnchorSide::Percentage(ref mut n) = f.side {
                    n.simplify_and_sort();
                }
                if let Some(fallback) = f.fallback.as_mut() {
                    fallback.node.simplify_and_sort();
                }
            },
            Self::AnchorSize(ref mut f) => {
                if let Some(fallback) = f.fallback.as_mut() {
                    fallback.node.simplify_and_sort();
                }
            },
        }
    }

    /// Simplifies and sorts the kids in the whole calculation subtree.
    pub fn simplify_and_sort(&mut self) {
        self.visit_depth_first(|node| node.simplify_and_sort_direct_children())
    }

    fn to_css_impl<W>(&self, dest: &mut CssWriter<W>, level: ArgumentLevel) -> fmt::Result
    where
        W: Write,
    {
        let write_closing_paren = match *self {
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
            Self::Hypot(_) => {
                dest.write_str("hypot(")?;
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
            Self::Leaf(_) => match level {
                ArgumentLevel::CalculationRoot => {
                    dest.write_str("calc(")?;
                    true
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
            Self::Abs(ref v) | Self::Sign(ref v) => {
                v.to_css_impl(dest, ArgumentLevel::ArgumentRoot)?
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
        // XXX Only supporting Sum and Leaf for now
        match *self {
            Self::Sum(ref children) => {
                let mut values = ThinVec::new();
                for child in &**children {
                    let nested = CalcNodeWithLevel {
                        node: child,
                        level: ArgumentLevel::Nested,
                    };
                    if let Some(TypedValue::Numeric(inner)) = nested.to_typed_value() {
                        values.push(inner);
                    }
                }
                dest.push(TypedValue::Numeric(NumericValue::Sum(MathSum { values })));
                Ok(())
            },
            Self::Leaf(ref l) => match l.to_typed_value() {
                Some(TypedValue::Numeric(inner)) => {
                    match level {
                        ArgumentLevel::CalculationRoot => {
                            dest.push(TypedValue::Numeric(NumericValue::Sum(MathSum {
                                values: ThinVec::from([inner]),
                            })));
                        },
                        ArgumentLevel::ArgumentRoot | ArgumentLevel::Nested => {
                            dest.push(TypedValue::Numeric(inner));
                        },
                    }
                    Ok(())
                },
                _ => Err(()),
            },
            _ => Err(()),
        }
    }

    fn compare(
        &self,
        other: &Self,
        basis_positive: PositivePercentageBasis,
    ) -> Option<cmp::Ordering> {
        match (self, other) {
            (&CalcNode::Leaf(ref one), &CalcNode::Leaf(ref other)) => {
                one.compare(other, basis_positive)
            },
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
        self.to_typed_impl(dest, ArgumentLevel::CalculationRoot)
    }
}

struct CalcNodeWithLevel<'a, L> {
    node: &'a CalcNode<L>,
    level: ArgumentLevel,
}

impl<'a, L: CalcNodeLeaf> ToTyped for CalcNodeWithLevel<'a, L> {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        self.node.to_typed_impl(dest, self.level.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn can_sum_with_checks() {
        assert!(CalcUnits::LENGTH.can_sum_with(CalcUnits::LENGTH));
        assert!(CalcUnits::LENGTH.can_sum_with(CalcUnits::PERCENTAGE));
        assert!(CalcUnits::LENGTH.can_sum_with(CalcUnits::LENGTH_PERCENTAGE));

        assert!(CalcUnits::PERCENTAGE.can_sum_with(CalcUnits::LENGTH));
        assert!(CalcUnits::PERCENTAGE.can_sum_with(CalcUnits::PERCENTAGE));
        assert!(CalcUnits::PERCENTAGE.can_sum_with(CalcUnits::LENGTH_PERCENTAGE));

        assert!(CalcUnits::LENGTH_PERCENTAGE.can_sum_with(CalcUnits::LENGTH));
        assert!(CalcUnits::LENGTH_PERCENTAGE.can_sum_with(CalcUnits::PERCENTAGE));
        assert!(CalcUnits::LENGTH_PERCENTAGE.can_sum_with(CalcUnits::LENGTH_PERCENTAGE));

        assert!(!CalcUnits::ANGLE.can_sum_with(CalcUnits::TIME));
        assert!(CalcUnits::ANGLE.can_sum_with(CalcUnits::ANGLE));

        assert!(!(CalcUnits::ANGLE | CalcUnits::TIME).can_sum_with(CalcUnits::ANGLE));
        assert!(!CalcUnits::ANGLE.can_sum_with(CalcUnits::ANGLE | CalcUnits::TIME));
        assert!(
            !(CalcUnits::ANGLE | CalcUnits::TIME).can_sum_with(CalcUnits::ANGLE | CalcUnits::TIME)
        );
    }
}
