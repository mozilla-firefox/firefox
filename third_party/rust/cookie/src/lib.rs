//! HTTP cookie parsing and cookie jar management.
//!
//! This crates provides the [`Cookie`] type, representing an HTTP cookie, and
//! the [`CookieJar`] type, which manages a collection of cookies for session
//! management, recording changes as they are made, and optional automatic
//! cookie encryption and signing.
//!
//! # Usage
//!
//! Add the following to the `[dependencies]` section of your `Cargo.toml`:
//!
//! ```toml
//! cookie = "0.16"
//! ```
//!
//! # Features
//!
//! This crate exposes several features, all of which are disabled by default:
//!
//! * **`percent-encode`**
//!
//!   Enables _percent encoding and decoding_ of names and values in cookies.
//!
//!   When this feature is enabled, the [`Cookie::encoded()`] and
//!   [`Cookie::parse_encoded()`] methods are available. The `encoded` method
//!   returns a wrapper around a `Cookie` whose `Display` implementation
//!   percent-encodes the name and value of the cookie. The `parse_encoded`
//!   method percent-decodes the name and value of a `Cookie` during parsing.
//!
//! * **`signed`**
//!
//!   Enables _signed_ cookies via [`CookieJar::signed()`].
//!
//!   When this feature is enabled, the [`CookieJar::signed()`] method,
//!   [`SignedJar`] type, and [`Key`] type are available. The jar acts as "child
//!   jar"; operations on the jar automatically sign and verify cookies as they
//!   are added and retrieved from the parent jar.
//!
//! * **`private`**
//!
//!   Enables _private_ (authenticated, encrypted) cookies via
//!   [`CookieJar::private()`].
//!
//!   When this feature is enabled, the [`CookieJar::private()`] method,
//!   [`PrivateJar`] type, and [`Key`] type are available. The jar acts as "child
//!   jar"; operations on the jar automatically encrypt and decrypt/authenticate
//!   cookies as they are added and retrieved from the parent jar.
//!
//! * **`key-expansion`**
//!
//!   Enables _key expansion_ or _key derivation_ via [`Key::derive_from()`].
//!
//!   When this feature is enabled, and either `signed` or `private` are _also_
//!   enabled, the [`Key::derive_from()`] method is available. The method can be
//!   used to derive a `Key` structure appropriate for use with signed and
//!   private jars from cryptographically valid key material that is shorter in
//!   length than the full key.
//!
//! * **`secure`**
//!
//!   A meta-feature that simultaneously enables `signed`, `private`, and
//!   `key-expansion`.
//!
//! You can enable features via `Cargo.toml`:
//!
//! ```toml
//! [dependencies.cookie]
//! features = ["secure", "percent-encode"]
//! ```

#![cfg_attr(all(nightly, doc), feature(doc_cfg))]

#![doc(html_root_url = "https://docs.rs/cookie/0.16")]
#![deny(missing_docs)]

pub use time;

mod builder;
mod parse;
mod jar;
mod delta;
mod draft;
mod expiration;

#[cfg(any(feature = "private", feature = "signed"))] #[macro_use] mod secure;
#[cfg(any(feature = "private", feature = "signed"))] pub use secure::*;

use std::borrow::Cow;
use std::fmt;
use std::str::FromStr;

#[allow(unused_imports, deprecated)]
use std::ascii::AsciiExt;

use time::{Duration, OffsetDateTime, UtcOffset, macros::datetime};

use crate::parse::parse_cookie;
pub use crate::parse::ParseError;
pub use crate::builder::CookieBuilder;
pub use crate::jar::{CookieJar, Delta, Iter};
pub use crate::draft::*;
pub use crate::expiration::*;

#[derive(Debug, Clone)]
enum CookieStr<'c> {
    /// An string derived from indexes (start, end).
    Indexed(usize, usize),
    /// A string derived from a concrete string.
    Concrete(Cow<'c, str>),
}

impl<'c> CookieStr<'c> {
    /// Retrieves the string `self` corresponds to. If `self` is derived from
    /// indexes, the corresponding subslice of `string` is returned. Otherwise,
    /// the concrete string is returned.
    ///
    /// # Panics
    ///
    /// Panics if `self` is an indexed string and `string` is None.
    fn to_str<'s>(&'s self, string: Option<&'s Cow<str>>) -> &'s str {
        match *self {
            CookieStr::Indexed(i, j) => {
                let s = string.expect("`Some` base string must exist when \
                    converting indexed str to str! (This is a module invariant.)");
                &s[i..j]
            },
            CookieStr::Concrete(ref cstr) => &*cstr,
        }
    }

    #[allow(clippy::ptr_arg)]
    fn to_raw_str<'s, 'b: 's>(&'s self, string: &'s Cow<'b, str>) -> Option<&'b str> {
        match *self {
            CookieStr::Indexed(i, j) => {
                match *string {
                    Cow::Borrowed(s) => Some(&s[i..j]),
                    Cow::Owned(_) => None,
                }
            },
            CookieStr::Concrete(_) => None,
        }
    }

    fn into_owned(self) -> CookieStr<'static> {
        use crate::CookieStr::*;

        match self {
            Indexed(a, b) => Indexed(a, b),
            Concrete(Cow::Owned(c)) => Concrete(Cow::Owned(c)),
            Concrete(Cow::Borrowed(c)) => Concrete(Cow::Owned(c.into())),
        }
    }
}

