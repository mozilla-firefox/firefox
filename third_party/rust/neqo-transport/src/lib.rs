// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(coverage_nightly, feature(coverage_attribute))]

use nss::Error as CryptoError;
use thiserror::Error;
use tracing::debug;

mod ackrate;
#[cfg(fuzzing)]
pub mod addr_valid;
#[cfg(not(fuzzing))]
mod addr_valid;
mod cc;
mod cid;
mod connection;
mod crypto;
mod datagram_queue;
pub mod ecn;
mod events;
mod fc;
#[cfg(any(fuzzing, feature = "bench"))]
pub mod frame;
#[cfg(not(any(fuzzing, feature = "bench")))]
mod frame;
mod pace;
#[cfg(any(fuzzing, feature = "bench"))]
pub mod packet;
#[cfg(not(any(fuzzing, feature = "bench")))]
mod packet;
mod path;
mod pmtud;
mod qlog;
mod quic_datagrams;
#[cfg(feature = "bench")]
pub mod recovery;
#[cfg(not(feature = "bench"))]
mod recovery;
pub mod recv_stream;
mod rtt;
mod saved;
mod scone;
pub mod send_stream;
mod sender;
pub mod server;
mod sni;
mod stateless_reset;
mod stats;
pub mod stream_id;
pub mod streams;
pub mod tparams;
mod tracking;
pub mod version;

pub use self::{
    cc::{CongestionControl, CongestionTrigger, HyStartCssBaseline, SlowStart},
    cid::{
        ConnectionId, ConnectionIdDecoder, ConnectionIdGenerator, ConnectionIdRef,
        EmptyConnectionIdGenerator, RandomConnectionIdGenerator,
    },
    connection::{
        Connection, Output, OutputBatch, State, ZeroRttState,
        params::{
            ConnectionParameters, INITIAL_LOCAL_MAX_DATA, INITIAL_LOCAL_MAX_STREAM_DATA,
            MAX_DATAGRAM_FRAME_SIZE, MAX_LOCAL_MAX_STREAM_DATA, StreamDataLimit,
        },
    },
    datagram_queue::{DatagramId, DatagramQueueCapacity, DatagramQueueOutcome, DroppedDatagrams},
    events::{ConnectionEvent, ConnectionEvents},
    frame::CloseError,
    packet::MIN_INITIAL_PACKET_SIZE,
    pmtud::Pmtud,
    quic_datagrams::DatagramTracking,
    rtt::DEFAULT_INITIAL_RTT,
    sni::find_sni,
    stateless_reset::Token,
    stats::{SlowStartExitReason, Stats},
    stream_id::{MAX_STREAMS_LIMIT, StreamId, StreamType},
    version::Version,
};
#[cfg(feature = "bench")]
pub use self::{
    crypto::{CryptoDxState, CryptoStates},
    fc::SenderFlowControl,
    pace::Pacer,
    stats::FrameStats,
};

pub type TransportError = u64;
const ERROR_APPLICATION_CLOSE: TransportError = 12;
const ERROR_CRYPTO_BUFFER_EXCEEDED: TransportError = 13;
const ERROR_AEAD_LIMIT_REACHED: TransportError = 15;
const ERROR_VERSION_NEGOTIATION: TransportError = 17;

