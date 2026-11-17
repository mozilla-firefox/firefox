// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Transport parameters. See -transport section 7.3.

use std::{
    cell::RefCell,
    cmp::min,
    fmt::{self, Debug, Display, Formatter},
    net::{Ipv4Addr, Ipv6Addr, SocketAddrV4, SocketAddrV6},
    rc::Rc,
};

use enum_map::{Enum, EnumMap};
use neqo_common::{Buffer, Decoder, Encoder, Role, hex::Hex, to_u64};
use nss::{
    HandshakeMessage, ZeroRttCheckResult, ZeroRttChecker,
    constants::{TLS_HS_CLIENT_HELLO, TLS_HS_ENCRYPTED_EXTENSIONS},
    ext::{ExtensionHandler, ExtensionHandlerResult, ExtensionWriterResult},
    random,
};
use strum::FromRepr;
use tracing::{debug, info, trace};

use crate::{
    Error, Res,
    cid::{ConnectionId, ConnectionIdEntry, ConnectionIdManager},
    packet::MIN_INITIAL_PACKET_SIZE,
    stateless_reset::Token as Srt,
    stream_id::MAX_STREAMS_LIMIT,
    tracking::DEFAULT_REMOTE_ACK_DELAY,
    version::{self, Version},
};

#[derive(Debug, Clone, Enum, PartialEq, Eq, Copy, FromRepr)]
#[repr(u64)]
pub enum TransportParameterId {
    OriginalDestinationConnectionId = 0x00,
    IdleTimeout = 0x01,
    StatelessResetToken = 0x02,
    MaxUdpPayloadSize = 0x03,
    InitialMaxData = 0x04,
    InitialMaxStreamDataBidiLocal = 0x05,
    InitialMaxStreamDataBidiRemote = 0x06,
    InitialMaxStreamDataUni = 0x07,
    InitialMaxStreamsBidi = 0x08,
    InitialMaxStreamsUni = 0x09,
    AckDelayExponent = 0x0a,
    MaxAckDelay = 0x0b,
    DisableMigration = 0x0c,
    PreferredAddress = 0x0d,
    ActiveConnectionIdLimit = 0x0e,
    InitialSourceConnectionId = 0x0f,
    RetrySourceConnectionId = 0x10,
    VersionInformation = 0x11,
    // draft-ietf-quic-reliable-stream-reset
    ResetStreamAt = 0x1d,
    // draft-ietf-scone-protocol
    Scone = 0x219e,
    GreaseQuicBit = 0x2ab2,
    MinAckDelay = 0xff02_de1a,
    MaxDatagramFrameSize = 0x0020,
    #[cfg(test)]
    TestTransportParameter = 0xce16,
}

impl Display for TransportParameterId {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "{self:?}(0x{:02x})", u64::from(*self))
    }
}

impl From<TransportParameterId> for u64 {
    fn from(val: TransportParameterId) -> Self {
        val as Self
    }
}

impl TryFrom<u64> for TransportParameterId {
    type Error = Error;

    fn try_from(value: u64) -> Result<Self, Self::Error> {
        Self::from_repr(value).ok_or(Error::UnknownTransportParameter)
    }
}

#[derive(Clone, Debug)]
pub struct PreferredAddress {
    v4: Option<SocketAddrV4>,
    v6: Option<SocketAddrV6>,
}

impl PreferredAddress {
    /// Make a new preferred address configuration.
    ///
    /// # Panics
    ///
    /// If neither address is provided, or if either address is of the wrong type.
    #[must_use]
    pub fn new(v4: Option<SocketAddrV4>, v6: Option<SocketAddrV6>) -> Self {
        assert!(v4.is_some() || v6.is_some());
        if let Some(a) = v4 {
            assert!(!a.ip().is_unspecified());
            assert_ne!(a.port(), 0);
        }
        if let Some(a) = v6 {
            assert!(!a.ip().is_unspecified());
            assert_ne!(a.port(), 0);
        }
        Self { v4, v6 }
    }

    /// A generic version of `new()` for testing.
    /// # Panics
    /// When the addresses are the wrong type.
    #[must_use]
    #[cfg(test)]
    pub fn new_any(v4: Option<std::net::SocketAddr>, v6: Option<std::net::SocketAddr>) -> Self {
        use std::net::SocketAddr;

        let v4 = v4.map(|v4| {
            let SocketAddr::V4(v4) = v4 else {
                panic!("not v4");
            };
            v4
        });
        let v6 = v6.map(|v6| {
            let SocketAddr::V6(v6) = v6 else {
                panic!("not v6");
            };
            v6
        });
        Self::new(v4, v6)
    }

    #[must_use]
    pub const fn ipv4(&self) -> Option<SocketAddrV4> {
        self.v4
    }
    #[must_use]
    pub const fn ipv6(&self) -> Option<SocketAddrV6> {
        self.v6
    }
}

#[derive(Clone, PartialEq, Eq)]
pub enum TransportParameter {
    Bytes(Vec<u8>),
    Integer(u64),
    Empty,
    PreferredAddress {
        v4: Option<SocketAddrV4>,
        v6: Option<SocketAddrV6>,
        cid: ConnectionId,
        srt: Srt,
    },
    Versions {
        current: version::Wire,
        other: Vec<version::Wire>,
    },
}

impl Debug for TransportParameter {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        struct VersionListDebug<T>(T);
        impl<T: AsRef<[version::Wire]>> Debug for VersionListDebug<T> {
            fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
                f.debug_list()
                    .entries(self.0.as_ref().iter().map(|v| Hex::new(v.to_be_bytes())))
                    .finish()
            }
        }

        match self {
            Self::Bytes(a) => f.debug_tuple("Bytes").field(&Hex::new(a)).finish(),
            Self::Integer(a) => f.debug_tuple("Integer").field(a).finish(),
            Self::Empty => f.write_str("Empty"),
            Self::PreferredAddress { v4, v6, cid, srt } => f
                .debug_struct("PreferredAddress")
                .field("v4", v4)
                .field("v6", v6)
                .field("cid", &Hex::new(cid))
                .field("srt", &Hex::new(srt))
                .finish(),
            Self::Versions { current, other } => f
                .debug_struct("Versions")
                .field("current", &Hex::new(&current.to_be_bytes()))
                .field("other", &VersionListDebug(other))
                .finish(),
        }
    }
}

impl TransportParameter {
    fn encode<B: Buffer>(&self, enc: &mut Encoder<B>, tp: TransportParameterId) {
        trace!("TP encoded; type {tp}) val {self:?}");
        enc.encode_varint(tp);
        match self {
            Self::Bytes(a) => {
                enc.encode_vvec(a);
            }
            Self::Integer(a) => {
                enc.encode_vvec_with(|enc_inner| {
                    enc_inner.encode_varint(*a);
                });
            }
            Self::Empty => {
                enc.encode_varint(0_u64);
            }
            Self::PreferredAddress { v4, v6, cid, srt } => {
                enc.encode_vvec_with(|enc_inner| {
                    if let Some(v4) = v4 {
                        enc_inner.encode(&v4.ip().octets()[..]);
                        enc_inner.encode_uint(2, v4.port());
                    } else {
                        enc_inner.encode([0; 6]);
                    }
                    if let Some(v6) = v6 {
                        enc_inner.encode(&v6.ip().octets()[..]);
                        enc_inner.encode_uint(2, v6.port());
                    } else {
                        enc_inner.encode([0; 18]);
                    }
                    enc_inner.encode_vec(1, &cid[..]);
                    enc_inner.encode(srt);
                });
            }
            Self::Versions { current, other } => {
                enc.encode_vvec_with(|enc_inner| {
                    enc_inner.encode_uint(4, *current);
                    for v in other {
                        enc_inner.encode_uint(4, *v);
                    }
                });
            }
        }
    }

