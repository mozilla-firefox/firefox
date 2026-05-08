/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified numbers and integers.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::transform::DirectionVector;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::generics::transform::IsParallelTo;
use crate::values::generics::{GreaterThanOrEqualToOne, NonNegative};
use crate::values::specified::calc::{CalcNode, CalcNumeric, Leaf};
use crate::values::specified::Percentage;
use crate::values::{reify_number, serialize_number};
use crate::values::{CSSFloat, CSSInteger};
use crate::{One, Zero};
use cssparser::{Parser, Token};
use std::fmt::{self, Write};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{CssWriter, ParseError, SpecifiedValueInfo, ToCss, ToTyped, TypedValue};
use thin_vec::ThinVec;

/// Parse a `<number>` value, with a given clamping mode.
pub fn parse_number_with_clamping_mode<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
    clamping_mode: AllowedNumericType,
) -> Result<Number, ParseError<'i>> {
    let location = input.current_source_location();
    match *input.next()? {
        Token::Number { value, .. } if clamping_mode.is_ok(context.parsing_mode, value) => {
            Ok(Number::NoCalc(NoCalcNumber::new(value)))
        },
        Token::Function(ref name) => {
            let function = CalcNode::math_function(context, name, location)?;
            CalcNode::parse_number(context, input, clamping_mode, function)
                .map(Box::new)
                .map(Number::Calc)
        },
        ref t => Err(location.new_unexpected_token_error(t.clone())),
    }
}

/// Parse an `<integer>` value, with a given clamping mode.
pub fn parse_integer_with_clamping_mode<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
    clamping_mode: AllowedNumericType,
) -> Result<Integer, ParseError<'i>> {
    let location = input.current_source_location();
    match *input.next()? {
        Token::Number {
            int_value: Some(v), ..
        } if clamping_mode.is_ok(context.parsing_mode, v as f32) => Ok(Integer::new(v)),
        Token::Function(ref name) => {
            let function = CalcNode::math_function(context, name, location)?;
            CalcNode::parse_number(context, input, clamping_mode, function)
                .map(Box::new)
                .map(Integer::Calc)
        },
        ref t => Err(location.new_unexpected_token_error(t.clone())),
    }
}

/// A non-calc `<number>` value.
#[derive(Clone, Copy, Debug, MallocSizeOf, ToShmem)]
#[repr(C)]
pub struct NoCalcNumber(CSSFloat);

impl NoCalcNumber {
    /// Returns a new literal number with the value `val`.
    #[inline]
    pub fn new(val: CSSFloat) -> Self {
        Self(val)
    }

    /// Returns the raw, underlying value of this number.
    #[inline]
    pub fn value(&self) -> f32 {
        self.0
    }

    /// Returns the numeric value, clamped if needed.
    #[inline]
    pub fn get(&self) -> f32 {
        crate::values::normalize(self.0).min(f32::MAX).max(f32::MIN)
    }

    /// Returns the unit string for a number value.
    pub fn unit(&self) -> &'static str {
        "number"
    }

    /// Returns the canonical unit for a number value (none).
    pub fn canonical_unit(&self) -> Option<&'static str> {
        None
    }

    /// Converts to the given unit, only succeeding if the unit is "number".
    pub fn to(&self, unit: &str) -> Result<Self, ()> {
        if !unit.eq_ignore_ascii_case("number") {
            return Err(());
        }
        Ok(self.clone())
    }
}

impl PartialEq<NoCalcNumber> for NoCalcNumber {
    fn eq(&self, other: &NoCalcNumber) -> bool {
        self.0 == other.0 || (self.0.is_nan() && other.0.is_nan())
    }
}

impl PartialOrd<NoCalcNumber> for NoCalcNumber {
    fn partial_cmp(&self, other: &NoCalcNumber) -> Option<std::cmp::Ordering> {
        self.get().partial_cmp(&other.get())
    }
}

impl ToCss for NoCalcNumber {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_number(self.0, /* was_calc = */ false, dest)
    }
}

impl ToTyped for NoCalcNumber {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        reify_number(self.0, /* was_calc = */ false, dest)
    }
}

impl ToComputedValue for NoCalcNumber {
    type ComputedValue = CSSFloat;

    fn to_computed_value(&self, _: &Context) -> Self::ComputedValue {
        self.get()
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self::new(*computed)
    }
}

/// A CSS `<number>` specified value.
///
/// https://drafts.csswg.org/css-values-3/#number-value
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem, ToTyped)]
pub enum Number {
    /// A literal <number> value.
    NoCalc(NoCalcNumber),
    /// A calc() expression that produces a <number>.
    Calc(Box<CalcNumeric>),
}

