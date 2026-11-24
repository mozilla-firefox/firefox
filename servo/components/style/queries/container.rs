/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Container queries.
//! https://drafts.csswg.org/css-conditional-5/#typedef-container-query

use super::feature::FeatureFlagsInfo;
use super::style::StyleQuery;
use super::{FeatureFlags, FeatureType, QueryFeatureExpression};
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::queries::boolean_expression::{BooleanExpr, BooleanExprTest};
use crate::stylesheets::CustomMediaEvaluator;
use crate::stylesheets::container_rule::AttrReferenceSet;
use crate::values::computed;
use crate::{parser::Parse, parser::ParserContext};
use cssparser::{Parser, Token};
use selectors::kleene_value::KleeneValue;
use style_traits::ParseError;

/// A `<container-query>`, which is a combination of container query tests.
/// https://drafts.csswg.org/css-conditional-5/#typedef-container-query
pub type ContainerQuery = BooleanExpr<ContainerQueryTest>;

impl ContainerQuery {
    /// Collect the attribute references in this query condition, if any.
    pub fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        if let Self::Test(ContainerQueryTest::Style(style)) = self {
            style.collect_attribute_references(references)
        }
    }
}

/// A test in a `<container-query>`. This is generally like `<query-in-parens>`, but
/// it excludes the nested `(<container-query>)` type and the `<general-enclosed>`
/// type which are handled as part of `BooleanExpr<ContainerQueryTest>`.
/// https://drafts.csswg.org/css-conditional-5/#typedef-query-in-parens
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem)]
pub enum ContainerQueryTest {
    /// A simple feature expression, implicitly parenthesized.
    Feature(QueryFeatureExpression),
    /// A style() query.
    #[css(function = "style")]
    Style(StyleQuery),
}

impl Parse for ContainerQueryTest {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        match *input.next()? {
            Token::ParenthesisBlock => Ok(Self::Feature(input.parse_nested_block(|input| {
                QueryFeatureExpression::parse_in_parenthesis_block(
                    context,
                    input,
                    FeatureType::Container,
                )
            })?)),
            Token::Function(ref name)
                if name.eq_ignore_ascii_case("style") && StyleQuery::enabled() =>
            {
                Ok(Self::Style(input.parse_nested_block(|input| {
                    StyleQuery::parse_top_level(context, input)
                })?))
            },
            _ => Err(ParseError::unexpected_token()),
        }
    }
}

impl BooleanExprTest for ContainerQueryTest {
    fn evaluate(
        &self,
        context: &computed::Context,
        _: &mut CustomMediaEvaluator,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        match self {
            Self::Feature(feature) => feature.matches(context),
            Self::Style(style) => style.matches(context, attribute_tracker),
        }
    }
}

impl FeatureFlagsInfo for ContainerQueryTest {
    fn feature_flags(&self) -> FeatureFlags {
        match self {
            Self::Feature(feature) => feature.feature_flags(),
            Self::Style(style) => FeatureFlags::STYLE | style.feature_flags(),
        }
    }
}
