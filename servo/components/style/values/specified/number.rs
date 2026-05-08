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
use crate::values::specified::calc::CalcNode;
use crate::values::specified::Percentage;
use crate::values::{reify_number, serialize_number};
use crate::values::{CSSFloat, CSSInteger};
use crate::{One, Zero};
use cssparser::{Parser, Token};
use std::fmt::{self, Write};
use std::ops::Add;
use style_traits::values::specified::AllowedNumericType;
use style_traits::{
    CssWriter, ParseError, SpecifiedValueInfo, StyleParseErrorKind, ToCss, ToTyped, TypedValue,
};
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
            Ok(Number {
                value,
                calc_clamping_mode: None,
            })
        },
        Token::Function(ref name) => {
            let function = CalcNode::math_function(context, name, location)?;
            let value = CalcNode::parse_number(context, input, function)?;
            Ok(Number {
                value,
                calc_clamping_mode: Some(clamping_mode),
            })
        },
        ref t => Err(location.new_unexpected_token_error(t.clone())),
    }
}

/// A CSS `<number>` specified value.
///
/// https://drafts.csswg.org/css-values-3/#number-value
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialOrd, ToShmem)]
pub struct Number {
    /// The numeric value itself.
    value: CSSFloat,
    /// If this number came from a calc() expression, this tells how clamping
    /// should be done on the value.
    calc_clamping_mode: Option<AllowedNumericType>,
}

impl Parse for Number {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_number_with_clamping_mode(context, input, AllowedNumericType::All)
    }
}

impl PartialEq<Number> for Number {
    fn eq(&self, other: &Number) -> bool {
        if self.calc_clamping_mode != other.calc_clamping_mode {
            return false;
        }

        self.value == other.value || (self.value.is_nan() && other.value.is_nan())
    }
}

impl Number {
    /// Returns a new number with the value `val`.
    #[inline]
    pub fn new_with_clamping_mode(
        value: CSSFloat,
        calc_clamping_mode: Option<AllowedNumericType>,
    ) -> Self {
        Self {
            value,
            calc_clamping_mode,
        }
    }

    /// Returns this percentage as a number.
    pub fn to_percentage(&self) -> Percentage {
        Percentage::new_with_clamping_mode(self.value, self.calc_clamping_mode)
    }

    /// Returns a new number with the value `val`.
    #[inline]
    pub fn new(val: CSSFloat) -> Self {
        Self::new_with_clamping_mode(val, None)
    }

    /// Returns whether this number came from a `calc()` expression.
    #[inline]
    pub fn was_calc(&self) -> bool {
        self.calc_clamping_mode.is_some()
    }

    /// Returns the raw, underlying value of this number.
    #[inline]
    pub fn value(&self) -> CSSFloat {
        self.value
    }

    /// Returns the numeric value, clamped if needed.
    #[inline]
    pub fn get(&self) -> f32 {
        crate::values::normalize(
            self.calc_clamping_mode
                .map_or(self.value, |mode| mode.clamp(self.value)),
        )
        .min(f32::MAX)
        .max(f32::MIN)
    }

    /// Return the unit, as a string.
    pub fn unit(&self) -> &'static str {
        "number"
    }

    /// Return no canonical unit (number values do not have one).
    pub fn canonical_unit(&self) -> Option<&'static str> {
        None
    }

    /// Convert only if the unit is the same (conversion to other units does
    /// not make sense).
    pub fn to(&self, unit: &str) -> Result<Self, ()> {
        if !unit.eq_ignore_ascii_case("number") {
            return Err(());
        }
        Ok(Self {
            value: self.value,
            calc_clamping_mode: self.calc_clamping_mode,
        })
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
        Number {
            value: self.value.min(1.),
            calc_clamping_mode: self.calc_clamping_mode,
        }
    }
}

impl ToComputedValue for Number {
    type ComputedValue = CSSFloat;

    #[inline]
    fn to_computed_value(&self, _: &Context) -> CSSFloat {
        self.get()
    }

    #[inline]
    fn from_computed_value(computed: &CSSFloat) -> Self {
        Number {
            value: *computed,
            calc_clamping_mode: None,
        }
    }
}

impl ToCss for Number {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_number(self.value, self.calc_clamping_mode.is_some(), dest)
    }
}

impl ToTyped for Number {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        reify_number(self.value, self.calc_clamping_mode.is_some(), dest)
    }
}

impl IsParallelTo for (Number, Number, Number) {
    fn is_parallel_to(&self, vector: &DirectionVector) -> bool {
        use euclid::approxeq::ApproxEq;
        // If a and b is parallel, the angle between them is 0deg, so
        // a x b = |a|*|b|*sin(0)*n = 0 * n, |a x b| == 0.
        let self_vector = DirectionVector::new(self.0.get(), self.1.get(), self.2.get());
        self_vector
            .cross(*vector)
            .square_length()
            .approx_eq(&0.0f32)
    }
}

