/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for text properties.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::properties::longhands::writing_mode::computed_value::T as SpecifiedWritingMode;
use crate::values::computed;
use crate::values::computed::text::TextEmphasisStyle as ComputedTextEmphasisStyle;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::generics::text::{
    GenericHyphenateLimitChars, GenericInitialLetter, GenericTextDecorationInset,
    GenericTextDecorationLength, GenericTextIndent,
};
use crate::values::generics::NumberOrAuto;
use crate::values::specified::length::{Length, LengthPercentage};
use crate::values::specified::{AllowQuirks, Integer, Number};
use crate::Zero;
use cssparser::Parser;
use icu_segmenter::GraphemeClusterSegmenter;
use std::fmt::{self, Write};
use style_traits::values::SequenceWriter;
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};
use style_traits::{KeywordsCollectFn, SpecifiedValueInfo};

/// A specified type for the `initial-letter` property.
pub type InitialLetter = GenericInitialLetter<Number, Integer>;

/// A spacing value used by either the `letter-spacing` or `word-spacing` properties.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
pub enum Spacing {
    /// `normal`
    Normal,
    /// `<value>`
    Value(LengthPercentage),
}

impl Parse for Spacing {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|i| i.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(Spacing::Normal);
        }
        LengthPercentage::parse_quirky(context, input, AllowQuirks::Yes).map(Spacing::Value)
    }
}

/// A specified value for the `letter-spacing` property.
#[derive(
    Clone, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub struct LetterSpacing(pub Spacing);

impl ToComputedValue for LetterSpacing {
    type ComputedValue = computed::LetterSpacing;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        use computed::text::GenericLetterSpacing;
        match self.0 {
            Spacing::Normal => GenericLetterSpacing(computed::LengthPercentage::zero()),
            Spacing::Value(ref v) => GenericLetterSpacing(v.to_computed_value(context)),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        if computed.0.is_zero() {
            return LetterSpacing(Spacing::Normal);
        }
        LetterSpacing(Spacing::Value(ToComputedValue::from_computed_value(
            &computed.0,
        )))
    }
}

/// A specified value for the `word-spacing` property.
#[derive(
    Clone, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub struct WordSpacing(pub Spacing);

impl ToComputedValue for WordSpacing {
    type ComputedValue = computed::WordSpacing;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self.0 {
            Spacing::Normal => computed::LengthPercentage::zero(),
            Spacing::Value(ref v) => v.to_computed_value(context),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        WordSpacing(Spacing::Value(ToComputedValue::from_computed_value(
            computed,
        )))
    }
}

/// A value for the `hyphenate-character` property.
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
#[repr(C, u8)]
#[typed(todo_derive_fields)]
pub enum HyphenateCharacter {
    /// `auto`
    Auto,
    /// `<string>`
    String(crate::OwnedStr),
}

/// A value for the `hyphenate-limit-chars` property.
pub type HyphenateLimitChars = GenericHyphenateLimitChars<Integer>;

impl Parse for HyphenateLimitChars {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        type IntegerOrAuto = NumberOrAuto<Integer>;

        let total_word_length = IntegerOrAuto::parse(context, input)?;
        let pre_hyphen_length = input
            .try_parse(|i| IntegerOrAuto::parse(context, i))
            .unwrap_or(IntegerOrAuto::Auto);
        let post_hyphen_length = input
            .try_parse(|i| IntegerOrAuto::parse(context, i))
            .unwrap_or_else(|_| pre_hyphen_length.clone());
        Ok(Self {
            total_word_length,
            pre_hyphen_length,
            post_hyphen_length,
        })
    }
}

impl Parse for InitialLetter {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|i| i.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(Self::normal());
        }
        let size = Number::parse_at_least_one(context, input)?;
        let sink = input
            .try_parse(|i| Integer::parse_positive(context, i))
            .unwrap_or_else(|_| crate::Zero::zero());
        Ok(Self { size, sink })
    }
}

/// A generic value for the `text-overflow` property.
#[derive(
    Clone,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    Parse,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
pub enum TextOverflowSide {
    /// Clip inline content.
    Clip,
    /// Render ellipsis to represent clipped inline content.
    Ellipsis,
    /// Render a given string to represent clipped inline content.
    String(crate::values::AtomString),
}

