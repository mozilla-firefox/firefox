// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::sync::LazyLock;

use neqo_common::expect_usize;

use crate::huffman_table::HUFFMAN_TABLE;

// Since we're encoding the table length as a u16, we need to ensure that it fits.
const _: () = assert!(HUFFMAN_TABLE.len() <= u16::MAX as usize);

pub struct HuffmanDecoderNode {
    pub next: [Option<Box<Self>>; 2],
    pub value: Option<u16>,
}

pub static HUFFMAN_DECODER_ROOT: LazyLock<HuffmanDecoderNode> =
    LazyLock::new(|| make_huffman_tree(0, 0));

fn make_huffman_tree(prefix: u32, len: u8) -> HuffmanDecoderNode {
    let mut found = false;
    let mut next = [None, None];
    for (i, iter) in HUFFMAN_TABLE.iter().enumerate() {
        if iter.len <= len {
            continue;
        }
        if (iter.val >> (iter.len - len)) != prefix {
            continue;
        }

        found = true;
        if iter.len == len + 1 {
            // This is a leaf; safe conversion because the value is 0 or 1.
            let bit = expect_usize(iter.val & 1);
            next[bit] = Some(Box::new(HuffmanDecoderNode {
                next: [None, None],
                #[expect(
                    clippy::cast_possible_truncation,
                    reason = "We've checked this in a const `assert!` above."
                )]
                value: Some(i as u16),
            }));
            if next[bit ^ 1].is_some() {
                return HuffmanDecoderNode { next, value: None };
            }
        }
    }

    if found {
        next[0].get_or_insert_with(|| Box::new(make_huffman_tree(prefix << 1, len + 1)));
        next[1].get_or_insert_with(|| Box::new(make_huffman_tree((prefix << 1) + 1, len + 1)));
    }

    HuffmanDecoderNode { next, value: None }
}
