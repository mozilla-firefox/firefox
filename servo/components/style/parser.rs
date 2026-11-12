/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The context within which CSS code is parsed.

use crate::context::QuirksMode;
use crate::error_reporting::{ContextualParseError, ParseErrorReporter};
use crate::properties::PropertyIdRef;
use crate::stylesheets::{CssRuleType, CssRuleTypes, Namespaces, Origin, UrlExtraData};
use crate::substitution::{AttrTaint, AttrTaintedRange};
use crate::use_counters::UseCounters;
use cssparser::{Parser, SourceLocation, UnicodeRange};
use selectors::parser::ParseRelative;
use std::borrow::Cow;
use std::cell::Cell;
use style_traits::{OneOrMoreSeparated, ParseError, ParsingMode, Separator};

/// Nesting context for parsing rules.
#[derive(Clone, Copy)]
pub struct NestingContext {
    /// All rule types we've nested into, if any.
    pub rule_types: CssRuleTypes,
    /// Whether or not parsing relative selector syntax should be allowed.
    pub parse_relative: ParseRelative,
}

impl NestingContext {
    fn parse_relative_for(rule_type: CssRuleType) -> ParseRelative {
        match rule_type {
            CssRuleType::Scope => ParseRelative::ForScope,
            CssRuleType::Style => ParseRelative::ForNesting,
            _ => ParseRelative::No,
        }
    }

    /// Create a new nesting context.
    pub fn new(rule_types: CssRuleTypes, parse_nested_rule_type: Option<CssRuleType>) -> Self {
        Self {
            rule_types,
            parse_relative: parse_nested_rule_type
                .map_or(ParseRelative::No, Self::parse_relative_for),
        }
    }

    /// Create a new nesting context based on the given rule.
    pub fn new_from_rule(rule_type: Option<CssRuleType>) -> Self {
        Self {
            rule_types: rule_type.map(CssRuleTypes::from).unwrap_or_default(),
            parse_relative: rule_type
                .map(Self::parse_relative_for)
                .unwrap_or(ParseRelative::No),
        }
    }

    /// Save the current nesting context.
    pub fn save(&mut self, rule_type: CssRuleType) -> Self {
        let old = *self;
        self.rule_types.insert(rule_type);
        let new_parse_relative = Self::parse_relative_for(rule_type);
        if new_parse_relative != ParseRelative::No {
            self.parse_relative = new_parse_relative;
        }
        old
    }

    /// Load the saved nesting context.
    pub fn restore(&mut self, saved: Self) {
        *self = saved;
    }
}

/// Context for the property declaration that is currently being parsed.
#[derive(Debug, Default)]
pub struct PropertyDeclarationContext<'a> {
    /// Reference to the current property's name/ID.
    property_id: Option<PropertyIdRef<'a>>,
    /// Current count of random() functions that have been parsed so far
    /// in the current property declaration. Used when a random() function
    /// has a `property-index-scoped` key.
    random_count: Cell<i32>,
}

impl<'a> PropertyDeclarationContext<'a> {
    /// The declaration being parsed, if any.
    pub fn property_id(&self) -> Option<PropertyIdRef<'a>> {
        self.property_id
    }

    /// The index of the current random() function in this declaration.
    pub fn current_random_index(&self) -> i32 {
        self.random_count.get()
    }

    /// Claims the next index for a random() function in this declaration. The
    /// index is 1-based and follows the order the functions appear in the
    /// parsed value.
    pub fn increment_random_count(&self) -> i32 {
        let index = self.random_count.get() + 1;
        self.random_count.set(index);
        index
    }
}

