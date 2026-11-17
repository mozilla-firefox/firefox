// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use crate::{
    Error, Res,
    huffman_decode_helper::{HUFFMAN_DECODER_ROOT, HuffmanDecoderNode},
    huffman_table::HUFFMAN_TABLE,
};

struct BitReader<'a> {
    input: &'a [u8],
    offset: usize,
    current_bit: u8,
}

impl<'a> BitReader<'a> {
    pub const fn new(input: &'a [u8]) -> Self {
        BitReader {
            input,
            offset: 0,
            current_bit: 8,
        }
    }

    pub const fn read_bit(&mut self) -> Res<u8> {
        if self.input.len() == self.offset {
            return Err(Error::NeedMoreData);
        }

        if self.current_bit == 0 {
            self.offset += 1;
            if self.offset == self.input.len() {
                return Err(Error::NeedMoreData);
            }
            self.current_bit = 8;
        }
        self.current_bit -= 1;
        Ok((self.input[self.offset] >> self.current_bit) & 0x01)
    }

    pub const fn verify_ending(&mut self, i: u8) -> Res<()> {
        if (i + self.current_bit) > 7 {
            return Err(Error::HuffmanDecompression);
        }

        if self.input.is_empty() {
            Ok(())
        } else if self.offset != self.input.len() {
            Err(Error::HuffmanDecompression)
        } else if self.input[self.input.len() - 1] & ((0x1 << (i + self.current_bit)) - 1)
            == ((0x1 << (i + self.current_bit)) - 1)
        {
            self.current_bit = 0;
            Ok(())
        } else {
            Err(Error::HuffmanDecompression)
        }
    }

    pub const fn has_more_data(&self) -> bool {
        !self.input.is_empty() && (self.offset != self.input.len() || (self.current_bit != 0))
    }
}

/// Decodes huffman encoded input.
///
/// # Errors
///
/// This function may return `Error::HuffmanDecompression` if `input` is not a correct
/// huffman-encoded array of bits.
///
/// # Panics
///
/// Never, but rust can't know that.
pub fn decode(input: &[u8]) -> Res<Vec<u8>> {
    let mut reader = BitReader::new(input);
    let mut output = Vec::with_capacity(input.len() * 2); // Huffman typically expands, so start with a reasonable guestimate.
    while reader.has_more_data() {
        if let Some(c) = decode_character(&mut reader)? {
            output.push(u8::try_from(c).map_err(|_| Error::HuffmanDecompression)?);
        }
    }

    Ok(output)
}

fn decode_character(reader: &mut BitReader) -> Res<Option<u16>> {
    let mut node: &HuffmanDecoderNode = &HUFFMAN_DECODER_ROOT;
    let mut i = 0;
    while node.value.is_none() {
        match reader.read_bit() {
            Err(_) => {
                reader.verify_ending(i)?;
                return Ok(None);
            }
            Ok(b) => {
                i += 1;
                if let Some(next) = &node.next[usize::from(b)] {
                    node = next;
                } else {
                    reader.verify_ending(i)?;
                    return Ok(None);
                }
            }
        }
    }
    debug_assert!(node.value.is_some());
    Ok(node.value)
}

/// # Panics
///
/// Never, but rust doesn't know that.
#[must_use]
pub fn encode(input: &[u8]) -> Vec<u8> {
    let mut output: Vec<u8> = Vec::with_capacity(input.len()); // Huffman compresses, so this should be big enough to not reallocate.
    let mut left: u8 = 8;
    let mut saved: u8 = 0;
    for c in input {
        let mut e = HUFFMAN_TABLE[usize::from(*c)];

        // Fill the previous byte
        if e.len < left {
            let b = (e.val & 0xFF) as u8; // Safe due to the mask.
            saved |= b << (left - e.len);
            left -= e.len;
            e.len = 0;
        } else {
            let v: u8 = u8::try_from(e.val >> (e.len - left)).expect("fits into u8");
            saved |= v;
            output.push(saved);
            e.len -= left;
            left = 8;
            saved = 0;
        }

        // Write full bytes
        while e.len >= 8 {
            let v: u8 = ((e.val >> (e.len - 8)) & 0xFF) as u8; // Safe due to the mask.
            output.push(v);
            e.len -= 8;
        }

        // Write the rest into saved.
        if e.len > 0 {
            saved = u8::try_from(e.val & ((1 << e.len) - 1)).expect("fits into u8") << (8 - e.len);
            left = 8 - e.len;
        }
    }

    if left < 8 {
        let v: u8 = (1 << left) - 1;
        saved |= v;
        output.push(saved);
    }

    output
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::{Error, decode, encode};

    struct TestElement {
        pub val: &'static [u8],
        pub res: &'static [u8],
    }
    const TEST_CASES: &[TestElement] = &[
        TestElement {
            val: b"www.example.com",
            res: &[
                0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff,
            ],
        },
        TestElement {
            val: b"no-cache",
            res: &[0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf],
        },
        TestElement {
            val: b"custom-key",
            res: &[0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f],
        },
        TestElement {
            val: b"custom-value",
            res: &[0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf],
        },
        TestElement {
            val: b"private",
            res: &[0xae, 0xc3, 0x77, 0x1a, 0x4b],
        },
        TestElement {
            val: b"Mon, 21 Oct 2013 20:13:21 GMT",
            res: &[
                0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b,
                0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff,
            ],
        },
        TestElement {
            val: b"https://www.example.com",
            res: &[
                0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 0xe9, 0xae, 0x82,
                0xae, 0x43, 0xd3,
            ],
        },
        TestElement {
            val: b"Mon, 21 Oct 2013 20:13:22 GMT",
            res: &[
                0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b,
                0x81, 0x66, 0xe0, 0x84, 0xa6, 0x2d, 0x1b, 0xff,
            ],
        },
        TestElement {
            val: b"gzip",
            res: &[0x9b, 0xd9, 0xab],
        },
        TestElement {
            val: b"foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
            res: &[
                0x94, 0xe7, 0x82, 0x1d, 0xd7, 0xf2, 0xe6, 0xc7, 0xb3, 0x35, 0xdf, 0xdf, 0xcd, 0x5b,
                0x39, 0x60, 0xd5, 0xaf, 0x27, 0x08, 0x7f, 0x36, 0x72, 0xc1, 0xab, 0x27, 0x0f, 0xb5,
                0x29, 0x1f, 0x95, 0x87, 0x31, 0x60, 0x65, 0xc0, 0x03, 0xed, 0x4e, 0xe5, 0xb1, 0x06,
                0x3d, 0x50, 0x07,
            ],
        },
        TestElement {
            val: b"<?\\ >",
            res: &[0xff, 0xf9, 0xfe, 0x7f, 0xff, 0x05, 0x3f, 0xef],
        },
    ];

    const WRONG_END: &[u8] = &[0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xaf];

    #[test]
    fn encoder() {
        for e in TEST_CASES {
            let out = encode(e.val);
            assert_eq!(out[..], *e.res);
        }
    }

    #[test]
    fn decoder() {
        for e in TEST_CASES {
            let res = decode(e.res);
            assert!(res.is_ok());
            assert_eq!(res.unwrap()[..], *e.val);
        }
    }

    #[test]
    fn decoder_error_wrong_ending() {
        assert_eq!(decode(WRONG_END), Err(Error::HuffmanDecompression));
    }
}
