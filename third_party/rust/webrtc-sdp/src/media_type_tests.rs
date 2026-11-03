/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;
use address::{AddressType, ExplicitlyTypedAddress};
use attribute_type::{
    SdpAttributeFmtp, SdpAttributeFmtpParameters, SdpAttributePayloadType, SdpAttributeRtcpFb,
    SdpAttributeRtcpFbType,
};
use std::convert::TryFrom;

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

// TODO is this useful outside of tests?
impl SdpFormatList {
    fn len(&self) -> Option<usize> {
        match *self {
            SdpFormatList::Integers(ref x) => Some(x.len()),
            SdpFormatList::Strings(ref x) => Some(x.len()),
            SdpFormatList::Unknown(_) => None,
        }
    }
}

pub fn add_dummy_attributes(media: &mut SdpMedia) {
    assert!(media
        .add_attribute(SdpAttribute::Rtcpfb(SdpAttributeRtcpFb {
            payload_type: SdpAttributePayloadType::Wildcard,
            feedback_type: SdpAttributeRtcpFbType::Ack,
            parameter: "".to_string(),
            extra: "".to_string(),
        },))
        .is_ok());
    assert!(media
        .add_attribute(SdpAttribute::Fmtp(SdpAttributeFmtp {
            payload_type: 1,
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
        },))
        .is_ok());
    assert!(media
        .add_attribute(SdpAttribute::Sctpmap(SdpAttributeSctpmap {
            port: 5000,
            channels: 2,
        }))
        .is_ok());
    assert!(media.add_attribute(SdpAttribute::BundleOnly).is_ok());
    assert!(media.add_attribute(SdpAttribute::SctpPort(5000)).is_ok());

    assert!(media.get_attribute(SdpAttributeType::Rtpmap).is_some());
    assert!(media.get_attribute(SdpAttributeType::Rtcpfb).is_some());
    assert!(media.get_attribute(SdpAttributeType::Fmtp).is_some());
    assert!(media.get_attribute(SdpAttributeType::Sctpmap).is_some());
    assert!(media.get_attribute(SdpAttributeType::SctpPort).is_some());
    assert!(media.get_attribute(SdpAttributeType::BundleOnly).is_some());
}

fn check_parse(media_line_str: &str) -> SdpMediaLine {
    if let Ok(SdpType::Media(media_line)) = parse_media(media_line_str) {
        media_line
    } else {
        unreachable!()
    }
}

fn check_parse_and_serialize(media_line_str: &str) {
    let parsed = check_parse(media_line_str);
    assert_eq!(parsed.to_string(), media_line_str.to_string());
}

#[test]
fn test_get_set_port() {
    let mut msection = create_dummy_media_section();
    assert_eq!(msection.get_port(), 9);
    msection.set_port(2048);
    assert_eq!(msection.get_port(), 2048);
}

#[test]
fn test_add_codec() -> Result<(), SdpParserInternalError> {
    let mut msection = create_dummy_media_section();
    msection.add_codec(SdpAttributeRtpmap::new(96, "foobar".to_string(), 1000))?;
    assert_eq!(msection.get_formats().len().unwrap(), 1);
    assert!(msection.get_attribute(SdpAttributeType::Rtpmap).is_some());

    let mut msection = create_dummy_media_section();
    msection.media.formats = SdpFormatList::Strings(Vec::new());
    msection.add_codec(SdpAttributeRtpmap::new(97, "boofar".to_string(), 1001))?;
    assert_eq!(msection.get_formats().len().unwrap(), 1);
    assert!(msection.get_attribute(SdpAttributeType::Rtpmap).is_some());
    Ok(())
}

#[test]
fn test_remove_codecs() -> Result<(), SdpParserInternalError> {
    let mut msection = create_dummy_media_section();
    msection.add_codec(SdpAttributeRtpmap::new(96, "foobar".to_string(), 1000))?;
    assert_eq!(msection.get_formats().len().unwrap(), 1);
    assert!(msection.get_attribute(SdpAttributeType::Rtpmap).is_some());
    msection.remove_codecs();
    assert_eq!(msection.get_formats().len().unwrap(), 0);
    assert!(msection.get_attribute(SdpAttributeType::Rtpmap).is_none());

    let mut msection = create_dummy_media_section();
    msection.media.formats = SdpFormatList::Strings(Vec::new());
    msection.add_codec(SdpAttributeRtpmap::new(97, "boofar".to_string(), 1001))?;
    assert_eq!(msection.get_formats().len().unwrap(), 1);

    add_dummy_attributes(&mut msection);

    msection.remove_codecs();
    assert_eq!(msection.get_formats().len().unwrap(), 0);
    assert!(msection.get_attribute(SdpAttributeType::Rtpmap).is_none());
    assert!(msection.get_attribute(SdpAttributeType::Rtcpfb).is_none());
    assert!(msection.get_attribute(SdpAttributeType::Fmtp).is_none());
    assert!(msection.get_attribute(SdpAttributeType::Sctpmap).is_none());
    assert!(msection.get_attribute(SdpAttributeType::SctpPort).is_none());
    Ok(())
}

