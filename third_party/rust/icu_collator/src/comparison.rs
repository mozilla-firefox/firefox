// This file is part of ICU4X. For terms of use, please see the file
// called LICENSE at the top level of the ICU4X source tree
// (online at: https://github.com/unicode-org/icu4x/blob/main/LICENSE ).

// Various collation-related algorithms and constants in this file are
// adapted from ICU4C and, therefore, are subject to the ICU license as
// described in LICENSE.

//! This module holds the `Collator` struct whose `compare_impl()` contains
//! the comparison of collation element sequences.

use alloc::collections::VecDeque;
use alloc::vec::Vec;

use crate::elements::CharacterAndClassAndTrieValue;
use crate::elements::CollationElement32;
use crate::elements::Tag;
use crate::elements::BACKWARD_COMBINING_MARKER;
use crate::elements::CE_BUFFER_SIZE;
use crate::elements::FALLBACK_CE32;
use crate::elements::HANGUL_N_COUNT;
use crate::elements::HANGUL_S_BASE;
use crate::elements::HANGUL_S_COUNT;
use crate::elements::HANGUL_T_COUNT;
use crate::elements::IDENTICAL_PREFIX_HANGUL_MARKER_CE32;
use crate::elements::NON_ROUND_TRIP_MARKER;
use crate::elements::{
    char_from_u32, CollationElement, CollationElements, NonPrimary, FFFD_CE32,
    HANGUL_SYLLABLE_MARKER, HIGH_ZEROS_MASK, JAMO_COUNT, LOW_ZEROS_MASK, NO_CE, NO_CE_PRIMARY,
    NO_CE_QUATERNARY, NO_CE_SECONDARY, NO_CE_TERTIARY, OPTIMIZED_DIACRITICS_MAX_COUNT,
    QUATERNARY_MASK,
};
use crate::options::CollatorOptionsBitField;
use crate::options::{
    AlternateHandling, CollatorOptions, MaxVariable, ResolvedCollatorOptions, Strength,
};
use crate::preferences::{CollationCaseFirst, CollationNumericOrdering, CollationType};
use crate::provider::CollationData;
use crate::provider::CollationDiacritics;
use crate::provider::CollationDiacriticsV1;
use crate::provider::CollationJamo;
use crate::provider::CollationJamoV1;
use crate::provider::CollationMetadataV1;
use crate::provider::CollationReordering;
use crate::provider::CollationReorderingV1;
use crate::provider::CollationRootV1;
use crate::provider::CollationSpecialPrimariesV1;
use crate::provider::CollationSpecialPrimariesValidated;
use crate::provider::CollationTailoringV1;
use core::cmp::Ordering;
use core::convert::{Infallible, TryFrom};
use icu_collections::codepointtrie::iter::CharsWithTrieDefaultForAsciiEx;
use icu_collections::codepointtrie::iter::CharsWithTrieEx;
#[cfg(feature = "latin1")]
use icu_collections::codepointtrie::iter::Latin1CharsWithTrieEx;
use icu_collections::codepointtrie::iter::Utf16CharsWithTrieEx;
use icu_collections::codepointtrie::iter::Utf8CharsWithTrieDefaultForAsciiEx;
use icu_collections::codepointtrie::iter::Utf8CharsWithTrieEx;
use icu_collections::codepointtrie::iter::WithTrie;
use icu_collections::codepointtrie::AbstractCodePointTrie;
#[cfg(feature = "serde")]
use icu_collections::codepointtrie::CodePointTrie;
#[cfg(not(feature = "serde"))]
use icu_collections::codepointtrie::FastCodePointTrie;
use icu_collections::codepointtrie::TypedCodePointTrie;
use icu_normalizer::provider::DecompositionData;
use icu_normalizer::provider::DecompositionTables;
use icu_normalizer::provider::NormalizerNfdDataV1;
use icu_normalizer::provider::NormalizerNfdTablesV1;
use icu_provider::marker::ErasedMarker;
use icu_provider::prelude::*;
use smallvec::SmallVec;
#[cfg(feature = "latin1")]
use utf_types::utf8::Ascii;
use zerovec::ule::AsULE;

#[cfg(feature = "serde")]
type NormTrie<'trie> = CodePointTrie<'trie, u32>;

#[cfg(not(feature = "serde"))]
type NormTrie<'trie> = FastCodePointTrie<'trie, u32>;

// Special sort key bytes for all levels.
const LEVEL_SEPARATOR_BYTE: u8 = 1;

/// Merge-sort-key separator.
///
/// Same as the unique primary and identical-level weights of U+FFFE.  Must not
/// be used as primary compression low terminator.  Otherwise usable.
const MERGE_SEPARATOR: char = '\u{fffe}';
const MERGE_SEPARATOR_BYTE: u8 = 2;
const MERGE_SEPARATOR_PRIMARY: u32 = 0x02000000;

/// Primary compression low terminator, must be greater than [`MERGE_SEPARATOR_BYTE`].
///
/// Reserved value in primary second byte if the lead byte is compressible.
/// Otherwise usable in all CE weight bytes.
const PRIMARY_COMPRESSION_LOW_BYTE: u8 = 3;

/// Primary compression high terminator.
///
/// Reserved value in primary second byte if the lead byte is compressible.
/// Otherwise usable in all CE weight bytes.
const PRIMARY_COMPRESSION_HIGH_BYTE: u8 = 0xff;

/// Default secondary/tertiary weight lead byte.
const COMMON_BYTE: u8 = 5;
const COMMON_WEIGHT16: u16 = 0x0500;

// Internal flags for sort key generation
const PRIMARY_LEVEL_FLAG: u8 = 0x01;
const SECONDARY_LEVEL_FLAG: u8 = 0x02;
const CASE_LEVEL_FLAG: u8 = 0x04;
const TERTIARY_LEVEL_FLAG: u8 = 0x08;
const QUATERNARY_LEVEL_FLAG: u8 = 0x10;

const LEVEL_MASKS: [u8; Strength::Identical as usize + 1] = [
    PRIMARY_LEVEL_FLAG,
    PRIMARY_LEVEL_FLAG | SECONDARY_LEVEL_FLAG,
    PRIMARY_LEVEL_FLAG | SECONDARY_LEVEL_FLAG | TERTIARY_LEVEL_FLAG,
    PRIMARY_LEVEL_FLAG | SECONDARY_LEVEL_FLAG | TERTIARY_LEVEL_FLAG | QUATERNARY_LEVEL_FLAG,
    0,
    0,
    0,
    PRIMARY_LEVEL_FLAG | SECONDARY_LEVEL_FLAG | TERTIARY_LEVEL_FLAG | QUATERNARY_LEVEL_FLAG,
];

// Internal constants for indexing into the below compression configurations
const WEIGHT_LOW: usize = 0;
const WEIGHT_MIDDLE: usize = 1;
const WEIGHT_HIGH: usize = 2;
const WEIGHT_MAX_COUNT: usize = 3;

// Secondary level: Compress up to 33 common weights as 05..25 or 25..45.
const SEC_COMMON: [u8; 4] = [COMMON_BYTE, COMMON_BYTE + 0x20, COMMON_BYTE + 0x40, 0x21];

// Case level, lowerFirst: Compress up to 7 common weights as 1..7 or 7..13.
const CASE_LOWER_FIRST_COMMON: [u8; 4] = [1, 7, 13, 7];

// Case level, upperFirst: Compress up to 13 common weights as 3..15.
const CASE_UPPER_FIRST_COMMON: [u8; 4] = [3, 0 /* unused */, 15, 13];

// Tertiary level only (no case): Compress up to 97 common weights as 05..65 or 65..C5.
const TER_ONLY_COMMON: [u8; 4] = [COMMON_BYTE, COMMON_BYTE + 0x60, COMMON_BYTE + 0xc0, 0x61];

// Tertiary with case, lowerFirst: Compress up to 33 common weights as 05..25 or 25..45.
const TER_LOWER_FIRST_COMMON: [u8; 4] = [COMMON_BYTE, COMMON_BYTE + 0x20, COMMON_BYTE + 0x40, 0x21];

// Tertiary with case, upperFirst: Compress up to 33 common weights as 85..A5 or A5..C5.
const TER_UPPER_FIRST_COMMON: [u8; 4] = [
    COMMON_BYTE + 0x80,
    COMMON_BYTE + 0x80 + 0x20,
    COMMON_BYTE + 0x80 + 0x40,
    0x21,
];

const QUAT_COMMON: [u8; 4] = [0x1c, 0x1c + 0x70, 0x1c + 0xe0, 0x71];
const QUAT_SHIFTED_LIMIT_BYTE: u8 = QUAT_COMMON[WEIGHT_LOW] - 1; // 0x1b

// Do not use byte values 0, 1, 2 because they are separators in sort keys.
const SLOPE_MIN: i32 = 3;
const SLOPE_MAX: i32 = 0xff;
const SLOPE_MIDDLE: i32 = 0x81;
const SLOPE_TAIL_COUNT: i32 = SLOPE_MAX - SLOPE_MIN + 1;
const SLOPE_SINGLE: i32 = 80;
const SLOPE_LEAD_2: i32 = 42;
const SLOPE_LEAD_3: i32 = 3;

// The difference value range for single-byters.
const SLOPE_REACH_POS_1: i32 = SLOPE_SINGLE;
const SLOPE_REACH_NEG_1: i32 = -SLOPE_SINGLE;

// The difference value range for double-byters.
const SLOPE_REACH_POS_2: i32 = SLOPE_LEAD_2 * SLOPE_TAIL_COUNT + (SLOPE_LEAD_2 - 1);
const SLOPE_REACH_NEG_2: i32 = -SLOPE_REACH_POS_2 - 1;

// The difference value range for 3-byters.
const SLOPE_REACH_POS_3: i32 = SLOPE_LEAD_3 * SLOPE_TAIL_COUNT * SLOPE_TAIL_COUNT
    + (SLOPE_LEAD_3 - 1) * SLOPE_TAIL_COUNT
    + (SLOPE_TAIL_COUNT - 1);
const SLOPE_REACH_NEG_3: i32 = -SLOPE_REACH_POS_3 - 1;

// The lead byte start values.
const SLOPE_START_POS_2: i32 = SLOPE_MIDDLE + SLOPE_SINGLE + 1;
const SLOPE_START_POS_3: i32 = SLOPE_START_POS_2 + SLOPE_LEAD_2;
const SLOPE_START_NEG_2: i32 = SLOPE_MIDDLE + SLOPE_REACH_NEG_1;
const SLOPE_START_NEG_3: i32 = SLOPE_START_NEG_2 - SLOPE_LEAD_2;

struct AnyQuaternaryAccumulator(u32);

impl AnyQuaternaryAccumulator {
    #[inline(always)]
    pub fn new() -> Self {
        AnyQuaternaryAccumulator(0)
    }
    #[inline(always)]
    pub fn accumulate(&mut self, non_primary: NonPrimary) {
        self.0 |= non_primary.bits()
    }
    #[inline(always)]
    pub fn has_quaternary(&self) -> bool {
        self.0 & u32::from(QUATERNARY_MASK) != 0
    }
}

/// `true` iff `i` is greater or equal to `start` and less or equal
/// to `end`.
#[inline(always)]
fn in_inclusive_range16(i: u16, start: u16, end: u16) -> bool {
    i.wrapping_sub(start) <= (end - start)
}

/// Finds the identical prefix of `left` and `right` containing
/// Latin1.
///
/// Returns the identical prefix, the part of `left` after the
/// prefix, and the part of `right` after the prefix.
///
/// ✨ *Enabled with the `latin1` Cargo feature.*
#[cfg(feature = "latin1")]
fn split_prefix_latin1<'a, 'b>(left: &'a [u8], right: &'b [u8]) -> (&'a [u8], &'a [u8], &'b [u8]) {
    let i = left
        .iter()
        .zip(right.iter())
        .take_while(|(l, r)| l == r)
        .count();
    if let Some((head, left_tail)) = left.split_at_checked(i) {
        if let Some(right_tail) = right.get(i..) {
            return (head, left_tail, right_tail);
        }
    }
    (&[], left, right)
}

/// Finds the identical prefix of `left` containing Latin1
/// and `right` containing potentially ill-formed UTF-16.
///
/// Returns the identical prefix, the part of `left` after the
/// prefix, and the part of `right` after the prefix.
///
/// ✨ *Enabled with the `latin1` Cargo feature.*
#[cfg(feature = "latin1")]
fn split_prefix_latin1_utf16<'a, 'b>(
    left: &'a [u8],
    right: &'b [u16],
) -> (&'a [u8], &'a [u8], &'b [u16]) {
    let i = left
        .iter()
        .zip(right.iter())
        .take_while(|(l, r)| u16::from(**l) == **r)
        .count();
    if let Some((head, left_tail)) = left.split_at_checked(i) {
        if let Some(right_tail) = right.get(i..) {
            return (head, left_tail, right_tail);
        }
    }
    (&[], left, right)
}

/// Finds the identical prefix of `left` and `right` containing
/// potentially ill-formed UTF-16, while avoiding splitting a
/// well-formed surrogate pair. In case of ill-formed
/// UTF-16, the prefix is not guaranteed to be maximal.
///
/// Returns the identical prefix, the part of `left` after the
/// prefix, and the part of `right` after the prefix.
fn split_prefix_u16<'a, 'b>(
    left: &'a [u16],
    right: &'b [u16],
) -> (&'a [u16], &'a [u16], &'b [u16]) {
    let mut i = left
        .iter()
        .zip(right.iter())
        .take_while(|(l, r)| l == r)
        .count();
    if i != 0 {
        if let Some(&last) = left.get(i.wrapping_sub(1)) {
            if in_inclusive_range16(last, 0xD800, 0xDBFF) {
                i -= 1;
            }
            if let Some((head, left_tail)) = left.split_at_checked(i) {
                if let Some(right_tail) = right.get(i..) {
                    return (head, left_tail, right_tail);
                }
            }
        }
    }
    (&[], left, right)
}

/// Finds the identical prefix of `left` and `right` containing
/// potentially ill-formed UTF-8, while avoiding splitting a UTF-8
/// byte sequence. In case of ill-formed UTF-8, the prefix is
/// not guaranteed to be maximal.
///
/// Returns the identical prefix, the part of `left` after the
/// prefix, and the part of `right` after the prefix.
fn split_prefix_u8<'a, 'b>(left: &'a [u8], right: &'b [u8]) -> (&'a [u8], &'a [u8], &'b [u8]) {
    let mut i = left
        .iter()
        .zip(right.iter())
        .take_while(|(l, r)| l == r)
        .count();
    if i != 0 {
        // Tails must not start with a UTF-8 continuation
        // byte unless it's the first byte of the original
        // slice.

        // First, left and right differ, but since they
        // are the same afterwards, one of them needs checking
        // only once.
        if let Some(right_first) = right.get(i) {
            if (right_first & 0b1100_0000) == 0b1000_0000 {
                i -= 1;
            }
        }
        while i != 0 {
            if let Some(left_first) = left.get(i) {
                if (left_first & 0b1100_0000) == 0b1000_0000 {
                    i -= 1;
                    continue;
                }
            }
            break;
        }
        if let Some((head, left_tail)) = left.split_at_checked(i) {
            if let Some(right_tail) = right.get(i..) {
                return (head, left_tail, right_tail);
            }
        }
    }
    (&[], left, right)
}

