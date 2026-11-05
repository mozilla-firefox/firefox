/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Common [values][values] used in CSS.
//!
//! [values]: https://drafts.csswg.org/css-values/

#![deny(missing_docs)]

use crate::Atom;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::typed_om::{KeywordValue, NumericType, NumericValue, ToTyped, TypedValue, UnitValue};
use crate::values::distance::{ComputeSquaredDistance, SquaredDistance};
use crate::values::generics::position::IsTreeScoped;
pub use cssparser::{CowRcStr, Parser, serialize_identifier, serialize_name};
pub use cssparser::{SourceLocation, Token};
use num_traits::Zero;
use precomputed_hash::PrecomputedHash;
use selectors::parser::SelectorParseErrorKind;
use std::fmt::{self, Debug, Write};
use style_traits::{CssString, CssWriter, ParseError, StyleParseErrorKind, ToCss};
use thin_vec::ThinVec;
use to_shmem::impl_trivial_to_shmem;

pub use crate::url::CssUrl;

pub mod animated;
pub mod computed;
pub mod distance;
pub mod generics;
pub mod resolved;
pub mod specified;
pub mod tagged_numeric;

/// A CSS float value.
pub type CSSFloat = f32;

/// Normalizes a float value to zero after a set of operations that might turn
/// it into NaN.
#[inline]
pub fn normalize(v: CSSFloat) -> CSSFloat {
    if v.is_nan() { 0.0 } else { v }
}

/// Computes the minimum value of the two floats. The CSS Values and Units definition
/// for min() considers -0 to be less than +0 (whereas Rust considers them equal).
/// https://drafts.csswg.org/css-values-4/#css-signed-zero
#[inline]
pub fn calc_min(a: CSSFloat, b: CSSFloat) -> CSSFloat {
    match (a.is_sign_negative(), b.is_sign_negative()) {
        (true, false) => a,
        (false, true) => b,
        _ => a.min(b),
    }
}

/// Computes the maximum value of the two floats. The CSS Values and Units definition
/// for max() considers +0 to be greater than -0 (whereas Rust considers them equal).
/// https://drafts.csswg.org/css-values-4/#css-signed-zero
#[inline]
pub fn calc_max(a: CSSFloat, b: CSSFloat) -> CSSFloat {
    match (a.is_sign_negative(), b.is_sign_negative()) {
        (true, false) => b,
        (false, true) => a,
        _ => a.max(b),
    }
}

/// Computes the sign of the given value. The CSS Values and Units definition for
/// sign() returns +0 or -0 for an input of +0 or -0, respectively (whereas the
/// Rust f32::signum() function returns +1 or -1, respectively).
/// https://drafts.csswg.org/css-values-4/#funcdef-sign
#[inline]
pub fn calc_sign(value: CSSFloat) -> CSSFloat {
    if value.is_nan() {
        f32::NAN
    } else if value.is_zero() {
        value
    } else if value.is_sign_negative() {
        -1.0
    } else {
        1.0
    }
}

/// Generates a random integer given a base value and limit.
/// https://drafts.csswg.org/css-values-5/#generate-a-random-integer
pub fn calc_random_integer(base: f32, limit: f32) -> f32 {
    (base * limit).floor()
}

