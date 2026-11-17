// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cmp::min, time::Instant};

use neqo_common::{Decoder, IncrementalDecoderUint, Role};
use neqo_qpack::{decoder::QPACK_UNI_STREAM_TYPE_DECODER, encoder::QPACK_UNI_STREAM_TYPE_ENCODER};
use neqo_transport::{Connection, StreamId, StreamType};
use tracing::trace;

use crate::{
    CloseType, Error, Http3StreamType, ReceiveOutput, RecvStream, Res, Stream,
    control_stream_local::HTTP3_UNI_STREAM_TYPE_CONTROL,
    frames::{HFrame, hframe::HFrameType, reader::FrameDecoder},
};

/// Server push stream type. Server push is not supported, so a push stream is rejected with a
/// connection error (see [`NewStreamType::final_stream_type`]).
const HTTP3_UNI_STREAM_TYPE_PUSH: u64 = 0x1;
pub const WEBTRANSPORT_UNI_STREAM: u64 = 0x54;
pub const WEBTRANSPORT_STREAM: u64 = 0x41;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NewStreamType {
    Control,
    Decoder,
    Encoder,
    WebTransportStream(u64),
    Http(u64),
    Unknown,
}

impl NewStreamType {
    /// Get the final `NewStreamType` from a stream type. Most streams are identified by
    /// the type only. This function returns None for `WebTransport` streams because they
    /// need an ID besides the type.
    ///
    /// # Errors
    ///
    /// Returns `HttpStreamCreation` if a stream type is not allowed for the role.
    fn final_stream_type(
        stream_type: u64,
        trans_stream_type: StreamType,
        role: Role,
    ) -> Res<Option<Self>> {
        match (stream_type, trans_stream_type, role) {
            (HTTP3_UNI_STREAM_TYPE_CONTROL, StreamType::UniDi, _) => Ok(Some(Self::Control)),
            (QPACK_UNI_STREAM_TYPE_ENCODER, StreamType::UniDi, _) => Ok(Some(Self::Decoder)),
            (QPACK_UNI_STREAM_TYPE_DECODER, StreamType::UniDi, _) => Ok(Some(Self::Encoder)),
            (WEBTRANSPORT_UNI_STREAM, StreamType::UniDi, _)
            | (WEBTRANSPORT_STREAM, StreamType::BiDi, _) => Ok(None),
            // Server push is not supported. As we never send MAX_PUSH_ID, a client that receives a
            // push stream treats it as a connection error of type H3_ID_ERROR (the push ID exceeds
            // the maximum). A server must never receive a push stream, which is a connection error
            // of type H3_STREAM_CREATION_ERROR.
            // See <https://www.rfc-editor.org/rfc/rfc9114.html#section-6.2.2>.
            (HTTP3_UNI_STREAM_TYPE_PUSH, StreamType::UniDi, Role::Client) => Err(Error::HttpId),
            (HTTP3_UNI_STREAM_TYPE_PUSH, StreamType::UniDi, Role::Server) => {
                Err(Error::HttpStreamCreation)
            }
            (_, StreamType::BiDi, Role::Server) => {
                // The "stream_type" for a bidirectional stream is a frame type. We accept
                // WEBTRANSPORT_STREAM (above), and HEADERS, and we have to ignore unknown types,
                // but any other frame type is bad if we know about it.
                let frame_type = HFrameType(stream_type);
                // This checks for reserved frame types here, which are not allowed.
                <HFrame as FrameDecoder<HFrame>>::frame_type_allowed(frame_type)?;
                if <HFrame as FrameDecoder<HFrame>>::is_known_type(frame_type)
                    && frame_type != HFrameType::HEADERS
                {
                    Err(Error::HttpFrame)
                } else {
                    Ok(Some(Self::Http(stream_type)))
                }
            }
            (_, StreamType::BiDi, Role::Client) => Err(Error::HttpStreamCreation),
            _ => Ok(Some(Self::Unknown)),
        }
    }
}

/// `NewStreamHeadReader` reads the head of an unidirectional stream to identify the stream.
/// There are 2 type of streams:
///  - streams identified by the single type (varint encoded). Most streams belong to this category.
///    The `NewStreamHeadReader` will switch from `ReadType`to `Done` state.
///  - streams identified by the type and the ID (both varint encoded). For example, a
///    `WebTransport` stream is identified by the type and a session ID. After reading the type in
///    the `ReadType` state, `NewStreamHeadReader` changes to `ReadId` state and from there to
///    `Done` state
#[derive(Debug)]
pub enum NewStreamHeadReader {
    ReadType {
        role: Role,
        reader: IncrementalDecoderUint,
        stream_id: StreamId,
    },
    ReadId {
        reader: IncrementalDecoderUint,
        stream_id: StreamId,
    },
    Done,
}