#[derive(
    Clone,
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
/// text-overflow.
/// When the specified value only has one side, that's the "second"
/// side, and the sides are logical, so "second" means "end".  The
/// start side is Clip in that case.
///
/// When the specified value has two sides, those are our "first"
/// and "second" sides, and they are physical sides ("left" and
/// "right").
pub struct TextOverflow {
    /// First side
    pub first: TextOverflowSide,
    /// Second side
    pub second: TextOverflowSide,
    /// True if the specified value only has one side.
    pub sides_are_logical: bool,
}

impl Parse for TextOverflow {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<TextOverflow, ParseError<'i>> {
        let first = TextOverflowSide::parse(context, input)?;
        Ok(
            if let Ok(second) = input.try_parse(|input| TextOverflowSide::parse(context, input)) {
                Self {
                    first,
                    second,
                    sides_are_logical: false,
                }
            } else {
                Self {
                    first: TextOverflowSide::Clip,
                    second: first,
                    sides_are_logical: true,
                }
            },
        )
    }
}

impl TextOverflow {
    /// Returns the initial `text-overflow` value
    pub fn get_initial_value() -> TextOverflow {
        TextOverflow {
            first: TextOverflowSide::Clip,
            second: TextOverflowSide::Clip,
            sides_are_logical: true,
        }
    }
}

impl ToCss for TextOverflow {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.sides_are_logical {
            debug_assert_eq!(self.first, TextOverflowSide::Clip);
            self.second.to_css(dest)?;
        } else {
            self.first.to_css(dest)?;
            dest.write_char(' ')?;
            self.second.to_css(dest)?;
        }
        Ok(())
    }
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    Parse,
    Serialize,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[cfg_attr(
    feature = "gecko",
    css(bitflags(
        single = "none,spelling-error,grammar-error",
        mixed = "underline,overline,line-through,blink",
    ))
)]
#[cfg_attr(
    not(feature = "gecko"),
    css(bitflags(single = "none", mixed = "underline,overline,line-through,blink",))
)]
#[repr(C)]
/// Specified keyword values for the text-decoration-line property.
pub struct TextDecorationLine(u8);
bitflags! {
    impl TextDecorationLine: u8 {
        /// No text decoration line is specified.
        const NONE = 0;
        /// underline
        const UNDERLINE = 1 << 0;
        /// overline
        const OVERLINE = 1 << 1;
        /// line-through
        const LINE_THROUGH = 1 << 2;
        /// blink
        const BLINK = 1 << 3;
        /// spelling-error
        const SPELLING_ERROR = 1 << 4;
        /// grammar-error
        const GRAMMAR_ERROR = 1 << 5;
        /// Only set by presentation attributes
        ///
        /// Setting this will mean that text-decorations use the color
        /// specified by `color` in quirks mode.
        ///
        /// For example, this gives <a href=foo><font color="red">text</font></a>
        /// a red text decoration
        #[cfg(feature = "gecko")]
        const COLOR_OVERRIDE = 1 << 7;
    }
}

impl Default for TextDecorationLine {
    fn default() -> Self {
        TextDecorationLine::NONE
    }
}

impl TextDecorationLine {
    #[inline]
    /// Returns the initial value of text-decoration-line
    pub fn none() -> Self {
        TextDecorationLine::NONE
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
/// Specified keyword values for case transforms in the text-transform property. (These are exclusive.)
pub enum TextTransformCase {
    /// No case transform.
    None,
    /// All uppercase.
    Uppercase,
    /// All lowercase.
    Lowercase,
    /// Capitalize each word.
    Capitalize,
    /// Automatic italicization of math variables.
    #[cfg(feature = "gecko")]
    MathAuto,
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    Parse,
    Serialize,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[cfg_attr(
    feature = "gecko",
    css(bitflags(
        single = "none,math-auto",
        mixed = "uppercase,lowercase,capitalize,full-width,full-size-kana",
        validate_mixed = "Self::validate_mixed_flags",
    ))
)]
#[cfg_attr(
    not(feature = "gecko"),
    css(bitflags(
        single = "none",
        mixed = "uppercase,lowercase,capitalize,full-width,full-size-kana",
        validate_mixed = "Self::validate_mixed_flags",
    ))
)]
#[repr(C)]
/// Specified value for the text-transform property.
/// (The spec grammar gives
/// `none | math-auto | [capitalize | uppercase | lowercase] || full-width || full-size-kana`.)
/// https://drafts.csswg.org/css-text-4/#text-transform-property
pub struct TextTransform(u8);
bitflags! {
    impl TextTransform: u8 {
        /// none
        const NONE = 0;
        /// All uppercase.
        const UPPERCASE = 1 << 0;
        /// All lowercase.
        const LOWERCASE = 1 << 1;
        /// Capitalize each word.
        const CAPITALIZE = 1 << 2;
        /// Automatic italicization of math variables.
        #[cfg(feature = "gecko")]
        const MATH_AUTO = 1 << 3;

        /// All the case transforms, which are exclusive with each other.
        #[cfg(feature = "gecko")]
        const CASE_TRANSFORMS = Self::UPPERCASE.0 | Self::LOWERCASE.0 | Self::CAPITALIZE.0 | Self::MATH_AUTO.0;
        /// All the case transforms, which are exclusive with each other.
        #[cfg(feature = "servo")]
        const CASE_TRANSFORMS = Self::UPPERCASE.0 | Self::LOWERCASE.0 | Self::CAPITALIZE.0;

        /// full-width
        const FULL_WIDTH = 1 << 4;
        /// full-size-kana
        const FULL_SIZE_KANA = 1 << 5;
    }
}

impl TextTransform {
    /// Returns the initial value of text-transform
    #[inline]
    pub fn none() -> Self {
        Self::NONE
    }

