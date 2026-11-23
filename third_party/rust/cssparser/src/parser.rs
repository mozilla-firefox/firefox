/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::cow_rc_str::CowRcStr;
use crate::tokenizer::{SeenStatus, SourceLocation, SourcePosition, Token};
use smallvec::SmallVec;
use std::fmt;
use std::ops::BitOr;

/// A capture of the internal state of a `Parser` (including the position within the input),
/// obtained from the `Parser::position` method.
///
/// Can be used with the `Parser::reset` method to restore that state.
/// Should only be used with the `Parser` instance it came from.
#[derive(Clone, Default)]
pub struct ParserState {
    pub(crate) position: usize,
    pub(crate) current_line_start_position: u32,
    /// Current line number shifted by `Self::BLOCK_TYPE_BITS`, with the low 2 bits holding the
    /// `BlockType` discriminant of the block the last returned token opened (if any).
    line_number_and_block_type: u32,
}

impl fmt::Debug for ParserState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ParserState")
            .field("position", &self.position)
            .field(
                "current_line_start_position",
                &self.current_line_start_position,
            )
            .field("current_line_number", &self.line_number())
            .field("at_start_of", &self.at_start_of())
            .finish()
    }
}

impl ParserState {
    const BLOCK_TYPE_BITS: u32 = 2;
    const BLOCK_TYPE_MASK: u32 = (1 << Self::BLOCK_TYPE_BITS) - 1;

    /// The position from the start of the input, counted in UTF-8 bytes.
    #[inline]
    pub fn position(&self) -> SourcePosition {
        SourcePosition(self.position)
    }

    /// The line number and column number
    #[inline]
    pub fn source_location(&self) -> SourceLocation {
        SourceLocation {
            line: self.line_number(),
            column: (self.position as u32)
                .wrapping_sub(self.current_line_start_position)
                .wrapping_add(1),
        }
    }

    #[inline]
    pub(crate) fn line_number(&self) -> u32 {
        self.line_number_and_block_type >> Self::BLOCK_TYPE_BITS
    }

    #[inline]
    pub(crate) fn advance_line_number(&mut self, count: u32) {
        self.line_number_and_block_type += count << Self::BLOCK_TYPE_BITS;
    }

    #[inline]
    pub(crate) fn at_start_of(&self) -> Option<BlockType> {
        let block_type = (self.line_number_and_block_type & Self::BLOCK_TYPE_MASK) as u8;
        BlockType::from_byte(block_type)
    }

    #[inline]
    pub(crate) fn set_at_start_of(&mut self, block_type: BlockType) {
        debug_assert!(self.at_start_of().is_none());
        self.line_number_and_block_type |= block_type as u32;
        debug_assert_eq!(self.at_start_of(), Some(block_type));
    }

    #[inline]
    pub(crate) fn take_at_start_of(&mut self) -> Option<BlockType> {
        let block_type = self.at_start_of();
        self.line_number_and_block_type &= !Self::BLOCK_TYPE_MASK;
        block_type
    }
}

/// When parsing until a given token, sometimes the caller knows that parsing is going to restart
/// at some earlier point, and consuming until we find a top level delimiter is just wasted work.
///
/// In that case, callers can pass ParseUntilErrorBehavior::Stop to avoid doing all that wasted
/// work.
///
/// This is important for things like CSS nesting, where something like:
///
///   foo:is(..) {
///     ...
///   }
///
/// Would need to scan the whole {} block to find a semicolon, only for parsing getting restarted
/// as a qualified rule later.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParseUntilErrorBehavior {
    /// Consume until we see the relevant delimiter or the end of the stream.
    Consume,
    /// Eagerly error.
    Stop,
}

/// Details about a `BasicParseError`
#[derive(Clone, Debug, PartialEq)]
pub enum BasicParseErrorKind {
    /// An unexpected token was encountered.
    ///
    /// The token itself is deliberately not stored: it made this enum 32 bytes,
    /// which pushed `Result<&Token, BasicParseError>` (returned from every token
    /// fetch) to 40 bytes and therefore out of registers and into memory.
    /// Callers that want to name the token can recover it from the source text
    /// they already carry for the error message.
    UnexpectedToken,
    /// The end of the input was encountered unexpectedly.
    EndOfInput,
    /// An `@` rule was encountered that was invalid. See `UnexpectedToken` for
    /// why the rule name is not stored.
    AtRuleInvalid,
    /// The body of an '@' rule was invalid.
    AtRuleBodyInvalid,
    /// A qualified rule was encountered that was invalid.
    QualifiedRuleInvalid,
    /// We've gone over the nesting limit.
    TooManyNestedBlocks,
}

