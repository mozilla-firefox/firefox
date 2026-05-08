/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The [`@font-face`][ff] at-rule.
//!
//! [ff]: https://drafts.csswg.org/css-fonts/#at-font-face-rule

use crate::derives::*;
use crate::error_reporting::ContextualParseError;
use crate::parser::{Parse, ParserContext};
use crate::shared_lock::{SharedRwLockReadGuard, ToCssWithGuard};
use crate::values::generics::font::FontStyle as GenericFontStyle;
use crate::values::specified::{url::SpecifiedUrl, Angle};
use cssparser::{Parser, RuleBodyParser, SourceLocation};
use std::fmt::{self, Write};
use style_traits::{CssStringWriter, CssWriter, ParseError, StyleParseErrorKind, ToCss};

pub use crate::properties::font_face::{DescriptorId, DescriptorParser, Descriptors};
pub use crate::values::computed::font::{FamilyName, FontStretch};
pub use crate::values::specified::font::{
    AbsoluteFontWeight, FontFeatureSettings, FontLanguageOverride,
    FontStretch as SpecifiedFontStretch, FontVariationSettings, MetricsOverride,
    SpecifiedFontStyle,
};

/// A source for a font-face rule.
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToCss, ToShmem)]
pub enum Source {
    /// A `url()` source.
    Url(UrlSource),
    /// A `local()` source.
    #[css(function)]
    Local(FamilyName),
}

/// A list of sources for the font-face src descriptor.
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToCss, ToShmem)]
#[css(comma)]
pub struct SourceList(#[css(iterable)] pub Vec<Source>);

// We can't just use OneOrMoreSeparated to derive Parse for the Source list,
// because we want to filter out components that parsed as None, then fail if no
// valid components remain. So we provide our own implementation here.
impl Parse for SourceList {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        // Parse the comma-separated list, then let filter_map discard any None items.
        let list = input
            .parse_comma_separated(|input| {
                let s = input.parse_entirely(|input| Source::parse(context, input));
                while input.next().is_ok() {}
                Ok(s.ok())
            })?
            .into_iter()
            .filter_map(|s| s)
            .collect::<Vec<Source>>();
        if list.is_empty() {
            Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        } else {
            Ok(SourceList(list))
        }
    }
}

/// Keywords for the font-face src descriptor's format() function.
/// ('None' and 'Unknown' are for internal use in gfx, not exposed to CSS.)
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, Parse, PartialEq, ToCss, ToShmem)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[repr(u8)]
#[allow(missing_docs)]
pub enum FontFaceSourceFormatKeyword {
    #[css(skip)]
    None,
    Collection,
    EmbeddedOpentype,
    Opentype,
    Svg,
    Truetype,
    Woff,
    Woff2,
    #[css(skip)]
    Unknown,
}

/// Flags for the @font-face tech() function, indicating font technologies
/// required by the resource.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[repr(C)]
pub struct FontFaceSourceTechFlags(u16);
bitflags! {
    impl FontFaceSourceTechFlags: u16 {
        /// Font requires OpenType feature support.
        const FEATURES_OPENTYPE = 1 << 0;
        /// Font requires Apple Advanced Typography support.
        const FEATURES_AAT = 1 << 1;
        /// Font requires Graphite shaping support.
        const FEATURES_GRAPHITE = 1 << 2;
        /// Font requires COLRv0 rendering support (simple list of colored layers).
        const COLOR_COLRV0 = 1 << 3;
        /// Font requires COLRv1 rendering support (graph of paint operations).
        const COLOR_COLRV1 = 1 << 4;
        /// Font requires SVG glyph rendering support.
        const COLOR_SVG = 1 << 5;
        /// Font has bitmap glyphs in 'sbix' format.
        const COLOR_SBIX = 1 << 6;
        /// Font has bitmap glyphs in 'CBDT' format.
        const COLOR_CBDT = 1 << 7;
        /// Font requires OpenType Variations support.
        const VARIATIONS = 1 << 8;
        /// Font requires CPAL palette selection support.
        const PALETTES = 1 << 9;
        /// Font requires support for incremental downloading.
        const INCREMENTAL = 1 << 10;
    }
}

