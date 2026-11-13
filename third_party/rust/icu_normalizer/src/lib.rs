// This file is part of ICU4X. For terms of use, please see the file
// called LICENSE at the top level of the ICU4X source tree
// (online at: https://github.com/unicode-org/icu4x/blob/main/LICENSE ).

// https://github.com/unicode-org/icu4x/blob/main/documents/process/boilerplate.md#library-annotations
#![cfg_attr(not(any(test, doc)), no_std)]
#![cfg_attr(
    not(test),
    deny(
        clippy::indexing_slicing,
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
    )
)]
#![warn(missing_docs)]

//! Normalizing text into Unicode Normalization Forms.
//!
//! This module is published as its own crate ([`icu_normalizer`](https://docs.rs/icu_normalizer/latest/icu_normalizer/))
//! and as part of the [`icu`](https://docs.rs/icu/latest/icu/) crate. See the latter for more details on the ICU4X project.
//!
//! # Functionality
//!
//! The top level of the crate provides normalization of input into the four normalization forms defined in [UAX #15: Unicode
//! Normalization Forms](https://www.unicode.org/reports/tr15/): NFC, NFD, NFKC, and NFKD.
//!
//! Three kinds of contiguous inputs are supported: known-well-formed UTF-8 (`&str`), potentially-not-well-formed UTF-8,
//! and potentially-not-well-formed UTF-16. Additionally, an iterator over `char` can be wrapped in a normalizing iterator.
//!
//! The `uts46` module provides the combination of mapping and normalization operations for [UTS #46: Unicode IDNA
//! Compatibility Processing](https://www.unicode.org/reports/tr46/). This functionality is not meant to be used by
//! applications directly. Instead, it is meant as a building block for a full implementation of UTS #46, such as the
//! [`idna`](https://docs.rs/idna/latest/idna/) crate.
//!
//! The `properties` module provides the non-recursive canonical decomposition operation on a per `char` basis and
//! the canonical compositon operation given two `char`s. It also provides access to the Canonical Combining Class
//! property. These operations are primarily meant for [HarfBuzz](https://harfbuzz.github.io/), the types
//! [`CanonicalComposition`](properties::CanonicalComposition), [`CanonicalDecomposition`](properties::CanonicalDecomposition),
//! and [`CanonicalCombiningClassMap`](properties::CanonicalCombiningClassMap) implement the [`harfbuzz_traits`] if
//! the `harfbuzz_traits` Cargo feature is enabled.
//!
//! Notably, this normalizer does _not_ provide the normalization “quick check” that can result in “maybe” in
//! addition to “yes” and “no”. The normalization checks provided by this crate always give a definitive
//! non-“maybe” answer.
//!
//! # Examples
//!
//! ```
//! let nfc = icu_normalizer::ComposingNormalizerBorrowed::new_nfc();
//! assert_eq!(nfc.normalize("a\u{0308}"), "ä");
//! assert!(nfc.is_normalized("ä"));
//!
//! let nfd = icu_normalizer::DecomposingNormalizerBorrowed::new_nfd();
//! assert_eq!(nfd.normalize("ä"), "a\u{0308}");
//! assert!(!nfd.is_normalized("ä"));
//! ```

extern crate alloc;

#[cfg(feature = "serde")]
type Trie<'trie> = CodePointTrie<'trie, u32>;

#[cfg(not(feature = "serde"))]
type Trie<'trie> = FastCodePointTrie<'trie, u32>;

type CombiningBuffer = SmallVec<[CharacterAndClass; 2]>;

type CompositionTrie<'trie> = FastCodePointTrie<'trie, u16>;

// We don't depend on icu_properties to minimize deps, but we want to be able
// to ensure we're using the right CCC values
macro_rules! ccc {
    ($name:ident, $num:expr) => {
        const {
            #[cfg(feature = "icu_properties")]
            if icu_properties::props::CanonicalCombiningClass::$name.to_icu4c_value() != $num {
                panic!("icu_normalizer has incorrect ccc values")
            }
            CanonicalCombiningClass::from_icu4c_value($num)
        }
    };
}

#[cfg(feature = "harfbuzz_traits")]
mod harfbuzz;
#[cfg(feature = "latin1")]
pub mod latin1;
pub mod properties;
pub mod provider;
pub mod uts46;

#[cfg(feature = "serde")]
use crate::provider::CanonicalCompositions;
use crate::provider::CanonicalCompositionsNew;
use crate::provider::DecompositionData;
use crate::provider::NormalizerNfdDataV1;
use crate::provider::NormalizerNfkdDataV1;
use crate::provider::NormalizerUts46DataV1;
use alloc::borrow::Cow;
use alloc::string::String;
use core::char::REPLACEMENT_CHARACTER;
use core::marker::PhantomData;
#[cfg(feature = "serde")]
use icu_collections::char16trie::Char16Trie;
#[cfg(feature = "serde")]
use icu_collections::char16trie::Char16TrieIterator;
#[cfg(feature = "serde")]
use icu_collections::char16trie::TrieResult;
use icu_collections::codepointtrie::iter::CharIterWithTrie;
use icu_collections::codepointtrie::iter::CharsWithTrieDefaultForAsciiEx;
#[cfg(feature = "utf16_iter")]
use icu_collections::codepointtrie::iter::Utf16CharsWithTrieEx;
#[cfg(feature = "utf8_iter")]
use icu_collections::codepointtrie::iter::Utf8CharsWithTrieDefaultForAsciiEx;
use icu_collections::codepointtrie::iter::WithTrie;
use icu_collections::codepointtrie::AbstractCodePointTrie;
use icu_collections::codepointtrie::CodePointTrie;
use icu_collections::codepointtrie::FastCodePointTrie;
#[cfg(feature = "icu_properties")]
use icu_properties::props::CanonicalCombiningClass;
use icu_provider::prelude::*;
use provider::DecompositionTables;
#[cfg(feature = "serde")]
use provider::NormalizerNfcV1;
use provider::NormalizerNfcV2;
use provider::NormalizerNfdTablesV1;
use provider::NormalizerNfkdTablesV1;
use smallvec::SmallVec;
#[cfg(feature = "utf8_iter")]
use utf8_iter::Utf8CharsEx;
use utf_types::utf8::LeadForMultiByte;
use zerovec::{zeroslice, ZeroSlice};

// The optimizations in the area where `likely` is used
// are extremely brittle. `likely` is useful in the typed-trie
// case on the UTF-16 fast path, but in order not to disturb
// the untyped-trie case on the UTF-16 fast path, make the
// annotations no-ops in the untyped-trie case.

// `cold_path` and `likely` come from
// https://github.com/rust-lang/hashbrown/commit/64bd7db1d1b148594edfde112cdb6d6260e2cfc3 .
// See https://github.com/rust-lang/hashbrown/commit/64bd7db1d1b148594edfde112cdb6d6260e2cfc3#commitcomment-164768806
// for permission to relicense under Unicode-3.0.

#[cfg(not(feature = "serde"))]
#[inline(always)]
#[cold]
fn cold_path() {}

#[cfg(not(feature = "serde"))]
#[inline(always)]
pub(crate) fn likely(b: bool) -> bool {
    if b {
        true
    } else {
        cold_path();
        false
    }
}

#[cfg(not(feature = "serde"))]
#[inline(always)]
pub(crate) fn unlikely(b: bool) -> bool {
    if b {
        cold_path();
        true
    } else {
        false
    }
}

// End import from https://github.com/rust-lang/hashbrown/commit/64bd7db1d1b148594edfde112cdb6d6260e2cfc3 .

/// No-op for typed trie case.
#[cfg(feature = "serde")]
#[inline(always)]
fn likely(b: bool) -> bool {
    b
}

/// No-op for typed trie case.
#[cfg(feature = "serde")]
#[inline(always)]
fn unlikely(b: bool) -> bool {
    b
}

/// This type exists as a shim for icu_properties CanonicalCombiningClass when the crate is disabled
/// It should not be exposed to users.
#[cfg(not(feature = "icu_properties"))]
#[derive(Copy, Clone, Eq, PartialEq, PartialOrd, Ord)]
struct CanonicalCombiningClass(pub(crate) u8);

#[cfg(not(feature = "icu_properties"))]
impl CanonicalCombiningClass {
    const fn from_icu4c_value(v: u8) -> Self {
        Self(v)
    }
    const fn to_icu4c_value(self) -> u8 {
        self.0
    }
}

const CCC_NOT_REORDERED: CanonicalCombiningClass = ccc!(NotReordered, 0);
const CCC_ABOVE: CanonicalCombiningClass = ccc!(Above, 230);

const LEAD_FOR_NFC_PASSTHROUGH_BOUND: LeadForMultiByte = const {
    // 0x300 is 0xCC 0x80 in UTF-8, i.e. the lowest character with 0xCC lead.
    if let Ok(lead) = LeadForMultiByte::try_new(0xCC) {
        lead
    } else {
        panic!()
    }
};

/// Treatment of the ignorable marker (0xFFFFFFFF) in data.
#[derive(Debug, PartialEq, Eq)]
enum IgnorableBehavior {
    /// 0xFFFFFFFF in data is not supported.
    Unsupported,
    /// Ignorables are ignored.
    Ignored,
    /// Ignorables are treated as singleton decompositions
    /// to the REPLACEMENT CHARACTER.
    ReplacementCharacter,
}

pub(crate) trait IteratorPolicy {
    const IGNORABLE_BEHAVIOR: IgnorableBehavior;
}

#[derive(Debug)]
struct Uax15Policy;

impl IteratorPolicy for Uax15Policy {
    const IGNORABLE_BEHAVIOR: IgnorableBehavior = IgnorableBehavior::Unsupported;
}

/// Marker for UTS 46 ignorables.
///
/// See trie-value-format.md
const IGNORABLE_MARKER: u32 = 0xFFFFFFFF;

/// Marker that the decomposition does not round trip via NFC.
///
/// See trie-value-format.md
const NON_ROUND_TRIP_MARKER: u32 = 1 << 30;

/// Marker that the first character of the decomposition
/// can combine backwards.
///
/// See trie-value-format.md
const BACKWARD_COMBINING_MARKER: u32 = 1 << 31;

/// Mask for the bits have to be zero for this to be a BMP
/// singleton decomposition, or value baked into the surrogate
/// range.
///
/// See trie-value-format.md
const HIGH_ZEROS_MASK: u32 = 0x3FFF0000;

/// Mask for the bits have to be zero for this to be a complex
/// decomposition.
///
/// See trie-value-format.md
const LOW_ZEROS_MASK: u32 = 0xFFE0;

/// Checks if a trie value carries a (non-zero) canonical
/// combining class.
///
/// See trie-value-format.md
#[inline]
fn trie_value_has_ccc(trie_value: u32) -> bool {
    (trie_value & 0x3FFFFE00) == 0xD800
}

/// Checks if the trie signifies a special non-starter decomposition.
///
/// See trie-value-format.md
fn trie_value_indicates_special_non_starter_decomposition(trie_value: u32) -> bool {
    (trie_value & 0x3FFFFF00) == 0xD900
}

/// Checks if the trie signifies a non-decomposing non-starter.
///
/// See trie-value-format.md
fn trie_value_indicates_non_decomposing_non_starter(trie_value: u32) -> bool {
    (trie_value & 0x3FFFFF00) == 0xD800
}

/// Checks if a trie value signifies a character whose decomposition
/// starts with a non-starter.
///
/// See trie-value-format.md
fn decomposition_starts_with_non_starter(trie_value: u32) -> bool {
    trie_value_has_ccc(trie_value)
}

/// Extracts a canonical combining class (possibly zero) from a trie value.
///
/// See trie-value-format.md
fn ccc_from_trie_value(trie_value: u32) -> CanonicalCombiningClass {
    if trie_value_has_ccc(trie_value) {
        CanonicalCombiningClass::from_icu4c_value(trie_value as u8)
    } else {
        CCC_NOT_REORDERED
    }
}

/// The tail (everything after the first character) of the NFKD form U+FDFA
/// as 16-bit units.
static FDFA_NFKD: [u16; 17] = [
    0x644, 0x649, 0x20, 0x627, 0x644, 0x644, 0x647, 0x20, 0x639, 0x644, 0x64A, 0x647, 0x20, 0x648,
    0x633, 0x644, 0x645,
];

/// Marker value for U+FDFA in NFKD. (Unified with Hangul syllable marker,
/// but they differ by `NON_ROUND_TRIP_MARKER`.)
///
/// See trie-value-format.md
const FDFA_MARKER: u16 = 1;

// These constants originate from page 143 of Unicode 14.0
/// Syllable base
const HANGUL_S_BASE: u32 = 0xAC00;
/// Lead jamo base
const HANGUL_L_BASE: u32 = 0x1100;
/// Vowel jamo base
const HANGUL_V_BASE: u32 = 0x1161;
/// Trail jamo base (deliberately off by one to account for the absence of a trail)
const HANGUL_T_BASE: u32 = 0x11A7;
/// Lead jamo count
const HANGUL_L_COUNT: u32 = 19;
/// Vowel jamo count
const HANGUL_V_COUNT: u32 = 21;
/// Trail jamo count (deliberately off by one to account for the absence of a trail)
const HANGUL_T_COUNT: u32 = 28;
/// Vowel jamo count times trail jamo count
const HANGUL_N_COUNT: u32 = 588;
/// Syllable count
const HANGUL_S_COUNT: u32 = 11172;
/// One past the conjoining jamo block
#[cfg(feature = "serde")]
const HANGUL_JAMO_LIMIT: u32 = 0x1200;
/// Trie value base corresponding for L
const HANGUL_L_TRIE_VAL_BASE: u16 = 0xD6A7;

/// If `opt` is `Some`, unwrap it. If `None`, panic if debug assertions
/// are enabled and return `default` if debug assertions are not enabled.
///
/// Use this only if the only reason why `opt` could be `None` is bogus
/// data from the provider.
#[inline(always)]
fn unwrap_or_gigo<T>(opt: Option<T>, default: T) -> T {
    if let Some(val) = opt {
        val
    } else {
        // GIGO case
        debug_assert!(false);
        default
    }
}

/// Convert a `u32` _obtained from data provider data_ to `char`.
#[inline(always)]
fn char_from_u32(u: u32) -> char {
    unwrap_or_gigo(core::char::from_u32(u), REPLACEMENT_CHARACTER)
}

/// Convert a `u16` _obtained from data provider data_ to `char`.
#[inline(always)]
fn char_from_u16(u: u16) -> char {
    char_from_u32(u32::from(u))
}

const EMPTY_U16: &ZeroSlice<u16> = zeroslice![];

const EMPTY_CHAR: &ZeroSlice<char> = zeroslice![];

#[inline(always)]
fn in_inclusive_range(c: char, start: char, end: char) -> bool {
    u32::from(c).wrapping_sub(u32::from(start)) <= (u32::from(end) - u32::from(start))
}

#[inline(always)]
#[cfg(feature = "utf16_iter")]
fn in_inclusive_range16(u: u16, start: u16, end: u16) -> bool {
    u.wrapping_sub(start) <= (end - start)
}

#[derive(Debug)]
pub(crate) enum CanonicalCompositionsPayload {
    Current(DataPayload<NormalizerNfcV2>),
    #[cfg(feature = "serde")]
    Legacy(DataPayload<NormalizerNfcV1>),
}

