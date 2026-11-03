/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use anonymizer::{AnonymizingClone, StatefulSdpAnonymizer};
use attribute_type::{
    maybe_print_param, SdpAttribute, SdpAttributeRtpmap, SdpAttributeSctpmap, SdpAttributeType,
};
use error::{SdpParserError, SdpParserInternalError};
use std::fmt;
use {SdpBandwidth, SdpConnection, SdpLine, SdpType};

/*
 * RFC4566
 * media-field =         %x6d "=" media SP port ["/" integer]
 *                       SP proto 1*(SP fmt) CRLF
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpMediaLine {
    pub media: SdpMediaValue,
    pub port: u32,
    pub port_count: u32,
    pub proto: SdpProtocolValue,
    pub formats: SdpFormatList,
}

impl fmt::Display for SdpMediaLine {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{media} {port}{pcount} {proto}{fmt_space}{formats}",
            media = self.media,
            port = self.port,
            pcount = maybe_print_param("/", self.port_count, 0),
            proto = self.proto,
            // We need to avoid emitting a trailing space if an unknown media
            // type is used and the format list is empty. We want to maintain
            // string comparability between the parsed and reserialized media
            // lines.
            fmt_space = if self.formats.is_empty() { "" } else { " " },
            formats = self.formats,
        )
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpMediaValue {
    Audio,
    Video,
    Application,
    Unknown(String),
}

impl fmt::Display for SdpMediaValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpMediaValue::Audio => "audio",
            SdpMediaValue::Video => "video",
            SdpMediaValue::Application => "application",
            SdpMediaValue::Unknown(ref s) => s,
        }
        .fmt(f)
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpProtocolValue {
    RtpAvp,          /* RTP/AVP [RFC4566] */
    RtpAvpf,         /* RTP/AVPF [RFC4585] */
    RtpSavp,         /* RTP/SAVP [RFC3711] */
    RtpSavpf,        /* RTP/SAVPF [RFC5124] */
    TcpDtlsRtpSavp,  /* TCP/DTLS/RTP/SAVP [RFC7850] */
    TcpDtlsRtpSavpf, /* TCP/DTLS/RTP/SAVPF [RFC7850] */
    UdpTlsRtpSavp,   /* UDP/TLS/RTP/SAVP [RFC5764] */
    UdpTlsRtpSavpf,  /* UDP/TLS/RTP/SAVPF [RFC5764] */
    DtlsSctp,        /* DTLS/SCTP [draft-ietf-mmusic-sctp-sdp-07] */
    UdpDtlsSctp,     /* UDP/DTLS/SCTP [draft-ietf-mmusic-sctp-sdp-26] */
    TcpDtlsSctp,     /* TCP/DTLS/SCTP [draft-ietf-mmusic-sctp-sdp-26] */
}

impl fmt::Display for SdpProtocolValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpProtocolValue::RtpAvp => "RTP/AVP",
            SdpProtocolValue::RtpAvpf => "RTP/AVPF",
            SdpProtocolValue::RtpSavp => "RTP/SAVP",
            SdpProtocolValue::RtpSavpf => "RTP/SAVPF",
            SdpProtocolValue::TcpDtlsRtpSavp => "TCP/DTLS/RTP/SAVP",
            SdpProtocolValue::TcpDtlsRtpSavpf => "TCP/DTLS/RTP/SAVPF",
            SdpProtocolValue::UdpTlsRtpSavp => "UDP/TLS/RTP/SAVP",
            SdpProtocolValue::UdpTlsRtpSavpf => "UDP/TLS/RTP/SAVPF",
            SdpProtocolValue::DtlsSctp => "DTLS/SCTP",
            SdpProtocolValue::UdpDtlsSctp => "UDP/DTLS/SCTP",
            SdpProtocolValue::TcpDtlsSctp => "TCP/DTLS/SCTP",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpFormatList {
    Integers(Vec<u32>),
    Strings(Vec<String>),
    // Unknown media types have unknown format list types, and are not parsed
    Unknown(String),
}

impl fmt::Display for SdpFormatList {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpFormatList::Integers(ref x) => write!(
                f,
                "{}",
                x.iter()
                    .map(ToString::to_string)
                    .collect::<Vec<String>>()
                    .join(" ")
            ),
            SdpFormatList::Strings(ref x) => write!(f, "{}", x.join(" ")),
            SdpFormatList::Unknown(ref x) => write!(f, "{}", x),
        }
    }
}

impl SdpFormatList {
    pub fn is_empty(&self) -> bool {
        match self {
            SdpFormatList::Integers(x) => x.is_empty(),
            SdpFormatList::Strings(x) => x.is_empty(),
            SdpFormatList::Unknown(unparsed) => unparsed.is_empty(),
        }
    }
}

