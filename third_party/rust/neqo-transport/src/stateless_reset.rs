// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Stateless Reset Token implementation.

use neqo_common::Decoder;
use nss::random;

use crate::Error;

/// A stateless reset token is a 16-byte value that is used to identify
/// a stateless reset packet.
#[derive(Clone, Debug, Default, Eq)]
pub struct Token([u8; Self::LEN]);

impl Token {
    pub const LEN: usize = 16;

    /// Create a new stateless reset token from a byte array.
    #[must_use]
    pub const fn new(token: [u8; Self::LEN]) -> Self {
        Self(token)
    }

    /// Generate a random stateless reset token.
    #[must_use]
    pub fn random() -> Self {
        Self(random::<{ Self::LEN }>())
    }

    /// Get the token as a byte array.
    #[must_use]
    pub const fn as_bytes(&self) -> &[u8; Self::LEN] {
        &self.0
    }
}

/// Compare two tokens in constant time to prevent timing attacks.
impl PartialEq for Token {
    fn eq(&self, other: &Self) -> bool {
        // rustc might decide to optimize this and make this non-constant-time.
        // It doesn't appear to currently.
        let mut c = 0;
        for (&a, &b) in self.0.iter().zip(&other.0) {
            c |= a ^ b;
        }
        c == 0
    }
}

impl TryFrom<&[u8]> for Token {
    type Error = Error;

    fn try_from(value: &[u8]) -> Result<Self, Self::Error> {
        Ok(Self(value.try_into()?))
    }
}

impl TryFrom<&mut Decoder<'_>> for Token {
    type Error = Error;

    fn try_from(d: &mut Decoder<'_>) -> Result<Self, Self::Error> {
        Ok(Self(*d.decode_array().ok_or(Error::NoMoreData)?))
    }
}

impl AsRef<[u8]> for Token {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::*;

    #[test]
    fn new_token() {
        let bytes = [1u8; Token::LEN];
        let token = Token::new(bytes);
        assert_eq!(token.as_bytes(), &bytes);
    }

    #[test]
    fn random_token() {
        nss::init().unwrap();
        let token1 = Token::random();
        let token2 = Token::random();
        // With very high probability, two random tokens should be different
        assert_ne!(token1, token2);
    }

    #[test]
    fn eq_same() {
        let bytes = [42u8; Token::LEN];
        let token1 = Token::new(bytes);
        let token2 = Token::new(bytes);
        assert_eq!(token1, token2);
    }

    #[test]
    fn eq_different() {
        let token1 = Token::new([1u8; Token::LEN]);
        let token2 = Token::new([2u8; Token::LEN]);
        assert_ne!(token1, token2);
    }

    #[test]
    fn from_slice_valid() {
        let bytes = [3u8; Token::LEN];
        let token = Token::try_from(&bytes[..]).unwrap();
        assert_eq!(token.as_bytes(), &bytes);
    }

    #[test]
    fn from_slice_invalid_length() {
        let bytes = [3u8; 15];
        let result = Token::try_from(&bytes[..]);
        assert!(result.is_err());
    }
}