/// Representation of an HTTP cookie.
///
/// # Constructing a `Cookie`
///
/// To construct a cookie with only a name/value, use [`Cookie::new()`]:
///
/// ```rust
/// use cookie::Cookie;
///
/// let cookie = Cookie::new("name", "value");
/// assert_eq!(&cookie.to_string(), "name=value");
/// ```
///
/// To construct more elaborate cookies, use [`Cookie::build()`] and
/// [`CookieBuilder`] methods:
///
/// ```rust
/// use cookie::Cookie;
///
/// let cookie = Cookie::build("name", "value")
///     .domain("www.rust-lang.org")
///     .path("/")
///     .secure(true)
///     .http_only(true)
///     .finish();
/// ```
#[derive(Debug, Clone)]
pub struct Cookie<'c> {
    /// Storage for the cookie string. Only used if this structure was derived
    /// from a string that was subsequently parsed.
    cookie_string: Option<Cow<'c, str>>,
    /// The cookie's name.
    name: CookieStr<'c>,
    /// The cookie's value.
    value: CookieStr<'c>,
    /// The cookie's expiration, if any.
    expires: Option<Expiration>,
    /// The cookie's maximum age, if any.
    max_age: Option<Duration>,
    /// The cookie's domain, if any.
    domain: Option<CookieStr<'c>>,
    /// The cookie's path domain, if any.
    path: Option<CookieStr<'c>>,
    /// Whether this cookie was marked Secure.
    secure: Option<bool>,
    /// Whether this cookie was marked HttpOnly.
    http_only: Option<bool>,
    /// The draft `SameSite` attribute.
    same_site: Option<SameSite>,
}

impl<'c> Cookie<'c> {
    /// Creates a new `Cookie` with the given name and value.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Cookie;
    ///
    /// let cookie = Cookie::new("name", "value");
    /// assert_eq!(cookie.name_value(), ("name", "value"));
    /// ```
    pub fn new<N, V>(name: N, value: V) -> Self
        where N: Into<Cow<'c, str>>,
              V: Into<Cow<'c, str>>
    {
        Cookie {
            cookie_string: None,
            name: CookieStr::Concrete(name.into()),
            value: CookieStr::Concrete(value.into()),
            expires: None,
            max_age: None,
            domain: None,
            path: None,
            secure: None,
            http_only: None,
            same_site: None,
        }
    }

    /// Creates a new `Cookie` with the given name and an empty value.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Cookie;
    ///
    /// let cookie = Cookie::named("name");
    /// assert_eq!(cookie.name(), "name");
    /// assert!(cookie.value().is_empty());
    /// ```
    pub fn named<N>(name: N) -> Cookie<'c>
        where N: Into<Cow<'c, str>>
    {
        Cookie::new(name, "")
    }

    /// Creates a new `CookieBuilder` instance from the given key and value
    /// strings.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::build("foo", "bar").finish();
    /// assert_eq!(c.name_value(), ("foo", "bar"));
    /// ```
    pub fn build<N, V>(name: N, value: V) -> CookieBuilder<'c>
        where N: Into<Cow<'c, str>>,
              V: Into<Cow<'c, str>>
    {
        CookieBuilder::new(name, value)
    }

    /// Parses a `Cookie` from the given HTTP cookie header value string. Does
    /// not perform any percent-decoding.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("foo=bar%20baz; HttpOnly").unwrap();
    /// assert_eq!(c.name_value(), ("foo", "bar%20baz"));
    /// assert_eq!(c.http_only(), Some(true));
    /// ```
    pub fn parse<S>(s: S) -> Result<Cookie<'c>, ParseError>
        where S: Into<Cow<'c, str>>
    {
        parse_cookie(s, false)
    }

    /// Parses a `Cookie` from the given HTTP cookie header value string where
    /// the name and value fields are percent-encoded. Percent-decodes the
    /// name/value fields.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse_encoded("foo=bar%20baz; HttpOnly").unwrap();
    /// assert_eq!(c.name_value(), ("foo", "bar baz"));
    /// assert_eq!(c.http_only(), Some(true));
    /// ```
    #[cfg(feature = "percent-encode")]
    #[cfg_attr(all(nightly, doc), doc(cfg(feature = "percent-encode")))]
    pub fn parse_encoded<S>(s: S) -> Result<Cookie<'c>, ParseError>
        where S: Into<Cow<'c, str>>
    {
        parse_cookie(s, true)
    }

