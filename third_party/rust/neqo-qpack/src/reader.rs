// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cmp::min, mem};

use neqo_transport::{Connection, StreamId};
use tracing::debug;

use crate::{Error, Res, huffman, prefix::Prefix};

const MAX_READ_SIZE: usize = 2048;

pub trait ReadByte {
    /// # Errors
    ///
    /// Return error occurred while reading a byte.
    /// The exact error depends on trait implementation.
    fn read_byte(&mut self) -> Res<u8>;
}

pub trait Reader {
    /// # Errors
    ///
    /// Return error occurred while reading date into a buffer.
    /// The exact error depends on trait implementation.
    fn read(&mut self, buf: &mut [u8]) -> Res<usize>;
}

pub(crate) struct ReceiverConnWrapper<'a> {
    conn: &'a mut Connection,
    stream_id: StreamId,
}

impl ReadByte for ReceiverConnWrapper<'_> {
    fn read_byte(&mut self) -> Res<u8> {
        let mut b = [0];
        match self.conn.stream_recv(self.stream_id, &mut b)? {
            (_, true) => Err(Error::ClosedCriticalStream),
            (0, false) => Err(Error::NeedMoreData),
            _ => Ok(b[0]),
        }
    }
}

impl Reader for ReceiverConnWrapper<'_> {
    fn read(&mut self, buf: &mut [u8]) -> Res<usize> {
        match self.conn.stream_recv(self.stream_id, buf)? {
            (_, true) => Err(Error::ClosedCriticalStream),
            (amount, false) => Ok(amount),
        }
    }
}

impl<'a> ReceiverConnWrapper<'a> {
    pub const fn new(conn: &'a mut Connection, stream_id: StreamId) -> Self {
        Self { conn, stream_id }
    }
}

/// This is only used by header decoder therefore all errors are `Error::Decompression`.
/// A header block is read entirely before decoding it, therefore if there is not enough
/// data in the buffer an error `Error::Decompression` will be return.
pub(crate) struct ReceiverBufferWrapper<'a> {
    buf: &'a [u8],
    offset: usize,
}

impl ReadByte for ReceiverBufferWrapper<'_> {
    fn read_byte(&mut self) -> Res<u8> {
        if self.offset == self.buf.len() {
            Err(Error::Decompression)
        } else {
            let b = self.buf[self.offset];
            self.offset += 1;
            Ok(b)
        }
    }
}

impl<'a> ReceiverBufferWrapper<'a> {
    pub const fn new(buf: &'a [u8]) -> Self {
        Self { buf, offset: 0 }
    }

    pub const fn peek(&self) -> Res<u8> {
        if self.offset == self.buf.len() {
            Err(Error::Decompression)
        } else {
            Ok(self.buf[self.offset])
        }
    }

    pub const fn done(&self) -> bool {
        self.offset == self.buf.len()
    }

    /// The function decodes varint with a prefixed, i.e. ignores `prefix_len` bits of the first
    /// byte.
    /// `ReceiverBufferWrapper` is only used for decoding header blocks. The header blocks are read
    /// entirely before a decoding starts, therefore any incomplete varint because of reaching the
    /// end of a buffer will be treated as the `Error::Decompression` error.
    pub fn read_prefixed_int(&mut self, prefix_len: u8) -> Res<u64> {
        debug_assert!(prefix_len < 8);

        let first_byte = self.read_byte()?;
        let mut reader = IntReader::new(first_byte, prefix_len);
        reader.read(self)
    }

    /// Do not use `LiteralReader` here to avoid copying data.
    /// The function decoded a literal with a prefix:
    ///   1) ignores `prefix_len` bits of the first byte,
    ///   2) reads "huffman bit"
    ///   3) decode varint that is the length of a literal
    ///   4) reads the literal
    ///   5) performs huffman decoding if needed.
    ///
    /// `ReceiverBufferWrapper` is only used for decoding header blocks. The header blocks are read
    /// entirely before a decoding starts, therefore any incomplete varint or literal because of
    /// reaching the end of a buffer will be treated as the `Error::Decompression` error.
    pub fn read_literal_from_buffer(&mut self, prefix_len: u8) -> Res<Vec<u8>> {
        debug_assert!(prefix_len < 7);

        let first_byte = self.read_byte()?;
        let use_huffman = (first_byte & (0x80 >> prefix_len)) != 0;
        let mut int_reader = IntReader::new(first_byte, prefix_len + 1);
        let length: usize = int_reader
            .read(self)?
            .try_into()
            .ok()
            .filter(|&l| l <= LiteralReader::MAX_LEN)
            .ok_or(Error::Decompression)?;
        if use_huffman {
            huffman::decode(self.slice(length)?)
        } else {
            Ok(self.slice(length)?.to_vec())
        }
    }

