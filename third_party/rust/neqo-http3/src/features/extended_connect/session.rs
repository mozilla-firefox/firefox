// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Debug, Display, Formatter},
    num::NonZeroUsize,
    rc::Rc,
    time::{Duration, Instant},
};

use neqo_common::{Bytes, Encoder, Header, MessageType, Role, to_u64};
use neqo_transport::{
    AppError, Connection, DatagramQueueCapacity, DatagramQueueOutcome, DatagramTracking, StreamId,
    StreamType,
    streams::{SendGroupId, SendOrder},
};
use rustc_hash::FxHashSet as HashSet;
use tracing::{debug, trace};

use crate::{
    CloseType, Error, Http3StreamType, HttpRecvStream, Priority, ReceiveOutput, RecvStream, Res,
    SendStream, Stream,
    features::extended_connect::{
        ExtendedConnectEvents, ExtendedConnectType, HeaderListener, Headers, stats::SessionStats,
    },
    frames::HFrame,
    priority::PriorityHandler,
    recv_message::{RecvMessage, RecvMessageInfo},
    send_message::SendMessage,
};

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum CloseReason {
    Error(AppError),
    Status(u16),
    Clean { error: u32, message: String },
}

impl From<CloseType> for CloseReason {
    fn from(close_type: CloseType) -> Self {
        match close_type {
            CloseType::ResetApp(e) | CloseType::ResetRemote(e) | CloseType::LocalError(e) => {
                Self::Error(e)
            }
            CloseType::Done => Self::Clean {
                error: 0,
                message: String::new(),
            },
        }
    }
}

#[derive(Debug)]
pub(crate) struct Session {
    control_stream_recv: Box<dyn RecvStream>,
    control_stream_send: Box<dyn SendStream>,
    stream_event_listener: Rc<RefCell<HeaderListener>>,
    id: StreamId,
    state: State,
    events: Box<dyn ExtendedConnectEvents>,
    /// Corresponds to the `:protocol` pseudo-header in the HTTP EXTENDED
    /// CONNECT request.
    protocol: Box<dyn Protocol>,
    draining: bool,
    /// Set when a datagram capsule was refused with `FlowControlLimit`, so a
    /// resume event fires once the control stream is writable again.
    datagram_capsule_blocked: bool,
    anticipated_incoming_uni: u16,
    anticipated_incoming_bidi: u16,
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub(crate) enum State {
    Negotiating,
    Active,
    FinPending,
    Done,
}

impl State {
    pub(crate) const fn closing_state(self) -> bool {
        matches!(self, Self::FinPending | Self::Done)
    }
}

impl Display for Session {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{}-session={}", self.protocol.connect_type(), self.id)
    }
}

impl Session {
    #[must_use]
    pub(crate) fn new(
        session_id: StreamId,
        events: Box<dyn ExtendedConnectEvents>,
        role: Role,
        qpack_encoder: Rc<RefCell<neqo_qpack::Encoder>>,
        qpack_decoder: Rc<RefCell<neqo_qpack::Decoder>>,
        connect_type: ExtendedConnectType,
    ) -> Self {
        let stream_event_listener = Rc::new(RefCell::new(HeaderListener::default()));
        let protocol = connect_type.new_protocol(session_id, role);
        Self {
            control_stream_recv: Box::new(RecvMessage::new(
                &RecvMessageInfo {
                    message_type: MessageType::Response,
                    stream_type: Http3StreamType::ExtendedConnect,
                    stream_id: session_id,
                    first_frame_type: None,
                },
                qpack_decoder,
                Box::new(Rc::clone(&stream_event_listener)),
                PriorityHandler::new(Priority::default(), false),
            )),
            control_stream_send: Box::new(SendMessage::new(
                MessageType::Request,
                Http3StreamType::ExtendedConnect,
                session_id,
                qpack_encoder,
                Box::new(Rc::clone(&stream_event_listener)),
            )),
            stream_event_listener,
            id: session_id,
            state: State::Negotiating,
            events,
            protocol,
            draining: false,
            datagram_capsule_blocked: false,
            anticipated_incoming_uni: 0,
            anticipated_incoming_bidi: 0,
        }
    }