    /// Converts `self` into a `Cookie` with a static lifetime with as few
    /// allocations as possible.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::new("a", "b");
    /// let owned_cookie = c.into_owned();
    /// assert_eq!(owned_cookie.name_value(), ("a", "b"));
    /// ```
    pub fn into_owned(self) -> Cookie<'static> {
        Cookie {
            cookie_string: self.cookie_string.map(|s| s.into_owned().into()),
            name: self.name.into_owned(),
            value: self.value.into_owned(),
            expires: self.expires,
            max_age: self.max_age,
            domain: self.domain.map(|s| s.into_owned()),
            path: self.path.map(|s| s.into_owned()),
            secure: self.secure,
            http_only: self.http_only,
            same_site: self.same_site,
        }
    }

    /// Returns the name of `self`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::new("name", "value");
    /// assert_eq!(c.name(), "name");
    /// ```
    #[inline]
    pub fn name(&self) -> &str {
        self.name.to_str(self.cookie_string.as_ref())
    }

    /// Returns the value of `self`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::new("name", "value");
    /// assert_eq!(c.value(), "value");
    /// ```
    #[inline]
    pub fn value(&self) -> &str {
        self.value.to_str(self.cookie_string.as_ref())
    }

    /// Returns the name and value of `self` as a tuple of `(name, value)`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::new("name", "value");
    /// assert_eq!(c.name_value(), ("name", "value"));
    /// ```
    #[inline]
    pub fn name_value(&self) -> (&str, &str) {
        (self.name(), self.value())
    }

    /// Returns whether this cookie was marked `HttpOnly` or not. Returns
    /// `Some(true)` when the cookie was explicitly set (manually or parsed) as
    /// `HttpOnly`, `Some(false)` when `http_only` was manually set to `false`,
    /// and `None` otherwise.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("name=value; httponly").unwrap();
    /// assert_eq!(c.http_only(), Some(true));
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.http_only(), None);
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.http_only(), None);
    ///
    /// // An explicitly set "false" value.
    /// c.set_http_only(false);
    /// assert_eq!(c.http_only(), Some(false));
    ///
    /// // An explicitly set "true" value.
    /// c.set_http_only(true);
    /// assert_eq!(c.http_only(), Some(true));
    /// ```
    #[inline]
    pub fn http_only(&self) -> Option<bool> {
        self.http_only
    }

    /// Returns whether this cookie was marked `Secure` or not. Returns
    /// `Some(true)` when the cookie was explicitly set (manually or parsed) as
    /// `Secure`, `Some(false)` when `secure` was manually set to `false`, and
    /// `None` otherwise.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("name=value; Secure").unwrap();
    /// assert_eq!(c.secure(), Some(true));
    ///
    /// let mut c = Cookie::parse("name=value").unwrap();
    /// assert_eq!(c.secure(), None);
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.secure(), None);
    ///
    /// // An explicitly set "false" value.
    /// c.set_secure(false);
    /// assert_eq!(c.secure(), Some(false));
    ///
    /// // An explicitly set "true" value.
    /// c.set_secure(true);
    /// assert_eq!(c.secure(), Some(true));
    /// ```
    #[inline]
    pub fn secure(&self) -> Option<bool> {
        self.secure
    }

    /// Returns the `SameSite` attribute of this cookie if one was specified.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::{Cookie, SameSite};
    ///
    /// let c = Cookie::parse("name=value; SameSite=Lax").unwrap();
    /// assert_eq!(c.same_site(), Some(SameSite::Lax));
    /// ```
    #[inline]
    pub fn same_site(&self) -> Option<SameSite> {
        self.same_site
    }

    /// Returns the specified max-age of the cookie if one was specified.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("name=value").unwrap();
    /// assert_eq!(c.max_age(), None);
    ///
    /// let c = Cookie::parse("name=value; Max-Age=3600").unwrap();
    /// assert_eq!(c.max_age().map(|age| age.whole_hours()), Some(1));
    /// ```
    #[inline]
    pub fn max_age(&self) -> Option<Duration> {
        self.max_age
    }

    /// Returns the `Path` of the cookie if one was specified.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("name=value").unwrap();
    /// assert_eq!(c.path(), None);
    ///
    /// let c = Cookie::parse("name=value; Path=/").unwrap();
    /// assert_eq!(c.path(), Some("/"));
    ///
    /// let c = Cookie::parse("name=value; path=/sub").unwrap();
    /// assert_eq!(c.path(), Some("/sub"));
    /// ```
    #[inline]
    pub fn path(&self) -> Option<&str> {
        match self.path {
            Some(ref c) => Some(c.to_str(self.cookie_string.as_ref())),
            None => None,
        }
    }

    /// Returns the `Domain` of the cookie if one was specified.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("name=value").unwrap();
    /// assert_eq!(c.domain(), None);
    ///
    /// let c = Cookie::parse("name=value; Domain=crates.io").unwrap();
    /// assert_eq!(c.domain(), Some("crates.io"));
    /// ```
    #[inline]
    pub fn domain(&self) -> Option<&str> {
        match self.domain {
            Some(ref c) => Some(c.to_str(self.cookie_string.as_ref())),
            None => None,
        }
    }

    /// Returns the [`Expiration`] of the cookie if one was specified.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::{Cookie, Expiration};
    ///
    /// let c = Cookie::parse("name=value").unwrap();
    /// assert_eq!(c.expires(), None);
    ///
    /// // Here, `cookie.expires_datetime()` returns `None`.
    /// let c = Cookie::build("name", "value").expires(None).finish();
    /// assert_eq!(c.expires(), Some(Expiration::Session));
    ///
    /// let expire_time = "Wed, 21 Oct 2017 07:28:00 GMT";
    /// let cookie_str = format!("name=value; Expires={}", expire_time);
    /// let c = Cookie::parse(cookie_str).unwrap();
    /// assert_eq!(c.expires().and_then(|e| e.datetime()).map(|t| t.year()), Some(2017));
    /// ```
    #[inline]
    pub fn expires(&self) -> Option<Expiration> {
        self.expires
    }

    /// Returns the expiration date-time of the cookie if one was specified.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let c = Cookie::parse("name=value").unwrap();
    /// assert_eq!(c.expires_datetime(), None);
    ///
    /// // Here, `cookie.expires()` returns `Some`.
    /// let c = Cookie::build("name", "value").expires(None).finish();
    /// assert_eq!(c.expires_datetime(), None);
    ///
    /// let expire_time = "Wed, 21 Oct 2017 07:28:00 GMT";
    /// let cookie_str = format!("name=value; Expires={}", expire_time);
    /// let c = Cookie::parse(cookie_str).unwrap();
    /// assert_eq!(c.expires_datetime().map(|t| t.year()), Some(2017));
    /// ```
    #[inline]
    pub fn expires_datetime(&self) -> Option<OffsetDateTime> {
        self.expires.and_then(|e| e.datetime())
    }

    /// Sets the name of `self` to `name`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.name(), "name");
    ///
    /// c.set_name("foo");
    /// assert_eq!(c.name(), "foo");
    /// ```
    pub fn set_name<N: Into<Cow<'c, str>>>(&mut self, name: N) {
        self.name = CookieStr::Concrete(name.into())
    }

    /// Sets the value of `self` to `value`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.value(), "value");
    ///
    /// c.set_value("bar");
    /// assert_eq!(c.value(), "bar");
    /// ```
    pub fn set_value<V: Into<Cow<'c, str>>>(&mut self, value: V) {
        self.value = CookieStr::Concrete(value.into())
    }

    /// Sets the value of `http_only` in `self` to `value`.  If `value` is
    /// `None`, the field is unset.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.http_only(), None);
    ///
    /// c.set_http_only(true);
    /// assert_eq!(c.http_only(), Some(true));
    ///
    /// c.set_http_only(false);
    /// assert_eq!(c.http_only(), Some(false));
    ///
    /// c.set_http_only(None);
    /// assert_eq!(c.http_only(), None);
    /// ```
    #[inline]
    pub fn set_http_only<T: Into<Option<bool>>>(&mut self, value: T) {
        self.http_only = value.into();
    }

    /// Sets the value of `secure` in `self` to `value`. If `value` is `None`,
    /// the field is unset.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.secure(), None);
    ///
    /// c.set_secure(true);
    /// assert_eq!(c.secure(), Some(true));
    ///
    /// c.set_secure(false);
    /// assert_eq!(c.secure(), Some(false));
    ///
    /// c.set_secure(None);
    /// assert_eq!(c.secure(), None);
    /// ```
    #[inline]
    pub fn set_secure<T: Into<Option<bool>>>(&mut self, value: T) {
        self.secure = value.into();
    }

    /// Sets the value of `same_site` in `self` to `value`. If `value` is
    /// `None`, the field is unset. If `value` is `SameSite::None`, the "Secure"
    /// flag will be set when the cookie is written out unless `secure` is
    /// explicitly set to `false` via [`Cookie::set_secure()`] or the equivalent
    /// builder method.
    ///
    /// [HTTP draft]: https://tools.ietf.org/html/draft-west-cookie-incrementalism-00
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::{Cookie, SameSite};
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.same_site(), None);
    ///
    /// c.set_same_site(SameSite::None);
    /// assert_eq!(c.same_site(), Some(SameSite::None));
    /// assert_eq!(c.to_string(), "name=value; SameSite=None; Secure");
    ///
    /// c.set_secure(false);
    /// assert_eq!(c.to_string(), "name=value; SameSite=None");
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.same_site(), None);
    ///
    /// c.set_same_site(SameSite::Strict);
    /// assert_eq!(c.same_site(), Some(SameSite::Strict));
    /// assert_eq!(c.to_string(), "name=value; SameSite=Strict");
    ///
    /// c.set_same_site(None);
    /// assert_eq!(c.same_site(), None);
    /// assert_eq!(c.to_string(), "name=value");
    /// ```
    #[inline]
    pub fn set_same_site<T: Into<Option<SameSite>>>(&mut self, value: T) {
        self.same_site = value.into();
    }

    /// Sets the value of `max_age` in `self` to `value`. If `value` is `None`,
    /// the field is unset.
    ///
    /// # Example
    ///
    /// ```rust
    /// # extern crate cookie;
    /// use cookie::Cookie;
    /// use cookie::time::Duration;
    ///
    /// # fn main() {
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.max_age(), None);
    ///
    /// c.set_max_age(Duration::hours(10));
    /// assert_eq!(c.max_age(), Some(Duration::hours(10)));
    ///
    /// c.set_max_age(None);
    /// assert!(c.max_age().is_none());
    /// # }
    /// ```
    #[inline]
    pub fn set_max_age<D: Into<Option<Duration>>>(&mut self, value: D) {
        self.max_age = value.into();
    }

    /// Sets the `path` of `self` to `path`.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.path(), None);
    ///
    /// c.set_path("/");
    /// assert_eq!(c.path(), Some("/"));
    /// ```
    pub fn set_path<P: Into<Cow<'c, str>>>(&mut self, path: P) {
        self.path = Some(CookieStr::Concrete(path.into()));
    }

    /// Unsets the `path` of `self`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.path(), None);
    ///
    /// c.set_path("/");
    /// assert_eq!(c.path(), Some("/"));
    ///
    /// c.unset_path();
    /// assert_eq!(c.path(), None);
    /// ```
    pub fn unset_path(&mut self) {
        self.path = None;
    }

    /// Sets the `domain` of `self` to `domain`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.domain(), None);
    ///
    /// c.set_domain("rust-lang.org");
    /// assert_eq!(c.domain(), Some("rust-lang.org"));
    /// ```
    pub fn set_domain<D: Into<Cow<'c, str>>>(&mut self, domain: D) {
        self.domain = Some(CookieStr::Concrete(domain.into()));
    }

    /// Unsets the `domain` of `self`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.domain(), None);
    ///
    /// c.set_domain("rust-lang.org");
    /// assert_eq!(c.domain(), Some("rust-lang.org"));
    ///
    /// c.unset_domain();
    /// assert_eq!(c.domain(), None);
    /// ```
    pub fn unset_domain(&mut self) {
        self.domain = None;
    }

    /// Sets the expires field of `self` to `time`. If `time` is `None`, an
    /// expiration of [`Session`](Expiration::Session) is set.
    ///
    /// # Example
    ///
    /// ```
    /// # extern crate cookie;
    /// use cookie::{Cookie, Expiration};
    /// use cookie::time::{Duration, OffsetDateTime};
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.expires(), None);
    ///
    /// let mut now = OffsetDateTime::now_utc();
    /// now += Duration::weeks(52);
    ///
    /// c.set_expires(now);
    /// assert!(c.expires().is_some());
    ///
    /// c.set_expires(None);
    /// assert_eq!(c.expires(), Some(Expiration::Session));
    /// ```
    pub fn set_expires<T: Into<Expiration>>(&mut self, time: T) {
        static MAX_DATETIME: OffsetDateTime = datetime!(9999-12-31 23:59:59.999_999 UTC);

        // RFC 6265 requires dates not to exceed 9999 years.
        self.expires = Some(time.into()
            .map(|time| std::cmp::min(time, MAX_DATETIME)));
    }

    /// Unsets the `expires` of `self`.
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::{Cookie, Expiration};
    ///
    /// let mut c = Cookie::new("name", "value");
    /// assert_eq!(c.expires(), None);
    ///
    /// c.set_expires(None);
    /// assert_eq!(c.expires(), Some(Expiration::Session));
    ///
    /// c.unset_expires();
    /// assert_eq!(c.expires(), None);
    /// ```
    pub fn unset_expires(&mut self) {
        self.expires = None;
    }

    /// Makes `self` a "permanent" cookie by extending its expiration and max
    /// age 20 years into the future.
    ///
    /// # Example
    ///
    /// ```rust
    /// # extern crate cookie;
    /// use cookie::Cookie;
    /// use cookie::time::Duration;
    ///
    /// # fn main() {
    /// let mut c = Cookie::new("foo", "bar");
    /// assert!(c.expires().is_none());
    /// assert!(c.max_age().is_none());
    ///
    /// c.make_permanent();
    /// assert!(c.expires().is_some());
    /// assert_eq!(c.max_age(), Some(Duration::days(365 * 20)));
    /// # }
    /// ```
    pub fn make_permanent(&mut self) {
        let twenty_years = Duration::days(365 * 20);
        self.set_max_age(twenty_years);
        self.set_expires(OffsetDateTime::now_utc() + twenty_years);
    }

    /// Make `self` a "removal" cookie by clearing its value, setting a max-age
    /// of `0`, and setting an expiration date far in the past.
    ///
    /// # Example
    ///
    /// ```rust
    /// # extern crate cookie;
    /// use cookie::Cookie;
    /// use cookie::time::Duration;
    ///
    /// # fn main() {
    /// let mut c = Cookie::new("foo", "bar");
    /// c.make_permanent();
    /// assert_eq!(c.max_age(), Some(Duration::days(365 * 20)));
    /// assert_eq!(c.value(), "bar");
    ///
    /// c.make_removal();
    /// assert_eq!(c.value(), "");
    /// assert_eq!(c.max_age(), Some(Duration::ZERO));
    /// # }
    /// ```
    pub fn make_removal(&mut self) {
        self.set_value("");
        self.set_max_age(Duration::seconds(0));
        self.set_expires(OffsetDateTime::now_utc() - Duration::days(365));
    }

    fn fmt_parameters(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if let Some(true) = self.http_only() {
            write!(f, "; HttpOnly")?;
        }

        if let Some(same_site) = self.same_site() {
            write!(f, "; SameSite={}", same_site)?;

            if same_site.is_none() && self.secure().is_none() {
                write!(f, "; Secure")?;
            }
        }

        if let Some(true) = self.secure() {
            write!(f, "; Secure")?;
        }

        if let Some(path) = self.path() {
            write!(f, "; Path={}", path)?;
        }

        if let Some(domain) = self.domain() {
            write!(f, "; Domain={}", domain)?;
        }

        if let Some(max_age) = self.max_age() {
            write!(f, "; Max-Age={}", max_age.whole_seconds())?;
        }

        if let Some(time) = self.expires_datetime() {
            let time = time.to_offset(UtcOffset::UTC);
            write!(f, "; Expires={}", time.format(&crate::parse::FMT1).map_err(|_| fmt::Error)?)?;
        }

        Ok(())
    }

    /// Returns the name of `self` as a string slice of the raw string `self`
    /// was originally parsed from. If `self` was not originally parsed from a
    /// raw string, returns `None`.
    ///
    /// This method differs from [`Cookie::name()`] in that it returns a string
    /// with the same lifetime as the originally parsed string. This lifetime
    /// may outlive `self`. If a longer lifetime is not required, or you're
    /// unsure if you need a longer lifetime, use [`Cookie::name()`].
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let cookie_string = format!("{}={}", "foo", "bar");
    ///
    /// // `c` will be dropped at the end of the scope, but `name` will live on
    /// let name = {
    ///     let c = Cookie::parse(cookie_string.as_str()).unwrap();
    ///     c.name_raw()
    /// };
    ///
    /// assert_eq!(name, Some("foo"));
    /// ```
    #[inline]
    pub fn name_raw(&self) -> Option<&'c str> {
        self.cookie_string.as_ref()
            .and_then(|s| self.name.to_raw_str(s))
    }

    /// Returns the value of `self` as a string slice of the raw string `self`
    /// was originally parsed from. If `self` was not originally parsed from a
    /// raw string, returns `None`.
    ///
    /// This method differs from [`Cookie::value()`] in that it returns a
    /// string with the same lifetime as the originally parsed string. This
    /// lifetime may outlive `self`. If a longer lifetime is not required, or
    /// you're unsure if you need a longer lifetime, use [`Cookie::value()`].
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let cookie_string = format!("{}={}", "foo", "bar");
    ///
    /// // `c` will be dropped at the end of the scope, but `value` will live on
    /// let value = {
    ///     let c = Cookie::parse(cookie_string.as_str()).unwrap();
    ///     c.value_raw()
    /// };
    ///
    /// assert_eq!(value, Some("bar"));
    /// ```
    #[inline]
    pub fn value_raw(&self) -> Option<&'c str> {
        self.cookie_string.as_ref()
            .and_then(|s| self.value.to_raw_str(s))
    }

    /// Returns the `Path` of `self` as a string slice of the raw string `self`
    /// was originally parsed from. If `self` was not originally parsed from a
    /// raw string, or if `self` doesn't contain a `Path`, or if the `Path` has
    /// changed since parsing, returns `None`.
    ///
    /// This method differs from [`Cookie::path()`] in that it returns a
    /// string with the same lifetime as the originally parsed string. This
    /// lifetime may outlive `self`. If a longer lifetime is not required, or
    /// you're unsure if you need a longer lifetime, use [`Cookie::path()`].
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let cookie_string = format!("{}={}; Path=/", "foo", "bar");
    ///
    /// // `c` will be dropped at the end of the scope, but `path` will live on
    /// let path = {
    ///     let c = Cookie::parse(cookie_string.as_str()).unwrap();
    ///     c.path_raw()
    /// };
    ///
    /// assert_eq!(path, Some("/"));
    /// ```
    #[inline]
    pub fn path_raw(&self) -> Option<&'c str> {
        match (self.path.as_ref(), self.cookie_string.as_ref()) {
            (Some(path), Some(string)) => path.to_raw_str(string),
            _ => None,
        }
    }

    /// Returns the `Domain` of `self` as a string slice of the raw string
    /// `self` was originally parsed from. If `self` was not originally parsed
    /// from a raw string, or if `self` doesn't contain a `Domain`, or if the
    /// `Domain` has changed since parsing, returns `None`.
    ///
    /// This method differs from [`Cookie::domain()`] in that it returns a
    /// string with the same lifetime as the originally parsed string. This
    /// lifetime may outlive `self` struct. If a longer lifetime is not
    /// required, or you're unsure if you need a longer lifetime, use
    /// [`Cookie::domain()`].
    ///
    /// # Example
    ///
    /// ```
    /// use cookie::Cookie;
    ///
    /// let cookie_string = format!("{}={}; Domain=crates.io", "foo", "bar");
    ///
    /// //`c` will be dropped at the end of the scope, but `domain` will live on
    /// let domain = {
    ///     let c = Cookie::parse(cookie_string.as_str()).unwrap();
    ///     c.domain_raw()
    /// };
    ///
    /// assert_eq!(domain, Some("crates.io"));
    /// ```
    #[inline]
    pub fn domain_raw(&self) -> Option<&'c str> {
        match (self.domain.as_ref(), self.cookie_string.as_ref()) {
            (Some(domain), Some(string)) => domain.to_raw_str(string),
            _ => None,
        }
    }

    /// Wraps `self` in an encoded [`Display`]: a cost-free wrapper around
    /// `Cookie` whose [`fmt::Display`] implementation percent-encodes the name
    /// and value of the wrapped `Cookie`.
    ///
    /// The returned structure can be chained with [`Display::stripped()`] to
    /// display only the name and value.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::build("my name", "this; value?").secure(true).finish();
    /// assert_eq!(&c.encoded().to_string(), "my%20name=this%3B%20value%3F; Secure");
    /// assert_eq!(&c.encoded().stripped().to_string(), "my%20name=this%3B%20value%3F");
    /// ```
    #[cfg(feature = "percent-encode")]
    #[cfg_attr(all(nightly, doc), doc(cfg(feature = "percent-encode")))]
    #[inline(always)]
    pub fn encoded<'a>(&'a self) -> Display<'a, 'c> {
        Display::new_encoded(self)
    }

    /// Wraps `self` in a stripped `Display`]: a cost-free wrapper around
    /// `Cookie` whose [`fmt::Display`] implementation prints only the `name`
    /// and `value` of the wrapped `Cookie`.
    ///
    /// The returned structure can be chained with [`Display::encoded()`] to
    /// encode the name and value.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Cookie;
    ///
    /// let mut c = Cookie::build("key?", "value").secure(true).path("/").finish();
    /// assert_eq!(&c.stripped().to_string(), "key?=value");
    #[cfg_attr(feature = "percent-encode", doc = r##"
