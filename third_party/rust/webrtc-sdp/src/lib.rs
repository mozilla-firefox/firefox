/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![warn(clippy::all)]
#![forbid(unsafe_code)]

#[macro_use]
extern crate log;
#[cfg(feature = "serialize")]
#[macro_use]
extern crate serde_derive;
#[cfg(feature = "serialize")]
extern crate serde;
use std::convert::TryFrom;
use std::fmt;

#[macro_use]
pub mod attribute_type;
pub mod address;
pub mod anonymizer;
pub mod error;
pub mod media_type;
pub mod network;

use address::{AddressTyped, ExplicitlyTypedAddress};
use anonymizer::{AnonymizingClone, StatefulSdpAnonymizer};
use attribute_type::{
    parse_attribute, SdpAttribute, SdpAttributeRid, SdpAttributeSimulcastVersion, SdpAttributeType,
    SdpSingleDirection,
};
use error::{SdpParserError, SdpParserInternalError};
use media_type::{
    parse_media, parse_media_vector, SdpFormatList, SdpMedia, SdpMediaLine, SdpMediaValue,
    SdpProtocolValue,
};
use network::{parse_address_type, parse_network_type};

/*
 * RFC4566
 * bandwidth-fields =    *(%x62 "=" bwtype ":" bandwidth CRLF)
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpBandwidth {
    As(u32),
    Ct(u32),
    Tias(u32),
    Unknown(String, u32),
}

impl fmt::Display for SdpBandwidth {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let (tp_string, value) = match *self {
            SdpBandwidth::As(ref x) => ("AS", x),
            SdpBandwidth::Ct(ref x) => ("CT", x),
            SdpBandwidth::Tias(ref x) => ("TIAS", x),
            SdpBandwidth::Unknown(ref tp, ref x) => (&tp[..], x),
        };
        write!(f, "{tp_string}:{value}")
    }
}

/*
 * RFC4566
 * connection-field =    [%x63 "=" nettype SP addrtype SP
 *                       connection-address CRLF]
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpConnection {
    pub address: ExplicitlyTypedAddress,
    pub ttl: Option<u8>,
    pub amount: Option<u32>,
}

impl fmt::Display for SdpConnection {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.address.fmt(f)?;
        write_option_string!(f, "/{}", self.ttl)?;
        write_option_string!(f, "/{}", self.amount)
    }
}

impl AnonymizingClone for SdpConnection {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        let mut masked = self.clone();
        masked.address = anon.mask_typed_address(&self.address);
        masked
    }
}

/*
 * RFC4566
 * origin-field =        %x6f "=" username SP sess-id SP sess-version SP
 *                       nettype SP addrtype SP unicast-address CRLF
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpOrigin {
    pub username: String,
    pub session_id: u64,
    pub session_version: u64,
    pub unicast_addr: ExplicitlyTypedAddress,
}

impl fmt::Display for SdpOrigin {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{username} {sess_id} {sess_vers} {unicast_addr}",
            username = self.username,
            sess_id = self.session_id,
            sess_vers = self.session_version,
            unicast_addr = self.unicast_addr
        )
    }
}

impl AnonymizingClone for SdpOrigin {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        let mut masked = self.clone();
        masked.username = anon.mask_origin_user(&self.username);
        masked.unicast_addr = anon.mask_typed_address(&masked.unicast_addr);
        masked
    }
}

/*
 * RFC4566
 * time-fields =         1*( %x74 "=" start-time SP stop-time
 *                       *(CRLF repeat-fields) CRLF)
 *                       [zone-adjustments CRLF]
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpTiming {
    pub start: u64,
    pub stop: u64,
}

impl fmt::Display for SdpTiming {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{start} {stop}", start = self.start, stop = self.stop)
    }
}

#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpType {
    // Note: Email, Information, Key, Phone, Repeat, Uri and Zone are left out
    //       on purposes as we don't want to support them.
    Attribute(SdpAttribute),
    Bandwidth(SdpBandwidth),
    Connection(SdpConnection),
    Media(SdpMediaLine),
    Origin(SdpOrigin),
    Session(String),
    Timing(SdpTiming),
    Version(u64),
}

#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpLine {
    pub line_number: usize,
    pub sdp_type: SdpType,
    pub text: String,
}

/*
 * RFC4566
 * ; SDP Syntax
 * session-description = proto-version
 *                       origin-field
 *                       session-name-field
 *                       information-field
 *                       uri-field
 *                       email-fields
 *                       phone-fields
 *                       connection-field
 *                       bandwidth-fields
 *                       time-fields
 *                       key-field
 *                       attribute-fields
 *                       media-descriptions
 */
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpSession {
    pub version: u64,
    pub origin: SdpOrigin,
    pub session: Option<String>,
    pub connection: Option<SdpConnection>,
    pub bandwidth: Vec<SdpBandwidth>,
    pub timing: Option<SdpTiming>,
    pub attribute: Vec<SdpAttribute>,
    pub media: Vec<SdpMedia>,
    pub warnings: Vec<SdpParserError>, // unsupported values:
                                       // information: Option<String>,
                                       // uri: Option<String>,
                                       // email: Option<String>,
                                       // phone: Option<String>,
                                       // repeat: Option<String>,
                                       // zone: Option<String>,
                                       // key: Option<String>
}