/// Evaluates a random() function given a base value and its arguments.
/// https://drafts.csswg.org/css-values-5/#random-evaluation
pub fn calc_random(base: f32, min: f32, max: f32, step: Option<f32>) -> f32 {
    if base.is_nan() || min.is_nan() || max.is_nan() || step.is_some_and(f32::is_nan) {
        return f32::NAN;
    }

    // Clamps a <number> into the range of a random base value, using the highest
    // f32 strictly less than one. The random base value has a half-open range of
    // [0, 1), but the `fixed <number>` format of <random-key> accepts the closed
    // range of [0, 1]. So if an author specifies `fixed 1`, this value is used
    // instead as the random base value.
    let base = normalize(base.max(0.).min((1.0f32).next_down()));

    // "In random(A, B), if A is infinite, the result is infinite."
    if min.is_infinite() {
        return min;
    }

    // "If A is finite, but the difference between A and B is either infinite or
    // large enough to be treated as infinite in the user agent, the result is
    // NaN."
    if !(max - min).is_finite() {
        return f32::NAN;
    }

    // "If the maximum value is less than the minimum value, it behaves as if
    // it's equal to the minimum value."
    let max = if max < min { min } else { max };
    let range = max - min;

    let Some(step) = step else {
        return min + base * range;
    };

    // "If C is infinite, the result is A."
    if step.is_infinite() {
        return min;
    }

    // "If C is negative, zero, or positive but close enough to zero that the
    // range for the step multiplier would be infinite in the user agent, the
    // step must be ignored."
    if step <= 0.0 {
        return min + base * range;
    }

    // "Let epsilon be step / 1000, or the smallest representable value greater
    // than zero in the numeric type being used if epsilon would round to zero."
    let epsilon = match step / 1000.0 {
        e if e > 0.0 => e,
        _ => (0.0f32).next_up(),
    };

    // "Let N be the largest integer such that min + N * step is less than or
    // equal to max."
    let mut n = (range / step).floor();
    if n.is_infinite() {
        return min + base * range;
    }

    // "If N produces a value that is not within epsilon of max, but N+1 would
    // produce a value within epsilon of max, set N to N+1."
    if (min + n * step - max).abs() > epsilon && (min + (n + 1.0) * step - max).abs() <= epsilon {
        n += 1.0;
    }

    // "Let step index be a random integer less than N+1, given R. Let value
    // be min + step index * step."
    let step_index = calc_random_integer(base, n + 1.0);
    let value = min + step_index * step;

    // "If step index is N and value is within epsilon of max, return max."
    if step_index == n && (value - max).abs() <= epsilon {
        return max;
    }

    value
}

/// A CSS integer value.
pub type CSSInteger = i32;

/// Serialize an identifier which is represented as an atom.
#[cfg(feature = "gecko")]
pub fn serialize_atom_identifier<W>(ident: &Atom, dest: &mut W) -> fmt::Result
where
    W: Write,
{
    ident.with_str(|s| serialize_identifier(s, dest))
}

/// Serialize an identifier which is represented as an atom.
#[cfg(feature = "servo")]
pub fn serialize_atom_identifier<Static, W>(
    ident: &::string_cache::Atom<Static>,
    dest: &mut W,
) -> fmt::Result
where
    Static: string_cache::StaticAtomSet,
    W: Write,
{
    serialize_identifier(&ident, dest)
}

/// Serialize a name which is represented as an Atom.
#[cfg(feature = "gecko")]
pub fn serialize_atom_name<W>(ident: &Atom, dest: &mut W) -> fmt::Result
where
    W: Write,
{
    ident.with_str(|s| serialize_name(s, dest))
}

/// Serialize a name which is represented as an Atom.
#[cfg(feature = "servo")]
pub fn serialize_atom_name<Static, W>(
    ident: &::string_cache::Atom<Static>,
    dest: &mut W,
) -> fmt::Result
where
    Static: string_cache::StaticAtomSet,
    W: Write,
{
    serialize_name(&ident, dest)
}

/// Serialize a number with calc, and NaN/infinity handling (if enabled)
pub fn serialize_number<W>(v: f32, dest: &mut CssWriter<W>) -> fmt::Result
where
    W: Write,
{
    serialize_specified_dimension(v, "", /* was_calc = */ false, dest)
}

