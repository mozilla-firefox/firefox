// Copyright Mozilla Foundation. See the COPYRIGHT
// file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Functions for converting between different in-RAM representations of text
//! and for quickly checking if the Unicode Bidirectional Algorithm can be
//! avoided.
//!
//! By using slices for output, the functions here seek to enable by-register
//! (ALU register or SIMD register as available) operations in order to
//! outperform iterator-based conversions available in the Rust standard
//! library.
//!
//! _Note:_ "Latin1" in this module refers to the Unicode range from U+0000 to
//! U+00FF, inclusive, and does not refer to the windows-1252 range. This
//! in-memory encoding is sometimes used as a storage optimization of text
//! when UTF-16 indexing and length semantics are exposed.
//!
//! The FFI binding for this module are in the
//! [encoding_c_mem crate](https://github.com/hsivonen/encoding_c_mem).

#[cfg(feature = "alloc")]
use alloc::borrow::Cow;
#[cfg(feature = "alloc")]
use alloc::string::String;
#[cfg(feature = "alloc")]
use alloc::vec::Vec;

use super::DecoderResult;
use super::in_inclusive_range8;
use super::in_inclusive_range16;
use super::in_inclusive_range32;
use super::in_range16;
use super::in_range32;
use crate::ascii::*;
use crate::utf_8::*;

macro_rules! non_fuzz_debug_assert {
    ($($arg:tt)*) => (if !cfg!(fuzzing) { debug_assert!($($arg)*); })
}

cfg_if! {
    if #[cfg(feature = "simd-accel")] {
        use ::core::intrinsics::likely;
        use ::core::intrinsics::unlikely;
    } else {
        #[inline(always)]
        fn likely(b: bool) -> bool {
            b
        }
        #[inline(always)]
        fn unlikely(b: bool) -> bool {
            b
        }
    }
}

/// Classification of text as Latin1 (all code points are below U+0100),
/// left-to-right with some non-Latin1 characters or as containing at least
/// some right-to-left characters.
#[must_use]
#[derive(Debug, PartialEq, Eq)]
#[repr(C)]
pub enum Latin1Bidi {
    /// Every character is below U+0100.
    Latin1 = 0,
    /// There is at least one character that's U+0100 or higher, but there
    /// are no right-to-left characters.
    LeftToRight = 1,
    /// There is at least one right-to-left character.
    Bidi = 2,
}

#[inline(always)]
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
fn is_utf8_latin1_impl(buffer: &[u8]) -> Option<usize> {
    let mut bytes = buffer;
    let mut total = 0;
    loop {
        if let Some((byte, offset)) = validate_ascii(bytes) {
            total += offset;
            if in_inclusive_range8(byte, 0xC2, 0xC3) {
                let next = offset + 1;
                if next == bytes.len() {
                    return Some(total);
                }
                if bytes[next] & 0xC0 != 0x80 {
                    return Some(total);
                }
                bytes = &bytes[offset + 2..];
                total += 2;
            } else {
                return Some(total);
            }
        } else {
            return None;
        }
    }
}

macro_rules! copy_impl {
    ($name:ident, $stride:ident, $src_unit:ty, $dst_unit:ty) => {
        #[inline(always)]
        fn $name(src: &[$src_unit], dst: &mut [$dst_unit]) {
            // Make both the same length here to have the chunks and tail match.
            let len = core::cmp::min(src.len(), dst.len());
            let (src_strides, src_tail) = src[..len].as_chunks::<STRIDE>();
            let (dst_strides, dst_tail) = dst[..len].as_chunks_mut::<STRIDE>();
            let (src_double_strides, src_single_stride) = src_strides.as_chunks::<2>();
            let (dst_double_strides, dst_single_stride) = dst_strides.as_chunks_mut::<2>();
            for (src_double_stride, dst_double_stride) in
                src_double_strides.iter().zip(dst_double_strides.iter_mut())
            {
                $stride(&src_double_stride[0], &mut dst_double_stride[0]);
                $stride(&src_double_stride[1], &mut dst_double_stride[1]);
            }
            for (src_stride, dst_stride) in
                src_single_stride.iter().zip(dst_single_stride.iter_mut())
            {
                $stride(src_stride, dst_stride);
            }
            for (src_slot, dst_slot) in src_tail.iter().zip(dst_tail.iter_mut()) {
                *dst_slot = *src_slot as $dst_unit;
            }
        }
    };
}

copy_impl!(unpack_latin1, unpack_stride, u8, u16);
copy_impl!(pack_latin1, pack_stride, u16, u8);

cfg_if! {
    if #[cfg(all(
        feature = "simd-accel",
        target_endian = "little",
    ))] {
        use core::simd::u8x16;
        use core::simd::u16x8;

        use crate::simd_funcs::unpack_stride;
        use crate::simd_funcs::pack_stride;

        #[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
        fn is_utf16_bidi_impl(buffer: &[u16]) -> bool {
            let (half_strides, tail) = buffer.as_chunks::<{STRIDE / 2}>();
            for half_stride in half_strides {
                if crate::simd_funcs::is_half_stride_bidi(half_stride) {
                    return true;
                }
            }
            tail.iter().any(|c| is_utf16_code_unit_bidi(*c))
        }

        #[inline(always)]
        fn is_str_latin1_bool_impl(buffer: &str) -> bool {
            let (strides, tail) = buffer.as_bytes().as_chunks::<STRIDE>();
            for stride in strides {
                let simd = (*stride).into();
                if !crate::simd_funcs::simd_is_str_latin1(simd) {
                    return false;
                }
            }
            tail.iter().all(|b| *b < 0xC4)
        }

        macro_rules! unit_check_impl {
            ($name:ident, $stride:ident, $unit:ty, $simd:ty, $bound:expr) => {
                #[inline(always)]
                fn $name(buffer: &[$unit]) -> bool {
                    // The most common reason to return `false` is for the first code
                    // unit to fail the test, so check that first.
                    if let Some(u) = buffer.first() {
                        if *u >= $bound {
                            return false;
                        }
                    }
                    let (strides, tail) = buffer.as_chunks::<{STRIDE / core::mem::size_of::<$unit>()}>();
                    let (quad_strides, strides_tail) = strides.as_chunks::<4>();
                    for quad_stride in quad_strides {
                        if let Some(reduced) = quad_stride.iter().map(|s| { let simd: $simd = (*s).into(); simd }).reduce(|a, b| a | b) {
                            if !crate::simd_funcs::$stride(reduced) {
                                return false;
                            }
                        } else {
                            debug_assert!(false);
                        }
                    }
                    if let Some(reduced) = strides_tail.iter().map(|s| { let simd: $simd = (*s).into(); simd }).reduce(|a, b| a | b) {
                        if !crate::simd_funcs::$stride(reduced) {
                            return false;
                        }
                    }
                    if let Some(reduced) = tail.iter().copied().reduce(|a, b| a | b) {
                        reduced < $bound
                    } else {
                        true
                    }
                }
            };
        }

        unit_check_impl!(is_ascii_impl, simd_is_ascii, u8, u8x16, 0x80);
        unit_check_impl!(is_basic_latin_impl, simd_is_basic_latin, u16, u16x8, 0x80);
        unit_check_impl!(is_utf16_latin1_impl, simd_is_latin1, u16, u16x8, 0x100);

        #[inline(always)]
        fn check_utf16_for_latin1_and_bidi_impl(buffer: &[u16]) -> Latin1Bidi {
            let (half_strides, tail) = buffer.as_chunks::<{STRIDE / 2}>();
            let mut half_stride_iter = half_strides.iter();
            loop {
                let Some(s) = half_stride_iter.next() else {
                    let mut iter = tail.iter();
                    loop {
                        let Some(u) = iter.next() else {
                            return Latin1Bidi::Latin1;
                        };
                        if *u < 0x100 {
                            continue;
                        }
                        if is_utf16_code_unit_bidi(*u) {
                            return Latin1Bidi::Bidi;
                        }
                        loop {
                            let Some(u) = iter.next() else {
                                return Latin1Bidi::LeftToRight;
                            };
                            if is_utf16_code_unit_bidi(*u) {
                                return Latin1Bidi::Bidi;
                            }
                        }
                    }
                };
                let simd: u16x8 = (*s).into();
                if crate::simd_funcs::simd_is_latin1(simd) {
                    continue;
                }
                if crate::simd_funcs::is_u16x8_bidi(simd) {
                    return Latin1Bidi::Bidi;
                }
                loop {
                    let Some(s) = half_stride_iter.next() else {
                        for u in tail {
                            if is_utf16_code_unit_bidi(*u) {
                                return Latin1Bidi::Bidi;
                            }
                        }
                        return Latin1Bidi::LeftToRight;
                    };
                    let simd: u16x8 = (*s).into();
                    if crate::simd_funcs::is_u16x8_bidi(simd) {
                        return Latin1Bidi::Bidi;
                    }
                }
            }
        }

    } else {
        use crate::ascii::unpack_stride;
        use crate::ascii::pack_stride;

        fn is_utf16_bidi_impl(buffer: &[u16]) -> bool {
            buffer.iter().any(|c| is_utf16_code_unit_bidi(*c))
        }

        #[inline(always)]
        fn is_str_latin1_bool_impl(buffer: &str) -> bool {
            is_str_latin1_impl(buffer).is_none()
        }

        macro_rules! unit_check_impl {
            ($name:ident, $stride:ident, $unit:ty, $bound:expr) => {
                #[inline(always)]
                fn $name(buffer: &[$unit]) -> bool {
                    let (strides, tail) = buffer.as_chunks::<STRIDE>();
                    for stride in strides {
                        if !crate::ascii::$stride(stride) {
                            return false;
                        }
                    }
                    if let Some(reduced) = tail.iter().copied().reduce(|a, b| a | b) {
                        reduced < $bound
                    } else {
                        true
                    }
                }
            };
        }

        unit_check_impl!(is_ascii_impl, is_ascii, u8, 0x80);
        unit_check_impl!(is_basic_latin_impl, is_basic_latin, u16, 0x80);
        unit_check_impl!(is_utf16_latin1_impl, is_utf16_latin1, u16, 0x100);

        #[inline(always)]
        fn check_utf16_for_latin1_and_bidi_impl(buffer: &[u16]) -> Latin1Bidi {
            let mut iter = buffer.iter();
            loop {
                let Some(u) = iter.next() else {
                    return Latin1Bidi::Latin1;
                };
                if *u < 0x100 {
                    continue;
                }
                if is_utf16_code_unit_bidi(*u) {
                    return Latin1Bidi::Bidi;
                }
                loop {
                    let Some(u) = iter.next() else {
                        return Latin1Bidi::LeftToRight;
                    };
                    if is_utf16_code_unit_bidi(*u) {
                        return Latin1Bidi::Bidi;
                    }
                }
            }
        }

    }
}

/// Checks whether the buffer is all-ASCII.
///
/// May read the entire buffer even if it isn't all-ASCII. (I.e. the function
/// is not guaranteed to fail fast.)
pub fn is_ascii(buffer: &[u8]) -> bool {
    is_ascii_impl(buffer)
}

/// Checks whether the buffer is all-Basic Latin (i.e. UTF-16 representing
/// only ASCII characters).
///
/// May read the entire buffer even if it isn't all-ASCII. (I.e. the function
/// is not guaranteed to fail fast.)
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn is_basic_latin(buffer: &[u16]) -> bool {
    is_basic_latin_impl(buffer)
}

/// Checks whether the buffer is valid UTF-8 representing only code points
/// less than or equal to U+00FF.
///
/// Fails fast. (I.e. returns before having read the whole buffer if UTF-8
/// invalidity or code points above U+00FF are discovered.
pub fn is_utf8_latin1(buffer: &[u8]) -> bool {
    is_utf8_latin1_impl(buffer).is_none()
}

/// Checks whether the buffer represents only code points less than or equal
/// to U+00FF.
///
/// Fails fast. (I.e. returns before having read the whole buffer if code
/// points above U+00FF are discovered.
pub fn is_str_latin1(buffer: &str) -> bool {
    is_str_latin1_bool_impl(buffer)
}

