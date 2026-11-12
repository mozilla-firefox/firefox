/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for the `<random-key>` of the CSS `random()` function.
//! https://drafts.csswg.org/css-values-5/#randomness

use crate::Atom;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::properties::PropertyIdRef;
use crate::values::computed::{Context, ToComputedValue};
use crate::values::specified::calc::PercentageContext;
use crate::values::specified::number::{Number, parse_number_with_clamping_mode};
use crate::values::{CSSFloat, CustomIdent, DashedIdent};
use cssparser::{Parser, match_ignore_ascii_case};
use selectors::parser::SelectorParseErrorKind;
use std::fmt::{self, Write};
use style_traits::values::SequenceWriter;
use style_traits::values::specified::AllowedNumericType;
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

/// The <random-ua-ident> part of a <random-cache-key>. This is a <custom-ident>
/// starting with `ua-` that is typically automatically constructed by the user agent.
///
/// https://drafts.csswg.org/css-values-5/#valdef-random-ua-ident
#[repr(C)]
#[derive(Clone, Debug, Hash, MallocSizeOf, PartialEq, ToCss, ToShmem)]
pub struct RandomUaIdent(CustomIdent);

impl RandomUaIdent {
    fn ua_prefixed(property_id: PropertyIdRef) -> String {
        let mut ua_ident = "ua-".to_string();
        match property_id {
            PropertyIdRef::NonCustom(non_custom) => {
                ua_ident += non_custom.name();
            },
            PropertyIdRef::Custom(custom) => {
                ua_ident += "--";
                #[cfg(feature = "gecko")]
                {
                    custom.with_str(|name| ua_ident += name);
                }
                #[cfg(feature = "servo")]
                {
                    ua_ident += custom;
                }
            },
        };
        ua_ident
    }

    /// Builds the `ua-PROPERTY` ident used to replace `property-scoped`.
    pub fn from_property(property_id: PropertyIdRef) -> Self {
        Self(CustomIdent(Atom::from(Self::ua_prefixed(property_id))))
    }

    /// Builds the `ua-PROPERTY-INDEX` ident used to replace `property-index-scoped`,
    /// where `index` is the 1-based index of this random function among all of the
    /// random functions in the declaration.
    pub fn from_property_and_index(property_id: PropertyIdRef, index: i32) -> Self {
        let mut ua_ident = Self::ua_prefixed(property_id);
        write!(ua_ident, "-{index}").unwrap();
        Self(CustomIdent(Atom::from(ua_ident)))
    }

    /// Special value for internal use. Useful where we can't use Option<>.
    pub fn empty() -> Self {
        Self(CustomIdent(atom!("")))
    }

    /// Check for special internal value.
    pub fn is_empty(&self) -> bool {
        self.0.0 == atom!("")
    }
}

impl Parse for RandomUaIdent {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let ident = input.expect_ident()?;

        if ident.eq_ignore_ascii_case("property-scoped") {
            let Some(property_id) = context.property_declaration_context.property_id() else {
                return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
            };
            return Ok(RandomUaIdent::from_property(property_id));
        }

        if ident.eq_ignore_ascii_case("property-index-scoped") {
            let Some(property_id) = context.property_declaration_context.property_id() else {
                return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
            };
            let index = context.property_declaration_context.current_random_index();
            return Ok(RandomUaIdent::from_property_and_index(property_id, index));
        }

        if !ident.starts_with("ua-") {
            return Err(ParseError::custom(SelectorParseErrorKind::UnexpectedIdent));
        }
        CustomIdent::from_ident(ident, &[]).map(RandomUaIdent)
    }
}

/// A <random-cache-key> that specifies parts of the random cache name.
///
/// https://drafts.csswg.org/css-values-5/#valdef-random-random-cache-key
#[repr(C)]
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct RandomCacheKey {
    /// The author-specified <dashed-ident>, or `DashedIdent::empty()` if absent.
    pub name: DashedIdent,
    /// The <random-ua-ident>, either as author-specified or as constructed to replace
    /// `property-scoped`/`property-index-scoped`, or `RandomUaIdent::empty()` if absent.
    pub ua_ident: RandomUaIdent,
    /// Whether `element-scoped` was specified.
    pub is_element_scoped: bool,
}