    /// Returns whether the value is 'none'
    #[inline]
    pub fn is_none(self) -> bool {
        self == Self::NONE
    }

    fn validate_mixed_flags(&self) -> bool {
        let case = self.intersection(Self::CASE_TRANSFORMS);
        // Case bits are exclusive with each other.
        case.is_empty() || case.bits().is_power_of_two()
    }

    /// Returns the corresponding TextTransformCase.
    pub fn case(&self) -> TextTransformCase {
        match *self & Self::CASE_TRANSFORMS {
            Self::NONE => TextTransformCase::None,
            Self::UPPERCASE => TextTransformCase::Uppercase,
            Self::LOWERCASE => TextTransformCase::Lowercase,
            Self::CAPITALIZE => TextTransformCase::Capitalize,
            #[cfg(feature = "gecko")]
            Self::MATH_AUTO => TextTransformCase::MathAuto,
            _ => unreachable!("Case bits are exclusive with each other"),
        }
    }
}

/// Specified and computed value of text-align-last.
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
#[allow(missing_docs)]
#[repr(u8)]
pub enum TextAlignLast {
    Auto,
    Start,
    End,
    Left,
    Right,
    Center,
    Justify,
}

/// Specified value of text-align keyword value.
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
#[allow(missing_docs)]
#[repr(u8)]
pub enum TextAlignKeyword {
    Start,
    Left,
    Right,
    Center,
    Justify,
    End,
    #[parse(aliases = "-webkit-center")]
    MozCenter,
    #[parse(aliases = "-webkit-left")]
    MozLeft,
    #[parse(aliases = "-webkit-right")]
    MozRight,
}

/// Specified value of text-align property.
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
#[typed(todo_derive_fields)]
pub enum TextAlign {
    /// Keyword value of text-align property.
    Keyword(TextAlignKeyword),
    /// `match-parent` value of text-align property. It has a different handling
    /// unlike other keywords.
    MatchParent,
    /// This is how we implement the following HTML behavior from
    /// https://html.spec.whatwg.org/#tables-2:
    ///
    ///     User agents are expected to have a rule in their user agent style sheet
    ///     that matches th elements that have a parent node whose computed value
    ///     for the 'text-align' property is its initial value, whose declaration
    ///     block consists of just a single declaration that sets the 'text-align'
    ///     property to the value 'center'.
    ///
    /// Since selectors can't depend on the ancestor styles, we implement it with a
    /// magic value that computes to the right thing. Since this is an
    /// implementation detail, it shouldn't be exposed to web content.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozCenterOrInherit,
}

impl ToComputedValue for TextAlign {
    type ComputedValue = TextAlignKeyword;

