/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for box properties.

use crate::derives::*;
pub use crate::logical_geometry::WritingModeProperty;
use crate::parser::{Parse, ParserContext};
use crate::properties::{LonghandId, PropertyDeclarationId, PropertyId};
use crate::values::generics::box_::{
    BaselineShiftKeyword, GenericBaselineShift, GenericContainIntrinsicSize, GenericLineClamp,
    GenericOverflowClipMargin, GenericPerspective, OverflowClipMarginBox,
};
use crate::values::specified::length::{LengthPercentage, NonNegativeLength};
use crate::values::specified::{AllowQuirks, Integer, NonNegativeNumberOrPercentage};
use crate::values::CustomIdent;
use cssparser::Parser;
use num_traits::FromPrimitive;
use std::fmt::{self, Write};
use style_traits::{CssWriter, KeywordsCollectFn, ParseError};
use style_traits::{SpecifiedValueInfo, StyleParseErrorKind, ToCss};

#[cfg(not(feature = "servo"))]
fn grid_enabled() -> bool {
    true
}

#[cfg(feature = "servo")]
fn grid_enabled() -> bool {
    static_prefs::pref!("layout.grid.enabled")
}

#[inline]
fn appearance_base_enabled(_context: &ParserContext) -> bool {
    static_prefs::pref!("layout.css.appearance-base.enabled")
}

#[inline]
fn appearance_base_select_enabled(_context: &ParserContext) -> bool {
    static_prefs::pref!("dom.select.customizable_select.enabled")
}

/// The specified value of `overflow-clip-margin`.
pub type OverflowClipMargin = GenericOverflowClipMargin<NonNegativeLength>;

impl Parse for OverflowClipMargin {
    // <visual-box> || <length [0,∞]>
    fn parse<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        use crate::Zero;
        let mut offset = None;
        let mut visual_box = None;
        loop {
            if offset.is_none() {
                offset = input
                    .try_parse(|i| NonNegativeLength::parse(context, i))
                    .ok();
            }
            if visual_box.is_none() {
                visual_box = input.try_parse(OverflowClipMarginBox::parse).ok();
                if visual_box.is_some() {
                    continue;
                }
            }
            break;
        }
        if offset.is_none() && visual_box.is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(Self {
            offset: offset.unwrap_or_else(NonNegativeLength::zero),
            visual_box: visual_box.unwrap_or(OverflowClipMarginBox::PaddingBox),
        })
    }
}

/// Defines an element’s display type, which consists of
/// the two basic qualities of how an element generates boxes
/// <https://drafts.csswg.org/css-display/#propdef-display>
#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, Eq, FromPrimitive, Hash, MallocSizeOf, PartialEq, ToCss, ToShmem)]
#[repr(u8)]
pub enum DisplayOutside {
    None = 0,
    Inline,
    Block,
    TableCaption,
    InternalTable,
    #[cfg(feature = "gecko")]
    InternalRuby,
}

#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, Eq, FromPrimitive, Hash, MallocSizeOf, PartialEq, ToCss, ToShmem)]
#[repr(u8)]
pub enum DisplayInside {
    None = 0,
    Contents,
    Flow,
    FlowRoot,
    Flex,
    Grid,
    Table,
    TableRowGroup,
    TableColumn,
    TableColumnGroup,
    TableHeaderGroup,
    TableFooterGroup,
    TableRow,
    TableCell,
    #[cfg(feature = "gecko")]
    Ruby,
    #[cfg(feature = "gecko")]
    RubyBase,
    #[cfg(feature = "gecko")]
    RubyBaseContainer,
    #[cfg(feature = "gecko")]
    RubyText,
    #[cfg(feature = "gecko")]
    RubyTextContainer,
    #[cfg(feature = "gecko")]
    WebkitBox,
}

impl DisplayInside {
    fn is_valid_for_list_item(self) -> bool {
        match self {
            DisplayInside::Flow => true,
            #[cfg(feature = "gecko")]
            DisplayInside::FlowRoot => true,
            _ => false,
        }
    }

    /// https://drafts.csswg.org/css-display/#inside-model:
    ///     If <display-outside> is omitted, the element’s outside display type defaults to block
    ///     — except for ruby, which defaults to inline.
    fn default_display_outside(self) -> DisplayOutside {
        match self {
            #[cfg(feature = "gecko")]
            DisplayInside::Ruby => DisplayOutside::Inline,
            _ => DisplayOutside::Block,
        }
    }
}

#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
    Hash,
    MallocSizeOf,
    PartialEq,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct Display(u16);

/// Gecko-only impl block for Display (shared stuff later in this file):
#[allow(missing_docs)]
#[allow(non_upper_case_globals)]
impl Display {
    // Our u16 bits are used as follows: LOOOOOOOIIIIIIII
    pub const LIST_ITEM_MASK: u16 = 0b1000000000000000;
    pub const OUTSIDE_MASK: u16 = 0b0111111100000000;
    pub const INSIDE_MASK: u16 = 0b0000000011111111;
    pub const OUTSIDE_SHIFT: u16 = 8;

