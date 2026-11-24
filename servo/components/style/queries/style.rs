/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Style queries.
//! https://drafts.csswg.org/css-conditional-5/#typedef-style-query

use super::condition::{AllowOr, OperationParser, Operator, consume_any_value, try_parse_block};
use super::{FeatureType, QueryStyleRange};
use crate::computed_value_flags::ComputedValueFlags;
use crate::custom_properties;
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::parser::ParserContext;
use crate::properties::CSSWideKeyword;
use crate::properties_and_values::rule::Descriptors as PropertyDescriptors;
use crate::properties_and_values::value::{
    AllowComputationallyDependent, ComputedValue as ComputedRegisteredValue,
    SpecifiedValue as SpecifiedRegisteredValue,
};
use crate::stylesheets::container_rule::AttrReferenceSet;
use crate::stylist::Stylist;
use crate::substitution;
use crate::values::computed;
use cssparser::{Parser, Token, parse_important};
use selectors::kleene_value::KleeneValue;
use servo_arc::Arc;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

#[derive(Clone, Debug, PartialEq, ToShmem)]
enum StyleFeatureValue {
    Value(Option<Arc<custom_properties::SpecifiedValue>>),
    Keyword(CSSWideKeyword),
}

/// https://drafts.csswg.org/css-conditional-5/#typedef-style-query
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum StyleQuery {
    /// A negation of a condition.
    Not(Box<StyleQuery>),
    /// A set of joint operations.
    Operation(Box<[StyleQuery]>, Operator),
    /// A condition wrapped in parenthesis.
    InParens(Box<StyleQuery>),
    /// A feature query (`--foo: bar` or just `--foo`).
    Feature(StyleFeature),
    /// An unknown "general-enclosed" term.
    GeneralEnclosed(String),
}

impl ToCss for StyleQuery {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        match *self {
            StyleQuery::Not(ref c) => {
                dest.write_str("not ")?;
                c.maybe_parenthesized(dest)
            },
            StyleQuery::Operation(ref list, op) => {
                let mut iter = list.iter();
                let item = iter.next().unwrap();
                item.maybe_parenthesized(dest)?;
                for item in iter {
                    dest.write_char(' ')?;
                    op.to_css(dest)?;
                    dest.write_char(' ')?;
                    item.maybe_parenthesized(dest)?;
                }
                Ok(())
            },
            StyleQuery::InParens(ref c) => match &**c {
                StyleQuery::Feature(_) | StyleQuery::InParens(_) => {
                    dest.write_char('(')?;
                    c.to_css(dest)?;
                    dest.write_char(')')
                },
                _ => c.to_css(dest),
            },
            StyleQuery::Feature(ref f) => f.to_css(dest),
            StyleQuery::GeneralEnclosed(ref s) => dest.write_str(s),
        }
    }
}

impl StyleQuery {
    // Helper for to_css when handling values within boolean operators:
    // GeneralEnclosed includes its parens in the string, so we don't need to
    // wrap the value with an additional set here.
    fn maybe_parenthesized<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        if let StyleQuery::GeneralEnclosed(s) = self {
            dest.write_str(s)
        } else {
            dest.write_char('(')?;
            self.to_css(dest)?;
            dest.write_char(')')
        }
    }

    /// Determines whether the style queries are enabled.
    pub fn enabled(feature_type: FeatureType) -> bool {
        crate::pref!("layout.css.style-queries.enabled") && feature_type == FeatureType::Container
    }

    /// Parses a style query.
    pub fn parse(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError> {
        if let Ok(feature) = input.try_parse(|input| StyleFeature::parse(context, input)) {
            return Ok(Self::Feature(feature));
        }

        let inner = Self::parse_internal(context, input, feature_type, AllowOr::Yes)?;
        Ok(Self::InParens(Box::new(inner)))
    }

    fn parse_in_parenthesis_block(
        context: &ParserContext,
        input: &mut Parser,
    ) -> Result<Self, ParseError> {
        // Base case. Make sure to preserve this error as it's more generally
        // relevant.
        let feature_error = match input.try_parse(|input| StyleFeature::parse(context, input)) {
            Ok(feature) => return Ok(Self::Feature(feature)),
            Err(e) => e,
        };

        if let Ok(inner) = Self::parse(context, input, FeatureType::Container) {
            return Ok(inner);
        }

        Err(feature_error)
    }

    /// Evaluate whether the style query matches.
    pub fn matches(
        &self,
        ctx: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        ctx.builder
            .add_flags(ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY);
        match *self {
            StyleQuery::Feature(ref f) => f.matches(ctx, attribute_tracker),
            StyleQuery::Not(ref c) => !c.matches(ctx, attribute_tracker),
            StyleQuery::InParens(ref c) => c.matches(ctx, attribute_tracker),
            StyleQuery::Operation(ref conditions, op) => {
                debug_assert!(!conditions.is_empty(), "We never create an empty op");
                match op {
                    Operator::And => KleeneValue::any_false(conditions.iter(), |c| {
                        c.matches(ctx, attribute_tracker)
                    }),
                    Operator::Or => {
                        KleeneValue::any(conditions.iter(), |c| c.matches(ctx, attribute_tracker))
                    },
                }
            },
            StyleQuery::GeneralEnclosed(_) => KleeneValue::Unknown,
        }
    }

    /// Recurses into the query to collect all referenced attributes.
    pub fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        match self {
            Self::Feature(c) => c.collect_attribute_references(references),
            Self::GeneralEnclosed(_) => {},
            Self::InParens(c) => c.collect_attribute_references(references),
            Self::Not(c) => c.collect_attribute_references(references),
            Self::Operation(c, _) => c
                .iter()
                .for_each(|c| c.collect_attribute_references(references)),
        }
    }
}

impl OperationParser for StyleQuery {
    fn parse_in_parens(
        context: &ParserContext,
        input: &mut Parser,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError> {
        assert!(feature_type == FeatureType::Container);
        input.skip_whitespace();
        let start = input.position();
        let start_location = input.current_source_location();
        match *input.next()? {
            Token::ParenthesisBlock => {
                if let Some(nested) = try_parse_block(context, input, start, start_location, |i| {
                    Self::parse_in_parenthesis_block(context, i)
                }) {
                    return Ok(nested);
                }
                // Accept <ident>: <any-value> as a GeneralEnclosed (which evaluates
                // to false, but does not invalidate the query as a whole).
                input.parse_nested_block(|i| {
                    i.expect_ident()?;
                    i.expect_colon()?;
                    consume_any_value(i)
                })?;
                Ok(Self::GeneralEnclosed(input.slice_from(start).to_owned()))
            },
            _ => Err(ParseError::unexpected_token()),
        }
    }

    fn new_not(inner: Box<Self>) -> Self {
        Self::Not(inner)
    }

    fn new_operation(conditions: Box<[Self]>, operator: Operator) -> Self {
        Self::Operation(conditions, operator)
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

impl StyleFeature {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if let Ok(range) = input.try_parse(|i| QueryStyleRange::parse(context, i)) {
            return Ok(Self::Range(range));
        }

        Ok(Self::Plain(StyleFeaturePlain::parse(context, input)?))
    }

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
