/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate url;
use super::*;
use address::{Address, AddressType};
use anonymizer::ToBytesVec;
use attribute_type::{SdpAttributeFmtp, SdpAttributeFmtpParameters, SdpAttributeRtpmap};
use std::net::IpAddr;
use std::net::Ipv4Addr;

fn create_dummy_sdp_session() -> SdpSession {
    let origin = parse_origin("mozilla 506705521068071134 0 IN IP4 0.0.0.0");
    assert!(origin.is_ok());
    let connection = parse_connection("IN IP4 198.51.100.7");
    assert!(connection.is_ok());
    let mut sdp_session;
    if let SdpType::Origin(o) = origin.unwrap() {
        sdp_session = SdpSession::new(0, o, "-".to_string());

        if let Ok(SdpType::Connection(c)) = connection {
            sdp_session.connection = Some(c);
        } else {
            unreachable!();
        }
    } else {
        unreachable!();
    }
    sdp_session
}

pub fn create_dummy_media_section() -> SdpMedia {
    let media_line = SdpMediaLine {
        media: SdpMediaValue::Audio,
        port: 9,
        port_count: 0,
        proto: SdpProtocolValue::RtpSavpf,
        formats: SdpFormatList::Integers(Vec::new()),
    };
    SdpMedia::new(media_line)
}

#[test]
fn test_session_works() -> Result<(), SdpParserInternalError> {
    parse_session("topic")?;
    Ok(())
}

#[test]
fn test_version_works() -> Result<(), SdpParserInternalError> {
    parse_version("0")?;
    Ok(())
}

#[test]
fn test_version_unsupported_input() {
    assert!(parse_version("1").is_err());
    assert!(parse_version("11").is_err());
    assert!(parse_version("a").is_err());
}

#[test]
fn test_origin_works() -> Result<(), SdpParserInternalError> {
    parse_origin("mozilla 506705521068071134 0 IN IP4 0.0.0.0")?;
    parse_origin("mozilla 506705521068071134 0 IN IP6 2001:db8::1")?;
    Ok(())
}

#[test]
fn test_origin_missing_username() {
    assert!(parse_origin("").is_err());
}

#[test]
fn test_origin_missing_session_id() {
    assert!(parse_origin("mozilla ").is_err());
}

#[test]
fn test_origin_missing_session_version() {
    assert!(parse_origin("mozilla 506705521068071134 ").is_err());
}

#[test]
fn test_origin_missing_nettype() {
    assert!(parse_origin("mozilla 506705521068071134 0 ").is_err());
}

#[test]
fn test_origin_unsupported_nettype() {
    assert!(parse_origin("mozilla 506705521068071134 0 UNSUPPORTED IP4 0.0.0.0").is_err());
}

#[test]
fn test_origin_missing_addtype() {
    assert!(parse_origin("mozilla 506705521068071134 0 IN ").is_err());
}

#[test]
fn test_origin_missing_ip_addr() {
    assert!(parse_origin("mozilla 506705521068071134 0 IN IP4 ").is_err());
}

#[test]
fn test_origin_unsupported_addrtpe() {
    assert!(parse_origin("mozilla 506705521068071134 0 IN IP1 0.0.0.0").is_err());
}

#[test]
fn test_origin_invalid_ip_addr() {
    assert!(parse_origin("mozilla 506705521068071134 0 IN IP4 1.1.1.256").is_err());
    assert!(parse_origin("mozilla 506705521068071134 0 IN IP6 ::g").is_err());
}

#[test]
fn test_origin_addr_type_mismatch() {
    assert!(parse_origin("mozilla 506705521068071134 0 IN IP4 ::1").is_err());
}

#[test]
fn connection_works() -> Result<(), SdpParserInternalError> {
    parse_connection("IN IP4 127.0.0.1")?;
    parse_connection("IN IP4 127.0.0.1/10/10")?;
    parse_connection("IN IP6 ::1")?;
    parse_connection("IN IP6 ::1/1/1")?;
    Ok(())
}