    fn decode_preferred_address(d: &mut Decoder) -> Res<Self> {
        // IPv4 address (maybe)
        let v4ip = Ipv4Addr::from_octets(*d.decode_array().ok_or(Error::NoMoreData)?);
        let v4port = d.decode_uint::<u16>().ok_or(Error::NoMoreData)?;
        // Can't have non-zero IP and zero port, or vice versa.
        if v4ip.is_unspecified() ^ (v4port == 0) {
            return Err(Error::TransportParameter);
        }
        let v4 = if v4port == 0 {
            None
        } else {
            Some(SocketAddrV4::new(v4ip, v4port))
        };

        // IPv6 address (mostly the same as v4)
        let v6ip = Ipv6Addr::from_octets(*d.decode_array().ok_or(Error::NoMoreData)?);
        let v6port = d.decode_uint().ok_or(Error::NoMoreData)?;
        if v6ip.to_canonical().is_unspecified() ^ (v6port == 0) {
            return Err(Error::TransportParameter);
        }
        let v6 = if v6port == 0 {
            None
        } else {
            Some(SocketAddrV6::new(v6ip, v6port, 0, 0))
        };
        // Need either v4 or v6 to be present.
        if v4.is_none() && v6.is_none() {
            return Err(Error::TransportParameter);
        }

        // Connection ID (non-zero length)
        let cid = ConnectionId::from(d.decode_vec(1).ok_or(Error::NoMoreData)?);
        if cid.is_empty() || cid.len() > ConnectionId::MAX_LEN {
            return Err(Error::TransportParameter);
        }

        // Stateless reset token
        let srt = Srt::try_from(d).map_err(|_| Error::TransportParameter)?;

        Ok(Self::PreferredAddress { v4, v6, cid, srt })
    }

    fn decode_versions(dec: &mut Decoder) -> Res<Self> {
        fn dv(dec: &mut Decoder) -> Res<version::Wire> {
            let v = dec
                .decode_uint::<version::Wire>()
                .ok_or(Error::NoMoreData)?;
            if v == 0 {
                Err(Error::TransportParameter)
            } else {
                Ok(v)
            }
        }

        let current = dv(dec)?;
        // This rounding down is OK because `decode` checks for left over data.
        let count = dec.remaining() / 4;
        let mut other = Vec::with_capacity(count);
        for _ in 0..count {
            other.push(dv(dec)?);
        }
        Ok(Self::Versions { current, other })
    }

