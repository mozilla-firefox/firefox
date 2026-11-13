// This file is part of ICU4X. For terms of use, please see the file
// called LICENSE at the top level of the ICU4X source tree
// (online at: https://github.com/unicode-org/icu4x/blob/main/LICENSE ).

//! Iterators by `char` and `TrieValue` or index, `char`, and `TrieValue`
//! over various kinds of slices of text given an `AbstractCodePointTrie`.

use core::iter::FusedIterator;
use core::marker::PhantomData;
use utf16_iter::Utf16CharIndicesWithHandler;
use utf16_iter::Utf16CharsEx;
use utf16_iter::Utf16CharsWithHandler;
use utf16_iter::Utf16Handler;
use utf8_iter::CharIndicesWithHandler;
use utf8_iter::CharsWithHandler;
use utf8_iter::Utf8CharIndicesWithHandler;
use utf8_iter::Utf8CharsEx;
use utf8_iter::Utf8CharsWithHandler;
use utf8_iter::Utf8Handler;
use utf_types::utf16::Bmp;
use utf_types::utf16::SurrogatePair;
use utf_types::utf8::Ascii;
use utf_types::utf8::FourByteSequence;
use utf_types::utf8::ThreeByteSequence;
use utf_types::utf8::TwoByteSequence;

use crate::codepointtrie::cptrie::BmpScalarBits;
use crate::codepointtrie::AbstractCodePointTrie;
use crate::codepointtrie::TrieValue;

/// Implementation of `utf8_iter::Utf8Handler` around
/// `AbstractCodePointTrie`.
#[derive(Debug)]
pub(crate) struct TrieUtf8Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'trie, T, V> TrieUtf8Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    pub(crate) fn new(trie: &'trie T) -> Self {
        Self {
            trie,
            phantom: PhantomData,
        }
    }

    #[inline(always)]
    pub(crate) fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'trie, T, V> Clone for TrieUtf8Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'trie, T, V> Utf8Handler for TrieUtf8Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Output = (char, V);

    #[inline(always)]
    fn single_byte(&self, ascii: Ascii) -> Self::Output {
        (ascii.to_char(), self.trie.ascii(ascii))
    }

    #[inline(always)]
    fn two_byte(&self, sequence: TwoByteSequence) -> Self::Output {
        (sequence.to_char(), self.trie.utf8_two_byte(sequence))
    }

    #[inline(always)]
    fn three_byte(&self, sequence: ThreeByteSequence) -> Self::Output {
        let bits = BmpScalarBits::new(sequence);
        (bits.to_char(), self.trie.bmp_bits(bits.into()))
    }

    #[inline(always)]
    fn four_byte(&self, sequence: FourByteSequence) -> Self::Output {
        let c = sequence.to_char();
        (c, self.trie.supplementary(u32::from(c)))
    }

    #[inline(always)]
    fn error(&self) -> Self::Output {
        (
            char::REPLACEMENT_CHARACTER,
            self.trie.bmp(char::REPLACEMENT_CHARACTER as u16),
        )
    }
}

/// Like `TrieUtf8Handler` but ASCII characters get `V::default()`
/// instead of a trie lookup.
#[derive(Debug)]
pub(crate) struct TrieUtf8HandlerDefaultForAscii<'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'trie, T, V> TrieUtf8HandlerDefaultForAscii<'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    pub(crate) fn new(trie: &'trie T) -> Self {
        Self {
            trie,
            phantom: PhantomData,
        }
    }

    #[inline(always)]
    pub(crate) fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'trie, T, V> Clone for TrieUtf8HandlerDefaultForAscii<'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'trie, T, V> Utf8Handler for TrieUtf8HandlerDefaultForAscii<'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Output = (char, V);

    #[inline(always)]
    fn single_byte(&self, ascii: Ascii) -> Self::Output {
        (ascii.to_char(), V::default())
    }

    #[inline(always)]
    fn two_byte(&self, sequence: TwoByteSequence) -> Self::Output {
        (sequence.to_char(), self.trie.utf8_two_byte(sequence))
    }

    #[inline(always)]
    fn three_byte(&self, sequence: ThreeByteSequence) -> Self::Output {
        let bits = BmpScalarBits::new(sequence);
        (bits.to_char(), self.trie.bmp_bits(bits.into()))
    }

    #[inline(always)]
    fn four_byte(&self, sequence: FourByteSequence) -> Self::Output {
        let c = sequence.to_char();
        (c, self.trie.supplementary(u32::from(c)))
    }

    #[inline(always)]
    fn error(&self) -> Self::Output {
        (
            char::REPLACEMENT_CHARACTER,
            self.trie.bmp(char::REPLACEMENT_CHARACTER as u16),
        )
    }
}

