// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    fmt::{self, Debug, Formatter, Write},
    io::{self, Cursor},
};

use crate::{Length, expect_usize, hex::HexWithLen, to_u64};

pub const MAX_VARINT: u64 = (1 << 62) - 1;

/// Decoder is a view into a byte array that has a read offset.  Use it for parsing.
pub struct Decoder<'a> {
    buf: &'a [u8],
    offset: usize,
}

impl<'a> Decoder<'a> {
    /// Make a new view of the provided slice.
    #[must_use]
    pub const fn new(buf: &'a [u8]) -> Self {
        Self { buf, offset: 0 }
    }

    /// Get the number of bytes remaining until the end.
    #[must_use]
    pub const fn remaining(&self) -> usize {
        self.buf.len() - self.offset
    }

    /// The number of bytes from the underlying slice that have been decoded.
    #[must_use]
    pub const fn offset(&self) -> usize {
        self.offset
    }

    /// Skip n bytes.
    ///
    /// # Panics
    ///
    /// If the remaining quantity is less than `n`.
    pub fn skip(&mut self, n: usize) {
        assert!(self.remaining() >= n, "insufficient data");
        self.offset += n;
    }

    /// Skip helper that panics if `n` is `None` or not able to fit in `usize`.
    /// Only use this for tests because we panic rather than reporting a result.
    #[cfg(any(test, feature = "test-fixture"))]
    fn skip_inner(&mut self, n: Option<u64>) {
        self.skip(expect_usize(n.expect("invalid length")));
    }

    /// Skip a vector.  Panics if there isn't enough space.
    /// Only use this for tests because we panic rather than reporting a result.
    #[cfg(any(test, feature = "test-fixture"))]
    pub fn skip_vec(&mut self, n: usize) {
        let len = self.decode_n(n);
        self.skip_inner(len);
    }

    /// Skip a variable length vector.  Panics if there isn't enough space.
    /// Only use this for tests because we panic rather than reporting a result.
    #[cfg(any(test, feature = "test-fixture"))]
    pub fn skip_vvec(&mut self) {
        let len = self.decode_varint();
        self.skip_inner(len);
    }

    /// Skip while the current byte is `predicate`. Returns the number of bytes
    /// skipped.
    pub fn skip_while(&mut self, predicate: u8) -> usize {
        /// One SIMD register (SSE2/NEON), so each chunk is a single wide load and compare.
        const CHUNK: usize = 16;

        let buf = self.as_ref(); // remaining bytes
        // Go bytewise until `CHUNK`-aligned, so chunk loads never straddle cache lines.
        let head = buf.as_ptr().align_offset(CHUNK).min(buf.len());
        if let Some(i) = buf[..head].iter().position(|v| *v != predicate) {
            self.skip(i);
            return i;
        }
        let (chunks, _) = buf[head..].as_chunks::<CHUNK>();
        let start = head
            + chunks
                .iter()
                .position(|c| *c != [predicate; CHUNK])
                .unwrap_or(chunks.len())
                * CHUNK;
        let until = buf[start..]
            .iter()
            .position(|v| *v != predicate)
            .map_or(buf.len(), |i| start + i);
        self.skip(until);
        until
    }

    /// Provides the next byte without moving the read position.
    #[must_use]
    pub const fn peek_byte(&self) -> Option<u8> {
        if self.remaining() < 1 {
            None
        } else {
            Some(self.buf[self.offset])
        }
    }

    /// Decodes arbitrary data.
    pub fn decode(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.remaining() < n {
            return None;
        }
        let res = &self.buf[self.offset..self.offset + n];
        self.offset += n;
        Some(res)
    }

    /// Decodes `N` bytes of arbitrary data as an array.
    pub fn decode_array<const N: usize>(&mut self) -> Option<&'a [u8; N]> {
        self.decode(N)?.as_array()
    }

    pub(crate) fn decode_n(&mut self, n: usize) -> Option<u64> {
        debug_assert!(n > 0 && n <= 8);
        if self.remaining() < n {
            return None;
        }
        Some(if n == 1 {
            let v = u64::from(self.buf[self.offset]);
            self.offset += 1;
            v
        } else {
            let mut buf = [0; 8];
            buf[8 - n..].copy_from_slice(&self.buf[self.offset..self.offset + n]);
            self.offset += n;
            u64::from_be_bytes(buf)
        })
    }

    /// Decodes a big-endian, unsigned integer value into the target type.
    /// This returns `None` if there is not enough data remaining
    /// or if the conversion to the identified type fails.
    /// Conversion is via `u64`, so failures are impossible for
    /// unsigned integer types: `u8`, `u16`, `u32`, or `u64`.
    /// Signed types will fail if the high bit is set.
    pub fn decode_uint<T: TryFrom<u64>>(&mut self) -> Option<T> {
        let v = self.decode_n(size_of::<T>());
        T::try_from(v?).ok()
    }

    /// Decodes a QUIC varint.
    pub fn decode_varint(&mut self) -> Option<u64> {
        let first = *self.buf.get(self.offset)?;
        // The first byte's two most significant bits encode the length: 1, 2, 4 or 8 bytes.
        // Range checks on that byte keep the common one-byte case a predictable branch, and
        // each arm is a single fixed-width load with those bits masked off.
        if first < 0x40 {
            self.offset += 1;
            return Some(u64::from(first));
        }
        if first < 0x80 {
            let v = u64::from(u16::from_be_bytes(
                *self.buf.get(self.offset..)?.first_chunk()?,
            )) & 0x3fff;
            self.offset += 2;
            return Some(v);
        }
        if first < 0xc0 {
            let v = u64::from(u32::from_be_bytes(
                *self.buf.get(self.offset..)?.first_chunk()?,
            )) & 0x3fff_ffff;
            self.offset += 4;
            return Some(v);
        }
        let v = u64::from_be_bytes(*self.buf.get(self.offset..)?.first_chunk()?) & MAX_VARINT;
        self.offset += 8;
        Some(v)
    }

    /// Decodes the rest of the buffer.  Infallible.
    pub fn decode_remainder(&mut self) -> &'a [u8] {
        let res = &self.buf[self.offset..];
        self.offset = self.buf.len();
        res
    }

    fn decode_checked(&mut self, n: Option<u64>) -> Option<&'a [u8]> {
        if let Ok(l) = usize::try_from(n?) {
            self.decode(l)
        } else {
            // sizeof(usize) < sizeof(u64) and the value is greater than
            // usize can hold. Throw away the rest of the input.
            self.offset = self.buf.len();
            None
        }
    }

    /// Decodes a TLS-style length-prefixed buffer.
    pub fn decode_vec(&mut self, n: usize) -> Option<&'a [u8]> {
        let len = self.decode_n(n);
        self.decode_checked(len)
    }

    /// Decodes a QUIC varint-length-prefixed buffer.
    pub fn decode_vvec(&mut self) -> Option<&'a [u8]> {
        let len = self.decode_varint();
        self.decode_checked(len)
    }
}