#[derive(Clone, Debug, PartialEq, PartialOrd, Ord, Eq, Error)]
pub enum Error {
    #[error("no error")]
    None,
    #[error("internal error")]
    Internal,
    #[error("connection refused")]
    ConnectionRefused,
    #[error("flow control error")]
    FlowControl,
    #[error("stream limit exceeded")]
    StreamLimit,
    #[error("stream state error")]
    StreamState,
    #[error("stream final size error")]
    FinalSize,
    #[error("frame encoding error")]
    FrameEncoding,
    #[error("transport parameter error")]
    TransportParameter,
    #[error("protocol violation")]
    ProtocolViolation,
    #[error("invalid token")]
    InvalidToken,
    #[error("application error")]
    Application,
    #[error("crypto buffer exceeded")]
    CryptoBufferExceeded,
    #[error("crypto error: {0}")]
    Crypto(#[source] CryptoError),
    #[error("qlog error")]
    Qlog,
    #[error("crypto alert: {0}")]
    CryptoAlert(u8),
    #[error("ECH retry")]
    EchRetry(Vec<u8>),

    // All internal errors from here.  Please keep these sorted.
    #[error("packet acknowledged but never sent")]
    AckedUnsentPacket,
    #[error("connection ID limit exceeded")]
    ConnectionIdLimitExceeded,
    #[error("connection IDs exhausted")]
    ConnectionIdsExhausted,
    #[error("invalid connection state")]
    ConnectionState,
    #[error("decryption error")]
    Decrypt,
    #[error("disabled version")]
    DisabledVersion,
    #[error("no packets received for longer than the idle timeout")]
    IdleTimeout,
    #[error("integer overflow")]
    IntegerOverflow,
    #[error("invalid input")]
    InvalidInput,
    #[error("invalid migration")]
    InvalidMigration,
    #[error("an invalid packet was dropped (internal use only)")]
    InvalidPacket,
    #[error("invalid resumption token")]
    InvalidResumptionToken,
    #[error("invalid retry packet dropped (internal use only)")]
    InvalidRetry,
    #[error("invalid stream ID")]
    InvalidStreamId,
    #[error("keys discarded for epoch {0:?}")]
    KeysDiscarded(crypto::Epoch),
    /// Packet protection keys are exhausted.  Also used when too many key
    /// updates have happened.
    #[error("keys exhausted")]
    KeysExhausted,
    /// Packet protection keys aren't available yet for the identified space.
    #[error("keys pending for epoch {0:?} (internal use only)")]
    KeysPending(crypto::Epoch),
    /// An attempt to update keys can be blocked if a packet sent with the
    /// current keys hasn't been acknowledged.
    #[error("key update blocked")]
    KeyUpdateBlocked,
    #[error("no available path")]
    NoAvailablePath,
    #[error("no more data")]
    NoMoreData,
    #[error("not available")]
    NotAvailable,
    #[error("not connected")]
    NotConnected,
    #[error("packet number overlap")]
    PacketNumberOverlap,
    #[error("peer application error: 0x{0:x}")]
    PeerApplication(AppError),
    #[error("peer error: {0}")]
    Peer(TransportError),
    #[error("stateless reset")]
    StatelessReset,
    /// Too many consecutive PTOs went unacknowledged, so the path is assumed to be
    /// a black hole. A distinct variant so the application can tell this apart, but
    /// reported on the wire as `NO_VIABLE_PATH` (there is no dedicated code).
    #[error("too many PTOs without acknowledgement; connection assumed broken")]
    TooManyPtos,
    #[error("too much data")]
    TooMuchData,
    #[error("unexpected message")]
    UnexpectedMessage,
    #[error("unknown connection ID")]
    UnknownConnectionId,
    #[error("unknown frame type")]
    UnknownFrameType,
    #[error("version negotiation")]
    VersionNegotiation,
    #[error("wrong role")]
    WrongRole,
    #[error("unknown transport parameter (internal use only)")]
    UnknownTransportParameter,
}

impl Error {
    #[must_use]
    pub fn code(&self) -> TransportError {
        match self {
            Self::None | Self::IdleTimeout | Self::Peer(_) | Self::PeerApplication(_) => 0,
            Self::ConnectionRefused => 2,
            Self::FlowControl => 3,
            Self::StreamLimit => 4,
            Self::StreamState => 5,
            Self::FinalSize => 6,
            Self::FrameEncoding | Self::UnknownFrameType => 7,
            Self::TransportParameter => 8,
            Self::ConnectionIdLimitExceeded => 9,
            Self::ProtocolViolation | Self::AckedUnsentPacket => 10,
            Self::InvalidToken => 11,
            Self::KeysExhausted => ERROR_AEAD_LIMIT_REACHED,
            Self::Application => ERROR_APPLICATION_CLOSE,
            Self::NoAvailablePath | Self::TooManyPtos => 16,
            Self::CryptoBufferExceeded => ERROR_CRYPTO_BUFFER_EXCEEDED,
            Self::CryptoAlert(a) => 0x100 + u64::from(*a),
            // As we have a special error code for ECH fallbacks, we lose the alert.
            // Send the server "ech_required" directly.
            Self::EchRetry(_) => 0x100 + 121,
            Self::VersionNegotiation => ERROR_VERSION_NEGOTIATION,
            // All the rest are internal errors.
            _ => 1,
        }
    }
}

impl From<CryptoError> for Error {
    fn from(err: CryptoError) -> Self {
        debug!("Crypto operation failed {err:?}");
        match err {
            CryptoError::EchRetry(config) => Self::EchRetry(config),
            _ => Self::Crypto(err),
        }
    }
}

impl From<std::num::TryFromIntError> for Error {
    fn from(_: std::num::TryFromIntError) -> Self {
        Self::IntegerOverflow
    }
}

pub type AppError = u64;

/// Reason why a connection closed.
#[derive(Clone, Debug, PartialEq, PartialOrd, Ord, Eq)]
pub enum CloseReason {
    Transport(Error),
    Application(AppError),
}

impl CloseReason {
    /// Checks enclosed error for [`Error::None`] and
    /// [`CloseReason::Application`] with code `0`.
    #[must_use]
    pub const fn is_error(&self) -> bool {
        !matches!(self, Self::Transport(Error::None) | Self::Application(0),)
    }
}

pub type Res<T> = Result<T, Error>;

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::{CloseReason, Error};

    #[test]
    fn error_codes() {
        for (err, code) in [
            (Error::None, 0),
            (Error::IdleTimeout, 0),
            (Error::Peer(0), 0),
            (Error::PeerApplication(0), 0),
            (Error::ConnectionRefused, 2),
            (Error::FlowControl, 3),
            (Error::StreamLimit, 4),
            (Error::StreamState, 5),
            (Error::FinalSize, 6),
            (Error::FrameEncoding, 7),
            (Error::UnknownFrameType, 7),
            (Error::TransportParameter, 8),
            (Error::ConnectionIdLimitExceeded, 9),
            (Error::ProtocolViolation, 10),
            (Error::AckedUnsentPacket, 10),
            (Error::InvalidToken, 11),
            (Error::KeysExhausted, 15),
            (Error::Application, 12),
            (Error::NoAvailablePath, 16),
            (Error::CryptoBufferExceeded, 13),
            (Error::CryptoAlert(0x2a), 0x12a),
            (Error::EchRetry(vec![]), 0x179),
            (Error::VersionNegotiation, 17),
            (Error::Internal, 1),
            (Error::TooManyPtos, 16),
        ] {
            assert_eq!(err.code(), code);
        }
    }

    #[test]
    fn close_reason_is_error() {
        assert!(!CloseReason::Transport(Error::None).is_error());
        assert!(!CloseReason::Application(0).is_error());
        assert!(CloseReason::Transport(Error::Internal).is_error());
        assert!(CloseReason::Application(1).is_error());
    }

    #[test]
    fn error_from_impls() {
        assert_eq!(
            Error::from(nss::Error::EchRetry(vec![1, 2])),
            Error::EchRetry(vec![1, 2])
        );
        assert!(matches!(
            Error::from(u64::try_from(-1_i32).unwrap_err()),
            Error::IntegerOverflow
        ));
    }
}