/// Implementation of `utf16_iter::Utf16Handler` around
/// `AbstractCodePointTrie`.
#[derive(Debug)]
pub(crate) struct TrieUtf16Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'trie, T, V> TrieUtf16Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    pub(crate) fn new(trie: &'trie T) -> Self {
        Self {
            trie,
            phantom: PhantomData,
        }
    }

    #[inline(always)]
    pub(crate) fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'trie, T, V> Clone for TrieUtf16Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    fn clone(&self) -> Self {
        Self {
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'trie, T, V> Utf16Handler for TrieUtf16Handler<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Output = (char, V);

    #[inline(always)]
    fn bmp(&self, bmp: Bmp) -> Self::Output {
        (bmp.to_char(), self.trie.bmp(bmp.to_u16()))
    }

    #[inline(always)]
    fn surrogate_pair(&self, surrogate_pair: SurrogatePair) -> Self::Output {
        let c = surrogate_pair.to_char();
        (c, self.trie.supplementary(u32::from(c)))
    }

    #[inline(always)]
    fn error(&self) -> Self::Output {
        (
            char::REPLACEMENT_CHARACTER,
            self.trie.bmp(char::REPLACEMENT_CHARACTER as u16),
        )
    }
}

/// Provides a trie accessor for types (likely iterators)
/// that are holding a reference to a type that implements
/// `AbstractCodePointTrie`.
pub trait WithTrie<'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Get a reference to the trie.
    fn trie(&self) -> &'trie T;
}

// ---

/// Iterator over `str` by `char` and `TrieValue`.
#[derive(Debug)]
pub struct CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: CharsWithHandler<'slice, TrieUtf8Handler<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `CharsWithTrie`.
    #[inline(always)]
    pub fn new(s: &'slice str, trie: &'trie T) -> Self {
        Self {
            delegate: CharsWithHandler::new(s, TrieUtf8Handler::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a string slice.
    #[inline(always)]
    pub fn as_str(&self) -> &'slice str {
        self.delegate.as_str()
    }

    /// Obtains the next `char` and the corresponding trie value such that the UTF-8 lead byte is greater than or equal to `multi_byte_lead`.
    #[inline(always)]
    pub fn next_with_minimum_lead(
        &mut self,
        multi_byte_lead: utf_types::utf8::LeadForMultiByte,
    ) -> Option<(char, V)> {
        self.delegate.next_with_minimum_lead(multi_byte_lead)
    }
}

impl<'slice, 'trie, T, V> Clone for CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        self.delegate.next()
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_str().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_str().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        self.delegate.next_back()
    }
}

impl<'slice, 'trie, T, V> FusedIterator for CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}
// --

/// Iterator over `str` by index, `char`, and `TrieValue`.
#[derive(Debug)]
pub struct CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: CharIndicesWithHandler<'slice, TrieUtf8Handler<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `CharIndicesWithTrie`.
    #[inline(always)]
    pub fn new(s: &'slice str, trie: &'trie T) -> Self {
        Self {
            delegate: CharIndicesWithHandler::new(s, TrieUtf8Handler::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a string slice.
    #[inline(always)]
    pub fn as_str(&self) -> &'slice str {
        self.delegate.as_str()
    }
}

impl<'slice, 'trie, T, V> Clone for CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next()?;
        Some((i, c, v))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_str().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_str().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next_back()?;
        Some((i, c, v))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Adds convenience methods to `str`.
pub trait CharsWithTrieEx<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `CharsWithTrie` on `str` analogously to `chars()`.
    fn chars_with_trie(&'slice self, trie: &'trie T) -> CharsWithTrie<'slice, 'trie, T, V>;

    /// Method for easily creating `CharIndicesWithTrie` on `str` analogously to `char_indices()`.
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> CharsWithTrieEx<'slice, 'trie, T, V> for str
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `CharsWithTrie` on `str` analogously to `chars()`.
    #[inline(always)]
    fn chars_with_trie(&'slice self, trie: &'trie T) -> CharsWithTrie<'slice, 'trie, T, V> {
        CharsWithTrie::new(self, trie)
    }

    /// Method for easily creating `CharIndicesWithTrie` on `str` analogously to `char_indices()`.
    #[inline(always)]
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> CharIndicesWithTrie<'slice, 'trie, T, V> {
        CharIndicesWithTrie::new(self, trie)
    }
}