// Implement `AsRef` for `Decoder` so that values can be examined without
// moving the cursor.
impl<'a> AsRef<[u8]> for Decoder<'a> {
    fn as_ref(&self) -> &'a [u8] {
        &self.buf[self.offset..]
    }
}

impl Debug for Decoder<'_> {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        HexWithLen::fmt(f, self)
    }
}

impl<'a> From<&'a [u8]> for Decoder<'a> {
    fn from(buf: &'a [u8]) -> Self {
        Decoder::new(buf)
    }
}

impl<'a, T> From<&'a T> for Decoder<'a>
where
    T: AsRef<[u8]>,
{
    fn from(buf: &'a T) -> Self {
        Decoder::new(buf.as_ref())
    }
}

impl<'b> PartialEq<Decoder<'b>> for Decoder<'_> {
    fn eq(&self, other: &Decoder<'b>) -> bool {
        self.buf == other.buf
    }
}

/// Bytes reserved in an [`Encoder`] and later filled by [`Encoder::overwrite`].
#[must_use = "call Encoder::overwrite to fill the reserved bytes"]
#[derive(Copy, Clone)]
pub struct Reservation<const N: usize>(usize);

/// Encoder is good for building data structures.
pub struct Encoder<B = Vec<u8>> {
    buf: B,
    /// Tracks the starting position of the buffer when the [`Encoder`] is created.
    /// This allows distinguishing between bytes that existed in the buffer before
    /// encoding began and those written by the [`Encoder`] itself.
    start: usize,
}

impl Clone for Encoder {
    fn clone(&self) -> Self {
        Self {
            buf: self.as_ref().to_vec(),
            start: 0,
        }
    }
}

impl<B: Buffer> PartialEq for Encoder<B> {
    fn eq(&self, other: &Self) -> bool {
        self.as_ref() == other.as_ref()
    }
}

impl<B: Buffer> Eq for Encoder<B> {}

impl<B: Buffer> Encoder<B> {
    /// Get the length of the [`Encoder`].
    ///
    /// Note that the length of the underlying buffer might be larger.
    #[must_use]
    pub fn len(&self) -> usize {
        self.buf.position() - self.start
    }

    /// Returns true if the encoder buffer contains no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Create a view of the current contents of the buffer.
    /// Note: for a view of a slice, use `Decoder::new(&enc[s..e])`
    #[must_use]
    pub fn as_decoder(&self) -> Decoder<'_> {
        Decoder::new(self.as_ref())
    }

    /// Generic encode routine for arbitrary data.
    ///
    /// # Panics
    ///
    /// When writing to the underlying buffer fails.
    pub fn encode<D: AsRef<[u8]>>(&mut self, data: D) -> &mut Self {
        self.buf
            .write_all(data.as_ref())
            .expect("Buffer has enough capacity.");
        self
    }

    /// Encode a single byte.
    ///
    /// # Panics
    ///
    /// When writing to the underlying buffer fails.
    pub fn encode_byte(&mut self, data: u8) -> &mut Self {
        self.buf
            .write_all(&[data])
            .expect("Buffer has enough capacity.");
        self
    }

    /// Encode an integer of any size up to u64.
    ///
    /// # Panics
    ///
    /// When `n` is outside the range `1..=8`.
    pub fn encode_uint<T: Into<u64>>(&mut self, n: usize, v: T) -> &mut Self {
        let v = v.into();
        assert!(n > 0 && n <= 8);
        // Using to_be_bytes() generates better assembly, especially for odd sizes
        // where it uses rev + strh instead of multiple strb instructions
        let bytes = v.to_be_bytes();
        self.encode(&bytes[8 - n..])
    }

    /// Encode a QUIC varint.
    ///
    /// # Panics
    ///
    /// When `v >= 1<<62`.
    pub fn encode_varint<T: Into<u64>>(&mut self, v: T) -> &mut Self {
        let v = v.into();
        // Using to_be_bytes() generates better assembly with rev instructions
        // instead of multiple shifts and ors
        #[expect(clippy::cast_possible_truncation, reason = "This is intentional.")]
        match () {
            () if v < (1 << 6) => self.encode_byte(v as u8),
            () if v < (1 << 14) => self.encode((v as u16 | (1 << 14)).to_be_bytes()),
            () if v < (1 << 30) => self.encode((v as u32 | (2 << 30)).to_be_bytes()),
            () if v < (1 << 62) => self.encode((v | (3 << 62)).to_be_bytes()),
            () => panic!("Varint value too large"),
        }
    }

    /// Reserve `N` bytes, filled with zero, for a later [`Self::overwrite`].
    ///
    /// # Panics
    ///
    /// When the underlying buffer cannot hold `N` more bytes.
    pub fn reserve<const N: usize>(&mut self) -> Reservation<N> {
        let pos = self.buf.position();
        self.pad_to(self.len() + N, 0);
        Reservation(pos)
    }

    /// Overwrite bytes previously returned by [`Self::reserve`].
    ///
    /// # Panics
    ///
    /// When `rsv` is not contained in the bytes written to this encoder.
    pub fn overwrite<const N: usize>(&mut self, rsv: Reservation<N>, data: [u8; N]) -> &mut Self {
        assert!(rsv.0 >= self.start, "reservation is outside the encoder");
        let offset = rsv.0 - self.start;
        let end = offset + N;
        let len = self.len();
        assert!(end <= len, "reservation extends past encoder");
        self.as_mut()[offset..end].copy_from_slice(&data);
        self
    }

    /// Encode a length or byte count as a QUIC varint, accepting either `usize` or `u64`.
    pub fn encode_len<T: Length>(&mut self, v: T) -> &mut Self {
        self.encode_varint(v.as_u64())
    }

    /// Encode a vector in TLS style.
    ///
    /// # Panics
    ///
    /// When `v` is longer than 2^n.
    pub fn encode_vec(&mut self, n: usize, v: &[u8]) -> &mut Self {
        self.encode_uint(n, to_u64(v.as_ref().len())).encode(v)
    }

    /// Encode a vector in TLS style using a closure for the contents.
    ///
    /// # Panics
    ///
    /// When `f()` returns a length larger than `2^8n`.
    #[expect(
        clippy::cast_possible_truncation,
        reason = "AND'ing with 0xff makes this OK."
    )]
    pub fn encode_vec_with<F: FnOnce(&mut Self)>(&mut self, n: usize, f: F) -> &mut Self {
        let start = self.buf.position();
        self.pad_to(n, 0);
        f(self);
        let len = self.buf.position() - start - n;
        assert!(len < (1 << (n * 8)));
        for i in 0..n {
            self.buf
                .write_at(start + i, ((len >> (8 * (n - i - 1))) & 0xff) as u8);
        }
        self
    }

    /// Encode a vector with a varint length.
    ///
    /// # Panics
    ///
    /// When `v` is longer than 2^62.
    pub fn encode_vvec(&mut self, v: &[u8]) -> &mut Self {
        self.encode_len(v.as_ref().len()).encode(v)
    }

    /// Encode a vector with a varint length using a closure.
    ///
    /// # Panics
    ///
    /// When `f()` writes more than 2^62 bytes.
    pub fn encode_vvec_with<F: FnOnce(&mut Self)>(&mut self, f: F) -> &mut Self {
        let start = self.buf.position();
        // Optimize for short buffers, reserve a single byte for the length.
        self.buf
            .write_all(&[0])
            .expect("Buffer has enough capacity.");
        f(self);
        let len = self.buf.position() - start - 1;

        // Now to insert a varint for `len` before the encoded block.
        //
        // We now have one zero byte at `start`, followed by `len` encoded bytes:
        //   |  0  | ... encoded ... |
        // We are going to encode a varint by putting the low bytes in that spare byte.
        // Any additional bytes for the varint are put after the encoded blob:
        //   | low | ... encoded ... | varint high |
        // Then we will rotate that entire piece right, by however many bytes we add:
        //   | varint high | low | ... encoded ... |
        // As long as encoding more than 63 bytes is rare, this won't cost much relative
        // to the convenience of being able to use this function.

        let v = to_u64(len);
        // The lower order byte fits before the inserted block of bytes.
        self.buf.write_at(start, (v & 0xff) as u8);
        let (count, bits) = match () {
            // Great.  The byte we have is enough.
            () if v < (1 << 6) => return self,
            () if v < (1 << 14) => (1, 1 << 6),
            () if v < (1 << 30) => (3, 2 << 22),
            () if v < (1 << 62) => (7, 3 << 54),
            () => panic!("Varint value too large"),
        };
        // Now, we need to encode the high bits after the main block, ...
        self.encode_uint(count, (v >> 8) | bits);
        // ..., then rotate the entire thing right by the same amount.
        self.buf.rotate_right(start, count);
        self
    }

    /// Truncate the encoder to the given size.
    pub fn truncate(&mut self, len: usize) {
        self.buf.truncate(len + self.start);
    }

    /// Pad the [`Encoder`] to `len` with bytes set to `v`.
    pub fn pad_to(&mut self, len: usize, v: u8) {
        let buffer_len = self.start + len;
        if buffer_len > self.buf.position() {
            self.buf.pad_to(buffer_len, v);
        }
    }
}