    pub(crate) fn new_with_http_streams(
        session_id: StreamId,
        events: Box<dyn ExtendedConnectEvents>,
        role: Role,
        mut control_stream_recv: Box<dyn RecvStream>,
        mut control_stream_send: Box<dyn SendStream>,
        connect_type: ExtendedConnectType,
    ) -> Res<Self> {
        let stream_event_listener = Rc::new(RefCell::new(HeaderListener::default()));
        let protocol = connect_type.new_protocol(session_id, role);
        control_stream_recv
            .http_stream()
            .ok_or(Error::Internal)?
            .set_new_listener(Box::new(Rc::clone(&stream_event_listener)));
        control_stream_send
            .http_stream()
            .ok_or(Error::Internal)?
            .set_new_listener(Box::new(Rc::clone(&stream_event_listener)));
        Ok(Self {
            control_stream_recv,
            control_stream_send,
            stream_event_listener,
            id: session_id,
            state: State::Active,
            events,
            protocol,
            draining: false,
            datagram_capsule_blocked: false,
            anticipated_incoming_uni: 0,
            anticipated_incoming_bidi: 0,
        })
    }

    /// Returns the type of this extended CONNECT session.
    pub(crate) fn connect_type(&self) -> ExtendedConnectType {
        self.protocol.connect_type()
    }

    /// Returns the stream ID of this extended CONNECT session.
    pub(crate) const fn id(&self) -> StreamId {
        self.id
    }

    pub(crate) const fn set_anticipated_incoming(&mut self, stream_type: StreamType, value: u16) {
        match stream_type {
            StreamType::UniDi => self.anticipated_incoming_uni = value,
            StreamType::BiDi => self.anticipated_incoming_bidi = value,
        }
    }

    #[must_use]
    pub(crate) const fn anticipated_incoming(&self, stream_type: StreamType) -> u16 {
        match stream_type {
            StreamType::UniDi => self.anticipated_incoming_uni,
            StreamType::BiDi => self.anticipated_incoming_bidi,
        }
    }

    /// Mark session as draining. Returns `true` if this was the first call
    /// (i.e. the session was not already draining).
    pub(crate) const fn set_draining(&mut self) -> bool {
        !std::mem::replace(&mut self.draining, true)
    }

    /// # Errors
    ///
    /// The function can only fail if supplied headers are not valid http headers.
    pub(crate) fn send_request(&mut self, headers: &[Header], conn: &mut Connection) -> Res<()> {
        debug!("[{self}]: send_request {headers:?}");
        self.control_stream_send
            .http_stream()
            .ok_or(Error::Internal)?
            .send_headers(headers, conn)
    }

    fn receive(&mut self, conn: &mut Connection, now: Instant) -> Res<(ReceiveOutput, bool)> {
        trace!("[{self}] receive control data");
        let (out, _) = self.control_stream_recv.receive(conn, now)?;
        debug_assert_eq!(out, ReceiveOutput::NoOutput);
        self.maybe_check_headers()?;
        self.read_control_stream(conn, now)?;
        Ok((ReceiveOutput::NoOutput, self.state == State::Done))
    }