impl fmt::Display for BasicParseErrorKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BasicParseErrorKind::TooManyNestedBlocks => {
                write!(f, "nesting block limit reached")
            }
            BasicParseErrorKind::UnexpectedToken => write!(f, "unexpected token"),
            BasicParseErrorKind::EndOfInput => write!(f, "unexpected end of input"),
            BasicParseErrorKind::AtRuleInvalid => write!(f, "invalid @ rule encountered"),
            BasicParseErrorKind::AtRuleBodyInvalid => write!(f, "invalid @ rule body encountered"),
            BasicParseErrorKind::QualifiedRuleInvalid => {
                write!(f, "invalid qualified rule encountered")
            }
        }
    }
}

/// The fundamental parsing errors that can be triggered by built-in parsing routines.
#[derive(Clone, Debug, PartialEq)]
pub struct BasicParseError {
    /// Details of this error
    pub kind: BasicParseErrorKind,
}

impl BasicParseError {
    /// Create a new BasicParseError of the given kind.
    #[inline]
    pub fn new(kind: BasicParseErrorKind) -> Self {
        Self { kind }
    }

    /// Create a new BasicParseError for an unexpected token.
    #[inline]
    pub fn unexpected_token() -> Self {
        Self::new(BasicParseErrorKind::UnexpectedToken)
    }
}

impl<T> From<BasicParseError> for ParseError<T> {
    #[inline]
    fn from(this: BasicParseError) -> ParseError<T> {
        ParseError {
            kind: ParseErrorKind::Basic(this.kind),
        }
    }
}

/// Details of a `ParseError`
#[derive(Clone, Debug, PartialEq)]
pub enum ParseErrorKind<T> {
    /// A fundamental parse error from a built-in parsing routine.
    Basic(BasicParseErrorKind),
    /// A parse error reported by downstream consumer code.
    Custom(T),
}

impl<T> ParseErrorKind<T> {
    /// Like `std::convert::Into::into`
    pub fn into<U>(self) -> ParseErrorKind<U>
    where
        T: Into<U>,
    {
        match self {
            ParseErrorKind::Basic(basic) => ParseErrorKind::Basic(basic),
            ParseErrorKind::Custom(custom) => ParseErrorKind::Custom(custom.into()),
        }
    }
}

impl<E: fmt::Display> fmt::Display for ParseErrorKind<E> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ParseErrorKind::Basic(basic) => basic.fmt(f),
            ParseErrorKind::Custom(custom) => custom.fmt(f),
        }
    }
}

/// Extensible parse errors that can be encountered by client parsing implementations.
#[derive(Clone, Debug, PartialEq)]
pub struct ParseError<E> {
    /// Details of this error
    pub kind: ParseErrorKind<E>,
}

impl<T> ParseError<T> {
    /// Create a new ParseError from a basic error kind.
    #[inline]
    pub fn from_basic_kind(kind: BasicParseErrorKind) -> Self {
        Self {
            kind: ParseErrorKind::Basic(kind),
        }
    }

    /// Create a new ParseError for an unexpected token.
    #[inline]
    pub fn unexpected_token() -> Self {
        Self::from_basic_kind(BasicParseErrorKind::UnexpectedToken)
    }

    /// Create a new ParseError from a consumer-defined error.
    #[inline]
    pub fn custom<E: Into<T>>(error: E) -> Self {
        Self {
            kind: ParseErrorKind::Custom(error.into()),
        }
    }

    /// Extract the fundamental parse error from an extensible error.
    pub fn basic(self) -> BasicParseError {
        match self.kind {
            ParseErrorKind::Basic(kind) => BasicParseError { kind },
            ParseErrorKind::Custom(_) => panic!("Not a basic parse error"),
        }
    }

    /// Like `std::convert::Into::into`
    pub fn into<U>(self) -> ParseError<U>
    where
        T: Into<U>,
    {
        ParseError {
            kind: self.kind.into(),
        }
    }
}

impl<E: fmt::Display> fmt::Display for ParseError<E> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.kind.fmt(f)
    }
}

impl<E: fmt::Display + fmt::Debug> std::error::Error for ParseError<E> {}

/// A CSS parser that borrows its `&str` input, yields `Token`s, and keeps track of nested blocks
/// and functions.
pub struct Parser<'i> {
    pub(crate) input: &'i str,
    pub(crate) state: ParserState,
    cached_token: CachedToken<'i>,
    current_block_depth: u8,
    nested_block_limit: u8,
    /// For parsers from `parse_until` or `parse_nested_block`
    stop_before: Delimiters,
    pub(crate) arbitrary_substitution_functions: SeenStatus<'i>,
    pub(crate) source_map_url: Option<&'i str>,
    pub(crate) source_url: Option<&'i str>,
}

