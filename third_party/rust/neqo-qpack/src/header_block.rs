// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    fmt::{self, Display, Formatter},
    mem,
    ops::{Deref, Div as _},
};

use neqo_common::Header;
use tracing::trace;

use crate::{
    Error, Res,
    prefix::{
        BASE_PREFIX_NEGATIVE, BASE_PREFIX_POSITIVE, HEADER_FIELD_INDEX_DYNAMIC,
        HEADER_FIELD_INDEX_DYNAMIC_POST, HEADER_FIELD_INDEX_STATIC,
        HEADER_FIELD_LITERAL_NAME_LITERAL, HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC,
        HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC_POST, HEADER_FIELD_LITERAL_NAME_REF_STATIC,
        NO_PREFIX,
    },
    qpack_send_buf::Encoder as _,
    reader::{LiteralReader, ReceiverBufferWrapper, parse_utf8},
    table::{DynamicTableEntry, HeaderTable},
};

#[derive(Default, Debug, PartialEq, Eq)]
pub struct HeaderEncoder {
    buf: neqo_common::Encoder,
    base: u64,
    use_huffman: bool,
    max_entries: u64,
    max_dynamic_index_ref: Option<u64>,
}

impl Display for HeaderEncoder {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "HeaderEncoder")
    }
}

impl HeaderEncoder {
    pub fn new(base: u64, use_huffman: bool, max_entries: u64) -> Self {
        Self {
            buf: neqo_common::Encoder::default(),
            base,
            use_huffman,
            max_entries,
            max_dynamic_index_ref: None,
        }
    }

    pub fn encode_indexed_static(&mut self, index: u64) {
        trace!("[{self}] encode static index {index}");
        self.buf
            .encode_prefixed_encoded_int(HEADER_FIELD_INDEX_STATIC, index);
    }

    const fn new_ref(&mut self, index: u64) {
        if let Some(r) = self.max_dynamic_index_ref {
            if r < index {
                self.max_dynamic_index_ref = Some(index);
            }
        } else {
            self.max_dynamic_index_ref = Some(index);
        }
    }

    pub fn encode_indexed_dynamic(&mut self, index: u64) {
        trace!("[{self}] encode dynamic index {index}");
        if index < self.base {
            self.buf
                .encode_prefixed_encoded_int(HEADER_FIELD_INDEX_DYNAMIC, self.base - index - 1);
        } else {
            self.buf
                .encode_prefixed_encoded_int(HEADER_FIELD_INDEX_DYNAMIC_POST, index - self.base);
        }
        self.new_ref(index);
    }

    pub fn encode_literal_with_name_ref(&mut self, is_static: bool, index: u64, value: &[u8]) {
        trace!(
            "[{self}] encode literal with name ref - index={index}, static={is_static}, value={value:x?}"
        );
        if is_static {
            self.buf
                .encode_prefixed_encoded_int(HEADER_FIELD_LITERAL_NAME_REF_STATIC, index);
        } else if index < self.base {
            self.buf.encode_prefixed_encoded_int(
                HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC,
                self.base - index - 1,
            );
            self.new_ref(index);
        } else {
            self.buf.encode_prefixed_encoded_int(
                HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC_POST,
                index - self.base,
            );
            self.new_ref(index);
        }

        self.buf.encode_literal(self.use_huffman, NO_PREFIX, value);
    }

    pub fn encode_literal_with_name_literal(&mut self, name: &[u8], value: &[u8]) {
        trace!("[{self}] encode literal with name literal - name={name:x?}, value={value:x?}");
        self.buf
            .encode_literal(self.use_huffman, HEADER_FIELD_LITERAL_NAME_LITERAL, name);
        self.buf.encode_literal(self.use_huffman, NO_PREFIX, value);
    }