/// Finds the identical prefix of `left` and `right` containing
/// guaranteed well-format UTF-8.
///
/// Returns the identical prefix, the part of `left` after the
/// prefix, and the part of `right` after the prefix.
fn split_prefix<'a, 'b>(left: &'a str, right: &'b str) -> (&'a str, &'a str, &'b str) {
    let left_bytes = left.as_bytes();
    let right_bytes = right.as_bytes();
    let mut i = left_bytes
        .iter()
        .zip(right_bytes.iter())
        .take_while(|(l, r)| l == r)
        .count();
    if i != 0 {
        // Tails must not start with a UTF-8 continuation
        // byte.

        // Since the inputs are valid UTF-8, the first byte
        // of either input slice cannot be a contination slice,
        // so we may rely on finding a lead byte when walking
        // backwards.

        // Since the inputs are valid UTF-8, if a tail starts
        // with a continuation, both tails must start with a
        // continuation, since the most recent lead byte must
        // be equal, so the difference is within valid UTF-8
        // sequences of equal length.

        // Therefore, it's sufficient to examine only one of
        // the sides.
        loop {
            if let Some(left_first) = left_bytes.get(i) {
                if (left_first & 0b1100_0000) == 0b1000_0000 {
                    i -= 1;
                    continue;
                }
            }
            break;
        }
        // The methods below perform useless UTF-8 boundary checks,
        // since we just checked. However, avoiding `unsafe` to
        // make this code easier to audit.
        if let Some((head, left_tail)) = left.split_at_checked(i) {
            if let Some(right_tail) = right.get(i..) {
                return (head, left_tail, right_tail);
            }
        }
    }
    ("", left, right)
}

/// Holder struct for payloads that are locale-dependent. (For code
/// reuse between owned and borrowed cases.)
#[derive(Debug)]
struct LocaleSpecificDataHolder {
    tailoring: Option<DataPayload<CollationTailoringV1>>,
    diacritics: DataPayload<CollationDiacriticsV1>,
    reordering: Option<DataPayload<CollationReorderingV1>>,
    merged_options: CollatorOptionsBitField,
    lithuanian_dot_above: bool,
}

icu_locale_core::preferences::define_preferences!(
    /// The preferences for collation.
    ///
    /// # Preferences
    ///
    /// Examples for using the different preferences below can be found in the [crate-level docs](crate).
    ///
    /// ## Case First
    ///
    /// See the [spec](https://www.unicode.org/reports/tr35/tr35-collation.html#Case_Parameters).
    /// This is the BCP47 key `kf`. Three possibilities: [`CollationCaseFirst::False`] (default,
    /// except for Danish and Maltese), [`CollationCaseFirst::Lower`], and [`CollationCaseFirst::Upper`]
    /// (default for Danish and Maltese).
    ///
    /// ## Numeric
    ///
    /// This is the BCP47 key `kn`. When set to [`CollationNumericOrdering::True`], any sequence of decimal
    /// digits (General_Category = Nd) is sorted at the primary level according to the
    /// numeric value. The default is [`CollationNumericOrdering::False`].
    [Copy]
    CollatorPreferences,
    {
        /// The collation type. This corresponds to the `-u-co` BCP-47 tag.
        collation_type: CollationType,
        /// Treatment of case. (Large and small kana differences are treated as case differences.)
        /// This corresponds to the `-u-kf` BCP-47 tag.
        case_first: CollationCaseFirst,
        /// When set to `True`, any sequence of decimal digits is sorted at a primary level according
        /// to the numeric value.
        /// This corresponds to the `-u-kn` BPC-47 tag.
        numeric_ordering: CollationNumericOrdering
    }
);

impl LocaleSpecificDataHolder {
    /// The constructor code reused between owned and borrowed cases.
    fn try_new_unstable_internal<D>(
        provider: &D,
        prefs: CollatorPreferences,
        options: CollatorOptions,
    ) -> Result<Self, DataError>
    where
        D: DataProvider<CollationTailoringV1>
            + DataProvider<CollationDiacriticsV1>
            + DataProvider<CollationMetadataV1>
            + DataProvider<CollationReorderingV1>
            + ?Sized,
    {
        let marker_attributes = prefs
            .collation_type
            .as_ref()
            // all collation types are valid marker attributes
            .map(|c| DataMarkerAttributes::from_str_or_panic(c.as_str()))
            .unwrap_or_default();

        let data_locale = CollationTailoringV1::make_locale(prefs.locale_preferences);
        let req = DataRequest {
            id: DataIdentifierBorrowed::for_marker_attributes_and_locale(
                marker_attributes,
                &data_locale,
            ),
            metadata: {
                let mut metadata = DataRequestMetadata::default();
                metadata.silent = true;
                metadata
            },
        };

        let fallback_req = DataRequest {
            id: DataIdentifierBorrowed::for_marker_attributes_and_locale(
                Default::default(),
                &data_locale,
            ),
            ..Default::default()
        };

        let metadata_payload: DataPayload<CollationMetadataV1> = provider
            .load(req)
            .or_else(|_| provider.load(fallback_req))?
            .payload;

        let metadata = metadata_payload.get();

        let tailoring: Option<DataPayload<CollationTailoringV1>> = if metadata.tailored() {
            Some(
                provider
                    .load(req)
                    .or_else(|_| provider.load(fallback_req))?
                    .payload,
            )
        } else {
            None
        };

        let reordering: Option<DataPayload<CollationReorderingV1>> = if metadata.reordering() {
            Some(
                provider
                    .load(req)
                    .or_else(|_| provider.load(fallback_req))?
                    .payload,
            )
        } else {
            None
        };

        if let Some(reordering) = &reordering {
            if reordering.get().reorder_table.len() != 256 {
                return Err(DataError::custom("invalid").with_marker(CollationReorderingV1::INFO));
            }
        }

        let tailored_diacritics = metadata.tailored_diacritics();
        let diacritics: DataPayload<CollationDiacriticsV1> = provider
            .load(if tailored_diacritics {
                req
            } else {
                Default::default()
            })?
            .payload;

        if tailored_diacritics {
            // In the tailored case we accept a shorter table in which case the tailoring is
            // responsible for supplying the missing values in the trie.
            // As of June 2022, none of the collations actually use a shortened table.
            // Vietnamese and Ewe load a full-length alternative table and the rest use
            // the default one.
            if diacritics.get().secondaries.len() > OPTIMIZED_DIACRITICS_MAX_COUNT {
                return Err(DataError::custom("invalid").with_marker(CollationDiacriticsV1::INFO));
            }
        } else if diacritics.get().secondaries.len() != OPTIMIZED_DIACRITICS_MAX_COUNT {
            return Err(DataError::custom("invalid").with_marker(CollationDiacriticsV1::INFO));
        }

        let mut altered_defaults = CollatorOptionsBitField::default();

        if metadata.alternate_shifted() {
            altered_defaults.set_alternate_handling(Some(AlternateHandling::Shifted));
        }
        if metadata.backward_second_level() {
            altered_defaults.set_backward_second_level(Some(true));
        }

        altered_defaults.set_case_first(Some(metadata.case_first()));
        altered_defaults.set_max_variable(Some(metadata.max_variable()));

        let mut merged_options = CollatorOptionsBitField::from(options);
        merged_options.set_case_first(prefs.case_first);
        merged_options.set_numeric_from_enum(prefs.numeric_ordering);
        merged_options.set_defaults(altered_defaults);

        Ok(LocaleSpecificDataHolder {
            tailoring,
            diacritics,
            merged_options,
            reordering,
            lithuanian_dot_above: metadata.lithuanian_dot_above(),
        })
    }
}

/// Compares strings according to culturally-relevant ordering.
#[derive(Debug)]
pub struct Collator {
    special_primaries: DataPayload<ErasedMarker<CollationSpecialPrimariesValidated<'static>>>,
    root: DataPayload<CollationRootV1>,
    tailoring: Option<DataPayload<CollationTailoringV1>>,
    jamo: DataPayload<CollationJamoV1>,
    diacritics: DataPayload<CollationDiacriticsV1>,
    options: CollatorOptionsBitField,
    reordering: Option<DataPayload<CollationReorderingV1>>,
    decompositions: DataPayload<NormalizerNfdDataV1>,
    tables: DataPayload<NormalizerNfdTablesV1>,
    lithuanian_dot_above: bool,
}

impl Collator {
    /// Constructs a borrowed version of this type for more efficient querying.
    pub fn as_borrowed(&self) -> CollatorBorrowed<'_> {
        CollatorBorrowed {
            special_primaries: self.special_primaries.get(),
            root: self.root.get(),
            tailoring: if let Some(t) = self.tailoring.as_ref() {
                t.get()
            } else {
                self.root.get()
            },
            jamo: self.jamo.get(),
            diacritics: self.diacritics.get(),
            options: self.options,
            reordering: self.reordering.as_ref().map(|s| s.get()),
            decompositions: self.decompositions.get(),
            tables: self.tables.get(),
            lithuanian_dot_above: self.lithuanian_dot_above,
        }
    }

    /// Creates `CollatorBorrowed` for the given locale and options from compiled data.
    #[cfg(feature = "compiled_data")]
    pub fn try_new(
        prefs: CollatorPreferences,
        options: CollatorOptions,
    ) -> Result<CollatorBorrowed<'static>, DataError> {
        CollatorBorrowed::try_new(prefs, options)
    }

    icu_provider::gen_buffer_data_constructors!(
        (prefs: CollatorPreferences, options: CollatorOptions) -> error: DataError,
        functions: [
            try_new: skip,
            try_new_with_buffer_provider,
            try_new_unstable,
            Self
        ]
    );

    #[doc = icu_provider::gen_buffer_unstable_docs!(UNSTABLE, Self::try_new)]
    pub fn try_new_unstable<D>(
        provider: &D,
        prefs: CollatorPreferences,
        options: CollatorOptions,
    ) -> Result<Self, DataError>
    where
        D: DataProvider<CollationSpecialPrimariesV1>
            + DataProvider<CollationRootV1>
            + DataProvider<CollationTailoringV1>
            + DataProvider<CollationDiacriticsV1>
            + DataProvider<CollationJamoV1>
            + DataProvider<CollationMetadataV1>
            + DataProvider<CollationReorderingV1>
            + DataProvider<NormalizerNfdDataV1>
            + DataProvider<NormalizerNfdTablesV1>
            + ?Sized,
    {
        Self::try_new_unstable_internal(
            provider,
            provider.load(Default::default())?.payload,
            provider.load(Default::default())?.payload,
            provider.load(Default::default())?.payload,
            provider.load(Default::default())?.payload,
            provider.load(Default::default())?.payload,
            prefs,
            options,
        )
    }

    #[expect(clippy::too_many_arguments)]
    fn try_new_unstable_internal<D>(
        provider: &D,
        root: DataPayload<CollationRootV1>,
        decompositions: DataPayload<NormalizerNfdDataV1>,
        tables: DataPayload<NormalizerNfdTablesV1>,
        jamo: DataPayload<CollationJamoV1>,
        special_primaries: DataPayload<CollationSpecialPrimariesV1>,
        prefs: CollatorPreferences,
        options: CollatorOptions,
    ) -> Result<Self, DataError>
    where
        D: DataProvider<CollationRootV1>
            + DataProvider<CollationTailoringV1>
            + DataProvider<CollationDiacriticsV1>
            + DataProvider<CollationMetadataV1>
            + DataProvider<CollationReorderingV1>
            + ?Sized,
    {
        let locale_dependent =
            LocaleSpecificDataHolder::try_new_unstable_internal(provider, prefs, options)?;

        // TODO: redesign Korean search collation handling
        if jamo.get().ce32s.len() != JAMO_COUNT {
            return Err(DataError::custom("invalid").with_marker(CollationJamoV1::INFO));
        }

        // `variant_count` isn't stable yet:
        // https://github.com/rust-lang/rust/issues/73662
        if special_primaries.get().last_primaries.len() <= (MaxVariable::Currency as usize) {
            return Err(DataError::custom("invalid").with_marker(CollationSpecialPrimariesV1::INFO));
        }
        let special_primaries = special_primaries.map_project(|csp, _| {
            let compressible_bytes = (csp.last_primaries.len()
                == MaxVariable::Currency as usize + 16)
                .then(|| {
                    csp.last_primaries
                        .as_maybe_borrowed()?
                        .as_ule_slice()
                        .get((MaxVariable::Currency as usize)..)?
                        .try_into()
                        .ok()
                })
                .flatten()
                .unwrap_or(
                    CollationSpecialPrimariesValidated::HARDCODED_COMPRESSIBLE_BYTES_FALLBACK,
                );

            CollationSpecialPrimariesValidated {
                last_primaries: csp.last_primaries.truncated(MaxVariable::Currency as usize),
                numeric_primary: csp.numeric_primary,
                compressible_bytes,
            }
        });

        Ok(Collator {
            special_primaries,
            root,
            tailoring: locale_dependent.tailoring,
            jamo,
            diacritics: locale_dependent.diacritics,
            options: locale_dependent.merged_options,
            reordering: locale_dependent.reordering,
            decompositions,
            tables,
            lithuanian_dot_above: locale_dependent.lithuanian_dot_above,
        })
    }
}

macro_rules! quick_primary_compare {
    ($left_primary:ident,
     $right_primary:ident,
     $variable_top:ident,
     $self:ident,
    ) => {
        if ($left_primary != $right_primary)
            && ($left_primary != 0)
            && ($right_primary != 0)
            && !($left_primary < $variable_top && $left_primary > MERGE_SEPARATOR_PRIMARY)
            && !($right_primary < $variable_top && $right_primary > MERGE_SEPARATOR_PRIMARY)
        {
            if let Some(reordering) = &$self.reordering {
                $left_primary = reordering.reorder($left_primary);
                $right_primary = reordering.reorder($right_primary);
            }
            if $left_primary < $right_primary {
                return Ordering::Less;
            }
            return Ordering::Greater;
        }
    };
}