struct CachedToken<'i> {
    token: Token<'i>,
    start_position: SourcePosition,
    end_state: ParserState,
}

#[derive(Copy, Clone, PartialEq, Eq, Debug)]
#[repr(u8)]
pub(crate) enum BlockType {
    // NOTE: We rely on the discriminants being 1, 2, 3 for the `ParserState`
    // bit packing (and from_byte).
    Parenthesis = 1,
    SquareBracket,
    CurlyBracket,
}

impl BlockType {
    #[inline]
    fn from_byte(b: u8) -> Option<BlockType> {
        let ty = match b {
            1 => BlockType::Parenthesis,
            2 => BlockType::SquareBracket,
            3 => BlockType::CurlyBracket,
            _ => return None,
        };
        debug_assert_eq!(ty as u8, b);
        Some(ty)
    }

    fn closing(token: &Token) -> Option<BlockType> {
        Some(match *token {
            Token::CloseParenthesis => BlockType::Parenthesis,
            Token::CloseSquareBracket => BlockType::SquareBracket,
            Token::CloseCurlyBracket => BlockType::CurlyBracket,
            _ => return None,
        })
    }
}

/// A set of characters, to be used with the `Parser::parse_until*` methods.
///
/// The union of two sets can be obtained with the `|` operator. Example:
///
/// ```rust,ignore
/// input.parse_until_before(Delimiter::CurlyBracketBlock | Delimiter::Semicolon)
/// ```
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct Delimiters {
    bits: u16,
}

/// `Delimiters` constants.
#[allow(non_upper_case_globals, non_snake_case)]
pub mod Delimiter {
    use super::Delimiters;

    /// The empty delimiter set
    pub const None: Delimiters = Delimiters { bits: 0 };
    /// The delimiter set with only the `{` opening curly bracket
    pub const CurlyBracketBlock: Delimiters = Delimiters { bits: 1 << 1 };
    /// The delimiter set with only the `;` semicolon
    pub const Semicolon: Delimiters = Delimiters { bits: 1 << 2 };
    /// The delimiter set with only the `!` exclamation point
    pub const Bang: Delimiters = Delimiters { bits: 1 << 3 };
    /// The delimiter set with only the `,` comma
    pub const Comma: Delimiters = Delimiters { bits: 1 << 4 };
    /// The delimiter set with only the `:` colon
    pub const Colon: Delimiters = Delimiters { bits: 1 << 5 };
}

#[allow(non_upper_case_globals, non_snake_case)]
mod ClosingDelimiter {
    use super::Delimiters;

    pub const CloseCurlyBracket: Delimiters = Delimiters { bits: 1 << 6 };
    pub const CloseSquareBracket: Delimiters = Delimiters { bits: 1 << 7 };
    pub const CloseParenthesis: Delimiters = Delimiters { bits: 1 << 8 };
}

impl BitOr<Delimiters> for Delimiters {
    type Output = Delimiters;

    #[inline]
    fn bitor(self, other: Delimiters) -> Delimiters {
        Delimiters {
            bits: self.bits | other.bits,
        }
    }
}

impl Delimiters {
    #[inline]
    fn contains(self, other: Delimiters) -> bool {
        (self.bits & other.bits) != 0
    }

    #[inline]
    pub(crate) fn from_byte(byte: u8) -> Delimiters {
        const TABLE: [Delimiters; 256] = {
            let mut table = [Delimiter::None; 256];
            table[b';' as usize] = Delimiter::Semicolon;
            table[b'!' as usize] = Delimiter::Bang;
            table[b',' as usize] = Delimiter::Comma;
            table[b':' as usize] = Delimiter::Colon;
            table[b'{' as usize] = Delimiter::CurlyBracketBlock;
            table[b'}' as usize] = ClosingDelimiter::CloseCurlyBracket;
            table[b']' as usize] = ClosingDelimiter::CloseSquareBracket;
            table[b')' as usize] = ClosingDelimiter::CloseParenthesis;
            table
        };

        TABLE[byte as usize]
    }
}

/// Used in some `fn expect_*` methods
macro_rules! expect {
    ($parser: ident, $($branches: tt)+) => {
        {
            match *$parser.next()? {
                $($branches)+
                _ => {
                    return Err(BasicParseError::unexpected_token())
                }
            }
        }
    }
}