    /// https://drafts.csswg.org/css-display/#the-display-properties
    /// ::new() inlined so cbindgen can use it
    pub const None: Self =
        Self(((DisplayOutside::None as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::None as u16);
    pub const Contents: Self = Self(
        ((DisplayOutside::None as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Contents as u16,
    );
    pub const Inline: Self =
        Self(((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Flow as u16);
    pub const InlineBlock: Self = Self(
        ((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::FlowRoot as u16,
    );
    pub const Block: Self =
        Self(((DisplayOutside::Block as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Flow as u16);
    #[cfg(feature = "gecko")]
    pub const FlowRoot: Self = Self(
        ((DisplayOutside::Block as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::FlowRoot as u16,
    );
    pub const Flex: Self =
        Self(((DisplayOutside::Block as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Flex as u16);
    pub const InlineFlex: Self =
        Self(((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Flex as u16);
    pub const Grid: Self =
        Self(((DisplayOutside::Block as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Grid as u16);
    pub const InlineGrid: Self =
        Self(((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Grid as u16);
    pub const Table: Self =
        Self(((DisplayOutside::Block as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Table as u16);
    pub const InlineTable: Self = Self(
        ((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Table as u16,
    );
    pub const TableCaption: Self = Self(
        ((DisplayOutside::TableCaption as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Flow as u16,
    );
    #[cfg(feature = "gecko")]
    pub const Ruby: Self =
        Self(((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::Ruby as u16);
    #[cfg(feature = "gecko")]
    pub const WebkitBox: Self = Self(
        ((DisplayOutside::Block as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::WebkitBox as u16,
    );
    #[cfg(feature = "gecko")]
    pub const WebkitInlineBox: Self = Self(
        ((DisplayOutside::Inline as u16) << Self::OUTSIDE_SHIFT) | DisplayInside::WebkitBox as u16,
    );

    // Internal table boxes.

    pub const TableRowGroup: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableRowGroup as u16,
    );
    pub const TableHeaderGroup: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableHeaderGroup as u16,
    );
    pub const TableFooterGroup: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableFooterGroup as u16,
    );
    pub const TableColumn: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableColumn as u16,
    );
    pub const TableColumnGroup: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableColumnGroup as u16,
    );
    pub const TableRow: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableRow as u16,
    );
    pub const TableCell: Self = Self(
        ((DisplayOutside::InternalTable as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::TableCell as u16,
    );

    /// Internal ruby boxes.
    #[cfg(feature = "gecko")]
    pub const RubyBase: Self = Self(
        ((DisplayOutside::InternalRuby as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::RubyBase as u16,
    );
    #[cfg(feature = "gecko")]
    pub const RubyBaseContainer: Self = Self(
        ((DisplayOutside::InternalRuby as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::RubyBaseContainer as u16,
    );
    #[cfg(feature = "gecko")]
    pub const RubyText: Self = Self(
        ((DisplayOutside::InternalRuby as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::RubyText as u16,
    );
    #[cfg(feature = "gecko")]
    pub const RubyTextContainer: Self = Self(
        ((DisplayOutside::InternalRuby as u16) << Self::OUTSIDE_SHIFT)
            | DisplayInside::RubyTextContainer as u16,
    );

    /// Make a raw display value from <display-outside> and <display-inside> values.
    #[inline]
    const fn new(outside: DisplayOutside, inside: DisplayInside) -> Self {
        Self((outside as u16) << Self::OUTSIDE_SHIFT | inside as u16)
    }

    /// Make a display enum value from <display-outside> and <display-inside> values.
    #[inline]
    fn from3(outside: DisplayOutside, inside: DisplayInside, list_item: bool) -> Self {
        let v = Self::new(outside, inside);
        if !list_item {
            return v;
        }
        Self(v.0 | Self::LIST_ITEM_MASK)
    }

    /// Accessor for the <display-inside> value.
    #[inline]
    pub fn inside(&self) -> DisplayInside {
        DisplayInside::from_u16(self.0 & Self::INSIDE_MASK).unwrap()
    }

    /// Accessor for the <display-outside> value.
    #[inline]
    pub fn outside(&self) -> DisplayOutside {
        DisplayOutside::from_u16((self.0 & Self::OUTSIDE_MASK) >> Self::OUTSIDE_SHIFT).unwrap()
    }

    /// Returns the raw underlying u16 value.
    #[inline]
    pub const fn to_u16(&self) -> u16 {
        self.0
    }

    /// Whether this is `display: inline` (or `inline list-item`).
    #[inline]
    pub fn is_inline_flow(&self) -> bool {
        self.outside() == DisplayOutside::Inline && self.inside() == DisplayInside::Flow
    }

    /// Returns whether this `display` value is some kind of list-item.
    #[inline]
    pub const fn is_list_item(&self) -> bool {
        (self.0 & Self::LIST_ITEM_MASK) != 0
    }

    /// Returns whether this `display` value is a ruby level container.
    pub fn is_ruby_level_container(&self) -> bool {
        match *self {
            #[cfg(feature = "gecko")]
            Display::RubyBaseContainer | Display::RubyTextContainer => true,
            _ => false,
        }
    }

    /// Returns whether this `display` value is one of the types for ruby.
    pub fn is_ruby_type(&self) -> bool {
        match self.inside() {
            #[cfg(feature = "gecko")]
            DisplayInside::Ruby
            | DisplayInside::RubyBase
            | DisplayInside::RubyText
            | DisplayInside::RubyBaseContainer
            | DisplayInside::RubyTextContainer => true,
            _ => false,
        }
    }
}

/// Shared Display impl for both Gecko and Servo.
impl Display {
    /// The initial display value.
    #[inline]
    pub fn inline() -> Self {
        Display::Inline
    }

    /// Returns whether this `display` value is the display of a flex or
    /// grid container.
    ///
    /// This is used to implement various style fixups.
    pub fn is_item_container(&self) -> bool {
        match self.inside() {
            DisplayInside::Flex => true,
            DisplayInside::Grid => true,
            _ => false,
        }
    }

    /// Returns whether an element with this display type is a line
    /// participant, which means it may lay its children on the same
    /// line as itself.
    pub fn is_line_participant(&self) -> bool {
        if self.is_inline_flow() {
            return true;
        }
        match *self {
            #[cfg(feature = "gecko")]
            Display::Contents | Display::Ruby | Display::RubyBaseContainer => true,
            _ => false,
        }
    }

    /// Convert this display into an equivalent block display.
    ///
    /// Also used for :root style adjustments.
    pub fn equivalent_block_display(&self, is_root_element: bool) -> Self {
        // Special handling for `contents` and `list-item`s on the root element.
        if is_root_element && (self.is_contents() || self.is_list_item()) {
            return Display::Block;
        }

        match self.outside() {
            DisplayOutside::Inline => {
                let inside = match self.inside() {
                    // `inline-block` blockifies to `block` rather than
                    // `flow-root`, for legacy reasons.
                    DisplayInside::FlowRoot => DisplayInside::Flow,
                    inside => inside,
                };
                Display::from3(DisplayOutside::Block, inside, self.is_list_item())
            },
            DisplayOutside::Block | DisplayOutside::None => *self,
            _ => Display::Block,
        }
    }

    /// Convert this display into an equivalent inline-outside display.
    /// https://drafts.csswg.org/css-display/#inlinify
    #[cfg(feature = "gecko")]
    pub fn inlinify(&self) -> Self {
        match self.outside() {
            DisplayOutside::Block => {
                let inside = match self.inside() {
                    // `display: block` inlinifies to `display: inline-block`,
                    // rather than `inline`, for legacy reasons.
                    DisplayInside::Flow => DisplayInside::FlowRoot,
                    inside => inside,
                };
                Display::from3(DisplayOutside::Inline, inside, self.is_list_item())
            },
            _ => *self,
        }
    }

    /// Returns true if the value is `Contents`
    #[inline]
    pub fn is_contents(&self) -> bool {
        match *self {
            Display::Contents => true,
            _ => false,
        }
    }

    /// Returns true if the value is `None`
    #[inline]
    pub fn is_none(&self) -> bool {
        *self == Display::None
    }
}

enum DisplayKeyword {
    Full(Display),
    Inside(DisplayInside),
    Outside(DisplayOutside),
    ListItem,
}

impl DisplayKeyword {
    fn parse<'i>(input: &mut Parser<'i, '_>) -> Result<Self, ParseError<'i>> {
        use self::DisplayKeyword::*;
        Ok(try_match_ident_ignore_ascii_case! { input,
            "none" => Full(Display::None),
            "contents" => Full(Display::Contents),
            "inline-block" => Full(Display::InlineBlock),
            "inline-table" => Full(Display::InlineTable),
            "-webkit-flex" => Full(Display::Flex),
            "inline-flex" | "-webkit-inline-flex" => Full(Display::InlineFlex),
            "inline-grid" if grid_enabled() => Full(Display::InlineGrid),
            "table-caption" => Full(Display::TableCaption),
            "table-row-group" => Full(Display::TableRowGroup),
            "table-header-group" => Full(Display::TableHeaderGroup),
            "table-footer-group" => Full(Display::TableFooterGroup),
            "table-column" => Full(Display::TableColumn),
            "table-column-group" => Full(Display::TableColumnGroup),
            "table-row" => Full(Display::TableRow),
            "table-cell" => Full(Display::TableCell),
            #[cfg(feature = "gecko")]
            "ruby-base" => Full(Display::RubyBase),
            #[cfg(feature = "gecko")]
            "ruby-base-container" => Full(Display::RubyBaseContainer),
            #[cfg(feature = "gecko")]
            "ruby-text" => Full(Display::RubyText),
            #[cfg(feature = "gecko")]
            "ruby-text-container" => Full(Display::RubyTextContainer),
            #[cfg(feature = "gecko")]
            "-webkit-box" => Full(Display::WebkitBox),
            #[cfg(feature = "gecko")]
            "-webkit-inline-box" => Full(Display::WebkitInlineBox),

            /// <display-outside> = block | inline | run-in
            /// https://drafts.csswg.org/css-display/#typedef-display-outside
            "block" => Outside(DisplayOutside::Block),
            "inline" => Outside(DisplayOutside::Inline),

            "list-item" => ListItem,

            /// <display-inside> = flow | flow-root | table | flex | grid | ruby
            /// https://drafts.csswg.org/css-display/#typedef-display-inside
            "flow" => Inside(DisplayInside::Flow),
            "flex" => Inside(DisplayInside::Flex),
            "flow-root" => Inside(DisplayInside::FlowRoot),
            "table" => Inside(DisplayInside::Table),
            "grid" if grid_enabled() => Inside(DisplayInside::Grid),
            #[cfg(feature = "gecko")]
            "ruby" => Inside(DisplayInside::Ruby),
        })
    }
}

impl ToCss for Display {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        let outside = self.outside();
        let inside = self.inside();
        match *self {
            Display::Block | Display::Inline => outside.to_css(dest),
            Display::InlineBlock => dest.write_str("inline-block"),
            #[cfg(feature = "gecko")]
            Display::WebkitInlineBox => dest.write_str("-webkit-inline-box"),
            Display::TableCaption => dest.write_str("table-caption"),
            _ => match (outside, inside) {
                (DisplayOutside::Inline, DisplayInside::Grid) => dest.write_str("inline-grid"),
                (DisplayOutside::Inline, DisplayInside::Flex) => dest.write_str("inline-flex"),
                (DisplayOutside::Inline, DisplayInside::Table) => dest.write_str("inline-table"),
                #[cfg(feature = "gecko")]
                (DisplayOutside::Block, DisplayInside::Ruby) => dest.write_str("block ruby"),
                (_, inside) => {
                    if self.is_list_item() {
                        if outside != DisplayOutside::Block {
                            outside.to_css(dest)?;
                            dest.write_char(' ')?;
                        }
                        if inside != DisplayInside::Flow {
                            inside.to_css(dest)?;
                            dest.write_char(' ')?;
                        }
                        dest.write_str("list-item")
                    } else {
                        inside.to_css(dest)
                    }
                },
            },
        }
    }
}

impl Parse for Display {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Display, ParseError<'i>> {
        let mut got_list_item = false;
        let mut inside = None;
        let mut outside = None;
        match DisplayKeyword::parse(input)? {
            DisplayKeyword::Full(d) => return Ok(d),
            DisplayKeyword::Outside(o) => {
                outside = Some(o);
            },
            DisplayKeyword::Inside(i) => {
                inside = Some(i);
            },
            DisplayKeyword::ListItem => {
                got_list_item = true;
            },
        };

        while let Ok(kw) = input.try_parse(DisplayKeyword::parse) {
            match kw {
                DisplayKeyword::ListItem if !got_list_item => {
                    got_list_item = true;
                },
                DisplayKeyword::Outside(o) if outside.is_none() => {
                    outside = Some(o);
                },
                DisplayKeyword::Inside(i) if inside.is_none() => {
                    inside = Some(i);
                },
                _ => return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
            }
        }

        let inside = inside.unwrap_or(DisplayInside::Flow);
        let outside = outside.unwrap_or_else(|| inside.default_display_outside());
        if got_list_item && !inside.is_valid_for_list_item() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        return Ok(Display::from3(outside, inside, got_list_item));
    }
}

impl SpecifiedValueInfo for Display {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        f(&[
            "block",
            "contents",
            "flex",
            "flow-root",
            "flow-root list-item",
            "grid",
            "inline",
            "inline-block",
            "inline-flex",
            "inline-grid",
            "inline-table",
            "inline list-item",
            "inline flow-root list-item",
            "list-item",
            "none",
            "block ruby",
            "ruby",
            "ruby-base",
            "ruby-base-container",
            "ruby-text",
            "ruby-text-container",
            "table",
            "table-caption",
            "table-cell",
            "table-column",
            "table-column-group",
            "table-footer-group",
            "table-header-group",
            "table-row",
            "table-row-group",
            "-webkit-box",
            "-webkit-inline-box",
        ]);
    }
}

/// A specified value for the `contain-intrinsic-size` property.
pub type ContainIntrinsicSize = GenericContainIntrinsicSize<NonNegativeLength>;

/// A specified value for the `line-clamp` property.
pub type LineClamp = GenericLineClamp<Integer>;

/// A specified value for the `baseline-shift` property.
pub type BaselineShift = GenericBaselineShift<LengthPercentage>;

impl Parse for BaselineShift {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(lp) =
            input.try_parse(|i| LengthPercentage::parse_quirky(context, i, AllowQuirks::Yes))
        {
            return Ok(BaselineShift::Length(lp));
        }

        Ok(BaselineShift::Keyword(BaselineShiftKeyword::parse(input)?))
    }
}

/// A specified value for the `dominant-baseline` property.
/// https://drafts.csswg.org/css-inline-3/#dominant-baseline
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
    Hash,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToShmem,
    ToComputedValue,
    ToResolvedValue,
    ToTyped,
)]
#[repr(u8)]
pub enum DominantBaseline {
    /// Equivalent to 'alphabetic' in horizontal writing modes and in vertical writing
    /// modes when 'text-orientation' is sideways. Equivalent to 'central' in vertical
    /// writing modes when 'text-orientation' is 'mixed' or 'upright'.
    Auto,
    /// Use the text-under baseline.
    #[parse(aliases = "text-after-edge")]
    TextBottom,
    /// Use the alphabetic baseline.
    Alphabetic,
    /// Use the ideographic-under baseline.
    Ideographic,
    /// In general, use the x-middle baselines; except under text-orientation: upright
    /// (where the alphabetic and x-height baselines are essentially meaningless) use
    /// the central baseline instead.
    Middle,
    /// Use the central baseline.
    Central,
    /// Use the math baseline.
    Mathematical,
    /// Use the hanging baseline.
    Hanging,
    /// Use the text-over baseline.
    #[parse(aliases = "text-before-edge")]
    TextTop,
}

/// A specified value for the `alignment-baseline` property.
/// https://drafts.csswg.org/css-inline-3/#alignment-baseline
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
    Hash,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToShmem,
    ToComputedValue,
    ToResolvedValue,
    ToTyped,
)]
#[repr(u8)]
pub enum AlignmentBaseline {
    /// Use the dominant baseline choice of the parent.
    Baseline,
    /// Use the text-under baseline.
    TextBottom,
    /// Use the alphabetic baseline.
    /// TODO: Bug 2010717 - Remove css(skip) to support alignment-baseline: alphabetic
    #[css(skip)]
    Alphabetic,
    /// Use the ideographic-under baseline.
    /// TODO: Bug 2010718 - Remove css(skip) support alignment-baseline: ideographic
    #[css(skip)]
    Ideographic,
    /// In general, use the x-middle baselines; except under text-orientation: upright
    /// (where the alphabetic and x-height baselines are essentially meaningless) use
    /// the central baseline instead.
    Middle,
    /// Use the central baseline.
    /// TODO: Bug 2010719 - Remove css(skip) to support alignment-baseline: central
    #[css(skip)]
    Central,
    /// Use the math baseline.
    /// TODO: Bug 2010720 - Remove css(skip) to support alignment-baseline: mathematical
    #[css(skip)]
    Mathematical,
    /// Use the hanging baseline.
    /// TODO: Bug 2017197 - Remove css(skip) to support alignment-baseline: hanging
    #[css(skip)]
    Hanging,
    /// Use the text-over baseline.
    TextTop,
    /// Used to implement the deprecated "align=middle" attribute for HTML img elements.
    #[cfg(feature = "gecko")]
    MozMiddleWithBaseline,
}

/// A specified value for the `baseline-source` property.
/// https://drafts.csswg.org/css-inline-3/#baseline-source
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
    ToCss,
    ToShmem,
    ToComputedValue,
    ToResolvedValue,
    ToTyped,
)]
#[repr(u8)]
pub enum BaselineSource {
    /// `Last` for `inline-block`, `First` otherwise.
    Auto,
    /// Use first baseline for alignment.
    First,
    /// Use last baseline for alignment.
    Last,
}

impl BaselineSource {
    /// Parse baseline source, but without the auto keyword, for the shorthand.
    pub fn parse_non_auto<'i>(input: &mut Parser<'i, '_>) -> Result<Self, ParseError<'i>> {
        Ok(try_match_ident_ignore_ascii_case! { input,
            "first" => Self::First,
            "last" => Self::Last,
        })
    }
}

/// https://drafts.csswg.org/css-scroll-snap-1/#snap-axis
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
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
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum ScrollSnapAxis {
    X,
    Y,
    Block,
    Inline,
    Both,
}

/// https://drafts.csswg.org/css-scroll-snap-1/#snap-strictness
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
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
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum ScrollSnapStrictness {
    #[css(skip)]
    None, // Used to represent scroll-snap-type: none.  It's not parsed.
    Mandatory,
    Proximity,
}

/// https://drafts.csswg.org/css-scroll-snap-1/#scroll-snap-type
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct ScrollSnapType {
    axis: ScrollSnapAxis,
    strictness: ScrollSnapStrictness,
}

impl ScrollSnapType {
    /// Returns `none`.
    #[inline]
    pub fn none() -> Self {
        Self {
            axis: ScrollSnapAxis::Both,
            strictness: ScrollSnapStrictness::None,
        }
    }
}

impl Parse for ScrollSnapType {
    /// none | [ x | y | block | inline | both ] [ mandatory | proximity ]?
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("none"))
            .is_ok()
        {
            return Ok(ScrollSnapType::none());
        }

        let axis = ScrollSnapAxis::parse(input)?;
        let strictness = input
            .try_parse(ScrollSnapStrictness::parse)
            .unwrap_or(ScrollSnapStrictness::Proximity);
        Ok(Self { axis, strictness })
    }
}

impl ToCss for ScrollSnapType {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.strictness == ScrollSnapStrictness::None {
            return dest.write_str("none");
        }
        self.axis.to_css(dest)?;
        if self.strictness != ScrollSnapStrictness::Proximity {
            dest.write_char(' ')?;
            self.strictness.to_css(dest)?;
        }
        Ok(())
    }
}

/// Specified value of scroll-snap-align keyword value.
#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
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
#[repr(u8)]
pub enum ScrollSnapAlignKeyword {
    None,
    Start,
    End,
    Center,
}

/// https://drafts.csswg.org/css-scroll-snap-1/#scroll-snap-align
#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct ScrollSnapAlign {
    block: ScrollSnapAlignKeyword,
    inline: ScrollSnapAlignKeyword,
}

impl ScrollSnapAlign {
    /// Returns `none`.
    #[inline]
    pub fn none() -> Self {
        ScrollSnapAlign {
            block: ScrollSnapAlignKeyword::None,
            inline: ScrollSnapAlignKeyword::None,
        }
    }
}

impl Parse for ScrollSnapAlign {
    /// [ none | start | end | center ]{1,2}
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<ScrollSnapAlign, ParseError<'i>> {
        let block = ScrollSnapAlignKeyword::parse(input)?;
        let inline = input
            .try_parse(ScrollSnapAlignKeyword::parse)
            .unwrap_or(block);
        Ok(ScrollSnapAlign { block, inline })
    }
}

impl ToCss for ScrollSnapAlign {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.block.to_css(dest)?;
        if self.block != self.inline {
            dest.write_char(' ')?;
            self.inline.to_css(dest)?;
        }
        Ok(())
    }
}

#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
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
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum ScrollSnapStop {
    Normal,
    Always,
}

#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
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
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum OverscrollBehavior {
    Auto,
    Contain,
    None,
}

#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
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
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum OverflowAnchor {
    Auto,
    None,
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
/// Provides a rendering hint to the user agent, stating what kinds of changes
/// the author expects to perform on the element.
///
/// `auto` is represented by an empty `features` list.
///
/// <https://drafts.csswg.org/css-will-change/#will-change>
pub struct WillChange {
    /// The features that are supposed to change.
    ///
    /// TODO(emilio): Consider using ArcSlice since we just clone them from the
    /// specified value? That'd save an allocation, which could be worth it.
    #[css(iterable, if_empty = "auto")]
    features: crate::OwnedSlice<CustomIdent>,
    /// A bitfield with the kind of change that the value will create, based
    /// on the above field.
    #[css(skip)]
    pub bits: WillChangeBits,
}

impl WillChange {
    #[inline]
    /// Get default value of `will-change` as `auto`
    pub fn auto() -> Self {
        Self::default()
    }
}

/// The change bits that we care about.
#[derive(
    Clone,
    Copy,
    Debug,
    Default,
    Eq,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct WillChangeBits(u16);
bitflags! {
    impl WillChangeBits: u16 {
        /// Whether a property which can create a stacking context **on any
        /// box** will change.
        const STACKING_CONTEXT_UNCONDITIONAL = 1 << 0;
        /// Whether `transform` or related properties will change.
        const TRANSFORM = 1 << 1;
        /// Whether `scroll-position` will change.
        const SCROLL = 1 << 2;
        /// Whether `contain` will change.
        const CONTAIN = 1 << 3;
        /// Whether `opacity` will change.
        const OPACITY = 1 << 4;
        /// Whether `perspective` will change.
        const PERSPECTIVE = 1 << 5;
        /// Whether `z-index` will change.
        const Z_INDEX = 1 << 6;
        /// Whether any property which creates a containing block for non-svg
        /// text frames will change.
        const FIXPOS_CB_NON_SVG = 1 << 7;
        /// Whether the position property will change.
        const POSITION = 1 << 8;
        /// Whether the view-transition-name property will change.
        const VIEW_TRANSITION_NAME = 1 << 9;
        /// Whether any property which establishes a backdrop-root will change.
        /// See https://drafts.fxtf.org/filter-effects-2/#BackdropFilterProperty
        const BACKDROP_ROOT = 1 << 10;
    }
}

fn change_bits_for_longhand(longhand: LonghandId) -> WillChangeBits {
    match longhand {
        LonghandId::Opacity => WillChangeBits::OPACITY | WillChangeBits::BACKDROP_ROOT,
        LonghandId::Contain => WillChangeBits::CONTAIN,
        LonghandId::Perspective => WillChangeBits::PERSPECTIVE,
        LonghandId::Position => {
            WillChangeBits::STACKING_CONTEXT_UNCONDITIONAL | WillChangeBits::POSITION
        },
        LonghandId::ZIndex => WillChangeBits::Z_INDEX,
        LonghandId::Transform
        | LonghandId::TransformStyle
        | LonghandId::Translate
        | LonghandId::Rotate
        | LonghandId::Scale
        | LonghandId::OffsetPath => WillChangeBits::TRANSFORM,
        LonghandId::Filter | LonghandId::BackdropFilter => {
            WillChangeBits::STACKING_CONTEXT_UNCONDITIONAL
                | WillChangeBits::BACKDROP_ROOT
                | WillChangeBits::FIXPOS_CB_NON_SVG
        },
        LonghandId::ViewTransitionName => {
            WillChangeBits::VIEW_TRANSITION_NAME | WillChangeBits::BACKDROP_ROOT
        },
        LonghandId::MixBlendMode => {
            WillChangeBits::STACKING_CONTEXT_UNCONDITIONAL | WillChangeBits::BACKDROP_ROOT
        },
        LonghandId::Isolation | LonghandId::MaskImage => {
            WillChangeBits::STACKING_CONTEXT_UNCONDITIONAL
        },
        LonghandId::ClipPath => {
            WillChangeBits::STACKING_CONTEXT_UNCONDITIONAL | WillChangeBits::BACKDROP_ROOT
        },
        _ => WillChangeBits::empty(),
    }
}

fn change_bits_for_maybe_property(ident: &str, context: &ParserContext) -> WillChangeBits {
    let id = match PropertyId::parse_ignoring_rule_type(ident, context) {
        Ok(id) => id,
        Err(..) => return WillChangeBits::empty(),
    };

    match id.as_shorthand() {
        Ok(shorthand) => shorthand
            .longhands()
            .fold(WillChangeBits::empty(), |flags, p| {
                flags | change_bits_for_longhand(p)
            }),
        Err(PropertyDeclarationId::Longhand(longhand)) => change_bits_for_longhand(longhand),
        Err(PropertyDeclarationId::Custom(..)) => WillChangeBits::empty(),
    }
}

impl Parse for WillChange {
    /// auto | <animateable-feature>#
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("auto"))
            .is_ok()
        {
            return Ok(Self::default());
        }

        let mut bits = WillChangeBits::empty();
        let custom_idents = input.parse_comma_separated(|i| {
            let location = i.current_source_location();
            let parser_ident = i.expect_ident()?;
            let ident = CustomIdent::from_ident(
                location,
                parser_ident,
                &["will-change", "none", "all", "auto"],
            )?;

            if context.in_ua_sheet() && ident.0 == atom!("-moz-fixed-pos-containing-block") {
                bits |= WillChangeBits::FIXPOS_CB_NON_SVG;
            } else if ident.0 == atom!("scroll-position") {
                bits |= WillChangeBits::SCROLL;
            } else {
                bits |= change_bits_for_maybe_property(&parser_ident, context);
            }
            Ok(ident)
        })?;

        Ok(Self {
            features: custom_idents.into(),
            bits,
        })
    }
}

/// Values for the `touch-action` property.
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
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(single = "none,auto,manipulation", mixed = "pan-x,pan-y,pinch-zoom"))]
#[repr(C)]
pub struct TouchAction(u8);
bitflags! {
    impl TouchAction: u8 {
        /// `none` variant
        const NONE = 1 << 0;
        /// `auto` variant
        const AUTO = 1 << 1;
        /// `pan-x` variant
        const PAN_X = 1 << 2;
        /// `pan-y` variant
        const PAN_Y = 1 << 3;
        /// `manipulation` variant
        const MANIPULATION = 1 << 4;
        /// `pinch-zoom` variant
        const PINCH_ZOOM = 1 << 5;
    }
}

impl TouchAction {
    #[inline]
    /// Get default `touch-action` as `auto`
    pub fn auto() -> TouchAction {
        TouchAction::AUTO
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
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    single = "none,strict,content",
    mixed = "size,layout,style,paint,inline-size",
    overlapping_bits
))]
#[repr(C)]
/// Constants for contain: https://drafts.csswg.org/css-contain/#contain-property
pub struct Contain(u8);
bitflags! {
    impl Contain: u8 {
        /// `none` variant, just for convenience.
        const NONE = 0;
        /// `inline-size` variant, turns on single-axis inline size containment
        const INLINE_SIZE = 1 << 0;
        /// `block-size` variant, turns on single-axis block size containment, internal only
        const BLOCK_SIZE = 1 << 1;
        /// `layout` variant, turns on layout containment
        const LAYOUT = 1 << 2;
        /// `style` variant, turns on style containment
        const STYLE = 1 << 3;
        /// `paint` variant, turns on paint containment
        const PAINT = 1 << 4;
        /// 'size' variant, turns on size containment
        const SIZE = 1 << 5 | Contain::INLINE_SIZE.bits() | Contain::BLOCK_SIZE.bits();
        /// `content` variant, turns on layout and paint containment
        const CONTENT = 1 << 6 | Contain::LAYOUT.bits() | Contain::STYLE.bits() | Contain::PAINT.bits();
        /// `strict` variant, turns on all types of containment
        const STRICT = 1 << 7 | Contain::LAYOUT.bits() | Contain::STYLE.bits() | Contain::PAINT.bits() | Contain::SIZE.bits();
    }
}

impl Parse for ContainIntrinsicSize {
    /// none | <length> | auto <length>
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(l) = input.try_parse(|i| NonNegativeLength::parse(context, i)) {
            return Ok(Self::Length(l));
        }

        if input.try_parse(|i| i.expect_ident_matching("auto")).is_ok() {
            if input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
                return Ok(Self::AutoNone);
            }

            let l = NonNegativeLength::parse(context, input)?;
            return Ok(Self::AutoLength(l));
        }

        input.expect_ident_matching("none")?;
        Ok(Self::None)
    }
}

impl Parse for LineClamp {
    /// none | <positive-integer>
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(i) =
            input.try_parse(|i| crate::values::specified::PositiveInteger::parse(context, i))
        {
            return Ok(Self(i.0));
        }
        input.expect_ident_matching("none")?;
        Ok(Self::none())
    }
}

/// https://drafts.csswg.org/css-contain-2/#content-visibility
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum ContentVisibility {
    /// `auto` variant, the element turns on layout containment, style containment, and paint
    /// containment. In addition, if the element is not relevant to the user (such as by being
    /// offscreen) it also skips its content
    Auto,
    /// `hidden` variant, the element skips its content
    Hidden,
    /// 'visible' variant, no effect
    Visible,
}

#[derive(
    Clone,
    Copy,
    Debug,
    PartialEq,
    Eq,
    MallocSizeOf,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    Parse,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    single = "normal",
    mixed = "size,inline-size,scroll-state",
    validate_mixed = "Self::validate_mixed_flags",
))]
#[repr(C)]
/// Specified keyword values for the container-type property.
/// Spec: normal | [ [ size | inline-size ] || scroll-state ]
///
/// Container Queries are moved from css-contain-3 to css-conditional-5 in August 2022:
/// https://drafts.csswg.org/css-contain-3/#container-type
/// https://drafts.csswg.org/css-conditional-5/#container-type
pub struct ContainerType(u8);
bitflags! {
    impl ContainerType: u8 {
        /// The `normal` variant.
        const NORMAL = 0;
        /// The `inline-size` variant.
        const INLINE_SIZE = 1 << 0;
        /// The `size` variant.
        const SIZE = 1 << 1;
        /// The `scroll-state` variant.
        const SCROLL_STATE = 1 << 2;
    }
}

impl ContainerType {
    fn validate_mixed_flags(&self) -> bool {
        // size and inline-size can't be mixed together.
        if self.contains(Self::SIZE | Self::INLINE_SIZE) {
            return false;
        }
        if self.contains(Self::SCROLL_STATE)
            && !static_prefs::pref!("layout.css.scroll-state.enabled")
        {
            return false;
        }
        true
    }

    /// Is this container-type: normal?
    pub fn is_normal(self) -> bool {
        self == Self::NORMAL
    }

    /// Is this type containing size in any way?
    pub fn is_size_container_type(self) -> bool {
        self.intersects(Self::SIZE | Self::INLINE_SIZE)
    }
}

/// https://drafts.csswg.org/css-contain-3/#container-name
#[repr(transparent)]
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
pub struct ContainerName(#[css(iterable, if_empty = "none")] pub crate::OwnedSlice<CustomIdent>);

impl ContainerName {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self(Default::default())
    }

    /// Returns whether this is the `none` value.
    pub fn is_none(&self) -> bool {
        self.0.is_empty()
    }

    fn parse_internal<'i>(
        input: &mut Parser<'i, '_>,
        for_query: bool,
    ) -> Result<Self, ParseError<'i>> {
        let mut idents = vec![];
        let location = input.current_source_location();
        let first = input.expect_ident()?;
        if !for_query && first.eq_ignore_ascii_case("none") {
            return Ok(Self::none());
        }
        const DISALLOWED_CONTAINER_NAMES: &'static [&'static str] = &["none", "not", "or", "and"];
        idents.push(CustomIdent::from_ident(
            location,
            first,
            DISALLOWED_CONTAINER_NAMES,
        )?);
        if !for_query {
            while let Ok(name) =
                input.try_parse(|input| CustomIdent::parse(input, DISALLOWED_CONTAINER_NAMES))
            {
                idents.push(name);
            }
        }
        Ok(ContainerName(idents.into()))
    }

    /// https://github.com/w3c/csswg-drafts/issues/7203
    /// Only a single name allowed in @container rule.
    /// Disallow none for container-name in @container rule.
    pub fn parse_for_query<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(input, /* for_query = */ true)
    }
}

impl Parse for ContainerName {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(input, /* for_query = */ false)
    }
}

/// A specified value for the `perspective` property.
pub type Perspective = GenericPerspective<NonNegativeLength>;

/// https://drafts.csswg.org/css-box/#propdef-float
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
    Hash,
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
pub enum Float {
    Left,
    Right,
    None,
    // https://drafts.csswg.org/css-logical-props/#float-clear
    InlineStart,
    InlineEnd,
}

impl Float {
    /// Returns true if `self` is not `None`.
    pub fn is_floating(self) -> bool {
        self != Self::None
    }
}

/// https://drafts.csswg.org/css2/#propdef-clear
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    FromPrimitive,
    Hash,
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
pub enum Clear {
    None,
    Left,
    Right,
    Both,
    // https://drafts.csswg.org/css-logical-props/#float-clear
    InlineStart,
    InlineEnd,
}

/// https://drafts.csswg.org/css-ui/#propdef-resize
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
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
    ToCss,
    ToShmem,
    ToTyped,
)]
pub enum Resize {
    None,
    Both,
    Horizontal,
    Vertical,
    // https://drafts.csswg.org/css-logical-1/#resize
    Inline,
    Block,
}

