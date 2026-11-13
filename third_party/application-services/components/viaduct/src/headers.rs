/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
pub use name::{HeaderName, InvalidHeaderName};
use std::collections::HashMap;
use std::iter::FromIterator;
use std::str::FromStr;
mod name;

/// A single header. Headers have a name (case insensitive) and a value. The
/// character set for header and values are both restrictive.
/// - Names must only contain a-zA-Z0-9 and and ('!' | '#' | '$' | '%' | '&' |
///   '\'' | '*' | '+' | '-' | '.' | '^' | '_' | '`' | '|' | '~') characters
///   (the field-name token production defined at
///   https://tools.ietf.org/html/rfc7230#section-3.2).
///   For request headers, we expect these to all be specified statically,
///   and so we panic if you provide an invalid one. (For response headers, we
///   ignore headers with invalid names, but emit a warning).
///
///   Header names are case insensitive, and we have several pre-defined ones in
///   the [`header_names`] module.
///
/// - Values may only contain printable ascii characters, and may not contain
///   \r or \n. Strictly speaking, HTTP is more flexible for header values,
///   however we don't need to support binary header values, and so we do not.
///
/// Note that typically you should not interact with this directly, and instead
/// use the methods on [`Request`] or [`Headers`] to manipulate these.
#[derive(Clone, Debug, PartialEq, PartialOrd, Hash, Eq, Ord)]
pub struct Header {
    pub name: HeaderName,
    pub value: String,
}

// Trim `s` without copying if it can be avoided.
fn trim_string<S: AsRef<str> + Into<String>>(s: S) -> String {
    let sr = s.as_ref();
    let trimmed = sr.trim();
    if sr.len() != trimmed.len() {
        trimmed.into()
    } else {
        s.into()
    }
}

fn is_valid_header_value(value: &str) -> bool {
    value.bytes().all(|b| (32..127).contains(&b) || b == b'\t')
}

impl Header {
    pub fn new<Name, Value>(name: Name, value: Value) -> Result<Self, crate::ViaductError>
    where
        Name: Into<HeaderName>,
        Value: AsRef<str> + Into<String>,
    {
        let name = name.into();
        let value = trim_string(value);
        if !is_valid_header_value(&value) {
            return Err(crate::ViaductError::RequestHeaderError(name.to_string()));
        }
        Ok(Self { name, value })
    }

    pub fn new_unchecked<Value>(name: HeaderName, value: Value) -> Self
    where
        Value: AsRef<str> + Into<String>,
    {
        Self {
            name,
            value: value.into(),
        }
    }

    #[inline]
    pub fn name(&self) -> &HeaderName {
        &self.name
    }

    #[inline]
    pub fn value(&self) -> &str {
        &self.value
    }

    #[inline]
    fn set_value<V: AsRef<str>>(&mut self, s: V) -> Result<(), crate::ViaductError> {
        let value = s.as_ref();
        if !is_valid_header_value(value) {
            Err(crate::ViaductError::RequestHeaderError(
                self.name.to_string(),
            ))
        } else {
            self.value.clear();
            self.value.push_str(s.as_ref().trim());
            Ok(())
        }
    }
}

impl std::fmt::Display for Header {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {}", self.name, self.value)
    }
}

/// A list of headers.
#[derive(Clone, PartialEq, Eq, Default)]
pub struct Headers {
    headers: Vec<Header>,
}

impl Headers {
    /// Initialize an empty list of headers.
    #[inline]
    pub fn new() -> Self {
        Default::default()
    }

    /// Create headers from a HashMap of name-value pairs
    ///
    /// # Errors
    /// Returns an error if any header name or value is invalid
    pub fn try_from_hashmap(map: HashMap<String, String>) -> Result<Self, crate::ViaductError> {
        let mut headers = Headers::new();
        for (name, value) in map {
            headers.insert(name, value)?;
        }
        Ok(headers)
    }

    /// Initialize an empty list of headers backed by a vector with the provided
    /// capacity.
    pub fn with_capacity(c: usize) -> Self {
        Self {
            headers: Vec::with_capacity(c),
        }
    }

    /// Convert this list of headers to a Vec<Header>
    #[inline]
    pub fn into_vec(self) -> Vec<Header> {
        self.headers
    }

    /// Returns the number of headers.
    #[inline]
    pub fn len(&self) -> usize {
        self.headers.len()
    }