/// A list of arbitrary substitution functions. Should be lowercase ascii.
/// See https://drafts.csswg.org/css-values-5/#arbitrary-substitution
pub type ArbitrarySubstitutionFunctions<'a> = &'a [&'static str];

impl<'i> Parser<'i> {
    /// 75 nested blocks seems reasonable enough.
    const REASONABLE_NESTED_BLOCK_LIMIT: u8 = 75;

    /// Create a new parser for the given input.
    #[inline]
    pub fn new(input: &'i str) -> Self {
        Self {
            input,
            state: ParserState::default(),
            stop_before: Delimiter::None,
            nested_block_limit: Self::REASONABLE_NESTED_BLOCK_LIMIT,
            current_block_depth: 0,
            cached_token: CachedToken {
                token: Token::Semicolon,                    // Anything would do.
                start_position: SourcePosition(usize::MAX), // No token would match this cache.
                end_state: ParserState::default(),
            },
            arbitrary_substitution_functions: SeenStatus::DontCare,
            source_map_url: None,
            source_url: None,
        }
    }

    /// Sets a limit for how many nested blocks we're allowed to parse. This is useful to avoid
    /// running out of stack space. By default, it's set to `REASONABLE_NESTED_BLOCK_LIMIT`, but it
    /// can be overridden or cleared. A limit of 0 will be equivalent to no limit at all.
    pub fn set_nested_block_limit(&mut self, limit: u8) {
        self.nested_block_limit = limit;
    }

    /// Check whether the input is exhausted. That is, if `.next()` would return a token.
    ///
    /// This ignores whitespace and comments.
    #[inline]
    pub fn is_exhausted(&mut self) -> bool {
        self.expect_exhausted().is_ok()
    }

    /// Check whether the input is exhausted. That is, if `.next()` would return a token.
    /// Return a `Result` so that the `?` operator can be used: `input.expect_exhausted()?`
    ///
    /// This ignores whitespace and comments.
    #[inline]
    pub fn expect_exhausted(&mut self) -> Result<(), BasicParseError> {
        let start = self.state();
        let result = match self.next() {
            Err(BasicParseError {
                kind: BasicParseErrorKind::EndOfInput,
                ..
            }) => Ok(()),
            Err(e) => unreachable!("Unexpected error encountered: {:?}", e),
            Ok(_) => Err(BasicParseError::unexpected_token()),
        };
        self.reset(&start);
        result
    }

    /// Create a new unexpected token or EOF ParseError at the current location
    #[inline]
    pub fn new_error_for_next_token<E>(&mut self) -> ParseError<E> {
        match self.next() {
            Ok(_) => ParseError::unexpected_token(),
            Err(e) => e.into(),
        }
    }

    /// Return the current internal state of the parser (including position within the input).
    ///
    /// This state can later be restored with the `Parser::reset` method.
    #[inline]
    pub fn state(&self) -> ParserState {
        self.state.clone()
    }

    /// Like `next_byte`, but returns `None` if the next byte is one of the delimiters this
    /// parser was told to stop before.
    #[inline]
    pub(crate) fn next_byte_before_delimiter(&self) -> Option<u8> {
        let byte = self.next_byte()?;
        if self.stop_before.contains(Delimiters::from_byte(byte)) {
            return None;
        }
        Some(byte)
    }

    /// Restore the internal state of the parser (including position within the input)
    /// to what was previously saved by the `Parser::position` method.
    ///
    /// Should only be used with `SourcePosition` values from the same `Parser` instance.
    #[inline]
    pub fn reset(&mut self, state: &ParserState) {
        self.state = state.clone();
    }

    /// The old name of `try_parse`, which requires raw identifiers in the Rust 2018 edition.
    #[inline]
    pub fn r#try<F, T, E>(&mut self, thing: F) -> Result<T, E>
    where
        F: FnOnce(&mut Parser<'i>) -> Result<T, E>,
    {
        self.try_parse(thing)
    }

    /// Execute the given closure, passing it the parser.
    /// If the result (returned unchanged) is `Err`,
    /// the internal state of the parser  (including position within the input)
    /// is restored to what it was before the call.
    #[inline]
    pub fn try_parse<F, T, E>(&mut self, thing: F) -> Result<T, E>
    where
        F: FnOnce(&mut Parser<'i>) -> Result<T, E>,
    {
        let start = self.state();
        let result = thing(self);
        if result.is_err() {
            self.reset(&start)
        }
        result
    }

    /// Return the next token in the input that is neither whitespace or a comment,
    /// and advance the position accordingly.
    ///
    /// After returning a `Function`, `ParenthesisBlock`,
    /// `CurlyBracketBlock`, or `SquareBracketBlock` token,
    /// the next call will skip until after the matching `CloseParenthesis`,
    /// `CloseCurlyBracket`, or `CloseSquareBracket` token.
    ///
    /// See the `Parser::parse_nested_block` method to parse the content of functions or blocks.
    ///
    /// This only returns a closing token when it is unmatched (and therefore an error).
    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> Result<&Token<'i>, BasicParseError> {
        self.skip_whitespace();
        self.next_including_whitespace_and_comments()
    }

    /// Same as `Parser::next`, but does not skip whitespace tokens.
    pub fn next_including_whitespace(&mut self) -> Result<&Token<'i>, BasicParseError> {
        while let Token::Comment(..) = self.next_including_whitespace_and_comments()? {
            // Keep going
        }
        Ok(&self.cached_token.token)
    }

    /// Same as `Parser::next`, but does not skip whitespace or comment tokens.
    ///
    /// **Note**: This should only be used in contexts like a CSS pre-processor
    /// where comments are preserved.
    /// When parsing higher-level values, per the CSS Syntax specification,
    /// comments should always be ignored between tokens.
    pub fn next_including_whitespace_and_comments(
        &mut self,
    ) -> Result<&Token<'i>, BasicParseError> {
        self.skip_block_at_start();

        if self.next_byte_before_delimiter().is_none() {
            return Err(BasicParseError::new(BasicParseErrorKind::EndOfInput));
        }

        let token_start_position = self.position();
        let using_cached_token = self.cached_token.start_position == token_start_position;
        if using_cached_token {
            self.state = self.cached_token.end_state.clone();
        } else {
            let new_token = self.next_unchecked();
            self.cached_token = CachedToken {
                token: new_token,
                start_position: token_start_position,
                end_state: self.state.clone(),
            };
        }

        Ok(&self.cached_token.token)
    }

    /// Have the given closure parse something, then check the the input is exhausted.
    /// The result is overridden to an `Err(..)` if some input remains.
    ///
    /// This can help tell e.g. `color: green;` from `color: green 4px;`
    #[inline]
    pub fn parse_entirely<F, T, E>(&mut self, parse: F) -> Result<T, ParseError<E>>
    where
        F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        let result = parse(self)?;
        self.expect_exhausted()?;
        Ok(result)
    }

    /// Parse a list of comma-separated values, all with the same syntax.
    ///
    /// The given closure is called repeatedly with a "delimited" parser
    /// (see the `Parser::parse_until_before` method) so that it can over
    /// consume the input past a comma at this block/function nesting level.
    ///
    /// Successful results are accumulated in a vector.
    ///
    /// This method returns an`Err(..)` the first time that a closure call does,
    /// or if a closure call leaves some input before the next comma or the end
    /// of the input.
    #[inline]
    pub fn parse_comma_separated<F, T, E>(&mut self, parse_one: F) -> Result<Vec<T>, ParseError<E>>
    where
        F: FnMut(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        self.parse_comma_separated_internal(parse_one, /* ignore_errors = */ false)
    }

    /// Like `parse_comma_separated`, but ignores errors on unknown components,
    /// rather than erroring out in the whole list.
    ///
    /// Caller must deal with the fact that the resulting list might be empty,
    /// if there's no valid component on the list.
    #[inline]
    pub fn parse_comma_separated_ignoring_errors<F, T, E>(&mut self, parse_one: F) -> Vec<T>
    where
        F: FnMut(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        match self.parse_comma_separated_internal(parse_one, /* ignore_errors = */ true) {
            Ok(values) => values,
            Err(..) => unreachable!(),
        }
    }

    #[inline]
    fn parse_comma_separated_internal<F, T, E>(
        &mut self,
        mut parse_one: F,
        ignore_errors: bool,
    ) -> Result<Vec<T>, ParseError<E>>
    where
        F: FnMut(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        // Vec grows from 0 to 4 by default on first push().  So allocate with
        // capacity 1, so in the somewhat common case of only one item we don't
        // way overallocate.  Note that we always push at least one item if
        // parsing succeeds.
        let mut values = Vec::with_capacity(1);
        loop {
            self.skip_whitespace(); // Unnecessary for correctness, but may help try() in parse_one rewind less.
            match self.parse_until_before(Delimiter::Comma, &mut parse_one) {
                Ok(v) => values.push(v),
                Err(e) if !ignore_errors => return Err(e),
                Err(_) => {}
            }
            match self.next() {
                Err(_) => return Ok(values),
                Ok(&Token::Comma) => continue,
                Ok(_) => unreachable!(),
            }
        }
    }

    /// Parse the content of a block or function.
    ///
    /// This method panics if the last token yielded by this parser
    /// (from one of the `next*` methods)
    /// is not a on that marks the start of a block or function:
    /// a `Function`, `ParenthesisBlock`, `CurlyBracketBlock`, or `SquareBracketBlock`.
    ///
    /// The given closure is called with a "delimited" parser
    /// that stops at the end of the block or function (at the matching closing token).
    ///
    /// The result is overridden to an `Err(..)` if the closure leaves some input before that point.
    #[inline]
    pub fn parse_nested_block<F, T, E>(&mut self, parse: F) -> Result<T, ParseError<E>>
    where
        F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        parse_nested_block(self, parse)
    }

    /// Limit parsing to until a given delimiter or the end of the input. (E.g.
    /// a semicolon for a property value.)
    ///
    /// The given closure is called with a "delimited" parser
    /// that stops before the first character at this block/function nesting level
    /// that matches the given set of delimiters, or at the end of the input.
    ///
    /// The result is overridden to an `Err(..)` if the closure leaves some input before that point.
    #[inline]
    pub fn parse_until_before<F, T, E>(
        &mut self,
        delimiters: Delimiters,
        parse: F,
    ) -> Result<T, ParseError<E>>
    where
        F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        parse_until_before(self, delimiters, ParseUntilErrorBehavior::Consume, parse)
    }

    /// Like `parse_until_before`, but also consume the delimiter token.
    ///
    /// This can be useful when you don’t need to know which delimiter it was
    /// (e.g. if these is only one in the given set)
    /// or if it was there at all (as opposed to reaching the end of the input).
    #[inline]
    pub fn parse_until_after<F, T, E>(
        &mut self,
        delimiters: Delimiters,
        parse: F,
    ) -> Result<T, ParseError<E>>
    where
        F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
    {
        parse_until_after(self, delimiters, ParseUntilErrorBehavior::Consume, parse)
    }

    /// Parse a <whitespace-token> and return its value.
    #[inline]
    pub fn expect_whitespace(&mut self) -> Result<&'i str, BasicParseError> {
        match *self.next_including_whitespace()? {
            Token::WhiteSpace(value) => Ok(value),
            _ => Err(BasicParseError::unexpected_token()),
        }
    }

    /// Parse a <ident-token> and return the unescaped value.
    #[inline]
    pub fn expect_ident(&mut self) -> Result<&CowRcStr<'i>, BasicParseError> {
        expect! {self,
            Token::Ident(ref value) => Ok(value),
        }
    }

    /// expect_ident, but clone the CowRcStr
    #[inline]
    pub fn expect_ident_cloned(&mut self) -> Result<CowRcStr<'i>, BasicParseError> {
        self.expect_ident().cloned()
    }

    /// Parse a <ident-token> whose unescaped value is an ASCII-insensitive match for the given value.
    #[inline]
    pub fn expect_ident_matching(&mut self, expected_value: &str) -> Result<(), BasicParseError> {
        expect! {self,
            Token::Ident(ref value) if value.eq_ignore_ascii_case(expected_value) => Ok(()),
        }
    }

    /// Parse a <string-token> and return the unescaped value.
    #[inline]
    pub fn expect_string(&mut self) -> Result<&CowRcStr<'i>, BasicParseError> {
        expect! {self,
            Token::QuotedString(ref value) => Ok(value),
        }
    }

    /// expect_string, but clone the CowRcStr
    #[inline]
    pub fn expect_string_cloned(&mut self) -> Result<CowRcStr<'i>, BasicParseError> {
        self.expect_string().cloned()
    }

    /// Parse either a <ident-token> or a <string-token>, and return the unescaped value.
    #[inline]
    pub fn expect_ident_or_string(&mut self) -> Result<&CowRcStr<'i>, BasicParseError> {
        expect! {self,
            Token::Ident(ref value) => Ok(value),
            Token::QuotedString(ref value) => Ok(value),
        }
    }

    /// Parse a <url-token> and return the unescaped value.
    #[inline]
    pub fn expect_url(&mut self) -> Result<CowRcStr<'i>, BasicParseError> {
        expect! {self,
            Token::UnquotedUrl(ref value) => Ok(value.clone()),
            Token::Function(ref name) if name.eq_ignore_ascii_case("url") => {
                self.parse_nested_block(|input| {
                    input.expect_string().map_err(Into::into).cloned()
                })
                .map_err(ParseError::<()>::basic)
            }
        }
    }

    /// Parse either a <url-token> or a <string-token>, and return the unescaped value.
    #[inline]
    pub fn expect_url_or_string(&mut self) -> Result<CowRcStr<'i>, BasicParseError> {
        expect! {self,
            Token::UnquotedUrl(ref value) => Ok(value.clone()),
            Token::QuotedString(ref value) => Ok(value.clone()),
            Token::Function(ref name) if name.eq_ignore_ascii_case("url") => {
                self.parse_nested_block(|input| {
                    input.expect_string().map_err(Into::into).cloned()
                })
                .map_err(ParseError::<()>::basic)
            }
        }
    }

    /// Parse a <number-token> and return the integer value.
    #[inline]
    pub fn expect_number(&mut self) -> Result<f32, BasicParseError> {
        expect! {self,
            Token::Number { value, .. } => Ok(value),
        }
    }

    /// Parse a <number-token> that does not have a fractional part, and return the integer value.
    #[inline]
    pub fn expect_integer(&mut self) -> Result<i32, BasicParseError> {
        expect! {self,
            Token::Number { int_value: Some(int_value), .. } => Ok(int_value),
        }
    }

    /// Parse a <percentage-token> and return the value.
    /// `0%` and `100%` map to `0.0` and `1.0` (not `100.0`), respectively.
    #[inline]
    pub fn expect_percentage(&mut self) -> Result<f32, BasicParseError> {
        expect! {self,
            Token::Percentage { unit_value, .. } => Ok(unit_value),
        }
    }

    /// Parse a `:` <colon-token>.
    #[inline]
    pub fn expect_colon(&mut self) -> Result<(), BasicParseError> {
        expect! {self,
            Token::Colon => Ok(()),
        }
    }

    /// Parse a `;` <semicolon-token>.
    #[inline]
    pub fn expect_semicolon(&mut self) -> Result<(), BasicParseError> {
        expect! {self,
            Token::Semicolon => Ok(()),
        }
    }

    /// Parse a `,` <comma-token>.
    #[inline]
    pub fn expect_comma(&mut self) -> Result<(), BasicParseError> {
        expect! {self,
            Token::Comma => Ok(()),
        }
    }

    /// Parse a <delim-token> with the given value.
    #[inline]
    pub fn expect_delim(&mut self, expected_value: char) -> Result<(), BasicParseError> {
        expect! {self,
            Token::Delim(value) if value == expected_value => Ok(()),
        }
    }

    /// Parse a `{ /* ... */ }` curly brackets block.
    ///
    /// If the result is `Ok`, you can then call the `Parser::parse_nested_block` method.
    #[inline]
    pub fn expect_curly_bracket_block(&mut self) -> Result<(), BasicParseError> {
        expect! {self,
            Token::CurlyBracketBlock => Ok(()),
        }
    }

    /// Parse a `[ /* ... */ ]` square brackets block.
    ///
    /// If the result is `Ok`, you can then call the `Parser::parse_nested_block` method.
    #[inline]
    pub fn expect_square_bracket_block(&mut self) -> Result<(), BasicParseError> {
        expect! {self,
            Token::SquareBracketBlock => Ok(()),
        }
    }

    /// Parse a `( /* ... */ )` parenthesis block.
    ///
    /// If the result is `Ok`, you can then call the `Parser::parse_nested_block` method.
    #[inline]
    pub fn expect_parenthesis_block(&mut self) -> Result<(), BasicParseError> {
        expect! {self,
            Token::ParenthesisBlock => Ok(()),
        }
    }

    /// Parse a <function> token and return its name.
    ///
    /// If the result is `Ok`, you can then call the `Parser::parse_nested_block` method.
    #[inline]
    pub fn expect_function(&mut self) -> Result<&CowRcStr<'i>, BasicParseError> {
        expect! {self,
            Token::Function(ref name) => Ok(name),
        }
    }

    /// Parse a <function> token whose name is an ASCII-insensitive match for the given value.
    ///
    /// If the result is `Ok`, you can then call the `Parser::parse_nested_block` method.
    #[inline]
    pub fn expect_function_matching(&mut self, expected_name: &str) -> Result<(), BasicParseError> {
        expect! {self,
            Token::Function(ref name) if name.eq_ignore_ascii_case(expected_name) => Ok(()),
        }
    }

    /// Parse the input until exhaustion and check that it contains no “error” token.
    ///
    /// See `Token::is_parse_error`. This also checks nested blocks and functions recursively.
    #[inline]
    pub fn expect_no_error_token(&mut self) -> Result<(), BasicParseError> {
        loop {
            match self.next_including_whitespace_and_comments() {
                Ok(&Token::Function(_))
                | Ok(&Token::ParenthesisBlock)
                | Ok(&Token::SquareBracketBlock)
                | Ok(&Token::CurlyBracketBlock) => self
                    .parse_nested_block(|input| input.expect_no_error_token().map_err(Into::into))
                    .map_err(ParseError::<()>::basic)?,
                Ok(t) => {
                    // FIXME: maybe these should be separate variants of
                    // BasicParseError instead?
                    if t.is_parse_error() {
                        return Err(BasicParseError::unexpected_token());
                    }
                }
                Err(_) => return Ok(()),
            }
        }
    }
}