macro_rules! hangul_syllable_compare {
    ($left_hangul_offset:ident,
     $right_hangul_offset:ident,
    ) => {
        let left_l = $left_hangul_offset / HANGUL_N_COUNT;
        let right_l = $right_hangul_offset / HANGUL_N_COUNT;
        if left_l != right_l {
            // We don't really support jamo tailoring, so let's
            // compare the jamo directly.
            if left_l < right_l {
                return Ordering::Less;
            }
            return Ordering::Greater;
        }
        let left_v = ($left_hangul_offset % HANGUL_N_COUNT) / HANGUL_T_COUNT;
        let right_v = ($right_hangul_offset % HANGUL_N_COUNT) / HANGUL_T_COUNT;
        if left_v != right_v {
            // We don't really support jamo tailoring, so let's
            // compare the jamo directly.
            if left_v < right_v {
                return Ordering::Less;
            }
            return Ordering::Greater;
        }
        let left_t = $left_hangul_offset % HANGUL_T_COUNT;
        let right_t = $right_hangul_offset % HANGUL_T_COUNT;
        // If either syllable is a two-jamo syllable, we fall through.
        if left_t != right_t && left_t != 0 && right_t != 0 {
            // We don't really support jamo tailoring, so let's
            // compare the jamo directly.
            if left_t < right_t {
                return Ordering::Less;
            }
            return Ordering::Greater;
        }
    };
}

macro_rules! compare {
    ($(#[$meta:meta])*,
     $compare:ident,
     $left_slice:ty,
     $right_slice:ty,
     $split_prefix:ident,
     $left_to_iter:ident,
     $right_to_iter:ident,
     $self:ident,
     $left_tail:ident,
     $right_tail:ident,
     $primary_check:block,
     $variable_top:ident,
    ) => {
        $(#[$meta])*
        pub fn $compare(&$self, left: &$left_slice, right: &$right_slice) -> Ordering {
            let (head, $left_tail, $right_tail) = $split_prefix(left, right);
            if $left_tail.is_empty() && $right_tail.is_empty() {
                return Ordering::Equal;
            }
            let $variable_top = $self.variable_top();
            if head.is_empty() {
                $primary_check
            }
            let norm_trie = $self.norm_trie();
            let ret = $self.compare_impl($left_tail.$left_to_iter(norm_trie), $right_tail.$right_to_iter(norm_trie), head.$left_to_iter(norm_trie), $variable_top);
            if $self.options.strength() == Strength::Identical && ret == Ordering::Equal {
                // We don't need to remove the leading U+0000, because it compares equal anyway.
                return icu_normalizer::new_decomposition($left_tail.$left_to_iter(norm_trie), $self.tables).map(|c| if c != MERGE_SEPARATOR { c as i32 } else { -1i32 }).cmp(
                    icu_normalizer::new_decomposition($right_tail.$right_to_iter(norm_trie), $self.tables).map(|c| if c != MERGE_SEPARATOR { c as i32 } else { -1i32 }),
                );
            }
            ret
        }
    }
}

/// Compares strings according to culturally-relevant ordering,
/// borrowed version.
#[derive(Debug)]
pub struct CollatorBorrowed<'a> {
    special_primaries: &'a CollationSpecialPrimariesValidated<'a>,
    root: &'a CollationData<'a>,
    tailoring: &'a CollationData<'a>,
    jamo: &'a CollationJamo<'a>,
    diacritics: &'a CollationDiacritics<'a>,
    options: CollatorOptionsBitField,
    reordering: Option<&'a CollationReordering<'a>>,
    decompositions: &'a DecompositionData<'a>,
    tables: &'a DecompositionTables<'a>,
    lithuanian_dot_above: bool,
}

impl CollatorBorrowed<'static> {
    /// Creates a collator for the given locale and options from compiled data.
    #[cfg(feature = "compiled_data")]
    pub fn try_new(
        prefs: CollatorPreferences,
        options: CollatorOptions,
    ) -> Result<Self, DataError> {
        // These are assigned to locals in order to keep the code after these assignments
        // copypaste-compatible with `Collator::try_new_unstable_internal`.

        let provider = &crate::provider::Baked;
        let decompositions = icu_normalizer::provider::Baked::SINGLETON_NORMALIZER_NFD_DATA_V1;
        let tables = icu_normalizer::provider::Baked::SINGLETON_NORMALIZER_NFD_TABLES_V1;
        let root = crate::provider::Baked::SINGLETON_COLLATION_ROOT_V1;
        let jamo = crate::provider::Baked::SINGLETON_COLLATION_JAMO_V1;

        let locale_dependent =
            LocaleSpecificDataHolder::try_new_unstable_internal(provider, prefs, options)?;

        // TODO: redesign Korean search collation handling
        const _: () = assert!(
            crate::provider::Baked::SINGLETON_COLLATION_JAMO_V1
                .ce32s
                .as_slice()
                .len()
                == JAMO_COUNT
        );

        // `variant_count` isn't stable yet:
        // https://github.com/rust-lang/rust/issues/73662
        const _: () = assert!(
            crate::provider::Baked::SINGLETON_COLLATION_SPECIAL_PRIMARIES_V1
                .last_primaries
                .as_slice()
                .len()
                > (MaxVariable::Currency as usize)
        );

        let special_primaries = const {
            &CollationSpecialPrimariesValidated {
                last_primaries: zerovec::ZeroSlice::from_ule_slice(
                    crate::provider::Baked::SINGLETON_COLLATION_SPECIAL_PRIMARIES_V1
                        .last_primaries
                        .as_slice()
                        .as_ule_slice()
                        .split_at(MaxVariable::Currency as usize)
                        .0,
                )
                .as_zerovec(),
                numeric_primary: crate::provider::Baked::SINGLETON_COLLATION_SPECIAL_PRIMARIES_V1
                    .numeric_primary,
                compressible_bytes: {
                    const C: &[<u16 as AsULE>::ULE] =
                        crate::provider::Baked::SINGLETON_COLLATION_SPECIAL_PRIMARIES_V1
                            .last_primaries
                            .as_slice()
                            .as_ule_slice();
                    if C.len() == MaxVariable::Currency as usize + 16 {
                        let i = MaxVariable::Currency as usize;
                        #[allow(clippy::indexing_slicing)] // protected, const
                        &[
                            C[i],
                            C[i + 1],
                            C[i + 2],
                            C[i + 3],
                            C[i + 4],
                            C[i + 5],
                            C[i + 6],
                            C[i + 7],
                            C[i + 8],
                            C[i + 9],
                            C[i + 10],
                            C[i + 11],
                            C[i + 12],
                            C[i + 13],
                            C[i + 14],
                            C[i + 15],
                        ]
                    } else {
                        CollationSpecialPrimariesValidated::HARDCODED_COMPRESSIBLE_BYTES_FALLBACK
                    }
                },
            }
        };

        // Attribute belongs closer to `unwrap`, but
        // https://github.com/rust-lang/rust/issues/15701
        #[expect(clippy::unwrap_used)]
        Ok(CollatorBorrowed {
            special_primaries,
            root,
            // Unwrap is OK, because we know we have the baked provider.
            tailoring: if let Some(s) = locale_dependent.tailoring {
                s.get_static().unwrap()
            } else {
                root
            },
            jamo,
            // Unwrap is OK, because we know we have the baked provider.
            diacritics: locale_dependent.diacritics.get_static().unwrap(),
            options: locale_dependent.merged_options,
            // Unwrap is OK, because we know we have the baked provider.
            reordering: locale_dependent.reordering.map(|s| s.get_static().unwrap()),
            decompositions,
            tables,
            lithuanian_dot_above: locale_dependent.lithuanian_dot_above,
        })
    }

    /// Cheaply converts a [`CollatorBorrowed<'static>`] into a [`Collator`].
    ///
    /// Note: Due to branching and indirection, using [`Collator`] might inhibit some
    /// compile-time optimizations that are possible with [`CollatorBorrowed`].
    pub const fn static_to_owned(self) -> Collator {
        Collator {
            special_primaries: DataPayload::from_static_ref(self.special_primaries),
            root: DataPayload::from_static_ref(self.root),
            tailoring: Some(DataPayload::from_static_ref(self.tailoring)),
            jamo: DataPayload::from_static_ref(self.jamo),
            diacritics: DataPayload::from_static_ref(self.diacritics),
            options: self.options,
            reordering: if let Some(s) = self.reordering {
                // `map` not available in const context
                Some(DataPayload::from_static_ref(s))
            } else {
                None
            },
            decompositions: DataPayload::from_static_ref(self.decompositions),
            tables: DataPayload::from_static_ref(self.tables),
            lithuanian_dot_above: self.lithuanian_dot_above,
        }
    }
}

macro_rules! collation_elements {
    ($self:expr, $chars:expr, $numeric_primary:expr) => {{
        let jamo = <&[<u32 as AsULE>::ULE; JAMO_COUNT]>::try_from($self.jamo.ce32s.as_ule_slice());

        let jamo = jamo.unwrap();

        CollationElements::new(
            $chars,
            $self.root,
            $self.tailoring,
            jamo,
            &$self.diacritics.secondaries,
            $self.tables,
            $numeric_primary,
            $self.lithuanian_dot_above,
        )
    }};
}

impl<'data> CollatorBorrowed<'data> {
    /// The resolved options showing how the default options, the requested options,
    /// and the options from locale data were combined.
    pub fn resolved_options(&self) -> ResolvedCollatorOptions {
        self.options.into()
    }

    fn norm_trie(&self) -> &'data NormTrie<'data> {
        #[allow(clippy::useless_conversion)]
        <&NormTrie<'data>>::try_from(&self.decompositions.trie)
            .unwrap_or_else(|_| unreachable!("Incompatible data"))
    }

    compare!(
        /// Compare guaranteed well-formed UTF-8 slices.
        ,
        compare,
        str,
        str,
        split_prefix,
        chars_with_trie_default_for_ascii,
        chars_with_trie_default_for_ascii,
        self,
        left_tail,
        right_tail,
        {
            // Not macroized: Copy and paste to the other UTF-8 case below.
            let tailoring_trie = &self.tailoring.trie;
            if let Some((left_c, left_u32)) = left_tail.chars_with_trie(tailoring_trie).next() {
                // Logically, there's a bunch of stuff we could do from the left side alone to determine
                // that reading from the right side at all or reading from the right trie is useless.
                // Doing that seems to be a pessimization, at least in the absence of PGO.
                if let Some((right_c, right_u32)) = right_tail.chars_with_trie(tailoring_trie).next() {
                    let left_ce32 = CollationElement32::new(left_u32);
                    let right_ce32 = CollationElement32::new(right_u32);
                    let mut left_primary = left_ce32.to_primary_in_quick_check(self.tailoring);
                    // If `left_primary` is now zero, we don't really need to extract `right_primary`,
                    // but let's avoid the branch on the likely path.
                    let mut right_primary = right_ce32.to_primary_in_quick_check(self.tailoring);
                    quick_primary_compare!(left_primary, right_primary, variable_top, self,);
                    // Try Hangul.
                    let right_hangul_offset = u32::from(right_c).wrapping_sub(HANGUL_S_BASE);
                    if right_hangul_offset < HANGUL_S_COUNT {
                        let left_hangul_offset = u32::from(left_c).wrapping_sub(HANGUL_S_BASE);
                        if left_hangul_offset < HANGUL_S_COUNT {
                            hangul_syllable_compare!(left_hangul_offset, right_hangul_offset,);
                        }
                    }
                }
            }
            // Note: It might look like a good idea to cache the CE32s, but
            // doing so actually makes things slower.
        },
        variable_top,
    );

    compare!(
        /// Compare potentially ill-formed UTF-8 slices. Ill-formed input is compared
        /// as if errors had been replaced with REPLACEMENT CHARACTERs according
        /// to the WHATWG Encoding Standard.
        ,
        compare_utf8,
        [u8],
        [u8],
        split_prefix_u8,
        chars_with_trie_default_for_ascii,
        chars_with_trie_default_for_ascii,
        self,
        left_tail,
        right_tail,
        {
            // Direct copypaste from the `str` case.
            let tailoring_trie = &self.tailoring.trie;
            if let Some((left_c, left_u32)) = left_tail.chars_with_trie(tailoring_trie).next() {
                // Logically, there's a bunch of stuff we could do from the left side alone to determine
                // that reading from the right side at all or reading from the right trie is useless.
                // Doing that seems to be a pessimization, at least in the absence of PGO.
                if let Some((right_c, right_u32)) = right_tail.chars_with_trie(tailoring_trie).next() {
                    let left_ce32 = CollationElement32::new(left_u32);
                    let right_ce32 = CollationElement32::new(right_u32);
                    let mut left_primary = left_ce32.to_primary_in_quick_check(self.tailoring);
                    // If `left_primary` is now zero, we don't really need to extract `right_primary`,
                    // but let's avoid the branch on the likely path.
                    let mut right_primary = right_ce32.to_primary_in_quick_check(self.tailoring);
                    quick_primary_compare!(left_primary, right_primary, variable_top, self,);
                    // Try Hangul.
                    let right_hangul_offset = u32::from(right_c).wrapping_sub(HANGUL_S_BASE);
                    if right_hangul_offset < HANGUL_S_COUNT {
                        let left_hangul_offset = u32::from(left_c).wrapping_sub(HANGUL_S_BASE);
                        if left_hangul_offset < HANGUL_S_COUNT {
                            hangul_syllable_compare!(left_hangul_offset, right_hangul_offset,);
                        }
                    }
                }
            }
            // Note: It might look like a good idea to cache the CE32s, but
            // doing so actually makes things slower.
        },
        variable_top,
    );

    compare!(
        /// Compare potentially ill-formed UTF-16 slices. Unpaired surrogates
        /// are compared as if each one was a REPLACEMENT CHARACTER.
        ,
        compare_utf16,
        [u16],
        [u16],
        split_prefix_u16,
        chars_with_trie,
        chars_with_trie,
        self,
        left_tail,
        right_tail,
        {
            let tailoring_trie = &self.tailoring.trie;
            if let Some(left_u) = left_tail.first() {
                if let Some(right_u) = right_tail.first() {
                    let left_u16 = *left_u;
                    let right_u16 = *right_u;
                    let left_u32 = tailoring_trie.get16(left_u16);
                    let right_u32 = tailoring_trie.get16(right_u16);
                    let left_ce32 = CollationElement32::new(left_u32);
                    let right_ce32 = CollationElement32::new(right_u32);
                    let mut left_primary = left_ce32.to_primary_in_quick_check(self.tailoring);
                    // If `left_primary` is now zero, we don't really need to extract `right_primary`,
                    // but let's avoid the branch on the likely path.
                    let mut right_primary = right_ce32.to_primary_in_quick_check(self.tailoring);
                    quick_primary_compare!(left_primary, right_primary, variable_top, self,);
                    // Try Hangul.
                    let left_hangul_offset = u32::from(left_u16).wrapping_sub(HANGUL_S_BASE);
                    if left_hangul_offset < HANGUL_S_COUNT {
                        let right_hangul_offset = u32::from(right_u16).wrapping_sub(HANGUL_S_BASE);
                        if right_hangul_offset < HANGUL_S_COUNT {
                            hangul_syllable_compare!(left_hangul_offset, right_hangul_offset,);
                        }
                    }
                }
            }
            // Note: It might look like a good idea to cache the CE32s, but
            // doing so actually makes things slower.
        },
        variable_top,
    );