impl<'data> CanonicalCompositionsPayload {
    pub(crate) fn as_borrowed(&'data self) -> CanonicalCompositionsBorrowed<'data> {
        match self {
            CanonicalCompositionsPayload::Current(data_payload) => {
                CanonicalCompositionsBorrowed::Current(data_payload.get())
            }
            #[cfg(feature = "serde")]
            CanonicalCompositionsPayload::Legacy(data_payload) => {
                CanonicalCompositionsBorrowed::Legacy(data_payload.get())
            }
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub(crate) enum CanonicalCompositionsBorrowed<'data> {
    Current(&'data CanonicalCompositionsNew<'data>),
    #[cfg(feature = "serde")]
    Legacy(&'data CanonicalCompositions<'data>),
}

impl CanonicalCompositionsBorrowed<'static> {
    pub(crate) const fn static_to_owned(self) -> CanonicalCompositionsPayload {
        match self {
            CanonicalCompositionsBorrowed::Current(s) => {
                CanonicalCompositionsPayload::Current(DataPayload::from_static_ref(s))
            }
            #[cfg(feature = "serde")]
            CanonicalCompositionsBorrowed::Legacy(s) => {
                CanonicalCompositionsPayload::Legacy(DataPayload::from_static_ref(s))
            }
        }
    }
}

impl<'data> CanonicalCompositionsBorrowed<'data> {
    pub(crate) fn as_ref(&'data self) -> CanonicalCompositionsRef<'data> {
        match self {
            CanonicalCompositionsBorrowed::Current(s) => CanonicalCompositionsRef::Current(
                <&CompositionTrie<'data>>::try_from(&s.trie)
                    .unwrap_or_else(|_| unreachable!("Incompatible data")),
                &s.linear16,
                &s.linear24,
            ),
            #[cfg(feature = "serde")]
            CanonicalCompositionsBorrowed::Legacy(s) => {
                CanonicalCompositionsRef::Legacy(s.canonical_compositions.clone())
            }
        }
    }
}

#[derive(Debug)]
pub(crate) enum CanonicalCompositionsRef<'data> {
    Current(
        &'data CompositionTrie<'data>,
        &'data ZeroSlice<(u16, u16)>,
        &'data ZeroSlice<(char, char)>,
    ),
    #[cfg(feature = "serde")]
    Legacy(Char16Trie<'data>),
}

impl<'data> CanonicalCompositionsRef<'data> {
    /// Performs canonical composition (including Hangul) on a pair of
    /// characters or returns `None` if these characters don't compose.
    /// Composition exclusions are taken into account.
    ///
    /// TODO: Have the caller retain more state and have this function return
    /// more information that is useful for retaining information between
    /// attempts to compose in a sequence of such attempts:
    ///
    /// * We can return the linear search slice when we search through it but don't find anything.
    /// * We can know that no further matches are possible.
    /// * We can know that the starter was a special ASCII vowel.
    /// * We can know that we just formed a Hangul LV syllable.
    pub(crate) fn compose(&self, starter: char, second: char) -> Option<char> {
        match self {
            CanonicalCompositionsRef::Current(trie, linear16, linear24) => {
                // According to Compiler Explorer, the `match` optimizes to a bitfield lookup.
                // Don't bother optimizing manually without inspecting the generated assembly.
                let (primary, secondary) = match starter {
                    'a' | 'e' | 'i' | 'o' | 'u' | 'A' | 'E' | 'I' | 'O' | 'U' => {
                        // This special case balances out the max length of entries
                        // in `linear` so that no entry exceeds 10 items as of Unicode 17.
                        (second, starter)
                    }
                    _ => (starter, second),
                };
                let packed = trie.scalar(primary);
                let len = usize::from(packed & 0b1111);
                let index = usize::from(packed >> 4);
                if let Some(slice16) = linear16.get_subslice(index..index + len) {
                    let secondary32 = u32::from(secondary);
                    for (candidate, composed) in slice16.iter() {
                        if u32::from(candidate) == secondary32 {
                            return Some(char_from_u16(composed));
                        }
                    }
                    return None;
                }

                if packed < 0b1000_0000_0000_0000 {
                    debug_assert_eq!(packed, 0b0111_1111_1111_1111);
                    return None;
                }

                // Mask off the bit that was the most-significant bit in `u16` before we
                // shifted right by 4.
                let index = index & 0b1_11111_11111; // 11 bits set
                if let Some(slice24) = linear24.get_subslice(index..index + len) {
                    for (candidate, composed) in slice24.iter() {
                        if candidate == secondary {
                            return Some(composed);
                        }
                    }
                    return None;
                }

                // Handle Hangul L after non-BMP, because HarfBuzz isn't actually supposed
                // to exercise this case and in the normalizer itself, we come here only
                // in NFKC in the case of enclosed Hangul.
                if packed >= HANGUL_L_TRIE_VAL_BASE {
                    // If the debug asserts fail, we have a GIGO case.
                    debug_assert!(u32::from(primary).wrapping_sub(HANGUL_L_BASE) < HANGUL_L_COUNT);
                    debug_assert_eq!(
                        u32::from(packed - HANGUL_L_TRIE_VAL_BASE),
                        u32::from(primary).wrapping_sub(HANGUL_L_BASE) * HANGUL_N_COUNT
                    );

                    let v = u32::from(second).wrapping_sub(HANGUL_V_BASE);
                    if v < HANGUL_V_COUNT {
                        // Acconding to Compiler Explorer, multiplication by `HANGUL_T_COUNT`
                        // optimizes to not actually using a multiplication instruction.
                        let lv = u32::from(packed - HANGUL_L_TRIE_VAL_BASE) + v * HANGUL_T_COUNT;
                        // SAFETY: Safe, because the inputs are known to be in range. Notably
                        // packed cannot have been above 0xFFFF, since it came from `u16`.
                        // That is, this must be in scalar value range. However, the result
                        // can still be GIGO if the trie value does not contain the right value
                        // within its possible range, in which case either of the above debug
                        // assertions should fail.
                        return Some(unsafe { char::from_u32_unchecked(HANGUL_S_BASE + lv) });
                    }
                    return None;
                }

                // `starter` is Hangul LV unless GIGO. If the debug asserts fail, we have a GIGO case.
                debug_assert!(u32::from(primary).wrapping_sub(HANGUL_S_BASE) < HANGUL_S_COUNT);
                debug_assert_eq!(
                    u32::from(primary).wrapping_sub(HANGUL_S_BASE) % HANGUL_T_COUNT,
                    0
                );
                if in_inclusive_range(secondary, '\u{11A8}', '\u{11C2}') {
                    let lvt = u32::from(primary) + (u32::from(secondary) - HANGUL_T_BASE);
                    if lvt < 0xD800 {
                        // SAFETY: Immediately above we checked that `c32` is below the surrogate
                        // range. (Not using `char::from_u32` itself as a micro optimization.)
                        // This is only a check about the safe `char` range. The result could
                        // still be GIGO, wich which case either of the above debug assertions
                        // should fail.
                        return Some(unsafe { char::from_u32_unchecked(lvt) });
                    } else {
                        // GIGO
                        // Asserting `false`, although either of the above two debug assertions
                        // should already have caught this case.
                        debug_assert!(false);
                    }
                }
                None
            }
            #[cfg(feature = "serde")]
            CanonicalCompositionsRef::Legacy(char16_trie) => {
                Self::compose_legacy(char16_trie.iter(), starter, second)
            }
        }
    }

    /// Performs canonical composition (including Hangul) on a pair of
    /// characters on the assumption that the second one is a starter
    /// or returns `None` if these characters don't compose.
    /// Composition exclusions are taken into account.
    ///
    /// The returned boolean can be true only if `char` a Hangul LV syllable.
    ///
    /// The argument `starter_is_lv` must be set either to false or to the value
    /// that this method previously returned alongside `starter`.
    pub(crate) fn compose_starter(
        &self,
        starter: char,
        second: char,
        starter_is_lv: bool,
    ) -> Option<(char, bool)> {
        if starter_is_lv {
            debug_assert!(u32::from(starter).wrapping_sub(HANGUL_S_BASE) < HANGUL_S_COUNT);
            debug_assert_eq!(
                u32::from(starter).wrapping_sub(HANGUL_S_BASE) % HANGUL_T_COUNT,
                0
            );
            if in_inclusive_range(second, '\u{11A8}', '\u{11C2}') {
                // We take the perf hit of checking the returned character for range
                // even though we could omit the check if we trusted 100% that the
                // other code has no mistakes regarding the stated required semantics
                // of `starter_is_lv`.
                return Some((
                    char_from_u32(u32::from(starter) + (u32::from(second) - HANGUL_T_BASE)),
                    false,
                ));
            }
            return None;
        }
        match self {
            CanonicalCompositionsRef::Current(trie, linear16, linear24) => {
                // We assume that future versions of Unicode won't introduce starters
                // that would compose with ASCII vowels.
                let primary = starter;
                let secondary = second;
                let packed = trie.scalar(primary);

                if packed >= HANGUL_L_TRIE_VAL_BASE {
                    // If the debug asserts fail, we have a GIGO case.
                    debug_assert!(u32::from(primary).wrapping_sub(HANGUL_L_BASE) < HANGUL_L_COUNT);
                    debug_assert_eq!(
                        u32::from(packed - HANGUL_L_TRIE_VAL_BASE),
                        u32::from(primary).wrapping_sub(HANGUL_L_BASE) * HANGUL_N_COUNT
                    );

                    let v = u32::from(second).wrapping_sub(HANGUL_V_BASE);
                    if v < HANGUL_V_COUNT {
                        // Acconding to Compiler Explorer, multiplication by `HANGUL_T_COUNT`
                        // optimizes to not actually using a multiplication instruction.
                        let lv = u32::from(packed - HANGUL_L_TRIE_VAL_BASE) + v * HANGUL_T_COUNT;
                        // SAFETY: Safe, because the inputs are known to be in range. Notably
                        // packed cannot have been above 0xFFFF, since it came from `u16`.
                        // That is, this must be in scalar value range. However, the result
                        // can still be GIGO if the trie value does not contain the right value
                        // within its possible range, in which case either of the above debug
                        // assertions should fail.
                        return Some((
                            unsafe { char::from_u32_unchecked(HANGUL_S_BASE + lv) },
                            true,
                        ));
                    }
                    return None;
                }

                // Putting the Hangul case above, because in NFD Hangul the above case
                // happens with each syllable whereas with the languages for which the
                // case immediately below is relevant, the case occurs only once in a
                // while.

                let len = usize::from(packed & 0b1111);
                let index = usize::from(packed >> 4);
                if let Some(slice16) = linear16.get_subslice(index..index + len) {
                    let secondary32 = u32::from(secondary);
                    for (candidate, composed) in slice16.iter() {
                        if u32::from(candidate) == secondary32 {
                            return Some((char_from_u16(composed), false));
                        }
                    }
                    return None;
                }

                if packed < 0b1000_0000_0000_0000 {
                    debug_assert_eq!(packed, 0b0111_1111_1111_1111);
                    return None;
                }
                // Mask off the bit that was the most-significant bit in `u16` before we
                // shifted right by 4.
                let index = index & 0b1_11111_11111; // 11 bits set
                if let Some(slice24) = linear24.get_subslice(index..index + len) {
                    for (candidate, composed) in slice24.iter() {
                        if candidate == secondary {
                            return Some((composed, false));
                        }
                    }
                    return None;
                }
                // `starter` is Hangul LV unless GIGO. If the debug asserts fail, we have a GIGO case.
                debug_assert!(u32::from(primary).wrapping_sub(HANGUL_S_BASE) < HANGUL_S_COUNT);
                debug_assert_eq!(
                    u32::from(primary).wrapping_sub(HANGUL_S_BASE) % HANGUL_T_COUNT,
                    0
                );
                if in_inclusive_range(secondary, '\u{11A8}', '\u{11C2}') {
                    let lvt = u32::from(primary) + (u32::from(secondary) - HANGUL_T_BASE);
                    if lvt < 0xD800 {
                        // SAFETY: Immediately above we checked that `c32` is below the surrogate
                        // range. (Not using `char::from_u32` itself as a micro optimization.)
                        // This is only a check about the safe `char` range. The result could
                        // still be GIGO, wich which case either of the above debug assertions
                        // should fail.
                        return Some((unsafe { char::from_u32_unchecked(lvt) }, false));
                    } else {
                        // GIGO
                        // Asserting `false`, although either of the above two debug assertions
                        // should already have caught this case.
                        debug_assert!(false);
                    }
                }
                None
            }
            #[cfg(feature = "serde")]
            CanonicalCompositionsRef::Legacy(char16_trie) => {
                Self::compose_legacy(char16_trie.iter(), starter, second).map(|c| (c, false))
            }
        }
    }

    #[cfg(feature = "serde")]
    #[cold]
    #[inline(never)]
    fn compose_legacy(mut iter: Char16TrieIterator, starter: char, second: char) -> Option<char> {
        let v = u32::from(second).wrapping_sub(HANGUL_V_BASE);
        if v >= HANGUL_JAMO_LIMIT - HANGUL_V_BASE {
            // To make the trie smaller, the pairs are stored second character first.
            // Given how this method is used in ways where it's known that `second`
            // is or isn't a starter. We could potentially split the trie into two
            // tries depending on whether `second` is a starter.
            match iter.next(second) {
                TrieResult::NoMatch => None,
                TrieResult::NoValue => match iter.next(starter) {
                    TrieResult::NoMatch => None,
                    TrieResult::FinalValue(i) => {
                        if let Some(c) = char::from_u32(i as u32) {
                            Some(c)
                        } else {
                            // GIGO case
                            debug_assert!(false);
                            None
                        }
                    }
                    TrieResult::NoValue | TrieResult::Intermediate(_) => {
                        // GIGO case
                        debug_assert!(false);
                        None
                    }
                },
                TrieResult::FinalValue(_) | TrieResult::Intermediate(_) => {
                    // GIGO case
                    debug_assert!(false);
                    None
                }
            }
        } else {
            if v < HANGUL_V_COUNT {
                let l = u32::from(starter).wrapping_sub(HANGUL_L_BASE);
                if l < HANGUL_L_COUNT {
                    let lv = l * HANGUL_N_COUNT + v * HANGUL_T_COUNT;
                    // Safe, because the inputs are known to be in range.
                    return Some(unsafe { char::from_u32_unchecked(HANGUL_S_BASE + lv) });
                }
                return None;
            }
            if in_inclusive_range(second, '\u{11A8}', '\u{11C2}') {
                let lv = u32::from(starter).wrapping_sub(HANGUL_S_BASE);
                if lv < HANGUL_S_COUNT && lv % HANGUL_T_COUNT == 0 {
                    let lvt = lv + (u32::from(second) - HANGUL_T_BASE);
                    // Safe, because the inputs are known to be in range.
                    return Some(unsafe { char::from_u32_unchecked(HANGUL_S_BASE + lvt) });
                }
            }
            None
        }
    }
}

/// See trie-value-format.md
#[inline(always)]
fn starter_and_decomposes_to_self_impl(trie_val: u32) -> bool {
    // The REPLACEMENT CHARACTER has `NON_ROUND_TRIP_MARKER` set,
    // and this function needs to ignore that.
    (trie_val & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER)) == 0
}

/// See trie-value-format.md
#[inline(always)]
#[cfg(feature = "utf8_iter")]
pub fn starter_and_decomposes_to_self_except_replacement(trie_val: u32) -> bool {
    // This intentionally leaves `NON_ROUND_TRIP_MARKER` in the value
    // to be compared with zero. U+FFFD has that flag set despite really
    // being being round-tripping in order to make UTF-8 errors
    // ineligible for passthrough.
    (trie_val & !BACKWARD_COMBINING_MARKER) == 0
}

/// See trie-value-format.md
#[inline(always)]
fn potential_passthrough_and_cannot_combine_backwards(trie_val: u32) -> bool {
    (trie_val & (NON_ROUND_TRIP_MARKER | BACKWARD_COMBINING_MARKER)) == 0
}

/// Struct for holding together a character and the value
/// looked up for it from the NFD trie in a more explicit
/// way than an anonymous pair.
/// Also holds a flag about the supplementary-trie provenance.
#[derive(Debug, PartialEq, Eq)]
struct CharacterAndTrieValue {
    character: char,
    /// See trie-value-format.md
    trie_val: u32,
}

impl CharacterAndTrieValue {
    #[inline(always)]
    pub fn new(c: char, trie_value: u32) -> Self {
        CharacterAndTrieValue {
            character: c,
            trie_val: trie_value,
        }
    }

    #[inline(always)]
    pub fn starter_and_decomposes_to_self(&self) -> bool {
        starter_and_decomposes_to_self_impl(self.trie_val)
    }

    /// See trie-value-format.md
    #[inline(always)]
    pub fn can_combine_backwards(&self) -> bool {
        (self.trie_val & BACKWARD_COMBINING_MARKER) != 0
    }
    /// See trie-value-format.md
    #[inline(always)]
    pub fn potential_passthrough(&self) -> bool {
        (self.trie_val & NON_ROUND_TRIP_MARKER) == 0
    }
}

/// Pack a `char` and a `CanonicalCombiningClass` in
/// 32 bits (the former in the lower 24 bits and the
/// latter in the high 8 bits). The latter can be
/// initialized to 0xFF upon creation, in which case
/// it can be actually set later by calling
/// `set_ccc_from_trie_if_not_already_set`. This is
/// a micro optimization to avoid the Canonical
/// Combining Class trie lookup when there is only
/// one combining character in a sequence. This type
/// is intentionally non-`Copy` to get compiler help
/// in making sure that the class is set on the
/// instance on which it is intended to be set
/// and not on a temporary copy.
///
/// Note that 0xFF is won't be assigned to an actual
/// canonical combining class per definition D104
/// in The Unicode Standard.
//
// NOTE: The Pernosco debugger has special knowledge
// of this struct. Please do not change the bit layout
// or the crate-module-qualified name of this struct
// without coordination.
#[derive(Debug)]
struct CharacterAndClass(u32);

impl<'data> CharacterAndClass {
    pub fn new(c: char, ccc: CanonicalCombiningClass) -> Self {
        CharacterAndClass(u32::from(c) | (u32::from(ccc.to_icu4c_value()) << 24))
    }
    pub fn new_with_placeholder(c: char) -> Self {
        CharacterAndClass(u32::from(c) | ((0xFF) << 24))
    }
    pub fn new_with_trie_value(c_tv: CharacterAndTrieValue) -> Self {
        Self::new(c_tv.character, ccc_from_trie_value(c_tv.trie_val))
    }
    pub fn new_starter(c: char) -> Self {
        CharacterAndClass(u32::from(c))
    }
    /// This method must exist for Pernosco to apply its special rendering.
    /// Also, this must not be dead code!
    pub fn character(&self) -> char {
        // SAFETY: Safe, because the low 24 bits came from a `char`
        // originally.
        unsafe { char::from_u32_unchecked(self.0 & 0xFFFFFF) }
    }
    /// This method must exist for Pernosco to apply its special rendering.
    pub fn ccc(&self) -> CanonicalCombiningClass {
        CanonicalCombiningClass::from_icu4c_value((self.0 >> 24) as u8)
    }

    pub fn character_and_ccc(&self) -> (char, CanonicalCombiningClass) {
        (self.character(), self.ccc())
    }
    pub fn set_ccc_from_trie_if_not_already_set<T: AbstractCodePointTrie<'data, u32>>(
        &mut self,
        trie: &'data T,
    ) {
        if self.0 >> 24 != 0xFF {
            return;
        }
        let scalar = self.0 & 0xFFFFFF;
        // SAFETY: Safe, because the low 24 bits came from a `char`
        // originally.
        self.0 = ((ccc_from_trie_value(trie.scalar(unsafe { char::from_u32_unchecked(scalar) }))
            .to_icu4c_value() as u32)
            << 24)
            | scalar;
    }
}

/// An iterator adaptor that turns an `Iterator` over `char` into
/// a lazily-decomposed `char` sequence.
#[derive(Debug)]
pub struct Decomposition<'data, I>
where
    I: Iterator<Item = char>,
{
    inner: DecompositionInner<
        'data,
        CharIterWithTrie<'data, Trie<'data>, u32, I>,
        Trie<'data>,
        Uax15Policy,
    >,
}

impl<'data, I> Decomposition<'data, I>
where
    I: Iterator<Item = char>,
{
    /// Constructs a decomposing iterator adapter from a delegate
    /// iterator and references to the necessary data, without
    /// supplementary data.
    ///
    /// Use `DecomposingNormalizer::normalize_iter()` instead unless
    /// there's a good reason to use this constructor directly.
    ///
    /// Public but hidden in order to be able to use this from the
    /// collator.
    #[doc(hidden)] // used in older versions of collator
    #[deprecated = "Use `new_decomposition()` instead"]
    pub fn new(
        delegate: I,
        decompositions: &'data DecompositionData,
        tables: &'data DecompositionTables,
    ) -> Self {
        let mut ret = Self {
            inner: DecompositionInner::new_with_supplements(
                CharIterWithTrie::new(
                    delegate,
                    #[allow(clippy::useless_conversion)]
                    <&Trie<'data>>::try_from(&decompositions.trie)
                        .unwrap_or_else(|_| unreachable!("Incompatible data")),
                ),
                tables,
                None,
            ),
        };
        let _ = ret.next();
        ret
    }
}

impl<I> Iterator for Decomposition<'_, I>
where
    I: Iterator<Item = char>,
{
    type Item = char;

    #[inline]
    fn next(&mut self) -> Option<char> {
        self.inner.next()
    }
}

/// The iterator first yields an extra U+FFFD and then
/// the sequence actually corresponding to the input.
#[doc(hidden)] // used in collator
#[inline(always)]
pub fn new_decomposition<'data, I, T>(
    delegate: I,
    tables: &'data DecompositionTables,
) -> impl Iterator<Item = char> + 'data
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32> + 'data,
    T: AbstractCodePointTrie<'data, u32> + 'data,
{
    DecompositionInner::<'data, I, T, Uax15Policy>::new_with_supplements(delegate, tables, None)
}