/// Checks whether the buffer represents only code point less than or equal
/// to U+00FF.
///
/// May read the entire buffer even if it isn't all-Latin1. (I.e. the function
/// is not guaranteed to fail fast.)
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn is_utf16_latin1(buffer: &[u16]) -> bool {
    is_utf16_latin1_impl(buffer)
}

/// Checks whether a potentially-invalid UTF-8 buffer contains code points
/// that trigger right-to-left processing.
///
/// The check is done on a Unicode block basis without regard to assigned
/// vs. unassigned code points in the block. Hebrew presentation forms in
/// the Alphabetic Presentation Forms block are treated as if they formed
/// a block on their own (i.e. it treated as right-to-left). Additionally,
/// the four RIGHT-TO-LEFT FOO controls in General Punctuation are checked
/// for. Control characters that are technically bidi controls but do not
/// cause right-to-left behavior without the presence of right-to-left
/// characters or right-to-left controls are not checked for. As a special
/// case, U+FEFF is excluded from Arabic Presentation Forms-B.
///
/// Returns `true` if the input is invalid UTF-8 or the input contains an
/// RTL character. Returns `false` if the input is valid UTF-8 and contains
/// no RTL characters.
#[allow(clippy::collapsible_if, clippy::cognitive_complexity)]
#[inline]
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn is_utf8_bidi(buffer: &[u8]) -> bool {
    // As of rustc 1.25.0-nightly (73ac5d6a8 2018-01-11), this is faster
    // than UTF-8 validation followed by `is_str_bidi()` for German,
    // Russian and Japanese. However, this is considerably slower for Thai.
    // Chances are that the compiler makes some branch predictions that are
    // unfortunate for Thai. Not spending the time to manually optimize
    // further at this time, since it's unclear if this variant even has
    // use cases. However, this is worth revisiting once Rust gets the
    // ability to annotate relative priorities of match arms.

    // U+058F: D6 8F
    // U+0590: D6 90
    // U+08FF: E0 A3 BF
    // U+0900: E0 A4 80
    //
    // U+200F: E2 80 8F
    // U+202B: E2 80 AB
    // U+202E: E2 80 AE
    // U+2067: E2 81 A7
    //
    // U+FB1C: EF AC 9C
    // U+FB1D: EF AC 9D
    // U+FDFF: EF B7 BF
    // U+FE00: EF B8 80
    //
    // U+FE6F: EF B9 AF
    // U+FE70: EF B9 B0
    // U+FEFE: EF BB BE
    // U+FEFF: EF BB BF
    //
    // U+107FF: F0 90 9F BF
    // U+10800: F0 90 A0 80
    // U+10FFF: F0 90 BF BF
    // U+11000: F0 91 80 80
    //
    // U+1E7FF: F0 9E 9F BF
    // U+1E800: F0 9E A0 80
    // U+1EFFF: F0 9E BF BF
    // U+1F000: F0 9F 80 80
    let mut src = buffer;
    'outer: loop {
        if let Some((mut byte, mut read)) = validate_ascii(src) {
            // Check for the longest sequence to avoid checking twice for the
            // multi-byte sequences.
            if read + 4 <= src.len() {
                'inner: loop {
                    // At this point, `byte` is not included in `read`.
                    match byte {
                        0..=0x7F => {
                            // ASCII: go back to SIMD.
                            read += 1;
                            src = &src[read..];
                            continue 'outer;
                        }
                        0xC2..=0xD5 => {
                            // Two-byte
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            if !in_inclusive_range8(second, 0x80, 0xBF) {
                                return true;
                            }
                            read += 2;
                        }
                        0xD6 => {
                            // Two-byte
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            if !in_inclusive_range8(second, 0x80, 0xBF) {
                                return true;
                            }
                            // XXX consider folding the above and below checks
                            if second > 0x8F {
                                return true;
                            }
                            read += 2;
                        }
                        // two-byte starting with 0xD7 and above is bidi
                        0xE1 | 0xE3..=0xEC | 0xEE => {
                            // Three-byte normal
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            if ((UTF8_DATA.table[usize::from(second)]
                                & unsafe {
                                    *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                })
                                | (third >> 6))
                                != 2
                            {
                                return true;
                            }
                            read += 3;
                        }
                        0xE2 => {
                            // Three-byte normal, potentially bidi
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            if ((UTF8_DATA.table[usize::from(second)]
                                & unsafe {
                                    *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                })
                                | (third >> 6))
                                != 2
                            {
                                return true;
                            }
                            if second == 0x80 {
                                if third == 0x8F || third == 0xAB || third == 0xAE {
                                    return true;
                                }
                            } else if second == 0x81 {
                                if third == 0xA7 {
                                    return true;
                                }
                            }
                            read += 3;
                        }
                        0xEF => {
                            // Three-byte normal, potentially bidi
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            if ((UTF8_DATA.table[usize::from(second)]
                                & unsafe {
                                    *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                })
                                | (third >> 6))
                                != 2
                            {
                                return true;
                            }
                            if in_inclusive_range8(second, 0xAC, 0xB7) {
                                if second == 0xAC {
                                    if third > 0x9C {
                                        return true;
                                    }
                                } else {
                                    return true;
                                }
                            } else if in_inclusive_range8(second, 0xB9, 0xBB) {
                                if second == 0xB9 {
                                    if third > 0xAF {
                                        return true;
                                    }
                                } else if second == 0xBB {
                                    if third != 0xBF {
                                        return true;
                                    }
                                } else {
                                    return true;
                                }
                            }
                            read += 3;
                        }
                        0xE0 => {
                            // Three-byte special lower bound, potentially bidi
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            if ((UTF8_DATA.table[usize::from(second)]
                                & unsafe {
                                    *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                })
                                | (third >> 6))
                                != 2
                            {
                                return true;
                            }
                            // XXX can this be folded into the above validity check
                            if second < 0xA4 {
                                return true;
                            }
                            read += 3;
                        }
                        0xED => {
                            // Three-byte special upper bound
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            if ((UTF8_DATA.table[usize::from(second)]
                                & unsafe {
                                    *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                })
                                | (third >> 6))
                                != 2
                            {
                                return true;
                            }
                            read += 3;
                        }
                        0xF1..=0xF4 => {
                            // Four-byte normal
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            let fourth = unsafe { *(src.get_unchecked(read + 3)) };
                            if (u16::from(
                                UTF8_DATA.table[usize::from(second)]
                                    & unsafe {
                                        *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                    },
                            ) | u16::from(third >> 6)
                                | (u16::from(fourth & 0xC0) << 2))
                                != 0x202
                            {
                                return true;
                            }
                            read += 4;
                        }
                        0xF0 => {
                            // Four-byte special lower bound, potentially bidi
                            let second = unsafe { *(src.get_unchecked(read + 1)) };
                            let third = unsafe { *(src.get_unchecked(read + 2)) };
                            let fourth = unsafe { *(src.get_unchecked(read + 3)) };
                            if (u16::from(
                                UTF8_DATA.table[usize::from(second)]
                                    & unsafe {
                                        *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80))
                                    },
                            ) | u16::from(third >> 6)
                                | (u16::from(fourth & 0xC0) << 2))
                                != 0x202
                            {
                                return true;
                            }
                            if unlikely(second == 0x90 || second == 0x9E) {
                                let third = src[read + 2];
                                if third >= 0xA0 {
                                    return true;
                                }
                            }
                            read += 4;
                        }
                        _ => {
                            // Invalid lead or bidi-only lead
                            return true;
                        }
                    }
                    if read + 4 > src.len() {
                        if read == src.len() {
                            return false;
                        }
                        byte = src[read];
                        break 'inner;
                    }
                    byte = src[read];
                    continue 'inner;
                }
            }
            // We can't have a complete 4-byte sequence, but we could still have
            // a complete shorter sequence.

            // At this point, `byte` is not included in `read`.
            match byte {
                0..=0x7F => {
                    // ASCII: go back to SIMD.
                    read += 1;
                    src = &src[read..];
                    continue 'outer;
                }
                0xC2..=0xD5 => {
                    // Two-byte
                    let new_read = read + 2;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    if !in_inclusive_range8(second, 0x80, 0xBF) {
                        return true;
                    }
                    read = new_read;
                    // We need to deal with the case where we came here with 3 bytes
                    // left, so we need to take a look at the last one.
                    src = &src[read..];
                    continue 'outer;
                }
                0xD6 => {
                    // Two-byte, potentially bidi
                    let new_read = read + 2;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    if !in_inclusive_range8(second, 0x80, 0xBF) {
                        return true;
                    }
                    // XXX consider folding the above and below checks
                    if second > 0x8F {
                        return true;
                    }
                    read = new_read;
                    // We need to deal with the case where we came here with 3 bytes
                    // left, so we need to take a look at the last one.
                    src = &src[read..];
                    continue 'outer;
                }
                // two-byte starting with 0xD7 and above is bidi
                0xE1 | 0xE3..=0xEC | 0xEE => {
                    // Three-byte normal
                    let new_read = read + 3;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    let third = unsafe { *(src.get_unchecked(read + 2)) };
                    if ((UTF8_DATA.table[usize::from(second)]
                        & unsafe { *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80)) })
                        | (third >> 6))
                        != 2
                    {
                        return true;
                    }
                }
                0xE2 => {
                    // Three-byte normal, potentially bidi
                    let new_read = read + 3;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    let third = unsafe { *(src.get_unchecked(read + 2)) };
                    if ((UTF8_DATA.table[usize::from(second)]
                        & unsafe { *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80)) })
                        | (third >> 6))
                        != 2
                    {
                        return true;
                    }
                    if second == 0x80 {
                        if third == 0x8F || third == 0xAB || third == 0xAE {
                            return true;
                        }
                    } else if second == 0x81 {
                        if third == 0xA7 {
                            return true;
                        }
                    }
                }
                0xEF => {
                    // Three-byte normal, potentially bidi
                    let new_read = read + 3;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    let third = unsafe { *(src.get_unchecked(read + 2)) };
                    if ((UTF8_DATA.table[usize::from(second)]
                        & unsafe { *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80)) })
                        | (third >> 6))
                        != 2
                    {
                        return true;
                    }
                    if in_inclusive_range8(second, 0xAC, 0xB7) {
                        if second == 0xAC {
                            if third > 0x9C {
                                return true;
                            }
                        } else {
                            return true;
                        }
                    } else if in_inclusive_range8(second, 0xB9, 0xBB) {
                        if second == 0xB9 {
                            if third > 0xAF {
                                return true;
                            }
                        } else if second == 0xBB {
                            if third != 0xBF {
                                return true;
                            }
                        } else {
                            return true;
                        }
                    }
                }
                0xE0 => {
                    // Three-byte special lower bound, potentially bidi
                    let new_read = read + 3;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    let third = unsafe { *(src.get_unchecked(read + 2)) };
                    if ((UTF8_DATA.table[usize::from(second)]
                        & unsafe { *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80)) })
                        | (third >> 6))
                        != 2
                    {
                        return true;
                    }
                    // XXX can this be folded into the above validity check
                    if second < 0xA4 {
                        return true;
                    }
                }
                0xED => {
                    // Three-byte special upper bound
                    let new_read = read + 3;
                    if new_read > src.len() {
                        return true;
                    }
                    let second = unsafe { *(src.get_unchecked(read + 1)) };
                    let third = unsafe { *(src.get_unchecked(read + 2)) };
                    if ((UTF8_DATA.table[usize::from(second)]
                        & unsafe { *(UTF8_DATA.table.get_unchecked(byte as usize + 0x80)) })
                        | (third >> 6))
                        != 2
                    {
                        return true;
                    }
                }
                _ => {
                    // Invalid lead, 4-byte lead or 2-byte bidi-only lead
                    return true;
                }
            }
            return false;
        } else {
            return false;
        }
    }
}