    fn slice(&mut self, len: usize) -> Res<&[u8]> {
        let end = self.offset.checked_add(len).ok_or(Error::Decompression)?;
        if end > self.buf.len() {
            Err(Error::Decompression)
        } else {
            let start = self.offset;
            self.offset = end;
            Ok(&self.buf[start..self.offset])
        }
    }
}

/// This is varint reader that can take into account a prefix.
#[derive(Debug)]
#[expect(clippy::module_name_repetitions, reason = "This is OK.")]
pub struct IntReader {
    value: u64,
    cnt: u8,
    done: bool,
}

impl IntReader {
    /// `IntReader` is created by supplying the first byte and prefix length.
    /// A varint may take only one byte, In that case already the first by has set state to done.
    ///
    /// # Panics
    ///
    /// When `prefix_len` is 8 or larger.
    #[must_use]
    pub fn new(first_byte: u8, prefix_len: u8) -> Self {
        debug_assert!(prefix_len < 8, "prefix cannot larger than 7");
        let mask = if prefix_len == 0 {
            0xff
        } else {
            (1 << (8 - prefix_len)) - 1
        };
        let value = u64::from(first_byte & mask);

        Self {
            value,
            cnt: 0,
            done: value < u64::from(mask),
        }
    }

    /// # Panics
    ///
    /// Never, but rust doesn't know that.
    #[must_use]
    pub fn make(first_byte: u8, prefixes: &[Prefix]) -> Self {
        for prefix in prefixes {
            if prefix.cmp_prefix(first_byte) {
                return Self::new(first_byte, prefix.len());
            }
        }
        unreachable!();
    }

    /// This function reads bytes until the varint is decoded or until stream/buffer does not
    /// have any more date.
    ///
    /// # Errors
    ///
    /// Possible errors are:
    ///  1) `NeedMoreData` if the reader needs more data,
    ///  2) `IntegerOverflow`,
    ///  3) Any `ReadByte`'s error
    pub fn read<R: ReadByte>(&mut self, s: &mut R) -> Res<u64> {
        let mut b: u8;
        while !self.done {
            b = s.read_byte()?;

            if (self.cnt == 63) && (b > 1 || (b == 1 && ((self.value >> 63) == 1))) {
                debug!("Error decoding prefixed encoded int - IntegerOverflow");
                return Err(Error::IntegerOverflow);
            }
            self.value += u64::from(b & 0x7f) << self.cnt;
            if (b & 0x80) == 0 {
                self.done = true;
            }
            self.cnt += 7;
            if self.cnt >= 64 {
                self.done = true;
            }
        }
        Ok(self.value)
    }
}

#[derive(Debug, Default)]
enum LiteralReaderState {
    #[default]
    ReadHuffman,
    ReadLength {
        reader: IntReader,
    },
    ReadLiteral {
        len: usize,
        offset: usize,
    },
    Done,
}

/// This is decoder of a literal with a prefix:
///   1) ignores `prefix_len` bits of the first byte,
///   2) reads "huffman bit"
///   3) decode varint that is the length of a literal
///   4) reads the literal
///   5) performs huffman decoding if needed.
#[derive(Debug, Default)]
#[expect(clippy::module_name_repetitions, reason = "This is OK.")]
pub struct LiteralReader {
    state: LiteralReaderState,
    literal: Vec<u8>,
    use_huffman: bool,
}

impl LiteralReader {
    /// Maximum length for a literal string in QPACK encoding.
    ///
    /// RFC 9204 requires implementations to set their own limits for string literal
    /// lengths to prevent denial-of-service attacks. The RFC does not mandate a
    /// specific value, stating only that limits "SHOULD be large enough to process
    /// the largest individual field the HTTP implementation can be configured to
    /// accept."
    ///
    /// The Gecko limit is in `network.http.max_response_header_size` and defaults to
    /// 393216 bytes (384 KB), see `modules/libpref/init/StaticPrefList.yaml`. We use
    /// the same limit.
    ///
    /// Also reused by `neqo-http3` (`hframe::MAX_HEADER_BYTES`).
    pub const MAX_LEN: usize = 384 * 1024;

