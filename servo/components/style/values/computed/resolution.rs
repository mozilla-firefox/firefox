/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Resolution values:
//!
//! https://drafts.csswg.org/css-values/#resolution

use crate::derives::*;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::specified;
use crate::values::specified::calc::Leaf;
use crate::values::CSSFloat;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ToCss};

/// A computed `<resolution>`.
#[repr(C)]
#[derive(Animate, Clone, Debug, MallocSizeOf, PartialEq, ToResolvedValue, ToShmem)]
pub struct Resolution(CSSFloat);

impl Resolution {
    /// Returns this resolution value as dppx.
    #[inline]
    pub fn dppx(&self) -> CSSFloat {
        self.0
    }

    /// Return a computed `resolution` value from a dppx float value.
    #[inline]
    pub fn from_dppx(dppx: CSSFloat) -> Self {
        Resolution(dppx)
    }
}

impl ToComputedValue for specified::Resolution {
    type ComputedValue = Resolution;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        Resolution(crate::values::normalize(match self {
            specified::Resolution::NoCalc(r) => r.dppx().max(0.0),
            specified::Resolution::Calc(ref calc) => {
                calc.clamping_mode
                    .clamp(match calc.node.with_computed_context(context).resolve() {
                        Ok(Leaf::Resolution(r)) => r.dppx().max(0.0),
                        __ => {
                            debug_assert!(
                                false,
                                "Unexpected Resolution::Calc without resolved resolution"
                            );
                            0.0
                        },
                    })
            },
        }))
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        specified::Resolution::from_dppx(computed.dppx())
    }
}

impl ToCss for Resolution {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        self.dppx().to_css(dest)?;
        dest.write_str("dppx")
    }
}