// Note: `encoded()` is only available when `percent-encode` is enabled.
assert_eq!(&c.stripped().encoded().to_string(), "key%3F=value");
    #"##)]
    /// ```
    #[inline(always)]
    pub fn stripped<'a>(&'a self) -> Display<'a, 'c> {
        Display::new_stripped(self)
    }
}

#[cfg(feature = "percent-encode")]
mod encoding {
    use percent_encoding::{AsciiSet, CONTROLS};

    /// https://url.spec.whatwg.org/#fragment-percent-encode-set
    const FRAGMENT: &AsciiSet = &CONTROLS
        .add(b' ')
        .add(b'"')
        .add(b'<')
        .add(b'>')
        .add(b'`');

    /// https://url.spec.whatwg.org/#path-percent-encode-set
    const PATH: &AsciiSet = &FRAGMENT
        .add(b'#')
        .add(b'?')
        .add(b'{')
        .add(b'}');

    /// https://url.spec.whatwg.org/#userinfo-percent-encode-set
    const USERINFO: &AsciiSet = &PATH
        .add(b'/')
        .add(b':')
        .add(b';')
        .add(b'=')
        .add(b'@')
        .add(b'[')
        .add(b'\\')
        .add(b']')
        .add(b'^')
        .add(b'|')
        .add(b'%');

