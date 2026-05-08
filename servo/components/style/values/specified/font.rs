/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified values for font properties

use crate::context::QuirksMode;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::font::{FamilyName, FontFamilyList, SingleFontFamily};
use crate::values::computed::Percentage as ComputedPercentage;
use crate::values::computed::{font as computed, Length, NonNegativeLength};
use crate::values::computed::{CSSPixelLength, Context, ToComputedValue};
use crate::values::generics::font::{
    self as generics, FeatureTagValue, FontSettings, FontTag, GenericLineHeight, VariationValue,
};
use crate::values::generics::NonNegative;
use crate::values::specified::length::{FontBaseSize, LineHeightBase, PX_PER_PT};
use crate::values::specified::{AllowQuirks, Angle, Integer, LengthPercentage};
use crate::values::specified::{
    FontRelativeLength, NoCalcLength, NonNegativeLengthPercentage, NonNegativeNumber,
    NonNegativePercentage, Number,
};
use crate::values::{serialize_atom_identifier, CustomIdent, SelectorParseErrorKind};
use crate::Atom;
use cssparser::{match_ignore_ascii_case, Parser, Token};
#[cfg(feature = "gecko")]
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps, MallocUnconditionalSizeOf};
use std::fmt::{self, Write};
use style_traits::{CssWriter, KeywordsCollectFn, ParseError};
use style_traits::{SpecifiedValueInfo, StyleParseErrorKind, ToCss};

// FIXME(emilio): The system font code is copy-pasta, and should be cleaned up.
macro_rules! system_font_methods {
    ($ty:ident, $field:ident) => {
        system_font_methods!($ty);

        fn compute_system(&self, _context: &Context) -> <$ty as ToComputedValue>::ComputedValue {
            debug_assert!(matches!(*self, $ty::System(..)));
            #[cfg(feature = "gecko")]
            {
                _context.cached_system_font.as_ref().unwrap().$field.clone()
            }
            #[cfg(feature = "servo")]
            {
                unreachable!()
            }
        }
    };

    ($ty:ident) => {
        /// Get a specified value that represents a system font.
        pub fn system_font(f: SystemFont) -> Self {
            $ty::System(f)
        }

        /// Retreive a SystemFont from the specified value.
        pub fn get_system(&self) -> Option<SystemFont> {
            if let $ty::System(s) = *self {
                Some(s)
            } else {
                None
            }
        }
    };
}

/// System fonts.
#[repr(u8)]
#[derive(
    Clone, Copy, Debug, Eq, Hash, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem,
)]
#[allow(missing_docs)]
#[cfg(feature = "gecko")]
pub enum SystemFont {
    /// https://drafts.csswg.org/css-fonts/#valdef-font-caption
    Caption,
    /// https://drafts.csswg.org/css-fonts/#valdef-font-icon
    Icon,
    /// https://drafts.csswg.org/css-fonts/#valdef-font-menu
    Menu,
    /// https://drafts.csswg.org/css-fonts/#valdef-font-message-box
    MessageBox,
    /// https://drafts.csswg.org/css-fonts/#valdef-font-small-caption
    SmallCaption,
    /// https://drafts.csswg.org/css-fonts/#valdef-font-status-bar
    StatusBar,
    /// Internal system font, used by the `<menupopup>`s on macOS.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozPullDownMenu,
    /// Internal system font, used for `<button>` elements.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozButton,
    /// Internal font, used by `<select>` elements.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozList,
    /// Internal font, used by `<input>` elements.
    #[parse(condition = "ParserContext::chrome_rules_enabled")]
    MozField,
    #[css(skip)]
    End, // Just for indexing purposes.
}

// We don't parse system fonts in servo, but in the interest of not
// littering a lot of code with `if engine == "gecko"` conditionals,
// we have a dummy system font module that does nothing

#[derive(
    Clone, Copy, Debug, Eq, Hash, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem,
)]
#[allow(missing_docs)]
#[cfg(feature = "servo")]
/// void enum for system font, can never exist
pub enum SystemFont {}

#[allow(missing_docs)]
#[cfg(feature = "servo")]
impl SystemFont {
    pub fn parse(_: &mut Parser) -> Result<Self, ()> {
        Err(())
    }
}

const DEFAULT_SCRIPT_MIN_SIZE_PT: u32 = 8;
const DEFAULT_SCRIPT_SIZE_MULTIPLIER: f64 = 0.71;

/// The minimum font-weight value per:
///
/// https://drafts.csswg.org/css-fonts-4/#font-weight-numeric-values
pub const MIN_FONT_WEIGHT: f32 = 1.;

/// The maximum font-weight value per:
///
/// https://drafts.csswg.org/css-fonts-4/#font-weight-numeric-values
pub const MAX_FONT_WEIGHT: f32 = 1000.;

/// A specified font-weight value.
///
/// https://drafts.csswg.org/css-fonts-4/#propdef-font-weight
#[derive(
    Clone, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub enum FontWeight {
    /// `<font-weight-absolute>`
    Absolute(AbsoluteFontWeight),
    /// Bolder variant
    Bolder,
    /// Lighter variant
    Lighter,
    /// System font variant.
    #[css(skip)]
    System(SystemFont),
}

impl FontWeight {
    system_font_methods!(FontWeight, font_weight);

    /// `normal`
    #[inline]
    pub fn normal() -> Self {
        FontWeight::Absolute(AbsoluteFontWeight::Normal)
    }

    /// Get a specified FontWeight from a gecko keyword
    pub fn from_gecko_keyword(kw: u32) -> Self {
        debug_assert!(kw % 100 == 0);
        debug_assert!(kw as f32 <= MAX_FONT_WEIGHT);
        FontWeight::Absolute(AbsoluteFontWeight::Weight(Number::new(kw as f32)))
    }
}

impl ToComputedValue for FontWeight {
    type ComputedValue = computed::FontWeight;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            FontWeight::Absolute(ref abs) => abs.to_computed_value(context),
            FontWeight::Bolder => context
                .builder
                .get_parent_font()
                .clone_font_weight()
                .bolder(),
            FontWeight::Lighter => context
                .builder
                .get_parent_font()
                .clone_font_weight()
                .lighter(),
            FontWeight::System(_) => self.compute_system(context),
        }
    }

    #[inline]
    fn from_computed_value(computed: &computed::FontWeight) -> Self {
        FontWeight::Absolute(AbsoluteFontWeight::from_computed_value(computed))
    }
}

/// An absolute font-weight value for a @font-face rule.
///
/// https://drafts.csswg.org/css-fonts-4/#font-weight-absolute-values
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
pub enum AbsoluteFontWeight {
    /// A `<number>`, with the additional constraints specified in:
    ///
    ///   https://drafts.csswg.org/css-fonts-4/#font-weight-numeric-values
    Weight(Number),
    /// Normal font weight. Same as 400.
    Normal,
    /// Bold font weight. Same as 700.
    Bold,
}

impl AbsoluteFontWeight {
    /// Returns the computed weight for use when computed value context is unavailable.
    /// Returns None if the weight is a calc expression that requires computed-value context.
    pub fn compute(&self) -> Option<computed::FontWeight> {
        match self {
            AbsoluteFontWeight::Weight(weight) => {
                Some(computed::FontWeight::from_float(weight.resolve()?))
            },
            AbsoluteFontWeight::Normal => Some(computed::FontWeight::NORMAL),
            AbsoluteFontWeight::Bold => Some(computed::FontWeight::BOLD),
        }
    }
}

