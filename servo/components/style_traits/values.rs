/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Helper types and traits for the handling of CSS values.

use app_units::Au;
use cssparser::ToCss as CssparserToCss;
use cssparser::{serialize_string, ParseError, Parser, Token, UnicodeRange};
use servo_arc::Arc;
use std::fmt::{self, Write};
use thin_vec::ThinVec;

/// Serialises a value according to its CSS representation.
///
/// This trait is implemented for `str` and its friends, serialising the string
/// contents as a CSS quoted string.
///
/// This trait is derivable with `#[derive(ToCss)]`, with the following behaviour:
/// * unit variants get serialised as the `snake-case` representation
///   of their name;
/// * unit variants whose name starts with "Moz" or "Webkit" are prepended
///   with a "-";
/// * if `#[css(comma)]` is found on a variant, its fields are separated by
///   commas, otherwise, by spaces;
/// * if `#[css(function)]` is found on a variant, the variant name gets
///   serialised like unit variants and its fields are surrounded by parentheses;
/// * if `#[css(iterable)]` is found on a function variant, that variant needs
///   to have a single member, and that member needs to be iterable. The
///   iterable will be serialized as the arguments for the function;
/// * an iterable field can also be annotated with `#[css(if_empty = "foo")]`
///   to print `"foo"` if the iterator is empty;
/// * if `#[css(dimension)]` is found on a variant, that variant needs
///   to have a single member. The variant would be serialized as a CSS
///   dimension token, like: <member><identifier>;
/// * if `#[css(skip)]` is found on a field, the `ToCss` call for that field
///   is skipped;
/// * if `#[css(skip_if = "function")]` is found on a field, the `ToCss` call
///   for that field is skipped if `function` returns true. This function is
///   provided the field as an argument;
/// * if `#[css(contextual_skip_if = "function")]` is found on a field, the
///   `ToCss` call for that field is skipped if `function` returns true. This
///   function is given all the fields in the current struct or variant as an
///   argument;
/// * `#[css(represents_keyword)]` can be used on bool fields in order to
///   serialize the field name if the field is true, or nothing otherwise.  It
///   also collects those keywords for `SpecifiedValueInfo`.
/// * `#[css(bitflags(single="", mixed="", validate_mixed="", overlapping_bits)]` can
///   be used to derive parse / serialize / etc on bitflags. The rules for parsing
///   bitflags are the following:
///
///     * `single` flags can only appear on their own. It's common that bitflags
///       properties at least have one such value like `none` or `auto`.
///     * `mixed` properties can appear mixed together, but not along any other
///       flag that shares a bit with itself. For example, if you have three
///       bitflags like:
///
///         FOO = 1 << 0;
///         BAR = 1 << 1;
///         BAZ = 1 << 2;
///         BAZZ = BAR | BAZ;
///
///       Then the following combinations won't be valid:
///
///         * foo foo: (every flag shares a bit with itself)
///         * bar bazz: (bazz shares a bit with bar)
///
///       But `bar baz` will be valid, as they don't share bits, and so would
///       `foo` with any other flag, or `bazz` on its own.
///    * `validate_mixed` can be used to reject invalid mixed combinations, and also to simplify
///      the type or add default ones if needed.
///    * `overlapping_bits` enables some tracking during serialization of mixed flags to avoid
///       serializing variants that can subsume other variants.
///       In the example above, you could do:
///         mixed="foo,bazz,bar,baz", overlapping_bits
///       to ensure that if bazz is serialized, bar and baz aren't, even though
///       their bits are set. Note that the serialization order is canonical,
///       and thus depends on the order you specify the flags in.
///
/// * finally, one can put `#[css(derive_debug)]` on the whole type, to
///   implement `Debug` by a single call to `ToCss::to_css`.
pub trait ToCss {
    /// Serialize `self` in CSS syntax, writing to `dest`.
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write;

    /// Serialize `self` in CSS syntax and return a string.
    ///
    /// (This is a convenience wrapper for `to_css` and probably should not be overridden.)
    #[inline]
    fn to_css_string(&self) -> String {
        let mut s = String::new();
        self.to_css(&mut CssWriter::new(&mut s)).unwrap();
        s
    }

