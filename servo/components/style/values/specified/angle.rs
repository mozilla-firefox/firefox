/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified angles.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::angle::Angle as ComputedAngle;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::specified::calc::{CalcNode, CalcNumeric, Leaf};
use crate::values::CSSFloat;
use crate::Zero;
use cssparser::{match_ignore_ascii_case, Parser, Token};
use std::f32::consts::PI;
use std::fmt::{self, Write};
use std::ops::Neg;
use style_traits::{CssWriter, ParseError, SpecifiedValueInfo, ToCss};

/// A non-calc `<angle>` value.
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq, PartialOrd, ToShmem)]
#[repr(C)]
pub enum NoCalcAngle {
    /// An angle with degree unit.
    Deg(CSSFloat),
    /// An angle with gradian unit.
    Grad(CSSFloat),
    /// An angle with radian unit.
    Rad(CSSFloat),
    /// An angle with turn unit.
    Turn(CSSFloat),
}

impl Zero for NoCalcAngle {
    fn zero() -> Self {
        Self::from_degrees(0.)
    }

    fn is_zero(&self) -> bool {
        self.unitless_value() == 0.0
    }
}

impl ToCss for NoCalcAngle {
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

impl SpecifiedValueInfo for NoCalcAngle {}

impl NoCalcAngle {
    /// Creates an angle with the given value in degrees.
    #[inline]
    pub fn from_degrees(value: CSSFloat) -> Self {
        Self::Deg(value)
    }

    /// Creates an angle with the given value in radians.
    #[inline]
    pub fn from_radians(value: CSSFloat) -> Self {
        Self::Rad(value)
    }

    /// Return `0deg`.
    pub fn zero() -> Self {
        Self::from_degrees(0.0)
    }

    /// Returns the value of the angle in degrees.
    #[inline]
    pub fn degrees(&self) -> CSSFloat {
        const DEG_PER_RAD: f32 = 180.0 / PI;
        const DEG_PER_TURN: f32 = 360.0;
        const DEG_PER_GRAD: f32 = 180.0 / 200.0;

        match *self {
            Self::Deg(d) => d,
            Self::Rad(rad) => rad * DEG_PER_RAD,
            Self::Turn(turns) => turns * DEG_PER_TURN,
            Self::Grad(gradians) => gradians * DEG_PER_GRAD,
        }
    }

    /// Returns the value of the angle in radians.
    #[inline]
    pub fn radians(&self) -> CSSFloat {
        const RAD_PER_DEG: f32 = PI / 180.0;
        self.degrees() * RAD_PER_DEG
    }

    /// Returns the unit of the angle.
    #[inline]
    pub fn unit(&self) -> &'static str {
        match *self {
            Self::Deg(_) => "deg",
            Self::Rad(_) => "rad",
            Self::Turn(_) => "turn",
            Self::Grad(_) => "grad",
        }
    }

    /// Returns the unitless, raw value
    #[inline]
    pub fn unitless_value(&self) -> CSSFloat {
        match *self {
            Self::Deg(v) | Self::Rad(v) | Self::Turn(v) | Self::Grad(v) => v,
        }
    }

    /// Parse an `<angle>` value given a value and a unit.
    pub fn parse_dimension(value: CSSFloat, unit: &str) -> Result<Self, ()> {
        Ok(match_ignore_ascii_case! { unit,
            "deg" => Self::Deg(value),
            "grad" => Self::Grad(value),
            "turn" => Self::Turn(value),
            "rad" => Self::Rad(value),
             _ => return Err(())
        })
    }
}

impl Neg for NoCalcAngle {
    type Output = NoCalcAngle;

    #[inline]
    fn neg(self) -> NoCalcAngle {
        match self {
            Self::Deg(v) => Self::Deg(-v),
            Self::Rad(v) => Self::Rad(-v),
            Self::Turn(v) => Self::Turn(-v),
            Self::Grad(v) => Self::Grad(-v),
        }
    }
}

/// A specified <angle> value, either a plain value or a `calc()` expression.
///
/// https://drafts.csswg.org/css-values/#angle-value
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
pub enum Angle {
    /// A plain <angle> value.
    NoCalc(NoCalcAngle),
    /// A calc() expression that produces an <angle>.
    Calc(Box<CalcNumeric>),
}