#[derive(Debug)]
struct DecompositionInner<'data, I, T, P>
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
    T: AbstractCodePointTrie<'data, u32>,
    P: IteratorPolicy,
{
    // See trie-value-format.md for the trie wrapped in `delegate`
    delegate: I,
    buffer: CombiningBuffer,
    /// The index of the next item to be read from `buffer`.
    /// The purpose if this index is to avoid having to move
    /// the rest upon every read.
    buffer_pos: usize,
    // At the start of `next()` if not `None`, this is a pending unnormalized
    // starter. When `Decomposition` appears alone, this is never a non-starter.
    // However, when `Decomposition` appears inside a `Composition`, this
    // may become a non-starter before `decomposing_next()` is called.
    pending: Option<CharacterAndTrieValue>, // None at end of stream
    scalars16: &'data ZeroSlice<u16>,
    scalars24: &'data ZeroSlice<char>,
    supplementary_scalars16: &'data ZeroSlice<u16>,
    supplementary_scalars24: &'data ZeroSlice<char>,
    _phantom_p: PhantomData<P>,
    _phantom_t: PhantomData<T>,
}

impl<'data, I, T, P> DecompositionInner<'data, I, T, P>
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
    T: AbstractCodePointTrie<'data, u32> + 'data,
    P: IteratorPolicy,
{
    /// Constructs a decomposing iterator adapter from a delegate
    /// iterator and references to the necessary data, including
    /// supplementary data.
    ///
    /// The iterator first yields a U+0000 and only then the sequence
    /// corresponding to the input. Unfortunately, due to the way
    /// stack placement of structs work in Rust, the caller is responsible
    /// for dealing with the initial U+0000. Alternatively, callers in this
    /// crate file can (and should) call `init()`.
    #[inline(always)]
    fn new_with_supplements(
        delegate: I,
        tables: &'data DecompositionTables,
        supplementary_tables: Option<&'data DecompositionTables>,
    ) -> Self {
        DecompositionInner::<I, T, P> {
            delegate,
            buffer: SmallVec::new(), // Normalized
            buffer_pos: 0,
            // Initialize with a placeholder starter in case
            // the real stream starts with a non-starter.
            pending: Some(CharacterAndTrieValue::new('\u{0}', 0)),
            scalars16: &tables.scalars16,
            scalars24: &tables.scalars24,
            supplementary_scalars16: if let Some(supplementary) = supplementary_tables {
                &supplementary.scalars16
            } else {
                EMPTY_U16
            },
            supplementary_scalars24: if let Some(supplementary) = supplementary_tables {
                &supplementary.scalars24
            } else {
                EMPTY_CHAR
            },
            _phantom_p: PhantomData,
            _phantom_t: PhantomData,
        }
    }

    /// Simplified alternative to calling `next()` and discarding the value after constructing this struct.
    fn init(&mut self) {
        self.pending = None;
        self.gather_and_sort_combining(0);
    }

    fn push_decomposition16(
        &mut self,
        offset: usize,
        len: usize,
        only_non_starters_in_trail: bool,
        slice16: &ZeroSlice<u16>,
    ) -> (char, usize) {
        let (starter, tail) = slice16
            .get_subslice(offset..offset + len)
            .and_then(|slice| slice.split_first())
            .map_or_else(
                || {
                    // GIGO case
                    debug_assert!(false);
                    (REPLACEMENT_CHARACTER, EMPTY_U16)
                },
                |(first, trail)| (char_from_u16(first), trail),
            );
        if only_non_starters_in_trail {
            // All the rest are combining
            self.buffer.extend(
                tail.iter()
                    .map(|u| CharacterAndClass::new_with_placeholder(char_from_u16(u))),
            );
            (starter, 0)
        } else {
            let mut i = 0;
            let mut combining_start = 0;
            for u in tail.iter() {
                let ch = char_from_u16(u);
                let trie_value = self.delegate.trie().scalar(ch);
                self.buffer.push(CharacterAndClass::new_with_trie_value(
                    CharacterAndTrieValue::new(ch, trie_value),
                ));
                i += 1;
                // Half-width kana and iota subscript don't occur in the tails
                // of these multicharacter decompositions.
                if !decomposition_starts_with_non_starter(trie_value) {
                    combining_start = i;
                }
            }
            (starter, combining_start)
        }
    }

    fn push_decomposition32(
        &mut self,
        offset: usize,
        len: usize,
        only_non_starters_in_trail: bool,
        slice32: &ZeroSlice<char>,
    ) -> (char, usize) {
        let (starter, tail) = slice32
            .get_subslice(offset..offset + len)
            .and_then(|slice| slice.split_first())
            .unwrap_or_else(|| {
                // GIGO case
                debug_assert!(false);
                (REPLACEMENT_CHARACTER, EMPTY_CHAR)
            });
        if only_non_starters_in_trail {
            // All the rest are combining
            self.buffer
                .extend(tail.iter().map(CharacterAndClass::new_with_placeholder));
            (starter, 0)
        } else {
            let mut i = 0;
            let mut combining_start = 0;
            for ch in tail.iter() {
                let trie_value = self.delegate.trie().scalar(ch);
                self.buffer.push(CharacterAndClass::new_with_trie_value(
                    CharacterAndTrieValue::new(ch, trie_value),
                ));
                i += 1;
                // Half-width kana and iota subscript don't occur in the tails
                // of these multicharacter decompositions.
                if !decomposition_starts_with_non_starter(trie_value) {
                    combining_start = i;
                }
            }
            (starter, combining_start)
        }
    }

    fn delegate_next_no_pending(&mut self) -> Option<CharacterAndTrieValue> {
        debug_assert!(self.pending.is_none());
        loop {
            let (c, trie_val) = self.delegate.next()?;

            if trie_val == IGNORABLE_MARKER {
                match P::IGNORABLE_BEHAVIOR {
                    IgnorableBehavior::Unsupported => {
                        debug_assert!(false);
                    }
                    IgnorableBehavior::ReplacementCharacter => {
                        return Some(CharacterAndTrieValue::new(
                            c,
                            u32::from(REPLACEMENT_CHARACTER) | NON_ROUND_TRIP_MARKER,
                        ));
                    }
                    IgnorableBehavior::Ignored => {
                        // Else ignore this character by reading the next one from the delegate.
                        continue;
                    }
                }
            }
            return Some(CharacterAndTrieValue::new(c, trie_val));
        }
    }

    fn delegate_next(&mut self) -> Option<CharacterAndTrieValue> {
        if let Some(pending) = self.pending.take() {
            // Only happens as part of `Composition` and as part of
            // the contiguous-buffer methods of `DecomposingNormalizer`.
            // I.e. does not happen as part of standalone iterator
            // usage of `Decomposition`.
            Some(pending)
        } else {
            self.delegate_next_no_pending()
        }
    }

    fn decomposing_next(&mut self, c_and_trie_val: CharacterAndTrieValue) -> char {
        let (starter, combining_start) = {
            let c = c_and_trie_val.character;
            // See trie-value-format.md
            let decomposition = c_and_trie_val.trie_val;
            // The REPLACEMENT CHARACTER has `NON_ROUND_TRIP_MARKER` set,
            // and that flag needs to be ignored here.
            if (decomposition & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER)) == 0 {
                // The character is its own decomposition
                (c, 0)
            } else {
                let high_zeros = (decomposition & HIGH_ZEROS_MASK) == 0;
                let low_zeros = (decomposition & LOW_ZEROS_MASK) == 0;
                if !high_zeros && !low_zeros {
                    // Decomposition into two BMP characters: starter and non-starter
                    let starter = char_from_u32(decomposition & 0x7FFF);
                    let combining = char_from_u32((decomposition >> 15) & 0x7FFF);
                    self.buffer
                        .push(CharacterAndClass::new_with_placeholder(combining));
                    (starter, 0)
                } else if high_zeros {
                    // Do the check by looking at `c` instead of looking at a marker
                    // in `singleton` below, because if we looked at the trie value,
                    // we'd still have to check that `c` is in the Hangul syllable
                    // range in order for the subsequent interpretations as `char`
                    // to be safe.
                    // Alternatively, `FDFA_MARKER` and the Hangul marker could
                    // be unified. That would add a branch for Hangul and remove
                    // a branch from singleton decompositions. It seems more
                    // important to favor Hangul syllables than singleton
                    // decompositions.
                    // Note that it would be valid to hoist this Hangul check
                    // one or even two steps earlier in this check hierarchy.
                    // Right now, it's assumed the kind of decompositions into
                    // BMP starter and non-starter, which occur in many languages,
                    // should be checked before Hangul syllables, which are about
                    // one language specifically. Hopefully, we get some
                    // instruction-level parallelism out of the disjointness of
                    // operations on `c` and `decomposition`.
                    let hangul_offset = u32::from(c).wrapping_sub(HANGUL_S_BASE); // SIndex in the spec
                    if hangul_offset < HANGUL_S_COUNT {
                        debug_assert_eq!(decomposition, 1);
                        // Hangul syllable
                        // The math here comes from page 144 of Unicode 14.0
                        let l = hangul_offset / HANGUL_N_COUNT;
                        let v = (hangul_offset % HANGUL_N_COUNT) / HANGUL_T_COUNT;
                        let t = hangul_offset % HANGUL_T_COUNT;

                        // The unsafe blocks here are OK, because the values stay
                        // within the Hangul jamo block and, therefore, the scalar
                        // value range by construction.
                        self.buffer.push(CharacterAndClass::new_starter(unsafe {
                            core::char::from_u32_unchecked(HANGUL_V_BASE + v)
                        }));
                        let first = unsafe { core::char::from_u32_unchecked(HANGUL_L_BASE + l) };
                        if t != 0 {
                            self.buffer.push(CharacterAndClass::new_starter(unsafe {
                                core::char::from_u32_unchecked(HANGUL_T_BASE + t)
                            }));
                            (first, 2)
                        } else {
                            (first, 1)
                        }
                    } else {
                        let singleton = decomposition as u16;
                        if singleton != FDFA_MARKER {
                            // Decomposition into one BMP character
                            let starter = char_from_u16(singleton);
                            (starter, 0)
                        } else {
                            // Special case for the NFKD form of U+FDFA.
                            self.buffer.extend(FDFA_NFKD.map(|u| {
                                // SAFETY: `FDFA_NFKD` is known not to contain
                                // surrogates.
                                CharacterAndClass::new_starter(unsafe {
                                    core::char::from_u32_unchecked(u32::from(u))
                                })
                            }));
                            ('\u{0635}', 17)
                        }
                    }
                } else {
                    debug_assert!(low_zeros);
                    // Only 12 of 14 bits used as of Unicode 16.
                    let offset = (((decomposition & !(0b11 << 30)) >> 16) as usize) - 1;
                    // Only 3 of 4 bits used as of Unicode 16.
                    let len_bits = decomposition & 0b1111;
                    let only_non_starters_in_trail = (decomposition & 0b10000) != 0;
                    if offset < self.scalars16.len() {
                        self.push_decomposition16(
                            offset,
                            (len_bits + 2) as usize,
                            only_non_starters_in_trail,
                            self.scalars16,
                        )
                    } else if offset < self.scalars16.len() + self.scalars24.len() {
                        self.push_decomposition32(
                            offset - self.scalars16.len(),
                            (len_bits + 1) as usize,
                            only_non_starters_in_trail,
                            self.scalars24,
                        )
                    } else if offset
                        < self.scalars16.len()
                            + self.scalars24.len()
                            + self.supplementary_scalars16.len()
                    {
                        self.push_decomposition16(
                            offset - (self.scalars16.len() + self.scalars24.len()),
                            (len_bits + 2) as usize,
                            only_non_starters_in_trail,
                            self.supplementary_scalars16,
                        )
                    } else {
                        self.push_decomposition32(
                            offset
                                - (self.scalars16.len()
                                    + self.scalars24.len()
                                    + self.supplementary_scalars16.len()),
                            (len_bits + 1) as usize,
                            only_non_starters_in_trail,
                            self.supplementary_scalars24,
                        )
                    }
                }
            }
        };
        // Either we're inside `Composition` or `self.pending.is_none()`.

        self.gather_and_sort_combining(combining_start);
        starter
    }

    // This function exists as a borrow check helper.
    #[inline(always)]
    fn sort_slice_by_ccc(slice: &mut [CharacterAndClass], trie: &'data T) {
        // We don't look up the canonical combining class for starters
        // of for single combining characters between starters. When
        // there's more than one combining character between starters,
        // we look up the canonical combining class for each character
        // exactly once.
        if slice.len() < 2 {
            return;
        }
        slice
            .iter_mut()
            .for_each(|cc| cc.set_ccc_from_trie_if_not_already_set(trie));
        slice.sort_by_key(|cc| cc.ccc());
    }

    #[cold]
    #[inline(never)]
    fn push_special_decomposition(buffer: &mut CombiningBuffer, c: char) {
        // The Tibetan special cases are starters that decompose into non-starters.
        let mapped = match c {
            '\u{0340}' => {
                // COMBINING GRAVE TONE MARK
                CharacterAndClass::new('\u{0300}', CCC_ABOVE)
            }
            '\u{0341}' => {
                // COMBINING ACUTE TONE MARK
                CharacterAndClass::new('\u{0301}', CCC_ABOVE)
            }
            '\u{0343}' => {
                // COMBINING GREEK KORONIS
                CharacterAndClass::new('\u{0313}', CCC_ABOVE)
            }
            '\u{0344}' => {
                // COMBINING GREEK DIALYTIKA TONOS
                buffer.push(CharacterAndClass::new('\u{0308}', CCC_ABOVE));
                CharacterAndClass::new('\u{0301}', CCC_ABOVE)
            }
            '\u{0F73}' => {
                // TIBETAN VOWEL SIGN II
                buffer.push(CharacterAndClass::new('\u{0F71}', ccc!(CCC129, 129)));
                CharacterAndClass::new('\u{0F72}', ccc!(CCC130, 130))
            }
            '\u{0F75}' => {
                // TIBETAN VOWEL SIGN UU
                buffer.push(CharacterAndClass::new('\u{0F71}', ccc!(CCC129, 129)));
                CharacterAndClass::new('\u{0F74}', ccc!(CCC132, 132))
            }
            '\u{0F81}' => {
                // TIBETAN VOWEL SIGN REVERSED II
                buffer.push(CharacterAndClass::new('\u{0F71}', ccc!(CCC129, 129)));
                CharacterAndClass::new('\u{0F80}', ccc!(CCC130, 130))
            }
            '\u{FF9E}' => {
                // HALFWIDTH KATAKANA VOICED SOUND MARK
                CharacterAndClass::new('\u{3099}', ccc!(KanaVoicing, 8))
            }
            '\u{FF9F}' => {
                // HALFWIDTH KATAKANA VOICED SOUND MARK
                CharacterAndClass::new('\u{309A}', ccc!(KanaVoicing, 8))
            }
            _ => {
                // GIGO case
                debug_assert!(false);
                CharacterAndClass::new_with_placeholder(REPLACEMENT_CHARACTER)
            }
        };
        buffer.push(mapped);
    }

    fn gather_and_sort_combining(&mut self, combining_start: usize) {
        // Not a `for` loop to avoid holding a mutable reference to `self` across
        // the loop body.
        while let Some(ch_and_trie_val) = self.delegate_next() {
            if !trie_value_has_ccc(ch_and_trie_val.trie_val) {
                self.pending = Some(ch_and_trie_val);
                break;
            } else if !trie_value_indicates_special_non_starter_decomposition(
                ch_and_trie_val.trie_val,
            ) {
                self.buffer
                    .push(CharacterAndClass::new_with_trie_value(ch_and_trie_val));
            } else {
                Self::push_special_decomposition(&mut self.buffer, ch_and_trie_val.character);
            }
        }
        // Slicing succeeds by construction; we've always ensured that `combining_start`
        // is in permissible range.
        #[expect(clippy::indexing_slicing)]
        Self::sort_slice_by_ccc(&mut self.buffer[combining_start..], self.delegate.trie());
    }
}

impl<'data, I, T, P> Iterator for DecompositionInner<'data, I, T, P>
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
    T: AbstractCodePointTrie<'data, u32> + 'data,
    P: IteratorPolicy,
{
    type Item = char;

    #[inline]
    fn next(&mut self) -> Option<char> {
        if let Some(ret) = self.buffer.get(self.buffer_pos).map(|c| c.character()) {
            self.buffer_pos += 1;
            if self.buffer_pos == self.buffer.len() {
                self.buffer.clear();
                self.buffer_pos = 0;
            }
            return Some(ret);
        }
        debug_assert_eq!(self.buffer_pos, 0);
        let c_and_trie_val = self.pending.take()?;
        Some(self.decomposing_next(c_and_trie_val))
    }
}

/// An iterator adaptor that turns an `Iterator` over `char` into
/// a lazily-decomposed and then canonically composed `char` sequence.
#[derive(Debug)]
pub struct Composition<'data, I>
where
    I: Iterator<Item = char>,
{
    inner: CompositionInner<
        'data,
        CharIterWithTrie<'data, Trie<'data>, u32, I>,
        Trie<'data>,
        Uax15Policy,
    >,
}

impl<I> Iterator for Composition<'_, I>
where
    I: Iterator<Item = char>,
{
    type Item = char;

    #[inline]
    fn next(&mut self) -> Option<char> {
        self.inner.next()
    }
}

#[derive(Debug)]
struct CompositionInner<'data, I, T, P>
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
    T: AbstractCodePointTrie<'data, u32>,
    P: IteratorPolicy,
{
    /// The decomposing part of the normalizer than operates before
    /// the canonical composition is performed on its output.
    decomposition: DecompositionInner<'data, I, T, P>,
    /// Non-Hangul canonical composition data.
    canonical_compositions: CanonicalCompositionsRef<'data>,
    /// To make `next()` yield in cases where there's a non-composing
    /// starter in the decomposition buffer, we put it here to let it
    /// wait for the next `next()` call (or a jump forward within the
    /// `next()` call).
    unprocessed_starter: Option<char>,
}

impl<'data, I, T, P> CompositionInner<'data, I, T, P>
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
    T: AbstractCodePointTrie<'data, u32>,
    P: IteratorPolicy,
{
    #[inline(always)]
    fn new(
        decomposition: DecompositionInner<'data, I, T, P>,
        canonical_compositions: CanonicalCompositionsRef<'data>,
    ) -> Self {
        Self {
            decomposition,
            canonical_compositions,
            unprocessed_starter: None,
        }
    }

    /// Performs canonical composition (including Hangul) on a pair of
    /// characters or returns `None` if these characters don't compose.
    /// Composition exclusions are taken into account.
    #[inline(always)]
    pub(crate) fn compose(&self, starter: char, second: char) -> Option<char> {
        self.canonical_compositions.compose(starter, second)
    }

    /// Performs canonical composition (including Hangul) on a pair of
    /// characters on the assumption that the second one is a starter
    /// or returns `None` if these characters don't compose.
    /// Composition exclusions are taken into account.
    ///
    /// The returned boolean can be true only if `char` a Hangul LV syllable.
    ///
    /// The argument `starter_is_lv` must be set either to false or to the value
    /// that this method previously returned alongside `starter`.
    #[inline(always)]
    pub(crate) fn compose_starter(
        &self,
        starter: char,
        second: char,
        starter_is_lv: bool,
    ) -> Option<(char, bool)> {
        self.canonical_compositions
            .compose_starter(starter, second, starter_is_lv)
    }
}

