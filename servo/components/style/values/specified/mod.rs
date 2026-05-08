/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified values.
//!
//! TODO(emilio): Enhance docs.

use super::computed::{Context, ToComputedValue};
use super::generics::grid::ImplicitGridTracks as GenericImplicitGridTracks;
use super::generics::grid::{GridLine as GenericGridLine, TrackBreadth as GenericTrackBreadth};
use super::generics::grid::{TrackList as GenericTrackList, TrackSize as GenericTrackSize};
use super::generics::{self, NonNegative};
use super::CSSFloat;
use crate::context::QuirksMode;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::specified::calc::CalcNode;
use crate::values::specified::number::parse_number_with_clamping_mode;
use crate::values::{serialize_atom_identifier, AtomString};
use crate::{Atom, Namespace, Prefix};
use cssparser::{Parser, Token};
use rustc_hash::FxHashMap;
use std::fmt::{self, Write};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

pub use self::align::{ContentDistribution, ItemPlacement, JustifyItems, SelfAlignment};
pub use self::angle::{AllowUnitlessZeroAngle, Angle};
pub use self::animation::{
    AnimationComposition, AnimationDirection, AnimationDuration, AnimationFillMode,
    AnimationIterationCount, AnimationName, AnimationPlayState, AnimationRangeEnd,
    AnimationRangeStart, AnimationTimeline, ScrollAxis, TimelineName, TransitionBehavior,
    TransitionProperty, ViewTimelineInset, ViewTransitionClass, ViewTransitionName,
};
pub use self::background::{BackgroundRepeat, BackgroundSize};
pub use self::basic_shape::FillRule;
pub use self::border::{
    BorderCornerRadius, BorderImageRepeat, BorderImageSideWidth, BorderImageSlice,
    BorderImageWidth, BorderRadius, BorderSideOffset, BorderSideWidth, BorderSpacing, BorderStyle,
    LineWidth,
};
pub use self::box_::{
    AlignmentBaseline, Appearance, BaselineShift, BaselineSource, BreakBetween, BreakWithin, Clear,
    Contain, ContainIntrinsicSize, ContainerName, ContainerType, ContentVisibility, Display,
    DominantBaseline, Float, LineClamp, Overflow, OverflowAnchor, OverflowClipMargin,
    OverscrollBehavior, Perspective, PositionProperty, Resize, ScrollSnapAlign, ScrollSnapAxis,
    ScrollSnapStop, ScrollSnapStrictness, ScrollSnapType, ScrollbarGutter, TouchAction, WillChange,
    WillChangeBits, WritingModeProperty, Zoom,
};
pub use self::color::{
    Color, ColorOrAuto, ColorPropertyValue, ColorScheme, ForcedColorAdjust, PrintColorAdjust,
};
pub use self::column::ColumnCount;
pub use self::counters::{Content, ContentItem, CounterIncrement, CounterReset, CounterSet};
pub use self::easing::TimingFunction;
pub use self::effects::{BoxShadow, Filter, SimpleShadow};
pub use self::flex::FlexBasis;
pub use self::font::{FontFamily, FontLanguageOverride, FontPalette, FontStyle};
pub use self::font::{FontFeatureSettings, FontVariantLigatures, FontVariantNumeric};
pub use self::font::{
    FontSize, FontSizeAdjust, FontSizeAdjustFactor, FontSizeKeyword, FontStretch, FontSynthesis,
    FontSynthesisStyle,
};
pub use self::font::{FontVariantAlternates, FontWeight};
pub use self::font::{FontVariantEastAsian, FontVariationSettings, LineHeight};
pub use self::font::{MathDepth, MozScriptMinSize, MozScriptSizeMultiplier, XLang, XTextScale};
pub use self::image::{EndingShape as GradientEndingShape, Gradient, Image, ImageRendering};
pub use self::length::{AbsoluteLength, CalcNumeric, CharacterWidth};
pub use self::length::{FontRelativeLength, Length, LengthOrNumber, NonNegativeLengthOrNumber};
pub use self::length::{LengthOrAuto, LengthPercentage, LengthPercentageOrAuto};
pub use self::length::{Margin, MaxSize, Size};
pub use self::length::{NoCalcLength, ViewportPercentageLength, ViewportVariant};
pub use self::length::{
    NonNegativeLength, NonNegativeLengthPercentage, NonNegativeLengthPercentageOrAuto,
};
pub use self::list::ListStyleType;
pub use self::list::Quotes;
pub use self::motion::{OffsetPath, OffsetPosition, OffsetRotate};
pub use self::number::{
    GreaterThanOrEqualToOneNumber, Integer, NoCalcNumber, NonNegativeInteger, NonNegativeNumber,
    Number, PositiveInteger,
};
pub use self::outline::OutlineStyle;
pub use self::page::{PageName, PageOrientation, PageSize, PageSizeOrientation, PaperSize};
pub use self::percentage::{NonNegativePercentage, Percentage};
pub use self::position::{
    AnchorFunction, AnchorName, AnchorNameIdent, AspectRatio, GridAutoFlow, GridTemplateAreas,
    Inset, MasonryAutoFlow, MasonryItemOrder, MasonryPlacement, Position, PositionAnchor,
    PositionAnchorKeyword, PositionArea, PositionAreaKeyword, PositionComponent, PositionOrAuto,
    PositionTryFallbacks, PositionTryOrder, PositionVisibility, ScopedName, ZIndex,
};
pub use self::ratio::Ratio;
pub use self::rect::NonNegativeLengthOrNumberRect;
pub use self::resolution::Resolution;
pub use self::svg::{DProperty, MozContextProperties};
pub use self::svg::{SVGLength, SVGOpacity, SVGPaint};
pub use self::svg::{SVGPaintOrder, SVGStrokeDashArray, SVGWidth, VectorEffect};
pub use self::svg_path::SVGPathData;
pub use self::text::RubyPosition;
pub use self::text::{HyphenateCharacter, HyphenateLimitChars};
pub use self::text::{InitialLetter, LetterSpacing, LineBreak, TextAlign, TextIndent};
pub use self::text::{OverflowWrap, TextEmphasisPosition, TextEmphasisStyle, WordBreak};
pub use self::text::{TextAlignKeyword, TextDecorationLine, TextOverflow, WordSpacing};
pub use self::text::{TextAlignLast, TextAutospace, TextUnderlinePosition};
pub use self::text::{TextBoxEdge, TextBoxTrim};
pub use self::text::{
    TextDecorationInset, TextDecorationLength, TextDecorationSkipInk, TextJustify, TextTransform,
};
pub use self::time::Time;
pub use self::transform::{Rotate, Scale, Transform};
pub use self::transform::{TransformBox, TransformOrigin, TransformStyle, Translate};
#[cfg(feature = "gecko")]
pub use self::ui::CursorImage;
pub use self::ui::{
    BoolInteger, Cursor, Inert, MozTheme, PointerEvents, ScrollbarColor, UserFocus, UserSelect,
};
pub use super::generics::grid::GridTemplateComponent as GenericGridTemplateComponent;