pub fn parse_until_before<'i, F, T, E>(
    parser: &mut Parser<'i>,
    delimiters: Delimiters,
    error_behavior: ParseUntilErrorBehavior,
    parse: F,
) -> Result<T, ParseError<E>>
where
    F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
{
    let old_stop_before = parser.stop_before;
    let delimiters = parser.stop_before | delimiters;
    parser.stop_before = delimiters;
    let result = parser.parse_entirely(parse);
    parser.stop_before = old_stop_before;
    if error_behavior == ParseUntilErrorBehavior::Stop && result.is_err() {
        return result;
    }
    parser.skip_block_at_start();
    // FIXME: have a special-purpose tokenizer method for this that does less work.
    while let Some(next_byte) = parser.next_byte() {
        if delimiters.contains(Delimiters::from_byte(next_byte)) {
            break;
        }
        parser.next_unchecked();
        parser.skip_block_at_start();
    }
    result
}

pub fn parse_until_after<'i, F, T, E>(
    parser: &mut Parser<'i>,
    delimiters: Delimiters,
    error_behavior: ParseUntilErrorBehavior,
    parse: F,
) -> Result<T, ParseError<E>>
where
    F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
{
    let result = parse_until_before(parser, delimiters, error_behavior, parse);
    if error_behavior == ParseUntilErrorBehavior::Stop && result.is_err() {
        return result;
    }
    if let Some(next_byte) = parser.next_byte() {
        let delimiter = Delimiters::from_byte(next_byte);
        if !parser.stop_before.contains(delimiter) {
            debug_assert!(delimiters.contains(delimiter));
            // We know this byte is ASCII.
            parser.advance(1);
            if next_byte == b'{' {
                parser.consume_until_end_of_block(BlockType::CurlyBracket);
            }
        }
    }
    result
}

