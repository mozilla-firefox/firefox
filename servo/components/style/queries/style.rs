/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Style queries.
//! https://drafts.csswg.org/css-conditional-5/#typedef-style-query

use super::feature::FeatureFlagsInfo;
use super::{FeatureFlags, QueryStyleRange};
use crate::computed_value_flags::ComputedValueFlags;
use crate::custom_properties;
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::properties::CSSWideKeyword;
use crate::properties_and_values::rule::Descriptors as PropertyDescriptors;
use crate::properties_and_values::value::{
    AllowComputationallyDependent, ComputedValue as ComputedRegisteredValue,
    SpecifiedValue as SpecifiedRegisteredValue,
};
use crate::queries::boolean_expression::{BooleanExpr, BooleanExprTest};
use crate::stylesheets::CustomMediaEvaluator;
use crate::stylesheets::container_rule::AttrReferenceSet;
use crate::stylist::Stylist;
use crate::substitution;
use crate::values::computed;
use crate::{parser::Parse, parser::ParserContext};
use cssparser::{Parser, parse_important};
use selectors::kleene_value::KleeneValue;
use servo_arc::Arc;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

#[derive(Clone, Debug, PartialEq, ToShmem)]
enum StyleFeatureValue {
    Value(Option<Arc<custom_properties::SpecifiedValue>>),
    Keyword(CSSWideKeyword),
}

/// A `<style-query>`, which is a combination of style query tests.
/// https://drafts.csswg.org/css-conditional-5/#typedef-style-query
pub type StyleQuery = BooleanExpr<StyleQueryTest>;

impl StyleQuery {
    /// Determines whether the style queries are enabled.
    pub fn enabled() -> bool {
        crate::pref!("layout.css.style-queries.enabled")
    }

    /// Parses a `<style-query>`, with the `style()` function already consumed. Note
    /// that at the top-level, an unparenthesized `<style-feature>` is valid.
    pub fn parse_top_level(
        context: &ParserContext,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        if let Ok(feature) = input.try_parse(|input| StyleFeature::parse(context, input)) {
            return Ok(Self::Test(StyleQueryTest {
                feature,
                parenthesized: false,
            }));
        }
        Self::parse(context, input)
    }

    /// Evaluate whether the style query matches.
    pub fn matches(
        &self,
        ctx: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        ctx.builder
            .add_flags(ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY);
        self.evaluate(ctx, &mut CustomMediaEvaluator::none(), attribute_tracker)
    }

    /// Recurses into the query to collect all referenced attributes.
    pub fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        self.for_each_test(&mut |test| test.feature.collect_attribute_references(references));
    }
}

/// A `<style-feature>` test in a `<style-query>`. Excludes both the nested `(<style-query>)`
///  and `<general-enclosed>`, which are handled by the outer `BooleanExpr<StyleQueryTest>`.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct StyleQueryTest {
    /// The feature query.
    feature: StyleFeature,
    /// Whether the feature was written within parentheses.
    parenthesized: bool,
}

impl ToCss for StyleQueryTest {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        if !self.parenthesized {
            return self.feature.to_css(dest);
        }
        dest.write_char('(')?;
        self.feature.to_css(dest)?;
        dest.write_char(')')
    }
}

impl Parse for StyleQueryTest {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        input.expect_parenthesis_block()?;
        let feature = input.parse_nested_block(|input| StyleFeature::parse(context, input))?;
        Ok(Self {
            feature,
            parenthesized: true,
        })
    }
}

impl BooleanExprTest for StyleQueryTest {
    fn evaluate(
        &self,
        context: &computed::Context,
        _: &mut CustomMediaEvaluator,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        self.feature.matches(context, attribute_tracker)
    }
}

impl FeatureFlagsInfo for StyleQueryTest {
    fn feature_flags(&self) -> FeatureFlags {
        FeatureFlags::STYLE
    }
}

/// A style query feature:
/// https://drafts.csswg.org/css-conditional-5/#typedef-style-feature
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToCss, ToShmem)]
pub enum StyleFeature {
    /// A property name and optional value to match.
    Plain(StyleFeaturePlain),
    /// A style query range expression.
    Range(QueryStyleRange),
}

impl Parse for StyleFeature {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if let Ok(range) = input.try_parse(|i| QueryStyleRange::parse(context, i)) {
            return Ok(Self::Range(range));
        }

        Ok(Self::Plain(StyleFeaturePlain::parse(context, input)?))
    }
}

impl StyleFeature {
    fn matches(
        &self,
        ctx: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        match self {
            Self::Plain(plain) => plain.matches(ctx, attribute_tracker),
            Self::Range(range) => range.evaluate(ctx, attribute_tracker),
        }
    }

    fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        match self {
            Self::Plain(plain) => plain.collect_attribute_references(references),
            Self::Range(range) => range.collect_attribute_references(references),
        }
    }
}

/// A style feature consisting of a custom property name and (optionally) value.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct StyleFeaturePlain {
    name: custom_properties::Name,
    #[ignore_malloc_size_of = "StyleFeatureValue has an Arc variant"]
    value: StyleFeatureValue,
}