    /// Returns true if `len()` is zero.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.headers.is_empty()
    }
    /// Clear this set of headers.
    #[inline]
    pub fn clear(&mut self) {
        self.headers.clear();
    }

    /// Insert or update a new header.
    ///
    /// This returns an error if you attempt to specify a header with an
    /// invalid value (values must be printable ASCII and may not contain
    /// \r or \n)
    ///
    /// ## Example
    /// ```
    /// # use viaduct::Headers;
    /// # fn main() -> Result<(), viaduct::ViaductError> {
    /// let mut h = Headers::new();
    /// h.insert("My-Cool-Header", "example")?;
    /// assert_eq!(h.get("My-Cool-Header"), Some("example"));
    ///
    /// // Note: names are sensitive
    /// assert_eq!(h.get("my-cool-header"), Some("example"));
    ///
    /// // Also note, constants for headers are in `viaduct::header_names`, and
    /// // you can chain the result of this function.
    /// h.insert(viaduct::header_names::CONTENT_TYPE, "something...")?
    ///  .insert("Something-Else", "etc")?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn insert<N, V>(&mut self, name: N, value: V) -> Result<&mut Self, crate::ViaductError>
    where
        N: Into<HeaderName> + PartialEq<HeaderName>,
        V: Into<String> + AsRef<str>,
    {
        if let Some(entry) = self.headers.iter_mut().find(|h| name == h.name) {
            entry.set_value(value)?;
        } else {
            self.headers.push(Header::new(name, value)?);
        }
        Ok(self)
    }

    /// Insert the provided header unless a header is already specified.
    /// Mostly used internally, e.g. to set "Content-Type: application/json"
    /// in `Request::json()` unless it has been set specifically.
    pub fn insert_if_missing<N, V>(
        &mut self,
        name: N,
        value: V,
    ) -> Result<&mut Self, crate::ViaductError>
    where
        N: Into<HeaderName> + PartialEq<HeaderName>,
        V: Into<String> + AsRef<str>,
    {
        if !self.headers.iter_mut().any(|h| name == h.name) {
            self.headers.push(Header::new(name, value)?);
        }
        Ok(self)
    }

    /// Insert or update a header directly. Typically you will want to use
    /// `insert` over this, as it performs less work if the header needs
    /// updating instead of insertion.
    pub fn insert_header(&mut self, new: Header) -> &mut Self {
        if let Some(entry) = self.headers.iter_mut().find(|h| h.name == new.name) {
            entry.value = new.value;
        } else {
            self.headers.push(new);
        }
        self
    }

    /// Add all the headers in the provided iterator to this list of headers.
    pub fn extend<I>(&mut self, iter: I) -> &mut Self
    where
        I: IntoIterator<Item = Header>,
    {
        let it = iter.into_iter();
        self.headers.reserve(it.size_hint().0);
        for h in it {
            self.insert_header(h);
        }
        self
    }

    /// Add all the headers in the provided iterator, unless any of them are Err.
    pub fn try_extend<I, E>(&mut self, iter: I) -> Result<&mut Self, E>
    where
        I: IntoIterator<Item = Result<Header, E>>,
    {
        // Not the most efficient but avoids leaving us in an unspecified state
        // if one returns Err.
        self.extend(iter.into_iter().collect::<Result<Vec<_>, E>>()?);
        Ok(self)
    }

    /// Get the header object with the requested name. Usually, you will
    /// want to use `get()` or `get_as::<T>()` instead.
    pub fn get_header<S>(&self, name: S) -> Option<&Header>
    where
        S: PartialEq<HeaderName>,
    {
        self.headers.iter().find(|h| name == h.name)
    }

    /// Get the value of the header with the provided name.
    ///
    /// See also `get_as`.
    ///
    /// ## Example
    /// ```
    /// # use viaduct::{Headers, header_names::CONTENT_TYPE};
    /// # fn main() -> Result<(), viaduct::ViaductError> {
    /// let mut h = Headers::new();
    /// h.insert(CONTENT_TYPE, "application/json")?;
    /// assert_eq!(h.get(CONTENT_TYPE), Some("application/json"));
    /// assert_eq!(h.get("Something-Else"), None);
    /// # Ok(())
    /// # }
    /// ```
    pub fn get<S>(&self, name: S) -> Option<&str>
    where
        S: PartialEq<HeaderName>,
    {
        self.get_header(name).map(|h| h.value.as_str())
    }

    /// Get the value of the header with the provided name, and
    /// attempt to parse it using [`std::str::FromStr`].
    ///
    /// - If the header is missing, it returns None.
    /// - If the header is present but parsing failed, returns
    ///   `Some(Err(<error returned by parsing>))`.
    /// - Otherwise, returns `Some(Ok(result))`.
    ///
    /// Note that if `Option<Result<T, E>>` is inconvenient for you,
    /// and you wish this returned `Result<Option<T>, E>`, you may use
    /// the built-in `transpose()` method to convert between them.
    ///
    /// ```
    /// # use viaduct::Headers;
    /// # fn main() -> Result<(), viaduct::ViaductError> {
    /// let mut h = Headers::new();
    /// h.insert("Example", "1234")?.insert("Illegal", "abcd")?;
    /// let v: Option<Result<i64, _>> = h.get_as("Example");
    /// assert_eq!(v, Some(Ok(1234)));
    /// assert_eq!(h.get_as::<i64, _>("Example"), Some(Ok(1234)));
    /// assert_eq!(h.get_as::<i64, _>("Illegal"), Some("abcd".parse::<i64>()));
    /// assert_eq!(h.get_as::<i64, _>("Something-Else"), None);
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_as<T, S>(&self, name: S) -> Option<Result<T, <T as FromStr>::Err>>
    where
        T: FromStr,
        S: PartialEq<HeaderName>,
    {
        self.get(name).map(str::parse)
    }
    /// Get the value of the header with the provided name, and
    /// attempt to parse it using [`std::str::FromStr`].
    ///
    /// This is a variant of `get_as` that returns None on error,
    /// intended to be used for cases where missing and invalid
    /// headers should be treated the same. (With `get_as` this
    /// requires `h.get_as(...).and_then(|r| r.ok())`, which is
    /// somewhat opaque.
    pub fn try_get<T, S>(&self, name: S) -> Option<T>
    where
        T: FromStr,
        S: PartialEq<HeaderName>,
    {
        self.get(name).and_then(|val| val.parse::<T>().ok())
    }

    /// Get an iterator over the headers in no particular order.
    ///
    /// Note that we also implement IntoIterator.
    pub fn iter(&self) -> <&Headers as IntoIterator>::IntoIter {
        self.into_iter()
    }
}

