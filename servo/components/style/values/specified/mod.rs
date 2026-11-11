/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified values.
//!
//! TODO(emilio): Enhance docs.

use super::CSSFloat;
use super::computed::{Context, ToComputedValue};
use super::generics::grid::ImplicitGridTracks as GenericImplicitGridTracks;
use super::generics::grid::{GridLine as GenericGridLine, TrackBreadth as GenericTrackBreadth};
use super::generics::grid::{TrackList as GenericTrackList, TrackSize as GenericTrackSize};
use super::generics::{self, NonNegative};
use crate::FxHashMap;
use crate::context::QuirksMode;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::typed_om::NumericBaseType;
use crate::values::computed;
use crate::values::specified::calc::PercentageContext;
use crate::values::specified::number::parse_number_with_clamping_mode;
use crate::{Namespace, Prefix};
use cssparser::{Parser, Token};
use style_traits::values::specified::AllowedNumericType;
use style_traits::{ParseError, StyleParseErrorKind};

pub use self::align::{ContentDistribution, ItemPlacement, JustifyItems, SelfAlignment};
pub use self::angle::{AllowUnitlessZeroAngle, Angle, NoCalcAngle};
pub use self::animation::{
    AnimationComposition, AnimationDirection, AnimationDuration, AnimationFillMode,
    AnimationIterationCount, AnimationName, AnimationPlayState, AnimationRangeEnd,
    AnimationRangeStart, AnimationTimeline, ScrollAxis, TimelineName, TransitionBehavior,
    TransitionProperty, ViewTimelineInset, ViewTransitionClass, ViewTransitionName,
};
pub use self::background::{
    BackgroundClip, BackgroundOrigin, BackgroundRepeat, BackgroundSize, ImageLayerAttachment,
};
pub use self::basic_shape::FillRule;
pub use self::border::{
    BorderCornerRadius, BorderImageRepeat, BorderImageSideWidth, BorderImageSlice,
    BorderImageWidth, BorderRadius, BorderSideOffset, BorderSideWidth, BorderSpacing, BorderStyle,
    BoxDecorationBreak, FloatEdge, LineWidth,
};
pub use self::box_::{
    AlignmentBaseline, Appearance, BackfaceVisibility, BaselineShift, BaselineSource, BoxAlign,
    BoxCollapse, BoxDirection, BoxOrient, BoxPack, BreakBetween, BreakWithin, Clear, Contain,
    ContainIntrinsicSize, ContainerName, ContainerType, ContentVisibility, DirectionProperty,
    Display, DominantBaseline, Float, ImageOrientation, Isolation, LineClamp, MarginTrim, Orient,
    Overflow, OverflowAnchor, OverflowClipMargin, OverscrollBehavior, Perspective,
    PositionProperty, Resize, ScrollBehavior, ScrollSnapAlign, ScrollSnapAxis, ScrollSnapStop,
    ScrollSnapStrictness, ScrollSnapType, ScrollbarGutter, ScrollbarInset, TextOrientation,
    TopLayer, TouchAction, Visibility, WillChange, WillChangeBits, WritingModeProperty, Zoom,
};
pub use self::calc::{CalcLengthPercentage, CalcNumeric};
pub use self::color::{
    Color, ColorOrAuto, ColorPropertyValue, ColorScheme, ForcedColorAdjust, PrintColorAdjust,
};
pub use self::column::{ColumnCount, ColumnFill, ColumnSpan};
pub use self::corner_shape::{CornerShape, CornerShapeRect, SuperellipseArg};
pub use self::counters::{Content, ContentItem, CounterIncrement, CounterReset, CounterSet};
pub use self::easing::TimingFunction;
pub use self::effects::{Blend, BoxShadow, Filter, SimpleShadow};
pub use self::flex::FlexBasis;
pub use self::font::{
    FontFamily, FontKerning, FontLanguageOverride, FontOpticalSizing, FontPalette, FontSmoothing,
    FontStyle, FontVariantCaps, FontVariantEmoji, FontVariantPosition, MathShift, MathStyle,
    MathVariant,
};
pub use self::font::{FontFeatureSettings, FontVariantLigatures, FontVariantNumeric};
pub use self::font::{
    FontSize, FontSizeAdjust, FontSizeAdjustFactor, FontSizeKeyword, FontSynthesis,
    FontSynthesisStyle, FontWidth,
};
pub use self::font::{FontVariantAlternates, FontWeight};
pub use self::font::{FontVariantEastAsian, FontVariationSettings, LineHeight};
pub use self::font::{MathDepth, MozScriptMinSize, MozScriptSizeMultiplier, XLang, XTextScale};
pub use self::image::{
    EndingShape as GradientEndingShape, Gradient, Image, ImageDecoding, ImageRendering,
};
pub use self::length::{Length, LengthOrNumber, LengthUnit, NonNegativeLengthOrNumber};
pub use self::length::{LengthOrAuto, LengthPercentage, LengthPercentageOrAuto};
pub use self::length::{Margin, MaxSize, Size};
pub use self::length::{NoCalcLength, ViewportVariant};
pub use self::length::{
    NonNegativeLength, NonNegativeLengthPercentage, NonNegativeLengthPercentageOrAuto,
};
pub use self::list::{ListStylePosition, ListStyleType, Quotes};
pub use self::motion::{OffsetPath, OffsetPosition, OffsetRotate};
pub use self::number::{
    GreaterThanOrEqualToOneNumber, Integer, NoCalcNumber, NonNegativeInteger, NonNegativeNumber,
    Number, PositiveInteger,
};
pub use self::outline::OutlineStyle;
pub use self::page::{PageName, PageOrientation, PageSize, PageSizeOrientation, PaperSize};
pub use self::param::LinkParameters;
pub use self::percentage::{NoCalcPercentage, NonNegativePercentage, Percentage};
pub use self::position::{
    AnchorFunction, AnchorName, AnchorNameIdent, AspectRatio, BoxSizing, FlexDirection, FlexWrap,
    GridAutoFlow, GridTemplateAreas, Inset, ObjectFit, Position, PositionAnchor,
    PositionAnchorKeyword, PositionArea, PositionAreaKeyword, PositionComponent, PositionOrAuto,
    PositionTryFallbacks, PositionTryOrder, PositionVisibility, ScopedName, ZIndex,
};
pub use self::random::{RandomCacheKey, RandomUaIdent};
pub use self::ratio::Ratio;
pub use self::rect::NonNegativeLengthOrNumberRect;
pub use self::resolution::{NoCalcResolution, Resolution};
pub use self::svg::{
    ColorInterpolation, DProperty, MaskComposite, MaskMode, MaskType, MozContextProperties,
    ShapeRendering, StrokeLinecap, StrokeLinejoin, TextAnchor,
};
pub use self::svg::{SVGLength, SVGOpacity, SVGPaint};
pub use self::svg::{SVGPaintOrder, SVGStrokeDashArray, SVGWidth, VectorEffect};
pub use self::svg_path::SVGPathData;
pub use self::table::{BorderCollapse, EmptyCells, TableLayout};
pub use self::text::{
    HyphenateCharacter, HyphenateLimitChars, Hyphens, RubyAlign, RubyPosition, TextCombineUpright,
    TextDecorationStyle, TextRendering, TextSecurity, TextSizeAdjust, TextWrapMode, TextWrapStyle,
    UnicodeBidi, WhiteSpaceCollapse,
};
pub use self::text::{InitialLetter, LetterSpacing, LineBreak, TextAlign, TextIndent};
pub use self::text::{OverflowWrap, TextEmphasisPosition, TextEmphasisStyle, WordBreak};
pub use self::text::{TextAlignKeyword, TextDecorationLine, TextOverflow, WordSpacing};
pub use self::text::{TextAlignLast, TextAutospace, TextUnderlinePosition};
pub use self::text::{TextBoxEdge, TextBoxTrim};
pub use self::text::{
    TextDecorationInset, TextDecorationLength, TextDecorationSkipInk, TextJustify, TextTransform,
};
pub use self::time::{NoCalcTime, Time};
pub use self::transform::{Rotate, Scale, Transform};
pub use self::transform::{TransformBox, TransformOrigin, TransformStyle, Translate};
pub use self::tree_counting::TreeCountingFunction;
#[cfg(feature = "gecko")]
pub use self::ui::CursorImage;
pub use self::ui::{
    BoolInteger, Cursor, FieldSizing, ImeMode, Inert, Interactivity, MozTheme, PointerEvents,
    ScrollbarColor, ScrollbarWidth, UserFocus, UserSelect, WindowDragging, WindowShadow,
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
pub mod corner_shape;
pub mod counters;
pub mod easing;
pub mod effects;
pub mod flex;
pub mod font;
pub mod frequency;
pub mod grid;
pub mod image;
pub mod intersection_observer;
pub mod length;
pub mod list;
pub mod motion;
pub mod number;
pub mod outline;
pub mod page;
pub mod param;
pub mod percentage;
pub mod position;
pub mod random;
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
pub mod tree_counting;
pub mod ui;
pub mod url;

/// <angle> | <percentage>
/// https://drafts.csswg.org/css-values/#typedef-angle-percentage
#[allow(missing_docs)]
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
pub enum AngleOrPercentage {
    Percentage(Percentage),
    Angle(Angle),
}

impl AngleOrPercentage {
    fn parse_internal(
        context: &ParserContext,
        input: &mut Parser,
        allow_unitless_zero: AllowUnitlessZeroAngle,
    ) -> Result<Self, ParseError> {
        if let Ok(per) = input.try_parse(|i| Percentage::parse(context, i)) {
            return Ok(AngleOrPercentage::Percentage(per));
        }

        Angle::parse_internal(context, input, allow_unitless_zero).map(AngleOrPercentage::Angle)
    }

    /// Allow unitless angles, used for conic-gradients as specified by the spec.
    /// https://drafts.csswg.org/css-images-4/#valdef-conic-gradient-angle
    pub fn parse_with_unitless(
        context: &ParserContext,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        AngleOrPercentage::parse_internal(context, input, AllowUnitlessZeroAngle::Yes)
    }
}

impl Parse for AngleOrPercentage {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        AngleOrPercentage::parse_internal(context, input, AllowUnitlessZeroAngle::No)
    }
}