#[test]
fn test_add_datachannel() -> Result<(), SdpParserInternalError> {
    let mut msection = create_dummy_media_section();
    msection.add_datachannel("foo".to_string(), 5000, 256, 0)?;
    assert_eq!(*msection.get_type(), SdpMediaValue::Application);
    assert!(msection.get_attribute(SdpAttributeType::SctpPort).is_none());
    assert!(msection
        .get_attribute(SdpAttributeType::MaxMessageSize)
        .is_none());
    assert!(msection.get_attribute(SdpAttributeType::Sctpmap).is_some());
    match *msection.get_attribute(SdpAttributeType::Sctpmap).unwrap() {
        SdpAttribute::Sctpmap(ref s) => {
            assert_eq!(s.port, 5000);
            assert_eq!(s.channels, 256);
        }
        _ => unreachable!(),
    }

    let mut msection = create_dummy_media_section();
    msection.add_datachannel("foo".to_string(), 5000, 256, 1234)?;
    assert_eq!(*msection.get_type(), SdpMediaValue::Application);
    assert!(msection.get_attribute(SdpAttributeType::SctpPort).is_none());
    assert!(msection
        .get_attribute(SdpAttributeType::MaxMessageSize)
        .is_some());
    match *msection
        .get_attribute(SdpAttributeType::MaxMessageSize)
        .unwrap()
    {
        SdpAttribute::MaxMessageSize(m) => {
            assert_eq!(m, 1234);
        }
        _ => unreachable!(),
    }

    let mut msection = create_dummy_media_section();
    msection.media.proto = SdpProtocolValue::UdpDtlsSctp;
    msection.add_datachannel("foo".to_string(), 5000, 256, 5678)?;
    assert_eq!(*msection.get_type(), SdpMediaValue::Application);
    assert!(msection.get_attribute(SdpAttributeType::Sctpmap).is_none());
    assert!(msection.get_attribute(SdpAttributeType::SctpPort).is_some());
    match *msection.get_attribute(SdpAttributeType::SctpPort).unwrap() {
        SdpAttribute::SctpPort(s) => {
            assert_eq!(s, 5000);
        }
        _ => unreachable!(),
    }
    assert!(msection
        .get_attribute(SdpAttributeType::MaxMessageSize)
        .is_some());
    match *msection
        .get_attribute(SdpAttributeType::MaxMessageSize)
        .unwrap()
    {
        SdpAttribute::MaxMessageSize(m) => {
            assert_eq!(m, 5678);
        }
        _ => unreachable!(),
    }
    Ok(())
}

#[test]
fn test_parse_media_token() -> Result<(), SdpParserInternalError> {
    let audio = parse_media_token("audio")?;
    assert_eq!(audio, SdpMediaValue::Audio);
    let video = parse_media_token("VIDEO")?;
    assert_eq!(video, SdpMediaValue::Video);
    let app = parse_media_token("aPplIcatIOn")?;
    assert_eq!(app, SdpMediaValue::Application);
    assert!(parse_media_token("").is_err());
    let unknown = parse_media_token("foobar")?;
    assert_eq!(unknown, SdpMediaValue::Unknown("foobar".to_string()));
    Ok(())
}

#[test]
fn test_parse_protocol_rtp_token() -> Result<(), SdpParserInternalError> {
    fn parse_and_serialize_protocol_token(
        token: &str,
        result: SdpProtocolValue,
    ) -> Result<(), SdpParserInternalError> {
        let rtps = parse_protocol_token(token)?;
        assert_eq!(rtps, result);
        assert_eq!(rtps.to_string(), token.to_uppercase());
        Ok(())
    }
    parse_and_serialize_protocol_token("rtp/avp", SdpProtocolValue::RtpAvp)?;
    parse_and_serialize_protocol_token("rtp/avpf", SdpProtocolValue::RtpAvpf)?;
    parse_and_serialize_protocol_token("rtp/savp", SdpProtocolValue::RtpSavp)?;
    parse_and_serialize_protocol_token("rtp/savpf", SdpProtocolValue::RtpSavpf)?;
    parse_and_serialize_protocol_token("udp/tls/rtp/savp", SdpProtocolValue::UdpTlsRtpSavp)?;
    parse_and_serialize_protocol_token("udp/tls/rtp/savpf", SdpProtocolValue::UdpTlsRtpSavpf)?;
    parse_and_serialize_protocol_token("TCP/dtls/rtp/savp", SdpProtocolValue::TcpDtlsRtpSavp)?;
    parse_and_serialize_protocol_token("tcp/DTLS/rtp/savpf", SdpProtocolValue::TcpDtlsRtpSavpf)?;

    assert!(parse_protocol_token("").is_err());
    assert!(parse_protocol_token("foobar").is_err());
    Ok(())
}