    /// https://www.rfc-editor.org/rfc/rfc6265#section-4.1.1 + '(', ')'
    const COOKIE: &AsciiSet = &USERINFO
        .add(b'(')
        .add(b')')
        .add(b',');

    /// Percent-encode a cookie name or value with the proper encoding set.
    pub fn encode(string: &str) -> impl std::fmt::Display + '_ {
        percent_encoding::percent_encode(string.as_bytes(), COOKIE)
    }
}

/// Wrapper around `Cookie` whose `Display` implementation either
/// percent-encodes the cookie's name and value, skips displaying the cookie's
/// parameters (only displaying it's name and value), or both.
///
/// A value of this type can be obtained via [`Cookie::encoded()`] and
/// [`Cookie::stripped()`], or an arbitrary chaining of the two methods. This
/// type should only be used for its `Display` implementation.
///
/// # Example
///
/// ```rust
/// use cookie::Cookie;
///
/// let c = Cookie::build("my name", "this; value%?").secure(true).finish();
/// assert_eq!(&c.stripped().to_string(), "my name=this; value%?");
#[cfg_attr(feature = "percent-encode", doc = r##"
// Note: `encoded()` is only available when `percent-encode` is enabled.
assert_eq!(&c.encoded().to_string(), "my%20name=this%3B%20value%25%3F; Secure");
assert_eq!(&c.stripped().encoded().to_string(), "my%20name=this%3B%20value%25%3F");
assert_eq!(&c.encoded().stripped().to_string(), "my%20name=this%3B%20value%25%3F");
"##)]
/// ```
pub struct Display<'a, 'c: 'a> {
    cookie: &'a Cookie<'c>,
    #[cfg(feature = "percent-encode")]
    encode: bool,
    strip: bool,
}

