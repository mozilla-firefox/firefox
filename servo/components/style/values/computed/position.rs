/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the computed value of
//! [`position`][position] values.
//!
//! [position]: https://drafts.csswg.org/css-backgrounds-3/#position

use crate::logical_geometry::PhysicalSide;
use crate::values::computed::{
    Context, Integer, LengthPercentage, NonNegativeNumber, Percentage, ToComputedValue,
};
use crate::values::generics;
#[cfg(feature = "gecko")]
use crate::values::generics::position::TreeScoped;
use crate::values::generics::position::{
    AnchorSideKeyword, AspectRatio as GenericAspectRatio, GenericAnchorFunction, GenericAnchorSide,
    GenericInset, Position as GenericPosition, PositionComponent as GenericPositionComponent,
    PositionOrAuto as GenericPositionOrAuto, ZIndex as GenericZIndex,
};
pub use crate::values::specified::position::{
    AnchorName, DashedIdentAndOrTryTactic, FlexWrap, GridAutoFlow, GridTemplateAreas,
    MasonryAutoFlow, PositionAnchor, PositionArea, PositionAreaAxis, PositionAreaKeyword,
    PositionAreaType, PositionTryFallbacks, PositionTryFallbacksTryTactic,
    PositionTryFallbacksTryTacticKeyword, PositionTryOrder, PositionVisibility, ScopedName,
};
use crate::Zero;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ToCss};

/// The computed value of a CSS `<position>`
pub type Position = GenericPosition<HorizontalPosition, VerticalPosition>;

/// The computed value of an `auto | <position>`
pub type PositionOrAuto = GenericPositionOrAuto<Position>;

/// The computed value of a CSS horizontal position.
pub type HorizontalPosition = LengthPercentage;

/// The computed value of a CSS vertical position.
pub type VerticalPosition = LengthPercentage;

/// The computed value of anchor side.
pub type AnchorSide = GenericAnchorSide<Percentage>;

impl AnchorSide {
    /// Break down given anchor side into its equivalent keyword and percentage.
    pub fn keyword_and_percentage(&self) -> (AnchorSideKeyword, Percentage) {
        match self {
            Self::Percentage(p) => (AnchorSideKeyword::Start, *p),
            Self::Keyword(k) => {
                if matches!(k, AnchorSideKeyword::Center) {
                    (AnchorSideKeyword::Start, Percentage(0.5))
                } else {
                    (*k, Percentage::zero())
                }
            },
        }
    }
}

/// The computed value of an `anchor()` function.
pub type AnchorFunction = GenericAnchorFunction<Percentage, Inset>;

#[cfg(feature = "gecko")]
use crate::{
    gecko_bindings::structs::AnchorPosOffsetResolutionParams,
    values::{computed::Length, DashedIdent},
};

impl AnchorFunction {
    /// Resolve the anchor function with the given resolver. Returns `Err()` if no anchor is found.
    #[cfg(feature = "gecko")]
    pub fn resolve(
        anchor_name: &TreeScoped<DashedIdent>,
        anchor_side: &AnchorSide,
        prop_side: PhysicalSide,
        params: &AnchorPosOffsetResolutionParams,
    ) -> Result<Length, ()> {
        use crate::gecko_bindings::structs::Gecko_GetAnchorPosOffset;

        let (keyword, percentage) = anchor_side.keyword_and_percentage();
        let mut offset = Length::zero();
        let valid = unsafe {
            Gecko_GetAnchorPosOffset(
                params,
                anchor_name.value.0.as_ptr(),
                &anchor_name.scope,
                prop_side as u8,
                keyword as u8,
                percentage.0,
                &mut offset,
            )
        };

        if !valid {
            return Err(());
        }

        Ok(offset)
    }
}

/// Perform the adjustment of a given value for a given try tactic, as per:
/// https://drafts.csswg.org/css-anchor-position-1/#swap-due-to-a-try-tactic
pub(crate) trait TryTacticAdjustment {
    /// Performs the adjustments necessary given an old side we're relative to, and a new side
    /// we're relative to.
    fn try_tactic_adjustment(&mut self, old_side: PhysicalSide, new_side: PhysicalSide);
}

impl<T: TryTacticAdjustment> TryTacticAdjustment for Box<T> {
    fn try_tactic_adjustment(&mut self, old_side: PhysicalSide, new_side: PhysicalSide) {
        (**self).try_tactic_adjustment(old_side, new_side);
    }
}

impl<T: TryTacticAdjustment> TryTacticAdjustment for generics::NonNegative<T> {
    fn try_tactic_adjustment(&mut self, old_side: PhysicalSide, new_side: PhysicalSide) {
        self.0.try_tactic_adjustment(old_side, new_side);
    }
}

impl<Percentage: TryTacticAdjustment> TryTacticAdjustment for GenericAnchorSide<Percentage> {
    fn try_tactic_adjustment(&mut self, old_side: PhysicalSide, new_side: PhysicalSide) {
        match self {
            Self::Percentage(p) => p.try_tactic_adjustment(old_side, new_side),
            Self::Keyword(side) => side.try_tactic_adjustment(old_side, new_side),
        }
    }
}

