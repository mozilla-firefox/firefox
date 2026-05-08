/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the specified value of
//! [`position`][position]s
//!
//! [position]: https://drafts.csswg.org/css-backgrounds-3/#position

use crate::derives::*;
use crate::logical_geometry::{LogicalAxis, LogicalSide, PhysicalSide, WritingMode};
use crate::parser::{Parse, ParserContext};
use crate::selector_map::PrecomputedHashMap;
use crate::str::HTML_SPACE_CHARACTERS;
use crate::values::computed::LengthPercentage as ComputedLengthPercentage;
use crate::values::computed::{Context, Percentage, ToComputedValue};
use crate::values::generics::length::GenericAnchorSizeFunction;
use crate::values::generics::position::PositionComponent as GenericPositionComponent;
use crate::values::generics::position::PositionOrAuto as GenericPositionOrAuto;
use crate::values::generics::position::ZIndex as GenericZIndex;
use crate::values::generics::position::{AspectRatio as GenericAspectRatio, GenericAnchorSide};
use crate::values::generics::position::{GenericAnchorFunction, GenericInset, TreeScoped};
use crate::values::generics::position::{IsTreeScoped, Position as GenericPosition};
use crate::values::specified;
use crate::values::specified::align::AlignFlags;
use crate::values::specified::percentage::NoCalcPercentage;
use crate::values::specified::{AllowQuirks, Integer, LengthPercentage, NonNegativeNumber};
use crate::values::{AtomIdent, DashedIdent};
use crate::Atom;
use cssparser::{match_ignore_ascii_case, Parser};
use num_traits::FromPrimitive;
use selectors::parser::SelectorParseErrorKind;
use servo_arc::Arc;
use smallvec::{smallvec, SmallVec};
use std::collections::hash_map::Entry;
use std::fmt::{self, Write};
use style_traits::arc_slice::ArcSlice;
use style_traits::values::specified::AllowedNumericType;
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};
use thin_vec::ThinVec;

/// The specified value of a CSS `<position>`
pub type Position = GenericPosition<HorizontalPosition, VerticalPosition>;

/// The specified value of an `auto | <position>`.
pub type PositionOrAuto = GenericPositionOrAuto<Position>;

/// The specified value of a horizontal position.
pub type HorizontalPosition = PositionComponent<HorizontalPositionKeyword>;

/// The specified value of a vertical position.
pub type VerticalPosition = PositionComponent<VerticalPositionKeyword>;

/// The specified value of a component of a CSS `<position>`.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
#[typed(todo_derive_fields)]
pub enum PositionComponent<S> {
    /// `center`
    Center,
    /// `<length-percentage>`
    Length(LengthPercentage),
    /// `<side> <length-percentage>?`
    Side(S, Option<LengthPercentage>),
}

/// A keyword for the X direction.
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[allow(missing_docs)]
#[repr(u8)]
pub enum HorizontalPositionKeyword {
    Left,
    Right,
}

/// A keyword for the Y direction.
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[allow(missing_docs)]
#[repr(u8)]
pub enum VerticalPositionKeyword {
    Top,
    Bottom,
}

impl Parse for Position {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let position = Self::parse_three_value_quirky(context, input, AllowQuirks::No)?;
        if position.is_three_value_syntax() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(position)
    }
}

impl Position {
    /// Parses a `<bg-position>`, with quirks.
    pub fn parse_three_value_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError<'i>> {
        match input.try_parse(|i| PositionComponent::parse_quirky(context, i, allow_quirks)) {
            Ok(x_pos @ PositionComponent::Center) => {
                if let Ok(y_pos) =
                    input.try_parse(|i| PositionComponent::parse_quirky(context, i, allow_quirks))
                {
                    return Ok(Self::new(x_pos, y_pos));
                }
                let x_pos = input
                    .try_parse(|i| PositionComponent::parse_quirky(context, i, allow_quirks))
                    .unwrap_or(x_pos);
                let y_pos = PositionComponent::Center;
                return Ok(Self::new(x_pos, y_pos));
            },
            Ok(PositionComponent::Side(x_keyword, lp)) => {
                if input
                    .try_parse(|i| i.expect_ident_matching("center"))
                    .is_ok()
                {
                    let x_pos = PositionComponent::Side(x_keyword, lp);
                    let y_pos = PositionComponent::Center;
                    return Ok(Self::new(x_pos, y_pos));
                }
                if let Ok(y_keyword) = input.try_parse(VerticalPositionKeyword::parse) {
                    let y_lp = input
                        .try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
                        .ok();
                    let x_pos = PositionComponent::Side(x_keyword, lp);
                    let y_pos = PositionComponent::Side(y_keyword, y_lp);
                    return Ok(Self::new(x_pos, y_pos));
                }
                let x_pos = PositionComponent::Side(x_keyword, None);
                let y_pos = lp.map_or(PositionComponent::Center, PositionComponent::Length);
                return Ok(Self::new(x_pos, y_pos));
            },
            Ok(x_pos @ PositionComponent::Length(_)) => {
                if let Ok(y_keyword) = input.try_parse(VerticalPositionKeyword::parse) {
                    let y_pos = PositionComponent::Side(y_keyword, None);
                    return Ok(Self::new(x_pos, y_pos));
                }
                if let Ok(y_lp) =
                    input.try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
                {
                    let y_pos = PositionComponent::Length(y_lp);
                    return Ok(Self::new(x_pos, y_pos));
                }
                let y_pos = PositionComponent::Center;
                let _ = input.try_parse(|i| i.expect_ident_matching("center"));
                return Ok(Self::new(x_pos, y_pos));
            },
            Err(_) => {},
        }
        let y_keyword = VerticalPositionKeyword::parse(input)?;
        let lp_and_x_pos: Result<_, ParseError> = input.try_parse(|i| {
            let y_lp = i
                .try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
                .ok();
            if let Ok(x_keyword) = i.try_parse(HorizontalPositionKeyword::parse) {
                let x_lp = i
                    .try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
                    .ok();
                let x_pos = PositionComponent::Side(x_keyword, x_lp);
                return Ok((y_lp, x_pos));
            };
            i.expect_ident_matching("center")?;
            let x_pos = PositionComponent::Center;
            Ok((y_lp, x_pos))
        });
        if let Ok((y_lp, x_pos)) = lp_and_x_pos {
            let y_pos = PositionComponent::Side(y_keyword, y_lp);
            return Ok(Self::new(x_pos, y_pos));
        }
        let x_pos = PositionComponent::Center;
        let y_pos = PositionComponent::Side(y_keyword, None);
        Ok(Self::new(x_pos, y_pos))
    }

    /// `center center`
    #[inline]
    pub fn center() -> Self {
        Self::new(PositionComponent::Center, PositionComponent::Center)
    }

    /// Returns true if this uses a 3 value syntax.
    #[inline]
    fn is_three_value_syntax(&self) -> bool {
        self.horizontal.component_count() != self.vertical.component_count()
    }
}