pub fn parse_nested_block<'i, F, T, E>(
    parser: &mut Parser<'i>,
    parse: F,
) -> Result<T, ParseError<E>>
where
    F: FnOnce(&mut Parser<'i>) -> Result<T, ParseError<E>>,
{
    let block_type = parser.state.take_at_start_of().expect(
        "\
         A nested parser can only be created when a Function, \
         ParenthesisBlock, SquareBracketBlock, or CurlyBracketBlock \
         token was just consumed.\
         ",
    );
    if parser.current_block_depth >= parser.nested_block_limit && parser.nested_block_limit != 0 {
        return Err(ParseError::from_basic_kind(
            BasicParseErrorKind::TooManyNestedBlocks,
        ));
    }
    // Fine to use wrapping addition, overflow can only occur without a limit.
    parser.current_block_depth = parser.current_block_depth.wrapping_add(1);

    let old_stop_before = parser.stop_before;
    parser.stop_before = match block_type {
        BlockType::CurlyBracket => ClosingDelimiter::CloseCurlyBracket,
        BlockType::SquareBracket => ClosingDelimiter::CloseSquareBracket,
        BlockType::Parenthesis => ClosingDelimiter::CloseParenthesis,
    };
    let result = parser.parse_entirely(parse);
    parser.skip_block_at_start();
    parser.consume_until_end_of_block(block_type);
    parser.stop_before = old_stop_before;
    parser.current_block_depth = parser.current_block_depth.wrapping_sub(1);
    result
}

impl Parser<'_> {
    /// Consume tokens until the end of a block of the given type that we're at the start of,
    /// ignoring any `stop_before` delimiters.
    #[inline(never)]
    #[cold]
    pub(crate) fn consume_until_end_of_block(&mut self, block_type: BlockType) {
        let mut stack = SmallVec::<[BlockType; 16]>::new();
        stack.push(block_type);

        // FIXME: have a special-purpose tokenizer method for this that does less work.
        while !self.is_eof() {
            let token = self.next_unchecked();
            if let Some(nested_block_type) = self.state.take_at_start_of() {
                stack.push(nested_block_type);
                continue;
            }
            if let Some(b) = BlockType::closing(&token) {
                if *stack.last().unwrap() == b {
                    stack.pop();
                    if stack.is_empty() {
                        return;
                    }
                }
            }
        }
    }
}