    /// Creates `LiteralReader` with the first byte. This constructor is always used
    /// when a literal has a prefix.
    /// For literals without a prefix please use the default constructor.
    ///
    /// # Panics
    ///
    /// If `prefix_len` is 8 or more.
    #[must_use]
    pub fn new_with_first_byte(first_byte: u8, prefix_len: u8) -> Self {
        assert!(prefix_len < 8);
        Self {
            state: LiteralReaderState::ReadLength {
                reader: IntReader::new(first_byte, prefix_len + 1),
            },
            literal: Vec::new(),
            use_huffman: (first_byte & (0x80 >> prefix_len)) != 0,
        }
    }

    /// This function reads bytes until the literal is decoded or until stream/buffer does not
    /// have any more date ready.
    ///
    /// # Errors
    ///
    /// Possible errors are:
    ///  1) `NeedMoreData` if the reader needs more data,
    ///  2) `IntegerOverflow`
    ///  3) Any `ReadByte`'s error
    ///
    /// It returns value if reading the literal is done or None if it needs more data.
    ///
    /// # Panics
    ///
    /// When this object is complete.
    pub fn read<T: ReadByte + Reader>(&mut self, s: &mut T) -> Res<Vec<u8>> {
        loop {
            debug!("state = {:?}", self.state);
            match &mut self.state {
                LiteralReaderState::ReadHuffman => {
                    let b = s.read_byte()?;

                    self.use_huffman = (b & 0x80) != 0;
                    self.state = LiteralReaderState::ReadLength {
                        reader: IntReader::new(b, 1),
                    };
                }
                LiteralReaderState::ReadLength { reader } => {
                    // TODO: Reject literals larger than the table capacity (RFC 9204 §3.2.2).
                    let v = usize::try_from(reader.read(s)?)
                        .ok()
                        .filter(|&l| l <= Self::MAX_LEN)
                        .ok_or(Error::Decoding)?;
                    self.state = LiteralReaderState::ReadLiteral { len: v, offset: 0 };
                }
                LiteralReaderState::ReadLiteral { len, offset } => {
                    if *offset == self.literal.len() {
                        let to_read = min(*len - *offset, MAX_READ_SIZE);
                        self.literal.reserve_exact(to_read);
                        self.literal.resize(*offset + to_read, 0);
                    }
                    let amount = s.read(&mut self.literal[*offset..])?;
                    *offset += amount;
                    if *offset == *len {
                        self.state = LiteralReaderState::Done;
                        if self.use_huffman {
                            break Ok(huffman::decode(&self.literal)?);
                        }
                        break Ok(mem::take(&mut self.literal));
                    }
                    if amount == 0 {
                        break Err(Error::NeedMoreData);
                    }
                }
                LiteralReaderState::Done => {
                    panic!("Should not call read() in this state");
                }
            }
        }
    }
}