    fn header_unblocked(
        &mut self,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<(ReceiveOutput, bool)> {
        let (out, _) = self
            .control_stream_recv
            .http_stream()
            .ok_or(Error::Internal)?
            .header_unblocked(conn, now)?;
        debug_assert_eq!(out, ReceiveOutput::NoOutput);
        self.maybe_check_headers()?;
        self.read_control_stream(conn, now)?;
        Ok((ReceiveOutput::NoOutput, self.state == State::Done))
    }

    fn maybe_update_priority(&mut self, priority: Priority) -> Res<bool> {
        self.control_stream_recv
            .http_stream()
            .ok_or(Error::Internal)?
            .maybe_update_priority(priority)
    }

    fn priority_update_frame(&mut self) -> Option<HFrame> {
        self.control_stream_recv
            .http_stream()?
            .priority_update_frame()
    }

    fn priority_update_sent(&mut self) -> Res<()> {
        self.control_stream_recv
            .http_stream()
            .ok_or(Error::Internal)?
            .priority_update_sent()
    }

    fn send(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        self.control_stream_send.send(conn, now)?;
        if self.control_stream_send.done() {
            self.state = State::Done;
        }
        Ok(())
    }

    fn stream_writable(&mut self) {
        // The control stream has flow-control space again; if a datagram capsule
        // was refused, tell the sender it can resume.
        if self.datagram_capsule_blocked {
            self.datagram_capsule_blocked = false;
            self.events.capsule_space_available(self.id);
        }
    }

    fn close(&mut self, close_type: CloseType) {
        if self.state.closing_state() {
            return;
        }
        debug!("[{self}]: close session type={close_type:?}");
        self.state = State::Done;
        if !close_type.locally_initiated() {
            self.events.session_end(
                self.protocol.connect_type(),
                self.id,
                CloseReason::from(close_type),
                None,
            );
        }
    }

    pub(crate) fn maybe_check_headers(&mut self) -> Res<()> {
        if self.state != State::Negotiating {
            return Ok(());
        }

        if let Some(Headers {
            headers,
            interim,
            fin,
        }) = self.stream_event_listener.borrow_mut().get_headers()
        {
            trace!("ExtendedConnect response headers {headers:?}, fin={fin}");

            if interim {
                if fin {
                    self.events.session_end(
                        self.protocol.connect_type(),
                        self.id,
                        CloseReason::Clean {
                            error: 0,
                            message: String::new(),
                        },
                        Some(headers),
                    );
                    self.state = State::Done;
                }
            } else {
                let status = headers
                    .iter()
                    .find_map(|h| {
                        if h.name() == ":status" {
                            str::from_utf8(h.value()).ok()?.parse::<u16>().ok()
                        } else {
                            None
                        }
                    })
                    .ok_or(Error::Internal)?;

                self.state = if (200..300).contains(&status) {
                    if fin {
                        self.events.session_end(
                            self.protocol.connect_type(),
                            self.id,
                            CloseReason::Clean {
                                error: 0,
                                message: String::new(),
                            },
                            Some(headers),
                        );
                        State::Done
                    } else {
                        self.protocol.process_response_headers(&headers);

                        self.events.session_start(
                            self.protocol.connect_type(),
                            self.id,
                            status,
                            headers,
                        );
                        self.protocol.session_start(&mut self.events)?;
                        State::Active
                    }
                } else {
                    self.events.session_end(
                        self.protocol.connect_type(),
                        self.id,
                        CloseReason::Status(status),
                        Some(headers),
                    );
                    State::Done
                };
            }
        }
        Ok(())
    }

    pub(crate) fn add_stream(&mut self, stream_id: StreamId) -> Res<()> {
        self.protocol
            .add_stream(stream_id, &mut self.events, self.state)
    }

    pub(crate) fn remove_recv_stream(&mut self, stream_id: StreamId) {
        self.protocol.remove_recv_stream(stream_id);
    }

    pub(crate) fn remove_send_stream(&mut self, stream_id: StreamId) {
        self.protocol.remove_send_stream(stream_id);
    }

    #[must_use]
    pub(crate) const fn is_active(&self) -> bool {
        matches!(self.state, State::Active)
    }

    #[must_use]
    pub(crate) const fn is_closing(&self) -> bool {
        self.state.closing_state()
    }

    pub(crate) fn take_sub_streams(&mut self) -> (HashSet<StreamId>, HashSet<StreamId>) {
        self.protocol.take_sub_streams()
    }

    /// # Errors
    ///
    /// It may return an error if the frame is not correctly decoded.
    pub(crate) fn read_control_stream(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        if self.state == State::Done {
            return Ok(());
        }
        debug!("[{self}]: read_control_stream");
        if let Some(new_state) = self.protocol.read_control_stream(
            conn,
            &mut self.events,
            &mut self.control_stream_recv,
            now,
        )? {
            self.enter_state(new_state, conn, now);
        }
        Ok(())
    }

    /// Change state; on the first move into a closing state, expire and drop
    /// this session's queued datagrams, as none of them can be sent anymore.
    fn enter_state(&mut self, state: State, conn: &mut Connection, now: Instant) {
        if state.closing_state() && !self.state.closing_state() {
            self.expire_datagrams(conn, now);
            self.drop_queued_datagrams(conn);
        }
        self.state = state;
    }

    /// # Errors
    ///
    /// Return an error if the stream was closed on the transport layer, but that information is not
    /// yet consumed on the http/3 layer.
    pub(crate) fn close_session(
        &mut self,
        conn: &mut Connection,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()> {
        debug!("[{self}]: close_session");
        self.enter_state(State::Done, conn, now);

        if let Some(close_frame) = self.protocol.close_frame(error, message) {
            self.control_stream_send
                .send_data_atomic(conn, close_frame.as_ref(), now)?;
        }

        self.control_stream_send.close(conn, now)?;
        self.state = if self.control_stream_send.done() {
            State::Done
        } else {
            State::FinPending
        };
        Ok(())
    }

    fn send_data(&mut self, conn: &mut Connection, buf: &[u8], now: Instant) -> Res<usize> {
        self.control_stream_send.send_data(conn, buf, now)
    }

    /// Send a datagram, as a QUIC datagram or, when the peer offers no QUIC
    /// datagram support, an HTTP DATAGRAM Capsule (connect-udp only).
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// - The session is not in Active state (`Error::Unavailable`).
    /// - The peer supports neither QUIC DATAGRAM nor the HTTP DATAGRAM Capsule fallback
    ///   (`Error::Transport(neqo_transport::Error::NotAvailable)`).
    /// - `send_group_id` is neither `SendGroupId::new(0)` (ungrouped) nor a group already
    ///   registered for this session (`Error::InvalidInput`).
    /// - The encoded datagram (including the session-id/protocol prefix) is longer than the peer's
    ///   datagram limit (`Error::Transport(neqo_transport::Error::TooMuchData)`). This compares the
    ///   payload, not the whole DATAGRAM frame; see `Connection::enqueue_datagram`.
    /// - HTTP DATAGRAM Capsule sending fails.
    ///
    /// On success, returns the queue's backpressure signal (see
    /// [`DatagramQueueOutcome`]) so the caller can throttle further writes.
    pub(crate) fn send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        conn: &mut Connection,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        trace!("[{self}] send_datagram state={:?}", self.state);
        if self.state != State::Active {
            debug!("[{self}]: cannot send datagram in {:?} state.", self.state);
            return Err(Error::Unavailable);
        }

        if conn.remote_datagram_size() == 0 {
            if !self.protocol.datagram_capsule_support() {
                // WebTransport only negotiates with a peer that supports QUIC
                // DATAGRAM, and connect-udp always has the Capsule fallback.
                debug_assert!(false, "no datagram path to the peer");
                debug!("[{self}]: peer supports neither QUIC DATAGRAM nor the Capsule fallback");
                return Err(Error::Transport(neqo_transport::Error::NotAvailable));
            }
            trace!("[{self}] remote_datagram_size is 0, trying HTTP DATAGRAM Capsule");
            // The Capsule path errors when the control stream's flow-control
            // window is exhausted, then emits a resume event once the stream is
            // writable again (see `stream_writable`).
            let res =
                self.protocol
                    .write_datagram_capsule(&mut self.control_stream_send, conn, buf, now);
            if matches!(res, Err(Error::FlowControlLimit)) {
                self.datagram_capsule_blocked = true;
            }
            res?;
            // Bypasses the queue, so count it sent now.
            self.protocol.record_sent_outgoing_datagrams(1);
            return Ok(DatagramQueueOutcome::Ok);
        }

        if send_group_id != SendGroupId::new(0) && !self.validate_send_group(send_group_id) {
            debug!("[{self}]: rejecting datagram with unregistered send group {send_group_id:?}");
            return Err(Error::InvalidInput);
        }

        // Sized exactly: the queue charges the buffer's capacity against its
        // byte budget, and growing from empty would bill the growth slack.
        let quarter_stream_id = self.id.as_u64() / 4;
        let prefix = self.protocol.datagram_prefix();
        let mut dgram_data = Encoder::with_capacity(
            Encoder::varint_len(quarter_stream_id)
                + prefix.map_or(0, Encoder::varint_len)
                + buf.len(),
        );
        dgram_data.encode_varint(quarter_stream_id);
        if let Some(context_id) = prefix {
            dgram_data.encode_varint(context_id);
        }
        dgram_data.encode(buf);

        let id = match id.into() {
            DatagramTracking::None => None,
            DatagramTracking::Id(v) => Some(v),
        };
        let outcome = conn.enqueue_datagram(
            self.id,
            dgram_data.into(),
            id,
            now,
            send_group_id,
            send_order,
        )?;
        trace!("[{self}] enqueued datagram: {outcome:?}");
        match &outcome {
            DatagramQueueOutcome::Ok | DatagramQueueOutcome::MaxBufferedReached => {}
            DatagramQueueOutcome::Rejected => self.report_dropped(1),
            DatagramQueueOutcome::Overflowed { dropped } => self.report_dropped(*dropped),
        }
        Ok(outcome)
    }

    fn report_dropped(&mut self, count: usize) {
        self.protocol
            .record_dropped_outgoing_datagrams(to_u64(count));
    }

    /// Drop this session's queue, recording its remaining datagrams and counts.
    pub(crate) fn drop_queued_datagrams(&mut self, conn: &mut Connection) {
        let dropped = conn.drop_session_datagrams(self.id);
        self.protocol.record_sent_outgoing_datagrams(dropped.sent);
        self.protocol
            .record_expired_outgoing_datagrams(dropped.expired);
        self.protocol
            .record_dropped_outgoing_datagrams(dropped.too_big);
        self.protocol
            .record_dropped_outgoing_datagrams(dropped.queued);
    }

    /// Set the outgoing-datagram queue's `outgoingMaxBufferedDatagrams`, or
    /// clear it back to no limit with `None`.  Ignored once closing: the
    /// queue is already dropped, and setting it would recreate it.
    pub(crate) fn set_max_buffered_datagrams(
        &self,
        conn: &mut Connection,
        mark: Option<NonZeroUsize>,
    ) {
        if self.state.closing_state() {
            return;
        }
        conn.set_max_buffered_datagrams(self.id, mark);
    }

    /// Set the outgoing-datagram queue's `outgoingMaxAge`, or clear it back
    /// to the implementation-defined default with `None`. Shrinking the
    /// limit can immediately push already-queued datagrams past it; those
    /// are expired on the spot and counted here rather than at the next
    /// sweep, so the stats a caller reads right after reflect them.
    /// Ignored once closing, as for [`Self::set_max_buffered_datagrams`].
    pub(crate) fn set_datagram_max_age(
        &mut self,
        conn: &mut Connection,
        max_age: Option<Duration>,
        now: Instant,
    ) {
        if self.state.closing_state() {
            return;
        }
        conn.set_datagram_max_age(self.id, max_age, now);
        self.protocol
            .record_expired_outgoing_datagrams(conn.take_session_expired_datagrams(self.id));
    }

    #[must_use]
    pub(crate) fn datagram_queue_capacity(&self, conn: &Connection) -> DatagramQueueCapacity {
        conn.datagram_queue_capacity(self.id)
    }

    /// Expire stale queued datagrams, counting them against this session's
    /// stats, and return how many expired. See
    /// `Connection::expire_session_datagrams`.
    ///
    /// Scoped to this session: a connection can carry several
    /// extended-CONNECT sessions (`datagrams_multiple_session` covers two),
    /// and a connection-wide sweep would hand whichever session swept first
    /// every other session's expiries, to count as its own.
    pub(crate) fn expire_datagrams(&mut self, conn: &mut Connection, now: Instant) -> u64 {
        let expired = conn.expire_session_datagrams(self.id, now);
        self.protocol.record_expired_outgoing_datagrams(expired);
        expired
    }

    /// Record the sent and too-big counts since the last call.
    pub(crate) fn report_sent_datagrams(&mut self, conn: &mut Connection) {
        let sent = conn.take_session_sent_datagrams(self.id);
        self.protocol.record_sent_outgoing_datagrams(sent);
        let too_big = conn.take_session_too_big_datagrams(self.id);
        self.protocol.record_dropped_outgoing_datagrams(too_big);
    }

    pub(crate) fn datagram(&self, datagram: Bytes) {
        if self.state != State::Active {
            debug!("[{self}]: received datagram on {:?} session.", self.state);
            return;
        }

        // dgram_context_id returns the payload after stripping any context ID
        match self.protocol.dgram_context_id(datagram) {
            Ok(slice) => {
                self.events
                    .new_datagram(self.id, slice, self.protocol.connect_type());
            }
            Err(e) => {
                debug!("[{self}]: received datagram with invalid context identifier: {e}");
            }
        }
    }

    pub(crate) fn validate_send_group(&self, group_id: SendGroupId) -> bool {
        self.protocol.validate_send_group(group_id)
    }

    /// Session statistics, for protocols that track them (only `WebTransport`).
    #[must_use]
    pub(crate) fn stats(&self) -> Option<SessionStats> {
        self.protocol.stats().copied()
    }

    fn has_data_to_send(&self) -> bool {
        self.control_stream_send.has_data_to_send()
    }

    fn done(&self) -> bool {
        self.state == State::Done
    }
}

impl Stream for Rc<RefCell<Session>> {
    fn stream_type(&self) -> Http3StreamType {
        Http3StreamType::ExtendedConnect
    }