impl Parse for Number {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_number_with_clamping_mode(context, input, AllowedNumericType::All)
    }
}

impl PartialOrd<Number> for Number {
    fn partial_cmp(&self, other: &Number) -> Option<std::cmp::Ordering> {
        self.get().partial_cmp(&other.get())
    }
}

impl Number {
    /// Returns a new number with the value `val`.
    #[inline]
    pub fn new(val: CSSFloat) -> Self {
        Number::NoCalc(NoCalcNumber::new(val))
    }

    /// Returns this number as a percentage.
    pub fn to_percentage(&self) -> Percentage {
        match self {
            Number::NoCalc(n) => Percentage::new(n.value()),
            Number::Calc(_) => {
                // TODO Bug 2035572 - Number::Calc requires Percentage::Calc
                Percentage::zero()
            },
        }
    }

    /// Returns the value if this is a plain (non-calc) number, or None otherwise.
    /// Use `resolve()` to also handle resolvable calc expressions, or `to_computed_value()`
    /// when computed context is available.
    #[inline]
    pub fn get(&self) -> Option<f32> {
        match self {
            Number::NoCalc(n) => Some(n.get()),
            Number::Calc(_) => None,
        }
    }

    /// Returns the value if it can be resolved at parse time, including resolvable calc
    /// expressions. Returns None for calc expressions that require computed-value context.
    pub fn resolve(&self) -> Option<f32> {
        match self {
            Number::NoCalc(n) => Some(n.get()),
            Number::Calc(ref calc) => calc.as_number().map(|n| n.get()),
        }
    }

    #[allow(missing_docs)]
    pub fn parse_non_negative<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Number, ParseError<'i>> {
        parse_number_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
    }

    #[allow(missing_docs)]
    pub fn parse_at_least_one<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Number, ParseError<'i>> {
        parse_number_with_clamping_mode(context, input, AllowedNumericType::AtLeastOne)
    }

    /// Clamp to 1.0 if the value is over 1.0.
    #[inline]
    pub fn clamp_to_one(self) -> Self {
        match self {
            Number::NoCalc(n) => Number::new(n.value().min(1.)),
            Number::Calc(ref calc) => Number::Calc(Box::new(
                calc.with_clamping_mode(AllowedNumericType::ZeroToOne),
            )),
        }
    }
}

impl ToComputedValue for Number {
    type ComputedValue = CSSFloat;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> CSSFloat {
        match self {
            Number::NoCalc(n) => n.to_computed_value(context),
            Number::Calc(ref calc) => {
                let value = match calc.node.with_computed_context(context).resolve() {
                    Ok(Leaf::Number(n)) => n.get(),
                    _ => {
                        debug_assert!(false, "Unexpected Number::Calc without resolved number");
                        f32::NAN
                    },
                };
                crate::values::normalize(calc.clamping_mode.clamp(value))
                    .min(f32::MAX)
                    .max(f32::MIN)
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &CSSFloat) -> Self {
        Number::new(*computed)
    }
}

impl IsParallelTo for (Number, Number, Number) {
    fn is_parallel_to(&self, vector: &DirectionVector) -> bool {
        use euclid::approxeq::ApproxEq;
        // If a and b is parallel, the angle between them is 0deg, so
        // a x b = |a|*|b|*sin(0)*n = 0 * n, |a x b| == 0.
        match (self.0.get(), self.1.get(), self.2.get()) {
            (Some(x), Some(y), Some(z)) => DirectionVector::new(x, y, z)
                .cross(*vector)
                .square_length()
                .approx_eq(&0.0f32),
            _ => false,
        }
    }
}

impl SpecifiedValueInfo for Number {}

impl Zero for Number {
    #[inline]
    fn zero() -> Self {
        Self::new(0.)
    }

    // Returns true if this number was a non-calc 0.
    #[inline]
    fn is_zero(&self) -> bool {
        matches!(self, Self::NoCalc(NoCalcNumber(0.)))
    }
}

/// A Number which is >= 0.0.
pub type NonNegativeNumber = NonNegative<Number>;

impl Parse for NonNegativeNumber {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_number_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
            .map(NonNegative::<Number>)
    }
}

impl One for NonNegativeNumber {
    #[inline]
    fn one() -> Self {
        NonNegativeNumber::new(1.0)
    }

    // Returns true if this number was a non-calc 1.
    #[inline]
    fn is_one(&self) -> bool {
        matches!(self.0, Number::NoCalc(NoCalcNumber(1.)))
    }
}

impl NonNegativeNumber {
    /// Returns a new non-negative number with the value `val`.
    pub fn new(val: CSSFloat) -> Self {
        NonNegative::<Number>(Number::new(val.max(0.)))
    }