    #[inline]
    fn to_computed_value(&self, _context: &Context) -> Self::ComputedValue {
        match *self {
            TextAlign::Keyword(key) => key,
            TextAlign::MatchParent => {
                // on the root <html> element we should still respect the dir
                // but the parent dir of that element is LTR even if it's <html dir=rtl>
                // and will only be RTL if certain prefs have been set.
                // In that case, the default behavior here will set it to left,
                // but we want to set it to right -- instead set it to the default (`start`),
                // which will do the right thing in this case (but not the general case)
                if _context.builder.is_root_element {
                    return TextAlignKeyword::Start;
                }
                let parent = _context
                    .builder
                    .get_parent_inherited_text()
                    .clone_text_align();
                let ltr = _context.builder.inherited_writing_mode().is_bidi_ltr();
                match (parent, ltr) {
                    (TextAlignKeyword::Start, true) => TextAlignKeyword::Left,
                    (TextAlignKeyword::Start, false) => TextAlignKeyword::Right,
                    (TextAlignKeyword::End, true) => TextAlignKeyword::Right,
                    (TextAlignKeyword::End, false) => TextAlignKeyword::Left,
                    _ => parent,
                }
            },
            TextAlign::MozCenterOrInherit => {
                let parent = _context
                    .builder
                    .get_parent_inherited_text()
                    .clone_text_align();
                if parent == TextAlignKeyword::Start {
                    TextAlignKeyword::Center
                } else {
                    parent
                }
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        TextAlign::Keyword(*computed)
    }
}

fn fill_mode_is_default_and_shape_exists(
    fill: &TextEmphasisFillMode,
    shape: &Option<TextEmphasisShapeKeyword>,
) -> bool {
    shape.is_some() && fill.is_filled()
}

/// Specified value of text-emphasis-style property.
///
/// https://drafts.csswg.org/css-text-decor/#propdef-text-emphasis-style
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
#[allow(missing_docs)]
#[typed(todo_derive_fields)]
pub enum TextEmphasisStyle {
    /// [ <fill> || <shape> ]
    Keyword {
        #[css(contextual_skip_if = "fill_mode_is_default_and_shape_exists")]
        fill: TextEmphasisFillMode,
        shape: Option<TextEmphasisShapeKeyword>,
    },
    /// `none`
    None,
    /// `<string>` (of which only the first grapheme cluster will be used).
    String(crate::OwnedStr),
}

/// Fill mode for the text-emphasis-style property
#[derive(
    Clone,
    Copy,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum TextEmphasisFillMode {
    /// `filled`
    Filled,
    /// `open`
    Open,
}

impl TextEmphasisFillMode {
    /// Whether the value is `filled`.
    #[inline]
    pub fn is_filled(&self) -> bool {
        matches!(*self, TextEmphasisFillMode::Filled)
    }
}

/// Shape keyword for the text-emphasis-style property
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum TextEmphasisShapeKeyword {
    /// `dot`
    Dot,
    /// `circle`
    Circle,
    /// `double-circle`
    DoubleCircle,
    /// `triangle`
    Triangle,
    /// `sesame`
    Sesame,
}

impl ToComputedValue for TextEmphasisStyle {
    type ComputedValue = ComputedTextEmphasisStyle;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            TextEmphasisStyle::Keyword { fill, shape } => {
                let shape = shape.unwrap_or_else(|| {
                    // FIXME(emilio, bug 1572958): This should set the
                    // rule_cache_conditions properly.
                    //
                    // Also should probably use WritingMode::is_vertical rather
                    // than the computed value of the `writing-mode` property.
                    if context.style().get_inherited_box().clone_writing_mode()
                        == SpecifiedWritingMode::HorizontalTb
                    {
                        TextEmphasisShapeKeyword::Circle
                    } else {
                        TextEmphasisShapeKeyword::Sesame
                    }
                });
                ComputedTextEmphasisStyle::Keyword { fill, shape }
            },
            TextEmphasisStyle::None => ComputedTextEmphasisStyle::None,
            TextEmphasisStyle::String(ref s) => {
                // FIXME(emilio): Doing this at computed value time seems wrong.
                // The spec doesn't say that this should be a computed-value
                // time operation. This is observable from getComputedStyle().
                //
                // Note that the first grapheme cluster boundary should always be the start of the string.
                let first_grapheme_end = GraphemeClusterSegmenter::new()
                    .segment_str(s)
                    .nth(1)
                    .unwrap_or(0);
                ComputedTextEmphasisStyle::String(s[0..first_grapheme_end].to_string().into())
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        match *computed {
            ComputedTextEmphasisStyle::Keyword { fill, shape } => TextEmphasisStyle::Keyword {
                fill,
                shape: Some(shape),
            },
            ComputedTextEmphasisStyle::None => TextEmphasisStyle::None,
            ComputedTextEmphasisStyle::String(ref string) => {
                TextEmphasisStyle::String(string.clone())
            },
        }
    }
}

impl Parse for TextEmphasisStyle {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("none"))
            .is_ok()
        {
            return Ok(TextEmphasisStyle::None);
        }

        if let Ok(s) = input.try_parse(|i| i.expect_string().map(|s| s.as_ref().to_owned())) {
            // Handle <string>
            return Ok(TextEmphasisStyle::String(s.into()));
        }

        // Handle a pair of keywords
        let mut shape = input.try_parse(TextEmphasisShapeKeyword::parse).ok();
        let fill = input.try_parse(TextEmphasisFillMode::parse).ok();
        if shape.is_none() {
            shape = input.try_parse(TextEmphasisShapeKeyword::parse).ok();
        }

        if shape.is_none() && fill.is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        // If a shape keyword is specified but neither filled nor open is
        // specified, filled is assumed.
        let fill = fill.unwrap_or(TextEmphasisFillMode::Filled);

        // We cannot do the same because the default `<shape>` depends on the
        // computed writing-mode.
        Ok(TextEmphasisStyle::Keyword { fill, shape })
    }
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    Parse,
    Serialize,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[css(bitflags(
    single = "auto",
    mixed = "over,under,left,right",
    validate_mixed = "Self::validate_and_simplify"
))]
/// Values for text-emphasis-position:
/// <https://drafts.csswg.org/css-text-decor/#text-emphasis-position-property>
pub struct TextEmphasisPosition(u8);
bitflags! {
    impl TextEmphasisPosition: u8 {
        /// Automatically choose mark position based on language.
        const AUTO = 1 << 0;
        /// Draw marks over the text in horizontal writing mode.
        const OVER = 1 << 1;
        /// Draw marks under the text in horizontal writing mode.
        const UNDER = 1 << 2;
        /// Draw marks to the left of the text in vertical writing mode.
        const LEFT = 1 << 3;
        /// Draw marks to the right of the text in vertical writing mode.
        const RIGHT = 1 << 4;
    }
}

impl TextEmphasisPosition {
    fn validate_and_simplify(&mut self) -> bool {
        // Require one but not both of 'over' and 'under'.
        if self.intersects(Self::OVER) == self.intersects(Self::UNDER) {
            return false;
        }

        // If 'left' is present, 'right' must be absent.
        if self.intersects(Self::LEFT) {
            return !self.intersects(Self::RIGHT);
        }

        self.remove(Self::RIGHT); // Right is the default
        true
    }
}

/// Values for the `word-break` property.
#[repr(u8)]
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
#[allow(missing_docs)]
pub enum WordBreak {
    Normal,
    BreakAll,
    KeepAll,
    /// The break-word value, needed for compat.
    ///
    /// Specifying `word-break: break-word` makes `overflow-wrap` behave as
    /// `anywhere`, and `word-break` behave like `normal`.
    #[cfg(feature = "gecko")]
    BreakWord,
}

/// Values for the `text-justify` CSS property.
#[repr(u8)]
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
#[allow(missing_docs)]
pub enum TextJustify {
    Auto,
    None,
    InterWord,
    // See https://drafts.csswg.org/css-text-3/#valdef-text-justify-distribute
    // and https://github.com/w3c/csswg-drafts/issues/6156 for the alias.
    #[parse(aliases = "distribute")]
    InterCharacter,
}

/// Values for the `-moz-control-character-visibility` CSS property.
#[repr(u8)]
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
#[allow(missing_docs)]
pub enum MozControlCharacterVisibility {
    Hidden,
    Visible,
}

#[cfg(feature = "gecko")]
impl Default for MozControlCharacterVisibility {
    fn default() -> Self {
        if static_prefs::pref!("layout.css.control-characters.visible") {
            Self::Visible
        } else {
            Self::Hidden
        }
    }
}

/// Values for the `line-break` property.
#[repr(u8)]
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
#[allow(missing_docs)]
pub enum LineBreak {
    Auto,
    Loose,
    Normal,
    Strict,
    Anywhere,
}

/// Values for the `overflow-wrap` property.
#[repr(u8)]
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
#[allow(missing_docs)]
pub enum OverflowWrap {
    Normal,
    BreakWord,
    Anywhere,
}

/// A specified value for the `text-indent` property
/// which takes the grammar of [<length-percentage>] && hanging? && each-line?
///
/// https://drafts.csswg.org/css-text/#propdef-text-indent
pub type TextIndent = GenericTextIndent<LengthPercentage>;

impl Parse for TextIndent {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let mut length = None;
        let mut hanging = false;
        let mut each_line = false;

