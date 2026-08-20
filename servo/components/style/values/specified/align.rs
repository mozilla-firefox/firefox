/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Values for CSS Box Alignment properties
//!
//! https://drafts.csswg.org/css-align/

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use cssparser::Parser;
use std::fmt::{self, Write};
use style_traits::{CssWriter, KeywordsCollectFn, ParseError, SpecifiedValueInfo, ToCss};

/// Constants shared by multiple CSS Box Alignment properties
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    Eq,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct AlignFlags(u8);
bitflags! {
    impl AlignFlags: u8 {
        // Enumeration stored in the lower 5 bits:
        /// {align,justify}-{content,items,self}: 'auto'
        const AUTO = 0;
        /// 'normal'
        const NORMAL = 1;
        /// 'start'
        const START = 2;
        /// 'end'
        const END = 3;
        /// 'flex-start'
        const FLEX_START = 4;
        /// 'flex-end'
        const FLEX_END = 5;
        /// 'center'
        const CENTER = 6;
        /// 'left'
        const LEFT = 7;
        /// 'right'
        const RIGHT = 8;
        /// 'baseline'
        const BASELINE = 9;
        /// 'last-baseline'
        const LAST_BASELINE = 10;
        /// 'stretch'
        const STRETCH = 11;
        /// 'self-start'
        const SELF_START = 12;
        /// 'self-end'
        const SELF_END = 13;
        /// 'space-between'
        const SPACE_BETWEEN = 14;
        /// 'space-around'
        const SPACE_AROUND = 15;
        /// 'space-evenly'
        const SPACE_EVENLY = 16;
        /// `anchor-center`
        const ANCHOR_CENTER = 17;

        // Additional flags stored in the upper bits:
        /// 'legacy' (mutually exclusive w. SAFE & UNSAFE)
        const LEGACY = 1 << 5;
        /// 'safe'
        const SAFE = 1 << 6;
        /// 'unsafe' (mutually exclusive w. SAFE)
        const UNSAFE = 1 << 7;

        /// Mask for the additional flags above.
        const FLAG_BITS = 0b11100000;
    }
}

impl AlignFlags {
    /// Returns the enumeration value stored in the lower 5 bits.
    #[inline]
    pub fn value(&self) -> Self {
        *self & !AlignFlags::FLAG_BITS
    }

    /// Returns an updated value with the same flags.
    #[inline]
    pub fn with_value(&self, value: AlignFlags) -> Self {
        debug_assert!(!value.intersects(Self::FLAG_BITS));
        value | self.flags()
    }

    /// Returns the flags stored in the upper 3 bits.
    #[inline]
    pub fn flags(&self) -> Self {
        *self & AlignFlags::FLAG_BITS
    }
}

impl ToCss for AlignFlags {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        let flags = self.flags();
        let value = self.value();
        match flags {
            AlignFlags::LEGACY => {
                dest.write_str("legacy")?;
                if value.is_empty() {
                    return Ok(());
                }
                dest.write_char(' ')?;
            },
            AlignFlags::SAFE => dest.write_str("safe ")?,
            AlignFlags::UNSAFE => dest.write_str("unsafe ")?,
            _ => {
                debug_assert_eq!(flags, AlignFlags::empty());
            },
        }

        dest.write_str(match value {
            AlignFlags::AUTO => "auto",
            AlignFlags::NORMAL => "normal",
            AlignFlags::START => "start",
            AlignFlags::END => "end",
            AlignFlags::FLEX_START => "flex-start",
            AlignFlags::FLEX_END => "flex-end",
            AlignFlags::CENTER => "center",
            AlignFlags::LEFT => "left",
            AlignFlags::RIGHT => "right",
            AlignFlags::BASELINE => "baseline",
            AlignFlags::LAST_BASELINE => "last baseline",
            AlignFlags::STRETCH => "stretch",
            AlignFlags::SELF_START => "self-start",
            AlignFlags::SELF_END => "self-end",
            AlignFlags::SPACE_BETWEEN => "space-between",
            AlignFlags::SPACE_AROUND => "space-around",
            AlignFlags::SPACE_EVENLY => "space-evenly",
            AlignFlags::ANCHOR_CENTER => "anchor-center",
            _ => unreachable!(),
        })
    }
}