impl ToComputedValue for AbsoluteFontWeight {
    type ComputedValue = computed::FontWeight;

    fn to_computed_value(&self, context: &Context) -> computed::FontWeight {
        match self {
            AbsoluteFontWeight::Weight(weight) => {
                computed::FontWeight::from_float(weight.to_computed_value(context))
            },
            AbsoluteFontWeight::Normal => computed::FontWeight::NORMAL,
            AbsoluteFontWeight::Bold => computed::FontWeight::BOLD,
        }
    }

    fn from_computed_value(computed: &computed::FontWeight) -> Self {
        AbsoluteFontWeight::Weight(Number::from_computed_value(&computed.value()))
    }
}

impl Parse for AbsoluteFontWeight {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(number) = input.try_parse(|input| Number::parse(context, input)) {
            // We could add another AllowedNumericType value, but it doesn't
            // seem worth it just for a single property with such a weird range,
            // so we do the clamping here manually.
            if matches!(number.get(), Some(v) if v < MIN_FONT_WEIGHT || v > MAX_FONT_WEIGHT) {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
            return Ok(AbsoluteFontWeight::Weight(number));
        }

        Ok(try_match_ident_ignore_ascii_case! { input,
            "normal" => AbsoluteFontWeight::Normal,
            "bold" => AbsoluteFontWeight::Bold,
        })
    }
}

/// The specified value of the `font-style` property, without the system font
/// crap.
pub type SpecifiedFontStyle = generics::FontStyle<Angle>;

impl ToCss for SpecifiedFontStyle {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            generics::FontStyle::Italic => dest.write_str("italic"),
            generics::FontStyle::Oblique(ref angle) => {
                // Not angle.is_zero() because we don't want to serialize
                // `oblique calc(0deg)` as `normal`.
                if *angle == Angle::zero() {
                    dest.write_str("normal")?;
                } else {
                    dest.write_str("oblique")?;
                    if *angle != Self::default_angle() {
                        dest.write_char(' ')?;
                        angle.to_css(dest)?;
                    }
                }
                Ok(())
            },
        }
    }
}

impl Parse for SpecifiedFontStyle {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(try_match_ident_ignore_ascii_case! { input,
            "normal" => generics::FontStyle::normal(),
            "italic" => generics::FontStyle::Italic,
            "oblique" => {
                let angle = input.try_parse(|input| Self::parse_angle(context, input))
                    .unwrap_or_else(|_| Self::default_angle());

                generics::FontStyle::Oblique(angle)
            },
        })
    }
}

impl ToComputedValue for SpecifiedFontStyle {
    type ComputedValue = computed::FontStyle;

    fn to_computed_value(&self, _: &Context) -> Self::ComputedValue {
        match *self {
            Self::Italic => computed::FontStyle::ITALIC,
            Self::Oblique(ref angle) => computed::FontStyle::oblique(angle.degrees()),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        if *computed == computed::FontStyle::ITALIC {
            return Self::Italic;
        }
        let degrees = computed.oblique_degrees();
        generics::FontStyle::Oblique(Angle::from_degrees(degrees, /* was_calc = */ false))
    }
}

/// From https://drafts.csswg.org/css-fonts-4/#valdef-font-style-oblique-angle:
///
///     Values less than -90deg or values greater than 90deg are
///     invalid and are treated as parse errors.
///
/// The maximum angle value that `font-style: oblique` should compute to.
pub const FONT_STYLE_OBLIQUE_MAX_ANGLE_DEGREES: f32 = 90.;

/// The minimum angle value that `font-style: oblique` should compute to.
pub const FONT_STYLE_OBLIQUE_MIN_ANGLE_DEGREES: f32 = -90.;

impl SpecifiedFontStyle {
    /// Gets a clamped angle in degrees from a specified Angle.
    pub fn compute_angle_degrees(angle: &Angle) -> f32 {
        angle
            .degrees()
            .max(FONT_STYLE_OBLIQUE_MIN_ANGLE_DEGREES)
            .min(FONT_STYLE_OBLIQUE_MAX_ANGLE_DEGREES)
    }

    /// Parse a suitable angle for font-style: oblique.
    pub fn parse_angle<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Angle, ParseError<'i>> {
        let angle = Angle::parse(context, input)?;
        if angle.was_calc() {
            return Ok(angle);
        }

        let degrees = angle.degrees();
        if degrees < FONT_STYLE_OBLIQUE_MIN_ANGLE_DEGREES
            || degrees > FONT_STYLE_OBLIQUE_MAX_ANGLE_DEGREES
        {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        return Ok(angle);
    }

    /// The default angle for `font-style: oblique`.
    pub fn default_angle() -> Angle {
        Angle::from_degrees(
            computed::FontStyle::DEFAULT_OBLIQUE_DEGREES as f32,
            /* was_calc = */ false,
        )
    }
}

/// The specified value of the `font-style` property.
#[derive(
    Clone, Copy, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
#[allow(missing_docs)]
#[typed(todo_derive_fields)]
pub enum FontStyle {
    Specified(SpecifiedFontStyle),
    #[css(skip)]
    System(SystemFont),
}

impl FontStyle {
    /// Return the `normal` value.
    #[inline]
    pub fn normal() -> Self {
        FontStyle::Specified(generics::FontStyle::normal())
    }

    system_font_methods!(FontStyle, font_style);
}

impl ToComputedValue for FontStyle {
    type ComputedValue = computed::FontStyle;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            FontStyle::Specified(ref specified) => specified.to_computed_value(context),
            FontStyle::System(..) => self.compute_system(context),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        FontStyle::Specified(SpecifiedFontStyle::from_computed_value(computed))
    }
}

/// A value for the `font-stretch` property.
///
/// https://drafts.csswg.org/css-fonts-4/#font-stretch-prop
#[allow(missing_docs)]
#[derive(
    Clone, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub enum FontStretch {
    Stretch(NonNegativePercentage),
    Keyword(FontStretchKeyword),
    #[css(skip)]
    System(SystemFont),
}

/// A keyword value for `font-stretch`.
#[derive(
    Clone, Copy, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
#[allow(missing_docs)]
pub enum FontStretchKeyword {
    Normal,
    Condensed,
    UltraCondensed,
    ExtraCondensed,
    SemiCondensed,
    SemiExpanded,
    Expanded,
    ExtraExpanded,
    UltraExpanded,
}

impl FontStretchKeyword {
    /// Turns the keyword into a computed value.
    pub fn compute(&self) -> computed::FontStretch {
        computed::FontStretch::from_keyword(*self)
    }

    /// Does the opposite operation to `compute`, in order to serialize keywords
    /// if possible.
    pub fn from_percentage(p: f32) -> Option<Self> {
        computed::FontStretch::from_percentage(p).as_keyword()
    }
}

impl FontStretch {
    /// `normal`.
    pub fn normal() -> Self {
        FontStretch::Keyword(FontStretchKeyword::Normal)
    }

    system_font_methods!(FontStretch, font_stretch);
}

impl ToComputedValue for FontStretch {
    type ComputedValue = computed::FontStretch;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            FontStretch::Stretch(ref percentage) => {
                let percentage = percentage.to_computed_value(context).0;
                computed::FontStretch::from_percentage(percentage.0)
            },
            FontStretch::Keyword(ref kw) => kw.compute(),
            FontStretch::System(_) => self.compute_system(context),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        FontStretch::Stretch(NonNegativePercentage::from_computed_value(&NonNegative(
            computed.to_percentage(),
        )))
    }
}

