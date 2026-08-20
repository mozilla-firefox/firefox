/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate url;
use std::convert::TryFrom;
use std::fmt;
use std::fmt::Write;
use std::iter;
use std::str::FromStr;

use error::SdpParserInternalError;
use network::{parse_network_type, parse_unicast_address};
use SdpType;

use address::{Address, AddressType, ExplicitlyTypedAddress};
use anonymizer::{AnonymizingClone, StatefulSdpAnonymizer};

// Serialization helper marcos and functions
#[macro_export]
macro_rules! option_to_string {
    ($fmt_str:expr, $opt:expr) => {
        match $opt {
            Some(ref x) => format!($fmt_str, x),
            None => "".to_string(),
        }
    };
}

#[macro_export]
macro_rules! write_option_string {
    ($f:expr, $fmt_str:expr, $opt:expr) => {
        match $opt {
            Some(ref x) => write!($f, $fmt_str, x),
            None => Ok(()),
        }
    };
}

#[macro_export]
macro_rules! maybe_vector_to_string {
    ($fmt_str:expr, $vec:expr, $sep:expr) => {
        match $vec.len() {
            0 => "".to_string(),
            _ => format!(
                $fmt_str,
                $vec.iter()
                    .map(ToString::to_string)
                    .filter(|s| !s.is_empty())
                    .collect::<Vec<String>>()
                    .join($sep)
            ),
        }
    };
}

#[macro_export]
macro_rules! non_empty_string_vec {
    ( $( $x:expr ),* ) => {
        {
            let mut temp_vec = Vec::new();
            $(
                if !$x.is_empty() {
                    temp_vec.push($x);
                }
            )*
            temp_vec
        }
    };
}

pub fn maybe_print_param<T>(name: &str, param: T, default_value: T) -> String
where
    T: PartialEq + ToString,
{
    if param != default_value {
        name.to_owned() + &param.to_string()
    } else {
        "".to_string()
    }
}

pub fn maybe_print_bool_param(name: &str, param: bool, default_value: bool) -> String {
    if param != default_value {
        name.to_owned() + "=" + &(param as i32).to_string()
    } else {
        "".to_string()
    }
}

#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpSingleDirection {
    // This is explicitly 1 and 2 to match the defines in the C++ glue code.
    Send = 1,
    Recv = 2,
}

impl fmt::Display for SdpSingleDirection {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpSingleDirection::Send => "send",
            SdpSingleDirection::Recv => "recv",
        }
        .fmt(f)
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributePayloadType {
    PayloadType(u8),
    Wildcard, // Wildcard means "*",
}

impl fmt::Display for SdpAttributePayloadType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributePayloadType::PayloadType(pt) => pt.fmt(f),
            SdpAttributePayloadType::Wildcard => "*".fmt(f),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributeCandidateTransport {
    Udp,
    Tcp,
}

impl fmt::Display for SdpAttributeCandidateTransport {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeCandidateTransport::Udp => "UDP",
            SdpAttributeCandidateTransport::Tcp => "TCP",
        }
        .fmt(f)
    }
}

#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributeCandidateType {
    Host,
    Srflx,
    Prflx,
    Relay,
}

impl fmt::Display for SdpAttributeCandidateType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeCandidateType::Host => "host",
            SdpAttributeCandidateType::Srflx => "srflx",
            SdpAttributeCandidateType::Prflx => "prflx",
            SdpAttributeCandidateType::Relay => "relay",
        }
        .fmt(f)
    }
}

#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributeCandidateTcpType {
    Active,
    Passive,
    Simultaneous,
}

impl fmt::Display for SdpAttributeCandidateTcpType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeCandidateTcpType::Active => "active",
            SdpAttributeCandidateTcpType::Passive => "passive",
            SdpAttributeCandidateTcpType::Simultaneous => "so",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeCandidate {
    pub foundation: String,
    pub component: u32,
    pub transport: SdpAttributeCandidateTransport,
    pub priority: u64,
    pub address: Address,
    pub port: u32,
    pub c_type: SdpAttributeCandidateType,
    pub raddr: Option<Address>,
    pub rport: Option<u32>,
    pub tcp_type: Option<SdpAttributeCandidateTcpType>,
    pub generation: Option<u32>,
    pub ufrag: Option<String>,
    pub networkcost: Option<u32>,
    pub unknown_extensions: Vec<(String, String)>,
}

impl fmt::Display for SdpAttributeCandidate {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{foundation} {component} {transport} {priority} \
             {address} {port} typ {ctype}\
             {raddr}{rport}{tcp_type}{generation}{ufrag}{cost}\
             {unknown}",
            foundation = self.foundation,
            component = self.component,
            transport = self.transport,
            priority = self.priority,
            address = self.address,
            port = self.port,
            ctype = self.c_type,
            raddr = option_to_string!(" raddr {}", self.raddr),
            rport = option_to_string!(" rport {}", self.rport),
            tcp_type = option_to_string!(" tcptype {}", self.tcp_type),
            generation = option_to_string!(" generation {}", self.generation),
            ufrag = option_to_string!(" ufrag {}", self.ufrag),
            cost = option_to_string!(" network-cost {}", self.networkcost),
            unknown =
                self.unknown_extensions
                    .iter()
                    .fold(String::new(), |mut output, (name, value)| {
                        let _ = write!(output, " {name} {value}");
                        output
                    })
        )
    }
}

impl SdpAttributeCandidate {
    pub fn new(
        foundation: String,
        component: u32,
        transport: SdpAttributeCandidateTransport,
        priority: u64,
        address: Address,
        port: u32,
        c_type: SdpAttributeCandidateType,
    ) -> SdpAttributeCandidate {
        SdpAttributeCandidate {
            foundation,
            component,
            transport,
            priority,
            address,
            port,
            c_type,
            raddr: None,
            rport: None,
            tcp_type: None,
            generation: None,
            ufrag: None,
            networkcost: None,
            unknown_extensions: Vec::new(),
        }
    }

    fn set_remote_address(&mut self, addr: Address) {
        self.raddr = Some(addr)
    }

    fn set_remote_port(&mut self, p: u32) {
        self.rport = Some(p)
    }

    fn set_tcp_type(&mut self, t: SdpAttributeCandidateTcpType) {
        self.tcp_type = Some(t)
    }

    fn set_generation(&mut self, g: u32) {
        self.generation = Some(g)
    }

    fn set_ufrag(&mut self, u: String) {
        self.ufrag = Some(u)
    }

    fn set_network_cost(&mut self, n: u32) {
        self.networkcost = Some(n)
    }

    fn add_unknown_extension(&mut self, name: String, value: String) {
        self.unknown_extensions.push((name, value));
    }
}

impl AnonymizingClone for SdpAttributeCandidate {
    fn masked_clone(&self, anonymizer: &mut StatefulSdpAnonymizer) -> Self {
        let mut masked = self.clone();
        masked.address = anonymizer.mask_address(&self.address);
        masked.port = anonymizer.mask_port(self.port);
        masked.raddr = self
            .raddr
            .clone()
            .map(|addr| anonymizer.mask_address(&addr));
        masked.rport = self.rport.map(|port| anonymizer.mask_port(port));
        masked
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttributeDtlsMessage {
    Client(String),
    Server(String),
}

impl fmt::Display for SdpAttributeDtlsMessage {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SdpAttributeDtlsMessage::Client(msg) => format!("client {msg}"),
            SdpAttributeDtlsMessage::Server(msg) => format!("server {msg}"),
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeRemoteCandidate {
    pub component: u32,
    pub address: Address,
    pub port: u32,
}

impl fmt::Display for SdpAttributeRemoteCandidate {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{component} {addr} {port}",
            component = self.component,
            addr = self.address,
            port = self.port
        )
    }
}

impl AnonymizingClone for SdpAttributeRemoteCandidate {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        SdpAttributeRemoteCandidate {
            address: anon.mask_address(&self.address),
            port: anon.mask_port(self.port),
            component: self.component,
        }
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeSimulcastId {
    pub id: String,
    pub paused: bool,
}

impl SdpAttributeSimulcastId {
    pub fn new(idstr: &str) -> SdpAttributeSimulcastId {
        if let Some(idstr) = idstr.strip_prefix('~') {
            SdpAttributeSimulcastId {
                id: idstr.to_string(),
                paused: true,
            }
        } else {
            SdpAttributeSimulcastId {
                id: idstr.to_string(),
                paused: false,
            }
        }
    }
}

impl fmt::Display for SdpAttributeSimulcastId {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.paused {
            write!(f, "~")?;
        }
        self.id.fmt(f)
    }
}

#[repr(C)]
#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeSimulcastVersion {
    pub ids: Vec<SdpAttributeSimulcastId>,
}

impl SdpAttributeSimulcastVersion {
    pub fn new(idlist: &str) -> SdpAttributeSimulcastVersion {
        SdpAttributeSimulcastVersion {
            ids: idlist
                .split(',')
                .map(SdpAttributeSimulcastId::new)
                .collect(),
        }
    }
}

impl fmt::Display for SdpAttributeSimulcastVersion {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.ids
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<String>>()
            .join(",")
            .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeSimulcast {
    pub send: Vec<SdpAttributeSimulcastVersion>,
    pub receive: Vec<SdpAttributeSimulcastVersion>,
}

impl fmt::Display for SdpAttributeSimulcast {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        non_empty_string_vec![
            maybe_vector_to_string!("send {}", self.send, ";"),
            maybe_vector_to_string!("recv {}", self.receive, ";")
        ]
        .join(" ")
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeRtcp {
    pub port: u16,
    pub unicast_addr: Option<ExplicitlyTypedAddress>,
}

impl SdpAttributeRtcp {
    pub fn new(port: u16) -> SdpAttributeRtcp {
        SdpAttributeRtcp {
            port,
            unicast_addr: None,
        }
    }