#[test]
fn connection_lots_of_whitespace() -> Result<(), SdpParserInternalError> {
    parse_connection("IN   IP4   127.0.0.1")?;
    Ok(())
}

#[test]
fn connection_wrong_amount_of_tokens() {
    assert!(parse_connection("IN IP4").is_err());
    assert!(parse_connection("IN IP4 0.0.0.0 foobar").is_err());
}

#[test]
fn connection_unsupported_nettype() {
    assert!(parse_connection("UNSUPPORTED IP4 0.0.0.0").is_err());
}

#[test]
fn connection_unsupported_addrtpe() {
    assert!(parse_connection("IN IP1 0.0.0.0").is_err());
}

#[test]
fn connection_broken_ip_addr() {
    assert!(parse_connection("IN IP4 1.1.1.256").is_err());
    assert!(parse_connection("IN IP6 ::g").is_err());
}

#[test]
fn connection_addr_type_mismatch() {
    assert!(parse_connection("IN IP4 ::1").is_err());
}

#[test]
fn bandwidth_works() -> Result<(), SdpParserInternalError> {
    parse_bandwidth("AS:1")?;
    parse_bandwidth("CT:123")?;
    parse_bandwidth("TIAS:12345")?;
    Ok(())
}

#[test]
fn bandwidth_wrong_amount_of_tokens() {
    assert!(parse_bandwidth("TIAS").is_err());
    assert!(parse_bandwidth("TIAS:12345:xyz").is_err());
}

#[test]
fn bandwidth_unsupported_type() -> Result<(), SdpParserInternalError> {
    parse_bandwidth("UNSUPPORTED:12345")?;
    Ok(())
}

#[test]
fn test_timing_works() -> Result<(), SdpParserInternalError> {
    parse_timing("0 0")?;
    Ok(())
}

#[test]
fn test_timing_non_numeric_tokens() {
    assert!(parse_timing("a 0").is_err());
    assert!(parse_timing("0 a").is_err());
}

#[test]
fn test_timing_wrong_amount_of_tokens() {
    assert!(parse_timing("0").is_err());
    assert!(parse_timing("0 0 0").is_err());
}

#[test]
fn test_parse_sdp_line_works() -> Result<(), SdpParserError> {
    parse_sdp_line("v=0", 0)?;
    parse_sdp_line("s=somesession", 0)?;
    Ok(())
}

#[test]
fn test_parse_sdp_line_empty_line() {
    assert!(parse_sdp_line("", 0).is_err());
}

#[test]
fn test_parse_sdp_line_unsupported_types() {
    assert!(parse_sdp_line("e=foobar", 0).is_err());
    assert!(parse_sdp_line("i=foobar", 0).is_err());
    assert!(parse_sdp_line("k=foobar", 0).is_err());
    assert!(parse_sdp_line("p=foobar", 0).is_err());
    assert!(parse_sdp_line("r=foobar", 0).is_err());
    assert!(parse_sdp_line("u=foobar", 0).is_err());
    assert!(parse_sdp_line("z=foobar", 0).is_err());
}

#[test]
fn test_parse_sdp_line_unknown_key() {
    assert!(parse_sdp_line("y=foobar", 0).is_err());
}

#[test]
fn test_parse_sdp_line_too_long_type() {
    assert!(parse_sdp_line("ab=foobar", 0).is_err());
}

#[test]
fn test_parse_sdp_line_without_equal() {
    assert!(parse_sdp_line("abcd", 0).is_err());
    assert!(parse_sdp_line("ab cd", 0).is_err());
}

#[test]
fn test_parse_sdp_line_empty_value() {
    assert!(parse_sdp_line("v=", 0).is_err());
    assert!(parse_sdp_line("o=", 0).is_err());
}

#[test]
fn test_parse_sdp_line_empty_name() {
    assert!(parse_sdp_line("=abc", 0).is_err());
}

#[test]
fn test_parse_sdp_line_valid_a_line() -> Result<(), SdpParserError> {
    parse_sdp_line("a=rtpmap:8 PCMA/8000", 0)?;
    Ok(())
}