impl<'data, I, T, P> Iterator for CompositionInner<'data, I, T, P>
where
    I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
    T: AbstractCodePointTrie<'data, u32> + 'data,
    P: IteratorPolicy,
{
    type Item = char;

    #[inline]
    fn next(&mut self) -> Option<char> {
        let mut undecomposed_starter = CharacterAndTrieValue::new('\u{0}', 0); // The compiler can't figure out that this gets overwritten before use.
        if self.unprocessed_starter.is_none() {
            // The loop is only broken out of as goto forward
            #[expect(clippy::never_loop)]
            loop {
                if let Some((character, ccc)) = self
                    .decomposition
                    .buffer
                    .get(self.decomposition.buffer_pos)
                    .map(|c| c.character_and_ccc())
                {
                    self.decomposition.buffer_pos += 1;
                    if self.decomposition.buffer_pos == self.decomposition.buffer.len() {
                        self.decomposition.buffer.clear();
                        self.decomposition.buffer_pos = 0;
                    }
                    if ccc == CCC_NOT_REORDERED {
                        // Previous decomposition contains a starter. This must
                        // now become the `unprocessed_starter` for it to have
                        // a chance to compose with the upcoming characters.
                        //
                        // E.g. parenthesized Hangul in NFKC comes through here,
                        // but suitable composition exclusion could exercise this
                        // in NFC.
                        self.unprocessed_starter = Some(character);
                        break; // We already have a starter, so skip taking one from `pending`.
                    }
                    return Some(character);
                }
                debug_assert_eq!(self.decomposition.buffer_pos, 0);
                undecomposed_starter = self.decomposition.pending.take()?;
                if undecomposed_starter.potential_passthrough() {
                    // TODO(#2385): In the NFC case (moot for NFKC and UTS46), if the upcoming
                    // character is not below `decomposition_passthrough_bound` but is
                    // below `composition_passthrough_bound`, we read from the trie
                    // unnecessarily.
                    if let Some(upcoming) = self.decomposition.delegate_next_no_pending() {
                        let cannot_combine_backwards = !upcoming.can_combine_backwards();
                        self.decomposition.pending = Some(upcoming);
                        if cannot_combine_backwards {
                            // Fast-track succeeded!
                            return Some(undecomposed_starter.character);
                        }
                    } else {
                        // End of stream
                        return Some(undecomposed_starter.character);
                    }
                }
                break; // Not actually looping
            }
        }
        let mut starter = '\u{0}'; // The compiler can't figure out this gets overwritten before use.
                                   // It would be fancier to bundle `starter` and `starter_is_lv` into an encapsulating
                                   // struct, but that would result in lots of useless assignments to `starter_is_lv`.
                                   // Using `debug_assert!(!starter_is_lv);` a lot instead.
        let mut starter_is_lv = false;

        // The point of having this boolean is to have only one call site to
        // `self.decomposition.decomposing_next`, which is hopefully beneficial for
        // code size under inlining.
        let mut attempt_composition = false;
        loop {
            if let Some(unprocessed) = self.unprocessed_starter.take() {
                debug_assert_eq!(undecomposed_starter, CharacterAndTrieValue::new('\u{0}', 0));
                debug_assert_eq!(starter, '\u{0}');
                debug_assert!(!starter_is_lv);
                starter = unprocessed;
            } else {
                debug_assert_eq!(self.decomposition.buffer_pos, 0);
                let next_starter = self.decomposition.decomposing_next(undecomposed_starter);
                if !attempt_composition {
                    debug_assert!(!starter_is_lv);
                    starter = next_starter;
                } else if let Some((composed, is_lv)) =
                    self.compose_starter(starter, next_starter, starter_is_lv)
                {
                    // Normal non-enclosed Hangul is composed here.
                    starter_is_lv = is_lv;
                    starter = composed;
                } else {
                    // This is our yield point. We'll pick this up above in the
                    // next call to `next()`.
                    self.unprocessed_starter = Some(next_starter);
                    return Some(starter);
                }
            }
            // We first loop by index to avoid moving the contents of `buffer`, but
            // if there's a discontiguous match, we'll start modifying `buffer` instead.
            loop {
                let (character, ccc) = if let Some((character, ccc)) = self
                    .decomposition
                    .buffer
                    .get(self.decomposition.buffer_pos)
                    .map(|c| c.character_and_ccc())
                {
                    (character, ccc)
                } else {
                    self.decomposition.buffer.clear();
                    self.decomposition.buffer_pos = 0;
                    break;
                };
                starter_is_lv = false;
                // In NFKC, enclosed Hangul is recomposed here.
                if let Some(composed) = self.compose(starter, character) {
                    debug_assert!(!starter_is_lv);
                    starter = composed;
                    self.decomposition.buffer_pos += 1;
                    continue;
                }
                let mut most_recent_skipped_ccc = ccc;
                {
                    let _ = self
                        .decomposition
                        .buffer
                        .drain(0..self.decomposition.buffer_pos);
                }
                self.decomposition.buffer_pos = 0;
                if most_recent_skipped_ccc == CCC_NOT_REORDERED {
                    // We failed to compose a starter. Discontiguous match not allowed.
                    // We leave the starter in `buffer` for `next()` to find.
                    return Some(starter);
                }
                // TODO: Make use of `compose` having figured out that no other matches are
                // possible, either.
                let mut i = 1; // We have skipped one non-starter.
                while let Some((character, ccc)) = self
                    .decomposition
                    .buffer
                    .get(i)
                    .map(|c| c.character_and_ccc())
                {
                    if ccc == CCC_NOT_REORDERED {
                        // Discontiguous match not allowed.
                        return Some(starter);
                    }
                    debug_assert!(ccc >= most_recent_skipped_ccc);
                    if ccc != most_recent_skipped_ccc {
                        // `character` is a non-starter, so we could use a variant of
                        // `compose` that omits all the Hangul cases.
                        // TODO: Make use of above `compose` having already done the trie lookup,
                        // so the linear slice could be reused here.
                        if let Some(composed) = self.compose(starter, character) {
                            self.decomposition.buffer.remove(i);
                            debug_assert!(!starter_is_lv);
                            starter = composed;
                            continue;
                        }
                    }
                    most_recent_skipped_ccc = ccc;
                    i += 1;
                }
                break;
            }

            debug_assert_eq!(self.decomposition.buffer_pos, 0);

            if !self.decomposition.buffer.is_empty() {
                return Some(starter);
            }
            // Now we need to check if composition with an upcoming starter is possible.
            #[expect(clippy::unwrap_used)]
            if self.decomposition.pending.is_some() {
                // We know that `pending_starter` decomposes to start with a starter.
                // Otherwise, it would have been moved to `self.decomposition.buffer`
                // by `self.decomposing_next()`. We do this set lookup here in order
                // to get an opportunity to go back to the fast track.
                // Note that this check has to happen _after_ checking that `pending`
                // holds a character, because this flag isn't defined to be meaningful
                // when `pending` isn't holding a character.
                let pending = self.decomposition.pending.as_ref().unwrap();
                if !pending.can_combine_backwards() {
                    // Won't combine backwards anyway.
                    return Some(starter);
                }
                // Consume what we peeked. `unwrap` OK, because we checked `is_some()`
                // above.
                undecomposed_starter = self.decomposition.pending.take().unwrap();
                // The following line is OK, because we're about to loop back
                // to `self.decomposition.decomposing_next(c);`, which will
                // restore the between-`next()`-calls invariant of `pending`
                // before this function returns.
                attempt_composition = true;
                continue;
            }
            // End of input
            return Some(starter);
        }
    }
}

macro_rules! composing_normalize_to {
    ($(#[$meta:meta])*,
     $normalize_to:ident,
     $write:path,
     $slice:ty,
     $prolog:block,
     $always_valid_utf:literal,
     $as_slice:ident,
     $fast:block,
     $text:ident,
     $sink:ident,
     $composition:ident,
     $undecomposed_starter:ident,
     $pending_slice:ident,
     $len_utf:ident,
     $self:ident,
     $chars_with_trie:ident,
    ) => {
        $(#[$meta])*
        pub fn $normalize_to<W: $write + ?Sized>(
            &$self,
            $text: $slice,
            $sink: &mut W,
        ) -> core::fmt::Result {
            $prolog
            let mut $composition = $self.normalize_iter_private::<_, Trie, Uax15Policy>($text.$chars_with_trie($self.trie()));
            let _ = $composition.decomposition.init(); // Discard the U+0000.

            for cc in $composition.decomposition.buffer.drain(..) {
                $sink.write_char(cc.character())?;
            }

            'outer: loop {
                debug_assert_eq!($composition.decomposition.buffer_pos, 0);
                let mut $undecomposed_starter =
                    if let Some(pending) = $composition.decomposition.pending.take() {
                        pending
                    } else {
                        return Ok(());
                    };
                if $undecomposed_starter.potential_passthrough()
                {
                    // We don't know if a `REPLACEMENT_CHARACTER` occurred in the slice or
                    // was returned in response to an error by the iterator. Assume the
                    // latter for correctness even though it pessimizes the former.
                    if $always_valid_utf || $undecomposed_starter.character != REPLACEMENT_CHARACTER {
                        let $pending_slice = &$text[$text.len() - $composition.decomposition.delegate.$as_slice().len() - $undecomposed_starter.character.$len_utf()..];
                        // The `$fast` block must either:
                        // 1. Return due to reaching EOF
                        // 2. Leave a starter with its trie value in `$undecomposed_starter`
                        //    and, if there is still more input, leave the next character
                        //    and its trie value in `$composition.decomposition.pending`.
                        $fast
                    }
                }
                // Fast track above, full algorithm below
                let mut starter = $composition
                    .decomposition
                    .decomposing_next($undecomposed_starter);
                'bufferloop: loop {
                    // We first loop by index to avoid moving the contents of `buffer`, but
                    // if there's a discontiguous match, we'll start modifying `buffer` instead.
                    loop {
                        let (character, ccc) = if let Some((character, ccc)) = $composition
                            .decomposition
                            .buffer
                            .get($composition.decomposition.buffer_pos)
                            .map(|c| c.character_and_ccc())
                        {
                            (character, ccc)
                        } else {
                            $composition.decomposition.buffer.clear();
                            $composition.decomposition.buffer_pos = 0;
                            break;
                        };
                        // In NFKC, enclosed Hangul get recomposed here.
                        // Furthermore, in NFC if input has lv followed by t, lv gets
                        // decomposed above and recomposed here.
                        if let Some(composed) = $composition.compose(starter, character) {
                            starter = composed;
                            $composition.decomposition.buffer_pos += 1;
                            continue;
                        }
                        let mut most_recent_skipped_ccc = ccc;
                        if most_recent_skipped_ccc == CCC_NOT_REORDERED {
                            // We failed to compose a starter. Discontiguous match not allowed.
                            // Write the current `starter` we've been composing, make the unmatched
                            // starter in the buffer the new `starter` (we know it's been decomposed)
                            // and process the rest of the buffer with that as the starter.
                            $sink.write_char(starter)?;
                            starter = character;
                            $composition.decomposition.buffer_pos += 1;
                            continue 'bufferloop;
                        } else {
                            {
                                let _ = $composition
                                    .decomposition
                                    .buffer
                                    .drain(0..$composition.decomposition.buffer_pos);
                            }
                            $composition.decomposition.buffer_pos = 0;
                        }
                        let mut i = 1; // We have skipped one non-starter.
                        while let Some((character, ccc)) = $composition
                            .decomposition
                            .buffer
                            .get(i)
                            .map(|c| c.character_and_ccc())
                        {
                            if ccc == CCC_NOT_REORDERED {
                                // Discontiguous match not allowed.
                                $sink.write_char(starter)?;
                                for cc in $composition.decomposition.buffer.drain(..i) {
                                    $sink.write_char(cc.character())?;
                                }
                                starter = character;
                                {
                                    let removed = $composition.decomposition.buffer.remove(0);
                                    debug_assert_eq!(starter, removed.character());
                                }
                                debug_assert_eq!($composition.decomposition.buffer_pos, 0);
                                continue 'bufferloop;
                            }
                            debug_assert!(ccc >= most_recent_skipped_ccc);
                            if ccc != most_recent_skipped_ccc {
                                // `character` is a non-starter, so we could use a variant of
                                // `compose` that omits all the Hangul cases.
                                if let Some(composed) =
                                    $composition.compose(starter, character)
                                {
                                    $composition.decomposition.buffer.remove(i);
                                    starter = composed;
                                    continue;
                                }
                            }
                            most_recent_skipped_ccc = ccc;
                            i += 1;
                        }
                        break;
                    }
                    debug_assert_eq!($composition.decomposition.buffer_pos, 0);

                    if !$composition.decomposition.buffer.is_empty() {
                        $sink.write_char(starter)?;
                        for cc in $composition.decomposition.buffer.drain(..) {
                            $sink.write_char(cc.character())?;
                        }
                        // We had non-empty buffer, so can't compose with upcoming.
                        continue 'outer;
                    }
                    // We can loop back in case we compose a Hangul LV. Looping back
                    // makes this code much simpler than trying to have a special
                    // case that advances the underlying iterator in the branch that
                    // now says `continue;` below.
                    let mut starter_is_lv = false;
                    loop {
                        // Now we need to check if composition with an upcoming starter is possible.
                        if $composition.decomposition.pending.is_some() {
                            // We know that `pending_starter` decomposes to start with a starter.
                            // Otherwise, it would have been moved to `composition.decomposition.buffer`
                            // by `composition.decomposing_next()`. We do this set lookup here in order
                            // to get an opportunity to go back to the fast track.
                            // Note that this check has to happen _after_ checking that `pending`
                            // holds a character, because this flag isn't defined to be meaningful
                            // when `pending` isn't holding a character.
                            let pending = $composition.decomposition.pending.as_ref().unwrap();
                            if !pending.can_combine_backwards()
                            {
                                // Won't combine backwards anyway.
                                $sink.write_char(starter)?;
                                continue 'outer;
                            }
                            let pending_starter = $composition.decomposition.pending.take().unwrap();
                            let decomposed = $composition.decomposition.decomposing_next(pending_starter);
                            // Normal non-enclosed Hangul is composed here. The case where we have LV and T,
                            // but LV was not composed here previously is possible.
                            if let Some((composed, is_lv)) = $composition.compose_starter(starter, decomposed, starter_is_lv) {
                                starter = composed;
                                if is_lv && $composition.decomposition.buffer.is_empty() {
                                    starter_is_lv = true;
                                    // TODO: Put a Hangul fast-path that deals with conjoining jamo and ASCII
                                    // in a manner specialized for the UTF (i.e. not doing surrogate checks,
                                    // since surrogates are neither conjoining jamo nor ASCII) here.
                                    // https://github.com/unicode-org/icu4x/issues/7516
                                    continue;
                                }
                            } else {
                                $sink.write_char(starter)?;
                                starter = decomposed;
                            }
                            continue 'bufferloop;
                        }
                        break;
                    }
                    // End of input
                    $sink.write_char(starter)?;
                    return Ok(());
                } // 'bufferloop
            }
        }
    };
}

