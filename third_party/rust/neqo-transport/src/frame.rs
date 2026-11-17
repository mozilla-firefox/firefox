// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Directly relating to QUIC frames.

#![cfg_attr(
    any(fuzzing, feature = "bench"),
    expect(
        clippy::module_name_repetitions,
        reason = "`frame` is only public API when fuzzing or the `bench` feature is enabled."
    )
)]

use std::{num::NonZeroUsize, ops::RangeInclusive};

use neqo_common::{Buffer, Decoder, Encoder, MAX_VARINT};
use strum::FromRepr;
use tracing::trace;

use crate::{
    AppError, ConnectionId, Error, Res, TransportError, ecn, packet,
    stateless_reset::Token as Srt,
    stream_id::{MAX_STREAMS_LIMIT, StreamId, StreamType},
};

/// The length of the data carried by `PATH_CHALLENGE` and `PATH_RESPONSE` frames, see
/// [RFC 9000, Section 19.17](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.17).
pub const PATH_CHALLENGE_LEN: usize = 8;

#[repr(u64)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, FromRepr)]
pub enum FrameType {
    Padding = 0x0,
    Ping = 0x1,
    Ack = 0x2,
    AckEcn = 0x3,
    ResetStream = 0x4,
    StopSending = 0x5,
    Crypto = 0x6,
    NewToken = 0x7,
    Stream = 0x08, // + 0b000
    StreamWithFin = 0x08 + 0b001,
    StreamWithLen = 0x08 + 0b010,
    StreamWithLenFin = 0x08 + 0b011,
    StreamWithOff = 0x08 + 0b100,
    StreamWithOffFin = 0x08 + 0b101,
    StreamWithOffLen = 0x08 + 0b110,
    StreamWithOffLenFin = 0x08 + 0b111,
    MaxData = 0x10,
    MaxStreamData = 0x11,
    MaxStreamsBiDi = 0x12,
    MaxStreamsUniDi = 0x13,
    DataBlocked = 0x14,
    StreamDataBlocked = 0x15,
    StreamsBlockedBiDi = 0x16,
    StreamsBlockedUniDi = 0x17,
    NewConnectionId = 0x18,
    RetireConnectionId = 0x19,
    PathChallenge = 0x1a,
    PathResponse = 0x1b,
    ConnectionCloseTransport = 0x1c,
    ConnectionCloseApplication = 0x1d,
    HandshakeDone = 0x1e,
    // draft-ietf-quic-reliable-stream-reset
    ResetStreamAt = 0x24,
    // draft-ietf-quic-ack-delay
    AckFrequency = 0xaf,
    // draft-ietf-quic-datagram
    Datagram = 0x30,
    DatagramWithLen = 0x31,
}

impl From<FrameType> for u64 {
    fn from(val: FrameType) -> Self {
        val as Self
    }
}

impl From<FrameType> for u8 {
    fn from(val: FrameType) -> Self {
        val as Self
    }
}

impl TryFrom<u64> for FrameType {
    type Error = Error;

    fn try_from(value: u64) -> Result<Self, Self::Error> {
        Self::from_repr(value).ok_or(Error::UnknownFrameType)
    }
}

impl FrameType {
    const fn is_stream_with_length(self) -> bool {
        matches!(
            self,
            Self::StreamWithLen
                | Self::StreamWithLenFin
                | Self::StreamWithOffLen
                | Self::StreamWithOffLenFin
        )
    }

    const fn is_stream_with_offset(self) -> bool {
        matches!(
            self,
            Self::StreamWithOff
                | Self::StreamWithOffFin
                | Self::StreamWithOffLen
                | Self::StreamWithOffLenFin
        )
    }
    const fn is_stream_with_fin(self) -> bool {
        matches!(
            self,
            Self::StreamWithFin
                | Self::StreamWithLenFin
                | Self::StreamWithOffFin
                | Self::StreamWithOffLenFin
        )
    }
}

impl TryFrom<FrameType> for StreamType {
    type Error = Error;

    fn try_from(value: FrameType) -> Result<Self, Self::Error> {
        match value {
            FrameType::MaxStreamsBiDi | FrameType::StreamsBlockedBiDi => Ok(Self::BiDi),
            FrameType::MaxStreamsUniDi | FrameType::StreamsBlockedUniDi => Ok(Self::UniDi),
            _ => Err(Error::FrameEncoding),
        }
    }
}

#[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Clone, Copy)]
pub enum CloseError {
    Transport(TransportError),
    Application(AppError),
}

impl CloseError {
    #[must_use]
    pub const fn code(&self) -> u64 {
        match self {
            Self::Transport(c) | Self::Application(c) => *c,
        }
    }
}

impl From<std::array::TryFromSliceError> for Error {
    fn from(_err: std::array::TryFromSliceError) -> Self {
        Self::FrameEncoding
    }
}

#[derive(PartialEq, Eq, Debug, Default, Clone)]
pub struct AckRange {
    gap: u64,
    range: u64,
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum Frame<'a> {
    Padding(u16),
    Ping,
    Ack {
        largest_acknowledged: u64,
        ack_delay: u64,
        first_ack_range: u64,
        ack_ranges: Vec<AckRange>,
        ecn_count: Option<ecn::Count>,
    },
    ResetStream {
        stream_id: StreamId,
        application_error_code: AppError,
        final_size: u64,
    },
    ResetStreamAt {
        stream_id: StreamId,
        application_error_code: AppError,
        final_size: u64,
        reliable_size: u64,
    },
    StopSending {
        stream_id: StreamId,
        application_error_code: AppError,
    },
    Crypto {
        offset: u64,
        data: &'a [u8],
    },
    NewToken {
        token: &'a [u8],
    },
    Stream {
        stream_id: StreamId,
        offset: u64,
        data: &'a [u8],
        fin: bool,
        fill: bool,
    },
    MaxData {
        maximum_data: u64,
    },
    MaxStreamData {
        stream_id: StreamId,
        maximum_stream_data: u64,
    },
    MaxStreams {
        stream_type: StreamType,
        maximum_streams: u64,
    },
    DataBlocked {
        data_limit: u64,
    },
    StreamDataBlocked {
        stream_id: StreamId,
        stream_data_limit: u64,
    },
    StreamsBlocked {
        stream_type: StreamType,
        stream_limit: u64,
    },
    NewConnectionId {
        sequence_number: u64,
        retire_prior: u64,
        connection_id: &'a [u8],
        stateless_reset_token: Srt,
    },
    RetireConnectionId {
        sequence_number: u64,
    },
    PathChallenge {
        data: [u8; PATH_CHALLENGE_LEN],
    },
    PathResponse {
        data: [u8; PATH_CHALLENGE_LEN],
    },
    ConnectionClose {
        error_code: CloseError,
        frame_type: u64,
        // Not a reference as we use this to hold the value.
        // This is not used in optimized builds anyway.
        reason_phrase: String,
    },
    HandshakeDone,
    AckFrequency {
        /// The current ACK frequency sequence number.
        seqno: u64,
        /// The number of contiguous packets that can be received without
        /// acknowledging immediately.
        tolerance: u64,
        /// The time to delay after receiving the first packet that is
        /// not immediately acknowledged.
        delay: u64,
        /// Ignore reordering when deciding to immediately acknowledge.
        ignore_order: bool,
    },
    Datagram {
        data: &'a [u8],
        /// Encoded size of the length field, `None` if the frame has none.
        /// Needed to check the whole frame against our `max_datagram_frame_size`
        /// (RFC 9221, Section 3); can't be derived from `data.len()`, as the
        /// varint may be non-minimal.
        len_field_len: Option<NonZeroUsize>,
    },
}

