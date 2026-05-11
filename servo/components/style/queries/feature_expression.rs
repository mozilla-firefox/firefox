/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Parsing for query feature expressions, like `(foo: bar)` or
//! `(width >= 400px)`.

use super::feature::{Evaluator, QueryFeatureDescription};
use super::feature::{FeatureFlags, KeywordDiscriminant};
use crate::context::QuirksMode;
use crate::custom_properties::{
    self, ComputedSubstitutionFunctions, VariableValue as CustomVariableValue,
};
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::parser::{Parse, ParserContext};
use crate::properties::{self, CSSWideKeyword};
use crate::properties_and_values::value::{ComputedValueComponent as Component, ValueInner};
use crate::selector_map::PrecomputedHashSet;
use crate::str::{starts_with_ignore_ascii_case, string_as_ascii_lowercase};
use crate::stylesheets::{CssRuleType, Origin, UrlExtraData};
use crate::values::computed::{self, CSSPixelLength, ToComputedValue};
use crate::values::specified::{
    Angle, Integer, Length, Number, Percentage, Ratio, Resolution, Time,
};
use crate::values::DashedIdent;
use crate::{Atom, Zero};
use cssparser::{Parser, ParserInput, Token};
use selectors::kleene_value::KleeneValue;
use std::cmp::Ordering;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, ParsingMode, StyleParseErrorKind, ToCss};

/// Whether we're parsing a media or container query feature.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
pub enum FeatureType {
    /// We're parsing a media feature.
    Media,
    /// We're parsing a container feature.
    Container,
}

impl FeatureType {
    fn features(&self) -> &'static [QueryFeatureDescription] {
        #[cfg(feature = "gecko")]
        use crate::gecko::media_features::MEDIA_FEATURES;
        #[cfg(feature = "servo")]
        use crate::servo::media_features::MEDIA_FEATURES;

        use crate::stylesheets::container_rule::CONTAINER_FEATURES;

        match *self {
            FeatureType::Media => &MEDIA_FEATURES,
            FeatureType::Container => &CONTAINER_FEATURES,
        }
    }

    fn find_feature(&self, name: &Atom) -> Option<(usize, &'static QueryFeatureDescription)> {
        self.features()
            .iter()
            .enumerate()
            .find(|(_, f)| f.name == *name)
    }
}

/// The kind of matching that should be performed on a feature value.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
enum LegacyRange {
    /// At least the specified value.
    Min,
    /// At most the specified value.
    Max,
}

/// The operator that was specified in this feature.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
pub enum Operator {
    /// =
    Equal,
    /// >
    GreaterThan,
    /// >=
    GreaterThanEqual,
    /// <
    LessThan,
    /// <=
    LessThanEqual,
}

impl ToCss for Operator {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        dest.write_str(match *self {
            Self::Equal => "=",
            Self::LessThan => "<",
            Self::LessThanEqual => "<=",
            Self::GreaterThan => ">",
            Self::GreaterThanEqual => ">=",
        })
    }
}

impl Operator {
    fn is_compatible_with(self, right_op: Self) -> bool {
        // Some operators are not compatible with each other in multi-range
        // context.
        match self {
            Self::Equal => false,
            Self::GreaterThan | Self::GreaterThanEqual => {
                matches!(right_op, Self::GreaterThan | Self::GreaterThanEqual)
            },
            Self::LessThan | Self::LessThanEqual => {
                matches!(right_op, Self::LessThan | Self::LessThanEqual)
            },
        }
    }

    fn evaluate(&self, cmp: Ordering) -> bool {
        match *self {
            Self::Equal => cmp == Ordering::Equal,
            Self::GreaterThan => cmp == Ordering::Greater,
            Self::GreaterThanEqual => cmp == Ordering::Equal || cmp == Ordering::Greater,
            Self::LessThan => cmp == Ordering::Less,
            Self::LessThanEqual => cmp == Ordering::Equal || cmp == Ordering::Less,
        }
    }