    /// Serialize `self` in CSS syntax and return a CssString.
    ///
    /// (This is a convenience wrapper for `to_css` and probably should not be overridden.)
    #[inline]
    fn to_css_cssstring(&self) -> CssString {
        let mut s = CssString::new();
        self.to_css(&mut CssWriter::new(&mut s)).unwrap();
        s
    }
}

impl<'a, T> ToCss for &'a T
where
    T: ToCss + ?Sized,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        (*self).to_css(dest)
    }
}

impl ToCss for crate::owned_str::OwnedStr {
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_string(self, dest)
    }
}

impl ToCss for str {
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_string(self, dest)
    }
}

impl ToCss for String {
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_string(self, dest)
    }
}

impl<T> ToCss for Option<T>
where
    T: ToCss,
{
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.as_ref().map_or(Ok(()), |value| value.to_css(dest))
    }
}

impl ToCss for () {
    #[inline]
    fn to_css<W>(&self, _: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        Ok(())
    }
}

/// A writer tailored for serialising CSS.
///
/// Coupled with SequenceWriter, this allows callers to transparently handle
/// things like comma-separated values etc.
pub struct CssWriter<'w, W: 'w> {
    inner: &'w mut W,
    prefix: Option<&'static str>,
}

impl<'w, W> CssWriter<'w, W>
where
    W: Write,
{
    /// Creates a new `CssWriter`.
    #[inline]
    pub fn new(inner: &'w mut W) -> Self {
        Self {
            inner,
            prefix: Some(""),
        }
    }
}

impl<'w, W> Write for CssWriter<'w, W>
where
    W: Write,
{
    #[inline]
    fn write_str(&mut self, s: &str) -> fmt::Result {
        if s.is_empty() {
            return Ok(());
        }
        if let Some(prefix) = self.prefix.take() {
            // We are going to write things, but first we need to write
            // the prefix that was set by `SequenceWriter::item`.
            if !prefix.is_empty() {
                self.inner.write_str(prefix)?;
            }
        }
        self.inner.write_str(s)
    }

    #[inline]
    fn write_char(&mut self, c: char) -> fmt::Result {
        if let Some(prefix) = self.prefix.take() {
            // See comment in `write_str`.
            if !prefix.is_empty() {
                self.inner.write_str(prefix)?;
            }
        }
        self.inner.write_char(c)
    }
}

/// To avoid accidentally instantiating multiple monomorphizations of large
/// serialization routines, we define explicit concrete types and require
/// them in those routines. This avoids accidental mixing of String and
/// nsACString arguments in Gecko, which would cause code size to blow up.
#[cfg(feature = "gecko")]
pub type CssStringWriter = ::nsstring::nsACString;

/// String type that coerces to CssStringWriter, used when serialization code
/// needs to allocate a temporary string. In Gecko, this is backed by
/// nsCString, which allows the result to be passed directly to C++ without
/// conversion or copying. This makes it suitable not only for temporary
/// serialization but also for values that need to cross the Rust/C++ boundary.
#[cfg(feature = "gecko")]
pub type CssString = ::nsstring::nsCString;

/// String. The comments for the Gecko types explain the need for this abstraction.
#[cfg(feature = "servo")]
pub type CssStringWriter = String;

/// String. The comments for the Gecko types explain the need for this abstraction.
#[cfg(feature = "servo")]
pub type CssString = String;

/// Convenience wrapper to serialise CSS values separated by a given string.
pub struct SequenceWriter<'a, 'b: 'a, W: 'b> {
    inner: &'a mut CssWriter<'b, W>,
    separator: &'static str,
}