    compare!(
        /// Compare Latin1 slices.
        ///
        /// ✨ *Enabled with the `latin1` Cargo feature.*
        #[cfg(feature = "latin1")]
        ,
        compare_latin1,
        [u8],
        [u8],
        split_prefix_latin1,
        latin1_chars_with_trie,
        latin1_chars_with_trie,
        self,
        left_tail,
        right_tail,
        {
            let tailoring_trie = &self.tailoring.trie;
            if let Some(left_u) = left_tail.first() {
                if let Some(right_u) = right_tail.first() {
                    let left_u8 = *left_u;
                    let right_u8 = *right_u;
                    // The probability of getting a non-simple ce32 from
                    // the Latin1 part above ASCII is so high that let's
                    // only consider ASCII on the left for this fast path.

                    if let Ok(left_ascii) = Ascii::try_new(left_u8) {
                        let left_u32 = tailoring_trie.ascii(left_ascii);
                        let left_ce32 = CollationElement32::new(left_u32);
                        if let Ok(right_ascii) = Ascii::try_new(right_u8) {
                            let right_u32 = tailoring_trie.ascii(right_ascii);
                            let right_ce32 = CollationElement32::new(right_u32);
                            // Should we use script reordering to cater to reordering
                            // digits or punctuation relative to letters?
                            let left_primary = left_ce32.to_primary_simple();
                            // If `left_primary` is now zero, we don't really need to extract `right_primary`,
                            // but let's avoid the branch on the likely path.
                            let right_primary = right_ce32.to_primary_simple();
                            if (left_primary != right_primary)
                                && (left_primary != 0)
                                && (right_primary != 0)
                                && !(left_primary < variable_top && left_primary > MERGE_SEPARATOR_PRIMARY)
                                && !(right_primary < variable_top && right_primary > MERGE_SEPARATOR_PRIMARY)
                            {
                                if left_primary < right_primary {
                                    return Ordering::Less;
                                }
                                return Ordering::Greater;
                            }
                        }
                    }
                }
            }
            // Note: It might look like a good idea to cache the CE32s, but
            // doing so actually makes things slower.
        },
        variable_top,
    );

    compare!(
        /// Compare Latin1 slice with potentially ill-formed UTF-16
        /// slice.
        ///
        /// If you need to compare a potentially ill-formed UTF-16
        /// slice with a Latin1 slice, swap the arguments and
        /// call `reverse()` on the return value.
        ///
        /// ✨ *Enabled with the `latin1` Cargo feature.*
        #[cfg(feature = "latin1")]
        ,
        compare_latin1_utf16,
        [u8],
        [u16],
        split_prefix_latin1_utf16,
        latin1_chars_with_trie,
        chars_with_trie,
        self,
        left_tail,
        right_tail,
        {
            let tailoring_trie = &self.tailoring.trie;
            if let Some(left_u) = left_tail.first() {
                if let Some(right_u) = right_tail.first() {
                    let left_u8 = *left_u;
                    // The probability of getting a non-simple ce32 from
                    // the Latin1 part above ASCII is so high that let's
                    // only consider ASCII on the left for this fast path.

                    if let Ok(left_ascii) = Ascii::try_new(left_u8) {
                        // SAFETY: Invariant of `get7` checked above.
                        let left_u32 = tailoring_trie.ascii(left_ascii);
                        let left_ce32 = CollationElement32::new(left_u32);
                        let right_u16 = *right_u;
                        let right_u32 = tailoring_trie.get16(right_u16);
                        let right_ce32 = CollationElement32::new(right_u32);
                        let mut left_primary = left_ce32.to_primary_simple();
                        // If `left_primary` is now zero, we don't really need to
                        // extract `right_primary`, but let's avoid the branch on
                        // the likely path.

                        // Don't use the macro to micro-optimize away the long primary
                        // case for ASCII.
                        let mut right_primary = right_ce32.to_primary_in_quick_check(self.tailoring);
                        if (left_primary != right_primary)
                            && (left_primary != 0)
                            && (right_primary != 0)
                            && !(left_primary < variable_top && left_primary > MERGE_SEPARATOR_PRIMARY)
                            && !(right_primary < variable_top && right_primary > MERGE_SEPARATOR_PRIMARY)
                        {
                            if let Some(reordering) = &self.reordering {
                                left_primary = reordering.reorder(left_primary);
                                right_primary = reordering.reorder(right_primary);
                            }
                            if left_primary < right_primary {
                                return Ordering::Less;
                            }
                            return Ordering::Greater;
                        }
                    }
                }
            }
            // Note: It might look like a good idea to cache the CE32s, but
            // doing so actually makes things slower.
        },
        variable_top,
    );

    #[inline(always)]
    fn numeric_primary(&self) -> Option<u8> {
        if self.options.numeric() {
            Some(self.special_primaries.numeric_primary)
        } else {
            None
        }
    }

    #[inline(always)]
    fn variable_top(&self) -> u32 {
        if self.options.alternate_handling() == AlternateHandling::NonIgnorable {
            0
        } else {
            // +1 so that we can use "<" and primary ignorables test out early.
            self.special_primaries
                .last_primary_for_group(self.options.max_variable())
                + 1
        }
    }