/// CSS font keywords
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    Serialize,
    Deserialize,
    ToTyped,
)]
#[allow(missing_docs)]
#[repr(u8)]
pub enum FontSizeKeyword {
    #[css(keyword = "xx-small")]
    XXSmall,
    XSmall,
    Small,
    Medium,
    Large,
    XLarge,
    #[css(keyword = "xx-large")]
    XXLarge,
    #[css(keyword = "xxx-large")]
    XXXLarge,
    /// Indicate whether to apply font-size: math is specified so that extra
    /// scaling due to math-depth changes is applied during the cascade.
    #[cfg(feature = "gecko")]
    Math,
    #[css(skip)]
    None,
}

impl FontSizeKeyword {
    /// Convert to an HTML <font size> value
    #[inline]
    pub fn html_size(self) -> u8 {
        self as u8
    }

    /// Returns true if the font size is the math keyword
    #[cfg(feature = "gecko")]
    pub fn is_math(self) -> bool {
        matches!(self, Self::Math)
    }

    /// Returns true if the font size is the math keyword
    #[cfg(feature = "servo")]
    pub fn is_math(self) -> bool {
        false
    }
}

impl Default for FontSizeKeyword {
    fn default() -> Self {
        FontSizeKeyword::Medium
    }
}

#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    PartialEq,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[cfg_attr(feature = "servo", derive(Serialize, Deserialize))]
/// Additional information for keyword-derived font sizes.
pub struct KeywordInfo {
    /// The keyword used
    pub kw: FontSizeKeyword,
    /// A factor to be multiplied by the computed size of the keyword
    #[css(skip)]
    pub factor: f32,
    /// An additional fixed offset to add to the kw * factor in the case of
    /// `calc()`.
    #[css(skip)]
    pub offset: CSSPixelLength,
}

impl KeywordInfo {
    /// KeywordInfo value for font-size: medium
    pub fn medium() -> Self {
        Self::new(FontSizeKeyword::Medium)
    }

    /// KeywordInfo value for font-size: none
    pub fn none() -> Self {
        Self::new(FontSizeKeyword::None)
    }

    fn new(kw: FontSizeKeyword) -> Self {
        KeywordInfo {
            kw,
            factor: 1.,
            offset: CSSPixelLength::new(0.),
        }
    }

    /// Computes the final size for this font-size keyword, accounting for
    /// text-zoom.
    fn to_computed_value(&self, context: &Context) -> CSSPixelLength {
        debug_assert_ne!(self.kw, FontSizeKeyword::None);
        #[cfg(feature = "gecko")]
        debug_assert_ne!(self.kw, FontSizeKeyword::Math);
        let base = context.maybe_zoom_text(self.kw.to_length(context).0);
        let zoom_factor = context.style().effective_zoom.value();
        CSSPixelLength::new(base.px() * self.factor * zoom_factor)
            + context.maybe_zoom_text(self.offset)
    }

    /// Given a parent keyword info (self), apply an additional factor/offset to
    /// it.
    fn compose(self, factor: f32) -> Self {
        if self.kw == FontSizeKeyword::None {
            return self;
        }
        KeywordInfo {
            kw: self.kw,
            factor: self.factor * factor,
            offset: self.offset * factor,
        }
    }
}

impl SpecifiedValueInfo for KeywordInfo {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        <FontSizeKeyword as SpecifiedValueInfo>::collect_completion_keywords(f);
    }
}

#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
/// A specified font-size value
pub enum FontSize {
    /// A length; e.g. 10px.
    Length(LengthPercentage),
    /// A keyword value, along with a ratio and absolute offset.
    /// The ratio in any specified keyword value
    /// will be 1 (with offset 0), but we cascade keywordness even
    /// after font-relative (percent and em) values
    /// have been applied, which is where the ratio
    /// comes in. The offset comes in if we cascaded a calc value,
    /// where the font-relative portion (em and percentage) will
    /// go into the ratio, and the remaining units all computed together
    /// will go into the offset.
    /// See bug 1355707.
    Keyword(KeywordInfo),
    /// font-size: smaller
    Smaller,
    /// font-size: larger
    Larger,
    /// Derived from a specified system font.
    #[css(skip)]
    System(SystemFont),
}

/// Specifies a prioritized list of font family names or generic family names.
#[derive(Clone, Debug, Eq, PartialEq, ToCss, ToShmem, ToTyped)]
#[cfg_attr(feature = "servo", derive(Hash))]
#[typed(todo_derive_fields)]
pub enum FontFamily {
    /// List of `font-family`
    #[css(comma)]
    Values(#[css(iterable)] FontFamilyList),
    /// System font
    #[css(skip)]
    System(SystemFont),
}

impl FontFamily {
    system_font_methods!(FontFamily, font_family);
}

impl ToComputedValue for FontFamily {
    type ComputedValue = computed::FontFamily;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match *self {
            FontFamily::Values(ref list) => computed::FontFamily {
                families: list.clone(),
                is_system_font: false,
                is_initial: false,
            },
            FontFamily::System(_) => self.compute_system(context),
        }
    }

    fn from_computed_value(other: &computed::FontFamily) -> Self {
        FontFamily::Values(other.families.clone())
    }
}

#[cfg(feature = "gecko")]
impl MallocSizeOf for FontFamily {
    fn size_of(&self, ops: &mut MallocSizeOfOps) -> usize {
        match *self {
            FontFamily::Values(ref v) => {
                // Although the family list is refcounted, we always attribute
                // its size to the specified value.
                v.list.unconditional_size_of(ops)
            },
            FontFamily::System(_) => 0,
        }
    }
}

impl Parse for FontFamily {
    /// <family-name>#
    /// <family-name> = <string> | [ <ident>+ ]
    /// TODO: <generic-family>
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<FontFamily, ParseError<'i>> {
        let values =
            input.parse_comma_separated(|input| SingleFontFamily::parse(context, input))?;
        Ok(FontFamily::Values(FontFamilyList {
            list: crate::ArcSlice::from_iter(values.into_iter()),
        }))
    }
}

impl SpecifiedValueInfo for FontFamily {}

/// `FamilyName::parse` is based on `SingleFontFamily::parse` and not the other
/// way around because we want the former to exclude generic family keywords.
impl Parse for FamilyName {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        match SingleFontFamily::parse(context, input) {
            Ok(SingleFontFamily::FamilyName(name)) => Ok(name),
            Ok(SingleFontFamily::Generic(_)) => {
                Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
            },
            Err(e) => Err(e),
        }
    }
}

/// A factor for one of the font-size-adjust metrics, which may be either a number
/// or the `from-font` keyword.
#[derive(
    Clone, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub enum FontSizeAdjustFactor {
    /// An explicitly-specified number.
    Number(NonNegativeNumber),
    /// The from-font keyword: resolve the number from font metrics.
    FromFont,
}

/// Specified value for font-size-adjust, intended to help
/// preserve the readability of text when font fallback occurs.
///
/// https://drafts.csswg.org/css-fonts-5/#font-size-adjust-prop
pub type FontSizeAdjust = generics::GenericFontSizeAdjust<FontSizeAdjustFactor>;

impl Parse for FontSizeAdjust {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        // First check if we have an adjustment factor without a metrics-basis keyword.
        if let Ok(factor) = input.try_parse(|i| FontSizeAdjustFactor::parse(context, i)) {
            return Ok(Self::ExHeight(factor));
        }