macro_rules! decomposing_normalize_to {
    ($(#[$meta:meta])*,
     $normalize_to:ident,
     $write:path,
     $slice:ty,
     $prolog:block,
     $as_slice:ident,
     $fast:block,
     $text:ident,
     $sink:ident,
     $decomposition:ident,
     $undecomposed_starter:ident,
     $pending_slice:ident,
     $outer:lifetime, // loop labels use lifetime tokens
     $self:ident,
     $chars_with_trie:ident,
    ) => {
        $(#[$meta])*
        pub fn $normalize_to<W: $write + ?Sized>(
            &$self,
            $text: $slice,
            $sink: &mut W,
        ) -> core::fmt::Result {
            $prolog

            let mut $decomposition = $self.normalize_iter_private::<_, Trie, Uax15Policy>($text.$chars_with_trie($self.trie()));
            let _ = $decomposition.init(); // Discard the U+0000.

            $outer: loop {
                for cc in $decomposition.buffer.drain(..) {
                    $sink.write_char(cc.character())?;
                }
                debug_assert_eq!($decomposition.buffer_pos, 0);
                let mut $undecomposed_starter = if let Some(pending) = $decomposition.pending.take() {
                    pending
                } else {
                    return Ok(());
                };
                loop {
                    if $undecomposed_starter.starter_and_decomposes_to_self() {
                        // Don't bother including `undecomposed_starter` in a contiguous buffer
                        // write: Just write it right away:
                        $sink.write_char($undecomposed_starter.character)?;

                        let $pending_slice = $decomposition.delegate.$as_slice();
                        $fast
                    }
                    debug_assert!($decomposition.pending.is_none());
                    let c_and_trie_val_unless_at_end = if let Some((upcoming, trie_val)) = $decomposition.delegate.next() {
                        if likely(!decomposition_starts_with_non_starter(trie_val)) {
                            Some(CharacterAndTrieValue::new(upcoming, trie_val))
                        } else {
                            $decomposition.pending = Some(CharacterAndTrieValue::new(upcoming, trie_val));
                            break;
                        }
                    } else {
                        None
                    };
                    // The upcoming character cannot sort into the tail of this decomposition,
                    // so, for performance, let's write decomposition directly here without
                    // going via `$decomposition.buffer`. This wall of (edited) copypaste is
                    // crucial for performance competitiveness with ICU4C.

                    // Start edited copypaste from `decomposing_next`

                    let c = $undecomposed_starter.character;
                    // See trie-value-format.md
                    let decomposition = $undecomposed_starter.trie_val;
                    // The REPLACEMENT CHARACTER has `NON_ROUND_TRIP_MARKER` set,
                    // and that flag needs to be ignored here.
                    if unlikely((decomposition & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER)) == 0) {
                        // The character is its own decomposition
                        $sink.write_char(c)?;
                    } else {
                        let high_zeros = (decomposition & HIGH_ZEROS_MASK) == 0;
                        let low_zeros = (decomposition & LOW_ZEROS_MASK) == 0;
                        if !high_zeros && !low_zeros {
                            // Decomposition into two BMP characters: starter and non-starter
                            let starter = char_from_u32(decomposition & 0x7FFF);
                            let combining = char_from_u32((decomposition >> 15) & 0x7FFF);
                            $sink.write_char(starter)?;
                            $sink.write_char(combining)?;
                        } else if high_zeros {
                            // Do the check by looking at `c` instead of looking at a marker
                            // in `singleton` below, because if we looked at the trie value,
                            // we'd still have to check that `c` is in the Hangul syllable
                            // range in order for the subsequent interpretations as `char`
                            // to be safe.
                            // Alternatively, `FDFA_MARKER` and the Hangul marker could
                            // be unified. That would add a branch for Hangul and remove
                            // a branch from singleton decompositions. It seems more
                            // important to favor Hangul syllables than singleton
                            // decompositions.
                            // Note that it would be valid to hoist this Hangul check
                            // one or even two steps earlier in this check hierarchy.
                            // Right now, it's assumed the kind of decompositions into
                            // BMP starter and non-starter, which occur in many languages,
                            // should be checked before Hangul syllables, which are about
                            // one language specifically. Hopefully, we get some
                            // instruction-level parallelism out of the disjointness of
                            // operations on `c` and `decomposition`.
                            let hangul_offset = u32::from(c).wrapping_sub(HANGUL_S_BASE); // SIndex in the spec
                            if hangul_offset < HANGUL_S_COUNT {
                                debug_assert_eq!(decomposition, 1);
                                // Hangul syllable
                                // The math here comes from page 144 of Unicode 14.0
                                let l = hangul_offset / HANGUL_N_COUNT;
                                let v = (hangul_offset % HANGUL_N_COUNT) / HANGUL_T_COUNT;
                                let t = hangul_offset % HANGUL_T_COUNT;

                                // The unsafe blocks here are OK, because the values stay
                                // within the Hangul jamo block and, therefore, the scalar
                                // value range by construction.
                                $sink.write_char(unsafe { core::char::from_u32_unchecked(HANGUL_L_BASE + l) })?;
                                $sink.write_char(unsafe {
                                    core::char::from_u32_unchecked(HANGUL_V_BASE + v)
                                })?;
                                if t != 0 {
                                    $sink.write_char(unsafe {
                                        core::char::from_u32_unchecked(HANGUL_T_BASE + t)
                                    })?;
                                }
                            } else {
                                let singleton = decomposition as u16;
                                if singleton != FDFA_MARKER {
                                    // Decomposition into one BMP character
                                    let starter = char_from_u16(singleton);
                                    $sink.write_char(starter)?;
                                } else {
                                    // Special case for the NFKD form of U+FDFA.
                                    $sink.write_char('\u{0635}')?;
                                    for u in FDFA_NFKD {
                                        // SAFETY: `FDFA_NFKD` is known not to contain
                                        // surrogates.
                                        $sink.write_char(unsafe { core::char::from_u32_unchecked(u32::from(u)) })?;
                                    }
                                }
                            }
                        } else {
                            debug_assert!(low_zeros);
                            // Only 12 of 14 bits used as of Unicode 16.
                            let offset = (((decomposition & !(0b11 << 30)) >> 16) as usize) - 1;
                            // Only 3 of 4 bits used as of Unicode 16.
                            let len_bits = decomposition & 0b1111;
                            if let Some(subslice) = $decomposition.scalars16.get_subslice(offset..offset+((len_bits + 2) as usize)) {
                                for u in subslice.iter() {
                                    $sink.write_char(char_from_u16(u))?;
                                }
                            } else {
                                let offset = offset - $decomposition.scalars16.len();
                                if let Some(subslice) = $decomposition.scalars24.get_subslice(offset..offset+((len_bits + 1) as usize)) {
                                    for c in subslice.iter() {
                                        $sink.write_char(c)?;
                                    }
                                } else {
                                    let offset = offset - $decomposition.scalars24.len();
                                    if let Some(subslice) = $decomposition.supplementary_scalars16.get_subslice(offset..offset+((len_bits + 2) as usize)) {
                                        for u in subslice.iter() {
                                            $sink.write_char(char_from_u16(u))?;
                                        }
                                    } else {
                                        let offset = offset - $decomposition.supplementary_scalars16.len();
                                        if let Some(subslice) = $decomposition.supplementary_scalars24.get_subslice(offset..offset+((len_bits + 1) as usize)) {
                                            for c in subslice.iter() {
                                                $sink.write_char(c)?;
                                            }
                                        } else {
                                            // GIGO case
                                            debug_assert!(false);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // End edited copypaste from `decomposing_next`

                    if let Some(c_and_trie_val) = c_and_trie_val_unless_at_end {
                        $undecomposed_starter = c_and_trie_val;
                        continue;
                    }
                    return Ok(());
                }
                let starter = $decomposition.decomposing_next($undecomposed_starter);
                $sink.write_char(starter)?;
            }
        }
    };
}

macro_rules! normalizer_methods {
    () => {
        /// Normalize a string slice into a `Cow<'a, str>`.
        pub fn normalize<'a>(&self, text: &'a str) -> Cow<'a, str> {
            let (head, tail) = self.split_normalized(text);
            if tail.is_empty() {
                return Cow::Borrowed(head);
            }
            let mut ret = String::new();
            ret.reserve(text.len());
            ret.push_str(head);
            let _ = self.normalize_to(tail, &mut ret);
            Cow::Owned(ret)
        }

        /// Split a string slice into maximum normalized prefix and unnormalized suffix
        /// such that the concatenation of the prefix and the normalization of the suffix
        /// is the normalization of the whole input.
        pub fn split_normalized<'a>(&self, text: &'a str) -> (&'a str, &'a str) {
            let up_to = self.is_normalized_up_to(text);
            text.split_at_checked(up_to).unwrap_or_else(|| {
                // Internal bug, not even GIGO, never supposed to happen
                debug_assert!(false);
                ("", text)
            })
        }

        /// Return the index a string slice is normalized up to.
        fn is_normalized_up_to(&self, text: &str) -> usize {
            let mut sink = IsNormalizedSinkStr::new(text);
            let _ = self.normalize_to(text, &mut sink);
            text.len() - sink.remaining_len()
        }

        /// Check whether a string slice is normalized.
        pub fn is_normalized(&self, text: &str) -> bool {
            self.is_normalized_up_to(text) == text.len()
        }

        /// Normalize a slice of potentially-invalid UTF-16 into a `Cow<'a, [u16]>`.
        ///
        /// Unpaired surrogates are mapped to the REPLACEMENT CHARACTER
        /// before normalizing.
        ///
        /// ✨ *Enabled with the `utf16_iter` Cargo feature.*
        #[cfg(feature = "utf16_iter")]
        pub fn normalize_utf16<'a>(&self, text: &'a [u16]) -> Cow<'a, [u16]> {
            let (head, tail) = self.split_normalized_utf16(text);
            if tail.is_empty() {
                return Cow::Borrowed(head);
            }
            let mut ret = alloc::vec::Vec::with_capacity(text.len());
            ret.extend_from_slice(head);
            let _ = self.normalize_utf16_to(tail, &mut ret);
            Cow::Owned(ret)
        }

        /// Split a slice of potentially-invalid UTF-16 into maximum normalized (and valid)
        /// prefix and unnormalized suffix such that the concatenation of the prefix and the
        /// normalization of the suffix is the normalization of the whole input.
        ///
        /// ✨ *Enabled with the `utf16_iter` Cargo feature.*
        #[cfg(feature = "utf16_iter")]
        pub fn split_normalized_utf16<'a>(&self, text: &'a [u16]) -> (&'a [u16], &'a [u16]) {
            let up_to = self.is_normalized_utf16_up_to(text);
            text.split_at_checked(up_to).unwrap_or_else(|| {
                // Internal bug, not even GIGO, never supposed to happen
                debug_assert!(false);
                (&[], text)
            })
        }

        /// Return the index a slice of potentially-invalid UTF-16 is normalized up to.
        ///
        /// ✨ *Enabled with the `utf16_iter` Cargo feature.*
        #[cfg(feature = "utf16_iter")]
        fn is_normalized_utf16_up_to(&self, text: &[u16]) -> usize {
            let mut sink = IsNormalizedSinkUtf16::new(text);
            let _ = self.normalize_utf16_to(text, &mut sink);
            text.len() - sink.remaining_len()
        }

        /// Checks whether a slice of potentially-invalid UTF-16 is normalized.
        ///
        /// Unpaired surrogates are treated as the REPLACEMENT CHARACTER.
        ///
        /// ✨ *Enabled with the `utf16_iter` Cargo feature.*
        #[cfg(feature = "utf16_iter")]
        pub fn is_normalized_utf16(&self, text: &[u16]) -> bool {
            self.is_normalized_utf16_up_to(text) == text.len()
        }

        /// Normalize a slice of potentially-invalid UTF-8 into a `Cow<'a, str>`.
        ///
        /// Ill-formed byte sequences are mapped to the REPLACEMENT CHARACTER
        /// according to the WHATWG Encoding Standard.
        ///
        /// ✨ *Enabled with the `utf8_iter` Cargo feature.*
        #[cfg(feature = "utf8_iter")]
        pub fn normalize_utf8<'a>(&self, text: &'a [u8]) -> Cow<'a, str> {
            let (head, tail) = self.split_normalized_utf8(text);
            if tail.is_empty() {
                return Cow::Borrowed(head);
            }
            let mut ret = String::new();
            ret.reserve(text.len());
            ret.push_str(head);
            let _ = self.normalize_utf8_to(tail, &mut ret);
            Cow::Owned(ret)
        }

        /// Split a slice of potentially-invalid UTF-8 into maximum normalized (and valid)
        /// prefix and unnormalized suffix such that the concatenation of the prefix and the
        /// normalization of the suffix is the normalization of the whole input.
        ///
        /// ✨ *Enabled with the `utf8_iter` Cargo feature.*
        #[cfg(feature = "utf8_iter")]
        pub fn split_normalized_utf8<'a>(&self, text: &'a [u8]) -> (&'a str, &'a [u8]) {
            let up_to = self.is_normalized_utf8_up_to(text);
            let (head, tail) = text.split_at_checked(up_to).unwrap_or_else(|| {
                // Internal bug, not even GIGO, never supposed to happen
                debug_assert!(false);
                (&[], text)
            });
            // SAFETY: The normalization check also checks for
            // UTF-8 well-formedness.
            (unsafe { core::str::from_utf8_unchecked(head) }, tail)
        }

        /// Return the index a slice of potentially-invalid UTF-8 is normalized up to
        ///
        /// ✨ *Enabled with the `utf8_iter` Cargo feature.*
        #[cfg(feature = "utf8_iter")]
        fn is_normalized_utf8_up_to(&self, text: &[u8]) -> usize {
            let mut sink = IsNormalizedSinkUtf8::new(text);
            let _ = self.normalize_utf8_to(text, &mut sink);
            text.len() - sink.remaining_len()
        }

        /// Check if a slice of potentially-invalid UTF-8 is normalized.
        ///
        /// Ill-formed byte sequences are mapped to the REPLACEMENT CHARACTER
        /// according to the WHATWG Encoding Standard before checking.
        ///
        /// ✨ *Enabled with the `utf8_iter` Cargo feature.*
        #[cfg(feature = "utf8_iter")]
        pub fn is_normalized_utf8(&self, text: &[u8]) -> bool {
            self.is_normalized_utf8_up_to(text) == text.len()
        }
    };
}

/// Borrowed version of a normalizer for performing decomposing normalization.
#[derive(Debug)]
pub struct DecomposingNormalizerBorrowed<'a> {
    decompositions: &'a DecompositionData<'a>,
    tables: &'a DecompositionTables<'a>,
    supplementary_tables: Option<&'a DecompositionTables<'a>>,
    decomposition_passthrough_bound: u8, // never above 0xC0
    composition_passthrough_bound: u16,  // never above 0x0300
}

impl DecomposingNormalizerBorrowed<'static> {
    /// Cheaply converts a [`DecomposingNormalizerBorrowed<'static>`] into a [`DecomposingNormalizer`].
    ///
    /// Note: Due to branching and indirection, using [`DecomposingNormalizer`] might inhibit some
    /// compile-time optimizations that are possible with [`DecomposingNormalizerBorrowed`].
    pub const fn static_to_owned(self) -> DecomposingNormalizer {
        DecomposingNormalizer {
            decompositions: DataPayload::from_static_ref(self.decompositions),
            tables: DataPayload::from_static_ref(self.tables),
            supplementary_tables: if let Some(s) = self.supplementary_tables {
                // `map` not available in const context
                Some(DataPayload::from_static_ref(s))
            } else {
                None
            },
            decomposition_passthrough_bound: self.decomposition_passthrough_bound,
            composition_passthrough_bound: self.composition_passthrough_bound,
        }
    }

    /// NFD constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfd() -> Self {
        const _: () = assert!(
            provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1
                .scalars16
                .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1
                    .scalars24
                    .const_len()
                <= 0xFFF,
            "future extension"
        );

        DecomposingNormalizerBorrowed {
            decompositions: provider::Baked::SINGLETON_NORMALIZER_NFD_DATA_V1,
            tables: provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1,
            supplementary_tables: None,
            decomposition_passthrough_bound: 0xC0,
            composition_passthrough_bound: 0x0300,
        }
    }

    /// NFKD constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfkd() -> Self {
        const _: () = assert!(
            provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1
                .scalars16
                .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1
                    .scalars24
                    .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFKD_TABLES_V1
                    .scalars16
                    .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFKD_TABLES_V1
                    .scalars24
                    .const_len()
                <= 0xFFF,
            "future extension"
        );

        // TODO: Perhaps hard-code these?

        const _: () = assert!(
            provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1.passthrough_cap <= 0x0300,
            "invalid"
        );

        const _: () = assert!(
            provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1.passthrough_cap >= 0x80,
            "invalid"
        );

        let decomposition_capped =
            if provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1.passthrough_cap < 0xC0 {
                provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1.passthrough_cap
            } else {
                0xC0
            };
        let composition_capped =
            if provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1.passthrough_cap < 0x0300 {
                provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1.passthrough_cap
            } else {
                0x0300
            };

        DecomposingNormalizerBorrowed {
            decompositions: provider::Baked::SINGLETON_NORMALIZER_NFKD_DATA_V1,
            tables: provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1,
            supplementary_tables: Some(provider::Baked::SINGLETON_NORMALIZER_NFKD_TABLES_V1),
            decomposition_passthrough_bound: decomposition_capped as u8,
            composition_passthrough_bound: composition_capped,
        }
    }

    #[cfg(feature = "compiled_data")]
    pub(crate) const fn new_uts46_decomposed() -> Self {
        const _: () = assert!(
            provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1
                .scalars16
                .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1
                    .scalars24
                    .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFKD_TABLES_V1
                    .scalars16
                    .const_len()
                + provider::Baked::SINGLETON_NORMALIZER_NFKD_TABLES_V1
                    .scalars24
                    .const_len()
                <= 0xFFF,
            "future extension"
        );

        const _: () = assert!(
            provider::Baked::SINGLETON_NORMALIZER_UTS46_DATA_V1.passthrough_cap <= 0x0300,
            "invalid"
        );

        // Is less than 0x80!

        let decomposition_capped =
            if provider::Baked::SINGLETON_NORMALIZER_UTS46_DATA_V1.passthrough_cap < 0xC0 {
                provider::Baked::SINGLETON_NORMALIZER_UTS46_DATA_V1.passthrough_cap
            } else {
                0xC0
            };
        let composition_capped =
            if provider::Baked::SINGLETON_NORMALIZER_UTS46_DATA_V1.passthrough_cap < 0x0300 {
                provider::Baked::SINGLETON_NORMALIZER_UTS46_DATA_V1.passthrough_cap
            } else {
                0x0300
            };

        DecomposingNormalizerBorrowed {
            decompositions: provider::Baked::SINGLETON_NORMALIZER_UTS46_DATA_V1,
            tables: provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1,
            supplementary_tables: Some(provider::Baked::SINGLETON_NORMALIZER_NFKD_TABLES_V1),
            decomposition_passthrough_bound: decomposition_capped as u8,
            composition_passthrough_bound: composition_capped,
        }
    }
}

impl<'data> DecomposingNormalizerBorrowed<'data> {
    /// NFD constructor using already-loaded data.
    ///
    /// This constructor is intended for use by collations.
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[doc(hidden)]
    pub fn new_with_data(
        decompositions: &'data DecompositionData<'data>,
        tables: &'data DecompositionTables<'data>,
    ) -> Self {
        Self {
            decompositions,
            tables,
            supplementary_tables: None,
            decomposition_passthrough_bound: 0xC0,
            composition_passthrough_bound: 0x0300,
        }
    }

