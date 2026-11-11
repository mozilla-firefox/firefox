/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for CSS values related to backgrounds.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::typed_om::{KeywordValue, ToTyped, TypedValue};
use crate::values::generics::background::BackgroundSize as GenericBackgroundSize;
use crate::values::specified::length::{
    NonNegativeLengthPercentage, NonNegativeLengthPercentageOrAuto,
};
use cssparser::{Parser, match_ignore_ascii_case};
use selectors::parser::SelectorParseErrorKind;
use std::fmt::{self, Write};
use style_traits::{CssString, CssWriter, ParseError, StyleParseErrorKind, ToCss};
use thin_vec::ThinVec;

/// A specified value for the `background-size` property.
pub type BackgroundSize = GenericBackgroundSize<NonNegativeLengthPercentage>;

impl Parse for BackgroundSize {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if let Ok(width) = input.try_parse(|i| NonNegativeLengthPercentageOrAuto::parse(context, i))
        {
            let height = input
                .try_parse(|i| NonNegativeLengthPercentageOrAuto::parse(context, i))
                .unwrap_or(NonNegativeLengthPercentageOrAuto::auto());
            return Ok(GenericBackgroundSize::ExplicitSize { width, height });
        }
        Ok(try_match_ident_ignore_ascii_case! { input,
            "cover" => GenericBackgroundSize::Cover,
            "contain" => GenericBackgroundSize::Contain,
        })
    }
}

/// One of the keywords for `background-repeat`.
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
#[value_info(other_values = "repeat-x,repeat-y")]
pub enum BackgroundRepeatKeyword {
    Repeat,
    Space,
    Round,
    NoRepeat,
}

/// The value of the `background-repeat` property, with `repeat-x` / `repeat-y`
/// represented as the combination of `no-repeat` and `repeat` in the opposite
/// axes.
///
/// https://drafts.csswg.org/css-backgrounds/#the-background-repeat
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
pub struct BackgroundRepeat(pub BackgroundRepeatKeyword, pub BackgroundRepeatKeyword);

impl BackgroundRepeat {
    /// Returns the `repeat repeat` value.
    pub fn repeat() -> Self {
        BackgroundRepeat(
            BackgroundRepeatKeyword::Repeat,
            BackgroundRepeatKeyword::Repeat,
        )
    }
}

impl ToCss for BackgroundRepeat {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match (self.0, self.1) {
            (BackgroundRepeatKeyword::Repeat, BackgroundRepeatKeyword::NoRepeat) => {
                dest.write_str("repeat-x")
            },
            (BackgroundRepeatKeyword::NoRepeat, BackgroundRepeatKeyword::Repeat) => {
                dest.write_str("repeat-y")
            },
            (horizontal, vertical) => {
                horizontal.to_css(dest)?;
                if horizontal != vertical {
                    dest.write_char(' ')?;
                    vertical.to_css(dest)?;
                }
                Ok(())
            },
        }
    }
}

impl ToTyped for BackgroundRepeat {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        match (self.0, self.1) {
            (BackgroundRepeatKeyword::Repeat, BackgroundRepeatKeyword::NoRepeat) => {
                dest.push(TypedValue::Keyword(KeywordValue(CssString::from(
                    "repeat-x",
                ))));
                Ok(())
            },
            (BackgroundRepeatKeyword::NoRepeat, BackgroundRepeatKeyword::Repeat) => {
                dest.push(TypedValue::Keyword(KeywordValue(CssString::from(
                    "repeat-y",
                ))));
                Ok(())
            },
            (horizontal, vertical) if horizontal == vertical => {
                ToTyped::to_typed(&horizontal, dest)
            },
            _ => Err(()),
        }
    }
}

impl Parse for BackgroundRepeat {
    fn parse(_context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let ident = input.expect_ident_cloned()?;

        match_ignore_ascii_case! { &ident,
            "repeat-x" => {
                return Ok(BackgroundRepeat(BackgroundRepeatKeyword::Repeat, BackgroundRepeatKeyword::NoRepeat));
            },
            "repeat-y" => {
                return Ok(BackgroundRepeat(BackgroundRepeatKeyword::NoRepeat, BackgroundRepeatKeyword::Repeat));
            },
            _ => {},
        }

        let horizontal = match BackgroundRepeatKeyword::from_ident(&ident) {
            Ok(h) => h,
            Err(()) => {
                return Err(ParseError::custom(SelectorParseErrorKind::UnexpectedIdent));
            },
        };

        let vertical = input.try_parse(BackgroundRepeatKeyword::parse).ok();
        Ok(BackgroundRepeat(horizontal, vertical.unwrap_or(horizontal)))
    }
}

fn background_clip_border_area_enabled(context: &ParserContext) -> bool {
    context.chrome_rules_enabled() || crate::pref!("layout.css.background-clip.border-area.enabled")
}