    fn parse<'i>(input: &mut Parser<'i, '_>) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let operator = match *input.next()? {
            Token::Delim('=') => return Ok(Operator::Equal),
            Token::Delim('>') => Operator::GreaterThan,
            Token::Delim('<') => Operator::LessThan,
            ref t => return Err(location.new_unexpected_token_error(t.clone())),
        };

        // https://drafts.csswg.org/mediaqueries-4/#mq-syntax:
        //
        //     No whitespace is allowed between the “<” or “>”
        //     <delim-token>s and the following “=” <delim-token>, if it’s
        //     present.
        //
        // TODO(emilio): Maybe we should ignore comments as well?
        // https://github.com/w3c/csswg-drafts/issues/6248
        let parsed_equal = input
            .try_parse(|i| {
                let t = i.next_including_whitespace().map_err(|_| ())?;
                if !matches!(t, Token::Delim('=')) {
                    return Err(());
                }
                Ok(())
            })
            .is_ok();

        if !parsed_equal {
            return Ok(operator);
        }

        Ok(match operator {
            Operator::GreaterThan => Operator::GreaterThanEqual,
            Operator::LessThan => Operator::LessThanEqual,
            _ => unreachable!(),
        })
    }
}

#[derive(Clone, Debug, MallocSizeOf, ToShmem, PartialEq)]
enum QueryFeatureExpressionKind {
    /// Just the media feature name.
    Empty,

    /// A single value.
    Single(QueryExpressionValue),

    /// Legacy range syntax (min-*: value) or so.
    LegacyRange(LegacyRange, QueryExpressionValue),

    /// Modern range context syntax:
    /// https://drafts.csswg.org/mediaqueries-5/#mq-range-context
    Range {
        left: Option<(Operator, QueryExpressionValue)>,
        right: Option<(Operator, QueryExpressionValue)>,
    },
}

impl QueryFeatureExpressionKind {
    /// Evaluate a given range given an optional query value and a value from
    /// the browser.
    fn evaluate<T>(
        &self,
        context_value: T,
        mut compute: impl FnMut(&QueryExpressionValue) -> T,
    ) -> bool
    where
        T: PartialOrd + Zero,
    {
        match *self {
            Self::Empty => return !context_value.is_zero(),
            Self::Single(ref value) => {
                let value = compute(value);
                let cmp = match context_value.partial_cmp(&value) {
                    Some(c) => c,
                    None => return false,
                };
                cmp == Ordering::Equal
            },
            Self::LegacyRange(ref range, ref value) => {
                let value = compute(value);
                let cmp = match context_value.partial_cmp(&value) {
                    Some(c) => c,
                    None => return false,
                };
                cmp == Ordering::Equal
                    || match range {
                        LegacyRange::Min => cmp == Ordering::Greater,
                        LegacyRange::Max => cmp == Ordering::Less,
                    }
            },
            Self::Range {
                ref left,
                ref right,
            } => {
                debug_assert!(left.is_some() || right.is_some());
                if let Some((ref op, ref value)) = left {
                    let value = compute(value);
                    let cmp = match value.partial_cmp(&context_value) {
                        Some(c) => c,
                        None => return false,
                    };
                    if !op.evaluate(cmp) {
                        return false;
                    }
                }
                if let Some((ref op, ref value)) = right {
                    let value = compute(value);
                    let cmp = match context_value.partial_cmp(&value) {
                        Some(c) => c,
                        None => return false,
                    };
                    if !op.evaluate(cmp) {
                        return false;
                    }
                }
                true
            },
        }
    }

    /// Non-ranged features only need to compare to one value at most.
    fn non_ranged_value(&self) -> Option<&QueryExpressionValue> {
        match *self {
            Self::Empty => None,
            Self::Single(ref v) => Some(v),
            Self::LegacyRange(..) | Self::Range { .. } => {
                debug_assert!(false, "Unexpected ranged value in non-ranged feature!");
                None
            },
        }
    }
}

/// A feature expression contains a reference to the feature, the value the
/// query contained, and the range to evaluate.
#[derive(Clone, Debug, MallocSizeOf, ToShmem, PartialEq)]
pub struct QueryFeatureExpression {
    feature_type: FeatureType,
    feature_index: usize,
    kind: QueryFeatureExpressionKind,
}

impl ToCss for QueryFeatureExpression {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        dest.write_char('(')?;

        match self.kind {
            QueryFeatureExpressionKind::Empty => self.write_name(dest)?,
            QueryFeatureExpressionKind::Single(ref v)
            | QueryFeatureExpressionKind::LegacyRange(_, ref v) => {
                self.write_name(dest)?;
                dest.write_str(": ")?;
                v.to_css(dest, Some(self))?;
            },
            QueryFeatureExpressionKind::Range {
                ref left,
                ref right,
            } => {
                if let Some((ref op, ref val)) = left {
                    val.to_css(dest, Some(self))?;
                    dest.write_char(' ')?;
                    op.to_css(dest)?;
                    dest.write_char(' ')?;
                }
                self.write_name(dest)?;
                if let Some((ref op, ref val)) = right {
                    dest.write_char(' ')?;
                    op.to_css(dest)?;
                    dest.write_char(' ')?;
                    val.to_css(dest, Some(self))?;
                }
            },
        }
        dest.write_char(')')
    }
}