/// Checks whether a valid UTF-8 buffer contains code points that trigger
/// right-to-left processing.
///
/// The check is done on a Unicode block basis without regard to assigned
/// vs. unassigned code points in the block. Hebrew presentation forms in
/// the Alphabetic Presentation Forms block are treated as if they formed
/// a block on their own (i.e. it treated as right-to-left). Additionally,
/// the four RIGHT-TO-LEFT FOO controls in General Punctuation are checked
/// for. Control characters that are technically bidi controls but do not
/// cause right-to-left behavior without the presence of right-to-left
/// characters or right-to-left controls are not checked for. As a special
/// case, U+FEFF is excluded from Arabic Presentation Forms-B.
#[allow(clippy::collapsible_if)]
#[inline]
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn is_str_bidi(buffer: &str) -> bool {
    // U+058F: D6 8F
    // U+0590: D6 90
    // U+08FF: E0 A3 BF
    // U+0900: E0 A4 80
    //
    // U+200F: E2 80 8F
    // U+202B: E2 80 AB
    // U+202E: E2 80 AE
    // U+2067: E2 81 A7
    //
    // U+FB1C: EF AC 9C
    // U+FB1D: EF AC 9D
    // U+FDFF: EF B7 BF
    // U+FE00: EF B8 80
    //
    // U+FE6F: EF B9 AF
    // U+FE70: EF B9 B0
    // U+FEFE: EF BB BE
    // U+FEFF: EF BB BF
    //
    // U+107FF: F0 90 9F BF
    // U+10800: F0 90 A0 80
    // U+10FFF: F0 90 BF BF
    // U+11000: F0 91 80 80
    //
    // U+1E7FF: F0 9E 9F BF
    // U+1E800: F0 9E A0 80
    // U+1EFFF: F0 9E BF BF
    // U+1F000: F0 9F 80 80
    let mut bytes = buffer.as_bytes();
    'outer: loop {
        // TODO: Instead of just validating ASCII using SIMD, use SIMD
        // to check for non-ASCII lead bytes, too, to quickly conclude
        // that the vector consist entirely of CJK and below-Hebrew
        // code points.
        // Unfortunately, scripts above Arabic but below CJK share
        // lead bytes with RTL.
        if let Some((mut byte, mut read)) = validate_ascii(bytes) {
            'inner: loop {
                // At this point, `byte` is not included in `read`.
                if byte < 0xE0 {
                    if byte >= 0x80 {
                        // Two-byte
                        // Adding `unlikely` here improved throughput on
                        // Russian plain text by 33%!
                        if unlikely(byte >= 0xD6) {
                            if byte == 0xD6 {
                                let second = bytes[read + 1];
                                if second > 0x8F {
                                    return true;
                                }
                            } else {
                                return true;
                            }
                        }
                        read += 2;
                    } else {
                        // ASCII: write and go back to SIMD.
                        read += 1;
                        // Intuitively, we should go back to the outer loop only
                        // if byte is 0x30 or above, so as to avoid trashing on
                        // ASCII space, comma and period in non-Latin context.
                        // However, the extra branch seems to cost more than it's
                        // worth.
                        bytes = &bytes[read..];
                        continue 'outer;
                    }
                } else if byte < 0xF0 {
                    // Three-byte
                    if unlikely(!in_inclusive_range8(byte, 0xE3, 0xEE) && byte != 0xE1) {
                        let second = bytes[read + 1];
                        if byte == 0xE0 {
                            if second < 0xA4 {
                                return true;
                            }
                        } else if byte == 0xE2 {
                            let third = bytes[read + 2];
                            if second == 0x80 {
                                if third == 0x8F || third == 0xAB || third == 0xAE {
                                    return true;
                                }
                            } else if second == 0x81 {
                                if third == 0xA7 {
                                    return true;
                                }
                            }
                        } else {
                            debug_assert_eq!(byte, 0xEF);
                            if in_inclusive_range8(second, 0xAC, 0xB7) {
                                if second == 0xAC {
                                    let third = bytes[read + 2];
                                    if third > 0x9C {
                                        return true;
                                    }
                                } else {
                                    return true;
                                }
                            } else if in_inclusive_range8(second, 0xB9, 0xBB) {
                                if second == 0xB9 {
                                    let third = bytes[read + 2];
                                    if third > 0xAF {
                                        return true;
                                    }
                                } else if second == 0xBB {
                                    let third = bytes[read + 2];
                                    if third != 0xBF {
                                        return true;
                                    }
                                } else {
                                    return true;
                                }
                            }
                        }
                    }
                    read += 3;
                } else {
                    // Four-byte
                    let second = bytes[read + 1];
                    if unlikely(byte == 0xF0 && (second == 0x90 || second == 0x9E)) {
                        let third = bytes[read + 2];
                        if third >= 0xA0 {
                            return true;
                        }
                    }
                    read += 4;
                }
                // The comparison is always < or == and never >, but including
                // > here to let the compiler assume that < is true if this
                // comparison is false.
                if read >= bytes.len() {
                    return false;
                }
                byte = bytes[read];
                continue 'inner;
            }
        } else {
            return false;
        }
    }
}

/// Checks whether a UTF-16 buffer contains code points that trigger
/// right-to-left processing.
///
/// The check is done on a Unicode block basis without regard to assigned
/// vs. unassigned code points in the block. Hebrew presentation forms in
/// the Alphabetic Presentation Forms block are treated as if they formed
/// a block on their own (i.e. it treated as right-to-left). Additionally,
/// the four RIGHT-TO-LEFT FOO controls in General Punctuation are checked
/// for. Control characters that are technically bidi controls but do not
/// cause right-to-left behavior without the presence of right-to-left
/// characters or right-to-left controls are not checked for. As a special
/// case, U+FEFF is excluded from Arabic Presentation Forms-B.
///
/// Returns `true` if the input contains an RTL character or an unpaired
/// high surrogate that could be the high half of an RTL character.
/// Returns `false` if the input contains neither RTL characters nor
/// unpaired high surrogates that could be higher halves of RTL characters.
pub fn is_utf16_bidi(buffer: &[u16]) -> bool {
    is_utf16_bidi_impl(buffer)
}

/// Checks whether a scalar value triggers right-to-left processing.
///
/// The check is done on a Unicode block basis without regard to assigned
/// vs. unassigned code points in the block. Hebrew presentation forms in
/// the Alphabetic Presentation Forms block are treated as if they formed
/// a block on their own (i.e. it treated as right-to-left). Additionally,
/// the four RIGHT-TO-LEFT FOO controls in General Punctuation are checked
/// for. Control characters that are technically bidi controls but do not
/// cause right-to-left behavior without the presence of right-to-left
/// characters or right-to-left controls are not checked for. As a special
/// case, U+FEFF is excluded from Arabic Presentation Forms-B.
#[inline(always)]
pub fn is_char_bidi(c: char) -> bool {
    // Controls:
    // Every control with RIGHT-TO-LEFT in its name in
    // https://www.unicode.org/charts/PDF/U2000.pdf
    // U+200F RLM
    // U+202B RLE
    // U+202E RLO
    // U+2067 RLI
    //
    // BMP RTL:
    // https://www.unicode.org/roadmaps/bmp/
    // U+0590...U+08FF
    // U+FB1D...U+FDFF Hebrew presentation forms and
    //                 Arabic Presentation Forms A
    // U+FE70...U+FEFE Arabic Presentation Forms B (excl. BOM)
    //
    // Supplementary RTL:
    // https://www.unicode.org/roadmaps/smp/
    // U+10800...U+10FFF (Lead surrogate U+D802 or U+D803)
    // U+1E800...U+1EFFF (Lead surrogate U+D83A or U+D83B)
    let code_point = u32::from(c);
    if code_point < 0x0590 {
        // Below Hebrew
        return false;
    }
    if in_range32(code_point, 0x0900, 0xFB1D) {
        // Above Arabic Extended-A and below Hebrew presentation forms
        if in_inclusive_range32(code_point, 0x200F, 0x2067) {
            // In the range that contains the RTL controls
            return code_point == 0x200F
                || code_point == 0x202B
                || code_point == 0x202E
                || code_point == 0x2067;
        }
        return false;
    }
    if code_point > 0x1EFFF {
        // Above second astral RTL. (Emoji is here.)
        return false;
    }
    if in_range32(code_point, 0x11000, 0x1E800) {
        // Between astral RTL blocks
        return false;
    }
    if in_range32(code_point, 0xFEFF, 0x10800) {
        // Above Arabic Presentations Forms B (excl. BOM) and below first
        // astral RTL
        return false;
    }
    if in_range32(code_point, 0xFE00, 0xFE70) {
        // Between Arabic Presentations Forms
        return false;
    }
    true
}

/// Checks whether a UTF-16 code unit triggers right-to-left processing.
///
/// The check is done on a Unicode block basis without regard to assigned
/// vs. unassigned code points in the block. Hebrew presentation forms in
/// the Alphabetic Presentation Forms block are treated as if they formed
/// a block on their own (i.e. it treated as right-to-left). Additionally,
/// the four RIGHT-TO-LEFT FOO controls in General Punctuation are checked
/// for. Control characters that are technically bidi controls but do not
/// cause right-to-left behavior without the presence of right-to-left
/// characters or right-to-left controls are not checked for. As a special
/// case, U+FEFF is excluded from Arabic Presentation Forms-B.
///
/// Since supplementary-plane right-to-left blocks are identifiable from the
/// high surrogate without examining the low surrogate, this function returns
/// `true` for such high surrogates making the function suitable for handling
/// supplementary-plane text without decoding surrogate pairs to scalar
/// values. Obviously, such high surrogates are then reported as right-to-left
/// even if actually unpaired.
#[inline(always)]
pub fn is_utf16_code_unit_bidi(u: u16) -> bool {
    if u < 0x0590 {
        // Below Hebrew
        return false;
    }
    if in_range16(u, 0x0900, 0xD802) {
        // Above Arabic Extended-A and below first RTL surrogate
        if in_inclusive_range16(u, 0x200F, 0x2067) {
            // In the range that contains the RTL controls
            return u == 0x200F || u == 0x202B || u == 0x202E || u == 0x2067;
        }
        return false;
    }
    if in_range16(u, 0xD83C, 0xFB1D) {
        // Between astral RTL high surrogates and Hebrew presentation forms
        // (Emoji is here)
        return false;
    }
    if in_range16(u, 0xD804, 0xD83A) {
        // Between RTL high surragates
        return false;
    }
    if u > 0xFEFE {
        // Above Arabic Presentation Forms (excl. BOM)
        return false;
    }
    if in_range16(u, 0xFE00, 0xFE70) {
        // Between Arabic Presentations Forms
        return false;
    }
    true
}

/// Checks whether a potentially invalid UTF-8 buffer contains code points
/// that trigger right-to-left processing or is all-Latin1.
///
/// Possibly more efficient than performing the checks separately.
///
/// Returns `Latin1Bidi::Latin1` if `is_utf8_latin1()` would return `true`.
/// Otherwise, returns `Latin1Bidi::Bidi` if `is_utf8_bidi()` would return
/// `true`. Otherwise, returns `Latin1Bidi::LeftToRight`.
pub fn check_utf8_for_latin1_and_bidi(buffer: &[u8]) -> Latin1Bidi {
    if let Some(offset) = is_utf8_latin1_impl(buffer) {
        if is_utf8_bidi(&buffer[offset..]) {
            Latin1Bidi::Bidi
        } else {
            Latin1Bidi::LeftToRight
        }
    } else {
        Latin1Bidi::Latin1
    }
}

/// Checks whether a valid UTF-8 buffer contains code points
/// that trigger right-to-left processing or is all-Latin1.
///
/// Possibly more efficient than performing the checks separately.
///
/// Returns `Latin1Bidi::Latin1` if `is_str_latin1()` would return `true`.
/// Otherwise, returns `Latin1Bidi::Bidi` if `is_str_bidi()` would return
/// `true`. Otherwise, returns `Latin1Bidi::LeftToRight`.
pub fn check_str_for_latin1_and_bidi(buffer: &str) -> Latin1Bidi {
    // The transition from the latin1 check to the bidi check isn't
    // optimal but not tweaking it to perfection today.
    if let Some(offset) = is_str_latin1_impl(buffer) {
        if is_str_bidi(&buffer[offset..]) {
            Latin1Bidi::Bidi
        } else {
            Latin1Bidi::LeftToRight
        }
    } else {
        Latin1Bidi::Latin1
    }
}

