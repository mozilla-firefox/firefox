/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! This module contains shared types and messages for use by devtools/script.
//! The traits are here instead of in script so that the devtools crate can be
//! modified independently of the rest of Servo.

#![crate_name = "style_traits"]
#![crate_type = "rlib"]
#![deny(unsafe_code, missing_docs)]

#[macro_use]
extern crate malloc_size_of_derive;
#[macro_use]
extern crate serde;
#[macro_use]
extern crate to_shmem_derive;
#[cfg(feature = "servo")]
extern crate url;

use bitflags::bitflags;
use selectors::parser::SelectorParseErrorKind;
#[cfg(feature = "servo")]
use stylo_atoms::Atom;

/// One hardware pixel.
///
/// This unit corresponds to the smallest addressable element of the display hardware.
#[derive(Clone, Copy, Debug)]
pub enum DevicePixel {}

/// Represents a mobile style pinch zoom factor.
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize)]
pub struct PinchZoomFactor(f32);

impl PinchZoomFactor {
    /// Construct a new pinch zoom factor.
    pub fn new(scale: f32) -> PinchZoomFactor {
        PinchZoomFactor(scale)
    }

    /// Get the pinch zoom factor as an untyped float.
    pub fn get(&self) -> f32 {
        self.0
    }
}

/// One CSS "px" in the coordinate system of the "initial viewport":
/// <http://www.w3.org/TR/css-device-adapt/#initial-viewport>
///
/// `CSSPixel` is equal to `DeviceIndependentPixel` times a "page zoom" factor controlled by the user.  This is
/// the desktop-style "full page" zoom that enlarges content but then reflows the layout viewport
/// so it still exactly fits the visible area.
///
/// At the default zoom level of 100%, one `CSSPixel` is equal to one `DeviceIndependentPixel`.  However, if the
/// document is zoomed in or out then this scale may be larger or smaller.
#[derive(Clone, Copy, Debug)]
pub enum CSSPixel {}

// In summary, the hierarchy of pixel units and the factors to convert from one to the next:
//
// DevicePixel
//   / hidpi_ratio => DeviceIndependentPixel
//     / desktop_zoom => CSSPixel

pub mod arc_slice;
pub mod dom;
pub mod specified_value_info;
#[macro_use]
pub mod values;
pub mod owned_array;
pub mod owned_slice;
pub mod owned_str;

pub use crate::specified_value_info::{CssType, KeywordsCollectFn, SpecifiedValueInfo};
pub use crate::values::{
    Comma, CommaWithSpace, CssString, CssStringWriter, CssWriter, OneOrMoreSeparated, Separator,
    Space, ToCss,
};

/// The error type for all CSS parsing routines.
pub type ParseError = cssparser::ParseError<StyleParseErrorKind>;

#[derive(Clone, Debug, PartialEq)]
/// Errors that can be encountered while parsing CSS values.
pub enum StyleParseErrorKind {
    /// A bad URL token in a DVB.
    BadUrlInDeclarationValueBlock,
    /// A bad string token in a DVB.
    BadStringInDeclarationValueBlock,
    /// Unexpected closing parenthesis in a DVB.
    UnbalancedCloseParenthesisInDeclarationValueBlock,
    /// Unexpected closing bracket in a DVB.
    UnbalancedCloseSquareBracketInDeclarationValueBlock,
    /// Unexpected closing curly bracket in a DVB.
    UnbalancedCloseCurlyBracketInDeclarationValueBlock,
    /// A property declaration value had input remaining after successfully parsing.
    PropertyDeclarationValueNotExhausted,
    /// An unexpected dimension token was encountered.
    UnexpectedDimension,
    /// Missing or invalid media feature name.
    MediaQueryExpectedFeatureName,
    /// Missing or invalid media feature value.
    MediaQueryExpectedFeatureValue,
    /// A media feature range operator was not expected.
    MediaQueryUnexpectedOperator,
    /// min- or max- properties must have a value.
    RangedExpressionWithNoValue,
    /// A function was encountered that was not expected.
    UnexpectedFunction,
    /// Error encountered parsing a @property's `syntax` descriptor
    PropertySyntaxField(PropertySyntaxParseError),
    /// Error encountered parsing a @property's `inherits` descriptor.
    ///
    /// TODO(zrhoffman, bug 1920365): Include the custom property name in error messages.
    PropertyInheritsField(PropertyInheritsParseError),
    /// @namespace must be before any rule but @charset and @import
    UnexpectedNamespaceRule,
    /// @import must be before any rule but @charset
    UnexpectedImportRule,
    /// @import rules are disallowed in the parser.
    DisallowedImportRule,
    /// Unexpected @charset rule encountered.
    UnexpectedCharsetRule,
    /// The @property `<custom-property-name>` must start with `--`
    UnexpectedIdent,
    /// A placeholder for many sources of errors that require more specific variants.
    UnspecifiedError,
    /// An unexpected token was found within a namespace rule.
    UnexpectedTokenWithinNamespace,
    /// An error was encountered while parsing a selector
    SelectorError(SelectorParseErrorKind),
    /// The property declaration was for an unknown property.
    UnknownProperty,
    /// The property declaration was for a disabled experimental property.
    ExperimentalProperty,
    /// The property declaration contained an invalid value.
    OtherInvalidValue,
    /// `!important` declarations are disallowed in `@position-try` or keyframes.
    UnexpectedImportantDeclaration,
}