fn consume_operation_or_colon<'i>(
    input: &mut Parser<'i, '_>,
) -> Result<Option<Operator>, ParseError<'i>> {
    if input.try_parse(|input| input.expect_colon()).is_ok() {
        return Ok(None);
    }
    Operator::parse(input).map(|op| Some(op))
}

#[allow(unused_variables)]
fn disabled_by_pref(feature: &Atom, context: &ParserContext) -> bool {
    #[cfg(feature = "gecko")]
    {
        // prefers-reduced-transparency is always enabled in the ua and chrome. On
        // the web it is hidden behind a preference (see Bug 1822176).
        if *feature == atom!("prefers-reduced-transparency") {
            return !context.chrome_rules_enabled()
                && !static_prefs::pref!("layout.css.prefers-reduced-transparency.enabled");
        }

        // inverted-colors is always enabled in the ua and chrome. On
        // the web it is hidden behind a preference.
        if *feature == atom!("inverted-colors") {
            return !context.chrome_rules_enabled()
                && !static_prefs::pref!("layout.css.inverted-colors.enabled");
        }
    }
    false
}

impl QueryFeatureExpression {
    fn new(
        feature_type: FeatureType,
        feature_index: usize,
        kind: QueryFeatureExpressionKind,
    ) -> Self {
        debug_assert!(feature_index < feature_type.features().len());
        Self {
            feature_type,
            feature_index,
            kind,
        }
    }

    fn write_name<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        let feature = self.feature();
        if feature.flags.contains(FeatureFlags::WEBKIT_PREFIX) {
            dest.write_str("-webkit-")?;
        }

        if let QueryFeatureExpressionKind::LegacyRange(range, _) = self.kind {
            match range {
                LegacyRange::Min => dest.write_str("min-")?,
                LegacyRange::Max => dest.write_str("max-")?,
            }
        }

        // NB: CssStringWriter not needed, feature names are under control.
        write!(dest, "{}", feature.name)?;