/// Checks whether a potentially invalid UTF-16 buffer contains code points
/// that trigger right-to-left processing or is all-Latin1.
///
/// Possibly more efficient than performing the checks separately.
///
/// Returns `Latin1Bidi::Latin1` if `is_utf16_latin1()` would return `true`.
/// Otherwise, returns `Latin1Bidi::Bidi` if `is_utf16_bidi()` would return
/// `true`. Otherwise, returns `Latin1Bidi::LeftToRight`.
pub fn check_utf16_for_latin1_and_bidi(buffer: &[u16]) -> Latin1Bidi {
    check_utf16_for_latin1_and_bidi_impl(buffer)
}

/// Converts potentially-invalid UTF-8 to valid UTF-16 with errors replaced
/// with the REPLACEMENT CHARACTER.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer _plus one_.
///
/// Returns the number of `u16`s written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn convert_utf8_to_utf16(src: &[u8], dst: &mut [u16]) -> usize {
    // TODO: Can the requirement for dst to be at least one unit longer
    // be eliminated?
    assert!(dst.len() > src.len());
    let mut decoder = Utf8Decoder::new_inner();
    let mut total_read = 0usize;
    let mut total_written = 0usize;
    loop {
        let (result, read, written) =
            decoder.decode_to_utf16_raw(&src[total_read..], &mut dst[total_written..], true);
        total_read += read;
        total_written += written;
        match result {
            DecoderResult::InputEmpty => {
                return total_written;
            }
            DecoderResult::OutputFull => {
                unreachable!("The assert at the top of the function should have caught this.");
            }
            DecoderResult::Malformed(_, _) => {
                // There should always be space for the U+FFFD, because
                // otherwise we'd have gotten OutputFull already.
                dst[total_written] = 0xFFFD;
                total_written += 1;
            }
        }
    }
}

/// Converts valid UTF-8 to valid UTF-16.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// Returns the number of `u16`s written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn convert_str_to_utf16(src: &str, dst: &mut [u16]) -> usize {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    let bytes = src.as_bytes();
    let mut read = 0;
    let mut written = 0;
    'outer: loop {
        let mut byte = {
            let src_remaining = &bytes[read..];
            let dst_remaining = &mut dst[written..];
            let length = src_remaining.len();
            match ascii_to_basic_latin(src_remaining, dst_remaining) {
                None => {
                    written += length;
                    return written;
                }
                Some((non_ascii, consumed)) => {
                    read += consumed;
                    written += consumed;
                    non_ascii
                }
            }
        };
        'inner: loop {
            // At this point, `byte` is not included in `read`.
            if byte < 0xE0 {
                if byte >= 0x80 {
                    // Two-byte
                    let second = unsafe { *(bytes.get_unchecked(read + 1)) };
                    let point = ((u16::from(byte) & 0x1F) << 6) | (u16::from(second) & 0x3F);
                    unsafe { *(dst.get_unchecked_mut(written)) = point };
                    read += 2;
                    written += 1;
                } else {
                    // ASCII: write and go back to SIMD.
                    unsafe { *(dst.get_unchecked_mut(written)) = u16::from(byte) };
                    read += 1;
                    written += 1;
                    // Intuitively, we should go back to the outer loop only
                    // if byte is 0x30 or above, so as to avoid trashing on
                    // ASCII space, comma and period in non-Latin context.
                    // However, the extra branch seems to cost more than it's
                    // worth.
                    continue 'outer;
                }
            } else if byte < 0xF0 {
                // Three-byte
                let second = unsafe { *(bytes.get_unchecked(read + 1)) };
                let third = unsafe { *(bytes.get_unchecked(read + 2)) };
                let point = ((u16::from(byte) & 0xF) << 12)
                    | ((u16::from(second) & 0x3F) << 6)
                    | (u16::from(third) & 0x3F);
                unsafe { *(dst.get_unchecked_mut(written)) = point };
                read += 3;
                written += 1;
            } else {
                // Four-byte
                let second = unsafe { *(bytes.get_unchecked(read + 1)) };
                let third = unsafe { *(bytes.get_unchecked(read + 2)) };
                let fourth = unsafe { *(bytes.get_unchecked(read + 3)) };
                let point = ((u32::from(byte) & 0x7) << 18)
                    | ((u32::from(second) & 0x3F) << 12)
                    | ((u32::from(third) & 0x3F) << 6)
                    | (u32::from(fourth) & 0x3F);
                unsafe { *(dst.get_unchecked_mut(written)) = (0xD7C0 + (point >> 10)) as u16 };
                unsafe {
                    *(dst.get_unchecked_mut(written + 1)) = (0xDC00 + (point & 0x3FF)) as u16
                };
                read += 4;
                written += 2;
            }
            // The comparison is always < or == and never >, but including
            // > here to let the compiler assume that < is true if this
            // comparison is false.
            if read >= src.len() {
                return written;
            }
            byte = bytes[read];
            continue 'inner;
        }
    }
}

/// Converts potentially-invalid UTF-8 to valid UTF-16 signaling on error.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// Returns the number of `u16`s written or `None` if the input was invalid.
///
/// When the input was invalid, some output may have been written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
pub fn convert_utf8_to_utf16_without_replacement(src: &[u8], dst: &mut [u16]) -> Option<usize> {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    let (read, written) = convert_utf8_to_utf16_up_to_invalid(src, dst);
    if read == src.len() {
        return Some(written);
    }
    None
}

/// Converts potentially-invalid UTF-16 to valid UTF-8 with errors replaced
/// with the REPLACEMENT CHARACTER with potentially insufficient output
/// space.
///
/// Returns the number of code units read and the number of bytes written.
///
/// Guarantees that the bytes in the destination beyond the number of
/// bytes claimed as written by the second item of the return tuple
/// are left unmodified.
///
/// Not all code units are read if there isn't enough output space.
///
/// Note  that this method isn't designed for general streamability but for
/// not allocating memory for the worst case up front. Specifically,
/// if the input starts with or ends with an unpaired surrogate, those are
/// replaced with the REPLACEMENT CHARACTER.
///
/// Matches the semantics of `TextEncoder.encodeInto()` from the
/// Encoding Standard.
///
/// # Safety
///
/// If you want to convert into a `&mut str`, use
/// `convert_utf16_to_str_partial()` instead of using this function
/// together with the `unsafe` method `as_bytes_mut()` on `&mut str`.
#[inline(always)]
pub fn convert_utf16_to_utf8_partial(src: &[u16], dst: &mut [u8]) -> (usize, usize) {
    // The two functions called below are marked `inline(never)` to make
    // transitions from the hot part (first function) into the cold part
    // (second function) go through a return and another call to discourage
    // the CPU from speculating from the hot code into the cold code.
    // Letting the transitions be mere intra-function jumps, even to
    // basic blocks out-of-lined to the end of the function would wipe
    // away a quarter of Arabic encode performance on Haswell!
    // As of 2026, making even the first function `inline(never)`
    // is still needed on Zen 3!
    let (read, written) = convert_utf16_to_utf8_partial_inner(src, dst);
    if likely(read == src.len()) {
        return (read, written);
    }
    let (tail_read, tail_written) =
        convert_utf16_to_utf8_partial_tail(&src[read..], &mut dst[written..]);
    (read + tail_read, written + tail_written)
}

/// Converts potentially-invalid UTF-16 to valid UTF-8 with errors replaced
/// with the REPLACEMENT CHARACTER.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer times three.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
///
/// # Safety
///
/// If you want to convert into a `&mut str`, use `convert_utf16_to_str()`
/// instead of using this function together with the `unsafe` method
/// `as_bytes_mut()` on `&mut str`.
#[inline(always)]
pub fn convert_utf16_to_utf8(src: &[u16], dst: &mut [u8]) -> usize {
    assert!(dst.len() >= src.len() * 3);
    let (read, written) = convert_utf16_to_utf8_partial(src, dst);
    debug_assert_eq!(read, src.len());
    written
}

/// Converts potentially-invalid UTF-16 to valid UTF-8 with errors replaced
/// with the REPLACEMENT CHARACTER such that the validity of the output is
/// signaled using the Rust type system with potentially insufficient output
/// space.
///
/// Returns the number of code units read and the number of bytes written.
///
/// Not all code units are read if there isn't enough output space.
///
/// Note  that this method isn't designed for general streamability but for
/// not allocating memory for the worst case up front. Specifically,
/// if the input starts with or ends with an unpaired surrogate, those are
/// replaced with the REPLACEMENT CHARACTER.
pub fn convert_utf16_to_str_partial(src: &[u16], dst: &mut str) -> (usize, usize) {
    // SAFETY: We trust that `convert_utf16_to_utf8_partial` writes
    // valid UTF-8 and does not modify bytes beyond the reported logical written
    // length. We then overwrite a possible partial UTF-8 byte sequence after that.
    // Then the rest must be valid on the assumption that `dst` was valid to begin
    // with.
    //
    // In case of a panic, the `ScopeGuard` zeros the whole slice, which ensures
    // it's valid UTF-8 in an use-after-panic scenario when unwinding is enabled.
    // (Relevant only if there's a panic due to a crate-internal bug. Panics
    // arising from misuse of the public API don't need this guard and end up
    // zeroing the slice unnecessarily.)
    let mut bytes = scopeguard::guard(unsafe { dst.as_bytes_mut() }, |bytes| {
        bytes.iter_mut().for_each(|b| *b = 0)
    });
    let (read, written) = convert_utf16_to_utf8_partial(src, &mut bytes);
    let len = bytes.len();
    let mut trail = written;
    while trail < len && ((bytes[trail] & 0xC0) == 0x80) {
        bytes[trail] = 0;
        trail += 1;
    }
    // Defuse the zeroing guard.
    let _ = scopeguard::ScopeGuard::<&mut [u8], _>::into_inner(bytes);
    (read, written)
}

/// Converts potentially-invalid UTF-16 to valid UTF-8 with errors replaced
/// with the REPLACEMENT CHARACTER such that the validity of the output is
/// signaled using the Rust type system.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer times three.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[inline(always)]
pub fn convert_utf16_to_str(src: &[u16], dst: &mut str) -> usize {
    assert!(dst.len() >= src.len() * 3);
    let (read, written) = convert_utf16_to_str_partial(src, dst);
    debug_assert_eq!(read, src.len());
    written
}

/// Converts bytes whose unsigned value is interpreted as Unicode code point
/// (i.e. U+0000 to U+00FF, inclusive) to UTF-16.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// The number of `u16`s written equals the length of the source buffer.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn convert_latin1_to_utf16(src: &[u8], dst: &mut [u16]) {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    unpack_latin1(src, dst);
}