impl<'a> Frame<'a> {
    #[must_use]
    pub const fn get_type(&self) -> FrameType {
        match self {
            Self::Padding { .. } => FrameType::Padding,
            Self::Ping => FrameType::Ping,
            Self::Ack { .. } => FrameType::Ack,
            Self::ResetStream { .. } => FrameType::ResetStream,
            Self::ResetStreamAt { .. } => FrameType::ResetStreamAt,
            Self::StopSending { .. } => FrameType::StopSending,
            Self::Crypto { .. } => FrameType::Crypto,
            Self::NewToken { .. } => FrameType::NewToken,
            Self::Stream {
                fin, offset, fill, ..
            } => Self::stream_type(*fin, *offset > 0, *fill),
            Self::MaxData { .. } => FrameType::MaxData,
            Self::MaxStreamData { .. } => FrameType::MaxStreamData,
            Self::MaxStreams { stream_type, .. } => match stream_type {
                StreamType::BiDi => FrameType::MaxStreamsBiDi,
                StreamType::UniDi => FrameType::MaxStreamsUniDi,
            },
            Self::DataBlocked { .. } => FrameType::DataBlocked,
            Self::StreamDataBlocked { .. } => FrameType::StreamDataBlocked,
            Self::StreamsBlocked { stream_type, .. } => match stream_type {
                StreamType::BiDi => FrameType::StreamsBlockedBiDi,
                StreamType::UniDi => FrameType::StreamsBlockedUniDi,
            },
            Self::NewConnectionId { .. } => FrameType::NewConnectionId,
            Self::RetireConnectionId { .. } => FrameType::RetireConnectionId,
            Self::PathChallenge { .. } => FrameType::PathChallenge,
            Self::PathResponse { .. } => FrameType::PathResponse,
            Self::ConnectionClose { error_code, .. } => match error_code {
                CloseError::Transport(_) => FrameType::ConnectionCloseTransport,
                CloseError::Application(_) => FrameType::ConnectionCloseApplication,
            },
            Self::HandshakeDone => FrameType::HandshakeDone,
            Self::AckFrequency { .. } => FrameType::AckFrequency,
            Self::Datagram { len_field_len, .. } => match len_field_len {
                Some(_) => FrameType::DatagramWithLen,
                None => FrameType::Datagram,
            },
        }
    }

    #[must_use]
    pub const fn is_stream(&self) -> bool {
        matches!(
            self,
            Self::ResetStream { .. }
                | Self::ResetStreamAt { .. }
                | Self::StopSending { .. }
                | Self::Stream { .. }
                | Self::MaxData { .. }
                | Self::MaxStreamData { .. }
                | Self::MaxStreams { .. }
                | Self::DataBlocked { .. }
                | Self::StreamDataBlocked { .. }
                | Self::StreamsBlocked { .. }
        )
    }

    #[must_use]
    pub const fn stream_type(fin: bool, nonzero_offset: bool, fill: bool) -> FrameType {
        match (nonzero_offset, fill, fin) {
            (false, true, false) => FrameType::Stream,
            (false, true, true) => FrameType::StreamWithFin,
            (false, false, false) => FrameType::StreamWithLen,
            (false, false, true) => FrameType::StreamWithLenFin,
            (true, true, false) => FrameType::StreamWithOff,
            (true, true, true) => FrameType::StreamWithOffFin,
            (true, false, false) => FrameType::StreamWithOffLen,
            (true, false, true) => FrameType::StreamWithOffLenFin,
        }
    }

    /// If the frame causes a recipient to generate an ACK within its
    /// advertised maximum acknowledgement delay.
    #[must_use]
    pub const fn ack_eliciting(&self) -> bool {
        !matches!(
            self,
            Self::Ack { .. } | Self::Padding { .. } | Self::ConnectionClose { .. }
        )
    }

    /// If the frame can be sent in a path probe
    /// without initiating migration to that path.
    #[must_use]
    pub const fn path_probing(&self) -> bool {
        matches!(
            self,
            Self::Padding { .. }
                | Self::NewConnectionId { .. }
                | Self::PathChallenge { .. }
                | Self::PathResponse { .. }
        )
    }

    /// Converts `AckRanges` as encoded in a ACK frame (see -transport
    /// 19.3.1) into ranges of acked packets (end, start), inclusive of
    /// start and end values.
    ///
    /// # Errors
    ///
    /// Returns an error if the ranges are invalid.
    pub fn decode_ack_frame(
        largest_acked: u64,
        first_ack_range: u64,
        ack_ranges: &[AckRange],
    ) -> Res<Vec<RangeInclusive<u64>>> {
        let mut acked_ranges = Vec::with_capacity(ack_ranges.len() + 1);

        if largest_acked < first_ack_range {
            return Err(Error::FrameEncoding);
        }
        acked_ranges.push((largest_acked - first_ack_range)..=largest_acked);
        if !ack_ranges.is_empty() && largest_acked < first_ack_range + 1 {
            return Err(Error::FrameEncoding);
        }
        let mut cur = if ack_ranges.is_empty() {
            0
        } else {
            largest_acked - first_ack_range - 1
        };
        for r in ack_ranges {
            if cur < r.gap + 1 {
                return Err(Error::FrameEncoding);
            }
            cur = cur - r.gap - 1;

            if cur < r.range {
                return Err(Error::FrameEncoding);
            }
            acked_ranges.push((cur - r.range)..=cur);

            if cur > r.range + 1 {
                cur -= r.range + 1;
            } else {
                cur -= r.range;
            }
        }

        Ok(acked_ranges)
    }