impl<'a, 'b, W> SequenceWriter<'a, 'b, W>
where
    W: Write + 'b,
{
    /// Returns whether this writer has written any item.
    pub fn has_written(&self) -> bool {
        // See comment in item()
        self.inner.prefix.is_none()
    }

    /// Create a new sequence writer.
    #[inline]
    pub fn new(inner: &'a mut CssWriter<'b, W>, separator: &'static str) -> Self {
        if inner.prefix.is_none() {
            // See comment in `item`.
            inner.prefix = Some("");
        }
        Self { inner, separator }
    }

    /// Serialize the CSS Value with the specific serialization function.
    #[inline]
    pub fn write_item<F>(&mut self, f: F) -> fmt::Result
    where
        F: FnOnce(&mut CssWriter<'b, W>) -> fmt::Result,
    {
        // Separate non-generic functions so that this code is not repeated
        // in every monomorphization with a different type `F` or `W`.
        // https://github.com/servo/servo/issues/26713
        fn before(
            prefix: &mut Option<&'static str>,
            separator: &'static str,
        ) -> Option<&'static str> {
            let old_prefix = *prefix;
            if old_prefix.is_none() {
                // If there is no prefix in the inner writer, a previous
                // call to this method produced output, which means we need
                // to write the separator next time we produce output again.
                *prefix = Some(separator);
            }
            old_prefix
        }
        fn after(
            old_prefix: Option<&'static str>,
            prefix: &mut Option<&'static str>,
            separator: &'static str,
        ) {
            match (old_prefix, *prefix) {
                (_, None) => {
                    // This call produced output and cleaned up after itself.
                },
                (None, Some(p)) => {
                    // Some previous call to `item` produced output,
                    // but this one did not, prefix should be the same as
                    // the one we set.
                    debug_assert_eq!(separator, p);
                    // We clean up here even though it's not necessary just
                    // to be able to do all these assertion checks.
                    *prefix = None;
                },
                (Some(old), Some(new)) => {
                    // No previous call to `item` produced output, and this one
                    // either.
                    debug_assert_eq!(old, new);
                },
            }
        }

        let old_prefix = before(&mut self.inner.prefix, self.separator);
        f(self.inner)?;
        after(old_prefix, &mut self.inner.prefix, self.separator);
        Ok(())
    }

    /// Serialises a CSS value, writing any separator as necessary.
    ///
    /// The separator is never written before any `item` produces any output,
    /// and is written in subsequent calls only if the `item` produces some
    /// output on its own again. This lets us handle `Option<T>` fields by
    /// just not printing anything on `None`.
    #[inline]
    pub fn item<T>(&mut self, item: &T) -> fmt::Result
    where
        T: ToCss,
    {
        self.write_item(|inner| item.to_css(inner))
    }

    /// Writes a string as-is (i.e. not escaped or wrapped in quotes)
    /// with any separator as necessary.
    ///
    /// See SequenceWriter::item.
    #[inline]
    pub fn raw_item(&mut self, item: &str) -> fmt::Result {
        self.write_item(|inner| inner.write_str(item))
    }
}

/// Type used as the associated type in the `OneOrMoreSeparated` trait on a
/// type to indicate that a serialized list of elements of this type is
/// separated by commas.
pub struct Comma;

/// Type used as the associated type in the `OneOrMoreSeparated` trait on a
/// type to indicate that a serialized list of elements of this type is
/// separated by spaces.
pub struct Space;

/// Type used as the associated type in the `OneOrMoreSeparated` trait on a
/// type to indicate that a serialized list of elements of this type is
/// separated by commas, but spaces without commas are also allowed when
/// parsing.
pub struct CommaWithSpace;

/// A trait satisfied by the types corresponding to separators.
pub trait Separator {
    /// The separator string that the satisfying separator type corresponds to.
    fn separator() -> &'static str;

    /// Parses a sequence of values separated by this separator.
    ///
    /// The given closure is called repeatedly for each item in the sequence.
    ///
    /// Successful results are accumulated in a vector.
    ///
    /// This method returns `Err(_)` the first time a closure does or if
    /// the separators aren't correct.
    fn parse<'i, 't, F, T, E>(
        parser: &mut Parser<'i, 't>,
        parse_one: F,
    ) -> Result<Vec<T>, ParseError<'i, E>>
    where
        F: for<'tt> FnMut(&mut Parser<'i, 'tt>) -> Result<T, ParseError<'i, E>>;
}

impl Separator for Comma {
    fn separator() -> &'static str {
        ", "
    }

    fn parse<'i, 't, F, T, E>(
        input: &mut Parser<'i, 't>,
        parse_one: F,
    ) -> Result<Vec<T>, ParseError<'i, E>>
    where
        F: for<'tt> FnMut(&mut Parser<'i, 'tt>) -> Result<T, ParseError<'i, E>>,
    {
        input.parse_comma_separated(parse_one)
    }
}