/// Converts bytes whose unsigned value is interpreted as Unicode code point
/// (i.e. U+0000 to U+00FF, inclusive) to UTF-8 with potentially insufficient
/// output space.
///
/// Returns the number of bytes read and the number of bytes written.
///
/// If the output isn't large enough, not all input is consumed.
///
/// # Safety
///
/// If you want to convert into a `&mut str`, use
/// `convert_latin1_to_str_partial()` instead of using this function
/// together with the `unsafe` method `as_bytes_mut()` on `&mut str`.
pub fn convert_latin1_to_utf8_partial(src: &[u8], dst: &mut [u8]) -> (usize, usize) {
    let src_len = src.len();
    let dst_len = dst.len();
    // Cap to avoid trailing garbage from `ascii_to_ascii` not getting overwritten
    // when the check for 2 available slots fails.
    let dst_len_capped = dst_len.saturating_sub(1);
    let mut total_read = 0usize;
    let mut total_written = 0usize;
    loop {
        if total_written < dst_len_capped {
            // src can't advance more than dst
            let src_left = src_len - total_read;
            let dst_left = dst_len_capped - total_written;
            let min_left = ::core::cmp::min(src_left, dst_left);
            if let Some((non_ascii, consumed)) =
                { ascii_to_ascii(&src[total_read..], &mut dst[total_written..dst_len_capped]) }
            {
                total_read += consumed;
                total_written += consumed;
                if total_written.checked_add(2).unwrap() > dst_len {
                    return (total_read, total_written);
                }

                total_read += 1; // consume `non_ascii`

                dst[total_written] = (non_ascii >> 6) | 0xC0;
                total_written += 1;
                dst[total_written] = (non_ascii & 0x3F) | 0x80;
                total_written += 1;
                continue;
            }
            total_read += min_left;
            total_written += min_left;
        }
        if total_read < src.len() && total_written < dst.len() {
            let b = src[total_read];
            if b.is_ascii() {
                dst[total_written] = b;
                total_read += 1;
                total_written += 1;
            }
        }
        return (total_read, total_written);
    }
}

/// Converts bytes whose unsigned value is interpreted as Unicode code point
/// (i.e. U+0000 to U+00FF, inclusive) to UTF-8.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer times two.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
///
/// # Safety
///
/// Note that this function may write garbage beyond the number of bytes
/// indicated by the return value, so using a `&mut str` interpreted as
/// `&mut [u8]` as the destination is not safe. If you want to convert into
/// a `&mut str`, use `convert_latin1_to_str()` instead of this function.
#[inline]
pub fn convert_latin1_to_utf8(src: &[u8], dst: &mut [u8]) -> usize {
    assert!(
        dst.len() >= src.len() * 2,
        "Destination must not be shorter than the source times two."
    );
    let (read, written) = convert_latin1_to_utf8_partial(src, dst);
    debug_assert_eq!(read, src.len());
    written
}

/// Converts bytes whose unsigned value is interpreted as Unicode code point
/// (i.e. U+0000 to U+00FF, inclusive) to UTF-8 such that the validity of the
/// output is signaled using the Rust type system with potentially insufficient
/// output space.
///
/// Returns the number of bytes read and the number of bytes written.
///
/// If the output isn't large enough, not all input is consumed.
#[inline]
pub fn convert_latin1_to_str_partial(src: &[u8], dst: &mut str) -> (usize, usize) {
    // In case of a panic, the `ScopeGuard` zeros the whole slice, which ensures
    // it's valid UTF-8 in an use-after-panic scenario when unwinding is enabled.
    // (Relevant only if there's a panic due to a crate-internal bug. Panics
    // arising from misuse of the public API don't need this guard and end up
    // zeroing the slice unnecessarily.)
    let mut bytes = scopeguard::guard(unsafe { dst.as_bytes_mut() }, |bytes| {
        bytes.iter_mut().for_each(|b| *b = 0)
    });
    let (read, written) = convert_latin1_to_utf8_partial(src, &mut bytes);
    let len = bytes.len();
    let mut trail = written;
    let max = ::core::cmp::min(len, trail + MAX_STRIDE_SIZE);
    while trail < max {
        bytes[trail] = 0;
        trail += 1;
    }
    while trail < len && ((bytes[trail] & 0xC0) == 0x80) {
        bytes[trail] = 0;
        trail += 1;
    }
    // Defuse the zeroing guard.
    let _ = scopeguard::ScopeGuard::<&mut [u8], _>::into_inner(bytes);
    (read, written)
}

/// Converts bytes whose unsigned value is interpreted as Unicode code point
/// (i.e. U+0000 to U+00FF, inclusive) to UTF-8 such that the validity of the
/// output is signaled using the Rust type system.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer times two.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[inline]
pub fn convert_latin1_to_str(src: &[u8], dst: &mut str) -> usize {
    assert!(
        dst.len() >= src.len() * 2,
        "Destination must not be shorter than the source times two."
    );
    let (read, written) = convert_latin1_to_str_partial(src, dst);
    debug_assert_eq!(read, src.len());
    written
}

/// If the input is valid UTF-8 representing only Unicode code points from
/// U+0000 to U+00FF, inclusive, converts the input into output that
/// represents the value of each code point as the unsigned byte value of
/// each output byte.
///
/// If the input does not fulfill the condition stated above, this function
/// panics if debug assertions are enabled (and fuzzing isn't) and otherwise
/// does something that is memory-safe without any promises about any
/// properties of the output. In particular, callers shouldn't assume the
/// output to be the same across crate versions or CPU architectures and
/// should not assume that non-ASCII input can't map to ASCII output.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
///
/// If debug assertions are enabled (and not fuzzing) and the input is
/// not in the range U+0000 to U+00FF, inclusive.
pub fn convert_utf8_to_latin1_lossy(src: &[u8], dst: &mut [u8]) -> usize {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    non_fuzz_debug_assert!(is_utf8_latin1(src));
    let src_len = src.len();
    let mut total_read = 0usize;
    let mut total_written = 0usize;
    loop {
        // dst can't advance more than src
        let src_left = src_len - total_read;
        if let Some((non_ascii, consumed)) =
            { ascii_to_ascii(&src[total_read..], &mut dst[total_written..]) }
        {
            total_read += consumed + 1;
            total_written += consumed;

            if total_read == src_len {
                return total_written;
            }

            let trail = src[total_read];
            total_read += 1;

            dst[total_written] = ((non_ascii & 0x1F) << 6) | (trail & 0x3F);
            total_written += 1;
            continue;
        }
        return total_written + src_left;
    }
}

/// If the input is valid UTF-16 representing only Unicode code points from
/// U+0000 to U+00FF, inclusive, converts the input into output that
/// represents the value of each code point as the unsigned byte value of
/// each output byte.
///
/// If the input does not fulfill the condition stated above, does something
/// that is memory-safe without any promises about any properties of the
/// output and will probably assert in debug builds in future versions.
/// In particular, callers shouldn't assume the output to be the same across
/// crate versions or CPU architectures and should not assume that non-ASCII
/// input can't map to ASCII output.
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// The number of bytes written equals the length of the source buffer.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
///
/// (Probably in future versions if debug assertions are enabled (and not
/// fuzzing) and the input is not in the range U+0000 to U+00FF, inclusive.)
pub fn convert_utf16_to_latin1_lossy(src: &[u16], dst: &mut [u8]) {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    // non_fuzz_debug_assert!(is_utf16_latin1(src));
    pack_latin1(src, dst);
}

/// Converts bytes whose unsigned value is interpreted as Unicode code point
/// (i.e. U+0000 to U+00FF, inclusive) to UTF-8.
///
/// Borrows if input is ASCII-only. Performs a single heap allocation
/// otherwise.
///
/// Only available if the `alloc` feature is enabled (enabled by default).
#[cfg(feature = "alloc")]
pub fn decode_latin1<'a>(bytes: &'a [u8]) -> Cow<'a, str> {
    let up_to = ascii_valid_up_to(bytes);
    // >= makes later things optimize better than ==
    if up_to >= bytes.len() {
        debug_assert_eq!(up_to, bytes.len());
        // SAFETY: We've checked that `bytes` is valid UTF-8, since
        // it's ASCII.
        let s: &str = unsafe { ::core::str::from_utf8_unchecked(bytes) };
        return Cow::Borrowed(s);
    }
    let (head, tail) = bytes.split_at(up_to);
    let capacity = head.len() + tail.len() * 2;
    let mut vec = Vec::with_capacity(capacity);
    vec.extend_from_slice(head);
    let old_len = vec.len();
    let spare_capacity = crate::minimally_init(vec.spare_capacity_mut());
    debug_assert_eq!(old_len, up_to);
    let written = convert_latin1_to_utf8(tail, spare_capacity);
    debug_assert!(written <= spare_capacity.len());
    let new_len = old_len + written;
    assert!(new_len <= vec.capacity());
    // SAFETY: We trust that `convert_latin1_to_utf8` wrote valid UTF-8
    // to `spare_capacity[..written]`. Also, regarding the information
    // disclosure risk of `minimally_init`, this also means trusting
    // that every byte of `spare_capacity[..written]` got overwritten.
    // (We're no worse off than before regarding
    // `spare_capacity[written..]`) which remains not logically exposed.)
    // We (non-debug )asserted immediately above that `new_len` conforms
    // to the invariant that it must not exceed `vec.capacity()`.
    unsafe {
        vec.set_len(new_len);
    }
    // SAFETY: We trust that `ascii_valid_up_to` and
    // `convert_latin1_to_utf8` are correct and the `Vec` contains
    // valid UTF-8.
    Cow::Owned(unsafe { String::from_utf8_unchecked(vec) })
}

/// If the input is valid UTF-8 representing only Unicode code points from
/// U+0000 to U+00FF, inclusive, converts the input into output that
/// represents the value of each code point as the unsigned byte value of
/// each output byte.
///
/// If the input does not fulfill the condition stated above, this function
/// panics if debug assertions are enabled (and fuzzing isn't) and otherwise
/// does something that is memory-safe without any promises about any
/// properties of the output. In particular, callers shouldn't assume the
/// output to be the same across crate versions or CPU architectures and
/// should not assume that non-ASCII input can't map to ASCII output.
///
/// Borrows if input is ASCII-only. Performs a single heap allocation
/// otherwise.
///
/// Only available if the `alloc` feature is enabled (enabled by default).
#[cfg(feature = "alloc")]
pub fn encode_latin1_lossy<'a>(string: &'a str) -> Cow<'a, [u8]> {
    let bytes = string.as_bytes();
    let up_to = ascii_valid_up_to(bytes);
    // >= makes later things optimize better than ==
    if up_to >= bytes.len() {
        debug_assert_eq!(up_to, bytes.len());
        return Cow::Borrowed(bytes);
    }
    let (head, tail) = bytes.split_at(up_to);
    let capacity = bytes.len();
    let mut vec = Vec::with_capacity(capacity);
    vec.extend_from_slice(head);
    let old_len = vec.len();
    let spare_capacity = crate::minimally_init(vec.spare_capacity_mut());
    debug_assert_eq!(old_len, up_to);
    let written = convert_utf8_to_latin1_lossy(tail, spare_capacity);
    debug_assert!(written <= spare_capacity.len());
    let new_len = old_len + written;
    assert!(new_len <= vec.capacity());
    // SAFETY: We trust that `convert_utf8_to_latin1_lossy` wrote to every
    // element of `spare_capacity[..written]`.
    // (We're no worse off than before regarding
    // `spare_capacity[written..]`) which remains not logically exposed.)
    // We (non-debug )asserted immediately above that `new_len` conforms
    // to the invariant that it must not exceed `vec.capacity()`.
    unsafe {
        vec.set_len(new_len);
    }
    Cow::Owned(vec)
}