// --

/// Iterator over `str` by `char` and `TrieValue` but
/// the trie value for ASCII is `V::default()` instead of
/// reading from the trie. (`V::default()` can be optimized
/// on at compile time while reading the trie's default value
/// is a run-time operation.)
#[derive(Debug)]
pub struct CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: CharsWithHandler<'slice, TrieUtf8HandlerDefaultForAscii<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `CharsWithTrieDefaultForAscii`.
    #[inline(always)]
    pub fn new(s: &'slice str, trie: &'trie T) -> Self {
        Self {
            delegate: CharsWithHandler::new(s, TrieUtf8HandlerDefaultForAscii::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a string slice.
    #[inline(always)]
    pub fn as_str(&self) -> &'slice str {
        self.delegate.as_str()
    }

    /// Obtains the next `char` and the corresponding trie value such that the UTF-8 lead byte is greater than or equal to `multi_byte_lead`.
    #[inline(always)]
    pub fn next_with_minimum_lead(
        &mut self,
        multi_byte_lead: utf_types::utf8::LeadForMultiByte,
    ) -> Option<(char, V)> {
        self.delegate.next_with_minimum_lead(multi_byte_lead)
    }
}

impl<'slice, 'trie, T, V> Clone for CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V>
    for CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        self.delegate.next()
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_str().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_str().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        self.delegate.next_back()
    }
}

impl<'slice, 'trie, T, V> FusedIterator for CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
}
// --

/// Iterator over `str` by index, `char`, and `TrieValue` but
/// the trie value for ASCII is `V::default()` instead of
/// reading from the trie. (`V::default()` can be optimized
/// on at compile time while reading the trie's default value
/// is a run-time operation.)
#[derive(Debug)]
pub struct CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: CharIndicesWithHandler<'slice, TrieUtf8HandlerDefaultForAscii<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `CharIndicesWithTrieDefaultForAscii`.
    #[inline(always)]
    pub fn new(s: &'slice str, trie: &'trie T) -> Self {
        Self {
            delegate: CharIndicesWithHandler::new(s, TrieUtf8HandlerDefaultForAscii::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a string slice.
    #[inline(always)]
    pub fn as_str(&self) -> &'slice str {
        self.delegate.as_str()
    }
}

impl<'slice, 'trie, T, V> Clone for CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V>
    for CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next()?;
        Some((i, c, v))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_str().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_str().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator
    for CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next_back()?;
        Some((i, c, v))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Adds convenience methods to `str`.
pub trait CharsWithTrieDefaultForAsciiEx<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `CharsWithTrieDefaultForAscii` on `str` analogously to `chars()`.
    fn chars_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>;

    /// Method for easily creating `CharIndicesWithTrieDefaultForAscii` on `str` analogously to `char_indices()`.
    fn char_indices_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> CharsWithTrieDefaultForAsciiEx<'slice, 'trie, T, V> for str
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `CharsWithTrieDefaultForAscii` on `str` analogously to `chars()`.
    #[inline(always)]
    fn chars_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> CharsWithTrieDefaultForAscii<'slice, 'trie, T, V> {
        CharsWithTrieDefaultForAscii::new(self, trie)
    }

    /// Method for easily creating `CharIndicesWithTrieDefaultForAscii` on `str` analogously to `char_indices()`.
    #[inline(always)]
    fn char_indices_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V> {
        CharIndicesWithTrieDefaultForAscii::new(self, trie)
    }
}

// --