/// An axis direction, either inline (for the `justify` properties) or block,
/// (for the `align` properties).
#[derive(Clone, Copy, PartialEq)]
pub enum AxisDirection {
    /// Block direction.
    Block,
    /// Inline direction.
    Inline,
}

/// Shared value for the `align-content` and `justify-content` properties.
///
/// <https://drafts.csswg.org/css-align/#content-distribution>
/// <https://drafts.csswg.org/css-align/#propdef-align-content>
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    Eq,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct ContentDistribution {
    primary: AlignFlags,
    // FIXME(https://github.com/w3c/csswg-drafts/issues/1002): This will need to
    // accept fallback alignment, eventually.
}

impl ContentDistribution {
    /// The initial value 'normal'
    #[inline]
    pub fn normal() -> Self {
        Self::new(AlignFlags::NORMAL)
    }

    /// `start`
    #[inline]
    pub fn start() -> Self {
        Self::new(AlignFlags::START)
    }

    /// The initial value 'normal'
    #[inline]
    pub fn new(primary: AlignFlags) -> Self {
        Self { primary }
    }

    /// Returns whether this value is a <baseline-position>.
    pub fn is_baseline_position(&self) -> bool {
        matches!(
            self.primary.value(),
            AlignFlags::BASELINE | AlignFlags::LAST_BASELINE
        )
    }

    /// The primary alignment
    #[inline]
    pub fn primary(self) -> AlignFlags {
        self.primary
    }

    /// Parse a value for align-content
    pub fn parse_block<'i>(
        _: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse(input, AxisDirection::Block)
    }

    /// Parse a value for justify-content
    pub fn parse_inline<'i>(
        _: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse(input, AxisDirection::Inline)
    }

    fn parse<'i, 't>(
        input: &mut Parser<'i, 't>,
        axis: AxisDirection,
    ) -> Result<Self, ParseError<'i>> {
        // NOTE Please also update the `list_keywords` function below
        //      when this function is updated.

        // Try to parse normal first
        if input
            .try_parse(|i| i.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(ContentDistribution::normal());
        }

        // Parse <baseline-position>, but only on the block axis.
        if axis == AxisDirection::Block {
            if let Ok(value) = input.try_parse(parse_baseline) {
                return Ok(ContentDistribution::new(value));
            }
        }

        // <content-distribution>
        if let Ok(value) = input.try_parse(parse_content_distribution) {
            return Ok(ContentDistribution::new(value));
        }

        // <overflow-position>? <content-position>
        let overflow_position = input
            .try_parse(parse_overflow_position)
            .unwrap_or(AlignFlags::empty());

        let content_position = try_match_ident_ignore_ascii_case! { input,
            "start" => AlignFlags::START,
            "end" => AlignFlags::END,
            "flex-start" => AlignFlags::FLEX_START,
            "flex-end" => AlignFlags::FLEX_END,
            "center" => AlignFlags::CENTER,
            "left" if axis == AxisDirection::Inline => AlignFlags::LEFT,
            "right" if axis == AxisDirection::Inline => AlignFlags::RIGHT,
        };

        Ok(ContentDistribution::new(
            content_position | overflow_position,
        ))
    }
}

impl SpecifiedValueInfo for ContentDistribution {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        f(&["normal"]);
        list_baseline_keywords(f); // block-axis only
        list_content_distribution_keywords(f);
        list_overflow_position_keywords(f);
        f(&["start", "end", "flex-start", "flex-end", "center"]);
        f(&["left", "right"]); // inline-axis only
    }
}

/// The specified value of the {align,justify}-self properties.
///
/// <https://drafts.csswg.org/css-align/#self-alignment>
/// <https://drafts.csswg.org/css-align/#propdef-align-self>
#[derive(
    Clone,
    Copy,
    Debug,
    Deref,
    Deserialize,
    Eq,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct SelfAlignment(pub AlignFlags);

impl SelfAlignment {
    /// The initial value 'auto'
    #[inline]
    pub fn auto() -> Self {
        SelfAlignment(AlignFlags::AUTO)
    }