impl Encoder<Vec<u8>> {
    /// Skip the first `n` bytes from the encoder buffer without copying.
    /// This advances the internal offset, making those bytes inaccessible.
    /// Skipped bytes stay allocated until [`Self::compact`].
    ///
    /// # Panics
    ///
    /// Panics if `n` is greater than the current length of the encoder.
    pub fn skip(&mut self, n: usize) {
        assert!(n <= self.len(), "Cannot skip beyond buffer length");
        self.start += n;
    }

    /// Get the allocated capacity of the underlying buffer.
    #[must_use]
    pub const fn capacity(&self) -> usize {
        self.buf.capacity()
    }

    /// Release skipped bytes once they are at least as many as the remaining ones,
    /// keeping the allocated capacity. This invalidates any outstanding [`Reservation`].
    pub fn compact(&mut self) {
        if self.start >= self.len() {
            self.buf.drain(..self.start);
            self.start = 0;
        }
    }

    /// Static helper function for previewing the results of encoding without doing it.
    ///
    /// # Panics
    ///
    /// When `v` is too large.
    #[must_use]
    pub const fn varint_len(v: u64) -> usize {
        match () {
            () if v < (1 << 6) => 1,
            () if v < (1 << 14) => 2,
            () if v < (1 << 30) => 4,
            () if v < (1 << 62) => 8,
            () => panic!("Varint value too large"),
        }
    }

    /// Static helper to determine how long a varint-prefixed array encodes to.
    ///
    /// # Panics
    ///
    /// When `len` doesn't fit in a `u64`.
    #[must_use]
    pub const fn vvec_len(len: usize) -> usize {
        Self::varint_len(to_u64(len)) + len
    }

    /// Construction of a buffer with a predetermined capacity.
    #[must_use]
    pub fn with_capacity(capacity: usize) -> Self {
        Self {
            buf: Vec::with_capacity(capacity),
            start: 0,
        }
    }

    /// Don't use this except in testing.
    ///
    /// # Panics
    ///
    /// When `s` contains non-hex values or an odd number of values.
    #[cfg(any(test, feature = "test-fixture"))]
    #[must_use]
    pub fn from_hex<A: AsRef<str>>(s: A) -> Self {
        let s = s.as_ref();
        assert_eq!(s.len() % 2, 0, "Needs to be even length");

        let cap = s.len() / 2;
        let mut enc = Self::with_capacity(cap);

        for i in 0..cap {
            #[expect(clippy::unwrap_used, reason = "Only used in tests.")]
            let v = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).unwrap();
            enc.encode_byte(v);
        }
        enc
    }
}

impl Default for Encoder {
    fn default() -> Self {
        Self {
            buf: Vec::new(),
            start: 0,
        }
    }
}

impl Debug for Encoder {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        HexWithLen::fmt(f, self)
    }
}

impl<B: Buffer> AsRef<[u8]> for Encoder<B> {
    fn as_ref(&self) -> &[u8] {
        &self.buf.as_slice()[self.start..]
    }
}

impl<B: Buffer> AsMut<[u8]> for Encoder<B> {
    fn as_mut(&mut self) -> &mut [u8] {
        &mut self.buf.as_mut()[self.start..]
    }
}