pub mod align;
pub mod angle;
pub mod animation;
pub mod background;
pub mod basic_shape;
pub mod border;
#[path = "box.rs"]
pub mod box_;
pub mod calc;
pub mod color;
pub mod column;
pub mod counters;
pub mod easing;
pub mod effects;
pub mod flex;
pub mod font;
pub mod grid;
pub mod image;
pub mod intersection_observer;
pub mod length;
pub mod list;
pub mod motion;
pub mod number;
pub mod outline;
pub mod page;
pub mod percentage;
pub mod position;
pub mod ratio;
pub mod rect;
pub mod resolution;
pub mod source_size_list;
pub mod svg;
pub mod svg_path;
pub mod table;
pub mod text;
pub mod time;
pub mod transform;
pub mod ui;
pub mod url;

/// <angle> | <percentage>
/// https://drafts.csswg.org/css-values/#typedef-angle-percentage
#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
pub enum AngleOrPercentage {
    Percentage(Percentage),
    Angle(Angle),
}

impl AngleOrPercentage {
    fn parse_internal<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_unitless_zero: AllowUnitlessZeroAngle,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(per) = input.try_parse(|i| Percentage::parse(context, i)) {
            return Ok(AngleOrPercentage::Percentage(per));
        }

        Angle::parse_internal(context, input, allow_unitless_zero).map(AngleOrPercentage::Angle)
    }

    /// Allow unitless angles, used for conic-gradients as specified by the spec.
    /// https://drafts.csswg.org/css-images-4/#valdef-conic-gradient-angle
    pub fn parse_with_unitless<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        AngleOrPercentage::parse_internal(context, input, AllowUnitlessZeroAngle::Yes)
    }
}