#[test]
fn test_parse_protocol_sctp_token() -> Result<(), SdpParserInternalError> {
    fn parse_and_serialize_protocol_token(
        token: &str,
        result: SdpProtocolValue,
    ) -> Result<(), SdpParserInternalError> {
        let rtps = parse_protocol_token(token)?;
        assert_eq!(rtps, result);
        assert_eq!(rtps.to_string(), token.to_uppercase());
        Ok(())
    }
    parse_and_serialize_protocol_token("dtLs/ScTP", SdpProtocolValue::DtlsSctp)?;
    parse_and_serialize_protocol_token("udp/DTLS/sctp", SdpProtocolValue::UdpDtlsSctp)?;
    parse_and_serialize_protocol_token("tcp/dtls/SCTP", SdpProtocolValue::TcpDtlsSctp)?;
    Ok(())
}

#[test]
fn test_media_works() {
    check_parse_and_serialize("audio 9 UDP/TLS/RTP/SAVPF 109");
    check_parse_and_serialize("video 9 UDP/TLS/RTP/SAVPF 126");
    check_parse_and_serialize("application 9 DTLS/SCTP 5000");
    check_parse_and_serialize("application 9 UDP/DTLS/SCTP webrtc-datachannel");

    check_parse_and_serialize("audio 9 UDP/TLS/RTP/SAVPF 109 9 0 8");
    check_parse_and_serialize("audio 0 UDP/TLS/RTP/SAVPF 8");
    check_parse_and_serialize("audio 9/2 UDP/TLS/RTP/SAVPF 8");
}

#[test]
fn test_media_missing_token() {
    assert!(parse_media("video 9 UDP/TLS/RTP/SAVPF").is_err());
}

#[test]
fn test_media_invalid_port_number() {
    assert!(parse_media("video 75123 UDP/TLS/RTP/SAVPF 8").is_err());
}

#[test]
fn test_media_unknown_type() {
    check_parse_and_serialize("unknownFormat 9 UDP/TLS/RTP/SAVPF");
    let media = parse_media("unknownFormat 9 UDP/TLS/RTP/SAVPF")
        .expect("Unknown media type should be parsed");
    assert!(matches!(media, SdpType::Media(_)));
    if let SdpType::Media(media) = media {
        let expected = SdpMediaValue::Unknown("unknownFormat".to_string());
        assert_eq!(media.media, expected);
        assert_eq!(media.port, 9);
        assert_eq!(media.port_count, 0);
        assert_eq!(media.proto, SdpProtocolValue::UdpTlsRtpSavpf);
        assert!(matches!(media.formats, SdpFormatList::Unknown(_)));
        if let SdpFormatList::Unknown(formats) = media.formats {
            assert_eq!(formats, "".to_string());
        }
    }
}

#[test]
fn test_media_unknown_type_with_fmt() {
    check_parse_and_serialize("unknownFormat 9 UDP/TLS/RTP/SAVPF 8A J");
    let media = parse_media("unknownFormat 9 UDP/TLS/RTP/SAVPF 8A J")
        .expect("Unknown media type should be parsed");
    assert!(matches!(media, SdpType::Media(_)));
    if let SdpType::Media(media) = media {
        let expected = SdpMediaValue::Unknown("unknownFormat".to_string());
        assert_eq!(media.media, expected);
        assert_eq!(media.port, 9);
        assert_eq!(media.port_count, 0);
        assert_eq!(media.proto, SdpProtocolValue::UdpTlsRtpSavpf);
        assert!(matches!(media.formats, SdpFormatList::Unknown(_)));
        if let SdpFormatList::Unknown(formats) = media.formats {
            assert_eq!(formats, "8A J".to_string());
        }
    }
}

#[test]
fn test_media_invalid_port() {
    assert!(parse_media("audio / UDP/TLS/RTP/SAVPF 8").is_err());
}

#[test]
fn test_media_invalid_transport() {
    assert!(parse_media("audio 9 invalid/invalid 8").is_err());
}

