// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cmp::min, fmt::Debug, num::NonZeroUsize, time::Instant};

use neqo_common::{
    Decoder, IncrementalDecoderBuffer, IncrementalDecoderIgnore, IncrementalDecoderUint,
    hex::{HexSnipMiddle, HexWithLen},
};
use neqo_transport::{Connection, Error as TransportError, StreamId};
use tracing::trace;

use super::hframe::HFrameType;
use crate::{Error, RecvStream, Res};

const MAX_READ_SIZE: usize = 2048; // Given a practical MTU of 1500 bytes, this seems reasonable.

pub trait FrameDecoder<T> {
    /// Fuzzing corpus name for this frame type. If `Some`, decoded frames will be
    /// written to the fuzzing corpus with this name.
    #[cfg(feature = "build-fuzzing-corpus")]
    const FUZZING_CORPUS: Option<&'static str> = None;

    fn is_known_type(frame_type: HFrameType) -> bool;

    /// # Errors
    ///
    /// Returns `HttpFrameUnexpected` if frames is not allowed, i.e. is a `H3_RESERVED_FRAME_TYPES`.
    fn frame_type_allowed(_frame_type: HFrameType) -> Res<()> {
        Ok(())
    }

    /// Upper bound on a known frame's declared length that we'll buffer before decoding.
    fn max_frame_data(frame_type: HFrameType) -> usize;

    /// # Errors
    ///
    /// If a frame cannot be properly decoded.
    fn decode(frame_type: HFrameType, frame_len: u64, data: Option<&[u8]>) -> Res<Option<T>>;
}

#[expect(clippy::module_name_repetitions, reason = "This is OK.")]
pub trait StreamReader {
    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, protocol error, etc.
    /// Return an error if the stream was closed on the transport layer, but that information is not
    /// yet consumed on the  http/3 layer.
    fn read_data(&mut self, buf: &mut [u8], now: Instant) -> Res<(usize, bool)>;
}

pub struct StreamReaderConnectionWrapper<'a> {
    conn: &'a mut Connection,
    stream_id: StreamId,
}

impl<'a> StreamReaderConnectionWrapper<'a> {
    pub const fn new(conn: &'a mut Connection, stream_id: StreamId) -> Self {
        Self { conn, stream_id }
    }
}

impl StreamReader for StreamReaderConnectionWrapper<'_> {
    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, protocol error, etc.
    fn read_data(&mut self, buf: &mut [u8], _now: Instant) -> Res<(usize, bool)> {
        let res = self.conn.stream_recv(self.stream_id, buf)?;
        Ok(res)
    }
}

pub struct StreamReaderRecvStreamWrapper<'a> {
    recv_stream: &'a mut Box<dyn RecvStream>,
    conn: &'a mut Connection,
}

impl<'a> StreamReaderRecvStreamWrapper<'a> {
    #[cfg_attr(fuzzing, expect(private_interfaces, reason = "OK for fuzzing."))]
    pub fn new(conn: &'a mut Connection, recv_stream: &'a mut Box<dyn RecvStream>) -> Self {
        Self { recv_stream, conn }
    }
}

impl StreamReader for StreamReaderRecvStreamWrapper<'_> {
    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, protocol error, etc.
    fn read_data(&mut self, buf: &mut [u8], now: Instant) -> Res<(usize, bool)> {
        self.recv_stream.read_data(self.conn, buf, now)
    }
}

#[derive(Clone, Debug)]
enum FrameReaderState {
    GetType { decoder: IncrementalDecoderUint },
    GetLength { decoder: IncrementalDecoderUint },
    GetData { decoder: IncrementalDecoderBuffer },
    UnknownFrameDischargeData { decoder: IncrementalDecoderIgnore },
}

#[expect(clippy::module_name_repetitions, reason = "This is OK.")]
pub struct FrameReader {
    state: FrameReaderState,
    frame_type: HFrameType,
    frame_len: u64,
    buffer: [u8; MAX_READ_SIZE],
}