impl ToCss for Position {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match (&self.horizontal, &self.vertical) {
            (
                x_pos @ &PositionComponent::Side(_, Some(_)),
                &PositionComponent::Length(ref y_lp),
            ) => {
                x_pos.to_css(dest)?;
                dest.write_str(" top ")?;
                y_lp.to_css(dest)
            },
            (
                &PositionComponent::Length(ref x_lp),
                y_pos @ &PositionComponent::Side(_, Some(_)),
            ) => {
                dest.write_str("left ")?;
                x_lp.to_css(dest)?;
                dest.write_char(' ')?;
                y_pos.to_css(dest)
            },
            (x_pos, y_pos) => {
                x_pos.to_css(dest)?;
                dest.write_char(' ')?;
                y_pos.to_css(dest)
            },
        }
    }
}

impl<S: Parse> Parse for PositionComponent<S> {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_quirky(context, input, AllowQuirks::No)
    }
}

impl<S: Parse> PositionComponent<S> {
    /// Parses a component of a CSS position, with quirks.
    pub fn parse_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|i| i.expect_ident_matching("center"))
            .is_ok()
        {
            return Ok(PositionComponent::Center);
        }
        if let Ok(lp) =
            input.try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
        {
            return Ok(PositionComponent::Length(lp));
        }
        let keyword = S::parse(context, input)?;
        let lp = input
            .try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
            .ok();
        Ok(PositionComponent::Side(keyword, lp))
    }
}

impl<S> GenericPositionComponent for PositionComponent<S> {
    fn is_center(&self) -> bool {
        match *self {
            PositionComponent::Center => true,
            PositionComponent::Length(LengthPercentage::Percentage(ref per)) => per.get() == 0.5,
            // 50% from any side is still the center.
            PositionComponent::Side(_, Some(LengthPercentage::Percentage(ref per))) => {
                per.get() == 0.5
            },
            _ => false,
        }
    }
}

impl<S> PositionComponent<S> {
    /// `0%`
    pub fn zero() -> Self {
        PositionComponent::Length(LengthPercentage::Percentage(NoCalcPercentage::zero()))
    }

    /// Returns the count of this component.
    fn component_count(&self) -> usize {
        match *self {
            PositionComponent::Length(..) | PositionComponent::Center => 1,
            PositionComponent::Side(_, ref lp) => {
                if lp.is_some() {
                    2
                } else {
                    1
                }
            },
        }
    }
}

impl<S: Side> ToComputedValue for PositionComponent<S> {
    type ComputedValue = ComputedLengthPercentage;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            PositionComponent::Center => ComputedLengthPercentage::new_percent(Percentage(0.5)),
            PositionComponent::Side(ref keyword, None) => {
                let p = Percentage(if keyword.is_start() { 0. } else { 1. });
                ComputedLengthPercentage::new_percent(p)
            },
            PositionComponent::Side(ref keyword, Some(ref length)) if !keyword.is_start() => {
                let length = length.to_computed_value(context);
                // We represent `<end-side> <length>` as `calc(100% - <length>)`.
                ComputedLengthPercentage::hundred_percent_minus(length, AllowedNumericType::All)
            },
            PositionComponent::Side(_, Some(ref length))
            | PositionComponent::Length(ref length) => length.to_computed_value(context),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        PositionComponent::Length(ToComputedValue::from_computed_value(computed))
    }
}

impl<S: Side> PositionComponent<S> {
    /// The initial specified value of a position component, i.e. the start side.
    pub fn initial_specified_value() -> Self {
        PositionComponent::Side(S::start(), None)
    }
}

/// https://drafts.csswg.org/css-anchor-position-1/#propdef-anchor-name
#[derive(
    Animate,
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(comma)]
#[repr(transparent)]
#[typed(todo_derive_fields)]
pub struct AnchorNameIdent(
    #[css(iterable, if_empty = "none")]
    #[ignore_malloc_size_of = "Arc"]
    #[animation(constant)]
    pub crate::ArcSlice<DashedIdent>,
);

impl AnchorNameIdent {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self(Default::default())
    }
}

impl Parse for AnchorNameIdent {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let first = input.expect_ident()?;
        if first.eq_ignore_ascii_case("none") {
            return Ok(Self::none());
        }
        // The common case is probably just to have a single anchor name, so
        // space for four on the stack should be plenty.
        let mut idents: SmallVec<[DashedIdent; 4]> =
            smallvec![DashedIdent::from_ident(location, first,)?];
        while input.try_parse(|input| input.expect_comma()).is_ok() {
            idents.push(DashedIdent::parse(context, input)?);
        }
        Ok(AnchorNameIdent(ArcSlice::from_iter(idents.drain(..))))
    }
}

impl IsTreeScoped for AnchorNameIdent {
    fn is_tree_scoped(&self) -> bool {
        !self.0.is_empty()
    }
}

/// https://drafts.csswg.org/css-anchor-position-1/#propdef-anchor-name
pub type AnchorName = TreeScoped<AnchorNameIdent>;

impl AnchorName {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self::with_default_level(AnchorNameIdent::none())
    }
}

/// List of scoped names, or none.
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(transparent)]
#[css(comma)]
#[typed(todo_derive_fields)]
pub struct ScopedNameList(
    /// `none | all | <dashed-ident>#`
    #[css(iterable, if_empty = "none")]
    #[ignore_malloc_size_of = "Arc"]
    crate::ArcSlice<AtomIdent>,
);

impl ScopedNameList {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self(crate::ArcSlice::default())
    }

    /// Whether we're the `none` value.
    pub fn is_none(&self) -> bool {
        self.0.is_empty()
    }

    /// Return the `all` value.
    pub fn all() -> Self {
        static ALL: std::sync::LazyLock<ScopedNameList> = std::sync::LazyLock::new(|| {
            ScopedNameList(crate::ArcSlice::from_iter_leaked(std::iter::once(
                AtomIdent::new(atom!("all")),
            )))
        });
        ALL.clone()
    }
}

impl Parse for ScopedNameList {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let first = input.expect_ident()?;
        if first.eq_ignore_ascii_case("none") {
            return Ok(Self::none());
        }
        if first.eq_ignore_ascii_case("all") {
            return Ok(Self::all());
        }
        // Authors using more than a handful of anchored elements is likely
        // uncommon, so we only pre-allocate for 8 on the stack here.
        let mut idents = SmallVec::<[AtomIdent; 8]>::new();
        idents.push(AtomIdent::new(DashedIdent::from_ident(location, first)?.0));
        while input.try_parse(|input| input.expect_comma()).is_ok() {
            idents.push(AtomIdent::new(DashedIdent::parse(context, input)?.0));
        }
        Ok(Self(ArcSlice::from_iter(idents.drain(..))))
    }
}

impl IsTreeScoped for ScopedNameList {
    fn is_tree_scoped(&self) -> bool {
        !self.is_none()
    }
}

/// A scoped name type, such as:
/// * https://drafts.csswg.org/css-anchor-position-1/#propdef-scope
pub type ScopedName = TreeScoped<ScopedNameList>;

impl ScopedName {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self::with_default_level(ScopedNameList::none())
    }

    /// Returns true if no scoped name is specified.
    pub fn is_none(&self) -> bool {
        self.value.is_none()
    }
}

/// https://drafts.csswg.org/css-anchor-position-1/#propdef-position-anchor
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[typed(todo_derive_fields)]
pub enum PositionAnchorKeyword {
    /// `normal`
    Normal,
    /// `none`
    None,
    /// `auto`
    Auto,
    /// `<dashed-ident>`
    Ident(DashedIdent),
}