impl ToCss for StyleFeaturePlain {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        dest.write_str("--")?;
        crate::values::serialize_atom_identifier(&self.name, dest)?;
        match self.value {
            StyleFeatureValue::Keyword(k) => {
                dest.write_str(": ")?;
                k.to_css(dest)?;
            },
            StyleFeatureValue::Value(Some(ref v)) => {
                dest.write_str(": ")?;
                v.to_css(dest)?;
            },
            StyleFeatureValue::Value(None) => (),
        }
        Ok(())
    }
}

impl StyleFeaturePlain {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let ident = input.expect_ident()?;
        // TODO(emilio): Maybe support non-custom properties?
        let name = match custom_properties::parse_name(ident.as_ref()) {
            Ok(name) => custom_properties::Name::from(name),
            Err(()) => return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError)),
        };
        let value = if input.try_parse(|i| i.expect_colon()).is_ok() {
            input.skip_whitespace();
            if let Ok(keyword) = input.try_parse(|i| CSSWideKeyword::parse(i)) {
                StyleFeatureValue::Keyword(keyword)
            } else {
                let value = custom_properties::SpecifiedValue::parse(context, input)?;
                // `!important` is allowed (but ignored) after the value.
                let _ = input.try_parse(parse_important);
                StyleFeatureValue::Value(Some(Arc::new(value)))
            }
        } else {
            StyleFeatureValue::Value(None)
        };
        Ok(Self { name, value })
    }

    // Substitute custom-property references in `value`, then re-parse and compute it,
    // and compare against `current_value`.
    fn substitute_and_compare(
        value: &Arc<custom_properties::SpecifiedValue>,
        registration: &PropertyDescriptors,
        stylist: &Stylist,
        ctx: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
        current_value: Option<&ComputedRegisteredValue>,
    ) -> bool {
        let substitution_functions = custom_properties::ComputedSubstitutionFunctions::new(
            Some(ctx.inherited_custom_properties().clone()),
            None,
        );
        let substitution::SubstitutionResult { css, attr_taint } = match substitution::substitute(
            value,
            /* property_id */ None,
            &substitution_functions,
            stylist,
            ctx,
            attribute_tracker,
        ) {
            Ok(sub) => sub,
            Err(_) => return current_value.is_none(),
        };
        if registration.is_universal() {
            return match current_value {
                Some(v) => v.as_universal().is_some_and(|v| v.css == css),
                None => css.is_empty(),
            };
        }
        let mut parser = Parser::new(&css);
        let computed = SpecifiedRegisteredValue::compute(
            &mut parser,
            registration,
            &value.url_data,
            ctx,
            AllowComputationallyDependent::Yes,
            attr_taint,
            /* property_id */ None,
        )
        .ok();
        computed.as_ref() == current_value
    }

    fn matches(
        &self,
        ctx: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        // FIXME(emilio): Confirm this is the right style to query.
        let stylist = ctx
            .builder
            .stylist
            .expect("container queries should have a stylist around");
        let registration = stylist.get_custom_property_registration(&self.name);
        let current_value = ctx
            .inherited_custom_properties()
            .get(registration, &self.name);
        KleeneValue::from(match self.value {
            StyleFeatureValue::Value(Some(ref v)) => {
                if ctx.container_info.is_none() {
                    // If no container, custom props are guaranteed-unknown.
                    false
                } else if v.has_references() {
                    // If there are --var() references in the query value,
                    // try to substitute them before comparing to current.
                    Self::substitute_and_compare(
                        v,
                        registration,
                        stylist,
                        ctx,
                        attribute_tracker,
                        current_value,
                    )
                } else {
                    custom_properties::compute_variable_value(
                        v,
                        registration,
                        ctx,
                        /* property_id */ None,
                    )
                    .as_ref()
                        == current_value
                }
            },
            StyleFeatureValue::Value(None) => current_value.is_some(),
            StyleFeatureValue::Keyword(kw) => {
                match kw {
                    CSSWideKeyword::Unset => current_value.is_none(),
                    CSSWideKeyword::Initial => {
                        if let Some(initial) = &registration.initial_value {
                            let v = custom_properties::compute_variable_value(
                                initial,
                                registration,
                                ctx,
                                /* property_id */ None,
                            );
                            v.as_ref() == current_value
                        } else {
                            current_value.is_none()
                        }
                    },
                    CSSWideKeyword::Inherit => {
                        if let Some(inherited) = ctx
                            .container_info
                            .as_ref()
                            .expect("queries should provide container info")
                            .inherited_style()
                        {
                            inherited.custom_properties().get(registration, &self.name)
                                == current_value
                        } else {
                            false
                        }
                    },
                    // Cascade-dependent keywords, such as revert and revert-layer,
                    // are invalid as values in a style feature, and cause the
                    // container style query to be false.
                    // https://drafts.csswg.org/css-conditional-5/#evaluate-a-style-range
                    CSSWideKeyword::Revert
                    | CSSWideKeyword::RevertLayer
                    | CSSWideKeyword::RevertRule => false,
                }
            },
        })
    }

    fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        if let StyleFeatureValue::Value(Some(v)) = &self.value {
            v.collect_attribute_references(references)
        }
    }
}