/// Serialize a specified dimension with unit, calc, and NaN/infinity handling (if enabled)
pub fn serialize_specified_dimension<W>(
    v: f32,
    unit: &str,
    was_calc: bool,
    dest: &mut CssWriter<W>,
) -> fmt::Result
where
    W: Write,
{
    if was_calc {
        dest.write_str("calc(")?;
    }

    if !v.is_finite() {
        // https://drafts.csswg.org/css-values/#calc-error-constants:
        // "While not technically numbers, these keywords act as numeric values,
        // similar to e and pi. Thus to get an infinite length, for example,
        // requires an expression like calc(infinity * 1px)."

        if v.is_nan() {
            dest.write_str("NaN")?;
        } else if v == f32::INFINITY {
            dest.write_str("infinity")?;
        } else if v == f32::NEG_INFINITY {
            dest.write_str("-infinity")?;
        }

        if !unit.is_empty() {
            dest.write_str(" * 1")?;
        }
    } else {
        v.to_css(dest)?;
    }

    dest.write_str(unit)?;

    if was_calc {
        dest.write_char(')')?;
    }
    Ok(())
}

/// A CSS string stored as an `Atom`.
#[repr(transparent)]
#[derive(
    Clone,
    Debug,
    Default,
    Deref,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
pub struct AtomString(pub Atom);

#[cfg(feature = "servo")]
impl AsRef<str> for AtomString {
    fn as_ref(&self) -> &str {
        &*self.0
    }
}

impl Parse for AtomString {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(Self(Atom::from(input.expect_string()?.as_ref())))
    }
}

impl cssparser::ToCss for AtomString {
    fn to_css<W>(&self, dest: &mut W) -> fmt::Result
    where
        W: Write,
    {
        // Wrap in quotes to form a string literal
        dest.write_char('"')?;
        #[cfg(feature = "servo")]
        {
            cssparser::CssStringWriter::new(dest).write_str(self.as_ref())?;
        }
        #[cfg(feature = "gecko")]
        {
            self.0
                .with_str(|s| cssparser::CssStringWriter::new(dest).write_str(s))?;
        }
        dest.write_char('"')
    }
}

impl style_traits::ToCss for AtomString {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        cssparser::ToCss::to_css(self, dest)
    }
}

impl PrecomputedHash for AtomString {
    #[inline]
    fn precomputed_hash(&self) -> u32 {
        self.0.precomputed_hash()
    }
}

impl From<&str> for AtomString {
    #[inline]
    fn from(string: &str) -> Self {
        Self(Atom::from(string))
    }
}

/// A generic CSS `<ident>` stored as an `Atom`.
#[cfg(feature = "servo")]
#[repr(transparent)]
#[derive(Deref)]
pub struct GenericAtomIdent<Set>(pub string_cache::Atom<Set>)
where
    Set: string_cache::StaticAtomSet;