    #[must_use]
    pub fn dump(&self) -> String {
        match self {
            Self::Crypto { offset, data } => {
                format!("Crypto {{ offset: {offset}, len: {} }}", data.len())
            }
            Self::Stream {
                stream_id,
                offset,
                fill,
                data,
                fin,
            } => format!(
                "Stream {{ stream_id: {}, offset: {offset}, len: {}{}, fin: {fin} }}",
                stream_id.as_u64(),
                if *fill { ">>" } else { "" },
                data.len(),
            ),
            Self::Padding(length) => format!("Padding {{ len: {length} }}"),
            Self::Datagram { data, .. } => format!("Datagram {{ len: {} }}", data.len()),
            _ => format!("{self:?}"),
        }
    }

    #[must_use]
    pub fn is_allowed(&self, pt: packet::Type) -> bool {
        match self {
            Self::Padding { .. } | Self::Ping => true,
            Self::Crypto { .. }
            | Self::Ack { .. }
            | Self::ConnectionClose {
                error_code: CloseError::Transport(_),
                ..
            } => pt != packet::Type::ZeroRtt,
            Self::NewToken { .. }
            | Self::ConnectionClose { .. }
            | Self::PathResponse { .. }
            | Self::HandshakeDone => pt == packet::Type::Short,
            _ => pt == packet::Type::ZeroRtt || pt == packet::Type::Short,
        }
    }

    /// # Errors
    ///
    /// Returns an error if the frame cannot be decoded.
    #[expect(
        clippy::too_many_lines,
        reason = "Yeah, but it's a nice match statement."
    )]
    pub fn decode(dec: &mut Decoder<'a>) -> Res<Self> {
        /// Maximum ACK Range Count in ACK Frame
        ///
        /// Given a max UDP datagram size of 64k bytes and a minimum ACK Range size of 2
        /// bytes (2 QUIC varints), a single datagram can at most contain 32k ACK
        /// Ranges.
        ///
        /// Note that the maximum (jumbogram) Ethernet MTU of 9216 or on the
        /// Internet the regular Ethernet MTU of 1518 are more realistically to
        /// be the limiting factor. Though for simplicity the higher limit is chosen.
        const MAX_ACK_RANGE_COUNT: u64 = 32 * 1024;

        fn d<T>(v: Option<T>) -> Res<T> {
            v.ok_or(Error::FrameEncoding)
        }
        fn dv(dec: &mut Decoder) -> Res<u64> {
            d(dec.decode_varint())
        }

        fn decode_ack<'a>(dec: &mut Decoder<'a>, ecn: bool) -> Res<Frame<'a>> {
            let la = dv(dec)?;
            let ad = dv(dec)?;
            let nr = dv(dec).and_then(|nr| {
                if nr < MAX_ACK_RANGE_COUNT {
                    Ok(nr)
                } else {
                    Err(Error::FrameEncoding)
                }
            })?;
            let fa = dv(dec)?;
            let mut arr: Vec<AckRange> = Vec::with_capacity(usize::try_from(nr)?);
            for _ in 0..nr {
                let ar = AckRange {
                    gap: dv(dec)?,
                    range: dv(dec)?,
                };
                arr.push(ar);
            }

            // Now check for the values for ACK_ECN.
            let ecn_count = ecn
                .then(|| -> Res<ecn::Count> {
                    Ok(ecn::Count::new(0, dv(dec)?, dv(dec)?, dv(dec)?))
                })
                .transpose()?;

            Ok(Frame::Ack {
                largest_acknowledged: la,
                ack_delay: ad,
                first_ack_range: fa,
                ack_ranges: arr,
                ecn_count,
            })
        }

        // Check for minimal encoding of frame type.
        let pos = dec.offset();
        let t = dv(dec)?;
        // RFC 9000, Section 12.4:
        //
        // The Frame Type field uses a variable-length integer encoding [...],
        // with one exception. To ensure simple and efficient implementations of
        // frame parsing, a frame type MUST use the shortest possible encoding.
        if Encoder::varint_len(t) != dec.offset() - pos {
            return Err(Error::ProtocolViolation);
        }

        let t = t.try_into()?;
        match t {
            FrameType::Padding => {
                // t itself + any additional `Frame::Padding`
                (1 + dec.skip_while(u8::from(FrameType::Padding)))
                    .try_into()
                    .map(Self::Padding)
                    .map_err(|_| Error::TooMuchData)
            }
            FrameType::Ping => Ok(Self::Ping),
            FrameType::ResetStream => Ok(Self::ResetStream {
                stream_id: StreamId::from(dv(dec)?),
                application_error_code: dv(dec)?,
                final_size: dv(dec)?,
            }),
            FrameType::ResetStreamAt => {
                let stream_id = StreamId::from(dv(dec)?);
                let application_error_code = dv(dec)?;
                let final_size = dv(dec)?;
                let reliable_size = dv(dec)?;
                // Reject `reliable_size > final_size` at the earliest point.
                if reliable_size > final_size {
                    return Err(Error::FrameEncoding);
                }
                Ok(Self::ResetStreamAt {
                    stream_id,
                    application_error_code,
                    final_size,
                    reliable_size,
                })
            }
            FrameType::Ack => decode_ack(dec, false),
            FrameType::AckEcn => decode_ack(dec, true),
            FrameType::StopSending => Ok(Self::StopSending {
                stream_id: StreamId::from(dv(dec)?),
                application_error_code: dv(dec)?,
            }),
            FrameType::Crypto => {
                let offset = dv(dec)?;
                let data = d(dec.decode_vvec())?;
                if offset + u64::try_from(data.len())? > MAX_VARINT {
                    return Err(Error::FrameEncoding);
                }
                Ok(Self::Crypto { offset, data })
            }
            FrameType::NewToken => {
                let token = d(dec.decode_vvec())?;
                if token.is_empty() {
                    return Err(Error::FrameEncoding);
                }
                Ok(Self::NewToken { token })
            }
            FrameType::Stream
            | FrameType::StreamWithFin
            | FrameType::StreamWithLen
            | FrameType::StreamWithLenFin
            | FrameType::StreamWithOff
            | FrameType::StreamWithOffFin
            | FrameType::StreamWithOffLen
            | FrameType::StreamWithOffLenFin => {
                let s = dv(dec)?;
                let o = if t.is_stream_with_offset() {
                    dv(dec)?
                } else {
                    0
                };
                let fill = !t.is_stream_with_length();
                let data = if fill {
                    trace!("STREAM frame, extends to the end of the packet");
                    dec.decode_remainder()
                } else {
                    trace!("STREAM frame, with length");
                    d(dec.decode_vvec())?
                };
                if o + u64::try_from(data.len())? > MAX_VARINT {
                    return Err(Error::FrameEncoding);
                }
                Ok(Self::Stream {
                    fin: t.is_stream_with_fin(),
                    stream_id: StreamId::from(s),
                    offset: o,
                    data,
                    fill,
                })
            }
            FrameType::MaxData => Ok(Self::MaxData {
                maximum_data: dv(dec)?,
            }),
            FrameType::MaxStreamData => Ok(Self::MaxStreamData {
                stream_id: StreamId::from(dv(dec)?),
                maximum_stream_data: dv(dec)?,
            }),
            FrameType::MaxStreamsBiDi | FrameType::MaxStreamsUniDi => {
                let m = dv(dec)?;
                if m > MAX_STREAMS_LIMIT {
                    return Err(Error::FrameEncoding);
                }
                Ok(Self::MaxStreams {
                    stream_type: t.try_into()?,
                    maximum_streams: m,
                })
            }
            FrameType::DataBlocked => Ok(Self::DataBlocked {
                data_limit: dv(dec)?,
            }),
            FrameType::StreamDataBlocked => Ok(Self::StreamDataBlocked {
                stream_id: dv(dec)?.into(),
                stream_data_limit: dv(dec)?,
            }),
            FrameType::StreamsBlockedBiDi | FrameType::StreamsBlockedUniDi => {
                let m = dv(dec)?;
                if m > MAX_STREAMS_LIMIT {
                    return Err(Error::FrameEncoding);
                }
                Ok(Self::StreamsBlocked {
                    stream_type: t.try_into()?,
                    stream_limit: m,
                })
            }
            FrameType::NewConnectionId => {
                let sequence_number = dv(dec)?;
                let retire_prior = dv(dec)?;
                if retire_prior > sequence_number {
                    return Err(Error::FrameEncoding);
                }
                let connection_id = d(dec.decode_vec(1))?;
                if connection_id.is_empty() || connection_id.len() > ConnectionId::MAX_LEN {
                    return Err(Error::FrameEncoding);
                }
                let stateless_reset_token = Srt::try_from(dec).map_err(|_| Error::FrameEncoding)?;

                Ok(Self::NewConnectionId {
                    sequence_number,
                    retire_prior,
                    connection_id,
                    stateless_reset_token,
                })
            }
            FrameType::RetireConnectionId => Ok(Self::RetireConnectionId {
                sequence_number: dv(dec)?,
            }),
            FrameType::PathChallenge => {
                let data = d(dec.decode(PATH_CHALLENGE_LEN))?;
                let mut datav = [0; PATH_CHALLENGE_LEN];
                datav.copy_from_slice(data);
                Ok(Self::PathChallenge { data: datav })
            }
            FrameType::PathResponse => {
                let data = d(dec.decode(PATH_CHALLENGE_LEN))?;
                let mut datav = [0; PATH_CHALLENGE_LEN];
                datav.copy_from_slice(data);
                Ok(Self::PathResponse { data: datav })
            }
            FrameType::ConnectionCloseTransport | FrameType::ConnectionCloseApplication => {
                let (error_code, frame_type) = if t == FrameType::ConnectionCloseTransport {
                    (CloseError::Transport(dv(dec)?), dv(dec)?)
                } else {
                    (CloseError::Application(dv(dec)?), 0)
                };
                // We can tolerate this copy for now.
                let reason_phrase = String::from_utf8_lossy(d(dec.decode_vvec())?).into_owned();
                Ok(Self::ConnectionClose {
                    error_code,
                    frame_type,
                    reason_phrase,
                })
            }
            FrameType::HandshakeDone => Ok(Self::HandshakeDone),
            FrameType::AckFrequency => {
                let seqno = dv(dec)?;
                let tolerance = dv(dec)?;
                if tolerance == 0 {
                    return Err(Error::FrameEncoding);
                }
                let delay = dv(dec)?;
                let ignore_order = bool::try_from(d(dec.decode_uint::<u8>())?)
                    .map_err(|_| Error::FrameEncoding)?;
                Ok(Self::AckFrequency {
                    seqno,
                    tolerance,
                    delay,
                    ignore_order,
                })
            }
            FrameType::Datagram => {
                trace!("DATAGRAM frame, extends to the end of the packet");
                Ok(Self::Datagram {
                    data: dec.decode_remainder(),
                    len_field_len: None,
                })
            }
            FrameType::DatagramWithLen => {
                trace!("DATAGRAM frame, with length");
                let len_start = dec.offset();
                let data = d(dec.decode_vvec())?;
                let len_field_len = dec.offset() - len_start - data.len();
                Ok(Self::Datagram {
                    data,
                    len_field_len: NonZeroUsize::new(len_field_len),
                })
            }
        }
    }
}

