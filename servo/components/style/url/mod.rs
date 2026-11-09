/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Common handling for the specified value CSS url() values.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::stylesheets::CorsMode;
use cssparser::Parser;
use style_traits::ParseError;

#[cfg(feature = "gecko")]
pub mod gecko;
#[cfg(feature = "gecko")]
pub use gecko::{ComputedUrl, CssUrl, SpecifiedUrl};
#[cfg(feature = "servo")]
pub mod servo;
#[cfg(feature = "servo")]
pub use servo::{ComputedUrl, CssUrl, SpecifiedUrl};

/// What caused a CSS url() to be loaded, which determines the Resource Timing
/// initiator type of the load.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq, ToShmem)]
pub enum UrlInitiator {
    /// A regular CSS url().
    Css,
    /// The background attribute of a <body> element.
    Body,
}

impl CssUrl {
    /// Parse a URL with a particular CORS mode.
    pub fn parse_with_cors_mode(
        context: &ParserContext,
        input: &mut Parser,
        cors_mode: CorsMode,
    ) -> Result<Self, ParseError> {
        let start = input.position().byte_index();
        let url = input.expect_url()?;
        let end = input.position().byte_index();
        Self::parse_from_string(url.as_ref().to_owned(), start, end, context, cors_mode)
    }

    /// Parse a URL from a string value that is a valid CSS token for a URL,
    /// enforcing attr()-tainting constraints if applicable.
    /// https://drafts.csswg.org/css-values-5/#attr-security
    pub fn parse_from_string(
        url: String,
        url_start: usize,
        url_end: usize,
        context: &ParserContext,
        cors_mode: CorsMode,
    ) -> Result<Self, ParseError> {
        use crate::substitution::AttrTaintedRange;
        use style_traits::StyleParseErrorKind;
        let range = AttrTaintedRange::new(url_start, url_end);
        if context.disallow_urls_in_range(&range) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(Self::new_from_string(
            url,
            context,
            cors_mode,
            UrlInitiator::Css,
        ))
    }

    /// Create a new CSS URL that is attr()-untainted given a valid CSS token for a URL.
    /// Be cautious when calling `expect_url()` to not bypass attr()-tainting checks. If
    /// it's possible attr()'s were substituted into the `url`, DO NOT use this method.
    /// https://drafts.csswg.org/css-values-5/#attr-security
    pub fn new_from_untainted_string(
        url: String,
        context: &ParserContext,
        cors_mode: CorsMode,
        initiator: UrlInitiator,
    ) -> Self {
        debug_assert!(context.attr_tainted_regions.is_empty());
        Self::new_from_string(url, context, cors_mode, initiator)
    }
}

impl Parse for CssUrl {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Self::parse_with_cors_mode(context, input, CorsMode::None)
    }
}
