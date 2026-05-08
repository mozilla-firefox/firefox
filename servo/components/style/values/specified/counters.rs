/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for counter properties.

use crate::counter_style::CounterStyle;
use crate::parser::{Parse, ParserContext};
use crate::values::generics::counters as generics;
use crate::values::generics::counters::CounterPair;
use crate::values::specified::image::Image;
use crate::values::specified::Attr;
use crate::values::specified::Integer;
use crate::values::CustomIdent;
use cssparser::{match_ignore_ascii_case, Parser, Token};
use selectors::parser::SelectorParseErrorKind;
use style_traits::{ParseError, StyleParseErrorKind};

#[derive(PartialEq)]
enum CounterType {
    Increment,
    Set,
    Reset,
}

impl CounterType {
    fn default_value(&self) -> i32 {
        match *self {
            Self::Increment => 1,
            Self::Reset | Self::Set => 0,
        }
    }
}

/// A specified value for the `counter-increment` property.
pub type CounterIncrement = generics::GenericCounterIncrement<Integer>;

impl Parse for CounterIncrement {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(Self::new(parse_counters(
            context,
            input,
            CounterType::Increment,
        )?))
    }
}

/// A specified value for the `counter-set` property.
pub type CounterSet = generics::GenericCounterSet<Integer>;

impl Parse for CounterSet {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(Self::new(parse_counters(context, input, CounterType::Set)?))
    }
}

/// A specified value for the `counter-reset` property.
pub type CounterReset = generics::GenericCounterReset<Integer>;

impl Parse for CounterReset {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(Self::new(parse_counters(
            context,
            input,
            CounterType::Reset,
        )?))
    }
}

fn parse_counters<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
    counter_type: CounterType,
) -> Result<Vec<CounterPair<Integer>>, ParseError<'i>> {
    if input
        .try_parse(|input| input.expect_ident_matching("none"))
        .is_ok()
    {
        return Ok(vec![]);
    }

    let mut counters = Vec::new();
    loop {
        let location = input.current_source_location();
        let (name, is_reversed) = match input.next() {
            Ok(&Token::Ident(ref ident)) => {
                (CustomIdent::from_ident(location, ident, &["none"])?, false)
            },
            Ok(&Token::Function(ref name))
                if counter_type == CounterType::Reset && name.eq_ignore_ascii_case("reversed") =>
            {
                input
                    .parse_nested_block(|input| Ok((CustomIdent::parse(input, &["none"])?, true)))?
            },
            Ok(t) => {
                let t = t.clone();
                return Err(location.new_unexpected_token_error(t));
            },
            Err(_) => break,
        };

        let value = match input.try_parse(|input| Integer::parse(context, input)) {
            Ok(start) => {
                if matches!(start.get(), Some(v) if v == i32::min_value()) {
                    // The spec says that values must be clamped to the valid range,
                    // and we reserve i32::min_value() as an internal magic value.
                    // https://drafts.csswg.org/css-lists/#auto-numbering
                    Integer::new(i32::min_value() + 1)
                } else {
                    start
                }
            },
            _ => Integer::new(if is_reversed {
                i32::min_value()
            } else {
                counter_type.default_value()
            }),
        };
        counters.push(CounterPair {
            name,
            value,
            is_reversed,
        });
    }

    if !counters.is_empty() {
        Ok(counters)
    } else {
        Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }
}

/// The specified value for the `content` property.
pub type Content = generics::GenericContent<Image>;

/// The specified value for a content item in the `content` property.
pub type ContentItem = generics::GenericContentItem<Image>;

impl Content {
    fn parse_counter_style(context: &ParserContext, input: &mut Parser) -> CounterStyle {
        use crate::counter_style::CounterStyleParsingFlags;
        input
            .try_parse(|input| {
                input.expect_comma()?;
                CounterStyle::parse(context, input, CounterStyleParsingFlags::empty())
            })
            .unwrap_or_else(|_| CounterStyle::decimal())
    }
}