        // The length-percentage and the two possible keywords can occur in any order.
        while !input.is_exhausted() {
            // If we haven't seen a length yet, try to parse one.
            if length.is_none() {
                if let Ok(len) = input
                    .try_parse(|i| LengthPercentage::parse_quirky(context, i, AllowQuirks::Yes))
                {
                    length = Some(len);
                    continue;
                }
            }

            // Servo doesn't support the keywords, so just break and let the caller deal with it.
            if cfg!(feature = "servo") {
                break;
            }

            // Check for the keywords (boolean flags).
            try_match_ident_ignore_ascii_case! { input,
                "hanging" if !hanging => hanging = true,
                "each-line" if !each_line => each_line = true,
            }
        }

        // The length-percentage value is required for the declaration to be valid.
        if let Some(length) = length {
            Ok(Self {
                length,
                hanging,
                each_line,
            })
        } else {
            Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        }
    }
}

/// Implements text-decoration-skip-ink which takes the keywords auto | none | all
///
/// https://drafts.csswg.org/css-text-decor-4/#text-decoration-skip-ink-property
#[repr(u8)]
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
#[allow(missing_docs)]
pub enum TextDecorationSkipInk {
    Auto,
    None,
    All,
}

/// Implements type for `text-decoration-thickness` property
pub type TextDecorationLength = GenericTextDecorationLength<LengthPercentage>;

impl TextDecorationLength {
    /// `Auto` value.
    #[inline]
    pub fn auto() -> Self {
        GenericTextDecorationLength::Auto
    }