impl Parse for AngleOrPercentage {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        AngleOrPercentage::parse_internal(context, input, AllowUnitlessZeroAngle::No)
    }
}

/// <number> | <percentage>
///
/// Accepts only non-negative numbers.
#[allow(missing_docs)]
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
pub enum NumberOrPercentage {
    Percentage(Percentage),
    Number(Number),
}

impl NumberOrPercentage {
    fn parse_with_clamping_mode<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        type_: AllowedNumericType,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(per) =
            input.try_parse(|i| Percentage::parse_with_clamping_mode(context, i, type_))
        {
            return Ok(NumberOrPercentage::Percentage(per));
        }

        parse_number_with_clamping_mode(context, input, type_).map(NumberOrPercentage::Number)
    }

    /// Parse a non-negative number or percentage.
    pub fn parse_non_negative<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
    }

    /// Convert the number or the percentage to a number.
    pub fn to_percentage(self) -> Percentage {
        match self {
            Self::Percentage(p) => p,
            Self::Number(n) => n.to_percentage(),
        }
    }

    /// Convert the number or the percentage to a number.
    pub fn to_number(&self) -> Number {
        match self {
            Self::Percentage(p) => p.to_number(),
            Self::Number(n) => n.clone(),
        }
    }
}

impl Parse for NumberOrPercentage {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::All)
    }
}

/// A non-negative <number> | <percentage>.
pub type NonNegativeNumberOrPercentage = NonNegative<NumberOrPercentage>;

impl NonNegativeNumberOrPercentage {
    /// Returns the `100%` value.
    #[inline]
    pub fn hundred_percent() -> Self {
        NonNegative(NumberOrPercentage::Percentage(Percentage::hundred()))
    }

    /// Return a particular number.
    #[inline]
    pub fn new_number(n: f32) -> Self {
        NonNegative(NumberOrPercentage::Number(Number::new(n)))
    }
}

impl Parse for NonNegativeNumberOrPercentage {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(NonNegative(NumberOrPercentage::parse_non_negative(
            context, input,
        )?))
    }
}

/// The value of Opacity is <alpha-value>, which is "<number> | <percentage>".
/// However, we serialize the specified value as number, so it's ok to store
/// the Opacity as Number.
#[derive(
    Clone, Debug, MallocSizeOf, PartialEq, PartialOrd, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub struct Opacity(Number);

impl Parse for Opacity {
    /// Opacity accepts <number> | <percentage>, so we parse it as NumberOrPercentage,
    /// and then convert into an Number if it's a Percentage.
    /// https://drafts.csswg.org/cssom/#serializing-css-values
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let number = NumberOrPercentage::parse(context, input)?.to_number();
        Ok(Opacity(number))
    }
}

impl ToComputedValue for Opacity {
    type ComputedValue = CSSFloat;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> CSSFloat {
        let value = self.0.to_computed_value(context);
        if context.for_animation {
            // Type <number> and <percentage> should be able to interpolate
            // out-of-range opacity values which benefits additive animation
            value
        } else {
            value.min(1.0).max(0.0)
        }
    }

    #[inline]
    fn from_computed_value(computed: &CSSFloat) -> Self {
        Opacity(Number::from_computed_value(computed))
    }
}

/// The specified value of a grid `<track-breadth>`
pub type TrackBreadth = GenericTrackBreadth<LengthPercentage>;