impl<'a, 'c: 'a> fmt::Display for Display<'a, 'c> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        #[cfg(feature = "percent-encode")] {
            if self.encode {
                let name = encoding::encode(self.cookie.name());
                let value = encoding::encode(self.cookie.value());
                write!(f, "{}={}", name, value)?;
            } else {
                write!(f, "{}={}", self.cookie.name(), self.cookie.value())?;
            }
        }

        #[cfg(not(feature = "percent-encode"))] {
            write!(f, "{}={}", self.cookie.name(), self.cookie.value())?;
        }

        match self.strip {
            true => Ok(()),
            false => self.cookie.fmt_parameters(f)
        }
    }
}

impl<'a, 'c> Display<'a, 'c> {
    #[cfg(feature = "percent-encode")]
    fn new_encoded(cookie: &'a Cookie<'c>) -> Self {
        Display { cookie, strip: false, encode: true }
    }

    fn new_stripped(cookie: &'a Cookie<'c>) -> Self {
        Display { cookie, strip: true, #[cfg(feature = "percent-encode")] encode: false }
    }

    /// Percent-encode the name and value pair.
    #[inline]
    #[cfg(feature = "percent-encode")]
    #[cfg_attr(all(nightly, doc), doc(cfg(feature = "percent-encode")))]
    pub fn encoded(mut self) -> Self {
        self.encode = true;
        self
    }

    /// Only display the name and value.
    #[inline]
    pub fn stripped(mut self) -> Self {
        self.strip = true;
        self
    }
}

impl<'c> fmt::Display for Cookie<'c> {
    /// Formats the cookie `self` as a `Set-Cookie` header value.
    ///
    /// Does _not_ percent-encode any values. To percent-encode, use
    /// [`Cookie::encoded()`].
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Cookie;
    ///
    /// let mut cookie = Cookie::build("foo", "bar")
    ///     .path("/")
    ///     .finish();
    ///
    /// assert_eq!(&cookie.to_string(), "foo=bar; Path=/");
    /// ```
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}={}", self.name(), self.value())?;
        self.fmt_parameters(f)
    }
}