#[test]
fn test_parse_sdp_line_invalid_a_line() {
    assert!(parse_sdp_line("a=rtpmap:200 PCMA/8000", 0).is_err());
}

#[test]
fn test_add_attribute() -> Result<(), SdpParserInternalError> {
    let mut sdp_session = create_dummy_sdp_session();

    sdp_session.add_attribute(SdpAttribute::Sendrecv)?;
    assert!(sdp_session.add_attribute(SdpAttribute::BundleOnly).is_err());
    assert_eq!(sdp_session.attribute.len(), 1);
    Ok(())
}

#[test]
fn test_sanity_check_sdp_session_timing() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    sdp_session.extend_media(vec![create_dummy_media_section()]);

    assert!(sanity_check_sdp_session(&sdp_session).is_err());

    let t = SdpTiming { start: 0, stop: 0 };
    sdp_session.set_timing(t);

    sanity_check_sdp_session(&sdp_session)?;
    Ok(())
}

#[test]
fn test_sanity_check_sdp_session_media() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    let t = SdpTiming { start: 0, stop: 0 };
    sdp_session.set_timing(t);

    sanity_check_sdp_session(&sdp_session)?;

    sdp_session.extend_media(vec![create_dummy_media_section()]);

    sanity_check_sdp_session(&sdp_session)?;
    Ok(())
}

#[test]
fn test_sanity_check_sdp_connection() -> Result<(), SdpParserInternalError> {
    let origin = parse_origin("mozilla 506705521068071134 0 IN IP4 0.0.0.0")?;
    let mut sdp_session;
    if let SdpType::Origin(o) = origin {
        sdp_session = SdpSession::new(0, o, "-".to_string());
    } else {
        unreachable!();
    }
    let t = SdpTiming { start: 0, stop: 0 };
    sdp_session.set_timing(t);

    assert!(sanity_check_sdp_session(&sdp_session).is_ok());

    // the dummy media section doesn't contain a connection
    sdp_session.extend_media(vec![create_dummy_media_section()]);

    assert!(sanity_check_sdp_session(&sdp_session).is_err());

    let connection = parse_connection("IN IP6 ::1")?;
    if let SdpType::Connection(c) = connection {
        sdp_session.connection = Some(c);
    } else {
        unreachable!();
    }

    assert!(sanity_check_sdp_session(&sdp_session).is_ok());

    let mut second_media = create_dummy_media_section();
    let mconnection = parse_connection("IN IP4 0.0.0.0")?;
    if let SdpType::Connection(c) = mconnection {
        second_media.set_connection(c);
    } else {
        unreachable!();
    }
    sdp_session.extend_media(vec![second_media]);
    assert!(sdp_session.media.len() == 2);

    assert!(sanity_check_sdp_session(&sdp_session).is_ok());
    Ok(())
}

#[test]
fn test_sanity_check_sdp_session_extmap() -> Result<(), SdpParserInternalError> {
    let mut sdp_session = create_dummy_sdp_session();
    let t = SdpTiming { start: 0, stop: 0 };
    sdp_session.set_timing(t);
    sdp_session.extend_media(vec![create_dummy_media_section()]);

    let attribute =
        parse_attribute("extmap:3 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time")?;
    if let SdpType::Attribute(a) = attribute {
        sdp_session.add_attribute(a)?;
    } else {
        unreachable!();
    }
    assert!(sdp_session
        .get_attribute(SdpAttributeType::Extmap)
        .is_some());

    assert!(sanity_check_sdp_session(&sdp_session).is_ok());

    let mut second_media = create_dummy_media_section();
    let mattribute =
        parse_attribute("extmap:1/sendonly urn:ietf:params:rtp-hdrext:ssrc-audio-level")?;
    if let SdpType::Attribute(ma) = mattribute {
        second_media.add_attribute(ma)?;
    } else {
        unreachable!();
    }
    assert!(second_media
        .get_attribute(SdpAttributeType::Extmap)
        .is_some());

    sdp_session.extend_media(vec![second_media]);
    assert!(sdp_session.media.len() == 2);

    assert!(sanity_check_sdp_session(&sdp_session).is_err());

    sdp_session.attribute = Vec::new();

    assert!(sanity_check_sdp_session(&sdp_session).is_ok());
    Ok(())
}

