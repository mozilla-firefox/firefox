// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    cmp::min,
    fmt::{self, Debug, Display, Formatter},
    num::NonZeroUsize,
    rc::Rc,
    time::Instant,
};

use neqo_common::{Buffer, Encoder, Header, MessageType, to_u64};
use neqo_qpack as qpack;
use neqo_transport::{Connection, StreamId};
use tracing::{debug, trace};

use crate::{
    BufferedStream, CloseType, Error, Http3StreamInfo, Http3StreamType, HttpSendStream, Res,
    SendStream, SendStreamEvents, Stream,
    frames::{HFrame, HFrameType},
    headers_checks::{headers_valid, is_interim, trailers_valid},
};

const MIN_DATA_FRAME_SIZE: usize = 3; // Minimal DATA frame size: 2 (header) + 1 (payload)
const MAX_DATA_HEADER_SIZE_2: usize = (1 << 6) - 1; // Maximal amount of data with DATA frame header size 2
const MAX_DATA_HEADER_SIZE_2_LIMIT: usize = MAX_DATA_HEADER_SIZE_2 + 3; // 63 + 3 (size of the next buffer data frame header)
const MAX_DATA_HEADER_SIZE_3: usize = (1 << 14) - 1; // Maximal amount of data with DATA frame header size 3
const MAX_DATA_HEADER_SIZE_3_LIMIT: usize = MAX_DATA_HEADER_SIZE_3 + 5; // 16383 + 5 (size of the next buffer data frame header)
const MAX_DATA_HEADER_SIZE_5: usize = (1 << 30) - 1; // Maximal amount of data with DATA frame header size 3
const MAX_DATA_HEADER_SIZE_5_LIMIT: usize = MAX_DATA_HEADER_SIZE_5 + 9; // 1073741823 + 9 (size of the next buffer data frame header)

/// A HTTP message, request and response, consists of headers, optional data and an optional
/// trailer header block. This state machine does not reflect what was already sent to the
/// transport layer but only reflect what has been supplied to the `SendMessage`. It is
/// represented by the following states:
///   `WaitingForHeaders` - the headers have not been supplied yet. In this state only a
///                         request/response header can be added. When headers are supplied
///                         the state changes to `WaitingForData`. A response may contain
///                         multiple messages only if all but the last one are informational(1xx)
///                         responses. The informational responses can only contain headers,
///                         therefore after an informational response is received the state
///                         machine states in `WaitingForHeaders` state.
///   `WaitingForData` - in this state, data and trailers can be supplied. This state means that
///                      a request or response header is already supplied.
///   `TrailersSet` - trailers have been supplied. At this stage no more data or headers can be
///                   supply only a fin.
///   `Done` - in this state no more data or headers can be added. This state is entered when the
///            message is closed.
#[derive(Debug, PartialEq)]
enum MessageState {
    WaitingForHeaders,
    WaitingForData,
    TrailersSet,
    Done,
}

impl MessageState {
    fn new_headers(&mut self, headers: &[Header], message_type: MessageType) -> Res<()> {
        match &self {
            Self::WaitingForHeaders => {
                match message_type {
                    MessageType::Request => {
                        *self = Self::WaitingForData; // Validated by `RequestDescription::new`.
                    }
                    MessageType::Response => {
                        let interim = headers_valid(headers, message_type)
                            .and_then(|()| is_interim(headers))
                            .map_err(|_| Error::InvalidInput)?;
                        if !interim {
                            *self = Self::WaitingForData;
                        }
                    }
                }
                Ok(())
            }
            Self::WaitingForData => {
                trailers_valid(headers)?;
                *self = Self::TrailersSet;
                Ok(())
            }
            Self::TrailersSet | Self::Done => Err(Error::InvalidInput),
        }
    }

    fn new_data(&self) -> Res<()> {
        if &Self::WaitingForData == self {
            Ok(())
        } else {
            Err(Error::InvalidInput)
        }
    }

    const fn fin(&mut self) -> Res<()> {
        match &self {
            Self::WaitingForHeaders | Self::Done => Err(Error::InvalidInput),
            Self::WaitingForData | Self::TrailersSet => {
                *self = Self::Done;
                Ok(())
            }
        }
    }

    fn done(&self) -> bool {
        &Self::Done == self
    }
}

#[derive(Debug)]
pub struct SendMessage {
    state: MessageState,
    stream_info: Http3StreamInfo,
    message_type: MessageType,
    stream_type: Http3StreamType,
    stream: BufferedStream,
    encoder: Rc<RefCell<qpack::Encoder>>,
    conn_events: Box<dyn SendStreamEvents>,
}