impl Parse for RandomCacheKey {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let mut key = RandomCacheKey {
            name: DashedIdent::empty(),
            ua_ident: RandomUaIdent::empty(),
            is_element_scoped: false,
        };

        loop {
            if key.name.is_empty()
                && let Ok(name) = input.try_parse(|input| DashedIdent::parse(context, input))
            {
                key.name = name;
                continue;
            }

            if !key.is_element_scoped
                && input
                    .try_parse(|input| input.expect_ident_matching("element-scoped"))
                    .is_ok()
            {
                if !context.has_element_context() {
                    return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                }

                key.is_element_scoped = true;
                continue;
            }

            if key.ua_ident.is_empty()
                && let Ok(ua_ident) = input.try_parse(|input| RandomUaIdent::parse(context, input))
            {
                key.ua_ident = ua_ident;
                continue;
            }

            break;
        }

        if key.name.is_empty() && !key.is_element_scoped && key.ua_ident.is_empty() {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(key)
    }
}

impl ToCss for RandomCacheKey {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        let mut writer = SequenceWriter::new(dest, " ");
        if !self.name.is_empty() {
            writer.item(&self.name)?;
        }
        if self.is_element_scoped {
            writer.raw_item("element-scoped")?;
        }
        if !self.ua_ident.is_empty() {
            writer.item(&self.ua_ident)?;
        }
        Ok(())
    }
}

/// A <random-key> that specifies how to compute the random base value for
/// a random function. Note that the `auto` value is not modeled as it is
/// immediately converted to the `element-scoped property-index-scoped`
/// cache key format during parsing.
///
/// https://drafts.csswg.org/css-values-5/#typedef-random-key
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
#[repr(u8)]
pub enum RandomKey {
    /// A <number> used directly as the random base value.
    /// TODO(bug 2071971): This is broken if it manages to get into a computed node. Refactor this
    /// away altogether.
    Fixed(Number),
    /// A <random-cache-key> used to generate the random base value.
    CacheKey(RandomCacheKey),
}

impl RandomKey {
    /// The result of the `auto` value (the default value of an omitted `<random-key>`),
    /// which is equivalent to specifying `element-scoped property-index-scoped`.
    pub fn auto(context: &ParserContext) -> Result<Self, ParseError> {
        if !context.has_element_context() {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }

        let Some(property_id) = context.property_declaration_context.property_id() else {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        };
        let index = context.property_declaration_context.current_random_index();

        Ok(Self::CacheKey(RandomCacheKey {
            name: DashedIdent::empty(),
            ua_ident: RandomUaIdent::from_property_and_index(property_id, index),
            is_element_scoped: true,
        }))
    }
}

impl Parse for RandomKey {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if let Ok(key) = input.try_parse(|input| RandomCacheKey::parse(context, input)) {
            return Ok(Self::CacheKey(key));
        }

        let ident = input.expect_ident()?;
        match_ignore_ascii_case! { &ident,
            "auto" => Self::auto(context),
            "fixed" => {
                let fixed = parse_number_with_clamping_mode(
                    context,
                    input,
                    AllowedNumericType::ZeroToOne,
                    PercentageContext::not_allowed(),
                )?;
                Ok(Self::Fixed(fixed))
            },
            _ => Err(ParseError::custom(SelectorParseErrorKind::UnexpectedIdent)),
        }
    }
}

impl ToComputedValue for RandomKey {
    type ComputedValue = CSSFloat;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self {
            Self::Fixed(number) => number.to_computed_value(context),
            Self::CacheKey(cache_key) => context.random_base_value(cache_key),
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self::Fixed(Number::from_computed_value(computed))
    }
}

impl ToCss for RandomKey {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match self {
            RandomKey::Fixed(number) => {
                dest.write_str("fixed ")?;
                number.to_css(dest)
            },
            RandomKey::CacheKey(cache_key) => cache_key.to_css(dest),
        }
    }
}
