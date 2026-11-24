/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The chrome-only `-moz-pref()` query feature.

use crate::derives::*;
use crate::values::{AtomString, computed};
use crate::{parser::Parse, parser::ParserContext};
use cssparser::Parser;
use selectors::kleene_value::KleeneValue;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, ToCss};

/// A boolean value for a pref query.
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    Eq,
    Parse,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToShmem,
)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum BoolValue {
    False,
    True,
}

/// Simple values we support for -moz-pref(). We don't want to deal with calc() and other
/// shenanigans for now.
#[derive(
    Clone,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToShmem,
)]
#[repr(u8)]
pub enum MozPrefFeatureValue<I> {
    /// No pref value, implicitly bool, but also used to represent missing prefs.
    #[css(skip)]
    None,
    /// A bool value.
    Boolean(BoolValue),
    /// An integer value, useful for int prefs.
    Integer(I),
    /// A string pref value.
    String(crate::values::AtomString),
}

type SpecifiedMozPrefFeatureValue = MozPrefFeatureValue<crate::values::specified::Integer>;
/// The computed -moz-pref() value.
pub type ComputedMozPrefFeatureValue = MozPrefFeatureValue<crate::values::computed::Integer>;

/// A custom -moz-pref(<name>, <value>) query feature. Note that this doesn't include
/// the outer `-moz-pref()` function token in its parsing and serialization.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct MozPrefFeature {
    name: crate::values::AtomString,
    value: SpecifiedMozPrefFeatureValue,
}

impl Parse for MozPrefFeature {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let name = AtomString::parse(context, input)?;
        let value = if input.try_parse(|i| i.expect_comma()).is_ok() {
            SpecifiedMozPrefFeatureValue::parse(context, input)?
        } else {
            SpecifiedMozPrefFeatureValue::None
        };
        Ok(Self { name, value })
    }
}

impl MozPrefFeature {
    /// Evaluate whether the pref feature query matches.
    #[cfg(feature = "gecko")]
    pub fn matches(&self, ctx: &computed::Context) -> KleeneValue {
        use crate::values::computed::ToComputedValue;
        let value = self.value.to_computed_value(ctx);
        KleeneValue::from(unsafe {
            crate::gecko_bindings::bindings::Gecko_EvalMozPrefFeature(self.name.as_ptr(), &value)
        })
    }

    /// Evaluate whether the pref feature query matches.
    #[cfg(feature = "servo")]
    pub fn matches(&self, _: &computed::Context) -> KleeneValue {
        KleeneValue::Unknown
    }
}

impl ToCss for MozPrefFeature {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        self.name.to_css(dest)?;
        if !matches!(self.value, MozPrefFeatureValue::None) {
            dest.write_str(", ")?;
            self.value.to_css(dest)?;
        }
        Ok(())
    }
}