/// The value for the `appearance` property.
///
/// https://developer.mozilla.org/en-US/docs/Web/CSS/-moz-appearance
#[allow(missing_docs)]
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
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum Appearance {
    /// No appearance at all.
    None,
    /// Default appearance for the element.
    ///
    /// This value doesn't make sense for -moz-default-appearance, but we don't bother to guard
    /// against parsing it.
    Auto,
    /// A searchfield.
    Searchfield,
    /// A multi-line text field, e.g. HTML <textarea>.
    Textarea,
    /// A checkbox element.
    Checkbox,
    /// A radio element within a radio group.
    Radio,
    /// A dropdown list.
    Menulist,
    /// List boxes.
    Listbox,
    /// A horizontal meter bar.
    Meter,
    /// A horizontal progress bar.
    ProgressBar,
    /// A typical dialog button.
    Button,
    /// A single-line text field, e.g. HTML <input type=text>.
    Textfield,
    /// The dropdown button(s) that open up a dropdown list.
    MenulistButton,
    /// https://drafts.csswg.org/css-forms/#appearance
    #[parse(condition = "appearance_base_enabled")]
    Base,
    /// Only relevant to the <select> element and ::picker(select) pseudo-element, allowing them to
    /// be styled.
    #[parse(condition = "appearance_base_select_enabled")]
    BaseSelect,
    /// Menu Popup background.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    Menupopup,
    /// The "arrowed" part of the dropdown button that open up a dropdown list.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozMenulistArrowButton,
    /// For HTML's <input type=number>
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    NumberInput,
    /// For HTML's <input type=password>
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    PasswordInput,
    /// nsRangeFrame and its subparts
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    Range,
    /// The scrollbar slider
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarHorizontal,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarVertical,
    /// A scrollbar button (up/down/left/right).
    /// Keep these in order (some code casts these values to `int` in order to
    /// compare them against each other).
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarbuttonUp,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarbuttonDown,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarbuttonLeft,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarbuttonRight,
    /// The scrollbar thumb.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarthumbHorizontal,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    ScrollbarthumbVertical,
    /// The scroll corner
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    Scrollcorner,
    /// The up button of a spin control.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    SpinnerUpbutton,
    /// The down button of a spin control.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    SpinnerDownbutton,
    /// A single toolbar button (with no associated dropdown).
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    Toolbarbutton,
    /// A tooltip.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    Tooltip,

    /// Sidebar appearance.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozSidebar,

    /// Mac help button.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozMacHelpButton,

    /// An appearance value for the root, so that we can get tinting and unified toolbar looks
    /// (which require a transparent gecko background) without really using the whole transparency
    /// set-up which otherwise loses window borders, see bug 1870481.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozMacWindow,

    /// Windows themed window frame elements.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowButtonBox,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowButtonClose,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowButtonMaximize,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowButtonMinimize,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowButtonRestore,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowTitlebar,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowTitlebarMaximized,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozWindowDecorations,

    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozMacDisclosureButtonClosed,
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozMacDisclosureButtonOpen,

    /// A themed focus outline (for outline:auto).
    ///
    /// This isn't exposed to CSS at all, just here for convenience.
    #[css(skip)]
    FocusOutline,

    /// A dummy variant that should be last to let the GTK widget do hackery.
    #[css(skip)]
    Count,
}