/// A generic CSS `<ident>` stored as an `Atom`, for the default atom set.
#[cfg(feature = "servo")]
pub type AtomIdent = GenericAtomIdent<stylo_atoms::AtomStaticSet>;

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> style_traits::SpecifiedValueInfo for GenericAtomIdent<Set> {}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> Default for GenericAtomIdent<Set> {
    fn default() -> Self {
        Self(string_cache::Atom::default())
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> std::fmt::Debug for GenericAtomIdent<Set> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> std::hash::Hash for GenericAtomIdent<Set> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.0.hash(state)
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> Eq for GenericAtomIdent<Set> {}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> PartialEq for GenericAtomIdent<Set> {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> Clone for GenericAtomIdent<Set> {
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> to_shmem::ToShmem for GenericAtomIdent<Set> {
    fn to_shmem(&self, builder: &mut to_shmem::SharedMemoryBuilder) -> to_shmem::Result<Self> {
        use std::mem::ManuallyDrop;

        let atom = self.0.to_shmem(builder)?;
        Ok(ManuallyDrop::new(Self(ManuallyDrop::into_inner(atom))))
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> malloc_size_of::MallocSizeOf for GenericAtomIdent<Set> {
    fn size_of(&self, ops: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        self.0.size_of(ops)
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> Parse for GenericAtomIdent<Set> {
    fn parse(_context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(Self::from(input.expect_ident()?.as_ref()))
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> cssparser::ToCss for GenericAtomIdent<Set> {
    fn to_css<W>(&self, dest: &mut W) -> fmt::Result
    where
        W: Write,
    {
        serialize_atom_identifier(&self.0, dest)
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> style_traits::ToCss for GenericAtomIdent<Set> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_atom_identifier(&self.0, dest)
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> PrecomputedHash for GenericAtomIdent<Set> {
    #[inline]
    fn precomputed_hash(&self) -> u32 {
        self.0.precomputed_hash()
    }
}

#[cfg(feature = "servo")]
impl<'a, Set: string_cache::StaticAtomSet> From<&'a str> for GenericAtomIdent<Set> {
    #[inline]
    fn from(string: &str) -> Self {
        Self(string_cache::Atom::from(string))
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> std::borrow::Borrow<string_cache::Atom<Set>>
    for GenericAtomIdent<Set>
{
    #[inline]
    fn borrow(&self) -> &string_cache::Atom<Set> {
        &self.0
    }
}

#[cfg(feature = "servo")]
impl<Set: string_cache::StaticAtomSet> GenericAtomIdent<Set> {
    /// Constructs a new GenericAtomIdent.
    #[inline]
    pub fn new(atom: string_cache::Atom<Set>) -> Self {
        Self(atom)
    }

    /// Cast an atom ref to an AtomIdent ref.
    #[inline]
    pub fn cast<'a>(atom: &'a string_cache::Atom<Set>) -> &'a Self {
        let ptr = atom as *const _ as *const Self;
        // safety: repr(transparent)
        unsafe { &*ptr }
    }
}

/// A CSS `<ident>` stored as an `Atom`.
#[cfg(feature = "gecko")]
#[repr(transparent)]
#[derive(
    Clone, Debug, Default, Deref, Eq, Hash, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToShmem,
)]
pub struct AtomIdent(pub Atom);

#[cfg(feature = "gecko")]
impl Parse for AtomIdent {
    fn parse(_context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(Self::from(input.expect_ident()?.as_ref()))
    }
}

#[cfg(feature = "gecko")]
impl cssparser::ToCss for AtomIdent {
    fn to_css<W>(&self, dest: &mut W) -> fmt::Result
    where
        W: Write,
    {
        serialize_atom_identifier(&self.0, dest)
    }
}

#[cfg(feature = "gecko")]
impl style_traits::ToCss for AtomIdent {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        cssparser::ToCss::to_css(self, dest)
    }
}

#[cfg(feature = "gecko")]
impl PrecomputedHash for AtomIdent {
    #[inline]
    fn precomputed_hash(&self) -> u32 {
        self.0.precomputed_hash()
    }
}

#[cfg(feature = "gecko")]
impl From<&str> for AtomIdent {
    #[inline]
    fn from(string: &str) -> Self {
        Self(Atom::from(string))
    }
}

#[cfg(feature = "gecko")]
impl AtomIdent {
    /// Constructs a new AtomIdent.
    #[inline]
    pub fn new(atom: Atom) -> Self {
        Self(atom)
    }

    /// Like `Atom::with` but for `AtomIdent`.
    pub unsafe fn with<F, R>(ptr: *const crate::gecko_bindings::structs::nsAtom, callback: F) -> R
    where
        F: FnOnce(&Self) -> R,
    {
        unsafe {
            Atom::with(ptr, |atom: &Atom| {
                // safety: repr(transparent)
                let atom = atom as *const Atom as *const AtomIdent;
                callback(&*atom)
            })
        }
    }

    /// Cast an atom ref to an AtomIdent ref.
    #[inline]
    pub fn cast(atom: &Atom) -> &Self {
        let ptr = atom as *const _ as *const Self;
        // safety: repr(transparent)
        unsafe { &*ptr }
    }
}

#[cfg(feature = "gecko")]
impl std::borrow::Borrow<crate::gecko_string_cache::WeakAtom> for AtomIdent {
    #[inline]
    fn borrow(&self) -> &crate::gecko_string_cache::WeakAtom {
        self.0.borrow()
    }
}

/// Serialize a value into percentage.
pub fn serialize_percentage<W>(value: CSSFloat, dest: &mut CssWriter<W>) -> fmt::Result
where
    W: Write,
{
    serialize_specified_dimension(value * 100., "%", /* was_calc = */ false, dest)
}

/// Serialize a value into normalized (no NaN/inf serialization) percentage.
pub fn serialize_normalized_percentage<W>(value: CSSFloat, dest: &mut CssWriter<W>) -> fmt::Result
where
    W: Write,
{
    (value * 100.).to_css(dest)?;
    dest.write_char('%')
}

/// Reify a percentage.
pub fn reify_percentage(value: CSSFloat, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
    let numeric_value = NumericValue::Unit(UnitValue {
        numeric_type: NumericType::percent(),
        value: value * 100.,
        unit: CssString::from("percent"),
    });

    dest.push(TypedValue::Numeric(numeric_value));
    Ok(())
}

/// Convenience void type to disable some properties and values through types.
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
)]
pub enum Impossible {}

// FIXME(nox): This should be derived but the derive code cannot cope
// with uninhabited enums.
impl ComputeSquaredDistance for Impossible {
    #[inline]
    fn compute_squared_distance(&self, _other: &Self) -> Result<SquaredDistance, ()> {
        match *self {}
    }
}

impl_trivial_to_shmem!(Impossible);

impl Parse for Impossible {
    fn parse(_context: &ParserContext, _input: &mut Parser) -> Result<Self, ParseError> {
        Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError))
    }
}

/// A struct representing one of two kinds of values.
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    MallocSizeOf,
    PartialEq,
    Parse,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
pub enum Either<A, B> {
    /// The first value.
    First(A),
    /// The second kind of value.
    Second(B),
}

impl<A: Debug, B: Debug> Debug for Either<A, B> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Either::First(ref v) => v.fmt(f),
            Either::Second(ref v) => v.fmt(f),
        }
    }
}

/// <https://drafts.csswg.org/css-values-4/#custom-idents>
#[derive(
    Clone,
    Debug,
    Default,
    Deserialize,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct CustomIdent(pub Atom);

impl CustomIdent {
    /// Parse a <custom-ident>
    ///
    /// TODO(zrhoffman, bug 1844501): Use CustomIdent::parse in more places instead of
    /// CustomIdent::from_ident.
    pub fn parse(input: &mut Parser, invalid: &[&str]) -> Result<Self, ParseError> {
        let ident = input.expect_ident()?;
        CustomIdent::from_ident(ident, invalid)
    }

    /// Parse an already-tokenizer identifier
    pub fn from_ident<'i>(ident: &CowRcStr<'i>, excluding: &[&str]) -> Result<Self, ParseError> {
        if !Self::is_valid(ident, excluding) {
            return Err(ParseError::custom(SelectorParseErrorKind::UnexpectedIdent));
        }
        if excluding.iter().any(|s| ident.eq_ignore_ascii_case(s)) {
            Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError))
        } else {
            Ok(CustomIdent(Atom::from(ident.as_ref())))
        }
    }

    fn is_valid(ident: &str, excluding: &[&str]) -> bool {
        use crate::properties::CSSWideKeyword;
        // https://drafts.csswg.org/css-values-4/#custom-idents:
        //
        //     The CSS-wide keywords are not valid <custom-ident>s. The default
        //     keyword is reserved and is also not a valid <custom-ident>.
        if CSSWideKeyword::from_ident(ident).is_ok() || ident.eq_ignore_ascii_case("default") {
            return false;
        }

        // https://drafts.csswg.org/css-values-4/#custom-idents:
        //
        //     Excluded keywords are excluded in all ASCII case permutations.
        !excluding.iter().any(|s| ident.eq_ignore_ascii_case(s))
    }
}