        let ident = input.expect_ident()?;
        let basis = match_ignore_ascii_case! { &ident,
            "none" => return Ok(Self::None),
            // Check for size adjustment basis keywords.
            "ex-height" => Self::ExHeight,
            "cap-height" => Self::CapHeight,
            "ch-width" => Self::ChWidth,
            "ic-width" => Self::IcWidth,
            "ic-height" => Self::IcHeight,
            // Unknown keyword.
            _ => return Err(location.new_custom_error(
                SelectorParseErrorKind::UnexpectedIdent(ident.clone())
            )),
        };

        Ok(basis(FontSizeAdjustFactor::parse(context, input)?))
    }
}

/// This is the ratio applied for font-size: larger
/// and smaller by both Firefox and Chrome
const LARGER_FONT_SIZE_RATIO: f32 = 1.2;

/// The default font size.
pub const FONT_MEDIUM_PX: f32 = 16.0;
/// The default line height.
pub const FONT_MEDIUM_LINE_HEIGHT_PX: f32 = FONT_MEDIUM_PX * 1.2;
/// The default ex height -- https://drafts.csswg.org/css-values/#ex
/// > In the cases where it is impossible or impractical to determine the x-height, a value of 0.5em must be assumed
pub const FONT_MEDIUM_EX_PX: f32 = FONT_MEDIUM_PX * 0.5;
/// The default cap height -- https://drafts.csswg.org/css-values/#cap
/// > In the cases where it is impossible or impractical to determine the cap-height, the font’s ascent must be used
pub const FONT_MEDIUM_CAP_PX: f32 = FONT_MEDIUM_PX;
/// The default advance measure -- https://drafts.csswg.org/css-values/#ch
/// > Thus, the ch unit falls back to 0.5em in the general case
pub const FONT_MEDIUM_CH_PX: f32 = FONT_MEDIUM_PX * 0.5;
/// The default idographic advance measure -- https://drafts.csswg.org/css-values/#ic
/// > In the cases where it is impossible or impractical to determine the ideographic advance measure, it must be assumed to be 1em
pub const FONT_MEDIUM_IC_PX: f32 = FONT_MEDIUM_PX;

impl FontSizeKeyword {
    #[inline]
    fn to_length(&self, cx: &Context) -> NonNegativeLength {
        let font = cx.style().get_font();

        #[cfg(feature = "servo")]
        let family = &font.font_family.families;
        #[cfg(feature = "gecko")]
        let family = &font.mFont.family.families;

        let generic = family
            .single_generic()
            .unwrap_or(computed::GenericFontFamily::None);

        #[cfg(feature = "gecko")]
        let base_size = unsafe {
            Atom::with(font.mLanguage.mRawPtr, |language| {
                cx.device().base_size_for_generic(language, generic)
            })
        };
        #[cfg(feature = "servo")]
        let base_size = cx.device().base_size_for_generic(generic);

        self.to_length_without_context(cx.quirks_mode, base_size)
    }

    /// Resolve a keyword length without any context, with explicit arguments.
    #[inline]
    pub fn to_length_without_context(
        &self,
        quirks_mode: QuirksMode,
        base_size: Length,
    ) -> NonNegativeLength {
        #[cfg(feature = "gecko")]
        debug_assert_ne!(*self, FontSizeKeyword::Math);
        // The tables in this function are originally from
        // nsRuleNode::CalcFontPointSize in Gecko:
        //
        // https://searchfox.org/mozilla-central/rev/c05d9d61188d32b8/layout/style/nsRuleNode.cpp#3150
        //
        // Mapping from base size and HTML size to pixels
        // The first index is (base_size - 9), the second is the
        // HTML size. "0" is CSS keyword xx-small, not HTML size 0,
        // since HTML size 0 is the same as 1.
        //
        //  xxs   xs      s      m     l      xl     xxl   -
        //  -     0/1     2      3     4      5      6     7
        static FONT_SIZE_MAPPING: [[i32; 8]; 8] = [
            [9, 9, 9, 9, 11, 14, 18, 27],
            [9, 9, 9, 10, 12, 15, 20, 30],
            [9, 9, 10, 11, 13, 17, 22, 33],
            [9, 9, 10, 12, 14, 18, 24, 36],
            [9, 10, 12, 13, 16, 20, 26, 39],
            [9, 10, 12, 14, 17, 21, 28, 42],
            [9, 10, 13, 15, 18, 23, 30, 45],
            [9, 10, 13, 16, 18, 24, 32, 48],
        ];

        // This table gives us compatibility with WinNav4 for the default fonts only.
        // In WinNav4, the default fonts were:
        //
        //     Times/12pt ==   Times/16px at 96ppi
        //   Courier/10pt == Courier/13px at 96ppi
        //
        // xxs   xs     s      m      l     xl     xxl    -
        // -     1      2      3      4     5      6      7
        static QUIRKS_FONT_SIZE_MAPPING: [[i32; 8]; 8] = [
            [9, 9, 9, 9, 11, 14, 18, 28],
            [9, 9, 9, 10, 12, 15, 20, 31],
            [9, 9, 9, 11, 13, 17, 22, 34],
            [9, 9, 10, 12, 14, 18, 24, 37],
            [9, 9, 10, 13, 16, 20, 26, 40],
            [9, 9, 11, 14, 17, 21, 28, 42],
            [9, 10, 12, 15, 17, 23, 30, 45],
            [9, 10, 13, 16, 18, 24, 32, 48],
        ];

        static FONT_SIZE_FACTORS: [i32; 8] = [60, 75, 89, 100, 120, 150, 200, 300];
        let base_size_px = base_size.px().round() as i32;
        let html_size = self.html_size() as usize;
        NonNegative(if base_size_px >= 9 && base_size_px <= 16 {
            let mapping = if quirks_mode == QuirksMode::Quirks {
                QUIRKS_FONT_SIZE_MAPPING
            } else {
                FONT_SIZE_MAPPING
            };
            Length::new(mapping[(base_size_px - 9) as usize][html_size] as f32)
        } else {
            base_size * FONT_SIZE_FACTORS[html_size] as f32 / 100.0
        })
    }
}

impl FontSize {
    /// <https://html.spec.whatwg.org/multipage/#rules-for-parsing-a-legacy-font-size>
    pub fn from_html_size(size: u8) -> Self {
        FontSize::Keyword(KeywordInfo::new(match size {
            // If value is less than 1, let it be 1.
            0 | 1 => FontSizeKeyword::XSmall,
            2 => FontSizeKeyword::Small,
            3 => FontSizeKeyword::Medium,
            4 => FontSizeKeyword::Large,
            5 => FontSizeKeyword::XLarge,
            6 => FontSizeKeyword::XXLarge,
            // If value is greater than 7, let it be 7.
            _ => FontSizeKeyword::XXXLarge,
        }))
    }