impl SpecifiedValueInfo for Number {}

impl Add for Number {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Self::new(self.get() + other.get())
    }
}

impl Zero for Number {
    #[inline]
    fn zero() -> Self {
        Self::new(0.)
    }

    #[inline]
    fn is_zero(&self) -> bool {
        self.get() == 0.
    }
}

impl From<Number> for f32 {
    #[inline]
    fn from(n: Number) -> Self {
        n.get()
    }
}

impl From<Number> for f64 {
    #[inline]
    fn from(n: Number) -> Self {
        n.get() as f64
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

    #[inline]
    fn is_one(&self) -> bool {
        self.get() == 1.0
    }
}

impl NonNegativeNumber {
    /// Returns a new non-negative number with the value `val`.
    pub fn new(val: CSSFloat) -> Self {
        NonNegative::<Number>(Number::new(val.max(0.)))
    }

    /// Returns the numeric value.
    #[inline]
    pub fn get(&self) -> f32 {
        self.0.get()
    }
}

/// An Integer which is >= 0.
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

/// A specified `<integer>`, either a simple integer value or a calc expression.
/// Note that a calc expression may not actually be an integer; it will be rounded
/// at computed-value time.
///
/// <https://drafts.csswg.org/css-values/#integers>
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq, PartialOrd, ToShmem, ToTyped)]
#[typed(todo_derive_fields)]
pub enum Integer {
    /// A literal integer value.
    Literal(CSSInteger),
    /// A calc expression, whose value will be rounded later if necessary.
    Calc(CSSFloat),
}

impl Zero for Integer {
    #[inline]
    fn zero() -> Self {
        Self::new(0)
    }

    #[inline]
    fn is_zero(&self) -> bool {
        *self == 0
    }
}

impl One for Integer {
    #[inline]
    fn one() -> Self {
        Self::new(1)
    }

    #[inline]
    fn is_one(&self) -> bool {
        *self == 1
    }
}

impl PartialEq<i32> for Integer {
    fn eq(&self, value: &i32) -> bool {
        self.value() == *value
    }
}

impl Integer {
    /// Trivially constructs a new `Integer` value.
    pub fn new(val: CSSInteger) -> Self {
        Self::Literal(val)
    }

    /// Returns the (rounded) integer value associated with this value.
    pub fn value(&self) -> CSSInteger {
        match *self {
            Self::Literal(i) => i,
            Self::Calc(n) => (n + 0.5).floor() as CSSInteger,
        }
    }

    /// Trivially constructs a new integer value from a `calc()` expression.
    fn from_calc(val: CSSFloat) -> Self {
        Self::Calc(val)
    }
}

impl Parse for Integer {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        match *input.next()? {
            Token::Number {
                int_value: Some(v), ..
            } => Ok(Integer::new(v)),
            Token::Function(ref name) => {
                let function = CalcNode::math_function(context, name, location)?;
                let result = CalcNode::parse_number(context, input, function)?;
                Ok(Integer::from_calc(result))
            },
            ref t => Err(location.new_unexpected_token_error(t.clone())),
        }
    }
}

impl Integer {
    /// Parse an integer value which is at least `min`.
    pub fn parse_with_minimum<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        min: i32,
    ) -> Result<Integer, ParseError<'i>> {
        let value = Integer::parse(context, input)?;
        // FIXME(emilio): The spec asks us to avoid rejecting it at parse
        // time except until computed value time.
        //
        // It's not totally clear it's worth it though, and no other browser
        // does this.
        if value.value() < min {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(value)
    }

    /// Parse a non-negative integer.
    pub fn parse_non_negative<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Integer, ParseError<'i>> {
        Integer::parse_with_minimum(context, input, 0)
    }

    /// Parse a positive integer (>= 1).
    pub fn parse_positive<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Integer, ParseError<'i>> {
        Integer::parse_with_minimum(context, input, 1)
    }
}

impl ToComputedValue for Integer {
    type ComputedValue = i32;

    #[inline]
    fn to_computed_value(&self, _: &Context) -> i32 {
        self.value()
    }

    #[inline]
    fn from_computed_value(computed: &i32) -> Self {
        Integer::new(*computed)
    }
}

impl ToCss for Integer {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            Integer::Literal(i) => i.to_css(dest),
            Integer::Calc(n) => {
                dest.write_str("calc(")?;
                n.to_css(dest)?;
                dest.write_char(')')
            },
        }
    }
}

impl SpecifiedValueInfo for Integer {}

/// A wrapper of Integer, with value >= 1.
pub type PositiveInteger = GreaterThanOrEqualToOne<Integer>;

impl Parse for PositiveInteger {
    #[inline]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Integer::parse_positive(context, input).map(GreaterThanOrEqualToOne)
    }
}