impl IsTreeScoped for PositionAnchorKeyword {
    fn is_tree_scoped(&self) -> bool {
        match *self {
            Self::Normal | Self::None | Self::Auto => false,
            Self::Ident(_) => true,
        }
    }
}

/// https://drafts.csswg.org/css-anchor-position-1/#propdef-position-anchor
pub type PositionAnchor = TreeScoped<PositionAnchorKeyword>;

impl PositionAnchor {
    /// Return the `normal` value.
    pub fn normal() -> Self {
        Self::with_default_level(PositionAnchorKeyword::Normal)
    }
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
/// How to swap values for the automatically-generated position tactic.
pub enum PositionTryFallbacksTryTacticKeyword {
    /// Swap the values in the block axis.
    FlipBlock,
    /// Swap the values in the inline axis.
    FlipInline,
    /// Swap the values in the start properties.
    FlipStart,
    /// Swap the values in the X axis.
    FlipX,
    /// Swap the values in the Y axis.
    FlipY,
}

#[derive(
    Clone,
    Debug,
    Default,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(transparent)]
/// Changes for the automatically-generated position option.
/// Note that this is order-dependent - e.g. `flip-start flip-inline` != `flip-inline flip-start`.
///
/// https://drafts.csswg.org/css-anchor-position-1/#typedef-position-try-fallbacks-try-tactic
pub struct PositionTryFallbacksTryTactic(
    #[css(iterable)] pub ThinVec<PositionTryFallbacksTryTacticKeyword>,
);

impl Parse for PositionTryFallbacksTryTactic {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let mut result = ThinVec::with_capacity(5);
        // Collect up to 5 keywords, disallowing duplicates.
        for _ in 0..5 {
            if let Ok(kw) = input.try_parse(PositionTryFallbacksTryTacticKeyword::parse) {
                if result.contains(&kw) {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                result.push(kw);
            } else {
                break;
            }
        }
        if result.is_empty() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(Self(result))
    }
}

impl PositionTryFallbacksTryTactic {
    /// Returns whether there's any tactic.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    /// Iterates over the fallbacks in order.
    #[inline]
    pub fn iter(&self) -> impl Iterator<Item = &PositionTryFallbacksTryTacticKeyword> {
        self.0.iter()
    }
}

#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
/// https://drafts.csswg.org/css-anchor-position-1/#propdef-position-try-fallbacks
/// <dashed-ident> || <try-tactic>
pub struct DashedIdentAndOrTryTactic {
    /// `<dashed-ident>`
    pub ident: DashedIdent,
    /// `<try-tactic>`
    pub try_tactic: PositionTryFallbacksTryTactic,
}

impl Parse for DashedIdentAndOrTryTactic {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let mut result = Self {
            ident: DashedIdent::empty(),
            try_tactic: PositionTryFallbacksTryTactic::default(),
        };

        loop {
            if result.ident.is_empty() {
                if let Ok(ident) = input.try_parse(|i| DashedIdent::parse(context, i)) {
                    result.ident = ident;
                    continue;
                }
            }
            if result.try_tactic.is_empty() {
                if let Ok(try_tactic) =
                    input.try_parse(|i| PositionTryFallbacksTryTactic::parse(context, i))
                {
                    result.try_tactic = try_tactic;
                    continue;
                }
            }
            break;
        }

        if result.ident.is_empty() && result.try_tactic.is_empty() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        return Ok(result);
    }
}

#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
/// https://drafts.csswg.org/css-anchor-position-1/#propdef-position-try-fallbacks
/// [ [<dashed-ident> || <try-tactic>] | <'position-area'> ]
pub enum PositionTryFallbacksItem {
    /// `<dashed-ident> || <try-tactic>`
    IdentAndOrTactic(DashedIdentAndOrTryTactic),
    #[parse(parse_fn = "PositionArea::parse_except_none")]
    /// `<position-area>`
    PositionArea(PositionArea),
}

#[derive(
    Clone,
    Debug,
    Default,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(comma)]
#[repr(C)]
#[typed(todo_derive_fields)]
/// https://drafts.csswg.org/css-anchor-position-1/#position-try-fallbacks
pub struct PositionTryFallbacks(
    #[css(iterable, if_empty = "none")]
    #[ignore_malloc_size_of = "Arc"]
    pub crate::ArcSlice<PositionTryFallbacksItem>,
);

impl PositionTryFallbacks {
    #[inline]
    /// Return the `none` value.
    pub fn none() -> Self {
        Self(Default::default())
    }

    /// Returns whether this is the `none` value.
    pub fn is_none(&self) -> bool {
        self.0.is_empty()
    }
}

impl Parse for PositionTryFallbacks {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
            return Ok(Self::none());
        }
        // The common case is unlikely to include many alternate positioning
        // styles, so space for four on the stack should typically be enough.
        let mut items: SmallVec<[PositionTryFallbacksItem; 4]> =
            smallvec![PositionTryFallbacksItem::parse(context, input)?];
        while input.try_parse(|input| input.expect_comma()).is_ok() {
            items.push(PositionTryFallbacksItem::parse(context, input)?);
        }
        Ok(Self(ArcSlice::from_iter(items.drain(..))))
    }
}

/// https://drafts.csswg.org/css-anchor-position-1/#position-try-order-property
#[derive(
    Clone,
    Copy,
    Debug,
    Default,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum PositionTryOrder {
    #[default]
    /// `normal`
    Normal,
    /// `most-width`
    MostWidth,
    /// `most-height`
    MostHeight,
    /// `most-block-size`
    MostBlockSize,
    /// `most-inline-size`
    MostInlineSize,
}

impl PositionTryOrder {
    #[inline]
    /// Return the `auto` value.
    pub fn normal() -> Self {
        Self::Normal
    }

    /// Returns whether this is the `auto` value.
    pub fn is_normal(&self) -> bool {
        *self == Self::Normal
    }
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(single = "always", mixed = "anchors-valid,anchors-visible,no-overflow"))]
#[repr(C)]
/// Specified keyword values for the position-visibility property.
pub struct PositionVisibility(u8);
bitflags! {
    impl PositionVisibility: u8 {
        /// Element is displayed without regard for its anchors or its overflowing status.
        const ALWAYS = 0;
        /// anchors-valid
        const ANCHORS_VALID = 1 << 0;
        /// anchors-visible
        const ANCHORS_VISIBLE = 1 << 1;
        /// no-overflow
        const NO_OVERFLOW = 1 << 2;
    }
}

impl Default for PositionVisibility {
    fn default() -> Self {
        Self::ALWAYS
    }
}

impl PositionVisibility {
    #[inline]
    /// Returns the initial value of position-visibility
    pub fn always() -> Self {
        Self::ALWAYS
    }
}

/// A value indicating which high level group in the formal grammar a
/// PositionAreaKeyword or PositionArea belongs to.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PositionAreaType {
    /// X || Y
    Physical,
    /// block || inline
    Logical,
    /// self-block || self-inline
    SelfLogical,
    /// start|end|span-* {1,2}
    Inferred,
    /// self-start|self-end|span-self-* {1,2}
    SelfInferred,
    /// center, span-all
    Common,
    /// none
    None,
}