impl FromStr for Cookie<'static> {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Cookie<'static>, ParseError> {
        Cookie::parse(s).map(|c| c.into_owned())
    }
}

impl<'a, 'b> PartialEq<Cookie<'b>> for Cookie<'a> {
    fn eq(&self, other: &Cookie<'b>) -> bool {
        let so_far_so_good = self.name() == other.name()
            && self.value() == other.value()
            && self.http_only() == other.http_only()
            && self.secure() == other.secure()
            && self.max_age() == other.max_age()
            && self.expires() == other.expires();

        if !so_far_so_good {
            return false;
        }

        match (self.path(), other.path()) {
            (Some(a), Some(b)) if a.eq_ignore_ascii_case(b) => {}
            (None, None) => {}
            _ => return false,
        };

        match (self.domain(), other.domain()) {
            (Some(a), Some(b)) if a.eq_ignore_ascii_case(b) => {}
            (None, None) => {}
            _ => return false,
        };

        true
    }
}

#[cfg(test)]
mod tests {
    use crate::{Cookie, SameSite, parse::parse_date};
    use time::{Duration, OffsetDateTime};

    #[test]
    fn format() {
        let cookie = Cookie::new("foo", "bar");
        assert_eq!(&cookie.to_string(), "foo=bar");

        let cookie = Cookie::build("foo", "bar")
            .http_only(true).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; HttpOnly");

        let cookie = Cookie::build("foo", "bar")
            .max_age(Duration::seconds(10)).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; Max-Age=10");

        let cookie = Cookie::build("foo", "bar")
            .secure(true).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; Secure");

        let cookie = Cookie::build("foo", "bar")
            .path("/").finish();
        assert_eq!(&cookie.to_string(), "foo=bar; Path=/");

        let cookie = Cookie::build("foo", "bar")
            .domain("www.rust-lang.org").finish();
        assert_eq!(&cookie.to_string(), "foo=bar; Domain=www.rust-lang.org");

        let time_str = "Wed, 21 Oct 2015 07:28:00 GMT";
        let expires = parse_date(time_str, &crate::parse::FMT1).unwrap();
        let cookie = Cookie::build("foo", "bar")
            .expires(expires).finish();
        assert_eq!(&cookie.to_string(),
                   "foo=bar; Expires=Wed, 21 Oct 2015 07:28:00 GMT");

        let cookie = Cookie::build("foo", "bar")
            .same_site(SameSite::Strict).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; SameSite=Strict");

        let cookie = Cookie::build("foo", "bar")
            .same_site(SameSite::Lax).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; SameSite=Lax");

        let mut cookie = Cookie::build("foo", "bar")
            .same_site(SameSite::None).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; SameSite=None; Secure");

        cookie.set_same_site(None);
        assert_eq!(&cookie.to_string(), "foo=bar");

        let mut cookie = Cookie::build("foo", "bar")
            .same_site(SameSite::None)
            .secure(false)
            .finish();
        assert_eq!(&cookie.to_string(), "foo=bar; SameSite=None");
        cookie.set_secure(true);
        assert_eq!(&cookie.to_string(), "foo=bar; SameSite=None; Secure");
    }

