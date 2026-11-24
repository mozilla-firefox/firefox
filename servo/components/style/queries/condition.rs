/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A query condition:
//!
//! https://drafts.csswg.org/mediaqueries-4/#typedef-media-condition
//! https://drafts.csswg.org/css-contain-3/#typedef-container-condition

use super::moz_pref::MozPrefFeature;
use super::style::StyleQuery;
use super::{FeatureFlags, FeatureType, QueryFeatureExpression};
use crate::context::QuirksMode;
use crate::custom_properties;
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::stylesheets::container_rule::AttrReferenceSet;
use crate::stylesheets::{CssRuleType, CustomMediaEvaluator, Origin, UrlExtraData};
use crate::substitution;
use crate::substitution::AttrTaint;
use crate::values::{DashedIdent, computed};
use crate::{error_reporting::ContextualParseError, parser::Parse, parser::ParserContext};
use cssparser::{Parser, SourceLocation, SourcePosition, Token, match_ignore_ascii_case};
use selectors::kleene_value::KleeneValue;
use std::borrow::Cow;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, ParsingMode, StyleParseErrorKind, ToCss};

/// A binary `and` or `or` operator.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, Parse, PartialEq, ToCss, ToShmem)]
#[allow(missing_docs)]
pub enum Operator {
    And,
    Or,
}

/// Whether to allow an `or` condition or not during parsing.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToCss)]
#[allow(missing_docs)]
pub enum AllowOr {
    Yes,
    No,
}

/// Trait for query elements that parse a series of conditions separated by
/// AND or OR operators, or prefixed with NOT.
///
/// This is used by both QueryCondition and StyleQuery as they support similar
/// syntax for combining multiple conditions with a boolean operator.
pub trait OperationParser: Sized {
    /// https://drafts.csswg.org/mediaqueries-5/#typedef-media-condition or
    /// https://drafts.csswg.org/mediaqueries-5/#typedef-media-condition-without-or
    /// (depending on `allow_or`).
    fn parse_internal(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
        allow_or: AllowOr,
    ) -> Result<Self, ParseError> {
        if input.try_parse(|i| i.expect_ident_matching("not")).is_ok() {
            let inner_condition = Self::parse_in_parens(context, input, feature_type)?;
            return Ok(Self::new_not(Box::new(inner_condition)));
        }

        let first_condition = Self::parse_in_parens(context, input, feature_type)?;
        let operator = match input.try_parse(Operator::parse) {
            Ok(op) => op,
            Err(..) => return Ok(first_condition),
        };

        if allow_or == AllowOr::No && operator == Operator::Or {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }

        let mut conditions = vec![];
        conditions.push(first_condition);
        conditions.push(Self::parse_in_parens(context, input, feature_type)?);

        let delim = match operator {
            Operator::And => "and",
            Operator::Or => "or",
        };

        loop {
            if input.try_parse(|i| i.expect_ident_matching(delim)).is_err() {
                return Ok(Self::new_operation(conditions.into_boxed_slice(), operator));
            }

            conditions.push(Self::parse_in_parens(context, input, feature_type)?);
        }
    }

    /// Parse a condition in parentheses, or `<general-enclosed>`.
    fn parse_in_parens(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError>;

    /// Helpers to create the appropriate enum variant of the implementing type:
    /// Create a Not result that encapsulates the `inner` condition.
    fn new_not(inner: Box<Self>) -> Self;

    /// Create an Operation result with the given list of `conditions` using `operator`.
    fn new_operation(conditions: Box<[Self]>, operator: Operator) -> Self;
}

/// Parse a nested block using the given parse function.
pub fn try_parse_block<'i, T, F>(
    context: &ParserContext,
    input: &mut Parser<'i>,
    start: SourcePosition,
    start_location: SourceLocation,
    parse: F,
) -> Option<T>
where
    F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError>,
{
    input
        .try_parse(|input| {
            let result = input.parse_nested_block(parse);
            if let Err(ref e) = result
                && context.error_reporting_enabled()
            {
                // We're about to swallow the error in a `<general-enclosed>` condition, so report
                // it while we can.
                let error =
                    ContextualParseError::InvalidMediaRule(input.slice_from(start), e.clone());
                context.log_css_error(start_location, error);
            }
            result
        })
        .ok()
}

/// Represents a condition.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum QueryCondition {
    /// A simple feature expression, implicitly parenthesized.
    Feature(QueryFeatureExpression),
    /// A custom media query reference in a boolean context, implicitly parenthesized.
    Custom(DashedIdent),
    /// A negation of a condition.
    Not(Box<QueryCondition>),
    /// A set of joint operations.
    Operation(Box<[QueryCondition]>, Operator),
    /// A condition wrapped in parenthesis.
    InParens(Box<QueryCondition>),
    /// A <style> query.
    Style(StyleQuery),
    /// A -moz-pref() query.
    MozPref(MozPrefFeature),
    /// [ <function-token> <any-value>? ) ] | [ ( <any-value>? ) ]
    GeneralEnclosed(String, UrlExtraData, FeatureFlags),
}

