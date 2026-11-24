/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The <boolean-expr[T]> type that combines boolean tests.
//! https://drafts.csswg.org/css-values-5/#typedef-boolean-expr

use crate::context::QuirksMode;
use crate::custom_properties::{self, VariableValue};
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::error_reporting::ContextualParseError;
use crate::parser::{Parse, ParserContext};
use crate::stylesheets::{CustomMediaEvaluator, Origin};
use crate::substitution::{self, AttrTaint};
use crate::values::computed;
use cssparser::{Parser, Token};
use selectors::kleene_value::KleeneValue;
use std::borrow::Cow;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, ParsingMode, ToCss};

/// A binary `and` or `or` operator.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, Parse, PartialEq, ToCss, ToShmem)]
#[allow(missing_docs)]
pub enum Operator {
    And,
    Or,
}

/// The `<boolean-expr[T]>` notation that represents a combination of boolean tests.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum BooleanExpr<T> {
    /// A boolean test.
    Test(T),
    /// A negated expression.
    Not(Box<Self>),
    /// A collection of expressions, joined by the given operator.
    Operation(Box<[Self]>, Operator),
    /// A parenthesized expression.
    InParens(Box<Self>),
    /// An unknown term.
    GeneralEnclosed(String),
    /// A `<general-enclosed>` term containing arbitrary substitution
    /// functions, re-parsed after substitution at evaluation time.
    Unparsed(Box<VariableValue>),
}

/// A test that returns a 3-state value which can participate in a `<boolean-expr>`.
pub trait BooleanExprTest: Parse {
    /// Evaluate the test.
    fn evaluate(
        &self,
        context: &computed::Context,
        custom: &mut CustomMediaEvaluator,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue;

    /// Optionally creates a contextual parsing error when the given `css` failed to parse
    /// as `Self` due to `error`. This new error is reported (if reporting is enabled) just
    /// before this expression is converted into a `<general-enclosed>` term.
    #[allow(unused_variables)]
    fn new_reporting_error<'a>(
        css: &'a str,
        error: ParseError,
    ) -> Option<ContextualParseError<'a>> {
        None
    }
}

impl<T> BooleanExpr<T> {
    /// Calls `f` for each test in the expression.
    pub fn for_each_test<'a>(&'a self, f: &mut impl FnMut(&'a T)) {
        match self {
            Self::Test(test) => f(test),
            Self::GeneralEnclosed(..) | Self::Unparsed(..) => {},
            Self::Not(expr) | Self::InParens(expr) => expr.for_each_test(f),
            Self::Operation(exprs, _) => exprs.iter().for_each(|expr| expr.for_each_test(f)),
        }
    }
}

impl<T: BooleanExprTest> BooleanExpr<T> {
    /// <boolean-expr-group> = <T> | ( <boolean-expr[ <T> ]> ) | <general-enclosed>
    fn parse_in_parens(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        input.skip_whitespace();
        let start = input.position();
        let start_location = input.current_source_location();
        let test_error = match input.try_parse(|input| T::parse(context, input)) {
            Ok(test) => return Ok(Self::Test(test)),
            Err(e) => e,
        };
        match *input.next()? {
            Token::ParenthesisBlock => {
                let nested = input.try_parse(|input| {
                    input.parse_nested_block(|input| Self::parse(context, input))
                });
                if let Ok(nested) = nested {
                    return Ok(Self::InParens(Box::new(nested)));
                }
            },
            Token::Function(_) => {},
            // A `<general-enclosed>` must either be parenthesized or be a function; all other
            // tokens are invalid in the expression and must be rejected.
            // https://drafts.csswg.org/mediaqueries-5/#typedef-general-enclosed
            _ => return Err(test_error),
        }
        input.parse_nested_block(|input| input.expect_no_error_token().map_err(Into::into))?;
        let css = input.slice_from(start);
        if let Ok(value) = VariableValue::parse(context, &mut Parser::new(css)) {
            if value.has_references() {
                return Ok(Self::Unparsed(Box::new(value)));
            }
        }
        if context.error_reporting_enabled()
            && let Some(error) = T::new_reporting_error(css, test_error)
        {
            // We're about to swallow the error in a `<general-enclosed>` term, so report it
            // while we can.
            context.log_css_error(start_location, error);
        }
        Ok(Self::GeneralEnclosed(css.to_owned()))
    }