impl FontFaceSourceTechFlags {
    /// Parse a single font-technology keyword and return its flag.
    pub fn parse_one<'i, 't>(input: &mut Parser<'i, 't>) -> Result<Self, ParseError<'i>> {
        Ok(try_match_ident_ignore_ascii_case! { input,
            "features-opentype" => Self::FEATURES_OPENTYPE,
            "features-aat" => Self::FEATURES_AAT,
            "features-graphite" => Self::FEATURES_GRAPHITE,
            "color-colrv0" => Self::COLOR_COLRV0,
            "color-colrv1" => Self::COLOR_COLRV1,
            "color-svg" => Self::COLOR_SVG,
            "color-sbix" => Self::COLOR_SBIX,
            "color-cbdt" => Self::COLOR_CBDT,
            "variations" => Self::VARIATIONS,
            "palettes" => Self::PALETTES,
            "incremental" => Self::INCREMENTAL,
        })
    }
}

impl Parse for FontFaceSourceTechFlags {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        // We don't actually care about the return value of parse_comma_separated,
        // because we insert the flags into result as we go.
        let mut result = Self::empty();
        input.parse_comma_separated(|input| {
            let flag = Self::parse_one(input)?;
            result.insert(flag);
            Ok(())
        })?;
        if !result.is_empty() {
            Ok(result)
        } else {
            Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        }
    }
}

#[allow(unused_assignments)]
impl ToCss for FontFaceSourceTechFlags {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        let mut first = true;

        macro_rules! write_if_flag {
            ($s:expr => $f:ident) => {
                if self.contains(Self::$f) {
                    if first {
                        first = false;
                    } else {
                        dest.write_str(", ")?;
                    }
                    dest.write_str($s)?;
                }
            };
        }

        write_if_flag!("features-opentype" => FEATURES_OPENTYPE);
        write_if_flag!("features-aat" => FEATURES_AAT);
        write_if_flag!("features-graphite" => FEATURES_GRAPHITE);
        write_if_flag!("color-colrv0" => COLOR_COLRV0);
        write_if_flag!("color-colrv1" => COLOR_COLRV1);
        write_if_flag!("color-svg" => COLOR_SVG);
        write_if_flag!("color-sbix" => COLOR_SBIX);
        write_if_flag!("color-cbdt" => COLOR_CBDT);
        write_if_flag!("variations" => VARIATIONS);
        write_if_flag!("palettes" => PALETTES);
        write_if_flag!("incremental" => INCREMENTAL);

        Ok(())
    }
}

/// <https://drafts.csswg.org/css-fonts/#font-face-rule>
#[derive(Clone, Debug, ToShmem, PartialEq)]
pub struct FontFaceRule {
    /// The descriptors of the @font-face rule.
    pub descriptors: Descriptors,
    /// The parser location of the rule.
    pub source_location: SourceLocation,
}

impl FontFaceRule {
    /// Returns an empty rule.
    pub fn empty(source_location: SourceLocation) -> Self {
        Self {
            descriptors: Default::default(),
            source_location,
        }
    }
}

/// A POD representation for Gecko. All pointers here are non-owned and as such
/// can't outlive the rule they came from, but we can't enforce that via C++.
///
/// All the strings are of course utf8.
#[cfg(feature = "gecko")]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum FontFaceSourceListComponent {
    Url(*const crate::gecko::url::CssUrl),
    Local(*mut crate::gecko_bindings::structs::nsAtom),
    FormatHintKeyword(FontFaceSourceFormatKeyword),
    FormatHintString {
        length: usize,
        utf8_bytes: *const u8,
    },
    TechFlags(FontFaceSourceTechFlags),
}

#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToCss, ToShmem)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[repr(u8)]
#[allow(missing_docs)]
pub enum FontFaceSourceFormat {
    Keyword(FontFaceSourceFormatKeyword),
    String(String),
}

/// A `UrlSource` represents a font-face source that has been specified with a
/// `url()` function.
///
/// <https://drafts.csswg.org/css-fonts/#src-desc>
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
pub struct UrlSource {
    /// The specified url.
    pub url: SpecifiedUrl,
    /// The format hint specified with the `format()` function, if present.
    pub format_hint: Option<FontFaceSourceFormat>,
    /// The font technology flags specified with the `tech()` function, if any.
    pub tech_flags: FontFaceSourceTechFlags,
}