    pub fn encode_header_block_prefix(&mut self) {
        let tmp = mem::take(&mut self.buf);
        let (enc_insert_cnt, delta, prefix) =
            self.max_dynamic_index_ref
                .map_or((0, self.base, BASE_PREFIX_POSITIVE), |r| {
                    let req_insert_cnt = r + 1;
                    if req_insert_cnt <= self.base {
                        (
                            req_insert_cnt % (2 * self.max_entries) + 1,
                            self.base - req_insert_cnt,
                            BASE_PREFIX_POSITIVE,
                        )
                    } else {
                        (
                            req_insert_cnt % (2 * self.max_entries) + 1,
                            req_insert_cnt - self.base - 1,
                            BASE_PREFIX_NEGATIVE,
                        )
                    }
                });
        trace!(
            "[{self}] encode header block prefix max_dynamic_index_ref={:?}, base={}, enc_insert_cnt={enc_insert_cnt}, delta={delta}, prefix={prefix:?}",
            self.max_dynamic_index_ref, self.base
        );

        self.buf
            .encode_prefixed_encoded_int(NO_PREFIX, enc_insert_cnt);
        self.buf.encode_prefixed_encoded_int(prefix, delta);

        self.buf.encode(tmp);
    }
}

impl Deref for HeaderEncoder {
    type Target = [u8];
    fn deref(&self) -> &Self::Target {
        self.buf.as_ref()
    }
}

pub struct HeaderDecoder<'a> {
    buf: ReceiverBufferWrapper<'a>,
    base: u64,
    req_insert_cnt: u64,
}

impl Display for HeaderDecoder<'_> {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "HeaderDecoder")
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum HeaderDecoderResult {
    Blocked(u64),
    Headers(Vec<Header>),
}

impl<'a> HeaderDecoder<'a> {
    pub const fn new(buf: &'a [u8]) -> Self {
        Self {
            buf: ReceiverBufferWrapper::new(buf),
            base: 0,
            req_insert_cnt: 0,
        }
    }

    pub fn refers_dynamic_table(
        &mut self,
        max_entries: u64,
        total_num_of_inserts: u64,
    ) -> Res<bool> {
        Error::map_error(
            self.read_base(max_entries, total_num_of_inserts),
            Error::Decompression,
        )?;
        Ok(self.req_insert_cnt != 0)
    }

    pub fn decode_header_block(
        &mut self,
        table: &HeaderTable,
        max_entries: u64,
        total_num_of_inserts: u64,
    ) -> Res<HeaderDecoderResult> {
        Error::map_error(
            self.read_base(max_entries, total_num_of_inserts),
            Error::Decompression,
        )?;

        if table.base() < self.req_insert_cnt {
            trace!(
                "[{self}] decoding is blocked, requested inserts count={}",
                self.req_insert_cnt
            );
            return Ok(HeaderDecoderResult::Blocked(self.req_insert_cnt));
        }
        let mut h: Vec<Header> = Vec::new();
        let mut remaining = LiteralReader::MAX_LEN;

        while !self.buf.done() {
            let b = Error::map_error(self.buf.peek(), Error::Decompression)?;
            let header = if HEADER_FIELD_INDEX_STATIC.cmp_prefix(b) {
                Error::map_error(self.read_indexed_static(), Error::Decompression)?
            } else if HEADER_FIELD_INDEX_DYNAMIC.cmp_prefix(b) {
                Error::map_error(self.read_indexed_dynamic(table), Error::Decompression)?
            } else if HEADER_FIELD_INDEX_DYNAMIC_POST.cmp_prefix(b) {
                Error::map_error(self.read_indexed_dynamic_post(table), Error::Decompression)?
            } else if HEADER_FIELD_LITERAL_NAME_REF_STATIC.cmp_prefix(b) {
                Error::map_error(
                    self.read_literal_with_name_ref_static(),
                    Error::Decompression,
                )?
            } else if HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC.cmp_prefix(b) {
                Error::map_error(
                    self.read_literal_with_name_ref_dynamic(table),
                    Error::Decompression,
                )?
            } else if HEADER_FIELD_LITERAL_NAME_LITERAL.cmp_prefix(b) {
                Error::map_error(self.read_literal_with_name_literal(), Error::Decompression)?
            } else if HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC_POST.cmp_prefix(b) {
                Error::map_error(
                    self.read_literal_with_name_ref_dynamic_post(table),
                    Error::Decompression,
                )?
            } else {
                unreachable!("All prefixes are covered");
            };
            remaining = remaining
                .checked_sub(header.field_size())
                .ok_or(Error::Decompression)?;
            h.push(header);
        }

        trace!("[{self}] done decoding header block");
        Ok(HeaderDecoderResult::Headers(h))
    }