    /// Returns whether this value is valid for both axis directions.
    pub fn is_valid_on_both_axes(&self) -> bool {
        match self.0.value() {
            // left | right are only allowed on the inline axis.
            AlignFlags::LEFT | AlignFlags::RIGHT => false,

            _ => true,
        }
    }

    /// Parse self-alignment on the block axis (for align-self)
    pub fn parse_block<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse(input, AxisDirection::Block)
    }

    /// Parse self-alignment on the block axis (for align-self)
    pub fn parse_inline<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse(input, AxisDirection::Inline)
    }

    /// Parse a self-alignment value on one of the axes.
    fn parse<'i, 't>(
        input: &mut Parser<'i, 't>,
        axis: AxisDirection,
    ) -> Result<Self, ParseError<'i>> {
        // NOTE Please also update the `list_keywords` function below
        //      when this function is updated.

        // <baseline-position>
        //
        // It's weird that this accepts <baseline-position>, but not
        // justify-content...
        if let Ok(value) = input.try_parse(parse_baseline) {
            return Ok(SelfAlignment(value));
        }

        // auto | normal | stretch
        if let Ok(value) = input.try_parse(parse_auto_normal_stretch) {
            return Ok(SelfAlignment(value));
        }

        // <overflow-position>? <self-position>
        let overflow_position = input
            .try_parse(parse_overflow_position)
            .unwrap_or(AlignFlags::empty());
        let self_position = parse_self_position(input, axis, AllowAnchorCenter::Yes)?;
        Ok(SelfAlignment(overflow_position | self_position))
    }

    fn list_keywords(f: KeywordsCollectFn, axis: AxisDirection) {
        list_baseline_keywords(f);
        list_auto_normal_stretch(f);
        list_overflow_position_keywords(f);
        list_self_position_keywords(f, axis);
    }

    /// Performs a flip of the position, that is, for self-start we return self-end, for left
    /// we return right, etc.
    pub fn flip_position(self) -> Self {
        let flipped_value = match self.0.value() {
            AlignFlags::START => AlignFlags::END,
            AlignFlags::END => AlignFlags::START,
            AlignFlags::FLEX_START => AlignFlags::FLEX_END,
            AlignFlags::FLEX_END => AlignFlags::FLEX_START,
            AlignFlags::LEFT => AlignFlags::RIGHT,
            AlignFlags::RIGHT => AlignFlags::LEFT,
            AlignFlags::SELF_START => AlignFlags::SELF_END,
            AlignFlags::SELF_END => AlignFlags::SELF_START,

            AlignFlags::AUTO
            | AlignFlags::NORMAL
            | AlignFlags::BASELINE
            | AlignFlags::LAST_BASELINE
            | AlignFlags::STRETCH
            | AlignFlags::CENTER
            | AlignFlags::SPACE_BETWEEN
            | AlignFlags::SPACE_AROUND
            | AlignFlags::SPACE_EVENLY
            | AlignFlags::ANCHOR_CENTER => return self,
            _ => {
                debug_assert!(false, "Unexpected alignment enumeration value");
                return self;
            },
        };
        self.with_value(flipped_value)
    }

    /// Returns a fixed-up alignment value.
    #[inline]
    pub fn with_value(self, value: AlignFlags) -> Self {
        Self(self.0.with_value(value))
    }
}

impl SpecifiedValueInfo for SelfAlignment {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        // TODO: This technically lists left/right for align-self. Not amazing but also not sure
        // worth fixing here, could be special-cased on the caller.
        Self::list_keywords(f, AxisDirection::Block);
    }
}

/// Value of the `align-items` and `justify-items` properties
///
/// <https://drafts.csswg.org/css-align/#propdef-align-items>
/// <https://drafts.csswg.org/css-align/#propdef-justify-items>
#[derive(
    Clone,
    Copy,
    Debug,
    Deref,
    Deserialize,
    Eq,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct ItemPlacement(pub AlignFlags);

impl ItemPlacement {
    /// The value 'normal'
    #[inline]
    pub fn normal() -> Self {
        Self(AlignFlags::NORMAL)
    }
}