/// Extension trait for [`Encoder`] that automates writing to fuzzing corpus.
pub trait FrameEncoder {
    /// Encode a frame with the given type and encoding closure.
    ///
    /// This method:
    /// 1. Encodes the frame type as a varint
    /// 2. Calls the provided closure to encode the frame-specific data
    /// 3. When fuzzing corpus collection is enabled, saves the frame to the corpus
    ///
    /// # Example
    /// ```ignore
    /// builder.encode_frame(FrameType::NewToken, |b| {
    ///     b.encode_vvec(&token);
    /// });
    /// ```
    fn encode_frame<T, F>(&mut self, frame_type: T, encode_fn: F) -> &mut Self
    where
        T: Into<u64>,
        F: FnOnce(&mut Self);
}

impl<B: Buffer> FrameEncoder for Encoder<B> {
    fn encode_frame<T, F>(&mut self, frame_type: T, encode_fn: F) -> &mut Self
    where
        T: Into<u64>,
        F: FnOnce(&mut Self),
    {
        #[cfg(feature = "build-fuzzing-corpus")]
        let frame_start = self.len();
        self.encode_varint(frame_type.into());
        encode_fn(self);
        #[cfg(feature = "build-fuzzing-corpus")]
        neqo_common::write_item_to_fuzzing_corpus("frame", &self.as_ref()[frame_start..]);
        self
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::num::NonZeroUsize;

    use neqo_common::{Decoder, Encoder, MAX_VARINT};

    use crate::{
        CloseError, ConnectionId, Error, MAX_STREAMS_LIMIT, StreamId, StreamType, Token as Srt,
        ecn::Count,
        frame::{AckRange, Frame, FrameType, PATH_CHALLENGE_LEN},
        packet,
    };

    fn just_dec(f: &Frame, s: &str) {
        let encoded = Encoder::from_hex(s);
        let decoded = Frame::decode(&mut encoded.as_decoder()).expect("Failed to decode frame");
        assert_eq!(*f, decoded);
    }

    #[test]
    fn padding() {
        let f = Frame::Padding(1);
        just_dec(&f, "00");
        let f = Frame::Padding(2);
        just_dec(&f, "0000");
    }

    #[test]
    fn ping() {
        let f = Frame::Ping;
        just_dec(&f, "01");
    }

    #[test]
    fn ack() {
        let ar = vec![AckRange { gap: 1, range: 2 }, AckRange { gap: 3, range: 4 }];

        let f = Frame::Ack {
            largest_acknowledged: 0x1234,
            ack_delay: 0x1235,
            first_ack_range: 0x1236,
            ack_ranges: ar.clone(),
            ecn_count: None,
        };

        just_dec(&f, "025234523502523601020304");

        // Try to parse ACK_ECN without ECN values
        let enc = Encoder::from_hex("035234523502523601020304");
        let mut dec = enc.as_decoder();
        assert_eq!(Frame::decode(&mut dec).unwrap_err(), Error::FrameEncoding);

        // Try to parse ACK_ECN with ECN values
        let ecn_count = Some(Count::new(0, 1, 2, 3));
        let fe = Frame::Ack {
            largest_acknowledged: 0x1234,
            ack_delay: 0x1235,
            first_ack_range: 0x1236,
            ack_ranges: ar,
            ecn_count,
        };
        let enc = Encoder::from_hex("035234523502523601020304010203");
        let mut dec = enc.as_decoder();
        assert_eq!(Frame::decode(&mut dec).unwrap(), fe);
    }

    #[test]
    fn reset_stream() {
        let f = Frame::ResetStream {
            stream_id: StreamId::from(0x1234),
            application_error_code: 0x77,
            final_size: 0x3456,
        };

        just_dec(&f, "04523440777456");
    }

    #[test]
    fn reset_stream_at() {
        let f = Frame::ResetStreamAt {
            stream_id: StreamId::from(0x1234),
            application_error_code: 0x77,
            final_size: 0x3456,
            reliable_size: 0x12,
        };

        just_dec(&f, "2452344077745612");
    }

    /// A `RESET_STREAM_AT` frame with `reliable_size > final_size` is rejected at decode.
    #[test]
    fn reset_stream_at_reliable_exceeds_final() {
        let mut enc = Encoder::default();
        enc.encode_varint(FrameType::ResetStreamAt);
        enc.encode_varint(0x1234u64); // stream_id
        enc.encode_varint(0x77u64); // application_error_code
        enc.encode_varint(0x10u64); // final_size
        enc.encode_varint(0x20u64); // reliable_size > final_size
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    /// `reliable_size == final_size` is the boundary case and must decode.
    #[test]
    fn reset_stream_at_reliable_equals_final() {
        let mut enc = Encoder::default();
        enc.encode_varint(FrameType::ResetStreamAt);
        enc.encode_varint(0x1234u64); // stream_id
        enc.encode_varint(0x77u64); // application_error_code
        enc.encode_varint(0x3456u64); // final_size
        enc.encode_varint(0x3456u64); // reliable_size == final_size
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap(),
            Frame::ResetStreamAt {
                stream_id: StreamId::from(0x1234),
                application_error_code: 0x77,
                final_size: 0x3456,
                reliable_size: 0x3456,
            }
        );
    }

    /// `RESET_STREAM_AT` relies on the default arms of `ack_eliciting`/`is_allowed`,
    /// matching `RESET_STREAM`: ack-eliciting, and allowed only in `ZeroRtt`/`Short`.
    #[test]
    fn reset_stream_at_ack_eliciting_and_allowed() {
        let f = Frame::ResetStreamAt {
            stream_id: StreamId::from(1),
            application_error_code: 2,
            final_size: 3,
            reliable_size: 1,
        };
        assert!(f.ack_eliciting());
        assert!(f.is_allowed(packet::Type::Short));
        assert!(f.is_allowed(packet::Type::ZeroRtt));
        assert!(!f.is_allowed(packet::Type::Handshake));
        assert!(!f.is_allowed(packet::Type::Initial));
        assert!(f.is_stream());
        assert_eq!(f.get_type(), FrameType::ResetStreamAt);
    }

    #[test]
    fn stop_sending() {
        let f = Frame::StopSending {
            stream_id: StreamId::from(63),
            application_error_code: 0x77,
        };

        just_dec(&f, "053F4077");
    }

    #[test]
    fn crypto() {
        let f = Frame::Crypto {
            offset: 1,
            data: &[1, 2, 3],
        };

        just_dec(&f, "060103010203");
    }

    #[test]
    fn new_token() {
        let f = Frame::NewToken {
            token: &[0x12, 0x34, 0x56],
        };

        just_dec(&f, "0703123456");
    }

    #[test]
    fn empty_new_token() {
        let mut dec = Decoder::from(&[0x07, 0x00][..]);
        assert_eq!(Frame::decode(&mut dec).unwrap_err(), Error::FrameEncoding);
    }

    #[test]
    fn stream() {
        // First, just set the length bit.
        let f = Frame::Stream {
            fin: false,
            stream_id: StreamId::from(5),
            offset: 0,
            data: &[1, 2, 3],
            fill: false,
        };

        just_dec(&f, "0a0503010203");

        // Now with offset != 0 and FIN
        let f = Frame::Stream {
            fin: true,
            stream_id: StreamId::from(5),
            offset: 1,
            data: &[1, 2, 3],
            fill: false,
        };
        just_dec(&f, "0f050103010203");

        // Now to fill the packet.
        let f = Frame::Stream {
            fin: true,
            stream_id: StreamId::from(5),
            offset: 0,
            data: &[1, 2, 3],
            fill: true,
        };
        just_dec(&f, "0905010203");
    }

    #[test]
    fn max_data() {
        let f = Frame::MaxData {
            maximum_data: 0x1234,
        };

        just_dec(&f, "105234");
    }

    #[test]
    fn max_stream_data() {
        let f = Frame::MaxStreamData {
            stream_id: StreamId::from(5),
            maximum_stream_data: 0x1234,
        };

        just_dec(&f, "11055234");
    }

    #[test]
    fn max_streams() {
        let mut f = Frame::MaxStreams {
            stream_type: StreamType::BiDi,
            maximum_streams: 0x1234,
        };

        just_dec(&f, "125234");

        f = Frame::MaxStreams {
            stream_type: StreamType::UniDi,
            maximum_streams: 0x1234,
        };

        just_dec(&f, "135234");
    }

    #[test]
    fn data_blocked() {
        let f = Frame::DataBlocked { data_limit: 0x1234 };

        just_dec(&f, "145234");
    }

    #[test]
    fn stream_data_blocked() {
        let f = Frame::StreamDataBlocked {
            stream_id: StreamId::from(5),
            stream_data_limit: 0x1234,
        };

        just_dec(&f, "15055234");
    }

    #[test]
    fn streams_blocked() {
        let mut f = Frame::StreamsBlocked {
            stream_type: StreamType::BiDi,
            stream_limit: 0x1234,
        };

        just_dec(&f, "165234");

        f = Frame::StreamsBlocked {
            stream_type: StreamType::UniDi,
            stream_limit: 0x1234,
        };

        just_dec(&f, "175234");
    }

    #[test]
    fn new_connection_id() {
        let f = Frame::NewConnectionId {
            sequence_number: 0x1234,
            retire_prior: 0,
            connection_id: &[0x01, 0x02],
            stateless_reset_token: Srt::new([9; Srt::LEN]),
        };

        just_dec(&f, "1852340002010209090909090909090909090909090909");
    }

    #[test]
    fn too_large_new_connection_id() {
        let mut enc = Encoder::from_hex("18523400"); // up to the CID
        enc.encode_vvec(&[0x0c; ConnectionId::MAX_LEN + 10]);
        enc.encode(&[0x11; 16][..]);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    #[test]
    fn zero_length_new_connection_id() {
        let mut enc = Encoder::from_hex("18523400"); // type, sequence_number, retire_prior
        enc.encode_vvec(&[]); // zero-length connection ID
        enc.encode(&[0x11; 16][..]);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    #[test]
    fn truncated_new_connection_id() {
        let mut enc = Encoder::from_hex("18523400"); // type, sequence_number, retire_prior
        enc.encode_vvec(&[0x01, 0x02]);
        enc.encode(&[0x11; 15][..]); // One byte short of a stateless reset token.
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    #[test]
    fn new_connection_id_retire_prior_after_sequence_number() {
        // retire_prior (5) greater than sequence_number (2).
        let mut enc = Encoder::from_hex("180205"); // type, sequence_number, retire_prior
        enc.encode_vvec(&[0x01, 0x02]);
        enc.encode(&[0x11; 16][..]);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    #[test]
    fn retire_connection_id() {
        let f = Frame::RetireConnectionId {
            sequence_number: 0x1234,
        };

        just_dec(&f, "195234");
    }

    #[test]
    fn path_challenge() {
        let f = Frame::PathChallenge {
            data: [9; PATH_CHALLENGE_LEN],
        };

        just_dec(&f, "1a0909090909090909");
    }

    #[test]
    fn path_response() {
        let f = Frame::PathResponse {
            data: [9; PATH_CHALLENGE_LEN],
        };

        just_dec(&f, "1b0909090909090909");
    }

    #[test]
    fn connection_close_transport() {
        let f = Frame::ConnectionClose {
            error_code: CloseError::Transport(0x5678),
            frame_type: 0x1234,
            reason_phrase: String::from("\x01\x02\x03"),
        };

        just_dec(&f, "1c80005678523403010203");
    }

    #[test]
    fn connection_close_application() {
        let f = Frame::ConnectionClose {
            error_code: CloseError::Application(0x5678),
            frame_type: 0,
            reason_phrase: String::from("\x01\x02\x03"),
        };

        just_dec(&f, "1d8000567803010203");
    }

    #[test]
    fn compare() {
        let f1 = Frame::Padding(1);
        let f2 = Frame::Padding(1);
        let f3 = Frame::Crypto {
            offset: 0,
            data: &[1, 2, 3],
        };
        let f4 = Frame::Crypto {
            offset: 0,
            data: &[1, 2, 3],
        };
        let f5 = Frame::Crypto {
            offset: 1,
            data: &[1, 2, 3],
        };
        let f6 = Frame::Crypto {
            offset: 0,
            data: &[1, 2, 4],
        };

        assert_eq!(f1, f2);
        assert_ne!(f1, f3);
        assert_eq!(f3, f4);
        assert_ne!(f3, f5);
        assert_ne!(f3, f6);
    }

    #[test]
    fn decode_ack_frame() {
        let res = Frame::decode_ack_frame(7, 2, &[AckRange { gap: 0, range: 3 }]);
        assert!(res.is_ok());
        assert_eq!(res.unwrap(), vec![5..=7, 0..=3]);
    }

    #[test]
    fn ack_frequency() {
        let f = Frame::AckFrequency {
            seqno: 10,
            tolerance: 5,
            delay: 2000,
            ignore_order: true,
        };
        just_dec(&f, "40af0a0547d001");
    }

    #[test]
    fn ack_frequency_ignore_error_error() {
        let enc = Encoder::from_hex("40af0a0547d003"); // ignore_order of 3
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    /// Hopefully this test is eventually redundant.
    #[test]
    fn ack_frequency_zero_packets() {
        let enc = Encoder::from_hex("40af0a000101"); // packets of 0
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    #[test]
    fn datagram() {
        // Without the length bit.
        let f = Frame::Datagram {
            data: &[1, 2, 3],
            len_field_len: None,
        };

        just_dec(&f, "30010203");

        // With the length bit.
        let f = Frame::Datagram {
            data: &[1, 2, 3],
            len_field_len: NonZeroUsize::new(1),
        };
        just_dec(&f, "3103010203");
    }

    #[test]
    fn frame_decode_enforces_bound_on_ack_range() {
        let mut e = Encoder::default();

        e.encode_varint(FrameType::Ack);
        e.encode_varint(0u64); // largest acknowledged
        e.encode_varint(0u64); // ACK delay
        e.encode_varint(u32::MAX); // ACK range count = huge, but maybe available for allocation

        let res = Frame::decode(&mut e.as_decoder());
        assert_eq!(res, Err(Error::FrameEncoding));
    }

    #[test]
    #[should_panic(expected = "Failed to decode frame")]
    fn invalid_frame_type_len() {
        let f = Frame::Datagram {
            data: &[1, 2, 3],
            len_field_len: None,
        };

        just_dec(&f, "4030010203");
    }

    /// See bug in <https://github.com/mozilla/neqo/issues/2838>.
    #[test]
    fn padding_frame_u16_overflow() {
        let mut e = Encoder::default();
        e.encode_varint(FrameType::Padding);
        // `Frame::Padding` uses u16 to store length. Try to overflow length.
        e.pad_to(usize::from(u16::MAX) + 1, 0);
        assert_eq!(Frame::decode(&mut e.as_decoder()), Err(Error::TooMuchData));
    }

    #[test]
    fn frame_type_to_u8() {
        assert_eq!(u8::from(FrameType::Padding), 0);
        assert_eq!(u8::from(FrameType::Ping), 1);
    }

    #[test]
    #[expect(clippy::too_many_lines, reason = "OK in tests.")]
    fn dump() {
        let s = |id| StreamId::from(id);
        assert_eq!(Frame::Padding(5).dump(), "Padding { len: 5 }");
        assert_eq!(
            Frame::Crypto {
                offset: 1,
                data: &[2, 3]
            }
            .dump(),
            "Crypto { offset: 1, len: 2 }"
        );
        assert_eq!(
            Frame::Stream {
                stream_id: s(4),
                offset: 10,
                data: &[1],
                fin: true,
                fill: false
            }
            .dump(),
            "Stream { stream_id: 4, offset: 10, len: 1, fin: true }"
        );
        assert_eq!(
            Frame::Stream {
                stream_id: s(4),
                offset: 0,
                data: &[1, 2],
                fin: false,
                fill: true
            }
            .dump(),
            "Stream { stream_id: 4, offset: 0, len: >>2, fin: false }"
        );
        assert_eq!(
            Frame::Datagram {
                data: &[1, 2, 3],
                len_field_len: NonZeroUsize::new(1),
            }
            .dump(),
            "Datagram { len: 3 }"
        );
        // Remaining frames use Debug format
        assert_eq!(Frame::Ping.dump(), "Ping");
        assert_eq!(
            Frame::Ack {
                largest_acknowledged: 1,
                ack_delay: 2,
                first_ack_range: 0,
                ack_ranges: vec![],
                ecn_count: None
            }
            .dump(),
            "Ack { largest_acknowledged: 1, ack_delay: 2, first_ack_range: 0, ack_ranges: [], ecn_count: None }"
        );
        assert_eq!(
            Frame::ResetStream {
                stream_id: s(1),
                application_error_code: 2,
                final_size: 3
            }
            .dump(),
            "ResetStream { stream_id: StreamId(1), application_error_code: 2, final_size: 3 }"
        );
        assert_eq!(
            Frame::StopSending {
                stream_id: s(1),
                application_error_code: 2
            }
            .dump(),
            "StopSending { stream_id: StreamId(1), application_error_code: 2 }"
        );
        assert_eq!(
            Frame::NewToken { token: &[1] }.dump(),
            "NewToken { token: [1] }"
        );
        assert_eq!(
            Frame::MaxData { maximum_data: 100 }.dump(),
            "MaxData { maximum_data: 100 }"
        );
        assert_eq!(
            Frame::MaxStreamData {
                stream_id: s(1),
                maximum_stream_data: 100
            }
            .dump(),
            "MaxStreamData { stream_id: StreamId(1), maximum_stream_data: 100 }"
        );
        assert_eq!(
            Frame::MaxStreams {
                stream_type: StreamType::BiDi,
                maximum_streams: 10
            }
            .dump(),
            "MaxStreams { stream_type: BiDi, maximum_streams: 10 }"
        );
        assert_eq!(
            Frame::DataBlocked { data_limit: 50 }.dump(),
            "DataBlocked { data_limit: 50 }"
        );
        assert_eq!(
            Frame::StreamDataBlocked {
                stream_id: s(1),
                stream_data_limit: 50
            }
            .dump(),
            "StreamDataBlocked { stream_id: StreamId(1), stream_data_limit: 50 }"
        );
        assert_eq!(
            Frame::StreamsBlocked {
                stream_type: StreamType::UniDi,
                stream_limit: 5
            }
            .dump(),
            "StreamsBlocked { stream_type: UniDi, stream_limit: 5 }"
        );
        assert_eq!(
            Frame::NewConnectionId {
                sequence_number: 1,
                retire_prior: 0,
                connection_id: &[1, 2],
                stateless_reset_token: Srt::new([0; 16])
            }
            .dump(),
            "NewConnectionId { sequence_number: 1, retire_prior: 0, connection_id: [1, 2], stateless_reset_token: Token([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]) }"
        );
        assert_eq!(
            Frame::RetireConnectionId { sequence_number: 1 }.dump(),
            "RetireConnectionId { sequence_number: 1 }"
        );
        assert_eq!(
            Frame::PathChallenge {
                data: [1; PATH_CHALLENGE_LEN]
            }
            .dump(),
            "PathChallenge { data: [1, 1, 1, 1, 1, 1, 1, 1] }"
        );
        assert_eq!(
            Frame::PathResponse {
                data: [2; PATH_CHALLENGE_LEN]
            }
            .dump(),
            "PathResponse { data: [2, 2, 2, 2, 2, 2, 2, 2] }"
        );
        assert_eq!(
            Frame::ConnectionClose {
                error_code: CloseError::Transport(0),
                frame_type: 0,
                reason_phrase: String::new()
            }
            .dump(),
            "ConnectionClose { error_code: Transport(0), frame_type: 0, reason_phrase: \"\" }"
        );
        assert_eq!(Frame::HandshakeDone.dump(), "HandshakeDone");
        assert_eq!(
            Frame::AckFrequency {
                seqno: 1,
                tolerance: 2,
                delay: 3,
                ignore_order: false
            }
            .dump(),
            "AckFrequency { seqno: 1, tolerance: 2, delay: 3, ignore_order: false }"
        );
    }

    #[test]
    fn stream_frame_type_constants() {
        assert_eq!(FrameType::StreamWithFin as u8, 0x08 + 0b001);
        assert_eq!(FrameType::StreamWithLen as u8, 0x08 + 0b010);
        assert_eq!(FrameType::StreamWithOff as u8, 0x08 + 0b100);
        assert_eq!(FrameType::StreamWithOffFin as u8, 0x08 + 0b101);
        assert_eq!(FrameType::StreamWithOffLen as u8, 0x08 + 0b110);
        assert_eq!(FrameType::StreamWithOffLenFin as u8, 0x08 + 0b111);
    }

    fn stream_frame(offset: u64) -> Frame<'static> {
        Frame::Stream {
            fin: false,
            stream_id: StreamId::from(1),
            offset,
            data: &[1],
            fill: false,
        }
    }

    #[test]
    fn stream_get_type_offset_flag() {
        assert_eq!(stream_frame(0).get_type(), FrameType::StreamWithLen);
        assert_eq!(stream_frame(1).get_type(), FrameType::StreamWithOffLen);
    }

    /// `is_allowed`: `NewToken` and app-close are only allowed in Short packets.
    #[test]
    fn is_allowed_new_token_and_app_close() {
        let new_token = Frame::NewToken { token: &[1, 2] };
        assert!(new_token.is_allowed(packet::Type::Short));
        assert!(!new_token.is_allowed(packet::Type::ZeroRtt));
        assert!(!new_token.is_allowed(packet::Type::Handshake));

        let app_close = Frame::ConnectionClose {
            error_code: CloseError::Application(1),
            frame_type: 0,
            reason_phrase: String::new(),
        };
        assert!(app_close.is_allowed(packet::Type::Short));
        assert!(!app_close.is_allowed(packet::Type::ZeroRtt));
    }

    /// `is_allowed`: `PATH_RESPONSE` and `HANDSHAKE_DONE` are only allowed in
    /// 1-RTT packets (RFC 9000, Table 3).
    #[test]
    fn is_allowed_path_response_and_handshake_done() {
        let path_response = Frame::PathResponse {
            data: [0; PATH_CHALLENGE_LEN],
        };
        assert!(path_response.is_allowed(packet::Type::Short));
        assert!(!path_response.is_allowed(packet::Type::ZeroRtt));

        assert!(Frame::HandshakeDone.is_allowed(packet::Type::Short));
        assert!(!Frame::HandshakeDone.is_allowed(packet::Type::ZeroRtt));

        // PATH_CHALLENGE, by contrast, is permitted in 0-RTT packets.
        let path_challenge = Frame::PathChallenge {
            data: [0; PATH_CHALLENGE_LEN],
        };
        assert!(path_challenge.is_allowed(packet::Type::ZeroRtt));
    }

    /// `decode_ack_frame` rejects invalid range configurations.
    #[test]
    fn decode_ack_frame_boundaries() {
        // largest_acked < first_ack_range is always invalid.
        assert!(Frame::decode_ack_frame(3, 4, &[]).is_err());

        // largest_acked == first_ack_range with additional ranges: no room for a gap.
        assert!(Frame::decode_ack_frame(4, 4, &[AckRange { gap: 0, range: 0 }]).is_err());

        // After the first range (5..=5), cur = 0, which is less than gap+1=1.
        assert!(Frame::decode_ack_frame(5, 4, &[AckRange { gap: 0, range: 0 }]).is_err());

        // With one extra unit of room (largest - first = 2), cur starts at 1 — enough.
        assert!(Frame::decode_ack_frame(5, 3, &[AckRange { gap: 0, range: 0 }]).is_ok());
    }

    /// `decode_ack_frame` correctly advances `cur` by `gap + 1` and produces exact ranges.
    #[test]
    fn decode_ack_frame_gap_arithmetic() {
        // gap=1 skips 2 packet numbers (gap+1=2): cur goes 5→3 after the gap.
        let result = Frame::decode_ack_frame(10, 4, &[AckRange { gap: 1, range: 0 }]);
        assert_eq!(result.unwrap(), vec![6..=10, 3..=3]);

        // cur < r.range: cur=7, range=8 → error.
        assert!(Frame::decode_ack_frame(10, 2, &[AckRange { gap: 0, range: 8 }]).is_err());

        // Two ranges with gaps; all arithmetic must stay consistent.
        let result = Frame::decode_ack_frame(
            10,
            2,
            &[AckRange { gap: 0, range: 1 }, AckRange { gap: 1, range: 1 }],
        );
        assert!(result.is_ok());
    }

    fn encode_ack_header(range_count: u64) -> Encoder {
        let mut enc = Encoder::default();
        enc.encode_byte(0x02); // ACK frame type
        enc.encode_varint(100u64); // largest_acknowledged
        enc.encode_varint(0u64); // ack_delay
        enc.encode_varint(range_count);
        enc
    }

    /// ACK with too many ranges is rejected; just below the limit passes the range count check.
    #[test]
    fn decode_ack_too_many_ranges() {
        let mut enc = encode_ack_header(32768);
        enc.pad_to(enc.len() + 1 + 2 * 32768, 0); // Only the range-count limit can fail.
        let result = Frame::decode(&mut enc.as_decoder());
        assert_eq!(result.unwrap_err(), Error::FrameEncoding);

        let mut enc = encode_ack_header(32767);
        enc.pad_to(enc.len() + 1 + 2 * 32767, 0); // First ACK Range and 32767 ACK Ranges.
        let result = Frame::decode(&mut enc.as_decoder());
        assert!(result.is_ok());
    }

    /// Both Stream and Crypto frames reject `offset + data.len() > MAX_VARINT`.
    #[test]
    fn decode_offset_overflow() {
        // StreamWithOffLen (0x0e): stream_id + offset + vvec data
        let mut enc = Encoder::default();
        enc.encode_byte(0x0e);
        enc.encode_varint(1u64); // stream_id
        enc.encode_varint(MAX_VARINT);
        enc.encode_vvec(&[0x01]);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );

        // Crypto (0x06): offset + vvec data (no stream_id)
        let mut enc = Encoder::default();
        enc.encode_byte(0x06);
        enc.encode_varint(MAX_VARINT);
        enc.encode_vvec(&[0x01]);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    /// A `MaxStreams` frame with value > 2^60 is rejected.
    #[test]
    fn decode_max_streams_exceeds_limit() {
        let mut enc = Encoder::default();
        enc.encode_byte(0x12); // MaxStreamsBiDi
        enc.encode_varint(MAX_STREAMS_LIMIT + 1);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    /// A `StreamsBlocked` frame with value > 2^60 is rejected.
    #[test]
    fn decode_streams_blocked_exceeds_limit() {
        let mut enc = Encoder::default();
        enc.encode_byte(0x16); // StreamsBlockedBiDi
        enc.encode_varint(MAX_STREAMS_LIMIT + 1);
        assert_eq!(
            Frame::decode(&mut enc.as_decoder()).unwrap_err(),
            Error::FrameEncoding
        );
    }

    /// `StreamType::try_from` rejects non-stream frame types.
    #[test]
    fn stream_type_try_from_invalid() {
        assert!(StreamType::try_from(FrameType::Ping).is_err());
    }
}