impl ToCss for QueryCondition {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        match *self {
            // NOTE(emilio): QueryFeatureExpression already includes the
            // parenthesis.
            Self::Feature(ref f) => f.to_css(dest),
            Self::Custom(ref name) => {
                dest.write_char('(')?;
                name.to_css(dest)?;
                dest.write_char(')')
            },
            Self::Not(ref c) => {
                dest.write_str("not ")?;
                c.to_css(dest)
            },
            Self::InParens(ref c) => {
                dest.write_char('(')?;
                c.to_css(dest)?;
                dest.write_char(')')
            },
            Self::Style(ref c) => {
                dest.write_str("style(")?;
                c.to_css(dest)?;
                dest.write_char(')')
            },
            Self::MozPref(ref c) => {
                dest.write_str("-moz-pref(")?;
                c.to_css(dest)?;
                dest.write_char(')')
            },
            Self::Operation(ref list, op) => {
                let mut iter = list.iter();
                iter.next().unwrap().to_css(dest)?;
                for item in iter {
                    dest.write_char(' ')?;
                    op.to_css(dest)?;
                    dest.write_char(' ')?;
                    item.to_css(dest)?;
                }
                Ok(())
            },
            Self::GeneralEnclosed(ref s, ..) => dest.write_str(s),
        }
    }
}

/// <https://drafts.csswg.org/css-syntax-3/#typedef-any-value>
pub fn consume_any_value(input: &mut Parser) -> Result<(), ParseError> {
    input.expect_no_error_token().map_err(Into::into)
}