fn background_clip_text_enabled(_: &ParserContext) -> bool {
    crate::pref!("layout.css.background-clip.text.enabled", gecko = true)
}

/// The specified value of the `background-clip` and `mask-clip` properties.
///
/// This is the union of the keywords both properties accept; each property restricts the set it
/// actually allows during parsing (see `valid_for_background` / `valid_for_mask`).
///
/// https://drafts.csswg.org/css-backgrounds-4/#background-clip
/// https://drafts.fxtf.org/css-masking-1/#propdef-mask-clip
#[allow(missing_docs)]
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
pub enum BackgroundClip {
    BorderBox,
    PaddingBox,
    ContentBox,
    // TODO(emilio): We should expose the svg values in SpecifiedValueInfo or so but only for
    // mask-clip... Maybe we need a newtype thing, or to rejigger the painting code / storage
    // further.
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    FillBox,
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    StrokeBox,
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    ViewBox,
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    NoClip,
    // TODO: text and border-area are supposed to combine in backgrounds-4...
    #[parse(condition = "background_clip_text_enabled")]
    #[cfg_attr(feature = "servo", value_info(skip))]
    Text,
    #[parse(condition = "background_clip_border_area_enabled")]
    #[value_info(skip)]
    BorderArea,
}

bitflags! {
    /// Whether a value is valid for background-clip, mask-clip, or both.
    #[derive(Clone, Copy)]
    struct ClipValidity: u8 {
        const BACKGROUND = 1 << 0;
        const MASK = 1 << 1;
        const BOTH = Self::BACKGROUND.bits() | Self::MASK.bits();
    }
}

impl BackgroundClip {
    fn validity(&self) -> ClipValidity {
        match *self {
            Self::BorderBox => ClipValidity::BOTH,
            Self::PaddingBox => ClipValidity::BOTH,
            Self::ContentBox => ClipValidity::BOTH,
            #[cfg(feature = "gecko")]
            Self::FillBox => ClipValidity::MASK,
            #[cfg(feature = "gecko")]
            Self::StrokeBox => ClipValidity::MASK,
            #[cfg(feature = "gecko")]
            Self::ViewBox => ClipValidity::MASK,
            #[cfg(feature = "gecko")]
            Self::NoClip => ClipValidity::MASK,
            Self::Text => ClipValidity::BACKGROUND,
            Self::BorderArea => ClipValidity::BACKGROUND,
        }
    }

    /// Parse the value of the `background-clip` property.
    pub fn parse_for_background(
        context: &ParserContext,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        let clip = Self::parse(context, input)?;
        if !clip.validity().intersects(ClipValidity::BACKGROUND) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(clip)
    }

    /// Parse the value of the `mask-clip` property.
    pub fn parse_for_mask(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let clip = Self::parse(context, input)?;
        if !clip.validity().intersects(ClipValidity::MASK) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(clip)
    }
}

/// https://drafts.csswg.org/css-backgrounds/#the-background-origin
///
/// Also used by mask-origin, which additionally accepts the SVG boxes.
#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    Eq,
    FromPrimitive,
    Hash,
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
#[repr(u8)]
pub enum BackgroundOrigin {
    BorderBox,
    PaddingBox,
    ContentBox,
    // TODO(emilio): Same caveat as BackgroundClip regarding SpecifiedValueInfo.
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    FillBox,
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    StrokeBox,
    #[cfg(feature = "gecko")]
    #[value_info(skip)]
    ViewBox,
}

impl BackgroundOrigin {
    /// Parse the value of the `background-origin` property, which doesn't
    /// accept the SVG boxes that `mask-origin` does.
    pub fn parse_for_background(
        _context: &ParserContext,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        let origin = Self::parse(input)?;
        #[cfg(feature = "gecko")]
        if matches!(origin, Self::FillBox | Self::StrokeBox | Self::ViewBox) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(origin)
    }
}

impl From<BackgroundOrigin> for BackgroundClip {
    fn from(origin: BackgroundOrigin) -> Self {
        match origin {
            BackgroundOrigin::BorderBox => Self::BorderBox,
            BackgroundOrigin::PaddingBox => Self::PaddingBox,
            BackgroundOrigin::ContentBox => Self::ContentBox,
            #[cfg(feature = "gecko")]
            BackgroundOrigin::FillBox => Self::FillBox,
            #[cfg(feature = "gecko")]
            BackgroundOrigin::StrokeBox => Self::StrokeBox,
            #[cfg(feature = "gecko")]
            BackgroundOrigin::ViewBox => Self::ViewBox,
        }
    }
}

/// https://drafts.csswg.org/css-backgrounds/#the-background-attachment
#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    Eq,
    FromPrimitive,
    Hash,
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
#[repr(u8)]
pub enum ImageLayerAttachment {
    Scroll,
    Fixed,
    #[cfg(feature = "gecko")]
    Local,
}
