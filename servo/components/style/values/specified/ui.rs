/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for UI properties.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::generics::ui as generics;
use crate::values::specified::Number;
use crate::values::specified::color::Color;
use crate::values::specified::image::Image;
use cssparser::Parser;
use std::fmt::{self, Write};
use style_traits::{
    CssWriter, KeywordsCollectFn, ParseError, SpecifiedValueInfo, StyleParseErrorKind, ToCss,
};

/// A specified value for the `cursor` property.
pub type Cursor = generics::GenericCursor<CursorImage>;

/// A specified value for item of `image cursors`.
pub type CursorImage = generics::GenericCursorImage<Image, Number>;

impl Parse for Cursor {
    /// cursor: [<url> [<number> <number>]?]# [auto | default | ...]
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let mut images = vec![];
        loop {
            match input.try_parse(|input| CursorImage::parse(context, input)) {
                Ok(image) => images.push(image),
                Err(_) => break,
            }
            input.expect_comma()?;
        }
        Ok(Self {
            images: images.into(),
            keyword: CursorKind::parse(input)?,
        })
    }
}

impl Parse for CursorImage {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        use crate::Zero;

        let image = Image::parse_only_url(context, input)?;
        let mut has_hotspot = false;
        let mut hotspot_x = Number::zero();
        let mut hotspot_y = Number::zero();

        if let Ok(x) = input.try_parse(|input| Number::parse(context, input)) {
            has_hotspot = true;
            hotspot_x = x;
            hotspot_y = Number::parse(context, input)?;
        }

        Ok(Self {
            image,
            has_hotspot,
            hotspot_x,
            hotspot_y,
        })
    }
}

// This trait is manually implemented because we don't support the whole <image>
// syntax for cursors
impl SpecifiedValueInfo for CursorImage {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        f(&["url", "image-set"]);
    }
}
/// Specified value of `-moz-force-broken-image-icon`
#[derive(
    Clone,
    Copy,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(transparent)]
#[typed(todo_derive_fields)]
pub struct BoolInteger(pub bool);

impl BoolInteger {
    /// Returns 0
    #[inline]
    pub fn zero() -> Self {
        Self(false)
    }
}

impl Parse for BoolInteger {
    fn parse(_context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        // We intentionally don't support calc values here.
        match input.expect_integer()? {
            0 => Ok(Self(false)),
            1 => Ok(Self(true)),
            _ => Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError)),
        }
    }
}

impl ToCss for BoolInteger {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str(if self.0 { "1" } else { "0" })
    }
}

/// A specified value for `scrollbar-color` property
pub type ScrollbarColor = generics::ScrollbarColor<Color>;

impl Parse for ScrollbarColor {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if input.try_parse(|i| i.expect_ident_matching("auto")).is_ok() {
            return Ok(generics::ScrollbarColor::Auto);
        }
        Ok(generics::ScrollbarColor::Colors {
            thumb: Color::parse(context, input)?,
            track: Color::parse(context, input)?,
        })
    }
}

/// The specified value for the `user-select` property.
///
/// https://drafts.csswg.org/css-ui-4/#propdef-user-select
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
pub enum UserSelect {
    Auto,
    Text,
    #[parse(aliases = "-moz-none")]
    None,
    #[cfg(feature = "servo")]
    Contain,
    /// Force selection of all children.
    All,
}

/// The keywords allowed in the Cursor property.
///
/// https://drafts.csswg.org/css-ui-4/#propdef-cursor
#[allow(missing_docs)]
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum CursorKind {
    None,
    Default,
    Pointer,
    ContextMenu,
    Help,
    Progress,
    Wait,
    Cell,
    Crosshair,
    Text,
    VerticalText,
    Alias,
    Copy,
    Move,
    NoDrop,
    NotAllowed,
    #[parse(aliases = "-moz-grab")]
    Grab,
    #[parse(aliases = "-moz-grabbing")]
    Grabbing,
    EResize,
    NResize,
    NeResize,
    NwResize,
    SResize,
    SeResize,
    SwResize,
    WResize,
    EwResize,
    NsResize,
    NeswResize,
    NwseResize,
    ColResize,
    RowResize,
    AllScroll,
    #[parse(aliases = "-moz-zoom-in")]
    ZoomIn,
    #[parse(aliases = "-moz-zoom-out")]
    ZoomOut,
    Auto,
}

/// The keywords allowed in the -moz-theme property.
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum MozTheme {
    /// Choose the default (maybe native) rendering.
    Auto,
    /// Choose the non-native rendering.
    NonNative,
}

/// The pointer-events property
/// https://svgwg.org/svg2-draft/interact.html#PointerEventsProperty
#[allow(missing_docs)]
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum PointerEvents {
    Auto,
    None,
    #[cfg(feature = "gecko")]
    Visiblepainted,
    #[cfg(feature = "gecko")]
    Visiblefill,
    #[cfg(feature = "gecko")]
    Visiblestroke,
    #[cfg(feature = "gecko")]
    Visible,
    #[cfg(feature = "gecko")]
    Painted,
    #[cfg(feature = "gecko")]
    Fill,
    #[cfg(feature = "gecko")]
    Stroke,
    #[cfg(feature = "gecko")]
    All,
}

/// Internal property to represent the inert attribute state:
/// https://html.spec.whatwg.org/multipage/#inert-subtrees
#[allow(missing_docs)]
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum Inert {
    None,
    Inert,
}

/// The interactivity property
/// https://drafts.csswg.org/css-ui-4/#propdef-interactivity
#[allow(missing_docs)]
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum Interactivity {
    Auto,
    Inert,
}

/// Internal -moz-user-focus property.
/// https://developer.mozilla.org/en-US/docs/Web/CSS/-moz-user-focus
#[allow(missing_docs)]
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
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum UserFocus {
    Normal,
    None,
    Ignore,
}

/// https://drafts.csswg.org/css-ui/#input-method-editor
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
pub enum ImeMode {
    Auto,
    Normal,
    Active,
    Disabled,
    Inactive,
}

/// https://drafts.csswg.org/css-scrollbars-1/#scrollbar-width
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
pub enum ScrollbarWidth {
    Auto,
    Thin,
    None,
}

/// Privileged property to control titlebar dragging area.
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
pub enum WindowDragging {
    Default,
    Drag,
    NoDrag,
}

/// None (Nonstandard internal property)
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
pub enum WindowShadow {
    Auto,
    None,
}

/// https://drafts.csswg.org/css-ui/#field-sizing
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
pub enum FieldSizing {
    Fixed,
    Content,
}