/// This is a helper function used only by `ReceiverBufferWrapper`, therefore it returns
/// `Error::Decompression` if any error happens.
///
/// # Errors
///
/// If an parsing error occurred, the function returns `BadUtf8`.
pub fn parse_utf8(v: &[u8]) -> Res<&str> {
    str::from_utf8(v).map_err(|_| Error::BadUtf8)
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
pub(crate) mod test_receiver {

    use std::collections::VecDeque;

    use super::{Error, ReadByte, Reader, Res};

    #[derive(Default)]
    pub struct TestReceiver {
        buf: VecDeque<u8>,
    }

    impl ReadByte for TestReceiver {
        fn read_byte(&mut self) -> Res<u8> {
            self.buf.pop_back().ok_or(Error::NeedMoreData)
        }
    }

    impl Reader for TestReceiver {
        fn read(&mut self, buf: &mut [u8]) -> Res<usize> {
            let len = if buf.len() > self.buf.len() {
                self.buf.len()
            } else {
                buf.len()
            };
            for item in buf.iter_mut().take(len) {
                *item = self.buf.pop_back().ok_or(Error::NeedMoreData)?;
            }
            Ok(len)
        }
    }

    impl TestReceiver {
        pub fn write(&mut self, buf: &[u8]) {
            for b in buf {
                self.buf.push_front(*b);
            }
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {

    use neqo_common::{Encoder, to_u64};
    use test_receiver::TestReceiver;

    use super::{
        Error, IntReader, LiteralReader, MAX_READ_SIZE, ReadByte as _, ReceiverBufferWrapper, Res,
        huffman, test_receiver,
    };
    use crate::{prefix::Prefix, qpack_send_buf::Encoder as _};

    const TEST_CASES_NUMBERS: [(&[u8], u8, u64); 7] = [
        (&[0xEA], 3, 10),
        (&[0x0A], 3, 10),
        (&[0x8A], 3, 10),
        (&[0xFF, 0x9A, 0x0A], 3, 1337),
        (&[0x1F, 0x9A, 0x0A], 3, 1337),
        (&[0x9F, 0x9A, 0x0A], 3, 1337),
        (&[0x2A], 0, 42),
    ];

    #[test]
    fn read_prefixed_int() {
        for (buf, prefix_len, value) in &TEST_CASES_NUMBERS {
            let mut reader = IntReader::new(buf[0], *prefix_len);
            let mut test_receiver: TestReceiver = TestReceiver::default();
            test_receiver.write(&buf[1..]);
            assert_eq!(reader.read(&mut test_receiver), Ok(*value));
        }
    }

    #[test]
    fn read_prefixed_int_with_more_data_in_buffer() {
        for (buf, prefix_len, value) in &TEST_CASES_NUMBERS {
            let mut reader = IntReader::new(buf[0], *prefix_len);
            let mut test_receiver: TestReceiver = TestReceiver::default();
            test_receiver.write(&buf[1..]);
            // add some more data
            test_receiver.write(&[0x0, 0x0, 0x0]);
            assert_eq!(reader.read(&mut test_receiver), Ok(*value));
        }
    }

    #[test]
    fn read_prefixed_int_slow_writer() {
        let (buf, prefix_len, value) = &TEST_CASES_NUMBERS[4];
        assert!(buf.len() > 1);
        let mut reader = IntReader::new(buf[0], *prefix_len);
        let mut test_receiver: TestReceiver = TestReceiver::default();

        // data has not been received yet, reading IntReader will return Err(Error::NeedMoreData).
        assert_eq!(reader.read(&mut test_receiver), Err(Error::NeedMoreData));

        // Write one byte.
        test_receiver.write(&buf[1..2]);
        // data has not been received yet, reading IntReader will return Err(Error::NeedMoreData).
        assert_eq!(reader.read(&mut test_receiver), Err(Error::NeedMoreData));

        // Write one byte.
        test_receiver.write(&buf[2..]);
        // Now prefixed int is complete.
        assert_eq!(reader.read(&mut test_receiver), Ok(*value));
    }

    type TestSetup = (&'static [u8], u8, Res<u64>);
    const TEST_CASES_BIG_NUMBERS: &[TestSetup] = &[
        // (1 << 64) - 1 is fine
        (
            &[
                0xFF, 0x80, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
            ],
            0,
            Ok(0xFFFF_FFFF_FFFF_FFFF),
        ),
        // 1 << 64 is too big
        (
            &[
                0xFF, 0x81, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
            ],
            0,
            Err(Error::IntegerOverflow),
        ),
        // Adding 1 << 64 is no good, no matter what else is there
        (
            &[
                0xFF, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02,
            ],
            0,
            Err(Error::IntegerOverflow),
        ),
        // 1 << 63
        (
            &[0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F],
            7,
            Ok(0x8000_0000_0000_0000),
        ),
        // 1 << 63 with extra zero values
        (
            &[
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
            ],
            7,
            Ok(0x8000_0000_0000_0000),
        ),
        // 1 << 64 with prefix 7
        (
            &[
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
            ],
            7,
            Err(Error::IntegerOverflow),
        ),
        // Adding 1 << 64 is no good, no matter what else is there
        (
            &[
                0xFF, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02,
            ],
            7,
            Err(Error::IntegerOverflow),
        ),
    ];

    #[test]
    fn read_prefixed_int_big_number() {
        for (buf, prefix_len, value) in TEST_CASES_BIG_NUMBERS {
            let mut reader = IntReader::new(buf[0], *prefix_len);
            let mut test_receiver: TestReceiver = TestReceiver::default();
            test_receiver.write(&buf[1..]);
            assert_eq!(
                reader.read(&mut test_receiver),
                *value,
                "p{prefix_len}: {buf:x?}"
            );
        }
    }

    const TEST_CASES_LITERAL: [(&[u8], u8, &[u8]); 9] = [
        // No Huffman
        (
            &[
                0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            1,
            b"custom-key",
        ),
        (
            &[
                0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            3,
            b"custom-key",
        ),
        (
            &[
                0xea, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            3,
            b"custom-key",
        ),
        (
            &[
                0x0d, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72,
            ],
            1,
            b"custom-header",
        ),
        // With Huffman
        (&[0x15, 0xae, 0xc3, 0x77, 0x1a, 0x4b], 3, b"private"),
        (
            &[
                0x56, 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04,
                0x0b, 0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff,
            ],
            1,
            b"Mon, 21 Oct 2013 20:13:21 GMT",
        ),
        (
            &[
                0xff, 0x0f, 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95,
                0x04, 0x0b, 0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff,
            ],
            4,
            b"Mon, 21 Oct 2013 20:13:21 GMT",
        ),
        (
            &[
                0x51, 0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 0xe9, 0xae,
                0x82, 0xae, 0x43, 0xd3,
            ],
            1,
            b"https://www.example.com",
        ),
        (
            &[
                0x91, 0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 0xe9, 0xae,
                0x82, 0xae, 0x43, 0xd3,
            ],
            0,
            b"https://www.example.com",
        ),
    ];

    #[test]
    fn read_literal() {
        for (buf, prefix_len, value) in &TEST_CASES_LITERAL {
            let mut reader = LiteralReader::new_with_first_byte(buf[0], *prefix_len);
            let mut test_receiver: TestReceiver = TestReceiver::default();
            test_receiver.write(&buf[1..]);
            assert_eq!(reader.read(&mut test_receiver).unwrap().as_slice(), *value);
        }
    }

    #[test]
    fn read_prefixed_int_receiver_buffer_wrapper() {
        for (buf, prefix_len, value) in &TEST_CASES_NUMBERS {
            let mut buffer = ReceiverBufferWrapper::new(buf);
            let mut reader = IntReader::new(buffer.read_byte().unwrap(), *prefix_len);
            assert_eq!(reader.read(&mut buffer), Ok(*value));
        }
    }

    #[test]
    fn read_prefixed_int_big_receiver_buffer_wrapper() {
        for (buf, prefix_len, value) in TEST_CASES_BIG_NUMBERS {
            let mut buffer = ReceiverBufferWrapper::new(buf);
            let mut reader = IntReader::new(buffer.read_byte().unwrap(), *prefix_len);
            assert_eq!(reader.read(&mut buffer), *value);
        }
    }

    #[test]
    fn read_literal_receiver_buffer_wrapper() {
        for (buf, prefix_len, value) in &TEST_CASES_LITERAL {
            let mut buffer = ReceiverBufferWrapper::new(buf);
            assert_eq!(
                buffer.read_literal_from_buffer(*prefix_len).unwrap(),
                *value
            );
        }
    }

    #[test]
    fn read_failure_receiver_buffer_wrapper_number() {
        let (buf, prefix_len, _) = &TEST_CASES_NUMBERS[4];
        let mut buffer = ReceiverBufferWrapper::new(&buf[..1]);
        let mut reader = IntReader::new(buffer.read_byte().unwrap(), *prefix_len);
        assert_eq!(reader.read(&mut buffer), Err(Error::Decompression));
    }

    #[test]
    fn read_failure_receiver_buffer_wrapper_literal() {
        let (buf, prefix_len, _) = &TEST_CASES_LITERAL[0];
        let mut buffer = ReceiverBufferWrapper::new(&buf[..6]);
        assert_eq!(
            buffer.read_literal_from_buffer(*prefix_len),
            Err(Error::Decompression)
        );
    }

    #[test]
    fn read_non_utf8_huffman_literal() {
        // Test non-UTF8 data with Huffman encoding
        // 0xE4 is 'ä' in ISO-8859-1 (extended ASCII), which is invalid UTF-8
        let non_utf8_data = &[0xE4u8];
        let encoded = huffman::encode(non_utf8_data);

        // Build a QPACK literal: [huffman_bit | length][data]
        // For prefix_len=3, the huffman bit is at position (0x80 >> 3) = 0x10
        let mut buf = Vec::new();
        #[expect(clippy::cast_possible_truncation, reason = "Test data is small")]
        let len = encoded.len() as u8;
        buf.push(0x10 | len); // Huffman bit set + length
        buf.extend_from_slice(&encoded);

        let mut buffer = ReceiverBufferWrapper::new(&buf);
        let result = buffer.read_literal_from_buffer(3).unwrap();
        assert_eq!(result, non_utf8_data);
    }

    #[test]
    fn read_non_utf8_plain_literal() {
        // Test non-UTF8 data without Huffman encoding
        // 0xFF, 0xFE are invalid UTF-8 sequences
        let non_utf8_data = &[0xFFu8, 0xFEu8];

        // Build a QPACK literal without Huffman: [length][data]
        // For prefix_len=3, no huffman bit
        let mut buf = Vec::new();
        #[expect(clippy::cast_possible_truncation, reason = "Test data is small")]
        let len = non_utf8_data.len() as u8;
        buf.push(len); // No Huffman bit, just length
        buf.extend_from_slice(non_utf8_data);

        let mut buffer = ReceiverBufferWrapper::new(&buf);
        let result = buffer.read_literal_from_buffer(3).unwrap();
        assert_eq!(result, non_utf8_data);
    }

    /// Create a [`LiteralReader`] and [`TestReceiver`] for a literal with the given length.
    fn literal_reader_for_test(literal_len: usize) -> (LiteralReader, TestReceiver) {
        const PREFIX_LEN: u8 = 3;
        let mut data = Encoder::default();
        data.encode_literal(
            false,
            Prefix::new(0x00, PREFIX_LEN),
            &vec![b'a'; literal_len],
        );
        let reader = LiteralReader::new_with_first_byte(data.as_ref()[0], PREFIX_LEN);
        let mut test_receiver = TestReceiver::default();
        test_receiver.write(&data.as_ref()[1..]);
        (reader, test_receiver)
    }

    /// Test that [`LiteralReader`] rejects literals exceeding [`MAX_LEN`].
    ///
    /// This prevents denial-of-service attacks where a malicious QPACK encoder
    /// sends an extremely large length value to trigger excessive memory allocation.
    /// RFC 9204 requires implementations to set their own limits for string literal
    /// lengths.
    #[test]
    fn literal_exceeding_max_len_rejected() {
        let (mut reader, mut test_receiver) = literal_reader_for_test(LiteralReader::MAX_LEN + 1);
        assert_eq!(reader.read(&mut test_receiver), Err(Error::Decoding));
    }

    /// Test that [`LiteralReader`] accepts literals at exactly [`MAX_LEN`].
    #[test]
    fn literal_at_max_len_accepted() {
        let (mut reader, mut test_receiver) = literal_reader_for_test(LiteralReader::MAX_LEN);
        let result = reader.read(&mut test_receiver).unwrap();
        assert_eq!(result.len(), LiteralReader::MAX_LEN);
    }

    #[test]
    fn literal_length_alone_allocates_one_chunk() {
        const PREFIX_LEN: u8 = 3;
        let mut data = Encoder::default();
        data.encode_prefixed_encoded_int(
            Prefix::new(0x00, PREFIX_LEN + 1),
            to_u64(LiteralReader::MAX_LEN),
        );
        let mut reader = LiteralReader::new_with_first_byte(data.as_ref()[0], PREFIX_LEN);
        let mut test_receiver = TestReceiver::default();
        test_receiver.write(&data.as_ref()[1..]);
        assert_eq!(reader.read(&mut test_receiver), Err(Error::NeedMoreData));
        assert_eq!(reader.literal.capacity(), MAX_READ_SIZE);
        test_receiver.write(&[0; 2 * MAX_READ_SIZE]);
        assert_eq!(reader.read(&mut test_receiver), Err(Error::NeedMoreData));
        assert_eq!(reader.literal.capacity(), 3 * MAX_READ_SIZE);
    }

    #[test]
    fn buffer_wrapper_rejects_oversized_literal() {
        const PREFIX_LEN: u8 = 3;
        // Encode only the length field (MAX_LEN + 1) without allocating the actual data.
        // The validation should fail before attempting to read the literal content.
        let mut data = Encoder::default();
        data.encode_prefixed_encoded_int(
            Prefix::new(0x00, PREFIX_LEN + 1),
            to_u64(LiteralReader::MAX_LEN + 1),
        );
        let mut buffer = ReceiverBufferWrapper::new(data.as_ref());
        assert_eq!(
            buffer.read_literal_from_buffer(PREFIX_LEN),
            Err(Error::Decompression)
        );
    }

    #[test]
    fn buffer_wrapper_slice_detects_overflow() {
        let buf = [0u8; 10];
        let mut wrapper = ReceiverBufferWrapper::new(&buf);
        wrapper.offset = 5;
        assert_eq!(wrapper.slice(7), Err(Error::Decompression));
        assert_eq!(wrapper.slice(usize::MAX), Err(Error::Decompression));
    }
}