impl ItemPlacement {
    /// Parse a value for align-items
    pub fn parse_block<'i>(
        _: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse(input, AxisDirection::Block)
    }

    /// Parse a value for justify-items
    pub fn parse_inline<'i>(
        _: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse(input, AxisDirection::Inline)
    }

    fn parse<'i, 't>(
        input: &mut Parser<'i, 't>,
        axis: AxisDirection,
    ) -> Result<Self, ParseError<'i>> {
        // NOTE Please also update `impl SpecifiedValueInfo` below when
        //      this function is updated.

        // <baseline-position>
        if let Ok(baseline) = input.try_parse(parse_baseline) {
            return Ok(Self(baseline));
        }

        // normal | stretch
        if let Ok(value) = input.try_parse(parse_normal_stretch) {
            return Ok(Self(value));
        }

        if axis == AxisDirection::Inline {
            // legacy | [ legacy && [ left | right | center ] ]
            if let Ok(value) = input.try_parse(parse_legacy) {
                return Ok(Self(value));
            }
        }

        // <overflow-position>? <self-position>
        let overflow = input
            .try_parse(parse_overflow_position)
            .unwrap_or(AlignFlags::empty());
        let self_position = parse_self_position(input, axis, AllowAnchorCenter::No)?;
        Ok(ItemPlacement(self_position | overflow))
    }
}

impl SpecifiedValueInfo for ItemPlacement {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        list_baseline_keywords(f);
        list_normal_stretch(f);
        list_overflow_position_keywords(f);
        list_self_position_keywords(f, AxisDirection::Block);
    }
}

/// Value of the `justify-items` property
///
/// <https://drafts.csswg.org/css-align/#justify-items-property>
#[derive(
    Clone,
    Copy,
    Debug,
    Deref,
    Deserialize,
    Eq,
    MallocSizeOf,
    PartialEq,
    Serialize,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
pub struct JustifyItems(pub ItemPlacement);

impl JustifyItems {
    /// The initial value 'legacy'
    #[inline]
    pub fn legacy() -> Self {
        Self(ItemPlacement(AlignFlags::LEGACY))
    }

    /// The value 'normal'
    #[inline]
    pub fn normal() -> Self {
        Self(ItemPlacement::normal())
    }
}

impl Parse for JustifyItems {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        ItemPlacement::parse_inline(context, input).map(Self)
    }
}

impl SpecifiedValueInfo for JustifyItems {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        ItemPlacement::collect_completion_keywords(f);
        list_legacy_keywords(f); // Inline axis only
    }
}

// auto | normal | stretch
fn parse_auto_normal_stretch<'i, 't>(
    input: &mut Parser<'i, 't>,
) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_auto_normal_stretch` function
    //      below when this function is updated.
    try_match_ident_ignore_ascii_case! { input,
        "auto" => Ok(AlignFlags::AUTO),
        "normal" => Ok(AlignFlags::NORMAL),
        "stretch" => Ok(AlignFlags::STRETCH),
    }
}

fn list_auto_normal_stretch(f: KeywordsCollectFn) {
    f(&["auto", "normal", "stretch"]);
}

// normal | stretch
fn parse_normal_stretch<'i, 't>(input: &mut Parser<'i, 't>) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_normal_stretch` function below
    //      when this function is updated.
    try_match_ident_ignore_ascii_case! { input,
        "normal" => Ok(AlignFlags::NORMAL),
        "stretch" => Ok(AlignFlags::STRETCH),
    }
}

fn list_normal_stretch(f: KeywordsCollectFn) {
    f(&["normal", "stretch"]);
}