impl NewStreamHeadReader {
    pub fn new(stream_id: StreamId, role: Role) -> Self {
        Self::ReadType {
            role,
            reader: IncrementalDecoderUint::default(),
            stream_id,
        }
    }

    fn read(&mut self, conn: &mut Connection) -> Res<(Option<u64>, bool)> {
        if let Self::ReadType {
            reader, stream_id, ..
        }
        | Self::ReadId {
            reader, stream_id, ..
        } = self
        {
            // This type only exists to read at most two varints (= 16 bytes) from the stream.
            let mut buf = [0; 16];
            loop {
                let to_read = min(reader.min_remaining(), buf.len());
                let buf = &mut buf[0..to_read];
                match conn.stream_recv(*stream_id, &mut buf[..])? {
                    (0, f) => return Ok((None, f)),
                    (amount, f) => {
                        let res = reader.consume(&mut Decoder::from(&buf[..amount]));
                        if res.is_some() || f {
                            return Ok((res, f));
                        }
                    }
                }
            }
        }
        Ok((None, false))
    }

    pub fn get_type(&mut self, conn: &mut Connection) -> Res<Option<NewStreamType>> {
        loop {
            let (output, fin) = self.read(conn)?;
            let Some(output) = output else {
                if fin {
                    *self = Self::Done;
                    return Err(Error::HttpStreamCreation);
                }
                return Ok(None);
            };

            trace!("Decoded uint {output}");
            match self {
                Self::ReadType {
                    role, stream_id, ..
                } => {
                    // final_stream_type may return:
                    //  - an error if a stream type is not allowed for the role, e.g. Push stream
                    //    received at the server.
                    //  - a final type if a stream is only identify by the type
                    //  - None - if a stream is not identified by the type only, but it needs
                    //    additional data from the header to produce the final type, e.g. a push
                    //    stream needs pushId as well.
                    let final_type =
                        NewStreamType::final_stream_type(output, stream_id.stream_type(), *role);
                    match (&final_type, fin) {
                        (Err(_), _) => {
                            *self = Self::Done;
                            return final_type;
                        }
                        (Ok(t), true) => {
                            *self = Self::Done;
                            return Self::map_stream_fin(*t);
                        }
                        (Ok(Some(t)), false) => {
                            trace!("Decoded stream type {:?}", *t);
                            *self = Self::Done;
                            return final_type;
                        }
                        (Ok(None), false) => {
                            // This is a WebTransport stream and it needs more data to be decoded.
                            *self = Self::ReadId {
                                reader: IncrementalDecoderUint::default(),
                                stream_id: *stream_id,
                            }
                        }
                    }
                }
                Self::ReadId { .. } => {
                    *self = Self::Done;
                    trace!("New Stream stream session_id={output}");
                    if fin {
                        return Err(Error::HttpGeneralProtocol);
                    }
                    return Ok(Some(NewStreamType::WebTransportStream(output)));
                }
                Self::Done => {
                    unreachable!("Cannot be in state NewStreamHeadReader::Done");
                }
            }
        }
    }

    fn map_stream_fin(decoded: Option<NewStreamType>) -> Res<Option<NewStreamType>> {
        match decoded {
            Some(NewStreamType::Control | NewStreamType::Encoder | NewStreamType::Decoder) => {
                Err(Error::HttpClosedCriticalStream)
            }
            None => Err(Error::HttpStreamCreation),
            Some(NewStreamType::Http(_)) => Err(Error::HttpFrame),
            Some(NewStreamType::Unknown) => Ok(decoded),
            Some(NewStreamType::WebTransportStream(_)) => {
                unreachable!("WebTransport streams are mapped to None at this stage")
            }
        }
    }

    const fn done(&self) -> bool {
        matches!(self, Self::Done)
    }
}

impl Stream for NewStreamHeadReader {
    fn stream_type(&self) -> Http3StreamType {
        Http3StreamType::NewStream
    }
}

impl RecvStream for NewStreamHeadReader {
    fn reset(&mut self, _close_type: CloseType) -> Res<()> {
        *self = Self::Done;
        Ok(())
    }