    /// The implementation of the comparison operation.
    ///
    /// `head_chars` is an iterator _backward_ over the identical
    /// prefix and `left_chars` and `right_chars` are iterators
    /// _forward_ over the parts after the identical prefix.
    fn compare_impl<L, R, H, T>(
        &'data self,
        left_chars: L,
        right_chars: R,
        mut head_chars: H,
        variable_top: u32,
    ) -> Ordering
    where
        L: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32> + 'data,
        R: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32> + 'data,
        H: DoubleEndedIterator<Item = (char, u32)> + 'data,
        T: AbstractCodePointTrie<'data, u32> + 'data,
    {
        // Sadly, it looks like variable CEs and backward second level
        // require us to store the full 64-bit CEs instead of storing only
        // the NonPrimary part.
        //
        // TODO(#2008): Consider having two monomorphizations of this method:
        // one that can deal with variables shifted to quaternary and
        // backward second level and another that doesn't support that
        // and only stores `NonPrimary` in `left_ces` and `right_ces`
        // with double the number of stack allocated elements.

        // Note: These are used only after the identical prefix skipping,
        // but initializing these up here improves performance at the time
        // of writing. Presumably the source order affects the stack frame
        // layout.
        let mut left_ces: SmallVec<[CollationElement; CE_BUFFER_SIZE]> = SmallVec::new();
        let mut right_ces: SmallVec<[CollationElement; CE_BUFFER_SIZE]> = SmallVec::new();

        // The algorithm comes from CollationCompare::compareUpToQuaternary in ICU4C.

        let mut any_variable = false;

        let numeric_primary = self.numeric_primary();

        let mut left = collation_elements!(self, left_chars, numeric_primary);
        let mut right = collation_elements!(self, right_chars, numeric_primary);

        // Start identical prefix

        // The logic here to check whether the boundary found by skipping
        // the identical prefix is safe is complicated compared to the ICU4C
        // approach of having a set of characters that are unsafe as the character
        // immediately following the identical prefix. However, the approach here
        // avoids extra data, and working on the main data avoids the bug
        // possibility of data structures not being mutually consistent.

        // This code intentionally does not keep around the `CollationElement32`s
        // that have been read from the collation data tries, because keeping
        // them around turned out to be a pessimization: There would be added
        // branches on the hot path of the algorithm that maps characters to
        // collation elements, and the element size of the upcoming buffer
        // would grow.
        //
        // However, the values read from the normalization trie _are_ kept around,
        // since there is already a place where to put them.

        // This loop is only broken out of as goto forward.
        #[expect(clippy::never_loop)]
        'prefix: loop {
            if let Some((mut head_last_c, head_last_trie_val)) = head_chars.next_back() {
                let mut head_last = CharacterAndClassAndTrieValue::new_with_trie_val(
                    head_last_c,
                    head_last_trie_val,
                );
                let mut head_last_ce32 = CollationElement32::default();
                let mut head_last_ok = false;
                if let Some(left_different) = left.iter_next_before_init() {
                    left.prepend_upcoming_before_init(left_different.clone());
                    if let Some(right_different) = right.iter_next_before_init() {
                        // Note: left_different and right_different may both be U+FFFD.
                        right.prepend_upcoming_before_init(right_different.clone());

                        // The base logic is that a boundary between two starters
                        // that decompose to selves is safe iff the starter
                        // before the boundary can't contract a starter, the
                        // starter after the boundary doesn't have a prefix
                        // condition, and, with the numeric mode enabled,
                        // they aren't both numeric.
                        //
                        // This base logic is then extended with Hangul
                        // syllables and characters that decompose to a
                        // BMP starter followed by a BMP non-starter.
                        // The logic could be extended further, in
                        // particular to cover singleton decompositions
                        // to a BMP starter, but such characters can be
                        // expected to be rare enough in real-world input
                        // that it's not worthwhile to make this code more
                        // branchy.
                        //
                        // A Hangul syllable is safe on either side of the
                        // boundary, because Hangul syllables can't participate
                        // in contraction or have prefix conditions. They are
                        // also known not to be numeric.
                        //
                        // Hangul jamo is safe to look up from the main trie
                        // instead of the jamo table, because they aren't
                        // allowed to participate in contractions or prefix
                        // conditions, either, and are known not to be numeric.
                        //
                        // After a boundary, a decomposition to a BMP starter
                        // and a BMP non-starter can obviously be analyzed by
                        // considering the starter as if it was a starter
                        // that decomposes to self.
                        //
                        // Before a boundary the contraction condition considers
                        // whether the contraction can contract a starter.
                        // For the case of contracting a non-starter, it's
                        // fine for the BMP starter of the decomposition to
                        // contract the non-starter from the same decomposition:
                        // Since that would happen as part of the prefix that
                        // is identical, it wouldn't affect anything after.
                        //
                        // The case of contracting a non-starter other than
                        // the one that came from the decomposition itself
                        // is irrelevant, because we don't allow a non-starter
                        // right after the boundary regardless of the contraction
                        // status of what's before the boundary.
                        //
                        // Finally, decompositions to starter and non-starter
                        // are known not to be numeric.

                        // The checks below are repetitive, but an attempt to factor
                        // repetitive code into an inlined function regressed,
                        // performance, so it seems that having the control flow
                        // right here without an intermediate enum from a
                        // function return to branch on is important.

                        // This loop is only broken out of as goto forward. The control flow
                        // is much more readable this way.
                        #[expect(clippy::never_loop)]
                        loop {
                            // The two highest bits are about NFC, which we don't
                            // care about here.
                            let decomposition = head_last.trie_val;
                            if (decomposition
                                & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER))
                                == 0
                            {
                                // Intentionally empty block to keep
                                // the same structure as in the cases
                                // where something happens here.
                            } else if ((decomposition & HIGH_ZEROS_MASK) != 0)
                                && ((decomposition & LOW_ZEROS_MASK) != 0)
                            {
                                // Decomposition into two BMP characters: starter and non-starter
                                // Let's take the starter
                                head_last_c = char_from_u32(decomposition & 0x7FFF);
                            } else if decomposition == HANGUL_SYLLABLE_MARKER {
                                head_last_ce32 = IDENTICAL_PREFIX_HANGUL_MARKER_CE32;
                            } else {
                                break;
                            }
                            head_last_ok = true;

                            let left_c;
                            let right_c;

                            let decomposition = left_different.trie_val;
                            if (decomposition
                                & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER))
                                == 0
                            {
                                left_c = left_different.character();
                            } else if ((decomposition & HIGH_ZEROS_MASK) != 0)
                                && ((decomposition & LOW_ZEROS_MASK) != 0)
                            {
                                // Decomposition into two BMP characters: starter and non-starter
                                // Let's take the starter
                                left_c = char_from_u32(decomposition & 0x7FFF);
                            } else if decomposition == HANGUL_SYLLABLE_MARKER {
                                left_c = left_different.character();
                                if right_different.trie_val == HANGUL_SYLLABLE_MARKER {
                                    // Hangul syllable to Hangul syllable comparison can give us
                                    // a quick exit.
                                    let left_hangul_offset =
                                        u32::from(left_c).wrapping_sub(HANGUL_S_BASE);
                                    let right_hangul_offset =
                                        u32::from(right_different.character())
                                            .wrapping_sub(HANGUL_S_BASE);
                                    // If these are not in range, we have a memory-safe GIGO case.
                                    debug_assert!(left_hangul_offset < HANGUL_S_COUNT);
                                    debug_assert!(right_hangul_offset < HANGUL_S_COUNT);
                                    hangul_syllable_compare!(
                                        left_hangul_offset,
                                        right_hangul_offset,
                                    );
                                    // We had a two-jamo syllable whose jamo matched
                                    // the first two jamo of the other syllable.
                                    // Still, we're at a good boundary.
                                    break 'prefix;
                                }
                            } else {
                                break;
                            }

                            let decomposition = right_different.trie_val;
                            if (decomposition
                                & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER))
                                == 0
                            {
                                right_c = right_different.character();
                            } else if ((decomposition & HIGH_ZEROS_MASK) != 0)
                                && ((decomposition & LOW_ZEROS_MASK) != 0)
                            {
                                // Decomposition into two BMP characters: starter and non-starter
                                // Let's take the starter
                                right_c = char_from_u32(decomposition & 0x7FFF);
                            } else if decomposition == HANGUL_SYLLABLE_MARKER {
                                right_c = right_different.character();
                            } else {
                                break;
                            }

                            // The last character of the prefix is OK on the normalization
                            // level. Now let's check its ce32 unless it's a Hangul syllable,
                            // in which case `head_last_ce32` already is a non-default placeholder.
                            if head_last_ce32 == CollationElement32::default() {
                                head_last_ce32 = self.tailoring.ce32_for_char(head_last_c);
                                if head_last_ce32 == FALLBACK_CE32 {
                                    head_last_ce32 = self.root.ce32_for_char(head_last_c);
                                }
                                if head_last_ce32.tag_checked() == Some(Tag::Contraction)
                                    && head_last_ce32.at_least_one_suffix_contains_starter()
                                {
                                    break;
                                }
                            }
                            let mut left_data = self.tailoring;
                            let mut left_ce32 = self.tailoring.ce32_for_char(left_c);
                            if left_ce32 == FALLBACK_CE32 {
                                left_ce32 = self.root.ce32_for_char(left_c);
                                left_data = self.root;
                            }
                            let mut right_data = self.tailoring;
                            let mut right_ce32 = self.tailoring.ce32_for_char(right_c);
                            if right_ce32 == FALLBACK_CE32 {
                                right_ce32 = self.root.ce32_for_char(right_c);
                                right_data = self.root;
                            }

                            // We might be at at a good boundary unless either ce32 is a
                            // prefix ce32. Let's check for the happy path first, though.

                            // Now check if the ce32s we have are simple enough to
                            // make a quick decision here.
                            let mut left_primary = left_ce32.to_primary_in_quick_check_numeric(
                                left_data,
                                numeric_primary.is_some(),
                            );
                            // If `left_primary` is now zero, we don't really need
                            // to extract `right_primary`, but let's avoid the branch
                            // on the likely path.
                            let mut right_primary = right_ce32.to_primary_in_quick_check_numeric(
                                right_data,
                                numeric_primary.is_some(),
                            );
                            quick_primary_compare!(left_primary, right_primary, variable_top, self,);

                            if left_ce32.tag_checked() == Some(Tag::Prefix)
                                || right_ce32.tag_checked() == Some(Tag::Prefix)
                            {
                                // TODO: For the Japanese tailoring, it might actually be worthwhile
                                // to handle the prefix ce32s inline above the quick check. We've already
                                // read the last character from `head` anyway.
                                break;
                            }

                            if numeric_primary.is_some()
                                && head_last_ce32.tag_checked() == Some(Tag::Digit)
                                && ((left_ce32.tag_checked() == Some(Tag::Digit)
                                    && left_ce32.digit() == 0)
                                    || (right_ce32.tag_checked() == Some(Tag::Digit)
                                        && right_ce32.digit() == 0)
                                    || ((left_ce32.tag_checked() == Some(Tag::Digit))
                                        ^ (right_ce32.tag_checked() == Some(Tag::Digit))))
                            {
                                // Avoid giving the zero the leading zero treatment
                                // and also reject the case where only one of
                                // right and left is a digit.
                                break;
                            }

                            // We're at a good boundary but could not make a quick primary comparison decision.

                            // Note: It might look like a good idea to cache the CE32s, but
                            // doing so actually makes things slower.
                            break 'prefix;
                        }
                    }
                }
                let mut tail_first_c;
                let mut tail_first_ce32;
                let mut tail_first_ok;
                loop {
                    // Take a step back.
                    left.prepend_upcoming_before_init(head_last.clone());
                    right.prepend_upcoming_before_init(head_last.clone());

                    tail_first_c = head_last_c;
                    tail_first_ce32 = head_last_ce32;
                    tail_first_ok = head_last_ok;

                    let Some((head_last_c_new, decomposition)) = head_chars.next_back() else {
                        // We need to step back beyond the start of the prefix.
                        // Treat as good boundary.
                        break 'prefix;
                    };
                    head_last_c = head_last_c_new;
                    head_last = CharacterAndClassAndTrieValue::new_with_trie_val(
                        head_last_c,
                        decomposition,
                    );
                    head_last_ce32 = CollationElement32::default();
                    head_last_ok = false;

                    if (decomposition & !(BACKWARD_COMBINING_MARKER | NON_ROUND_TRIP_MARKER)) == 0 {
                        // Intentionally empty block to keep
                        // the same structure as in the cases
                        // where something happens here.
                    } else if ((decomposition & HIGH_ZEROS_MASK) != 0)
                        && ((decomposition & LOW_ZEROS_MASK) != 0)
                    {
                        // Decomposition into two BMP characters: starter and non-starter
                        // Let's take the starter
                        head_last_c = char_from_u32(decomposition & 0x7FFF);
                    } else if decomposition == HANGUL_SYLLABLE_MARKER {
                        head_last_ce32 = FFFD_CE32;
                    } else {
                        continue;
                    }
                    head_last_ok = true;
                    if !tail_first_ok {
                        continue;
                    }
                    // The last character of the prefix is OK on the normalization
                    // level. Now let's check its ce32 unless it's a Hangul syllable.
                    if head_last_ce32 == CollationElement32::default() {
                        head_last_ce32 = self.tailoring.ce32_for_char(head_last_c);
                        if head_last_ce32 == FALLBACK_CE32 {
                            head_last_ce32 = self.root.ce32_for_char(head_last_c);
                        }
                        if head_last_ce32.tag_checked() == Some(Tag::Contraction)
                            && head_last_ce32.at_least_one_suffix_contains_starter()
                        {
                            continue;
                        }
                    }
                    // Check this _after_ `head_last_ce32` to make sure
                    // `head_last_ce32` is initialized for the next loop round
                    // trip if applicable.
                    if tail_first_ce32 == CollationElement32::default() {
                        tail_first_ce32 = self.tailoring.ce32_for_char(tail_first_c);
                        if tail_first_ce32 == FALLBACK_CE32 {
                            tail_first_ce32 = self.root.ce32_for_char(tail_first_c);
                        }
                    } // else we already have a trie value from the previous loop iteration or we have Hangul syllable
                    if tail_first_ce32.tag_checked() == Some(Tag::Prefix) {
                        continue;
                    }
                    if numeric_primary.is_some()
                        && head_last_ce32.tag_checked() == Some(Tag::Digit)
                        && tail_first_ce32.tag_checked() == Some(Tag::Digit)
                        && tail_first_ce32.digit() == 0
                    {
                        // Avoid giving the zero the leading zero treatment.
                        continue;
                    }
                    // We are at a good boundary!
                    break 'prefix;
                }
            } else {
                // The prefix is empty
                break 'prefix;
            }
            // Unreachable line
        }

        // End identical prefix

        left.init();
        right.init();

        loop {
            let mut left_primary;
            'left_primary_loop: loop {
                let ce = left.next();
                left_primary = ce.primary();
                // TODO(#2008): Consider compiling out the variable handling when we know we aren't
                // shifting variable CEs.
                if !(left_primary < variable_top && left_primary > MERGE_SEPARATOR_PRIMARY) {
                    left_ces.push(ce);
                } else {
                    // Variable CE, shift it to quaternary level.
                    // Ignore all following primary ignorables, and shift further variable CEs.
                    any_variable = true;
                    // Relative to ICU4C, the next line is hoisted out of the following loop
                    // in order to keep the variables called `ce` immutable to make it easier
                    // to reason about each assignment into `ce` resulting in exactly a single
                    // push into `left_ces`.
                    left_ces.push(ce.clone_with_non_primary_zeroed());
                    loop {
                        // This loop is simpler than in ICU4C; unlike in C++, we get to break by label.
                        let ce = left.next();
                        left_primary = ce.primary();
                        if left_primary != 0
                            && !(left_primary < variable_top
                                && left_primary > MERGE_SEPARATOR_PRIMARY)
                        {
                            // Neither a primary ignorable nor a variable CE.
                            left_ces.push(ce);
                            break 'left_primary_loop;
                        }
                        // If `left_primary == 0`, the following line ignores a primary-ignorable.
                        // Otherwise, it shifts a variable CE.
                        left_ces.push(ce.clone_with_non_primary_zeroed());
                    }
                }
                if left_primary != 0 {
                    break;
                }
            }
            let mut right_primary;
            'right_primary_loop: loop {
                let ce = right.next();
                right_primary = ce.primary();
                // TODO(#2008): Consider compiling out the variable handling when we know we aren't
                // shifting variable CEs.
                if !(right_primary < variable_top && right_primary > MERGE_SEPARATOR_PRIMARY) {
                    right_ces.push(ce);
                } else {
                    // Variable CE, shift it to quaternary level.
                    // Ignore all following primary ignorables, and shift further variable CEs.
                    any_variable = true;
                    // Relative to ICU4C, the next line is hoisted out of the following loop
                    // in order to keep the variables called `ce` immutable to make it easier
                    // to reason about each assignment into `ce` resulting in exactly a single
                    // push into `right_ces`.
                    right_ces.push(ce.clone_with_non_primary_zeroed());
                    loop {
                        // This loop is simpler than in ICU4C; unlike in C++, we get to break by label.
                        let ce = right.next();
                        right_primary = ce.primary();
                        if right_primary != 0
                            && !(right_primary < variable_top
                                && right_primary > MERGE_SEPARATOR_PRIMARY)
                        {
                            // Neither a primary ignorable nor a variable CE.
                            right_ces.push(ce);
                            break 'right_primary_loop;
                        }
                        // If `right_primary == 0`, the following line ignores a primary-ignorable.
                        // Otherwise, it shifts a variable CE.
                        right_ces.push(ce.clone_with_non_primary_zeroed());
                    }
                }
                if right_primary != 0 {
                    break;
                }
            }
            if left_primary != right_primary {
                if let Some(reordering) = &self.reordering {
                    left_primary = reordering.reorder(left_primary);
                    right_primary = reordering.reorder(right_primary);
                }
                if left_primary < right_primary {
                    return Ordering::Less;
                }
                return Ordering::Greater;
            }
            if left_primary == NO_CE_PRIMARY {
                break;
            }
        }

        // Sadly, we end up pushing the sentinel value, which means these
        // `SmallVec`s allocate more often than if we didn't actually
        // store the sentinel.
        debug_assert_eq!(left_ces.last(), Some(&NO_CE));
        debug_assert_eq!(right_ces.last(), Some(&NO_CE));

        // Note: `unwrap_or_default` in the iterations below should never
        // actually end up using the "_or_default" part, because the sentinel
        // is in the `SmallVec`s. These could be changed to `unwrap()` if we
        // preferred panic in case of a bug.
        // TODO(#2009): Should we save one slot by not putting the sentinel in
        // the `SmallVec`s? So far, the answer seems "no", as it would complicate
        // the primary comparison above.

        // Compare the buffered secondary & tertiary weights.
        // We might skip the secondary level but continue with the case level
        // which is turned on separately.
        if self.options.strength() >= Strength::Secondary {
            if !self.options.backward_second_level() {
                let mut left_iter = left_ces.iter();
                let mut right_iter = right_ces.iter();
                let mut left_secondary;
                let mut right_secondary;
                loop {
                    loop {
                        left_secondary = left_iter.next().unwrap_or_default().secondary();
                        if left_secondary != 0 {
                            break;
                        }
                    }
                    loop {
                        right_secondary = right_iter.next().unwrap_or_default().secondary();
                        if right_secondary != 0 {
                            break;
                        }
                    }
                    if left_secondary != right_secondary {
                        if left_secondary < right_secondary {
                            return Ordering::Less;
                        }
                        return Ordering::Greater;
                    }
                    if left_secondary == NO_CE_SECONDARY {
                        break;
                    }
                }
            } else {
                let mut left_remaining = &left_ces[..];
                let mut right_remaining = &right_ces[..];
                loop {
                    if left_remaining.is_empty() {
                        debug_assert!(right_remaining.is_empty());
                        break;
                    }
                    let (left_prefix, right_prefix) = {
                        let mut left_iter = left_remaining.iter();
                        loop {
                            let left_primary = left_iter.next().unwrap_or_default().primary();
                            if left_primary != 0 && left_primary <= MERGE_SEPARATOR_PRIMARY {
                                break;
                            }
                            debug_assert_ne!(left_primary, NO_CE_PRIMARY);
                        }
                        let left_new_remaining = left_iter.as_slice();
                        // Index in range by construction
                        #[expect(clippy::indexing_slicing)]
                        let left_prefix =
                            &left_remaining[..left_remaining.len() - 1 - left_new_remaining.len()];
                        left_remaining = left_new_remaining;

                        let mut right_iter = right_remaining.iter();
                        loop {
                            let right_primary = right_iter.next().unwrap_or_default().primary();
                            if right_primary != 0 && right_primary <= MERGE_SEPARATOR_PRIMARY {
                                break;
                            }
                            debug_assert_ne!(right_primary, NO_CE_PRIMARY);
                        }
                        let right_new_remaining = right_iter.as_slice();
                        // Index in range by construction
                        #[expect(clippy::indexing_slicing)]
                        let right_prefix = &right_remaining
                            [..right_remaining.len() - 1 - right_new_remaining.len()];
                        right_remaining = right_new_remaining;

                        (left_prefix, right_prefix)
                    };
                    let mut left_iter = left_prefix.iter();
                    let mut right_iter = right_prefix.iter();

                    let mut left_secondary;
                    let mut right_secondary;
                    loop {
                        loop {
                            left_secondary = left_iter.next_back().unwrap_or_default().secondary();
                            if left_secondary != 0 {
                                break;
                            }
                        }
                        loop {
                            right_secondary =
                                right_iter.next_back().unwrap_or_default().secondary();
                            if right_secondary != 0 {
                                break;
                            }
                        }
                        if left_secondary != right_secondary {
                            if left_secondary < right_secondary {
                                return Ordering::Less;
                            }
                            return Ordering::Greater;
                        }
                        if left_secondary == NO_CE_SECONDARY {
                            break;
                        }
                    }
                }
            }
        }

        if self.options.case_level() {
            let mut left_non_primary;
            let mut right_non_primary;
            let mut left_case;
            let mut right_case;
            let mut left_iter = left_ces.iter();
            let mut right_iter = right_ces.iter();
            if self.options.strength() == Strength::Primary {
                // Primary+caseLevel: Ignore case level weights of primary ignorables.
                // Otherwise we would get a-umlaut > a
                // which is not desirable for accent-insensitive sorting.
                // Check for (lower 32 bits) == 0 as well because variable CEs are stored
                // with only primary weights.
                loop {
                    loop {
                        let ce = left_iter.next().unwrap_or_default();
                        left_non_primary = ce.non_primary();
                        if !ce.either_half_zero() {
                            break;
                        }
                    }
                    left_case = left_non_primary.case();
                    loop {
                        let ce = right_iter.next().unwrap_or_default();
                        right_non_primary = ce.non_primary();
                        if !ce.either_half_zero() {
                            break;
                        }
                    }
                    right_case = right_non_primary.case();
                    // No need to handle NO_CE and MERGE_SEPARATOR specially:
                    // There is one case weight for each previous-level weight,
                    // so level length differences were handled there.
                    if left_case != right_case {
                        if !self.options.upper_first() {
                            if left_case < right_case {
                                return Ordering::Less;
                            }
                            return Ordering::Greater;
                        }
                        if left_case < right_case {
                            return Ordering::Greater;
                        }
                        return Ordering::Less;
                    }
                    if left_non_primary.secondary() == NO_CE_SECONDARY {
                        break;
                    }
                }
            } else {
                // Secondary+caseLevel: By analogy with the above,
                // ignore case level weights of secondary ignorables.
                //
                // Note: A tertiary CE has uppercase case bits (0.0.ut)
                // to keep tertiary+caseFirst well-formed.
                //
                // Tertiary+caseLevel: Also ignore case level weights of secondary ignorables.
                // Otherwise a tertiary CE's uppercase would be no greater than
                // a primary/secondary CE's uppercase.
                // (See UCA well-formedness condition 2.)
                // We could construct a special case weight higher than uppercase,
                // but it's simpler to always ignore case weights of secondary ignorables,
                // turning 0.0.ut into 0.0.0.t.
                // (See LDML Collation, Case Parameters.)
                loop {
                    loop {
                        left_non_primary = left_iter.next().unwrap_or_default().non_primary();
                        if left_non_primary.secondary() != 0 {
                            break;
                        }
                    }
                    left_case = left_non_primary.case();
                    loop {
                        right_non_primary = right_iter.next().unwrap_or_default().non_primary();
                        if right_non_primary.secondary() != 0 {
                            break;
                        }
                    }
                    right_case = right_non_primary.case();
                    // No need to handle NO_CE and MERGE_SEPARATOR specially:
                    // There is one case weight for each previous-level weight,
                    // so level length differences were handled there.
                    if left_case != right_case {
                        if !self.options.upper_first() {
                            if left_case < right_case {
                                return Ordering::Less;
                            }
                            return Ordering::Greater;
                        }
                        if left_case < right_case {
                            return Ordering::Greater;
                        }
                        return Ordering::Less;
                    }
                    if left_non_primary.secondary() == NO_CE_SECONDARY {
                        break;
                    }
                }
            }
        }

        if let Some(tertiary_mask) = self.options.tertiary_mask() {
            let mut any_quaternaries = AnyQuaternaryAccumulator::new();
            let mut left_iter = left_ces.iter();
            let mut right_iter = right_ces.iter();
            loop {
                let mut left_non_primary;
                let mut left_tertiary;
                loop {
                    left_non_primary = left_iter.next().unwrap_or_default().non_primary();
                    any_quaternaries.accumulate(left_non_primary);
                    debug_assert!(
                        left_non_primary.tertiary() != 0 || left_non_primary.case_quaternary() == 0
                    );
                    left_tertiary = left_non_primary.tertiary_case_quarternary(tertiary_mask);
                    if left_tertiary != 0 {
                        break;
                    }
                }

                let mut right_non_primary;
                let mut right_tertiary;
                loop {
                    right_non_primary = right_iter.next().unwrap_or_default().non_primary();
                    any_quaternaries.accumulate(right_non_primary);
                    debug_assert!(
                        right_non_primary.tertiary() != 0
                            || right_non_primary.case_quaternary() == 0
                    );
                    right_tertiary = right_non_primary.tertiary_case_quarternary(tertiary_mask);
                    if right_tertiary != 0 {
                        break;
                    }
                }

                if left_tertiary != right_tertiary {
                    if self.options.upper_first() {
                        // Pass through NO_CE and keep real tertiary weights larger than that.
                        // Do not change the artificial uppercase weight of a tertiary CE (0.0.ut),
                        // to keep tertiary CEs well-formed.
                        // Their case+tertiary weights must be greater than those of
                        // primary and secondary CEs.
                        // Magic numbers from ICU4C.
                        if left_tertiary > NO_CE_TERTIARY {
                            if left_non_primary.secondary() != 0 {
                                left_tertiary ^= 0xC000;
                            } else {
                                left_tertiary += 0x4000;
                            }
                        }
                        if right_tertiary > NO_CE_TERTIARY {
                            if right_non_primary.secondary() != 0 {
                                right_tertiary ^= 0xC000;
                            } else {
                                right_tertiary += 0x4000;
                            }
                        }
                    }
                    if left_tertiary < right_tertiary {
                        return Ordering::Less;
                    }
                    return Ordering::Greater;
                }

                if left_tertiary == NO_CE_TERTIARY {
                    break;
                }
            }
            if !any_variable && !any_quaternaries.has_quaternary() {
                return Ordering::Equal;
            }
        } else {
            return Ordering::Equal;
        }

        if self.options.strength() <= Strength::Tertiary {
            return Ordering::Equal;
        }

        let mut left_iter = left_ces.iter();
        let mut right_iter = right_ces.iter();
        loop {
            let mut left_quaternary;
            loop {
                let ce = left_iter.next().unwrap_or_default();
                if ce.tertiary_ignorable() {
                    left_quaternary = ce.primary();
                } else {
                    left_quaternary = ce.quaternary();
                }
                if left_quaternary != 0 {
                    break;
                }
            }
            let mut right_quaternary;
            loop {
                let ce = right_iter.next().unwrap_or_default();
                if ce.tertiary_ignorable() {
                    right_quaternary = ce.primary();
                } else {
                    right_quaternary = ce.quaternary();
                }
                if right_quaternary != 0 {
                    break;
                }
            }
            if left_quaternary != right_quaternary {
                if let Some(reordering) = &self.reordering {
                    left_quaternary = reordering.reorder(left_quaternary);
                    right_quaternary = reordering.reorder(right_quaternary);
                }
                if left_quaternary < right_quaternary {
                    return Ordering::Less;
                }
                return Ordering::Greater;
            }
            if left_quaternary == NO_CE_PRIMARY {
                break;
            }
        }

        Ordering::Equal
    }

    fn sort_key_levels(&self) -> u8 {
        #[expect(clippy::indexing_slicing)]
        let mut levels = LEVEL_MASKS[self.options.strength() as usize];
        if self.options.case_level() {
            levels |= CASE_LEVEL_FLAG;
        }
        levels
    }

    /// Given valid UTF-8, write the sort key bytes up to the collator's strength.
    ///
    /// The bytes are written to an implementor of [`CollationKeySink`], a no-std version of `std::io::Write`.
    /// This trait is currently unstable, but it is implemented by `Vec<u8>`, `VecDeque<u8>`,
    /// `SmallVec<[u8; N]>`, and `&mut [u8]` (returning an error if the slice is too small).
    ///
    /// If two sort keys generated at the same strength are compared bytewise, the result is
    /// the same as a collation comparison of the original strings at that strength.
    ///
    /// For identical strength, the UTF-8 NFD normalization is appended for breaking ties.
    ///
    /// No terminating zero byte is written to the output, so the output is not a valid C
    /// string, but the caller may append a zero afterward if a C string is desired.
    ///
    /// ⚠️ Generating a sort key is expensive relative to comparison because to compare, the
    /// collator skips identical prefixes before doing more complex comparison.  Only use sort
    /// keys if you expect to compare them many times so as to amortize the cost of generating
    /// them.  Measurement of this performance trade-off would be a good idea.
    ///
    /// ⚠️ Sort keys, if stored durably, should be presumed to be invalidated by a CLDR update, a
    /// new version of Unicode, or an update to the ICU4X code.  Applications using sort keys
    /// *must* be prepared to recompute them if required and should take the performance of
    /// such an operation into account when deciding to use sort keys.
    ///
    /// ⚠️ If you should store sort keys in a database that is or becomes so large that
    /// regenerating sort keys becomes impractical, you should not expect ICU4X to support your
    /// using an older, frozen copy of the sort key generation algorithm with a later version
    /// of the library.
    ///
    /// # Example
    ///
    /// ```
    /// use icu_collator::{
    ///     options::{CollatorOptions, Strength},
    ///     Collator,
    /// };
    /// use icu_locale::locale;
    /// let locale = locale!("utf").into();
    /// let mut options = CollatorOptions::default();
    /// options.strength = Some(Strength::Primary);
    /// let collator = Collator::try_new(locale, options).unwrap();
    ///
    /// let mut k1 = Vec::new();
    /// let Ok(()) = collator.write_sort_key_to("hello", &mut k1);
    /// let mut k2 = Vec::new();
    /// let Ok(()) = collator.write_sort_key_to("Héłłö", &mut k2);
    /// assert_eq!(k1, k2);
    /// ```
    pub fn write_sort_key_to<S>(&self, s: &str, sink: &mut S) -> Result<S::Output, S::Error>
    where
        S: CollationKeySink + ?Sized,
        S::State: Default,
    {
        self.write_sort_key_impl(s.chars_with_trie_default_for_ascii(self.norm_trie()), sink)
    }

    /// Given potentially invalid UTF-8, write the sort key bytes up to the collator's strength.
    ///
    /// For further details, see [`Self::write_sort_key_to`].
    pub fn write_sort_key_utf8_to<S>(&self, s: &[u8], sink: &mut S) -> Result<S::Output, S::Error>
    where
        S: CollationKeySink + ?Sized,
        S::State: Default,
    {
        self.write_sort_key_impl(s.chars_with_trie_default_for_ascii(self.norm_trie()), sink)
    }

    /// Given potentially invalid UTF-16, write the sort key bytes up to the collator's strength.
    ///
    /// For further details, see [`Self::write_sort_key_to`].
    pub fn write_sort_key_utf16_to<S>(&self, s: &[u16], sink: &mut S) -> Result<S::Output, S::Error>
    where
        S: CollationKeySink + ?Sized,
        S::State: Default,
    {
        self.write_sort_key_impl(s.chars_with_trie(self.norm_trie()), sink)
    }

    fn write_sort_key_impl<I, T, S>(
        &'data self,
        iter: I,
        sink: &mut S,
    ) -> Result<S::Output, S::Error>
    where
        I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32> + Clone + 'data,
        T: AbstractCodePointTrie<'data, u32> + 'data,
        S: CollationKeySink + ?Sized,
        S::State: Default,
    {
        let identical = if self.options.strength() == Strength::Identical {
            Some(iter.clone())
        } else {
            None
        };

        let mut state = S::State::default();
        self.write_sort_key_up_to_quaternary(iter, sink, &mut state)?;

        if let Some(iter) = identical {
            sink.write_byte(&mut state, LEVEL_SEPARATOR_BYTE)?;

            let mut iter = icu_normalizer::new_decomposition(iter, self.tables);
            let _ = iter.next(); // Discard the U+0000.
            write_identical_level(iter, sink, &mut state)?;
        }

        sink.finish(state)
    }

    /// Write the sort key bytes up to the collator's strength.
    ///
    /// Optionally write the case level.  Separate levels with the `LEVEL_SEPARATOR_BYTE`, but
    /// do not write a terminating zero as with a C string.
    fn write_sort_key_up_to_quaternary<I, S, T>(
        &'data self,
        iter: I,
        sink: &mut S,
        state: &mut S::State,
    ) -> Result<(), S::Error>
    where
        I: Iterator<Item = (char, u32)> + WithTrie<'data, T, u32> + Clone + 'data,
        T: AbstractCodePointTrie<'data, u32> + 'data,
        S: CollationKeySink + ?Sized,
    {
        // This algorithm comes from `CollationKeys::writeSortKeyUpToQuaternary` in ICU4C.
        let levels = self.sort_key_levels();

        let mut iter = collation_elements!(self, iter, self.numeric_primary());
        iter.init();
        let variable_top = self.variable_top();

        let tertiary_mask = self.options.tertiary_mask().unwrap_or_default();

        let mut cases = SortKeyLevel::default();
        let mut secondaries = SortKeyLevel::default();
        let mut tertiaries = SortKeyLevel::default();
        let mut quaternaries = SortKeyLevel::default();

        let mut prev_reordered_primary = 0;
        let mut common_cases = 0usize;
        let mut common_secondaries = 0usize;
        let mut common_tertiaries = 0usize;
        let mut common_quaternaries = 0usize;
        let mut prev_secondary = 0;
        let mut sec_segment_start = 0;

        loop {
            let mut ce = iter.next();
            let mut p = ce.primary();
            if p < variable_top && p > MERGE_SEPARATOR_PRIMARY {
                // Variable CE, shift it to quaternary level.  Ignore all following primary
                // ignorables, and shift further variable CEs.
                if common_quaternaries != 0 {
                    common_quaternaries -= 1;
                    while common_quaternaries >= QUAT_COMMON[WEIGHT_MAX_COUNT] as _ {
                        quaternaries.append_byte(QUAT_COMMON[WEIGHT_MIDDLE]);
                        common_quaternaries -= QUAT_COMMON[WEIGHT_MAX_COUNT] as usize;
                    }
                    // Shifted primary weights are lower than the common weight.
                    quaternaries.append_byte(QUAT_COMMON[WEIGHT_LOW] + common_quaternaries as u8);
                    common_quaternaries = 0;
                }

                loop {
                    if levels & QUATERNARY_LEVEL_FLAG != 0 {
                        if let Some(reordering) = &self.reordering {
                            p = reordering.reorder(p);
                        }
                        if (p >> 24) as u8 >= QUAT_SHIFTED_LIMIT_BYTE {
                            // Prevent shifted primary lead bytes from overlapping with the
                            // common compression range.
                            quaternaries.append_byte(QUAT_SHIFTED_LIMIT_BYTE);
                        }
                        quaternaries.append_weight_32(p);
                    }
                    loop {
                        ce = iter.next();
                        p = ce.primary();
                        if p != 0 {
                            break;
                        }
                    }
                    if !(p < variable_top && p > MERGE_SEPARATOR_PRIMARY) {
                        break;
                    }
                }
            }

            // ce could be primary ignorable, or NO_CE, or the merge separator, or a regular
            // primary CE, but it is not variable.  If ce == NO_CE, then write nothing for the
            // primary level but terminate compression on all levels and then exit the loop.
            if p > NO_CE_PRIMARY && levels & PRIMARY_LEVEL_FLAG != 0 {
                // Test the un-reordered primary for compressibility.
                let is_compressible = self.special_primaries.is_compressible((p >> 24) as _);
                if let Some(reordering) = &self.reordering {
                    p = reordering.reorder(p);
                }
                let p1 = (p >> 24) as u8;
                if !is_compressible || p1 != (prev_reordered_primary >> 24) as u8 {
                    if prev_reordered_primary != 0 {
                        if p < prev_reordered_primary {
                            // No primary compression terminator at the end of the level or
                            // merged segment.
                            if p1 > MERGE_SEPARATOR_BYTE {
                                sink.write(state, &[PRIMARY_COMPRESSION_LOW_BYTE])?;
                            }
                        } else {
                            sink.write(state, &[PRIMARY_COMPRESSION_HIGH_BYTE])?;
                        }
                    }
                    sink.write_byte(state, p1)?;
                    prev_reordered_primary = if is_compressible { p } else { 0 };
                }

                let p2 = (p >> 16) as u8;
                if p2 != 0 {
                    let (b0, b1, b2) = (p2, (p >> 8) as _, p as _);
                    sink.write_byte(state, b0)?;
                    if b1 != 0 {
                        sink.write_byte(state, b1)?;
                        if b2 != 0 {
                            sink.write_byte(state, b2)?;
                        }
                    }
                }
            }

            let non_primary = ce.non_primary();
            if non_primary.ignorable() {
                continue; // completely ignorable, no secondary/case/tertiary/quaternary
            }

            macro_rules! handle_common {
                ($key:ident, $w:ident, $common:ident, $weights:ident, $lim:expr) => {
                    if $common != 0 {
                        $common -= 1;
                        while $common >= $weights[WEIGHT_MAX_COUNT] as _ {
                            $key.append_byte($weights[WEIGHT_MIDDLE]);
                            $common -= $weights[WEIGHT_MAX_COUNT] as usize;
                        }
                        let b = if $w < $lim {
                            $weights[WEIGHT_LOW] + ($common as u8)
                        } else {
                            $weights[WEIGHT_HIGH] - ($common as u8)
                        };
                        $key.append_byte(b);
                        $common = 0;
                    }
                };
                ($key:ident, $w:ident, $common:ident, $weights:ident) => {
                    handle_common!($key, $w, $common, $weights, COMMON_WEIGHT16);
                };
            }

            if levels & SECONDARY_LEVEL_FLAG != 0 {
                let s = non_primary.secondary();
                if s == 0 {
                    // secondary ignorable
                } else if s == COMMON_WEIGHT16
                    && (!self.options.backward_second_level() || p != MERGE_SEPARATOR_PRIMARY)
                {
                    // s is a common secondary weight, and backwards-secondary is off or the ce
                    // is not the merge separator.
                    common_secondaries += 1;
                } else if !self.options.backward_second_level() {
                    handle_common!(secondaries, s, common_secondaries, SEC_COMMON);
                    secondaries.append_weight_16(s);
                } else {
                    if common_secondaries != 0 {
                        common_secondaries -= 1;
                        // Append reverse weights.  The level will be re-reversed later.
                        let remainder = common_secondaries % SEC_COMMON[WEIGHT_MAX_COUNT] as usize;
                        let b = if prev_secondary < COMMON_WEIGHT16 {
                            SEC_COMMON[WEIGHT_LOW] + remainder as u8
                        } else {
                            SEC_COMMON[WEIGHT_HIGH] - remainder as u8
                        };
                        secondaries.append_byte(b);
                        common_secondaries -= remainder;
                        // common_secondaries is now a multiple of SEC_COMMON[WEIGHT_MAX_COUNT]
                        while common_secondaries > 0 {
                            // same as >= SEC_COMMON[WEIGHT_MAX_COUNT]
                            secondaries.append_byte(SEC_COMMON[WEIGHT_MIDDLE]);
                            common_secondaries -= SEC_COMMON[WEIGHT_MAX_COUNT] as usize;
                        }
                        // commonSecondaries == 0
                    }
                    if 0 < p && p <= MERGE_SEPARATOR_PRIMARY {
                        // The backwards secondary level compares secondary weights backwards
                        // within segments separated by the merge separator (U+FFFE).
                        let secs = &mut secondaries.buf;
                        let last = secs.len() - 1;
                        if sec_segment_start < last {
                            let mut q = sec_segment_start;
                            let mut r = last;

                            // these indices start at valid values and we stop when they cross
                            #[expect(clippy::indexing_slicing)]
                            while q < r {
                                let b = secs[q];
                                secs[q] = secs[r];
                                q += 1;
                                secs[r] = b;
                                r -= 1;
                            }
                        }
                        let b = if p == NO_CE_PRIMARY {
                            LEVEL_SEPARATOR_BYTE
                        } else {
                            MERGE_SEPARATOR_BYTE
                        };
                        secondaries.append_byte(b);
                        prev_secondary = 0;
                        sec_segment_start = secondaries.len();
                    } else {
                        secondaries.append_reverse_weight_16(s);
                        prev_secondary = s;
                    }
                }
            }

            if levels & CASE_LEVEL_FLAG != 0 {
                if self.options.strength() == Strength::Primary && p == 0
                    || non_primary.bits() <= 0xffff
                {
                    // Primary+caseLevel: Ignore case level weights of primary ignorables.
                    // Otherwise: Ignore case level weights of secondary ignorables.  For
                    // details see the comments in the CollationCompare class.
                } else {
                    // case bits & tertiary lead byte
                    let mut c = ((non_primary.bits() >> 8) & 0xff) as u8;
                    debug_assert_ne!(c & 0xc0, 0xc0);
                    if c & 0xc0 == 0 && c > LEVEL_SEPARATOR_BYTE {
                        common_cases += 1;
                    } else {
                        if !self.options.upper_first() {
                            // lower first:  Compress common weights to nibbles 1..7..13,
                            // mixed=14, upper=15.  If there are only common (=lowest) weights
                            // in the whole level, then we need not write anything.  Level
                            // length differences are handled already on the next-higher level.
                            if common_cases != 0 && (c > LEVEL_SEPARATOR_BYTE || !cases.is_empty())
                            {
                                common_cases -= 1;
                                while common_cases >= CASE_LOWER_FIRST_COMMON[WEIGHT_MAX_COUNT] as _
                                {
                                    cases.append_byte(CASE_LOWER_FIRST_COMMON[WEIGHT_MIDDLE] << 4);
                                    common_cases -=
                                        CASE_LOWER_FIRST_COMMON[WEIGHT_MAX_COUNT] as usize;
                                }
                                let b = if c <= LEVEL_SEPARATOR_BYTE {
                                    CASE_LOWER_FIRST_COMMON[WEIGHT_LOW] + common_cases as u8
                                } else {
                                    CASE_LOWER_FIRST_COMMON[WEIGHT_HIGH] - common_cases as u8
                                };
                                cases.append_byte(b << 4);
                                common_cases = 0;
                            }
                            if c > LEVEL_SEPARATOR_BYTE {
                                // 14 or 15
                                c = (CASE_LOWER_FIRST_COMMON[WEIGHT_HIGH] + (c >> 6)) << 4;
                            }
                        } else {
                            // upper first:  Compress common weights to nibbles 3..15, mixed=2,
                            // upper=1.  The compressed common case weights only go up from the
                            // "low" value because with upperFirst the common weight is the
                            // highest one.
                            if common_cases != 0 {
                                common_cases -= 1;
                                while common_cases >= CASE_UPPER_FIRST_COMMON[WEIGHT_MAX_COUNT] as _
                                {
                                    cases.append_byte(CASE_UPPER_FIRST_COMMON[WEIGHT_LOW] << 4);
                                    common_cases -=
                                        CASE_UPPER_FIRST_COMMON[WEIGHT_MAX_COUNT] as usize;
                                }
                                cases.append_byte(
                                    (CASE_UPPER_FIRST_COMMON[WEIGHT_LOW] + common_cases as u8) << 4,
                                );
                                common_cases = 0;
                            }
                            if c > LEVEL_SEPARATOR_BYTE {
                                // 2 or 1
                                c = (CASE_UPPER_FIRST_COMMON[WEIGHT_LOW] - (c >> 6)) << 4;
                            }
                        }
                        // c is a separator byte 01 or a left-shifted nibble 0x10, 0x20, ...
                        // 0xf0.
                        cases.append_byte(c);
                    }
                }
            }

            if levels & TERTIARY_LEVEL_FLAG != 0 {
                let mut t = non_primary.tertiary_case_quarternary(tertiary_mask);
                debug_assert_ne!(non_primary.bits() & 0xc000, 0xc000);
                if t == COMMON_WEIGHT16 {
                    common_tertiaries += 1;
                } else if tertiary_mask & 0x8000 == 0 {
                    // Tertiary weights without case bits.  Move lead bytes 06..3F to C6..FF
                    // for a large common-weight range.
                    handle_common!(tertiaries, t, common_tertiaries, TER_ONLY_COMMON);
                    if t > COMMON_WEIGHT16 {
                        t += 0xc000;
                    }
                    tertiaries.append_weight_16(t);
                } else if !self.options.upper_first() {
                    // Tertiary weights with caseFirst=lowerFirst.  Move lead bytes 06..BF to
                    // 46..FF for the common-weight range.
                    handle_common!(tertiaries, t, common_tertiaries, TER_LOWER_FIRST_COMMON);
                    if t > COMMON_WEIGHT16 {
                        t += 0x4000;
                    }
                    tertiaries.append_weight_16(t);
                } else {
                    // Tertiary weights with caseFirst=upperFirst.  Do not change the
                    // artificial uppercase weight of a tertiary CE (0.0.ut), to keep tertiary
                    // CEs well-formed.  Their case+tertiary weights must be greater than those
                    // of primary and secondary CEs.
                    //
                    // Separator         01 -> 01      (unchanged)
                    // Lowercase     02..04 -> 82..84  (includes uncased)
                    // Common weight     05 -> 85..C5  (common-weight compression range)
                    // Lowercase     06..3F -> C6..FF
                    // Mixed case    42..7F -> 42..7F
                    // Uppercase     82..BF -> 02..3F
                    // Tertiary CE   86..BF -> C6..FF
                    if t <= NO_CE_TERTIARY {
                        // Keep separators unchanged.
                    } else if non_primary.bits() > 0xffff {
                        // Invert case bits of primary & secondary CEs.
                        t ^= 0xc000;
                        if t < (TER_UPPER_FIRST_COMMON[WEIGHT_HIGH] as u16) << 8 {
                            t -= 0x4000;
                        }
                    } else {
                        // Keep uppercase bits of tertiary CEs.
                        debug_assert!((0x8600..=0xbfff).contains(&t));
                        t += 0x4000;
                    }
                    handle_common!(
                        tertiaries,
                        t,
                        common_tertiaries,
                        TER_UPPER_FIRST_COMMON,
                        (TER_UPPER_FIRST_COMMON[WEIGHT_LOW] as u16) << 8
                    );
                    tertiaries.append_weight_16(t);
                }
            }

            if levels & QUATERNARY_LEVEL_FLAG != 0 {
                let q = (non_primary.bits() & 0xffff) as u16;
                if q & 0xc0 == 0 && q > NO_CE_QUATERNARY {
                    common_quaternaries += 1;
                } else if q == NO_CE_QUATERNARY
                    && self.options.alternate_handling() == AlternateHandling::NonIgnorable
                    && quaternaries.is_empty()
                {
                    // If alternate=non-ignorable and there are only common quaternary weights,
                    // then we need not write anything.  The only weights greater than the
                    // merge separator and less than the common weight are shifted primary
                    // weights, which are not generated for alternate=non-ignorable.  There are
                    // also exactly as many quaternary weights as tertiary weights, so level
                    // length differences are handled already on tertiary level.  Any
                    // above-common quaternary weight will compare greater regardless.
                    quaternaries.append_byte(LEVEL_SEPARATOR_BYTE);
                } else {
                    let q = if q == NO_CE_QUATERNARY {
                        LEVEL_SEPARATOR_BYTE
                    } else {
                        (0xfc + ((q >> 6) & 3)) as u8
                    };
                    handle_common!(
                        quaternaries,
                        q,
                        common_quaternaries,
                        QUAT_COMMON,
                        QUAT_COMMON[WEIGHT_LOW]
                    );
                    quaternaries.append_byte(q);
                }
            }

            if (non_primary.bits() >> 24) as u8 == LEVEL_SEPARATOR_BYTE {
                break; // ce == NO_CE
            }
        }

        macro_rules! write_level {
            ($key:ident, $flag:ident) => {
                if levels & $flag != 0 {
                    sink.write(state, &[LEVEL_SEPARATOR_BYTE])?;
                    sink.write(state, &$key.buf)?;
                }
            };
        }

        write_level!(secondaries, SECONDARY_LEVEL_FLAG);

        if levels & CASE_LEVEL_FLAG != 0 {
            sink.write(state, &[LEVEL_SEPARATOR_BYTE])?;

            // Write pairs of nibbles as bytes, except separator bytes as themselves.
            let mut b = 0;
            if let Some((last, head)) = cases.buf.split_last() {
                debug_assert_eq!(*last, 1); // The trailing NO_CE
                for c in head {
                    debug_assert_eq!(*c & 0xf, 0);
                    debug_assert_ne!(*c, 0);
                    if b == 0 {
                        b = *c;
                    } else {
                        sink.write_byte(state, b | (*c >> 4))?;
                        b = 0;
                    }
                }
            } else {
                debug_assert!(false);
            }
            if b != 0 {
                sink.write_byte(state, b)?;
            }
        }

        write_level!(tertiaries, TERTIARY_LEVEL_FLAG);
        write_level!(quaternaries, QUATERNARY_LEVEL_FLAG);

        Ok(())
    }
}