    /// Compute it against a given base font size
    pub fn to_computed_value_against(
        &self,
        context: &Context,
        base_size: FontBaseSize,
        line_height_base: LineHeightBase,
    ) -> computed::FontSize {
        let compose_keyword = |factor| {
            context
                .style()
                .get_parent_font()
                .clone_font_size()
                .keyword_info
                .compose(factor)
        };
        let mut info = KeywordInfo::none();
        let size = match *self {
            FontSize::Length(LengthPercentage::Length(ref l)) => {
                if let NoCalcLength::FontRelative(ref value) = *l {
                    if let FontRelativeLength::Em(em) = *value {
                        // If the parent font was keyword-derived, this is
                        // too. Tack the em unit onto the factor
                        info = compose_keyword(em);
                    }
                }
                let result =
                    l.to_computed_value_with_base_size(context, base_size, line_height_base);
                if l.should_zoom_text() {
                    context.maybe_zoom_text(result)
                } else {
                    result
                }
            },
            FontSize::Length(LengthPercentage::Percentage(pc)) => {
                // If the parent font was keyword-derived, this is too.
                // Tack the % onto the factor
                info = compose_keyword(pc.get());
                (base_size.resolve(context).computed_size() * pc.get()).normalized()
            },
            FontSize::Length(LengthPercentage::Calc(ref calc)) => {
                let calc = calc.to_computed_value_zoomed(context, base_size, line_height_base);
                calc.resolve(base_size.resolve(context).computed_size())
            },
            FontSize::Keyword(i) => {
                if i.kw.is_math() {
                    // Scaling is done in recompute_math_font_size_if_needed().
                    info = compose_keyword(1.);
                    // i.kw will always be FontSizeKeyword::Math here. But writing it this
                    // allows this code to compile for servo where the Math variant is cfg'd out.
                    info.kw = i.kw;
                    FontRelativeLength::Em(1.).to_computed_value(
                        context,
                        base_size,
                        line_height_base,
                    )
                } else {
                    // As a specified keyword, this is keyword derived
                    info = i;
                    i.to_computed_value(context).clamp_to_non_negative()
                }
            },
            FontSize::Smaller => {
                info = compose_keyword(1. / LARGER_FONT_SIZE_RATIO);
                FontRelativeLength::Em(1. / LARGER_FONT_SIZE_RATIO).to_computed_value(
                    context,
                    base_size,
                    line_height_base,
                )
            },
            FontSize::Larger => {
                info = compose_keyword(LARGER_FONT_SIZE_RATIO);
                FontRelativeLength::Em(LARGER_FONT_SIZE_RATIO).to_computed_value(
                    context,
                    base_size,
                    line_height_base,
                )
            },

            FontSize::System(_) => {
                #[cfg(feature = "servo")]
                {
                    unreachable!()
                }
                #[cfg(feature = "gecko")]
                {
                    context
                        .cached_system_font
                        .as_ref()
                        .unwrap()
                        .font_size
                        .computed_size()
                        .zoom(context.builder.effective_zoom)
                }
            },
        };
        computed::FontSize {
            computed_size: NonNegative(size),
            used_size: NonNegative(size),
            keyword_info: info,
        }
    }
}

impl ToComputedValue for FontSize {
    type ComputedValue = computed::FontSize;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> computed::FontSize {
        self.to_computed_value_against(
            context,
            FontBaseSize::InheritedStyle,
            LineHeightBase::InheritedStyle,
        )
    }

    #[inline]
    fn from_computed_value(computed: &computed::FontSize) -> Self {
        FontSize::Length(LengthPercentage::Length(
            ToComputedValue::from_computed_value(&computed.computed_size()),
        ))
    }
}

impl FontSize {
    system_font_methods!(FontSize);

    /// Get initial value for specified font size.
    #[inline]
    pub fn medium() -> Self {
        FontSize::Keyword(KeywordInfo::medium())
    }

    /// Parses a font-size, with quirks.
    pub fn parse_quirky<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_quirks: AllowQuirks,
    ) -> Result<FontSize, ParseError<'i>> {
        if let Ok(lp) = input
            .try_parse(|i| LengthPercentage::parse_non_negative_quirky(context, i, allow_quirks))
        {
            return Ok(FontSize::Length(lp));
        }

        if let Ok(kw) = input.try_parse(|i| FontSizeKeyword::parse(i)) {
            return Ok(FontSize::Keyword(KeywordInfo::new(kw)));
        }

        try_match_ident_ignore_ascii_case! { input,
            "smaller" => Ok(FontSize::Smaller),
            "larger" => Ok(FontSize::Larger),
        }
    }
}

impl Parse for FontSize {
    /// <length> | <percentage> | <absolute-size> | <relative-size>
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<FontSize, ParseError<'i>> {
        FontSize::parse_quirky(context, input, AllowQuirks::No)
    }
}

bitflags! {
    #[derive(Clone, Copy)]
    /// Flags of variant alternates in bit
    struct VariantAlternatesParsingFlags: u8 {
        /// None of variant alternates enabled
        const NORMAL = 0;
        /// Historical forms
        const HISTORICAL_FORMS = 0x01;
        /// Stylistic Alternates
        const STYLISTIC = 0x02;
        /// Stylistic Sets
        const STYLESET = 0x04;
        /// Character Variant
        const CHARACTER_VARIANT = 0x08;
        /// Swash glyphs
        const SWASH = 0x10;
        /// Ornaments glyphs
        const ORNAMENTS = 0x20;
        /// Annotation forms
        const ANNOTATION = 0x40;
    }
}