    fn receive(&mut self, conn: &mut Connection, _now: Instant) -> Res<(ReceiveOutput, bool)> {
        let t = self.get_type(conn)?;
        Ok((
            t.map_or(ReceiveOutput::NoOutput, ReceiveOutput::NewStream),
            self.done(),
        ))
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::{Encoder, Role};
    use neqo_qpack::{
        decoder::QPACK_UNI_STREAM_TYPE_DECODER, encoder::QPACK_UNI_STREAM_TYPE_ENCODER,
    };
    use neqo_transport::{Connection, StreamId, StreamType};
    use test_fixture::{connect, now};

    use super::{
        HTTP3_UNI_STREAM_TYPE_PUSH, NewStreamHeadReader, WEBTRANSPORT_STREAM,
        WEBTRANSPORT_UNI_STREAM,
    };
    use crate::{
        CloseType, Error, NewStreamType, ReceiveOutput, RecvStream as _, Res,
        control_stream_local::HTTP3_UNI_STREAM_TYPE_CONTROL, frames::HFrameType,
    };

    struct Test {
        conn_c: Connection,
        conn_s: Connection,
        stream_id: StreamId,
        decoder: NewStreamHeadReader,
    }

    impl Test {
        fn new(stream_type: StreamType, role: Role) -> Self {
            let (mut conn_c, mut conn_s) = connect();
            // create a stream
            let stream_id = conn_s.stream_create(stream_type).unwrap();
            let out = conn_s.process_output(now());
            drop(conn_c.process(out.dgram(), now()));

            Self {
                conn_c,
                conn_s,
                stream_id,
                decoder: NewStreamHeadReader::new(stream_id, role),
            }
        }

        fn decode_buffer(
            &mut self,
            enc: &[u8],
            fin: bool,
            outcome: &Res<(ReceiveOutput, bool)>,
            done: bool,
        ) {
            let len = enc.len() - 1;
            for i in 0..len {
                self.conn_s
                    .stream_send(self.stream_id, &enc[i..=i])
                    .unwrap();
                let out = self.conn_s.process_output(now());
                drop(self.conn_c.process(out.dgram(), now()));
                assert_eq!(
                    self.decoder.receive(&mut self.conn_c, now()).unwrap(),
                    (ReceiveOutput::NoOutput, false)
                );
                assert!(!self.decoder.done());
            }
            self.conn_s
                .stream_send(self.stream_id, &enc[enc.len() - 1..])
                .unwrap();
            if fin {
                self.conn_s.stream_close_send(self.stream_id).unwrap();
            }
            let out = self.conn_s.process_output(now());
            drop(self.conn_c.process(out.dgram(), now()));
            assert_eq!(&self.decoder.receive(&mut self.conn_c, now()), outcome);
            assert_eq!(self.decoder.done(), done);
        }

        fn decode(
            &mut self,
            to_encode: &[u64],
            fin: bool,
            outcome: &Res<(ReceiveOutput, bool)>,
            done: bool,
        ) {
            let mut enc = Encoder::default();
            for i in to_encode {
                enc.encode_varint(*i);
            }
            self.decode_buffer(enc.as_ref(), fin, outcome, done);
        }
    }

    #[test]
    fn decode_stream_decoder() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[QPACK_UNI_STREAM_TYPE_DECODER],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Encoder), true)),
            true,
        );
    }

    #[test]
    fn decode_stream_encoder() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[QPACK_UNI_STREAM_TYPE_ENCODER],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Decoder), true)),
            true,
        );
    }

    #[test]
    fn decode_stream_control() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[HTTP3_UNI_STREAM_TYPE_CONTROL],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Control), true)),
            true,
        );
    }

    #[test]
    fn decode_stream_unknown() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[0x3fff_ffff_ffff_ffff],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Unknown), true)),
            true,
        );
    }

    // Server push is not supported (RFC 9114, Section 6.2.2). A client rejects a push stream with
    // H3_ID_ERROR because it never enabled push; a server, which must never receive one, rejects it
    // with H3_STREAM_CREATION_ERROR.
    #[test]
    fn decode_stream_push() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[HTTP3_UNI_STREAM_TYPE_PUSH],
            false,
            &Err(Error::HttpId),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Server);
        t.decode(
            &[HTTP3_UNI_STREAM_TYPE_PUSH],
            false,
            &Err(Error::HttpStreamCreation),
            true,
        );
    }

    #[test]
    fn decode_stream_http() {
        let mut t = Test::new(StreamType::BiDi, Role::Server);
        t.decode(
            &[u64::from(HFrameType::HEADERS)],
            false,
            &Ok((
                ReceiveOutput::NewStream(NewStreamType::Http(u64::from(HFrameType::HEADERS))),
                true,
            )),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Client);
        t.decode(
            &[u64::from(HFrameType::HEADERS)],
            false,
            &Err(Error::HttpStreamCreation),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Server);
        t.decode(
            &[HFrameType::SETTINGS.into()],
            true,
            &Err(Error::HttpFrame),
            true,
        );
    }

    #[test]
    fn decode_stream_reserved_frame_type() {
        // A reserved (HTTP/2) frame type that starts a request stream is a connection error of
        // type H3_FRAME_UNEXPECTED, the same as when it arrives as a later frame, rather than
        // being discharged like an unknown type (RFC 9114, Sections 7.1 and 11.2.1).
        for reserved in HFrameType::RESERVED {
            let mut t = Test::new(StreamType::BiDi, Role::Server);
            t.decode(
                &[u64::from(*reserved)],
                false,
                &Err(Error::HttpFrameUnexpected),
                true,
            );
        }
    }

    #[test]
    fn decode_stream_wt_bidi() {
        let mut t = Test::new(StreamType::BiDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_STREAM, 0xaaaa_aaaa],
            false,
            &Ok((
                ReceiveOutput::NewStream(NewStreamType::WebTransportStream(0xaaaa_aaaa)),
                true,
            )),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_STREAM],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Unknown), true)),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_STREAM, 0xaaaa_aaaa],
            false,
            &Ok((
                ReceiveOutput::NewStream(NewStreamType::WebTransportStream(0xaaaa_aaaa)),
                true,
            )),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_STREAM],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Unknown), true)),
            true,
        );
    }

    #[test]
    fn decode_stream_wt_unidi() {
        let mut t = Test::new(StreamType::UniDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM, 0xaaaa_aaaa],
            false,
            &Ok((
                ReceiveOutput::NewStream(NewStreamType::WebTransportStream(0xaaaa_aaaa)),
                true,
            )),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM],
            false,
            // WEBTRANSPORT_UNI_STREAM is treated as an unknown frame type here.
            &Ok((ReceiveOutput::NewStream(NewStreamType::Http(84)), true)),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM, 0xaaaa_aaaa],
            false,
            &Ok((
                ReceiveOutput::NewStream(NewStreamType::WebTransportStream(0xaaaa_aaaa)),
                true,
            )),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM],
            false,
            &Err(Error::HttpStreamCreation),
            true,
        );
    }

    #[test]
    fn done_decoding() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[0x3fff],
            false,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Unknown), true)),
            true,
        );
        // NewStreamHeadReader is done, it will not continue reading from the stream.
        t.decode(
            &[QPACK_UNI_STREAM_TYPE_DECODER],
            false,
            &Ok((ReceiveOutput::NoOutput, true)),
            true,
        );
    }

    #[test]
    fn decoding_truncate() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode_buffer(&[0xff], false, &Ok((ReceiveOutput::NoOutput, false)), false);
    }

    #[test]
    fn reset() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decoder.reset(CloseType::ResetRemote(0x100)).unwrap();
        // after a reset NewStreamHeadReader will not read more data.
        t.decode(
            &[QPACK_UNI_STREAM_TYPE_DECODER],
            false,
            &Ok((ReceiveOutput::NoOutput, true)),
            true,
        );
    }

    #[test]
    fn stream_fin_decoder() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[QPACK_UNI_STREAM_TYPE_DECODER],
            true,
            &Err(Error::HttpClosedCriticalStream),
            true,
        );
    }

    #[test]
    fn stream_fin_encoder() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[QPACK_UNI_STREAM_TYPE_ENCODER],
            true,
            &Err(Error::HttpClosedCriticalStream),
            true,
        );
    }

    #[test]
    fn stream_fin_control() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[HTTP3_UNI_STREAM_TYPE_CONTROL],
            true,
            &Err(Error::HttpClosedCriticalStream),
            true,
        );
    }

    #[test]
    fn stream_fin_wt() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM, 0xaaaa_aaaa],
            true,
            &Err(Error::HttpGeneralProtocol),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM],
            true,
            &Err(Error::HttpStreamCreation),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM, 0xaaaa_aaaa],
            true,
            &Err(Error::HttpGeneralProtocol),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_UNI_STREAM],
            true,
            &Err(Error::HttpStreamCreation),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_STREAM, 0xaaaa_aaaa],
            true,
            &Err(Error::HttpGeneralProtocol),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Client);
        t.decode(
            &[WEBTRANSPORT_STREAM],
            true,
            &Err(Error::HttpStreamCreation),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_STREAM, 0xaaaa_aaaa],
            true,
            &Err(Error::HttpGeneralProtocol),
            true,
        );

        let mut t = Test::new(StreamType::BiDi, Role::Server);
        t.decode(
            &[WEBTRANSPORT_STREAM],
            true,
            &Err(Error::HttpStreamCreation),
            true,
        );
    }

    #[test]
    fn stream_fin_uknown() {
        let mut t = Test::new(StreamType::UniDi, Role::Client);
        t.decode(
            &[0x3fff_ffff_ffff_ffff],
            true,
            &Ok((ReceiveOutput::NewStream(NewStreamType::Unknown), true)),
            true,
        );

        let mut t = Test::new(StreamType::UniDi, Role::Client);
        // A stream ID of 0x3fff_ffff_ffff_ffff is encoded into [0xff; 8].
        // For this test the stream type is truncated.
        // This should cause an error.
        t.decode_buffer(&[0xff; 7], true, &Err(Error::HttpStreamCreation), true);
    }
}