/// Error indicating that a [`CollationKeySink`] with limited space ran out of space.
#[derive(Debug, PartialEq, Eq)]
pub struct TooSmall {
    /// The total length, in bytes, of the sort key.
    pub length: usize,
}

impl TooSmall {
    pub fn new(length: usize) -> Self {
        Self { length }
    }
}

/// A [`std::io::Write`]-like trait for writing to a buffer-like object.
///
/// (This crate does not have access to [`std`].)
///
/// <div class="stab unstable">
/// 🚧 This code is considered unstable; it may change at any time, in breaking or non-breaking ways,
/// including in SemVer minor releases. Do not implement or call methods on this trait
/// unless you are prepared for things to occasionally break.
///
/// Graduation tracking issue: [issue #7178](https://github.com/unicode-org/icu4x/issues/7178).
/// </div>
///
/// ✨ *Enabled with the `unstable` Cargo feature.*
pub trait CollationKeySink {
    /// The type of error the sink may return.
    type Error;

    /// An intermediate state object used by the sink, which must implement [`Default`].
    type State;

    /// A result value indicating the final state of the sink (e.g. a number of bytes written).
    type Output;

    /// Writes a buffer into the writer.
    fn write(&mut self, state: &mut Self::State, buf: &[u8]) -> Result<(), Self::Error>;