/// Iterator over `[u8]` containing potentially-ill-formed UTF-8 by `char` and `TrieValue`.
#[derive(Debug)]
pub struct Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: Utf8CharsWithHandler<'slice, TrieUtf8Handler<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Utf8CharsWithTrie`.
    #[inline(always)]
    pub fn new(bytes: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            delegate: Utf8CharsWithHandler::new(bytes, TrieUtf8Handler::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a `u8` slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        self.delegate.next()
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_slice().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_slice().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        self.delegate.next_back()
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}
// --

/// Iterator over `[u8]` containing potentially-ill-formed UTF-8 by index, `char`, and `TrieValue`.
#[derive(Debug)]
pub struct Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: Utf8CharIndicesWithHandler<'slice, TrieUtf8Handler<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Utf8CharIndicesWithTrie`.
    #[inline(always)]
    pub fn new(bytes: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            delegate: Utf8CharIndicesWithHandler::new(bytes, TrieUtf8Handler::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a `u8` slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next()?;
        Some((i, c, v))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_slice().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_slice().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next_back()?;
        Some((i, c, v))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf8CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Adds convenience methods to `[u8]`.
pub trait Utf8CharsWithTrieEx<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Utf8CharsWithTrie` on `[u8]` analogously to `chars()` on `str`.
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf8CharsWithTrie<'slice, 'trie, T, V>;

    /// Method for easily creating `Utf8CharIndicesWithTrie` on `[u8]` analogously to `char_indices()` on `str`.
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrieEx<'slice, 'trie, T, V> for [u8]
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Utf8CharsWithTrie` on `[u8]` analogously to `chars()` on `str`.
    #[inline(always)]
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf8CharsWithTrie<'slice, 'trie, T, V> {
        Utf8CharsWithTrie::new(self, trie)
    }

    /// Method for easily creating `Utf8CharIndicesWithTrie` on `[u8]` analogously to `char_indices()` on `str`.
    #[inline(always)]
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrie<'slice, 'trie, T, V> {
        Utf8CharIndicesWithTrie::new(self, trie)
    }
}

// --

/// Iterator over `[u8]` by `char` and `TrieValue` but
/// the trie value for ASCII is `V::default()` instead of
/// reading from the trie. (`V::default()` can be optimized
/// on at compile time while reading the trie's default value
/// is a run-time operation.)
#[derive(Debug)]
pub struct Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: Utf8CharsWithHandler<'slice, TrieUtf8HandlerDefaultForAscii<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Utf8CharsWithTrieDefaultForAscii`.
    #[inline(always)]
    pub fn new(bytes: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            delegate: Utf8CharsWithHandler::new(bytes, TrieUtf8HandlerDefaultForAscii::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a `u8` slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V>
    for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        self.delegate.next()
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_slice().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_slice().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator
    for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        self.delegate.next_back()
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
}
// --

/// Iterator over `[u8]` by index, `char`, and `TrieValue` but
/// the trie value for ASCII is `V::default()` instead of
/// reading from the trie. (`V::default()` can be optimized
/// on at compile time while reading the trie's default value
/// is a run-time operation.)
#[derive(Debug)]
pub struct Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: Utf8CharIndicesWithHandler<'slice, TrieUtf8HandlerDefaultForAscii<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Utf8CharIndicesWithTrieDefaultForAscii`.
    #[inline(always)]
    pub fn new(bytes: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            delegate: Utf8CharIndicesWithHandler::new(
                bytes,
                TrieUtf8HandlerDefaultForAscii::new(trie),
            ),
        }
    }

    /// Obtains the remainder of the iterator as a `u8` slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V>
    for Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next()?;
        Some((i, c, v))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_slice().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_slice().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator
    for Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next_back()?;
        Some((i, c, v))
    }
}

impl<'slice, 'trie, T, V> FusedIterator
    for Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Adds convenience methods to `[u8]`.
pub trait Utf8CharsWithTrieDefaultForAsciiEx<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Utf8CharsWithTrieDefaultForAscii` on `[u8]` analogously to `chars()` on `str`.
    fn chars_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>;

    /// Method for easily creating `Utf8CharIndicesWithTrieDefaultForAscii` on `[u8]` analogously to `char_indices()` on `str`.
    fn char_indices_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrieDefaultForAsciiEx<'slice, 'trie, T, V> for [u8]
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Utf8CharsWithTrieDefaultForAscii` on `[u8]` analogously to `chars()` on `str`.
    #[inline(always)]
    fn chars_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V> {
        Utf8CharsWithTrieDefaultForAscii::new(self, trie)
    }

    /// Method for easily creating `Utf8CharIndicesWithTrieDefaultForAscii` on `[u8]` analogously to `char_indices()` on `str`.
    #[inline(always)]
    fn char_indices_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrieDefaultForAscii<'slice, 'trie, T, V> {
        Utf8CharIndicesWithTrieDefaultForAscii::new(self, trie)
    }
}

