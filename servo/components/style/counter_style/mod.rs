/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The [`@counter-style`][counter-style] at-rule.
//!
//! [counter-style]: https://drafts.csswg.org/css-counter-styles/

use crate::derives::*;
use crate::error_reporting::ContextualParseError;
use crate::parser::{Parse, ParserContext};
use crate::shared_lock::{SharedRwLockReadGuard, ToCssWithGuard};
use crate::values::specified::Integer;
use crate::values::{AtomString, CustomIdent};
use crate::Atom;
use cssparser::{
    ascii_case_insensitive_phf_map, match_ignore_ascii_case, CowRcStr, Parser, RuleBodyParser,
    SourceLocation, Token,
};
use std::fmt::{self, Write};
use std::mem;
use std::num::Wrapping;
use style_traits::{
    Comma, CssStringWriter, CssWriter, KeywordsCollectFn, OneOrMoreSeparated, ParseError,
    SpecifiedValueInfo, StyleParseErrorKind, ToCss,
};

pub use crate::properties::counter_style::{DescriptorId, DescriptorParser, Descriptors};

/// https://drafts.csswg.org/css-counter-styles/#typedef-symbols-type
#[allow(missing_docs)]
#[cfg_attr(feature = "servo", derive(Deserialize, Serialize))]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum SymbolsType {
    Cyclic,
    Numeric,
    Alphabetic,
    Symbolic,
    Fixed,
}

/// <https://drafts.csswg.org/css-counter-styles/#typedef-counter-style>
///
/// Note that 'none' is not a valid name, but we include this (along with String) for space
/// efficiency when storing list-style-type.
#[derive(
    Clone, Debug, Eq, MallocSizeOf, PartialEq, ToComputedValue, ToCss, ToResolvedValue, ToShmem,
)]
#[repr(u8)]
pub enum CounterStyle {
    /// The 'none' value.
    None,
    /// `<counter-style-name>`
    Name(CustomIdent),
    /// `symbols()`
    #[css(function)]
    Symbols {
        /// The <symbols-type>, or symbolic if not specified.
        #[css(skip_if = "is_symbolic")]
        ty: SymbolsType,
        /// The actual symbols.
        symbols: Symbols,
    },
    /// A single string value, useful for `<list-style-type>`.
    String(AtomString),
}

#[inline]
fn is_symbolic(symbols_type: &SymbolsType) -> bool {
    *symbols_type == SymbolsType::Symbolic
}

impl CounterStyle {
    /// disc value
    pub fn disc() -> Self {
        CounterStyle::Name(CustomIdent(atom!("disc")))
    }

    /// decimal value
    pub fn decimal() -> Self {
        CounterStyle::Name(CustomIdent(atom!("decimal")))
    }

    /// Is this a bullet? (i.e. `list-style-type: disc|circle|square|disclosure-closed|disclosure-open`)
    #[inline]
    pub fn is_bullet(&self) -> bool {
        match self {
            CounterStyle::Name(CustomIdent(ref name)) => {
                name == &atom!("disc")
                    || name == &atom!("circle")
                    || name == &atom!("square")
                    || name == &atom!("disclosure-closed")
                    || name == &atom!("disclosure-open")
            },
            _ => false,
        }
    }
}

bitflags! {
    #[derive(Clone, Copy)]
    /// Flags to control parsing of counter styles.
    pub struct CounterStyleParsingFlags: u8 {
        /// Whether `none` is allowed.
        const ALLOW_NONE = 1 << 0;
        /// Whether a bare string is allowed.
        const ALLOW_STRING = 1 << 1;
    }
}

impl CounterStyle {
    /// Parse a counter style, and optionally none|string (for list-style-type).
    pub fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        flags: CounterStyleParsingFlags,
    ) -> Result<Self, ParseError<'i>> {
        use self::CounterStyleParsingFlags as Flags;
        let location = input.current_source_location();
        match input.next()? {
            Token::QuotedString(ref string) if flags.intersects(Flags::ALLOW_STRING) => {
                Ok(Self::String(AtomString::from(string.as_ref())))
            },
            Token::Ident(ref ident) => {
                if flags.intersects(Flags::ALLOW_NONE) && ident.eq_ignore_ascii_case("none") {
                    return Ok(Self::None);
                }
                Ok(Self::Name(counter_style_name_from_ident(ident, location)?))
            },
            Token::Function(ref name) if name.eq_ignore_ascii_case("symbols") => {
                input.parse_nested_block(|input| {
                    let symbols_type = input
                        .try_parse(SymbolsType::parse)
                        .unwrap_or(SymbolsType::Symbolic);
                    let symbols = Symbols::parse(context, input)?;
                    // There must be at least two symbols for alphabetic or
                    // numeric system.
                    if (symbols_type == SymbolsType::Alphabetic
                        || symbols_type == SymbolsType::Numeric)
                        && symbols.0.len() < 2
                    {
                        return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                    }
                    // Identifier is not allowed in symbols() function.
                    if symbols.0.iter().any(|sym| !sym.is_allowed_in_symbols()) {
                        return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                    }
                    Ok(Self::Symbols {
                        ty: symbols_type,
                        symbols,
                    })
                })
            },
            t => Err(location.new_unexpected_token_error(t.clone())),
        }
    }
}