    /// Write a single byte into the writer.
    fn write_byte(&mut self, state: &mut Self::State, b: u8) -> Result<(), Self::Error> {
        self.write(state, &[b])
    }

    /// Finalize any internal sink state (perhaps by flushing a buffer) and return the final
    /// output value.
    fn finish(&mut self, state: Self::State) -> Result<Self::Output, Self::Error>;
}

impl CollationKeySink for Vec<u8> {
    type Error = Infallible;
    type State = ();
    type Output = ();

    fn write(&mut self, _: &mut Self::State, buf: &[u8]) -> Result<(), Self::Error> {
        self.extend_from_slice(buf);
        Ok(())
    }

    fn finish(&mut self, _: Self::State) -> Result<Self::Output, Self::Error> {
        Ok(())
    }
}

impl CollationKeySink for VecDeque<u8> {
    type Error = Infallible;
    type State = ();
    type Output = ();

    fn write(&mut self, _: &mut Self::State, buf: &[u8]) -> Result<(), Self::Error> {
        self.extend(buf.iter());
        Ok(())
    }

    fn finish(&mut self, _: Self::State) -> Result<Self::Output, Self::Error> {
        Ok(())
    }
}

impl<const N: usize> CollationKeySink for SmallVec<[u8; N]> {
    type Error = Infallible;
    type State = ();
    type Output = ();

    fn write(&mut self, _: &mut Self::State, buf: &[u8]) -> Result<(), Self::Error> {
        self.extend_from_slice(buf);
        Ok(())
    }

    fn finish(&mut self, _: Self::State) -> Result<Self::Output, Self::Error> {
        Ok(())
    }
}

impl CollationKeySink for [u8] {
    type Error = TooSmall;
    type State = usize;
    type Output = usize;

    fn write(&mut self, offset: &mut Self::State, buf: &[u8]) -> Result<(), Self::Error> {
        if *offset + buf.len() <= self.len() {
            // just checked bounds
            #[expect(clippy::indexing_slicing)]
            self[*offset..*offset + buf.len()].copy_from_slice(buf);
        }
        *offset += buf.len();
        Ok(())
    }