impl From<SelectorParseErrorKind> for StyleParseErrorKind {
    fn from(this: SelectorParseErrorKind) -> Self {
        StyleParseErrorKind::SelectorError(this)
    }
}

/// Errors that can be encountered while parsing the @property rule's syntax descriptor.
#[derive(Clone, Debug, PartialEq)]
pub enum PropertySyntaxParseError {
    /// The syntax descriptor is required for the @property rule to be valid; if it’s missing, the
    /// @property rule is invalid.
    ///
    /// <https://drafts.css-houdini.org/css-properties-values-api-1/#ref-for-descdef-property-syntax②>
    NoSyntax,
    /// The string's length was 0.
    EmptyInput,
    /// A non-whitespace, non-pipe character was fount after parsing a component.
    ExpectedPipeBetweenComponents,
    /// The start of an identifier was expected but not found.
    ///
    /// <https://drafts.csswg.org/css-syntax-3/#name-start-code-point>
    InvalidNameStart,
    /// The name is not a valid `<ident>`.
    InvalidName,
    /// The data type name was not closed.
    ///
    /// <https://drafts.css-houdini.org/css-properties-values-api-1/#consume-data-type-name>
    UnclosedDataTypeName,
    /// The next byte was expected while parsing, but EOF was found instead.
    UnexpectedEOF,
    /// The data type is not a supported syntax component name.
    ///
    /// <https://drafts.css-houdini.org/css-properties-values-api-1/#supported-names>
    UnknownDataTypeName,
}

/// Errors that can be encountered while parsing the @property rule's inherits descriptor.
#[derive(Clone, Debug, PartialEq)]
pub enum PropertyInheritsParseError {
    /// The inherits descriptor is required for the @property rule to be valid; if it’s missing,
    /// the @property rule is invalid.
    ///
    /// <https://drafts.css-houdini.org/css-properties-values-api-1/#ref-for-descdef-property-inherits②>
    NoInherits,

    /// The inherits descriptor must successfully parse as `true` or `false`.
    InvalidInherits,
}

bitflags! {
    /// The mode to use when parsing values.
    #[derive(Clone, Copy, Eq, PartialEq)]
    #[repr(C)]
    pub struct ParsingMode: u8 {
        /// In CSS; lengths must have units, except for zero values, where the unit can be omitted.
        /// <https://www.w3.org/TR/css3-values/#lengths>
        const DEFAULT = 0;
        /// In SVG; a coordinate or length value without a unit identifier (e.g., "25") is assumed
        /// to be in user units (px).
        /// <https://www.w3.org/TR/SVG/coords.html#Units>
        const ALLOW_UNITLESS_LENGTH = 1;
        /// In SVG; out-of-range values are not treated as an error in parsing.
        /// <https://www.w3.org/TR/SVG/implnote.html#RangeClamping>
        const ALLOW_ALL_NUMERIC_VALUES = 1 << 1;
        /// In CSS Properties and Values, the initial value must be computationally
        /// independent.
        /// <https://drafts.css-houdini.org/css-properties-values-api-1/#ref-for-computationally-independent%E2%91%A0>
        const DISALLOW_COMPUTATIONALLY_DEPENDENT = 1 << 2;
        /// In Typed OM; unitless zero must not be interpreted as a length.
        const DISALLOW_UNITLESS_ZERO_LENGTH = 1 << 3;
        /// Media query conditions in the preludes for @media, @custom-media, and @import.
        /// <https://drafts.csswg.org/mediaqueries/>
        const MEDIA_QUERY_CONDITION = 1 << 4;
    }
}

impl ParsingMode {
    /// Whether the parsing mode allows unitless lengths for non-zero values to be intpreted as px.
    #[inline]
    pub fn allows_unitless_lengths(&self) -> bool {
        self.intersects(ParsingMode::ALLOW_UNITLESS_LENGTH)
    }

    /// Whether the parsing mode allows all numeric values.
    #[inline]
    pub fn allows_all_numeric_values(&self) -> bool {
        self.intersects(ParsingMode::ALLOW_ALL_NUMERIC_VALUES)
    }

    /// Whether the parsing mode allows units or functions that are not computationally independent.
    #[inline]
    pub fn allows_computational_dependence(&self) -> bool {
        !self.intersects(ParsingMode::DISALLOW_COMPUTATIONALLY_DEPENDENT)
    }

    /// Whether the parsing mode allows unitless zero lengths to be interpreted as px.
    #[inline]
    pub fn allows_unitless_zero_lengths(&self) -> bool {
        !self.intersects(ParsingMode::DISALLOW_UNITLESS_ZERO_LENGTH)
    }
}

#[cfg(feature = "servo")]
/// Speculatively execute paint code in the worklet thread pool.
pub trait SpeculativePainter: Send + Sync {
    /// <https://drafts.css-houdini.org/css-paint-api/#draw-a-paint-image>
    fn speculatively_draw_a_paint_image(
        &self,
        properties: Vec<(Atom, String)>,
        arguments: Vec<String>,
    );
}