    fn session_protocol(&self) -> Option<String> {
        self.borrow().protocol.protocol().map(ToString::to_string)
    }

    fn register_send_group(&mut self, id: SendGroupId) -> Res<()> {
        self.borrow_mut().protocol.register_send_group(id)
    }

    fn validate_send_group(&self, group_id: SendGroupId) -> bool {
        self.borrow().protocol.validate_send_group(group_id)
    }
}

impl RecvStream for Rc<RefCell<Session>> {
    fn receive(&mut self, conn: &mut Connection, now: Instant) -> Res<(ReceiveOutput, bool)> {
        self.borrow_mut().receive(conn, now)
    }

    fn reset(&mut self, close_type: CloseType) -> Res<()> {
        self.borrow_mut().close(close_type);
        Ok(())
    }

    fn http_stream(&mut self) -> Option<&mut dyn HttpRecvStream> {
        Some(self)
    }

    fn extended_connect_session(&self) -> Option<Rc<RefCell<Session>>> {
        Some(Self::clone(self))
    }
}

impl HttpRecvStream for Rc<RefCell<Session>> {
    fn header_unblocked(
        &mut self,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<(ReceiveOutput, bool)> {
        self.borrow_mut().header_unblocked(conn, now)
    }

    fn maybe_update_priority(&mut self, priority: Priority) -> Res<bool> {
        self.borrow_mut().maybe_update_priority(priority)
    }

    fn priority_update_frame(&mut self) -> Option<HFrame> {
        self.borrow_mut().priority_update_frame()
    }

    fn priority_update_sent(&mut self) -> Res<()> {
        self.borrow_mut().priority_update_sent()
    }
}

impl SendStream for Rc<RefCell<Session>> {
    fn send(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        self.borrow_mut().send(conn, now)
    }