    pub const fn get_req_insert_cnt(&self) -> u64 {
        self.req_insert_cnt
    }

    fn read_base(&mut self, max_entries: u64, total_num_of_inserts: u64) -> Res<()> {
        let insert_cnt = self.buf.read_prefixed_int(0)?;
        self.req_insert_cnt =
            HeaderDecoder::calc_req_insert_cnt(insert_cnt, max_entries, total_num_of_inserts)?;

        let s = self.buf.peek()? & 0x80 != 0;
        let base_delta = self.buf.read_prefixed_int(1)?;
        self.base = if s {
            if self.req_insert_cnt <= base_delta {
                return Err(Error::Decompression);
            }
            self.req_insert_cnt - base_delta - 1
        } else {
            self.req_insert_cnt
                .checked_add(base_delta)
                .ok_or(Error::Decompression)?
        };
        trace!(
            "[{self}] requested inserts count is {} and base is {}",
            self.req_insert_cnt, self.base
        );
        Ok(())
    }

    fn calc_req_insert_cnt(encoded: u64, max_entries: u64, total_num_of_inserts: u64) -> Res<u64> {
        if encoded == 0 {
            Ok(0)
        } else if max_entries == 0 {
            Err(Error::Decompression)
        } else {
            let full_range = 2 * max_entries;
            if encoded > full_range {
                return Err(Error::Decompression);
            }
            let max_value = total_num_of_inserts + max_entries;
            let max_wrapped = max_value.div(full_range) * full_range;
            let mut req_insert_cnt = max_wrapped + encoded - 1;
            if req_insert_cnt > max_value {
                if req_insert_cnt < full_range {
                    return Err(Error::Decompression);
                }
                req_insert_cnt -= full_range;
            }
            if req_insert_cnt == 0 {
                return Err(Error::Decompression);
            }
            Ok(req_insert_cnt)
        }
    }

    fn read_indexed_static(&mut self) -> Res<Header> {
        let index = self
            .buf
            .read_prefixed_int(HEADER_FIELD_INDEX_STATIC.len())?;
        trace!("[{self}] decoder static indexed {index}");
        let entry = HeaderTable::get_static(index)?;
        Ok(Header::new(parse_utf8(entry.name())?, entry.value()))
    }