    /// Whether this is the `Auto` value.
    #[inline]
    pub fn is_auto(&self) -> bool {
        matches!(*self, GenericTextDecorationLength::Auto)
    }
}

/// Implements type for `text-decoration-inset` property
pub type TextDecorationInset = GenericTextDecorationInset<Length>;

impl TextDecorationInset {
    /// `Auto` value.
    #[inline]
    pub fn auto() -> Self {
        GenericTextDecorationInset::Auto
    }

    /// Whether this is the `Auto` value.
    #[inline]
    pub fn is_auto(&self) -> bool {
        matches!(*self, GenericTextDecorationInset::Auto)
    }
}

impl Parse for TextDecorationInset {
    fn parse<'i, 't>(
        ctx: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(start) = input.try_parse(|i| Length::parse(ctx, i)) {
            let end = input.try_parse(|i| Length::parse(ctx, i));
            let end = end.unwrap_or_else(|_| start.clone());
            return Ok(TextDecorationInset::Length { start, end });
        }
        input.expect_ident_matching("auto")?;
        Ok(TextDecorationInset::Auto)
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
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    single = "auto",
    mixed = "from-font,under,left,right",
    validate_mixed = "Self::validate_mixed_flags",
))]
#[repr(C)]
/// Specified keyword values for the text-underline-position property.
/// (Non-exclusive, but not all combinations are allowed: the spec grammar gives
/// `auto | [ from-font | under ] || [ left | right ]`.)
/// https://drafts.csswg.org/css-text-decor-4/#text-underline-position-property
pub struct TextUnderlinePosition(u8);
bitflags! {
    impl TextUnderlinePosition: u8 {
        /// Use automatic positioning below the alphabetic baseline.
        const AUTO = 0;
        /// Use underline position from the first available font.
        const FROM_FONT = 1 << 0;
        /// Below the glyph box.
        const UNDER = 1 << 1;
        /// In vertical mode, place to the left of the text.
        const LEFT = 1 << 2;
        /// In vertical mode, place to the right of the text.
        const RIGHT = 1 << 3;
    }
}

impl TextUnderlinePosition {
    fn validate_mixed_flags(&self) -> bool {
        if self.contains(Self::LEFT | Self::RIGHT) {
            // left and right can't be mixed together.
            return false;
        }
        if self.contains(Self::FROM_FONT | Self::UNDER) {
            // from-font and under can't be mixed together either.
            return false;
        }
        true
    }
}

impl ToCss for TextUnderlinePosition {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.is_empty() {
            return dest.write_str("auto");
        }

        let mut writer = SequenceWriter::new(dest, " ");
        let mut any = false;

        macro_rules! maybe_write {
            ($ident:ident => $str:expr) => {
                if self.contains(TextUnderlinePosition::$ident) {
                    any = true;
                    writer.raw_item($str)?;
                }
            };
        }