impl ToCss for UrlSource {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        self.url.to_css(dest)?;
        if let Some(hint) = &self.format_hint {
            dest.write_str(" format(")?;
            hint.to_css(dest)?;
            dest.write_char(')')?;
        }
        if !self.tech_flags.is_empty() {
            dest.write_str(" tech(")?;
            self.tech_flags.to_css(dest)?;
            dest.write_char(')')?;
        }
        Ok(())
    }
}

/// A font-display value for a @font-face rule.
/// The font-display descriptor determines how a font face is displayed based
/// on whether and when it is downloaded and ready to use.
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone, Copy, Debug, Eq, MallocSizeOf, Parse, PartialEq, ToComputedValue, ToCss, ToShmem,
)]
#[repr(u8)]
pub enum FontDisplay {
    Auto,
    Block,
    Swap,
    Fallback,
    Optional,
}

macro_rules! impl_range {
    ($range:ident, $component:ident) => {
        impl Parse for $range {
            fn parse<'i, 't>(
                context: &ParserContext,
                input: &mut Parser<'i, 't>,
            ) -> Result<Self, ParseError<'i>> {
                let first = $component::parse(context, input)?;
                let second = input
                    .try_parse(|input| $component::parse(context, input))
                    .unwrap_or_else(|_| first.clone());
                Ok($range(first, second))
            }
        }
        impl ToCss for $range {
            fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
            where
                W: fmt::Write,
            {
                self.0.to_css(dest)?;
                if self.0 != self.1 {
                    dest.write_char(' ')?;
                    self.1.to_css(dest)?;
                }
                Ok(())
            }
        }
    };
}

/// The font-weight descriptor:
///
/// https://drafts.csswg.org/css-fonts-4/#descdef-font-face-font-weight
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct FontWeightRange(pub AbsoluteFontWeight, pub AbsoluteFontWeight);
impl_range!(FontWeightRange, AbsoluteFontWeight);

/// The computed representation of the above so Gecko can read them easily.
///
/// This one is needed because cbindgen doesn't know how to generate
/// specified::Number.
#[repr(C)]
#[allow(missing_docs)]
pub struct ComputedFontWeightRange(f32, f32);

#[inline]
fn sort_range<T: PartialOrd>(a: T, b: T) -> (T, T) {
    if a > b {
        (b, a)
    } else {
        (a, b)
    }
}

impl FontWeightRange {
    /// Returns a computed font-weight range, or None if either bound is an unresolvable calc.
    pub fn compute(&self) -> Option<ComputedFontWeightRange> {
        let (min, max) = sort_range(self.0.compute()?.value(), self.1.compute()?.value());
        Some(ComputedFontWeightRange(min, max))
    }
}

/// The font-stretch descriptor:
///
/// https://drafts.csswg.org/css-fonts-4/#descdef-font-face-font-stretch
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct FontStretchRange(pub SpecifiedFontStretch, pub SpecifiedFontStretch);
impl_range!(FontStretchRange, SpecifiedFontStretch);

/// The computed representation of the above, so that Gecko can read them
/// easily.
#[repr(C)]
#[allow(missing_docs)]
pub struct ComputedFontStretchRange(FontStretch, FontStretch);

impl FontStretchRange {
    /// Returns a computed font-stretch range.
    pub fn compute(&self) -> ComputedFontStretchRange {
        fn compute_stretch(s: &SpecifiedFontStretch) -> FontStretch {
            match *s {
                SpecifiedFontStretch::Keyword(ref kw) => kw.compute(),
                SpecifiedFontStretch::Stretch(ref p) => FontStretch::from_percentage(p.0.get()),
                SpecifiedFontStretch::System(..) => unreachable!(),
            }
        }

        let (min, max) = sort_range(compute_stretch(&self.0), compute_stretch(&self.1));
        ComputedFontStretchRange(min, max)
    }
}

/// The font-style descriptor:
///
/// https://drafts.csswg.org/css-fonts-4/#descdef-font-face-font-style
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
#[allow(missing_docs)]
pub enum FontStyle {
    Italic,
    Oblique(Angle, Angle),
}

/// The computed representation of the above, with angles in degrees, so that
/// Gecko can read them easily.
#[repr(u8)]
#[allow(missing_docs)]
pub enum ComputedFontStyleDescriptor {
    Italic,
    Oblique(f32, f32),
}