impl fmt::Display for SdpSession {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "v={version}\r\n\
             o={origin}\r\n\
             s={session}\r\n\
             {timing}\
             {bandwidth}\
             {connection}\
             {session_attributes}\
             {media_sections}",
            version = self.version,
            origin = self.origin,
            session = self.get_session_text(),
            timing = option_to_string!("t={}\r\n", self.timing),
            bandwidth = maybe_vector_to_string!("b={}\r\n", self.bandwidth, "\r\nb="),
            connection = option_to_string!("c={}\r\n", self.connection),
            session_attributes = maybe_vector_to_string!("a={}\r\n", self.attribute, "\r\na="),
            media_sections = self.media.iter().map(|s| s.to_string()).collect::<String>(),
        )
    }
}

impl SdpSession {
    pub fn new(version: u64, origin: SdpOrigin, session: String) -> SdpSession {
        let session = match session.trim() {
            s if !s.is_empty() => Some(s.to_owned()),
            _ => None,
        };
        SdpSession {
            version,
            origin,
            session,
            connection: None,
            bandwidth: Vec::new(),
            timing: None,
            attribute: Vec::new(),
            media: Vec::new(),
            warnings: Vec::new(),
        }
    }

    pub fn get_version(&self) -> u64 {
        self.version
    }

    pub fn get_origin(&self) -> &SdpOrigin {
        &self.origin
    }

    pub fn get_session(&self) -> &Option<String> {
        &self.session
    }

    pub fn get_session_text(&self) -> &str {
        if let Some(text) = &self.session {
            text.as_str()
        } else {
            " "
        }
    }
    pub fn get_connection(&self) -> &Option<SdpConnection> {
        &self.connection
    }

    pub fn set_connection(&mut self, c: SdpConnection) {
        self.connection = Some(c)
    }

    pub fn add_bandwidth(&mut self, b: SdpBandwidth) {
        self.bandwidth.push(b)
    }

    pub fn set_timing(&mut self, t: SdpTiming) {
        self.timing = Some(t)
    }

    pub fn add_attribute(&mut self, a: SdpAttribute) -> Result<(), SdpParserInternalError> {
        if !a.allowed_at_session_level() {
            return Err(SdpParserInternalError::Generic(format!(
                "{a} not allowed at session level"
            )));
        };
        self.attribute.push(a);
        Ok(())
    }

    pub fn extend_media(&mut self, v: Vec<SdpMedia>) {
        self.media.extend(v)
    }

    pub fn parse_session_vector(&mut self, lines: &mut Vec<SdpLine>) -> Result<(), SdpParserError> {
        while !lines.is_empty() {
            let line = lines.remove(0);
            match line.sdp_type {
                SdpType::Attribute(a) => {
                    let _line_number = line.line_number;
                    self.add_attribute(a).map_err(|e: SdpParserInternalError| {
                        SdpParserError::Sequence {
                            message: format!("{e}"),
                            line_number: _line_number,
                        }
                    })?
                }
                SdpType::Bandwidth(b) => self.add_bandwidth(b),
                SdpType::Timing(t) => self.set_timing(t),
                SdpType::Connection(c) => self.set_connection(c),

                SdpType::Origin(_) | SdpType::Session(_) | SdpType::Version(_) => {
                    return Err(SdpParserError::Sequence {
                        message: "version, origin or session at wrong level".to_string(),
                        line_number: line.line_number,
                    });
                }
                SdpType::Media(_) => {
                    return Err(SdpParserError::Sequence {
                        message: "media line not allowed in session parser".to_string(),
                        line_number: line.line_number,
                    });
                }
            }
        }
        Ok(())
    }