    fn send_data(&mut self, conn: &mut Connection, buf: &[u8], now: Instant) -> Res<usize> {
        self.borrow_mut().send_data(conn, buf, now)
    }

    fn has_data_to_send(&self) -> bool {
        self.borrow_mut().has_data_to_send()
    }

    fn stream_writable(&self) {
        self.borrow_mut().stream_writable();
    }

    fn done(&self) -> bool {
        self.borrow_mut().done()
    }

    fn close(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        self.borrow_mut().close_session(conn, 0, "", now)
    }

    fn close_with_message(
        &mut self,
        conn: &mut Connection,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()> {
        self.borrow_mut().close_session(conn, error, message, now)
    }

    fn handle_stop_sending(&mut self, close_type: CloseType) {
        self.borrow_mut().close(close_type);
    }
}

/// An extended connect protocol.
///
/// "Protocol" here corresponds to the `:protocol` pseudo header in the HTTP
/// Extended CONNECT method.
pub(crate) trait Protocol: Debug + Display {
    fn connect_type(&self) -> ExtendedConnectType;

    fn session_start(&mut self, _events: &mut Box<dyn ExtendedConnectEvents>) -> Res<()> {
        Ok(())
    }

    fn close_frame(&self, _error: u32, _message: &str) -> Option<Vec<u8>> {
        None
    }

