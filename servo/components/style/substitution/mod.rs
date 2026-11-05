/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Arbitrary substitution functions.
//! https://drafts.csswg.org/css-values-5/#arbitrary-substitution-function

pub mod attr;
pub mod env;
pub mod ident;
pub mod var;

use crate::custom_properties::{
    ComputedSubstitutionFunctions, ComputedValue, Name, VariableValue, compute_value,
};
use crate::derives::*;
use crate::dom::AttributeTracker;
use crate::parser::{Parse, ParserContext};
use crate::properties::PropertyIdRef;
use crate::properties_and_values::{
    rule::Descriptors as PropertyDescriptors, value::ComputedValue as ComputedRegisteredValue,
};
use crate::stylesheets::{Origin, UrlExtraData};
use crate::stylist::Stylist;
use crate::values::computed;
use crate::values::specified::NoCalcLength;
use cssparser::{
    CowRcStr, Delimiter, Delimiters, Parser, SourcePosition, Token, TokenSerializationType,
};
use servo_arc::Arc;
use smallvec::SmallVec;
use std::borrow::Cow;
use std::num;
use strum::VariantNames;
use style_traits::{ParseError, ParsingMode, StyleParseErrorKind};

pub use self::attr::AttrFunction;
pub use self::env::EnvFunction;
pub use self::ident::IdentFunction;
pub use self::var::VarFunction;

/// Substitution function name.
#[derive(Copy, Clone, Debug, MallocSizeOf, Hash, Eq, PartialEq, ToShmem, VariantNames)]
#[strum(serialize_all = "kebab-case")]
pub enum SubstitutionFunctionKind {
    /// CSS variable / custom property
    Var,
    /// Environment variable
    Env,
    /// DOM attribute
    Attr,
    /// The ident() substitution function
    Ident,
}

impl SubstitutionFunctionKind {
    /// Parse the substitution function kind from its name.
    #[inline]
    pub fn from_ident(ident: &str) -> Result<Self, ()> {
        Ok(cssparser::match_ignore_ascii_case! {
            ident,
            "var" => Self::Var,
            "env" => Self::Env,
            "attr" => Self::Attr,
            "ident" if static_prefs::pref!("layout.css.ident-function.enabled") => {
                Self::Ident
            },
            _ => return Err(()),
        })
    }
}

/// Context for parsing an arbitrary substitution function.
pub struct SubstitutionParserContext<'a> {
    /// Original parser context.
    pub parser_context: &'a ParserContext<'a>,
    /// The start of the value being parsed. Positions are recorded relative to this.
    pub input_start: SourcePosition,
    /// Closing characters implied by EOF, to be appended to the value's serialization.
    pub missing_closing_characters: String,
}

impl<'a> SubstitutionParserContext<'a> {
    /// Creates a parser context for a value starting at `input_start`.
    pub fn new(parser_context: &'a ParserContext<'a>, input_start: SourcePosition) -> Self {
        Self {
            parser_context,
            input_start,
            missing_closing_characters: String::new(),
        }
    }
}

/// An arbitrary `<declaration-value>` nested inside of a substitution function.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct ArbitraryDeclarationValue {
    /// The start of the value in the referencing value's CSS, counted in bytes.
    pub start: num::NonZeroUsize,
    /// The end of the value in the referencing value's CSS, counted in bytes.
    pub end: num::NonZeroUsize,
    first_token_type: TokenSerializationType,
    last_token_type: TokenSerializationType,
    /// References from this value.
    pub references: References,
}

impl ArbitraryDeclarationValue {
    fn parse_internal(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
        additional_delimiters: Delimiters,
    ) -> Result<Self, ParseError> {
        let start = num::NonZeroUsize::new(
            input.position().byte_index() - context.input_start.byte_index(),
        )
        .unwrap();
        let mut references = References::default();
        // NOTE(emilio): Intentionally using parse_declaration_value rather than
        // parse_declaration_value_block, since that's what parse_fallback used to do.
        let (first_token_type, last_token_type) =
            parse_declaration_value(input, context, &mut references, additional_delimiters)?;
        let end = num::NonZeroUsize::new(
            input.position().byte_index() - context.input_start.byte_index()
                + context.missing_closing_characters.len(),
        )
        .unwrap();
        Ok(Self {
            start,
            end,
            first_token_type,
            last_token_type,
            references,
        })
    }

