/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A media query:
//!
//! https://drafts.csswg.org/mediaqueries/#typedef-media-query

use crate::Atom;
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::error_reporting::ContextualParseError;
use crate::parser::{Parse, ParserContext};
use crate::queries::boolean_expression::{BooleanExpr, BooleanExprTest, Operator};
use crate::queries::feature::FeatureFlagsInfo;
use crate::queries::moz_pref::MozPrefFeature;
use crate::queries::{FeatureFlags, FeatureType, QueryFeatureExpression};
use crate::str::string_as_ascii_lowercase;
use crate::stylesheets::CustomMediaEvaluator;
use crate::values::{CustomIdent, DashedIdent, computed};
use cssparser::{Parser, Token, match_ignore_ascii_case};
use selectors::kleene_value::KleeneValue;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

/// <https://drafts.csswg.org/mediaqueries/#mq-prefix>
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, Parse, PartialEq, ToCss, ToShmem)]
pub enum Qualifier {
    /// Hide a media query from legacy UAs:
    /// <https://drafts.csswg.org/mediaqueries/#mq-only>
    Only,
    /// Negate a media query:
    /// <https://drafts.csswg.org/mediaqueries/#mq-not>
    Not,
}

/// <https://drafts.csswg.org/mediaqueries/#media-types>
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
pub struct MediaType(pub CustomIdent);

impl MediaType {
    /// The `screen` media type.
    pub fn screen() -> Self {
        MediaType(CustomIdent(atom!("screen")))
    }

    /// The `print` media type.
    pub fn print() -> Self {
        MediaType(CustomIdent(atom!("print")))
    }

    fn parse(name: &str) -> Result<Self, ()> {
        // From https://drafts.csswg.org/mediaqueries/#mq-syntax:
        //
        //   The <media-type> production does not include the keywords only, not, and, or, and layer.
        //
        // Here we also perform the to-ascii-lowercase part of the serialization
        // algorithm: https://drafts.csswg.org/cssom/#serializing-media-queries
        match_ignore_ascii_case! { name,
            "not" | "or" | "and" | "only" | "layer" => Err(()),
            _ => Ok(MediaType(CustomIdent(Atom::from(string_as_ascii_lowercase(name))))),
        }
    }
}

/// A [media query][mq].
///
/// [mq]: https://drafts.csswg.org/mediaqueries/
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct MediaQuery {
    /// The qualifier for this query.
    pub qualifier: Option<Qualifier>,
    /// The media type for this query, that can be known, unknown, or "all".
    pub media_type: MediaQueryType,
    /// The condition that this media query contains. This cannot have `or`
    /// in the first level.
    pub condition: Option<MediaCondition>,
}

impl ToCss for MediaQuery {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if let Some(qual) = self.qualifier {
            qual.to_css(dest)?;
            dest.write_char(' ')?;
        }

        match self.media_type {
            MediaQueryType::All => {
                // We need to print "all" if there's a qualifier, or there's
                // just an empty list of expressions.
                //
                // Otherwise, we'd serialize media queries like "(min-width:
                // 40px)" in "all (min-width: 40px)", which is unexpected.
                if self.qualifier.is_some() || self.condition.is_none() {
                    dest.write_str("all")?;
                }
            },
            MediaQueryType::Concrete(MediaType(ref desc)) => desc.to_css(dest)?,
        }

        let condition = match self.condition {
            Some(ref c) => c,
            None => return Ok(()),
        };

        if self.media_type != MediaQueryType::All || self.qualifier.is_some() {
            dest.write_str(" and ")?;
        }

        condition.to_css(dest)
    }
}

impl MediaQuery {
    /// Return a media query that never matches, used for when we fail to parse
    /// a given media query.
    pub fn never_matching() -> Self {
        Self {
            qualifier: Some(Qualifier::Not),
            media_type: MediaQueryType::All,
            condition: None,
        }
    }

    /// Returns whether this media query depends on the viewport.
    pub fn is_viewport_dependent(&self) -> bool {
        self.condition
            .as_ref()
            .is_some_and(|c| c.feature_flags().contains(FeatureFlags::VIEWPORT_DEPENDENT))
    }

    /// Parse a media query given css input.
    ///
    /// Returns an error if any of the expressions is unknown.
    pub fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let (qualifier, explicit_media_type) = input
            .try_parse(|input| -> Result<_, ()> {
                let qualifier = input.try_parse(Qualifier::parse).ok();
                let ident = input.expect_ident().map_err(|_| ())?;
                let media_type = MediaQueryType::parse(ident)?;
                Ok((qualifier, Some(media_type)))
            })
            .unwrap_or_default();