/// A three-bit value that represents the axis in which position-area operates on.
/// Represented as 4 bits: axis type (physical or logical), direction type (physical or logical),
/// axis value.
///
/// There are two special values on top (Inferred and None) that represent ambiguous or axis-less
/// keywords, respectively.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq, FromPrimitive)]
#[allow(missing_docs)]
pub enum PositionAreaAxis {
    Horizontal = 0b000,
    Vertical = 0b001,

    X = 0b010,
    Y = 0b011,

    Block = 0b110,
    Inline = 0b111,

    Inferred = 0b100,
    None = 0b101,
}

impl PositionAreaAxis {
    /// Whether this axis is physical or not.
    pub fn is_physical(self) -> bool {
        (self as u8 & 0b100) == 0
    }

    /// Whether the direction is logical or not.
    fn is_flow_relative_direction(self) -> bool {
        self == Self::Inferred || (self as u8 & 0b10) != 0
    }

    /// Whether this axis goes first in the canonical syntax.
    fn is_canonically_first(self) -> bool {
        self != Self::Inferred && (self as u8) & 1 == 0
    }

    #[allow(unused)]
    fn flip(self) -> Self {
        if matches!(self, Self::Inferred | Self::None) {
            return self;
        }
        Self::from_u8(self as u8 ^ 1u8).unwrap()
    }

    fn to_logical(self, wm: WritingMode, inferred: LogicalAxis) -> Option<LogicalAxis> {
        Some(match self {
            PositionAreaAxis::Horizontal | PositionAreaAxis::X => {
                if wm.is_vertical() {
                    LogicalAxis::Block
                } else {
                    LogicalAxis::Inline
                }
            },
            PositionAreaAxis::Vertical | PositionAreaAxis::Y => {
                if wm.is_vertical() {
                    LogicalAxis::Inline
                } else {
                    LogicalAxis::Block
                }
            },
            PositionAreaAxis::Block => LogicalAxis::Block,
            PositionAreaAxis::Inline => LogicalAxis::Inline,
            PositionAreaAxis::Inferred => inferred,
            PositionAreaAxis::None => return None,
        })
    }
}

/// Specifies which tracks(s) on the axis that the position-area span occupies.
/// Represented as 3 bits: start, center, end track.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq, FromPrimitive)]
pub enum PositionAreaTrack {
    /// First track
    Start = 0b001,
    /// First and center.
    SpanStart = 0b011,
    /// Last track.
    End = 0b100,
    /// Last and center.
    SpanEnd = 0b110,
    /// Center track.
    Center = 0b010,
    /// All tracks
    SpanAll = 0b111,
}

impl PositionAreaTrack {
    fn flip(self) -> Self {
        match self {
            Self::Start => Self::End,
            Self::SpanStart => Self::SpanEnd,
            Self::End => Self::Start,
            Self::SpanEnd => Self::SpanStart,
            Self::Center | Self::SpanAll => self,
        }
    }

    fn start(self) -> bool {
        self as u8 & 1 != 0
    }
}

/// The shift to the left needed to set the axis.
pub const AXIS_SHIFT: usize = 3;
/// The mask used to extract the axis.
pub const AXIS_MASK: u8 = 0b111u8 << AXIS_SHIFT;
/// The mask used to extract the track.
pub const TRACK_MASK: u8 = 0b111u8;
/// The self-wm bit.
pub const SELF_WM: u8 = 1u8 << 6;

#[derive(
    Clone,
    Copy,
    Debug,
    Default,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    FromPrimitive,
)]
#[allow(missing_docs)]
#[repr(u8)]
/// Possible values for the `position-area` property's keywords.
/// Represented by [0z xxx yyy], where z means "self wm resolution", xxxx is the axis (as in
/// PositionAreaAxis) and yyy is the PositionAreaTrack
/// https://drafts.csswg.org/css-anchor-position-1/#propdef-position-area
pub enum PositionAreaKeyword {
    #[default]
    None = (PositionAreaAxis::None as u8) << AXIS_SHIFT,