    /// Parses a free-form `<declaration-value>` production, which may be surrounded by curly
    /// braces. Also returns the source text of the value, excluding any curly braces. If parsing
    /// a legacy non-strict production, an unbraced value may also contain commas and {} blocks.
    /// https://drafts.csswg.org/css-values-5/#component-function-commas
    /// https://drafts.csswg.org/css-values-5/#non-strict-free-form-production
    fn parse_free_form<'i>(
        input: &mut Parser<'i>,
        context: &mut SubstitutionParserContext,
        additional_delimiters: Delimiters,
        is_non_strict: bool,
    ) -> Result<(Self, &'i str), ParseError> {
        if input
            .try_parse(|input| input.expect_curly_bracket_block())
            .is_ok()
        {
            let mut inner_end_position = None;
            let result = input.parse_nested_block(|input| {
                input.skip_whitespace();
                let start_position = input.position();
                let value = Self::parse_internal(input, context, Delimiter::None)?;
                let end_position = input.position();
                inner_end_position = Some(end_position);
                Ok((value, input.slice(start_position..end_position)))
            })?;
            if inner_end_position.unwrap() == input.position() {
                context.missing_closing_characters.push('}');
            }
            return Ok(result);
        }
        input.skip_whitespace();
        let start_position = input.position();
        let delimiters = if is_non_strict {
            additional_delimiters
        } else {
            additional_delimiters | Delimiter::Comma | Delimiter::CurlyBracketBlock
        };
        let value = Self::parse_internal(input, context, delimiters)?;
        Ok((value, input.slice_from(start_position)))
    }

    fn is_empty(&self) -> bool {
        self.first_token_type == TokenSerializationType::Nothing
    }
}

fn parse_optional_fallback(
    input: &mut Parser,
    context: &mut SubstitutionParserContext,
) -> Result<Option<ArbitraryDeclarationValue>, ParseError> {
    if input.try_parse(|input| input.expect_comma()).is_ok() {
        let (fallback, _) = ArbitraryDeclarationValue::parse_free_form(
            input,
            context,
            Delimiter::None,
            /* is_non_strict */ true,
        )?;
        Ok(Some(fallback))
    } else {
        let state = input.state();
        // We still need to consume the rest of the potentially-unclosed
        // tokens, but make sure to not consume tokens that would otherwise be
        // invalid, by calling reset().
        let mut ignored_references = References::default();
        parse_declaration_value_block(input, context, &mut ignored_references)?;
        input.reset(&state);
        Ok(None)
    }
}

/// <https://drafts.csswg.org/css-syntax-3/#typedef-declaration-value>
pub fn parse_declaration_value(
    input: &mut Parser,
    context: &mut SubstitutionParserContext,
    references: &mut References,
    additional_delimiters: Delimiters,
) -> Result<(TokenSerializationType, TokenSerializationType), ParseError> {
    input.parse_until_before(
        Delimiter::Bang | Delimiter::Semicolon | additional_delimiters,
        |input| parse_declaration_value_block(input, context, references),
    )
}