    /// Returns the numeric value.
    #[inline]
    pub fn get(&self) -> Option<f32> {
        self.0.get()
    }
}

/// An Integer which is >= 0. For calc expressions that couldn't be resolved at parse time,
/// this value is clamped to 0 at computed-value time.
pub type NonNegativeInteger = NonNegative<Integer>;

impl Parse for NonNegativeInteger {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(NonNegative(Integer::parse_non_negative(context, input)?))
    }
}

/// A Number which is >= 1.0.
pub type GreaterThanOrEqualToOneNumber = GreaterThanOrEqualToOne<Number>;

impl Parse for GreaterThanOrEqualToOneNumber {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_number_with_clamping_mode(context, input, AllowedNumericType::AtLeastOne)
            .map(GreaterThanOrEqualToOne::<Number>)
    }
}

/// A specified `<integer>`, either a simple integer value, a resolved calc expression,
/// or a full calc expression tree that cannot be computed at parse time.
/// Note that a calc expression may not actually be an integer; it will be rounded
/// at computed-value time.
///
/// <https://drafts.csswg.org/css-values/#integers>
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem, ToTyped)]
#[typed(todo_derive_fields)]
pub enum Integer {
    /// A literal integer value.
    NoCalc(CSSInteger),
    /// A calc expression that produces an <integer>, stored as a full
    /// AST node to be evaluated at computed-value time.
    Calc(Box<CalcNumeric>),
}

impl Zero for Integer {
    #[inline]
    fn zero() -> Self {
        Self::new(0)
    }

    // Returns true if this integer was a non-calc 0.
    #[inline]
    fn is_zero(&self) -> bool {
        matches!(self, Self::NoCalc(0))
    }
}

impl One for Integer {
    #[inline]
    fn one() -> Self {
        Self::new(1)
    }

    // Returns true if this integer was a non-calc 1.
    #[inline]
    fn is_one(&self) -> bool {
        matches!(self, Self::NoCalc(1))
    }
}

impl PartialEq<i32> for Integer {
    fn eq(&self, value: &i32) -> bool {
        self.get().is_some_and(|v| v == *value)
    }
}

impl Integer {
    /// Trivially constructs a new `Integer` value.
    pub fn new(val: CSSInteger) -> Self {
        Self::NoCalc(val)
    }

    /// Returns the value if this is a plain (non-calc) integer, or None otherwise.
    /// Use `resolve()` to also handle resolvable calc expressions, or `to_computed_value()`
    /// when computed context is available.
    pub fn get(&self) -> Option<CSSInteger> {
        match *self {
            Self::NoCalc(i) => Some(i),
            Self::Calc(_) => None,
        }
    }

    /// Returns the value if it can be resolved at parse time, including resolvable calc
    /// expressions. Returns None for calc expressions that require computed-value context.
    pub fn resolve(&self) -> Option<CSSInteger> {
        Some(match self {
            Self::NoCalc(i) => *i,
            Self::Calc(ref calc) => {
                let value = calc.as_number()?.get();
                (value + 0.5).floor() as CSSInteger
            },
        })
    }
}

impl Parse for Integer {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_integer_with_clamping_mode(context, input, AllowedNumericType::All)
    }
}

impl Integer {
    /// Parse a non-negative integer.
    pub fn parse_non_negative<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Integer, ParseError<'i>> {
        parse_integer_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
    }

    /// Parse a positive integer (>= 1).
    pub fn parse_positive<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Integer, ParseError<'i>> {
        parse_integer_with_clamping_mode(context, input, AllowedNumericType::AtLeastOne)
    }
}

impl ToComputedValue for Integer {
    type ComputedValue = i32;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> i32 {
        match *self {
            Self::NoCalc(i) => i,
            Self::Calc(ref calc) => {
                let value = match calc.node.with_computed_context(context).resolve() {
                    Ok(Leaf::Number(n)) => n.get(),
                    _ => {
                        debug_assert!(false, "Unexpected Integer::Calc without resolved number");
                        f32::NAN
                    },
                };
                let clamped = crate::values::normalize(calc.clamping_mode.clamp(value))
                    .min(f32::MAX)
                    .max(f32::MIN);
                (clamped + 0.5).floor() as i32
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &i32) -> Self {
        Integer::new(*computed)
    }
}

impl SpecifiedValueInfo for Integer {}

/// An Integer which is >= 1. For calc expressions that couldn't be resolved at parse time,
/// this value is clamped to 1 at computed-value time.
pub type PositiveInteger = GreaterThanOrEqualToOne<Integer>;

impl Parse for PositiveInteger {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Integer::parse_positive(context, input).map(GreaterThanOrEqualToOne)
    }
}