/// The specified value of a grid `<track-size>`
pub type TrackSize = GenericTrackSize<LengthPercentage>;

/// The specified value of a grid `<track-size>+`
pub type ImplicitGridTracks = GenericImplicitGridTracks<TrackSize>;

/// The specified value of a grid `<track-list>`
/// (could also be `<auto-track-list>` or `<explicit-track-list>`)
pub type TrackList = GenericTrackList<LengthPercentage, Integer>;

/// The specified value of a `<grid-line>`.
pub type GridLine = GenericGridLine<Integer>;

/// `<grid-template-rows> | <grid-template-columns>`
pub type GridTemplateComponent = GenericGridTemplateComponent<LengthPercentage, Integer>;

/// rect(...)
pub type ClipRect = generics::GenericClipRect<LengthOrAuto>;

impl Parse for ClipRect {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_quirky(context, input, AllowQuirks::No)
    }
}

impl ClipRect {
    /// Parses a rect(<top>, <left>, <bottom>, <right>), allowing quirks.
    fn parse_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError<'i>> {
        input.expect_function_matching("rect")?;

        fn parse_argument<'i, 't>(
            context: &ParserContext,
            input: &mut Parser<'i, 't>,
            allow_quirks: AllowQuirks,
        ) -> Result<LengthOrAuto, ParseError<'i>> {
            LengthOrAuto::parse_quirky(context, input, allow_quirks)
        }

        input.parse_nested_block(|input| {
            let top = parse_argument(context, input, allow_quirks)?;
            let right;
            let bottom;
            let left;

            if input.try_parse(|input| input.expect_comma()).is_ok() {
                right = parse_argument(context, input, allow_quirks)?;
                input.expect_comma()?;
                bottom = parse_argument(context, input, allow_quirks)?;
                input.expect_comma()?;
                left = parse_argument(context, input, allow_quirks)?;
            } else {
                right = parse_argument(context, input, allow_quirks)?;
                bottom = parse_argument(context, input, allow_quirks)?;
                left = parse_argument(context, input, allow_quirks)?;
            }

            Ok(ClipRect {
                top,
                right,
                bottom,
                left,
            })
        })
    }
}

/// rect(...) | auto
pub type ClipRectOrAuto = generics::GenericClipRectOrAuto<ClipRect>;

impl ClipRectOrAuto {
    /// Parses a ClipRect or Auto, allowing quirks.
    pub fn parse_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(v) = input.try_parse(|i| ClipRect::parse_quirky(context, i, allow_quirks)) {
            return Ok(generics::GenericClipRectOrAuto::Rect(v));
        }
        input.expect_ident_matching("auto")?;
        Ok(generics::GenericClipRectOrAuto::Auto)
    }
}

/// Whether quirks are allowed in this context.
#[derive(Clone, Copy, PartialEq)]
pub enum AllowQuirks {
    /// Quirks are not allowed.
    No,
    /// Quirks are allowed, in quirks mode.
    Yes,
    /// Quirks are always allowed, used for SVG lengths.
    Always,
}

impl AllowQuirks {
    /// Returns `true` if quirks are allowed in this context.
    pub fn allowed(self, quirks_mode: QuirksMode) -> bool {
        match self {
            AllowQuirks::Always => true,
            AllowQuirks::No => false,
            AllowQuirks::Yes => quirks_mode == QuirksMode::Quirks,
        }
    }
}

#[derive(Clone, Debug, PartialEq, MallocSizeOf, ToShmem)]
/// A namespace wrapper to distinguish between valid variants
pub enum ParsedNamespace {
    /// Unregistered namespace
    Unknown,
    /// Registered namespace
    Known(Namespace),
}

impl ParsedNamespace {
    /// Parse a namespace prefix and resolve it to the correct
    /// namespace URI.
    pub fn parse<'i, 't>(
        namespaces: &FxHashMap<Prefix, Namespace>,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        // We don't need to keep the prefix because different
        // prefixes can resolve to the same id. Additionally,
        // we also don't need it for serialization as substitution
        // functions serialize from the direct css declaration.
        parse_namespace(namespaces, input, /*allow_non_registered*/ true)
            .map(|(_prefix, namespace)| namespace)
    }
}