#[test]
fn test_sanity_check_sdp_session_simulcast() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    let t = SdpTiming { start: 0, stop: 0 };
    sdp_session.set_timing(t);
    sdp_session.extend_media(vec![create_dummy_media_section()]);

    sanity_check_sdp_session(&sdp_session)?;
    Ok(())
}

#[test]
fn test_parse_sdp_zero_length_string_fails() {
    assert!(parse_sdp("", true).is_err());
}

#[test]
fn test_parse_sdp_to_short_string() {
    assert!(parse_sdp("fooooobarrrr", true).is_err());
}

#[test]
fn test_parse_sdp_minimal_sdp_successfully() -> Result<(), SdpParserError> {
    parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP6 ::1\r\n
s=-\r\n
c=IN IP6 ::1\r\n
t=0 0\r\n",
        true,
    )?;
    Ok(())
}

#[test]
fn test_parse_sdp_too_short() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_line_error() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n
t=0 foobar\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_unsupported_error() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n
t=0 0\r\n
m=foobar 0 UDP/TLS/RTP/SAVPF 0\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_unsupported_warning() -> Result<(), SdpParserError> {
    parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n
c=IN IP4 198.51.100.7\r\n
t=0 0\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n
a=unsupported\r\n",
        false,
    )?;
    Ok(())
}

#[test]
fn test_parse_sdp_sequence_error() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n
t=0 0\r\n
a=bundle-only\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_integer_error() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n
t=0 0\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n
a=rtcp:34er21\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_ipaddr_error() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.a.b.0\r\n
s=-\r\n
t=0 0\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_invalid_session_attribute() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.a.b.0\r\n
s=-\r\n
t=0 0\r\n
a=bundle-only\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_parse_sdp_invalid_media_attribute() {
    assert!(parse_sdp(
        "v=0\r\n
o=- 0 0 IN IP4 0.a.b.0\r\n
s=-\r\n
t=0 0\r\n
m=audio 0 UDP/TLS/RTP/SAVPF 0\r\n
a=ice-lite\r\n",
        true
    )
    .is_err());
}

#[test]
fn test_mask_origin() {
    let mut anon = StatefulSdpAnonymizer::new();
    if let SdpType::Origin(origin_1) =
        parse_origin("mozilla 506705521068071134 0 IN IP4 0.0.0.0").unwrap()
    {
        for _ in 0..2 {
            let masked = origin_1.masked_clone(&mut anon);
            assert_eq!(masked.username, "origin-user-00000001");
            assert_eq!(
                masked.unicast_addr,
                ExplicitlyTypedAddress::Ip(IpAddr::V4(Ipv4Addr::from(1)))
            );
        }
    } else {
        unreachable!();
    }
}