/*
 * RFC4566
 * media-descriptions =  *( media-field
 *                       information-field
 *                       *connection-field
 *                       bandwidth-fields
 *                       key-field
 *                       attribute-fields )
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpMedia {
    media: SdpMediaLine,
    information: Option<String>,
    connection: Option<SdpConnection>,
    bandwidth: Vec<SdpBandwidth>,
    attribute: Vec<SdpAttribute>,
    // unsupported values:
    // key: Option<String>,
}

impl fmt::Display for SdpMedia {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "m={mline}\r\n{connection}{bw}{attributes}",
            mline = self.media,
            bw = maybe_vector_to_string!("b={}\r\n", self.bandwidth, "\r\nb="),
            connection = option_to_string!("c={}\r\n", self.connection),
            attributes = maybe_vector_to_string!("a={}\r\n", self.attribute, "\r\na=")
        )
    }
}

impl SdpMedia {
    pub fn new(media: SdpMediaLine) -> SdpMedia {
        SdpMedia {
            media,
            information: None,
            connection: None,
            bandwidth: Vec::new(),
            attribute: Vec::new(),
        }
    }

    pub fn get_type(&self) -> &SdpMediaValue {
        &self.media.media
    }

    pub fn set_port(&mut self, port: u32) {
        self.media.port = port;
    }

    pub fn get_port(&self) -> u32 {
        self.media.port
    }

    pub fn get_port_count(&self) -> u32 {
        self.media.port_count
    }

    pub fn get_proto(&self) -> &SdpProtocolValue {
        &self.media.proto
    }

    pub fn get_formats(&self) -> &SdpFormatList {
        &self.media.formats
    }

    pub fn get_information(&self) -> &Option<String> {
        &self.information
    }

    pub fn set_information(&mut self, information: Option<String>) {
        self.information = information;
    }

    pub fn get_bandwidth(&self) -> &Vec<SdpBandwidth> {
        &self.bandwidth
    }

    pub fn add_bandwidth(&mut self, bw: SdpBandwidth) {
        self.bandwidth.push(bw)
    }

    pub fn get_attributes(&self) -> &Vec<SdpAttribute> {
        &self.attribute
    }

    pub fn add_attribute(&mut self, attr: SdpAttribute) -> Result<(), SdpParserInternalError> {
        if !attr.allowed_at_media_level() {
            return Err(SdpParserInternalError::Generic(format!(
                "{attr} not allowed at media level"
            )));
        }
        self.attribute.push(attr);
        Ok(())
    }

    pub fn get_attribute(&self, t: SdpAttributeType) -> Option<&SdpAttribute> {
        self.attribute
            .iter()
            .find(|a| SdpAttributeType::from(*a) == t)
    }

    pub fn remove_attribute(&mut self, t: SdpAttributeType) {
        self.attribute.retain(|a| SdpAttributeType::from(a) != t);
    }

    pub fn set_attribute(&mut self, attr: SdpAttribute) -> Result<(), SdpParserInternalError> {
        self.remove_attribute(SdpAttributeType::from(&attr));
        self.add_attribute(attr)
    }

    pub fn remove_codecs(&mut self) {
        match self.media.formats {
            SdpFormatList::Integers(_) => self.media.formats = SdpFormatList::Integers(Vec::new()),
            SdpFormatList::Strings(_) => self.media.formats = SdpFormatList::Strings(Vec::new()),
            SdpFormatList::Unknown(_) => self.media.formats = SdpFormatList::Unknown(String::new()),
        }

        self.attribute.retain({
            |x| {
                !matches!(
                    *x,
                    SdpAttribute::Rtpmap(_)
                        | SdpAttribute::Fmtp(_)
                        | SdpAttribute::Rtcpfb(_)
                        | SdpAttribute::Sctpmap(_)
                        | SdpAttribute::SctpPort(_)
                )
            }
        });
    }

    // This seems like it could be test-only
    pub fn add_codec(&mut self, rtpmap: SdpAttributeRtpmap) -> Result<(), SdpParserInternalError> {
        match self.media.formats {
            SdpFormatList::Integers(ref mut x) => x.push(u32::from(rtpmap.payload_type)),
            SdpFormatList::Strings(ref mut x) => x.push(rtpmap.payload_type.to_string()),
            SdpFormatList::Unknown(_) => {}
        }

        self.add_attribute(SdpAttribute::Rtpmap(rtpmap))?;
        Ok(())
    }

    pub fn get_attributes_of_type(&self, t: SdpAttributeType) -> Vec<&SdpAttribute> {
        self.attribute
            .iter()
            .filter(|a| SdpAttributeType::from(*a) == t)
            .collect()
    }

    pub fn get_connection(&self) -> &Option<SdpConnection> {
        &self.connection
    }

    pub fn set_connection(&mut self, c: SdpConnection) {
        self.connection = Some(c)
    }

    pub fn add_datachannel(
        &mut self,
        name: String,
        port: u16,
        streams: u16,
        msg_size: u32,
    ) -> Result<(), SdpParserInternalError> {
        // Only one allowed, for now. This may change as the specs (and deployments) evolve.
        match self.media.proto {
            SdpProtocolValue::UdpDtlsSctp | SdpProtocolValue::TcpDtlsSctp => {
                // new data channel format according to draft 21
                self.media.formats = SdpFormatList::Strings(vec![name]);
                self.set_attribute(SdpAttribute::SctpPort(u64::from(port)))?;
            }
            _ => {
                // old data channels format according to draft 05
                self.media.formats = SdpFormatList::Integers(vec![u32::from(port)]);
                self.set_attribute(SdpAttribute::Sctpmap(SdpAttributeSctpmap {
                    port,
                    channels: u32::from(streams),
                }))?;
            }
        }
        if msg_size > 0 {
            self.set_attribute(SdpAttribute::MaxMessageSize(u64::from(msg_size)))?;
        }
        self.media.media = SdpMediaValue::Application;

        Ok(())
    }
}

impl AnonymizingClone for SdpMedia {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        let mut masked = SdpMedia {
            media: self.media.clone(),
            information: self.information.clone(),
            bandwidth: self.bandwidth.clone(),
            connection: self.connection.clone(),
            attribute: Vec::new(),
        };
        for i in &self.attribute {
            masked.attribute.push(i.masked_clone(anon));
        }
        masked
    }
}

fn parse_media_token(value: &str) -> Result<SdpMediaValue, SdpParserInternalError> {
    Ok(match value.to_lowercase().as_ref() {
        "audio" => SdpMediaValue::Audio,
        "video" => SdpMediaValue::Video,
        "application" => SdpMediaValue::Application,
        "" => {
            return Err(SdpParserInternalError::Generic(
                "empty media value".to_string(),
            ))
        }
        _ => SdpMediaValue::Unknown(value.to_string()),
    })
}

fn parse_protocol_token(value: &str) -> Result<SdpProtocolValue, SdpParserInternalError> {
    Ok(match value.to_uppercase().as_ref() {
        "RTP/AVP" => SdpProtocolValue::RtpAvp,
        "RTP/AVPF" => SdpProtocolValue::RtpAvpf,
        "RTP/SAVP" => SdpProtocolValue::RtpSavp,
        "RTP/SAVPF" => SdpProtocolValue::RtpSavpf,
        "TCP/DTLS/RTP/SAVP" => SdpProtocolValue::TcpDtlsRtpSavp,
        "TCP/DTLS/RTP/SAVPF" => SdpProtocolValue::TcpDtlsRtpSavpf,
        "UDP/TLS/RTP/SAVP" => SdpProtocolValue::UdpTlsRtpSavp,
        "UDP/TLS/RTP/SAVPF" => SdpProtocolValue::UdpTlsRtpSavpf,
        "DTLS/SCTP" => SdpProtocolValue::DtlsSctp,
        "UDP/DTLS/SCTP" => SdpProtocolValue::UdpDtlsSctp,
        "TCP/DTLS/SCTP" => SdpProtocolValue::TcpDtlsSctp,
        _ => {
            return Err(SdpParserInternalError::Unsupported(format!(
                "unsupported protocol value: {value}"
            )));
        }
    })
}

pub fn parse_media(value: &str) -> Result<SdpType, SdpParserInternalError> {
    // Split out the media type, port, and protocol tokens, with the remainder
    // being the format list in single string.
    let mv: Vec<&str> = value.splitn(4, char::is_whitespace).collect();
    if mv.is_empty() {
        return Err(SdpParserInternalError::Generic(
            "media attribute must have a media type".to_string(),
        ));
    }
    let media = parse_media_token(mv[0])?;
    match (&media, mv.len()) {
        (SdpMediaValue::Unknown(_), l) if l < 3 => {
            // RFC 8866: https://www.rfc-editor.org/info/rfc8866/#MediaTypes
            // While spec requires a <fmt> token, it leaves the definition of
            // what is in that token to the specification that defines the
            // media type. It could conceivably allow for an empty string.
            // Since we ignore unknown media types, we refrain from having
            // an opinion on the contents of <fmt>.
            return Err(SdpParserInternalError::Generic(
                "media attribute with unknown type must have at least 3 tokens".to_string(),
            ));
        }
        (
            m @ SdpMediaValue::Audio | m @ SdpMediaValue::Video | m @ SdpMediaValue::Application,
            l,
        ) if l < 4 => {
            return Err(SdpParserInternalError::Generic(format!(
                "media attribute of type {m} must have at least 4 tokens"
            )));
        }
        _ => {}
    }
    let mut ptokens = mv[1].split('/');
    let port = match ptokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "missing port token".to_string(),
            ));
        }
        Some(p) => p.parse::<u32>()?,
    };
    if port > 65535 {
        return Err(SdpParserInternalError::Generic(
            "media port token is too big".to_string(),
        ));
    }
    let port_count = match ptokens.next() {
        None => 0,
        Some(c) => c.parse::<u32>()?,
    };
    let proto = parse_protocol_token(mv[2])?;
    let formats = match media {
        SdpMediaValue::Audio | SdpMediaValue::Video => {
            let fmt_slice = &mv[3].split_whitespace().collect::<Vec<&str>>();
            let mut fmt_vec: Vec<u32> = vec![];
            for num in fmt_slice {
                let fmt_num = num.parse::<u32>()?;
                #[cfg(feature = "pedantic")]
                if matches!(fmt_num, 1 | 2 | 19 | 64..=95 | 128 .. ) {
                    return Err(SdpParserInternalError::Generic(
                        "format number in media line is out of range".to_string(),
                    ));
                }
                fmt_vec.push(fmt_num);
            }
            SdpFormatList::Integers(fmt_vec)
        }
        SdpMediaValue::Application => {
            let fmt_slice = &mv[3].split_whitespace().collect::<Vec<&str>>();
            let mut fmt_vec: Vec<String> = vec![];
            // TODO enforce length == 1 and content 'webrtc-datachannel' only?
            for token in fmt_slice {
                fmt_vec.push(String::from(*token));
            }
            SdpFormatList::Strings(fmt_vec)
        }
        SdpMediaValue::Unknown(_) => SdpFormatList::Unknown(if mv.len() > 3 {
            mv[3].to_string()
        } else {
            "".to_string()
        }),
    };
    let m = SdpMediaLine {
        media,
        port,
        port_count,
        proto,
        formats,
    };
    trace!("media: {}, {}, {}, {}", m.media, m.port, m.proto, m.formats);
    Ok(SdpType::Media(m))
}

pub fn parse_media_vector(lines: &mut Vec<SdpLine>) -> Result<Vec<SdpMedia>, SdpParserError> {
    let mut media_sections: Vec<SdpMedia> = Vec::new();

    let media_line = lines.remove(0);
    let mut sdp_media = match media_line.sdp_type {
        SdpType::Media(v) => SdpMedia::new(v),
        _ => {
            return Err(SdpParserError::Sequence {
                message: "first line in media section needs to be a media line".to_string(),
                line_number: media_line.line_number,
            });
        }
    };

    while !lines.is_empty() {
        let line = lines.remove(0);
        let _line_number = line.line_number;
        match line.sdp_type {
            SdpType::Information(i) => {
                if sdp_media.information.is_some() {
                    return Err(SdpParserError::Sequence {
                        message: "information type already exists at this media level".to_string(),
                        line_number: _line_number,
                    });
                }
                sdp_media.set_information(Some(i));
            }
            SdpType::Connection(c) => {
                if sdp_media.connection.is_some() {
                    return Err(SdpParserError::Sequence {
                        message: "connection type already exists at this media level".to_string(),
                        line_number: _line_number,
                    });
                }

                sdp_media.set_connection(c);
            }
            SdpType::Bandwidth(b) => sdp_media.add_bandwidth(b),
            SdpType::Attribute(a) => {
                match a {
                    SdpAttribute::DtlsMessage(_) => {
                        // Ignore this attribute on media level
                        Ok(())
                    }
                    SdpAttribute::Rtpmap(rtpmap) => {
                        sdp_media.add_attribute(SdpAttribute::Rtpmap(SdpAttributeRtpmap {
                            payload_type: rtpmap.payload_type,
                            codec_name: rtpmap.codec_name.clone(),
                            frequency: rtpmap.frequency,
                            channels: rtpmap.channels,
                        }))
                    }
                    _ => sdp_media.add_attribute(a),
                }
                .map_err(|e: SdpParserInternalError| SdpParserError::Sequence {
                    message: format!("{e}"),
                    line_number: _line_number,
                })?
            }
            SdpType::Media(v) => {
                media_sections.push(sdp_media);
                sdp_media = SdpMedia::new(v);
            }

            SdpType::Origin(_) | SdpType::Session(_) | SdpType::Timing(_) | SdpType::Version(_) => {
                return Err(SdpParserError::Sequence {
                    message: "invalid type in media section".to_string(),
                    line_number: line.line_number,
                });
            }
        };
    }

    media_sections.push(sdp_media);

    Ok(media_sections)
}

#[cfg(test)]
#[path = "./media_type_tests.rs"]
mod media_type_tests;