impl Debug for FrameReader {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let frame_len = self
            .frame_len
            .try_into()
            .unwrap_or(usize::MAX)
            .min(self.buffer.len());
        f.debug_struct("FrameReader")
            .field("state", &self.state)
            .field("frame_type", &self.frame_type)
            .field("frame", &HexSnipMiddle::new(&self.buffer[..frame_len]))
            .finish()
    }
}

impl FrameReader {
    #[must_use]
    #[cfg_attr(
        fuzzing,
        expect(clippy::new_without_default, reason = "OK for fuzzing.")
    )]
    pub fn new() -> Self {
        Self {
            state: FrameReaderState::GetType {
                decoder: IncrementalDecoderUint::default(),
            },
            frame_type: HFrameType(u64::MAX),
            frame_len: 0,
            buffer: [0; MAX_READ_SIZE],
        }
    }

    #[must_use]
    pub fn new_with_type(frame_type: HFrameType) -> Self {
        Self {
            state: FrameReaderState::GetLength {
                decoder: IncrementalDecoderUint::default(),
            },
            frame_type,
            frame_len: 0,
            buffer: [0; MAX_READ_SIZE],
        }
    }

    fn reset(&mut self) {
        self.state = FrameReaderState::GetType {
            decoder: IncrementalDecoderUint::default(),
        };
    }

    fn min_remaining(&self) -> usize {
        match &self.state {
            FrameReaderState::GetType { decoder } | FrameReaderState::GetLength { decoder } => {
                decoder.min_remaining()
            }
            FrameReaderState::GetData { decoder } => decoder.min_remaining(),
            FrameReaderState::UnknownFrameDischargeData { decoder } => decoder.min_remaining(),
        }
    }

    const fn decoding_in_progress(&self) -> bool {
        if let FrameReaderState::GetType { decoder } = &self.state {
            decoder.decoding_in_progress()
        } else {
            true
        }
    }

    /// Returns true if QUIC stream was closed.
    ///
    /// # Errors
    ///
    /// May return [`Error::HttpFrame`] if a frame cannot be decoded.
    /// Absorbs [`TransportError::NoMoreData`] and [`Error::TransportStreamDoesNotExist`]
    /// into a zero-length read if the stream was reset and the stream closed.
    /// The reset is propagated through events.
    pub fn receive<T: FrameDecoder<T>>(
        &mut self,
        stream_reader: &mut dyn StreamReader,
        now: Instant,
    ) -> Res<(Option<T>, bool)> {
        loop {
            let to_read = min(self.min_remaining(), self.buffer.len());
            let (output, read, fin) =
                match stream_reader.read_data(&mut self.buffer[..to_read], now) {
                    Ok((0, f)) => (None, false, f),
                    Ok((amount, f)) => {
                        trace!("FrameReader::receive: reading {amount} byte, fin={f}");
                        (self.consume::<T>(amount)?, true, f)
                    }
                    // A `RESET_STREAM` could cause the transport to report `NoMoreData` or
                    // that the stream no longer exists. Don't treat that as an error here, let
                    // the event handling deal with it.
                    Err(
                        Error::Transport(TransportError::NoMoreData)
                        | Error::TransportStreamDoesNotExist,
                    ) => {
                        break Ok((None, false));
                    }
                    Err(e) => return Err(e),
                };

            if output.is_some() {
                break Ok((output, fin));
            }

            if fin {
                if self.decoding_in_progress() {
                    break Err(Error::HttpFrame);
                }
                break Ok((None, fin));
            }

            if !read {
                // There was no new data, exit the loop.
                break Ok((None, false));
            }
        }
    }

    /// # Errors
    ///
    /// May return `HttpFrame` if a frame cannot be decoded.
    fn consume<T: FrameDecoder<T>>(&mut self, amount: usize) -> Res<Option<T>> {
        let mut input = Decoder::from(&self.buffer[..amount]);
        match &mut self.state {
            FrameReaderState::GetType { decoder } => {
                if let Some(v) = decoder.consume(&mut input) {
                    trace!("FrameReader::receive: read frame type {v}");
                    self.frame_type_decoded::<T>(HFrameType(v))?;
                }
            }
            FrameReaderState::GetLength { decoder } => {
                if let Some(len) = decoder.consume(&mut input) {
                    trace!(
                        "FrameReader::receive: frame type {:?} length {len}",
                        self.frame_type
                    );
                    return self.frame_length_decoded::<T>(len);
                }
            }
            FrameReaderState::GetData { decoder } => {
                if let Some(data) = decoder.consume(&mut input) {
                    trace!(
                        "received frame {:?}: {}",
                        self.frame_type,
                        HexWithLen::new(&data[..])
                    );
                    return self.frame_data_decoded::<T>(&data);
                }
            }
            FrameReaderState::UnknownFrameDischargeData { decoder } => {
                if decoder.consume(&mut input) {
                    self.reset();
                }
            }
        }
        Ok(None)
    }
    fn frame_type_decoded<T: FrameDecoder<T>>(&mut self, frame_type: HFrameType) -> Res<()> {
        T::frame_type_allowed(frame_type)?;
        self.frame_type = frame_type;
        self.state = FrameReaderState::GetLength {
            decoder: IncrementalDecoderUint::default(),
        };
        Ok(())
    }

    fn frame_length_decoded<T: FrameDecoder<T>>(&mut self, len: u64) -> Res<Option<T>> {
        self.frame_len = len;
        match T::decode(
            self.frame_type,
            self.frame_len,
            if len > 0 { None } else { Some(&[]) },
        )? {
            Some(f) => {
                #[cfg(feature = "build-fuzzing-corpus")]
                if let Some(corpus) = T::FUZZING_CORPUS {
                    // Write zero-length frames to the fuzzing corpus to test parsing of frames with
                    // only type and length fields.
                    self.write_item_to_fuzzing_corpus(corpus, None);
                }
                self.reset();
                return Ok(Some(f));
            }
            None => {
                if T::is_known_type(self.frame_type) {
                    let len = usize::try_from(len).or(Err(Error::HttpFrame))?;
                    if len > T::max_frame_data(self.frame_type) {
                        return Err(Error::HttpExcessiveLoad);
                    }
                    self.state = FrameReaderState::GetData {
                        decoder: IncrementalDecoderBuffer::new(len),
                    };
                } else if let Some(len) =
                    NonZeroUsize::new(usize::try_from(len).or(Err(Error::HttpFrame))?)
                {
                    self.state = FrameReaderState::UnknownFrameDischargeData {
                        decoder: IncrementalDecoderIgnore::new(len),
                    };
                } else {
                    self.reset();
                }
            }
        }
        Ok(None)
    }

    fn frame_data_decoded<T: FrameDecoder<T>>(&mut self, data: &[u8]) -> Res<Option<T>> {
        #[cfg(feature = "build-fuzzing-corpus")]
        if let Some(corpus) = T::FUZZING_CORPUS {
            self.write_item_to_fuzzing_corpus(corpus, Some(data));
        }

        let res = T::decode(self.frame_type, self.frame_len, Some(data))?;
        self.reset();
        Ok(res)
    }

    #[cfg(feature = "build-fuzzing-corpus")]
    /// Write `HFrame` data to indicated fuzzing corpus.
    ///
    /// The output consists of the varint-encoded frame type and length, followed by the optional
    /// payload data.
    fn write_item_to_fuzzing_corpus(&self, corpus: &str, data: Option<&[u8]>) {
        // We need to include the frame type and length varints before the data
        // to create a complete frame that the fuzzer can process.
        let mut encoder = neqo_common::Encoder::default();
        encoder.encode_varint(self.frame_type.0);
        encoder.encode_varint(self.frame_len);
        if let Some(d) = data {
            encoder.encode(d);
        }
        neqo_common::write_item_to_fuzzing_corpus(corpus, encoder.as_ref());
    }
}