impl Separator for Space {
    fn separator() -> &'static str {
        " "
    }

    fn parse<'i, 't, F, T, E>(
        input: &mut Parser<'i, 't>,
        mut parse_one: F,
    ) -> Result<Vec<T>, ParseError<'i, E>>
    where
        F: for<'tt> FnMut(&mut Parser<'i, 'tt>) -> Result<T, ParseError<'i, E>>,
    {
        input.skip_whitespace(); // Unnecessary for correctness, but may help try_parse() rewind less.
        let mut results = vec![parse_one(input)?];
        loop {
            input.skip_whitespace(); // Unnecessary for correctness, but may help try_parse() rewind less.
            if let Ok(item) = input.try_parse(&mut parse_one) {
                results.push(item);
            } else {
                return Ok(results);
            }
        }
    }
}

impl Separator for CommaWithSpace {
    fn separator() -> &'static str {
        ", "
    }

    fn parse<'i, 't, F, T, E>(
        input: &mut Parser<'i, 't>,
        mut parse_one: F,
    ) -> Result<Vec<T>, ParseError<'i, E>>
    where
        F: for<'tt> FnMut(&mut Parser<'i, 'tt>) -> Result<T, ParseError<'i, E>>,
    {
        input.skip_whitespace(); // Unnecessary for correctness, but may help try_parse() rewind less.
        let mut results = vec![parse_one(input)?];
        loop {
            input.skip_whitespace(); // Unnecessary for correctness, but may help try_parse() rewind less.
            let comma_location = input.current_source_location();
            let comma = input.try_parse(|i| i.expect_comma()).is_ok();
            input.skip_whitespace(); // Unnecessary for correctness, but may help try_parse() rewind less.
            if let Ok(item) = input.try_parse(&mut parse_one) {
                results.push(item);
            } else if comma {
                return Err(comma_location.new_unexpected_token_error(Token::Comma));
            } else {
                break;
            }
        }
        Ok(results)
    }
}

/// Marker trait on T to automatically implement ToCss for Vec<T> when T's are
/// separated by some delimiter `delim`.
pub trait OneOrMoreSeparated {
    /// Associated type indicating which separator is used.
    type S: Separator;
}

impl OneOrMoreSeparated for UnicodeRange {
    type S = Comma;
}

impl<T> ToCss for Vec<T>
where
    T: ToCss + OneOrMoreSeparated,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        let mut iter = self.iter();
        iter.next().unwrap().to_css(dest)?;
        for item in iter {
            dest.write_str(<T as OneOrMoreSeparated>::S::separator())?;
            item.to_css(dest)?;
        }
        Ok(())
    }
}

impl<T> ToCss for Box<T>
where
    T: ?Sized + ToCss,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        (**self).to_css(dest)
    }
}

impl<T> ToCss for Arc<T>
where
    T: ?Sized + ToCss,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        (**self).to_css(dest)
    }
}

impl ToCss for Au {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.to_f64_px().to_css(dest)?;
        dest.write_str("px")
    }
}

macro_rules! impl_to_css_for_predefined_type {
    ($name: ty) => {
        impl<'a> ToCss for $name {
            fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
            where
                W: Write,
            {
                ::cssparser::ToCss::to_css(self, dest)
            }
        }
    };
}

impl_to_css_for_predefined_type!(f32);
impl_to_css_for_predefined_type!(i8);
impl_to_css_for_predefined_type!(i32);
impl_to_css_for_predefined_type!(u8);
impl_to_css_for_predefined_type!(u16);
impl_to_css_for_predefined_type!(u32);
impl_to_css_for_predefined_type!(::cssparser::Token<'a>);
impl_to_css_for_predefined_type!(::cssparser::UnicodeRange);

/// Helper types for the handling of specified values.
pub mod specified {
    use crate::ParsingMode;

    /// Whether to allow negative lengths or not.
    #[repr(u8)]
    #[derive(
        Clone, Copy, Debug, Deserialize, Eq, MallocSizeOf, PartialEq, PartialOrd, Serialize, ToShmem,
    )]
    pub enum AllowedNumericType {
        /// Allow all kind of numeric values.
        All,
        /// Allow only non-negative numeric values.
        NonNegative,
        /// Allow only numeric values greater or equal to 1.0.
        AtLeastOne,
        /// Allow only numeric values from 0 to 1.0.
        ZeroToOne,
    }

    impl Default for AllowedNumericType {
        #[inline]
        fn default() -> Self {
            AllowedNumericType::All
        }
    }

    impl AllowedNumericType {
        /// Whether the value fits the rules of this numeric type.
        #[inline]
        pub fn is_ok(&self, parsing_mode: ParsingMode, val: f32) -> bool {
            if parsing_mode.allows_all_numeric_values() {
                return true;
            }
            match *self {
                AllowedNumericType::All => true,
                AllowedNumericType::NonNegative => val >= 0.0,
                AllowedNumericType::AtLeastOne => val >= 1.0,
                AllowedNumericType::ZeroToOne => val >= 0.0 && val <= 1.0,
            }
        }

        /// Clamp the value following the rules of this numeric type.
        #[inline]
        pub fn clamp(&self, val: f32) -> f32 {
            match *self {
                AllowedNumericType::All => val,
                AllowedNumericType::NonNegative => val.max(0.),
                AllowedNumericType::AtLeastOne => val.max(1.),
                AllowedNumericType::ZeroToOne => val.max(0.).min(1.),
            }
        }
    }
}