// <baseline-position>
fn parse_baseline<'i, 't>(input: &mut Parser<'i, 't>) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_baseline_keywords` function
    //      below when this function is updated.
    try_match_ident_ignore_ascii_case! { input,
        "baseline" => Ok(AlignFlags::BASELINE),
        "first" => {
            input.expect_ident_matching("baseline")?;
            Ok(AlignFlags::BASELINE)
        },
        "last" => {
            input.expect_ident_matching("baseline")?;
            Ok(AlignFlags::LAST_BASELINE)
        },
    }
}

fn list_baseline_keywords(f: KeywordsCollectFn) {
    f(&["baseline", "first baseline", "last baseline"]);
}

// <content-distribution>
fn parse_content_distribution<'i, 't>(
    input: &mut Parser<'i, 't>,
) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_content_distribution_keywords`
    //      function below when this function is updated.
    try_match_ident_ignore_ascii_case! { input,
        "stretch" => Ok(AlignFlags::STRETCH),
        "space-between" => Ok(AlignFlags::SPACE_BETWEEN),
        "space-around" => Ok(AlignFlags::SPACE_AROUND),
        "space-evenly" => Ok(AlignFlags::SPACE_EVENLY),
    }
}

fn list_content_distribution_keywords(f: KeywordsCollectFn) {
    f(&["stretch", "space-between", "space-around", "space-evenly"]);
}

// <overflow-position>
fn parse_overflow_position<'i, 't>(
    input: &mut Parser<'i, 't>,
) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_overflow_position_keywords`
    //      function below when this function is updated.
    try_match_ident_ignore_ascii_case! { input,
        "safe" => Ok(AlignFlags::SAFE),
        "unsafe" => Ok(AlignFlags::UNSAFE),
    }
}

fn list_overflow_position_keywords(f: KeywordsCollectFn) {
    f(&["safe", "unsafe"]);
}

enum AllowAnchorCenter {
    No,
    Yes,
}

// <self-position> | left | right in the inline axis.
fn parse_self_position<'i, 't>(
    input: &mut Parser<'i, 't>,
    axis: AxisDirection,
    allow_anchor_center: AllowAnchorCenter,
) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_self_position_keywords`
    //      function below when this function is updated.
    Ok(try_match_ident_ignore_ascii_case! { input,
        "start" => AlignFlags::START,
        "end" => AlignFlags::END,
        "flex-start" => AlignFlags::FLEX_START,
        "flex-end" => AlignFlags::FLEX_END,
        "center" => AlignFlags::CENTER,
        "self-start" => AlignFlags::SELF_START,
        "self-end" => AlignFlags::SELF_END,
        "left" if axis == AxisDirection::Inline => AlignFlags::LEFT,
        "right" if axis == AxisDirection::Inline => AlignFlags::RIGHT,
        "anchor-center" if matches!(allow_anchor_center, AllowAnchorCenter::Yes) => AlignFlags::ANCHOR_CENTER,
    })
}

fn list_self_position_keywords(f: KeywordsCollectFn, axis: AxisDirection) {
    f(&[
        "start",
        "end",
        "flex-start",
        "flex-end",
        "center",
        "self-start",
        "self-end",
        "anchor-center",
    ]);

    if axis == AxisDirection::Inline {
        f(&["left", "right"]);
    }
}

fn parse_left_right_center<'i, 't>(
    input: &mut Parser<'i, 't>,
) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_legacy_keywords` function below
    //      when this function is updated.
    Ok(try_match_ident_ignore_ascii_case! { input,
        "left" => AlignFlags::LEFT,
        "right" => AlignFlags::RIGHT,
        "center" => AlignFlags::CENTER,
    })
}

// legacy | [ legacy && [ left | right | center ] ]
fn parse_legacy<'i, 't>(input: &mut Parser<'i, 't>) -> Result<AlignFlags, ParseError<'i>> {
    // NOTE Please also update the `list_legacy_keywords` function below
    //      when this function is updated.
    let flags = try_match_ident_ignore_ascii_case! { input,
        "legacy" => {
            let flags = input.try_parse(parse_left_right_center)
                .unwrap_or(AlignFlags::empty());

            return Ok(AlignFlags::LEGACY | flags)
        },
        "left" => AlignFlags::LEFT,
        "right" => AlignFlags::RIGHT,
        "center" => AlignFlags::CENTER,
    };

    input.expect_ident_matching("legacy")?;
    Ok(AlignFlags::LEGACY | flags)
}

fn list_legacy_keywords(f: KeywordsCollectFn) {
    f(&["legacy", "left", "right", "center"]);
}
