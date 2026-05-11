/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified percentages.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::percentage::Percentage as ComputedPercentage;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::generics::NonNegative;
use crate::values::specified::calc::{CalcNode, CalcNumeric, Leaf};
use crate::values::specified::{NoCalcNumber, Number};
use crate::values::{normalize, reify_percentage, serialize_percentage, CSSFloat};
use cssparser::{Parser, Token};
use std::fmt::{self, Write};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{CssWriter, ParseError, SpecifiedValueInfo, ToCss, ToTyped, TypedValue};
use thin_vec::ThinVec;

/// A percentage value, where [0 .. 100%] maps to [0.0 .. 1.0]
#[derive(Clone, Copy, Debug, Default, MallocSizeOf, PartialEq, ToShmem)]
#[repr(C)]
pub struct NoCalcPercentage(CSSFloat);

impl SpecifiedValueInfo for NoCalcPercentage {}

impl ToCss for NoCalcPercentage {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_percentage(self.0, dest)
    }
}

impl ToTyped for NoCalcPercentage {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        reify_percentage(self.0, /* was_calc = */ false, dest)
    }
}

impl NoCalcPercentage {
    /// Creates a percentage from a numeric value.
    pub fn new(value: CSSFloat) -> Self {
        Self(value)
    }

    /// `0%`
    #[inline]
    pub fn zero() -> Self {
        Self::new(0.)
    }

    /// `100%`
    #[inline]
    pub fn hundred() -> Self {
        Self::new(1.)
    }

    /// Gets the underlying value for this float.
    #[inline]
    pub fn get(&self) -> CSSFloat {
        self.0
    }

    /// Return the unit, as a string.
    pub fn unit(&self) -> &'static str {
        "percent"
    }

    /// Return no canonical unit (percent values do not have one).
    pub fn canonical_unit(&self) -> Option<&'static str> {
        None
    }

    /// Convert only if the unit is the same (conversion to other units does
    /// not make sense).
    pub fn to(&self, unit: &str) -> Result<Self, ()> {
        if !unit.eq_ignore_ascii_case("percent") {
            return Err(());
        }
        Ok(self.clone())
    }
}

impl ToComputedValue for NoCalcPercentage {
    type ComputedValue = ComputedPercentage;

    fn to_computed_value(&self, _: &Context) -> Self::ComputedValue {
        ComputedPercentage(normalize(self.get()))
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self::new(computed.0)
    }
}

/// A specified percentage value, either a plain value or a `calc()` expression.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem, ToTyped)]
pub enum Percentage {
    /// A plain percentage value.
    NoCalc(NoCalcPercentage),
    /// A `calc()` expression that resolves to a percentage.
    Calc(Box<CalcNumeric>),
}

impl Percentage {
    /// Creates a percentage from a numeric value.
    pub fn new(value: CSSFloat) -> Self {
        Self::NoCalc(NoCalcPercentage::new(value))
    }

    /// `0%`
    #[inline]
    pub fn zero() -> Self {
        Self::NoCalc(NoCalcPercentage::zero())
    }

    /// `100%`
    #[inline]
    pub fn hundred() -> Self {
        Self::NoCalc(NoCalcPercentage::hundred())
    }

    /// Returns the value if this is a plain (non-calc) percentage, or None otherwise.
    /// Use `resolve()` to also handle resolvable calc expressions, or `to_computed_value()`
    /// when computed context is available.
    #[inline]
    pub fn get(&self) -> Option<CSSFloat> {
        match self {
            Percentage::NoCalc(p) => Some(p.get()),
            Percentage::Calc(_) => None,
        }
    }

    /// Returns the value if it can be resolved at parse time, including resolvable calc
    /// expressions. Returns None for calc expressions that require computed context
    /// (e.g. those using relative lengths or sibling-index()).
    #[inline]
    pub fn resolve(&self) -> Option<CSSFloat> {
        match self {
            Percentage::NoCalc(p) => Some(p.get()),
            Percentage::Calc(ref calc) => calc.as_percentage().map(|p| p.get()),
        }
    }

    /// Returns this percentage as a number.
    pub fn to_number(&self) -> Option<Number> {
        Some(match self {
            Percentage::NoCalc(p) => Number::new(p.0),
            Percentage::Calc(ref calc) => {
                let p = calc.as_percentage()?.get();
                Number::Calc(Box::new(
                    calc.with_leaf_node(Leaf::Number(NoCalcNumber::new(p))),
                ))
            },
        })
    }