    /// Wraps a delegate iterator into a decomposing iterator
    /// adapter by using the data already held by this normalizer.
    #[inline]
    pub fn normalize_iter<I: Iterator<Item = char>>(&self, iter: I) -> Decomposition<'data, I> {
        let mut ret = Decomposition {
            inner: self.normalize_iter_private(CharIterWithTrie::new(iter, self.trie())),
        };
        ret.inner.init(); // Discard the U+0000.
        ret
    }

    /// There's an extra U+FFFD at the start. The caller must deal with it.
    #[inline(always)]
    fn normalize_iter_private<
        I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
        T: AbstractCodePointTrie<'data, u32> + 'data,
        P: IteratorPolicy,
    >(
        &self,
        iter: I,
    ) -> DecompositionInner<'data, I, T, P> {
        DecompositionInner::new_with_supplements(iter, self.tables, self.supplementary_tables)
    }

    fn trie<T: AbstractCodePointTrie<'data, u32>>(&self) -> &'data T
    where
        &'data T: TryFrom<&'data CodePointTrie<'data, u32>>,
    {
        <&T>::try_from(&self.decompositions.trie)
            .unwrap_or_else(|_| unreachable!("Incompatible data"))
    }

    normalizer_methods!();

    decomposing_normalize_to!(
        /// Normalize a string slice into a `Write` sink.
        ,
        normalize_to,
        core::fmt::Write,
        &str,
        {
        },
        as_str,
        {
            'fast: loop {
                if let Some((mut upcoming, mut trie_val)) = decomposition.delegate.next() {
                    if starter_and_decomposes_to_self_impl(trie_val) {
                        continue 'fast;
                    }

                    // Try to handle a single  combining mark followed by a starter in a way
                    // that avoids `decomposition.buffer`.

                    if likely(trie_value_indicates_non_decomposing_non_starter(trie_val)) {
                        // This loop is only broken out of as goto forward.
                        #[expect(clippy::never_loop)]
                        loop {
                            if let Some((after_mark, after_mark_trie_value)) = decomposition.delegate.next() {
                                if likely(starter_and_decomposes_to_self_impl(after_mark_trie_value)) {
                                    continue 'fast;
                                }
                                if likely(!decomposition_starts_with_non_starter(after_mark_trie_value)) {
                                    // We have a decomposing starter.
                                    upcoming = after_mark;
                                    trie_val = after_mark_trie_value;
                                    break;
                                }
                                // We have another combining mark.
                                // We put the first combining mark, which we know doesn't decompose,
                                // directly into the buffer. We put the second one, which might decompose,
                                // into `decomposition.pending` for `gather_and_sort_combining` to deal
                                // with.

                                let consumed_so_far_slice = &pending_slice[..pending_slice.len()
                                    - decomposition.delegate.as_str().len()
                                    - upcoming.len_utf8()
                                    - after_mark.len_utf8()];
                                sink.write_str(consumed_so_far_slice)?;

                                debug_assert!(decomposition.buffer.is_empty());

                                // Narrowing `trie_value` to `u8` is OK, because we already checked
                                // `decomposition_starts_with_non_starter`.
                                debug_assert!(trie_value_has_ccc(trie_val));
                                decomposition.buffer.push(CharacterAndClass::new(upcoming, CanonicalCombiningClass::from_icu4c_value(trie_val as u8)));

                                decomposition.pending = Some(CharacterAndTrieValue::new(after_mark, after_mark_trie_value));
                                decomposition.gather_and_sort_combining(0);
                                continue 'outer;
                            }
                            // End of stream
                            sink.write_str(pending_slice)?;
                            return Ok(());
                        }
                    }
                    // End skipping over single combining mark

                    let upcoming_with_trie_value = CharacterAndTrieValue::new(upcoming, trie_val);
                    let consumed_so_far_slice = &pending_slice[..pending_slice.len()
                        - decomposition.delegate.as_str().len()
                        - upcoming.len_utf8()];
                    sink.write_str(consumed_so_far_slice)?;

                    // Now let's figure out if we got a starter or a non-starter.
                    if decomposition_starts_with_non_starter(
                        trie_val,
                    ) {
                        // Let this trie value to be reprocessed in case it is
                        // one of the rare decomposing ones.
                        decomposition.pending = Some(upcoming_with_trie_value);
                        decomposition.gather_and_sort_combining(0);
                        continue 'outer;
                    }
                    undecomposed_starter = upcoming_with_trie_value;
                    debug_assert!(decomposition.pending.is_none());
                    break 'fast;
                }
                // End of stream
                sink.write_str(pending_slice)?;
                return Ok(());
            }
        },
        text,
        sink,
        decomposition,
        undecomposed_starter,
        pending_slice,
        'outer,
        self,
        chars_with_trie_default_for_ascii,
    );

    decomposing_normalize_to!(
        /// Normalize a slice of potentially-invalid UTF-8 into a `Write` sink.
        ///
        /// Ill-formed byte sequences are mapped to the REPLACEMENT CHARACTER
        /// according to the WHATWG Encoding Standard.
        ///
        /// ✨ *Enabled with the `utf8_iter` Cargo feature.*
        #[cfg(feature = "utf8_iter")]
        ,
        normalize_utf8_to,
        core::fmt::Write,
        &[u8],
        {
        },
        as_slice,
        {
            'fast: loop {
                if let Some((mut upcoming, mut trie_val)) = decomposition.delegate.next() {
                    if starter_and_decomposes_to_self_except_replacement(trie_val) {
                        // Note: The trie value of the REPLACEMENT CHARACTER is
                        // intentionally formatted to fail the
                        // `starter_and_decomposes_to_self` test even though it
                        // really is a starter that decomposes to self. This
                        // Allows moving the branch on REPLACEMENT CHARACTER
                        // below this `continue`.
                        continue 'fast;
                    }

                    // Try to handle a single  combining mark followed by a starter in a way
                    // that avoids `decomposition.buffer`.

                    if likely(trie_value_indicates_non_decomposing_non_starter(trie_val)) {
                        // This loop is only broken out of as goto forward.
                        #[expect(clippy::never_loop)]
                        loop {
                            if let Some((after_mark, after_mark_trie_value)) = decomposition.delegate.next() {
                                if likely(starter_and_decomposes_to_self_except_replacement(after_mark_trie_value)) {
                                    continue 'fast;
                                }
                                if likely(!decomposition_starts_with_non_starter(after_mark_trie_value)) {
                                    // We have a decomposing starter.
                                    upcoming = after_mark;
                                    trie_val = after_mark_trie_value;
                                    break;
                                }
                                // We have another combining mark.
                                // We put the first combining mark, which we know doesn't decompose,
                                // directly into the buffer. We put the second one, which might decompose,
                                // into `decomposition.pending` for `gather_and_sort_combining` to deal
                                // with.

                                // `len_utf8` is OK, since knowing that we have two combining marks
                                // means that neither is U+FFFD, so we didn't have a UTF-8 error.
                                debug_assert_ne!(upcoming, '\u{FFFD}');
                                debug_assert_ne!(after_mark, '\u{FFFD}');
                                #[expect(clippy::indexing_slicing)]
                                let consumed_so_far_slice = &pending_slice[..pending_slice.len()
                                    - decomposition.delegate.as_slice().len()
                                    - upcoming.len_utf8()
                                    - after_mark.len_utf8()];
                                sink.write_str(unsafe { core::str::from_utf8_unchecked(consumed_so_far_slice) } )?;

                                debug_assert!(decomposition.buffer.is_empty());

                                // Narrowing `trie_value` to `u8` is OK, because we already checked
                                // `decomposition_starts_with_non_starter`.
                                debug_assert!(trie_value_has_ccc(trie_val));
                                decomposition.buffer.push(CharacterAndClass::new(upcoming, CanonicalCombiningClass::from_icu4c_value(trie_val as u8)));

                                decomposition.pending = Some(CharacterAndTrieValue::new(after_mark, after_mark_trie_value));
                                decomposition.gather_and_sort_combining(0);
                                continue 'outer;
                            }
                            // End of stream
                            sink.write_str(unsafe { core::str::from_utf8_unchecked(pending_slice) } )?;
                            return Ok(());
                        }
                    }
                    // End skipping over single combining mark

                    let upcoming_with_trie_value = CharacterAndTrieValue::new(upcoming, trie_val);
                    if unlikely(upcoming == REPLACEMENT_CHARACTER) {
                        // We might have an error, so fall out of the fast path.

                        // Since the U+FFFD might signify an error, we can't
                        // assume `upcoming.len_utf8()` for the backoff length.
                        #[expect(clippy::indexing_slicing)]
                        let mut consumed_so_far = pending_slice[..pending_slice.len() - decomposition.delegate.as_slice().len()].chars();
                        let back = consumed_so_far.next_back();
                        debug_assert_eq!(back, Some(REPLACEMENT_CHARACTER));
                        let consumed_so_far_slice = consumed_so_far.as_slice();
                        sink.write_str(unsafe { core::str::from_utf8_unchecked(consumed_so_far_slice) } )?;

                        // We could call `gather_and_sort_combining` here and
                        // `continue 'outer`, but this should be better for code
                        // size.
                        undecomposed_starter = upcoming_with_trie_value;
                        debug_assert!(decomposition.pending.is_none());
                        break 'fast;
                    }

                    #[expect(clippy::indexing_slicing)]
                    let consumed_so_far_slice = &pending_slice[..pending_slice.len()
                        - decomposition.delegate.as_slice().len()
                        - upcoming.len_utf8()];
                    sink.write_str(unsafe { core::str::from_utf8_unchecked(consumed_so_far_slice) } )?;

                    // Now let's figure out if we got a starter or a non-starter.
                    if decomposition_starts_with_non_starter(
                        upcoming_with_trie_value.trie_val,
                    ) {
                        // Let this trie value to be reprocessed in case it is
                        // one of the rare decomposing ones.
                        decomposition.pending = Some(upcoming_with_trie_value);
                        decomposition.gather_and_sort_combining(0);
                        continue 'outer;
                    }
                    undecomposed_starter = upcoming_with_trie_value;
                    debug_assert!(decomposition.pending.is_none());
                    break 'fast;
                }
                // End of stream
                sink.write_str(unsafe { core::str::from_utf8_unchecked(pending_slice) } )?;
                return Ok(());
            }
        },
        text,
        sink,
        decomposition,
        undecomposed_starter,
        pending_slice,
        'outer,
        self,
        chars_with_trie_default_for_ascii,
    );

    decomposing_normalize_to!(
        /// Normalize a slice of potentially-invalid UTF-16 into a `Write16` sink.
        ///
        /// Unpaired surrogates are mapped to the REPLACEMENT CHARACTER
        /// before normalizing.
        ///
        /// ✨ *Enabled with the `utf16_iter` Cargo feature.*
        #[cfg(feature = "utf16_iter")]
        ,
        normalize_utf16_to,
        write16::Write16,
        &[u16],
        {
            sink.size_hint(text.len())?;
        },
        as_slice,
        {
            // This loop is only broken out of as goto forward and only as release-build recovery from
            // detecting an internal bug without panic. (In debug builds, internal bugs panic instead.)
            #[expect(clippy::never_loop)]
            'fastwrap: loop {
                // Commented out `code_unit_iter` and used `ptr` and `end` to
                // work around https://github.com/rust-lang/rust/issues/144684 .
                //
                // let mut code_unit_iter = decomposition.delegate.as_slice().iter();
                let delegate_as_slice = decomposition.delegate.as_slice();
                let mut ptr: *const u16 = delegate_as_slice.as_ptr();
                // SAFETY: materializing a pointer immediately past the end of an
                // allocation is OK.
                let end: *const u16 = unsafe { ptr.add(delegate_as_slice.len()) };
                let decomposition_passthrough_bound = u16::from(self.decomposition_passthrough_bound);
                'fast: loop {
                    // if let Some(&upcoming_code_unit) = code_unit_iter.next() {
                    if likely(ptr != end) {
                        // SAFETY: We just checked that `ptr` has not reached `end`.
                        // `ptr` always advances by one, and we always have a check
                        // per advancement.
                        let upcoming_code_unit = unsafe { *ptr };
                        // SAFETY: Since `ptr` hadn't reached `end`, yet, advancing
                        // by one points to the same allocation or to immediately
                        // after, which is OK.
                        ptr = unsafe { ptr.add(1) };

                        // The performance of what logically is supposed to be this
                        // branch is _incredibly_ brittle and what LLVM ends up doing
                        // that affects the performance of what's logically about this
                        // decision can swing to double/halve the throughput for Basic
                        // Latin in ways that are completely unintuitive. Basically _any_
                        // change to _any_ code that participates in how LLVM sees the
                        // code around here can make the perf fall over. In seems that
                        // manually annotating this branch as likely has worse effects
                        // on non-Basic-Latin input that the case where LLVM just happens to
                        // do the right thing.
                        //
                        // What happens with this branch may depend on what sink type
                        // this code is monomorphized over.
                        //
                        // What a terrible sink of developer time!
                        if upcoming_code_unit < decomposition_passthrough_bound {
                            continue 'fast;
                        }
                        // We might be doing a trie lookup by surrogate. Surrogates get
                        // a decomposition to U+FFFD.
                        let mut trie_value = decomposition.delegate.trie().bmp(upcoming_code_unit);
                        if likely(starter_and_decomposes_to_self_impl(trie_value)) {
                            continue 'fast;
                        }

                        let mut upcoming32 = u32::from(upcoming_code_unit);

                        // We might now be looking at a surrogate.
                        // The loop is only broken out of as goto forward
                        #[expect(clippy::never_loop)]
                        'surrogateloop: loop {

                            // Try to handle a single BMP combining mark followed by a starter in a way
                            // that avoids `decomposition.buffer`. Crucial for perf competitiveness with ICU4C.

                            if likely(trie_value_indicates_non_decomposing_non_starter(trie_value)) {
                                if likely(ptr != end) {
                                    // SAFETY: We just checked that `ptr` has not reached `end`.
                                    // `ptr` always advances by one, and we always have a check
                                    // per advancement.
                                    let after_mark_code_unit = unsafe { *ptr };
                                    // SAFETY: Since `ptr` hadn't reached `end`, yet, advancing
                                    // by one points to the same allocation or to immediately
                                    // after, which is OK.
                                    ptr = unsafe { ptr.add(1) };
                                    let after_mark_trie_value = decomposition.delegate.trie().bmp(after_mark_code_unit);
                                    if likely(starter_and_decomposes_to_self_impl(after_mark_trie_value)) {
                                        continue 'fast;
                                    }
                                    if unlikely(in_inclusive_range16(after_mark_code_unit, 0xD800, 0xDFFF)) {
                                        // We have a surrogate. Too complicated to deal with, because
                                        // it might be the first half of a combining mark.
                                        // Pretend we didn't see it.

                                        // SAFETY: We just incremented `ptr`, so decrementing it
                                        // has to stay within the allocation.
                                        ptr = unsafe { ptr.sub(1) };
                                        break 'surrogateloop;
                                    }
                                    if likely(!decomposition_starts_with_non_starter(after_mark_trie_value)) {
                                        // We have a decomposing starter.

                                        // No need to sync `upcoming_code_unit`, since nothing reads it below.
                                        upcoming32 = u32::from(after_mark_code_unit);
                                        trie_value = after_mark_trie_value;
                                        break 'surrogateloop;
                                    }
                                    // We have another combining mark.
                                    // We put the first combining mark, which we know doesn't decompose,
                                    // directly into the buffer. We put the second one, which might decompose,
                                    // into `decomposition.pending` for `gather_and_sort_combining` to deal
                                    // with.

                                    let Some(consumed_so_far_slice) = pending_slice.get(..pending_slice.len() -
                                        // code_unit_iter.as_slice().len()
                                        // SAFETY: `ptr` and `end` have been derived from the same allocation
                                        // and `ptr` is never greater than `end`.
                                        unsafe { end.offset_from(ptr) as usize }
                                        - 2) else {
                                        // If we ever come here, it's a bug, but let's avoid panic code paths in release builds.
                                        debug_assert!(false);
                                        // Throw away the results of the fast path.
                                        break 'fastwrap;
                                    };
                                    sink.write_slice(consumed_so_far_slice)?;

                                    // Our belief that `upcoming32` is not a surrogate is based on trie data,
                                    // which might be GIGO.
                                    let upcoming = char_from_u32(upcoming32);

                                    debug_assert!(decomposition.buffer.is_empty());

                                    // Narrowing `trie_value` to `u8` is OK, because we already checked
                                    // `decomposition_starts_with_non_starter`.
                                    debug_assert!(trie_value_has_ccc(trie_value));
                                    decomposition.buffer.push(CharacterAndClass::new(upcoming, CanonicalCombiningClass::from_icu4c_value(trie_value as u8)));

                                    // Sync with main iterator
                                    // SAFETY: `ptr` and `end` have been derived from the same allocation
                                    // and `ptr` is never greater than `end`.
                                    decomposition.delegate = unsafe { core::slice::from_raw_parts(ptr, end.offset_from(ptr) as usize) }.chars_with_trie(decomposition.delegate.trie());
                                    // Let this trie value to be reprocessed in case it is
                                    // one of the rare decomposing ones.
                                    // SAFETY: We checked above that we don't have surrogate.
                                    let after_mark_char = unsafe { char::from_u32_unchecked(u32::from(after_mark_code_unit))};
                                    decomposition.pending = Some(CharacterAndTrieValue::new(after_mark_char, after_mark_trie_value));
                                    decomposition.gather_and_sort_combining(0);
                                    continue 'outer;
                                }
                                // End of stream
                                sink.write_slice(pending_slice)?;
                                return Ok(());
                            }

                            // End skipping over single combining mark

                            // LLVM's optimizations are incredibly brittle for the code _above_,
                            // and using `likely` _below_ without using it _above_ helps!
                            // What a massive sink of developer time!
                            // Seriously, the effect of these annotations is massively
                            // unintuitive. Measure everything!
                            // Notably, the `if likely(...)` formulation optimizes differently
                            // than just putting `cold_path()` on the `else` path!
                            let surrogate_base = upcoming32.wrapping_sub(0xD800);
                            if likely(surrogate_base > (0xDFFF - 0xD800)) {
                                // Not surrogate
                                break 'surrogateloop;
                            }
                            if likely(surrogate_base <= (0xDBFF - 0xD800)) {
                                // let iter_backup = code_unit_iter.clone();
                                // if let Some(&low) = code_unit_iter.next() {
                                if likely(ptr != end) {
                                    // SAFETY: We just checked that `ptr` has not reached `end`.
                                    // `ptr` always advances by one, and we always have a check
                                    // per advancement.
                                    let low = unsafe { *ptr };
                                    if likely(in_inclusive_range16(low, 0xDC00, 0xDFFF)) {
                                        // SAFETY: Since `ptr` hadn't reached `end`, yet, advancing
                                        // by one points to the same allocation or to immediately
                                        // after, which is OK.
                                        ptr = unsafe { ptr.add(1) };

                                        upcoming32 = (upcoming32 << 10) + u32::from(low)
                                            - (((0xD800u32 << 10) - 0x10000u32) + 0xDC00u32);
                                        // Successfully-paired surrogate. Read from the trie again.
                                        trie_value = {
                                            // Semantically, this bit of conditional compilation makes no sense.
                                            // The purpose is to keep LLVM seeing the untyped trie case the way
                                            // it did before so as not to regress the performance of the untyped
                                            // case due to unintuitive optimizer effects. If you care about the
                                            // perf of the untyped trie case and have better ideas, please try
                                            // something better.
                                            #[cfg(feature = "serde")]
                                            {decomposition.delegate.trie().code_point(upcoming32)}
                                            #[cfg(not(feature = "serde"))]
                                            {decomposition.delegate.trie().supplementary(upcoming32)}
                                        };
                                        if likely(starter_and_decomposes_to_self_impl(trie_value)) {
                                            continue 'fast;
                                        }
                                        break 'surrogateloop;
                                    // } else {
                                    //     code_unit_iter = iter_backup;
                                    }
                                }
                            }
                            // unpaired surrogate
                            upcoming32 = 0xFFFD; // Safe value for `char::from_u32_unchecked` and matches later potential error check.
                            // trie_value already holds a decomposition to U+FFFD.
                            break 'surrogateloop;
                        }

                        let upcoming = unsafe { char::from_u32_unchecked(upcoming32) };
                        let upcoming_with_trie_value = CharacterAndTrieValue::new(upcoming, trie_value);


                        let Some(consumed_so_far_slice) = pending_slice.get(..pending_slice.len() -
                            // code_unit_iter.as_slice().len()
                            // SAFETY: `ptr` and `end` have been derived from the same allocation
                            // and `ptr` is never greater than `end`.
                            unsafe { end.offset_from(ptr) as usize }
                            - upcoming.len_utf16()) else {
                            // If we ever come here, it's a bug, but let's avoid panic code paths in release builds.
                            debug_assert!(false);
                            // Throw away the results of the fast path.
                            break 'fastwrap;
                        };
                        sink.write_slice(consumed_so_far_slice)?;

                        if decomposition_starts_with_non_starter(
                            upcoming_with_trie_value.trie_val,
                        ) {
                            // Sync with main iterator
                            // decomposition.delegate = code_unit_iter.as_slice().chars();
                            // SAFETY: `ptr` and `end` have been derived from the same allocation
                            // and `ptr` is never greater than `end`.
                            decomposition.delegate = unsafe { core::slice::from_raw_parts(ptr, end.offset_from(ptr) as usize) }.chars_with_trie(decomposition.delegate.trie());
                            // Let this trie value to be reprocessed in case it is
                            // one of the rare decomposing ones.
                            decomposition.pending = Some(upcoming_with_trie_value);
                            decomposition.gather_and_sort_combining(0);
                            continue 'outer;
                        }
                        undecomposed_starter = upcoming_with_trie_value;
                        debug_assert!(decomposition.pending.is_none());
                        break 'fast;
                    }
                    // End of stream
                    sink.write_slice(pending_slice)?;
                    return Ok(());
                }
                // Sync the main iterator
                // decomposition.delegate = code_unit_iter.as_slice().chars();
                // SAFETY: `ptr` and `end` have been derived from the same allocation
                // and `ptr` is never greater than `end`.
                decomposition.delegate = unsafe { core::slice::from_raw_parts(ptr, end.offset_from(ptr) as usize) }.chars_with_trie(decomposition.delegate.trie());
                break 'fastwrap;
            }
        },
        text,
        sink,
        decomposition,
        undecomposed_starter,
        pending_slice,
        'outer,
        self,
        chars_with_trie,
    );
}

