// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    net::Ipv6Addr,
    str::{FromStr, from_utf8},
};

use enumset::{EnumSet, EnumSetType, enum_set};
use neqo_common::{Header, header::HeadersExt as _};

use crate::{Error, MessageType, Res};

#[derive(EnumSetType, Debug)]
enum PseudoHeaderState {
    Status,
    Method,
    Scheme,
    Authority,
    Path,
    Protocol,
    Regular,
}

impl PseudoHeaderState {
    fn is_pseudo(self) -> bool {
        self != Self::Regular
    }
}

impl TryFrom<(MessageType, &str)> for PseudoHeaderState {
    type Error = Error;

    fn try_from(v: (MessageType, &str)) -> Res<Self> {
        match v {
            (MessageType::Response, ":status") => Ok(Self::Status),
            (MessageType::Request, ":method") => Ok(Self::Method),
            (MessageType::Request, ":scheme") => Ok(Self::Scheme),
            (MessageType::Request, ":authority") => Ok(Self::Authority),
            (MessageType::Request, ":path") => Ok(Self::Path),
            (MessageType::Request, ":protocol") => Ok(Self::Protocol),
            (_, _) => Err(Error::InvalidHeader),
        }
    }
}

/// Check whether the response is informational(1xx).
///
/// # Errors
///
/// Returns an error if response headers do not contain
/// a status header or if the value of the header is 101 or cannot be parsed.
pub fn is_interim(headers: &[Header]) -> Res<bool> {
    let status = headers.iter().take(1).find_header(":status");
    match status.map(Header::value) {
        // https://datatracker.ietf.org/doc/html/rfc9114#section-4.5
        Some(b"101") => Err(Error::HttpGeneralProtocolStream),
        Some([d @ b'1'..=b'5', b'0'..=b'9', b'0'..=b'9']) => Ok(*d == b'1'),
        _ => Err(Error::InvalidHeader), // RFC 9110, Section 15.
    }
}

fn track_pseudo(
    name: &str,
    result_state: &mut EnumSet<PseudoHeaderState>,
    message_type: MessageType,
) -> Res<bool> {
    let new_state = if name.starts_with(':') {
        if result_state.contains(PseudoHeaderState::Regular) {
            return Err(Error::InvalidHeader);
        }
        PseudoHeaderState::try_from((message_type, name))?
    } else {
        PseudoHeaderState::Regular
    };

    let pseudo = new_state.is_pseudo();
    if *result_state & new_state == EnumSet::empty() || !pseudo {
        *result_state |= new_state;
        Ok(pseudo)
    } else {
        Err(Error::InvalidHeader)
    }
}

/// Whether `b` is a `tchar` other than ALPHA or DIGIT (RFC 9110, Section 5.6.2).
const fn is_tchar_symbol(b: u8) -> bool {
    matches!(
        b,
        b'!' | b'#'
            | b'$'
            | b'%'
            | b'&'
            | b'\''
            | b'*'
            | b'+'
            | b'-'
            | b'.'
            | b'^'
            | b'_'
            | b'`'
            | b'|'
            | b'~'
    )
}

/// Whether `b` is a `tchar` (RFC 9110, Section 5.6.2).
const fn is_tchar(b: u8) -> bool {
    b.is_ascii_alphanumeric() || is_tchar_symbol(b)
}

/// Whether `s` is a `token` (RFC 9110, Section 5.6.2).
fn is_token(s: &[u8]) -> bool {
    !s.is_empty() && s.iter().copied().all(is_tchar)
}

/// Whether `b` may appear in an HTTP/3 field name (RFC 9114, Section 4.2).
const fn is_field_name_char(b: u8) -> bool {
    b.is_ascii_lowercase() || b.is_ascii_digit() || is_tchar_symbol(b)
}

/// Whether a request scheme is `http` or `https`, the only schemes supported.
#[must_use]
pub const fn request_scheme_valid(scheme: &[u8]) -> bool {
    scheme.eq_ignore_ascii_case(b"http") || scheme.eq_ignore_ascii_case(b"https")
}