impl Parse for FontStyle {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        // We parse 'normal' explicitly here to distinguish it from 'oblique 0deg',
        // because we must not accept a following angle.
        if input
            .try_parse(|i| i.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(FontStyle::Oblique(Angle::zero(), Angle::zero()));
        }

        let style = SpecifiedFontStyle::parse(context, input)?;
        Ok(match style {
            GenericFontStyle::Italic => FontStyle::Italic,
            GenericFontStyle::Oblique(angle) => {
                let second_angle = input
                    .try_parse(|input| SpecifiedFontStyle::parse_angle(context, input))
                    .unwrap_or_else(|_| angle.clone());

                FontStyle::Oblique(angle, second_angle)
            },
        })
    }
}

impl ToCss for FontStyle {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        match *self {
            FontStyle::Italic => dest.write_str("italic"),
            FontStyle::Oblique(ref first, ref second) => {
                // Not first.is_zero() because we don't want to serialize
                // `oblique calc(0deg)` as `normal`.
                if *first == Angle::zero() && first == second {
                    return dest.write_str("normal");
                }
                dest.write_str("oblique")?;
                if *first != SpecifiedFontStyle::default_angle() || first != second {
                    dest.write_char(' ')?;
                    first.to_css(dest)?;
                }
                if first != second {
                    dest.write_char(' ')?;
                    second.to_css(dest)?;
                }
                Ok(())
            },
        }
    }
}

impl FontStyle {
    /// Returns a computed font-style descriptor.
    pub fn compute(&self) -> ComputedFontStyleDescriptor {
        match *self {
            FontStyle::Italic => ComputedFontStyleDescriptor::Italic,
            FontStyle::Oblique(ref first, ref second) => {
                let (min, max) = sort_range(
                    SpecifiedFontStyle::compute_angle_degrees(first),
                    SpecifiedFontStyle::compute_angle_degrees(second),
                );
                ComputedFontStyleDescriptor::Oblique(min, max)
            },
        }
    }
}

/// Parse the block inside a `@font-face` rule.
///
/// Note that the prelude parsing code lives in the `stylesheets` module.
pub fn parse_font_face_block(
    context: &ParserContext,
    input: &mut Parser,
    source_location: SourceLocation,
) -> FontFaceRule {
    let mut rule = FontFaceRule::empty(source_location);
    {
        let mut parser = DescriptorParser {
            context,
            descriptors: &mut rule.descriptors,
        };
        let mut iter = RuleBodyParser::new(input, &mut parser);
        while let Some(declaration) = iter.next() {
            if let Err((error, slice)) = declaration {
                let location = error.location;
                let error = ContextualParseError::UnsupportedFontFaceDescriptor(slice, error);
                context.log_css_error(location, error)
            }
        }
    }
    rule
}

impl Parse for Source {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Source, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_function_matching("local"))
            .is_ok()
        {
            return input
                .parse_nested_block(|input| FamilyName::parse(context, input))
                .map(Source::Local);
        }

        let url = SpecifiedUrl::parse(context, input)?;

        // Parsing optional format()
        let format_hint = if input
            .try_parse(|input| input.expect_function_matching("format"))
            .is_ok()
        {
            input.parse_nested_block(|input| {
                if let Ok(kw) = input.try_parse(FontFaceSourceFormatKeyword::parse) {
                    Ok(Some(FontFaceSourceFormat::Keyword(kw)))
                } else {
                    let s = input.expect_string()?.as_ref().to_owned();
                    Ok(Some(FontFaceSourceFormat::String(s)))
                }
            })?
        } else {
            None
        };

        // Parse optional tech()
        let tech_flags = if static_prefs::pref!("layout.css.font-tech.enabled")
            && input
                .try_parse(|input| input.expect_function_matching("tech"))
                .is_ok()
        {
            input.parse_nested_block(|input| FontFaceSourceTechFlags::parse(context, input))?
        } else {
            FontFaceSourceTechFlags::empty()
        };

        Ok(Source::Url(UrlSource {
            url,
            format_hint,
            tech_flags,
        }))
    }
}

impl ToCssWithGuard for FontFaceRule {
    // Serialization of FontFaceRule is not specced.
    fn to_css(&self, _guard: &SharedRwLockReadGuard, dest: &mut CssStringWriter) -> fmt::Result {
        dest.write_str("@font-face { ")?;
        self.descriptors.to_css(&mut CssWriter::new(dest))?;
        dest.write_char('}')
    }
}