#[test]
fn test_mask_sdp() {
    let mut anon = StatefulSdpAnonymizer::new();
    let sdp = parse_sdp(
        "v=0\r\n
        o=ausername 4294967296 2 IN IP4 127.0.0.1\r\n
        s=SIP Call\r\n
        c=IN IP4 198.51.100.7/51\r\n
        a=ice-pwd:12340\r\n
        a=ice-ufrag:4a799b2e\r\n
        a=fingerprint:sha-1 CD:34:D1:62:16:95:7B:B7:EB:74:E2:39:27:97:EB:0B:23:73:AC:BC\r\n
        t=0 0\r\n
        m=video 56436 RTP/SAVPF 120\r\n
        a=candidate:77142221 1 udp 2113937151 192.168.137.1 54081 typ host\r\n
        a=remote-candidates:0 10.0.0.1 5555\r\n
        a=rtpmap:120 VP8/90000\r\n",
        true,
    )
    .unwrap();
    let mut masked = sdp.masked_clone(&mut anon);
    assert_eq!(masked.origin.username, "origin-user-00000001");
    assert_eq!(
        masked.origin.unicast_addr,
        ExplicitlyTypedAddress::Ip(IpAddr::V4(Ipv4Addr::from(1)))
    );
    assert_eq!(
        masked.connection.unwrap().address,
        ExplicitlyTypedAddress::Ip(IpAddr::V4(Ipv4Addr::from(2)))
    );
    let mut attributes = masked.attribute;
    for m in &mut masked.media {
        for attribute in m.get_attributes() {
            attributes.push(attribute.clone());
        }
    }
    for attribute in attributes {
        match attribute {
            SdpAttribute::Candidate(c) => {
                assert_eq!(c.address, Address::Ip(IpAddr::V4(Ipv4Addr::from(3))));
                assert_eq!(c.port, 1);
            }
            SdpAttribute::Fingerprint(f) => {
                assert_eq!(f.fingerprint, 1u64.to_byte_vec());
            }
            SdpAttribute::IcePwd(p) => {
                assert_eq!(p, "ice-password-00000001");
            }
            SdpAttribute::IceUfrag(u) => {
                assert_eq!(u, "ice-user-00000001");
            }
            SdpAttribute::RemoteCandidate(r) => {
                assert_eq!(r.address, Address::Ip(IpAddr::V4(Ipv4Addr::from(4))));
                assert_eq!(r.port, 2);
            }
            _ => {}
        }
    }
}

#[test]
fn test_parse_session_vector() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("a=sendrecv", 1)?];
    sdp_session.parse_session_vector(&mut lines)?;
    assert_eq!(sdp_session.attribute.len(), 1);
    Ok(())
}

#[test]
fn test_parse_session_vector_non_session_attribute() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("a=bundle-only", 2)?];
    assert!(sdp_session.parse_session_vector(&mut lines).is_err());
    assert_eq!(sdp_session.attribute.len(), 0);
    Ok(())
}