    fn set_addr(&mut self, addr: ExplicitlyTypedAddress) {
        self.unicast_addr = Some(addr)
    }
}

impl fmt::Display for SdpAttributeRtcp {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.unicast_addr {
            Some(ref addr) => write!(f, "{} {}", self.port, addr),
            None => self.port.fmt(f),
        }
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttributeRtcpFbType {
    Ack = 0,
    Ccm = 2, // This is explicitly 2 to make the conversion to the
    // enum used in the glue-code possible. The glue code has "app"
    // in the place of 1
    Nack,
    TrrInt,
    Remb,
    TransCc,
}

impl fmt::Display for SdpAttributeRtcpFbType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeRtcpFbType::Ack => "ack",
            SdpAttributeRtcpFbType::Ccm => "ccm",
            SdpAttributeRtcpFbType::Nack => "nack",
            SdpAttributeRtcpFbType::TrrInt => "trr-int",
            SdpAttributeRtcpFbType::Remb => "goog-remb",
            SdpAttributeRtcpFbType::TransCc => "transport-cc",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeRtcpFb {
    pub payload_type: SdpAttributePayloadType,
    pub feedback_type: SdpAttributeRtcpFbType,
    pub parameter: String,
    pub extra: String,
}

impl fmt::Display for SdpAttributeRtcpFb {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{} {}", self.payload_type, self.feedback_type,)?;
        if !self.parameter.is_empty() {
            write!(
                f,
                " {}{}",
                self.parameter,
                maybe_print_param(" ", self.extra.clone(), "".to_string()),
            )?;
        }
        Ok(())
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttributeDirection {
    Recvonly,
    Sendonly,
    Sendrecv,
}

impl fmt::Display for SdpAttributeDirection {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeDirection::Recvonly => "recvonly",
            SdpAttributeDirection::Sendonly => "sendonly",
            SdpAttributeDirection::Sendrecv => "sendrecv",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeExtmap {
    pub id: u16,
    pub direction: Option<SdpAttributeDirection>,
    pub url: String,
    pub extension_attributes: Option<String>,
}

impl fmt::Display for SdpAttributeExtmap {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{id}{direction} {url}{ext}",
            id = self.id,
            direction = option_to_string!("/{}", self.direction),
            url = self.url,
            ext = option_to_string!(" {}", self.extension_attributes)
        )
    }
}

#[derive(Clone, Copy)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct RtxFmtpParameters {
    pub apt: u8,
    pub rtx_time: Option<u32>,
}

impl fmt::Display for RtxFmtpParameters {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if let Some(rtx_time) = self.rtx_time {
            write!(f, "apt={};rtx-time={}", self.apt, rtx_time)
        } else {
            write!(f, "apt={}", self.apt)
        }
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeFmtpParameters {
    // H264
    // TODO(bug 1466859): Support sprop-parameter-sets
    // pub static const max_sprop_len: u32 = 128,
    // pub sprop_parameter_sets: [u8, max_sprop_len],
    pub packetization_mode: u32,
    pub level_asymmetry_allowed: bool,
    pub profile_level_id: u32,
    pub max_fs: u32,
    pub max_cpb: u32,
    pub max_dpb: u32,
    pub max_br: u32,
    pub max_mbps: u32,

    // VP8 and VP9
    // max_fs, already defined in H264
    pub max_fr: u32,

    // AV1
    // Defined in https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
    // f(3) a 3 bit value that specifies the profile of the AV1 codec which is
    // equivalent to the seq_profile field in the AV1 General Sequence Header
    // OBU
    // Defaults to 0 when not present
    // Value must not be greater than 2 as values 3 to 7 are reserved
    pub profile: Option<u8>,
    // f(5) a 5 bit value that specifies the level maps to a table in A.3. of
    // https://aomediacodec.github.io/av1-spec/av1-spec.pdf
    // Defaults to 5 when not present
    pub level_idx: Option<u8>,
    // f(1) a 1 that specifies the tier 5.5.1 of
    // https://aomediacodec.github.io/av1-spec/av1-spec.pdf
    // Defaults to 0 when not present
    pub tier: Option<u8>,

    // Opus https://tools.ietf.org/html/rfc7587
    pub maxplaybackrate: u32,
    pub maxaveragebitrate: u32,
    pub usedtx: bool,
    pub stereo: bool,
    pub useinbandfec: bool,
    pub cbr: bool,
    pub ptime: u32,
    pub minptime: u32,
    pub maxptime: u32,

    // Red
    pub encodings: Vec<u8>,

    // telephone-event
    pub dtmf_tones: String,

    // RTX
    pub rtx: Option<RtxFmtpParameters>,

    // Unknown
    pub unknown_tokens: Vec<String>,
}

impl fmt::Display for SdpAttributeFmtpParameters {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if let Some(ref rtx) = self.rtx {
            // rtx
            return write!(f, "{rtx}");
        }
        if !self.dtmf_tones.is_empty() {
            // telephone-event
            return write!(f, "{}", self.dtmf_tones);
        } else if !self.encodings.is_empty() {
            // red encodings
            return self
                .encodings
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<String>>()
                .join("/")
                .fmt(f);
        };
        write!(
            f,
            "{}",
            non_empty_string_vec![
                maybe_print_param(
                    "profile-level-id=",
                    format!("{:06x}", self.profile_level_id),
                    "42000a".to_string()
                ),
                maybe_print_bool_param(
                    "level-asymmetry-allowed",
                    self.level_asymmetry_allowed,
                    false
                ),
                maybe_print_param("packetization-mode=", self.packetization_mode, 0),
                maybe_print_param("max-fs=", self.max_fs, 0),
                maybe_print_param("max-cpb=", self.max_cpb, 0),
                maybe_print_param("max-dpb=", self.max_dpb, 0),
                maybe_print_param("max-br=", self.max_br, 0),
                maybe_print_param("max-mbps=", self.max_mbps, 0),
                maybe_print_param("max-fr=", self.max_fr, 0),
                match self.profile {
                    Some(profile) => format!("profile={}", profile),
                    None => "".to_string(),
                },
                match self.level_idx {
                    Some(level_idx) => format!("level-idx={}", level_idx),
                    None => "".to_string(),
                },
                match self.tier {
                    Some(tier) => format!("tier={}", tier),
                    None => "".to_string(),
                },
                maybe_print_param("maxplaybackrate=", self.maxplaybackrate, 48000),
                maybe_print_param("maxaveragebitrate=", self.maxaveragebitrate, 0),
                maybe_print_param("ptime=", self.ptime, 0),
                maybe_print_param("minptime=", self.minptime, 0),
                maybe_print_param("maxptime=", self.maxptime, 0),
                maybe_print_bool_param("usedtx", self.usedtx, false),
                maybe_print_bool_param("stereo", self.stereo, false),
                maybe_print_bool_param("useinbandfec", self.useinbandfec, false),
                maybe_print_bool_param("cbr", self.cbr, false),
                maybe_vector_to_string!("{}", self.unknown_tokens, ",")
            ]
            .join(";")
        )
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeFmtp {
    pub payload_type: u8,
    pub parameters: SdpAttributeFmtpParameters,
}

impl fmt::Display for SdpAttributeFmtp {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let parameters = self.parameters.to_string();
        if parameters.is_empty() {
            return Ok(());
        }
        write!(
            f,
            "{pt} {parameter}",
            pt = self.payload_type,
            parameter = parameters
        )
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttributeFingerprintHashType {
    Sha1,
    Sha224,
    Sha256,
    Sha384,
    Sha512,
}

impl SdpAttributeFingerprintHashType {
    pub fn try_from_name(name: &str) -> Result<Self, SdpParserInternalError> {
        match name {
            "sha-1" => Ok(Self::Sha1),
            "sha-224" => Ok(Self::Sha224),
            "sha-256" => Ok(Self::Sha256),
            "sha-384" => Ok(Self::Sha384),
            "sha-512" => Ok(Self::Sha512),
            unknown => Err(SdpParserInternalError::Unsupported(format!(
                "fingerprint contains an unsupported hash algorithm '{unknown}'"
            ))),
        }
    }
    pub fn octet_count(&self) -> usize {
        match self {
            Self::Sha1 => 20,
            Self::Sha224 => 28,
            Self::Sha256 => 32,
            Self::Sha384 => 48,
            Self::Sha512 => 64,
        }
    }

    pub fn parse_octets(&self, octets_string: &str) -> Result<Vec<u8>, SdpParserInternalError> {
        let bytes = octets_string
            .split(':')
            .map(|byte_token| {
                if byte_token.len() != 2 {
                    return Err(SdpParserInternalError::Generic(
                        "fingerpint's byte tokens must have 2 hexdigits".to_string(),
                    ));
                }
                Ok(u8::from_str_radix(byte_token, 16)?)
            })
            .collect::<Result<Vec<u8>, _>>()?;

        if bytes.len() != self.octet_count() {
            return Err(SdpParserInternalError::Generic(format!(
                "fingerprint has {} bytes but should have {} bytes",
                bytes.len(),
                self.octet_count(),
            )));
        }

        Ok(bytes)
    }
}

impl fmt::Display for SdpAttributeFingerprintHashType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeFingerprintHashType::Sha1 => "sha-1",
            SdpAttributeFingerprintHashType::Sha224 => "sha-224",
            SdpAttributeFingerprintHashType::Sha256 => "sha-256",
            SdpAttributeFingerprintHashType::Sha384 => "sha-384",
            SdpAttributeFingerprintHashType::Sha512 => "sha-512",
        }
        .fmt(f)
    }
}

#[derive(Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeFingerprint {
    pub hash_algorithm: SdpAttributeFingerprintHashType,
    pub fingerprint: Vec<u8>,
}

impl TryFrom<(SdpAttributeFingerprintHashType, Vec<u8>)> for SdpAttributeFingerprint {
    type Error = SdpParserInternalError;
    fn try_from(
        parts: (SdpAttributeFingerprintHashType, Vec<u8>),
    ) -> Result<Self, SdpParserInternalError> {
        let (hash_algorithm, fingerprint) = parts;
        match (hash_algorithm.octet_count(), fingerprint.len()) {
            (a, b) if a == b => Ok(Self {
                hash_algorithm,
                fingerprint,
            }),
            (a, b) => Err(SdpParserInternalError::Generic(format!(
                "Hash algoritm expects {a} fingerprint bytes not {b}",
            ))),
        }
    }
}

impl fmt::Display for SdpAttributeFingerprint {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{hash} {fp}",
            hash = self.hash_algorithm,
            fp = self
                .fingerprint
                .iter()
                .map(|byte| format!("{byte:02X}"))
                .collect::<Vec<String>>()
                .join(":")
        )
    }
}

impl AnonymizingClone for SdpAttributeFingerprint {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        SdpAttributeFingerprint {
            hash_algorithm: self.hash_algorithm,
            fingerprint: anon.mask_cert_finger_print(&self.fingerprint),
        }
    }
}

fn imageattr_discrete_value_list_to_string<T>(values: &[T]) -> String
where
    T: ToString,
{
    match values.len() {
        1 => values[0].to_string(),
        _ => format!(
            "[{}]",
            values
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<String>>()
                .join(",")
        ),
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributeImageAttrXyRange {
    Range(u32, u32, Option<u32>), // min, max, step
    DiscreteValues(Vec<u32>),
}

impl fmt::Display for SdpAttributeImageAttrXyRange {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeImageAttrXyRange::Range(ref min, ref max, ref step_opt) => {
                write!(f, "[{min}:")?;
                if step_opt.is_some() {
                    write!(f, "{}:", step_opt.unwrap())?;
                }
                write!(f, "{max}]")
            }
            SdpAttributeImageAttrXyRange::DiscreteValues(ref values) => {
                write!(f, "{}", imageattr_discrete_value_list_to_string(values))
            }
        }
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributeImageAttrSRange {
    Range(f32, f32), // min, max
    DiscreteValues(Vec<f32>),
}

impl fmt::Display for SdpAttributeImageAttrSRange {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SdpAttributeImageAttrSRange::Range(min, max) => write!(f, "[{min}-{max}]"),
            SdpAttributeImageAttrSRange::DiscreteValues(values) => {
                write!(f, "{}", imageattr_discrete_value_list_to_string(values))
            }
        }
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub struct SdpAttributeImageAttrPRange {
    pub min: f32,
    pub max: f32,
}

impl fmt::Display for SdpAttributeImageAttrPRange {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "[{}-{}]", self.min, self.max)
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub struct SdpAttributeImageAttrSet {
    pub x: SdpAttributeImageAttrXyRange,
    pub y: SdpAttributeImageAttrXyRange,
    pub sar: Option<SdpAttributeImageAttrSRange>,
    pub par: Option<SdpAttributeImageAttrPRange>,
    pub q: Option<f32>,
}

impl fmt::Display for SdpAttributeImageAttrSet {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "[x={x},y={y}", x = self.x, y = self.y)?;
        write_option_string!(f, ",sar={}", self.sar)?;
        write_option_string!(f, ",par={}", self.par)?;
        write_option_string!(f, ",q={}", self.q)?;
        write!(f, "]")
    }
}

#[derive(Debug, PartialEq, Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub enum SdpAttributeImageAttrSetList {
    Sets(Vec<SdpAttributeImageAttrSet>),
    Wildcard,
}

impl fmt::Display for SdpAttributeImageAttrSetList {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeImageAttrSetList::Sets(ref sets) => sets
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<String>>()
                .join(" ")
                .fmt(f),
            SdpAttributeImageAttrSetList::Wildcard => "*".fmt(f),
        }
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeImageAttr {
    pub pt: SdpAttributePayloadType,
    pub send: SdpAttributeImageAttrSetList,
    pub recv: SdpAttributeImageAttrSetList,
}

impl fmt::Display for SdpAttributeImageAttr {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let maybe_sets_to_string = |set_list| match set_list {
            SdpAttributeImageAttrSetList::Sets(sets) => match sets.len() {
                0 => None,
                _ => Some(SdpAttributeImageAttrSetList::Sets(sets)),
            },
            x => Some(x),
        };
        self.pt.fmt(f)?;
        write_option_string!(f, " send {}", maybe_sets_to_string(self.send.clone()))?;
        write_option_string!(f, " recv {}", maybe_sets_to_string(self.recv.clone()))
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeSctpmap {
    pub port: u16,
    pub channels: u32,
}

impl fmt::Display for SdpAttributeSctpmap {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{port} webrtc-datachannel {channels}",
            port = self.port,
            channels = self.channels
        )
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttributeGroupSemantic {
    LipSynchronization,          // RFC5888
    FlowIdentification,          // RFC5888
    SingleReservationFlow,       // RFC3524
    AlternateNetworkAddressType, // RFC4091
    ForwardErrorCorrection,      // RFC5956
    DecodingDependency,          // RFC5583
    Bundle,                      // draft-ietc-mmusic-bundle
}

impl fmt::Display for SdpAttributeGroupSemantic {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeGroupSemantic::LipSynchronization => "LS",
            SdpAttributeGroupSemantic::FlowIdentification => "FID",
            SdpAttributeGroupSemantic::SingleReservationFlow => "SRF",
            SdpAttributeGroupSemantic::AlternateNetworkAddressType => "ANAT",
            SdpAttributeGroupSemantic::ForwardErrorCorrection => "FEC",
            SdpAttributeGroupSemantic::DecodingDependency => "DDP",
            SdpAttributeGroupSemantic::Bundle => "BUNDLE",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeGroup {
    pub semantics: SdpAttributeGroupSemantic,
    pub tags: Vec<String>,
}

impl fmt::Display for SdpAttributeGroup {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{}{}",
            self.semantics,
            maybe_vector_to_string!(" {}", self.tags, " ")
        )
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeMsid {
    pub id: String,
    pub appdata: Option<String>,
}

impl fmt::Display for SdpAttributeMsid {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.id.fmt(f)?;
        write_option_string!(f, " {}", self.appdata)
    }
}

#[derive(Clone, Debug)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
pub struct SdpAttributeMsidSemantic {
    pub semantic: String,
    pub msids: Vec<String>,
}

impl fmt::Display for SdpAttributeMsidSemantic {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{} ", self.semantic)?;
        match self.msids.len() {
            0 => "*".fmt(f),
            _ => self.msids.join(" ").fmt(f),
        }
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeRidParameters {
    pub max_width: u32,
    pub max_height: u32,
    pub max_fps: u32,
    pub max_fs: u32,
    pub max_br: u32,
    pub max_pps: u32,

    pub unknown: Vec<String>,
}

impl fmt::Display for SdpAttributeRidParameters {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        non_empty_string_vec![
            maybe_print_param("max-width=", self.max_width, 0),
            maybe_print_param("max-height=", self.max_height, 0),
            maybe_print_param("max-fps=", self.max_fps, 0),
            maybe_print_param("max-fs=", self.max_fs, 0),
            maybe_print_param("max-br=", self.max_br, 0),
            maybe_print_param("max-pps=", self.max_pps, 0),
            maybe_vector_to_string!("{}", self.unknown, ";")
        ]
        .join(";")
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeRid {
    pub id: String,
    pub direction: SdpSingleDirection,
    pub formats: Vec<u16>,
    pub params: SdpAttributeRidParameters,
    pub depends: Vec<String>,
}

impl fmt::Display for SdpAttributeRid {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{id} {direction}{format}",
            id = self.id,
            direction = self.direction,
            format = match non_empty_string_vec![
                maybe_vector_to_string!("pt={}", self.formats, ","),
                self.params.to_string(),
                maybe_vector_to_string!("depends={}", self.depends, ",")
            ]
            .join(";")
            .as_str()
            {
                "" => "".to_string(),
                x => format!(" {x}"),
            }
        )
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeRtpmap {
    pub payload_type: u8,
    pub codec_name: String,
    pub frequency: u32,
    pub channels: Option<u32>,
}

impl SdpAttributeRtpmap {
    pub fn new(payload_type: u8, codec_name: String, frequency: u32) -> SdpAttributeRtpmap {
        SdpAttributeRtpmap {
            payload_type,
            codec_name,
            frequency,
            channels: None,
        }
    }

