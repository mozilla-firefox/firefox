use std::borrow::Cow;
use std::error::Error;
use std::convert::{From, TryFrom};
use std::str::Utf8Error;
use std::fmt;

#[allow(unused_imports, deprecated)]
use std::ascii::AsciiExt;

#[cfg(feature = "percent-encode")]
use percent_encoding::percent_decode;
use time::{PrimitiveDateTime, Duration, OffsetDateTime};
use time::{parsing::Parsable, macros::format_description, format_description::FormatItem};

use crate::{Cookie, SameSite, CookieStr};

// The three formats spec'd in http://tools.ietf.org/html/rfc2616#section-3.3.1.
// Additional ones as encountered in the real world.
pub static FMT1: &[FormatItem<'_>] = format_description!("[weekday repr:short], [day] [month repr:short] [year padding:none] [hour]:[minute]:[second] GMT");
pub static FMT2: &[FormatItem<'_>] = format_description!("[weekday], [day]-[month repr:short]-[year repr:last_two] [hour]:[minute]:[second] GMT");
pub static FMT3: &[FormatItem<'_>] = format_description!("[weekday repr:short] [month repr:short] [day padding:space] [hour]:[minute]:[second] [year padding:none]");
pub static FMT4: &[FormatItem<'_>] = format_description!("[weekday repr:short], [day]-[month repr:short]-[year padding:none] [hour]:[minute]:[second] GMT");

/// Enum corresponding to a parsing error.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
#[non_exhaustive]
pub enum ParseError {
    /// The cookie did not contain a name/value pair.
    MissingPair,
    /// The cookie's name was empty.
    EmptyName,
    /// Decoding the cookie's name or value resulted in invalid UTF-8.
    Utf8Error(Utf8Error),
}

impl ParseError {
    /// Returns a description of this error as a string
    pub fn as_str(&self) -> &'static str {
        match *self {
            ParseError::MissingPair => "the cookie is missing a name/value pair",
            ParseError::EmptyName => "the cookie's name is empty",
            ParseError::Utf8Error(_) => {
                "decoding the cookie's name or value resulted in invalid UTF-8"
            }
        }
    }
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

impl From<Utf8Error> for ParseError {
    fn from(error: Utf8Error) -> ParseError {
        ParseError::Utf8Error(error)
    }
}

impl Error for ParseError {
    fn description(&self) -> &str {
        self.as_str()
    }
}

fn indexes_of(needle: &str, haystack: &str) -> Option<(usize, usize)> {
    let haystack_start = haystack.as_ptr() as usize;
    let needle_start = needle.as_ptr() as usize;

    if needle_start < haystack_start {
        return None;
    }

    if (needle_start + needle.len()) > (haystack_start + haystack.len()) {
        return None;
    }

    let start = needle_start - haystack_start;
    let end = start + needle.len();
    Some((start, end))
}

#[cfg(feature = "percent-encode")]
fn name_val_decoded(
    name: &str,
    val: &str
) -> Result<Option<(CookieStr<'static>, CookieStr<'static>)>, ParseError> {
    let decoded_name = percent_decode(name.as_bytes()).decode_utf8()?;
    let decoded_value = percent_decode(val.as_bytes()).decode_utf8()?;

    if let (&Cow::Borrowed(_), &Cow::Borrowed(_)) = (&decoded_name, &decoded_value) {
         Ok(None)
    } else {
        let name = CookieStr::Concrete(Cow::Owned(decoded_name.into()));
        let val = CookieStr::Concrete(Cow::Owned(decoded_value.into()));
        Ok(Some((name, val)))
    }
}

#[cfg(not(feature = "percent-encode"))]
fn name_val_decoded(
    _: &str,
    _: &str
) -> Result<Option<(CookieStr<'static>, CookieStr<'static>)>, ParseError> {
    unreachable!("This function should never be called with 'percent-encode' disabled!")
}

fn trim_quotes(s: &str) -> &str {
    if s.len() < 2 {
        return s;
    }

    match (s.chars().next(), s.chars().last()) {
        (Some('"'), Some('"')) => &s[1..(s.len() - 1)],
        _ => s
    }
}

// This function does the real parsing but _does not_ set the `cookie_string` in
// the returned cookie object. This only exists so that the borrow to `s` is
// returned at the end of the call, allowing the `cookie_string` field to be
// set in the outer `parse` function.
fn parse_inner<'c>(s: &str, decode: bool) -> Result<Cookie<'c>, ParseError> {
    let mut attributes = s.split(';');

    // Determine the name = val.
    let key_value = attributes.next().expect("first str::split().next() returns Some");
    let (name, value) = match key_value.find('=') {
        Some(i) => {
            let (key, value) = (key_value[..i].trim(), key_value[(i + 1)..].trim());
            (key, trim_quotes(value).trim())
        },
        None => return Err(ParseError::MissingPair)
    };

    if name.is_empty() {
        return Err(ParseError::EmptyName);
    }

    // If there is nothing to decode, or we're not decoding, use indexes.
    let indexed_names = |s, name, value| {
        let name_indexes = indexes_of(name, s).expect("name sub");
        let value_indexes = indexes_of(value, s).expect("value sub");
        let name = CookieStr::Indexed(name_indexes.0, name_indexes.1);
        let value = CookieStr::Indexed(value_indexes.0, value_indexes.1);
        (name, value)
    };

    // Create a cookie with all of the defaults. We'll fill things in while we
    // iterate through the parameters below.
    let (name, value) = if decode {
        match name_val_decoded(name, value)? {
            Some((name, value)) => (name, value),
            None => indexed_names(s, name, value)
        }
    } else {
        indexed_names(s, name, value)
    };

    let mut cookie: Cookie<'c> = Cookie {
        name, value,
        cookie_string: None,
        expires: None,
        max_age: None,
        domain: None,
        path: None,
        secure: None,
        http_only: None,
        same_site: None
    };

    for attr in attributes {
        let (key, value) = match attr.find('=') {
            Some(i) => (attr[..i].trim(), Some(attr[(i + 1)..].trim())),
            None => (attr.trim(), None),
        };

        match (&*key.to_ascii_lowercase(), value) {
            ("secure", _) => cookie.secure = Some(true),
            ("httponly", _) => cookie.http_only = Some(true),
            ("max-age", Some(mut v)) => cookie.max_age = {
                let is_negative = v.starts_with('-');
                if is_negative {
                    v = &v[1..];
                }

                if !v.chars().all(|d| d.is_digit(10)) {
                    continue
                }

                // From RFC 6265 5.2.2: neg values indicate that the earliest
                // expiration should be used, so set the max age to 0 seconds.
                if is_negative {
                    Some(Duration::ZERO)
                } else {
                    Some(v.parse::<i64>()
                        .map(Duration::seconds)
                        .unwrap_or_else(|_| Duration::seconds(i64::max_value())))
                }
            },
            ("domain", Some(mut domain)) if !domain.is_empty() => {
                if domain.starts_with('.') {
                    domain = &domain[1..];
                }

                let (i, j) = indexes_of(domain, s).expect("domain sub");
                cookie.domain = Some(CookieStr::Indexed(i, j));
            }
            ("path", Some(v)) => {
                let (i, j) = indexes_of(v, s).expect("path sub");
                cookie.path = Some(CookieStr::Indexed(i, j));
            }
            ("samesite", Some(v)) => {
                if v.eq_ignore_ascii_case("strict") {
                    cookie.same_site = Some(SameSite::Strict);
                } else if v.eq_ignore_ascii_case("lax") {
                    cookie.same_site = Some(SameSite::Lax);
                } else if v.eq_ignore_ascii_case("none") {
                    cookie.same_site = Some(SameSite::None);
                } else {
                    // We do nothing here, for now. When/if the `SameSite`
                    // attribute becomes standard, the spec says that we should
                    // ignore this cookie, i.e, fail to parse it, when an
                    // invalid value is passed in. The draft is at
                    // http://httpwg.org/http-extensions/draft-ietf-httpbis-cookie-same-site.html.
                }
            }
            ("expires", Some(v)) => {
                let tm = parse_date(v, &FMT1)
                    .or_else(|_| parse_date(v, &FMT2))
                    .or_else(|_| parse_date(v, &FMT3))
                    .or_else(|_| parse_date(v, &FMT4));
                    // .or_else(|_| parse_date(v, &FMT5));

                if let Ok(time) = tm {
                    cookie.expires = Some(time.into())
                }
            }
            _ => {
                // We're going to be permissive here. If we have no idea what
                // this is, then it's something nonstandard. We're not going to
                // store it (because it's not compliant), but we're also not
                // going to emit an error.
            }
        }
    }

    Ok(cookie)
}

pub(crate) fn parse_cookie<'c, S>(cow: S, decode: bool) -> Result<Cookie<'c>, ParseError>
    where S: Into<Cow<'c, str>>
{
    let s = cow.into();
    let mut cookie = parse_inner(&s, decode)?;
    cookie.cookie_string = Some(s);
    Ok(cookie)
}

pub(crate) fn parse_date(s: &str, format: &impl Parsable) -> Result<OffsetDateTime, time::Error> {
    // Parse. Handle "abbreviated" dates like Chromium. See cookie#162.
    let mut date = format.parse(s.as_bytes())?;
    if let Some(y) = date.year().or_else(|| date.year_last_two().map(|v| v as i32)) {
        let offset = match y {
            0..=68 => 2000,
            69..=99 => 1900,
            _ => 0,
        };

        date.set_year(y + offset);
    }

    Ok(PrimitiveDateTime::try_from(date)?.assume_utc())
}

#[cfg(test)]
mod tests {
    use super::parse_date;
    use crate::{Cookie, SameSite};
    use time::Duration;

    macro_rules! assert_eq_parse {
        ($string:expr, $expected:expr) => (
            let cookie = match Cookie::parse($string) {
                Ok(cookie) => cookie,
                Err(e) => panic!("Failed to parse {:?}: {:?}", $string, e)
            };

            assert_eq!(cookie, $expected);
        )
    }

    macro_rules! assert_ne_parse {
        ($string:expr, $expected:expr) => (
            let cookie = match Cookie::parse($string) {
                Ok(cookie) => cookie,
                Err(e) => panic!("Failed to parse {:?}: {:?}", $string, e)
            };

            assert_ne!(cookie, $expected);
        )
    }

    #[test]
    fn parse_same_site() {
        let expected = Cookie::build("foo", "bar")
            .same_site(SameSite::Lax)
            .finish();

        assert_eq_parse!("foo=bar; SameSite=Lax", expected);
        assert_eq_parse!("foo=bar; SameSite=lax", expected);
        assert_eq_parse!("foo=bar; SameSite=LAX", expected);
        assert_eq_parse!("foo=bar; samesite=Lax", expected);
        assert_eq_parse!("foo=bar; SAMESITE=Lax", expected);

        let expected = Cookie::build("foo", "bar")
            .same_site(SameSite::Strict)
            .finish();

        assert_eq_parse!("foo=bar; SameSite=Strict", expected);
        assert_eq_parse!("foo=bar; SameSITE=Strict", expected);
        assert_eq_parse!("foo=bar; SameSite=strict", expected);
        assert_eq_parse!("foo=bar; SameSite=STrICT", expected);
        assert_eq_parse!("foo=bar; SameSite=STRICT", expected);

        let expected = Cookie::build("foo", "bar")
            .same_site(SameSite::None)
            .finish();

        assert_eq_parse!("foo=bar; SameSite=None", expected);
        assert_eq_parse!("foo=bar; SameSITE=none", expected);
        assert_eq_parse!("foo=bar; SameSite=NOne", expected);
        assert_eq_parse!("foo=bar; SameSite=nOne", expected);
    }

    #[test]
    fn parse() {
        assert!(Cookie::parse("bar").is_err());
        assert!(Cookie::parse("=bar").is_err());
        assert!(Cookie::parse(" =bar").is_err());
        assert!(Cookie::parse("foo=").is_ok());

        let expected = Cookie::build("foo", "bar=baz").finish();
        assert_eq_parse!("foo=bar=baz", expected);

        let expected = Cookie::build("foo", "\"bar\"").finish();
        assert_eq_parse!("foo=\"\"bar\"\"", expected);

        let expected = Cookie::build("foo", "\"bar").finish();
        assert_eq_parse!("foo=  \"bar", expected);
        assert_eq_parse!("foo=\"bar  ", expected);
        assert_eq_parse!("foo=\"\"bar\"", expected);
        assert_eq_parse!("foo=\"\"bar  \"", expected);
        assert_eq_parse!("foo=\"\"bar  \"  ", expected);

        let expected = Cookie::build("foo", "bar\"").finish();
        assert_eq_parse!("foo=bar\"", expected);
        assert_eq_parse!("foo=\"bar\"\"", expected);
        assert_eq_parse!("foo=\"  bar\"\"", expected);
        assert_eq_parse!("foo=\"  bar\"  \"  ", expected);

        let mut expected = Cookie::build("foo", "bar").finish();
        assert_eq_parse!("foo=bar", expected);
        assert_eq_parse!("foo = bar", expected);
        assert_eq_parse!("foo=\"bar\"", expected);
        assert_eq_parse!(" foo=bar ", expected);
        assert_eq_parse!(" foo=\"bar   \" ", expected);
        assert_eq_parse!(" foo=bar ;Domain=", expected);
        assert_eq_parse!(" foo=bar ;Domain= ", expected);
        assert_eq_parse!(" foo=bar ;Ignored", expected);

        let mut unexpected = Cookie::build("foo", "bar").http_only(false).finish();
        assert_ne_parse!(" foo=bar ;HttpOnly", unexpected);
        assert_ne_parse!(" foo=bar; httponly", unexpected);

        expected.set_http_only(true);
        assert_eq_parse!(" foo=bar ;HttpOnly", expected);
        assert_eq_parse!(" foo=bar ;httponly", expected);
        assert_eq_parse!(" foo=bar ;HTTPONLY=whatever", expected);
        assert_eq_parse!(" foo=bar ; sekure; HTTPONLY", expected);

        expected.set_secure(true);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure=aaaa", expected);

        unexpected.set_http_only(true);
        unexpected.set_secure(true);
        assert_ne_parse!(" foo=bar ;HttpOnly; skeure", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; =secure", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly;", unexpected);

        unexpected.set_secure(false);
        assert_ne_parse!(" foo=bar ;HttpOnly; secure", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; secure", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; secure", unexpected);

        expected.set_max_age(Duration::ZERO);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=0", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age = 0 ", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=-1", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age = -1 ", expected);

        expected.set_max_age(Duration::minutes(1));
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=60", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age =   60 ", expected);

        expected.set_max_age(Duration::seconds(4));
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age = 4 ", expected);

        unexpected.set_secure(true);
        unexpected.set_max_age(Duration::minutes(1));
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=122", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age = 38 ", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=51", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age = -1 ", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age = 0", unexpected);

        expected.set_path("/");
        assert_eq_parse!("foo=bar;HttpOnly; Secure; Max-Age=4; Path=/", expected);
        assert_eq_parse!("foo=bar;HttpOnly; Secure; Max-Age=4;Path=/", expected);

        expected.set_path("/foo");
        assert_eq_parse!("foo=bar;HttpOnly; Secure; Max-Age=4; Path=/foo", expected);
        assert_eq_parse!("foo=bar;HttpOnly; Secure; Max-Age=4;Path=/foo", expected);
        assert_eq_parse!("foo=bar;HttpOnly; Secure; Max-Age=4;path=/foo", expected);
        assert_eq_parse!("foo=bar;HttpOnly; Secure; Max-Age=4;path = /foo", expected);

        unexpected.set_max_age(Duration::seconds(4));
        unexpected.set_path("/bar");
        assert_ne_parse!("foo=bar;HttpOnly; Secure; Max-Age=4; Path=/foo", unexpected);
        assert_ne_parse!("foo=bar;HttpOnly; Secure; Max-Age=4;Path=/baz", unexpected);

        expected.set_domain("www.foo.com");
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=www.foo.com", expected);

        expected.set_domain("foo.com");
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=foo.com", expected);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=FOO.COM", expected);

        unexpected.set_path("/foo");
        unexpected.set_domain("bar.com");
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=foo.com", unexpected);
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=FOO.COM", unexpected);

        let time_str = "Wed, 21 Oct 2015 07:28:00 GMT";
        let expires = parse_date(time_str, &super::FMT1).unwrap();
        expected.set_expires(expires);
        assert_eq_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=foo.com; Expires=Wed, 21 Oct 2015 07:28:00 GMT", expected);

        unexpected.set_domain("foo.com");
        let bad_expires = parse_date(time_str, &super::FMT1).unwrap();
        expected.set_expires(bad_expires);
        assert_ne_parse!(" foo=bar ;HttpOnly; Secure; Max-Age=4; Path=/foo; \
            Domain=foo.com; Expires=Wed, 21 Oct 2015 07:28:00 GMT", unexpected);
    }

    #[test]
    fn parse_abbreviated_years() {
        let cookie_str = "foo=bar; expires=Thu, 10-Sep-20 20:00:00 GMT";
        let cookie = Cookie::parse(cookie_str).unwrap();
        assert_eq!(cookie.expires_datetime().unwrap().year(), 2020);

        let cookie_str = "foo=bar; expires=Thu, 10-Sep-68 20:00:00 GMT";
        let cookie = Cookie::parse(cookie_str).unwrap();
        assert_eq!(cookie.expires_datetime().unwrap().year(), 2068);

        let cookie_str = "foo=bar; expires=Thu, 10-Sep-69 20:00:00 GMT";
        let cookie = Cookie::parse(cookie_str).unwrap();
        assert_eq!(cookie.expires_datetime().unwrap().year(), 1969);

        let cookie_str = "foo=bar; expires=Thu, 10-Sep-99 20:00:00 GMT";
        let cookie = Cookie::parse(cookie_str).unwrap();
        assert_eq!(cookie.expires_datetime().unwrap().year(), 1999);

        let cookie_str = "foo=bar; expires=Thu, 10-Sep-2069 20:00:00 GMT";
        let cookie = Cookie::parse(cookie_str).unwrap();
        assert_eq!(cookie.expires_datetime().unwrap().year(), 2069);
    }

    #[test]
    fn parse_variant_date_fmts() {
        let cookie_str = "foo=bar; expires=Sun, 06 Nov 1994 08:49:37 GMT";
        Cookie::parse(cookie_str).unwrap().expires_datetime().unwrap();

        let cookie_str = "foo=bar; expires=Sunday, 06-Nov-94 08:49:37 GMT";
        Cookie::parse(cookie_str).unwrap().expires_datetime().unwrap();

        let cookie_str = "foo=bar; expires=Sun Nov  6 08:49:37 1994";
        Cookie::parse(cookie_str).unwrap().expires_datetime().unwrap();
    }

    #[test]
    fn parse_very_large_max_ages() {
        let mut expected = Cookie::build("foo", "bar")
            .max_age(Duration::seconds(i64::max_value()))
            .finish();

        let string = format!("foo=bar; Max-Age={}", 1u128 << 100);
        assert_eq_parse!(&string, expected);

        expected.set_max_age(Duration::seconds(0));
        assert_eq_parse!("foo=bar; Max-Age=-129", expected);

        let string = format!("foo=bar; Max-Age=-{}", 1u128 << 100);
        assert_eq_parse!(&string, expected);

        let string = format!("foo=bar; Max-Age=-{}", i64::max_value());
        assert_eq_parse!(&string, expected);

        let string = format!("foo=bar; Max-Age={}", i64::max_value());
        expected.set_max_age(Duration::seconds(i64::max_value()));
        assert_eq_parse!(&string, expected);
    }

    #[test]
    fn odd_characters() {
        let expected = Cookie::new("foo", "b%2Fr");
        assert_eq_parse!("foo=b%2Fr", expected);
    }

    #[test]
    #[cfg(feature = "percent-encode")]
    fn odd_characters_encoded() {
        let expected = Cookie::new("foo", "b/r");
        let cookie = match Cookie::parse_encoded("foo=b%2Fr") {
            Ok(cookie) => cookie,
            Err(e) => panic!("Failed to parse: {:?}", e)
        };

        assert_eq!(cookie, expected);
    }

    #[test]
    fn do_not_panic_on_large_max_ages() {
        let max_seconds = Duration::MAX.whole_seconds();
        let expected = Cookie::build("foo", "bar")
            .max_age(Duration::seconds(max_seconds))
            .finish();
        let too_many_seconds = (max_seconds as u64) + 1;
        assert_eq_parse!(format!(" foo=bar; Max-Age={:?}", too_many_seconds), expected);
    }
}