/// Like parse_declaration_value, but accept `!` and `;` since they are only invalid at the top level.
fn parse_declaration_value_block(
    input: &mut Parser,
    context: &mut SubstitutionParserContext,
    references: &mut References,
) -> Result<(TokenSerializationType, TokenSerializationType), ParseError> {
    let mut is_first = true;
    let mut first_token_type = TokenSerializationType::Nothing;
    let mut last_token_type = TokenSerializationType::Nothing;
    let mut prev_reference_index: Option<usize> = None;
    loop {
        let token_start = input.position();
        let Ok(token) = input.next_including_whitespace_and_comments() else {
            break;
        };

        let prev_token_type = last_token_type;
        let serialization_type = token.serialization_type();
        last_token_type = serialization_type;
        if is_first {
            first_token_type = last_token_type;
            is_first = false;
        }

        macro_rules! nested {
            ($closing:expr) => {{
                let mut inner_end_position = None;
                let result = input.parse_nested_block(|input| {
                    let result = parse_declaration_value_block(input, context, references)?;
                    inner_end_position = Some(input.position());
                    Ok(result)
                })?;
                if inner_end_position.unwrap() == input.position() {
                    context.missing_closing_characters.push_str($closing);
                }
                result
            }};
        }
        if let Some(index) = prev_reference_index.take() {
            references.refs[index].next_token_type = serialization_type;
        }
        match *token {
            Token::Comment(_) => {
                let token_slice = input.slice_from(token_start);
                if !token_slice.ends_with("*/") {
                    context
                        .missing_closing_characters
                        .push_str(if token_slice.ends_with('*') {
                            "/"
                        } else {
                            "*/"
                        })
                }
            },
            Token::BadUrl(..) => {
                let e = StyleParseErrorKind::BadUrlInDeclarationValueBlock;
                return Err(ParseError::custom(e));
            },
            Token::BadString(..) => {
                let e = StyleParseErrorKind::BadStringInDeclarationValueBlock;
                return Err(ParseError::custom(e));
            },
            Token::CloseParenthesis => {
                let e = StyleParseErrorKind::UnbalancedCloseParenthesisInDeclarationValueBlock;
                return Err(ParseError::custom(e));
            },
            Token::CloseSquareBracket => {
                let e = StyleParseErrorKind::UnbalancedCloseSquareBracketInDeclarationValueBlock;
                return Err(ParseError::custom(e));
            },
            Token::CloseCurlyBracket => {
                let e = StyleParseErrorKind::UnbalancedCloseCurlyBracketInDeclarationValueBlock;
                return Err(ParseError::custom(e));
            },
            Token::Function(ref name) => {
                let substitution_kind = SubstitutionFunctionKind::from_ident(name).ok();
                if let Some(substitution_kind) = substitution_kind {
                    let start = token_start.byte_index() - context.input_start.byte_index();
                    let mut input_end_position = None;
                    let function = input.parse_nested_block(|input| {
                        let function =
                            SubstitutionFunction::parse(substitution_kind, input, context)?;
                        input.expect_exhausted()?;
                        input_end_position = Some(input.position());
                        Ok(function)
                    })?;
                    if input_end_position.unwrap() == input.position() {
                        context.missing_closing_characters.push(')');
                    }
                    let end = input.position().byte_index() - context.input_start.byte_index()
                        + context.missing_closing_characters.len();
                    references.flags |= function.reference_flags();
                    // Bubble up flags from our nested values, so we know what we might reference
                    // from the outer scope.
                    function.for_each_unparsed_value(|value| {
                        references.flags |= value.references.flags
                    });
                    prev_reference_index = Some(references.refs.len());
                    references.refs.push(SubstitutionFunctionReference {
                        start,
                        end,
                        prev_token_type,
                        // To be fixed up (if needed) on the next loop iteration via prev_reference_index.
                        next_token_type: TokenSerializationType::Nothing,
                        function,
                    });
                } else {
                    nested!(")");
                }
            },
            Token::ParenthesisBlock => {
                nested!(")");
            },
            Token::CurlyBracketBlock => {
                nested!("}");
            },
            Token::SquareBracketBlock => {
                nested!("]");
            },
            Token::QuotedString(_) => {
                let token_slice = input.slice_from(token_start);
                let quote = &token_slice[..1];
                debug_assert!(matches!(quote, "\"" | "'"));
                if !(token_slice.ends_with(quote) && token_slice.len() > 1) {
                    context.missing_closing_characters.push_str(quote)
                }
            },
            Token::Ident(ref value)
            | Token::AtKeyword(ref value)
            | Token::Hash(ref value)
            | Token::IDHash(ref value)
            | Token::UnquotedUrl(ref value)
            | Token::Dimension {
                unit: ref value, ..
            } => {
                references.flags.insert(ReferenceFlags::from_unit(value));
                let is_unquoted_url = matches!(token, Token::UnquotedUrl(_));
                if value.ends_with("�") && input.slice_from(token_start).ends_with("\\") {
                    // Unescaped backslash at EOF in these contexts is interpreted as U+FFFD
                    // Check the value in case the final backslash was itself escaped.
                    // Serialize as escaped U+FFFD, which is also interpreted as U+FFFD.
                    // (Unescaped U+FFFD would also work, but removing the backslash is annoying.)
                    context.missing_closing_characters.push('�')
                }
                if is_unquoted_url && !input.slice_from(token_start).ends_with(")") {
                    context.missing_closing_characters.push(')');
                }
            },
            _ => {},
        };
    }
    Ok((first_token_type, last_token_type))
}

