/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Typed OM Numeric Values.

use crate::derives::*;
use crate::values::specified::{NoCalcLength, NoCalcNumber, Percentage, Time};
use crate::values::CSSFloat;
use cssparser::match_ignore_ascii_case;
use style_traits::ParsingMode;

/// A numeric value without a `calc` expression.
#[derive(Clone, ToTyped)]
#[repr(u8)]
pub enum NoCalcNumeric {
    /// A `<length>` value.
    ///
    /// <https://drafts.csswg.org/css-values/#lengths>
    Length(NoCalcLength),

    /// A `<time>` value.
    ///
    /// <https://drafts.csswg.org/css-values/#time>
    Time(Time),

    /// A `<number>` value.
    ///
    /// <https://drafts.csswg.org/css-values/#number-value>
    Number(NoCalcNumber),

    /// A `<percentage>` value.
    ///
    /// <https://drafts.csswg.org/css-values/#percentages>
    Percentage(Percentage),
    // TODO: Add other values.
}

impl NoCalcNumeric {
    /// Return the unitless, raw value.
    pub fn unitless_value(&self) -> CSSFloat {
        match *self {
            Self::Length(v) => v.unitless_value(),
            Self::Time(v) => v.unitless_value(),
            Self::Number(v) => v.get(),
            Self::Percentage(v) => v.get(),
        }
    }

    /// Return the unit, as a string.
    ///
    /// TODO: Investigate returning SortKey or adding a new variant for
    /// returning the unit as SortKey. Tracked in
    /// <https://bugzilla.mozilla.org/show_bug.cgi?id=2015863>
    pub fn unit(&self) -> &'static str {
        match *self {
            Self::Length(v) => v.unit(),
            Self::Time(v) => v.unit(),
            Self::Number(v) => v.unit(),
            Self::Percentage(v) => v.unit(),
        }
    }

    /// Return the canonical unit for this value, if one exists.
    ///
    /// TODO: Investigate returning SortKey. Tracked in
    /// <https://bugzilla.mozilla.org/show_bug.cgi?id=2015863>
    pub fn canonical_unit(&self) -> Option<&'static str> {
        match *self {
            Self::Length(v) => v.canonical_unit(),
            Self::Time(v) => v.canonical_unit(),
            Self::Number(v) => v.canonical_unit(),
            Self::Percentage(v) => v.canonical_unit(),
        }
    }

    /// Convert this value to the specified unit, if possible.
    ///
    /// TODO: Investigate using SortKey. Tracked in
    /// <https://bugzilla.mozilla.org/show_bug.cgi?id=2015863>
    pub fn to(&self, unit: &str) -> Result<Self, ()> {
        match self {
            Self::Length(v) => Ok(Self::Length(v.to(unit)?)),
            Self::Time(v) => Ok(Self::Time(v.to(unit)?)),
            Self::Number(v) => Ok(Self::Number(v.to(unit)?)),
            Self::Percentage(v) => Ok(Self::Percentage(v.to(unit)?)),
        }
    }

    /// Parse a given unit value.
    pub fn parse_unit_value(value: CSSFloat, unit: &str) -> Result<Self, ()> {
        if let Ok(length) = NoCalcLength::parse_dimension_with_flags(
            ParsingMode::DEFAULT,
            /* in_page_rule = */ false,
            value,
            unit,
        ) {
            return Ok(NoCalcNumeric::Length(length));
        }

        if let Ok(time) = Time::parse_dimension(value, unit) {
            return Ok(NoCalcNumeric::Time(time));
        }

        match_ignore_ascii_case! { unit,
            "number" => Ok(NoCalcNumeric::Number(NoCalcNumber::new(value))),
            "percent" => Ok(NoCalcNumeric::Percentage(Percentage::new(value))),
            _ => Err(()),
        }

        // TODO: Add support for other values.
    }
}