    fn read_control_stream(
        &mut self,
        conn: &mut Connection,
        events: &mut Box<dyn ExtendedConnectEvents>,
        control_stream_recv: &mut Box<dyn RecvStream>,
        now: Instant,
    ) -> Res<Option<State>>;

    fn add_stream(
        &mut self,
        _stream_id: StreamId,
        _events: &mut Box<dyn ExtendedConnectEvents>,
        _state: State,
    ) -> Res<()> {
        let msg = "Protocol does not support adding streams";
        debug!("{msg}");
        debug_assert!(false, "{msg}");
        Ok(())
    }

    fn remove_recv_stream(&mut self, _stream_id: StreamId) {
        let msg = "Protocol does not support removing recv streams";
        debug!("{msg}");
        debug_assert!(false, "{msg}");
    }

    fn remove_send_stream(&mut self, _stream_id: StreamId) {
        let msg = "Protocol does not support removing send streams";
        debug!("{msg}");
        debug_assert!(false, "{msg}");
    }

    fn take_sub_streams(&mut self) -> (HashSet<StreamId>, HashSet<StreamId>) {
        (HashSet::default(), HashSet::default())
    }

    fn process_response_headers(&mut self, _headers: &[Header]) {}

    /// Per-session statistics, for protocols that expose them.
    ///
    /// Only `WebTransport` surfaces session statistics to the API consumer, so
    /// every other protocol leaves this at `None` and the counters above are
    /// simply not recorded.
    /// Callers reach this only after checking the session is `WebTransport`, so
    /// a protocol without stats never gets here.
    fn stats(&self) -> Option<&SessionStats> {
        debug_assert!(
            false,
            "stats called for extended connect protocol not tracking stats"
        );
        None
    }