    fn set_channels(&mut self, c: u32) {
        self.channels = Some(c)
    }
}

impl fmt::Display for SdpAttributeRtpmap {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{pt} {name}/{freq}",
            pt = self.payload_type,
            name = self.codec_name,
            freq = self.frequency
        )?;
        write_option_string!(f, "/{}", self.channels)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttributeSetup {
    Active,
    Actpass,
    Holdconn,
    Passive,
}

impl fmt::Display for SdpAttributeSetup {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeSetup::Active => "active",
            SdpAttributeSetup::Actpass => "actpass",
            SdpAttributeSetup::Holdconn => "holdconn",
            SdpAttributeSetup::Passive => "passive",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub struct SdpAttributeSsrc {
    pub id: u32,
    pub attribute: Option<String>,
    pub value: Option<String>,
}

impl SdpAttributeSsrc {
    pub fn new(id: u32) -> SdpAttributeSsrc {
        SdpAttributeSsrc {
            id,
            attribute: None,
            value: None,
        }
    }

    fn set_attribute(&mut self, a: &str) {
        if a.find(':').is_none() {
            self.attribute = Some(a.to_string());
        } else {
            let v: Vec<&str> = a.splitn(2, ':').collect();
            self.attribute = Some(v[0].to_string());
            self.value = Some(v[1].to_string());
        }
    }
}

impl fmt::Display for SdpAttributeSsrc {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.id.fmt(f)?;
        write_option_string!(f, " {}", self.attribute)?;
        write_option_string!(f, ":{}", self.value)
    }
}

impl AnonymizingClone for SdpAttributeSsrc {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        Self {
            id: self.id,
            attribute: self.attribute.clone(),
            value: self.attribute.as_ref().and_then(|attribute| {
                match (attribute.to_lowercase().as_str(), &self.value) {
                    ("cname", Some(ref cname)) => Some(anon.mask_cname(cname.as_str())),
                    (_, Some(_)) => self.value.clone(),
                    (_, None) => None,
                }
            }),
        }
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpSsrcGroupSemantic {
    Duplication,              // RFC7104
    FlowIdentification,       // RFC5576
    ForwardErrorCorrection,   // RFC5576
    ForwardErrorCorrectionFr, // RFC5956
    Sim,                      // not registered with IANA, but used in hangouts
}

impl fmt::Display for SdpSsrcGroupSemantic {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpSsrcGroupSemantic::Duplication => "DUP",
            SdpSsrcGroupSemantic::FlowIdentification => "FID",
            SdpSsrcGroupSemantic::ForwardErrorCorrection => "FEC",
            SdpSsrcGroupSemantic::ForwardErrorCorrectionFr => "FEC-FR",
            SdpSsrcGroupSemantic::Sim => "SIM",
        }
        .fmt(f)
    }
}

#[derive(Clone)]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "enhanced_debug", derive(Debug))]
pub enum SdpAttribute {
    BundleOnly,
    Candidate(SdpAttributeCandidate),
    DtlsMessage(SdpAttributeDtlsMessage),
    EndOfCandidates,
    Extmap(SdpAttributeExtmap),
    ExtmapAllowMixed,
    Fingerprint(SdpAttributeFingerprint),
    Fmtp(SdpAttributeFmtp),
    FrameRate(f64),
    Group(SdpAttributeGroup),
    IceLite,
    IceMismatch,
    IceOptions(Vec<String>),
    IcePacing(u64),
    IcePwd(String),
    IceUfrag(String),
    Identity(String),
    ImageAttr(SdpAttributeImageAttr),
    Inactive,
    Label(String),
    MaxMessageSize(u64),
    MaxPtime(u64),
    Mid(String),
    Msid(SdpAttributeMsid),
    MsidSemantic(SdpAttributeMsidSemantic),
    Ptime(u64),
    Rid(SdpAttributeRid),
    Recvonly,
    RemoteCandidate(SdpAttributeRemoteCandidate),
    Rtpmap(SdpAttributeRtpmap),
    Rtcp(SdpAttributeRtcp),
    Rtcpfb(SdpAttributeRtcpFb),
    RtcpMux,
    RtcpMuxOnly, // RFC8858
    RtcpRsize,
    Sctpmap(SdpAttributeSctpmap),
    SctpPort(u64),
    Sendonly,
    Sendrecv,
    Setup(SdpAttributeSetup),
    Simulcast(SdpAttributeSimulcast),
    Ssrc(SdpAttributeSsrc),
    SsrcGroup(SdpSsrcGroupSemantic, Vec<SdpAttributeSsrc>),
}

impl SdpAttribute {
    pub fn allowed_at_session_level(&self) -> bool {
        match *self {
            SdpAttribute::BundleOnly
            | SdpAttribute::Candidate(..)
            | SdpAttribute::Fmtp(..)
            | SdpAttribute::FrameRate(..)
            | SdpAttribute::IceMismatch
            | SdpAttribute::ImageAttr(..)
            | SdpAttribute::Label(..)
            | SdpAttribute::MaxMessageSize(..)
            | SdpAttribute::MaxPtime(..)
            | SdpAttribute::Mid(..)
            | SdpAttribute::Msid(..)
            | SdpAttribute::Ptime(..)
            | SdpAttribute::Rid(..)
            | SdpAttribute::RemoteCandidate(..)
            | SdpAttribute::Rtpmap(..)
            | SdpAttribute::Rtcp(..)
            | SdpAttribute::Rtcpfb(..)
            | SdpAttribute::RtcpMux
            | SdpAttribute::RtcpMuxOnly
            | SdpAttribute::RtcpRsize
            | SdpAttribute::Sctpmap(..)
            | SdpAttribute::SctpPort(..)
            | SdpAttribute::Simulcast(..)
            | SdpAttribute::Ssrc(..)
            | SdpAttribute::SsrcGroup(..) => false,

            SdpAttribute::DtlsMessage { .. }
            | SdpAttribute::EndOfCandidates
            | SdpAttribute::Extmap(..)
            | SdpAttribute::ExtmapAllowMixed
            | SdpAttribute::Fingerprint(..)
            | SdpAttribute::Group(..)
            | SdpAttribute::IceLite
            | SdpAttribute::IceOptions(..)
            | SdpAttribute::IcePacing(..)
            | SdpAttribute::IcePwd(..)
            | SdpAttribute::IceUfrag(..)
            | SdpAttribute::Identity(..)
            | SdpAttribute::Inactive
            | SdpAttribute::MsidSemantic(..)
            | SdpAttribute::Recvonly
            | SdpAttribute::Sendonly
            | SdpAttribute::Sendrecv
            | SdpAttribute::Setup(..) => true,
        }
    }

    pub fn allowed_at_media_level(&self) -> bool {
        match *self {
            SdpAttribute::DtlsMessage { .. }
            | SdpAttribute::Group(..)
            | SdpAttribute::IceLite
            | SdpAttribute::IcePacing(..)
            | SdpAttribute::Identity(..)
            | SdpAttribute::MsidSemantic(..) => false,

            SdpAttribute::BundleOnly
            | SdpAttribute::Candidate(..)
            | SdpAttribute::EndOfCandidates
            | SdpAttribute::Extmap(..)
            | SdpAttribute::ExtmapAllowMixed
            | SdpAttribute::Fingerprint(..)
            | SdpAttribute::Fmtp(..)
            | SdpAttribute::FrameRate(..)
            | SdpAttribute::IceMismatch
            | SdpAttribute::IceOptions(..)
            | SdpAttribute::IcePwd(..)
            | SdpAttribute::IceUfrag(..)
            | SdpAttribute::ImageAttr(..)
            | SdpAttribute::Inactive
            | SdpAttribute::Label(..)
            | SdpAttribute::MaxMessageSize(..)
            | SdpAttribute::MaxPtime(..)
            | SdpAttribute::Mid(..)
            | SdpAttribute::Msid(..)
            | SdpAttribute::Ptime(..)
            | SdpAttribute::Rid(..)
            | SdpAttribute::Recvonly
            | SdpAttribute::RemoteCandidate(..)
            | SdpAttribute::Rtpmap(..)
            | SdpAttribute::Rtcp(..)
            | SdpAttribute::Rtcpfb(..)
            | SdpAttribute::RtcpMux
            | SdpAttribute::RtcpMuxOnly
            | SdpAttribute::RtcpRsize
            | SdpAttribute::Sctpmap(..)
            | SdpAttribute::SctpPort(..)
            | SdpAttribute::Sendonly
            | SdpAttribute::Sendrecv
            | SdpAttribute::Setup(..)
            | SdpAttribute::Simulcast(..)
            | SdpAttribute::Ssrc(..)
            | SdpAttribute::SsrcGroup(..) => true,
        }
    }
}

impl FromStr for SdpAttribute {
    type Err = SdpParserInternalError;