impl Parse for Content {
    // normal | none | [ <string> | <counter> | open-quote | close-quote | no-open-quote |
    // no-close-quote ]+
    // TODO: <uri>, attr(<identifier>)
    #[cfg_attr(feature = "servo", allow(unused_mut))]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(generics::Content::Normal);
        }
        if input
            .try_parse(|input| input.expect_ident_matching("none"))
            .is_ok()
        {
            return Ok(generics::Content::None);
        }

        let mut items = thin_vec::ThinVec::new();
        let mut alt_start = None;
        loop {
            if alt_start.is_none() {
                if let Ok(image) = input.try_parse(|i| Image::parse_forbid_none(context, i)) {
                    items.push(generics::ContentItem::Image(image));
                    continue;
                }
            }
            let Ok(t) = input.next() else { break };
            match *t {
                Token::QuotedString(ref value) => {
                    items.push(generics::ContentItem::String(
                        value.as_ref().to_owned().into(),
                    ));
                },
                Token::Function(ref name) => {
                    // FIXME(emilio): counter() / counters() should be valid per spec past
                    // the alt marker, but it's likely non-trivial to support and other
                    // browsers don't support it either, so restricting it for now.
                    let result = match_ignore_ascii_case! { &name,
                        "counter" if alt_start.is_none() => input.parse_nested_block(|input| {
                            let name = CustomIdent::parse(input, &[])?;
                            let style = Content::parse_counter_style(context, input);
                            Ok(generics::ContentItem::Counter(name, style))
                        }),
                        "counters" if alt_start.is_none() => input.parse_nested_block(|input| {
                            let name = CustomIdent::parse(input, &[])?;
                            input.expect_comma()?;
                            let separator = input.expect_string()?.as_ref().to_owned().into();
                            let style = Content::parse_counter_style(context, input);
                            Ok(generics::ContentItem::Counters(name, separator, style))
                        }),
                        "attr" if !static_prefs::pref!("layout.css.attr.enabled") => input.parse_nested_block(|input| {
                            Ok(generics::ContentItem::Attr(Attr::parse_function(context, input)?))
                        }),
                        _ => {
                            use style_traits::StyleParseErrorKind;
                            let name = name.clone();
                            return Err(input.new_custom_error(
                                StyleParseErrorKind::UnexpectedFunction(name),
                            ))
                        }
                    }?;
                    items.push(result);
                },
                Token::Ident(ref ident) if alt_start.is_none() => {
                    items.push(match_ignore_ascii_case! { &ident,
                        "open-quote" => generics::ContentItem::OpenQuote,
                        "close-quote" => generics::ContentItem::CloseQuote,
                        "no-open-quote" => generics::ContentItem::NoOpenQuote,
                        "no-close-quote" => generics::ContentItem::NoCloseQuote,
                        #[cfg(feature = "gecko")]
                        "-moz-alt-content" if context.in_ua_sheet() => {
                            generics::ContentItem::MozAltContent
                        },
                        #[cfg(feature = "gecko")]
                        "-moz-label-content" if context.chrome_rules_enabled() => {
                            generics::ContentItem::MozLabelContent
                        },
                        _ =>{
                            let ident = ident.clone();
                            return Err(input.new_custom_error(
                                SelectorParseErrorKind::UnexpectedIdent(ident)
                            ));
                        }
                    });
                },
                Token::Delim('/')
                    if alt_start.is_none()
                        && !items.is_empty()
                        && static_prefs::pref!("layout.css.content.alt-text.enabled") =>
                {
                    alt_start = Some(items.len());
                },
                ref t => {
                    let t = t.clone();
                    return Err(input.new_unexpected_token_error(t));
                },
            }
        }
        if items.is_empty() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        let alt_start = alt_start.unwrap_or(items.len());
        Ok(generics::Content::Items(generics::GenericContentItems {
            items,
            alt_start,
        }))
    }
}