/// Whether a request authority is valid (RFC 9110, Section 4.2).
#[must_use]
pub fn request_authority_valid(authority: &[u8]) -> bool {
    authority_valid(authority, false)
}

/// Whether a request path is in origin-form (RFC 9112, Section 3.2.1).
#[must_use]
pub fn request_path_valid(path: &[u8]) -> bool {
    // Other VCHARs are accepted, as Firefox sends them unescaped (RFC 3986 allows only pchar).
    path.starts_with(b"/") && path.iter().all(|&b| b.is_ascii_graphic() && b != b'#')
}

/// Whether a CONNECT target is in authority-form (RFC 9112, Section 3.2.3).
#[must_use]
pub fn connect_authority_valid(authority: &[u8]) -> bool {
    authority_valid(authority, true)
}

/// Whether `s` is a `reg-name` (RFC 3986, Section 3.2.2).
fn is_reg_name(s: &[u8]) -> bool {
    let mut bytes = s.iter();
    while let Some(&b) = bytes.next() {
        let valid = if b == b'%' {
            bytes.next().is_some_and(u8::is_ascii_hexdigit)
                && bytes.next().is_some_and(u8::is_ascii_hexdigit)
        } else {
            b.is_ascii_alphanumeric()
                || matches!(
                    b,
                    b'-' | b'.'
                        | b'_'
                        | b'~'
                        | b'!'
                        | b'$'
                        | b'&'
                        | b'\''
                        | b'('
                        | b')'
                        | b'*'
                        | b'+'
                        | b','
                        | b';'
                        | b'='
                )
        };
        if !valid {
            return false;
        }
    }
    true
}

/// Whether `a` is `host [ ":" port ]` (RFC 3986, Section 3.2; RFC 9110, Sections 4.2 and 9.3.6).
fn authority_valid(a: &[u8], port_required: bool) -> bool {
    let (host, port) = match a.iter().rposition(|&b| b == b':') {
        Some(i) if i + 1 == a.len() => (&a[..i], None),
        Some(i) if !a[i..].contains(&b']') => (&a[..i], Some(&a[i + 1..])),
        _ => (a, None),
    };
    let host_valid = match host {
        [b'[', ip @ .., b']'] => parse::<Ipv6Addr>(ip).is_some(),
        // TODO: Require a WHATWG valid host string <https://github.com/mozilla/neqo/issues/4328>.
        _ => !host.is_empty() && is_reg_name(host),
    };
    host_valid
        && port.map_or(!port_required, |p| {
            p.first().is_some_and(u8::is_ascii_digit) && parse::<u16>(p).is_some_and(|p| p != 0) // RFC 6335, Section 6.
        })
}

fn parse<T: FromStr>(b: &[u8]) -> Option<T> {
    from_utf8(b).ok()?.parse().ok()
}