/// A single segment of an unparsed Typed OM value.
///
/// This corresponds to the `CSSUnparsedSegment` union in the Typed OM
/// specification. Unparsed values are represented as a list of string
/// fragments and variable references.
#[derive(Clone, Debug)]
#[repr(C)]
pub enum UnparsedSegment {
    /// A string fragment.
    ///
    /// This corresponds to the string branch of `CSSUnparsedSegment` and is
    /// used for the non-variable parts of a `CSSUnparsedValue`.
    String(CssString),

    /// A `var()` reference segment.
    ///
    /// This corresponds to `CSSVariableReferenceValue` in the Typed OM
    /// specification.
    VariableReference(VariableReferenceValue),
}

/// An unparsed value used by the Typed OM.
///
/// This corresponds to `CSSUnparsedValue` in the Typed OM specification. It
/// is used for values that cannot be reified into a more specific
/// property-agnostic representation and therefore need to preserve their
/// token-like structure as a sequence of string fragments and variable
/// references.
///
/// The underlying list of segments corresponds to the `[[tokens]]` internal
/// slot of `CSSUnparsedValue`.
///
/// This is represented as a type alias over `ThinVec<UnparsedSegment>` rather
/// than a dedicated struct. This avoids the need for additional wrapper types
/// when embedding unparsed values within other structures, while still
/// allowing recursive representations via the segment list.
pub type UnparsedValue = ThinVec<UnparsedSegment>;

/// A variable reference inside an unparsed Typed OM value.
///
/// This corresponds to `CSSVariableReferenceValue` in the Typed OM
/// specification.
#[derive(Clone, Debug)]
#[repr(C)]
pub struct VariableReferenceValue {
    /// The referenced custom property name.
    ///
    /// This corresponds to the `variable` attribute of
    /// `CSSVariableReferenceValue`.
    pub variable: CssString,

    /// The fallback value, if present.
    ///
    /// This corresponds to the `fallback` attribute of
    /// `CSSVariableReferenceValue`. When `has_fallback` is false, this value
    /// must be ignored. When `has_fallback` is true, this contains the
    /// fallback tokens (which may be empty).
    pub fallback: UnparsedValue,

    /// Whether a fallback was explicitly provided.
    ///
    /// This is needed to distinguish between the absence of a fallback
    /// (`var(--a)`) and an explicitly empty fallback (`var(--a,)`), which are
    /// observable via Typed OM.
    pub has_fallback: bool,
}

/// A keyword value used by the Typed OM.
///
/// This corresponds to `CSSKeywordValue` in the Typed OM specification.
/// The keyword is stored as a `CssString` so it can be represented and
/// transferred independently of any specific property (e.g. `"none"`,
/// `"block"`, `"thin"`).
#[derive(Clone, Debug)]
#[repr(C)]
pub struct KeywordValue(pub CssString);

/// A single numeric value with an associated unit.
///
/// This corresponds to `CSSUnitValue` in the Typed OM specification. The
/// numeric component is stored separately from the textual unit identifier.
#[derive(Clone, Debug)]
#[repr(C)]
pub struct UnitValue {
    /// The numeric component of the value.
    pub value: f32,

    /// The textual unit string (e.g. `"px"`, `"em"`, `"%"`, `"deg"`).
    pub unit: CssString,
}