#[test]
fn test_parse_session_vector_version_repeated() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 3)?];
    assert!(sdp_session.parse_session_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_parse_session_vector_contains_media_type() -> Result<(), SdpParserError> {
    let mut sdp_session = create_dummy_sdp_session();
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("m=audio 0 UDP/TLS/RTP/SAVPF 0", 4)?];
    assert!(sdp_session.parse_session_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_no_media_section() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 1)?];
    lines.push(parse_sdp_line(
        "o=ausername 4294967296 2 IN IP4 127.0.0.1",
        1,
    )?);
    lines.push(parse_sdp_line("s=SIP Call", 1)?);
    lines.push(parse_sdp_line("t=0 0", 1)?);
    lines.push(parse_sdp_line("c=IN IP6 ::1", 1)?);
    assert!(parse_sdp_vector(&mut lines).is_ok());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_with_media_section() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 1)?];
    lines.push(parse_sdp_line(
        "o=ausername 4294967296 2 IN IP4 127.0.0.1",
        1,
    )?);
    lines.push(parse_sdp_line("s=SIP Call", 1)?);
    lines.push(parse_sdp_line("t=0 0", 1)?);
    lines.push(parse_sdp_line("m=video 56436 RTP/SAVPF 120", 1)?);
    lines.push(parse_sdp_line("c=IN IP6 ::1", 1)?);
    assert!(parse_sdp_vector(&mut lines).is_ok());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_with_missing_rtcp_mux() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 1)?];
    lines.push(parse_sdp_line(
        "o=ausername 4294967296 2 IN IP4 127.0.0.1",
        1,
    )?);
    lines.push(parse_sdp_line("s=SIP Call", 1)?);
    lines.push(parse_sdp_line("t=0 0", 1)?);
    lines.push(parse_sdp_line("m=video 56436 RTP/SAVPF 120", 1)?);
    lines.push(parse_sdp_line("c=IN IP6 ::1", 1)?);
    lines.push(parse_sdp_line("a=rtcp-mux-only", 1)?);
    assert!(parse_sdp_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_too_short() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 1)?];
    assert!(parse_sdp_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_missing_version() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line(
        "o=ausername 4294967296 2 IN IP4 127.0.0.1",
        1,
    )?];
    for _ in 0..3 {
        lines.push(parse_sdp_line("a=sendrecv", 1)?);
    }
    assert!(parse_sdp_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_missing_origin() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 1)?];
    for _ in 0..3 {
        lines.push(parse_sdp_line("a=sendrecv", 1)?);
    }
    assert!(parse_sdp_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_parse_sdp_vector_missing_session() -> Result<(), SdpParserError> {
    let mut lines: Vec<SdpLine> = vec![parse_sdp_line("v=0", 1)?];
    lines.push(parse_sdp_line(
        "o=ausername 4294967296 2 IN IP4 127.0.0.1",
        1,
    )?);
    for _ in 0..2 {
        lines.push(parse_sdp_line("a=sendrecv", 1)?);
    }
    assert!(parse_sdp_vector(&mut lines).is_err());
    Ok(())
}

#[test]
fn test_session_add_media_works() {
    let mut sdp_session = create_dummy_sdp_session();
    assert!(sdp_session
        .add_media(
            SdpMediaValue::Audio,
            SdpAttribute::Sendrecv,
            99,
            SdpProtocolValue::RtpSavpf,
            ExplicitlyTypedAddress::from(Ipv4Addr::new(127, 0, 0, 1))
        )
        .is_ok());
    assert!(sdp_session.get_connection().is_some());
    assert_eq!(sdp_session.attribute.len(), 0);
    assert_eq!(sdp_session.media.len(), 1);
    assert_eq!(sdp_session.media[0].get_attributes().len(), 1);
    assert!(sdp_session.media[0]
        .get_attribute(SdpAttributeType::Sendrecv)
        .is_some());
}

#[test]
fn test_session_add_media_invalid_attribute_fails() -> Result<(), SdpParserInternalError> {
    let mut sdp_session = create_dummy_sdp_session();
    assert!(sdp_session
        .add_media(
            SdpMediaValue::Audio,
            SdpAttribute::IceLite,
            99,
            SdpProtocolValue::RtpSavpf,
            ExplicitlyTypedAddress::try_from((AddressType::IpV4, "127.0.0.1"))?
        )
        .is_err());
    Ok(())
}

#[test]
fn test_default_fmtp_should_be_empty() -> Result<(), SdpParserError> {
    let media_line = SdpMediaLine {
        media: SdpMediaValue::Audio,
        port: 9,
        port_count: 0,
        proto: SdpProtocolValue::RtpSavpf,
        formats: SdpFormatList::Integers(Vec::new()),
    };
    let mut msection = SdpMedia::new(media_line);
    msection
        .add_codec(SdpAttributeRtpmap::new(96, "boofar".to_string(), 1001))
        .unwrap();

    assert!(msection
        .add_attribute(SdpAttribute::Fmtp(SdpAttributeFmtp {
            payload_type: 96,
            parameters: SdpAttributeFmtpParameters {
                packetization_mode: 0,
                level_asymmetry_allowed: false,
                profile_level_id: 0x0042_0010,
                max_fs: 0,
                max_cpb: 0,
                max_dpb: 0,
                max_br: 0,
                max_mbps: 0,
                usedtx: false,
                stereo: false,
                useinbandfec: false,
                cbr: false,
                max_fr: 0,
                profile: None,
                level_idx: None,
                tier: None,
                maxplaybackrate: 48000,
                maxaveragebitrate: 0,
                ptime: 0,
                minptime: 0,
                maxptime: 0,
                encodings: Vec::new(),
                dtmf_tones: "".to_string(),
                rtx: None,
                unknown_tokens: Vec::new()
            }
        }))
        .is_ok());

    let sdpstring = "v=0\r\n
o=- 0 0 IN IP4 0.0.0.0\r\n
s=-\r\n
t=0 0\r\n"
        .to_owned()
        + &msection.to_string()
        + "\r\nc=IN IP4 0.0.0.0";

    assert!(msection.get_attribute(SdpAttributeType::Rtpmap).is_some());
    assert!(msection.get_attribute(SdpAttributeType::Fmtp).is_some());

    parse_sdp(&sdpstring, true).map(|_| ())
}