impl SendMessage {
    pub fn new(
        message_type: MessageType,
        stream_type: Http3StreamType,
        stream_id: StreamId,
        encoder: Rc<RefCell<qpack::Encoder>>,
        conn_events: Box<dyn SendStreamEvents>,
    ) -> Self {
        debug!("Create a request stream_id={stream_id}");
        Self {
            state: MessageState::WaitingForHeaders,
            stream_info: Http3StreamInfo::new(stream_id, Http3StreamType::Http),
            message_type,
            stream_type,
            stream: BufferedStream::new(stream_id),
            encoder,
            conn_events,
        }
    }

    /// Bytes a `DATA` frame carrying `payload_len` payload bytes takes on the
    /// stream: the frame's type and length varints plus the payload. Mirrors
    /// what [`Self::send_data_atomic`] writes.
    #[must_use]
    pub(crate) fn data_frame_len(payload_len: usize) -> usize {
        Encoder::varint_len(u64::from(HFrameType::DATA))
            + Encoder::varint_len(to_u64(payload_len))
            + payload_len
    }

    /// # Errors
    ///
    /// `ClosedCriticalStream` if the encoder stream is closed.
    /// `InternalError` if an unexpected error occurred.
    fn encode<B: Buffer>(
        encoder: &mut Encoder<B>,
        qpack_encoder: &mut qpack::Encoder,
        headers: &[Header],
        conn: &mut Connection,
        stream_id: StreamId,
    ) {
        debug!("Encoding headers");
        let header_block = qpack_encoder.encode_header_block(conn, headers, stream_id);
        let hframe = HFrame::Headers {
            header_block: header_block.to_vec(),
        };
        hframe.encode(encoder);
    }

    const fn stream_id(&self) -> StreamId {
        self.stream_info.stream_id()
    }
}

impl Stream for SendMessage {
    fn stream_type(&self) -> Http3StreamType {
        self.stream_type
    }
}
impl SendStream for SendMessage {
    fn send_data(&mut self, conn: &mut Connection, buf: &[u8], now: Instant) -> Res<usize> {
        trace!("[{self}] send_body: len={}", buf.len());

        self.state.new_data()?;

        self.stream.send_buffer(conn, now)?;
        if self.has_data_to_send() {
            return Ok(0);
        }
        let available = conn.stream_avail_send_space(self.stream_id())?;
        if available < MIN_DATA_FRAME_SIZE {
            // Setting this once, instead of every time the available send space
            // is exhausted, would suffice. That said, function call should be
            // cheap, thus not worth optimizing.
            conn.stream_set_writable_event_low_watermark(
                self.stream_id(),
                NonZeroUsize::new(MIN_DATA_FRAME_SIZE).ok_or(Error::Internal)?,
            )?;
            return Ok(0);
        }
        let to_send = if available <= MAX_DATA_HEADER_SIZE_2_LIMIT {
            // 63 + 3
            min(min(buf.len(), available - 2), MAX_DATA_HEADER_SIZE_2)
        } else if available <= MAX_DATA_HEADER_SIZE_3_LIMIT {
            // 16383 + 5
            min(min(buf.len(), available - 3), MAX_DATA_HEADER_SIZE_3)
        } else if available <= MAX_DATA_HEADER_SIZE_5 {
            // 1073741823 + 9
            min(min(buf.len(), available - 5), MAX_DATA_HEADER_SIZE_5_LIMIT)
        } else {
            min(buf.len(), available - 9)
        };

        debug!("[{self}] send_request_body: available={available} to_send={to_send}");

        let data_frame = HFrame::Data {
            len: to_u64(to_send),
        };
        let sent_fh = self
            .stream
            .send_atomic_with(conn, |e| data_frame.encode(e), now)?;
        debug_assert!(sent_fh);

        let sent = self.stream.send_atomic(conn, &buf[..to_send], now)?;
        debug_assert!(sent);
        Ok(to_send)
    }

    fn done(&self) -> bool {
        !self.has_data_to_send() && self.state.done()
    }

    fn stream_writable(&self) {
        if !self.has_data_to_send() && !self.state.done() {
            // DataWritable is just a signal for an application to try to write more data,
            // if writing fails it is fine. Therefore we do not need to properly check
            // whether more credits are available on the transport layer.
            self.conn_events.data_writable(&self.stream_info);
        }
    }