impl SpecifiedValueInfo for CounterStyle {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        // XXX The best approach for implementing this is probably
        // having a CounterStyleName type wrapping CustomIdent, and
        // put the predefined list for that type in counter_style mod.
        // But that's a non-trivial change itself, so we use a simpler
        // approach here.
        macro_rules! predefined {
            ($($name:expr,)+) => {
                f(&["symbols", "none", $($name,)+])
            }
        }
        include!("predefined.rs");
    }
}

fn parse_counter_style_name<'i>(input: &mut Parser<'i, '_>) -> Result<CustomIdent, ParseError<'i>> {
    let location = input.current_source_location();
    let ident = input.expect_ident()?;
    counter_style_name_from_ident(ident, location)
}

/// This allows the reserved counter style names "decimal" and "disc".
fn counter_style_name_from_ident<'i>(
    ident: &CowRcStr<'i>,
    location: SourceLocation,
) -> Result<CustomIdent, ParseError<'i>> {
    macro_rules! predefined {
        ($($name: tt,)+) => {{
            ascii_case_insensitive_phf_map! {
                predefined -> Atom = {
                    $(
                        $name => atom!($name),
                    )+
                }
            }

            // This effectively performs case normalization only on predefined names.
            if let Some(lower_case) = predefined::get(&ident) {
                Ok(CustomIdent(lower_case.clone()))
            } else {
                // none is always an invalid <counter-style> value.
                CustomIdent::from_ident(location, ident, &["none"])
            }
        }}
    }
    include!("predefined.rs")
}

fn is_valid_name_definition(ident: &CustomIdent) -> bool {
    ident.0 != atom!("decimal")
        && ident.0 != atom!("disc")
        && ident.0 != atom!("circle")
        && ident.0 != atom!("square")
        && ident.0 != atom!("disclosure-closed")
        && ident.0 != atom!("disclosure-open")
}

/// Parse the prelude of an @counter-style rule
pub fn parse_counter_style_name_definition<'i, 't>(
    input: &mut Parser<'i, 't>,
) -> Result<CustomIdent, ParseError<'i>> {
    parse_counter_style_name(input).and_then(|ident| {
        if !is_valid_name_definition(&ident) {
            Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        } else {
            Ok(ident)
        }
    })
}

/// A @counter-style rule
#[derive(Clone, Debug, ToShmem)]
pub struct CounterStyleRule {
    name: CustomIdent,
    generation: Wrapping<u32>,
    descriptors: Descriptors,
    /// The parser location of the @counter-style rule.
    pub source_location: SourceLocation,
}

/// Parse the body (inside `{}`) of an @counter-style rule
pub fn parse_counter_style_body<'i, 't>(
    name: CustomIdent,
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
    location: SourceLocation,
) -> Result<CounterStyleRule, ParseError<'i>> {
    let start = input.current_source_location();
    let mut rule = CounterStyleRule::empty(name, location);
    {
        let mut parser = DescriptorParser {
            context,
            descriptors: &mut rule.descriptors,
        };
        let mut iter = RuleBodyParser::new(input, &mut parser);
        while let Some(declaration) = iter.next() {
            if let Err((error, slice)) = declaration {
                let location = error.location;
                let error = ContextualParseError::UnsupportedCounterStyleDescriptorDeclaration(
                    slice, error,
                );
                context.log_css_error(location, error)
            }
        }
    }
    let error = match *rule.resolved_system() {
        ref system @ System::Cyclic
        | ref system @ System::Fixed { .. }
        | ref system @ System::Symbolic
        | ref system @ System::Alphabetic
        | ref system @ System::Numeric
            if rule.descriptors.symbols.is_none() =>
        {
            let system = system.to_css_string();
            Some(ContextualParseError::InvalidCounterStyleWithoutSymbols(
                system,
            ))
        },
        ref system @ System::Alphabetic | ref system @ System::Numeric
            if rule.descriptors.symbols.as_ref().unwrap().0.len() < 2 =>
        {
            let system = system.to_css_string();
            Some(ContextualParseError::InvalidCounterStyleNotEnoughSymbols(
                system,
            ))
        },
        System::Additive if rule.descriptors.additive_symbols.is_none() => {
            Some(ContextualParseError::InvalidCounterStyleWithoutAdditiveSymbols)
        },
        System::Extends(_) if rule.descriptors.symbols.is_some() => {
            Some(ContextualParseError::InvalidCounterStyleExtendsWithSymbols)
        },
        System::Extends(_) if rule.descriptors.additive_symbols.is_some() => {
            Some(ContextualParseError::InvalidCounterStyleExtendsWithAdditiveSymbols)
        },
        _ => None,
    };
    if let Some(error) = error {
        context.log_css_error(start, error);
        Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
    } else {
        Ok(rule)
    }
}