/// <number> | <percentage>
///
/// Accepts only non-negative numbers.
///
/// TODO(Bug 2040559) - Convert this into a NumericUnion, instead of an enum over
/// Number and Percentage. Both types are also NumericUnions of unitless floats.
#[allow(missing_docs)]
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
pub enum NumberOrPercentage {
    Percentage(Percentage),
    Number(Number),
}

impl NumberOrPercentage {
    fn parse_with_clamping_mode(
        context: &ParserContext,
        input: &mut Parser,
        type_: AllowedNumericType,
    ) -> Result<Self, ParseError> {
        if let Ok(per) =
            input.try_parse(|i| Percentage::parse_with_clamping_mode(context, i, type_))
        {
            return Ok(NumberOrPercentage::Percentage(per));
        }

        parse_number_with_clamping_mode(
            context,
            input,
            type_,
            PercentageContext::allowed_with_hint(NumericBaseType::Percent),
        )
        .map(NumberOrPercentage::Number)
    }

    /// Parse a non-negative number or percentage.
    pub fn parse_non_negative(
        context: &ParserContext,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        Self::parse_with_clamping_mode(context, input, AllowedNumericType::NonNegative)
    }

    /// Convert the number or the percentage to a number.
    pub fn to_percentage(self) -> Option<Percentage> {
        match self {
            Self::Percentage(p) => Some(p),
            Self::Number(n) => n.to_percentage(),
        }
    }