        let condition = if explicit_media_type.is_none() {
            Some(MediaCondition::parse(context, input)?)
        } else if input.try_parse(|i| i.expect_ident_matching("and")).is_ok() {
            // https://drafts.csswg.org/mediaqueries-5/#typedef-media-condition-without-or
            let condition = MediaCondition::parse(context, input)?;
            if matches!(condition, BooleanExpr::Operation(_, Operator::Or)) {
                return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
            }
            Some(condition)
        } else {
            None
        };

        let media_type = explicit_media_type.unwrap_or(MediaQueryType::All);
        Ok(Self {
            qualifier,
            media_type,
            condition,
        })
    }
}

/// <http://dev.w3.org/csswg/mediaqueries-3/#media0>
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
pub enum MediaQueryType {
    /// A media type that matches every device.
    All,
    /// A specific media type.
    Concrete(MediaType),
}

impl MediaQueryType {
    fn parse(ident: &str) -> Result<Self, ()> {
        match_ignore_ascii_case! { ident,
            "all" => return Ok(MediaQueryType::All),
            _ => (),
        };

        // If parseable, accept this type as a concrete type.
        MediaType::parse(ident).map(MediaQueryType::Concrete)
    }

    /// Returns whether this media query type matches a MediaType.
    pub fn matches(&self, other: MediaType) -> bool {
        match *self {
            MediaQueryType::All => true,
            MediaQueryType::Concrete(ref known_type) => *known_type == other,
        }
    }
}

/// A `<media-condition>`, which is a combination of media condition tests.
/// https://drafts.csswg.org/mediaqueries-5/#typedef-media-condition
pub type MediaCondition = BooleanExpr<MediaConditionTest>;

/// A media condition test. Note that this is somewhat like the `<media-in-parens>`
/// type in the specification, but the name doesn't match exactly because of the
/// non-parenthesized `-moz-pref()` variant and the lack of `<general-enclosed>`,
/// which is handled by the enclosing `MediaCondition` type.
/// https://drafts.csswg.org/mediaqueries-5/#typedef-media-in-parens
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum MediaConditionTest {
    /// A simple feature expression, implicitly parenthesized.
    Feature(QueryFeatureExpression),
    /// A custom media query reference in a boolean context, implicitly parenthesized.
    Custom(DashedIdent),
    /// A -moz-pref() query.
    MozPref(MozPrefFeature),
}

impl Parse for MediaConditionTest {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        match *input.next()? {
            Token::ParenthesisBlock => input.parse_nested_block(|input| {
                // Make sure to preserve this error as it's more generally relevant.
                let feature_error = match input.try_parse(|input| {
                    QueryFeatureExpression::parse_in_parenthesis_block(
                        context,
                        input,
                        FeatureType::Media,
                    )
                }) {
                    Ok(expr) => return Ok(Self::Feature(expr)),
                    Err(e) => e,
                };
                if crate::pref!("layout.css.custom-media.enabled")
                    && let Ok(custom) = input.try_parse(|input| DashedIdent::parse(context, input))
                {
                    return Ok(Self::Custom(custom));
                }
                Err(feature_error)
            }),
            Token::Function(ref name)
                if name.eq_ignore_ascii_case("-moz-pref") && context.chrome_rules_enabled() =>
            {
                Ok(Self::MozPref(input.parse_nested_block(|input| {
                    MozPrefFeature::parse(context, input)
                })?))
            },
            _ => Err(ParseError::unexpected_token()),
        }
    }
}

impl ToCss for MediaConditionTest {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match self {
            // NOTE(emilio): QueryFeatureExpression already includes the
            // parenthesis.
            Self::Feature(feature) => feature.to_css(dest),
            Self::Custom(name) => {
                dest.write_char('(')?;
                name.to_css(dest)?;
                dest.write_char(')')
            },
            Self::MozPref(feature) => {
                dest.write_str("-moz-pref(")?;
                feature.to_css(dest)?;
                dest.write_char(')')
            },
        }
    }
}

impl BooleanExprTest for MediaConditionTest {
    fn evaluate(
        &self,
        context: &computed::Context,
        custom: &mut CustomMediaEvaluator,
        _: &mut AttributeTracker,
    ) -> KleeneValue {
        match self {
            Self::Feature(feature) => feature.matches(context),
            Self::Custom(name) => custom.matches(name, context),
            Self::MozPref(feature) => feature.matches(context),
        }
    }

    fn new_reporting_error<'a>(
        css: &'a str,
        error: ParseError,
    ) -> Option<ContextualParseError<'a>> {
        Some(ContextualParseError::InvalidMediaRule(css, error))
    }
}

impl FeatureFlagsInfo for MediaConditionTest {
    fn feature_flags(&self) -> FeatureFlags {
        match self {
            Self::Feature(feature) => feature.feature_flags(),
            Self::Custom(_) | Self::MozPref(_) => FeatureFlags::empty(),
        }
    }
}
