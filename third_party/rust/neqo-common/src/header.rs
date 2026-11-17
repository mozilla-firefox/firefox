// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    fmt::{self, Debug},
    str::{FromStr, Utf8Error},
};

use thiserror::Error;

use crate::hex::HexWithLen;

#[derive(PartialEq, PartialOrd, Eq, Ord, Clone)]
pub struct Header {
    name: String,
    /// The raw header field value as bytes.
    ///
    /// HTTP allows field values to contain any visible ASCII characters and
    /// arbitrary 0x80–0xFF bytes (`obs-text`).  Unlike field *names*, field
    /// values are not guaranteed to be valid UTF-8.
    ///
    /// See also <https://www.rfc-editor.org/rfc/rfc9110#section-5.5>.
    value: Vec<u8>,
}

impl Header {
    pub fn new<N, V>(name: N, value: V) -> Self
    where
        N: Into<String>,
        V: Into<Vec<u8>>,
    {
        Self {
            name: name.into(),
            value: value.into(),
        }
    }

    #[must_use]
    pub fn is_allowed_for_response(&self) -> bool {
        !matches!(
            self.name.as_str(),
            "connection"
                | "host"
                | "keep-alive"
                | "proxy-connection"
                | "te"
                | "transfer-encoding"
                | "upgrade"
        )
    }

    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    #[must_use]
    pub fn value(&self) -> &[u8] {
        &self.value
    }

    /// Size of the field line, per RFC 9114, Section 4.2.2.
    #[must_use]
    pub const fn field_size(&self) -> usize {
        self.name.len() + self.value.len() + 32
    }

    /// Try to interpret the header value as UTF-8.
    ///
    /// # Errors
    ///
    /// Returns an error if the value contains invalid UTF-8.
    pub fn value_utf8(&self) -> Result<&str, Utf8Error> {
        str::from_utf8(&self.value)
    }
}

impl Debug for Header {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.name.starts_with(':') {
            write!(f, "<{}>", self.name)?;
        } else {
            f.write_str(&self.name)?;
        }
        f.write_str(": ")?;
        if let Ok(s) = self.value_utf8() {
            write!(f, "{s:?}")
        } else {
            HexWithLen::fmt(f, &self.value)
        }
    }
}

impl<T: AsRef<str>, U: AsRef<[u8]>> PartialEq<(T, U)> for Header {
    fn eq(&self, other: &(T, U)) -> bool {
        self.name == other.0.as_ref() && self.value == other.1.as_ref()
    }
}

pub trait HeadersExt<'h> {
    fn contains_header<T: AsRef<str>, U: AsRef<[u8]>>(self, name: T, value: U) -> bool;
    fn find_header<T: AsRef<str> + 'h>(self, name: T) -> Option<&'h Header>;
}

impl<'h, H> HeadersExt<'h> for H
where
    H: IntoIterator<Item = &'h Header> + 'h,
{
    fn contains_header<T: AsRef<str>, U: AsRef<[u8]>>(self, name: T, value: U) -> bool {
        let (name, value) = (name.as_ref(), value.as_ref());
        self.into_iter().any(|h| h == &(name, value))
    }

    fn find_header<T: AsRef<str> + 'h>(self, name: T) -> Option<&'h Header> {
        let name = name.as_ref();
        self.into_iter().find(|h| h.name == name)
    }
}

#[derive(Debug, PartialEq, Eq, Error)]
pub enum FromStrError {
    #[error("Header string missing colon")]
    MissingColon,
    #[error("Header string missing name")]
    MissingName,
}

impl FromStr for Header {
    type Err = FromStrError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (seperator, _) = s
            .match_indices(':')
            // Pseudo-header starts with a ':'. Skip it.
            .find(|(i, _)| *i != 0)
            .ok_or(FromStrError::MissingColon)?;

        let name = s[..seperator].trim().to_ascii_lowercase();
        if name.is_empty() {
            return Err(FromStrError::MissingName);
        }

        let value = s[seperator + 1..].trim();

        Ok(Self::new(name, value))
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::*;

    #[test]
    fn from_str_valid() {
        let header = Header::from_str("Content-Type: text/html").unwrap();
        assert_eq!(header.name(), "content-type");
        assert_eq!(header.value(), b"text/html");

        let header = Header::from_str("Content-Type:").unwrap();
        assert_eq!(header.name(), "content-type");
        assert_eq!(header.value(), b"");
    }