#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
/// Set of variant alternates
pub enum VariantAlternates {
    /// Enables display of stylistic alternates
    #[css(function)]
    Stylistic(CustomIdent),
    /// Enables display with stylistic sets
    #[css(comma, function)]
    Styleset(#[css(iterable)] crate::OwnedSlice<CustomIdent>),
    /// Enables display of specific character variants
    #[css(comma, function)]
    CharacterVariant(#[css(iterable)] crate::OwnedSlice<CustomIdent>),
    /// Enables display of swash glyphs
    #[css(function)]
    Swash(CustomIdent),
    /// Enables replacement of default glyphs with ornaments
    #[css(function)]
    Ornaments(CustomIdent),
    /// Enables display of alternate annotation forms
    #[css(function)]
    Annotation(CustomIdent),
    /// Enables display of historical forms
    HistoricalForms,
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
#[repr(transparent)]
#[typed(todo_derive_fields)]
/// List of Variant Alternates
pub struct FontVariantAlternates(
    #[css(if_empty = "normal", iterable)] crate::OwnedSlice<VariantAlternates>,
);

impl FontVariantAlternates {
    /// Returns the length of all variant alternates.
    pub fn len(&self) -> usize {
        self.0.iter().fold(0, |acc, alternate| match *alternate {
            VariantAlternates::Swash(_)
            | VariantAlternates::Stylistic(_)
            | VariantAlternates::Ornaments(_)
            | VariantAlternates::Annotation(_) => acc + 1,
            VariantAlternates::Styleset(ref slice)
            | VariantAlternates::CharacterVariant(ref slice) => acc + slice.len(),
            _ => acc,
        })
    }
}

impl Parse for FontVariantAlternates {
    /// normal |
    ///  [ stylistic(<feature-value-name>)           ||
    ///    historical-forms                          ||
    ///    styleset(<feature-value-name> #)          ||
    ///    character-variant(<feature-value-name> #) ||
    ///    swash(<feature-value-name>)               ||
    ///    ornaments(<feature-value-name>)           ||
    ///    annotation(<feature-value-name>) ]
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<FontVariantAlternates, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(Default::default());
        }

        let mut stylistic = None;
        let mut historical = None;
        let mut styleset = None;
        let mut character_variant = None;
        let mut swash = None;
        let mut ornaments = None;
        let mut annotation = None;

        // Parse values for the various alternate types in any order.
        let mut parsed_alternates = VariantAlternatesParsingFlags::empty();
        macro_rules! check_if_parsed(
            ($input:expr, $flag:path) => (
                if parsed_alternates.contains($flag) {
                    return Err($input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
                }
                parsed_alternates |= $flag;
            )
        );
        while let Ok(_) = input.try_parse(|input| match *input.next()? {
            Token::Ident(ref value) if value.eq_ignore_ascii_case("historical-forms") => {
                check_if_parsed!(input, VariantAlternatesParsingFlags::HISTORICAL_FORMS);
                historical = Some(VariantAlternates::HistoricalForms);
                Ok(())
            },
            Token::Function(ref name) => {
                let name = name.clone();
                input.parse_nested_block(|i| {
                    match_ignore_ascii_case! { &name,
                        "swash" => {
                            check_if_parsed!(i, VariantAlternatesParsingFlags::SWASH);
                            let ident = CustomIdent::parse(i, &[])?;
                            swash = Some(VariantAlternates::Swash(ident));
                            Ok(())
                        },
                        "stylistic" => {
                            check_if_parsed!(i, VariantAlternatesParsingFlags::STYLISTIC);
                            let ident = CustomIdent::parse(i, &[])?;
                            stylistic = Some(VariantAlternates::Stylistic(ident));
                            Ok(())
                        },
                        "ornaments" => {
                            check_if_parsed!(i, VariantAlternatesParsingFlags::ORNAMENTS);
                            let ident = CustomIdent::parse(i, &[])?;
                            ornaments = Some(VariantAlternates::Ornaments(ident));
                            Ok(())
                        },
                        "annotation" => {
                            check_if_parsed!(i, VariantAlternatesParsingFlags::ANNOTATION);
                            let ident = CustomIdent::parse(i, &[])?;
                            annotation = Some(VariantAlternates::Annotation(ident));
                            Ok(())
                        },
                        "styleset" => {
                            check_if_parsed!(i, VariantAlternatesParsingFlags::STYLESET);
                            let idents = i.parse_comma_separated(|i| {
                                CustomIdent::parse(i, &[])
                            })?;
                            styleset = Some(VariantAlternates::Styleset(idents.into()));
                            Ok(())
                        },
                        "character-variant" => {
                            check_if_parsed!(i, VariantAlternatesParsingFlags::CHARACTER_VARIANT);
                            let idents = i.parse_comma_separated(|i| {
                                CustomIdent::parse(i, &[])
                            })?;
                            character_variant = Some(VariantAlternates::CharacterVariant(idents.into()));
                            Ok(())
                        },
                        _ => return Err(i.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
                    }
                })
            },
            _ => Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
        }) {}

        if parsed_alternates.is_empty() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        // Collect the parsed values in canonical order, so that we'll serialize correctly.
        let mut alternates = Vec::new();
        macro_rules! push_if_some(
            ($value:expr) => (
                if let Some(v) = $value {
                    alternates.push(v);
                }
            )
        );
        push_if_some!(stylistic);
        push_if_some!(historical);
        push_if_some!(styleset);
        push_if_some!(character_variant);
        push_if_some!(swash);
        push_if_some!(ornaments);
        push_if_some!(annotation);

        Ok(FontVariantAlternates(alternates.into()))
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
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    single = "normal",
    mixed = "jis78,jis83,jis90,jis04,simplified,traditional,full-width,proportional-width,ruby",
    validate_mixed = "Self::validate_mixed_flags",
))]
#[repr(C)]
/// Variants for east asian variant
pub struct FontVariantEastAsian(u16);
bitflags! {
    impl FontVariantEastAsian: u16 {
        /// None of the features
        const NORMAL = 0;
        /// Enables rendering of JIS78 forms (OpenType feature: jp78)
        const JIS78  = 1 << 0;
        /// Enables rendering of JIS83 forms (OpenType feature: jp83).
        const JIS83 = 1 << 1;
        /// Enables rendering of JIS90 forms (OpenType feature: jp90).
        const JIS90 = 1 << 2;
        /// Enables rendering of JIS2004 forms (OpenType feature: jp04).
        const JIS04 = 1 << 3;
        /// Enables rendering of simplified forms (OpenType feature: smpl).
        const SIMPLIFIED = 1 << 4;
        /// Enables rendering of traditional forms (OpenType feature: trad).
        const TRADITIONAL = 1 << 5;

        /// These values are exclusive with each other.
        const JIS_GROUP = Self::JIS78.0 | Self::JIS83.0 | Self::JIS90.0 | Self::JIS04.0 | Self::SIMPLIFIED.0 | Self::TRADITIONAL.0;

        /// Enables rendering of full-width variants (OpenType feature: fwid).
        const FULL_WIDTH = 1 << 6;
        /// Enables rendering of proportionally-spaced variants (OpenType feature: pwid).
        const PROPORTIONAL_WIDTH = 1 << 7;
        /// Enables display of ruby variant glyphs (OpenType feature: ruby).
        const RUBY = 1 << 8;
    }
}

impl FontVariantEastAsian {
    /// The number of variants.
    pub const COUNT: usize = 9;

    fn validate_mixed_flags(&self) -> bool {
        if self.contains(Self::FULL_WIDTH | Self::PROPORTIONAL_WIDTH) {
            // full-width and proportional-width are exclusive with each other.
            return false;
        }
        let jis = self.intersection(Self::JIS_GROUP);
        if !jis.is_empty() && !jis.bits().is_power_of_two() {
            return false;
        }
        true
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
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[css(bitflags(
    single = "normal,none",
    mixed = "common-ligatures,no-common-ligatures,discretionary-ligatures,no-discretionary-ligatures,historical-ligatures,no-historical-ligatures,contextual,no-contextual",
    validate_mixed = "Self::validate_mixed_flags",
))]
#[repr(C)]
/// Variants of ligatures
pub struct FontVariantLigatures(u16);
bitflags! {
    impl FontVariantLigatures: u16 {
        /// Specifies that common default features are enabled
        const NORMAL = 0;
        /// Specifies that no features are enabled;
        const NONE = 1;
        /// Enables display of common ligatures
        const COMMON_LIGATURES  = 1 << 1;
        /// Disables display of common ligatures
        const NO_COMMON_LIGATURES  = 1 << 2;
        /// Enables display of discretionary ligatures
        const DISCRETIONARY_LIGATURES = 1 << 3;
        /// Disables display of discretionary ligatures
        const NO_DISCRETIONARY_LIGATURES = 1 << 4;
        /// Enables display of historical ligatures
        const HISTORICAL_LIGATURES = 1 << 5;
        /// Disables display of historical ligatures
        const NO_HISTORICAL_LIGATURES = 1 << 6;
        /// Enables display of contextual alternates
        const CONTEXTUAL = 1 << 7;
        /// Disables display of contextual alternates
        const NO_CONTEXTUAL = 1 << 8;
    }
}

impl FontVariantLigatures {
    /// The number of variants.
    pub const COUNT: usize = 9;

    fn validate_mixed_flags(&self) -> bool {
        // Mixing a value and its disabling value is forbidden.
        if self.contains(Self::COMMON_LIGATURES | Self::NO_COMMON_LIGATURES)
            || self.contains(Self::DISCRETIONARY_LIGATURES | Self::NO_DISCRETIONARY_LIGATURES)
            || self.contains(Self::HISTORICAL_LIGATURES | Self::NO_HISTORICAL_LIGATURES)
            || self.contains(Self::CONTEXTUAL | Self::NO_CONTEXTUAL)
        {
            return false;
        }
        true
    }
}

/// Variants of numeric values
#[derive(
    Clone,
    Copy,
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
    ToTyped,
)]
#[css(bitflags(
    single = "normal",
    mixed = "lining-nums,oldstyle-nums,proportional-nums,tabular-nums,diagonal-fractions,stacked-fractions,ordinal,slashed-zero",
    validate_mixed = "Self::validate_mixed_flags",
))]
#[repr(C)]
pub struct FontVariantNumeric(u8);
bitflags! {
    impl FontVariantNumeric : u8 {
        /// Specifies that common default features are enabled
        const NORMAL = 0;
        /// Enables display of lining numerals.
        const LINING_NUMS = 1 << 0;
        /// Enables display of old-style numerals.
        const OLDSTYLE_NUMS = 1 << 1;
        /// Enables display of proportional numerals.
        const PROPORTIONAL_NUMS = 1 << 2;
        /// Enables display of tabular numerals.
        const TABULAR_NUMS = 1 << 3;
        /// Enables display of lining diagonal fractions.
        const DIAGONAL_FRACTIONS = 1 << 4;
        /// Enables display of lining stacked fractions.
        const STACKED_FRACTIONS = 1 << 5;
        /// Enables display of slashed zeros.
        const SLASHED_ZERO = 1 << 6;
        /// Enables display of letter forms used with ordinal numbers.
        const ORDINAL = 1 << 7;
    }
}