    /// Reverses this percentage, preserving calc-ness.
    ///
    /// For example: If it was 20%, convert it into 80%.
    pub fn reverse(&mut self) {
        match self {
            Percentage::NoCalc(p) => {
                p.0 = 1. - p.0;
            },
            Percentage::Calc(calc) => {
                let mut sum = smallvec::SmallVec::<[CalcNode; 2]>::new();
                sum.push(CalcNode::Leaf(
                    Leaf::Percentage(NoCalcPercentage::hundred()),
                ));
                let mut node = calc.node.clone();
                node.negate();
                sum.push(node);
                let mut diff = CalcNode::Sum(sum.into_boxed_slice().into());
                diff.simplify_and_sort();
                calc.node = diff;
            },
        }
    }

    /// Parses a specific kind of percentage.
    pub fn parse_with_clamping_mode<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        num_context: AllowedNumericType,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        match *input.next()? {
            Token::Percentage { unit_value, .. }
                if num_context.is_ok(context.parsing_mode, unit_value) =>
            {
                Ok(Percentage::new(unit_value))
            },
            Token::Function(ref name) => {
                let function = CalcNode::math_function(context, name, location)?;
                CalcNode::parse_percentage(context, input, num_context, function)
                    .map(Box::new)
                    .map(Percentage::Calc)
            },
            ref t => Err(location.new_unexpected_token_error(t.clone())),
        }
    }

    /// Parses a percentage token, but rejects it if it's negative.
    pub fn parse_non_negative<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
    }

    /// Parses a percentage token, but rejects it if it's negative or more than
    /// 100%.
    pub fn parse_zero_to_a_hundred<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::ZeroToOne)
    }

    /// Clamp to 100% if the value is over 100%.
    #[inline]
    pub fn clamp_to_hundred(self) -> Self {
        match self {
            Percentage::NoCalc(p) => Percentage::NoCalc(NoCalcPercentage::new(p.0.min(1.))),
            Percentage::Calc(ref calc) => Percentage::Calc(Box::new(
                calc.with_clamping_mode(AllowedNumericType::ZeroToOne),
            )),
        }
    }
}

impl Parse for Percentage {
    #[inline]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::All)
    }
}

impl ToComputedValue for Percentage {
    type ComputedValue = ComputedPercentage;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self {
            Percentage::NoCalc(p) => p.to_computed_value(context),
            Percentage::Calc(ref calc) => {
                let resolved = calc.node.with_computed_context(context).resolve();
                let value = match resolved {
                    Ok(Leaf::Percentage(p)) => p.get(),
                    _ => {
                        debug_assert!(
                            false,
                            "Unexpected Percentage::Calc without resolved percentage"
                        );
                        f32::NAN
                    },
                };
                ComputedPercentage(
                    crate::values::normalize(calc.clamping_mode.clamp(value))
                        .min(f32::MAX)
                        .max(f32::MIN),
                )
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Percentage::new(computed.0)
    }
}

impl SpecifiedValueInfo for Percentage {}

/// Turns the percentage into a plain float.
pub trait ToPercentage {
    /// Returns whether this percentage used to be a calc().
    fn is_calc(&self) -> bool {
        false
    }
    /// Returns the percentage as a plain float, or None for calc expressions that require
    /// computed context. Will always return Some if `is_calc` is false.
    fn to_percentage(&self) -> Option<CSSFloat>;
}

impl ToPercentage for Percentage {
    fn is_calc(&self) -> bool {
        matches!(self, Percentage::Calc(_))
    }

    fn to_percentage(&self) -> Option<CSSFloat> {
        self.resolve()
    }
}

/// A wrapper of Percentage, whose value must be >= 0.
pub type NonNegativePercentage = NonNegative<Percentage>;

impl Parse for NonNegativePercentage {
    #[inline]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(NonNegative(Percentage::parse_non_negative(context, input)?))
    }
}

impl NonNegativePercentage {
    /// Convert to ComputedPercentage, for FontFaceRule size-adjust getter.
    /// Returns None if the value is a calc expression that cannot be resolved at parse time.
    #[inline]
    pub fn compute(&self) -> Option<ComputedPercentage> {
        self.0
            .resolve()
            .map(|f| AllowedNumericType::NonNegative.clamp(f))
            .map(ComputedPercentage)
    }
}