    #[test]
    #[ignore]
    fn format_date_wraps() {
        let expires = OffsetDateTime::UNIX_EPOCH + Duration::MAX;
        let cookie = Cookie::build("foo", "bar").expires(expires).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; Expires=Fri, 31 Dec 9999 23:59:59 GMT");

        let expires = time::macros::datetime!(9999-01-01 0:00 UTC) + Duration::days(1000);
        let cookie = Cookie::build("foo", "bar").expires(expires).finish();
        assert_eq!(&cookie.to_string(), "foo=bar; Expires=Fri, 31 Dec 9999 23:59:59 GMT");
    }

    #[test]
    fn cookie_string_long_lifetimes() {
        let cookie_string = "bar=baz; Path=/subdir; HttpOnly; Domain=crates.io".to_owned();
        let (name, value, path, domain) = {
            // Create a cookie passing a slice
            let c = Cookie::parse(cookie_string.as_str()).unwrap();
            (c.name_raw(), c.value_raw(), c.path_raw(), c.domain_raw())
        };

        assert_eq!(name, Some("bar"));
        assert_eq!(value, Some("baz"));
        assert_eq!(path, Some("/subdir"));
        assert_eq!(domain, Some("crates.io"));
    }

    #[test]
    fn owned_cookie_string() {
        let cookie_string = "bar=baz; Path=/subdir; HttpOnly; Domain=crates.io".to_owned();
        let (name, value, path, domain) = {
            // Create a cookie passing an owned string
            let c = Cookie::parse(cookie_string).unwrap();
            (c.name_raw(), c.value_raw(), c.path_raw(), c.domain_raw())
        };

        assert_eq!(name, None);
        assert_eq!(value, None);
        assert_eq!(path, None);
        assert_eq!(domain, None);
    }

    #[test]
    fn owned_cookie_struct() {
        let cookie_string = "bar=baz; Path=/subdir; HttpOnly; Domain=crates.io";
        let (name, value, path, domain) = {
            // Create an owned cookie
            let c = Cookie::parse(cookie_string).unwrap().into_owned();

            (c.name_raw(), c.value_raw(), c.path_raw(), c.domain_raw())
        };

        assert_eq!(name, None);
        assert_eq!(value, None);
        assert_eq!(path, None);
        assert_eq!(domain, None);
    }

    #[test]
    #[cfg(feature = "percent-encode")]
    fn format_encoded() {
        let cookie = Cookie::build("foo !%?=", "bar;;, a").finish();
        let cookie_str = cookie.encoded().to_string();
        assert_eq!(&cookie_str, "foo%20!%25%3F%3D=bar%3B%3B%2C%20a");

        let cookie = Cookie::parse_encoded(cookie_str).unwrap();
        assert_eq!(cookie.name_value(), ("foo !%?=", "bar;;, a"));
    }
}