    /// Record that `count` outgoing datagrams were sent.
    /// A no-op default for protocols that don't track [`SessionStats`].
    fn record_sent_outgoing_datagrams(&mut self, _count: u64) {}

    /// Record that `count` outgoing datagrams expired before being sent.
    /// A no-op default for protocols that don't track [`SessionStats`].
    fn record_expired_outgoing_datagrams(&mut self, _count: u64) {}

    /// Record that `count` outgoing datagrams were dropped unsent.
    /// A no-op default for protocols that don't track [`SessionStats`].
    fn record_dropped_outgoing_datagrams(&mut self, _count: u64) {}

    fn protocol(&self) -> Option<&str> {
        None
    }

    fn register_send_group(&mut self, _id: SendGroupId) -> Res<()> {
        Err(Error::InvalidStreamId)
    }

    fn validate_send_group(&self, _group_id: SendGroupId) -> bool {
        false
    }

    /// The varint this protocol puts on every HTTP Datagram between the
    /// quarter stream ID and the payload (connect-udp's context ID), if any.
    fn datagram_prefix(&self) -> Option<u64>;

    fn dgram_context_id(&self, datagram: Bytes) -> Result<Bytes, DgramContextIdError>;

    /// Whether the extended CONNECT protocol supports sending datagrams as HTTP
    /// DATAGRAM Capsules when QUIC datagrams are unavailable.
    fn datagram_capsule_support(&self) -> bool;

    /// Write a datagram as an HTTP DATAGRAM Capsule to the control stream.
    ///
    /// Capsules are buffered on the control stream, so their limit is that
    /// stream's flow-control window. A write that would exceed the window returns
    /// [`FlowControlLimit`] instead of dropping the datagram, and arms a writable
    /// notification so an [`OutgoingDatagramSpaceAvailable`] event fires once the
    /// stream can hold a capsule again; a successful write returns `Ok(())`.
    ///
    /// [`FlowControlLimit`]: crate::Error::FlowControlLimit
    /// [`OutgoingDatagramSpaceAvailable`]: crate::Http3ClientEvent::OutgoingDatagramSpaceAvailable
    fn write_datagram_capsule(
        &self,
        _control_stream_send: &mut Box<dyn SendStream>,
        _conn: &mut Connection,
        _buf: &[u8],
        _now: Instant,
    ) -> Res<()>;
}

#[derive(Debug, Error)]
pub(crate) enum DgramContextIdError {
    #[error("Missing context identifier")]
    MissingIdentifier,
    #[error("Unknown context identifier: {0}")]
    UnknownIdentifier(u64),
}