    // Common (shared) keywords:
    Center = ((PositionAreaAxis::None as u8) << AXIS_SHIFT) | PositionAreaTrack::Center as u8,
    SpanAll = ((PositionAreaAxis::None as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanAll as u8,

    // Inferred-axis edges:
    Start = ((PositionAreaAxis::Inferred as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    End = ((PositionAreaAxis::Inferred as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,
    SpanStart =
        ((PositionAreaAxis::Inferred as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanEnd = ((PositionAreaAxis::Inferred as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,

    // Purely physical edges:
    Left = ((PositionAreaAxis::Horizontal as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    Right = ((PositionAreaAxis::Horizontal as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,
    Top = ((PositionAreaAxis::Vertical as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    Bottom = ((PositionAreaAxis::Vertical as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,

    // Flow-relative physical-axis edges:
    XStart = ((PositionAreaAxis::X as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    XEnd = ((PositionAreaAxis::X as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,
    YStart = ((PositionAreaAxis::Y as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    YEnd = ((PositionAreaAxis::Y as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,

    // Logical edges:
    BlockStart = ((PositionAreaAxis::Block as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    BlockEnd = ((PositionAreaAxis::Block as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,
    InlineStart = ((PositionAreaAxis::Inline as u8) << AXIS_SHIFT) | PositionAreaTrack::Start as u8,
    InlineEnd = ((PositionAreaAxis::Inline as u8) << AXIS_SHIFT) | PositionAreaTrack::End as u8,

    // Composite values with Span:
    SpanLeft =
        ((PositionAreaAxis::Horizontal as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanRight =
        ((PositionAreaAxis::Horizontal as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,
    SpanTop =
        ((PositionAreaAxis::Vertical as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanBottom =
        ((PositionAreaAxis::Vertical as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,

    // Flow-relative physical-axis edges:
    SpanXStart = ((PositionAreaAxis::X as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanXEnd = ((PositionAreaAxis::X as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,
    SpanYStart = ((PositionAreaAxis::Y as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanYEnd = ((PositionAreaAxis::Y as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,

    // Logical edges:
    SpanBlockStart =
        ((PositionAreaAxis::Block as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanBlockEnd =
        ((PositionAreaAxis::Block as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,
    SpanInlineStart =
        ((PositionAreaAxis::Inline as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanStart as u8,
    SpanInlineEnd =
        ((PositionAreaAxis::Inline as u8) << AXIS_SHIFT) | PositionAreaTrack::SpanEnd as u8,

    // Values using the Self element's writing-mode:
    SelfStart = SELF_WM | (Self::Start as u8),
    SelfEnd = SELF_WM | (Self::End as u8),
    SpanSelfStart = SELF_WM | (Self::SpanStart as u8),
    SpanSelfEnd = SELF_WM | (Self::SpanEnd as u8),

    SelfXStart = SELF_WM | (Self::XStart as u8),
    SelfXEnd = SELF_WM | (Self::XEnd as u8),
    SelfYStart = SELF_WM | (Self::YStart as u8),
    SelfYEnd = SELF_WM | (Self::YEnd as u8),
    SelfBlockStart = SELF_WM | (Self::BlockStart as u8),
    SelfBlockEnd = SELF_WM | (Self::BlockEnd as u8),
    SelfInlineStart = SELF_WM | (Self::InlineStart as u8),
    SelfInlineEnd = SELF_WM | (Self::InlineEnd as u8),

    SpanSelfXStart = SELF_WM | (Self::SpanXStart as u8),
    SpanSelfXEnd = SELF_WM | (Self::SpanXEnd as u8),
    SpanSelfYStart = SELF_WM | (Self::SpanYStart as u8),
    SpanSelfYEnd = SELF_WM | (Self::SpanYEnd as u8),
    SpanSelfBlockStart = SELF_WM | (Self::SpanBlockStart as u8),
    SpanSelfBlockEnd = SELF_WM | (Self::SpanBlockEnd as u8),
    SpanSelfInlineStart = SELF_WM | (Self::SpanInlineStart as u8),
    SpanSelfInlineEnd = SELF_WM | (Self::SpanInlineEnd as u8),
}

impl PositionAreaKeyword {
    /// Returns the 'none' value.
    #[inline]
    pub fn none() -> Self {
        Self::None
    }

    /// Returns true if this is the none keyword.
    pub fn is_none(&self) -> bool {
        *self == Self::None
    }

    /// Whether we're one of the self-wm keywords.
    pub fn self_wm(self) -> bool {
        (self as u8 & SELF_WM) != 0
    }

    /// Get this keyword's axis.
    pub fn axis(self) -> PositionAreaAxis {
        PositionAreaAxis::from_u8((self as u8 >> AXIS_SHIFT) & 0b111).unwrap()
    }

    /// Returns this keyword but with the axis swapped by the argument.
    pub fn with_axis(self, axis: PositionAreaAxis) -> Self {
        Self::from_u8(((self as u8) & !AXIS_MASK) | ((axis as u8) << AXIS_SHIFT)).unwrap()
    }

    /// If this keyword uses an inferred axis, replaces it.
    pub fn with_inferred_axis(self, axis: PositionAreaAxis) -> Self {
        if self.axis() == PositionAreaAxis::Inferred {
            self.with_axis(axis)
        } else {
            self
        }
    }

    /// Get this keyword's track, or None if we're the `None` keyword.
    pub fn track(self) -> Option<PositionAreaTrack> {
        let result = PositionAreaTrack::from_u8(self as u8 & TRACK_MASK);
        debug_assert_eq!(
            result.is_none(),
            self.is_none(),
            "Only the none keyword has no track"
        );
        result
    }

    fn group_type(self) -> PositionAreaType {
        let axis = self.axis();
        if axis == PositionAreaAxis::None {
            if self.is_none() {
                return PositionAreaType::None;
            }
            return PositionAreaType::Common;
        }
        if axis == PositionAreaAxis::Inferred {
            return if self.self_wm() {
                PositionAreaType::SelfInferred
            } else {
                PositionAreaType::Inferred
            };
        }
        if axis.is_physical() {
            return PositionAreaType::Physical;
        }
        if self.self_wm() {
            PositionAreaType::SelfLogical
        } else {
            PositionAreaType::Logical
        }
    }

    fn to_physical(
        self,
        cb_wm: WritingMode,
        self_wm: WritingMode,
        inferred_axis: LogicalAxis,
    ) -> Self {
        let wm = if self.self_wm() { self_wm } else { cb_wm };
        let axis = self.axis();
        if !axis.is_flow_relative_direction() {
            return self;
        }
        let Some(logical_axis) = axis.to_logical(wm, inferred_axis) else {
            return self;
        };
        let Some(track) = self.track() else {
            debug_assert!(false, "How did we end up with no track here? {self:?}");
            return self;
        };
        let start = track.start();
        let logical_side = match logical_axis {
            LogicalAxis::Block => {
                if start {
                    LogicalSide::BlockStart
                } else {
                    LogicalSide::BlockEnd
                }
            },
            LogicalAxis::Inline => {
                if start {
                    LogicalSide::InlineStart
                } else {
                    LogicalSide::InlineEnd
                }
            },
        };
        let physical_side = logical_side.to_physical(wm);
        let physical_start = matches!(physical_side, PhysicalSide::Top | PhysicalSide::Left);
        let new_track = if physical_start != start {
            track.flip()
        } else {
            track
        };
        let new_axis = if matches!(physical_side, PhysicalSide::Top | PhysicalSide::Bottom) {
            PositionAreaAxis::Vertical
        } else {
            PositionAreaAxis::Horizontal
        };
        Self::from_u8(new_track as u8 | ((new_axis as u8) << AXIS_SHIFT)).unwrap()
    }

    fn flip_track(self) -> Self {
        let Some(old_track) = self.track() else {
            return self;
        };
        let new_track = old_track.flip();
        Self::from_u8((self as u8 & !TRACK_MASK) | new_track as u8).unwrap()
    }

    /// Returns a value for the self-alignment properties in order to resolve
    /// `normal`, in terms of the containing block's writing mode.
    ///
    /// Note that the caller must have converted the position-area to physical
    /// values.
    ///
    /// <https://drafts.csswg.org/css-anchor-position/#position-area-alignment>
    pub fn to_self_alignment(self, axis: LogicalAxis, cb_wm: &WritingMode) -> Option<AlignFlags> {
        let track = self.track()?;
        Some(match track {
            // "If the only the center track in an axis is selected, the default alignment in that axis is center."
            PositionAreaTrack::Center => AlignFlags::CENTER,
            // "If all three tracks are selected, the default alignment in that axis is anchor-center."
            PositionAreaTrack::SpanAll => AlignFlags::ANCHOR_CENTER,
            // "Otherwise, the default alignment in that axis is toward the non-specified side track: if it’s
            // specifying the “start” track of its axis, the default alignment in that axis is end; etc."
            _ => {
                debug_assert_eq!(self.group_type(), PositionAreaType::Physical);
                if axis == LogicalAxis::Inline {
                    // For the inline axis, map 'start' to 'end' unless the axis is inline-reversed,
                    // meaning that its logical flow is counter to physical coordinates and therefore
                    // physical 'start' already corresponds to logical 'end'.
                    if track.start() == cb_wm.intersects(WritingMode::INLINE_REVERSED) {
                        AlignFlags::START
                    } else {
                        AlignFlags::END
                    }
                } else {
                    // For the block axis, only vertical-rl has reversed flow and therefore
                    // does not map 'start' to 'end' here.
                    if track.start() == cb_wm.is_vertical_rl() {
                        AlignFlags::START
                    } else {
                        AlignFlags::END
                    }
                }
            },
        })
    }
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
/// https://drafts.csswg.org/css-anchor-position-1/#propdef-position-area
pub struct PositionArea {
    /// First keyword, if any.
    pub first: PositionAreaKeyword,
    /// Second keyword, if any.
    #[css(skip_if = "PositionAreaKeyword::is_none")]
    pub second: PositionAreaKeyword,
}

impl PositionArea {
    /// Returns the none value.
    #[inline]
    pub fn none() -> Self {
        Self {
            first: PositionAreaKeyword::None,
            second: PositionAreaKeyword::None,
        }
    }

    /// Returns whether we're the none value.
    #[inline]
    pub fn is_none(&self) -> bool {
        self.first.is_none()
    }

    /// Parses a <position-area> without allowing `none`.
    pub fn parse_except_none<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(context, input, /*allow_none*/ false)
    }

    /// Get the high-level grammar group of this.
    pub fn get_type(&self) -> PositionAreaType {
        let first = self.first.group_type();
        let second = self.second.group_type();
        if matches!(second, PositionAreaType::None | PositionAreaType::Common) {
            return first;
        }
        if first == PositionAreaType::Common {
            return second;
        }
        if first != second {
            return PositionAreaType::None;
        }
        let first_axis = self.first.axis();
        if first_axis != PositionAreaAxis::Inferred
            && first_axis.is_canonically_first() == self.second.axis().is_canonically_first()
        {
            return PositionAreaType::None;
        }
        first
    }

    fn parse_internal<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_none: bool,
    ) -> Result<Self, ParseError<'i>> {
        let mut location = input.current_source_location();
        let mut first = PositionAreaKeyword::parse(input)?;
        if first.is_none() {
            if allow_none {
                return Ok(Self::none());
            }
            return Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        location = input.current_source_location();
        let second = input.try_parse(PositionAreaKeyword::parse);
        if let Ok(PositionAreaKeyword::None) = second {
            // `none` is only allowed as a single value
            return Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        let mut second = second.unwrap_or(PositionAreaKeyword::None);
        if second.is_none() {
            // Either there was no second keyword and try_parse returned a
            // BasicParseErrorKind::EndOfInput, or else the second "keyword"
            // was invalid. We assume the former case here, and if it's the
            // latter case then our caller detects the error (try_parse will,
            // have rewound, leaving an unparsed token).
            return Ok(Self { first, second });
        }

        let pair_type = Self { first, second }.get_type();
        if pair_type == PositionAreaType::None {
            // Mismatched types or what not.
            return Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        // For types that have a canonical order, remove 'span-all' (the default behavior;
        // unnecessary for keyword pairs with a known order).
        if matches!(
            pair_type,
            PositionAreaType::Physical | PositionAreaType::Logical | PositionAreaType::SelfLogical
        ) {
            if second == PositionAreaKeyword::SpanAll {
                // Span-all is the default behavior, so specifying `span-all` is
                // superfluous.
                second = PositionAreaKeyword::None;
            } else if first == PositionAreaKeyword::SpanAll {
                first = second;
                second = PositionAreaKeyword::None;
            }
        }
        if first == second {
            second = PositionAreaKeyword::None;
        }
        let mut result = Self { first, second };
        result.canonicalize_order();
        Ok(result)
    }

    fn canonicalize_order(&mut self) {
        let first_axis = self.first.axis();
        if first_axis.is_canonically_first() || self.second.is_none() {
            return;
        }
        let second_axis = self.second.axis();
        if first_axis == second_axis {
            // Inferred or axis-less keywords.
            return;
        }
        if second_axis.is_canonically_first()
            || (second_axis == PositionAreaAxis::None && first_axis != PositionAreaAxis::Inferred)
        {
            std::mem::swap(&mut self.first, &mut self.second);
        }
    }

    fn make_missing_second_explicit(&mut self) {
        if !self.second.is_none() {
            return;
        }
        let axis = self.first.axis();
        if matches!(axis, PositionAreaAxis::Inferred | PositionAreaAxis::None) {
            self.second = self.first;
            return;
        }
        self.second = PositionAreaKeyword::SpanAll;
        if !axis.is_canonically_first() {
            std::mem::swap(&mut self.first, &mut self.second);
        }
    }

    /// Turns this <position-area> value into a physical <position-area>.
    pub fn to_physical(mut self, cb_wm: WritingMode, self_wm: WritingMode) -> Self {
        self.make_missing_second_explicit();
        // If both axes are None, to_physical and canonicalize_order are not useful.
        // The first value refers to the block axis, the second to the inline axis;
        // but as a physical type, they will be interpreted as the x- and y-axis
        // respectively, so if the writing mode is horizontal we need to swap the
        // values (block -> y, inline -> x).
        if self.first.axis() == PositionAreaAxis::None
            && self.second.axis() == PositionAreaAxis::None
            && !cb_wm.is_vertical()
        {
            std::mem::swap(&mut self.first, &mut self.second);
        } else {
            self.first = self.first.to_physical(cb_wm, self_wm, LogicalAxis::Block);
            self.second = self.second.to_physical(cb_wm, self_wm, LogicalAxis::Inline);
            self.canonicalize_order();
        }
        self
    }

    fn flip_logical_axis(&mut self, wm: WritingMode, axis: LogicalAxis) {
        if self.first.axis().to_logical(wm, LogicalAxis::Block) == Some(axis) {
            self.first = self.first.flip_track();
        } else {
            self.second = self.second.flip_track();
        }
    }

    fn flip_start(&mut self) {
        self.first = self.first.with_axis(self.first.axis().flip());
        self.second = self.second.with_axis(self.second.axis().flip());
    }

    /// Applies a try tactic to this `<position-area>` value.
    pub fn with_tactic(
        mut self,
        wm: WritingMode,
        tactic: PositionTryFallbacksTryTacticKeyword,
    ) -> Self {
        self.make_missing_second_explicit();
        let axis_to_flip = match tactic {
            PositionTryFallbacksTryTacticKeyword::FlipStart => {
                self.flip_start();
                return self;
            },
            PositionTryFallbacksTryTacticKeyword::FlipBlock => LogicalAxis::Block,
            PositionTryFallbacksTryTacticKeyword::FlipInline => LogicalAxis::Inline,
            PositionTryFallbacksTryTacticKeyword::FlipX => {
                if wm.is_horizontal() {
                    LogicalAxis::Inline
                } else {
                    LogicalAxis::Block
                }
            },
            PositionTryFallbacksTryTacticKeyword::FlipY => {
                if wm.is_vertical() {
                    LogicalAxis::Inline
                } else {
                    LogicalAxis::Block
                }
            },
        };
        self.flip_logical_axis(wm, axis_to_flip);
        self
    }
}

impl Parse for PositionArea {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(context, input, /* allow_none = */ true)
    }
}

/// Represents a side, either horizontal or vertical, of a CSS position.
pub trait Side {
    /// Returns the start side.
    fn start() -> Self;

    /// Returns whether this side is the start side.
    fn is_start(&self) -> bool;
}

impl Side for HorizontalPositionKeyword {
    #[inline]
    fn start() -> Self {
        HorizontalPositionKeyword::Left
    }

    #[inline]
    fn is_start(&self) -> bool {
        *self == Self::start()
    }
}

impl Side for VerticalPositionKeyword {
    #[inline]
    fn start() -> Self {
        VerticalPositionKeyword::Top
    }

    #[inline]
    fn is_start(&self) -> bool {
        *self == Self::start()
    }
}

/// Controls how the auto-placement algorithm works specifying exactly how auto-placed items
/// get flowed into the grid: [ row | column ] || dense
/// https://drafts.csswg.org/css-grid-2/#grid-auto-flow-property
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    mixed = "row,column,dense",
    validate_mixed = "Self::validate_and_simplify"
))]
#[repr(C)]
pub struct GridAutoFlow(u8);
bitflags! {
    impl GridAutoFlow: u8 {
        /// 'row' - mutually exclusive with 'column'
        const ROW = 1 << 0;
        /// 'column' - mutually exclusive with 'row'
        const COLUMN = 1 << 1;
        /// 'dense'
        const DENSE = 1 << 2;
    }
}

impl GridAutoFlow {
    /// [ row | column ] || dense
    fn validate_and_simplify(&mut self) -> bool {
        if self.contains(Self::ROW | Self::COLUMN) {
            // row and column are mutually exclusive.
            return false;
        }
        if *self == Self::DENSE {
            // If there's no column, default to row.
            self.insert(Self::ROW);
        }
        true
    }
}

impl ToCss for GridAutoFlow {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        let dense = self.intersects(Self::DENSE);
        if self.intersects(Self::ROW) {
            return if dense {
                dest.write_str("dense")
            } else {
                dest.write_str("row")
            };
        }
        debug_assert!(self.intersects(Self::COLUMN));
        if dense {
            dest.write_str("column dense")
        } else {
            dest.write_str("column")
        }
    }
}

#[repr(u8)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
/// Masonry auto-placement algorithm packing.
pub enum MasonryPlacement {
    /// Place the item in the track(s) with the smallest extent so far.
    Pack,
    /// Place the item after the last item, from start to end.
    Next,
}

#[repr(u8)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
/// Masonry auto-placement algorithm item sorting option.
pub enum MasonryItemOrder {
    /// Place all items with a definite placement before auto-placed items.
    DefiniteFirst,
    /// Place items in `order-modified document order`.
    Ordered,
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
/// Controls how the Masonry layout algorithm works
/// specifying exactly how auto-placed items get flowed in the masonry axis.
pub struct MasonryAutoFlow {
    /// Specify how to pick a auto-placement track.
    #[css(contextual_skip_if = "is_pack_with_non_default_order")]
    pub placement: MasonryPlacement,
    /// Specify how to pick an item to place.
    #[css(skip_if = "is_item_order_definite_first")]
    pub order: MasonryItemOrder,
}

#[inline]
fn is_pack_with_non_default_order(placement: &MasonryPlacement, order: &MasonryItemOrder) -> bool {
    *placement == MasonryPlacement::Pack && *order != MasonryItemOrder::DefiniteFirst
}

#[inline]
fn is_item_order_definite_first(order: &MasonryItemOrder) -> bool {
    *order == MasonryItemOrder::DefiniteFirst
}

impl MasonryAutoFlow {
    #[inline]
    /// Get initial `masonry-auto-flow` value.
    pub fn initial() -> MasonryAutoFlow {
        MasonryAutoFlow {
            placement: MasonryPlacement::Pack,
            order: MasonryItemOrder::DefiniteFirst,
        }
    }
}

impl Parse for MasonryAutoFlow {
    /// [ definite-first | ordered ] || [ pack | next ]
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<MasonryAutoFlow, ParseError<'i>> {
        let mut value = MasonryAutoFlow::initial();
        let mut got_placement = false;
        let mut got_order = false;
        while !input.is_exhausted() {
            let location = input.current_source_location();
            let ident = input.expect_ident()?;
            let success = match_ignore_ascii_case! { &ident,
                "pack" if !got_placement => {
                    got_placement = true;
                    true
                },
                "next" if !got_placement => {
                    value.placement = MasonryPlacement::Next;
                    got_placement = true;
                    true
                },
                "definite-first" if !got_order => {
                    got_order = true;
                    true
                },
                "ordered" if !got_order => {
                    value.order = MasonryItemOrder::Ordered;
                    got_order = true;
                    true
                },
                _ => false
            };
            if !success {
                return Err(location
                    .new_custom_error(SelectorParseErrorKind::UnexpectedIdent(ident.clone())));
            }
        }

        if got_placement || got_order {
            Ok(value)
        } else {
            Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        }
    }
}

#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
/// https://drafts.csswg.org/css-grid/#named-grid-area
pub struct TemplateAreas {
    /// `named area` containing for each template area
    #[css(skip)]
    pub areas: crate::OwnedSlice<NamedArea>,
    /// The simplified CSS strings for serialization purpose.
    /// https://drafts.csswg.org/css-grid/#serialize-template
    // Note: We also use the length of `strings` when computing the explicit grid end line number
    // (i.e. row number).
    #[css(iterable)]
    pub strings: crate::OwnedSlice<crate::OwnedStr>,
    /// The number of columns of the grid.
    #[css(skip)]
    pub width: u32,
}

/// Parser for grid template areas.
#[derive(Default)]
pub struct TemplateAreasParser {
    areas: Vec<NamedArea>,
    area_indices: PrecomputedHashMap<Atom, usize>,
    strings: Vec<crate::OwnedStr>,
    width: u32,
    row: u32,
}

impl TemplateAreasParser {
    /// Parse a single string.
    pub fn try_parse_string<'i>(
        &mut self,
        input: &mut Parser<'i, '_>,
    ) -> Result<(), ParseError<'i>> {
        input.try_parse(|input| {
            self.parse_string(input.expect_string()?)
                .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        })
    }

    /// Parse a single string.
    fn parse_string(&mut self, string: &str) -> Result<(), ()> {
        self.row += 1;
        let mut simplified_string = String::new();
        let mut current_area_index: Option<usize> = None;
        let mut column = 0u32;
        for token in TemplateAreasTokenizer(string) {
            column += 1;
            if column > 1 {
                simplified_string.push(' ');
            }
            let name = if let Some(token) = token? {
                simplified_string.push_str(token);
                Atom::from(token)
            } else {
                if let Some(index) = current_area_index.take() {
                    if self.areas[index].columns.end != column {
                        return Err(());
                    }
                }
                simplified_string.push('.');
                continue;
            };
            if let Some(index) = current_area_index {
                if self.areas[index].name == name {
                    if self.areas[index].rows.start == self.row {
                        self.areas[index].columns.end += 1;
                    }
                    continue;
                }
                if self.areas[index].columns.end != column {
                    return Err(());
                }
            }
            match self.area_indices.entry(name) {
                Entry::Occupied(ref e) => {
                    let index = *e.get();
                    if self.areas[index].columns.start != column
                        || self.areas[index].rows.end != self.row
                    {
                        return Err(());
                    }
                    self.areas[index].rows.end += 1;
                    current_area_index = Some(index);
                },
                Entry::Vacant(v) => {
                    let index = self.areas.len();
                    let name = v.key().clone();
                    v.insert(index);
                    self.areas.push(NamedArea {
                        name,
                        columns: UnsignedRange {
                            start: column,
                            end: column + 1,
                        },
                        rows: UnsignedRange {
                            start: self.row,
                            end: self.row + 1,
                        },
                    });
                    current_area_index = Some(index);
                },
            }
        }
        if column == 0 {
            // Each string must produce a valid token.
            // https://github.com/w3c/csswg-drafts/issues/5110
            return Err(());
        }
        if let Some(index) = current_area_index {
            if self.areas[index].columns.end != column + 1 {
                debug_assert_ne!(self.areas[index].rows.start, self.row);
                return Err(());
            }
        }
        if self.row == 1 {
            self.width = column;
        } else if self.width != column {
            return Err(());
        }

        self.strings.push(simplified_string.into());
        Ok(())
    }

    /// Return the parsed template areas.
    pub fn finish(self) -> Result<TemplateAreas, ()> {
        if self.strings.is_empty() {
            return Err(());
        }
        Ok(TemplateAreas {
            areas: self.areas.into(),
            strings: self.strings.into(),
            width: self.width,
        })
    }
}

impl TemplateAreas {
    fn parse_internal(input: &mut Parser) -> Result<Self, ()> {
        let mut parser = TemplateAreasParser::default();
        while parser.try_parse_string(input).is_ok() {}
        parser.finish()
    }
}

impl Parse for TemplateAreas {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(input)
            .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }
}

/// Arc type for `Arc<TemplateAreas>`
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(transparent)]
pub struct TemplateAreasArc(#[ignore_malloc_size_of = "Arc"] pub Arc<TemplateAreas>);

impl Parse for TemplateAreasArc {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let parsed = TemplateAreas::parse(context, input)?;
        Ok(TemplateAreasArc(Arc::new(parsed)))
    }
}

/// A range of rows or columns. Using this instead of std::ops::Range for FFI
/// purposes.
#[repr(C)]
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
pub struct UnsignedRange {
    /// The start of the range.
    pub start: u32,
    /// The end of the range.
    pub end: u32,
}

#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
/// Not associated with any particular grid item, but can be referenced from the
/// grid-placement properties.
pub struct NamedArea {
    /// Name of the `named area`
    pub name: Atom,
    /// Rows of the `named area`
    pub rows: UnsignedRange,
    /// Columns of the `named area`
    pub columns: UnsignedRange,
}

/// Tokenize the string into a list of the tokens,
/// using longest-match semantics
struct TemplateAreasTokenizer<'a>(&'a str);

impl<'a> Iterator for TemplateAreasTokenizer<'a> {
    type Item = Result<Option<&'a str>, ()>;

    fn next(&mut self) -> Option<Self::Item> {
        let rest = self.0.trim_start_matches(HTML_SPACE_CHARACTERS);
        if rest.is_empty() {
            return None;
        }
        if rest.starts_with('.') {
            self.0 = &rest[rest.find(|c| c != '.').unwrap_or(rest.len())..];
            return Some(Ok(None));
        }
        if !rest.starts_with(is_name_code_point) {
            return Some(Err(()));
        }
        let token_len = rest.find(|c| !is_name_code_point(c)).unwrap_or(rest.len());
        let token = &rest[..token_len];
        self.0 = &rest[token_len..];
        Some(Ok(Some(token)))
    }
}

fn is_name_code_point(c: char) -> bool {
    c >= 'A' && c <= 'Z'
        || c >= 'a' && c <= 'z'
        || c >= '\u{80}'
        || c == '_'
        || c >= '0' && c <= '9'
        || c == '-'
}

/// This property specifies named grid areas.
///
/// The syntax of this property also provides a visualization of the structure
/// of the grid, making the overall layout of the grid container easier to
/// understand.
#[repr(C, u8)]
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[typed(todo_derive_fields)]
pub enum GridTemplateAreas {
    /// The `none` value.
    None,
    /// The actual value.
    Areas(TemplateAreasArc),
}

impl GridTemplateAreas {
    #[inline]
    /// Get default value as `none`
    pub fn none() -> GridTemplateAreas {
        GridTemplateAreas::None
    }
}

/// A specified value for the `z-index` property.
pub type ZIndex = GenericZIndex<Integer>;

/// A specified value for the `aspect-ratio` property.
pub type AspectRatio = GenericAspectRatio<NonNegativeNumber>;

impl Parse for AspectRatio {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use crate::values::generics::position::PreferredRatio;
        use crate::values::specified::Ratio;

        let location = input.current_source_location();
        let mut auto = input.try_parse(|i| i.expect_ident_matching("auto"));
        let ratio = input.try_parse(|i| Ratio::parse(context, i));
        if auto.is_err() {
            auto = input.try_parse(|i| i.expect_ident_matching("auto"));
        }

        if auto.is_err() && ratio.is_err() {
            return Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(AspectRatio {
            auto: auto.is_ok(),
            ratio: match ratio {
                Ok(ratio) => PreferredRatio::Ratio(ratio),
                Err(..) => PreferredRatio::None,
            },
        })
    }
}

impl AspectRatio {
    /// Returns Self by a valid ratio.
    pub fn from_mapped_ratio(w: f32, h: f32) -> Self {
        use crate::values::generics::position::PreferredRatio;
        use crate::values::generics::ratio::Ratio;
        AspectRatio {
            auto: true,
            ratio: PreferredRatio::Ratio(Ratio(
                NonNegativeNumber::new(w),
                NonNegativeNumber::new(h),
            )),
        }
    }
}

/// A specified value for inset types.
pub type Inset = GenericInset<specified::Percentage, LengthPercentage>;

impl Inset {
    /// Parses an inset type, allowing the unitless length quirk.
    /// <https://quirks.spec.whatwg.org/#the-unitless-length-quirk>
    #[inline]
    pub fn parse_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(l) = input.try_parse(|i| LengthPercentage::parse_quirky(context, i, allow_quirks))
        {
            return Ok(Self::LengthPercentage(l));
        }
        match input.try_parse(|i| i.expect_ident_matching("auto")) {
            Ok(_) => return Ok(Self::Auto),
            Err(e) if !static_prefs::pref!("layout.css.anchor-positioning.enabled") => {
                return Err(e.into());
            },
            Err(_) => (),
        };
        Self::parse_anchor_functions_quirky(context, input, allow_quirks)
    }

    fn parse_as_anchor_function_fallback<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(l) =
            input.try_parse(|i| LengthPercentage::parse_quirky(context, i, AllowQuirks::No))
        {
            return Ok(Self::LengthPercentage(l));
        }
        Self::parse_anchor_functions_quirky(context, input, AllowQuirks::No)
    }

    fn parse_anchor_functions_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError<'i>> {
        debug_assert!(
            static_prefs::pref!("layout.css.anchor-positioning.enabled"),
            "How are we parsing with pref off?"
        );
        if let Ok(inner) = input.try_parse(|i| AnchorFunction::parse(context, i)) {
            return Ok(Self::AnchorFunction(Box::new(inner)));
        }
        if let Ok(inner) =
            input.try_parse(|i| GenericAnchorSizeFunction::<Inset>::parse(context, i))
        {
            return Ok(Self::AnchorSizeFunction(Box::new(inner)));
        }
        Ok(Self::AnchorContainingCalcFunction(input.try_parse(
            |i| LengthPercentage::parse_quirky_with_anchor_functions(context, i, allow_quirks),
        )?))
    }
}

impl Parse for Inset {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_quirky(context, input, AllowQuirks::No)
    }
}

/// A specified value for `anchor()` function.
pub type AnchorFunction = GenericAnchorFunction<specified::Percentage, Inset>;

impl Parse for AnchorFunction {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if !static_prefs::pref!("layout.css.anchor-positioning.enabled") {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        input.expect_function_matching("anchor")?;
        input.parse_nested_block(|i| {
            let target_element = i.try_parse(|i| DashedIdent::parse(context, i)).ok();
            let side = GenericAnchorSide::parse(context, i)?;
            let target_element = if target_element.is_none() {
                i.try_parse(|i| DashedIdent::parse(context, i)).ok()
            } else {
                target_element
            };
            let fallback = i
                .try_parse(|i| {
                    i.expect_comma()?;
                    Inset::parse_as_anchor_function_fallback(context, i)
                })
                .ok();
            Ok(Self {
                target_element: TreeScoped::with_default_level(
                    target_element.unwrap_or_else(DashedIdent::empty),
                ),
                side,
                fallback: fallback.into(),
            })
        })
    }
}