impl<Percentage: TryTacticAdjustment, Fallback: TryTacticAdjustment> TryTacticAdjustment
    for GenericAnchorFunction<Percentage, Fallback>
{
    fn try_tactic_adjustment(&mut self, old_side: PhysicalSide, new_side: PhysicalSide) {
        self.side.try_tactic_adjustment(old_side, new_side);
        if let Some(fallback) = self.fallback.as_mut() {
            fallback.try_tactic_adjustment(old_side, new_side);
        }
    }
}

/// A computed type for `inset` properties.
pub type Inset = GenericInset<Percentage, LengthPercentage>;
impl TryTacticAdjustment for Inset {
    // https://drafts.csswg.org/css-anchor-position-1/#swap-due-to-a-try-tactic:
    //
    //     For inset properties, change the specified side in anchor() functions to maintain the
    //     same relative relationship to the new direction that they had to the old.
    //
    //     If a <percentage> is used, and directions are opposing, change it to 100% minus the
    //     original percentage.
    fn try_tactic_adjustment(&mut self, old_side: PhysicalSide, new_side: PhysicalSide) {
        match self {
            Self::Auto => {},
            Self::AnchorContainingCalcFunction(lp) | Self::LengthPercentage(lp) => {
                lp.try_tactic_adjustment(old_side, new_side)
            },
            Self::AnchorFunction(anchor) => anchor.try_tactic_adjustment(old_side, new_side),
            Self::AnchorSizeFunction(anchor) => anchor.try_tactic_adjustment(old_side, new_side),
        }
    }
}

impl Position {
    /// `50% 50%`
    #[inline]
    pub fn center() -> Self {
        Self::new(
            LengthPercentage::new_percent(Percentage(0.5)),
            LengthPercentage::new_percent(Percentage(0.5)),
        )
    }

    /// `0% 0%`
    #[inline]
    pub fn zero() -> Self {
        Self::new(LengthPercentage::zero(), LengthPercentage::zero())
    }
}

impl ToCss for Position {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.horizontal.to_css(dest)?;
        dest.write_char(' ')?;
        self.vertical.to_css(dest)
    }
}

impl GenericPositionComponent for LengthPercentage {
    fn is_center(&self) -> bool {
        match self.to_percentage() {
            Some(Percentage(per)) => per == 0.5,
            _ => false,
        }
    }
}

#[inline]
fn block_or_inline_to_inferred(keyword: PositionAreaKeyword) -> PositionAreaKeyword {
    if matches!(
        keyword.axis(),
        PositionAreaAxis::Block | PositionAreaAxis::Inline
    ) {
        keyword.with_axis(PositionAreaAxis::Inferred)
    } else {
        keyword
    }
}

#[inline]
fn inferred_to_block(keyword: PositionAreaKeyword) -> PositionAreaKeyword {
    keyword.with_inferred_axis(PositionAreaAxis::Block)
}

#[inline]
fn inferred_to_inline(keyword: PositionAreaKeyword) -> PositionAreaKeyword {
    keyword.with_inferred_axis(PositionAreaAxis::Inline)
}

// This exists because the spec currently says that further simplifications
// should be made to the computed value. That's confusing though, and probably
// all these simplifications should be wrapped up into the simplifications that
// we make to the specified value. I.e. all this should happen in
// PositionArea::parse_internal().
// See also https://github.com/w3c/csswg-drafts/issues/12759
impl ToComputedValue for PositionArea {
    type ComputedValue = Self;

    fn to_computed_value(&self, _context: &Context) -> Self {
        let mut computed = self.clone();
        let pair_type = self.get_type();
        if pair_type == PositionAreaType::Logical || pair_type == PositionAreaType::SelfLogical {
            if computed.second != PositionAreaKeyword::None {
                computed.first = block_or_inline_to_inferred(computed.first);
                computed.second = block_or_inline_to_inferred(computed.second);
            }
        } else if pair_type == PositionAreaType::Inferred
            || pair_type == PositionAreaType::SelfInferred
        {
            if computed.second == PositionAreaKeyword::SpanAll {
                // We remove the superfluous span-all, converting the inferred
                // keyword to a logical keyword to avoid ambiguity, per spec.
                computed.first = inferred_to_block(computed.first);
                computed.second = PositionAreaKeyword::None;
            } else if computed.first == PositionAreaKeyword::SpanAll {
                computed.first = computed.second;
                computed.first = inferred_to_inline(computed.first);
                computed.second = PositionAreaKeyword::None;
            }
        }

        if computed.first == computed.second {
            computed.second = PositionAreaKeyword::None;
        }
        computed
    }

    fn from_computed_value(computed: &Self) -> Self {
        computed.clone()
    }
}

/// A computed value for the `z-index` property.
pub type ZIndex = GenericZIndex<Integer>;

/// A computed value for the `aspect-ratio` property.
pub type AspectRatio = GenericAspectRatio<NonNegativeNumber>;