/// The data that the parser needs from outside in order to parse a stylesheet.
pub struct ParserContext<'a> {
    /// The `Origin` of the stylesheet, whether it's a user, author or
    /// user-agent stylesheet.
    pub stylesheet_origin: Origin,
    /// The extra data we need for resolving url values.
    pub url_data: &'a UrlExtraData,
    /// The mode to use when parsing.
    pub parsing_mode: ParsingMode,
    /// The quirks mode of this stylesheet.
    pub quirks_mode: QuirksMode,
    /// The active error reporter, or none if error reporting is disabled.
    error_reporter: Option<&'a dyn ParseErrorReporter>,
    /// The currently active namespaces.
    pub namespaces: Cow<'a, Namespaces>,
    /// The use counters we want to record while parsing style rules, if any.
    pub use_counters: Option<&'a UseCounters>,
    /// Current nesting context.
    pub nesting_context: NestingContext,
    /// The relevant regions in the input that have been tainted by attr() if relevant.
    pub attr_tainted_regions: Cow<'a, AttrTaint>,
    /// Current property declaration context.
    pub property_declaration_context: PropertyDeclarationContext<'a>,
}

impl<'a> ParserContext<'a> {
    /// Create a parser context.
    #[inline]
    pub fn new(
        stylesheet_origin: Origin,
        url_data: &'a UrlExtraData,
        rule_type: Option<CssRuleType>,
        parsing_mode: ParsingMode,
        quirks_mode: QuirksMode,
        namespaces: Cow<'a, Namespaces>,
        error_reporter: Option<&'a dyn ParseErrorReporter>,
        use_counters: Option<&'a UseCounters>,
        attr_tainted_regions: AttrTaint,
    ) -> Self {
        Self {
            stylesheet_origin,
            url_data,
            parsing_mode,
            quirks_mode,
            error_reporter,
            namespaces,
            use_counters,
            nesting_context: NestingContext::new_from_rule(rule_type),
            attr_tainted_regions: Cow::Owned(attr_tainted_regions),
            property_declaration_context: Default::default(),
        }
    }

    /// Create a parser context with the given property ID.
    #[inline]
    pub fn new_with_property_id(
        stylesheet_origin: Origin,
        url_data: &'a UrlExtraData,
        rule_type: Option<CssRuleType>,
        parsing_mode: ParsingMode,
        quirks_mode: QuirksMode,
        namespaces: Cow<'a, Namespaces>,
        error_reporter: Option<&'a dyn ParseErrorReporter>,
        use_counters: Option<&'a UseCounters>,
        attr_tainted_regions: AttrTaint,
        property_id: Option<PropertyIdRef<'a>>,
    ) -> Self {
        Self {
            stylesheet_origin,
            url_data,
            parsing_mode,
            quirks_mode,
            error_reporter,
            namespaces,
            use_counters,
            nesting_context: NestingContext::new_from_rule(rule_type),
            attr_tainted_regions: Cow::Owned(attr_tainted_regions),
            property_declaration_context: PropertyDeclarationContext {
                property_id,
                ..Default::default()
            },
        }
    }

    /// Constructs a temporary ParserContext with the given property ID, passes it to the
    /// callback and returns its result.
    pub fn with_property_id<'b, R>(
        &'b self,
        property_id: Option<PropertyIdRef<'b>>,
        cb: impl FnOnce(&ParserContext<'b>) -> R,
    ) -> R {
        cb(&ParserContext {
            stylesheet_origin: self.stylesheet_origin,
            url_data: self.url_data,
            parsing_mode: self.parsing_mode,
            quirks_mode: self.quirks_mode,
            error_reporter: self.error_reporter,
            namespaces: Cow::Borrowed(&*self.namespaces),
            use_counters: self.use_counters,
            nesting_context: self.nesting_context,
            attr_tainted_regions: Cow::Borrowed(&*self.attr_tainted_regions),
            property_declaration_context: PropertyDeclarationContext {
                property_id,
                ..Default::default()
            },
        })
    }

    /// Temporarily sets the rule_type and executes the callback function, returning its result.
    pub fn nest_for_rule<R>(
        &mut self,
        rule_type: CssRuleType,
        cb: impl FnOnce(&mut Self) -> R,
    ) -> R {
        let old = self.nesting_context.save(rule_type);
        let r = cb(self);
        self.nesting_context.restore(old);
        r
    }