/// A sum of numeric values.
///
/// This corresponds to `CSSMathSum` in the Typed OM specification. A sum
/// value represents an expression such as `10px + 2em`. Each entry is itself
/// a `NumericValue`, allowing nested sums if needed.
#[derive(Clone, Debug)]
#[repr(C)]
pub struct MathSum {
    /// The list of numeric terms that make up the sum.
    pub values: ThinVec<NumericValue>,
}

/// A numeric value used by the Typed OM.
///
/// This corresponds to `CSSNumericValue` and its subclasses in the Typed OM
/// specification. It represents numbers that can appear in CSS values,
/// including both simple unit quantities and composite expressions.
///
/// Unlike the parser-level representation, `NumericValue` is property-agnostic
/// and suitable for conversion to or from the `CSSNumericValue` family of DOM
/// objects.
#[derive(Clone, Debug)]
#[repr(C)]
pub enum NumericValue {
    /// A single numeric value with a concrete unit.
    ///
    /// This corresponds to `CSSUnitValue`.
    Unit(UnitValue),

    /// A sum of numeric values.
    ///
    /// This corresponds to `CSSMathSum`.
    Sum(MathSum),
}

/// A property-agnostic representation of a value, used by Typed OM.
///
/// `TypedValue` is the internal counterpart of the various `CSSStyleValue`
/// subclasses defined by the Typed OM specification. It captures values that
/// can be represented independently of any particular property.
#[derive(Clone, Debug)]
#[repr(C)]
pub enum TypedValue {
    /// An unparsed value consisting of string fragments and variable
    /// references.
    ///
    /// This corresponds to `CSSUnparsedValue` in the Typed OM specification.
    Unparsed(UnparsedValue),

    /// A keyword value such as `"block"`, `"none"`, or `"thin"`.
    ///
    /// This corresponds to `CSSKeywordValue` in the Typed OM specification.
    /// Keywords are represented as a standalone `KeywordValue` so they can
    /// be carried and compared independently of any particular property.
    Keyword(KeywordValue),

    /// A numeric value such as a length, angle, time, or a sum thereof.
    ///
    /// This corresponds to the `CSSNumericValue` hierarchy in the Typed OM
    /// specification, including `CSSUnitValue` and `CSSMathSum`.
    Numeric(NumericValue),
}

/// A list of property-agnostic values used by the Typed OM.
///
/// `TypedValueList` is the internal counterpart of CSS value lists exposed by
/// Typed OM. It stores one or more [`TypedValue`] items in source order and
/// is used when a value reifies to multiple property-agnostic components.
#[derive(Clone, Debug)]
#[repr(C)]
pub struct TypedValueList {
    /// The list of reified values.
    pub values: ThinVec<TypedValue>,
}