    /// # Errors
    ///
    /// `TransportStreamDoesNotExist` if the transport stream does not exist (this may happen if
    /// `process_output` has not been called when needed, and HTTP3 layer has not picked up the
    /// info that the stream has been closed.)
    /// `InvalidInput` if the underlying transport call rejects the data.
    fn send(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        let sent = self.stream.send_buffer(conn, now)?;

        trace!("[{self}] {sent} bytes sent");
        if !self.has_data_to_send() {
            if self.state.done() {
                conn.stream_close_send(self.stream_id())?;
                trace!("[{self}] done sending request");
            } else {
                // DataWritable is just a signal for an application to try to write more data,
                // if writing fails it is fine. Therefore we do not need to properly check
                // whether more credits are available on the transport layer.
                self.conn_events.data_writable(&self.stream_info);
            }
        }
        Ok(())
    }

    fn commit(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        // Flush so the commitment covers everything buffered so far. If it cannot all be flushed
        // the commitment would fall short, so fail rather than silently under-commit.
        self.stream.send_buffer(conn, now)?;
        if self.has_data_to_send() {
            debug!("buffered data at neqo-http3 layer, failing to commit");
            return Err(Error::FlowControlLimit);
        }
        conn.stream_commit(self.stream_id())?;
        Ok(())
    }

    // SendMessage owns headers and sends them. It may also own data for the server side.
    // This method returns if they're still being sent. Request body (if any) is sent by
    // http client afterwards using `send_request_body` after receiving DataWritable event.
    fn has_data_to_send(&self) -> bool {
        self.stream.has_buffered_data()
    }

    fn close(&mut self, conn: &mut Connection, _now: Instant) -> Res<()> {
        self.state.fin()?;
        if !self.has_data_to_send() {
            conn.stream_close_send(self.stream_id())?;
        }

        self.conn_events
            .send_closed(&self.stream_info, CloseType::Done);
        Ok(())
    }

    fn handle_stop_sending(&mut self, close_type: CloseType) {
        if !self.state.done() {
            self.conn_events.send_closed(&self.stream_info, close_type);
        }
    }

    fn http_stream(&mut self) -> Option<&mut dyn HttpSendStream> {
        Some(self)
    }

    fn send_data_atomic(&mut self, conn: &mut Connection, buf: &[u8], now: Instant) -> Res<()> {
        let data_frame = HFrame::Data {
            len: to_u64(buf.len()),
        };
        self.stream.encode_with(|e| data_frame.encode(e));
        self.stream.buffer(buf);
        _ = self.stream.send_buffer(conn, now)?;
        Ok(())
    }
}

impl HttpSendStream for SendMessage {
    fn send_headers(&mut self, headers: &[Header], conn: &mut Connection) -> Res<()> {
        self.state.new_headers(headers, self.message_type)?;
        let stream_id = self.stream_id();
        self.stream.encode_with(|e| {
            Self::encode(e, &mut self.encoder.borrow_mut(), headers, conn, stream_id);
        });
        Ok(())
    }

    fn set_new_listener(&mut self, conn_events: Box<dyn SendStreamEvents>) {
        self.stream_type = Http3StreamType::ExtendedConnect;
        self.conn_events = conn_events;
    }
}

impl Display for SendMessage {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "SendMessage {}", self.stream_id())
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_transport::StreamType;
    use test_fixture::{connect, now};

    use super::*;

    #[derive(Debug)]
    struct NoopEvents;
    impl SendStreamEvents for NoopEvents {}

    /// Regression test for <https://bugzilla.mozilla.org/show_bug.cgi?id=2071513>.
    #[test]
    fn send_after_transport_reset_is_an_error_not_a_panic() {
        let (mut client, _server) = connect();
        let stream_id = client.stream_create(StreamType::BiDi).unwrap();

        let encoder = Rc::new(RefCell::new(qpack::Encoder::new(
            &qpack::Settings::default(),
            true,
        )));
        let mut msg = SendMessage::new(
            MessageType::Request,
            Http3StreamType::Http,
            stream_id,
            encoder,
            Box::new(NoopEvents),
        );
        msg.send_headers(
            &[
                Header::new(":method", "GET"),
                Header::new(":scheme", "https"),
                Header::new(":authority", "something.com"),
                Header::new(":path", "/"),
            ],
            &mut client,
        )
        .unwrap();

        client.stream_reset_send(stream_id, 0).unwrap();

        assert!(matches!(
            msg.send(&mut client, now()),
            Err(Error::TransportStreamDoesNotExist)
        ));
        assert!(client.state().connected());
    }
}