// ---

/// Iterator over `[u16]` containing potentially-ill-formed UTF-16 by `char` and `TrieValue`.
///
/// Unpaired surrogates are mapped to the REPLACEMENT CHARACTER.
#[derive(Debug)]
pub struct Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: Utf16CharsWithHandler<'slice, TrieUtf16Handler<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Utf16CharsWithTrie`.
    #[inline(always)]
    pub fn new(code_units: &'slice [u16], trie: &'trie T) -> Self {
        Self {
            delegate: Utf16CharsWithHandler::new(code_units, TrieUtf16Handler::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a `u16` slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u16] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        self.delegate.next()
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_slice().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_slice().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        self.delegate.next_back()
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}
// --

/// Iterator over `[u16]` containing potentially-ill-formed UTF-16 by index, `char`, and `TrieValue`.
///
/// Unpaired surrogates are mapped to the REPLACEMENT CHARACTER.
#[derive(Debug)]
pub struct Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: Utf16CharIndicesWithHandler<'slice, TrieUtf16Handler<'trie, T, V>>,
}

impl<'slice, 'trie, T, V> Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Utf16CharIndicesWithTrie`.
    #[inline(always)]
    pub fn new(bytes: &'slice [u16], trie: &'trie T) -> Self {
        Self {
            delegate: Utf16CharIndicesWithHandler::new(bytes, TrieUtf16Handler::new(trie)),
        }
    }

    /// Obtains the remainder of the iterator as a `u16` slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u16] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.delegate.handler().trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next()?;
        Some((i, c, v))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.as_slice().chars().count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.as_slice().chars().size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `Chars` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let (i, (c, v)) = self.delegate.next_back()?;
        Some((i, c, v))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Adds convenience methods to `[u16]`.
pub trait Utf16CharsWithTrieEx<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Utf16CharsWithTrie` on `[u16]` analogously to `chars()` on `str`.
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf16CharsWithTrie<'slice, 'trie, T, V>;

    /// Method for easily creating `Utf16CharIndicesWithTrie` on `[u16]` analogously to `char_indices()` on `str`.
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf16CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Utf16CharsWithTrieEx<'slice, 'trie, T, V> for [u16]
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Utf16CharsWithTrie` on `[u8]` analogously to `chars()` on `str`.
    #[inline(always)]
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf16CharsWithTrie<'slice, 'trie, T, V> {
        Utf16CharsWithTrie::new(self, trie)
    }

    /// Method for easily creating `Utf16CharIndicesWithTrie` on `[u8]` analogously to `char_indices()` on `str`.
    #[inline(always)]
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf16CharIndicesWithTrie<'slice, 'trie, T, V> {
        Utf16CharIndicesWithTrie::new(self, trie)
    }
}

// --

/// Iterator over Latin1 `[u8]` by `char` and `TrieValue`.
#[derive(Debug)]
pub struct Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    delegate: core::slice::Iter<'slice, u8>,
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'slice, 'trie, T, V> Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Latin1CharsWithTrie`.
    #[inline(always)]
    pub fn new(s: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            delegate: s.iter(),
            trie,
            phantom: PhantomData,
        }
    }

    /// Obtains the remainder of the iterator as a slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'slice, 'trie, T, V> Iterator for Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let b = *self.delegate.next()?;
        Some((char::from(b), self.trie.latin1(b)))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.delegate.count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.delegate.size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `delegate` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let b = *self.delegate.next_back()?;
        Some((char::from(b), self.trie.latin1(b)))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