/// An argument of a substitution function that is parsed as `T` at substitution time.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum SubstitutionArgument<T> {
    /// The argument in its `<declaration-value>` form with nested substitution functions.
    Unparsed(ArbitraryDeclarationValue),
    /// The argument successfully parsed as `T` at parse time.
    Parsed(T),
    /// The argument is a valid `<declaration-value>` and contains no nested substitution
    /// functions, but is not a valid `T`.
    Invalid,
}

impl<T: Parse> SubstitutionArgument<T> {
    /// Parses the argument as a `<declaration-value>`. If it has no nested substitution
    /// functions, it is also parsed right away, giving `Parsed` or `Invalid`; otherwise
    /// it is kept as `Unparsed`, to be parsed at substitution time.
    pub fn try_parse(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
        delimiters: Delimiters,
    ) -> Result<Self, ParseError> {
        let (value, css) = ArbitraryDeclarationValue::parse_free_form(
            input, context, delimiters, /* is_non_strict */ false,
        )?;
        if value.is_empty() {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        if !value.references.has_references() {
            let parser_context = context.parser_context;
            if let Ok(result) =
                Parser::new(css).parse_entirely(|input| T::parse(parser_context, input))
            {
                return Ok(Self::Parsed(result));
            }
            return Ok(Self::Invalid);
        }
        Ok(Self::Unparsed(value))
    }
}

impl<T: Clone + Parse> SubstitutionArgument<T> {
    /// Returns the argument, first substituting its nested references and
    /// parsing it into `T` via `parse` if it is `Unparsed`.
    pub fn substitute_and_parse<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> ParsedArgument<'a, T> {
        let arg = match self {
            Self::Unparsed(arg) => arg,
            Self::Parsed(value) => {
                return ParsedArgument {
                    value: Some(Cow::Borrowed(value)),
                    attr_tainted: false,
                };
            },
            Self::Invalid => {
                return ParsedArgument {
                    value: None,
                    attr_tainted: false,
                };
            },
        };
        let Ok(substitution) = context.substitute_declaration_value(arg, source) else {
            return ParsedArgument {
                value: None,
                attr_tainted: false,
            };
        };
        let parser_context = context.parser_context(&source.url_data);
        ParsedArgument {
            value: Parser::new(&substitution.css)
                .parse_entirely(|input| T::parse(&parser_context, input))
                .ok()
                .map(Cow::Owned),
            attr_tainted: substitution.attr_tainted,
        }
    }
}

/// The result of substituting and parsing a `SubstitutionArgument`.
pub struct ParsedArgument<'a, T: Clone> {
    /// The parsed argument, or `None` if it failed to substitute or parse.
    pub value: Option<Cow<'a, T>>,
    /// Whether any attr()-tainted values were involved in the substitution.
    pub attr_tainted: bool,
}

/// A substitution function along with the data specific to it.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum SubstitutionFunction {
    /// A `var()` reference.
    Var(VarFunction),
    /// An `env()` reference.
    Env(EnvFunction),
    /// An `attr()` reference.
    Attr(AttrFunction),
    /// An `ident()` reference.
    Ident(IdentFunction),
}

impl SubstitutionFunction {
    /// Parses the arguments of a substitution function of the given `kind`, with `input`
    /// positioned inside its nested block.
    pub fn parse(
        kind: SubstitutionFunctionKind,
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
    ) -> Result<Self, ParseError> {
        Ok(match kind {
            SubstitutionFunctionKind::Var => Self::Var(VarFunction::parse(input, context)?),
            SubstitutionFunctionKind::Env => Self::Env(EnvFunction::parse(input, context)?),
            SubstitutionFunctionKind::Attr => Self::Attr(AttrFunction::parse(input, context)?),
            SubstitutionFunctionKind::Ident => Self::Ident(IdentFunction::parse(input, context)?),
        })
    }