impl Default for ParsedNamespace {
    fn default() -> Self {
        Self::Known(Namespace::default())
    }
}

/// An attr(...) rule
///
/// `[namespace? `|`]? ident`
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
)]
#[css(function)]
#[repr(C)]
pub struct Attr {
    /// Optional namespace prefix.
    pub namespace_prefix: Prefix,
    /// Optional namespace URL.
    pub namespace_url: Namespace,
    /// Attribute name
    pub attribute: Atom,
    /// Fallback value
    pub fallback: AtomString,
}

impl Parse for Attr {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Attr, ParseError<'i>> {
        input.expect_function_matching("attr")?;
        input.parse_nested_block(|i| Attr::parse_function(context, i))
    }
}

/// Try to parse a namespace and return it if parsed, or none if there was not one present
pub fn parse_namespace<'i, 't>(
    namespaces: &FxHashMap<Prefix, Namespace>,
    input: &mut Parser<'i, 't>,
    // TODO: Once general attr is enabled, we should remove this flag
    allow_non_registered: bool,
) -> Result<(Prefix, ParsedNamespace), ParseError<'i>> {
    let ns_prefix = match input.next()? {
        Token::Ident(ref prefix) => Some(Prefix::from(prefix.as_ref())),
        Token::Delim('|') => None,
        _ => return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
    };

    if ns_prefix.is_some() && !matches!(*input.next_including_whitespace()?, Token::Delim('|')) {
        return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
    }

    if let Some(prefix) = ns_prefix {
        let ns = match namespaces.get(&prefix).cloned() {
            Some(ns) => ParsedNamespace::Known(ns),
            None => {
                if allow_non_registered {
                    ParsedNamespace::Unknown
                } else {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
            },
        };
        Ok((prefix, ns))
    } else {
        Ok((Prefix::default(), ParsedNamespace::default()))
    }
}

impl Attr {
    /// Parse contents of attr() assuming we have already parsed `attr` and are
    /// within a parse_nested_block()
    pub fn parse_function<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Attr, ParseError<'i>> {
        // Syntax is `[namespace? '|']? ident [',' fallback]?`
        let namespace = input
            .try_parse(|input| {
                parse_namespace(
                    &context.namespaces.prefixes,
                    input,
                    /*allow_non_registered*/ false,
                )
            })
            .ok();
        let namespace_is_some = namespace.is_some();
        let (namespace_prefix, namespace_url) = namespace.unwrap_or_default();
        let ParsedNamespace::Known(namespace_url) = namespace_url else {
            unreachable!("Non-registered url not allowed (see parse namespace flag).")
        };

        // If there is a namespace, ensure no whitespace following '|'
        let attribute = Atom::from(if namespace_is_some {
            let location = input.current_source_location();
            match *input.next_including_whitespace()? {
                Token::Ident(ref ident) => ident.as_ref(),
                ref t => return Err(location.new_unexpected_token_error(t.clone())),
            }
        } else {
            input.expect_ident()?.as_ref()
        });

        // Fallback will always be a string value for now as we do not support
        // attr() types yet.
        let fallback = input
            .try_parse(|input| -> Result<AtomString, ParseError<'i>> {
                input.expect_comma()?;
                Ok(input.expect_string()?.as_ref().into())
            })
            .unwrap_or_default();

        Ok(Attr {
            namespace_prefix,
            namespace_url,
            attribute,
            fallback,
        })
    }
}

impl ToCss for Attr {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str("attr(")?;
        if !self.namespace_prefix.is_empty() {
            serialize_atom_identifier(&self.namespace_prefix, dest)?;
            dest.write_char('|')?;
        }
        serialize_atom_identifier(&self.attribute, dest)?;

        if !self.fallback.is_empty() {
            dest.write_str(", ")?;
            self.fallback.to_css(dest)?;
        }

        dest.write_char(')')
    }
}