cfg_if! {
    if #[cfg(all(
        feature = "simd-accel",
        target_endian = "little",
    ))] {
        pub(crate) use crate::simd_funcs::validate_bmp_stride;
        pub(crate) use crate::simd_funcs::validate_latin1_str_stride;

        #[inline(always)]
        #[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
        fn is_str_latin1_impl(buffer: &str) -> Option<usize> {
            let mut consumed = 0;
            let (strides, tail) = buffer.as_bytes().as_chunks::<STRIDE>();
            for stride in strides.iter() {
                if let Some(pos) = validate_latin1_str_stride(stride) {
                    return Some(consumed + pos);
                }
                consumed += STRIDE;
            }
            for slot in tail.iter() {
                if *slot > 0xC3 {
                    return Some(consumed);
                }
                consumed += 1;
            }
            None
        }
    } else {
        #[inline(always)]
        pub(crate) fn validate_bmp_stride(stride: &[u16; STRIDE]) -> Option<usize> {
            if (stride[0] & 0xF800 != 0xD800)
                && (stride[1] & 0xF800 != 0xD800)
                && (stride[2] & 0xF800 != 0xD800)
                && (stride[3] & 0xF800 != 0xD800)
                && (stride[4] & 0xF800 != 0xD800)
                && (stride[5] & 0xF800 != 0xD800)
                && (stride[6] & 0xF800 != 0xD800)
                && (stride[7] & 0xF800 != 0xD800)
                && (stride[8] & 0xF800 != 0xD800)
                && (stride[9] & 0xF800 != 0xD800)
                && (stride[10] & 0xF800 != 0xD800)
                && (stride[11] & 0xF800 != 0xD800)
                && (stride[12] & 0xF800 != 0xD800)
                && (stride[13] & 0xF800 != 0xD800)
                && (stride[14] & 0xF800 != 0xD800)
                && (stride[15] & 0xF800 != 0xD800)
            {
                return None;
            }
            for (i, c) in stride.iter().enumerate() {
                if c & 0xF800 == 0xD800 {
                    return Some(i);
                }
            }
            debug_assert!(false);
            None
        }

        #[inline(always)]
        fn is_str_latin1_impl(buffer: &str) -> Option<usize> {
            let mut bytes = buffer.as_bytes();
            let mut total = 0;
            loop {
                if let Some((byte, offset)) = validate_ascii(bytes) {
                    total += offset;
                    if byte > 0xC3 {
                        return Some(total);
                    }
                    bytes = &bytes[offset + 2..];
                    total += 2;
                } else {
                    return None;
                }
            }
        }
    }
}

/// Returns the index of the first unpaired surrogate or, if the input is
/// valid UTF-16 in its entirety, the length of the input.
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn utf16_valid_up_to(buffer: &[u16]) -> usize {
    let mut consumed = 0usize;
    'outer: loop {
        let (strides, tail) = &buffer[consumed..].as_chunks::<STRIDE>();
        // This loop is only broken out of as goto forward.
        #[allow(clippy::never_loop)]
        'bmp: loop {
            for stride in strides.iter() {
                if let Some(pos) = validate_bmp_stride(stride) {
                    consumed += pos;
                    break 'bmp;
                }
                consumed += STRIDE;
            }
            for slot in tail.iter() {
                if *slot & 0xF800 == 0xD800 {
                    break 'bmp;
                }
                consumed += 1;
            }
            debug_assert_eq!(consumed, buffer.len());
            return consumed;
        }
        // `buffer[consumed]` is now in range and is a surrogate.
        let mut unit = buffer[consumed];
        let mut unit_minus_surrogate_start = unit.wrapping_sub(0xD800);
        debug_assert!(unit_minus_surrogate_start <= (0xDFFF - 0xD800));
        'surrogate: loop {
            if unit_minus_surrogate_start > (0xDBFF - 0xD800) {
                // Not high surrogate. Must be unpaired low surrogate.
                return consumed;
            }
            // high surrogate
            let next = consumed + 1;
            if next == buffer.len() {
                // Buffer ends with unpaired high surrogate
                return consumed;
            }
            let second = buffer[next];
            let second_minus_low_surrogate_start = second.wrapping_sub(0xDC00);
            if second_minus_low_surrogate_start > (0xDFFF - 0xDC00) {
                // The next unit is not a low surrogate. We had an unpaired.
                // high surrogate.
                return consumed;
            }
            // The next code unit is a low surrogate. Advance position.
            consumed = next + 1;
            // We could just do `continue 'outer;` here, and that would
            // be optimal for emoji and the occasional non-BMP Hanzi.
            // However, that would be bad for non-BMP scripts.
            loop {
                if consumed == buffer.len() {
                    // End of buffer.
                    return consumed;
                }
                unit = buffer[consumed];
                unit_minus_surrogate_start = unit.wrapping_sub(0xD800);
                if unit_minus_surrogate_start <= (0xDFFF - 0xD800) {
                    continue 'surrogate;
                }
                // We got a non-surrogate.
                consumed += 1;
                // Avoid bouncing to SIMD for ASCII spaces.
                if unit == 0x0020 {
                    continue;
                }
                continue 'outer;
            }
        }
    }
}

/// Returns the index of first byte that starts an invalid byte
/// sequence or a non-Latin1 byte sequence, or the length of the
/// string if there are neither.
pub fn utf8_latin1_up_to(buffer: &[u8]) -> usize {
    is_utf8_latin1_impl(buffer).unwrap_or(buffer.len())
}

/// Returns the index of first byte that starts a non-Latin1 byte
/// sequence, or the length of the string if there are none.
pub fn str_latin1_up_to(buffer: &str) -> usize {
    is_str_latin1_impl(buffer).unwrap_or(buffer.len())
}

/// Replaces unpaired surrogates in the input with the REPLACEMENT CHARACTER.
#[inline]
pub fn ensure_utf16_validity(buffer: &mut [u16]) {
    let mut offset = 0;
    loop {
        offset += utf16_valid_up_to(&buffer[offset..]);
        if offset == buffer.len() {
            return;
        }
        buffer[offset] = 0xFFFD;
        offset += 1;
    }
}

/// Copies ASCII from source to destination up to the first non-ASCII byte
/// (or the end of the input if it is ASCII in its entirety).
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
pub fn copy_ascii_to_ascii(src: &[u8], dst: &mut [u8]) -> usize {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    if let Some((_, consumed)) = { ascii_to_ascii(src, dst) } {
        consumed
    } else {
        src.len()
    }
}

/// Copies ASCII from source to destination zero-extending it to UTF-16 up to
/// the first non-ASCII byte (or the end of the input if it is ASCII in its
/// entirety).
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// Returns the number of `u16`s written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn copy_ascii_to_basic_latin(src: &[u8], dst: &mut [u16]) -> usize {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    if let Some((_, consumed)) = { ascii_to_basic_latin(src, dst) } {
        consumed
    } else {
        src.len()
    }
}

/// Copies Basic Latin from source to destination narrowing it to ASCII up to
/// the first non-Basic Latin code unit (or the end of the input if it is
/// Basic Latin in its entirety).
///
/// The length of the destination buffer must be at least the length of the
/// source buffer.
///
/// Returns the number of bytes written.
///
/// # Panics
///
/// Panics if the destination buffer is shorter than stated above.
#[crate::multiversion(targets("x86_64+avx2+bmi1", "x86+avx2+bmi1"))]
pub fn copy_basic_latin_to_ascii(src: &[u16], dst: &mut [u8]) -> usize {
    assert!(
        dst.len() >= src.len(),
        "Destination must not be shorter than the source."
    );
    if let Some((_, consumed)) = { basic_latin_to_ascii(src, dst) } {
        consumed
    } else {
        src.len()
    }
}

// Any copyright to the test code below this comment is dedicated to the
// Public Domain. http://creativecommons.org/publicdomain/zero/1.0/

#[cfg(all(test, feature = "alloc"))]
mod tests {
    use super::*;

    #[test]
    fn test_is_ascii_success() {
        let mut src: Vec<u8> = Vec::with_capacity(128);
        src.resize(128, 0);
        for i in 0..src.len() {
            src[i] = i as u8;
        }
        for i in 0..src.len() {
            assert!(is_ascii(&src[i..]));
        }
    }

    #[test]
    fn test_is_ascii_fail() {
        let mut src: Vec<u8> = Vec::with_capacity(128);
        src.resize(128, 0);
        for i in 0..src.len() {
            src[i] = i as u8;
        }
        for i in 0..src.len() {
            let tail = &mut src[i..];
            for j in 0..tail.len() {
                tail[j] = 0xA0;
                assert!(!is_ascii(tail));
            }
        }
    }

    #[test]
    fn test_is_basic_latin_success() {
        let mut src: Vec<u16> = Vec::with_capacity(128);
        src.resize(128, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            assert!(is_basic_latin(&src[i..]));
        }
    }

    #[test]
    fn test_is_basic_latin_fail() {
        let mut src: Vec<u16> = Vec::with_capacity(128);
        src.resize(128, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            let tail = &mut src[i..];
            for j in 0..tail.len() {
                tail[j] = 0xA0;
                assert!(!is_basic_latin(tail));
            }
        }
    }

    #[test]
    fn test_is_utf16_latin1_success() {
        let mut src: Vec<u16> = Vec::with_capacity(256);
        src.resize(256, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            assert!(is_utf16_latin1(&src[i..]));
            assert_eq!(
                check_utf16_for_latin1_and_bidi(&src[i..]),
                Latin1Bidi::Latin1
            );
        }
    }

    #[test]
    fn test_is_utf16_latin1_fail() {
        let len = if cfg!(miri) { 64 } else { 256 }; // Miri is too slow
        let mut src: Vec<u16> = Vec::with_capacity(len);
        src.resize(len, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            let tail = &mut src[i..];
            for j in 0..tail.len() {
                tail[j] = 0x100 + j as u16;
                assert!(!is_utf16_latin1(tail));
                assert_ne!(check_utf16_for_latin1_and_bidi(tail), Latin1Bidi::Latin1);
            }
        }
    }

    #[test]
    fn test_is_str_latin1_success() {
        let len = if cfg!(miri) { 64 } else { 256 }; // Miri is too slow
        let mut src: Vec<u16> = Vec::with_capacity(len);
        src.resize(len, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            let s = String::from_utf16(&src[i..]).unwrap();
            assert!(is_str_latin1(&s[..]));
            assert_eq!(check_str_for_latin1_and_bidi(&s[..]), Latin1Bidi::Latin1);
        }
    }

    #[test]
    fn test_is_str_latin1_fail() {
        let len = if cfg!(miri) { 32 } else { 256 }; // Miri is too slow
        let mut src: Vec<u16> = Vec::with_capacity(len);
        src.resize(len, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            let tail = &mut src[i..];
            for j in 0..tail.len() {
                tail[j] = 0x100 + j as u16;
                let s = String::from_utf16(tail).unwrap();
                assert!(!is_str_latin1(&s[..]));
                assert_ne!(check_str_for_latin1_and_bidi(&s[..]), Latin1Bidi::Latin1);
            }
        }
    }

    #[test]
    fn test_is_utf8_latin1_success() {
        let len = if cfg!(miri) { 64 } else { 256 }; // Miri is too slow
        let mut src: Vec<u16> = Vec::with_capacity(len);
        src.resize(len, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            let s = String::from_utf16(&src[i..]).unwrap();
            assert!(is_utf8_latin1(s.as_bytes()));
            assert_eq!(
                check_utf8_for_latin1_and_bidi(s.as_bytes()),
                Latin1Bidi::Latin1
            );
        }
    }

    #[test]
    fn test_is_utf8_latin1_fail() {
        let len = if cfg!(miri) { 32 } else { 256 }; // Miri is too slow
        let mut src: Vec<u16> = Vec::with_capacity(len);
        src.resize(len, 0);
        for i in 0..src.len() {
            src[i] = i as u16;
        }
        for i in 0..src.len() {
            let tail = &mut src[i..];
            for j in 0..tail.len() {
                tail[j] = 0x100 + j as u16;
                let s = String::from_utf16(tail).unwrap();
                assert!(!is_utf8_latin1(s.as_bytes()));
                assert_ne!(
                    check_utf8_for_latin1_and_bidi(s.as_bytes()),
                    Latin1Bidi::Latin1
                );
            }
        }
    }

    #[test]
    fn test_is_utf8_latin1_invalid() {
        assert!(!is_utf8_latin1(b"\xC3"));
        assert!(!is_utf8_latin1(b"a\xC3"));
        assert!(!is_utf8_latin1(b"\xFF"));
        assert!(!is_utf8_latin1(b"a\xFF"));
        assert!(!is_utf8_latin1(b"\xC3\xFF"));
        assert!(!is_utf8_latin1(b"a\xC3\xFF"));
    }

    #[test]
    fn test_convert_utf8_to_utf16() {
        let src = "abcdefghijklmnopqrstu\u{1F4A9}v\u{2603}w\u{00B6}xyzz";
        let mut dst: Vec<u16> = Vec::with_capacity(src.len() + 1);
        dst.resize(src.len() + 1, 0);
        let len = convert_utf8_to_utf16(src.as_bytes(), &mut dst[..]);
        dst.truncate(len);
        let reference: Vec<u16> = src.encode_utf16().collect();
        assert_eq!(dst, reference);
    }