#[cfg(feature = "pedantic")]
#[test]
fn test_media_invalid_payload() {
    assert!(parse_media("audio 9 UDP/TLS/RTP/SAVPF 300").is_err());
}

#[test]
fn test_media_vector_first_line_failure() {
    let mut sdp_lines: Vec<SdpLine> = Vec::new();
    let line = SdpLine {
        line_number: 0,
        sdp_type: SdpType::Session("hello".to_string()),
        text: "".to_owned(),
    };
    sdp_lines.push(line);
    assert!(parse_media_vector(&mut sdp_lines).is_err());
}

#[test]
fn test_media_vector_multiple_information() {
    let mut sdp_lines: Vec<SdpLine> = Vec::new();
    let media_line = SdpMediaLine {
        media: SdpMediaValue::Audio,
        port: 9,
        port_count: 0,
        proto: SdpProtocolValue::RtpSavpf,
        formats: SdpFormatList::Integers(Vec::new()),
    };
    let media = SdpLine {
        line_number: 0,
        sdp_type: SdpType::Media(media_line),
        text: "".to_owned(),
    };
    sdp_lines.push(media);
    let i1 = SdpLine {
        line_number: 1,
        sdp_type: SdpType::Information("foo-information-1".to_owned()),
        text: "foo-information-1".to_owned(),
    };
    let i2 = SdpLine {
        line_number: 2,
        sdp_type: SdpType::Information("foo-information-2".to_owned()),
        text: "foo-information-2".to_owned(),
    };
    sdp_lines.push(i1);
    sdp_lines.push(i2);
    assert!(parse_media_vector(&mut sdp_lines).is_err());
}

#[test]
fn test_media_vector_multiple_connections() {
    let mut sdp_lines: Vec<SdpLine> = Vec::new();
    let media_line = SdpMediaLine {
        media: SdpMediaValue::Audio,
        port: 9,
        port_count: 0,
        proto: SdpProtocolValue::RtpSavpf,
        formats: SdpFormatList::Integers(Vec::new()),
    };
    let media = SdpLine {
        line_number: 0,
        sdp_type: SdpType::Media(media_line),
        text: "".to_owned(),
    };
    sdp_lines.push(media);
    let c = SdpConnection {
        address: ExplicitlyTypedAddress::try_from((AddressType::IpV4, "127.0.0.1")).unwrap(),
        ttl: None,
        amount: None,
    };
    let c1 = SdpLine {
        line_number: 1,
        sdp_type: SdpType::Connection(c.clone()),
        text: "".to_owned(),
    };
    sdp_lines.push(c1);
    let c2 = SdpLine {
        line_number: 2,
        sdp_type: SdpType::Connection(c),
        text: "".to_owned(),
    };
    sdp_lines.push(c2);
    assert!(parse_media_vector(&mut sdp_lines).is_err());
}

#[test]
fn test_media_vector_invalid_types() {
    let mut sdp_lines: Vec<SdpLine> = Vec::new();
    let media_line = SdpMediaLine {
        media: SdpMediaValue::Audio,
        port: 9,
        port_count: 0,
        proto: SdpProtocolValue::RtpSavpf,
        formats: SdpFormatList::Integers(Vec::new()),
    };
    let media = SdpLine {
        line_number: 0,
        sdp_type: SdpType::Media(media_line),
        text: "".to_owned(),
    };
    sdp_lines.push(media);
    use SdpTiming;
    let t = SdpTiming { start: 0, stop: 0 };
    let tline = SdpLine {
        line_number: 1,
        sdp_type: SdpType::Timing(t),
        text: "".to_owned(),
    };
    sdp_lines.push(tline);
    assert!(parse_media_vector(&mut sdp_lines).is_err());
}

#[test]
fn test_media_vector_invalid_media_level_attribute() {
    let mut sdp_lines: Vec<SdpLine> = Vec::new();
    let media_line = SdpMediaLine {
        media: SdpMediaValue::Audio,
        port: 9,
        port_count: 0,
        proto: SdpProtocolValue::RtpSavpf,
        formats: SdpFormatList::Integers(Vec::new()),
    };
    let media = SdpLine {
        line_number: 0,
        sdp_type: SdpType::Media(media_line),
        text: "".to_owned(),
    };
    sdp_lines.push(media);
    let a = SdpAttribute::IceLite;
    let aline = SdpLine {
        line_number: 1,
        sdp_type: SdpType::Attribute(a),
        text: "".to_owned(),
    };
    sdp_lines.push(aline);
    assert!(parse_media_vector(&mut sdp_lines).is_err());
}