        Ok(())
    }

    fn feature(&self) -> &'static QueryFeatureDescription {
        &self.feature_type.features()[self.feature_index]
    }

    /// Returns the feature flags for our feature.
    pub fn feature_flags(&self) -> FeatureFlags {
        self.feature().flags
    }

    fn parse_feature_name<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<(usize, Option<LegacyRange>), ParseError<'i>> {
        let mut flags = FeatureFlags::empty();
        let location = input.current_source_location();
        let ident = input.expect_ident()?;

        if context.chrome_rules_enabled() {
            flags.insert(FeatureFlags::CHROME_AND_UA_ONLY);
        }

        let mut feature_name = &**ident;
        if starts_with_ignore_ascii_case(feature_name, "-webkit-") {
            feature_name = &feature_name[8..];
            flags.insert(FeatureFlags::WEBKIT_PREFIX);
        }

        let range = if starts_with_ignore_ascii_case(feature_name, "min-") {
            feature_name = &feature_name[4..];
            Some(LegacyRange::Min)
        } else if starts_with_ignore_ascii_case(feature_name, "max-") {
            feature_name = &feature_name[4..];
            Some(LegacyRange::Max)
        } else {
            None
        };

        let atom = Atom::from(string_as_ascii_lowercase(feature_name));
        let (feature_index, feature) = match feature_type.find_feature(&atom) {
            Some((i, f)) => (i, f),
            None => {
                return Err(location.new_custom_error(
                    StyleParseErrorKind::MediaQueryExpectedFeatureName(ident.clone()),
                ))
            },
        };

        if disabled_by_pref(&feature.name, context)
            || !flags.contains(feature.flags.parsing_requirements())
            || (range.is_some() && !feature.allows_ranges())
        {
            return Err(location.new_custom_error(
                StyleParseErrorKind::MediaQueryExpectedFeatureName(ident.clone()),
            ));
        }

        Ok((feature_index, range))
    }

    /// Parses the following range syntax:
    ///
    ///   (feature-value <operator> feature-name)
    ///   (feature-value <operator> feature-name <operator> feature-value)
    fn parse_multi_range_syntax<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        let start = input.state();

        // To parse the values, we first need to find the feature name. We rely
        // on feature values for ranged features not being able to be top-level
        // <ident>s, which holds.
        let feature_index = loop {
            // NOTE: parse_feature_name advances the input.
            if let Ok((index, range)) = Self::parse_feature_name(context, input, feature_type) {
                if range.is_some() {
                    // Ranged names are not allowed here.
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                break index;
            }
            if input.is_exhausted() {
                return Err(start
                    .source_location()
                    .new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
        };

        input.reset(&start);

        let feature = &feature_type.features()[feature_index];
        let left_val = QueryExpressionValue::parse(feature, context, input)?;
        let left_op = Operator::parse(input)?;

        {
            let (parsed_index, _) = Self::parse_feature_name(context, input, feature_type)?;
            debug_assert_eq!(
                parsed_index, feature_index,
                "How did we find a different feature?"
            );
        }

        let right_op = input.try_parse(Operator::parse).ok();
        let right = match right_op {
            Some(op) => {
                if !left_op.is_compatible_with(op) {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                Some((op, QueryExpressionValue::parse(feature, context, input)?))
            },
            None => None,
        };
        Ok(Self::new(
            feature_type,
            feature_index,
            QueryFeatureExpressionKind::Range {
                left: Some((left_op, left_val)),
                right,
            },
        ))
    }

    /// Parse a feature expression where we've already consumed the parenthesis.
    pub fn parse_in_parenthesis_block<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        let (feature_index, range) =
            match input.try_parse(|input| Self::parse_feature_name(context, input, feature_type)) {
                Ok(v) => v,
                Err(e) => {
                    if let Ok(expr) = Self::parse_multi_range_syntax(context, input, feature_type) {
                        return Ok(expr);
                    }
                    return Err(e);
                },
            };
        let operator = input.try_parse(consume_operation_or_colon);
        let operator = match operator {
            Err(..) => {
                // If there's no colon, this is a query of the form
                // '(<feature>)', that is, there's no value specified.
                //
                // Gecko doesn't allow ranged expressions without a
                // value, so just reject them here too.
                if range.is_some() {
                    return Err(
                        input.new_custom_error(StyleParseErrorKind::RangedExpressionWithNoValue)
                    );
                }

                return Ok(Self::new(
                    feature_type,
                    feature_index,
                    QueryFeatureExpressionKind::Empty,
                ));
            },
            Ok(operator) => operator,
        };

        let feature = &feature_type.features()[feature_index];

        let value = QueryExpressionValue::parse(feature, context, input).map_err(|err| {
            err.location
                .new_custom_error(StyleParseErrorKind::MediaQueryExpectedFeatureValue)
        })?;

        let kind = match range {
            Some(range) => {
                if operator.is_some() {
                    return Err(
                        input.new_custom_error(StyleParseErrorKind::MediaQueryUnexpectedOperator)
                    );
                }
                QueryFeatureExpressionKind::LegacyRange(range, value)
            },
            None => match operator {
                Some(operator) => {
                    if !feature.allows_ranges() {
                        return Err(input
                            .new_custom_error(StyleParseErrorKind::MediaQueryUnexpectedOperator));
                    }
                    QueryFeatureExpressionKind::Range {
                        left: None,
                        right: Some((operator, value)),
                    }
                },
                None => QueryFeatureExpressionKind::Single(value),
            },
        };

        Ok(Self::new(feature_type, feature_index, kind))
    }

    /// Returns whether this "plain" feature query evaluates to true for the given device.
    pub fn matches(&self, context: &computed::Context) -> KleeneValue {
        macro_rules! expect {
            ($variant:ident, $v:expr) => {
                match *$v {
                    QueryExpressionValue::$variant(ref v) => v,
                    _ => unreachable!("Unexpected QueryExpressionValue"),
                }
            };
        }

        KleeneValue::from(match self.feature().evaluator {
            Evaluator::Length(eval) => {
                let v = eval(context);
                self.kind
                    .evaluate(v, |v| expect!(Length, v).to_computed_value(context))
            },
            Evaluator::OptionalLength(eval) => {
                let v = match eval(context) {
                    Some(v) => v,
                    None => return KleeneValue::Unknown,
                };
                self.kind
                    .evaluate(v, |v| expect!(Length, v).to_computed_value(context))
            },
            Evaluator::Integer(eval) => {
                let v = eval(context);
                self.kind
                    .evaluate(v, |v| expect!(Integer, v).to_computed_value(context))
            },
            Evaluator::Float(eval) => {
                let v = eval(context);
                self.kind
                    .evaluate(v, |v| expect!(Float, v).to_computed_value(context))
            },
            Evaluator::NumberRatio(eval) => {
                let ratio = eval(context);
                // A ratio of 0/0 behaves as the ratio 1/0, so we need to call used_value()
                // to convert it if necessary.
                // FIXME: we may need to update here once
                // https://github.com/w3c/csswg-drafts/issues/4954 got resolved.
                self.kind.evaluate(ratio, |v| {
                    expect!(NumberRatio, v)
                        .to_computed_value(context)
                        .used_value()
                })
            },
            Evaluator::OptionalNumberRatio(eval) => {
                let ratio = match eval(context) {
                    Some(v) => v,
                    None => return KleeneValue::Unknown,
                };
                // See above for subtleties here.
                self.kind.evaluate(ratio, |v| {
                    expect!(NumberRatio, v)
                        .to_computed_value(context)
                        .used_value()
                })
            },
            Evaluator::Resolution(eval) => {
                let v = eval(context).dppx();
                self.kind.evaluate(v, |v| {
                    expect!(Resolution, v).to_computed_value(context).dppx()
                })
            },
            Evaluator::Enumerated { evaluator, .. } => {
                let computed = self
                    .kind
                    .non_ranged_value()
                    .map(|v| *expect!(Enumerated, v));
                return evaluator(context, computed);
            },
            Evaluator::BoolInteger(eval) => {
                let computed = self
                    .kind
                    .non_ranged_value()
                    .map(|v| expect!(BoolInteger, v).to_computed_value(context));
                let boolean = eval(context);
                computed.map_or(boolean, |v| v == boolean as i32)
            },
        })
    }
}

/// A value found or expected in a expression.
///
/// FIXME(emilio): How should calc() serialize in the Number / Integer /
/// BoolInteger / NumberRatio case, as computed or as specified value?
///
/// If the first, this would need to store the relevant values.
///
/// See: https://github.com/w3c/csswg-drafts/issues/1968
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum QueryExpressionValue {
    /// A length.
    Length(Length),
    /// An integer.
    Integer(Integer),
    /// A floating point value.
    Float(Number),
    /// A boolean value, specified as an integer (i.e., either 0 or 1).
    BoolInteger(Integer),
    /// A single non-negative number or two non-negative numbers separated by '/',
    /// with optional whitespace on either side of the '/'.
    NumberRatio(Ratio),
    /// A resolution.
    Resolution(Resolution),
    /// An enumerated value, defined by the variant keyword table in the
    /// feature's `mData` member.
    Enumerated(KeywordDiscriminant),
    /// Value types only used by style-range query expressions, not feature queries.
    /// A CSS-wide keyword.
    Keyword(CSSWideKeyword),
    /// A percentage.
    Percentage(Percentage),
    /// An angle.
    Angle(Angle),
    /// A time value.
    Time(Time),
    /// A custom property name.
    Custom(DashedIdent),
    /// An arbitrary substitution function (var(), attr(), env()), stored as a string
    /// for later evaluation. We store this as a custom-property value to make it easy
    /// to resolve later.
    Function(Box<CustomVariableValue>),
}

impl QueryExpressionValue {
    fn to_css<W>(
        &self,
        dest: &mut CssWriter<W>,
        for_expr: Option<&QueryFeatureExpression>,
    ) -> fmt::Result
    where
        W: fmt::Write,
    {
        match *self {
            QueryExpressionValue::Length(ref l) => l.to_css(dest),
            QueryExpressionValue::Integer(ref v) => v.to_css(dest),
            QueryExpressionValue::Float(ref v) => v.to_css(dest),
            QueryExpressionValue::BoolInteger(ref v) => v.to_css(dest),
            QueryExpressionValue::NumberRatio(ref ratio) => ratio.to_css(dest),
            QueryExpressionValue::Resolution(ref r) => r.to_css(dest),
            QueryExpressionValue::Keyword(k) => k.to_css(dest),
            QueryExpressionValue::Percentage(ref v) => v.to_css(dest),
            QueryExpressionValue::Angle(ref v) => v.to_css(dest),
            QueryExpressionValue::Time(ref v) => v.to_css(dest),
            QueryExpressionValue::Custom(ref v) => v.to_css(dest),
            QueryExpressionValue::Function(ref f) => f.to_css(dest),
            QueryExpressionValue::Enumerated(value) => match for_expr
                .expect("caller should have passed for_expr")
                .feature()
                .evaluator
            {
                Evaluator::Enumerated { serializer, .. } => dest.write_str(&*serializer(value)),
                _ => unreachable!(),
            },
        }
    }

    fn parse<'i, 't>(
        for_feature: &QueryFeatureDescription,
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<QueryExpressionValue, ParseError<'i>> {
        Ok(match for_feature.evaluator {
            Evaluator::OptionalLength(..) | Evaluator::Length(..) => {
                let length = Length::parse(context, input)?;
                QueryExpressionValue::Length(length)
            },
            Evaluator::Integer(..) => {
                let integer = Integer::parse(context, input)?;
                QueryExpressionValue::Integer(integer)
            },
            Evaluator::BoolInteger(..) => {
                let integer = Integer::parse(context, input)?;
                if matches!(integer.resolve(), Some(v) if v != 0 && v != 1) {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                QueryExpressionValue::BoolInteger(integer)
            },
            Evaluator::Float(..) => {
                let number = Number::parse(context, input)?;
                QueryExpressionValue::Float(number)
            },
            Evaluator::OptionalNumberRatio(..) | Evaluator::NumberRatio(..) => {
                use crate::values::specified::Ratio as SpecifiedRatio;
                let ratio = SpecifiedRatio::parse(context, input)?;
                QueryExpressionValue::NumberRatio(ratio)
            },
            Evaluator::Resolution(..) => {
                QueryExpressionValue::Resolution(Resolution::parse(context, input)?)
            },
            Evaluator::Enumerated { parser, .. } => {
                QueryExpressionValue::Enumerated(parser(context, input)?)
            },
        })
    }

    // Parse any of the types that can occur in a <style-range> query:
    // <number>, <percentage>, <length>, <angle>, <time>, <frequency> or <resolution>,
    // or a custom property name.
    // NB: we don't currently implement the <frequency> type anywhere, so it is not
    // parsed here.
    fn parse_for_style_range<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(number) = input.try_parse(|i| Number::parse(context, i)) {
            return Ok(Self::Float(number));
        }
        if let Ok(percent) = input.try_parse(|i| Percentage::parse(context, i)) {
            return Ok(Self::Percentage(percent));
        }
        if let Ok(length) = input.try_parse(|i| Length::parse(context, i)) {
            return Ok(Self::Length(length));
        }
        if let Ok(angle) = input.try_parse(|i| Angle::parse(context, i)) {
            return Ok(Self::Angle(angle));
        }
        if let Ok(time) = input.try_parse(|i| Time::parse(context, i)) {
            return Ok(Self::Time(time));
        }
        if let Ok(resolution) = input.try_parse(|i| Resolution::parse(context, i)) {
            return Ok(Self::Resolution(resolution));
        }
        if let Ok(ident) = input.try_parse(|i| DashedIdent::parse(context, i)) {
            return Ok(Self::Custom(ident));
        }
        if let Ok(keyword) = input.try_parse(|i| CSSWideKeyword::parse(i)) {
            return Ok(Self::Keyword(keyword));
        }
        input.skip_whitespace();
        let start = input.position();
        if let Ok(Token::Function(ref name)) = input.next() {
            // Helper to parse the function arg and store the complete expression (function
            // name and parenthesized argument) into a CustomVariableValue.
            let parse_func =
                |input: &mut Parser<'i, 't>| -> Result<CustomVariableValue, ParseError<'i>> {
                    input.parse_nested_block(|i| i.expect_no_error_token().map_err(Into::into))?;
                    let mut input = ParserInput::new(input.slice_from(start));
                    CustomVariableValue::parse(
                        &mut Parser::new(&mut input),
                        Some(&context.namespaces.prefixes),
                        context.url_data,
                    )
                };

            if properties::enabled_arbitrary_substitution_functions()
                .iter()
                .any(|n| n.eq_ignore_ascii_case(name))
            {
                return Ok(Self::Function(Box::new(parse_func(input)?)));
            }
        }
        Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    }
}