/// Checks if request/response headers are well formed, i.e. contain
/// allowed pseudo headers and in a right order, etc.
///
/// # Errors
///
/// Returns an error if headers are not well formed.
pub fn headers_valid(headers: &[Header], message_type: MessageType) -> Res<()> {
    let mut method_value: Option<&[u8]> = None;
    let mut protocol_value: Option<&[u8]> = None;
    let mut scheme_value: Option<&[u8]> = None;
    let mut authority_value: Option<&[u8]> = None;
    let mut path_value: Option<&[u8]> = None;
    let mut pseudo_state = EnumSet::new();
    for header in headers {
        let is_pseudo = track_pseudo(header.name(), &mut pseudo_state, message_type)?;

        if is_pseudo {
            let value = Some(header.value());
            match header.name() {
                ":method" => method_value = value,
                ":protocol" => protocol_value = value,
                ":scheme" => scheme_value = value,
                ":authority" => authority_value = value,
                ":path" => path_value = value,
                _ => {}
            }
        }

        let name = header.name();
        let name = name.strip_prefix(':').unwrap_or(name);
        if name.is_empty() || !name.bytes().all(is_field_name_char) {
            return Err(Error::InvalidHeader); // illegal characters.
        }

        // CR, LF, and NUL are not permitted in a field value either (RFC 9114,
        // Section 4.3); carried verbatim into an HTTP/1.1 serialization they
        // would split the message.
        // Other CTLs are accepted, as in Firefox's HTTP/2 stack and Fetch (httpwg/http-core#215).
        if header
            .value()
            .iter()
            .any(|b| matches!(b, b'\0' | b'\n' | b'\r'))
        {
            return Err(Error::InvalidHeader); // illegal characters.
        }

        // Connection-specific header fields make a message malformed (RFC 9114,
        // Section 4.2). TE is the only one that may appear, and only with the
        // value "trailers". Names are already lowercased above.
        match header.name() {
            "connection" | "keep-alive" | "proxy-connection" | "transfer-encoding" | "upgrade" => {
                return Err(Error::InvalidHeader);
            }
            "te" if header.value() != b"trailers" => return Err(Error::InvalidHeader),
            "host" if message_type == MessageType::Request => {
                if authority_value.is_some_and(|a| a != header.value()) {
                    return Err(Error::InvalidHeader); // RFC 9114, Section 4.3.1.
                }
                authority_value = Some(header.value());
            }
            _ => {}
        }
    }
    // Clear the regular header bit, since we only check pseudo headers below.
    pseudo_state.remove(PseudoHeaderState::Regular);
    let is_connect = method_value == Some(b"CONNECT".as_ref());
    let pseudo_header_mask = match message_type {
        MessageType::Response => enum_set!(PseudoHeaderState::Status),
        MessageType::Request => {
            if is_connect {
                let connect_mask = PseudoHeaderState::Method | PseudoHeaderState::Authority;
                if let Some(protocol) = protocol_value {
                    // For a webtransport CONNECT, the :scheme field must be set to https.
                    if protocol == b"webtransport" && scheme_value != Some(b"https".as_ref()) {
                        return Err(Error::HttpGeneralProtocolStream);
                    }
                    // The CONNECT request with :protocol included must have the scheme,
                    // authority, and path set.
                    connect_mask | PseudoHeaderState::Scheme | PseudoHeaderState::Path
                } else {
                    connect_mask
                }
            } else {
                PseudoHeaderState::Method | PseudoHeaderState::Scheme | PseudoHeaderState::Path
            }
        }
    };

    if (MessageType::Request == message_type)
        && pseudo_state.contains(PseudoHeaderState::Protocol)
        && !is_connect
    {
        return Err(Error::InvalidHeader);
    }

    if pseudo_state & pseudo_header_mask != pseudo_header_mask {
        return Err(Error::InvalidHeader);
    }

    let valid = match message_type {
        MessageType::Response => true,
        MessageType::Request if is_connect && protocol_value.is_none() => {
            scheme_value.is_none()
                && path_value.is_none()
                && authority_value.is_some_and(connect_authority_valid)
        }
        MessageType::Request => match (method_value, scheme_value, authority_value, path_value) {
            (Some(m), Some(s), Some(a), Some(p)) => {
                is_token(m)
                    && request_scheme_valid(s)
                    && request_authority_valid(a)
                    && (request_path_valid(p) || (p == b"*" && m == b"OPTIONS")) // RFC 9112, Section 3.2.4.
            }
            _ => false,
        },
    };
    if valid {
        Ok(())
    } else {
        Err(Error::InvalidHeader)
    }
}