    fn decode(role: Role, dec: &mut Decoder) -> Res<Option<(TransportParameterId, Self)>> {
        let tp = dec.decode_varint().ok_or(Error::NoMoreData)?;
        let content = dec.decode_vvec().ok_or(Error::NoMoreData)?;
        trace!("TP {tp:x} length {:x}", content.len());
        let tp = match tp.try_into() {
            Ok(tp) => tp,
            Err(Error::UnknownTransportParameter) => return Ok(None), // Skip
            Err(e) => return Err(e),
        };
        let mut d = Decoder::from(content);
        let value = match tp {
            TransportParameterId::OriginalDestinationConnectionId
            | TransportParameterId::RetrySourceConnectionId => {
                if role == Role::Server {
                    return Err(Error::TransportParameter);
                }
                Self::Bytes(d.decode_remainder().to_vec())
            }
            TransportParameterId::InitialSourceConnectionId => {
                Self::Bytes(d.decode_remainder().to_vec())
            }
            TransportParameterId::StatelessResetToken => {
                if role == Role::Server || d.remaining() != 16 {
                    return Err(Error::TransportParameter);
                }
                Self::Bytes(d.decode_remainder().to_vec())
            }
            TransportParameterId::IdleTimeout
            | TransportParameterId::InitialMaxData
            | TransportParameterId::InitialMaxStreamDataBidiLocal
            | TransportParameterId::InitialMaxStreamDataBidiRemote
            | TransportParameterId::InitialMaxStreamDataUni
            | TransportParameterId::MaxDatagramFrameSize => match d.decode_varint() {
                Some(v) => Self::Integer(v),
                None => return Err(Error::TransportParameter),
            },
            TransportParameterId::MaxAckDelay => match d.decode_varint() {
                // RFC 9000, Section 18.2: "Values of 2^14 or greater are invalid."
                Some(v) if v < (1 << 14) => Self::Integer(v),
                _ => return Err(Error::TransportParameter),
            },
            TransportParameterId::InitialMaxStreamsBidi
            | TransportParameterId::InitialMaxStreamsUni => match d.decode_varint() {
                Some(v) if v <= MAX_STREAMS_LIMIT => Self::Integer(v),
                _ => return Err(Error::StreamLimit),
            },
            TransportParameterId::MaxUdpPayloadSize => match d.decode_varint() {
                Some(v) if v >= MIN_INITIAL_PACKET_SIZE.try_into()? => {
                    // QUIC doesn't cap this value, even to 2^16, but we need to.
                    Self::Integer(min(to_u64(usize::MAX), v))
                }
                _ => return Err(Error::TransportParameter),
            },
            TransportParameterId::AckDelayExponent => match d.decode_varint() {
                Some(v) if v <= 20 => Self::Integer(v),
                _ => return Err(Error::TransportParameter),
            },
            TransportParameterId::ActiveConnectionIdLimit => match d.decode_varint() {
                Some(v) if v >= 2 => Self::Integer(v),
                _ => return Err(Error::TransportParameter),
            },
            TransportParameterId::DisableMigration
            | TransportParameterId::GreaseQuicBit
            | TransportParameterId::Scone
            | TransportParameterId::ResetStreamAt => Self::Empty,
            TransportParameterId::PreferredAddress => {
                if role == Role::Server {
                    return Err(Error::TransportParameter);
                }
                Self::decode_preferred_address(&mut d)?
            }
            TransportParameterId::MinAckDelay => match d.decode_varint() {
                Some(v) if v < (1 << 24) => Self::Integer(v),
                _ => return Err(Error::TransportParameter),
            },
            TransportParameterId::VersionInformation => Self::decode_versions(&mut d)?,
            #[cfg(test)]
            TransportParameterId::TestTransportParameter => {
                Self::Bytes(d.decode_remainder().to_vec())
            }
        };
        if d.remaining() > 0 {
            return Err(Error::TooMuchData);
        }
        trace!("TP decoded; type {tp} val {value:?}");
        Ok(Some((tp, value)))
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TransportParameters {
    params: EnumMap<TransportParameterId, Option<TransportParameter>>,
}

impl TransportParameters {
    /// Set a value.
    pub fn set(&mut self, k: TransportParameterId, v: TransportParameter) {
        self.params[k] = Some(v);
    }

    /// Clear a key.
    pub fn remove(&mut self, k: TransportParameterId) {
        self.params[k].take();
    }

    /// Decode is a static function that parses transport parameters
    /// using the provided decoder.
    ///
    /// `role` is the intended recipient of the transport parameters.
    /// A server will pass [`Role::Client`] when recovering the saved value
    /// of its own transport parameters when validating 0-RTT, as an exception.
    ///
    /// # Errors
    /// When the transport parameters are malformed.
    pub fn decode(role: Role, d: &mut Decoder) -> Res<Self> {
        #[cfg(feature = "build-fuzzing-corpus")]
        neqo_common::write_item_to_fuzzing_corpus("tparams", d.as_ref());

        let mut tps = Self::default();
        trace!("Parsed fixed TP header");

        while d.remaining() > 0 {
            match TransportParameter::decode(role, d) {
                Ok(Some((tipe, tp))) => {
                    // RFC 9000, Section 7.4:
                    //
                    // > An endpoint MUST NOT send a parameter more than once in a given transport
                    // > parameters extension. An endpoint SHOULD treat receipt of duplicate
                    // > transport parameters as a connection error of type
                    // > TRANSPORT_PARAMETER_ERROR.
                    //
                    // The SHOULD only exists to let endpoints skip tracking parameters they do
                    // not understand (see RFC 9413), not for robustness, so reject a duplicate of
                    // a parameter we have already parsed.
                    if tps.params[tipe].is_some() {
                        info!("Duplicate transport parameter {tipe}");
                        return Err(Error::TransportParameter);
                    }
                    tps.set(tipe, tp);
                }
                Ok(None) => {}
                Err(e) => return Err(e),
            }
        }
        Ok(tps)
    }

    const fn retain_all(_tp: TransportParameterId, _v: Option<&TransportParameter>) -> bool {
        true
    }

    pub(crate) fn encode<B: Buffer>(&self, enc: &mut Encoder<B>) {
        #[cfg(feature = "build-fuzzing-corpus")]
        let start = enc.len();
        self.encode_filtered(Self::retain_all, enc);
        #[cfg(feature = "build-fuzzing-corpus")]
        neqo_common::write_item_to_fuzzing_corpus("tparams", &enc.as_ref()[start..]);
    }

    fn encode_filtered<F, B: Buffer>(&self, f: F, enc: &mut Encoder<B>)
    where
        F: Fn(TransportParameterId, Option<&TransportParameter>) -> bool,
    {
        for (i, v) in self.params.iter().filter(|(i, v)| f(*i, v.as_ref())) {
            if let Some(tp) = v {
                tp.encode(enc, i);
            }
        }
    }

    // Get an integer type or a default.
    /// # Panics
    /// When the transport parameter isn't recognized as being an integer.
    #[must_use]
    pub fn get_integer(&self, tp: TransportParameterId) -> u64 {
        let dflt = Self::integer_default(tp).expect("Transport parameter not a known Integer");
        match self.params[tp] {
            None => dflt,
            Some(TransportParameter::Integer(x)) => x,
            _ => panic!("Internal error"),
        }
    }

    fn integer_default(tp: TransportParameterId) -> Option<u64> {
        // Note: testing for this can't catch new values; make sure to update the test.
        Some(match tp {
            TransportParameterId::IdleTimeout
            | TransportParameterId::InitialMaxData
            | TransportParameterId::InitialMaxStreamDataBidiLocal
            | TransportParameterId::InitialMaxStreamDataBidiRemote
            | TransportParameterId::InitialMaxStreamDataUni
            | TransportParameterId::InitialMaxStreamsBidi
            | TransportParameterId::InitialMaxStreamsUni
            | TransportParameterId::MinAckDelay
            | TransportParameterId::MaxDatagramFrameSize => 0,
            TransportParameterId::MaxUdpPayloadSize => 65527,
            TransportParameterId::AckDelayExponent => 3,
            TransportParameterId::MaxAckDelay => {
                u64::try_from(DEFAULT_REMOTE_ACK_DELAY.as_millis())
                    .expect("default remote ack delay in ms can't overflow u64")
            }
            TransportParameterId::ActiveConnectionIdLimit => 2,
            _ => return None,
        })
    }

    // Set an integer transport parameter, removing it if it matches the spec default.
    /// # Panics
    /// When the transport parameter isn't recognized as being an integer.
    pub fn set_integer(&mut self, tp: TransportParameterId, value: u64) {
        match Self::integer_default(tp) {
            Some(dflt) if dflt == value => self.remove(tp),
            Some(_) => self.set(tp, TransportParameter::Integer(value)),
            None => panic!("Transport parameter not known"),
        }
    }

    /// # Panics
    /// When the transport parameter isn't recognized as containing bytes.
    #[must_use]
    pub fn get_bytes(&self, tp: TransportParameterId) -> Option<&[u8]> {
        match tp {
            TransportParameterId::OriginalDestinationConnectionId
            | TransportParameterId::InitialSourceConnectionId
            | TransportParameterId::RetrySourceConnectionId
            | TransportParameterId::StatelessResetToken => {}
            _ => panic!("Transport parameter not known or not type bytes"),
        }

        match &self.params[tp] {
            None => None,
            Some(TransportParameter::Bytes(x)) => Some(x),
            _ => panic!("Internal error"),
        }
    }

    /// # Panics
    /// When the transport parameter isn't recognized as containing bytes.
    pub fn set_bytes(&mut self, tp: TransportParameterId, value: Vec<u8>) {
        match tp {
            TransportParameterId::OriginalDestinationConnectionId
            | TransportParameterId::InitialSourceConnectionId
            | TransportParameterId::RetrySourceConnectionId
            | TransportParameterId::StatelessResetToken => {
                self.set(tp, TransportParameter::Bytes(value));
            }
            _ => panic!("Transport parameter not known or not type bytes"),
        }
    }

    /// # Panics
    /// When the transport parameter isn't recognized as being empty.
    pub fn set_empty(&mut self, tp: TransportParameterId) {
        match tp {
            TransportParameterId::DisableMigration
            | TransportParameterId::GreaseQuicBit
            | TransportParameterId::Scone
            | TransportParameterId::ResetStreamAt => {
                self.set(tp, TransportParameter::Empty);
            }
            _ => panic!("Transport parameter not known or not type empty"),
        }
    }

    /// Set version information.
    pub fn set_versions(&mut self, role: Role, versions: &version::Config) {
        let mut other: Vec<u32> = Vec::with_capacity(versions.all().len() + 1);
        let grease = u32::from_ne_bytes(random::<4>()) & 0xf0f0_f0f0 | 0x0a0a_0a0a;
        other.push(grease);
        for &v in versions.all() {
            if role == Role::Client && !versions.initial().is_compatible(v) {
                continue;
            }
            other.push(v.wire_version());
        }
        let current = versions.initial().wire_version();
        self.set(
            TransportParameterId::VersionInformation,
            TransportParameter::Versions { current, other },
        );
    }

    fn compatible_upgrade(&mut self, v: Version) {
        if let Some(TransportParameter::Versions {
            ref mut current, ..
        }) = self.params[TransportParameterId::VersionInformation]
        {
            *current = v.wire_version();
        } else {
            unreachable!("Compatible upgrade without transport parameters set!");
        }
    }

    /// # Panics
    /// When the indicated transport parameter is present but NOT empty.
    /// This should not happen if the parsing code in `TransportParameter::decode` is correct.
    #[must_use]
    pub fn get_empty(&self, tipe: TransportParameterId) -> bool {
        match self.params[tipe] {
            None => false,
            Some(TransportParameter::Empty) => true,
            _ => panic!("Internal error"),
        }
    }

    /// Return true if the remembered transport parameters are OK for 0-RTT.
    /// Generally this means that any value that is currently in effect is greater than
    /// or equal to the promised value.
    pub(crate) fn ok_for_0rtt(&self, remembered: &Self) -> bool {
        for (k, v_rem) in &remembered.params {
            // Skip checks for these, which don't affect 0-RTT.
            if v_rem.is_none()
                || matches!(
                    k,
                    TransportParameterId::OriginalDestinationConnectionId
                        | TransportParameterId::InitialSourceConnectionId
                        | TransportParameterId::RetrySourceConnectionId
                        | TransportParameterId::StatelessResetToken
                        | TransportParameterId::IdleTimeout
                        | TransportParameterId::AckDelayExponent
                        | TransportParameterId::MaxAckDelay
                        | TransportParameterId::ActiveConnectionIdLimit
                        | TransportParameterId::PreferredAddress
                        | TransportParameterId::Scone
                )
            {
                continue;
            }

            // Other parameters need to be checked manually, because they
            // require that the previous value be remembered and available for use.
            // Thus, the new setting needs to be compatible with the old one.
            //
            // This is because the feature might be used in 0-RTT, which is impossible
            // to separate from regular 1-RTT data (given retransmission), so features
            // used in 0-RTT cannot be disabled if they are remembered.
            //
            // For empty values, that just means being present;
            // for integer values, *generally* the value has to be larger;
            // any other types either need to be ignorable or have special handling.
            let ok = self.params[k]
                .as_ref()
                .is_some_and(|v_self| match (v_self, v_rem) {
                    (
                        TransportParameter::Integer(i_self),
                        Some(TransportParameter::Integer(i_rem)),
                    ) => {
                        if k == TransportParameterId::MinAckDelay {
                            // MIN_ACK_DELAY is backwards:
                            // it can only be reduced safely.
                            *i_self <= *i_rem
                        } else {
                            *i_self >= *i_rem
                        }
                    }
                    (TransportParameter::Empty, Some(TransportParameter::Empty)) => true,
                    (
                        TransportParameter::Versions {
                            current: v_self, ..
                        },
                        Some(TransportParameter::Versions { current: v_rem, .. }),
                    ) => v_self == v_rem,
                    _ => false,
                });
            if !ok {
                return false;
            }
        }
        true
    }

    /// Get the preferred address in a usable form.
    #[must_use]
    pub fn get_preferred_address(&self) -> Option<(PreferredAddress, ConnectionIdEntry<Srt>)> {
        if let Some(TransportParameter::PreferredAddress { v4, v6, cid, srt }) =
            &self.params[TransportParameterId::PreferredAddress]
        {
            Some((
                PreferredAddress::new(*v4, *v6),
                ConnectionIdEntry::new(
                    ConnectionIdManager::SEQNO_PREFERRED,
                    cid.clone(),
                    srt.clone(),
                ),
            ))
        } else {
            None
        }
    }

    /// Get the version negotiation values for validation.
    #[must_use]
    pub fn get_versions(&self) -> Option<(version::Wire, &[version::Wire])> {
        if let Some(TransportParameter::Versions { current, other }) =
            &self.params[TransportParameterId::VersionInformation]
        {
            Some((*current, other))
        } else {
            None
        }
    }

    #[must_use]
    pub fn has_value(&self, tp: TransportParameterId) -> bool {
        self.params[tp].is_some()
    }
}

#[derive(Debug)]
pub struct TransportParametersHandler {
    role: Role,
    versions: version::Config,
    version_selected: bool,
    local: TransportParameters,
    remote_handshake: Option<TransportParameters>,
    remote_0rtt: Option<TransportParameters>,
    error: Option<Error>,
}

impl TransportParametersHandler {
    #[must_use]
    pub fn new(role: Role, versions: version::Config) -> Self {
        let mut local = TransportParameters::default();
        local.set_versions(role, &versions);
        Self {
            role,
            versions,
            version_selected: false,
            local,
            remote_handshake: None,
            remote_0rtt: None,
            error: None,
        }
    }

    /// When resuming, the version is set based on the ticket.
    /// That needs to be done to override the default choice from configuration.
    pub fn set_version(&mut self, version: Version) {
        debug_assert_eq!(self.role, Role::Client);
        self.versions.set_initial(version);
        self.local.set_versions(self.role, &self.versions);
    }

    /// # Panics
    /// When this function is called before the peer has provided transport parameters.
    /// Do not call this function if you are not also able to send data.
    #[must_use]
    pub fn remote(&self) -> &TransportParameters {
        match (self.remote_handshake(), self.remote_0rtt()) {
            (Some(tp), _) | (_, Some(tp)) => tp,
            _ => panic!("no transport parameters from peer"),
        }
    }

    /// Get the version as set (or as determined by a compatible upgrade).
    #[must_use]
    pub const fn version(&self) -> Version {
        self.versions.initial()
    }

    fn compatible_upgrade(&mut self, remote_tp: &TransportParameters) -> Res<()> {
        if self.version_selected {
            // A second call to this is only possible on the server,
            // if we need to send a TLS HelloRetryRequest.
            debug_assert_eq!(self.role, Role::Server);
            return Ok(());
        }

        let Some((current, other)) = remote_tp.get_versions() else {
            return Ok(());
        };
        trace!(
            "Peer versions: {current:x} {other:x?}; config {:?}",
            self.versions,
        );

        if self.role == Role::Client {
            let chosen = Version::try_from(current)?;
            if self.versions.compatible().any(|&v| v == chosen) {
                self.version_selected = true;
                Ok(())
            } else {
                info!(
                    "Chosen version {current:x} is not compatible with initial version {:x}",
                    self.versions.initial().wire_version(),
                );
                Err(Error::VersionNegotiation)
            }
        } else {
            if current != self.versions.initial().wire_version() {
                info!(
                    "Current version {current:x} != own version {:x}",
                    self.versions.initial().wire_version(),
                );
                return Err(Error::VersionNegotiation);
            }

            if let Some(preferred) = self.versions.preferred_compatible(other) {
                if preferred != self.versions.initial() {
                    info!(
                        "Compatible upgrade {:?} ==> {preferred:?}",
                        self.versions.initial()
                    );
                    self.versions.set_initial(preferred);
                    self.local.compatible_upgrade(preferred);
                }
                self.version_selected = true;
                Ok(())
            } else {
                info!("Unable to find any compatible version");
                Err(Error::TransportParameter)
            }
        }
    }

    #[must_use]
    pub const fn local(&self) -> &TransportParameters {
        &self.local
    }

    #[must_use]
    pub const fn local_mut(&mut self) -> &mut TransportParameters {
        &mut self.local
    }

    pub fn set_remote_0rtt(&mut self, remote_0rtt: Option<TransportParameters>) {
        self.remote_0rtt = remote_0rtt;
    }

    #[must_use]
    pub const fn remote_0rtt(&self) -> Option<&TransportParameters> {
        self.remote_0rtt.as_ref()
    }

    #[must_use]
    pub const fn remote_handshake(&self) -> Option<&TransportParameters> {
        self.remote_handshake.as_ref()
    }

    pub(crate) const fn take_error(&mut self) -> Option<Error> {
        self.error.take()
    }

    /// Filter to retain only those transport parameters that are necessary for an outer
    /// `ClientHello`.
    ///
    /// We don't need the connection for long if we are forced into an ECH fallback,
    /// we only need it around long enough to get a fresh ECH config; and no data is exchanged.
    ///
    /// However, we do need to ensure that the connection attempt works.
    /// That motivates the inclusion of version negotiation and connection ID parameters,
    /// which would break the connection if they were dropped.
    ///
    /// Also, we include any parameters that might affect the configuration of the connection
    /// in ways that might adversely affect operation.
    /// That saves us from having to swap in a different configuration (i.e.,
    /// `ConnectionParameters`) if the outer `ClientHello` is used.
    /// These probably aren't strictly necessary, even then:
    /// * ACK-related parameters only affect RTT estimation, which won't matter much; and
    /// * UDP datagram sizes will look like a path MTU restriction.
    ///
    /// There is no privacy harm to including them as the values are fixed (maximum ACK delay)
    /// or not set (ACK delay exponent and UDP payload size) in our code.
    const fn filter_ch_outer(tp: TransportParameterId, _v: Option<&TransportParameter>) -> bool {
        matches!(
            tp,
            TransportParameterId::OriginalDestinationConnectionId
                | TransportParameterId::StatelessResetToken
                | TransportParameterId::InitialSourceConnectionId
                | TransportParameterId::RetrySourceConnectionId
                | TransportParameterId::VersionInformation
                | TransportParameterId::AckDelayExponent
                | TransportParameterId::MaxAckDelay
                | TransportParameterId::MaxUdpPayloadSize
        )
    }
}

impl ExtensionHandler for TransportParametersHandler {
    fn write(
        &mut self,
        msg: HandshakeMessage,
        ch_outer: bool,
        d: &mut [u8],
    ) -> ExtensionWriterResult {
        if !matches!(msg, TLS_HS_CLIENT_HELLO | TLS_HS_ENCRYPTED_EXTENSIONS) {
            return ExtensionWriterResult::Skip;
        }

        debug!("Writing transport parameters, msg={msg:?}");

        let mut enc = Encoder::new_borrowed_slice(d);
        let f = if ch_outer {
            debug_assert_eq!(msg, TLS_HS_CLIENT_HELLO);
            Self::filter_ch_outer
        } else {
            TransportParameters::retain_all
        };
        self.local.encode_filtered(f, &mut enc);
        ExtensionWriterResult::Write(enc.len())
    }

    fn handle(&mut self, msg: HandshakeMessage, d: &[u8]) -> ExtensionHandlerResult {
        trace!(
            "Handling transport parameters, msg={msg:?} value={}",
            Hex::new(d),
        );

        if !matches!(msg, TLS_HS_CLIENT_HELLO | TLS_HS_ENCRYPTED_EXTENSIONS) {
            return ExtensionHandlerResult::Alert(110); // unsupported_extension
        }

        let mut dec = Decoder::from(d);
        match TransportParameters::decode(self.role, &mut dec)
            .map_err(|_| Error::TransportParameter)
            .and_then(|tp| self.compatible_upgrade(&tp).map(|()| tp))
        {
            Ok(tp) => {
                self.remote_handshake = Some(tp);
                ExtensionHandlerResult::Ok
            }
            Err(e) => {
                self.error = Some(e);
                ExtensionHandlerResult::Alert(47) // illegal_parameter
            }
        }
    }
}

#[derive(Debug)]
pub(crate) struct TpZeroRttChecker<T> {
    handler: Rc<RefCell<TransportParametersHandler>>,
    app_checker: T,
}

impl<T> TpZeroRttChecker<T>
where
    T: ZeroRttChecker + 'static,
{
    pub fn wrap(
        handler: Rc<RefCell<TransportParametersHandler>>,
        app_checker: T,
    ) -> Box<dyn ZeroRttChecker> {
        Box::new(Self {
            handler,
            app_checker,
        })
    }
}

impl<T> ZeroRttChecker for TpZeroRttChecker<T>
where
    T: ZeroRttChecker,
{
    fn check(&self, token: &[u8]) -> ZeroRttCheckResult {
        // Reject 0-RTT if there is no token.
        if token.is_empty() {
            debug!("0-RTT: no token, no 0-RTT");
            return ZeroRttCheckResult::Reject;
        }
        let mut dec = Decoder::from(token);
        let Some(tpslice) = dec.decode_vvec() else {
            info!("0-RTT: token code error");
            return ZeroRttCheckResult::Fail;
        };
        let mut dec_tp = Decoder::from(tpslice);
        // This runs on the server, but it is checking its own transport parameters.
        let Ok(remembered) = TransportParameters::decode(Role::Client, &mut dec_tp) else {
            info!("0-RTT: transport parameter decode error");
            return ZeroRttCheckResult::Fail;
        };
        if self.handler.borrow().local.ok_for_0rtt(&remembered) {
            info!("0-RTT: transport parameters OK, passing to application checker");
            self.app_checker.check(dec.decode_remainder())
        } else {
            info!("0-RTT: transport parameters bad, rejecting");
            ZeroRttCheckResult::Reject
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{
        net::{Ipv4Addr, Ipv6Addr, SocketAddrV4, SocketAddrV6},
        str::FromStr as _,
    };

    use TransportParameterId::*;
    use neqo_common::{
        Decoder, Encoder,
        Role::{Client, Server},
        to_u64,
    };
    use test_fixture::fixture_init;
    use tracing::debug;

    use super::PreferredAddress;
    use crate::{
        ConnectionId, Error, MAX_STREAMS_LIMIT, Version,
        stateless_reset::Token as Srt,
        tparams::{TransportParameter, TransportParameterId, TransportParameters},
    };

    #[test]
    fn debug_hex() {
        let bytes = TransportParameter::Bytes(vec![0x01, 0x23, 0xab, 0xcd]);
        assert_eq!(format!("{bytes:?}"), "Bytes(0123abcd)");

        let versions = TransportParameter::Versions {
            current: 0x0000_0001,
            other: vec![0xff00_001d, 0x709a_50c4],
        };
        assert_eq!(
            format!("{versions:?}"),
            "Versions { current: 00000001, other: [ff00001d, 709a50c4] }"
        );

        let spa = make_spa();
        let formatted = format!("{spa:?}");
        assert!(formatted.contains("cid: 0102030405"), "{formatted}");
        assert!(
            formatted.contains("srt: 03030303030303030303030303030303"),
            "{formatted}"
        );
    }

    #[test]
    fn basic_tps() {
        const RESET_TOKEN: &[u8] = &[1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8];
        let mut tps = TransportParameters::default();
        tps.set(
            StatelessResetToken,
            TransportParameter::Bytes(RESET_TOKEN.to_vec()),
        );
        tps.params[InitialMaxStreamsBidi] = Some(TransportParameter::Integer(10));

        let mut enc = Encoder::default();
        tps.encode(&mut enc);

        let tps2 =
            TransportParameters::decode(Client, &mut enc.as_decoder()).expect("Couldn't decode");
        assert_eq!(tps, tps2);

        println!("TPS = {tps:?}");
        assert_eq!(tps2.get_integer(IdleTimeout), 0); // Default
        assert_eq!(tps2.get_integer(MaxAckDelay), 25); // Default
        assert_eq!(tps2.get_integer(ActiveConnectionIdLimit), 2); // Default
        assert_eq!(tps2.get_integer(InitialMaxStreamsBidi), 10); // Sent
        assert_eq!(tps2.get_bytes(StatelessResetToken), Some(RESET_TOKEN));
        assert_eq!(tps2.get_bytes(OriginalDestinationConnectionId), None);
        assert_eq!(tps2.get_bytes(InitialSourceConnectionId), None);
        assert_eq!(tps2.get_bytes(RetrySourceConnectionId), None);
        assert!(!tps2.has_value(OriginalDestinationConnectionId));
        assert!(!tps2.has_value(InitialSourceConnectionId));
        assert!(!tps2.has_value(RetrySourceConnectionId));
        assert!(!tps2.has_value(Scone));
        assert!(!tps2.has_value(PreferredAddress));
        assert!(!tps2.has_value(ResetStreamAt));
        assert!(tps2.has_value(StatelessResetToken));

        let mut enc = Encoder::default();
        tps.encode(&mut enc);

        TransportParameters::decode(Client, &mut enc.as_decoder()).expect("Couldn't decode");
    }

    /// Validate that default values don't get set.
    #[test]
    fn default_tps() {
        use enum_map::Enum as _;
        let mut tps = TransportParameters::default();
        let mut count = 0;
        for i in 0..TransportParameterId::LENGTH {
            let tp = TransportParameterId::from_usize(i);
            if let Some(value) = TransportParameters::integer_default(tp) {
                count += 1;
                tps.set_integer(tp, value);
                assert!(!tps.has_value(tp), "{tp}");

                tps.set_integer(tp, value + 1);
                assert!(tps.has_value(tp), "{tp}");
                tps.set_integer(tp, value);
                assert!(!tps.has_value(tp), "{tp}");
            }
        }
        assert!(count > 0);
    }

    fn make_spa() -> TransportParameter {
        TransportParameter::PreferredAddress {
            v4: Some(SocketAddrV4::new(Ipv4Addr::from(0xc000_0201), 443)),
            v6: Some(SocketAddrV6::new(
                Ipv6Addr::from(0xfe80_0000_0000_0000_0000_0000_0000_0001),
                443,
                0,
                0,
            )),
            cid: ConnectionId::from(&[1, 2, 3, 4, 5]),
            srt: Srt::new([3; Srt::LEN]),
        }
    }

    #[test]
    fn preferred_address_encode_decode() {
        const ENCODED: &[u8] = &[
            0x0d, 0x2e, 0xc0, 0x00, 0x02, 0x01, 0x01, 0xbb, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0xbb, 0x05, 0x01,
            0x02, 0x03, 0x04, 0x05, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
            0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        ];
        let spa = make_spa();
        let mut enc = Encoder::default();
        spa.encode(&mut enc, PreferredAddress);
        assert_eq!(enc.as_ref(), ENCODED);

        let mut dec = enc.as_decoder();
        let (id, decoded) = TransportParameter::decode(Client, &mut dec)
            .unwrap()
            .unwrap();
        assert_eq!(id, PreferredAddress);
        assert_eq!(decoded, spa);
    }

    fn mutate_spa<F>(wrecker: F) -> TransportParameter
    where
        F: FnOnce(&mut Option<SocketAddrV4>, &mut Option<SocketAddrV6>, &mut ConnectionId),
    {
        let mut spa = make_spa();
        if let TransportParameter::PreferredAddress { v4, v6, cid, .. } = &mut spa {
            wrecker(v4, v6, cid);
        } else {
            unreachable!();
        }
        spa
    }

    /// This takes a `TransportParameter::PreferredAddress` that has been mutilated.
    /// It then encodes it, working from the knowledge that the `encode` function
    /// doesn't care about validity, and decodes it.  The result should be failure.
    fn assert_invalid_spa(spa: &TransportParameter) {
        let mut enc = Encoder::default();
        spa.encode(&mut enc, PreferredAddress);
        assert_eq!(
            TransportParameter::decode(Client, &mut enc.as_decoder()).unwrap_err(),
            Error::TransportParameter
        );
    }

    /// This is for those rare mutations that are acceptable.
    fn assert_valid_spa(spa: &TransportParameter) {
        let mut enc = Encoder::default();
        spa.encode(&mut enc, PreferredAddress);
        let mut dec = enc.as_decoder();
        let (id, decoded) = TransportParameter::decode(Client, &mut dec)
            .unwrap()
            .unwrap();
        assert_eq!(id, PreferredAddress);
        assert_eq!(&decoded, spa);
    }

    #[test]
    fn preferred_address_zero_address() {
        // Either port being zero is bad.
        assert_invalid_spa(&mutate_spa(|v4, _, _| {
            v4.as_mut().unwrap().set_port(0);
        }));
        assert_invalid_spa(&mutate_spa(|_, v6, _| {
            v6.as_mut().unwrap().set_port(0);
        }));
        // Either IP being zero is bad.
        assert_invalid_spa(&mutate_spa(|v4, _, _| {
            v4.as_mut().unwrap().set_ip(Ipv4Addr::from(0));
        }));
        assert_invalid_spa(&mutate_spa(|_, v6, _| {
            v6.as_mut().unwrap().set_ip(Ipv6Addr::from(0));
        }));
        assert_invalid_spa(&mutate_spa(|_, v6, _| {
            v6.as_mut()
                .unwrap()
                .set_ip(Ipv4Addr::UNSPECIFIED.to_ipv6_mapped());
        }));
        // Either address being absent is OK.
        assert_valid_spa(&mutate_spa(|v4, _, _| {
            *v4 = None;
        }));
        assert_valid_spa(&mutate_spa(|_, v6, _| {
            *v6 = None;
        }));
        // Both addresses being absent is bad.
        assert_invalid_spa(&mutate_spa(|v4, v6, _| {
            *v4 = None;
            *v6 = None;
        }));
    }

    #[test]
    fn preferred_address_bad_cid() {
        assert_invalid_spa(&mutate_spa(|_, _, cid| {
            *cid = ConnectionId::from(&[]);
        }));
        assert_invalid_spa(&mutate_spa(|_, _, cid| {
            *cid = ConnectionId::from(&[0x0c; 21]);
        }));
    }

    #[test]
    fn preferred_address_truncated() {
        let spa = make_spa();
        let mut enc = Encoder::default();
        spa.encode(&mut enc, PreferredAddress);
        let mut dec = Decoder::from(&enc.as_ref()[..enc.len() - 1]);
        assert_eq!(
            TransportParameter::decode(Client, &mut dec).unwrap_err(),
            Error::NoMoreData
        );
    }

    #[test]
    #[should_panic(expected = "v4.is_some() || v6.is_some()")]
    fn preferred_address_neither() {
        _ = PreferredAddress::new(None, None);
    }

    #[test]
    #[should_panic(expected = ".is_unspecified")]
    fn preferred_address_v4_unspecified() {
        _ = PreferredAddress::new(Some(SocketAddrV4::new(Ipv4Addr::from(0), 443)), None);
    }

    #[test]
    #[should_panic(expected = "left != right")]
    fn preferred_address_v4_zero_port() {
        _ = PreferredAddress::new(
            Some(SocketAddrV4::new(Ipv4Addr::from(0xc000_0201), 0)),
            None,
        );
    }

    #[test]
    #[should_panic(expected = ".is_unspecified")]
    fn preferred_address_v6_unspecified() {
        _ = PreferredAddress::new(None, Some(SocketAddrV6::new(Ipv6Addr::from(0), 443, 0, 0)));
    }

    #[test]
    #[should_panic(expected = "left != right")]
    fn preferred_address_v6_zero_port() {
        _ = PreferredAddress::new(None, Some(SocketAddrV6::new(Ipv6Addr::from(1), 0, 0, 0)));
    }

    #[test]
    fn compatible_0rtt_ignored_values() {
        let mut tps_a = TransportParameters::default();
        tps_a.set(
            StatelessResetToken,
            TransportParameter::Bytes(vec![1, 2, 3]),
        );
        tps_a.set(IdleTimeout, TransportParameter::Integer(10));
        tps_a.set(MaxAckDelay, TransportParameter::Integer(22));
        tps_a.set(ActiveConnectionIdLimit, TransportParameter::Integer(33));

        let mut tps_b = TransportParameters::default();
        assert!(tps_a.ok_for_0rtt(&tps_b));
        assert!(tps_b.ok_for_0rtt(&tps_a));

        tps_b.set(
            StatelessResetToken,
            TransportParameter::Bytes(vec![8, 9, 10]),
        );
        tps_b.set(IdleTimeout, TransportParameter::Integer(100));
        tps_b.set(MaxAckDelay, TransportParameter::Integer(2));
        tps_b.set(ActiveConnectionIdLimit, TransportParameter::Integer(44));
        assert!(tps_a.ok_for_0rtt(&tps_b));
        assert!(tps_b.ok_for_0rtt(&tps_a));
    }

    #[test]
    fn compatible_0rtt_integers() {
        const INTEGER_KEYS: &[TransportParameterId] = &[
            InitialMaxData,
            InitialMaxStreamDataBidiLocal,
            InitialMaxStreamDataBidiRemote,
            InitialMaxStreamDataUni,
            InitialMaxStreamsBidi,
            InitialMaxStreamsUni,
            MaxUdpPayloadSize,
            MinAckDelay,
            MaxDatagramFrameSize,
        ];

        let mut tps_a = TransportParameters::default();
        for i in INTEGER_KEYS {
            tps_a.set(*i, TransportParameter::Integer(12));
        }

        let tps_b = tps_a.clone();
        assert!(tps_a.ok_for_0rtt(&tps_b));
        assert!(tps_b.ok_for_0rtt(&tps_a));

        // For each integer key, choose a new value that will be accepted.
        for i in INTEGER_KEYS {
            let mut tps_b = tps_a.clone();
            // Set a safe new value; reducing MIN_ACK_DELAY instead.
            let safe_value = if *i == MinAckDelay { 11 } else { 13 };
            tps_b.set(*i, TransportParameter::Integer(safe_value));
            // If the new value is not safe relative to the remembered value,
            // then we can't attempt 0-RTT with these parameters.
            assert!(!tps_a.ok_for_0rtt(&tps_b));
            // The opposite situation is fine.
            assert!(tps_b.ok_for_0rtt(&tps_a));
        }

        // Drop integer values and check that that is OK.
        for i in INTEGER_KEYS {
            let mut tps_b = tps_a.clone();
            tps_b.remove(*i);
            // A value that is missing from what is remembered is OK.
            assert!(tps_a.ok_for_0rtt(&tps_b));
            // A value that is remembered, but not current is not OK.
            assert!(!tps_b.ok_for_0rtt(&tps_a));
        }
    }

    /// `ACTIVE_CONNECTION_ID_LIMIT` can't be less than 2.
    #[test]
    fn active_connection_id_limit_min_2() {
        let mut tps = TransportParameters::default();

        // Intentionally set an invalid value for the ACTIVE_CONNECTION_ID_LIMIT transport
        // parameter.
        tps.params[ActiveConnectionIdLimit] = Some(TransportParameter::Integer(1));

        let mut enc = Encoder::default();
        tps.encode(&mut enc);

        // When decoding a set of transport parameters with an invalid ACTIVE_CONNECTION_ID_LIMIT
        // the result should be an error.
        let invalid_decode_result = TransportParameters::decode(Client, &mut enc.as_decoder());
        assert!(invalid_decode_result.is_err());
    }

    /// RFC 9000, Section 7.4: a duplicate transport parameter is a connection
    /// error of type [`TRANSPORT_PARAMETER_ERROR`].
    #[test]
    fn duplicate_tp_rejected() {
        let mut enc = Encoder::default();
        TransportParameter::Integer(10).encode(&mut enc, IdleTimeout);
        TransportParameter::Integer(20).encode(&mut enc, IdleTimeout);
        assert_eq!(
            TransportParameters::decode(Client, &mut enc.as_decoder()).unwrap_err(),
            Error::TransportParameter
        );
    }

    /// Distinct transport parameters next to each other still decode.
    #[test]
    fn distinct_tps_accepted() {
        let mut enc = Encoder::default();
        TransportParameter::Integer(10).encode(&mut enc, IdleTimeout);
        TransportParameter::Integer(20).encode(&mut enc, InitialMaxData);
        let tps =
            TransportParameters::decode(Client, &mut enc.as_decoder()).expect("should decode");
        assert_eq!(tps.get_integer(IdleTimeout), 10);
        assert_eq!(tps.get_integer(InitialMaxData), 20);
    }

    /// The `reset_stream_at` transport parameter is an empty parameter that round-trips.
    #[test]
    fn reset_stream_at_empty_round_trip() {
        let mut tps = TransportParameters::default();
        assert!(!tps.get_empty(ResetStreamAt));
        tps.set_empty(ResetStreamAt);
        assert!(tps.get_empty(ResetStreamAt));

        let mut enc = Encoder::default();
        tps.encode(&mut enc);
        let tps2 =
            TransportParameters::decode(Client, &mut enc.as_decoder()).expect("should decode");
        assert!(tps2.get_empty(ResetStreamAt));
        assert_eq!(tps, tps2);
    }

    /// A `reset_stream_at` transport parameter carrying a non-empty value is rejected.
    #[test]
    fn reset_stream_at_non_empty_rejected() {
        let mut enc = Encoder::default();
        enc.encode_varint(ResetStreamAt);
        enc.encode_vvec(&[0x01]); // non-empty content
        assert_eq!(
            TransportParameter::decode(Client, &mut enc.as_decoder()).unwrap_err(),
            Error::TooMuchData
        );
    }

    /// A duplicate `reset_stream_at` transport parameter is rejected.
    #[test]
    fn reset_stream_at_duplicate_rejected() {
        let mut enc = Encoder::default();
        TransportParameter::Empty.encode(&mut enc, ResetStreamAt);
        TransportParameter::Empty.encode(&mut enc, ResetStreamAt);
        assert_eq!(
            TransportParameters::decode(Client, &mut enc.as_decoder()).unwrap_err(),
            Error::TransportParameter
        );
    }

    /// When the server accepts 0-RTT it MUST NOT disable `reset_stream_at` on the resumed
    /// connection: if it was remembered (advertised on the original connection) but is not
    /// currently offered, 0-RTT must be rejected.
    #[test]
    fn reset_stream_at_ok_for_0rtt() {
        let mut remembered = TransportParameters::default();
        remembered.set_empty(ResetStreamAt);

        // Remembered, but not currently offered: 0-RTT is NOT OK (would disable the extension).
        let current = TransportParameters::default();
        assert!(!current.ok_for_0rtt(&remembered));

        // Offered on both sides: OK (extension is preserved).
        let mut current = TransportParameters::default();
        current.set_empty(ResetStreamAt);
        assert!(current.ok_for_0rtt(&remembered));

        // Not remembered, but currently offered: OK (offering more is always fine).
        let remembered = TransportParameters::default();
        assert!(current.ok_for_0rtt(&remembered));
    }

    #[test]
    fn versions_encode_decode() {
        const ENCODED: &[u8] = &[
            0x11, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x1a, 0x2a, 0x3a, 0x4a, 0x5a, 0x6a, 0x7a, 0x8a,
        ];
        let vn = TransportParameter::Versions {
            current: Version::Version1.wire_version(),
            other: vec![0x1a2a_3a4a, 0x5a6a_7a8a],
        };

        let mut enc = Encoder::default();
        vn.encode(&mut enc, VersionInformation);
        assert_eq!(enc.as_ref(), ENCODED);

        let mut dec = enc.as_decoder();
        let (id, decoded) = TransportParameter::decode(Client, &mut dec)
            .unwrap()
            .unwrap();
        assert_eq!(id, VersionInformation);
        assert_eq!(decoded, vn);
    }

    #[test]
    fn versions_truncated() {
        const TRUNCATED: &[u8] = &[
            0x80, 0xff, 0x73, 0xdb, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x1a, 0x2a, 0x3a, 0x4a, 0x5a,
            0x6a, 0x7a,
        ];
        let mut dec = Decoder::from(&TRUNCATED);
        assert_eq!(
            TransportParameter::decode(Client, &mut dec).unwrap_err(),
            Error::NoMoreData
        );
    }

    #[test]
    fn versions_zero() {
        const ZERO1: &[u8] = &[0x11, 0x04, 0x00, 0x00, 0x00, 0x00];
        const ZERO2: &[u8] = &[0x11, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00];

        let mut dec = Decoder::from(&ZERO1);
        assert_eq!(
            TransportParameter::decode(Client, &mut dec).unwrap_err(),
            Error::TransportParameter
        );
        let mut dec = Decoder::from(&ZERO2);
        assert_eq!(
            TransportParameter::decode(Client, &mut dec).unwrap_err(),
            Error::TransportParameter
        );
    }

    #[test]
    fn versions_equal_0rtt() {
        let mut current = TransportParameters::default();
        debug!("Current = {current:?}");
        current.set(
            VersionInformation,
            TransportParameter::Versions {
                current: Version::Version1.wire_version(),
                other: vec![0x1a2a_3a4a],
            },
        );

        let mut remembered = TransportParameters::default();
        // It's OK to not remember having versions.
        assert!(current.ok_for_0rtt(&remembered));
        // But it is bad in the opposite direction.
        assert!(!remembered.ok_for_0rtt(&current));

        // If the version matches, it's OK to use 0-RTT.
        remembered.set(
            VersionInformation,
            TransportParameter::Versions {
                current: Version::Version1.wire_version(),
                other: vec![0x5a6a_7a8a, 0x9aaa_baca],
            },
        );
        assert!(current.ok_for_0rtt(&remembered));
        assert!(remembered.ok_for_0rtt(&current));

        // An apparent "upgrade" is still cause to reject 0-RTT.
        remembered.set(
            VersionInformation,
            TransportParameter::Versions {
                current: Version::Version1.wire_version() + 1,
                other: vec![],
            },
        );
        assert!(!current.ok_for_0rtt(&remembered));
        assert!(!remembered.ok_for_0rtt(&current));
    }

    #[test]
    fn transport_parameter_id_display() {
        assert_eq!(InitialMaxData.to_string(), "InitialMaxData(0x04)");
        assert_eq!(format!("{IdleTimeout}"), "IdleTimeout(0x01)");
    }

    // Helper: encode an integer TP, then decode it.
    fn decode_tp_integer(tp: TransportParameterId, v: u64) -> crate::Res<TransportParameter> {
        let mut enc = Encoder::default();
        TransportParameter::Integer(v).encode(&mut enc, tp);
        TransportParameter::decode(Client, &mut enc.as_decoder()).map(|r| r.unwrap().1)
    }

    #[test]
    fn max_streams_boundary() {
        for tp in [InitialMaxStreamsBidi, InitialMaxStreamsUni] {
            assert!(decode_tp_integer(tp, MAX_STREAMS_LIMIT).is_ok(), "{tp}");
            assert!(
                decode_tp_integer(tp, MAX_STREAMS_LIMIT + 1).is_err(),
                "{tp}"
            );
        }
    }

    #[test]
    fn max_udp_payload_size_boundary() {
        let min = to_u64(crate::packet::MIN_INITIAL_PACKET_SIZE);
        assert!(decode_tp_integer(MaxUdpPayloadSize, min).is_ok());
        assert!(decode_tp_integer(MaxUdpPayloadSize, min - 1).is_err());
        if cfg!(target_pointer_width = "32") {
            assert_eq!(
                decode_tp_integer(MaxUdpPayloadSize, to_u64(usize::MAX) + 1),
                Ok(TransportParameter::Integer(to_u64(usize::MAX))),
                "we should cap UDP payload size on 32-bit builds"
            );
        }
    }

    #[test]
    fn ack_delay_exponent_boundary() {
        assert!(decode_tp_integer(AckDelayExponent, 20).is_ok());
        assert!(decode_tp_integer(AckDelayExponent, 21).is_err());
    }

    #[test]
    fn max_ack_delay_boundary() {
        // Just below the limit is valid.
        assert!(decode_tp_integer(MaxAckDelay, (1 << 14) - 1).is_ok());
        // At the limit (2^14) and above is an error.
        assert!(decode_tp_integer(MaxAckDelay, 1 << 14).is_err());
    }

    #[test]
    fn min_ack_delay_boundary() {
        // Just below the limit is valid.
        assert!(decode_tp_integer(MinAckDelay, (1 << 24) - 1).is_ok());
        // At the limit (not strictly less than) is an error.
        assert!(decode_tp_integer(MinAckDelay, 1 << 24).is_err());
    }

    #[test]
    fn trailing_data_rejected() {
        // Encode a valid TP, then append an extra byte.
        let mut enc = Encoder::default();
        TransportParameter::Integer(42).encode(&mut enc, IdleTimeout);
        // Corrupt the vvec length to include an extra zero byte.
        let mut raw: Vec<u8> = enc.into();
        raw[1] += 1; // Increase vvec length by 1 (second byte is the length).
        raw.push(0xff); // Extra byte.
        let err = TransportParameter::decode(Client, &mut Decoder::from(&raw[..])).unwrap_err();
        assert_eq!(err, Error::TooMuchData);
    }

    #[test]
    fn preferred_address_max_len_cid() {
        // A preferred address with exactly MAX_LEN-byte CID should be valid.
        let cid = ConnectionId::from(&[0xab; ConnectionId::MAX_LEN]);
        let spa = mutate_spa(|_, _, cid_out| *cid_out = cid);
        assert_valid_spa(&spa);
    }

    #[test]
    fn server_rejects_server_only_tparams() {
        fixture_init();
        for tp in [
            OriginalDestinationConnectionId,
            RetrySourceConnectionId,
            StatelessResetToken,
            PreferredAddress,
        ] {
            let mut enc = Encoder::default();
            let value = match tp {
                PreferredAddress => {
                    let v4addr = SocketAddrV4::from_str("1.2.3.4:23").unwrap();
                    TransportParameter::PreferredAddress {
                        v4: Some(v4addr),
                        v6: None,
                        cid: ConnectionId::generate_initial(),
                        srt: Srt::new([8; Srt::LEN]),
                    }
                }
                StatelessResetToken => TransportParameter::Bytes(vec![7; Srt::LEN]),
                OriginalDestinationConnectionId | RetrySourceConnectionId => {
                    TransportParameter::Bytes(vec![0xab; 8])
                }
                _ => unreachable!(),
            };
            value.encode(&mut enc, tp);
            let res = TransportParameter::decode(Server, &mut enc.as_decoder());
            assert_eq!(res, Err(Error::TransportParameter));
        }
    }
}