impl std::fmt::Debug for Headers {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_map()
            .entries(self.headers.iter().map(|h| (h.name().as_str(), &h.value)))
            .finish()
    }
}

impl std::iter::IntoIterator for Headers {
    type IntoIter = <Vec<Header> as IntoIterator>::IntoIter;
    type Item = Header;
    fn into_iter(self) -> Self::IntoIter {
        self.headers.into_iter()
    }
}

impl<'a> std::iter::IntoIterator for &'a Headers {
    type IntoIter = <&'a [Header] as IntoIterator>::IntoIter;
    type Item = &'a Header;
    fn into_iter(self) -> Self::IntoIter {
        self.headers[..].iter()
    }
}

impl FromIterator<Header> for Headers {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = Header>,
    {
        let mut v = iter.into_iter().collect::<Vec<Header>>();
        v.sort_by(|a, b| a.name.cmp(&b.name));
        v.reverse();
        v.dedup_by(|a, b| a.name == b.name);
        v.into()
    }
}

impl From<Vec<Header>> for Headers {
    fn from(headers: Vec<Header>) -> Self {
        Self { headers }
    }
}

#[allow(clippy::implicit_hasher)] // https://github.com/rust-lang/rust-clippy/issues/3899
impl From<Headers> for HashMap<String, String> {
    fn from(headers: Headers) -> HashMap<String, String> {
        headers
            .into_iter()
            .map(|h| (String::from(h.name), h.value))
            .collect()
    }
}

pub mod consts {
    use super::name::HeaderName;
    macro_rules! def_header_consts {
        ($(($NAME:ident, $string:literal)),* $(,)?) => {
            $(pub const $NAME: HeaderName = HeaderName(std::borrow::Cow::Borrowed($string));)*
        };
    }

    macro_rules! headers {
        ($(($NAME:ident, $string:literal)),* $(,)?) => {
            def_header_consts!($(($NAME, $string)),*);
            // Unused except for tests.
            const _ALL: &[&str] = &[$($string),*];
        };
    }

    // Predefined header names, for convenience.
    // Feel free to add to these.
    headers!(
        (ACCEPT_ENCODING, "accept-encoding"),
        (ACCEPT, "accept"),
        (AUTHORIZATION, "authorization"),
        (CACHE_CONTROL, "cache-control"),
        (CONTENT_TYPE, "content-type"),
        (ETAG, "etag"),
        (IF_NONE_MATCH, "if-none-match"),
        (USER_AGENT, "user-agent"),
        // non-standard, but it's convenient to have these.
        (RETRY_AFTER, "retry-after"),
        (X_IF_UNMODIFIED_SINCE, "x-if-unmodified-since"),
        (X_KEYID, "x-keyid"),
        (X_LAST_MODIFIED, "x-last-modified"),
        (X_TIMESTAMP, "x-timestamp"),
        (X_WEAVE_NEXT_OFFSET, "x-weave-next-offset"),
        (X_WEAVE_RECORDS, "x-weave-records"),
        (X_WEAVE_TIMESTAMP, "x-weave-timestamp"),
        (X_WEAVE_BACKOFF, "x-weave-backoff"),
    );

    #[test]
    fn test_predefined() {
        for &name in _ALL {
            assert!(
                HeaderName::new(name).is_ok(),
                "Invalid header name in predefined header constants: {}",
                name
            );
            assert_eq!(
                name.to_ascii_lowercase(),
                name,
                "Non-lowercase name in predefined header constants: {}",
                name
            );
        }
    }
}
