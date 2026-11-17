// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#[derive(Copy, Clone, Debug)]
pub struct Prefix {
    #[expect(clippy::struct_field_names, reason = "Still the best name.")]
    prefix: u8,
    len: u8,
    mask: u8,
}

impl Prefix {
    pub fn new(prefix: u8, len: u8) -> Self {
        // len should never be larger than 7.
        // Most of Prefixes are instantiated as consts below. The only place where this
        // constructor is used is in tests and when literals are encoded and the Huffman
        // bit is added to one of the consts below. create_prefix guaranty that all const
        // have len < 7 so we can safely assert that len is <=7.
        assert!(len <= 7);
        assert!((len == 0) || (prefix & ((1 << (8 - len)) - 1) == 0));
        Self {
            prefix,
            len,
            mask: if len == 0 {
                0xFF
            } else {
                ((1 << len) - 1) << (8 - len)
            },
        }
    }

    pub const fn len(self) -> u8 {
        self.len
    }

    pub const fn prefix(self) -> u8 {
        self.prefix
    }

    pub const fn cmp_prefix(self, b: u8) -> bool {
        (b & self.mask) == self.prefix
    }
}

#[macro_export]
macro_rules! create_prefix {
    ($n:ident) => {
        pub const $n: Prefix = Prefix {
            prefix: 0x0,
            len: 0,
            mask: 0xFF,
        };
    };
    ($n:ident, $v:expr, $l:expr) => {
        pub const $n: Prefix = {
            assert!($l < 7);
            assert!($v & ((1 << (8 - $l)) - 1) == 0);
            Prefix {
                prefix: $v,
                len: $l,
                mask: ((1 << $l) - 1) << (8 - $l),
            }
        };
    };
    ($n:ident, $v:expr, $l:expr, $m:expr) => {
        pub const $n: Prefix = {
            assert!($l < 7);
            assert!($v & ((1 << (8 - $l)) - 1) == 0);
            assert!((((1 << $l) - 1) << (8 - $l)) >= $m);
            Prefix {
                prefix: $v,
                len: $l,
                mask: $m,
            }
        };
    };
}

create_prefix!(NO_PREFIX);
//=====================================================================
// Decoder instructions prefix
//=====================================================================

// | 1 |      Stream ID (7+)       |
create_prefix!(DECODER_HEADER_ACK, 0x80, 1);

// | 0 | 1 |     Stream ID (6+)    |
create_prefix!(DECODER_STREAM_CANCELLATION, 0x40, 2);

// | 0 | 0 |     Increment (6+)    |
create_prefix!(DECODER_INSERT_COUNT_INCREMENT, 0x00, 2);

//=====================================================================
// Encoder instructions prefix
//=====================================================================

// | 0 | 0 | 1 |   Capacity (5+)   |
create_prefix!(ENCODER_CAPACITY, 0x20, 3);

// | 1 | T |    Name Index (6+)    |
// T == 1 static
// T == 0 dynamic
create_prefix!(ENCODER_INSERT_WITH_NAME_REF_STATIC, 0xC0, 2);
create_prefix!(ENCODER_INSERT_WITH_NAME_REF_DYNAMIC, 0x80, 2);

// | 0 | 1 | H | Name Length (5+)  |
// H is not relevant for decoding this prefix, therefore the mask is 1100 0000 = 0xC0
create_prefix!(ENCODER_INSERT_WITH_NAME_LITERAL, 0x40, 2);

// | 0 | 0 | 0 |    Index (5+)     |
create_prefix!(ENCODER_DUPLICATE, 0x00, 3);

//=====================================================================
// Header block encoding prefixes
//=====================================================================

create_prefix!(BASE_PREFIX_POSITIVE, 0x00, 1);
create_prefix!(BASE_PREFIX_NEGATIVE, 0x80, 1);

// | 1 | T |  index(6+) |
// T == 1 static
// T == 0 dynamic
create_prefix!(HEADER_FIELD_INDEX_STATIC, 0xC0, 2);
create_prefix!(HEADER_FIELD_INDEX_DYNAMIC, 0x80, 2);

// | 0 | 0 | 0 | 1 |  Index(4+) |
create_prefix!(HEADER_FIELD_INDEX_DYNAMIC_POST, 0x10, 4);

// | 0 | 1 | N | T |  Index(4+) |
// T == 1 static
// T == 0 dynamic
// N is ignored, therefore the mask is 1101 0000 = 0xD0
create_prefix!(HEADER_FIELD_LITERAL_NAME_REF_STATIC, 0x50, 4, 0xD0);
create_prefix!(HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC, 0x40, 4, 0xD0);

// | 0 | 0 | 0 | 0 | N |  Index(3+) |
// N is ignored, therefore the mask is 1111 0000 = 0xF0
create_prefix!(HEADER_FIELD_LITERAL_NAME_REF_DYNAMIC_POST, 0x00, 5, 0xF0);

// | 0 | 0 | 1 | N | H |  Index(3+) |
// N is ignored and H is not relevant for decoding this prefix, therefore the mask is 1110 0000 =
// 0xE0
create_prefix!(HEADER_FIELD_LITERAL_NAME_LITERAL, 0x20, 4, 0xE0);