    /// Resolve a dynamic-table reference from a field line representation. A reference
    /// whose absolute index is at or above the Required Insert Count is a decoding error
    /// per RFC 9204, Section 2.2.3, even when the entry is still present in the table
    /// because later insertions arrived. This also covers Section 2.2.1: such a reference
    /// means the declared Required Insert Count was smaller than the block actually needs.
    fn dynamic_entry<'t>(
        &self,
        table: &'t HeaderTable,
        index: u64,
        post: bool,
    ) -> Res<&'t DynamicTableEntry> {
        let entry = table.get_dynamic(index, self.base, post)?;
        if entry.index() >= self.req_insert_cnt {
            return Err(Error::Decompression);
        }
        Ok(entry)
    }

    fn read_indexed_dynamic(&mut self, table: &HeaderTable) -> Res<Header> {
        let index = self
            .buf
            .read_prefixed_int(HEADER_FIELD_INDEX_DYNAMIC.len())?;
        trace!("[{self}] decoder dynamic indexed {index}");
        let entry = self.dynamic_entry(table, index, false)?;
        Ok(Header::new(parse_utf8(entry.name())?, entry.value()))
    }

    fn read_indexed_dynamic_post(&mut self, table: &HeaderTable) -> Res<Header> {
        let index = self
            .buf
            .read_prefixed_int(HEADER_FIELD_INDEX_DYNAMIC_POST.len())?;
        trace!("[{self}] decode post-based {index}");
        let entry = self.dynamic_entry(table, index, true)?;
        Ok(Header::new(parse_utf8(entry.name())?, entry.value()))
    }

    fn read_literal_with_name_ref_static(&mut self) -> Res<Header> {
        trace!("[{self}] read literal with name reference to the static table");

        let index = self
            .buf
            .read_prefixed_int(HEADER_FIELD_LITERAL_NAME_REF_STATIC.len())?;

        Ok(Header::new(
            parse_utf8(HeaderTable::get_static(index)?.name())?,
            self.buf.read_literal_from_buffer(0)?,
        ))
    }

    fn read_literal_with_name_ref_dynamic(&mut self, table: &HeaderTable) -> Res<Header> {
        trace!("[{self}] read literal with name reference of the dynamic table");

        let index = self
            .buf
            .read_prefixed_int(HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC.len())?;

        Ok(Header::new(
            parse_utf8(self.dynamic_entry(table, index, false)?.name())?,
            self.buf.read_literal_from_buffer(0)?,
        ))
    }

    fn read_literal_with_name_ref_dynamic_post(&mut self, table: &HeaderTable) -> Res<Header> {
        trace!("[{self}] decoder literal with post-based index");

        let index = self
            .buf
            .read_prefixed_int(HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC_POST.len())?;

        Ok(Header::new(
            parse_utf8(self.dynamic_entry(table, index, true)?.name())?,
            self.buf.read_literal_from_buffer(0)?,
        ))
    }

    fn read_literal_with_name_literal(&mut self) -> Res<Header> {
        trace!("[{self}] decode literal with name literal");

        let name_bytes = self
            .buf
            .read_literal_from_buffer(HEADER_FIELD_LITERAL_NAME_LITERAL.len())?;

        // Header names must be valid UTF-8
        let name = parse_utf8(&name_bytes)?.to_string();

        Ok(Header::new(name, self.buf.read_literal_from_buffer(0)?))
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {

    use super::{HeaderDecoder, HeaderDecoderResult, HeaderEncoder, HeaderTable, LiteralReader};
    use crate::{Error, table::ADDITIONAL_TABLE_ENTRY_SIZE};

    const INDEX_STATIC_TEST: &[(u64, &[u8], &str, &str)] = &[
        (0, &[0x0, 0x0, 0xc0], ":authority", ""),
        (10, &[0x0, 0x0, 0xca], "last-modified", ""),
        (15, &[0x0, 0x0, 0xcf], ":method", "CONNECT"),
        (65, &[0x0, 0x0, 0xff, 0x02], ":status", "206"),
    ];

    const INDEX_DYNAMIC_TEST: &[(u64, &[u8], &str, &str)] = &[
        (0, &[0x02, 0x41, 0xbf, 0x2], "header0", "0"),
        (10, &[0x0c, 0x37, 0xb7], "header10", "10"),
        (15, &[0x11, 0x32, 0xb2], "header15", "15"),
        (65, &[0x43, 0x0, 0x80], "header65", "65"),
    ];

    const INDEX_DYNAMIC_POST_TEST: &[(u64, &[u8], &str, &str)] = &[
        (0, &[0x02, 0x80, 0x10], "header0", "0"),
        (10, &[0x0c, 0x8a, 0x1a], "header10", "10"),
        (15, &[0x11, 0x8f, 0x1f, 0x00], "header15", "15"),
        (65, &[0x43, 0xc1, 0x1f, 0x32], "header65", "65"),
    ];

    const NAME_REF_STATIC: &[(u64, &[u8], &str, &str)] = &[
        (
            0,
            &[
                0x00, 0x00, 0x50, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            ":authority",
            "custom-key",
        ),
        (
            10,
            &[
                0x00, 0x00, 0x5a, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            "last-modified",
            "custom-key",
        ),
        (
            15,
            &[
                0x00, 0x00, 0x5f, 0x00, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            ":method",
            "custom-key",
        ),
        (
            65,
            &[
                0x00, 0x00, 0x5f, 0x32, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            ":status",
            "custom-key",
        ),
    ];

    const NAME_REF_DYNAMIC: &[(u64, &[u8], &str, &str)] = &[
        (
            0,
            &[
                0x02, 0x41, 0x4f, 0x32, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            "header0",
            "custom-key",
        ),
        (
            10,
            &[
                0x0c, 0x37, 0x4f, 0x28, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            "header10",
            "custom-key",
        ),
        (
            15,
            &[
                0x11, 0x32, 0x4f, 0x23, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            "header15",
            "custom-key",
        ),
        (
            65,
            &[
                0x43, 0x00, 0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            "header65",
            "custom-key",
        ),
    ];

    const NAME_REF_DYNAMIC_POST: &[(u64, &[u8], &str, &str)] = &[
        (
            0,
            &[
                0x02, 0x80, 0x00, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            ],
            "header0",
            "custom-key",
        ),
        (
            10,
            &[
                0x0c, 0x8a, 0x07, 0x03, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            "header10",
            "custom-key",
        ),
        (
            15,
            &[
                0x11, 0x8f, 0x07, 0x08, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            "header15",
            "custom-key",
        ),
        (
            65,
            &[
                0x43, 0xc1, 0x07, 0x3a, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                0x79,
            ],
            "header65",
            "custom-key",
        ),
    ];

    const NAME_REF_DYNAMIC_HUFFMAN: &[(u64, &[u8], &str, &str)] = &[
        (
            0,
            &[
                0x02, 0x41, 0x4f, 0x32, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f,
            ],
            "header0",
            "custom-key",
        ),
        (
            10,
            &[
                0x0c, 0x37, 0x4f, 0x28, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f,
            ],
            "header10",
            "custom-key",
        ),
        (
            15,
            &[
                0x11, 0x32, 0x4f, 0x23, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f,
            ],
            "header15",
            "custom-key",
        ),
        (
            65,
            &[
                0x43, 0x00, 0x40, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f,
            ],
            "header65",
            "custom-key",
        ),
    ];

    const VALUE: &[u8] = &[0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79];

    const LITERAL_LITERAL: &[u8] = &[
        0x0, 0x42, 0x27, 0x03, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79, 0x0a,
        0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
    ];
    const LITERAL_LITERAL_HUFFMAN: &[u8] = &[
        0x0, 0x42, 0x2f, 0x01, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f, 0x88, 0x25, 0xa8,
        0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f,
    ];

    const LITERAL_VALUE: &str = "custom-key";

    #[test]
    fn encode_indexed_static() {
        for (index, result, _, _) in INDEX_STATIC_TEST {
            let mut encoded_h = HeaderEncoder::new(0, true, 1000);
            encoded_h.encode_indexed_static(*index);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }

    #[test]
    fn encode_indexed_dynamic() {
        for (index, result, _, _) in INDEX_DYNAMIC_TEST {
            let mut encoded_h = HeaderEncoder::new(66, true, 1000);
            encoded_h.encode_indexed_dynamic(*index);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }

    #[test]
    fn encode_indexed_dynamic_post() {
        for (index, result, _, _) in INDEX_DYNAMIC_POST_TEST {
            let mut encoded_h = HeaderEncoder::new(0, true, 1000);
            encoded_h.encode_indexed_dynamic(*index);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }

    #[test]
    fn encode_literal_with_name_ref_static() {
        for (index, result, _, _) in NAME_REF_STATIC {
            let mut encoded_h = HeaderEncoder::new(0, false, 1000);
            encoded_h.encode_literal_with_name_ref(true, *index, VALUE);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }

    #[test]
    fn encode_literal_with_name_ref_dynamic() {
        for (index, result, _, _) in NAME_REF_DYNAMIC {
            let mut encoded_h = HeaderEncoder::new(66, false, 1000);
            encoded_h.encode_literal_with_name_ref(false, *index, VALUE);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }

    #[test]
    fn encode_literal_with_name_ref_dynamic_post() {
        for (index, result, _, _) in NAME_REF_DYNAMIC_POST {
            let mut encoded_h = HeaderEncoder::new(0, false, 1000);
            encoded_h.encode_literal_with_name_ref(false, *index, VALUE);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }

    #[test]
    fn encode_literal_with_name_ref_dynamic_huffman() {
        for (index, result, _, _) in NAME_REF_DYNAMIC_HUFFMAN {
            let mut encoded_h = HeaderEncoder::new(66, true, 1000);
            encoded_h.encode_literal_with_name_ref(false, *index, VALUE);
            encoded_h.encode_header_block_prefix();
            assert_eq!(&&*encoded_h, result);
        }
    }
    #[test]
    fn encode_literal_with_literal() {
        let mut encoded_h = HeaderEncoder::new(66, false, 1000);
        encoded_h.encode_literal_with_name_literal(VALUE, VALUE);
        encoded_h.encode_header_block_prefix();
        assert_eq!(&*encoded_h, LITERAL_LITERAL);

        let mut encoded_h = HeaderEncoder::new(66, true, 1000);
        encoded_h.encode_literal_with_name_literal(VALUE, VALUE);
        encoded_h.encode_header_block_prefix();
        assert_eq!(&*encoded_h, LITERAL_LITERAL_HUFFMAN);
    }

    #[test]
    fn decode_indexed_static() {
        for (_, encoded, decoded1, decoded2) in INDEX_STATIC_TEST {
            let table = HeaderTable::new(false);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    fn fill_table(table: &mut HeaderTable) {
        table.set_capacity(10000).unwrap();
        for i in 0..66 {
            let mut v = b"header".to_vec();
            let mut num = i.to_string().as_bytes().to_vec();
            v.append(&mut num);
            table.insert(&v[..], i.to_string().as_bytes()).unwrap();
        }
    }

    #[test]
    fn decode_indexed_dynamic() {
        for (_, encoded, decoded1, decoded2) in INDEX_DYNAMIC_TEST {
            let mut table = HeaderTable::new(false);
            fill_table(&mut table);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    #[test]
    fn decode_indexed_dynamic_post() {
        for (_, encoded, decoded1, decoded2) in INDEX_DYNAMIC_POST_TEST {
            let mut table = HeaderTable::new(false);
            fill_table(&mut table);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    #[test]
    fn decode_literal_with_name_ref_static() {
        for (_, encoded, decoded1, decoded2) in NAME_REF_STATIC {
            let table = HeaderTable::new(false);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    #[test]
    fn decode_literal_with_name_ref_dynamic() {
        for (_, encoded, decoded1, decoded2) in NAME_REF_DYNAMIC {
            let mut table = HeaderTable::new(false);
            fill_table(&mut table);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    #[test]
    fn decode_literal_with_name_ref_dynamic_post() {
        for (_, encoded, decoded1, decoded2) in NAME_REF_DYNAMIC_POST {
            let mut table = HeaderTable::new(false);
            fill_table(&mut table);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    #[test]
    fn decode_literal_with_name_ref_dynamic_huffman() {
        for (_, encoded, decoded1, decoded2) in NAME_REF_DYNAMIC_HUFFMAN {
            let mut table = HeaderTable::new(false);
            fill_table(&mut table);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    #[test]
    fn decode_literal_literal() {
        let mut table = HeaderTable::new(false);
        fill_table(&mut table);
        let mut decoder_h = HeaderDecoder::new(LITERAL_LITERAL);
        if let HeaderDecoderResult::Headers(result) =
            decoder_h.decode_header_block(&table, 1000, 0).unwrap()
        {
            assert_eq!(result.len(), 1);
            assert_eq!(result[0].name(), LITERAL_VALUE);
            assert_eq!(result[0].value(), LITERAL_VALUE.as_bytes());
        } else {
            panic!("No headers");
        }

        let mut decoder_h = HeaderDecoder::new(LITERAL_LITERAL_HUFFMAN);
        if let HeaderDecoderResult::Headers(result) =
            decoder_h.decode_header_block(&table, 1000, 0).unwrap()
        {
            assert_eq!(result.len(), 1);
            assert_eq!(result[0].name(), LITERAL_VALUE);
            assert_eq!(result[0].value(), LITERAL_VALUE.as_bytes());
        } else {
            panic!("No headers");
        }
    }

    #[test]
    fn decode_literal_non_utf8_value() {
        // Test decoding a header with UTF-8 name but non-UTF8 value
        // Based on LITERAL_LITERAL but with non-UTF8 value (0xE4 instead of "custom-key")
        const LITERAL_NON_UTF8_VALUE: &[u8] = &[
            0x0, 0x42, 0x27, 0x03, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
            0x01, // value length = 1
            0xE4, // non-UTF8 byte
        ];

        let table = HeaderTable::new(false);
        let mut decoder_h = HeaderDecoder::new(LITERAL_NON_UTF8_VALUE);
        if let HeaderDecoderResult::Headers(result) =
            decoder_h.decode_header_block(&table, 1000, 0).unwrap()
        {
            assert_eq!(result.len(), 1);
            assert_eq!(result[0].name(), "custom-key");
            assert_eq!(result[0].value(), &[0xE4u8]);
        } else {
            panic!("No headers");
        }
    }

    // Test that we are ignoring N-bit.
    #[test]
    fn decode_ignore_n_bit() {
        const TEST_N_BIT: &[(&[u8], &str, &str)] = &[
            (
                &[
                    0x02, 0x41, 0x6f, 0x32, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b,
                    0x65, 0x79,
                ],
                "header0",
                "custom-key",
            ),
            (
                &[
                    0x02, 0x80, 0x08, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                    0x79,
                ],
                "header0",
                "custom-key",
            ),
            (
                &[
                    0x0, 0x42, 0x37, 0x03, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                    0x79, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
                ],
                "custom-key",
                "custom-key",
            ),
            (
                &[
                    0x0, 0x42, 0x3f, 0x01, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f, 0x88,
                    0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f,
                ],
                "custom-key",
                "custom-key",
            ),
        ];

        for (encoded, decoded1, decoded2) in TEST_N_BIT {
            let mut table = HeaderTable::new(false);
            fill_table(&mut table);
            let mut decoder_h = HeaderDecoder::new(encoded);
            if let HeaderDecoderResult::Headers(result) =
                decoder_h.decode_header_block(&table, 1000, 0).unwrap()
            {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].name(), *decoded1);
                assert_eq!(result[0].value(), decoded2.as_bytes());
            } else {
                panic!("No headers");
            }
        }
    }

    /// If the base calculation goes negative, that is an error.
    #[test]
    fn negative_base() {
        let mut table = HeaderTable::new(false);
        fill_table(&mut table);
        let mut decoder_h = HeaderDecoder::new(&[0x0, 0x87, 0x01, 0x02, 0x03]);
        assert_eq!(
            Error::Decompression,
            decoder_h.decode_header_block(&table, 1000, 0).unwrap_err()
        );
    }

    /// If the base calculation overflows the largest value we support (`u64::MAX`),
    /// then that is an error.
    #[test]
    fn overflow_base() {
        let mut table = HeaderTable::new(false);
        fill_table(&mut table);
        // A small required insert count is necessary, but we can set the
        // base delta to u64::MAX.
        let mut decoder_h = HeaderDecoder::new(&[
            0xff, 0x01, 0x7f, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02,
            0x03,
        ]);
        assert_eq!(
            Error::Decompression,
            decoder_h.decode_header_block(&table, 1000, 0).unwrap_err()
        );
    }

    /// RFC 9204 Section 2.2.3: a field line that references a dynamic table entry
    /// whose absolute index is at or above the declared Required Insert Count is a
    /// decoding error, even when a later insertion has left that entry in the table.
    #[test]
    fn reference_at_or_above_required_insert_count() {
        let mut table = HeaderTable::new(false);
        table.set_capacity(10000).unwrap();
        table.insert(b"header0", b"0").unwrap(); // absolute index 0
        table.insert(b"header1", b"1").unwrap(); // absolute index 1
        assert_eq!(table.base(), 2);

        // Required Insert Count = 1 (only absolute index 0 may be referenced), Base = 2,
        // then an indexed dynamic reference that resolves to absolute index 1.
        let mut decoder_h = HeaderDecoder::new(&[0x02, 0x01, 0x80]);
        assert_eq!(
            Error::Decompression,
            decoder_h.decode_header_block(&table, 1000, 2).unwrap_err()
        );

        // The same reference decodes once the Required Insert Count admits it:
        // Required Insert Count = 2, Base = 2, indexed dynamic reference to index 1.
        let mut decoder_h = HeaderDecoder::new(&[0x03, 0x00, 0x80]);
        if let HeaderDecoderResult::Headers(result) =
            decoder_h.decode_header_block(&table, 1000, 2).unwrap()
        {
            assert_eq!(result.len(), 1);
            assert_eq!(result[0].name(), "header1");
        } else {
            panic!("No headers");
        }
    }

    /// RFC 9204 Section 4.5.1.1: a non-zero Encoded Insert Count that
    /// reconstructs to a Required Insert Count of 0 is a decoding error.
    /// A value of 1 maps to a value of 0, which is invalid unless the
    /// value has wrapped, which this test doesn't cover.
    #[test]
    fn req_insert_count_reconstructed_to_zero() {
        let table = HeaderTable::new(false);
        let mut decoder_h = HeaderDecoder::new(&[0x01, 0x00]);
        assert_eq!(
            Error::Decompression,
            decoder_h.decode_header_block(&table, 1000, 0).unwrap_err()
        );

        // An Encoded Insert Count of 0 legitimately means no dynamic references
        // and still decodes to an empty field section.
        let mut decoder_h = HeaderDecoder::new(&[0x00, 0x00]);
        assert_eq!(
            HeaderDecoderResult::Headers(Vec::new()),
            decoder_h.decode_header_block(&table, 1000, 0).unwrap()
        );
    }

    /// A header block that references the same static table entry many times must be
    /// rejected before exhausting memory.
    #[test]
    fn header_list_size_limit() {
        let table = HeaderTable::new(false);
        // 0xC0 = HEADER_FIELD_INDEX_STATIC with index 0 (:authority, "").
        let entry_size = ":authority".len() + ADDITIONAL_TABLE_ENTRY_SIZE;
        let reps = LiteralReader::MAX_LEN / entry_size + 1;
        // Prefix: required_insert_cnt=0, base_delta=0 (2 bytes: 0x00, 0x00)
        let mut buf = vec![0x00u8, 0x00];
        buf.resize(buf.len() + reps, 0xC0);
        let mut decoder_h = HeaderDecoder::new(&buf);
        assert_eq!(
            Error::Decompression,
            decoder_h.decode_header_block(&table, 1000, 0).unwrap_err()
        );
    }

    /// Truncated input for each header-block prefix type must propagate as
    /// `Error::Decompression`. Each byte sets the maximum value for its prefix's
    /// integer field, forcing a continuation byte that is not present.
    #[test]
    fn decode_truncated_for_each_prefix() {
        const TRUNCATED_PREFIXES: &[u8] = &[
            0xFF, // HEADER_FIELD_INDEX_STATIC
            0xBF, // HEADER_FIELD_INDEX_DYNAMIC
            0x1F, // HEADER_FIELD_INDEX_DYNAMIC_POST
            0x5F, // HEADER_FIELD_LITERAL_NAME_REF_STATIC
            0x4F, // HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC
            0x3F, // HEADER_FIELD_LITERAL_NAME_LITERAL
            0x07, // HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC_POST
        ];
        let mut table = HeaderTable::new(false);
        fill_table(&mut table);
        for &prefix in TRUNCATED_PREFIXES {
            let buf = [0x00, 0x00, prefix];
            let mut decoder_h = HeaderDecoder::new(&buf);
            assert_eq!(
                Error::Decompression,
                decoder_h.decode_header_block(&table, 1000, 0).unwrap_err(),
                "prefix {prefix:#04x}"
            );
        }
    }
}