impl From<&[u8]> for Encoder {
    fn from(buf: &[u8]) -> Self {
        Self {
            buf: Vec::from(buf),
            start: 0,
        }
    }
}

impl From<Encoder> for Vec<u8> {
    fn from(mut enc: Encoder) -> Self {
        if enc.start > 0 {
            enc.buf.drain(..enc.start);
        }
        enc.buf
    }
}

#[expect(
    clippy::unwrap_in_result,
    reason = "successful writing to buffer needs to be guaranteed by caller"
)]
impl<B: io::Write> Write for Encoder<B> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.buf
            .write_all(s.as_bytes())
            .expect("Buffer has enough capacity.");
        Ok(())
    }
}

#[expect(clippy::unnecessary_safety_doc, reason = "relevant for created object")]
impl<'a> Encoder<Cursor<&'a mut [u8]>> {
    /// # Safety
    ///
    /// Any mutable method on [`Encoder<Cursor<&mut [u8]>>`] assumes the
    /// underlying buffer has enough capacity for the called operation. This
    /// invariant needs to be upheld by the caller.
    #[must_use]
    pub const fn new_borrowed_slice(buf: &'a mut [u8]) -> Self {
        Encoder {
            buf: Cursor::new(buf),
            start: 0,
        }
    }
}

impl<'a> Encoder<&'a mut Vec<u8>> {
    #[must_use]
    pub fn new_borrowed_vec(buf: &'a mut Vec<u8>) -> Self {
        Encoder {
            start: buf.position(),
            buf,
        }
    }
}

/// Extends a memory buffer with methods beyond [`std::io::Write`]. Needed for
/// [`Encoder`].
///
/// Note that each method operates on the bytes written, not the entire buffer.
/// E.g. [`Buffer::as_slice`] returns the bytes written, not all bytes of the
/// underlying buffer.
pub trait Buffer: io::Write {
    fn position(&self) -> usize;

    fn is_empty(&self) -> bool {
        self.position() == 0
    }

    fn as_slice(&self) -> &[u8];

    fn as_mut(&mut self) -> &mut [u8];

    fn truncate(&mut self, len: usize);

    fn pad_to(&mut self, n: usize, v: u8);

    // Functions needed for `Encoder::encode_vvec_with` and `Encoder::encode_vec_with`.

    fn write_at(&mut self, pos: usize, data: u8);

    fn rotate_right(&mut self, start: usize, count: usize);
}

impl Buffer for Vec<u8> {
    fn position(&self) -> usize {
        self.len()
    }

    fn as_slice(&self) -> &[u8] {
        self.as_ref()
    }

    fn as_mut(&mut self) -> &mut [u8] {
        self.as_mut_slice()
    }

    fn truncate(&mut self, len: usize) {
        Self::truncate(self, len);
    }

    fn pad_to(&mut self, n: usize, v: u8) {
        self.resize(n, v);
    }

    fn write_at(&mut self, pos: usize, data: u8) {
        self[pos] = data;
    }

    fn rotate_right(&mut self, start: usize, count: usize) {
        self[start..].rotate_right(count);
    }
}

impl Buffer for &mut Vec<u8> {
    fn position(&self) -> usize {
        Vec::len(self)
    }

    fn as_slice(&self) -> &[u8] {
        self.as_ref()
    }

    fn as_mut(&mut self) -> &mut [u8] {
        self.as_mut_slice()
    }

    fn truncate(&mut self, len: usize) {
        Vec::truncate(self, len);
    }

    fn pad_to(&mut self, n: usize, v: u8) {
        self.resize(n, v);
    }

    fn write_at(&mut self, pos: usize, data: u8) {
        self[pos] = data;
    }

    fn rotate_right(&mut self, start: usize, count: usize) {
        self[start..].rotate_right(count);
    }
}

impl Buffer for Cursor<&mut [u8]> {
    fn position(&self) -> usize {
        expect_usize(self.position())
    }

    fn as_slice(&self) -> &[u8] {
        &self.get_ref()[..Buffer::position(self)]
    }

    fn as_mut(&mut self) -> &mut [u8] {
        let len = Buffer::position(self);
        &mut self.get_mut()[..len]
    }

    fn truncate(&mut self, len: usize) {
        let old_position = Buffer::position(self);
        if len < old_position {
            self.set_position(to_u64(len));
            self.get_mut()[len..old_position].fill(0);
        }
    }

    fn pad_to(&mut self, n: usize, v: u8) {
        let start = expect_usize(self.position());

        self.get_mut()[start..n].fill(v);
        self.set_position(to_u64(n));
    }

    fn write_at(&mut self, pos: usize, data: u8) {
        self.get_mut()[pos] = data;
    }