    pub fn get_attribute(&self, t: SdpAttributeType) -> Option<&SdpAttribute> {
        self.attribute
            .iter()
            .find(|a| SdpAttributeType::from(*a) == t)
    }

    pub fn add_media(
        &mut self,
        media_type: SdpMediaValue,
        direction: SdpAttribute,
        port: u32,
        protocol: SdpProtocolValue,
        addr: ExplicitlyTypedAddress,
    ) -> Result<(), SdpParserInternalError> {
        let mut media = SdpMedia::new(SdpMediaLine {
            media: media_type,
            port,
            port_count: 1,
            proto: protocol,
            formats: SdpFormatList::Integers(Vec::new()),
        });

        media.add_attribute(direction)?;

        media.set_connection(SdpConnection {
            address: addr,
            ttl: None,
            amount: None,
        });

        self.media.push(media);

        Ok(())
    }
}

impl AnonymizingClone for SdpSession {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        let mut masked: SdpSession = SdpSession {
            version: self.version,
            session: self.session.clone(),
            origin: self.origin.masked_clone(anon),
            connection: self.connection.clone(),
            timing: self.timing.clone(),
            bandwidth: self.bandwidth.clone(),
            attribute: Vec::new(),
            media: Vec::new(),
            warnings: Vec::new(),
        };
        masked.origin = self.origin.masked_clone(anon);
        masked.connection = masked.connection.map(|con| con.masked_clone(anon));
        for i in &self.attribute {
            masked.attribute.push(i.masked_clone(anon));
        }
        masked
    }
}

/* removing this wrap would not allow us to call this from the match statement inside
 * parse_sdp_line() */
#[allow(clippy::unnecessary_wraps)]
fn parse_session(value: &str) -> Result<SdpType, SdpParserInternalError> {
    trace!("session: {}", value);
    Ok(SdpType::Session(String::from(value)))
}

fn parse_version(value: &str) -> Result<SdpType, SdpParserInternalError> {
    let ver = value.parse::<u64>()?;
    if ver != 0 {
        return Err(SdpParserInternalError::Generic(format!(
            "version type contains unsupported value {ver}"
        )));
    };
    trace!("version: {}", ver);
    Ok(SdpType::Version(ver))
}

fn parse_origin(value: &str) -> Result<SdpType, SdpParserInternalError> {
    let mut tokens = value.split_whitespace();
    let username = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Origin type is missing username token".to_string(),
            ));
        }
        Some(x) => x,
    };
    let session_id = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Origin type is missing session ID token".to_string(),
            ));
        }
        Some(x) => x.parse::<u64>()?,
    };
    let session_version = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Origin type is missing session version token".to_string(),
            ));
        }
        Some(x) => x.parse::<u64>()?,
    };
    match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Origin type is missing network type token".to_string(),
            ));
        }
        Some(x) => parse_network_type(x)?,
    };
    let addrtype = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Origin type is missing address type token".to_string(),
            ));
        }
        Some(x) => parse_address_type(x)?,
    };
    let unicast_addr = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Origin type is missing IP address token".to_string(),
            ));
        }
        Some(x) => ExplicitlyTypedAddress::try_from((addrtype, x))?,
    };
    if addrtype != unicast_addr.address_type() {
        return Err(SdpParserInternalError::Generic(
            "Origin addrtype does not match address.".to_string(),
        ));
    }
    let o = SdpOrigin {
        username: String::from(username),
        session_id,
        session_version,
        unicast_addr,
    };
    trace!("origin: {}", o);
    Ok(SdpType::Origin(o))
}

fn parse_connection(value: &str) -> Result<SdpType, SdpParserInternalError> {
    let cv: Vec<&str> = value.split_whitespace().collect();
    if cv.len() != 3 {
        return Err(SdpParserInternalError::Generic(
            "connection attribute must have three tokens".to_string(),
        ));
    }
    parse_network_type(cv[0])?;
    let addrtype = parse_address_type(cv[1])?;
    let mut ttl = None;
    let mut amount = None;
    let mut addr_token = cv[2];
    if addr_token.find('/').is_some() {
        let addr_tokens: Vec<&str> = addr_token.split('/').collect();
        if addr_tokens.len() >= 3 {
            amount = Some(addr_tokens[2].parse::<u32>()?);
        }
        ttl = Some(addr_tokens[1].parse::<u8>()?);
        addr_token = addr_tokens[0];
    }
    let address = ExplicitlyTypedAddress::try_from((addrtype, addr_token))?;
    let c = SdpConnection {
        address,
        ttl,
        amount,
    };
    trace!("connection: {}", c);
    Ok(SdpType::Connection(c))
}