/// Reifies a value into its Typed OM representation.
///
/// This trait is the Typed OM analogue of [`ToCss`]. Instead of serializing
/// values into CSS syntax, it converts them into [`TypedValue`]s that can be
/// exposed to the DOM as `CSSStyleValue` subclasses.
///
/// Most consumers should use [`ToTyped::to_typed_value`] or
/// [`ToTyped::to_typed_value_list`], depending on whether they need a single
/// reified value or the full list of reified values.
///
/// This trait is derivable with `#[derive(ToTyped)]`. The derived
/// implementation currently supports:
///
/// * Keyword enums: Enums whose variants are all unit variants are
///   automatically reified as [`TypedValue::Keyword`], using the same
///   serialization logic as [`ToCss`].
///
/// * Structs and data-carrying variants: By default, the derive attempts to
///   call `.to_typed()` recursively on supported fields or variant payloads,
///   producing [`TypedValue`]s when possible. This recursion can be disabled
///   with `#[typed(skip_derive_fields)]`.
///
/// * Other cases: If no automatic mapping is defined, or recursion is
///   explicitly disabled, the derived implementation falls back to the
///   default method (which returns `Err(())`, and thus `to_typed_value()`
///   returns `None`).
///
/// Over time, the derive may be extended to handle additional CSS value
/// categories such as numeric, color, and transform types.
///
/// Summary of derive attributes recognized by `#[derive(ToTyped)]`:
///
/// * `#[typed(skip_derive_fields)]` on the type disables recursion for
///   structs and data-carrying enum variants.
///
/// * `#[css(skip)]`, `#[typed(skip)]`, or `#[typed(todo)]` on a variant cause
///   that variant to be treated as unsupported (the derived implementation
///   returns `Err(())`).
///
/// * `#[css(skip)]` on a field causes that field to be ignored during
///   reification.
///
/// * `#[typed(skip_if = "...")]` on a field conditionally disables reification
///   for that field. If the provided function returns `true` for the field
///   value, the field is ignored.
///
/// * `#[css(keyword = "...")]` on a unit variant overrides the keyword that
///   would otherwise be derived from the Rust identifier.
///
/// * `#[css(comma)]` on the variant indicates that supported fields may reify
///   to multiple separate values. When this attribute is present, multiple
///   [`TypedValue`] items may be produced, unless
///   `#[typed(no_multiple_values)]` is also present. If multiple values are
///   not allowed and the derived implementation would produce more than one
///   item, it returns `Err(())`.
///
/// * `#[typed(no_multiple_values)]` on a variant prevents it from reifying to
///   multiple [`TypedValue`] items, even if `#[css(comma)]` is present.
///
/// * `#[css(iterable)]` on a field indicates that the field represents a list
///   of values. Each item in the iterable is reified individually by calling
///   `ToTyped::to_typed` on the element type.
///
/// * `#[css(if_empty = "...")]` on an iterable field specifies a keyword
///   value that should be produced when the iterable is empty.
pub trait ToTyped {
    /// Attempt to convert `self` into one or more [`TypedValue`] items.
    ///
    /// Implementations append any resulting values to `dest`. This is the
    /// low-level entry point used by the Typed OM reification infrastructure.
    /// Most callers should prefer [`ToTyped::to_typed_value`] or
    /// [`ToTyped::to_typed_value_list`].
    ///
    /// Returning `Err(())` indicates that the value cannot be represented as
    /// a property-agnostic Typed OM value.
    fn to_typed(&self, _dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        Err(())
    }

    /// Attempt to convert `self` into a [`TypedValue`].
    ///
    /// Returns the first reified value as `Some(TypedValue)` if the value can
    /// be reified into a property-agnostic CSSStyleValue subclass. Returns
    /// `None` if the value is unrepresentable, in which case consumers
    /// produce a property-tied CSSStyleValue instead.
    fn to_typed_value(&self) -> Option<TypedValue> {
        let mut dest = ThinVec::new();
        self.to_typed(&mut dest).ok()?;
        dest.into_iter().next()
    }

    /// Attempt to convert `self` into a [`TypedValueList`].
    ///
    /// Returns `Some(TypedValueList)` if the value can be reified into one or
    /// more property-agnostic Typed OM values. Returns `None` if the value is
    /// unrepresentable, in which case consumers produce a property-tied
    /// `CSSStyleValue` instead.
    fn to_typed_value_list(&self) -> Option<TypedValueList> {
        let mut dest = ThinVec::new();
        self.to_typed(&mut dest).ok()?;
        Some(TypedValueList { values: dest })
    }
}

impl<'a, T> ToTyped for &'a T
where
    T: ToTyped + ?Sized,
{
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        (*self).to_typed(dest)
    }
}

impl<T> ToTyped for Box<T>
where
    T: ?Sized + ToTyped,
{
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        (**self).to_typed(dest)
    }
}

impl<T> ToTyped for Arc<T>
where
    T: ?Sized + ToTyped,
{
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        (**self).to_typed(dest)
    }
}

impl ToTyped for Au {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        let value = self.to_f32_px();
        let unit = CssString::from("px");
        dest.push(TypedValue::Numeric(NumericValue::Unit(UnitValue {
            value,
            unit,
        })));
        Ok(())
    }
}

macro_rules! impl_to_typed_for_predefined_type {
    ($name: ty) => {
        impl<'a> ToTyped for $name {
            fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
                dest.push(TypedValue::Numeric(NumericValue::Unit(UnitValue {
                    value: *self as f32,
                    unit: CssString::from("number"),
                })));
                Ok(())
            }
        }
    };
}

impl_to_typed_for_predefined_type!(f32);
impl_to_typed_for_predefined_type!(i8);
impl_to_typed_for_predefined_type!(i32);
