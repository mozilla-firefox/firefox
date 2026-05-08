/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified time values.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::time::Time as ComputedTime;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::specified::calc::{CalcNode, CalcNumeric, Leaf};
use crate::values::CSSFloat;
use crate::Zero;
use cssparser::{match_ignore_ascii_case, Parser, Token};
use std::fmt::{self, Write};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{
    CssString, CssWriter, NumericValue, ParseError, SpecifiedValueInfo, StyleParseErrorKind, ToCss,
    ToTyped, TypedValue, UnitValue,
};
use thin_vec::ThinVec;

/// A time value according to CSS-VALUES § 6.2.
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq, ToShmem)]
#[repr(C)]
pub struct NoCalcTime {
    seconds: CSSFloat,
    unit: TimeUnit,
}

/// A time unit.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
#[repr(u8)]
pub enum TimeUnit {
    /// `s`
    Second,
    /// `ms`
    Millisecond,
}

impl NoCalcTime {
    /// Returns a time value that represents `seconds` seconds.
    pub fn from_seconds(seconds: CSSFloat) -> Self {
        Self {
            seconds,
            unit: TimeUnit::Second,
        }
    }

    /// Returns the time in fractional seconds.
    pub fn seconds(&self) -> CSSFloat {
        self.seconds
    }

    /// Returns the unit of the time.
    #[inline]
    pub fn unit(&self) -> &'static str {
        match self.unit {
            TimeUnit::Second => "s",
            TimeUnit::Millisecond => "ms",
        }
    }

    /// Return the unitless, raw value.
    #[inline]
    pub fn unitless_value(&self) -> CSSFloat {
        match self.unit {
            TimeUnit::Second => self.seconds,
            TimeUnit::Millisecond => self.seconds * 1000.,
        }
    }

    /// Return the canonical unit for this value.
    pub fn canonical_unit(&self) -> Option<&'static str> {
        Some("s")
    }

    /// Convert this value to the specified unit, if possible.
    pub fn to(&self, unit: &str) -> Result<Self, ()> {
        let unit = match_ignore_ascii_case! { unit,
            "s" => TimeUnit::Second,
            "ms" => TimeUnit::Millisecond,
             _ => return Err(()),
        };

        Ok(Self {
            seconds: self.seconds,
            unit,
        })
    }

    /// Parses a time according to CSS-VALUES § 6.2.
    pub fn parse_dimension(value: CSSFloat, unit: &str) -> Result<Self, ()> {
        let (seconds, unit) = match_ignore_ascii_case! { unit,
            "s" => (value, TimeUnit::Second),
            "ms" => (value / 1000.0, TimeUnit::Millisecond),
            _ => return Err(())
        };

        Ok(Self { seconds, unit })
    }
}

impl ToCss for NoCalcTime {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        crate::values::serialize_specified_dimension(
            self.unitless_value(),
            self.unit(),
            /* was_calc = */ false,
            dest,
        )
    }
}

impl ToComputedValue for NoCalcTime {
    type ComputedValue = ComputedTime;

    fn to_computed_value(&self, _: &Context) -> Self::ComputedValue {
        ComputedTime::from_seconds(self.seconds())
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self::from_seconds(computed.seconds())
    }
}

impl ToTyped for NoCalcTime {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        let numeric_value = NumericValue::Unit(UnitValue {
            value: self.unitless_value(),
            unit: CssString::from(self.unit()),
        });

        // https://drafts.css-houdini.org/css-typed-om-1/#reify-a-math-expression
        dest.push(TypedValue::Numeric(numeric_value));

        Ok(())
    }
}

impl SpecifiedValueInfo for NoCalcTime {}

/// A specified time value, either a plain value or a `calc()` expression.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
pub enum Time {
    /// A plain <time> value.
    NoCalc(NoCalcTime),
    /// A calc() expression that produces a <time>.
    Calc(Box<CalcNumeric>),
}

impl Time {
    /// Returns a time value that represents `seconds` seconds.
    pub fn from_seconds(seconds: CSSFloat) -> Self {
        Time::NoCalc(NoCalcTime::from_seconds(seconds))
    }

    fn parse_with_clamping_mode<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        clamping_mode: AllowedNumericType,
    ) -> Result<Self, ParseError<'i>> {
        use style_traits::ParsingMode;

        let location = input.current_source_location();
        match *input.next()? {
            // Note that we generally pass ParserContext to is_ok() to check
            // that the ParserMode of the ParserContext allows all numeric
            // values for SMIL regardless of clamping_mode, but in this Time
            // value case, the value does not animate for SMIL at all, so we use
            // ParsingMode::DEFAULT directly.
            Token::Dimension {
                value, ref unit, ..
            } if clamping_mode.is_ok(ParsingMode::DEFAULT, value) => {
                NoCalcTime::parse_dimension(value, unit)
                    .map_err(|()| location.new_custom_error(StyleParseErrorKind::UnspecifiedError))
                    .map(Time::NoCalc)
            },
            Token::Function(ref name) => {
                let function = CalcNode::math_function(context, name, location)?;
                CalcNode::parse_time(context, input, clamping_mode, function)
                    .map(Box::new)
                    .map(Time::Calc)
            },
            ref t => return Err(location.new_unexpected_token_error(t.clone())),
        }
    }

    /// Parses a non-negative time value.
    pub fn parse_non_negative<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
    }
}

impl Zero for Time {
    #[inline]
    fn zero() -> Self {
        Self::from_seconds(0.0)
    }

    #[inline]
    fn is_zero(&self) -> bool {
        // The unit doesn't matter, i.e. `s` and `ms` are the same for zero.
        matches!(self, Self::NoCalc(t) if t.seconds() == 0.0)
    }
}

impl ToComputedValue for Time {
    type ComputedValue = ComputedTime;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self {
            Time::NoCalc(t) => t.to_computed_value(context),
            Time::Calc(ref calc) => {
                let value = match calc.node.with_computed_context(context).resolve() {
                    Ok(Leaf::Time(t)) => t.seconds(),
                    _ => {
                        debug_assert!(false, "Unexpected Time::Calc without resolved time");
                        f32::NAN
                    },
                };
                ComputedTime::from_seconds(
                    crate::values::normalize(calc.clamping_mode.clamp(value))
                        .min(f32::MAX)
                        .max(f32::MIN),
                )
            },
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Time::NoCalc(NoCalcTime::from_seconds(computed.seconds()))
    }
}

impl Parse for Time {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::All)
    }
}