impl QueryCondition {
    /// Parse a single condition.
    pub fn parse(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError> {
        Self::parse_internal(context, input, feature_type, AllowOr::Yes)
    }

    fn visit<F>(&self, visitor: &mut F)
    where
        F: FnMut(&Self),
    {
        visitor(self);
        match *self {
            Self::Custom(..)
            | Self::Feature(..)
            | Self::GeneralEnclosed(..)
            | Self::Style(..)
            | Self::MozPref(..) => {},
            Self::Not(ref cond) => cond.visit(visitor),
            Self::Operation(ref conds, _op) => {
                for cond in conds.iter() {
                    cond.visit(visitor);
                }
            },
            Self::InParens(ref cond) => cond.visit(visitor),
        }
    }

    /// Returns the union of all flags in the expression. This is useful for
    /// container queries.
    pub fn cumulative_flags(&self) -> FeatureFlags {
        let mut result = FeatureFlags::empty();
        self.visit(&mut |condition| match condition {
            Self::Style(..) => result.insert(FeatureFlags::STYLE),
            Self::Feature(f) => result.insert(f.feature_flags()),
            Self::GeneralEnclosed(_, _, flags) => result.insert(*flags),
            _ => {},
        });
        result
    }

    /// Parse a single condition, disallowing `or` expressions.
    ///
    /// To be used from the legacy query syntax.
    pub fn parse_disallow_or(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError> {
        Self::parse_internal(context, input, feature_type, AllowOr::No)
    }

    fn parse_in_parenthesis_block(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError> {
        // Base case. Make sure to preserve this error as it's more generally
        // relevant.
        let feature_error = match input.try_parse(|input| {
            QueryFeatureExpression::parse_in_parenthesis_block(context, input, feature_type)
        }) {
            Ok(expr) => return Ok(Self::Feature(expr)),
            Err(e) => e,
        };
        if crate::pref!("layout.css.custom-media.enabled")
            && let Ok(custom) = input.try_parse(|input| DashedIdent::parse(context, input))
        {
            return Ok(Self::Custom(custom));
        }
        if let Ok(inner) = Self::parse(context, input, feature_type) {
            return Ok(Self::InParens(Box::new(inner)));
        }
        Err(feature_error)
    }

    /// Whether this condition matches the device and quirks mode.
    /// https://drafts.csswg.org/mediaqueries/#evaluating
    /// https://drafts.csswg.org/mediaqueries/#typedef-general-enclosed
    /// Kleene 3-valued logic is adopted here due to the introduction of
    /// <general-enclosed>.
    pub fn matches(
        &self,
        context: &computed::Context,
        custom: &mut CustomMediaEvaluator,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        match *self {
            Self::Custom(ref f) => custom.matches(f, context),
            Self::Feature(ref f) => f.matches(context),
            Self::GeneralEnclosed(ref str, ref url_data, _) => {
                self.matches_general(str, url_data, context, custom, attribute_tracker)
            },
            Self::InParens(ref c) => c.matches(context, custom, attribute_tracker),
            Self::Not(ref c) => !c.matches(context, custom, attribute_tracker),
            Self::Style(ref c) => c.matches(context, attribute_tracker),
            Self::MozPref(ref c) => c.matches(context),
            Self::Operation(ref conditions, op) => {
                debug_assert!(!conditions.is_empty(), "We never create an empty op");
                match op {
                    Operator::And => KleeneValue::any_false(conditions.iter(), |c| {
                        c.matches(context, custom, attribute_tracker)
                    }),
                    Operator::Or => KleeneValue::any(conditions.iter(), |c| {
                        c.matches(context, custom, attribute_tracker)
                    }),
                }
            },
        }
    }

    /// For a condition that was parsed as GeneralEnclosed, try applying custom-property
    /// substitution and re-parse the result.
    fn matches_general(
        &self,
        css_text: &str,
        url_data: &UrlExtraData,
        context: &computed::Context,
        custom: &mut CustomMediaEvaluator,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        // This only applies (currently, at least) to container queries.
        if !context.in_container_query {
            return KleeneValue::Unknown;
        }

        let stylist = context
            .builder
            .stylist
            .expect("container query should provide a Stylist");

        // TODO(Bug 2087381) - Need to propagate the stylesheet's namespaces through here
        // in order to support namespaced `attr()` functions in container query conditions.
        let mut parser_context = ParserContext::new(
            Origin::Author,
            url_data,
            Some(CssRuleType::Container),
            ParsingMode::DEFAULT,
            QuirksMode::NoQuirks,
            /* namespaces = */ Default::default(),
            /* error_reporter = */ None,
            /* use_counters = */ None,
            AttrTaint::default(),
        );

        // Parse the text as a custom-property value to identify references.
        let value = match custom_properties::SpecifiedValue::parse(
            &parser_context,
            &mut Parser::new(css_text),
        ) {
            Ok(val) => val,
            Err(_) => return KleeneValue::Unknown,
        };

        // If no references, we're not going to end up with a new result, just bail out.
        if !value.has_references() {
            return KleeneValue::Unknown;
        }

        // Substitute var() functions if possible.
        let substitution_functions = custom_properties::ComputedSubstitutionFunctions::new(
            Some(context.inherited_custom_properties().clone()),
            None,
        );
        let substitution::SubstitutionResult { css, attr_taint } = match substitution::substitute(
            &value,
            /* property_id */ None,
            &substitution_functions,
            stylist,
            context,
            attribute_tracker,
        ) {
            Ok(sub) => sub,
            Err(_) => return KleeneValue::Unknown,
        };
        parser_context.attr_tainted_regions = Cow::Owned(attr_taint);

        // Re-parse the result as a query-condition, and evaluate it.
        match Self::parse(
            &parser_context,
            &mut Parser::new(&css),
            FeatureType::Container,
        ) {
            Ok(Self::GeneralEnclosed(..)) => {
                // If the result is still GeneralEnclosed, the query is unknown.
                KleeneValue::Unknown
            },
            Ok(query) => query.matches(context, custom, attribute_tracker),
            Err(_) => KleeneValue::Unknown,
        }
    }

    /// Collect the attribute references in this query condition, if any.
    pub fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        if let QueryCondition::Style(c) = self {
            c.collect_attribute_references(references)
        }
    }
}

impl OperationParser for QueryCondition {
    /// Parse a condition in parentheses, or `<general-enclosed>`.
    ///
    /// https://drafts.csswg.org/mediaqueries/#typedef-media-in-parens
    fn parse_in_parens(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError> {
        input.skip_whitespace();
        let start = input.position();
        let start_location = input.current_source_location();
        let mut flags = FeatureFlags::empty();
        match *input.next()? {
            Token::ParenthesisBlock => {
                let nested = try_parse_block(context, input, start, start_location, |input| {
                    Self::parse_in_parenthesis_block(context, input, feature_type)
                });
                if let Some(nested) = nested {
                    return Ok(nested);
                }
            },
            Token::Function(ref name) => {
                match_ignore_ascii_case! { name,
                    "style" if StyleQuery::enabled(feature_type) => {
                        let query = try_parse_block(context, input, start, start_location, |input| {
                            StyleQuery::parse(context, input, feature_type)
                        });
                        if let Some(query) = query {
                            return Ok(Self::Style(query));
                        }
                        flags.insert(FeatureFlags::STYLE);
                    },
                    "-moz-pref" => {
                        let feature = try_parse_block(context, input, start, start_location, |input| {
                            MozPrefFeature::parse(context, input, feature_type)
                        });
                        if let Some(feature) = feature {
                            return Ok(Self::MozPref(feature));
                        }
                    },
                    _ => {},
                }
            },
            _ => return Err(ParseError::unexpected_token()),
        }
        input.parse_nested_block(consume_any_value)?;
        Ok(Self::GeneralEnclosed(
            input.slice_from(start).to_owned(),
            context.url_data.clone(),
            flags,
        ))
    }

    fn new_not(inner: Box<Self>) -> Self {
        Self::Not(inner)
    }

    fn new_operation(conditions: Box<[Self]>, operator: Operator) -> Self {
        Self::Operation(conditions, operator)
    }
}