fn parse_bandwidth(value: &str) -> Result<SdpType, SdpParserInternalError> {
    let bv: Vec<&str> = value.split(':').collect();
    if bv.len() != 2 {
        return Err(SdpParserInternalError::Generic(
            "bandwidth attribute must have two tokens".to_string(),
        ));
    }
    let bandwidth = bv[1].parse::<u32>()?;
    let bw = match bv[0].to_uppercase().as_ref() {
        "AS" => SdpBandwidth::As(bandwidth),
        "CT" => SdpBandwidth::Ct(bandwidth),
        "TIAS" => SdpBandwidth::Tias(bandwidth),
        _ => SdpBandwidth::Unknown(String::from(bv[0]), bandwidth),
    };
    trace!("bandwidth: {}", bw);
    Ok(SdpType::Bandwidth(bw))
}

fn parse_timing(value: &str) -> Result<SdpType, SdpParserInternalError> {
    let tv: Vec<&str> = value.split_whitespace().collect();
    if tv.len() != 2 {
        return Err(SdpParserInternalError::Generic(
            "timing attribute must have two tokens".to_string(),
        ));
    }
    let start = tv[0].parse::<u64>()?;
    let stop = tv[1].parse::<u64>()?;
    let t = SdpTiming { start, stop };
    trace!("timing: {}", t);
    Ok(SdpType::Timing(t))
}

pub fn parse_sdp_line(line: &str, line_number: usize) -> Result<SdpLine, SdpParserError> {
    if line.find('=').is_none() {
        return Err(SdpParserError::Line {
            error: SdpParserInternalError::Generic("missing = character in line".to_string()),
            line: line.to_string(),
            line_number,
        });
    }
    let mut splitted_line = line.splitn(2, '=');
    let line_type = match splitted_line.next() {
        None => {
            return Err(SdpParserError::Line {
                error: SdpParserInternalError::Generic("missing type".to_string()),
                line: line.to_string(),
                line_number,
            });
        }
        Some(t) => {
            let trimmed = t.trim();
            if trimmed.len() > 1 {
                return Err(SdpParserError::Line {
                    error: SdpParserInternalError::Generic("type too long".to_string()),
                    line: line.to_string(),
                    line_number,
                });
            }
            if trimmed.is_empty() {
                return Err(SdpParserError::Line {
                    error: SdpParserInternalError::Generic("type is empty".to_string()),
                    line: line.to_string(),
                    line_number,
                });
            }
            trimmed.to_lowercase()
        }
    };
    let (line_value, untrimmed_line_value) = match splitted_line.next() {
        None => {
            return Err(SdpParserError::Line {
                error: SdpParserInternalError::Generic("missing value".to_string()),
                line: line.to_string(),
                line_number,
            });
        }
        Some(v) => {
            let trimmed = v.trim();
            // For compatibility with sites that don't adhere to "s=-" for no session ID
            if trimmed.is_empty() && line_type.as_str() != "s" {
                return Err(SdpParserError::Line {
                    error: SdpParserInternalError::Generic("value is empty".to_string()),
                    line: line.to_string(),
                    line_number,
                });
            }
            (trimmed, v)
        }
    };
    match line_type.as_ref() {
        "a" => parse_attribute(line_value),
        "b" => parse_bandwidth(line_value),
        "c" => parse_connection(line_value),
        "e" => Err(SdpParserInternalError::Generic(format!(
            "unsupported type email: {line_value}"
        ))),
        "i" => Err(SdpParserInternalError::Generic(format!(
            "unsupported type information: {line_value}"
        ))),
        "k" => Err(SdpParserInternalError::Generic(format!(
            "unsupported insecure key exchange: {line_value}"
        ))),
        "m" => parse_media(line_value),
        "o" => parse_origin(line_value),
        "p" => Err(SdpParserInternalError::Generic(format!(
            "unsupported type phone: {line_value}"
        ))),
        "r" => Err(SdpParserInternalError::Generic(format!(
            "unsupported type repeat: {line_value}"
        ))),
        "s" => parse_session(untrimmed_line_value),
        "t" => parse_timing(line_value),
        "u" => Err(SdpParserInternalError::Generic(format!(
            "unsupported type uri: {line_value}"
        ))),
        "v" => parse_version(line_value),
        "z" => Err(SdpParserInternalError::Generic(format!(
            "unsupported type zone: {line_value}"
        ))),
        _ => Err(SdpParserInternalError::Generic(
            "unknown sdp type".to_string(),
        )),
    }
    .map(|sdp_type| SdpLine {
        line_number,
        sdp_type,
        text: line.to_owned(),
    })
    .map_err(|e| match e {
        SdpParserInternalError::UnknownAddressType(..)
        | SdpParserInternalError::AddressTypeMismatch { .. }
        | SdpParserInternalError::Generic(..)
        | SdpParserInternalError::Integer(..)
        | SdpParserInternalError::Float(..)
        | SdpParserInternalError::Domain(..)
        | SdpParserInternalError::IpAddress(..) => SdpParserError::Line {
            error: e,
            line: line.to_string(),
            line_number,
        },
        SdpParserInternalError::Unsupported(..) => SdpParserError::Unsupported {
            error: e,
            line: line.to_string(),
            line_number,
        },
    })
}