    /// Evaluates an unparsed expression term by substituting its arbitrary
    /// substitution functions and evaluating the re-parsed result.
    fn evaluate_unparsed(
        value: &VariableValue,
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
            &value.url_data,
            /* rule_type = */ None,
            ParsingMode::DEFAULT,
            QuirksMode::NoQuirks,
            /* namespaces = */ Default::default(),
            /* error_reporter = */ None,
            /* use_counters = */ None,
            AttrTaint::default(),
        );

        // Substitute var() functions if possible.
        let substitution_functions = custom_properties::ComputedSubstitutionFunctions::new(
            Some(context.inherited_custom_properties().clone()),
            None,
        );
        let substitution::SubstitutionResult { css, attr_taint } = match substitution::substitute(
            value,
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

        // Re-parse the result, and evaluate it.
        match Self::parse(&parser_context, &mut Parser::new(&css)) {
            Ok(Self::GeneralEnclosed(..)) => {
                // If the result is still GeneralEnclosed, the term is unknown.
                KleeneValue::Unknown
            },
            Ok(expr) => expr.evaluate(context, custom, attribute_tracker),
            Err(_) => KleeneValue::Unknown,
        }
    }
}

impl<T: BooleanExprTest> BooleanExprTest for BooleanExpr<T> {
    fn evaluate(
        &self,
        context: &computed::Context,
        custom: &mut CustomMediaEvaluator,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        match self {
            Self::Test(test) => test.evaluate(context, custom, attribute_tracker),
            Self::Not(expr) => !expr.evaluate(context, custom, attribute_tracker),
            Self::Operation(exprs, operator) => {
                debug_assert!(!exprs.is_empty(), "We never create an empty op");
                match operator {
                    Operator::And => KleeneValue::any_false(exprs.iter(), |expr| {
                        expr.evaluate(context, custom, attribute_tracker)
                    }),
                    Operator::Or => KleeneValue::any(exprs.iter(), |expr| {
                        expr.evaluate(context, custom, attribute_tracker)
                    }),
                }
            },
            Self::InParens(expr) => expr.evaluate(context, custom, attribute_tracker),
            Self::GeneralEnclosed(..) => KleeneValue::Unknown,
            Self::Unparsed(value) => {
                Self::evaluate_unparsed(value, context, custom, attribute_tracker)
            },
        }
    }
}

impl<T: BooleanExprTest> Parse for BooleanExpr<T> {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        if input.try_parse(|i| i.expect_ident_matching("not")).is_ok() {
            let inner_condition = Self::parse_in_parens(context, input)?;
            return Ok(Self::Not(Box::new(inner_condition)));
        }

        let first_expr = Self::parse_in_parens(context, input)?;
        let operator = match input.try_parse(Operator::parse) {
            Ok(op) => op,
            Err(..) => return Ok(first_expr),
        };

        let mut exprs = vec![];
        exprs.push(first_expr);
        exprs.push(Self::parse_in_parens(context, input)?);

        let delim = match operator {
            Operator::And => "and",
            Operator::Or => "or",
        };

        loop {
            if input.try_parse(|i| i.expect_ident_matching(delim)).is_err() {
                return Ok(Self::Operation(exprs.into_boxed_slice(), operator));
            }
            exprs.push(Self::parse_in_parens(context, input)?);
        }
    }
}

impl<T: ToCss> ToCss for BooleanExpr<T> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match self {
            Self::Test(test) => test.to_css(dest),
            Self::Not(expr) => {
                dest.write_str("not ")?;
                expr.to_css(dest)
            },
            Self::Operation(exprs, operator) => {
                let mut iter = exprs.iter();
                iter.next().unwrap().to_css(dest)?;
                for expr in iter {
                    dest.write_char(' ')?;
                    operator.to_css(dest)?;
                    dest.write_char(' ')?;
                    expr.to_css(dest)?;
                }
                Ok(())
            },
            Self::InParens(expr) => {
                dest.write_char('(')?;
                expr.to_css(dest)?;
                dest.write_char(')')
            },
            Self::GeneralEnclosed(css) => dest.write_str(css),
            Self::Unparsed(value) => value.to_css(dest),
        }
    }
}