/// A kind of break between two boxes.
///
/// https://drafts.csswg.org/css-break/#break-between
#[allow(missing_docs)]
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
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum BreakBetween {
    Always,
    Auto,
    Page,
    Avoid,
    Left,
    Right,
}

impl BreakBetween {
    /// Parse a legacy break-between value for `page-break-{before,after}`.
    ///
    /// See https://drafts.csswg.org/css-break/#page-break-properties.
    #[cfg_attr(feature = "servo", allow(unused))]
    #[inline]
    pub(crate) fn parse_legacy<'i>(
        _: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        let break_value = BreakBetween::parse(input)?;
        match break_value {
            BreakBetween::Always => Ok(BreakBetween::Page),
            BreakBetween::Auto | BreakBetween::Avoid | BreakBetween::Left | BreakBetween::Right => {
                Ok(break_value)
            },
            BreakBetween::Page => {
                Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
            },
        }
    }

    /// Serialize a legacy break-between value for `page-break-*`.
    ///
    /// See https://drafts.csswg.org/css-break/#page-break-properties.
    #[cfg_attr(feature = "servo", allow(unused))]
    pub(crate) fn to_css_legacy<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            BreakBetween::Auto | BreakBetween::Avoid | BreakBetween::Left | BreakBetween::Right => {
                self.to_css(dest)
            },
            BreakBetween::Page => dest.write_str("always"),
            BreakBetween::Always => Ok(()),
        }
    }
}