impl ToCssWithGuard for CounterStyleRule {
    fn to_css(&self, _guard: &SharedRwLockReadGuard, dest: &mut CssStringWriter) -> fmt::Result {
        dest.write_str("@counter-style ")?;
        self.name.to_css(&mut CssWriter::new(dest))?;
        dest.write_str(" { ")?;
        self.descriptors.to_css(&mut CssWriter::new(dest))?;
        dest.write_char('}')
    }
}

// Implements the special checkers for some setters.
// See <https://drafts.csswg.org/css-counter-styles/#the-csscounterstylerule-interface>
impl CounterStyleRule {
    fn empty(name: CustomIdent, source_location: SourceLocation) -> Self {
        Self {
            name: name,
            generation: Wrapping(0),
            descriptors: Descriptors::default(),
            source_location,
        }
    }

    /// Expose the descriptors as read-only, since we rely on updating our generation when they
    /// change, and some setters need extra validation.
    pub fn descriptors(&self) -> &Descriptors {
        &self.descriptors
    }

    /// Set a descriptor, with the relevant validation. returns whether the descriptor changed.
    pub fn set_descriptor<'i>(
        &mut self,
        id: DescriptorId,
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<bool, ParseError<'i>> {
        // Some descriptors need a couple extra checks, deal with them specially.
        // TODO(emilio): Remove these, see https://github.com/w3c/csswg-drafts/issues/5717
        if id == DescriptorId::AdditiveSymbols
            && matches!(*self.resolved_system(), System::Extends(..))
        {
            // No additive symbols should be set for extends system.
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        let changed = match id {
            DescriptorId::System => {
                let system = input.parse_entirely(|i| System::parse(context, i))?;
                if !self.check_system(&system) {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                let new = Some(system);
                if self.descriptors.system == new {
                    return Ok(false);
                }
                self.descriptors.system = new;
                true
            },
            DescriptorId::Symbols => {
                let symbols = input.parse_entirely(|i| Symbols::parse(context, i))?;
                if !self.check_symbols(&symbols) {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                let new = Some(symbols);
                if self.descriptors.symbols == new {
                    return Ok(false);
                }
                self.descriptors.symbols = new;
                true
            },
            _ => self.descriptors.set(id, context, input)?,
        };
        if changed {
            self.generation += Wrapping(1);
        }
        Ok(changed)
    }

    /// Check that the system is effectively not changed. Only params of system descriptor is
    /// changeable.
    fn check_system(&self, value: &System) -> bool {
        mem::discriminant(self.resolved_system()) == mem::discriminant(value)
    }

    fn check_symbols(&self, value: &Symbols) -> bool {
        match *self.resolved_system() {
            // These two systems require at least two symbols.
            System::Numeric | System::Alphabetic => value.0.len() >= 2,
            // No symbols should be set for extends system.
            System::Extends(_) => false,
            _ => true,
        }
    }

    /// Get the name of the counter style rule.
    pub fn name(&self) -> &CustomIdent {
        &self.name
    }

    /// Set the name of the counter style rule. Caller must ensure that
    /// the name is valid.
    pub fn set_name(&mut self, name: CustomIdent) {
        debug_assert!(is_valid_name_definition(&name));
        self.name = name;
    }

    /// Get the current generation of the counter style rule.
    pub fn generation(&self) -> u32 {
        self.generation.0
    }

    /// Get the system of this counter style rule, default to
    /// `symbolic` if not specified.
    pub fn resolved_system(&self) -> &System {
        match self.descriptors.system {
            Some(ref system) => system,
            None => &System::Symbolic,
        }
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-system>
#[derive(Clone, Debug, MallocSizeOf, ToShmem, PartialEq)]
pub enum System {
    /// 'cyclic'
    Cyclic,
    /// 'numeric'
    Numeric,
    /// 'alphabetic'
    Alphabetic,
    /// 'symbolic'
    Symbolic,
    /// 'additive'
    Additive,
    /// 'fixed <integer>?'
    Fixed {
        /// '<integer>?'
        first_symbol_value: Option<Integer>,
    },
    /// 'extends <counter-style-name>'
    Extends(CustomIdent),
}

impl Parse for System {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        try_match_ident_ignore_ascii_case! { input,
            "cyclic" => Ok(System::Cyclic),
            "numeric" => Ok(System::Numeric),
            "alphabetic" => Ok(System::Alphabetic),
            "symbolic" => Ok(System::Symbolic),
            "additive" => Ok(System::Additive),
            "fixed" => {
                let first_symbol_value = input.try_parse(|i| Integer::parse(context, i)).ok();
                Ok(System::Fixed { first_symbol_value })
            },
            "extends" => {
                let other = parse_counter_style_name(input)?;
                Ok(System::Extends(other))
            },
        }
    }
}

impl ToCss for System {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match self {
            System::Cyclic => dest.write_str("cyclic"),
            System::Numeric => dest.write_str("numeric"),
            System::Alphabetic => dest.write_str("alphabetic"),
            System::Symbolic => dest.write_str("symbolic"),
            System::Additive => dest.write_str("additive"),
            System::Fixed { first_symbol_value } => {
                if let Some(value) = first_symbol_value {
                    dest.write_str("fixed ")?;
                    value.to_css(dest)
                } else {
                    dest.write_str("fixed")
                }
            },
            System::Extends(ref other) => {
                dest.write_str("extends ")?;
                other.to_css(dest)
            },
        }
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#typedef-symbol>
#[derive(
    Clone, Debug, Eq, MallocSizeOf, PartialEq, ToComputedValue, ToResolvedValue, ToCss, ToShmem,
)]
#[repr(u8)]
pub enum Symbol {
    /// <string>
    String(crate::OwnedStr),
    /// <custom-ident>
    Ident(CustomIdent),
    // Not implemented:
    // /// <image>
    // Image(Image),
}

impl Parse for Symbol {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        match *input.next()? {
            Token::QuotedString(ref s) => Ok(Symbol::String(s.as_ref().to_owned().into())),
            Token::Ident(ref s) => Ok(Symbol::Ident(CustomIdent::from_ident(location, s, &[])?)),
            ref t => Err(location.new_unexpected_token_error(t.clone())),
        }
    }
}

impl Symbol {
    /// Returns whether this symbol is allowed in symbols() function.
    pub fn is_allowed_in_symbols(&self) -> bool {
        match self {
            // Identifier is not allowed.
            &Symbol::Ident(_) => false,
            _ => true,
        }
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-negative>
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
pub struct Negative(pub Symbol, pub Option<Symbol>);

impl Parse for Negative {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(Negative(
            Symbol::parse(context, input)?,
            input.try_parse(|input| Symbol::parse(context, input)).ok(),
        ))
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-range>
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
pub struct CounterRange {
    /// The start of the range.
    pub start: CounterBound,
    /// The end of the range.
    pub end: CounterBound,
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-range>
///
/// Empty represents 'auto'
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
#[css(comma)]
pub struct CounterRanges(#[css(iterable, if_empty = "auto")] pub crate::OwnedSlice<CounterRange>);

/// A bound found in `CounterRanges`.
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
pub enum CounterBound {
    /// An integer bound.
    Integer(Integer),
    /// The infinite bound.
    Infinite,
}

impl Parse for CounterRanges {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input
            .try_parse(|input| input.expect_ident_matching("auto"))
            .is_ok()
        {
            return Ok(CounterRanges(Default::default()));
        }

        let ranges = input.parse_comma_separated(|input| {
            let start = parse_bound(context, input)?;
            let end = parse_bound(context, input)?;
            if let (CounterBound::Integer(ref s), CounterBound::Integer(ref e)) = (&start, &end) {
                // Rejects calc expressions that cannot be resolved at parse time,
                // since @counter-style descriptors require concrete values.
                let s = s.resolve();
                let e = e.resolve();
                if s.is_none() || e.is_none() || s.unwrap() > e.unwrap() {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
            }
            Ok(CounterRange { start, end })
        })?;

        Ok(CounterRanges(ranges.into()))
    }
}

fn parse_bound<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
) -> Result<CounterBound, ParseError<'i>> {
    if let Ok(integer) = input.try_parse(|input| Integer::parse(context, input)) {
        return Ok(CounterBound::Integer(integer));
    }
    input.expect_ident_matching("infinite")?;
    Ok(CounterBound::Infinite)
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-pad>
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
pub struct Pad(pub Integer, pub Symbol);

impl Parse for Pad {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let pad_with = input.try_parse(|input| Symbol::parse(context, input));
        let min_length = Integer::parse_non_negative(context, input)?;
        // Rejects calc expressions that cannot be resolved at parse time,
        // since @counter-style descriptors require concrete values.
        if min_length.resolve().is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        let pad_with = pad_with.or_else(|_| Symbol::parse(context, input))?;
        Ok(Pad(min_length, pad_with))
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-fallback>
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
pub struct Fallback(pub CustomIdent);

impl Parse for Fallback {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        Ok(Fallback(parse_counter_style_name(input)?))
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#descdef-counter-style-symbols>
#[derive(
    Clone, Debug, Eq, MallocSizeOf, PartialEq, ToComputedValue, ToResolvedValue, ToCss, ToShmem,
)]
#[repr(C)]
pub struct Symbols(
    #[css(iterable)]
    #[ignore_malloc_size_of = "Arc"]
    pub crate::ArcSlice<Symbol>,
);

impl Parse for Symbols {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let mut symbols = smallvec::SmallVec::<[_; 5]>::new();
        while let Ok(s) = input.try_parse(|input| Symbol::parse(context, input)) {
            symbols.push(s);
        }
        if symbols.is_empty() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(Symbols(crate::ArcSlice::from_iter(symbols.drain(..))))
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#descdef-counter-style-additive-symbols>
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
#[css(comma)]
pub struct AdditiveSymbols(#[css(iterable)] pub crate::OwnedSlice<AdditiveTuple>);

impl Parse for AdditiveSymbols {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let tuples = Vec::<AdditiveTuple>::parse(context, input)?;
        if tuples.iter().any(|t| t.weight.resolve().is_none()) {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        // FIXME maybe? https://github.com/w3c/csswg-drafts/issues/1220
        if tuples
            .windows(2)
            .any(|window| window[0].weight.get() <= window[1].weight.get())
        {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(AdditiveSymbols(tuples.into()))
    }
}

/// <integer> && <symbol>
#[derive(Clone, Debug, MallocSizeOf, ToCss, ToShmem, PartialEq)]
pub struct AdditiveTuple {
    /// <integer>
    pub weight: Integer,
    /// <symbol>
    pub symbol: Symbol,
}

impl OneOrMoreSeparated for AdditiveTuple {
    type S = Comma;
}

impl Parse for AdditiveTuple {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let symbol = input.try_parse(|input| Symbol::parse(context, input));
        let weight = Integer::parse_non_negative(context, input)?;
        let symbol = symbol.or_else(|_| Symbol::parse(context, input))?;
        Ok(Self { weight, symbol })
    }
}

/// <https://drafts.csswg.org/css-counter-styles/#counter-style-speak-as>
#[derive(Clone, Debug, MallocSizeOf, ToCss, PartialEq, ToShmem)]
pub enum SpeakAs {
    /// auto
    Auto,
    /// bullets
    Bullets,
    /// numbers
    Numbers,
    /// words
    Words,
    // /// spell-out, not supported, see bug 1024178
    // SpellOut,
    /// <counter-style-name>
    Other(CustomIdent),
}

impl Parse for SpeakAs {
    fn parse<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let mut is_spell_out = false;
        let result = input.try_parse(|input| {
            let ident = input.expect_ident().map_err(|_| ())?;
            match_ignore_ascii_case! { &*ident,
                "auto" => Ok(SpeakAs::Auto),
                "bullets" => Ok(SpeakAs::Bullets),
                "numbers" => Ok(SpeakAs::Numbers),
                "words" => Ok(SpeakAs::Words),
                "spell-out" => {
                    is_spell_out = true;
                    Err(())
                },
                _ => Err(()),
            }
        });
        if is_spell_out {
            // spell-out is not supported, but don’t parse it as a <counter-style-name>.
            // See bug 1024178.
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        result.or_else(|_| Ok(SpeakAs::Other(parse_counter_style_name(input)?)))
    }
}