impl<'slice, 'trie, T, V> ExactSizeIterator for Latin1CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Iterator over Latin1 `[u8]` by index, `char` and `TrieValue`.
#[derive(Debug)]
pub struct Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    offset: usize,
    delegate: core::slice::Iter<'slice, u8>,
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'slice, 'trie, T, V> Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Construct a new `Latin1CharIndicesWithTrie`.
    #[inline(always)]
    pub fn new(s: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            offset: 0,
            delegate: s.iter(),
            trie,
            phantom: PhantomData,
        }
    }

    /// Obtains the remainder of the iterator as a slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.delegate.as_slice()
    }
}

impl<'slice, 'trie, T, V> Clone for Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            offset: self.offset,
            delegate: self.delegate.clone(),
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'slice, 'trie, T, V> Iterator for Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let b = *self.delegate.next()?;
        let old_offset = self.offset;
        self.offset += 1;
        Some((old_offset, char::from(b), self.trie.latin1(b)))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.delegate.count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.delegate.size_hint()
    }

    #[inline(always)]
    fn last(mut self) -> Option<Self::Item> {
        self.next_back()
    }

    // TODO: Delegate advance_by to `delegate` once stabilized.
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let b = *self.delegate.next_back()?;
        Some((
            self.offset + self.as_slice().len(),
            char::from(b),
            self.trie.latin1(b),
        ))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

impl<'slice, 'trie, T, V> ExactSizeIterator for Latin1CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

// --