    /// The reference flags to record for a reference to this function.
    #[inline]
    pub fn reference_flags(&self) -> ReferenceFlags {
        match self {
            Self::Var(..) => ReferenceFlags::VAR,
            Self::Env(..) => ReferenceFlags::ENV,
            Self::Attr(..) => ReferenceFlags::ATTR,
            Self::Ident(..) => ReferenceFlags::empty(),
        }
    }

    fn replace<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> Result<Substitution<'a>, ()> {
        match self {
            Self::Var(function) => function.replace(source, context),
            Self::Env(function) => function.replace(source, context),
            Self::Attr(function) => function.replace(source, context),
            Self::Ident(function) => function.replace(source, context),
        }
    }

    /// Calls `f` for each arbitrary declaration value nested in the function.
    #[inline]
    pub fn for_each_unparsed_value<F>(&self, f: F)
    where
        F: FnMut(&ArbitraryDeclarationValue),
    {
        match self {
            Self::Var(function) => function.for_each_unparsed_value(f),
            Self::Env(function) => function.for_each_unparsed_value(f),
            Self::Attr(function) => function.for_each_unparsed_value(f),
            Self::Ident(function) => function.for_each_unparsed_value(f),
        }
    }
}

/// A reference to a substitution function like env() / var() / attr().
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct SubstitutionFunctionReference {
    /// The start of the function in the referencing value's CSS, counted in bytes.
    pub start: usize,
    /// The end of the function in the referencing value's CSS, counted in bytes.
    pub end: usize,
    /// The serialization type of the token before the function.
    pub prev_token_type: TokenSerializationType,
    /// The serialization type of the token after the function.
    pub next_token_type: TokenSerializationType,
    /// Function-specific data.
    pub function: SubstitutionFunction,
}

/// Set of flags to references this custom property makes.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, MallocSizeOf, ToShmem)]
pub struct ReferenceFlags(u8);

bitflags! {
    impl ReferenceFlags : u8 {
        /// At least one custom property depends on font-relative units.
        const FONT_UNITS = 1 << 0;
        /// At least one custom property depends on root element's font-relative units.
        const ROOT_FONT_UNITS = 1 << 1;
        /// At least one custom property depends on line height units.
        const LH_UNITS = 1 << 2;
        /// At least one custom property depends on root element's line height units.
        const ROOT_LH_UNITS = 1 << 3;
        /// The value depends on the used color-scheme (e.g. a registered `<color>` property).
        const COLOR_SCHEME = 1 << 4;
        /// All dependencies not depending on the root element.
        const NON_ROOT_DEPENDENCIES = Self::FONT_UNITS.0 | Self::LH_UNITS.0;
        /// All dependencies depending on the root element.
        const ROOT_DEPENDENCIES = Self::ROOT_FONT_UNITS.0 | Self::ROOT_LH_UNITS.0;
        /// All non-custom dependencies
        const NON_CUSTOM = Self::NON_ROOT_DEPENDENCIES.0 | Self::ROOT_DEPENDENCIES.0;
        /// At least one attr() reference.
        const ATTR = 1 << 5;
        /// At least one env() reference.
        const ENV = 1 << 6;
        /// At least one var() reference.
        const VAR = 1 << 7;
    }
}

