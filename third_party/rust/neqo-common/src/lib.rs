// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(coverage_nightly, feature(coverage_attribute))]

pub mod bytes;
mod codec;
pub mod datagram;
pub mod event;
#[cfg(feature = "build-fuzzing-corpus")]
mod fuzz;
pub mod header;
pub mod hex;
pub mod hrtime;
mod incrdecoder;
pub mod log;
pub mod qlog;
pub mod tos;

use enum_map::Enum;
use strum::Display;

#[cfg(feature = "build-fuzzing-corpus")]
pub use self::fuzz::write_item_to_fuzzing_corpus;
pub use self::{
    bytes::Bytes,
    codec::{Buffer, Decoder, Encoder, MAX_VARINT, Reservation},
    datagram::Datagram,
    header::Header,
    incrdecoder::{IncrementalDecoderBuffer, IncrementalDecoderIgnore, IncrementalDecoderUint},
    tos::{Dscp, Ecn, Tos},
};

#[must_use]
pub const fn const_max(a: usize, b: usize) -> usize {
    [a, b][(a <= b) as usize]
}
#[must_use]
pub const fn const_min(a: usize, b: usize) -> usize {
    [a, b][(a > b) as usize]
}
#[must_use]
pub const fn const_min_u64(a: u64, b: u64) -> u64 {
    [a, b][(a > b) as usize]
}

// The conversions below depend on this relationship.
const _: () = assert!(usize::BITS <= u64::BITS);

/// A trait for values that represent a length or byte count, convertible
/// to the wire domain (`u64`).
pub trait Length: Copy {
    fn as_u64(self) -> u64;
}

impl Length for u64 {
    fn as_u64(self) -> u64 {
        self
    }
}

impl Length for usize {
    fn as_u64(self) -> u64 {
        to_u64(self)
    }
}

/// Convert a `usize` to `u64`.
#[expect(
    clippy::cast_possible_truncation,
    reason = "debug_assert roundtrip `v as u64 as usize` contains a u64→usize cast; \
              const assert above ensures it is lossless on all supported targets"
)]
#[inline]
#[must_use]
pub const fn to_u64(v: usize) -> u64 {
    debug_assert!(v as u64 as usize == v);
    v as u64
}

/// Convert a numeric type to a `usize`.
/// # Panics
/// If we have an overflow on the conversion.
/// Callers of this function should document why they think overflow is impossible.
#[inline]
#[must_use]
#[track_caller]
pub fn expect_usize<T>(v: T) -> usize
where
    usize: TryFrom<T, Error: std::fmt::Debug>,
{
    usize::try_from(v).expect("usize should be large enough for this value")
}

#[derive(Debug, PartialEq, Eq, Copy, Clone, Enum, Display)]
/// Client or Server.
pub enum Role {
    Client,
    Server,
}

impl Role {
    #[must_use]
    pub const fn remote(self) -> Self {
        match self {
            Self::Client => Self::Server,
            Self::Server => Self::Client,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageType {
    Request,
    Response,
}

/// Dispatch a method call on an enum to its variants' inner values.
///
/// The variant list is given once in a local wrapper; method bodies stay clean:
///
/// ```ignore
/// // Once per enum, in the impl module:
/// macro_rules! dispatch {
///     ($self:ident . $method:ident $args:tt) => {
///         neqo_common::dispatch!([Variant1, Variant2, Variant3] $self . $method $args)
///     };
/// }
///
/// impl SomeTrait for MyEnum {
///     fn method(&self) -> T { dispatch!(self.method()) }
/// }
/// ```
#[macro_export]
macro_rules! dispatch {
    ([$($variant:ident),+ $(,)?] $self:ident . $method:ident $args:tt) => {
        match $self {
            $( Self::$variant(v) => v.$method $args, )+
        }
    };
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::{const_max, const_min};

    #[test]
    fn const_minmax() {
        for (a, b, min, max) in [(2, 5, 2, 5), (5, 2, 2, 5), (3, 3, 3, 3)] {
            assert_eq!(const_min(a, b), min);
            assert_eq!(const_max(a, b), max);
        }
    }
}