impl ToCss for CustomIdent {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_atom_identifier(&self.0, dest)
    }
}

impl ToTyped for CustomIdent {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        // This shouldn't escape identifiers. See bug 2023533.
        let s = ToCss::to_css_cssstring(self);
        dest.push(TypedValue::Keyword(KeywordValue(s)));
        Ok(())
    }
}

/// <https://www.w3.org/TR/css-values-4/#dashed-idents>
/// This is simply an Atom, but will only parse if the identifier starts with "--".
#[repr(transparent)]
#[derive(
    Clone,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    Serialize,
    Deserialize,
)]
pub struct DashedIdent(pub Atom);

impl DashedIdent {
    /// Parse an already-tokenizer identifier
    pub fn from_ident<'i>(ident: &CowRcStr<'i>) -> Result<Self, ParseError> {
        if !ident.starts_with("--") {
            return Err(ParseError::custom(SelectorParseErrorKind::UnexpectedIdent));
        }
        Ok(Self(Atom::from(ident.as_ref())))
    }

    /// Special value for internal use. Useful where we can't use Option<>.
    pub fn empty() -> Self {
        Self(atom!(""))
    }

    /// Check for special internal value.
    pub fn is_empty(&self) -> bool {
        self.0 == atom!("")
    }

    /// Returns an atom with the same value, but without the starting "--".
    ///
    /// # Panics
    ///
    /// Panics when used on the special `DashedIdent::empty()`.
    pub(crate) fn undashed(&self) -> Atom {
        assert!(!self.is_empty(), "Can't undash the empty DashedIdent");
        #[cfg(feature = "gecko")]
        let name = &self.0.as_slice()[2..];
        #[cfg(feature = "servo")]
        let name = &self.0[2..];
        Atom::from(name)
    }
}