    fn from_str(line: &str) -> Result<Self, Self::Err> {
        let tokens: Vec<_> = line.splitn(2, ':').collect();
        let name = tokens[0].to_lowercase();
        let val = match tokens.get(1) {
            Some(x) => x.trim(),
            None => "",
        };
        if tokens.len() > 1 {
            match name.as_str() {
                "bundle-only" | "end-of-candidates" | "extmap-allow-mixed" | "ice-lite"
                | "ice-mismatch" | "inactive" | "recvonly" | "rtcp-mux" | "rtcp-mux-only"
                | "rtcp-rsize" | "sendonly" | "sendrecv" => {
                    return Err(SdpParserInternalError::Generic(format!(
                        "{name} attribute is not allowed to have a value",
                    )));
                }
                _ => (),
            }
        }
        match name.as_str() {
            "bundle-only" => Ok(SdpAttribute::BundleOnly),
            "dtls-message" => parse_dtls_message(val),
            "end-of-candidates" => Ok(SdpAttribute::EndOfCandidates),
            "ice-lite" => Ok(SdpAttribute::IceLite),
            "ice-mismatch" => Ok(SdpAttribute::IceMismatch),
            "extmap-allow-mixed" => Ok(SdpAttribute::ExtmapAllowMixed),
            "ice-pwd" => Ok(SdpAttribute::IcePwd(string_or_empty(val)?)),
            "ice-ufrag" => Ok(SdpAttribute::IceUfrag(string_or_empty(val)?)),
            "identity" => Ok(SdpAttribute::Identity(string_or_empty(val)?)),
            "imageattr" => parse_image_attr(val),
            "inactive" => Ok(SdpAttribute::Inactive),
            "label" => Ok(SdpAttribute::Label(string_or_empty(val)?)),
            "max-message-size" => Ok(SdpAttribute::MaxMessageSize(val.parse()?)),
            "maxptime" => Ok(SdpAttribute::MaxPtime(val.parse()?)),
            "mid" => Ok(SdpAttribute::Mid(string_or_empty(val)?)),
            "msid-semantic" => parse_msid_semantic(val),
            "ptime" => Ok(SdpAttribute::Ptime(val.parse()?)),
            "ice-pacing" => parse_ice_pacing(val),
            "rid" => parse_rid(val),
            "recvonly" => Ok(SdpAttribute::Recvonly),
            "rtcp-mux" => Ok(SdpAttribute::RtcpMux),
            "rtcp-mux-only" => Ok(SdpAttribute::RtcpMuxOnly),
            "rtcp-rsize" => Ok(SdpAttribute::RtcpRsize),
            "sendonly" => Ok(SdpAttribute::Sendonly),
            "sendrecv" => Ok(SdpAttribute::Sendrecv),
            "ssrc-group" => parse_ssrc_group(val),
            "sctp-port" => parse_sctp_port(val),
            "candidate" => parse_candidate(val),
            "extmap" => parse_extmap(val),
            "fingerprint" => parse_fingerprint(val),
            "fmtp" => parse_fmtp(val),
            "framerate" => parse_framerate(val),
            "group" => parse_group(val),
            "ice-options" => parse_ice_options(val),
            "msid" => parse_msid(val),
            "remote-candidates" => parse_remote_candidates(val),
            "rtpmap" => parse_rtpmap(val),
            "rtcp" => parse_rtcp(val),
            "rtcp-fb" => parse_rtcp_fb(val),
            "sctpmap" => parse_sctpmap(val),
            "setup" => parse_setup(val),
            "simulcast" => parse_simulcast(val),
            "ssrc" => parse_ssrc(val),
            _ => Err(SdpParserInternalError::Unsupported(format!(
                "Unknown attribute type {name}",
            ))),
        }
    }
}

impl fmt::Display for SdpAttribute {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let attr_type_name = SdpAttributeType::from(self).to_string();
        let attr_to_string = |attr_str: String| {
            if attr_str.is_empty() {
                "".to_string()
            } else {
                attr_type_name + ":" + &attr_str
            }
        };
        match *self {
            SdpAttribute::BundleOnly => SdpAttributeType::BundleOnly.to_string(),
            SdpAttribute::Candidate(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::DtlsMessage(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::EndOfCandidates => SdpAttributeType::EndOfCandidates.to_string(),
            SdpAttribute::Extmap(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::ExtmapAllowMixed => SdpAttributeType::ExtmapAllowMixed.to_string(),
            SdpAttribute::Fingerprint(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Fmtp(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::FrameRate(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Group(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::IceLite => SdpAttributeType::IceLite.to_string(),
            SdpAttribute::IceMismatch => SdpAttributeType::IceMismatch.to_string(),
            SdpAttribute::IceOptions(ref a) => attr_to_string(a.join(" ")),
            SdpAttribute::IcePacing(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::IcePwd(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::IceUfrag(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Identity(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::ImageAttr(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Inactive => SdpAttributeType::Inactive.to_string(),
            SdpAttribute::Label(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::MaxMessageSize(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::MaxPtime(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Mid(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Msid(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::MsidSemantic(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Ptime(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Rid(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Recvonly => SdpAttributeType::Recvonly.to_string(),
            SdpAttribute::RemoteCandidate(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Rtpmap(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Rtcp(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Rtcpfb(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::RtcpMux => SdpAttributeType::RtcpMux.to_string(),
            SdpAttribute::RtcpMuxOnly => SdpAttributeType::RtcpMuxOnly.to_string(),
            SdpAttribute::RtcpRsize => SdpAttributeType::RtcpRsize.to_string(),
            SdpAttribute::Sctpmap(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::SctpPort(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Sendonly => SdpAttributeType::Sendonly.to_string(),
            SdpAttribute::Sendrecv => SdpAttributeType::Sendrecv.to_string(),
            SdpAttribute::Setup(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Simulcast(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::Ssrc(ref a) => attr_to_string(a.to_string()),
            SdpAttribute::SsrcGroup(ref a, ref ssrcs) => {
                let stringified_ssrcs: Vec<String> =
                    ssrcs.iter().map(|ssrc| ssrc.to_string()).collect();
                attr_to_string(a.to_string()) + " " + &stringified_ssrcs.join(" ")
            }
        }
        .fmt(f)
    }
}

impl AnonymizingClone for SdpAttribute {
    fn masked_clone(&self, anon: &mut StatefulSdpAnonymizer) -> Self {
        match self {
            SdpAttribute::Candidate(i) => SdpAttribute::Candidate(i.masked_clone(anon)),
            SdpAttribute::Fingerprint(i) => SdpAttribute::Fingerprint(i.masked_clone(anon)),
            SdpAttribute::IcePwd(i) => SdpAttribute::IcePwd(anon.mask_ice_password(i)),
            SdpAttribute::IceUfrag(i) => SdpAttribute::IceUfrag(anon.mask_ice_user(i)),
            SdpAttribute::RemoteCandidate(i) => SdpAttribute::RemoteCandidate(i.masked_clone(anon)),
            SdpAttribute::Ssrc(i) => SdpAttribute::Ssrc(i.masked_clone(anon)),
            _ => self.clone(),
        }
    }
}

#[derive(Clone, PartialEq)]
pub enum SdpAttributeType {
    BundleOnly,
    Candidate,
    DtlsMessage,
    EndOfCandidates,
    Extmap,
    ExtmapAllowMixed,
    Fingerprint,
    Fmtp,
    FrameRate,
    Group,
    IceLite,
    IceMismatch,
    IceOptions,
    IcePacing,
    IcePwd,
    IceUfrag,
    Identity,
    ImageAttr,
    Inactive,
    Label,
    MaxMessageSize,
    MaxPtime,
    Mid,
    Msid,
    MsidSemantic,
    Ptime,
    Rid,
    Recvonly,
    RemoteCandidate,
    Rtpmap,
    Rtcp,
    Rtcpfb,
    RtcpMux,
    RtcpMuxOnly,
    RtcpRsize,
    Sctpmap,
    SctpPort,
    Sendonly,
    Sendrecv,
    Setup,
    Simulcast,
    Ssrc,
    SsrcGroup,
}

impl From<&SdpAttribute> for SdpAttributeType {
    fn from(other: &SdpAttribute) -> Self {
        match *other {
            SdpAttribute::BundleOnly => SdpAttributeType::BundleOnly,
            SdpAttribute::Candidate { .. } => SdpAttributeType::Candidate,
            SdpAttribute::DtlsMessage { .. } => SdpAttributeType::DtlsMessage,
            SdpAttribute::EndOfCandidates => SdpAttributeType::EndOfCandidates,
            SdpAttribute::Extmap { .. } => SdpAttributeType::Extmap,
            SdpAttribute::ExtmapAllowMixed => SdpAttributeType::ExtmapAllowMixed,
            SdpAttribute::Fingerprint { .. } => SdpAttributeType::Fingerprint,
            SdpAttribute::Fmtp { .. } => SdpAttributeType::Fmtp,
            SdpAttribute::FrameRate { .. } => SdpAttributeType::FrameRate,
            SdpAttribute::Group { .. } => SdpAttributeType::Group,
            SdpAttribute::IceLite => SdpAttributeType::IceLite,
            SdpAttribute::IceMismatch => SdpAttributeType::IceMismatch,
            SdpAttribute::IceOptions { .. } => SdpAttributeType::IceOptions,
            SdpAttribute::IcePacing { .. } => SdpAttributeType::IcePacing,
            SdpAttribute::IcePwd { .. } => SdpAttributeType::IcePwd,
            SdpAttribute::IceUfrag { .. } => SdpAttributeType::IceUfrag,
            SdpAttribute::Identity { .. } => SdpAttributeType::Identity,
            SdpAttribute::ImageAttr { .. } => SdpAttributeType::ImageAttr,
            SdpAttribute::Inactive => SdpAttributeType::Inactive,
            SdpAttribute::Label { .. } => SdpAttributeType::Label,
            SdpAttribute::MaxMessageSize { .. } => SdpAttributeType::MaxMessageSize,
            SdpAttribute::MaxPtime { .. } => SdpAttributeType::MaxPtime,
            SdpAttribute::Mid { .. } => SdpAttributeType::Mid,
            SdpAttribute::Msid { .. } => SdpAttributeType::Msid,
            SdpAttribute::MsidSemantic { .. } => SdpAttributeType::MsidSemantic,
            SdpAttribute::Ptime { .. } => SdpAttributeType::Ptime,
            SdpAttribute::Rid { .. } => SdpAttributeType::Rid,
            SdpAttribute::Recvonly => SdpAttributeType::Recvonly,
            SdpAttribute::RemoteCandidate { .. } => SdpAttributeType::RemoteCandidate,
            SdpAttribute::Rtcp { .. } => SdpAttributeType::Rtcp,
            SdpAttribute::Rtcpfb { .. } => SdpAttributeType::Rtcpfb,
            SdpAttribute::RtcpMux => SdpAttributeType::RtcpMux,
            SdpAttribute::RtcpMuxOnly => SdpAttributeType::RtcpMuxOnly,
            SdpAttribute::RtcpRsize => SdpAttributeType::RtcpRsize,
            SdpAttribute::Rtpmap { .. } => SdpAttributeType::Rtpmap,
            SdpAttribute::Sctpmap { .. } => SdpAttributeType::Sctpmap,
            SdpAttribute::SctpPort { .. } => SdpAttributeType::SctpPort,
            SdpAttribute::Sendonly => SdpAttributeType::Sendonly,
            SdpAttribute::Sendrecv => SdpAttributeType::Sendrecv,
            SdpAttribute::Setup { .. } => SdpAttributeType::Setup,
            SdpAttribute::Simulcast { .. } => SdpAttributeType::Simulcast,
            SdpAttribute::Ssrc { .. } => SdpAttributeType::Ssrc,
            SdpAttribute::SsrcGroup { .. } => SdpAttributeType::SsrcGroup,
        }
    }
}

impl fmt::Display for SdpAttributeType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            SdpAttributeType::BundleOnly => "bundle-only",
            SdpAttributeType::Candidate => "candidate",
            SdpAttributeType::DtlsMessage => "dtls-message",
            SdpAttributeType::EndOfCandidates => "end-of-candidates",
            SdpAttributeType::Extmap => "extmap",
            SdpAttributeType::ExtmapAllowMixed => "extmap-allow-mixed",
            SdpAttributeType::Fingerprint => "fingerprint",
            SdpAttributeType::Fmtp => "fmtp",
            SdpAttributeType::FrameRate => "framerate",
            SdpAttributeType::Group => "group",
            SdpAttributeType::IceLite => "ice-lite",
            SdpAttributeType::IceMismatch => "ice-mismatch",
            SdpAttributeType::IceOptions => "ice-options",
            SdpAttributeType::IcePacing => "ice-pacing",
            SdpAttributeType::IcePwd => "ice-pwd",
            SdpAttributeType::IceUfrag => "ice-ufrag",
            SdpAttributeType::Identity => "identity",
            SdpAttributeType::ImageAttr => "imageattr",
            SdpAttributeType::Inactive => "inactive",
            SdpAttributeType::Label => "label",
            SdpAttributeType::MaxMessageSize => "max-message-size",
            SdpAttributeType::MaxPtime => "maxptime",
            SdpAttributeType::Mid => "mid",
            SdpAttributeType::Msid => "msid",
            SdpAttributeType::MsidSemantic => "msid-semantic",
            SdpAttributeType::Ptime => "ptime",
            SdpAttributeType::Rid => "rid",
            SdpAttributeType::Recvonly => "recvonly",
            SdpAttributeType::RemoteCandidate => "remote-candidates",
            SdpAttributeType::Rtpmap => "rtpmap",
            SdpAttributeType::Rtcp => "rtcp",
            SdpAttributeType::Rtcpfb => "rtcp-fb",
            SdpAttributeType::RtcpMux => "rtcp-mux",
            SdpAttributeType::RtcpMuxOnly => "rtcp-mux-only",
            SdpAttributeType::RtcpRsize => "rtcp-rsize",
            SdpAttributeType::Sctpmap => "sctpmap",
            SdpAttributeType::SctpPort => "sctp-port",
            SdpAttributeType::Sendonly => "sendonly",
            SdpAttributeType::Sendrecv => "sendrecv",
            SdpAttributeType::Setup => "setup",
            SdpAttributeType::Simulcast => "simulcast",
            SdpAttributeType::Ssrc => "ssrc",
            SdpAttributeType::SsrcGroup => "ssrc-group",
        }
        .fmt(f)
    }
}

fn string_or_empty(to_parse: &str) -> Result<String, SdpParserInternalError> {
    if to_parse.is_empty() {
        Err(SdpParserInternalError::Generic(
            "This attribute is required to have a value".to_string(),
        ))
    } else {
        Ok(to_parse.to_string())
    }
}

fn parse_payload_type(to_parse: &str) -> Result<SdpAttributePayloadType, SdpParserInternalError> {
    Ok(match to_parse {
        "*" => SdpAttributePayloadType::Wildcard,
        _ => SdpAttributePayloadType::PayloadType(to_parse.parse::<u8>()?),
    })
}

fn parse_single_direction(to_parse: &str) -> Result<SdpSingleDirection, SdpParserInternalError> {
    match to_parse {
        "send" => Ok(SdpSingleDirection::Send),
        "recv" => Ok(SdpSingleDirection::Recv),
        x => Err(SdpParserInternalError::Generic(format!(
            "Unknown direction description found: '{x:}'"
        ))),
    }
}

///////////////////////////////////////////////////////////////////////////
// a=ssrc-group, RFC5576
//-------------------------------------------------------------------------
// a=ssrc-group:<semantics> <ssrc-id> ...
fn parse_ssrc_group(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.split_whitespace();
    let semantics = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Ssrc group attribute is missing semantics".to_string(),
            ));
        }
        Some(x) => match x.to_uppercase().as_ref() {
            "DUP" => SdpSsrcGroupSemantic::Duplication,
            "FID" => SdpSsrcGroupSemantic::FlowIdentification,
            "FEC" => SdpSsrcGroupSemantic::ForwardErrorCorrection,
            "FEC-FR" => SdpSsrcGroupSemantic::ForwardErrorCorrectionFr,
            "SIM" => SdpSsrcGroupSemantic::Sim,
            unknown => {
                return Err(SdpParserInternalError::Unsupported(format!(
                    "Unknown ssrc semantic '{unknown:?}' found"
                )));
            }
        },
    };

    let mut ssrcs = Vec::new();
    for token in tokens {
        match parse_ssrc(token) {
            Ok(SdpAttribute::Ssrc(ssrc)) => {
                ssrcs.push(ssrc);
            }
            Err(err) => {
                return Err(err);
            }
            _ => unreachable!(),
        }
    }

    if ssrcs.is_empty() {
        return Err(SdpParserInternalError::Generic(
            "Ssrc group must contain at least one ssrc".to_string(),
        ));
    }

    Ok(SdpAttribute::SsrcGroup(semantics, ssrcs))
}

///////////////////////////////////////////////////////////////////////////
// a=sctp-port, draft-ietf-mmusic-sctp-sdp-26#section-15.2.1
//-------------------------------------------------------------------------
// no ABNF given
fn parse_sctp_port(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let port = to_parse.parse()?;
    if port > 65535 {
        return Err(SdpParserInternalError::Generic(format!(
            "Sctpport port {port} can only be a bit 16bit number"
        )));
    }
    Ok(SdpAttribute::SctpPort(port))
}

///////////////////////////////////////////////////////////////////////////
// a=candidate, RFC5245
//-------------------------------------------------------------------------
//
// candidate-attribute   = "candidate" ":" foundation SP component-id SP
//                          transport SP
//                          priority SP
//                          connection-address SP     ;from RFC 4566
//                          port         ;port from RFC 4566
//                          SP cand-type
//                          [SP rel-addr]
//                          [SP rel-port]
//                          *(SP extension-att-name SP
//                               extension-att-value)
// foundation            = 1*32ice-char
// component-id          = 1*5DIGIT
// transport             = "UDP" / transport-extension
// transport-extension   = token              ; from RFC 3261
// priority              = 1*10DIGIT
// cand-type             = "typ" SP candidate-types
// candidate-types       = "host" / "srflx" / "prflx" / "relay" / token
// rel-addr              = "raddr" SP connection-address
// rel-port              = "rport" SP port
// extension-att-name    = byte-string    ;from RFC 4566
// extension-att-value   = byte-string
// ice-char              = ALPHA / DIGIT / "+" / "/"
fn parse_candidate(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.split_whitespace().collect();
    if tokens.len() < 8 {
        return Err(SdpParserInternalError::Generic(
            "Candidate needs to have minimum eigth tokens".to_string(),
        ));
    }
    let component = tokens[1].parse::<u32>()?;
    let transport = match tokens[2].to_lowercase().as_ref() {
        "udp" => SdpAttributeCandidateTransport::Udp,
        "tcp" => SdpAttributeCandidateTransport::Tcp,
        _ => {
            return Err(SdpParserInternalError::Generic(
                "Unknonw candidate transport value".to_string(),
            ));
        }
    };
    let priority = tokens[3].parse::<u64>()?;
    let address = Address::from_str(tokens[4])?;
    let port = tokens[5].parse::<u32>()?;
    if port > 65535 {
        return Err(SdpParserInternalError::Generic(
            "ICE candidate port can only be a bit 16bit number".to_string(),
        ));
    }
    match tokens[6].to_lowercase().as_ref() {
        "typ" => (),
        _ => {
            return Err(SdpParserInternalError::Generic(
                "Candidate attribute token must be 'typ'".to_string(),
            ));
        }
    };
    let cand_type = match tokens[7].to_lowercase().as_ref() {
        "host" => SdpAttributeCandidateType::Host,
        "srflx" => SdpAttributeCandidateType::Srflx,
        "prflx" => SdpAttributeCandidateType::Prflx,
        "relay" => SdpAttributeCandidateType::Relay,
        _ => {
            return Err(SdpParserInternalError::Generic(
                "Unknow candidate type value".to_string(),
            ));
        }
    };
    let mut cand = SdpAttributeCandidate::new(
        tokens[0].to_string(),
        component,
        transport,
        priority,
        address,
        port,
        cand_type,
    );
    if tokens.len() > 8 {
        let mut index = 8;
        while tokens.len() > index + 1 {
            match tokens[index].to_lowercase().as_ref() {
                "generation" => {
                    let generation = tokens[index + 1].parse::<u32>()?;
                    cand.set_generation(generation);
                    index += 2;
                }
                "network-cost" => {
                    let cost = tokens[index + 1].parse::<u32>()?;
                    cand.set_network_cost(cost);
                    index += 2;
                }
                "raddr" => {
                    let addr = parse_unicast_address(tokens[index + 1])?;
                    cand.set_remote_address(addr);
                    index += 2;
                }
                "rport" => {
                    let port = tokens[index + 1].parse::<u32>()?;
                    if port > 65535 {
                        return Err(SdpParserInternalError::Generic(
                            "ICE candidate rport can only be a bit 16bit number".to_string(),
                        ));
                    }
                    cand.set_remote_port(port);
                    index += 2;
                }
                "tcptype" => {
                    cand.set_tcp_type(match tokens[index + 1].to_lowercase().as_ref() {
                        "active" => SdpAttributeCandidateTcpType::Active,
                        "passive" => SdpAttributeCandidateTcpType::Passive,
                        "so" => SdpAttributeCandidateTcpType::Simultaneous,
                        _ => {
                            return Err(SdpParserInternalError::Generic(
                                "Unknown tcptype value in candidate line".to_string(),
                            ));
                        }
                    });
                    index += 2;
                }
                "ufrag" => {
                    let ufrag = tokens[index + 1];
                    cand.set_ufrag(ufrag.to_string());
                    index += 2;
                }
                _ => {
                    let name = tokens[index].to_string();
                    let value = tokens[index + 1].to_string();
                    cand.add_unknown_extension(name, value);
                    index += 2;
                }
            };
        }
        if tokens.len() > index {
            return Err(SdpParserInternalError::Unsupported(
                "Ice candidate extension name without value".to_string(),
            ));
        }
    }
    Ok(SdpAttribute::Candidate(cand))
}

///////////////////////////////////////////////////////////////////////////
// a=dtls-message, draft-rescorla-dtls-in-sdp
//-------------------------------------------------------------------------
//   attribute               =/   dtls-message-attribute
//
//   dtls-message-attribute  =    "dtls-message" ":" role SP value
//
//   role                    =    "client" / "server"
//
//   value                   =    1*(ALPHA / DIGIT / "+" / "/" / "=" )
//                                ; base64 encoded message
fn parse_dtls_message(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.split(' ').collect();

    if tokens.len() != 2 {
        return Err(SdpParserInternalError::Generic(
            "dtls-message must have a role token and a value token.".to_string(),
        ));
    }

    Ok(SdpAttribute::DtlsMessage(match tokens[0] {
        "client" => SdpAttributeDtlsMessage::Client(tokens[1].to_string()),
        "server" => SdpAttributeDtlsMessage::Server(tokens[1].to_string()),
        e => {
            return Err(SdpParserInternalError::Generic(format!(
                "dtls-message has unknown role token '{e}'"
            )));
        }
    }))
}

// Returns true if valid byte-string as defined by RFC 4566
// https://tools.ietf.org/html/rfc4566
fn valid_byte_string(input: &str) -> bool {
    !(input.contains(0x00 as char) || input.contains(0x0A as char) || input.contains(0x0D as char))
}

///////////////////////////////////////////////////////////////////////////
// a=extmap, RFC5285
//-------------------------------------------------------------------------
// RFC5285
//        extmap = mapentry SP extensionname [SP extensionattributes]
//
//        extensionname = URI
//
//        direction = "sendonly" / "recvonly" / "sendrecv" / "inactive"
//
//        mapentry = "extmap:" 1*5DIGIT ["/" direction]
//
//        extensionattributes = byte-string
//
//        URI = <Defined in RFC 3986>
//
//        byte-string = <Defined in RFC 4566>
//
//        SP = <Defined in RFC 5234>
//
//        DIGIT = <Defined in RFC 5234>
fn parse_extmap(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.split_whitespace().collect();
    if tokens.len() < 2 {
        return Err(SdpParserInternalError::Generic(
            "Extmap needs to have at least two tokens".to_string(),
        ));
    }
    let id: u16;
    let mut direction: Option<SdpAttributeDirection> = None;
    if tokens[0].find('/').is_none() {
        id = tokens[0].parse::<u16>()?;
    } else {
        let id_dir: Vec<&str> = tokens[0].splitn(2, '/').collect();
        id = id_dir[0].parse::<u16>()?;
        direction = Some(match id_dir[1].to_lowercase().as_ref() {
            "recvonly" => SdpAttributeDirection::Recvonly,
            "sendonly" => SdpAttributeDirection::Sendonly,
            "sendrecv" => SdpAttributeDirection::Sendrecv,
            _ => {
                return Err(SdpParserInternalError::Generic(
                    "Unsupported direction in extmap value".to_string(),
                ));
            }
        })
    }
    // Consider replacing to_parse.split_whitespace() above with splitn on space. Would we want the pattern to split on any amout of any kind of whitespace?
    let extension_attributes = if tokens.len() == 2 {
        None
    } else {
        let ext_string: String = tokens[2..].join(" ");
        if !valid_byte_string(&ext_string) {
            return Err(SdpParserInternalError::Generic(
                "Illegal character in extmap extension attributes".to_string(),
            ));
        }
        Some(ext_string)
    };
    Ok(SdpAttribute::Extmap(SdpAttributeExtmap {
        id,
        direction,
        url: tokens[1].to_string(),
        extension_attributes,
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=fingerprint, RFC4572
//-------------------------------------------------------------------------
//   fingerprint-attribute  =  "fingerprint" ":" hash-func SP fingerprint
//
//   hash-func              =  "sha-1" / "sha-224" / "sha-256" /
//                             "sha-384" / "sha-512" /
//                             "md5" / "md2" / token
//                             ; Additional hash functions can only come
//                             ; from updates to RFC 3279
//
//   fingerprint            =  2UHEX *(":" 2UHEX)
//                             ; Each byte in upper-case hex, separated
//                             ; by colons.
//
//   UHEX                   =  DIGIT / %x41-46 ; A-F uppercase
fn parse_fingerprint(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.split_whitespace().collect();
    if tokens.len() != 2 {
        return Err(SdpParserInternalError::Generic(
            "Fingerprint needs to have two tokens".to_string(),
        ));
    }

    let hash_algorithm = SdpAttributeFingerprintHashType::try_from_name(tokens[0])?;
    let bytes = hash_algorithm.parse_octets(tokens[1])?;
    let fingerprint = SdpAttributeFingerprint::try_from((hash_algorithm, bytes))?;
    Ok(SdpAttribute::Fingerprint(fingerprint))
}

///////////////////////////////////////////////////////////////////////////
// a=fmtp, RFC4566, RFC5576
//-------------------------------------------------------------------------
//       a=fmtp:<format> <format specific parameters>
fn parse_fmtp(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.splitn(2, ' ').collect();

    // Support space seperated parameter blocks
    if tokens.len() != 2 {
        return Err(SdpParserInternalError::Unsupported(
            "Fmtp attributes require a payload type and a parameter block.".to_string(),
        ));
    }

    let payload_token = tokens[0];

    // Default initiliaze SdpAttributeFmtpParameters
    let mut parameters = SdpAttributeFmtpParameters {
        packetization_mode: 0,
        level_asymmetry_allowed: false,
        profile_level_id: 0x0042_000A,
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
        unknown_tokens: Vec::new(),
    };

    for parameter_token in tokens[1..].iter() {
        if parameter_token.contains('=') {
            // Permit Leading/Trailing/Inner ';' by filtering out empty splits
            let parameter_tokens: Vec<&str> = parameter_token
                .split(';')
                .filter(|token| !token.is_empty())
                .collect();
            for parameter_token in parameter_tokens.iter() {
                let name_value_pair: Vec<&str> = parameter_token.splitn(2, '=').collect();
                if name_value_pair.len() != 2 {
                    return Err(SdpParserInternalError::Generic(
                        "A fmtp parameter must be either a telephone event, a parameter list or a red codec list"
                            .to_string(),
                    ));
                }

                let parse_bool =
                    |val: &str, param_name: &str| -> Result<bool, SdpParserInternalError> {
                        match val.parse::<u8>()? {
                            0 => Ok(false),
                            1 => Ok(true),
                            _ => Err(SdpParserInternalError::Generic(format!(
                                "The fmtp parameter '{param_name:}' must be 0 or 1"
                            ))),
                        }
                    };

                let parameter_name = name_value_pair[0];
                let parameter_val = name_value_pair[1];

                match parameter_name.to_uppercase().as_str() {
                    // H264
                    "PROFILE-LEVEL-ID" => parameters.profile_level_id = match u32::from_str_radix(
                        parameter_val,
                        16,
                    )? {
                        x @ 0..=0x00ff_ffff => x,
                        _ => return Err(SdpParserInternalError::Generic(
                            "The fmtp parameter 'profile-level-id' must be in range [0,0xffffff]"
                                .to_string(),
                        )),
                    },
                    "PACKETIZATION-MODE" => {
                        parameters.packetization_mode = match parameter_val.parse::<u32>()? {
                            x @ 0..=2 => x,
                            _ => {
                                return Err(SdpParserInternalError::Generic(
                                    "The fmtp parameter 'packetization-mode' must be 0,1 or 2"
                                        .to_string(),
                                ));
                            }
                        }
                    }
                    "LEVEL-ASYMMETRY-ALLOWED" => {
                        parameters.level_asymmetry_allowed =
                            parse_bool(parameter_val, "level-asymmetry-allowed")?
                    }
                    "MAX-MBPS" => parameters.max_mbps = parameter_val.parse::<u32>()?,
                    "MAX-FS" => parameters.max_fs = parameter_val.parse::<u32>()?,
                    "MAX-CPB" => parameters.max_cpb = parameter_val.parse::<u32>()?,
                    "MAX-DPB" => parameters.max_dpb = parameter_val.parse::<u32>()?,
                    "MAX-BR" => parameters.max_br = parameter_val.parse::<u32>()?,

                    // VP8 and VP9
                    "MAX-FR" => parameters.max_fr = parameter_val.parse::<u32>()?,

                    // AV1
                    "PROFILE" => {
                        parameters.profile = match parameter_val.parse::<u8>()? {
                            x @ 0..=2 => Some(x),
                            _ => {
                                return Err(SdpParserInternalError::Generic(
                                    "The fmtp parameter 'profile' must be in the range [0,2]"
                                        .to_string(),
                                ));
                            }
                        }
                    }
                    "LEVEL-IDX" => {
                        parameters.level_idx = match parameter_val.parse::<u8>()? {
                            x @ 0..=31 => Some(x),
                            _ => {
                                return Err(SdpParserInternalError::Generic(
                                    "The fmtp parameter 'level-idx' must be in the range [0,31]"
                                        .to_string(),
                                ));
                            }
                        }
                    }
                    "TIER" => {
                        parameters.tier = match parameter_val.parse::<u8>()? {
                            x @ 0..=1 => Some(x),
                            _ => {
                                return Err(SdpParserInternalError::Generic(
                                    "The fmtp parameter 'tier' must be in the range [0,1]"
                                        .to_string(),
                                ));
                            }
                        }
                    }
                    //Opus https://tools.ietf.org/html/rfc7587
                    "MAXPLAYBACKRATE" => {
                        parameters.maxplaybackrate = parameter_val.parse::<u32>()?
                    }
                    "MAXAVERAGEBITRATE" => {
                        parameters.maxaveragebitrate = parameter_val.parse::<u32>()?
                    }
                    "PTIME" => parameters.ptime = parameter_val.parse::<u32>()?,
                    "MAXPTIME" => parameters.maxptime = parameter_val.parse::<u32>()?,
                    "MINPTIME" => parameters.minptime = parameter_val.parse::<u32>()?,
                    "USEDTX" => parameters.usedtx = parse_bool(parameter_val, "usedtx")?,
                    "STEREO" => parameters.stereo = parse_bool(parameter_val, "stereo")?,
                    "USEINBANDFEC" => {
                        parameters.useinbandfec = parse_bool(parameter_val, "useinbandfec")?
                    }
                    "CBR" => parameters.cbr = parse_bool(parameter_val, "cbr")?,
                    "APT" => {
                        parameters.rtx = Some(RtxFmtpParameters {
                            apt: parameter_val.parse::<u8>()?,
                            rtx_time: None,
                        })
                    }
                    "RTX-TIME" => {
                        if let Some(ref mut rtx) = parameters.rtx {
                            rtx.rtx_time = Some(parameter_val.parse::<u32>()?)
                        } else {
                            return Err(SdpParserInternalError::Generic(
                                "RTX codec must have an APT field".to_string(),
                            ));
                        }
                    }
                    _ => parameters
                        .unknown_tokens
                        .push((*parameter_token).to_string()),
                }
            }
        } else if parameter_token.contains('/') {
            let encodings: Vec<&str> = parameter_token.split('/').collect();

            for encoding in encodings {
                match encoding.parse::<u8>()? {
                    x @ 0..=128 => parameters.encodings.push(x),
                    _ => {
                        return Err(SdpParserInternalError::Generic(
                            "Red codec must be in range [0,128]".to_string(),
                        ));
                    }
                }
            }
        } else {
            // This is the case for the 'telephone-event' codec
            let dtmf_tones: Vec<&str> = parameter_token.split(',').collect();
            let mut dtmf_tone_is_ok = true;

            // This closure verifies the output of some_number_as_string.parse::<u8>().ok() like calls
            let validate_digits = |digit_option: Option<u8>| -> Option<u8> {
                match digit_option {
                    Some(x) => match x {
                        0..=100 => Some(x),
                        _ => None,
                    },
                    None => None,
                }
            };

            // This loop does some sanity checking on the passed dtmf tones
            for dtmf_tone in dtmf_tones {
                let dtmf_tone_range: Vec<&str> = dtmf_tone.splitn(2, '-').collect();

                dtmf_tone_is_ok = match dtmf_tone_range.len() {
                    // In this case the dtmf tone is a range
                    2 => {
                        match validate_digits(dtmf_tone_range[0].parse::<u8>().ok()) {
                            Some(l) => match validate_digits(dtmf_tone_range[1].parse::<u8>().ok())
                            {
                                Some(u) => {
                                    // Check that the first part of the range is smaller than the second part
                                    l < u
                                }
                                None => false,
                            },
                            None => false,
                        }
                    }
                    // In this case the dtmf tone is a single tone
                    1 => validate_digits(dtmf_tone.parse::<u8>().ok()).is_some(),
                    _ => false,
                };

                if !dtmf_tone_is_ok {
                    break;
                }
            }

            // Set the parsed dtmf tones or in case the parsing was insuccessfull, set it to the default "0-15"
            parameters.dtmf_tones = if dtmf_tone_is_ok {
                (*parameter_token).to_string()
            } else {
                "0-15".to_string()
            };
        }
    }
    Ok(SdpAttribute::Fmtp(SdpAttributeFmtp {
        payload_type: payload_token.parse::<u8>()?,
        parameters,
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=framerate, RFC4566, RFC8866
//-------------------------------------------------------------------------
//       a=framerate:<framerate-value>
fn parse_framerate(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let framerate: f64 = to_parse.parse()?;

    if framerate.is_nan() {
        return Err(SdpParserInternalError::Generic(
            "FrameRate attribute must be a number".to_string(),
        ));
    }

    if framerate.is_infinite() {
        return Err(SdpParserInternalError::Generic(
            "FrameRate attribute cannot be finite".to_string(),
        ));
    }

    if framerate == 0.0 {
        return Err(SdpParserInternalError::Generic(
            "FrameRate attribute cannot be zero".to_string(),
        ));
    }

    Ok(SdpAttribute::FrameRate(framerate))
}

///////////////////////////////////////////////////////////////////////////
// a=group, RFC5888
//-------------------------------------------------------------------------
//         group-attribute     = "a=group:" semantics
//                               *(SP identification-tag)
//         semantics           = "LS" / "FID" / semantics-extension
//         semantics-extension = token
//         identification-tag  = token
fn parse_group(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.split_whitespace();
    let semantics = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Group attribute is missing semantics token".to_string(),
            ));
        }
        Some(x) => match x.to_uppercase().as_ref() {
            "LS" => SdpAttributeGroupSemantic::LipSynchronization,
            "FID" => SdpAttributeGroupSemantic::FlowIdentification,
            "SRF" => SdpAttributeGroupSemantic::SingleReservationFlow,
            "ANAT" => SdpAttributeGroupSemantic::AlternateNetworkAddressType,
            "FEC" => SdpAttributeGroupSemantic::ForwardErrorCorrection,
            "DDP" => SdpAttributeGroupSemantic::DecodingDependency,
            "BUNDLE" => SdpAttributeGroupSemantic::Bundle,
            unknown => {
                return Err(SdpParserInternalError::Unsupported(format!(
                    "Unknown group semantic '{unknown:?}' found",
                )));
            }
        },
    };
    Ok(SdpAttribute::Group(SdpAttributeGroup {
        semantics,
        tags: tokens.map(ToString::to_string).collect(),
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=ice-options, draft-ietf-mmusic-ice-sip-sdp
//-------------------------------------------------------------------------
//  ice-options           = "ice-options:" ice-option-tag
//                           0*(SP ice-option-tag)
//  ice-option-tag        = 1*ice-char
fn parse_ice_options(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    if to_parse.is_empty() {
        return Err(SdpParserInternalError::Generic(
            "ice-options is required to have a value".to_string(),
        ));
    }
    Ok(SdpAttribute::IceOptions(
        to_parse
            .split_whitespace()
            .map(ToString::to_string)
            .collect(),
    ))
}

///////////////////////////////////////////////////////////////////////////
// a=ice-pacing, draft-ietf-mmusic-ice-sip-sdp
//-------------------------------------------------------------------------
//  ice-pacing-att            = "ice-pacing:" pacing-value
//  pacing-value              = 1*10DIGIT
fn parse_ice_pacing(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let parsed = to_parse.parse::<u64>()?;
    if parsed >= 1_00_00_00_00_00 {
        return Err(SdpParserInternalError::Generic(
            "ice-pacing value is not a 10 digit integer".to_string(),
        ));
    }
    Ok(SdpAttribute::IcePacing(parsed))
}

fn parse_imageattr_tokens(to_parse: &str, separator: char) -> Vec<String> {
    let mut tokens = Vec::new();
    let mut open_braces_counter = 0;
    let mut current_tokens = Vec::new();

    for token in to_parse.split(separator) {
        if token.contains('[') {
            open_braces_counter += 1;
        }
        if token.contains(']') {
            open_braces_counter -= 1;
        }

        current_tokens.push(token.to_string());

        if open_braces_counter == 0 {
            tokens.push(current_tokens.join(&separator.to_string()));
            current_tokens = Vec::new();
        }
    }

    tokens
}

fn parse_imagettr_braced_token(to_parse: &str) -> Option<&str> {
    if !to_parse.starts_with('[') {
        return None;
    }

    if !to_parse.ends_with(']') {
        return None;
    }

    Some(&to_parse[1..to_parse.len() - 1])
}

fn parse_image_attr_xyrange(
    to_parse: &str,
) -> Result<SdpAttributeImageAttrXyRange, SdpParserInternalError> {
    if to_parse.starts_with('[') {
        let value_tokens = parse_imagettr_braced_token(to_parse).ok_or_else(|| {
            SdpParserInternalError::Generic(
                "imageattr's xyrange has no closing tag ']'".to_string(),
            )
        })?;

        if to_parse.contains(':') {
            // Range values
            let range_tokens: Vec<&str> = value_tokens.split(':').collect();

            if range_tokens.len() == 3 {
                Ok(SdpAttributeImageAttrXyRange::Range(
                    range_tokens[0].parse::<u32>()?,
                    range_tokens[2].parse::<u32>()?,
                    Some(range_tokens[1].parse::<u32>()?),
                ))
            } else if range_tokens.len() == 2 {
                Ok(SdpAttributeImageAttrXyRange::Range(
                    range_tokens[0].parse::<u32>()?,
                    range_tokens[1].parse::<u32>()?,
                    None,
                ))
            } else {
                Err(SdpParserInternalError::Generic(
                    "imageattr's xyrange must contain 2 or 3 fields".to_string(),
                ))
            }
        } else {
            // Discrete values
            let values = value_tokens
                .split(',')
                .map(str::parse::<u32>)
                .collect::<Result<Vec<u32>, _>>()?;

            if values.len() < 2 {
                return Err(SdpParserInternalError::Generic(
                    "imageattr's discrete value list must have at least two elements".to_string(),
                ));
            }

            Ok(SdpAttributeImageAttrXyRange::DiscreteValues(values))
        }
    } else {
        Ok(SdpAttributeImageAttrXyRange::DiscreteValues(vec![
            to_parse.parse::<u32>()?
        ]))
    }
}

fn parse_image_attr_set(
    to_parse: &str,
) -> Result<SdpAttributeImageAttrSet, SdpParserInternalError> {
    let mut tokens = parse_imageattr_tokens(to_parse, ',').into_iter();

    let x_token = tokens.next().ok_or_else(|| {
        SdpParserInternalError::Generic("imageattr set is missing the 'x=' token".to_string())
    })?;
    if !x_token.starts_with("x=") {
        return Err(SdpParserInternalError::Generic(
            "The first token in an imageattr set must begin with 'x='".to_string(),
        ));
    }
    let x = parse_image_attr_xyrange(&x_token[2..])?;

    let y_token = tokens.next().ok_or_else(|| {
        SdpParserInternalError::Generic("imageattr set is missing the 'y=' token".to_string())
    })?;
    if !y_token.starts_with("y=") {
        return Err(SdpParserInternalError::Generic(
            "The second token in an imageattr set must begin with 'y='".to_string(),
        ));
    }
    let y = parse_image_attr_xyrange(&y_token[2..])?;

    let mut sar = None;
    let mut par = None;
    let mut q = None;

    let parse_ps_range = |resolution_range: &str| -> Result<(f32, f32), SdpParserInternalError> {
        let minmax_pair: Vec<&str> = resolution_range.split('-').collect();

        if minmax_pair.len() != 2 {
            return Err(SdpParserInternalError::Generic(
                "imageattr's par and sar ranges must have two components".to_string(),
            ));
        }

        let min = minmax_pair[0].parse::<f32>()?;
        let max = minmax_pair[1].parse::<f32>()?;

        if min >= max {
            return Err(SdpParserInternalError::Generic(
                "In imageattr's par and sar ranges, first must be < than the second".to_string(),
            ));
        }

        Ok((min, max))
    };

    for current_token in tokens {
        if let Some(value_token) = current_token.strip_prefix("sar=") {
            if value_token.starts_with('[') {
                let sar_values = parse_imagettr_braced_token(value_token).ok_or_else(|| {
                    SdpParserInternalError::Generic(
                        "imageattr's sar value is missing closing tag ']'".to_string(),
                    )
                })?;

                if value_token.contains('-') {
                    // Range
                    let range = parse_ps_range(sar_values)?;
                    sar = Some(SdpAttributeImageAttrSRange::Range(range.0, range.1))
                } else if value_token.contains(',') {
                    // Discrete values
                    let values = sar_values
                        .split(',')
                        .map(str::parse::<f32>)
                        .collect::<Result<Vec<f32>, _>>()?;

                    if values.len() < 2 {
                        return Err(SdpParserInternalError::Generic(
                            "imageattr's sar discrete value list must have at least two values"
                                .to_string(),
                        ));
                    }

                    // Check that all the values are ascending
                    let mut last_value = 0.0;
                    for value in &values {
                        if last_value >= *value {
                            return Err(SdpParserInternalError::Generic(
                                "imageattr's sar discrete value list must contain ascending values"
                                    .to_string(),
                            ));
                        }
                        last_value = *value;
                    }
                    sar = Some(SdpAttributeImageAttrSRange::DiscreteValues(values))
                }
            } else {
                sar = Some(SdpAttributeImageAttrSRange::DiscreteValues(vec![
                    value_token.parse::<f32>()?,
                ]))
            }
        } else if let Some(braced_value_token) = current_token.strip_prefix("par=") {
            if !braced_value_token.starts_with('[') {
                return Err(SdpParserInternalError::Generic(
                    "imageattr's par value must start with '['".to_string(),
                ));
            }

            let par_values = parse_imagettr_braced_token(braced_value_token).ok_or_else(|| {
                SdpParserInternalError::Generic(
                    "imageattr's par value must be enclosed with ']'".to_string(),
                )
            })?;
            let range = parse_ps_range(par_values)?;
            par = Some(SdpAttributeImageAttrPRange {
                min: range.0,
                max: range.1,
            })
        } else if let Some(qval) = current_token.strip_prefix("q=") {
            q = Some(qval.parse::<f32>()?);
        }
    }

    Ok(SdpAttributeImageAttrSet { x, y, sar, par, q })
}

fn parse_image_attr_set_list<I>(
    tokens: &mut iter::Peekable<I>,
) -> Result<SdpAttributeImageAttrSetList, SdpParserInternalError>
where
    I: Iterator<Item = String> + Clone,
{
    let parse_set = |set_token: &str| -> Result<SdpAttributeImageAttrSet, SdpParserInternalError> {
        parse_image_attr_set(parse_imagettr_braced_token(set_token).ok_or_else(|| {
            SdpParserInternalError::Generic("imageattr sets must be enclosed by ']'".to_string())
        })?)
    };

    match tokens
        .next()
        .ok_or_else(|| {
            SdpParserInternalError::Generic(
                "imageattr must have a parameter set after a direction token".to_string(),
            )
        })?
        .as_str()
    {
        "*" => Ok(SdpAttributeImageAttrSetList::Wildcard),
        x => {
            let mut sets = vec![parse_set(x)?];
            while let Some(set_str) = tokens.clone().peek() {
                if set_str.starts_with('[') {
                    sets.push(parse_set(&tokens.next().unwrap())?);
                } else {
                    break;
                }
            }

            Ok(SdpAttributeImageAttrSetList::Sets(sets))
        }
    }
}

///////////////////////////////////////////////////////////////////////////
// a=imageattr, RFC6236
//-------------------------------------------------------------------------
//     image-attr = "imageattr:" PT 1*2( 1*WSP ( "send" / "recv" )
//                                       1*WSP attr-list )
//     PT = 1*DIGIT / "*"
//     attr-list = ( set *(1*WSP set) ) / "*"
//       ;  WSP and DIGIT defined in [RFC5234]
//
//     set= "[" "x=" xyrange "," "y=" xyrange *( "," key-value ) "]"
//                ; x is the horizontal image size range (pixel count)
//                ; y is the vertical image size range (pixel count)
//
//     key-value = ( "sar=" srange )
//               / ( "par=" prange )
//               / ( "q=" qvalue )
//                ; Key-value MAY be extended with other keyword
//                ;  parameters.
//                ; At most, one instance each of sar, par, or q
//                ;  is allowed in a set.
//                ;
//                ; sar (sample aspect ratio) is the sample aspect ratio
//                ;  associated with the set (optional, MAY be ignored)
//                ; par (picture aspect ratio) is the allowed
//                ;  ratio between the display's x and y physical
//                ;  size (optional)
//                ; q (optional, range [0.0..1.0], default value 0.5)
//                ;  is the preference for the given set,
//                ;  a higher value means a higher preference
//
//     onetonine = "1" / "2" / "3" / "4" / "5" / "6" / "7" / "8" / "9"
//                ; Digit between 1 and 9
//     xyvalue = onetonine *5DIGIT
//                ; Digit between 1 and 9 that is
//                ; followed by 0 to 5 other digits
//     step = xyvalue
//     xyrange = ( "[" xyvalue ":" [ step ":" ] xyvalue "]" )
//                ; Range between a lower and an upper value
//                ; with an optional step, default step = 1
//                ; The rightmost occurrence of xyvalue MUST have a
//                ; higher value than the leftmost occurrence.
//             / ( "[" xyvalue 1*( "," xyvalue ) "]" )
//                ; Discrete values separated by ','
//             / ( xyvalue )
//                ; A single value
//     spvalue = ( "0" "." onetonine *3DIGIT )
//                ; Values between 0.1000 and 0.9999
//             / ( onetonine "." 1*4DIGIT )
//                ; Values between 1.0000 and 9.9999
//     srange =  ( "[" spvalue 1*( "," spvalue ) "]" )
//                ; Discrete values separated by ','.
//                ; Each occurrence of spvalue MUST be
//                ; greater than the previous occurrence.
//             / ( "[" spvalue "-" spvalue "]" )
//                ; Range between a lower and an upper level (inclusive)
//                ; The second occurrence of spvalue MUST have a higher
//                ; value than the first
//             / ( spvalue )
//                ; A single value
//
//     prange =  ( "[" spvalue "-" spvalue "]" )
//                ; Range between a lower and an upper level (inclusive)
//                ; The second occurrence of spvalue MUST have a higher
//                ; value than the first
//
//     qvalue  = ( "0" "." 1*2DIGIT )
//             / ( "1" "." 1*2("0") )
//                ; Values between 0.00 and 1.00
fn parse_image_attr(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = parse_imageattr_tokens(to_parse, ' ').into_iter().peekable();

    let pt = parse_payload_type(
        tokens
            .next()
            .ok_or_else(|| {
                SdpParserInternalError::Generic("imageattr requires a payload token".to_string())
            })?
            .as_str(),
    )?;
    let first_direction = parse_single_direction(
        tokens
            .next()
            .ok_or_else(|| {
                SdpParserInternalError::Generic(
                    "imageattr's second token must be a direction token".to_string(),
                )
            })?
            .as_str(),
    )?;

    let first_set_list = parse_image_attr_set_list(&mut tokens)?;

    let mut second_set_list = SdpAttributeImageAttrSetList::Sets(Vec::new());

    // Check if there is a second direction defined
    if let Some(direction_token) = tokens.next() {
        if parse_single_direction(direction_token.as_str())? == first_direction {
            return Err(SdpParserInternalError::Generic(
                "imageattr's second direction token must be different from the first one"
                    .to_string(),
            ));
        }

        second_set_list = parse_image_attr_set_list(&mut tokens)?;
    }

    if tokens.next().is_some() {
        return Err(SdpParserInternalError::Generic(
            "imageattr must not contain any token after the second set list".to_string(),
        ));
    }

    Ok(SdpAttribute::ImageAttr(match first_direction {
        SdpSingleDirection::Send => SdpAttributeImageAttr {
            pt,
            send: first_set_list,
            recv: second_set_list,
        },
        SdpSingleDirection::Recv => SdpAttributeImageAttr {
            pt,
            send: second_set_list,
            recv: first_set_list,
        },
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=msid, draft-ietf-mmusic-msid
//-------------------------------------------------------------------------
//   msid-attr = "msid:" identifier [ SP appdata ]
//   identifier = 1*64token-char ; see RFC 4566
//   appdata = 1*64token-char  ; see RFC 4566
fn parse_msid(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.split_whitespace();
    let id = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Msid attribute is missing msid-id token".to_string(),
            ));
        }
        Some(x) => x.to_string(),
    };
    let appdata = tokens.next().map(|x| x.to_string());
    Ok(SdpAttribute::Msid(SdpAttributeMsid { id, appdata }))
}

///////////////////////////////////////////////////////////////////////////
// a=msid-semantic, draft-ietf-mmusic-msid
//-------------------------------------------------------------------------
//   msid-semantic-attr = "msid-semantic:" msid-semantic msid-list
//   msid-semantic = token ; see RFC 4566
//   msid-list = *(" " msid-id) / " *"
fn parse_msid_semantic(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<_> = to_parse.split_whitespace().collect();
    if tokens.is_empty() {
        return Err(SdpParserInternalError::Generic(
            "Msid-semantic attribute is missing msid-semantic token".to_string(),
        ));
    }
    // TODO: Should msids be checked to ensure they are non empty?
    let semantic = SdpAttributeMsidSemantic {
        semantic: tokens[0].to_string(),
        msids: tokens[1..].iter().map(ToString::to_string).collect(),
    };
    Ok(SdpAttribute::MsidSemantic(semantic))
}

///////////////////////////////////////////////////////////////////////////
// a=rid, draft-ietf-mmusic-rid
//-------------------------------------------------------------------------
// rid-syntax        = %s"a=rid:" rid-id SP rid-dir
//                     [ rid-pt-param-list / rid-param-list ]
// rid-id            = 1*(alpha-numeric / "-" / "_")
// alpha-numeric     = < as defined in {{RFC4566}} >
// rid-dir           = %s"send" / %s"recv"
// rid-pt-param-list = SP rid-fmt-list *(";" rid-param)
// rid-param-list    = SP rid-param *(";" rid-param)
// rid-fmt-list      = %s"pt=" fmt *( "," fmt )
// fmt               = < as defined in {{RFC4566}} >
// rid-param         = rid-width-param
//                     / rid-height-param
//                     / rid-fps-param
//                     / rid-fs-param
//                     / rid-br-param
//                     / rid-pps-param
//                     / rid-bpp-param
//                     / rid-depend-param
//                     / rid-param-other
// rid-width-param   = %s"max-width" [ "=" int-param-val ]
// rid-height-param  = %s"max-height" [ "=" int-param-val ]
// rid-fps-param     = %s"max-fps" [ "=" int-param-val ]
// rid-fs-param      = %s"max-fs" [ "=" int-param-val ]
// rid-br-param      = %s"max-br" [ "=" int-param-val ]
// rid-pps-param     = %s"max-pps" [ "=" int-param-val ]
// rid-bpp-param     = %s"max-bpp" [ "=" float-param-val ]
// rid-depend-param  = %s"depend=" rid-list
// rid-param-other   = 1*(alpha-numeric / "-") [ "=" param-val ]
// rid-list          = rid-id *( "," rid-id )
// int-param-val     = 1*DIGIT
// float-param-val   = 1*DIGIT "." 1*DIGIT
// param-val         = *( %x20-58 / %x60-7E )
//                     ; Any printable character except semicolon
fn parse_rid(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.splitn(3, ' ').collect();

    if tokens.len() < 2 {
        return Err(SdpParserInternalError::Generic(
            "A rid attribute must at least have an id and a direction token.".to_string(),
        ));
    }

    // Default initilize
    let mut params = SdpAttributeRidParameters {
        max_width: 0,
        max_height: 0,
        max_fps: 0,
        max_fs: 0,
        max_br: 0,
        max_pps: 0,
        unknown: Vec::new(),
    };
    let mut formats: Vec<u16> = Vec::new();
    let mut depends: Vec<String> = Vec::new();

    if let Some(param_token) = tokens.get(2) {
        let mut parameters = param_token.split(';').peekable();

        // The 'pt' parameter must be the first parameter if present, so it
        // cannot be checked along with the other parameters below
        if let Some(maybe_fmt_parameter) = parameters.clone().peek() {
            if let Some(fmt_list) = maybe_fmt_parameter.strip_prefix("pt=") {
                for fmt in fmt_list.split(',') {
                    formats.push(fmt.trim().parse::<u16>()?);
                }
                parameters.next();
            }
        }

        for param in parameters {
            // TODO: Bug 1225877. Add support for params without '='
            let param_value_pair: Vec<&str> = param.splitn(2, '=').collect();
            if param_value_pair.len() != 2 {
                return Err(SdpParserInternalError::Generic(
                    "A rid parameter needs to be of form 'param=value'".to_string(),
                ));
            }

            match param_value_pair[0] {
                "max-width" => params.max_width = param_value_pair[1].parse::<u32>()?,
                "max-height" => params.max_height = param_value_pair[1].parse::<u32>()?,
                "max-fps" => params.max_fps = param_value_pair[1].parse::<u32>()?,
                "max-fs" => params.max_fs = param_value_pair[1].parse::<u32>()?,
                "max-br" => params.max_br = param_value_pair[1].parse::<u32>()?,
                "max-pps" => params.max_pps = param_value_pair[1].parse::<u32>()?,
                "depends" => {
                    depends.extend(param_value_pair[1].split(',').map(ToString::to_string));
                }
                _ => params.unknown.push(param.to_string()),
            }
        }
    }

    Ok(SdpAttribute::Rid(SdpAttributeRid {
        id: tokens[0].to_string(),
        direction: parse_single_direction(tokens[1])?,
        formats,
        params,
        depends,
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=remote-candiate, RFC5245
//-------------------------------------------------------------------------
//   remote-candidate-att = "remote-candidates" ":" remote-candidate
//                           0*(SP remote-candidate)
//   remote-candidate = component-ID SP connection-address SP port
fn parse_remote_candidates(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.split_whitespace();
    let component = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Remote-candidate attribute is missing component ID".to_string(),
            ));
        }
        Some(x) => x.parse::<u32>()?,
    };
    let address = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Remote-candidate attribute is missing connection address".to_string(),
            ));
        }
        Some(x) => parse_unicast_address(x)?,
    };
    let port = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Remote-candidate attribute is missing port number".to_string(),
            ));
        }
        Some(x) => x.parse::<u32>()?,
    };
    if port > 65535 {
        return Err(SdpParserInternalError::Generic(
            "Remote-candidate port can only be a bit 16bit number".to_string(),
        ));
    };
    Ok(SdpAttribute::RemoteCandidate(SdpAttributeRemoteCandidate {
        component,
        address,
        port,
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=rtpmap, RFC4566
//-------------------------------------------------------------------------
// a=rtpmap:<payload type> <encoding name>/<clock rate> [/<encoding parameters>]
fn parse_rtpmap(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.split_whitespace();
    let payload_type: u8 = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Rtpmap missing payload type".to_string(),
            ));
        }
        Some(x) => {
            let pt = x.parse::<u8>()?;
            if pt > 127 {
                return Err(SdpParserInternalError::Generic(
                    "Rtpmap payload type must be less then 127".to_string(),
                ));
            };
            pt
        }
    };
    let mut parameters = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Rtpmap missing payload type".to_string(),
            ));
        }
        Some(x) => x.split('/'),
    };
    let name = match parameters.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Rtpmap missing codec name".to_string(),
            ));
        }
        Some(x) => x.to_string(),
    };
    let frequency = match parameters.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Rtpmap missing codec name".to_string(),
            ));
        }
        Some(x) => x.parse::<u32>()?,
    };
    let mut rtpmap = SdpAttributeRtpmap::new(payload_type, name, frequency);
    if let Some(x) = parameters.next() {
        rtpmap.set_channels(x.parse::<u32>()?)
    };
    Ok(SdpAttribute::Rtpmap(rtpmap))
}

///////////////////////////////////////////////////////////////////////////
// a=rtcp, RFC3605
//-------------------------------------------------------------------------
//   rtcp-attribute =  "a=rtcp:" port  [nettype space addrtype space
//                         connection-address] CRLF
fn parse_rtcp(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.split_whitespace();
    let port = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Rtcp attribute is missing port number".to_string(),
            ));
        }
        Some(x) => x.parse::<u16>()?,
    };
    let mut rtcp = SdpAttributeRtcp::new(port);
    match tokens.next() {
        None => (),
        Some(x) => {
            parse_network_type(x)?;
            match tokens.next() {
                None => {
                    return Err(SdpParserInternalError::Generic(
                        "Rtcp attribute is missing address type token".to_string(),
                    ));
                }
                Some(x) => {
                    let addrtype = AddressType::from_str(x)?;
                    let addr = match tokens.next() {
                        None => {
                            return Err(SdpParserInternalError::Generic(
                                "Rtcp attribute is missing ip address token".to_string(),
                            ));
                        }
                        Some(x) => ExplicitlyTypedAddress::try_from((addrtype, x))?,
                    };
                    rtcp.set_addr(addr);
                }
            };
        }
    };
    Ok(SdpAttribute::Rtcp(rtcp))
}

///////////////////////////////////////////////////////////////////////////
// a=rtcp-fb, RFC4585
//-------------------------------------------------------------------------
//    rtcp-fb-syntax = "a=rtcp-fb:" rtcp-fb-pt SP rtcp-fb-val CRLF
//
//    rtcp-fb-pt         = "*"   ; wildcard: applies to all formats
//                       / fmt   ; as defined in SDP spec
//
//    rtcp-fb-val        = "ack" rtcp-fb-ack-param
//                       / "nack" rtcp-fb-nack-param
//                       / "trr-int" SP 1*DIGIT
//                       / rtcp-fb-id rtcp-fb-param
//
//    rtcp-fb-id         = 1*(alpha-numeric / "-" / "_")
//
//    rtcp-fb-param      = SP "app" [SP byte-string]
//                       / SP token [SP byte-string]
//                       / ; empty
//
//    rtcp-fb-ack-param  = SP "rpsi"
//                       / SP "app" [SP byte-string]
//                       / SP token [SP byte-string]
//                       / ; empty
//
//    rtcp-fb-nack-param = SP "pli"
//                       / SP "sli"
//                       / SP "rpsi"
//                       / SP "app" [SP byte-string]
//                       / SP token [SP byte-string]
//                       / ; empty
fn parse_rtcp_fb(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.splitn(4, ' ').collect();

    // Parse this in advance to use it later in the parameter switch
    let feedback_type = match tokens.get(1) {
        Some(x) => match *x {
            "ack" => SdpAttributeRtcpFbType::Ack,
            "ccm" => SdpAttributeRtcpFbType::Ccm,
            "nack" => SdpAttributeRtcpFbType::Nack,
            "trr-int" => SdpAttributeRtcpFbType::TrrInt,
            "goog-remb" => SdpAttributeRtcpFbType::Remb,
            "transport-cc" => SdpAttributeRtcpFbType::TransCc,
            _ => {
                return Err(SdpParserInternalError::Unsupported(format!(
                    "Unknown rtcpfb feedback type: {x:?}"
                )));
            }
        },
        None => {
            return Err(SdpParserInternalError::Generic(
                "Error parsing rtcpfb: no feedback type".to_string(),
            ));
        }
    };

    // Parse this in advance to make the initilization block below better readable
    let parameter = match feedback_type {
        SdpAttributeRtcpFbType::Ack => match tokens.get(2) {
            Some(x) => match *x {
                "rpsi" | "app" => (*x).to_string(),
                _ => {
                    return Err(SdpParserInternalError::Unsupported(format!(
                        "Unknown rtcpfb ack parameter: {x:?}"
                    )));
                }
            },
            None => {
                return Err(SdpParserInternalError::Unsupported(
                    "The rtcpfb ack feeback type needs a parameter:".to_string(),
                ));
            }
        },
        SdpAttributeRtcpFbType::Ccm => match tokens.get(2) {
            Some(x) => match *x {
                "fir" | "tmmbr" | "tstr" | "vbcm" => (*x).to_string(),
                _ => {
                    return Err(SdpParserInternalError::Unsupported(format!(
                        "Unknown rtcpfb ccm parameter: {x:?}"
                    )));
                }
            },
            None => "".to_string(),
        },
        SdpAttributeRtcpFbType::Nack => match tokens.get(2) {
            Some(x) => match *x {
                "sli" | "pli" | "rpsi" | "app" => (*x).to_string(),
                _ => {
                    return Err(SdpParserInternalError::Unsupported(format!(
                        "Unknown rtcpfb nack parameter: {x:?}"
                    )));
                }
            },
            None => "".to_string(),
        },
        SdpAttributeRtcpFbType::TrrInt => match tokens.get(2) {
            Some(x) => match x {
                _ if x.parse::<u32>().is_ok() => (*x).to_string(),
                _ => {
                    return Err(SdpParserInternalError::Generic(format!(
                        "Unknown rtcpfb trr-int parameter: {x:?}"
                    )));
                }
            },
            None => {
                return Err(SdpParserInternalError::Generic(
                    "The rtcpfb trr-int feedback type needs a parameter".to_string(),
                ));
            }
        },
        SdpAttributeRtcpFbType::Remb | SdpAttributeRtcpFbType::TransCc => match tokens.get(2) {
            Some(x) => {
                return Err(SdpParserInternalError::Unsupported(format!(
                    "Unknown rtcpfb {feedback_type} parameter: {x:?}"
                )));
            }
            None => "".to_string(),
        },
    };

    Ok(SdpAttribute::Rtcpfb(SdpAttributeRtcpFb {
        payload_type: parse_payload_type(tokens[0])?,
        feedback_type,
        parameter,
        extra: match tokens.get(3) {
            Some(x) => (*x).to_string(),
            None => "".to_string(),
        },
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=sctpmap, draft-ietf-mmusic-sctp-sdp-05
//-------------------------------------------------------------------------
//      sctpmap-attr        =  "a=sctpmap:" sctpmap-number media-subtypes
// [streams]
//      sctpmap-number      =  1*DIGIT
//      protocol            =  labelstring
//        labelstring         =  text
//        text                =  byte-string
//      streams      =  1*DIGIT
//
//  Note: this was replace in later versions of the draft by sctp-port
fn parse_sctpmap(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let tokens: Vec<&str> = to_parse.split_whitespace().collect();
    if tokens.len() != 3 {
        return Err(SdpParserInternalError::Generic(
            "Sctpmap needs to have three tokens".to_string(),
        ));
    }
    let port = tokens[0].parse::<u16>()?;
    if tokens[1].to_lowercase() != "webrtc-datachannel" {
        return Err(SdpParserInternalError::Generic(
            "Unsupported sctpmap type token".to_string(),
        ));
    }
    Ok(SdpAttribute::Sctpmap(SdpAttributeSctpmap {
        port,
        channels: tokens[2].parse::<u32>()?,
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=setup, RFC4145
//-------------------------------------------------------------------------
//       setup-attr           =  "a=setup:" role
//       role                 =  "active" / "passive" / "actpass" / "holdconn"
fn parse_setup(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    Ok(SdpAttribute::Setup(
        match to_parse.to_lowercase().as_ref() {
            "active" => SdpAttributeSetup::Active,
            "actpass" => SdpAttributeSetup::Actpass,
            "holdconn" => SdpAttributeSetup::Holdconn,
            "passive" => SdpAttributeSetup::Passive,
            _ => {
                return Err(SdpParserInternalError::Generic(
                    "Unsupported setup value".to_string(),
                ));
            }
        },
    ))
}

fn parse_simulcast_version_list(
    to_parse: &str,
) -> Result<Vec<SdpAttributeSimulcastVersion>, SdpParserInternalError> {
    let make_version_list = |to_parse: &str| {
        to_parse
            .split(';')
            .map(SdpAttributeSimulcastVersion::new)
            .collect()
    };
    if to_parse.contains('=') {
        let mut descriptor_versionlist_pair = to_parse.splitn(2, '=');
        match descriptor_versionlist_pair.next().unwrap() {
            // TODO Bug 1470568
            "rid" => Ok(make_version_list(
                descriptor_versionlist_pair.next().unwrap(),
            )),
            descriptor => Err(SdpParserInternalError::Generic(format!(
                "Simulcast attribute has unknown list descriptor '{descriptor:?}'"
            ))),
        }
    } else {
        Ok(make_version_list(to_parse))
    }
}

///////////////////////////////////////////////////////////////////////////
// a=simulcast, draft-ietf-mmusic-sdp-simulcast
//-------------------------------------------------------------------------
// Old draft-04
// sc-attr     = "a=simulcast:" 1*2( WSP sc-str-list ) [WSP sc-pause-list]
// sc-str-list = sc-dir WSP sc-id-type "=" sc-alt-list *( ";" sc-alt-list )
// sc-pause-list = "paused=" sc-alt-list
// sc-dir      = "send" / "recv"
// sc-id-type  = "pt" / "rid" / token
// sc-alt-list = sc-id *( "," sc-id )
// sc-id       = fmt / rid-identifier / token
// ; WSP defined in [RFC5234]
// ; fmt, token defined in [RFC4566]
// ; rid-identifier defined in [I-D.pthatcher-mmusic-rid]
//
// New draft 14, need to parse this for now, will eventually emit it
// sc-value     = ( sc-send [SP sc-recv] ) / ( sc-recv [SP sc-send] )
// sc-send      = %s"send" SP sc-str-list
// sc-recv      = %s"recv" SP sc-str-list
// sc-str-list  = sc-alt-list *( ";" sc-alt-list )
// sc-alt-list  = sc-id *( "," sc-id )
// sc-id-paused = "~"
// sc-id        = [sc-id-paused] rid-id
// ; SP defined in [RFC5234]
// ; rid-id defined in [I-D.ietf-mmusic-rid]
fn parse_simulcast(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    // TODO: Bug 1225877: Stop accepting all kinds of whitespace here, and only accept SP
    let mut tokens = to_parse.split_whitespace();
    let first_direction = match tokens.next() {
        Some(x) => parse_single_direction(x)?,
        None => {
            return Err(SdpParserInternalError::Generic(
                "Simulcast attribute is missing send/recv value".to_string(),
            ));
        }
    };

    let first_version_list = match tokens.next() {
        Some(x) => parse_simulcast_version_list(x)?,
        None => {
            return Err(SdpParserInternalError::Generic(
                "Simulcast attribute must have an alternatives list after the direction token"
                    .to_string(),
            ));
        }
    };

    let mut second_version_list = Vec::new();
    if let Some(x) = tokens.next() {
        if parse_single_direction(x)? == first_direction {
            return Err(SdpParserInternalError::Generic(
                "Simulcast attribute has defined two times the same direction".to_string(),
            ));
        }

        second_version_list = match tokens.next() {
            Some(x) => parse_simulcast_version_list(x)?,
            None => {
                return Err(SdpParserInternalError::Generic(format!(
                    "{:?}{:?}",
                    "Simulcast has defined a second direction but",
                    "no second list of simulcast stream versions"
                )));
            }
        }
    }

    Ok(SdpAttribute::Simulcast(match first_direction {
        SdpSingleDirection::Send => SdpAttributeSimulcast {
            send: first_version_list,
            receive: second_version_list,
        },
        SdpSingleDirection::Recv => SdpAttributeSimulcast {
            send: second_version_list,
            receive: first_version_list,
        },
    }))
}

///////////////////////////////////////////////////////////////////////////
// a=ssrc, RFC5576
//-------------------------------------------------------------------------
// ssrc-attr = "ssrc:" ssrc-id SP attribute
// ; The base definition of "attribute" is in RFC 4566.
// ; (It is the content of "a=" lines.)
//
// ssrc-id = integer ; 0 .. 2**32 - 1
fn parse_ssrc(to_parse: &str) -> Result<SdpAttribute, SdpParserInternalError> {
    let mut tokens = to_parse.splitn(2, ' ');
    let ssrc_id = match tokens.next() {
        None => {
            return Err(SdpParserInternalError::Generic(
                "Ssrc attribute is missing ssrc-id value".to_string(),
            ));
        }
        Some(x) => x.parse::<u32>()?,
    };
    let mut ssrc = SdpAttributeSsrc::new(ssrc_id);
    match tokens.next() {
        None => (),
        Some(x) => ssrc.set_attribute(x),
    };
    Ok(SdpAttribute::Ssrc(ssrc))
}

pub fn parse_attribute(value: &str) -> Result<SdpType, SdpParserInternalError> {
    Ok(SdpType::Attribute(value.trim().parse()?))
}

#[cfg(test)]
#[path = "./attribute_type_tests.rs"]
mod tests;