impl ReferenceFlags {
    fn from_unit(value: &CowRcStr) -> Self {
        // For registered properties, any reference to font-relative dimensions
        // make it dependent on font-related properties.
        // TODO(dshin): When we unit algebra gets implemented and handled -
        // Is it valid to say that `calc(1em / 2em * 3px)` triggers this?
        if value.eq_ignore_ascii_case(NoCalcLength::LH) {
            return Self::FONT_UNITS | Self::LH_UNITS;
        }
        if value.eq_ignore_ascii_case(NoCalcLength::EM)
            || value.eq_ignore_ascii_case(NoCalcLength::EX)
            || value.eq_ignore_ascii_case(NoCalcLength::CAP)
            || value.eq_ignore_ascii_case(NoCalcLength::CH)
            || value.eq_ignore_ascii_case(NoCalcLength::IC)
        {
            return Self::FONT_UNITS;
        }
        if value.eq_ignore_ascii_case(NoCalcLength::RLH) {
            return Self::ROOT_FONT_UNITS | Self::ROOT_LH_UNITS;
        }
        if value.eq_ignore_ascii_case(NoCalcLength::REM)
            || value.eq_ignore_ascii_case(NoCalcLength::REX)
            || value.eq_ignore_ascii_case(NoCalcLength::RCH)
            || value.eq_ignore_ascii_case(NoCalcLength::RCAP)
            || value.eq_ignore_ascii_case(NoCalcLength::RIC)
        {
            return Self::ROOT_FONT_UNITS;
        }
        Self::empty()
    }
}

/// A struct holding information about the external references to that a custom property value may
/// have.
#[derive(Clone, Debug, Default, MallocSizeOf, PartialEq, ToShmem)]
pub struct References {
    /// The actual list of references that we end up with.
    pub refs: Vec<SubstitutionFunctionReference>,
    /// Various flags about the kind of references we hold. Note that these include data about our
    /// nested references.
    ///
    /// TODO(emilio): Do we need to distinguish between own and nested flags?
    pub flags: ReferenceFlags,
}

impl References {
    /// Whether there are any substitution function references.
    pub fn has_references(&self) -> bool {
        !self.refs.is_empty()
    }

    pub(crate) fn non_custom_references(&self, is_root_element: bool) -> ReferenceFlags {
        let mut mask = ReferenceFlags::NON_ROOT_DEPENDENCIES;
        if is_root_element {
            mask |= ReferenceFlags::ROOT_DEPENDENCIES
        }
        self.flags & mask
    }
}

/// For a CSS string, the range, counted in bytes, that is attr()-tainted.
#[derive(Clone, Debug, Default, MallocSizeOf, PartialEq, ToShmem, ToComputedValue)]
pub struct AttrTaintedRange {
    /// Start of the range, counted in bytes. Inclusive.
    start: usize,
    /// End of the range, counted in bytes. Exclusive.
    end: usize,
}

impl AttrTaintedRange {
    /// Creates a range within a CSS string that is tainted by attr().
    #[inline(always)]
    pub fn new(start: usize, end: usize) -> Self {
        debug_assert!(start <= end);
        Self { start, end }
    }
}

/// In CSS Values and Units, values produced by `attr()` are considered attr()-tainted, as are
/// functions that contain an attr()-tainted value. Using an attr()-tainted value as or in a <url>
/// makes a declaration invalid at computed-value time.
/// https://drafts.csswg.org/css-values-5/#attr-security
#[derive(Clone, Debug, Default, MallocSizeOf, PartialEq, ToShmem)]
pub struct AttrTaint(SmallVec<[AttrTaintedRange; 1]>);

impl AttrTaint {
    /// For a CSS string, determine whether any `<url>` overlapping this `range`
    /// is disallowed due to attr()-tainting.
    #[inline(always)]
    pub fn should_disallow_urls_in_range(&self, range: &AttrTaintedRange) -> bool {
        self.0
            .iter()
            .any(|r| r.start <= range.end && r.end >= range.start)
    }

    /// Returns true if the attr()-tainted range contains no elements.
    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    #[inline(always)]
    fn new_fully_tainted(end: usize) -> Self {
        let mut taint = Self::default();
        taint.push(0, end);
        taint
    }

    /// Marks the range [start, end) as tainted.
    #[inline(always)]
    pub fn push(&mut self, start: usize, end: usize) {
        self.0.push(AttrTaintedRange::new(start, end));
    }
}

/// The result of substituting a value or a single substitution function.
#[derive(Default, Debug)]
pub struct Substitution<'a> {
    /// The substituted CSS.
    pub css: Cow<'a, str>,
    /// The serialization type of the first token in `css`.
    pub first_token_type: TokenSerializationType,
    /// The serialization type of the last token in `css`.
    pub last_token_type: TokenSerializationType,
    /// Whether any attr()-tainted values were involved in the substitution.
    pub attr_tainted: bool,
}

