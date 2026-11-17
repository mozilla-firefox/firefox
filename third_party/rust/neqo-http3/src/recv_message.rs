// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    cmp::min,
    fmt::{self, Debug, Display, Formatter},
    rc::Rc,
    time::Instant,
};

use neqo_common::{Header, header::HeadersExt as _};
use neqo_qpack as qpack;
use neqo_transport::{Connection, StreamId};
use tracing::{debug, info, trace};

use crate::{
    CloseType, Error, Http3StreamInfo, Http3StreamType, HttpRecvStream, HttpRecvStreamEvents,
    MessageType, Priority, ReceiveOutput, RecvStream, Res, Stream,
    frames::{
        FrameReader, HFrame, StreamReaderConnectionWrapper,
        hframe::{HFrameType, MAX_HEADER_BYTES},
    },
    headers_checks::{headers_valid, is_interim},
    priority::PriorityHandler,
    qlog,
};

pub struct RecvMessageInfo {
    pub message_type: MessageType,
    pub stream_type: Http3StreamType,
    pub stream_id: StreamId,
    pub first_frame_type: Option<u64>,
}

/*
 * Response stream state:
 *    WaitingForResponseHeaders : we wait for headers.
 *    DecodingHeaders : In this step the headers will be decoded. The stream
 *                      may be blocked in this state on encoder instructions.
 *    WaitingForData : we got HEADERS, we are waiting for one or more data
 *                     frames. In this state we can receive a HEADERS frame
 *                     carrying trailers.
 *    ReadingData : we got a DATA frame, now we letting the app read payload.
 *                  From here we will go back to WaitingForData state to wait
 *                  for more data frames or to CLosed state
 *    ClosePending : waiting for app to pick up data, after that we can delete
 * the TransactionClient.
 *    Closed
 *    ExtendedConnect: this request is for a WebTransport session. In this
 *                         state RecvMessage will not be treated as a HTTP
 *                         stream anymore. It is waiting to be transformed
 *                         into WebTransport session or to be closed.
 */
#[derive(Debug)]
enum RecvMessageState {
    WaitingForResponseHeaders { frame_reader: FrameReader },
    DecodingHeaders { header_block: Vec<u8>, fin: bool },
    WaitingForData { frame_reader: FrameReader },
    ReadingData { remaining_data_len: usize },
    WaitingForFinAfterTrailers { frame_reader: FrameReader },
    ClosePending, // Close must first be read by application
    Closed,
    ExtendedConnect,
}

#[derive(Debug)]
pub struct RecvMessage {
    state: RecvMessageState,
    stream_info: Http3StreamInfo,
    message_type: MessageType,
    stream_type: Http3StreamType,
    qpack_decoder: Rc<RefCell<qpack::Decoder>>,
    conn_events: Box<dyn HttpRecvStreamEvents>,
    priority_handler: PriorityHandler,
    interim_budget: usize,
}

impl Display for RecvMessage {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "RecvMessage {}", self.stream_info.stream_id())
    }
}

impl RecvMessage {
    pub fn new(
        message_info: &RecvMessageInfo,
        qpack_decoder: Rc<RefCell<qpack::Decoder>>,
        conn_events: Box<dyn HttpRecvStreamEvents>,
        priority_handler: PriorityHandler,
    ) -> Self {
        Self {
            state: RecvMessageState::WaitingForResponseHeaders {
                frame_reader: message_info
                    .first_frame_type
                    .map_or_else(FrameReader::new, |frame_type| {
                        FrameReader::new_with_type(HFrameType(frame_type))
                    }),
            },
            stream_info: Http3StreamInfo::new(message_info.stream_id, Http3StreamType::Http),
            message_type: message_info.message_type,
            stream_type: message_info.stream_type,
            qpack_decoder,
            conn_events,
            priority_handler,
            interim_budget: MAX_HEADER_BYTES,
        }
    }

    fn handle_headers_frame(&mut self, header_block: Vec<u8>, fin: bool) -> Res<()> {
        match self.state {
            RecvMessageState::WaitingForResponseHeaders { .. } => {
                if header_block.is_empty() {
                    return Err(Error::HttpGeneralProtocolStream);
                }
                self.state = RecvMessageState::DecodingHeaders { header_block, fin };
            }
            RecvMessageState::WaitingForData { .. } => {
                // TODO implement trailers, for now just ignore them.
                self.state = RecvMessageState::WaitingForFinAfterTrailers {
                    frame_reader: FrameReader::new(),
                };
            }
            RecvMessageState::WaitingForFinAfterTrailers { .. } => {
                return Err(Error::HttpFrameUnexpected);
            }
            _ => unreachable!(
                "This functions is only called in WaitingForResponseHeaders | WaitingForData | WaitingForFinAfterTrailers state"
            ),
        }
        Ok(())
    }