    #[test]
    fn test_convert_str_to_utf16() {
        let src = "abcdefghijklmnopqrstu\u{1F4A9}v\u{2603}w\u{00B6}xyzz";
        let mut dst: Vec<u16> = Vec::with_capacity(src.len());
        dst.resize(src.len(), 0);
        let len = convert_str_to_utf16(src, &mut dst[..]);
        dst.truncate(len);
        let reference: Vec<u16> = src.encode_utf16().collect();
        assert_eq!(dst, reference);
    }

    #[test]
    fn test_convert_utf16_to_utf8_partial() {
        let reference = "abcdefghijklmnopqrstu\u{1F4A9}v\u{2603}w\u{00B6}xyzz";
        let src: Vec<u16> = reference.encode_utf16().collect();
        let mut dst: Vec<u8> = Vec::with_capacity(src.len() * 3 + 1);
        dst.resize(src.len() * 3 + 1, 0);
        let (read, written) = convert_utf16_to_utf8_partial(&src[..], &mut dst[..24]);
        let len = written + convert_utf16_to_utf8(&src[read..], &mut dst[written..]);
        dst.truncate(len);
        assert_eq!(dst, reference.as_bytes());
    }

    #[test]
    fn test_convert_utf16_to_utf8_partial_leaves_tail_unmodified() {
        // A non-Basic Latin code unit after an ASCII run of every length
        // up to past a double stride, so that the SIMD stride functions see
        // it at every position.
        for &non_ascii in &[0x00CAu16, 0x00E4, 0x4E00, 0xD83D, 0xDCA9] {
            for prefix_len in 0..70 {
                let mut src = vec![u16::from(b'a'); prefix_len];
                src.push(non_ascii);
                src.extend_from_slice(&[u16::from(b'b'); 40]);
                for dst_len in 0..=(src.len() * 3) {
                    let mut dst = vec![b'q'; dst_len];
                    let (_, written) = convert_utf16_to_utf8_partial(&src[..], &mut dst[..]);
                    assert!(dst[written..].iter().all(|&b| b == b'q'));

                    let mut string = "q".repeat(dst_len);
                    convert_utf16_to_str_partial(&src[..], &mut string[..]);
                    assert!(core::str::from_utf8(string.as_bytes()).is_ok());
                }
            }
        }
    }

    #[test]
    fn test_convert_latin1_to_utf8_partial_leaves_tail_unmodified() {
        // A non-ASCII byte after an ASCII run of every length up to past a
        // double stride, so that the SIMD stride functions see it at every
        // position.
        for &non_ascii in &[0x80u8, 0xA0, 0xCA, 0xE4, 0xFF] {
            for prefix_len in 0..70 {
                let mut src = vec![b'a'; prefix_len];
                src.push(non_ascii);
                src.extend_from_slice(&[b'b'; 40]);
                for dst_len in 0..=(src.len() * 2) {
                    let mut dst = vec![b'q'; dst_len];
                    let (_, written) = convert_latin1_to_utf8_partial(&src[..], &mut dst[..]);
                    assert!(dst[written..].iter().all(|&b| b == b'q'));

                    let mut string = "q".repeat(dst_len);
                    convert_latin1_to_str_partial(&src[..], &mut string[..]);
                    assert!(core::str::from_utf8(string.as_bytes()).is_ok());
                }
            }
        }
    }

    #[test]
    fn test_convert_utf16_to_utf8() {
        let reference = "abcdefghijklmnopqrstu\u{1F4A9}v\u{2603}w\u{00B6}xyzz";
        let src: Vec<u16> = reference.encode_utf16().collect();
        let mut dst: Vec<u8> = Vec::with_capacity(src.len() * 3 + 1);
        dst.resize(src.len() * 3 + 1, 0);
        let len = convert_utf16_to_utf8(&src[..], &mut dst[..]);
        dst.truncate(len);
        assert_eq!(dst, reference.as_bytes());
    }

    #[test]
    fn test_convert_latin1_to_utf16() {
        let mut src: Vec<u8> = Vec::with_capacity(256);
        src.resize(256, 0);
        let mut reference: Vec<u16> = Vec::with_capacity(256);
        reference.resize(256, 0);
        for i in 0..256 {
            src[i] = i as u8;
            reference[i] = i as u16;
        }
        let mut dst: Vec<u16> = Vec::with_capacity(src.len());
        dst.resize(src.len(), 0);
        convert_latin1_to_utf16(&src[..], &mut dst[..]);
        assert_eq!(dst, reference);
    }

    #[test]
    fn test_convert_latin1_to_utf8_partial() {
        let mut dst = [0u8, 2];
        let (read, written) = convert_latin1_to_utf8_partial(b"a\xFF", &mut dst[..]);
        assert_eq!(read, 1);
        assert_eq!(written, 1);
    }

    #[test]
    fn test_convert_latin1_to_utf8() {
        let mut src: Vec<u8> = Vec::with_capacity(256);
        src.resize(256, 0);
        let mut reference: Vec<u16> = Vec::with_capacity(256);
        reference.resize(256, 0);
        for i in 0..256 {
            src[i] = i as u8;
            reference[i] = i as u16;
        }
        let s = String::from_utf16(&reference[..]).unwrap();
        let mut dst: Vec<u8> = Vec::with_capacity(src.len() * 2);
        dst.resize(src.len() * 2, 0);
        let len = convert_latin1_to_utf8(&src[..], &mut dst[..]);
        dst.truncate(len);
        assert_eq!(&dst[..], s.as_bytes());
    }

    #[test]
    fn test_convert_utf8_to_latin1_lossy() {
        let mut reference: Vec<u8> = Vec::with_capacity(256);
        reference.resize(256, 0);
        let mut src16: Vec<u16> = Vec::with_capacity(256);
        src16.resize(256, 0);
        for i in 0..256 {
            src16[i] = i as u16;
            reference[i] = i as u8;
        }
        let src = String::from_utf16(&src16[..]).unwrap();
        let mut dst: Vec<u8> = Vec::with_capacity(src.len());
        dst.resize(src.len(), 0);
        let len = convert_utf8_to_latin1_lossy(src.as_bytes(), &mut dst[..]);
        dst.truncate(len);
        assert_eq!(dst, reference);
    }

    #[cfg(all(debug_assertions, not(fuzzing)))]
    #[test]
    #[should_panic]
    fn test_convert_utf8_to_latin1_lossy_panics() {
        let mut dst = [0u8; 16];
        let _ = convert_utf8_to_latin1_lossy("\u{100}".as_bytes(), &mut dst[..]);
    }

    #[test]
    fn test_convert_utf16_to_latin1_lossy() {
        let mut src: Vec<u16> = Vec::with_capacity(256);
        src.resize(256, 0);
        let mut reference: Vec<u8> = Vec::with_capacity(256);
        reference.resize(256, 0);
        for i in 0..256 {
            src[i] = i as u16;
            reference[i] = i as u8;
        }
        let mut dst: Vec<u8> = Vec::with_capacity(src.len());
        dst.resize(src.len(), 0);
        convert_utf16_to_latin1_lossy(&src[..], &mut dst[..]);
        assert_eq!(dst, reference);
    }

    #[test]
    // #[should_panic]
    fn test_convert_utf16_to_latin1_lossy_panics() {
        let mut dst = [0u8; 16];
        let _ = convert_utf16_to_latin1_lossy(&[0x0100u16], &mut dst[..]);
    }

    #[test]
    fn test_utf16_valid_up_to() {
        let valid = vec![
            0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0x2603u16,
            0xD83Du16, 0xDCA9u16, 0x00B6u16,
        ];
        assert_eq!(utf16_valid_up_to(&valid[..]), 16);
        let lone_high = vec![
            0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0x2603u16, 0xD83Du16, 0x00B6u16,
        ];
        assert_eq!(utf16_valid_up_to(&lone_high[..]), 14);
        let lone_low = vec![
            0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0x2603u16, 0xDCA9u16, 0x00B6u16,
        ];
        assert_eq!(utf16_valid_up_to(&lone_low[..]), 14);
        let lone_high_at_end = vec![
            0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0x2603u16, 0x00B6u16, 0xD83Du16,
        ];
        assert_eq!(utf16_valid_up_to(&lone_high_at_end[..]), 15);
    }

    #[test]
    fn test_ensure_utf16_validity() {
        let mut src = vec![
            0u16, 0xD83Du16, 0u16, 0u16, 0u16, 0xD83Du16, 0xDCA9u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0u16, 0xDCA9u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
        ];
        let reference = vec![
            0u16, 0xFFFDu16, 0u16, 0u16, 0u16, 0xD83Du16, 0xDCA9u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0u16, 0xFFFDu16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
            0u16, 0u16, 0u16, 0u16, 0u16, 0u16,
        ];
        ensure_utf16_validity(&mut src[..]);
        assert_eq!(src, reference);
    }

    #[test]
    fn test_is_char_bidi() {
        assert!(!is_char_bidi('a'));
        assert!(!is_char_bidi('\u{03B1}'));
        assert!(!is_char_bidi('\u{3041}'));
        assert!(!is_char_bidi('\u{1F4A9}'));
        assert!(!is_char_bidi('\u{FE00}'));
        assert!(!is_char_bidi('\u{202C}'));
        assert!(!is_char_bidi('\u{FEFF}'));
        assert!(is_char_bidi('\u{0590}'));
        assert!(is_char_bidi('\u{08FF}'));
        assert!(is_char_bidi('\u{061C}'));
        assert!(is_char_bidi('\u{FB50}'));
        assert!(is_char_bidi('\u{FDFF}'));
        assert!(is_char_bidi('\u{FE70}'));
        assert!(is_char_bidi('\u{FEFE}'));
        assert!(is_char_bidi('\u{200F}'));
        assert!(is_char_bidi('\u{202B}'));
        assert!(is_char_bidi('\u{202E}'));
        assert!(is_char_bidi('\u{2067}'));
        assert!(is_char_bidi('\u{10800}'));
        assert!(is_char_bidi('\u{10FFF}'));
        assert!(is_char_bidi('\u{1E800}'));
        assert!(is_char_bidi('\u{1EFFF}'));
    }

    #[test]
    fn test_is_utf16_code_unit_bidi() {
        assert!(!is_utf16_code_unit_bidi(0x0062));
        assert!(!is_utf16_code_unit_bidi(0x03B1));
        assert!(!is_utf16_code_unit_bidi(0x3041));
        assert!(!is_utf16_code_unit_bidi(0xD801));
        assert!(!is_utf16_code_unit_bidi(0xFE00));
        assert!(!is_utf16_code_unit_bidi(0x202C));
        assert!(!is_utf16_code_unit_bidi(0xFEFF));
        assert!(is_utf16_code_unit_bidi(0x0590));
        assert!(is_utf16_code_unit_bidi(0x08FF));
        assert!(is_utf16_code_unit_bidi(0x061C));
        assert!(is_utf16_code_unit_bidi(0xFB1D));
        assert!(is_utf16_code_unit_bidi(0xFB50));
        assert!(is_utf16_code_unit_bidi(0xFDFF));
        assert!(is_utf16_code_unit_bidi(0xFE70));
        assert!(is_utf16_code_unit_bidi(0xFEFE));
        assert!(is_utf16_code_unit_bidi(0x200F));
        assert!(is_utf16_code_unit_bidi(0x202B));
        assert!(is_utf16_code_unit_bidi(0x202E));
        assert!(is_utf16_code_unit_bidi(0x2067));
        assert!(is_utf16_code_unit_bidi(0xD802));
        assert!(is_utf16_code_unit_bidi(0xD803));
        assert!(is_utf16_code_unit_bidi(0xD83A));
        assert!(is_utf16_code_unit_bidi(0xD83B));
    }