    /// Temporarily adds a parsing mode flag and executes the callback, returning its result.
    pub fn with_parsing_mode<R>(&mut self, mode: ParsingMode, cb: impl FnOnce(&Self) -> R) -> R {
        let old = self.parsing_mode;
        self.parsing_mode |= mode;
        let r = cb(self);
        self.parsing_mode = old;
        r
    }

    /// Whether we're in a @page rule.
    #[inline]
    pub fn in_page_rule(&self) -> bool {
        self.nesting_context.rule_types.contains(CssRuleType::Page)
    }

    /// Returns whether !important declarations are forbidden.
    #[inline]
    pub fn allows_important_declarations(&self) -> bool {
        !self
            .nesting_context
            .rule_types
            .intersects(CssRuleTypes::IMPORTANT_FORBIDDEN)
    }

    /// Returns whether we can parse element-dependent values.
    #[inline]
    pub fn has_element_context(&self) -> bool {
        if self
            .nesting_context
            .rule_types
            .intersects(CssRuleTypes::WITHOUT_ELEMENT_CONTEXT)
        {
            return false;
        }

        if self
            .parsing_mode
            .intersects(ParsingMode::MEDIA_QUERY_CONDITION)
        {
            return false;
        }

        if !self.allows_computational_dependence() {
            return false;
        }

        true
    }

    /// Get the rule type, which assumes that one is available.
    #[inline]
    pub fn rule_types(&self) -> CssRuleTypes {
        self.nesting_context.rule_types
    }

    /// Returns whether CSS error reporting is enabled.
    #[inline]
    pub fn error_reporting_enabled(&self) -> bool {
        self.error_reporter.is_some()
    }

    /// Record a CSS parse error with this context’s error reporting.
    pub fn log_css_error(&self, location: SourceLocation, error: ContextualParseError) {
        let error_reporter = match self.error_reporter {
            Some(r) => r,
            None => return,
        };

        error_reporter.report_error(self.url_data, location, error)
    }

    /// Whether we're in a user-agent stylesheet.
    #[inline]
    pub fn in_ua_sheet(&self) -> bool {
        self.stylesheet_origin == Origin::UserAgent
    }

    /// Returns whether chrome-only rules should be parsed.
    #[inline]
    pub fn chrome_rules_enabled(&self) -> bool {
        self.url_data.chrome_rules_enabled() || self.stylesheet_origin != Origin::Author
    }

    /// Whether the parsing mode allows units or functions that are not computationally independent.
    #[inline]
    pub fn allows_computational_dependence(&self) -> bool {
        self.parsing_mode.allows_computational_dependence()
    }

    /// Whether any `<url>` over this `range` is disallowed due to attr()-tainting.
    pub fn disallow_urls_in_range(&self, range: &AttrTaintedRange) -> bool {
        self.attr_tainted_regions
            .should_disallow_urls_in_range(range)
    }
}

/// A trait to abstract parsing of a specified value given a `ParserContext` and
/// CSS input.
///
/// This can be derived on keywords with `#[derive(Parse)]`.
///
/// The derive code understands the following attributes on each of the variants:
///
///  * `#[parse(aliases = "foo,bar")]` can be used to alias a value with another
///    at parse-time.
///
///  * `#[parse(condition = "function")]` can be used to make the parsing of the
///    value conditional on `function`, which needs to fulfill
///    `fn(&ParserContext) -> bool`.
///
///  * `#[parse(parse_fn = "function")]` can be used to specify a function other than Parser::parse
///    for a particular variant.
pub trait Parse: Sized {
    /// Parse a value of this type.
    ///
    /// Returns an error on failure.
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError>;
}

impl<T> Parse for Vec<T>
where
    T: Parse + OneOrMoreSeparated,
    <T as OneOrMoreSeparated>::S: Separator,
{
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        <T as OneOrMoreSeparated>::S::parse(input, |i| T::parse(context, i))
    }
}

impl<T> Parse for Box<T>
where
    T: Parse,
{
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        T::parse(context, input).map(Box::new)
    }
}

impl Parse for crate::OwnedStr {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(input.expect_string()?.as_ref().to_owned().into())
    }
}

impl Parse for UnicodeRange {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(UnicodeRange::parse(input)?)
    }
}