/// A kind of break within a box.
///
/// https://drafts.csswg.org/css-break/#break-within
#[allow(missing_docs)]
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
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum BreakWithin {
    Auto,
    Avoid,
    AvoidPage,
    AvoidColumn,
}

impl BreakWithin {
    /// Parse a legacy break-between value for `page-break-inside`.
    ///
    /// See https://drafts.csswg.org/css-break/#page-break-properties.
    #[cfg_attr(feature = "servo", allow(unused))]
    #[inline]
    pub(crate) fn parse_legacy<'i>(
        _: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Self, ParseError<'i>> {
        let break_value = BreakWithin::parse(input)?;
        match break_value {
            BreakWithin::Auto | BreakWithin::Avoid => Ok(break_value),
            BreakWithin::AvoidPage | BreakWithin::AvoidColumn => {
                Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
            },
        }
    }

    /// Serialize a legacy break-between value for `page-break-inside`.
    ///
    /// See https://drafts.csswg.org/css-break/#page-break-properties.
    #[cfg_attr(feature = "servo", allow(unused))]
    pub(crate) fn to_css_legacy<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            BreakWithin::Auto | BreakWithin::Avoid => self.to_css(dest),
            BreakWithin::AvoidPage | BreakWithin::AvoidColumn => Ok(()),
        }
    }
}