    fn handle_data_frame(&mut self, len: u64, fin: bool) -> Res<()> {
        match self.state {
            RecvMessageState::WaitingForResponseHeaders { .. }
            | RecvMessageState::WaitingForFinAfterTrailers { .. } => {
                return Err(Error::HttpFrameUnexpected);
            }
            RecvMessageState::WaitingForData { .. } => {
                if len > 0 {
                    if fin {
                        return Err(Error::HttpFrame);
                    }
                    self.state = RecvMessageState::ReadingData {
                        remaining_data_len: usize::try_from(len).or(Err(Error::HttpFrame))?,
                    };
                }
            }
            _ => unreachable!(
                "This functions is only called in WaitingForResponseHeaders | WaitingForData | WaitingForFinAfterTrailers state"
            ),
        }
        Ok(())
    }

    fn add_headers(&mut self, mut headers: Vec<Header>, fin: bool) -> Res<()> {
        trace!("[{self}] Add new headers fin={fin}");
        let interim = match self.message_type {
            MessageType::Request => false,
            MessageType::Response => is_interim(&headers)?,
        };
        headers_valid(&headers, self.message_type)?;

        if fin && interim {
            return Err(Error::HttpGeneralProtocolStream);
        }

        if interim {
            let size = headers.iter().map(Header::field_size).sum();
            self.interim_budget = self
                .interim_budget
                .checked_sub(size)
                .ok_or(Error::HttpExcessiveLoad)?;
        }
        if self.message_type == MessageType::Response {
            headers.retain(Header::is_allowed_for_response);
        }

        let is_extended_connect = self.message_type == MessageType::Request
            && headers.contains_header(":method", "CONNECT")
            && headers.find_header(":protocol").is_some();
        if is_extended_connect {
            self.conn_events
                .extended_connect_new_session(self.stream_info.stream_id(), headers);
        } else {
            self.conn_events
                .header_ready(&self.stream_info, headers, interim, fin);
        }

        if fin {
            self.set_closed();
        } else {
            self.state = if is_extended_connect {
                self.stream_type = Http3StreamType::ExtendedConnect;
                RecvMessageState::ExtendedConnect
            } else if interim {
                RecvMessageState::WaitingForResponseHeaders {
                    frame_reader: FrameReader::new(),
                }
            } else {
                RecvMessageState::WaitingForData {
                    frame_reader: FrameReader::new(),
                }
            };
        }
        Ok(())
    }

    fn set_state_to_close_pending(&mut self, post_readable_event: bool) -> Res<()> {
        // Stream has received fin. Depending on headers state set header_ready
        // or data_readable event so that app can pick up the fin.
        trace!(
            "[{self}] set_state_to_close_pending: state={:?}",
            self.state
        );

        match self.state {
            RecvMessageState::WaitingForResponseHeaders { .. } => {
                return Err(Error::HttpGeneralProtocolStream);
            }
            RecvMessageState::ReadingData { .. } => {}
            RecvMessageState::WaitingForData { .. }
            | RecvMessageState::WaitingForFinAfterTrailers { .. } => {
                if post_readable_event {
                    self.conn_events.data_readable(&self.stream_info);
                }
            }
            _ => unreachable!("Closing an already closed transaction"),
        }
        if !matches!(self.state, RecvMessageState::Closed) {
            self.state = RecvMessageState::ClosePending;
        }
        Ok(())
    }

    fn receive_internal(
        &mut self,
        conn: &mut Connection,
        post_readable_event: bool,
        now: Instant,
    ) -> Res<()> {
        loop {
            debug!("[{self}] state={:?}", self.state);
            match &mut self.state {
                // In the following 3 states we need to read frames.
                RecvMessageState::WaitingForResponseHeaders { frame_reader }
                | RecvMessageState::WaitingForData { frame_reader }
                | RecvMessageState::WaitingForFinAfterTrailers { frame_reader } => {
                    match frame_reader.receive(
                        &mut StreamReaderConnectionWrapper::new(conn, self.stream_info.stream_id()),
                        now,
                    )? {
                        (None, true) => {
                            break self.set_state_to_close_pending(post_readable_event);
                        }
                        (None, false) => break Ok(()),
                        (Some(frame), fin) => {
                            debug!(
                                "[{self}] recv frame: {frame:?}; state={:?} fin={fin}",
                                self.state,
                            );
                            match frame {
                                HFrame::Headers { header_block } => {
                                    self.handle_headers_frame(header_block, fin)?;
                                }
                                HFrame::Data { len } => self.handle_data_frame(len, fin)?,
                                // Server push is not supported. A client (reading a response)
                                // rejects PUSH_PROMISE with H3_ID_ERROR because it never enabled
                                // push; a server, which must never receive one, with
                                // H3_FRAME_UNEXPECTED. See
                                // <https://www.rfc-editor.org/rfc/rfc9114.html#section-7.2.5>.
                                HFrame::PushPromise => {
                                    break Err(if self.message_type == MessageType::Response {
                                        Error::HttpId
                                    } else {
                                        Error::HttpFrameUnexpected
                                    });
                                }
                                _ => break Err(Error::HttpFrameUnexpected),
                            }
                            if matches!(self.state, RecvMessageState::Closed) {
                                break Ok(());
                            }
                            if fin
                                && !matches!(self.state, RecvMessageState::DecodingHeaders { .. })
                            {
                                break self.set_state_to_close_pending(post_readable_event);
                            }
                        }
                    }
                }
                RecvMessageState::DecodingHeaders { header_block, fin } => {
                    let done = *fin;
                    let d_headers = self
                        .qpack_decoder
                        .borrow_mut()
                        .decode_header_block(header_block, self.stream_info.stream_id())?;
                    if let Some(headers) = d_headers {
                        self.add_headers(headers, done)?;
                        if matches!(
                            self.state,
                            RecvMessageState::Closed | RecvMessageState::ExtendedConnect
                        ) {
                            break Ok(());
                        }
                    } else {
                        info!("[{self}] decoding header is blocked");
                        break Ok(());
                    }
                }
                RecvMessageState::ReadingData { .. } => {
                    if post_readable_event {
                        self.conn_events.data_readable(&self.stream_info);
                    }
                    break Ok(());
                }
                RecvMessageState::ClosePending | RecvMessageState::Closed => {
                    panic!("Stream readable after being closed!");
                }
                RecvMessageState::ExtendedConnect => {
                    // Ignore read event, this request is waiting to be picked up by a new
                    // WebTransportSession
                    break Ok(());
                }
            }
        }
    }