/// Checks if trailers are well formed, i.e. pseudo headers are not
/// allowed in trailers.
///
/// # Errors
///
/// Returns an error if trailers are not well formed.
pub fn trailers_valid(headers: &[Header]) -> Res<()> {
    for header in headers {
        if header.name().starts_with(':') {
            return Err(Error::InvalidHeader);
        }
    }
    Ok(())
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::Header;

    use super::{headers_valid, is_interim};
    use crate::{Error, MessageType, Res};

    fn create_connect_headers() -> Vec<Header> {
        vec![
            Header::new(":method", "CONNECT"),
            Header::new(":protocol", "webtransport"),
            Header::new(":scheme", "https"),
            Header::new(":authority", "something.com"),
            Header::new(":path", "/here"),
        ]
    }

    fn create_connect_headers_without_field(field: &str) -> Vec<Header> {
        create_connect_headers()
            .into_iter()
            .filter(|header| header.name() != field)
            .collect()
    }

    #[test]
    fn connect_with_missing_header() {
        for field in &[":scheme", ":path", ":authority"] {
            assert!(
                headers_valid(
                    &create_connect_headers_without_field(field),
                    MessageType::Request
                )
                .is_err()
            );
        }
    }

    #[test]
    fn invalid_scheme_webtransport_connect() {
        let mut headers = create_connect_headers();
        headers[2] = Header::new(":scheme", "http");
        assert_eq!(
            headers_valid(&headers, MessageType::Request),
            Err(Error::HttpGeneralProtocolStream)
        );
    }

    #[test]
    fn valid_webtransport_connect() {
        assert!(headers_valid(&create_connect_headers(), MessageType::Request).is_ok());
    }

    #[test]
    fn invalid_webtransport_connect_with_status() {
        assert!(
            headers_valid(
                [
                    create_connect_headers(),
                    vec![Header::new(":status", "200")]
                ]
                .concat()
                .as_slice(),
                MessageType::Request
            )
            .is_err()
        );
    }

    #[test]
    fn is_interim_invalid_utf8() {
        // Create a header with invalid UTF-8 bytes in the status value
        let invalid_utf8_bytes = vec![0xFF, 0xFE, 0xFD];
        let header = Header::new(":status", invalid_utf8_bytes.as_slice());
        let headers = vec![header];
        assert!(is_interim(&headers).is_err());
    }

    #[test]
    fn is_interim_not_a_number() {
        let headers = vec![Header::new(":status", "not-a-number")];
        assert!(is_interim(&headers).is_err());
    }

    #[test]
    fn is_interim_not_three_digits() {
        // A status code is a three-digit integer (RFC 9110, Section 15).
        for bad in [
            "+200", "0200", "20", "7", "65535", "+150", "x00", "1x0", "10x", "001", "099", "600",
            "700", "999",
        ] {
            let headers = vec![Header::new(":status", bad)];
            assert_eq!(is_interim(&headers), Err(Error::InvalidHeader));
        }
        assert_eq!(is_interim(&[Header::new(":status", "103")]), Ok(true));
        assert_eq!(is_interim(&[Header::new(":status", "200")]), Ok(false));
    }

    #[test]
    fn protocol_requires_connect_method() {
        // :protocol is only valid with CONNECT method.
        let mut headers = create_connect_headers();
        headers[0] = Header::new(":method", "GET");
        assert!(headers_valid(&headers, MessageType::Request).is_err());
    }

    #[test]
    fn classic_connect_valid() {
        // Classic CONNECT only requires :method and :authority.
        let headers = vec![
            Header::new(":method", "CONNECT"),
            Header::new(":authority", "proxy.example.com:443"),
        ];
        assert!(headers_valid(&headers, MessageType::Request).is_ok());
    }

    #[test]
    fn response_requires_status() {
        let headers = vec![Header::new(":status", "200")];
        assert!(headers_valid(&headers, MessageType::Response).is_ok());
    }

    #[test]
    fn response_host_not_checked() {
        let headers = vec![
            Header::new(":status", "200"),
            Header::new("host", ""),
            Header::new("host", "a.example"),
        ];
        assert_eq!(headers_valid(&headers, MessageType::Response), Ok(()));
    }

    #[test]
    fn response_missing_status() {
        let headers: Vec<Header> = vec![];
        assert!(headers_valid(&headers, MessageType::Response).is_err());
    }

    #[test]
    fn regular_request_valid() {
        let headers = request("GET", "https", "something.com", "/index.html");
        assert_eq!(check(&headers), Ok(()));
    }

    #[test]
    fn reject_cr_lf_in_field_name() {
        // CR (0x0d) and LF (0x0a) in a field name must be treated as malformed
        // (RFC 9114, Section 4.3), otherwise they can be carried verbatim into a
        // downstream HTTP/1.1 serialization and split the message.
        for bad in ["x\rname", "x\nname", "x\r\nname"] {
            let headers = [get_request(), field(bad, "value")].concat();
            assert_eq!(check(&headers), Err(Error::InvalidHeader));
        }

        // The same request with a clean field name is accepted.
        let headers = [get_request(), field("xname", "value")].concat();
        assert_eq!(check(&headers), Ok(()));
    }

    #[test]
    fn reject_cr_lf_nul_in_field_value() {
        // CR (0x0d), LF (0x0a), and NUL (0x00) in a field value must be treated
        // as malformed (RFC 9114, Section 4.3), otherwise they can be carried
        // verbatim into a downstream HTTP/1.1 serialization and split the message.
        for bad in [b"v\rx".as_slice(), b"v\nx", b"v\r\nx", b"v\0x"] {
            let headers = [get_request(), field("name", bad)].concat();
            assert_eq!(check(&headers), Err(Error::InvalidHeader));
        }

        // The same bytes in a pseudo-header value are rejected too.
        let headers = request("GET", "https", "something.com", "/\r\nx");
        assert_eq!(check(&headers), Err(Error::InvalidHeader));
    }

    #[test]
    fn reject_non_token_field_name() {
        // A non-token field name is malformed (RFC 9114, Section 10.3), in either direction.
        for bad in [
            "", "x y", "x\ty", "te ", "x\x0by", "x\x7fy", "x\u{e9}y", "x(y)", "X", "xY",
        ] {
            let response = vec![Header::new(":status", "200"), Header::new(bad, "1")];
            assert_eq!(
                headers_valid(&response, MessageType::Response),
                Err(Error::InvalidHeader)
            );

            let request = [get_request(), field(bad, "1")].concat();
            assert_eq!(check(&request), Err(Error::InvalidHeader));
        }

        // Every lowercase tchar is allowed.
        let response = vec![
            Header::new(":status", "200"),
            Header::new("abcdefghijklmnopqrstuvwxyz0123456789!#$%&'*+-.^_`|~", "1"),
        ];
        assert_eq!(headers_valid(&response, MessageType::Response), Ok(()));
    }

    #[test]
    fn accept_other_ctl_in_field_value() {
        let value = b"1\x0b\x7f2".as_slice();
        let response = vec![Header::new(":status", "200"), Header::new("x", value)];
        assert_eq!(headers_valid(&response, MessageType::Response), Ok(()));

        let request = [get_request(), field("x", value)].concat();
        assert_eq!(check(&request), Ok(()));
    }

    fn get_request() -> Vec<Header> {
        request("GET", "https", "something.com", "/")
    }

    fn request(method: &str, scheme: &str, authority: &str, path: &str) -> Vec<Header> {
        vec![
            Header::new(":method", method),
            Header::new(":scheme", scheme),
            Header::new(":authority", authority),
            Header::new(":path", path),
        ]
    }

    fn connect(authority: &str) -> Vec<Header> {
        vec![
            Header::new(":method", "CONNECT"),
            Header::new(":authority", authority),
        ]
    }

    fn no_authority(scheme: &str) -> Vec<Header> {
        vec![
            Header::new(":method", "GET"),
            Header::new(":scheme", scheme),
            Header::new(":path", "/"),
        ]
    }

    fn field(name: &str, value: impl Into<Vec<u8>>) -> Vec<Header> {
        vec![Header::new(name, value)]
    }

    fn check(headers: &[Header]) -> Res<()> {
        headers_valid(headers, MessageType::Request)
    }

    #[test]
    fn reject_invalid_request_pseudo_header_value() {
        // Invalid pseudo-header values make a request malformed (RFC 9114, Sections 4.3.1 and 4.4).
        for bad in [
            request("GET /admin HTTP/1.1", "https", "a.example", "/x"),
            request("", "https", "a.example", "/x"),
            request("GE\x7fT", "https", "a.example", "/x"),
            request("GET", "", "a.example", "/x"),
            request("GET", "1http", "a.example", "/x"),
            request("GET", "ht_tp", "a.example", "/x"),
            request("GET", "ftp", "a.example", "/x"),
            request("GET", "a1+-.", "u@a.example", "x"),
            request("GET", "https", "", "/x"),
            request("GET", "https", "u@a.example", "/x"),
            request("GET", "http", "u@a.example", "/x"),
            request("GET", "https", "a.example", ""),
            request("GET", "https", "a.example", "x"),
            request("GET", "HTTPS", "a.example", "x"),
            request("GET", "https", "a.example", "*"),
            request("OPTIONS", "https", "a.example", "*x"),
            request("GET", "https", "a.example", "/x HTTP/1.0"),
            request("GET", "https", "a.example", "/x\x01"),
            request("GET", "https", "a.example", "/x\x7f"),
            [
                request("GET", "https", "a.example", "/"),
                field("host", "b.example"),
            ]
            .concat(),
            no_authority("https"),
            no_authority("http"),
            no_authority("a1+-."),
            [no_authority("https"), field("host", "")].concat(),
            connect(""),
            connect("a.example"),
            connect("a.example:"),
            connect("a.example:4x3"),
            connect("::1:443"),
            connect("[::1:443"),
            connect("::1]:443"),
            connect("[::1]"),
            [connect("a.example:443"), field(":scheme", "https")].concat(),
            [connect("a.example:443"), field(":path", "/")].concat(),
        ] {
            assert_eq!(check(&bad), Err(Error::InvalidHeader));
        }

        for good in [
            request("GET", "https", "a.example", "/x?y"),
            request("OPTIONS", "https", "a.example", "*"),
            [
                request("GET", "https", "a.example", "/"),
                field("host", "a.example"),
            ]
            .concat(),
            [no_authority("https"), field("host", "a.example")].concat(),
            connect("a.example:443"),
            connect("[::1]:443"),
        ] {
            assert_eq!(check(&good), Ok(()));
        }
    }

    #[test]
    fn reject_invalid_request_target() {
        // RFC 3986 grammar (RFC 9114, Section 4.3.1; RFC 9110, Sections 4.2 and 9.3.6).
        for bad in [
            request("GET", "https", "a.example", "/\u{e9}"),
            request("GET", "https", "a.example", "/x?a#b"),
            request("GET", "https", "a.example", "/x#y"),
            request("GET", "https", "a.example", "#y"),
            request("GET", "https", "a.example", "/a b"),
            request("GET", "https", "a.example:abc", "/"),
            request("GET", "https", "a.example:65536", "/"),
            request("GET", "https", ":443", "/"),
            request("GET", "https", "a b", "/"),
            request("GET", "https", "a.example/", "/"),
            request("GET", "https", "a%zz", "/"),
            request("GET", "https", "\u{e9}.example", "/"),
            request("GET", "https", "::1", "/"),
            request("GET", "https", "[::g]", "/"),
            request("GET", "https", "[1.2.3.4]", "/"),
            request("GET", "https", "[::1]x", "/"),
            request("GET", "https", "[v1]", "/"),
            request("GET", "https", "[v.x]", "/"),
            request("GET", "https", "[vx.y]", "/"),
            request("GET", "https", "[v1.]", "/"),
            request("GET", "https", "[v1.%41]", "/"),
            request("GET", "https", "[v1F.a:b]", "/"),
            request("GET", "https", "[V1.x]", "/"),
            request("GET", "https", "a.example:+443", "/"),
            request("GET", "https", "a.example:0", "/"),
            request("GET", "https", "a.example:00", "/"),
            connect("a.example:0"),
            [no_authority("https"), field("host", "a.example:abc")].concat(),
            connect(":443"),
            connect("a.example:65536"),
            connect("a b:443"),
            connect("[v1]:443"),
            connect("[v1.x]:443"),
        ] {
            assert_eq!(check(&bad), Err(Error::InvalidHeader));
        }

        let vchars: String = (b'!'..=b'~')
            .filter(|&b| b != b'#')
            .map(char::from)
            .collect();
        for good in [
            request("GET", "https", "a.example", "/a-._~!$&'()*+,;=:@/%2F%aa"),
            request("GET", "https", "a.example", "/a//b/"),
            request("GET", "https", "a.example", "/x?a/b?c:@%20"),
            request("GET", "https", "a.example", "//x"),
            request("GET", "https", "a.example", "/a|b"),
            request("GET", "https", "a.example", "/a[b]"),
            request("GET", "https", "a.example", "/%"),
            request("GET", "https", "a.example", "/%2"),
            request("GET", "https", "a.example", "/%2g"),
            request("GET", "https", "a.example", "/%g2"),
            request("GET", "https", "a.example", "/x?a[b]"),
            request("GET", "https", "a.example", "/x?%zz"),
            request("GET", "https", "a.example", &format!("/{vchars}")),
            request("GET", "https", "a.example:", "/"),
            request("GET", "https", "a.example:0443", "/"),
            request("GET", "https", "a.example:65535", "/"),
            request("GET", "https", "a_b-c~!$&'()*+,;=%2E", "/"),
            request("GET", "https", "192.0.2.1:443", "/"),
            request("GET", "https", "[::1]", "/"),
            request("GET", "https", "[2001:db8::192.0.2.1]:443", "/"),
            [no_authority("https"), field("host", "a.example:443")].concat(),
            connect("a.example:65535"),
        ] {
            assert_eq!(check(&good), Ok(()));
        }
    }

    #[test]
    fn reject_unknown_pseudo_header() {
        let request = [
            request("GET", "https", "a.example", "/"),
            field(":foo", "x"),
        ]
        .concat();
        assert_eq!(check(&request), Err(Error::InvalidHeader));
        let response = [Header::new(":status", "200"), Header::new(":path", "/")];
        assert_eq!(
            headers_valid(&response, MessageType::Response),
            Err(Error::InvalidHeader)
        );
    }

    #[test]
    fn reject_connection_specific_header() {
        // A message carrying a connection-specific header field is malformed
        // (RFC 9114, Section 4.2), in either direction.
        for name in [
            "connection",
            "keep-alive",
            "proxy-connection",
            "transfer-encoding",
            "upgrade",
            "te",
        ] {
            let response = vec![Header::new(":status", "200"), Header::new(name, "x")];
            assert!(headers_valid(&response, MessageType::Response).is_err());

            let request = [get_request(), field(name, "x")].concat();
            assert_eq!(check(&request), Err(Error::InvalidHeader));
        }

        // TE is the sole exception, but only carrying the value "trailers".
        let ok = [get_request(), field("te", "trailers")].concat();
        assert_eq!(check(&ok), Ok(()));
    }

    #[test]
    fn regular_request_missing_method() {
        let headers = vec![
            Header::new(":scheme", "https"),
            Header::new(":authority", "something.com"),
            Header::new(":path", "/index.html"),
        ];
        assert!(headers_valid(&headers, MessageType::Request).is_err());
    }

    #[test]
    fn regular_request_missing_scheme() {
        let headers = vec![
            Header::new(":method", "GET"),
            Header::new(":path", "/index.html"),
        ];
        assert!(headers_valid(&headers, MessageType::Request).is_err());
    }

    #[test]
    fn regular_request_missing_path() {
        let headers = vec![
            Header::new(":method", "GET"),
            Header::new(":scheme", "https"),
            Header::new(":authority", "something.com"),
        ];
        assert!(headers_valid(&headers, MessageType::Request).is_err());
    }
}