impl IsTreeScoped for DashedIdent {
    fn is_tree_scoped(&self) -> bool {
        !self.is_empty()
    }
}

impl Parse for DashedIdent {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let ident = input.expect_ident()?;
        Self::from_ident(ident)
    }
}

impl ToCss for DashedIdent {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        serialize_atom_identifier(&self.0, dest)
    }
}

/// The <keyframes-name>.
///
/// <https://drafts.csswg.org/css-animations/#typedef-keyframes-name>
///
/// We use a single atom for this. Empty atom represents `none` animation.
#[repr(transparent)]
#[derive(
    Clone,
    Debug,
    Eq,
    Hash,
    PartialEq,
    MallocSizeOf,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
pub struct KeyframesName(Atom);

impl KeyframesName {
    /// <https://drafts.csswg.org/css-animations/#dom-csskeyframesrule-name>
    pub fn from_ident(value: &str) -> Self {
        Self(Atom::from(value))
    }

    /// Returns the `none` value.
    pub fn none() -> Self {
        Self(atom!(""))
    }

    /// Returns whether this is the special `none` value.
    pub fn is_none(&self) -> bool {
        self.0 == atom!("")
    }

    /// Create a new KeyframesName from Atom.
    #[cfg(feature = "gecko")]
    pub fn from_atom(atom: Atom) -> Self {
        Self(atom)
    }

    /// The name as an Atom
    pub fn as_atom(&self) -> &Atom {
        &self.0
    }
}

impl Parse for KeyframesName {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(match *input.next()? {
            Token::Ident(ref s) => Self(CustomIdent::from_ident(s, &["none"])?.0),
            // Note that empty <string> should be rejected.
            Token::QuotedString(ref s) if !s.as_ref().is_empty() => Self(Atom::from(s.as_ref())),
            _ => return Err(ParseError::unexpected_token()),
        })
    }
}

impl ToCss for KeyframesName {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.is_none() {
            return dest.write_str("none");
        }

        fn serialize<W: Write>(string: &str, dest: &mut CssWriter<W>) -> fmt::Result {
            if CustomIdent::is_valid(string, &["none"]) {
                serialize_identifier(string, dest)
            } else {
                string.to_css(dest)
            }
        }

        #[cfg(feature = "gecko")]
        return self.0.with_str(|s| serialize(s, dest));

        #[cfg(feature = "servo")]
        return serialize(self.0.as_ref(), dest);
    }
}

impl ToTyped for KeyframesName {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        let s = ToCss::to_css_cssstring(self);
        dest.push(TypedValue::Keyword(KeywordValue(s)));
        Ok(())
    }
}