/// Whether to allow parsing an unitless zero as a valid angle.
///
/// This should always be `No`, except for exceptions like:
///
///   https://github.com/w3c/fxtf-drafts/issues/228
///
/// See also: https://github.com/w3c/csswg-drafts/issues/1162.
#[allow(missing_docs)]
pub enum AllowUnitlessZeroAngle {
    Yes,
    No,
}

impl Parse for Angle {
    /// Parses an angle according to CSS-VALUES § 6.1.
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(context, input, AllowUnitlessZeroAngle::No)
    }
}

impl Zero for Angle {
    fn zero() -> Self {
        Angle::NoCalc(NoCalcAngle::zero())
    }

    fn is_zero(&self) -> bool {
        match self {
            Angle::NoCalc(a) => a.is_zero(),
            Angle::Calc(_) => false,
        }
    }
}

impl ToComputedValue for Angle {
    type ComputedValue = ComputedAngle;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        let degrees = match self {
            Angle::NoCalc(a) => a.degrees(),
            Angle::Calc(ref calc) => {
                calc.clamping_mode
                    .clamp(match calc.node.with_computed_context(context).resolve() {
                        Ok(Leaf::Angle(a)) => a.degrees(),
                        _ => {
                            debug_assert!(false, "Unexpected Angle::Calc without resolved angle");
                            f32::NAN
                        },
                    })
            },
        };

        // NaN and +-infinity should degenerate to 0: https://github.com/w3c/csswg-drafts/issues/6105
        ComputedAngle::from_degrees(if degrees.is_finite() { degrees } else { 0.0 })
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Angle::NoCalc(NoCalcAngle::from_degrees(computed.degrees()))
    }
}

impl Angle {
    /// Creates an angle with the given value in degrees.
    #[inline]
    pub fn from_degrees(value: CSSFloat) -> Self {
        Angle::NoCalc(NoCalcAngle::from_degrees(value))
    }

    /// Return `0deg`.
    pub fn zero() -> Self {
        Angle::NoCalc(NoCalcAngle::zero())
    }

    /// Returns the angle in degrees if it can be resolved at parse time, or None for calc
    /// expressions that require computed context. Prefer `to_computed_value(context).degrees()`
    /// when an element context is available.
    #[inline]
    pub fn degrees(&self) -> Option<CSSFloat> {
        match self {
            Angle::NoCalc(a) => Some(a.degrees()),
            Angle::Calc(ref calc) => calc
                .as_angle()
                .map(|a| calc.clamping_mode.clamp(a.degrees())),
        }
    }

    /// Parse an `<angle>` allowing unitless zero to represent a zero angle.
    ///
    /// See the comment in `AllowUnitlessZeroAngle` for why.
    #[inline]
    pub fn parse_with_unitless<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(context, input, AllowUnitlessZeroAngle::Yes)
    }

    pub(super) fn parse_internal<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_unitless_zero: AllowUnitlessZeroAngle,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let t = input.next()?;
        let allow_unitless_zero = matches!(allow_unitless_zero, AllowUnitlessZeroAngle::Yes);
        match *t {
            Token::Dimension {
                value, ref unit, ..
            } => match NoCalcAngle::parse_dimension(value, unit) {
                Ok(angle) => Ok(Angle::NoCalc(angle)),
                Err(()) => {
                    let t = t.clone();
                    Err(input.new_unexpected_token_error(t))
                },
            },
            Token::Function(ref name) => {
                let function = CalcNode::math_function(context, name, location)?;
                CalcNode::parse_angle(context, input, function)
                    .map(Box::new)
                    .map(Angle::Calc)
            },
            Token::Number { value, .. } if value == 0. && allow_unitless_zero => Ok(Angle::zero()),
            ref t => {
                let t = t.clone();
                Err(input.new_unexpected_token_error(t))
            },
        }
    }
}

impl Neg for Angle {
    type Output = Angle;

    #[inline]
    fn neg(self) -> Angle {
        match self {
            Angle::NoCalc(a) => Angle::NoCalc(-a),
            Angle::Calc(c) => {
                let mut node = c.node;
                node.negate();
                Angle::Calc(Box::new(CalcNumeric {
                    clamping_mode: c.clamping_mode,
                    node,
                }))
            },
        }
    }
}