impl FontVariantNumeric {
    /// The number of variants.
    pub const COUNT: usize = 8;

    /// normal |
    ///  [ <numeric-figure-values>   ||
    ///    <numeric-spacing-values>  ||
    ///    <numeric-fraction-values> ||
    ///    ordinal                   ||
    ///    slashed-zero ]
    /// <numeric-figure-values>   = [ lining-nums | oldstyle-nums ]
    /// <numeric-spacing-values>  = [ proportional-nums | tabular-nums ]
    /// <numeric-fraction-values> = [ diagonal-fractions | stacked-fractions ]
    fn validate_mixed_flags(&self) -> bool {
        if self.contains(Self::LINING_NUMS | Self::OLDSTYLE_NUMS)
            || self.contains(Self::PROPORTIONAL_NUMS | Self::TABULAR_NUMS)
            || self.contains(Self::DIAGONAL_FRACTIONS | Self::STACKED_FRACTIONS)
        {
            return false;
        }
        true
    }
}

/// This property provides low-level control over OpenType or TrueType font features.
pub type FontFeatureSettings = FontSettings<FeatureTagValue<Integer>>;

impl FontFeatureSettings {
    /// Like `parse`, but rejects calc expressions that cannot be resolved at parse time,
    /// since @font-face descriptors require concrete values.
    pub fn parse_for_font_face_rule<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let settings = FontFeatureSettings::parse(context, input)?;
        if settings
            .0
            .iter()
            .any(|setting| setting.value.resolve().is_none())
        {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(settings)
    }
}

/// For font-language-override, use the same representation as the computed value.
pub use crate::values::computed::font::FontLanguageOverride;

impl Parse for FontLanguageOverride {
    /// normal | <string>
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<FontLanguageOverride, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(FontLanguageOverride::normal());
        }

        let string = input.expect_string()?;

        // The OpenType spec requires tags to be 1 to 4 ASCII characters:
        // https://learn.microsoft.com/en-gb/typography/opentype/spec/otff#data-types
        if string.is_empty() || string.len() > 4 || !string.is_ascii() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        let mut bytes = [b' '; 4];
        for (byte, str_byte) in bytes.iter_mut().zip(string.as_bytes()) {
            *byte = *str_byte;
        }

        Ok(FontLanguageOverride(u32::from_be_bytes(bytes)))
    }
}

/// A value for any of the font-synthesis-{weight,small-caps,position} properties.
#[repr(u8)]
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
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
pub enum FontSynthesis {
    /// This attribute may be synthesized if not supported by a face.
    Auto,
    /// Do not attempt to synthesis this style attribute.
    None,
}

/// A value for the font-synthesis-style property.
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
pub enum FontSynthesisStyle {
    /// This attribute may be synthesized if not supported by a face.
    Auto,
    /// Do not attempt to synthesis this style attribute.
    None,
    /// Allow synthesis for oblique, but not for italic.
    ObliqueOnly,
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
/// Allows authors to choose a palette from those supported by a color font
/// (and potentially @font-palette-values overrides).
pub struct FontPalette(Atom);

#[allow(missing_docs)]
impl FontPalette {
    pub fn normal() -> Self {
        Self(atom!("normal"))
    }
    pub fn light() -> Self {
        Self(atom!("light"))
    }
    pub fn dark() -> Self {
        Self(atom!("dark"))
    }
}

impl Parse for FontPalette {
    /// normal | light | dark | dashed-ident
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<FontPalette, ParseError<'i>> {
        let location = input.current_source_location();
        let ident = input.expect_ident()?;
        match_ignore_ascii_case! { &ident,
            "normal" => Ok(Self::normal()),
            "light" => Ok(Self::light()),
            "dark" => Ok(Self::dark()),
            _ => if ident.starts_with("--") {
                Ok(Self(Atom::from(ident.as_ref())))
            } else {
                Err(location.new_custom_error(SelectorParseErrorKind::UnexpectedIdent(ident.clone())))
            },
        }
    }
}

impl ToCss for FontPalette {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_atom_identifier(&self.0, dest)
    }
}

/// This property provides low-level control over OpenType or TrueType font
/// variations.
pub type FontVariationSettings = FontSettings<VariationValue<Number>>;

fn parse_one_feature_value<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
) -> Result<Integer, ParseError<'i>> {
    if let Ok(integer) = input.try_parse(|i| Integer::parse_non_negative(context, i)) {
        return Ok(integer);
    }

    try_match_ident_ignore_ascii_case! { input,
        "on" => Ok(Integer::new(1)),
        "off" => Ok(Integer::new(0)),
    }
}

impl Parse for FeatureTagValue<Integer> {
    /// https://drafts.csswg.org/css-fonts-4/#feature-tag-value
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let tag = FontTag::parse(context, input)?;
        let value = input
            .try_parse(|i| parse_one_feature_value(context, i))
            .unwrap_or_else(|_| Integer::new(1));

        Ok(Self { tag, value })
    }
}

impl Parse for VariationValue<Number> {
    /// This is the `<string> <number>` part of the font-variation-settings
    /// syntax.
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let tag = FontTag::parse(context, input)?;
        let value = Number::parse(context, input)?;
        Ok(Self { tag, value })
    }
}

impl FontVariationSettings {
    /// Like `parse`, but rejects calc expressions that cannot be resolved at parse time,
    /// since @font-face descriptors require concrete values.
    pub fn parse_for_font_face_rule<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let settings = FontVariationSettings::parse(context, input)?;
        if settings
            .0
            .iter()
            .any(|setting| setting.value.resolve().is_none())
        {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(settings)
    }
}

/// A metrics override value for a @font-face descriptor
///
/// https://drafts.csswg.org/css-fonts/#font-metrics-override-desc
#[derive(Clone, Debug, MallocSizeOf, Parse, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
pub enum MetricsOverride {
    /// A non-negative `<percentage>` of the computed font size
    Override(NonNegativePercentage),
    /// Normal metrics from the font.
    Normal,
}

impl MetricsOverride {
    #[inline]
    /// Get default value with `normal`
    pub fn normal() -> MetricsOverride {
        MetricsOverride::Normal
    }

    /// The ToComputedValue implementation, used for @font-face descriptors.
    ///
    /// Valid override percentages must be non-negative; we return -1.0 to indicate
    /// the absence of an override (i.e. 'normal'). Returns None if the value contains
    /// a calc expression that cannot be resolved at parse time.
    #[inline]
    pub fn compute(&self) -> Option<ComputedPercentage> {
        Some(ComputedPercentage(match self {
            MetricsOverride::Normal => -1.0,
            MetricsOverride::Override(percent) => percent.compute()?.0,
        }))
    }
}

