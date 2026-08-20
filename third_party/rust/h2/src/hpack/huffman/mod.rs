mod table;

use self::table::{DECODE_TABLE, ENCODE_TABLE};
use crate::hpack::DecoderError;

use bytes::{BufMut, BytesMut};

const BRANCH: u16 = 0x8000;
const TABLE_INDEX_MASK: u16 = 0x7f00;
const TABLE_WIDTH: usize = 256;

pub fn decode(src: &[u8], buf: &mut BytesMut) -> Result<BytesMut, DecoderError> {
    // Max compression ratio is >= 0.5
    buf.reserve(src.len() << 1);

    let mut table = 0;
    let mut acc = 0u32;
    let mut bits = 0;

    for &byte in src {
        acc = (acc << 8) | byte as u32;
        bits += 8;

        while bits >= 8 {
            let index = (acc >> (bits - 8)) as u8 as usize;
            let entry = DECODE_TABLE[table * TABLE_WIDTH + index];

            if entry & BRANCH == 0 {
                buf.put_u8(entry as u8);
                table = 0;
                bits -= (entry >> 8) as usize;
            } else {
                table = ((entry & TABLE_INDEX_MASK) >> 8) as usize;
                if table == 0 {
                    return Err(DecoderError::InvalidHuffmanCode);
                }
                bits -= 8;
            }
        }
    }

    // Fewer than eight bits remain. A prefix of the EOS code (all ones) is
    // valid padding only when the previous symbol has completed.
    while bits > 0 {
        debug_assert!(bits < 8);
        let padding = (1u32 << bits) - 1;
        if table == 0 && acc & padding == padding {
            break;
        }

        let index = (acc << (8 - bits)) as u8 as usize;
        let entry = DECODE_TABLE[table * TABLE_WIDTH + index];
        if entry & BRANCH != 0 {
            return Err(DecoderError::InvalidHuffmanCode);
        }

        let used = (entry >> 8) as usize;
        if used > bits {
            return Err(DecoderError::InvalidHuffmanCode);
        }

        buf.put_u8(entry as u8);
        table = 0;
        bits -= used;
    }

    if table == 0 {
        Ok(buf.split())
    } else {
        Err(DecoderError::InvalidHuffmanCode)
    }
}

pub fn encode(src: &[u8], dst: &mut BytesMut) {
    let mut bits: u64 = 0;
    let mut bits_left = 40;

    for &b in src {
        let (nbits, code) = ENCODE_TABLE[b as usize];

        bits |= code << (bits_left - nbits);
        bits_left -= nbits;

        while bits_left <= 32 {
            dst.put_u8((bits >> 32) as u8);

            bits <<= 8;
            bits_left += 8;
        }
    }

    if bits_left != 40 {
        // This writes the EOS token
        bits |= (1 << bits_left) - 1;
        dst.put_u8((bits >> 32) as u8);
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn decode(src: &[u8]) -> Result<BytesMut, DecoderError> {
        let mut buf = BytesMut::new();
        super::decode(src, &mut buf)
    }

    #[test]
    fn decode_single_byte() {
        assert_eq!("o", decode(&[0b00111111]).unwrap());
        assert_eq!("0", decode(&[7]).unwrap());
        assert_eq!("A", decode(&[(0x21 << 2) + 3]).unwrap());
    }

    #[test]
    fn single_char_multi_byte() {
        assert_eq!("#", decode(&[255, 160 + 15]).unwrap());
        assert_eq!("$", decode(&[255, 200 + 7]).unwrap());
        assert_eq!("\x0a", decode(&[255, 255, 255, 240 + 3]).unwrap());
    }

    #[test]
    fn multi_char() {
        assert_eq!("!0", decode(&[254, 1]).unwrap());
        assert_eq!(" !", decode(&[0b01010011, 0b11111000]).unwrap());
    }

    #[test]
    fn encode_single_byte() {
        let mut dst = BytesMut::with_capacity(1);

        encode(b"o", &mut dst);
        assert_eq!(&dst[..], &[0b00111111]);

        dst.clear();
        encode(b"0", &mut dst);
        assert_eq!(&dst[..], &[7]);

        dst.clear();
        encode(b"A", &mut dst);
        assert_eq!(&dst[..], &[(0x21 << 2) + 3]);
    }

    #[test]
    fn encode_decode_str() {
        const DATA: &[&str] = &[
            "hello world",
            ":method",
            ":scheme",
            ":authority",
            "yahoo.co.jp",
            "GET",
            "http",
            ":path",
            "/images/top/sp2/cmn/logo-ns-130528.png",
            "example.com",
            "hpack-test",
            "xxxxxxx1",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.8; rv:16.0) Gecko/20100101 Firefox/16.0",
            "accept",
            "Accept",
            "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            "cookie",
            "B=76j09a189a6h4&b=3&s=0b",
            "TE",
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Morbi non bibendum libero. \
             Etiam ultrices lorem ut.",
        ];

        for s in DATA {
            let mut dst = BytesMut::with_capacity(s.len());

            encode(s.as_bytes(), &mut dst);

            let decoded = decode(&dst).unwrap();

            assert_eq!(&decoded[..], s.as_bytes());
        }
    }

    #[test]
    fn encode_decode_u8() {
        const DATA: &[&[u8]] = &[b"\0", b"\0\0\0", b"\0\x01\x02\x03\x04\x05", b"\xFF\xF8"];

        for s in DATA {
            let mut dst = BytesMut::with_capacity(s.len());

            encode(s, &mut dst);

            let decoded = decode(&dst).unwrap();

            assert_eq!(&decoded[..], &s[..]);
        }
    }

    #[test]
    fn encode_decode_all_octets() {
        let src: Vec<_> = (0..=u8::MAX).collect();
        let mut encoded = BytesMut::new();
        encode(&src, &mut encoded);
        assert_eq!(decode(&encoded).unwrap(), src);
    }

    #[test]
    fn rejects_eos_and_invalid_padding() {
        assert_eq!(decode(&[0xff]), Err(DecoderError::InvalidHuffmanCode));
        assert_eq!(
            decode(&[0xff, 0xff, 0xff, 0xff]),
            Err(DecoderError::InvalidHuffmanCode)
        );
        assert_eq!(decode(&[0]), Err(DecoderError::InvalidHuffmanCode));
    }
}

/*
// uncomment to run benchmarks
#[cfg(test)]
mod bench {
    extern crate test;

    use self::test::{black_box, Bencher};
    use super::*;

    fn decode_input(b: &mut Bencher, input: &[u8]) {
        let mut encoded = BytesMut::new();
        encode(input, &mut encoded);

        let mut scratch = BytesMut::with_capacity(input.len() * 2);
        b.bytes = encoded.len() as u64;
        b.iter(|| {
            let decoded = decode(black_box(encoded.as_ref()), &mut scratch).unwrap();
            black_box(decoded);
        });
    }

    #[bench]
    fn decode_short_ascii(b: &mut Bencher) {
        decode_input(b, b"www.example.com");
    }

    #[bench]
    fn decode_header_value(b: &mut Bencher) {
        decode_input(
            b,
            b"text/html,application/xhtml+xml,application/xml;q=0.9;q=0.8",
        );
    }

    #[bench]
    fn decode_long_ascii(b: &mut Bencher) {
        decode_input(
            b,
            b"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.8; rv:16.0) Gecko/20100101 Firefox/16.0",
        );
    }

    #[bench]
    fn decode_all_octets(b: &mut Bencher) {
        let input: Vec<_> = (0..=u8::MAX).collect();
        decode_input(b, &input);
    }
}
*/