impl<'a> Substitution<'a> {
    /// Creates a substitution from a variable value.
    pub fn from_value(v: VariableValue, attr_tainted: bool) -> Self {
        Substitution {
            css: v.css.into(),
            first_token_type: v.first_token_type,
            last_token_type: v.last_token_type,
            attr_tainted,
        }
    }

    /// Computes the substitution against the registration of a custom property.
    pub fn into_value(
        self,
        url_data: &UrlExtraData,
        registration: &PropertyDescriptors,
        computed_context: &computed::Context,
        property_id: Option<PropertyIdRef>,
    ) -> Result<ComputedRegisteredValue, ()> {
        if registration.is_universal() {
            let mut value = ComputedRegisteredValue::universal(Arc::new(VariableValue::new(
                self.css.into_owned(),
                url_data,
                self.first_token_type,
                self.last_token_type,
            )));
            value.attr_tainted |= self.attr_tainted;
            return Ok(value);
        }
        let taint = if self.attr_tainted {
            // Per spec: substitution value of an arbitrary substitution function is
            // attr()-tainted as a whole if any attr()-tainted values were involved
            // in creating that substitution value.
            // https://drafts.csswg.org/css-values-5/#attr-security
            AttrTaint::new_fully_tainted(self.css.len())
        } else {
            AttrTaint::default()
        };
        let mut v = compute_value(
            &self.css,
            url_data,
            registration,
            computed_context,
            taint,
            property_id,
        )?;
        v.attr_tainted |= self.attr_tainted;
        Ok(v)
    }

    /// Creates a substitution from its parts.
    pub fn new(
        css: Cow<'a, str>,
        first_token_type: TokenSerializationType,
        last_token_type: TokenSerializationType,
        attr_tainted: bool,
    ) -> Self {
        Self {
            css,
            first_token_type,
            last_token_type,
            attr_tainted,
        }
    }
}

/// Context for replacing an arbitrary substitution function with its value.
pub struct SubstitutionReplacementContext<'a, 'b, 'c, 't> {
    /// The property the value is being substituted for, if any.
    pub property_id: Option<PropertyIdRef<'b>>,
    /// The custom properties and attributes to substitute from.
    pub substitution_functions: &'a ComputedSubstitutionFunctions,
    /// The stylist.
    pub stylist: &'b Stylist,
    /// The computed context.
    pub computed_context: &'b computed::Context<'c>,
    /// Tracks the attributes queried by `attr()`.
    pub attribute_tracker: &'b mut AttributeTracker<'t>,
    /// The custom properties currently being resolved, for cycle detection.
    pub seen: SmallVec<[Name; 8]>,
}

impl<'a, 'b, 'c, 't> SubstitutionReplacementContext<'a, 'b, 'c, 't> {
    /// Creates a new context for substitution function replacement.
    pub fn new(
        property_id: Option<PropertyIdRef<'b>>,
        substitution_functions: &'a ComputedSubstitutionFunctions,
        stylist: &'b Stylist,
        computed_context: &'b computed::Context<'c>,
        attribute_tracker: &'b mut AttributeTracker<'t>,
    ) -> Self {
        Self {
            property_id,
            substitution_functions,
            stylist,
            computed_context,
            attribute_tracker,
            seen: SmallVec::new(),
        }
    }