/// The value for the `overflow-x` / `overflow-y` properties.
#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum Overflow {
    Visible,
    Hidden,
    Scroll,
    Auto,
    Clip,
}

// This can be derived once we remove or keep `-moz-hidden-unscrollable`
// indefinitely.
impl Parse for Overflow {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(try_match_ident_ignore_ascii_case! { input,
            "visible" => Self::Visible,
            "hidden" => Self::Hidden,
            "scroll" => Self::Scroll,
            "auto" | "overlay" => Self::Auto,
            "clip" => Self::Clip,
            #[cfg(feature = "gecko")]
            "-moz-hidden-unscrollable" if static_prefs::pref!("layout.css.overflow-moz-hidden-unscrollable.enabled") => {
                Overflow::Clip
            },
        })
    }
}

impl Overflow {
    /// Return true if the value will create a scrollable box.
    #[inline]
    pub fn is_scrollable(&self) -> bool {
        matches!(*self, Self::Hidden | Self::Scroll | Self::Auto)
    }
    /// Convert the value to a scrollable value if it's not already scrollable.
    /// This maps `visible` to `auto` and `clip` to `hidden`.
    #[inline]
    pub fn to_scrollable(&self) -> Self {
        match *self {
            Self::Hidden | Self::Scroll | Self::Auto => *self,
            Self::Visible => Self::Auto,
            Self::Clip => Self::Hidden,
        }
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
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[css(bitflags(
    single = "auto",
    mixed = "stable,both-edges",
    validate_mixed = "Self::has_stable"
))]
/// Values for scrollbar-gutter:
/// <https://drafts.csswg.org/css-overflow-3/#scrollbar-gutter-property>
pub struct ScrollbarGutter(u8);
bitflags! {
    impl ScrollbarGutter: u8 {
        /// `auto` variant. Just for convenience if there is no flag set.
        const AUTO = 0;
        /// `stable` variant.
        const STABLE = 1 << 0;
        /// `both-edges` variant.
        const BOTH_EDGES = 1 << 1;
    }
}

impl ScrollbarGutter {
    #[inline]
    fn has_stable(&self) -> bool {
        self.intersects(Self::STABLE)
    }
}

/// A specified value for the zoom property.
#[derive(
    Clone, Debug, MallocSizeOf, PartialEq, Parse, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
#[allow(missing_docs)]
#[typed(todo_derive_fields)]
pub enum Zoom {
    Normal,
    /// An internal value that resets the effective zoom to 1. Used for scrollbar parts, which
    /// disregard zoom. We use this name because WebKit has this value exposed to the web.
    #[parse(condition = "ParserContext::in_ua_sheet")]
    Document,
    Value(NonNegativeNumberOrPercentage),
}

impl Zoom {
    /// Return a particular number value of the zoom property.
    #[inline]
    pub fn new_number(n: f32) -> Self {
        Self::Value(NonNegativeNumberOrPercentage::new_number(n))
    }
}

pub use crate::values::generics::box_::PositionProperty;