/// A normalizer for performing decomposing normalization.
#[derive(Debug)]
pub struct DecomposingNormalizer {
    decompositions: DataPayload<NormalizerNfdDataV1>,
    tables: DataPayload<NormalizerNfdTablesV1>,
    supplementary_tables: Option<DataPayload<NormalizerNfkdTablesV1>>,
    decomposition_passthrough_bound: u8, // never above 0xC0
    composition_passthrough_bound: u16,  // never above 0x0300
}

impl DecomposingNormalizer {
    /// Constructs a borrowed version of this type for more efficient querying.
    pub fn as_borrowed(&self) -> DecomposingNormalizerBorrowed<'_> {
        DecomposingNormalizerBorrowed {
            decompositions: self.decompositions.get(),
            tables: self.tables.get(),
            supplementary_tables: self.supplementary_tables.as_ref().map(|s| s.get()),
            decomposition_passthrough_bound: self.decomposition_passthrough_bound,
            composition_passthrough_bound: self.composition_passthrough_bound,
        }
    }

    /// NFD constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfd() -> DecomposingNormalizerBorrowed<'static> {
        DecomposingNormalizerBorrowed::new_nfd()
    }

    icu_provider::gen_buffer_data_constructors!(
        () -> error: DataError,
        functions: [
            new_nfd: skip,
            try_new_nfd_with_buffer_provider,
            try_new_nfd_unstable,
            Self,
        ]
    );

    #[doc = icu_provider::gen_buffer_unstable_docs!(UNSTABLE, Self::new_nfd)]
    pub fn try_new_nfd_unstable<D>(provider: &D) -> Result<Self, DataError>
    where
        D: DataProvider<NormalizerNfdDataV1> + DataProvider<NormalizerNfdTablesV1> + ?Sized,
    {
        let decompositions: DataPayload<NormalizerNfdDataV1> =
            provider.load(Default::default())?.payload;
        let tables: DataPayload<NormalizerNfdTablesV1> = provider.load(Default::default())?.payload;

        if tables.get().scalars16.len() + tables.get().scalars24.len() > 0xFFF {
            // The data is from a future where there exists a normalization flavor whose
            // complex decompositions take more than 0xFFF but fewer than 0x1FFF code points
            // of space. If a good use case from such a decomposition flavor arises, we can
            // dynamically change the bit masks so that the length mask becomes 0x1FFF instead
            // of 0xFFF and the all-non-starters mask becomes 0 instead of 0x1000. However,
            // since for now the masks are hard-coded, error out.
            return Err(
                DataError::custom("future extension").with_marker(NormalizerNfdTablesV1::INFO)
            );
        }

        let cap = decompositions.get().passthrough_cap;
        if cap > 0x0300 {
            return Err(DataError::custom("invalid").with_marker(NormalizerNfdDataV1::INFO));
        }
        if cap < 0x80 {
            return Err(DataError::custom("invalid").with_marker(NormalizerNfdDataV1::INFO));
        }
        let decomposition_capped = cap.min(0xC0);
        let composition_capped = cap.min(0x0300);

        Ok(DecomposingNormalizer {
            decompositions,
            tables,
            supplementary_tables: None,
            decomposition_passthrough_bound: decomposition_capped as u8,
            composition_passthrough_bound: composition_capped,
        })
    }

    icu_provider::gen_buffer_data_constructors!(
        () -> error: DataError,
        functions: [
            new_nfkd: skip,
            try_new_nfkd_with_buffer_provider,
            try_new_nfkd_unstable,
            Self,
        ]
    );

    /// NFKD constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfkd() -> DecomposingNormalizerBorrowed<'static> {
        DecomposingNormalizerBorrowed::new_nfkd()
    }

    #[doc = icu_provider::gen_buffer_unstable_docs!(UNSTABLE, Self::new_nfkd)]
    pub fn try_new_nfkd_unstable<D>(provider: &D) -> Result<Self, DataError>
    where
        D: DataProvider<NormalizerNfkdDataV1>
            + DataProvider<NormalizerNfdTablesV1>
            + DataProvider<NormalizerNfkdTablesV1>
            + ?Sized,
    {
        let decompositions: DataPayload<NormalizerNfkdDataV1> =
            provider.load(Default::default())?.payload;
        let tables: DataPayload<NormalizerNfdTablesV1> = provider.load(Default::default())?.payload;
        let supplementary_tables: DataPayload<NormalizerNfkdTablesV1> =
            provider.load(Default::default())?.payload;

        if tables.get().scalars16.len()
            + tables.get().scalars24.len()
            + supplementary_tables.get().scalars16.len()
            + supplementary_tables.get().scalars24.len()
            > 0xFFF
        {
            // The data is from a future where there exists a normalization flavor whose
            // complex decompositions take more than 0xFFF but fewer than 0x1FFF code points
            // of space. If a good use case from such a decomposition flavor arises, we can
            // dynamically change the bit masks so that the length mask becomes 0x1FFF instead
            // of 0xFFF and the all-non-starters mask becomes 0 instead of 0x1000. However,
            // since for now the masks are hard-coded, error out.
            return Err(
                DataError::custom("future extension").with_marker(NormalizerNfdTablesV1::INFO)
            );
        }

        let cap = decompositions.get().passthrough_cap;
        if cap > 0x0300 {
            return Err(DataError::custom("invalid").with_marker(NormalizerNfkdDataV1::INFO));
        }
        if cap < 0x80 {
            return Err(DataError::custom("invalid").with_marker(NormalizerNfdDataV1::INFO));
        }
        let decomposition_capped = cap.min(0xC0);
        let composition_capped = cap.min(0x0300);

        Ok(DecomposingNormalizer {
            decompositions: decompositions.cast(),
            tables,
            supplementary_tables: Some(supplementary_tables),
            decomposition_passthrough_bound: decomposition_capped as u8,
            composition_passthrough_bound: composition_capped,
        })
    }

    /// UTS 46 decomposed constructor (testing only)
    ///
    /// This is a special building block normalization for IDNA. It is the decomposed counterpart of
    /// ICU4C's UTS 46 normalization with two exceptions: characters that UTS 46 disallows and
    /// ICU4C maps to U+FFFD and characters that UTS 46 maps to the empty string normalize as in
    /// NFD in this normalization. In both cases, the previous UTS 46 processing before using
    /// normalization is expected to deal with these characters. Making the disallowed characters
    /// behave like this is beneficial to data size, and this normalizer implementation cannot
    /// deal with a character normalizing to the empty string, which doesn't happen in NFD or
    /// NFKD as of Unicode 14.
    ///
    /// Warning: In this normalization, U+0345 COMBINING GREEK YPOGEGRAMMENI exhibits a behavior
    /// that no character in Unicode exhibits in NFD, NFKD, NFC, or NFKC: Case folding turns
    /// U+0345 from a reordered character into a non-reordered character before reordering happens.
    /// Therefore, the output of this normalization may differ for different inputs that are
    /// canonically equivalent with each other if they differ by how U+0345 is ordered relative
    /// to other reorderable characters.
    pub(crate) fn try_new_uts46_decomposed_unstable<D>(provider: &D) -> Result<Self, DataError>
    where
        D: DataProvider<NormalizerUts46DataV1>
            + DataProvider<NormalizerNfdTablesV1>
            + DataProvider<NormalizerNfkdTablesV1>
            // UTS 46 tables merged into CompatibilityDecompositionTablesV1
            + ?Sized,
    {
        let decompositions: DataPayload<NormalizerUts46DataV1> =
            provider.load(Default::default())?.payload;
        let tables: DataPayload<NormalizerNfdTablesV1> = provider.load(Default::default())?.payload;
        let supplementary_tables: DataPayload<NormalizerNfkdTablesV1> =
            provider.load(Default::default())?.payload;

        if tables.get().scalars16.len()
            + tables.get().scalars24.len()
            + supplementary_tables.get().scalars16.len()
            + supplementary_tables.get().scalars24.len()
            > 0xFFF
        {
            // The data is from a future where there exists a normalization flavor whose
            // complex decompositions take more than 0xFFF but fewer than 0x1FFF code points
            // of space. If a good use case from such a decomposition flavor arises, we can
            // dynamically change the bit masks so that the length mask becomes 0x1FFF instead
            // of 0xFFF and the all-non-starters mask becomes 0 instead of 0x1000. However,
            // since for now the masks are hard-coded, error out.
            return Err(
                DataError::custom("future extension").with_marker(NormalizerNfdTablesV1::INFO)
            );
        }

        let cap = decompositions.get().passthrough_cap;
        if cap > 0x0300 {
            return Err(DataError::custom("invalid").with_marker(NormalizerUts46DataV1::INFO));
        }
        // Can be below 0x80!
        let decomposition_capped = cap.min(0xC0);
        let composition_capped = cap.min(0x0300);

        Ok(DecomposingNormalizer {
            decompositions: decompositions.cast(),
            tables,
            supplementary_tables: Some(supplementary_tables),
            decomposition_passthrough_bound: decomposition_capped as u8,
            composition_passthrough_bound: composition_capped,
        })
    }
}

/// Borrowed version of a normalizer for performing composing normalization.
#[derive(Debug)]
pub struct ComposingNormalizerBorrowed<'a> {
    decomposing_normalizer: DecomposingNormalizerBorrowed<'a>,
    canonical_compositions: CanonicalCompositionsBorrowed<'a>,
}

impl ComposingNormalizerBorrowed<'static> {
    /// Cheaply converts a [`ComposingNormalizerBorrowed<'static>`] into a [`ComposingNormalizer`].
    ///
    /// Note: Due to branching and indirection, using [`ComposingNormalizer`] might inhibit some
    /// compile-time optimizations that are possible with [`ComposingNormalizerBorrowed`].
    pub const fn static_to_owned(self) -> ComposingNormalizer {
        ComposingNormalizer {
            decomposing_normalizer: self.decomposing_normalizer.static_to_owned(),
            canonical_compositions: self.canonical_compositions.static_to_owned(),
        }
    }

    /// NFC constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfc() -> Self {
        ComposingNormalizerBorrowed {
            decomposing_normalizer: DecomposingNormalizerBorrowed::new_nfd(),
            canonical_compositions: CanonicalCompositionsBorrowed::Current(
                provider::Baked::SINGLETON_NORMALIZER_NFC_V2,
            ),
        }
    }

    /// NFKC constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfkc() -> Self {
        ComposingNormalizerBorrowed {
            decomposing_normalizer: DecomposingNormalizerBorrowed::new_nfkd(),
            canonical_compositions: CanonicalCompositionsBorrowed::Current(
                provider::Baked::SINGLETON_NORMALIZER_NFC_V2,
            ),
        }
    }

    /// This is a special building block normalization for IDNA that implements parts of the Map
    /// step and the following Normalize step.
    ///
    /// Warning: In this normalization, U+0345 COMBINING GREEK YPOGEGRAMMENI exhibits a behavior
    /// that no character in Unicode exhibits in NFD, NFKD, NFC, or NFKC: Case folding turns
    /// U+0345 from a reordered character into a non-reordered character before reordering happens.
    /// Therefore, the output of this normalization may differ for different inputs that are
    /// canonically equivalents with each other if they differ by how U+0345 is ordered relative
    /// to other reorderable characters.
    #[cfg(feature = "compiled_data")]
    pub(crate) const fn new_uts46() -> Self {
        ComposingNormalizerBorrowed {
            decomposing_normalizer: DecomposingNormalizerBorrowed::new_uts46_decomposed(),
            canonical_compositions: CanonicalCompositionsBorrowed::Current(
                provider::Baked::SINGLETON_NORMALIZER_NFC_V2,
            ),
        }
    }
}

impl<'data> ComposingNormalizerBorrowed<'data> {
    /// Wraps a delegate iterator into a composing iterator
    /// adapter by using the data already held by this normalizer.
    #[inline]
    pub fn normalize_iter<I: Iterator<Item = char>>(&'data self, iter: I) -> Composition<'data, I> {
        let mut ret = Composition {
            inner: self.normalize_iter_private(CharIterWithTrie::new(iter, self.trie())),
        };
        ret.inner.decomposition.init(); // Discard the U+0000.
        ret
    }

    /// There's an extra U+FFFD at the start. The caller must deal with it.
    #[inline(always)]
    fn normalize_iter_private<
        I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32>,
        T: AbstractCodePointTrie<'data, u32> + 'data,
        P: IteratorPolicy,
    >(
        &'data self,
        iter: I,
    ) -> CompositionInner<'data, I, T, P> {
        CompositionInner::new(
            DecompositionInner::new_with_supplements(
                iter,
                self.decomposing_normalizer.tables,
                self.decomposing_normalizer.supplementary_tables,
            ),
            self.canonical_compositions.as_ref(),
        )
    }

    fn trie<T: AbstractCodePointTrie<'data, u32>>(&self) -> &'data T
    where
        &'data T: TryFrom<&'data CodePointTrie<'data, u32>>,
    {
        self.decomposing_normalizer.trie()
    }

    normalizer_methods!();

