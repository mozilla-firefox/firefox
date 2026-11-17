// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cmp::{max, min},
    fmt,
};

macro_rules! hex_struct {
    {$(#[$m:meta])* $n:ident, $f:item $($in:expr => $out:expr),*$(,)?} => {
$(#[$m])*
pub struct $n<T>(T);

impl<T> $n<T> {
    /// Wrap an object (or reference) for hex formatting.
    ///
    /// For example:
    /// ```
    #[doc = concat!("# use neqo_common::hex::", stringify!($n), ";")]
    /// struct Example(String, Vec<u8>);
    /// impl std::fmt::Debug for Example {
    ///     fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
    ///         f.debug_tuple("Example")
    ///          .field(&self.0)
    #[doc = concat!(r#"          .field(&"#, stringify!($n), "::new(&self.1))")]
    ///          .finish()
    ///     }
    /// }
    $(
    #[doc = concat!(r##"let expected = concat!(r#"Example("v", "#, "##, stringify!($out), r#", ")");"#)]
    #[doc = concat!(r#"assert_eq!(format!("{:?}", Example("v".to_string(), vec!"#, stringify!($in), ")), expected);")]
    )*
    /// ```
    pub const fn new(v: T) -> Self {
        Self(v)
    }
}

impl<T: AsRef<[u8]>> $n<T> {
    /// Write hex-formatted text to the formatter.
    ///
    /// For example:
    /// ```
    #[doc = concat!("# use neqo_common::hex::", stringify!($n), ";")]
    /// struct VecWrapper(Vec<u8>);
    /// impl std::fmt::Display for VecWrapper {
    ///     fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
    #[doc = concat!("        ", stringify!($n), "::fmt(f, &self.0)")]
    ///     }
    /// }
    $(
    #[doc = concat!(r"assert_eq!(VecWrapper(vec!", stringify!($in), ").to_string(), ", stringify!($out), r");")]
    )*
    /// ```
    ///
    /// # Errors
    /// Propagates any errors from the formatter.
    pub fn fmt(f: &mut fmt::Formatter<'_>, v: T) -> fmt::Result {
        write!(f, "{}", &Self::new(v))
    }
}

impl<T: AsRef<[u8]>> fmt::Display for $n<T> {
    $f
}

impl<T: AsRef<[u8]>> fmt::Debug for $n<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}
    };
}

/// A fast hex implementation with fixed overhead.
fn chunk_hex<T: AsRef<[u8]>>(f: &mut fmt::Formatter<'_>, v: T) -> fmt::Result {
    const HEX: &[u8] = b"0123456789abcdef";
    const CHUNK_SIZE: usize = 128;
    let mut chunk = [0u8; CHUNK_SIZE * 2];
    for slice in v.as_ref().chunks(CHUNK_SIZE) {
        let (pairs, _) = chunk.as_chunks_mut::<2>();
        for (pair, &b) in pairs.iter_mut().zip(slice) {
            *pair = [HEX[usize::from(b >> 4)], HEX[usize::from(b & 0xf)]];
        }
        // SAFETY: only ASCII hex is written to the chunk
        f.write_str(unsafe { str::from_utf8_unchecked(&chunk[..slice.len() * 2]) })?;
    }
    Ok(())
}

hex_struct! {
    /// Simple Hex converter for formatting.
    Hex,
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        chunk_hex(f, &self.0)
    }
    [] => "",
    [1, 2, 3] => "010203",
}

hex_struct! {
    /// Hex converter for formatting that trims long sequences.
    HexSnipMiddle,
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        const SHOW_LEN: usize = 8;
        let buf = self.0.as_ref();
        write!(f, "[{}]", buf.len())?;
        if !buf.is_empty() {
            f.write_str(": ")?;
        }
        let first = min(buf.len(), SHOW_LEN);
        chunk_hex(f, &buf[..first])?;
        let last = max(buf.len().saturating_sub(SHOW_LEN), first);
        if last > first {
            f.write_str("..")?;
        }
        chunk_hex(f, &buf[last..])
    }
    [] => "[0]",
    [1, 2, 3] => "[3]: 010203",
    [0; 20] => "[20]: 0000000000000000..0000000000000000",
}

hex_struct! {
    /// Hex converter for formatting that reports the length of the sequence.
    HexWithLen,
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let buf = self.0.as_ref();
        write!(f, "[{}]", buf.len())?;
        if !buf.is_empty() {
            f.write_str(": ")?;
        }
        chunk_hex(f, buf)
    }
    [1, 2, 3] => "[3]: 010203",
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::{Hex, HexSnipMiddle, HexWithLen};

    #[test]
    fn hex_output() {
        assert_eq!(Hex::new([]).to_string(), "");
        assert_eq!(Hex::new([0xab, 0xcd]).to_string(), "abcd");

        assert_eq!(HexSnipMiddle::new([]).to_string(), "[0]");
        assert_eq!(HexWithLen::new([]).to_string(), "[0]");

        assert_eq!(HexSnipMiddle::new([0xab, 0xcd]).to_string(), "[2]: abcd");
        assert_eq!(HexWithLen::new([0xab, 0xcd]).to_string(), "[2]: abcd");
    }

    #[test]
    fn hex_snip_middle_boundary() {
        // Exactly SHOW_LEN*2 = 16 bytes: should use full hex (no "..").
        let short: Vec<u8> = (0..16).collect();
        let s = HexSnipMiddle::new(&short).to_string();
        assert!(!s.contains(".."), "16 bytes should not be truncated");
        assert!(s.ends_with("0e0f"));

        // 17 bytes: one over the boundary, should be truncated.
        let just_over: Vec<u8> = (0..17).collect();
        assert!(HexSnipMiddle::new(&just_over).to_string().contains(".."));

        // 20 bytes: truncated, check first 8 and last 8 bytes are exact.
        let long: Vec<u8> = (0..20).collect();
        let s = HexSnipMiddle::new(&long).to_string();
        assert!(s.starts_with("[20]: 0001020304050607"));
        assert!(s.contains(".."));
        // Last 8 bytes (12..20 = 0x0c..0x13) must be exactly "0c0d0e0f10111213".
        assert!(s.ends_with("0c0d0e0f10111213"));
    }
}
