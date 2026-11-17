// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cmp::min, mem, num::NonZeroUsize};

use crate::codec::Decoder;

#[derive(Clone, Debug, Default)]
pub struct IncrementalDecoderUint {
    v: u64,
    remaining: Option<usize>,
}

impl IncrementalDecoderUint {
    #[must_use]
    pub fn min_remaining(&self) -> usize {
        self.remaining.unwrap_or(1)
    }

    /// Consume some data.
    ///
    /// # Panics
    ///
    /// Never, but this is not something the compiler can tell.
    pub fn consume(&mut self, dv: &mut Decoder) -> Option<u64> {
        if let Some(r) = &mut self.remaining {
            let amount = min(*r, dv.remaining());
            if amount < 8 {
                self.v <<= amount * 8;
            }
            self.v |= dv.decode_n(amount)?;
            *r -= amount;
            (*r == 0).then_some(self.v)
        } else {
            let (v, remaining) = dv.decode_uint::<u8>().map_or_else(
                || unreachable!(),
                |b| {
                    (
                        u64::from(b & 0x3f),
                        match b >> 6 {
                            0 => 0,
                            1 => 1,
                            2 => 3,
                            3 => 7,
                            _ => unreachable!(),
                        },
                    )
                },
            );
            self.remaining = Some(remaining);
            self.v = v;
            (remaining == 0).then_some(v)
        }
    }

    #[must_use]
    pub const fn decoding_in_progress(&self) -> bool {
        self.remaining.is_some()
    }
}

#[derive(Clone, Debug)]
pub struct IncrementalDecoderBuffer {
    v: Vec<u8>,
    remaining: usize,
}

impl IncrementalDecoderBuffer {
    #[must_use]
    pub const fn new(n: usize) -> Self {
        Self {
            v: Vec::new(),
            remaining: n,
        }
    }

    #[must_use]
    pub const fn min_remaining(&self) -> usize {
        self.remaining
    }

    /// Consume some bytes from the decoder.
    ///
    /// # Panics
    ///
    /// Never; but rust doesn't know that.
    pub fn consume(&mut self, dv: &mut Decoder) -> Option<Vec<u8>> {
        let amount = min(self.remaining, dv.remaining());
        let b = dv.decode(amount)?;
        self.v.extend_from_slice(b);
        self.remaining -= amount;
        (self.remaining == 0).then(|| mem::take(&mut self.v))
    }
}

#[derive(Clone, Debug)]
pub struct IncrementalDecoderIgnore {
    remaining: usize,
}

impl IncrementalDecoderIgnore {
    /// Make a new ignoring decoder.
    #[must_use]
    pub const fn new(n: NonZeroUsize) -> Self {
        Self { remaining: n.get() }
    }

    #[must_use]
    pub const fn min_remaining(&self) -> usize {
        self.remaining
    }