    /// Convert the number or the percentage to a number.
    pub fn to_number(&self) -> Option<Number> {
        match self {
            Self::Percentage(p) => p.to_number(),
            Self::Number(n) => Some(n.clone()),
        }
    }

    /// Gets a reference to the underlying percentage, or None if this is a number
    pub fn as_percentage(&self) -> Option<&Percentage> {
        match self {
            NumberOrPercentage::Percentage(percentage) => Some(percentage),
            _ => None,
        }
    }

    /// If this is a non-calc percentage, replaces it with the equivalent
    /// number; otherwise, returns the original value.
    pub fn into_simplified_number(self) -> NumberOrPercentage {
        match self.as_percentage().and_then(|p| p.get()) {
            Some(p) => NumberOrPercentage::Number(Number::new(p)),
            None => self,
        }
    }

    /// Attempts to resolve this number or percentage to a computed value.
    pub fn to_computed_value_without_context(&self) -> Result<computed::NumberOrPercentage, ()> {
        Ok(match self {
            NumberOrPercentage::Percentage(percentage) => computed::NumberOrPercentage::Percentage(
                computed::Percentage(percentage.resolve().ok_or(())?),
            ),
            NumberOrPercentage::Number(number) => {
                computed::NumberOrPercentage::Number(number.resolve().ok_or(())?)
            },
        })
    }
}