    #[test]
    fn from_str_pseudo_header() {
        let header = Header::from_str(":scheme: https").unwrap();
        assert_eq!(header.name(), ":scheme");
        assert_eq!(header.value(), b"https");
    }

    #[test]
    fn from_str_pseudo_header_with_value_with_colon() {
        let header = Header::from_str(":some: he:ader").unwrap();
        assert_eq!(header.name(), ":some");
        assert_eq!(header.value(), b"he:ader");
    }

    #[test]
    fn from_str_errors() {
        assert_eq!(Header::from_str("").err(), Some(FromStrError::MissingColon));
        assert_eq!(
            Header::from_str(" : text/html").err(),
            Some(FromStrError::MissingName)
        );
    }

    #[test]
    fn non_utf8_header_value() {
        // Create a header with non-UTF-8 bytes in the value
        let non_utf8_bytes: Vec<u8> = vec![0xFF, 0xFE, 0xFD, 0x80, 0x81];
        let header = Header::new("custom-header", non_utf8_bytes.as_slice());

        assert_eq!(header.name(), "custom-header");
        assert_eq!(header.value(), non_utf8_bytes.as_slice());

        // Verify that the value is indeed not valid UTF-8
        assert!(str::from_utf8(header.value()).is_err());
        // Test the value_utf8() helper method
        assert!(header.value_utf8().is_err());
    }

    #[test]
    fn non_ascii_header_value() {
        // Create a header with non-ASCII but valid UTF-8 bytes (emoji: rocket + star)
        let emoji_bytes = b"\xF0\x9F\x9A\x80\xF0\x9F\x8C\x9F";
        let header = Header::new("emoji-header", emoji_bytes.as_slice());

        assert_eq!(header.name(), "emoji-header");
        assert_eq!(header.value(), emoji_bytes.as_slice());

        // Verify we can convert back to UTF-8
        let emoji_str = str::from_utf8(header.value()).unwrap();
        assert_eq!(emoji_str, str::from_utf8(emoji_bytes).unwrap());
        // Test the value_utf8() helper method
        assert_eq!(header.value_utf8().unwrap(), emoji_str);
    }

    #[test]
    fn value_utf8_method() {
        // Test value_utf8() with valid UTF-8
        let header = Header::new("content-type", "text/html");
        assert_eq!(header.value_utf8().unwrap(), "text/html");

        // Test value_utf8() with bytes
        let header2 = Header::new("test", b"value");
        assert_eq!(header2.value_utf8().unwrap(), "value");
    }

    #[test]
    fn header_comparison_with_bytes() {
        let header = Header::new("test", b"value");
        assert_eq!(header, ("test", b"value".as_ref()));
        assert_ne!(header, ("test", b"other".as_ref()));
        assert_ne!(header, ("other", b"value".as_ref()));
    }

    #[test]
    fn is_allowed_for_response() {
        assert!(Header::new("content-type", "text/html").is_allowed_for_response());
        for name in ["connection", "host", "keep-alive", "transfer-encoding"] {
            assert!(!Header::new(name, "x").is_allowed_for_response());
        }
    }

    #[test]
    fn headers_ext() {
        let headers = [
            Header::new("content-type", "text/html"),
            Header::new("x-custom", "value"),
        ];
        assert!(headers.iter().contains_header("content-type", "text/html"));
        assert!(!headers.iter().contains_header("content-type", "other"));
        assert!(!headers.iter().contains_header("missing", "value"));
        assert_eq!(
            headers.iter().find_header("x-custom").unwrap().name(),
            "x-custom"
        );
        assert!(headers.iter().find_header("missing").is_none());
    }

    #[test]
    fn debug_format() {
        let h = Header::new(":status", "200");
        assert_eq!(format!("{h:?}"), r#"<:status>: "200""#);

        let h = Header::new("content-type", "text/html");
        assert_eq!(format!("{h:?}"), r#"content-type: "text/html""#);

        let h = Header::new("has-quotes", "abc\"123");
        assert_eq!(format!("{h:?}"), r#"has-quotes: "abc\"123""#);

        let h = Header::new("binary", vec![0xff, 0xfe]);
        assert_eq!(format!("{h:?}"), "binary: [2]: fffe");
    }
}