fn sanity_check_sdp_session(session: &SdpSession) -> Result<(), SdpParserError> {
    let make_seq_error = |x: &str| SdpParserError::Sequence {
        message: x.to_string(),
        line_number: 0,
    };

    if session.timing.is_none() {
        return Err(make_seq_error("Missing timing type at session level"));
    }
    // Checks that all media have connections if there is no top level
    // This explicitly allows for zero connection lines if there are no media
    // sections for interoperability reasons.
    let media_cons = &session.media.iter().all(|m| m.get_connection().is_some());
    if !media_cons && session.get_connection().is_none() {
        return Err(make_seq_error(
            "Without connection type at session level all media sections must have connection types",
        ));
    }

    // Check that extmaps are not defined on session and media level
    if session.get_attribute(SdpAttributeType::Extmap).is_some() {
        for msection in &session.media {
            if msection.get_attribute(SdpAttributeType::Extmap).is_some() {
                return Err(make_seq_error(
                    "Extmap can't be define at session and media level",
                ));
            }
        }
    }

    for msection in &session.media {
        if msection
            .get_attribute(SdpAttributeType::RtcpMuxOnly)
            .is_some()
            && msection.get_attribute(SdpAttributeType::RtcpMux).is_none()
        {
            return Err(make_seq_error(
                "rtcp-mux-only media sections must also contain the rtcp-mux attribute",
            ));
        }

        let rids: Vec<&SdpAttributeRid> = msection
            .get_attributes()
            .iter()
            .filter_map(|attr| match *attr {
                SdpAttribute::Rid(ref rid) => Some(rid),
                _ => None,
            })
            .collect();
        let recv_rids: Vec<&str> = rids
            .iter()
            .filter_map(|rid| match rid.direction {
                SdpSingleDirection::Recv => Some(rid.id.as_str()),
                _ => None,
            })
            .collect();
        let send_rids: Vec<&str> = rids
            .iter()
            .filter_map(|rid| match rid.direction {
                SdpSingleDirection::Send => Some(rid.id.as_str()),
                _ => None,
            })
            .collect();

        for rid_format in rids.iter().flat_map(|rid| &rid.formats) {
            match *msection.get_formats() {
                SdpFormatList::Integers(ref int_fmt) => {
                    if !int_fmt.contains(&(u32::from(*rid_format))) {
                        return Err(make_seq_error(
                            "Rid pts must be declared in the media section",
                        ));
                    }
                }
                SdpFormatList::Strings(ref str_fmt) => {
                    if !str_fmt.contains(&rid_format.to_string()) {
                        return Err(make_seq_error(
                            "Rid pts must be declared in the media section",
                        ));
                    }
                }
            }
        }

        if let Some(SdpAttribute::Simulcast(simulcast)) =
            msection.get_attribute(SdpAttributeType::Simulcast)
        {
            let check_defined_rids =
                |simulcast_version_list: &Vec<SdpAttributeSimulcastVersion>,
                 rid_ids: &[&str]|
                 -> Result<(), SdpParserError> {
                    for simulcast_rid in simulcast_version_list.iter().flat_map(|x| &x.ids) {
                        if !rid_ids.contains(&simulcast_rid.id.as_str()) {
                            return Err(make_seq_error(
                                "Simulcast RIDs must be defined in any rid attribute",
                            ));
                        }
                    }
                    Ok(())
                };

            check_defined_rids(&simulcast.receive, &recv_rids)?;
            check_defined_rids(&simulcast.send, &send_rids)?;
        }
    }

    Ok(())
}