/// https://drafts.csswg.org/css-conditional-5/#typedef-style-range
#[derive(Clone, Debug, MallocSizeOf, ToShmem, PartialEq)]
pub enum QueryStyleRange {
    /// A style-range for style container queries with two values
    /// (val1 OP val2).
    #[allow(missing_docs)]
    StyleRange2 {
        value1: QueryExpressionValue,
        op1: Operator,
        value2: QueryExpressionValue,
    },

    /// A style-range for style container queries with three values
    /// (val1 OP val2 OP val3).
    #[allow(missing_docs)]
    StyleRange3 {
        value1: QueryExpressionValue,
        op1: Operator,
        value2: QueryExpressionValue,
        op2: Operator,
        value3: QueryExpressionValue,
    },
}

impl ToCss for QueryStyleRange {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        match self {
            Self::StyleRange2 {
                ref value1,
                ref op1,
                ref value2,
            } => {
                value1.to_css(dest, None)?;
                dest.write_char(' ')?;
                op1.to_css(dest)?;
                dest.write_char(' ')?;
                value2.to_css(dest, None)
            },
            Self::StyleRange3 {
                ref value1,
                ref op1,
                ref value2,
                ref op2,
                ref value3,
            } => {
                value1.to_css(dest, None)?;
                dest.write_char(' ')?;
                op1.to_css(dest)?;
                dest.write_char(' ')?;
                value2.to_css(dest, None)?;
                dest.write_char(' ')?;
                op2.to_css(dest)?;
                dest.write_char(' ')?;
                value3.to_css(dest, None)
            },
        }
    }
}