    composing_normalize_to!(
        /// Normalize a string slice into a `Write` sink.
        ,
        normalize_to,
        core::fmt::Write,
        &str,
        {},
        true,
        as_str,
        {
            let composition_passthrough_byte_bound = if self.decomposing_normalizer.composition_passthrough_bound == 0x300 {
                LEAD_FOR_NFC_PASSTHROUGH_BOUND
            } else {
                // We can make this fancy if a normalization other than NFC where looking at
                // non-ASCII lead bytes is worthwhile is ever introduced.
                LeadForMultiByte::MIN
            };
            'fast: loop {
                if let Some((upcoming, trie_val)) = composition.decomposition.delegate.next_with_minimum_lead(composition_passthrough_byte_bound) {
                    if potential_passthrough_and_cannot_combine_backwards(trie_val) {
                        continue 'fast;
                    }
                    let upcoming_with_trie_value = CharacterAndTrieValue::new(upcoming, trie_val);
                    // We need to fall off the fast path.
                    composition.decomposition.pending = Some(upcoming_with_trie_value);

                    // slicing and unwrap OK, because we've just evidently read enough previously.
                    let mut consumed_so_far = pending_slice[..pending_slice.len() - composition.decomposition.delegate.as_str().len() - upcoming.len_utf8()].chars_with_trie_default_for_ascii(composition.decomposition.delegate.trie());
                    // Whether we could do something better than `next_back()` below is
                    // https://github.com/unicode-org/icu4x/issues/7525
                    // `unwrap` OK, because we've previously manage to read the previous character
                    #[expect(clippy::unwrap_used)]
                    let (undecomposed, undecomposed_trie_val) = consumed_so_far.next_back().unwrap();
                    undecomposed_starter = CharacterAndTrieValue::new(undecomposed, undecomposed_trie_val);
                    let consumed_so_far_slice = consumed_so_far.as_str();
                    sink.write_str(consumed_so_far_slice)?;
                    break 'fast;
                }
                // End of stream
                sink.write_str(pending_slice)?;
                return Ok(());
            }
        },
        text,
        sink,
        composition,
        undecomposed_starter,
        pending_slice,
        len_utf8,
        self,
        chars_with_trie_default_for_ascii,
    );

    composing_normalize_to!(
        /// Normalize a slice of potentially-invalid UTF-8 into a `Write` sink.
        ///
        /// Ill-formed byte sequences are mapped to the REPLACEMENT CHARACTER
        /// according to the WHATWG Encoding Standard.
        ///
        /// ✨ *Enabled with the `utf8_iter` Cargo feature.*
        #[cfg(feature = "utf8_iter")]
        ,
        normalize_utf8_to,
        core::fmt::Write,
        &[u8],
        {},
        false,
        as_slice,
        {
            'fast: loop {
                if let Some((upcoming, trie_val)) = composition.decomposition.delegate.next() {
                    if potential_passthrough_and_cannot_combine_backwards(trie_val) {
                        // Note: The trie value of the REPLACEMENT CHARACTER is
                        // intentionally formatted to fail the
                        // `potential_passthrough_and_cannot_combine_backwards`
                        // test even though it really is a starter that decomposes
                        // to self and cannot combine backwards. This
                        // Allows moving the branch on REPLACEMENT CHARACTER
                        // below this `continue`.
                        continue 'fast;
                    }
                    // We need to fall off the fast path.

                    let upcoming_with_trie_value = CharacterAndTrieValue::new(upcoming, trie_val);
                    if unlikely(upcoming == REPLACEMENT_CHARACTER) {
                        // Can't tell if this is an error or a literal U+FFFD in
                        // the input. Assuming the former to be sure.

                        // Since the U+FFFD might signify an error, we can't
                        // assume `upcoming.len_utf8()` for the backoff length.
                        #[expect(clippy::indexing_slicing)]
                        let mut consumed_so_far = pending_slice[..pending_slice.len() - composition.decomposition.delegate.as_slice().len()].chars();
                        let back = consumed_so_far.next_back();
                        debug_assert_eq!(back, Some(REPLACEMENT_CHARACTER));
                        let consumed_so_far_slice = consumed_so_far.as_slice();
                        sink.write_str(unsafe { core::str::from_utf8_unchecked(consumed_so_far_slice) })?;
                        undecomposed_starter = CharacterAndTrieValue::new(REPLACEMENT_CHARACTER, 0);
                        composition.decomposition.pending = None;
                        break 'fast;
                    }

                    composition.decomposition.pending = Some(upcoming_with_trie_value);
                    // slicing and unwrap OK, because we've just evidently read enough previously.
                    // `unwrap` OK, because we've previously manage to read the previous character
                    #[expect(clippy::indexing_slicing)]
                    let mut consumed_so_far = pending_slice[..pending_slice.len() - composition.decomposition.delegate.as_slice().len() - upcoming.len_utf8()].chars_with_trie_default_for_ascii(composition.decomposition.delegate.trie());
                    #[expect(clippy::unwrap_used)]
                    {
                        // Whether we could do something better than `next_back()` below is
                        // https://github.com/unicode-org/icu4x/issues/7525
                        let (undecomposed, undecomposed_trie_val) = consumed_so_far.next_back().unwrap();
                        undecomposed_starter = CharacterAndTrieValue::new(undecomposed, undecomposed_trie_val);
                    }
                    let consumed_so_far_slice = consumed_so_far.as_slice();
                    sink.write_str(unsafe { core::str::from_utf8_unchecked(consumed_so_far_slice)})?;
                    break 'fast;
                }
                // End of stream
                sink.write_str(unsafe { core::str::from_utf8_unchecked(pending_slice) })?;
                return Ok(());
            }
        },
        text,
        sink,
        composition,
        undecomposed_starter,
        pending_slice,
        len_utf8,
        self,
        chars_with_trie_default_for_ascii,
    );

    composing_normalize_to!(
        /// Normalize a slice of potentially-invalid UTF-16 into a `Write16` sink.
        ///
        /// Unpaired surrogates are mapped to the REPLACEMENT CHARACTER
        /// before normalizing.
        ///
        /// ✨ *Enabled with the `utf16_iter` Cargo feature.*
        #[cfg(feature = "utf16_iter")]
        ,
        normalize_utf16_to,
        write16::Write16,
        &[u16],
        {
            sink.size_hint(text.len())?;
        },
        false,
        as_slice,
        {
            // This loop is only broken out of as goto forward and only as release-build recovery from
            // detecting an internal bug without panic. (In debug builds, internal bugs panic instead.)
            #[expect(clippy::never_loop)]
            'fastwrap: loop {
                // Commented out `code_unit_iter` and used `ptr` and `end` to
                // work around https://github.com/rust-lang/rust/issues/144684 .
                //
                // let mut code_unit_iter = composition.decomposition.delegate.as_slice().iter();
                let delegate_as_slice = composition.decomposition.delegate.as_slice();
                let mut ptr: *const u16 = delegate_as_slice.as_ptr();
                // SAFETY: materializing a pointer immediately past the end of an
                // allocation is OK.
                let end: *const u16 = unsafe { ptr.add(delegate_as_slice.len()) };
                let composition_passthrough_bound = self.decomposing_normalizer.composition_passthrough_bound;
                'fast: loop {
                    // Only broken out of as goto forward
                    'end: loop {
                        // if let Some(&upcoming_code_unit) = code_unit_iter.next() {
                        if likely(ptr != end) {
                            // SAFETY: We just checked that `ptr` has not reached `end`.
                            // `ptr` always advances by one, and we always have a check
                            // per advancement.
                            let mut upcoming_code_unit = unsafe { *ptr };
                            // SAFETY: Since `ptr` hadn't reached `end`, yet, advancing
                            // by one points to the same allocation or to immediately
                            // after, which is OK.
                            ptr = unsafe { ptr.add(1) };

                            if likely(upcoming_code_unit < composition_passthrough_bound) {
                                // No need for surrogate or U+FFFD check, because
                                // `composition_passthrough_bound` cannot be higher than
                                // U+0300.
                                // Fast-track succeeded!
                                continue 'fast;
                            }
                            if unlikely(in_inclusive_range16(upcoming_code_unit, 0x2013, 0x2022)) && upcoming_code_unit != 0x2017 {
                                // Don't allow dashes and smart quotes to fall off the trie-bypass
                                // path.
                                // Fast-track succeeded!
                                continue 'fast;
                            }
                            // This is intentionally bimodal so that if we exit the above trie-bypass path,
                            // we stay on the trie-reading path until we've processed a non-BMP character
                            // (likely emoji) or to the end of this passthrough run. This makes NFC faster
                            // than ICU4C for most real-world content. The result is not optimal for NFKC
                            // Latin, but let's take the NFC non-Latin win.
                            let mut trie_value;
                            let mut upcoming32; // May be surrogate
                            loop {
                                // We might be doing a trie lookup by surrogate. Surrogates get
                                // a decomposition to U+FFFD.
                                trie_value = composition.decomposition.delegate.trie().bmp(upcoming_code_unit);
                                if likely(potential_passthrough_and_cannot_combine_backwards(trie_value)) {
                                    // Can't combine backwards, hence a plain (non-backwards-combining)
                                    // starter albeit past `composition_passthrough_bound`

                                    // Fast-track succeeded!
                                    // Instead of going back to `'fast`, we stay here to skip the branch
                                    // for `composition_passthrough_bound`.
                                    if likely(ptr != end) {
                                        // SAFETY: We just checked that `ptr` has not reached `end`.
                                        // `ptr` always advances by one, and we always have a check
                                        // per advancement.
                                        upcoming_code_unit = unsafe { *ptr };
                                        // SAFETY: Since `ptr` hadn't reached `end`, yet, advancing
                                        // by one points to the same allocation or to immediately
                                        // after, which is OK.
                                        ptr = unsafe { ptr.add(1) };
                                        continue;
                                    }
                                    break 'end;
                                }
                                upcoming32 = u32::from(upcoming_code_unit);
                                break;
                            }

                            // We might now be looking at a surrogate.
                            // The loop is only broken out of as goto forward
                            #[expect(clippy::never_loop)]
                            'surrogateloop: loop {
                                // The `likely` annotations _below_ exist to make the code _above_
                                // go faster!
                                let surrogate_base = upcoming32.wrapping_sub(0xD800);
                                if likely(surrogate_base > (0xDFFF - 0xD800)) {
                                    // Not surrogate
                                    break 'surrogateloop;
                                }
                                if likely(surrogate_base <= (0xDBFF - 0xD800)) {
                                    // let iter_backup = code_unit_iter.clone();
                                    // if let Some(&low) = code_unit_iter.next() {
                                    if likely(ptr != end) {
                                        // SAFETY: We just checked that `ptr` has not reached `end`.
                                        // `ptr` always advances by one, and we always have a check
                                        // per advancement.
                                        let low = unsafe { *ptr };
                                        if likely(in_inclusive_range16(low, 0xDC00, 0xDFFF)) {
                                            // SAFETY: Since `ptr` hadn't reached `end`, yet, advancing
                                            // by one points to the same allocation or to immediately
                                            // after, which is OK.
                                            ptr = unsafe { ptr.add(1) };

                                            upcoming32 = (upcoming32 << 10) + u32::from(low)
                                                - (((0xD800u32 << 10) - 0x10000u32) + 0xDC00u32);
                                            // Successfully-paired surrogate. Read from the trie again.
                                            trie_value = {
                                                // Semantically, this bit of conditional compilation makes no sense.
                                                // The purpose is to keep LLVM seeing the untyped trie case the way
                                                // it did before so as not to regress the performance of the untyped
                                                // case due to unintuitive optimizer effects. If you care about the
                                                // perf of the untyped trie case and have better ideas, please try
                                                // something better.
                                                #[cfg(feature = "serde")]
                                                {composition.decomposition.delegate.trie().code_point(upcoming32)}
                                                #[cfg(not(feature = "serde"))]
                                                {composition.decomposition.delegate.trie().supplementary(upcoming32)}
                                            };
                                            if likely(potential_passthrough_and_cannot_combine_backwards(trie_value)) {
                                                // Fast-track succeeded!
                                                continue 'fast;
                                            }
                                            break 'surrogateloop;
                                        // } else {
                                        //     code_unit_iter = iter_backup;
                                        }
                                    }
                                }
                                // unpaired surrogate
                                upcoming32 = 0xFFFD; // Safe value for `char::from_u32_unchecked` and matches later potential error check.
                                // trie_value already holds a decomposition to U+FFFD.
                                debug_assert_eq!(trie_value, NON_ROUND_TRIP_MARKER | BACKWARD_COMBINING_MARKER | 0xFFFD);
                                break 'surrogateloop;
                            }

                            // SAFETY: upcoming32 can no longer be a surrogate.
                            let upcoming = unsafe { char::from_u32_unchecked(upcoming32) };
                            let upcoming_with_trie_value = CharacterAndTrieValue::new(upcoming, trie_value);
                            // We need to fall off the fast path.
                            composition.decomposition.pending = Some(upcoming_with_trie_value);
                            let Some(consumed_so_far_slice) = pending_slice.get(..pending_slice.len() -
                                // code_unit_iter.as_slice().len()
                                // SAFETY: `ptr` and `end` have been derived from the same allocation
                                // and `ptr` is never greater than `end`.
                                unsafe { end.offset_from(ptr) as usize }
                                - upcoming.len_utf16()) else {
                                // If we ever come here, it's a bug, but let's avoid panic code paths in release builds.
                                debug_assert!(false);
                                // Throw away the results of the fast path.
                                break 'fastwrap;
                            };
                            let mut consumed_so_far = consumed_so_far_slice.chars_with_trie(composition.decomposition.delegate.trie());
                            // Whether we could do something better than `next_back()` below is
                            // https://github.com/unicode-org/icu4x/issues/7525
                            let Some((c_from_back, trie_val_from_back)) = consumed_so_far.next_back() else {
                                // If we ever come here, it's a bug, but let's avoid panic code paths in release builds.
                                debug_assert!(false);
                                // Throw away the results of the fast path.
                                break 'fastwrap;
                            };
                            // TODO: If the previous character was below the passthrough bound,
                            // we really need to read from the trie. Otherwise, we could maintain
                            // the most-recent trie value. Need to measure what's more expensive:
                            // Remembering the trie value on each iteration or re-reading the
                            // last one after the fast-track run.
                            undecomposed_starter = CharacterAndTrieValue::new(c_from_back, trie_val_from_back);
                            sink.write_slice(consumed_so_far.as_slice())?;
                            break 'fast;
                        }
                        break;
                    }
                    // End of stream
                    sink.write_slice(pending_slice)?;
                    return Ok(());
                }
                // Sync the main iterator
                // composition.decomposition.delegate = code_unit_iter.as_slice().chars();
                // SAFETY: `ptr` and `end` have been derive from the same allocation
                // and `ptr` is never greater than `end`.
                composition.decomposition.delegate = unsafe { core::slice::from_raw_parts(ptr, end.offset_from(ptr) as usize) }.chars_with_trie(composition.decomposition.delegate.trie());
                break 'fastwrap;
            }
        },
        text,
        sink,
        composition,
        undecomposed_starter,
        pending_slice,
        len_utf16,
        self,
        chars_with_trie,
    );
}

/// A normalizer for performing composing normalization.
#[derive(Debug)]
pub struct ComposingNormalizer {
    decomposing_normalizer: DecomposingNormalizer,
    canonical_compositions: CanonicalCompositionsPayload,
}

impl ComposingNormalizer {
    /// Constructs a borrowed version of this type for more efficient querying.
    pub fn as_borrowed(&self) -> ComposingNormalizerBorrowed<'_> {
        ComposingNormalizerBorrowed {
            decomposing_normalizer: self.decomposing_normalizer.as_borrowed(),
            canonical_compositions: self.canonical_compositions.as_borrowed(),
        }
    }

    /// NFC constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfc() -> ComposingNormalizerBorrowed<'static> {
        ComposingNormalizerBorrowed::new_nfc()
    }

    icu_provider::gen_buffer_data_constructors!(
        () -> error: DataError,
        functions: [
            new_nfc: skip,
            try_new_nfc_with_buffer_provider,
            try_new_nfc_unstable,
            Self,
        ]
    );

    #[doc = icu_provider::gen_buffer_unstable_docs!(UNSTABLE, Self::new_nfc)]
    pub fn try_new_nfc_unstable<D>(provider: &D) -> Result<Self, DataError>
    where
        D: DataProvider<NormalizerNfdDataV1>
            + DataProvider<NormalizerNfdTablesV1>
            + DataProvider<NormalizerNfcV2>
            + ?Sized,
    {
        let decomposing_normalizer = DecomposingNormalizer::try_new_nfd_unstable(provider)?;

        let canonical_compositions: DataPayload<NormalizerNfcV2> =
            provider.load(Default::default())?.payload;

        Ok(ComposingNormalizer {
            decomposing_normalizer,
            canonical_compositions: CanonicalCompositionsPayload::Current(canonical_compositions),
        })
    }

    /// NFKC constructor using compiled data.
    ///
    /// ✨ *Enabled with the `compiled_data` Cargo feature.*
    ///
    /// [📚 Help choosing a constructor](icu_provider::constructors)
    #[cfg(feature = "compiled_data")]
    pub const fn new_nfkc() -> ComposingNormalizerBorrowed<'static> {
        ComposingNormalizerBorrowed::new_nfkc()
    }

    icu_provider::gen_buffer_data_constructors!(
        () -> error: DataError,
        functions: [
            new_nfkc: skip,
            try_new_nfkc_with_buffer_provider,
            try_new_nfkc_unstable,
            Self,
        ]
    );

    #[doc = icu_provider::gen_buffer_unstable_docs!(UNSTABLE, Self::new_nfkc)]
    pub fn try_new_nfkc_unstable<D>(provider: &D) -> Result<Self, DataError>
    where
        D: DataProvider<NormalizerNfkdDataV1>
            + DataProvider<NormalizerNfdTablesV1>
            + DataProvider<NormalizerNfkdTablesV1>
            + DataProvider<NormalizerNfcV2>
            + ?Sized,
    {
        let decomposing_normalizer = DecomposingNormalizer::try_new_nfkd_unstable(provider)?;

        let canonical_compositions: DataPayload<NormalizerNfcV2> =
            provider.load(Default::default())?.payload;

        Ok(ComposingNormalizer {
            decomposing_normalizer,
            canonical_compositions: CanonicalCompositionsPayload::Current(canonical_compositions),
        })
    }

    #[doc = icu_provider::gen_buffer_unstable_docs!(UNSTABLE, Self::new_uts46)]
    pub(crate) fn try_new_uts46_unstable<D>(provider: &D) -> Result<Self, DataError>
    where
        D: DataProvider<NormalizerUts46DataV1>
            + DataProvider<NormalizerNfdTablesV1>
            + DataProvider<NormalizerNfkdTablesV1>
            // UTS 46 tables merged into CompatibilityDecompositionTablesV1
            + DataProvider<NormalizerNfcV2>
            + ?Sized,
    {
        let decomposing_normalizer =
            DecomposingNormalizer::try_new_uts46_decomposed_unstable(provider)?;

        let canonical_compositions: DataPayload<NormalizerNfcV2> =
            provider.load(Default::default())?.payload;

        Ok(ComposingNormalizer {
            decomposing_normalizer,
            canonical_compositions: CanonicalCompositionsPayload::Current(canonical_compositions),
        })
    }
}

#[cfg(feature = "utf16_iter")]
struct IsNormalizedSinkUtf16<'a> {
    expect: &'a [u16],
}

#[cfg(feature = "utf16_iter")]
impl<'a> IsNormalizedSinkUtf16<'a> {
    pub fn new(slice: &'a [u16]) -> Self {
        IsNormalizedSinkUtf16 { expect: slice }
    }
    pub fn remaining_len(&self) -> usize {
        self.expect.len()
    }
}

#[cfg(feature = "utf16_iter")]
impl write16::Write16 for IsNormalizedSinkUtf16<'_> {
    fn write_slice(&mut self, s: &[u16]) -> core::fmt::Result {
        // We know that if we get a slice, it's a pass-through,
        // so we can compare addresses. Indexing is OK, because
        // an indexing failure would be a code bug rather than
        // an input or data issue.
        #[expect(clippy::indexing_slicing)]
        if core::ptr::eq(s.as_ptr(), self.expect.as_ptr()) {
            self.expect = &self.expect[s.len()..];
            Ok(())
        } else {
            Err(core::fmt::Error {})
        }
    }

    fn write_char(&mut self, c: char) -> core::fmt::Result {
        let mut iter = utf16_iter::ErrorReportingUtf16Chars::new(self.expect);
        if iter.next() == Some(Ok(c)) {
            self.expect = iter.as_slice();
            Ok(())
        } else {
            Err(core::fmt::Error {})
        }
    }
}

#[cfg(feature = "utf8_iter")]
struct IsNormalizedSinkUtf8<'a> {
    expect: &'a [u8],
}

#[cfg(feature = "utf8_iter")]
impl<'a> IsNormalizedSinkUtf8<'a> {
    pub fn new(slice: &'a [u8]) -> Self {
        IsNormalizedSinkUtf8 { expect: slice }
    }
    pub fn remaining_len(&self) -> usize {
        self.expect.len()
    }
}

#[cfg(feature = "utf8_iter")]
impl core::fmt::Write for IsNormalizedSinkUtf8<'_> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        // We know that if we get a slice, it's a pass-through,
        // so we can compare addresses. Indexing is OK, because
        // an indexing failure would be a code bug rather than
        // an input or data issue.
        #[expect(clippy::indexing_slicing)]
        if core::ptr::eq(s.as_ptr(), self.expect.as_ptr()) {
            self.expect = &self.expect[s.len()..];
            Ok(())
        } else {
            Err(core::fmt::Error {})
        }
    }

    fn write_char(&mut self, c: char) -> core::fmt::Result {
        let mut iter = utf8_iter::ErrorReportingUtf8Chars::new(self.expect);
        if iter.next() == Some(Ok(c)) {
            self.expect = iter.as_slice();
            Ok(())
        } else {
            Err(core::fmt::Error {})
        }
    }
}

struct IsNormalizedSinkStr<'a> {
    expect: &'a str,
}

impl<'a> IsNormalizedSinkStr<'a> {
    pub fn new(slice: &'a str) -> Self {
        IsNormalizedSinkStr { expect: slice }
    }
    pub fn remaining_len(&self) -> usize {
        self.expect.len()
    }
}

impl core::fmt::Write for IsNormalizedSinkStr<'_> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        // We know that if we get a slice, it's a pass-through,
        // so we can compare addresses. Indexing is OK, because
        // an indexing failure would be a code bug rather than
        // an input or data issue.
        if core::ptr::eq(s.as_ptr(), self.expect.as_ptr()) {
            self.expect = &self.expect[s.len()..];
            Ok(())
        } else {
            Err(core::fmt::Error {})
        }
    }

    fn write_char(&mut self, c: char) -> core::fmt::Result {
        let mut iter = self.expect.chars();
        if iter.next() == Some(c) {
            self.expect = iter.as_str();
            Ok(())
        } else {
            Err(core::fmt::Error {})
        }
    }
}