    pub fn consume(&mut self, dv: &mut Decoder) -> bool {
        let amount = min(self.remaining, dv.remaining());
        _ = dv.decode(amount);
        self.remaining -= amount;
        self.remaining == 0
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::num::NonZeroUsize;

    use super::{
        Decoder, IncrementalDecoderBuffer, IncrementalDecoderIgnore, IncrementalDecoderUint,
    };
    use crate::codec::Encoder;

    #[test]
    fn buffer_incremental() {
        let b = &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        let mut dec = IncrementalDecoderBuffer::new(b.len());
        let mut i = 0;
        while i < b.len() {
            // Feed in b in increasing-sized chunks.
            let incr = if i < b.len() / 2 { i + 1 } else { b.len() - i };
            let mut dv = Decoder::from(&b[i..i + incr]);
            i += incr;
            match dec.consume(&mut dv) {
                None => {
                    assert!(i < b.len());
                }
                Some(res) => {
                    assert_eq!(i, b.len());
                    assert_eq!(res, b);
                }
            }
        }
    }

    struct UintTestCase {
        b: String,
        v: u64,
    }

    impl UintTestCase {
        pub fn run(&self) {
            eprintln!(
                "IncrementalDecoderUint decoder with {:?} ; expect {:?}",
                self.b, self.v
            );

            let decoder = IncrementalDecoderUint::default();
            let mut db = Encoder::from_hex(&self.b);
            // Add padding so that we can verify that the reader doesn't over-consume.
            db.encode_byte(0xff);

            for tail in 1..db.len() {
                let split = db.len() - tail;
                let mut dv = Decoder::from(&db.as_ref()[0..split]);
                eprintln!("  split at {split}: {dv:?}");

                // Clone the basic decoder for each iteration of the loop.
                let mut dec = decoder.clone();
                let mut res = None;
                while dv.remaining() > 0 {
                    res = dec.consume(&mut dv);
                }
                assert!(dec.min_remaining() < tail);

                if tail > 1 {
                    assert_eq!(res, None);
                    assert!(dec.min_remaining() > 0);
                    let mut dv = Decoder::from(&db.as_ref()[split..]);
                    eprintln!("  split remainder {split}: {dv:?}");
                    res = dec.consume(&mut dv);
                    assert_eq!(dv.remaining(), 1);
                }

                assert_eq!(dec.min_remaining(), 0);
                assert_eq!(res.unwrap(), self.v);
            }
        }
    }

    macro_rules! uint_tc {
        [$( $b:expr => $v:expr ),+ $(,)?] => {
            vec![ $( UintTestCase { b: String::from($b), v: $v, } ),+]
        };
    }

    #[test]
    fn varint() {
        for c in uint_tc![
            "00" => 0,
            "01" => 1,
            "3f" => 63,
            "4040" => 64,
            "7fff" => 16383,
            "80004000" => 16384,
            "bfffffff" => (1 << 30) - 1,
            "c000000040000000" => 1 << 30,
            "ffffffffffffffff" => (1 << 62) - 1,
        ] {
            c.run();
        }
    }

    #[test]
    fn zero_len() {
        let enc = Encoder::from_hex("ff");
        let mut dec = Decoder::new(enc.as_ref());
        let mut incr = IncrementalDecoderBuffer::new(0);
        assert_eq!(incr.consume(&mut dec), Some(Vec::new()));
        assert_eq!(dec.remaining(), enc.len());
    }

    #[test]
    fn ignore() {
        let db = Encoder::from_hex("12345678ff");

        let decoder = IncrementalDecoderIgnore::new(NonZeroUsize::new(4).unwrap());

        for tail in 1..db.len() {
            let split = db.len() - tail;
            let mut dv = Decoder::from(&db.as_ref()[0..split]);
            eprintln!("  split at {split}: {dv:?}");

            // Clone the basic decoder for each iteration of the loop.
            let mut dec = decoder.clone();
            let mut res = dec.consume(&mut dv);
            assert_eq!(dv.remaining(), 0);
            assert!(dec.min_remaining() < tail);

            if tail > 1 {
                assert!(!res);
                assert!(dec.min_remaining() > 0);
                let mut dv = Decoder::from(&db.as_ref()[split..]);
                eprintln!("  split remainder {split}: {dv:?}");
                res = dec.consume(&mut dv);
                assert_eq!(dv.remaining(), 1);
            }

            assert_eq!(dec.min_remaining(), 0);
            assert!(res);
        }
    }

    #[test]
    fn decoding_in_progress() {
        let mut dec = IncrementalDecoderUint::default();
        assert!(!dec.decoding_in_progress());
        let mut dv = Decoder::new(&[0x40]); // Start of 2-byte varint
        assert!(dec.consume(&mut dv).is_none());
        assert!(dec.decoding_in_progress());
    }

    #[test]
    fn buffer_min_remaining() {
        let dec = IncrementalDecoderBuffer::new(5);
        assert_eq!(dec.min_remaining(), 5);
    }
}