    fn rotate_right(&mut self, start: usize, count: usize) {
        let len = Buffer::position(self);
        self.get_mut()[start..len].rotate_right(count);
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{
        fmt::Write as _,
        io::{Cursor, Write as _},
    };

    use super::{Buffer, Decoder, Encoder, MAX_VARINT, Reservation};

    #[test]
    fn reserve_then_overwrite() {
        let mut enc = Encoder::default();
        enc.encode_byte(0xaa);
        let one: Reservation<1> = enc.reserve();
        let two: Reservation<2> = enc.reserve();
        enc.encode_byte(0xbb);
        assert_eq!(enc, Encoder::from_hex("aa000000bb"));
        enc.overwrite(two, [0x22, 0x33]);
        enc.overwrite(one, [0x11]);
        assert_eq!(enc, Encoder::from_hex("aa112233bb"));

        // `start` is not zero: the reserved offset is absolute in the buffer.
        let mut buf = vec![0xff];
        {
            let mut enc = Encoder::new_borrowed_vec(&mut buf);
            let reserved = enc.reserve::<1>();
            enc.encode_byte(0x22);
            enc.overwrite(reserved, [0x11]);
        }
        assert_eq!(buf, [0xff, 0x11, 0x22]);
    }

    #[test]
    fn decode() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode(2).unwrap(), &[0x01, 0x23]);
        assert!(dec.decode(2).is_none());
    }

    #[test]
    fn decode_array() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode_array(), Some(&[0x01, 0x23]));
        assert!(dec.decode_array::<2>().is_none());
        assert_eq!(dec.decode_array(), Some(&[0x45]));
    }

    #[test]
    fn decode_byte() {
        let enc = Encoder::from_hex("0123");
        let mut dec = enc.as_decoder();

        assert_eq!(dec.decode_uint::<u8>().unwrap(), 0x01);
        assert_eq!(dec.decode_uint::<u8>().unwrap(), 0x23);
        assert!(dec.decode_uint::<u8>().is_none());
    }

    #[test]
    fn peek_byte() {
        let enc = Encoder::from_hex("01");
        let mut dec = enc.as_decoder();

        assert_eq!(dec.offset(), 0);
        assert_eq!(dec.peek_byte().unwrap(), 0x01);
        dec.skip(1);
        assert_eq!(dec.offset(), 1);
        assert!(dec.peek_byte().is_none());
    }

    #[test]
    fn decode_byte_short() {
        let enc = Encoder::from_hex("");
        let mut dec = enc.as_decoder();
        assert!(dec.decode_uint::<u8>().is_none());
    }

    #[test]
    fn decode_remainder() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode_remainder(), &[0x01, 0x23, 0x45]);
        assert!(dec.decode(2).is_none());

        let mut dec = Decoder::from(&[]);
        assert!(dec.decode_remainder().is_empty());
    }

    #[test]
    fn decode_vec() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode_vec(1).expect("read one octet length"), &[0x23]);
        assert_eq!(dec.remaining(), 1);

        let enc = Encoder::from_hex("00012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode_vec(2).expect("read two octet length"), &[0x23]);
        assert_eq!(dec.remaining(), 1);
    }

    #[test]
    fn decode_vec_short() {
        // The length is too short.
        let enc = Encoder::from_hex("02");
        let mut dec = enc.as_decoder();
        assert!(dec.decode_vec(2).is_none());

        // The body is too short.
        let enc = Encoder::from_hex("0200");
        let mut dec = enc.as_decoder();
        assert!(dec.decode_vec(1).is_none());
    }

    #[test]
    fn decode_vvec() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode_vvec().expect("read one octet length"), &[0x23]);
        assert_eq!(dec.remaining(), 1);

        let enc = Encoder::from_hex("40012345");
        let mut dec = enc.as_decoder();
        assert_eq!(dec.decode_vvec().expect("read two octet length"), &[0x23]);
        assert_eq!(dec.remaining(), 1);
    }

    #[test]
    fn decode_vvec_short() {
        // The length field is too short.
        let enc = Encoder::from_hex("ff");
        let mut dec = enc.as_decoder();
        assert!(dec.decode_vvec().is_none());

        let enc = Encoder::from_hex("405500");
        let mut dec = enc.as_decoder();
        assert!(dec.decode_vvec().is_none());
    }

    #[test]
    fn skip() {
        let enc = Encoder::from_hex("ffff");
        let mut dec = enc.as_decoder();
        dec.skip(1);
        assert_eq!(dec.remaining(), 1);
    }

    #[test]
    #[should_panic(expected = "insufficient data")]
    fn skip_too_much() {
        let enc = Encoder::from_hex("ff");
        let mut dec = enc.as_decoder();
        dec.skip(2);
    }

    #[test]
    fn skip_vec() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        dec.skip_vec(1);
        assert_eq!(dec.remaining(), 1);
    }

    #[test]
    #[should_panic(expected = "insufficient data")]
    fn skip_vec_too_much() {
        let enc = Encoder::from_hex("ff1234");
        let mut dec = enc.as_decoder();
        dec.skip_vec(1);
    }

    #[test]
    #[should_panic(expected = "invalid length")]
    fn skip_vec_short_length() {
        let enc = Encoder::from_hex("ff");
        let mut dec = enc.as_decoder();
        dec.skip_vec(4);
    }
    #[test]
    fn skip_vvec() {
        let enc = Encoder::from_hex("012345");
        let mut dec = enc.as_decoder();
        dec.skip_vvec();
        assert_eq!(dec.remaining(), 1);
    }

    #[test]
    #[should_panic(expected = "insufficient data")]
    fn skip_vvec_too_much() {
        let enc = Encoder::from_hex("0f1234");
        let mut dec = enc.as_decoder();
        dec.skip_vvec();
    }

    #[test]
    #[should_panic(expected = "invalid length")]
    fn skip_vvec_short_length() {
        let enc = Encoder::from_hex("ff");
        let mut dec = enc.as_decoder();
        dec.skip_vvec();
    }

    #[test]
    fn skip_while() {
        let enc = Encoder::from_hex("000001020202");
        let mut dec = enc.as_decoder();

        // Skip all zeros
        let skipped = dec.skip_while(0);
        assert_eq!(skipped, 2);
        assert_eq!(dec.offset(), 2);
        assert_eq!(dec.remaining(), 4);
        assert_eq!(dec.as_ref(), &[0x01, 0x02, 0x02, 0x02]);

        // Skip until 0x02
        let skipped = dec.skip_while(0x01);
        assert_eq!(skipped, 1);
        assert_eq!(dec.offset(), 3);
        assert_eq!(dec.remaining(), 3);
        assert_eq!(dec.as_ref(), &[0x02, 0x02, 0x02]);

        // Don't skip on no match.
        let skipped = dec.skip_while(0xFF);
        assert_eq!(skipped, 0);
        assert_eq!(dec.offset(), 3);
        assert_eq!(dec.remaining(), 3);
        assert_eq!(dec.as_ref(), &[0x02, 0x02, 0x02]);

        // Skip till end.
        let skipped = dec.skip_while(0x02);
        assert_eq!(skipped, 3);
        assert_eq!(dec.offset(), 6);
        assert_eq!(dec.remaining(), 0);
        assert_eq!(dec.as_ref(), &[0u8; 0]);
    }

    #[test]
    fn encoded_lengths() {
        assert_eq!(Encoder::varint_len(0), 1);
        assert_eq!(Encoder::varint_len(0x3f), 1);
        assert_eq!(Encoder::varint_len(0x40), 2);
        assert_eq!(Encoder::varint_len(0x3fff), 2);
        assert_eq!(Encoder::varint_len(0x4000), 4);
        assert_eq!(Encoder::varint_len(0x3fff_ffff), 4);
        assert_eq!(Encoder::varint_len(0x4000_0000), 8);
        assert_eq!(Encoder::varint_len(MAX_VARINT), 8);
        assert_eq!(MAX_VARINT, (1 << 62) - 1);
    }

    #[test]
    #[should_panic(expected = "Varint value too large")]
    const fn encoded_length_oob() {
        _ = Encoder::varint_len(1 << 62);
    }

    #[test]
    fn encoded_vvec_lengths() {
        assert_eq!(Encoder::vvec_len(0), 1);
        assert_eq!(Encoder::vvec_len(0x3f), 0x40);
        assert_eq!(Encoder::vvec_len(0x40), 0x42);
        assert_eq!(Encoder::vvec_len(0x3fff), 0x4001);
        assert_eq!(Encoder::vvec_len(0x4000), 0x4004);
        assert_eq!(Encoder::vvec_len(0x3fff_ffff), 0x4000_0003);
        assert_eq!(Encoder::vvec_len(0x4000_0000), 0x4000_0008);
    }

    #[test]
    #[cfg(target_pointer_width = "64")] // Test does not compile on 32-bit targets.
    #[should_panic(expected = "Varint value too large")]
    fn encoded_vvec_length_oob() {
        _ = Encoder::vvec_len(1 << 62);
    }

    #[test]
    fn encode_byte() {
        let mut enc = Encoder::default();

        enc.encode_byte(1);
        assert_eq!(enc, Encoder::from_hex("01"));

        enc.encode_byte(0xfe);
        assert_eq!(enc, Encoder::from_hex("01fe"));
    }

    #[test]
    fn encode() {
        let mut enc = Encoder::default();
        enc.encode([1, 2, 3]);
        assert_eq!(enc, Encoder::from_hex("010203"));
    }

    #[test]
    fn encode_uint() {
        let mut enc = Encoder::default();
        enc.encode_uint(2, 10_u8); // 000a
        enc.encode_uint(1, 257_u16); // 01
        enc.encode_uint(3, 0xff_ffff_u32); // ffffff
        enc.encode_uint(8, 0xfedc_ba98_7654_3210_u64);
        assert_eq!(enc, Encoder::from_hex("000a01fffffffedcba9876543210"));
    }

    #[test]
    fn builder_from_slice() {
        let slice = &[1, 2, 3];
        let enc = Encoder::from(&slice[..]);
        assert_eq!(enc, Encoder::from_hex("010203"));
    }

    #[test]
    fn builder_inas_decoder() {
        let enc = Encoder::from_hex("010203");
        let buf = &[1, 2, 3];
        assert_eq!(enc.as_decoder(), Decoder::new(buf));
        assert_ne!(enc.as_decoder(), Decoder::new(&[9, 9, 9]));
    }

    struct UintTestCase {
        v: u64,
        b: String,
    }

    macro_rules! uint_tc {
        [$( $v:expr => $b:expr ),+ $(,)?] => {
            vec![ $( UintTestCase { v: $v, b: String::from($b) } ),+]
        };
    }

    #[test]
    fn varint_encode_decode() {
        let cases = uint_tc![
            0 => "00",
            1 => "01",
            63 => "3f",
            64 => "4040",
            16383 => "7fff",
            16384 => "80004000",
            (1 << 30) - 1 => "bfffffff",
            1 << 30 => "c000000040000000",
            (1 << 62) - 1 => "ffffffffffffffff",
        ];

        for c in cases {
            assert_eq!(Encoder::varint_len(c.v), c.b.len() / 2);

            let mut enc = Encoder::default();
            enc.encode_varint(c.v);
            let encoded = Encoder::from_hex(&c.b);
            assert_eq!(enc, encoded);

            let mut dec = encoded.as_decoder();
            let v = dec.decode_varint().expect("should decode");
            assert_eq!(dec.remaining(), 0);
            assert_eq!(v, c.v);
        }
    }

    #[test]
    fn varint_decode_long_zero() {
        for c in &["4000", "80000000", "c000000000000000"] {
            let encoded = Encoder::from_hex(c);
            let mut dec = encoded.as_decoder();
            let v = dec.decode_varint().expect("should decode");
            assert_eq!(dec.remaining(), 0);
            assert_eq!(v, 0);
        }
    }

    #[test]
    fn varint_decode_short() {
        for c in &["40", "800000", "c0000000000000"] {
            let encoded = Encoder::from_hex(c);
            let mut dec = encoded.as_decoder();
            assert!(dec.decode_varint().is_none());
        }
    }

    #[test]
    fn encode_vec() {
        let mut enc = Encoder::default();
        enc.encode_vec(2, &[1, 2, 0x34]);
        assert_eq!(enc, Encoder::from_hex("0003010234"));
    }

    #[test]
    fn encode_vec_with() {
        let mut enc = Encoder::default();
        enc.encode_vec_with(2, |enc_inner| {
            enc_inner.encode(Encoder::from_hex("02").as_ref());
        });
        assert_eq!(enc, Encoder::from_hex("000102"));
    }

    #[test]
    #[should_panic(expected = "assertion failed")]
    fn encode_vec_with_overflow() {
        let mut enc = Encoder::default();
        enc.encode_vec_with(1, |enc_inner| {
            enc_inner.encode([0xb0; 256]);
        });
    }

    #[test]
    fn encode_vvec() {
        let mut enc = Encoder::default();
        enc.encode_vvec(&[1, 2, 0x34]);
        assert_eq!(enc, Encoder::from_hex("03010234"));
    }

    #[test]
    fn encode_vvec_with() {
        let mut enc = Encoder::default();
        enc.encode_vvec_with(|enc_inner| {
            enc_inner.encode(Encoder::from_hex("02").as_ref());
        });
        assert_eq!(enc, Encoder::from_hex("0102"));
    }

    #[test]
    fn encode_vvec_with_longer() {
        let mut enc = Encoder::default();
        enc.encode_vvec_with(|enc_inner| {
            enc_inner.encode([0xa5; 65]);
        });
        let v: Vec<u8> = enc.into();
        assert_eq!(&v[..3], &[0x40, 0x41, 0xa5]);
    }

    #[test]
    fn encode_vvec_with_30bit() {
        let mut enc = Encoder::default();
        enc.encode_vvec_with(|enc_inner| {
            enc_inner.encode([0xbe; 16384]); // Just past 14-bit limit
        });
        let v: Vec<u8> = enc.into();
        // 4-byte varint: 0x80004000 for 16384
        assert_eq!(&v[..5], &[0x80, 0x00, 0x40, 0x00, 0xbe]);
    }

    // Test that Deref to &[u8] works for Encoder.
    #[test]
    fn encode_builder() {
        let mut enc = Encoder::from_hex("ff");
        let enc2 = Encoder::from_hex("010234");
        enc.encode(enc2.as_ref());
        assert_eq!(enc, Encoder::from_hex("ff010234"));
    }

    // Test that Deref to &[u8] works for Decoder.
    #[test]
    fn encode_view() {
        let mut enc = Encoder::from_hex("ff");
        let enc2 = Encoder::from_hex("010234");
        let v = enc2.as_decoder();
        enc.encode(v.as_ref());
        assert_eq!(enc, Encoder::from_hex("ff010234"));
    }

    #[test]
    fn encode_mutate() {
        let mut enc = Encoder::from_hex("010234");
        enc.as_mut()[0] = 0xff;
        assert_eq!(enc, Encoder::from_hex("ff0234"));
    }

    #[test]
    fn pad() {
        let mut enc = Encoder::from_hex("010234");
        enc.pad_to(5, 0);
        assert_eq!(enc, Encoder::from_hex("0102340000"));
        enc.pad_to(4, 0);
        assert_eq!(enc, Encoder::from_hex("0102340000"));
        enc.pad_to(7, 0xc2);
        assert_eq!(enc, Encoder::from_hex("0102340000c2c2"));
    }

    #[test]
    fn truncate() {
        let mut enc = Encoder::from_hex("0102030405");
        enc.truncate(3);
        assert_eq!(enc, Encoder::from_hex("010203"));
    }

    #[test]
    fn with_capacity() {
        let mut enc = Encoder::with_capacity(10);
        enc.encode_byte(1);
        assert_eq!(enc.as_ref(), &[1]);
    }

    #[test]
    fn buffer_write_zeroes() {
        fn check_write_zeroes<B: Buffer>(mut buf: B) {
            const NUM_BYTES: usize = 5;

            assert!(buf.is_empty());
            buf.pad_to(NUM_BYTES, 0);
            assert!(!buf.is_empty());

            assert_eq!(buf.position(), NUM_BYTES);
            let written = &buf.as_slice()[..NUM_BYTES];
            assert!(written.iter().all(|&b| b == 0));
        }

        check_write_zeroes(Vec::<u8>::new());

        let mut buf = Vec::<u8>::new();
        check_write_zeroes(&mut buf);

        let mut buf = [0; 16];
        check_write_zeroes(Cursor::new(&mut buf[..]));
    }

    #[test]
    fn buffer_truncate() {
        fn check_truncate<B: Buffer>(mut buf: B) {
            buf.write_all(&[1, 2, 3, 4, 5]).unwrap();
            assert_eq!(buf.position(), 5);
            buf.truncate(3);
            assert_eq!(buf.position(), 3);
            assert_eq!(buf.as_slice(), &[1, 2, 3]);
        }

        check_truncate(Vec::<u8>::new());

        let mut buf = Vec::<u8>::new();
        check_truncate(&mut buf);

        let mut buf = [0; 16];
        check_truncate(Cursor::new(&mut buf[..]));
    }

    #[test]
    fn buffer_rotate_right() {
        fn check_rotate_right<B: Buffer>(mut buf: B) {
            const DATA: [u8; 5] = [1, 2, 3, 4, 5];
            const EXPECTED: [u8; 5] = [1, 4, 5, 2, 3];
            const START: usize = 1;
            const COUNT: usize = 2;

            buf.write_all(&DATA).expect("Buffer has enough capacity.");

            buf.rotate_right(START, COUNT);

            assert_eq!(&buf.as_slice()[..EXPECTED.len()], EXPECTED);
        }

        check_rotate_right(Vec::<u8>::new());

        let mut buf = Vec::<u8>::new();
        check_rotate_right(&mut buf);

        let mut buf = [0; 16];
        check_rotate_right(Cursor::new(&mut buf[..]));
    }

    #[test]
    fn encoder_as_mut() {
        fn check_as_mut<B: Buffer>(mut enc: Encoder<B>) {
            enc.encode_byte(41);
            enc.as_mut()[0] = 42;
            assert_eq!(enc.as_ref(), &[42]);
        }

        check_as_mut(Encoder::default());

        let mut buf = Vec::<u8>::new();
        check_as_mut(Encoder::new_borrowed_vec(&mut buf));

        let mut buf = [0; 16];
        check_as_mut(Encoder::new_borrowed_slice(&mut buf[..]));
    }

    /// When reusing one [`Buffer`] across [`Encoder`]s, [`Buffer::position`]
    /// can be larger than [`Encoder::len`].
    #[test]
    fn buffer_vs_encoder_len() {
        let mut non_empty_vec = vec![1, 2, 3, 4];
        assert_eq!(non_empty_vec.len(), Buffer::position(&non_empty_vec));

        let mut enc = Encoder::new_borrowed_vec(&mut non_empty_vec);
        assert!(enc.is_empty());
        enc.encode_byte(5);
        assert!(!enc.is_empty());
        assert_eq!(enc.len(), 1);

        assert_eq!(non_empty_vec.len(), 5);
        assert_eq!(non_empty_vec.len(), Buffer::position(&non_empty_vec));
    }

    /// [`Buffer::position`] returns the number of bytes written to and not the
    /// length of the underyling buffer.
    ///
    /// When using [`Vec<u8>`] length and position are equal. When using
    /// [`Cursor<&mut [u8]>`] they are not.
    #[test]
    fn buffer_position() {
        let mut a = [0; 16];
        let buf = Cursor::new(&mut a[..]);
        assert_eq!(Buffer::position(&buf), 0);
    }

    #[test]
    fn encoder_skip() {
        let mut enc = Encoder::from_hex("010203040506");

        enc.skip(2);
        assert_eq!(enc.len(), 4);
        assert_eq!(enc.as_ref(), &[0x03, 0x04, 0x05, 0x06]);

        enc.skip(4);
        assert_eq!(enc.len(), 0);
        assert!(enc.is_empty());
    }

    #[test]
    #[should_panic(expected = "Cannot skip beyond buffer length")]
    fn encoder_skip_too_much() {
        let mut enc = Encoder::from_hex("0102");
        enc.skip(3);
    }

    #[test]
    fn encoder_compact() {
        let mut enc = Encoder::from_hex("010203040506");
        let capacity = enc.buf.capacity();
        enc.skip(2);
        enc.compact();
        assert_eq!((enc.start, enc.buf.len()), (2, 6));
        enc.skip(1);
        enc.compact();
        assert_eq!((enc.start, enc.buf.as_slice()), (0, [4, 5, 6].as_slice()));
        enc.skip(3);
        enc.compact();
        assert_eq!((enc.start, enc.buf.len()), (0, 0));
        assert_eq!(enc.buf.capacity(), capacity);
    }

    /// [`Encoder::as_decoder`] should only expose the bytes actively encoded through this
    /// [`Encoder`], not all bytes of the underlying [`Buffer`].
    #[test]
    fn as_decoder_exposes_encoded_bytes_only_not_whole_buffer() {
        let mut buffer = vec![1, 2, 3, 4];
        let mut enc = Encoder::new_borrowed_vec(&mut buffer);
        enc.encode([5, 6, 7]);

        let decoder = enc.as_decoder();
        assert_eq!(decoder.as_ref().len(), 3);
        assert_eq!(decoder.as_ref(), &[5, 6, 7]);
        assert_eq!(buffer, &[1, 2, 3, 4, 5, 6, 7]);
    }

    /// Converting an [`Encoder`] to [`Vec<u8>`] should respect the `start` offset.
    #[test]
    fn into_vec_respects_skip() {
        let mut enc = Encoder::from_hex("010203040506");
        enc.skip(2);
        let v: Vec<u8> = enc.into();
        assert_eq!(v, vec![0x03, 0x04, 0x05, 0x06]);
    }

    /// Converting an [`Encoder`] without skip should return the full buffer.
    #[test]
    fn into_vec_without_skip() {
        let enc = Encoder::from_hex("010203");
        let v: Vec<u8> = enc.into();
        assert_eq!(v, vec![0x01, 0x02, 0x03]);
    }

    #[test]
    fn partial_eq_respects_skip() {
        let mut enc1 = Encoder::from_hex("010203040506");
        enc1.skip(2);
        let enc2 = Encoder::from_hex("03040506");
        assert_eq!(enc1, enc2);
        assert_ne!(enc1, Encoder::from_hex("ffffff"));
    }

    /// [`Clone`] should not clone skipped bytes.
    #[test]
    fn clone_respects_skip() {
        let mut enc = Encoder::from_hex("010203040506");
        enc.skip(2);
        let cloned = enc.clone();
        assert_eq!(cloned.as_ref(), &[0x03, 0x04, 0x05, 0x06]);
        assert_eq!(cloned.len(), 4);
        let v: Vec<u8> = cloned.into();
        assert_eq!(v, vec![0x03, 0x04, 0x05, 0x06]);
    }

    #[test]
    fn encoder_debug() {
        let enc = Encoder::from_hex("010203");
        assert_eq!(format!("{enc:?}"), "[3]: 010203");
    }

    #[test]
    fn decoder_debug() {
        let buf = &[0x01u8, 0x02, 0x03];
        let dec = Decoder::new(buf);
        assert_eq!(format!("{dec:?}"), "[3]: 010203");
    }

    // encode_vec_with with length > 255 exercises the multi-byte length field encoding.
    #[test]
    fn encode_vec_with_large_length() {
        let mut enc = Encoder::default();
        enc.encode_vec_with(2, |enc_inner| {
            enc_inner.encode([0xab; 256]);
        });
        // 2-byte length 0x0100 followed by 256 bytes of 0xab.
        assert_eq!(&enc.as_ref()[..2], &[0x01, 0x00]);
        assert_eq!(enc.as_ref().len(), 258);
    }

    fn encode_vvec_n_bytes(n: usize) -> Vec<u8> {
        let mut enc = Encoder::default();
        enc.encode_vvec_with(|enc_inner| {
            enc_inner.encode(vec![0u8; n]);
        });
        enc.into()
    }

    // 64 bytes exceeds the 6-bit varint range, so the length requires a 2-byte varint.
    #[test]
    fn encode_vvec_with_64_bytes() {
        let v = encode_vvec_n_bytes(64);
        assert_eq!(&v[..2], &[0x40, 0x40]); // 64 → 2-byte varint 0x4040
        assert_eq!(v.len(), 66);
    }

    // 16383 is the last value fitting in a 2-byte varint (0x3FFF).
    #[test]
    fn encode_vvec_with_16383_bytes() {
        let v = encode_vvec_n_bytes(16383);
        assert_eq!(&v[..2], &[0x7f, 0xff]); // 16383 = 0x3FFF → 2-byte varint 0x7FFF
        assert_eq!(v.len(), 16385);
    }

    // Encoder::truncate when skip (start) is non-zero.
    #[test]
    fn truncate_with_skip() {
        let mut enc = Encoder::from_hex("0102030405");
        enc.skip(2);
        enc.truncate(1); // Should keep 1 visible byte (index 2 = 0x03).
        assert_eq!(enc.as_ref(), &[0x03]);
    }

    // Encoder::pad_to when already at the target length (should be a no-op).
    #[test]
    fn pad_to_no_op() {
        let mut enc = Encoder::from_hex("010203");
        enc.pad_to(3, 0xff);
        assert_eq!(enc.as_ref(), &[0x01, 0x02, 0x03]); // Unchanged.
    }

    // with_capacity should not be equivalent to Default (capacity is preserved).
    #[test]
    fn with_capacity_has_capacity() {
        let enc = Encoder::with_capacity(64);
        assert_eq!(enc.len(), 0);
        // Can't directly inspect capacity, but round-trip works.
        let v: Vec<u8> = enc.into();
        assert!(v.capacity() >= 64);
    }

    // From<Encoder> for Vec<u8> with start == 0 (no drain path).
    #[test]
    fn into_vec_no_skip() {
        let enc = Encoder::from_hex("0102");
        let v: Vec<u8> = enc.into();
        assert_eq!(v, &[0x01, 0x02]);
    }

    // Write::write_str coverage.
    #[test]
    fn write_str_coverage() {
        let mut enc = Encoder::default();
        write!(enc, "hi").unwrap();
        assert_eq!(enc.as_ref(), b"hi");
    }

    // Cursor<&mut [u8]> truncate at exactly the current position (no-op branch).
    #[test]
    fn cursor_truncate_no_op() {
        let mut buf = [0u8; 8];
        let mut cur = Cursor::new(&mut buf[..]);
        cur.write_all(&[1, 2, 3]).unwrap();
        assert_eq!(Buffer::position(&cur), 3);
        cur.truncate(3); // At current position — should be a no-op.
        assert_eq!(Buffer::position(&cur), 3);
        assert_eq!(cur.as_slice(), &[1, 2, 3]);
    }

    // Cursor<&mut [u8]> write_at should modify the byte at the given index.
    #[test]
    fn cursor_write_at() {
        let mut buf = [0u8; 8];
        let mut cur = Cursor::new(&mut buf[..]);
        cur.write_all(&[1, 2, 3]).unwrap();
        cur.write_at(1, 0xab);
        assert_eq!(cur.as_slice(), &[1, 0xab, 3]);
    }
}