    #[test]
    fn test_is_str_bidi() {
        assert!(!is_str_bidi("abcdefghijklmnopaabcdefghijklmnop"));
        assert!(!is_str_bidi("abcdefghijklmnop\u{03B1}abcdefghijklmnop"));
        assert!(!is_str_bidi("abcdefghijklmnop\u{3041}abcdefghijklmnop"));
        assert!(!is_str_bidi("abcdefghijklmnop\u{1F4A9}abcdefghijklmnop"));
        assert!(!is_str_bidi("abcdefghijklmnop\u{FE00}abcdefghijklmnop"));
        assert!(!is_str_bidi("abcdefghijklmnop\u{202C}abcdefghijklmnop"));
        assert!(!is_str_bidi("abcdefghijklmnop\u{FEFF}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{0590}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{08FF}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{061C}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{FB50}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{FDFF}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{FE70}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{FEFE}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{200F}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{202B}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{202E}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{2067}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{10800}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{10FFF}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{1E800}abcdefghijklmnop"));
        assert!(is_str_bidi("abcdefghijklmnop\u{1EFFF}abcdefghijklmnop"));
    }

    #[test]
    fn test_is_utf8_bidi() {
        assert!(!is_utf8_bidi(
            "abcdefghijklmnopaabcdefghijklmnop".as_bytes()
        ));
        assert!(!is_utf8_bidi(
            "abcdefghijklmnop\u{03B1}abcdefghijklmnop".as_bytes()
        ));
        assert!(!is_utf8_bidi(
            "abcdefghijklmnop\u{3041}abcdefghijklmnop".as_bytes()
        ));
        assert!(!is_utf8_bidi(
            "abcdefghijklmnop\u{1F4A9}abcdefghijklmnop".as_bytes()
        ));
        assert!(!is_utf8_bidi(
            "abcdefghijklmnop\u{FE00}abcdefghijklmnop".as_bytes()
        ));
        assert!(!is_utf8_bidi(
            "abcdefghijklmnop\u{202C}abcdefghijklmnop".as_bytes()
        ));
        assert!(!is_utf8_bidi(
            "abcdefghijklmnop\u{FEFF}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{0590}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{08FF}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{061C}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{FB50}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{FDFF}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{FE70}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{FEFE}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{200F}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{202B}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{202E}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{2067}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{10800}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{10FFF}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{1E800}abcdefghijklmnop".as_bytes()
        ));
        assert!(is_utf8_bidi(
            "abcdefghijklmnop\u{1EFFF}abcdefghijklmnop".as_bytes()
        ));
    }

    #[test]
    fn test_is_utf16_bidi() {
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x0062, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x03B1, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x3041, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD801, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFE00, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x202C, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(!is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFEFF, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x0590, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x08FF, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x061C, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFB1D, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFB50, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFDFF, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFE70, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFEFE, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x200F, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x202B, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x202E, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x2067, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD802, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD803, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD83A, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));
        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD83B, 0x62, 0x63, 0x64, 0x65, 0x66,
            0x67, 0x68, 0x69,
        ]));

        assert!(is_utf16_bidi(&[
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x0590, 0x3041, 0x62, 0x63, 0x64, 0x65,
            0x66, 0x67, 0x68, 0x69,
        ]));
    }

    #[test]
    fn test_check_str_for_latin1_and_bidi() {
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnopaabcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{03B1}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{3041}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{1F4A9}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{FE00}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{202C}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{FEFF}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{0590}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{08FF}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{061C}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{FB50}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{FDFF}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{FE70}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{FEFE}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{200F}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{202B}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{202E}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{2067}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{10800}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{10FFF}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{1E800}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_str_for_latin1_and_bidi("abcdefghijklmnop\u{1EFFF}abcdefghijklmnop"),
            Latin1Bidi::Bidi
        );
    }

    #[test]
    fn test_check_utf8_for_latin1_and_bidi() {
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnopaabcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{03B1}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{3041}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{1F4A9}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{FE00}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{202C}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{FEFF}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{0590}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{08FF}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{061C}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{FB50}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{FDFF}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{FE70}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{FEFE}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{200F}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{202B}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{202E}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{2067}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{10800}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{10FFF}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{1E800}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf8_for_latin1_and_bidi("abcdefghijklmnop\u{1EFFF}abcdefghijklmnop".as_bytes()),
            Latin1Bidi::Bidi
        );
    }

    #[test]
    fn test_check_utf16_for_latin1_and_bidi() {
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x0062, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x03B1, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x3041, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD801, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFE00, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x202C, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_ne!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFEFF, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x0590, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x08FF, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x061C, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFB1D, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFB50, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFDFF, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFE70, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xFEFE, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x200F, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x202B, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x202E, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x2067, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD802, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD803, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD83A, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xD83B, 0x62, 0x63, 0x64, 0x65,
                0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );

        assert_eq!(
            check_utf16_for_latin1_and_bidi(&[
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x0590, 0x3041, 0x62, 0x63, 0x64,
                0x65, 0x66, 0x67, 0x68, 0x69,
            ]),
            Latin1Bidi::Bidi
        );
    }

    #[inline(always)]
    pub fn reference_is_char_bidi(c: char) -> bool {
        match c {
            '\u{0590}'..='\u{08FF}'
            | '\u{FB1D}'..='\u{FDFF}'
            | '\u{FE70}'..='\u{FEFE}'
            | '\u{10800}'..='\u{10FFF}'
            | '\u{1E800}'..='\u{1EFFF}'
            | '\u{200F}'
            | '\u{202B}'
            | '\u{202E}'
            | '\u{2067}' => true,
            _ => false,
        }
    }

    #[inline(always)]
    pub fn reference_is_utf16_code_unit_bidi(u: u16) -> bool {
        match u {
            0x0590..=0x08FF
            | 0xFB1D..=0xFDFF
            | 0xFE70..=0xFEFE
            | 0xD802
            | 0xD803
            | 0xD83A
            | 0xD83B
            | 0x200F
            | 0x202B
            | 0x202E
            | 0x2067 => true,
            _ => false,
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)] // Miri is too slow
    fn test_is_char_bidi_thoroughly() {
        for i in 0..0xD800u32 {
            let c: char = ::core::char::from_u32(i).unwrap();
            assert_eq!(is_char_bidi(c), reference_is_char_bidi(c));
        }
        for i in 0xE000..0x110000u32 {
            let c: char = ::core::char::from_u32(i).unwrap();
            assert_eq!(is_char_bidi(c), reference_is_char_bidi(c));
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)] // Miri is too slow
    fn test_is_utf16_code_unit_bidi_thoroughly() {
        for i in 0..0x10000u32 {
            let u = i as u16;
            assert_eq!(
                is_utf16_code_unit_bidi(u),
                reference_is_utf16_code_unit_bidi(u)
            );
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)] // Miri is too slow
    fn test_is_str_bidi_thoroughly() {
        let mut buf = [0; 4];
        for i in 0..0xD800u32 {
            let c: char = ::core::char::from_u32(i).unwrap();
            assert_eq!(
                is_str_bidi(c.encode_utf8(&mut buf[..])),
                reference_is_char_bidi(c)
            );
        }
        for i in 0xE000..0x110000u32 {
            let c: char = ::core::char::from_u32(i).unwrap();
            assert_eq!(
                is_str_bidi(c.encode_utf8(&mut buf[..])),
                reference_is_char_bidi(c)
            );
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)] // Miri is too slow
    fn test_is_utf8_bidi_thoroughly() {
        let mut buf = [0; 8];
        for i in 0..0xD800u32 {
            let c: char = ::core::char::from_u32(i).unwrap();
            let expect = reference_is_char_bidi(c);
            {
                let len = {
                    let bytes = c.encode_utf8(&mut buf[..]).as_bytes();
                    assert_eq!(is_utf8_bidi(bytes), expect);
                    bytes.len()
                };
                {
                    let tail = &mut buf[len..];
                    for b in tail.iter_mut() {
                        *b = 0;
                    }
                }
            }
            assert_eq!(is_utf8_bidi(&buf[..]), expect);
        }
        for i in 0xE000..0x110000u32 {
            let c: char = ::core::char::from_u32(i).unwrap();
            let expect = reference_is_char_bidi(c);
            {
                let len = {
                    let bytes = c.encode_utf8(&mut buf[..]).as_bytes();
                    assert_eq!(is_utf8_bidi(bytes), expect);
                    bytes.len()
                };
                {
                    let tail = &mut buf[len..];
                    for b in tail.iter_mut() {
                        *b = 0;
                    }
                }
            }
            assert_eq!(is_utf8_bidi(&buf[..]), expect);
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)] // Miri is too slow
    fn test_is_utf16_bidi_thoroughly() {
        let mut buf = [0; 32];
        for i in 0..0x10000u32 {
            let u = i as u16;
            buf[15] = u;
            assert_eq!(
                is_utf16_bidi(&buf[..]),
                reference_is_utf16_code_unit_bidi(u)
            );
        }
    }

    #[test]
    fn test_is_utf8_bidi_edge_cases() {
        assert!(!is_utf8_bidi(b"\xD5\xBF\x61"));
        assert!(!is_utf8_bidi(b"\xD6\x80\x61"));
        assert!(!is_utf8_bidi(b"abc"));
        assert!(is_utf8_bidi(b"\xD5\xBF\xC2"));
        assert!(is_utf8_bidi(b"\xD6\x80\xC2"));
        assert!(is_utf8_bidi(b"ab\xC2"));
    }

    #[test]
    fn test_decode_latin1() {
        match decode_latin1(b"ab") {
            Cow::Borrowed(s) => {
                assert_eq!(s, "ab");
            }
            Cow::Owned(_) => {
                unreachable!("Should have borrowed");
            }
        }
        assert_eq!(decode_latin1(b"a\xE4"), "a\u{E4}");
    }

    #[test]
    fn test_encode_latin1_lossy() {
        match encode_latin1_lossy("ab") {
            Cow::Borrowed(s) => {
                assert_eq!(s, b"ab");
            }
            Cow::Owned(_) => {
                unreachable!("Should have borrowed");
            }
        }
        assert_eq!(encode_latin1_lossy("a\u{E4}"), &(b"a\xE4")[..]);
    }

    #[test]
    fn test_convert_utf8_to_utf16_without_replacement() {
        let mut buf = [0u16; 5];
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"ab", &mut buf[..2]),
            Some(2)
        );
        assert_eq!(buf[0], u16::from(b'a'));
        assert_eq!(buf[1], u16::from(b'b'));
        assert_eq!(buf[2], 0);
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xC3\xA4c", &mut buf[..3]),
            Some(2)
        );
        assert_eq!(buf[0], 0xE4);
        assert_eq!(buf[1], u16::from(b'c'));
        assert_eq!(buf[2], 0);
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xE2\x98\x83", &mut buf[..3]),
            Some(1)
        );
        assert_eq!(buf[0], 0x2603);
        assert_eq!(buf[1], u16::from(b'c'));
        assert_eq!(buf[2], 0);
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xE2\x98\x83d", &mut buf[..4]),
            Some(2)
        );
        assert_eq!(buf[0], 0x2603);
        assert_eq!(buf[1], u16::from(b'd'));
        assert_eq!(buf[2], 0);
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xE2\x98\x83\xC3\xA4", &mut buf[..5]),
            Some(2)
        );
        assert_eq!(buf[0], 0x2603);
        assert_eq!(buf[1], 0xE4);
        assert_eq!(buf[2], 0);
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xF0\x9F\x93\x8E", &mut buf[..4]),
            Some(2)
        );
        assert_eq!(buf[0], 0xD83D);
        assert_eq!(buf[1], 0xDCCE);
        assert_eq!(buf[2], 0);
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xF0\x9F\x93\x8Ee", &mut buf[..5]),
            Some(3)
        );
        assert_eq!(buf[0], 0xD83D);
        assert_eq!(buf[1], 0xDCCE);
        assert_eq!(buf[2], u16::from(b'e'));
        assert_eq!(
            convert_utf8_to_utf16_without_replacement(b"\xF0\x9F\x93", &mut buf[..5]),
            None
        );
    }
}
