// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    fmt::{self, Display, Formatter},
    num::NonZeroUsize,
    time::Instant,
};

use neqo_common::{Bytes, Decoder, Encoder};
use neqo_transport::{Connection, StreamId};
use tracing::{debug, trace};

use crate::{
    Error, RecvStream, Res, SendStream,
    features::extended_connect::{
        CloseReason, ExtendedConnectEvents, ExtendedConnectType, Protocol,
        session::{DgramContextIdError, State},
    },
    frames::{FrameReader, StreamReaderRecvStreamWrapper, capsule::Capsule},
    send_message::SendMessage,
};

/// The only context ID RFC 9298 defines: the payload is a UDP packet.
const CONTEXT_ID: u64 = 0;

#[derive(Debug)]
pub struct Session {
    frame_reader: FrameReader,
    session_id: StreamId,
}

impl Session {
    #[must_use]
    pub(crate) fn new(session_id: StreamId) -> Self {
        Self {
            session_id,
            frame_reader: FrameReader::new(),
        }
    }
}

impl Display for Session {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "ConnectUdpSession")
    }
}

impl Protocol for Session {
    fn connect_type(&self) -> ExtendedConnectType {
        ExtendedConnectType::ConnectUdp
    }

    fn read_control_stream(
        &mut self,
        conn: &mut Connection,
        events: &mut Box<dyn ExtendedConnectEvents>,
        control_stream_recv: &mut Box<dyn RecvStream>,
        now: Instant,
    ) -> Res<Option<State>> {
        loop {
            let (capsule, fin) = self
                .frame_reader
                .receive::<Capsule>(
                    &mut StreamReaderRecvStreamWrapper::new(conn, control_stream_recv),
                    now,
                )
                .map_err(|_| Error::HttpGeneralProtocolStream)?;

            let capsule_is_some = capsule.is_some();

            match capsule {
                Some(Capsule::Datagram { payload }) => match self.dgram_context_id(payload) {
                    Ok(slice) => {
                        events.new_datagram(self.session_id, slice, self.connect_type());
                    }
                    Err(e) => {
                        debug!("[{self}]: received capsule with invalid context identifier: {e}");
                    }
                },
                None => {}
            }

            if fin {
                events.session_end(
                    ExtendedConnectType::ConnectUdp,
                    self.session_id,
                    CloseReason::Clean {
                        error: 0,
                        message: String::new(),
                    },
                    None,
                );
                return Ok(Some(State::Done));
            }

            if !capsule_is_some {
                return Ok(None);
            }
        }
    }

    fn datagram_prefix(&self) -> Option<u64> {
        Some(CONTEXT_ID)
    }

    fn dgram_context_id(&self, datagram: Bytes) -> Result<Bytes, DgramContextIdError> {
        let (context_id, offset) = {
            let mut decoder = Decoder::new(datagram.as_ref());
            (decoder.decode_varint(), decoder.offset())
        };
        match context_id {
            Some(0) => Ok(datagram.skip(offset)),
            Some(context_id) => Err(DgramContextIdError::UnknownIdentifier(context_id)),
            None => {
                // > all HTTP Datagrams associated with UDP Proxying request streams start with a Context ID field;
                //
                // <https://datatracker.ietf.org/doc/html/rfc9298#name-context-identifiers>
                Err(DgramContextIdError::MissingIdentifier)
            }
        }
    }

    fn datagram_capsule_support(&self) -> bool {
        true
    }

    fn write_datagram_capsule(
        &self,
        control_stream_send: &mut Box<dyn SendStream>,
        conn: &mut Connection,
        buf: &[u8],
        now: Instant,
    ) -> Res<()> {
        let mut dgram_data = Encoder::with_capacity(Encoder::varint_len(CONTEXT_ID) + buf.len());
        dgram_data.encode_varint(CONTEXT_ID);
        dgram_data.encode(buf);
        // TODO: Make Capsule abstract over either an owned (Bytes) or borrowed (&[u8]) type
        // to avoid this allocation.
        let capsule = Capsule::Datagram {
            payload: Bytes::from(Vec::from(dgram_data)),
        };

        // The capsule goes on the control stream wrapped in an HTTP/3 DATA frame, so the
        // flow-control window has to hold both wrappers, not just the payload.
        let needed = SendMessage::data_frame_len(capsule.encoded_len());
        if conn.stream_avail_send_space(self.session_id)? < needed {
            debug!("[{self}] datagram capsule exceeds control-stream flow-control space");
            // Ask to be told when the stream can hold a capsule this size again.
            // Repeated refusals overwrite this, so the event tracks the most recent
            // size rather than the largest; the sender retries and re-arms.
            if let Some(watermark) = NonZeroUsize::new(needed) {
                conn.stream_set_writable_event_low_watermark(self.session_id, watermark)?;
            }
            return Err(Error::FlowControlLimit);
        }
        let mut enc = Encoder::default();
        capsule.encode(&mut enc);
        control_stream_send.send_data_atomic(conn, enc.as_ref(), now)?;
        // Drop the watermark a previous refusal raised, so neqo-transport stops
        // suppressing writable events for other users of the control stream.
        conn.stream_set_writable_event_low_watermark(self.session_id, NonZeroUsize::MIN)?;
        trace!("[{self}] sent datagram via HTTP DATAGRAM Capsule");
        Ok(())
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::Bytes;
    use neqo_transport::StreamId;

    use super::Session;
    use crate::features::extended_connect::session::Protocol as _;

    #[test]
    fn varint_0_context_id() {
        let session = Session::new(StreamId::new(42));
        // Varint [0x00] is 0, i.e. a supported connect-udp context ID.
        assert_eq!(
            session
                .dgram_context_id(Bytes::from(vec![0x00, 0x00, 0x00]))
                .unwrap(),
            Bytes::from(vec![0x00, 0x00])
        );
        // Varint [0x40 0x00] is 0 as well, thus a supported connect-udp context ID, too.
        assert_eq!(
            session
                .dgram_context_id(Bytes::from(vec![0x40, 0x00, 0x00, 0x00]))
                .unwrap(),
            Bytes::from(vec![0x00, 0x00])
        );
    }
}