    /// Substitutes the references in a variable value. Returns `Err(..)` for invalid at computed
    /// time.
    pub fn substitute_variable_value(
        &mut self,
        variable_value: &'a VariableValue,
        attr_taint: Option<&mut AttrTaint>,
    ) -> Result<Substitution<'a>, ()> {
        self.substitute_chunk(
            variable_value,
            /* start = */ 0,
            /* end = */ variable_value.css.len(),
            variable_value.first_token_type,
            variable_value.last_token_type,
            &variable_value.references.refs,
            attr_taint,
        )
    }

    /// Substitutes one of a function's nested values, which is part of `source`.
    pub fn substitute_declaration_value(
        &mut self,
        value: &'a ArbitraryDeclarationValue,
        source: &'a VariableValue,
    ) -> Result<Substitution<'a>, ()> {
        self.substitute_chunk(
            source,
            value.start.get(),
            value.end.get(),
            value.first_token_type,
            value.last_token_type,
            &value.references.refs,
            /* attr_taint */ None,
        )
    }

    fn substitute_chunk(
        &mut self,
        source: &'a VariableValue,
        start: usize,
        end: usize,
        first_token_type: TokenSerializationType,
        last_token_type: TokenSerializationType,
        references: &'a [SubstitutionFunctionReference],
        mut attr_taint: Option<&mut AttrTaint>,
    ) -> Result<Substitution<'a>, ()> {
        if start == end {
            // Empty string. Easy.
            return Ok(Substitution::default());
        }
        let css = &source.css;
        // Easy case: no references involved.
        if references.is_empty() {
            let result = &css[start..end];
            return Ok(Substitution::new(
                Cow::Borrowed(result),
                first_token_type,
                last_token_type,
                Default::default(),
            ));
        }

        let mut substituted = ComputedValue::empty(&source.url_data);
        let mut next_token_type = first_token_type;
        let mut cur_pos = start;
        let mut attr_tainted = false;
        for reference in references {
            if reference.start != cur_pos {
                substituted.push(
                    &css[cur_pos..reference.start],
                    next_token_type,
                    reference.prev_token_type,
                    /* attr_taint */ None,
                )?;
            }

            let substitution = reference.function.replace(source, self)?;

            // Optimize the property: var(--...) case to avoid allocating at all.
            if reference.start == start && reference.end == end {
                if let Some(taint) = attr_taint.filter(|_| substitution.attr_tainted) {
                    taint.push(start, substitution.css.len());
                }
                return Ok(substitution);
            }

            substituted.push(
                &substitution.css,
                substitution.first_token_type,
                substitution.last_token_type,
                attr_taint
                    .as_deref_mut()
                    .filter(|_| substitution.attr_tainted),
            )?;
            attr_tainted |= substitution.attr_tainted;
            next_token_type = reference.next_token_type;
            cur_pos = reference.end;
        }
        // Push the rest of the value if needed.
        if cur_pos != end {
            substituted.push(
                &css[cur_pos..end],
                next_token_type,
                last_token_type,
                /* attr_taint */ None,
            )?;
        }
        Ok(Substitution::from_value(substituted, attr_tainted))
    }

    /// Build the context to use when parsing values during substitution.
    pub fn parser_context(&self, url_data: &'b UrlExtraData) -> ParserContext<'b> {
        ParserContext::new_with_property_id(
            Origin::Author,
            url_data,
            None,
            ParsingMode::DEFAULT,
            self.computed_context.quirks_mode,
            /* namespaces = */ Default::default(),
            None,
            None,
            AttrTaint::default(),
            self.property_id,
        )
    }
}

/// Result of var(), env(), and attr() substitution.
#[derive(Debug)]
pub struct SubstitutionResult<'a> {
    /// The resolved CSS string after substitution.
    pub css: Cow<'a, str>,
    /// Regions in the `css` string that are attr()-tainted, if any.
    pub attr_taint: AttrTaint,
}

/// Replace var(), env(), and attr() functions, returning the resulting CSS string.
pub fn substitute<'a>(
    variable_value: &'a VariableValue,
    property_id: Option<PropertyIdRef>,
    substitution_functions: &'a ComputedSubstitutionFunctions,
    stylist: &Stylist,
    computed_context: &computed::Context,
    attribute_tracker: &mut AttributeTracker,
) -> Result<SubstitutionResult<'a>, ()> {
    debug_assert!(variable_value.has_references());
    let mut attr_taint = AttrTaint::default();
    let v = SubstitutionReplacementContext::new(
        property_id,
        substitution_functions,
        stylist,
        computed_context,
        attribute_tracker,
    )
    .substitute_variable_value(variable_value, Some(&mut attr_taint))?;
    Ok(SubstitutionResult {
        css: v.css,
        attr_taint,
    })
}