        maybe_write!(FROM_FONT => "from-font");
        maybe_write!(UNDER => "under");
        maybe_write!(LEFT => "left");
        maybe_write!(RIGHT => "right");

        debug_assert!(any);

        Ok(())
    }
}

/// Values for `ruby-position` property
#[repr(u8)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[allow(missing_docs)]
pub enum RubyPosition {
    AlternateOver,
    AlternateUnder,
    Over,
    Under,
}

impl Parse for RubyPosition {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<RubyPosition, ParseError<'i>> {
        // Parse alternate before
        let alternate = input
            .try_parse(|i| i.expect_ident_matching("alternate"))
            .is_ok();
        if alternate && input.is_exhausted() {
            return Ok(RubyPosition::AlternateOver);
        }
        // Parse over / under
        let over = try_match_ident_ignore_ascii_case! { input,
            "over" => true,
            "under" => false,
        };
        // Parse alternate after
        let alternate = alternate
            || input
                .try_parse(|i| i.expect_ident_matching("alternate"))
                .is_ok();

        Ok(match (over, alternate) {
            (true, true) => RubyPosition::AlternateOver,
            (false, true) => RubyPosition::AlternateUnder,
            (true, false) => RubyPosition::Over,
            (false, false) => RubyPosition::Under,
        })
    }
}

impl ToCss for RubyPosition {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str(match self {
            RubyPosition::AlternateOver => "alternate",
            RubyPosition::AlternateUnder => "alternate under",
            RubyPosition::Over => "over",
            RubyPosition::Under => "under",
        })
    }
}

impl SpecifiedValueInfo for RubyPosition {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        f(&["alternate", "over", "under"])
    }
}

/// Specified value for the text-autospace property
/// which takes the grammar:
///     normal | <autospace> | auto
/// where:
///     <autospace> = no-autospace |
///                   [ ideograph-alpha || ideograph-numeric || punctuation ]
///                   || [ insert | replace ]
///
/// https://drafts.csswg.org/css-text-4/#text-autospace-property
///
/// Bug 1980111: 'replace' value is not supported yet.
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
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    single = "normal,auto,no-autospace",
    // Bug 1980111: add 'replace' to 'mixed' in the future so that it parses correctly.
    // Bug 1986500: add 'punctuation' to 'mixed' in the future so that it parses correctly.
    mixed = "ideograph-alpha,ideograph-numeric,insert",
    // Bug 1980111: Uncomment 'validate_mixed' to support 'replace' value.
    // validate_mixed = "Self::validate_mixed_flags",
))]
#[repr(C)]
pub struct TextAutospace(u8);
bitflags! {
    impl TextAutospace: u8 {
        /// No automatic space is inserted.
        const NO_AUTOSPACE = 0;

        /// The user agent chooses a set of typographically high quality spacing values.
        const AUTO = 1 << 0;

        /// Same behavior as ideograph-alpha ideograph-numeric.
        const NORMAL = 1 << 1;

        /// 1/8ic space between ideographic characters and non-ideographic letters.
        const IDEOGRAPH_ALPHA = 1 << 2;

        /// 1/8ic space between ideographic characters and non-ideographic decimal numerals.
        const IDEOGRAPH_NUMERIC = 1 << 3;

        /* Bug 1986500: Uncomment the following to support the 'punctuation' value.
        /// Apply special spacing between letters and punctuation (French).
        const PUNCTUATION = 1 << 4;
        */

        /// Auto-spacing is only inserted if no space character is present in the text.
        const INSERT = 1 << 5;

        /* Bug 1980111: Uncomment the following to support 'replace' value.
        /// Auto-spacing may replace an existing U+0020 space with custom space.
        const REPLACE = 1 << 6;
        */
    }
}

/* Bug 1980111: Uncomment the following to support 'replace' value.
impl TextAutospace {
    fn validate_mixed_flags(&self) -> bool {
        // It's not valid to have both INSERT and REPLACE set.
        !self.contains(TextAutospace::INSERT | TextAutospace::REPLACE)
    }
}
*/
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
/// Identifies specific font metrics for use in the <text-edge> typedef.
///
/// https://drafts.csswg.org/css-inline-3/#typedef-text-edge
pub enum TextEdgeKeyword {
    /// Use the text-over baseline/text-under baseline as the over/under edge.
    Text,
    /// Use the ideographic-over baseline/ideographic-under baseline as the over/under edge.
    Ideographic,
    /// Use the ideographic-ink-over baseline/ideographic-ink-under baseline as the over/under edge.
    IdeographicInk,
    /// Use the cap-height baseline as the over edge.
    Cap,
    /// Use the x-height baseline as the over edge.
    Ex,
    /// Use the alphabetic baseline as the under edge.
    Alphabetic,
}