impl QueryStyleRange {
    /// Parses the following range syntax:
    ///
    ///   value <operator> value
    ///   value <operator> value <operator> value
    ///
    /// This is only used when parsing @container style() queries; the feature_type
    /// and index is hardcoded (and ignored).
    pub fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let value1 = QueryExpressionValue::parse_for_style_range(context, input)?;
        let op1 = Operator::parse(input)?;
        let value2 = QueryExpressionValue::parse_for_style_range(context, input)?;

        if let Ok(op2) = input.try_parse(|i| Operator::parse(i)) {
            if op1.is_compatible_with(op2) {
                let value3 = QueryExpressionValue::parse_for_style_range(context, input)?;
                return Ok(Self::StyleRange3 {
                    value1,
                    op1,
                    value2,
                    op2,
                    value3,
                });
            }
        }

        Ok(Self::StyleRange2 {
            value1,
            op1,
            value2,
        })
    }

    /// Returns whether this style-range query evaluates to true for the given context.
    pub fn evaluate(
        &self,
        context: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
    ) -> KleeneValue {
        match self {
            QueryStyleRange::StyleRange2 {
                ref value1,
                ref op1,
                ref value2,
            } => Self::compare_values(
                Self::resolve_value(
                    value1,
                    context,
                    attribute_tracker,
                    &mut PrecomputedHashSet::default(),
                )
                .as_ref(),
                Self::resolve_value(
                    value2,
                    context,
                    attribute_tracker,
                    &mut PrecomputedHashSet::default(),
                )
                .as_ref(),
            )
            .is_some_and(|c| op1.evaluate(c))
            .into(),

            QueryStyleRange::StyleRange3 {
                ref value1,
                ref op1,
                ref value2,
                ref op2,
                ref value3,
            } => {
                let v1 = Self::resolve_value(
                    value1,
                    context,
                    attribute_tracker,
                    &mut PrecomputedHashSet::default(),
                );
                let v2 = Self::resolve_value(
                    value2,
                    context,
                    attribute_tracker,
                    &mut PrecomputedHashSet::default(),
                );
                Self::compare_values(v1.as_ref(), v2.as_ref())
                    .is_some_and(|c1| {
                        op1.evaluate(c1)
                            && Self::compare_values(
                                v2.as_ref(),
                                Self::resolve_value(
                                    value3,
                                    context,
                                    attribute_tracker,
                                    &mut PrecomputedHashSet::default(),
                                )
                                .as_ref(),
                            )
                            .is_some_and(|c2| op2.evaluate(c2))
                    })
                    .into()
            },
        }
    }

    // Resolve a QueryExpressionValue to its computed value for comparison.
    fn resolve_value(
        value: &QueryExpressionValue,
        context: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
        visited_set: &mut PrecomputedHashSet<DashedIdent>,
    ) -> Option<Component> {
        match value {
            QueryExpressionValue::Custom(ident) => {
                // `ident` is the dashed ident, but we need the name
                // without "--" for custom-property lookup.
                let name = ident.undashed();
                let stylist = context
                    .builder
                    .stylist
                    .expect("container queries should have a stylist around");
                let registration = stylist.get_custom_property_registration(&name);
                let current_value = context
                    .inherited_custom_properties()
                    .get(registration, &name)?;
                match &current_value.v {
                    ValueInner::Component(component) => Some(component.clone()),
                    ValueInner::Universal(v) => {
                        // If visited_set.insert() returns false, ident was already seen
                        // and we risk infinite recursion, so instead return None
                        // (i.e. the value cannot be resolved).
                        if visited_set.insert(ident.clone()) {
                            Self::resolve_universal(
                                &v.css,
                                &v.url_data,
                                context,
                                attribute_tracker,
                                visited_set,
                            )
                        } else {
                            None
                        }
                    },
                    ValueInner::List(_) => {
                        debug_assert!(false, "We don't parse list values in style queries");
                        None
                    },
                }
            },
            QueryExpressionValue::Function(value) => {
                let sub_funcs = ComputedSubstitutionFunctions::new(
                    Some(context.inherited_custom_properties().clone()),
                    None,
                );
                let stylist = context
                    .builder
                    .stylist
                    .expect("container queries should have a stylist around");
                let substituted = custom_properties::substitute(
                    &value,
                    &sub_funcs,
                    stylist,
                    context,
                    attribute_tracker,
                )
                .ok()?;
                Self::resolve_universal(
                    &substituted.css,
                    &value.url_data,
                    context,
                    attribute_tracker,
                    visited_set,
                )
            },
            QueryExpressionValue::Length(v) => {
                Some(Component::Length(v.to_computed_value(context)))
            },
            QueryExpressionValue::Float(v) => Some(Component::Number(v.to_computed_value(context))),
            QueryExpressionValue::Resolution(v) => {
                Some(Component::Resolution(v.to_computed_value(context)))
            },
            QueryExpressionValue::Percentage(v) => {
                Some(Component::Percentage(v.to_computed_value(context)))
            },
            QueryExpressionValue::Angle(v) => Some(Component::Angle(v.to_computed_value(context))),
            QueryExpressionValue::Time(v) => Some(Component::Time(v.to_computed_value(context))),
            // It's unclear to me what CSS-wide keywords would mean in a style-range query;
            // for now, at least, they'll just fail to resolve.
            QueryExpressionValue::Keyword(_) => None,
            _ => {
                debug_assert!(false, "unexpected value type in style range");
                None
            },
        }
    }

    // If a custom-property QueryExpressionValue has a "universal-syntax" value, we need to
    // send the current CSS text of the value to QueryExpressionValue::parse_for_style_range
    // to try and resolve to a specific typed value.
    // After parsing, this will call back to QueryExpressionValue::resolve_value with the
    // parsed result, which has the potential for mutual recursion; we keep track of a
    // visited_set of custom property names to protect against this.
    fn resolve_universal(
        css_text: &str,
        url_data: &UrlExtraData,
        context: &computed::Context,
        attribute_tracker: &mut AttributeTracker,
        visited_set: &mut PrecomputedHashSet<DashedIdent>,
    ) -> Option<Component> {
        let parser_context = ParserContext::new(
            Origin::Author,
            url_data,
            Some(CssRuleType::Container),
            ParsingMode::DEFAULT,
            QuirksMode::NoQuirks,
            /* namespaces = */ Default::default(),
            /* error_reporter = */ None,
            /* use_counters = */ None,
            /* attr_taint */ Default::default(),
        );
        let mut input = ParserInput::new(css_text);
        QueryExpressionValue::parse_for_style_range(&parser_context, &mut Parser::new(&mut input))
            .ok()
            .and_then(|parsed| {
                Self::resolve_value(&parsed, context, attribute_tracker, visited_set)
            })
    }

    fn compare_values(value1: Option<&Component>, value2: Option<&Component>) -> Option<Ordering> {
        let value1 = value1?;
        let value2 = value2?;
        match (value1, value2) {
            (Component::Length(v1), Component::Length(v2)) => v1.partial_cmp(&v2),
            (Component::Number(v1), Component::Number(v2)) => v1.partial_cmp(&v2),
            (Component::Resolution(v1), Component::Resolution(v2)) => {
                v1.dppx().partial_cmp(&v2.dppx())
            },
            (Component::Percentage(v1), Component::Percentage(v2)) => v1.partial_cmp(&v2),
            (Component::Angle(v1), Component::Angle(v2)) => v1.partial_cmp(&v2),
            (Component::Time(v1), Component::Time(v2)) => v1.partial_cmp(&v2),
            (Component::Length(v1), Component::Number(v2)) => {
                if v2.is_zero() {
                    v1.partial_cmp(&CSSPixelLength::zero())
                } else {
                    None
                }
            },
            (Component::Number(v1), Component::Length(v2)) => {
                if v1.is_zero() {
                    CSSPixelLength::zero().partial_cmp(&v2)
                } else {
                    None
                }
            },
            _ => None,
        }
    }
}