    fn set_closed(&mut self) {
        self.state = RecvMessageState::Closed;
        self.conn_events
            .recv_closed(&self.stream_info, CloseType::Done);
    }

    const fn closing(&self) -> bool {
        matches!(
            self.state,
            RecvMessageState::ClosePending | RecvMessageState::Closed
        )
    }
}

impl Stream for RecvMessage {
    fn stream_type(&self) -> Http3StreamType {
        self.stream_type
    }
}

impl RecvStream for RecvMessage {
    fn receive(&mut self, conn: &mut Connection, now: Instant) -> Res<(ReceiveOutput, bool)> {
        self.receive_internal(conn, true, now)?;
        Ok((
            ReceiveOutput::NoOutput,
            matches!(self.state, RecvMessageState::Closed),
        ))
    }

    fn reset(&mut self, close_type: CloseType) -> Res<()> {
        if !self.closing() {
            self.qpack_decoder
                .borrow_mut()
                .cancel_stream(self.stream_info.stream_id());
        }
        self.conn_events.recv_closed(&self.stream_info, close_type);
        self.state = RecvMessageState::Closed;
        Ok(())
    }

    fn read_data(
        &mut self,
        conn: &mut Connection,
        buf: &mut [u8],
        now: Instant,
    ) -> Res<(usize, bool)> {
        let stream_id = self.stream_info.stream_id();
        let mut written = 0;
        loop {
            match self.state {
                RecvMessageState::ReadingData {
                    ref mut remaining_data_len,
                } => {
                    let to_read = min(*remaining_data_len, buf.len() - written);
                    let (amount, fin) =
                        conn.stream_recv(stream_id, &mut buf[written..written + to_read])?;
                    qlog::h3_data_moved_up(conn.qlog_mut(), stream_id, amount, now);

                    debug_assert!(amount <= to_read);
                    *remaining_data_len -= amount;
                    written += amount;

                    if fin {
                        if *remaining_data_len > 0 {
                            return Err(Error::HttpFrame);
                        }
                        self.set_closed();
                        break Ok((written, fin));
                    } else if *remaining_data_len == 0 {
                        self.state = RecvMessageState::WaitingForData {
                            frame_reader: FrameReader::new(),
                        };
                        self.receive_internal(conn, false, now)?;
                    } else {
                        break Ok((written, false));
                    }
                }
                RecvMessageState::ClosePending => {
                    self.set_closed();
                    break Ok((written, true));
                }
                _ => break Ok((written, false)),
            }
        }
    }

    fn http_stream(&mut self) -> Option<&mut dyn HttpRecvStream> {
        Some(self)
    }
}

impl HttpRecvStream for RecvMessage {
    fn header_unblocked(
        &mut self,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<(ReceiveOutput, bool)> {
        self.receive(conn, now)
    }

    fn maybe_update_priority(&mut self, priority: Priority) -> Res<bool> {
        Ok(self.priority_handler.maybe_update_priority(priority))
    }

    fn priority_update_frame(&mut self) -> Option<HFrame> {
        self.priority_handler
            .maybe_encode_frame(self.stream_info.stream_id())
    }

    fn priority_update_sent(&mut self) -> Res<()> {
        self.priority_handler.priority_update_sent();
        Ok(())
    }

    fn set_new_listener(&mut self, conn_events: Box<dyn HttpRecvStreamEvents>) {
        self.state = RecvMessageState::WaitingForData {
            frame_reader: FrameReader::new(),
        };
        self.conn_events = conn_events;
    }

    fn extended_connect_wait_for_response(&self) -> bool {
        matches!(self.state, RecvMessageState::ExtendedConnect)
    }
}