#[derive(
    Clone,
    Copy,
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
/// How to do font-size scaling.
pub enum XTextScale {
    /// Both min-font-size and text zoom are enabled.
    All,
    /// Text-only zoom is enabled, but min-font-size is not honored.
    ZoomOnly,
    /// Neither of them is enabled.
    None,
}

impl XTextScale {
    /// Returns whether text zoom is enabled.
    #[inline]
    pub fn text_zoom_enabled(self) -> bool {
        self != Self::None
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
    ToTyped,
)]
#[cfg_attr(feature = "servo", derive(Deserialize, Eq, Hash, Serialize))]
/// Internal property that reflects the lang attribute
pub struct XLang(#[css(skip)] pub Atom);

impl XLang {
    #[inline]
    /// Get default value for `-x-lang`
    pub fn get_initial_value() -> XLang {
        XLang(atom!(""))
    }
}

impl Parse for XLang {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<XLang, ParseError<'i>> {
        debug_assert!(
            false,
            "Should be set directly by presentation attributes only."
        );
        Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }
}

#[cfg_attr(feature = "gecko", derive(MallocSizeOf))]
#[derive(Clone, Copy, Debug, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
/// Specifies the minimum font size allowed due to changes in scriptlevel.
/// Ref: https://wiki.mozilla.org/MathML:mstyle
pub struct MozScriptMinSize(pub NoCalcLength);

impl MozScriptMinSize {
    #[inline]
    /// Calculate initial value of -moz-script-min-size.
    pub fn get_initial_value() -> Length {
        Length::new(DEFAULT_SCRIPT_MIN_SIZE_PT as f32 * PX_PER_PT)
    }
}

impl Parse for MozScriptMinSize {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<MozScriptMinSize, ParseError<'i>> {
        debug_assert!(
            false,
            "Should be set directly by presentation attributes only."
        );
        Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }
}

/// A value for the `math-depth` property.
/// https://mathml-refresh.github.io/mathml-core/#the-math-script-level-property
#[cfg_attr(feature = "gecko", derive(MallocSizeOf))]
#[derive(Clone, Debug, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped)]
pub enum MathDepth {
    /// Increment math-depth if math-style is compact.
    AutoAdd,

    /// Add the function's argument to math-depth.
    #[css(function)]
    Add(Integer),

    /// Set math-depth to the specified value.
    Absolute(Integer),
}

impl Parse for MathDepth {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<MathDepth, ParseError<'i>> {
        if input
            .try_parse(|i| i.expect_ident_matching("auto-add"))
            .is_ok()
        {
            return Ok(MathDepth::AutoAdd);
        }
        if let Ok(math_depth_value) = input.try_parse(|input| Integer::parse(context, input)) {
            return Ok(MathDepth::Absolute(math_depth_value));
        }
        input.expect_function_matching("add")?;
        let math_depth_delta_value =
            input.parse_nested_block(|input| Integer::parse(context, input))?;
        Ok(MathDepth::Add(math_depth_delta_value))
    }
}

#[cfg_attr(feature = "gecko", derive(MallocSizeOf))]
#[derive(
    Clone,
    Copy,
    Debug,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
/// Specifies the multiplier to be used to adjust font size
/// due to changes in scriptlevel.
///
/// Ref: https://www.w3.org/TR/MathML3/chapter3.html#presm.mstyle.attrs
pub struct MozScriptSizeMultiplier(pub f32);

impl MozScriptSizeMultiplier {
    #[inline]
    /// Get default value of `-moz-script-size-multiplier`
    pub fn get_initial_value() -> MozScriptSizeMultiplier {
        MozScriptSizeMultiplier(DEFAULT_SCRIPT_SIZE_MULTIPLIER as f32)
    }
}

impl Parse for MozScriptSizeMultiplier {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<MozScriptSizeMultiplier, ParseError<'i>> {
        debug_assert!(
            false,
            "Should be set directly by presentation attributes only."
        );
        Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }
}

impl From<f32> for MozScriptSizeMultiplier {
    fn from(v: f32) -> Self {
        MozScriptSizeMultiplier(v)
    }
}

impl From<MozScriptSizeMultiplier> for f32 {
    fn from(v: MozScriptSizeMultiplier) -> f32 {
        v.0
    }
}

/// A specified value for the `line-height` property.
pub type LineHeight = GenericLineHeight<NonNegativeNumber, NonNegativeLengthPercentage>;

impl ToComputedValue for LineHeight {
    type ComputedValue = computed::LineHeight;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self {
            GenericLineHeight::Normal => GenericLineHeight::Normal,
            #[cfg(feature = "gecko")]
            GenericLineHeight::MozBlockHeight => GenericLineHeight::MozBlockHeight,
            GenericLineHeight::Number(ref number) => {
                GenericLineHeight::Number(number.to_computed_value(context))
            },
            GenericLineHeight::Length(ref non_negative_lp) => {
                let result = match non_negative_lp.0 {
                    LengthPercentage::Length(NoCalcLength::Absolute(ref abs)) => {
                        context.maybe_zoom_text(abs.to_computed_value(context))
                    },
                    LengthPercentage::Length(ref length) => {
                        // line-height units specifically resolve against parent's
                        // font and line-height properties, while the rest of font
                        // relative units still resolve against the element's own
                        // properties.
                        length.to_computed_value_with_base_size(
                            context,
                            FontBaseSize::CurrentStyle,
                            LineHeightBase::InheritedStyle,
                        )
                    },
                    LengthPercentage::Percentage(ref p) => FontRelativeLength::Em(p.get())
                        .to_computed_value(
                            context,
                            FontBaseSize::CurrentStyle,
                            LineHeightBase::InheritedStyle,
                        ),
                    LengthPercentage::Calc(ref calc) => {
                        let computed_calc = calc.to_computed_value_zoomed(
                            context,
                            FontBaseSize::CurrentStyle,
                            LineHeightBase::InheritedStyle,
                        );
                        let base = context.style().get_font().clone_font_size().computed_size();
                        computed_calc.resolve(base)
                    },
                };
                GenericLineHeight::Length(result.into())
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        match *computed {
            GenericLineHeight::Normal => GenericLineHeight::Normal,
            #[cfg(feature = "gecko")]
            GenericLineHeight::MozBlockHeight => GenericLineHeight::MozBlockHeight,
            GenericLineHeight::Number(ref number) => {
                GenericLineHeight::Number(NonNegativeNumber::from_computed_value(number))
            },
            GenericLineHeight::Length(ref length) => {
                GenericLineHeight::Length(NoCalcLength::from_computed_value(&length.0).into())
            },
        }
    }
}

/// Flags for the query_font_metrics() function.
#[repr(C)]
pub struct QueryFontMetricsFlags(u8);

bitflags! {
    impl QueryFontMetricsFlags: u8 {
        /// Should we use the user font set?
        const USE_USER_FONT_SET = 1 << 0;
        /// Does the caller need the `ch` unit (width of the ZERO glyph)?
        const NEEDS_CH = 1 << 1;
        /// Does the caller need the `ic` unit (width of the WATER ideograph)?
        const NEEDS_IC = 1 << 2;
        /// Does the caller need math scales to be retrieved?
        const NEEDS_MATH_SCALES = 1 << 3;
    }
}