impl Parse for NumberOrPercentage {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
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
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(NonNegative(NumberOrPercentage::parse_non_negative(
            context, input,
        )?))
    }
}

/// A specified CSS `opacity`
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
pub struct Opacity(NumberOrPercentage);

impl Parse for Opacity {
    /// Opacity accepts <number> | <percentage>, so we parse it as NumberOrPercentage,
    /// and then convert into an Number if it's a non-calc Percentage.
    /// https://drafts.csswg.org/css-color-4/#serializing-opacity-values
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(Opacity(
            NumberOrPercentage::parse(context, input)?.into_simplified_number(),
        ))
    }
}

impl ToComputedValue for Opacity {
    type ComputedValue = CSSFloat;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> CSSFloat {
        let value = self.0.to_computed_value(context).value();
        if context.for_smil_animation {
            // SMIL expects to be able to interpolate between out-of-range
            // opacity values.
            value
        } else {
            value.min(1.0).max(0.0)
        }
    }

    #[inline]
    fn from_computed_value(computed: &CSSFloat) -> Self {
        Opacity(NumberOrPercentage::Number(Number::from_computed_value(
            computed,
        )))
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
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Self::parse_quirky(context, input, AllowQuirks::No)
    }
}

impl ClipRect {
    /// Parses a rect(<top>, <left>, <bottom>, <right>), allowing quirks.
    fn parse_quirky(
        context: &ParserContext,
        input: &mut Parser,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError> {
        input.expect_function_matching("rect")?;

        fn parse_argument(
            context: &ParserContext,
            input: &mut Parser,
            allow_quirks: AllowQuirks,
        ) -> Result<LengthOrAuto, ParseError> {
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
    pub fn parse_quirky(
        context: &ParserContext,
        input: &mut Parser,
        allow_quirks: AllowQuirks,
    ) -> Result<Self, ParseError> {
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
    pub fn parse(
        namespaces: &FxHashMap<Prefix, Namespace>,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        // We don't need to keep the prefix because different
        // prefixes can resolve to the same id. Additionally,
        // we also don't need it for serialization as substitution
        // functions serialize from the direct css declaration.
        parse_namespace(namespaces, input).map(|(_prefix, namespace)| namespace)
    }
}

impl Default for ParsedNamespace {
    fn default() -> Self {
        Self::Known(Namespace::default())
    }
}

/// Try to parse a namespace and return it if parsed, or none if there was not one present
pub fn parse_namespace(
    namespaces: &FxHashMap<Prefix, Namespace>,
    input: &mut Parser,
) -> Result<(Prefix, ParsedNamespace), ParseError> {
    let ns_prefix = match input.next()? {
        Token::Ident(prefix) => Some(Prefix::from(prefix.as_ref())),
        Token::Delim('|') => None,
        _ => return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError)),
    };

    if ns_prefix.is_some() && !matches!(*input.next_including_whitespace()?, Token::Delim('|')) {
        return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
    }

    if let Some(prefix) = ns_prefix {
        let ns = match namespaces.get(&prefix).cloned() {
            Some(ns) => ParsedNamespace::Known(ns),
            None => ParsedNamespace::Unknown,
        };
        Ok((prefix, ns))
    } else {
        Ok((Prefix::default(), ParsedNamespace::default()))
    }
}