impl TextEdgeKeyword {
    fn is_valid_for_over(&self) -> bool {
        match self {
            TextEdgeKeyword::Text
            | TextEdgeKeyword::Ideographic
            | TextEdgeKeyword::IdeographicInk
            | TextEdgeKeyword::Cap
            | TextEdgeKeyword::Ex => true,
            _ => false,
        }
    }

    fn is_valid_for_under(&self) -> bool {
        match self {
            TextEdgeKeyword::Text
            | TextEdgeKeyword::Ideographic
            | TextEdgeKeyword::IdeographicInk
            | TextEdgeKeyword::Alphabetic => true,
            _ => false,
        }
    }
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
/// The <text-edge> typedef, used by the `line-fit-edge` and
/// `text-box-edge` properties.
///
/// The first value specifies the text over edge; the second value
/// specifies the text under edge. If only one value is specified,
/// both edges are assigned that same keyword if possible; else
/// text is assumed as the missing value.
///
/// https://drafts.csswg.org/css-inline-3/#typedef-text-edge
pub struct TextEdge {
    /// Font metric to use for the text over edge.
    pub over: TextEdgeKeyword,
    /// Font metric to use for the text under edge.
    pub under: TextEdgeKeyword,
}

impl Parse for TextEdge {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<TextEdge, ParseError<'i>> {
        let first = TextEdgeKeyword::parse(input)?;

        if let Ok(second) = input.try_parse(TextEdgeKeyword::parse) {
            if !first.is_valid_for_over() || !second.is_valid_for_under() {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }

            return Ok(TextEdge {
                over: first,
                under: second,
            });
        }

        // https://drafts.csswg.org/css-inline-3/#typedef-text-edge
        // > If only one value is specified, both edges are assigned that same
        // > keyword if possible; else 'text' is assumed as the missing value.
        match (first.is_valid_for_over(), first.is_valid_for_under()) {
            (true, true) => Ok(TextEdge {
                over: first,
                under: first,
            }),
            (true, false) => Ok(TextEdge {
                over: first,
                under: TextEdgeKeyword::Text,
            }),
            (false, true) => Ok(TextEdge {
                over: TextEdgeKeyword::Text,
                under: first,
            }),
            _ => unreachable!("Parsed keyword will be valid for at least one edge"),
        }
    }
}

impl ToCss for TextEdge {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match (self.over, self.under) {
            (over, TextEdgeKeyword::Text) if !over.is_valid_for_under() => over.to_css(dest),
            (TextEdgeKeyword::Text, under) if !under.is_valid_for_over() => under.to_css(dest),
            (over, under) => {
                over.to_css(dest)?;

                if over != under {
                    dest.write_char(' ')?;
                    self.under.to_css(dest)?;
                }

                Ok(())
            },
        }
    }
}

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
    ToTyped,
)]
#[repr(C, u8)]
/// Specified value for the `text-box-edge` property.
///
/// https://drafts.csswg.org/css-inline-3/#text-box-edge
pub enum TextBoxEdge {
    /// Uses the value of `line-fit-edge`, interpreting `leading` (the initial value) as `text`.
    Auto,
    /// Uses the specified font metrics.
    TextEdge(TextEdge),
}

#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    PartialEq,
    Parse,
    Serialize,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(single = "none,trim-start,trim-end,trim-both"))]
#[repr(C)]
/// Specified value for the `text-box-trim` property.
///
/// https://drafts.csswg.org/css-inline-3/#text-box-edge
pub struct TextBoxTrim(u8);
bitflags! {
    impl TextBoxTrim: u8 {
        /// NONE
        const NONE = 0;
        /// TRIM_START
        const TRIM_START = 1 << 0;
        /// TRIM_END
        const TRIM_END = 1 << 1;
        /// TRIM_BOTH
        const TRIM_BOTH = Self::TRIM_START.0 | Self::TRIM_END.0;
    }
}

impl TextBoxTrim {
    /// Returns the initial value of text-box-trim
    #[inline]
    pub fn none() -> Self {
        TextBoxTrim::NONE
    }
}
