/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt;
use std::ops;

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct ByteSize(u64);

impl ByteSize {
    pub const fn b(value: u64) -> Self {
        Self(value)
    }

    pub const fn kib(value: u64) -> Self {
        Self(Self::b(1024).0.saturating_mul(value))
    }

    pub const fn mib(value: u64) -> Self {
        Self(Self::kib(1024).0.saturating_mul(value))
    }

    pub const fn as_u64(&self) -> u64 {
        self.0
    }
}

impl ops::Mul<u64> for ByteSize {
    type Output = Self;

    fn mul(self, rhs: u64) -> Self::Output {
        Self(self.0.saturating_mul(rhs))
    }
}

impl fmt::Display for ByteSize {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let bytes_u64 = self.0;
        if bytes_u64 >= 1024 * 1024 {
            write!(f, "{} MB", bytes_u64 / (1024 * 1024))
        } else if bytes_u64 >= 1024 {
            write!(f, "{} KB", bytes_u64 / 1024)
        } else {
            write!(f, "{} bytes", bytes_u64)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_byte_size_constructors() {
        assert_eq!(ByteSize::b(1024).as_u64(), 1024);
        assert_eq!(ByteSize::mib(1).as_u64(), 1024 * 1024);
        assert_eq!(ByteSize::mib(10).as_u64(), 10 * 1024 * 1024);
    }

    #[test]
    fn test_byte_size_overflow() {
        assert_eq!(ByteSize::mib(u64::MAX).as_u64(), u64::MAX);
    }

    #[test]
    fn test_byte_size_display() {
        assert_eq!(ByteSize::b(512).to_string(), "512 bytes");
        assert_eq!(ByteSize::kib(1).to_string(), "1 KB");
        assert_eq!(ByteSize::kib(1024).to_string(), "1 MB");
        assert_eq!(ByteSize::mib(1).to_string(), "1 MB");
        assert_eq!(ByteSize::mib(100).to_string(), "100 MB");
    }

    #[test]
    fn test_byte_size_comparison() {
        let small = ByteSize::b(1024);
        let medium = ByteSize::kib(1);
        let large = ByteSize::mib(1);

        assert_eq!(small, medium);
        assert!(small < large);
        assert!(large > small);
        assert!(small <= medium);
        assert!(large >= small);
        assert!(small != large);
    }

    #[test]
    fn test_byte_size_ordering() {
        let sizes = vec![
            ByteSize::mib(10),
            ByteSize::b(100),
            ByteSize::kib(5),
            ByteSize::mib(1),
        ];
        let mut sorted = sizes.clone();
        sorted.sort();

        assert_eq!(
            sorted,
            vec![
                ByteSize::b(100),
                ByteSize::kib(5),
                ByteSize::mib(1),
                ByteSize::mib(10),
            ]
        );
    }
}