    fn finish(&mut self, offset: Self::State) -> Result<Self::Output, Self::Error> {
        if offset <= self.len() {
            Ok(offset)
        } else {
            Err(TooSmall::new(offset))
        }
    }
}

#[derive(Default)]
struct SortKeyLevel {
    buf: SmallVec<[u8; 40]>,
}

impl SortKeyLevel {
    fn len(&self) -> usize {
        self.buf.len()
    }

    fn is_empty(&self) -> bool {
        self.buf.is_empty()
    }

    fn append_byte(&mut self, x: u8) {
        self.buf.push(x);
    }

    fn append_weight_16(&mut self, w: u16) {
        debug_assert_ne!(w, 0);
        let b0 = (w >> 8) as u8;
        let b1 = w as u8;
        self.append_byte(b0);
        if b1 != 0 {
            self.append_byte(b1);
        }
    }

    fn append_reverse_weight_16(&mut self, w: u16) {
        debug_assert_ne!(w, 0);
        let b0 = (w >> 8) as u8;
        let b1 = w as u8;
        if b1 != 0 {
            self.append_byte(b1);
        }
        self.append_byte(b0);
    }

    fn append_weight_32(&mut self, w: u32) {
        debug_assert_ne!(w, 0);
        let b0 = (w >> 24) as u8;
        let b1 = (w >> 16) as u8;
        let b2 = (w >> 8) as u8;
        let b3 = w as u8;
        self.append_byte(b0);
        if b1 != 0 {
            self.append_byte(b1);
            if b2 != 0 {
                self.append_byte(b2);
                if b3 != 0 {
                    self.append_byte(b3);
                }
            }
        }
    }
}

// The algorithm below (BOCSU or Binary Ordered Compression Scheme for Unicode) is translated
// from the C++ code in ICU4C at icu4c/source/i18n/bocsu.{cpp,h}.  The algorithm works by
// converting a sequence of codepoints into a sequence of presumably small differences.  See
// the C++ code for a more detailed explanation.

macro_rules! negdivmod {
    ($n:ident, $d:ident, $m:ident) => {
        $m = $n % $d;
        $n /= $d;
        if $m < 0 {
            $n -= 1;
            $m += $d;
        }
    };
}

fn write_diff<S>(mut diff: i32, sink: &mut S, state: &mut S::State) -> Result<(), S::Error>
where
    S: CollationKeySink + ?Sized,
{
    let mut out = |b| sink.write_byte(state, b);

    if diff >= SLOPE_REACH_NEG_1 {
        if diff <= SLOPE_REACH_POS_1 {
            out((SLOPE_MIDDLE + diff) as _)?;
        } else if diff <= SLOPE_REACH_POS_2 {
            out((SLOPE_START_POS_2 + (diff / SLOPE_TAIL_COUNT)) as _)?;
            out((SLOPE_MIN + diff % SLOPE_TAIL_COUNT) as _)?;
        } else if diff <= SLOPE_REACH_POS_3 {
            let p2 = SLOPE_MIN + diff % SLOPE_TAIL_COUNT;
            diff /= SLOPE_TAIL_COUNT;
            let p1 = SLOPE_MIN + diff % SLOPE_TAIL_COUNT;
            let p0 = SLOPE_START_POS_3 + (diff / SLOPE_TAIL_COUNT);
            out(p0 as _)?;
            out(p1 as _)?;
            out(p2 as _)?;
        } else {
            let p3 = SLOPE_MIN + diff % SLOPE_TAIL_COUNT;
            diff /= SLOPE_TAIL_COUNT;
            let p2 = SLOPE_MIN + diff % SLOPE_TAIL_COUNT;
            diff /= SLOPE_TAIL_COUNT;
            let p1 = SLOPE_MIN + diff % SLOPE_TAIL_COUNT;
            out(SLOPE_MAX as _)?;
            out(p1 as _)?;
            out(p2 as _)?;
            out(p3 as _)?;
        }
    } else {
        let mut m;

        if diff >= SLOPE_REACH_NEG_2 {
            negdivmod!(diff, SLOPE_TAIL_COUNT, m);
            out((SLOPE_START_NEG_2 + diff) as _)?;
            out((SLOPE_MIN + m) as _)?;
        } else if diff >= SLOPE_REACH_NEG_3 {
            negdivmod!(diff, SLOPE_TAIL_COUNT, m);
            let p2 = SLOPE_MIN + m;
            negdivmod!(diff, SLOPE_TAIL_COUNT, m);
            let p1 = SLOPE_MIN + m;
            let p0 = SLOPE_START_NEG_3 + diff;
            out(p0 as _)?;
            out(p1 as _)?;
            out(p2 as _)?;
        } else {
            negdivmod!(diff, SLOPE_TAIL_COUNT, m);
            let p3 = SLOPE_MIN + m;
            negdivmod!(diff, SLOPE_TAIL_COUNT, m);
            let p2 = SLOPE_MIN + m;
            negdivmod!(diff, SLOPE_TAIL_COUNT, m);
            let p1 = SLOPE_MIN + m;
            let _ = diff;
            out(SLOPE_MIN as _)?;
            out(p1 as _)?;
            out(p2 as _)?;
            out(p3 as _)?;
        }
    }

    Ok(())
}

fn write_identical_level<I, S>(iter: I, sink: &mut S, state: &mut S::State) -> Result<(), S::Error>
where
    I: Iterator<Item = char>,
    S: CollationKeySink + ?Sized,
{
    let mut prev = 0i32;

    for c in iter {
        if !(0x4e00..=0xa000).contains(&prev) {
            prev = (prev & !0x7f) - SLOPE_REACH_NEG_1;
        } else {
            // Unihan U+4e00..U+9fa5:  double-bytes down from the upper end
            prev = 0x9fff - SLOPE_REACH_POS_2;
        }

        if c == MERGE_SEPARATOR {
            sink.write_byte(state, MERGE_SEPARATOR_BYTE)?;
            prev = 0;
        } else {
            let c = c as i32;
            write_diff(c - prev, sink, state)?;
            prev = c;
        }
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use icu_locale::locale;

    type Key = Vec<u8>;

    fn collator_en(strength: Strength) -> CollatorBorrowed<'static> {
        let locale = locale!("en").into();
        let mut options = CollatorOptions::default();
        options.strength = Some(strength);
        Collator::try_new(locale, options).unwrap()
    }

    fn collator_en_case_level(strength: Strength) -> CollatorBorrowed<'static> {
        let locale = locale!("en").into();
        let mut options = CollatorOptions::default();
        options.strength = Some(strength);
        options.case_level = Some(crate::options::CaseLevel::On);
        Collator::try_new(locale, options).unwrap()
    }

    fn keys(strength: Strength) -> (Key, Key, Key) {
        let collator = collator_en(strength);

        let mut k0 = Vec::new();
        let Ok(()) = collator.write_sort_key_to("aabc", &mut k0);
        let mut k1 = Vec::new();
        let Ok(()) = collator.write_sort_key_to("aAbc", &mut k1);
        let mut k2 = Vec::new();
        let Ok(()) = collator.write_sort_key_to("áAbc", &mut k2);

        (k0, k1, k2)
    }

    #[test]
    fn sort_key_primary() {
        let (k0, k1, k2) = keys(Strength::Primary);
        assert_eq!(k0, k1);
        assert_eq!(k1, k2);
    }

    #[test]
    fn sort_key_secondary() {
        let (k0, k1, k2) = keys(Strength::Secondary);
        assert_eq!(k0, k1);
        assert!(k1 < k2);
    }

    #[test]
    fn sort_key_tertiary() {
        let (k0, k1, k2) = keys(Strength::Tertiary);
        assert!(k0 < k1);
        assert!(k1 < k2);
    }

    fn collator_ja(strength: Strength) -> CollatorBorrowed<'static> {
        let locale = locale!("ja").into();
        let mut options = CollatorOptions::default();
        options.strength = Some(strength);
        Collator::try_new(locale, options).unwrap()
    }

    fn keys_ja_strs(strength: Strength, s0: &str, s1: &str) -> (Key, Key) {
        let collator = collator_ja(strength);

        let mut k0 = Vec::new();
        let Ok(()) = collator.write_sort_key_to(s0, &mut k0);
        let mut k1 = Vec::new();
        let Ok(()) = collator.write_sort_key_to(s1, &mut k1);

        (k0, k1)
    }

    fn keys_ja(strength: Strength) -> (Key, Key) {
        keys_ja_strs(strength, "あ", "ア")
    }

    #[test]
    fn sort_keys_ja_to_quaternary() {
        let (k0, k1) = keys_ja(Strength::Primary);
        assert_eq!(k0, k1);
        let (k0, k1) = keys_ja(Strength::Secondary);
        assert_eq!(k0, k1);
        let (k0, k1) = keys_ja(Strength::Tertiary);
        assert_eq!(k0, k1);
        let (k0, k1) = keys_ja(Strength::Quaternary);
        assert!(k0 < k1);
    }

    #[test]
    fn sort_keys_ja_identical() {
        let (k0, k1) = keys_ja_strs(Strength::Quaternary, "ア", "ｱ");
        assert_eq!(k0, k1);
        let (k0, k1) = keys_ja_strs(Strength::Identical, "ア", "ｱ");
        assert!(k0 < k1);
    }

    #[test]
    fn sort_keys_utf16() {
        let collator = collator_en(Strength::Identical);

        const STR8: &[u8] = b"hello world!";
        let mut k8 = Vec::new();
        let Ok(()) = collator.write_sort_key_utf8_to(STR8, &mut k8);

        const STR16: &[u16] = &[
            0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x21,
        ];
        let mut k16 = Vec::new();
        let Ok(()) = collator.write_sort_key_utf16_to(STR16, &mut k16);
        assert_eq!(k8, k16);
    }

    #[test]
    fn sort_keys_invalid() {
        let collator = collator_en(Strength::Identical);

        // some invalid strings
        let mut k = Vec::new();
        let Ok(()) = collator.write_sort_key_utf8_to(b"\xf0\x90", &mut k);
        let mut k = Vec::new();
        let Ok(()) = collator.write_sort_key_utf16_to(&[0xdd1e], &mut k);
    }

    #[test]
    fn sort_key_to_vecdeque() {
        let collator = collator_en(Strength::Identical);

        let mut k0 = Vec::new();
        let Ok(()) = collator.write_sort_key_to("áAbc", &mut k0);
        let mut k1 = VecDeque::new();
        let Ok(()) = collator.write_sort_key_to("áAbc", &mut k1);
        assert!(k0.iter().eq(k1.iter()));
    }

    #[test]
    fn sort_key_to_slice() {
        let collator = collator_en(Strength::Identical);

        let mut k0 = Vec::new();
        let Ok(()) = collator.write_sort_key_to("áAbc", &mut k0);
        let mut k1 = [0u8; 100];
        let len = collator.write_sort_key_to("áAbc", &mut k1[..]).unwrap();
        assert_eq!(len, k0.len());
        assert!(k0.iter().eq(k1[..len].iter()));
    }

    #[test]
    fn sort_key_to_slice_no_space() {
        let collator = collator_en(Strength::Identical);
        let mut k = [0u8; 0];
        let res = collator.write_sort_key_to("áAbc", &mut k[..]);
        assert!(matches!(res, Err(TooSmall { .. })));
    }

    #[test]
    fn sort_key_to_slice_too_long() {
        // This runs out of space in write_sort_key_up_to_quaternary.
        let collator = collator_en(Strength::Identical);
        let mut k = [0u8; 5];
        let res = collator.write_sort_key_to("áAbc", &mut k[..]);
        assert!(matches!(res, Err(TooSmall { .. })));
    }

    #[test]
    fn sort_key_to_slice_identical_too_long() {
        // This runs out of space while appending UTF-8 in the SinkAdapter.
        let collator = collator_en(Strength::Identical);
        let mut k = [0u8; 22];
        let res = collator.write_sort_key_to("áAbc", &mut k[..]);
        assert!(matches!(res, Err(TooSmall { .. })));
    }

    #[test]
    fn sort_key_just_right() {
        // get the length needed
        let collator = collator_en(Strength::Identical);
        let mut k = [0u8; 0];
        let res = collator.write_sort_key_to("áAbc", &mut k[..]);
        let len = res.unwrap_err().length;

        // almost enough
        let mut k = vec![0u8; len - 1];
        let res = collator.write_sort_key_to("áAbc", &mut k[..]);
        let len = res.unwrap_err().length;

        // just right
        let mut k = vec![0u8; len];
        let res = collator.write_sort_key_to("áAbc", &mut k[..]);
        assert_eq!(res, Ok(len));
    }

    #[test]
    fn sort_key_utf16_slice_too_small() {
        let collator = collator_en(Strength::Identical);
        const STR16: &[u16] = &[0x68, 0x65, 0x6c, 0x6c, 0x6f];
        let mut k = [0u8; 4];
        let res = collator.write_sort_key_utf16_to(STR16, &mut k[..]);
        assert!(matches!(res, Err(TooSmall { .. })));
    }

    #[test]
    fn sort_key_very_long() {
        let collator = collator_en(Strength::Secondary);
        let mut k = Vec::new();
        let Ok(()) = collator.write_sort_key_to(&"a".repeat(300), &mut k);
    }

    #[test]
    fn sort_key_case_level() {
        let collator = collator_en_case_level(Strength::Tertiary);
        let mut k = Vec::new();
        let Ok(()) = collator.write_sort_key_to("aBc", &mut k);
    }

    #[test]
    fn sort_key_case_level_empty() {
        let collator = collator_en_case_level(Strength::Tertiary);
        let mut k = Vec::new();
        let Ok(()) = collator.write_sort_key_to("", &mut k);
    }

    fn check_sort_key_less(a: &[u16], b: &[u16]) {
        let collator = collator_en(Strength::Identical);
        let mut ak = Vec::new();
        let Ok(()) = collator.write_sort_key_utf16_to(a, &mut ak);
        let mut bk = Vec::new();
        let Ok(()) = collator.write_sort_key_utf16_to(b, &mut bk);
        assert!(ak < bk, "failed: {a:04x?} - {b:04x?}");
    }

    #[test]
    fn sort_key_fffe_bug_6811() {
        check_sort_key_less(
            &[0xfffe, 0x0001, 0x0002, 0x0003],
            &[0x0001, 0xfffe, 0x0002, 0x0003],
        );
        check_sort_key_less(
            &[0x0001, 0xfffe, 0x0002, 0x0003],
            &[0x0001, 0x0002, 0xfffe, 0x0003],
        );
        check_sort_key_less(
            &[0x0001, 0x0002, 0xfffe, 0x0003],
            &[0x0001, 0x0002, 0x0003, 0xfffe],
        );
        check_sort_key_less(&[0xfffe, 0x0000, 0x0000], &[0x0000, 0xfffe, 0x0000]);
        check_sort_key_less(&[0x0000, 0xfffe, 0x0000], &[0x0000, 0x0000, 0xfffe]);
    }
}