fn parse_sdp_vector(lines: &mut Vec<SdpLine>) -> Result<SdpSession, SdpParserError> {
    if lines.len() < 4 {
        return Err(SdpParserError::Sequence {
            message: "SDP neeeds at least 4 lines".to_string(),
            line_number: 0,
        });
    }

    let version = match lines.remove(0).sdp_type {
        SdpType::Version(v) => v,
        _ => {
            return Err(SdpParserError::Sequence {
                message: "first line needs to be version number".to_string(),
                line_number: 0,
            });
        }
    };
    let origin = match lines.remove(0).sdp_type {
        SdpType::Origin(v) => v,
        _ => {
            return Err(SdpParserError::Sequence {
                message: "second line needs to be origin".to_string(),
                line_number: 1,
            });
        }
    };
    let session = match lines.remove(0).sdp_type {
        SdpType::Session(v) => v,
        _ => {
            return Err(SdpParserError::Sequence {
                message: "third line needs to be session".to_string(),
                line_number: 2,
            });
        }
    };
    let mut sdp_session = SdpSession::new(version, origin, session);

    let _media_pos = lines
        .iter()
        .position(|l| matches!(l.sdp_type, SdpType::Media(_)));

    match _media_pos {
        Some(p) => {
            let mut media: Vec<_> = lines.drain(p..).collect();
            sdp_session.parse_session_vector(lines)?;
            sdp_session.extend_media(parse_media_vector(&mut media)?);
        }
        None => sdp_session.parse_session_vector(lines)?,
    };

    sanity_check_sdp_session(&sdp_session)?;
    Ok(sdp_session)
}

pub fn parse_sdp(sdp: &str, fail_on_warning: bool) -> Result<SdpSession, SdpParserError> {
    if sdp.is_empty() {
        return Err(SdpParserError::Line {
            error: SdpParserInternalError::Generic("empty SDP".to_string()),
            line: sdp.to_string(),
            line_number: 0,
        });
    }
    // see test_parse_sdp_minimal_sdp_successfully
    if sdp.len() < 51 {
        return Err(SdpParserError::Line {
            error: SdpParserInternalError::Generic("string too short to be valid SDP".to_string()),
            line: sdp.to_string(),
            line_number: 0,
        });
    }
    let lines = sdp.lines();
    let mut errors: Vec<SdpParserError> = Vec::new();
    let mut warnings: Vec<SdpParserError> = Vec::new();
    let mut sdp_lines: Vec<SdpLine> = Vec::new();
    for (line_number, line) in lines.enumerate() {
        let stripped_line = line.trim();
        if stripped_line.is_empty() {
            continue;
        }
        match parse_sdp_line(line, line_number) {
            Ok(n) => {
                sdp_lines.push(n);
            }
            Err(e) => {
                match e {
                    // TODO is this really a good way to accomplish this?
                    SdpParserError::Line {
                        error,
                        line,
                        line_number,
                    } => errors.push(SdpParserError::Line {
                        error,
                        line,
                        line_number,
                    }),
                    SdpParserError::Unsupported {
                        error,
                        line,
                        line_number,
                    } => {
                        warnings.push(SdpParserError::Unsupported {
                            error,
                            line,
                            line_number,
                        });
                    }
                    SdpParserError::Sequence {
                        message,
                        line_number,
                    } => errors.push(SdpParserError::Sequence {
                        message,
                        line_number,
                    }),
                }
            }
        };
    }

    if fail_on_warning && (!warnings.is_empty()) {
        return Err(warnings.remove(0));
    }

    // We just return the last of the errors here
    if let Some(e) = errors.pop() {
        return Err(e);
    };

    let mut session = parse_sdp_vector(&mut sdp_lines)?;
    session.warnings = warnings;

    for warning in &session.warnings {
        warn!("Warning: {}", warning);
    }

    Ok(session)
}

#[cfg(test)]
#[path = "./lib_tests.rs"]
mod tests;