/// Adds convenience methods to `[u8]`.
pub trait Latin1CharsWithTrieEx<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Latin1CharsWithTrie` on `[u8]` analogously to `chars()` on `str`.
    /// (The name is prefixed with `latin1_` to avoid ambiguity with interpreting [u8] as UTF-8.)
    fn latin1_chars_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Latin1CharsWithTrie<'slice, 'trie, T, V>;

    /// Method for easily creating `Latin1CharIndicesWithTrie` on `[u8]` analogously to `char_indices()` on `str`.
    /// (The name is prefixed with `latin1_` to avoid ambiguity with interpreting [u8] as UTF-8.)
    fn latin1_char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Latin1CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Latin1CharsWithTrieEx<'slice, 'trie, T, V> for [u8]
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Method for easily creating `Latin1CharsWithTrie` on `[u8]` analogously to `chars()` on `str`.
    /// (The name is prefixed with `latin1_` to avoid ambiguity with interpreting [u8] as UTF-8.)
    #[inline(always)]
    fn latin1_chars_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Latin1CharsWithTrie<'slice, 'trie, T, V> {
        Latin1CharsWithTrie::new(self, trie)
    }

    /// Method for easily creating `Latin1CharIndicesWithTrie` on `[u8]` analogously to `char_indices()` on `str`.
    /// (The name is prefixed with `latin1_` to avoid ambiguity with interpreting [u8] as UTF-8.)
    #[inline(always)]
    fn latin1_char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Latin1CharIndicesWithTrie<'slice, 'trie, T, V> {
        Latin1CharIndicesWithTrie::new(self, trie)
    }
}

// --

/// Wraps an `Iterator<Item = char>` with a reference to
/// an `AbstractCodePointTrie`.
#[derive(Debug)]
pub struct CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: Iterator<Item = char>,
{
    delegate: I,
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'trie, T, V, I> CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: Iterator<Item = char>,
{
    /// Constructs a new `CharIterWithTrie`.
    #[inline(always)]
    pub fn new(iter: I, trie: &'trie T) -> Self {
        Self {
            delegate: iter,
            trie,
            phantom: PhantomData,
        }
    }
}

impl<'trie, T, V, I> WithTrie<'trie, T, V> for CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: Iterator<Item = char>,
{
    #[inline(always)]
    fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'trie, T, V, I> Clone for CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: Iterator<Item = char> + Clone,
{
    #[inline(always)]
    fn clone(&self) -> Self {
        Self {
            delegate: self.delegate.clone(),
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'trie, T, V, I> Iterator for CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: Iterator<Item = char>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let c = self.delegate.next()?;
        Some((c, self.trie.scalar(c)))
    }

    #[inline(always)]
    fn count(self) -> usize {
        self.delegate.count()
    }

    #[inline(always)]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.delegate.size_hint()
    }

    // Looks like conditionally implementing `last()` is not allowed.

    // TODO: Delegate advance_by to `delegate` once stabilized.
}

impl<'trie, T, V, I> DoubleEndedIterator for CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: DoubleEndedIterator<Item = char>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let c = self.delegate.next_back()?;
        Some((c, self.trie.scalar(c)))
    }
}

impl<'trie, T, V, I> FusedIterator for CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: FusedIterator<Item = char>,
{
}

impl<'trie, T, V, I> ExactSizeIterator for CharIterWithTrie<'trie, T, V, I>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
    I: ExactSizeIterator<Item = char>,
{
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_forward() {
        let trie = crate::codepointtrie::planes::get_planes_trie();
        let s = "abäαあ🥳𧉧";
        let mut iter = s.chars_with_trie(&trie);
        assert_eq!(iter.next(), Some(('a', 0)));
        assert_eq!(iter.next(), Some(('b', 0)));
        assert_eq!(iter.next(), Some(('ä', 0)));
        assert_eq!(iter.next(), Some(('α', 0)));
        assert_eq!(iter.next(), Some(('あ', 0)));
        assert_eq!(iter.next(), Some(('🥳', 1)));
        assert_eq!(iter.next(), Some(('𧉧', 2)));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_backwards() {
        let trie = crate::codepointtrie::planes::get_planes_trie();
        let s = "abäαあ🥳𧉧";
        let mut iter = s.chars_with_trie(&trie);
        assert_eq!(iter.next_back(), Some(('𧉧', 2)));
        assert_eq!(iter.next_back(), Some(('🥳', 1)));
        assert_eq!(iter.next_back(), Some(('あ', 0)));
        assert_eq!(iter.next_back(), Some(('α', 0)));
        assert_eq!(iter.next_back(), Some(('ä', 0)));
        assert_eq!(iter.next_back(), Some(('b', 0)));
        assert_eq!(iter.next_back(), Some(('a', 0)));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_indices_forward() {
        let trie = crate::codepointtrie::planes::get_planes_trie();
        let s = "abäαあ🥳𧉧";
        let mut iter = s.char_indices_with_trie(&trie);
        assert_eq!(iter.next(), Some((0, 'a', 0)));
        assert_eq!(iter.next(), Some((1, 'b', 0)));
        assert_eq!(iter.next(), Some((2, 'ä', 0)));
        assert_eq!(iter.next(), Some((4, 'α', 0)));
        assert_eq!(iter.next(), Some((6, 'あ', 0)));
        assert_eq!(iter.next(), Some((9, '🥳', 1)));
        assert_eq!(iter.next(), Some((13, '𧉧', 2)));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_indices_backwards() {
        let trie = crate::codepointtrie::planes::get_planes_trie();
        let s = "abäαあ🥳𧉧";
        let mut iter = s.char_indices_with_trie(&trie);
        assert_eq!(iter.next_back(), Some((13, '𧉧', 2)));
        assert_eq!(iter.next_back(), Some((9, '🥳', 1)));
        assert_eq!(iter.next_back(), Some((6, 'あ', 0)));
        assert_eq!(iter.next_back(), Some((4, 'α', 0)));
        assert_eq!(iter.next_back(), Some((2, 'ä', 0)));
        assert_eq!(iter.next_back(), Some((1, 'b', 0)));
        assert_eq!(iter.next_back(), Some((0, 'a', 0)));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_utf16_forward() {
        let trie = crate::codepointtrie::planes::get_planes_trie();
        let s = &[0xD83Eu16, 0xDD73u16, 0xD83Eu16, 0x00E4u16, 0xD83Eu16];
        let mut iter = s.chars_with_trie(&trie);
        assert_eq!(iter.next(), Some(('🥳', 1)));
        assert_eq!(iter.next(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next(), Some(('\u{00E4}', 0)));
        assert_eq!(iter.next(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_utf16_backwards() {
        let trie = crate::codepointtrie::planes::get_planes_trie();
        let s = &[0xD83Eu16, 0xDD73u16, 0xD83Eu16, 0x00E4u16, 0xD83Eu16];
        let mut iter = s.chars_with_trie(&trie);
        assert_eq!(iter.next_back(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next_back(), Some(('\u{00E4}', 0)));
        assert_eq!(iter.next_back(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next_back(), Some(('🥳', 1)));
        assert_eq!(iter.next_back(), None);
    }
}
