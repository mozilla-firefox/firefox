/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! `list` specified values.

use crate::counter_style::{CounterStyle, CounterStyleParsingFlags};
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use cssparser::{match_ignore_ascii_case, Parser, Token};
use style_traits::{KeywordsCollectFn, ParseError, SpecifiedValueInfo, StyleParseErrorKind};

/// Specified and computed `list-style-type` property.
#[derive(
    Clone,
    Debug,
    Eq,
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
pub struct ListStyleType(pub CounterStyle);

impl ListStyleType {
    /// Initial specified value for `list-style-type`.
    #[inline]
    pub fn disc() -> Self {
        Self(CounterStyle::disc())
    }

    /// none value.
    #[inline]
    pub fn none() -> Self {
        Self(CounterStyle::None)
    }

    /// Returns whether `self` is a particular identifier.
    #[inline]
    pub fn is_name(&self, n: &crate::Atom) -> bool {
        self.0.is_name(n)
    }

    /// Convert from gecko keyword to list-style-type.
    ///
    /// This should only be used for mapping type attribute to list-style-type, and thus only
    /// values possible in that attribute is considered here.
    #[cfg(feature = "gecko")]
    pub fn from_gecko_keyword(value: u32) -> Self {
        use crate::gecko_bindings::structs;
        use crate::values::CustomIdent;
        let v8 = value as u8;
        if v8 == structs::ListStyle_None {
            return Self::none();
        }

        Self(CounterStyle::Name(CustomIdent(match v8 {
            structs::ListStyle_Disc => atom!("disc"),
            structs::ListStyle_Circle => atom!("circle"),
            structs::ListStyle_Square => atom!("square"),
            structs::ListStyle_Decimal => atom!("decimal"),
            structs::ListStyle_LowerRoman => atom!("lower-roman"),
            structs::ListStyle_UpperRoman => atom!("upper-roman"),
            structs::ListStyle_LowerAlpha => atom!("lower-alpha"),
            structs::ListStyle_UpperAlpha => atom!("upper-alpha"),
            _ => unreachable!("Unknown counter style keyword value"),
        })))
    }

    /// Is this a bullet? (i.e. `list-style-type: disc|circle|square|disclosure-closed|disclosure-open`)
    #[inline]
    pub fn is_bullet(&self) -> bool {
        self.0.is_bullet()
    }
}

impl Parse for ListStyleType {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let flags = CounterStyleParsingFlags::ALLOW_NONE | CounterStyleParsingFlags::ALLOW_STRING;
        Ok(Self(CounterStyle::parse(context, input, flags)?))
    }
}

/// A quote pair.
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
)]
#[repr(C)]
pub struct QuotePair {
    /// The opening quote.
    pub opening: crate::OwnedStr,

    /// The closing quote.
    pub closing: crate::OwnedStr,
}

/// List of quote pairs for the specified/computed value of `quotes` property.
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
)]
#[repr(transparent)]
pub struct QuoteList(
    #[css(iterable, if_empty = "none")]
    #[ignore_malloc_size_of = "Arc"]
    pub crate::ArcSlice<QuotePair>,
);

/// Specified and computed `quotes` property: `auto`, `none`, `match-parent`,
/// or a list of characters.
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub enum Quotes {
    /// list of quote pairs
    QuoteList(QuoteList),
    /// auto (use lang-dependent quote marks)
    Auto,
    /// match-parent (use the value from the element's parent)
    MatchParent,
}

impl SpecifiedValueInfo for Quotes {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        f(&["auto", "none"]);
        if crate::pref!("layout.css.quotes.match-parent.enabled") {
            f(&["match-parent"]);
        }
    }
}

impl Parse for Quotes {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Quotes, ParseError> {
        let mut quotes = Vec::new();
        loop {
            let opening = match input.next() {
                Ok(Token::QuotedString(value)) => value.as_ref().to_owned().into(),
                Ok(Token::Ident(id)) if quotes.is_empty() => return Self::from_ident(id),
                Ok(_) => return Err(ParseError::unexpected_token()),
                Err(_) => break,
            };

            let closing = input.expect_string()?.as_ref().to_owned().into();
            quotes.push(QuotePair { opening, closing });
        }

        if !quotes.is_empty() {
            Ok(Quotes::QuoteList(QuoteList(crate::ArcSlice::from_iter(
                quotes.into_iter(),
            ))))
        } else {
            Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError))
        }
    }
}

impl Quotes {
    fn from_ident(id: &str) -> Result<Self, ParseError> {
        match_ignore_ascii_case! { id,
            "auto" => Ok(Self::Auto),
            "none" => Ok(Self::QuoteList(QuoteList::default())),
            "match-parent" if crate::pref!("layout.css.quotes.match-parent.enabled") => {
                Ok(Self::MatchParent)
            },
            _ => Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError)),
        }
    }
}

/// https://drafts.csswg.org/css-lists/#propdef-list-style-position
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
pub enum ListStylePosition {
    Outside,
    Inside,
}
