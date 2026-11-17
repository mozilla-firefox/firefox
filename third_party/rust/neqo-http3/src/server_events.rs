// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Display, Formatter},
    ops::Deref,
    rc::Rc,
    time::Instant,
};

use neqo_common::{Header, event::Queue as EventQueue};
use neqo_transport::{AppError, Connection, StreamId, server::ConnectionRef};
use tracing::debug;

use crate::{
    Http3StreamInfo, Priority, Res, connection::Http3State, connection_server::Http3ServerHandler,
};

#[derive(Debug, Clone)]
pub struct StreamHandler {
    pub conn: ConnectionRef,
    pub handler: Rc<RefCell<Http3ServerHandler>>,
    pub stream_info: Http3StreamInfo,
}

impl Display for StreamHandler {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        let conn: &Connection = &self.conn.borrow();
        write!(f, "conn={conn} stream_info={:?}", self.stream_info)
    }
}

impl std::hash::Hash for StreamHandler {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.conn.hash(state);
        state.write_u64(self.stream_info.stream_id().as_u64());
    }
}

impl PartialEq for StreamHandler {
    fn eq(&self, other: &Self) -> bool {
        self.conn == other.conn && self.stream_info.stream_id() == other.stream_info.stream_id()
    }
}

impl StreamHandler {
    pub const fn stream_id(&self) -> StreamId {
        self.stream_info.stream_id()
    }

    /// Supply a response header to a request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn send_headers(&self, headers: &[Header]) -> Res<()> {
        self.handler.borrow_mut().send_headers(
            self.stream_id(),
            headers,
            &mut self.conn.borrow_mut(),
        )
    }

    /// Supply response data to a request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn send_data(&self, buf: &[u8], now: Instant) -> Res<usize> {
        self.handler
            .borrow_mut()
            .send_data(self.stream_id(), buf, &mut self.conn.borrow_mut(), now)
    }

    /// Bytes sendable on stream at the QUIC layer.
    ///
    /// Note that this does not yet account for HTTP3 frame headers.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn available(&self) -> Res<usize> {
        let stream_id = self.stream_id();
        let n = self.conn.borrow_mut().stream_avail_send_space(stream_id)?;
        Ok(n)
    }

    /// Close sending side.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn stream_close_send(&self, now: Instant) -> Res<()> {
        self.handler.borrow_mut().stream_close_send(
            self.stream_id(),
            &mut self.conn.borrow_mut(),
            now,
        )
    }

    /// Request a peer to stop sending a stream.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn stream_stop_sending(&self, app_error: AppError) -> Res<()> {
        debug!(
            "[{self}] stop sending stream_id:{} error:{app_error}",
            self.stream_info.stream_id()
        );
        self.handler.borrow_mut().stream_stop_sending(
            self.stream_info.stream_id(),
            app_error,
            &mut self.conn.borrow_mut(),
        )
    }

    /// Reset sending side of a stream.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn stream_reset_send(&self, app_error: AppError) -> Res<()> {
        debug!(
            "[{self}] reset send stream_id:{} error:{app_error}",
            self.stream_info.stream_id()
        );
        self.handler.borrow_mut().stream_reset_send(
            self.stream_info.stream_id(),
            app_error,
            &mut self.conn.borrow_mut(),
        )
    }

    /// Commit to reliably delivering the stream data buffered so far. If the stream is later
    /// reset, that prefix is delivered using `RESET_STREAM_AT` (when the peer supports it).
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore, or `NotAvailable`
    /// if the peer did not enable reliable reset.
    pub fn stream_commit(&self, now: Instant) -> Res<()> {
        self.handler.borrow_mut().stream_commit(
            self.stream_info.stream_id(),
            &mut self.conn.borrow_mut(),
            now,
        )
    }

    /// Reset a stream/request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore
    pub fn cancel_fetch(&self, app_error: AppError) -> Res<()> {
        debug!("[{self}] reset error:{app_error}");
        self.handler.borrow_mut().cancel_fetch(
            self.stream_info.stream_id(),
            app_error,
            &mut self.conn.borrow_mut(),
        )
    }
}

#[derive(Debug, Clone)]
pub struct Http3OrWebTransportStream {
    stream_handler: StreamHandler,
}

impl Display for Http3OrWebTransportStream {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Stream server {:?}", self.stream_handler)
    }
}

impl Http3OrWebTransportStream {
    pub(crate) const fn new(
        conn: ConnectionRef,
        handler: Rc<RefCell<Http3ServerHandler>>,
        stream_info: Http3StreamInfo,
    ) -> Self {
        Self {
            stream_handler: StreamHandler {
                conn,
                handler,
                stream_info,
            },
        }
    }

    /// Supply a response header to a request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn send_headers(&self, headers: &[Header]) -> Res<()> {
        self.stream_handler.send_headers(headers)
    }

    /// Supply response data to a request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn send_data(&self, data: &[u8], now: Instant) -> Res<usize> {
        debug!("[{self}] Set new response");
        self.stream_handler.send_data(data, now)
    }

    /// Close sending side.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn stream_close_send(&self, now: Instant) -> Res<()> {
        debug!("[{self}] Set new response");
        self.stream_handler.stream_close_send(now)
    }
}

impl Deref for Http3OrWebTransportStream {
    type Target = StreamHandler;
    fn deref(&self) -> &Self::Target {
        &self.stream_handler
    }
}

impl std::hash::Hash for Http3OrWebTransportStream {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.stream_handler.hash(state);
    }
}

impl PartialEq for Http3OrWebTransportStream {
    fn eq(&self, other: &Self) -> bool {
        self.stream_handler == other.stream_handler
    }
}

impl Eq for Http3OrWebTransportStream {}

/// Server events for one or more connections.
#[derive(Debug, Clone)]
pub enum Http3ServerEvent {
    /// Headers are ready.
    Headers {
        stream: Http3OrWebTransportStream,
        headers: Vec<Header>,
        fin: bool,
    },
    /// Request data is ready.
    Data {
        stream: Http3OrWebTransportStream,
        data: Vec<u8>,
        fin: bool,
    },
    DataWritable {
        stream: Http3OrWebTransportStream,
    },
    StreamReset {
        stream: Http3OrWebTransportStream,
        error: AppError,
    },
    StreamStopSending {
        stream: Http3OrWebTransportStream,
        error: AppError,
    },
    /// When individual connection change state. It is only used for tests.
    StateChange {
        conn: ConnectionRef,
        state: Http3State,
    },
    PriorityUpdate {
        stream_id: StreamId,
        priority: Priority,
    },
    /// The outgoing QUIC datagram queue of session `session_id` has space again after having been
    /// full.
    OutgoingDatagramSpaceAvailable {
        conn: ConnectionRef,
        session_id: StreamId,
    },
    WebTransport(crate::webtransport::ServerEvent),
    ConnectUdp(crate::connect_udp::ServerEvent),
}

#[derive(Debug, Default, Clone)]
pub struct Http3ServerEvents {
    events: EventQueue<Http3ServerEvent>,
}

impl Http3ServerEvents {
    /// Append an event, allowing duplicates.
    pub(crate) fn push(&self, event: Http3ServerEvent) {
        self.events.push(event);
    }

    /// Take all events
    pub fn events(&self) -> impl Iterator<Item = Http3ServerEvent> + use<> {
        self.events.take_all().into_iter()
    }

    /// Whether there is request pending.
    pub fn has_events(&self) -> bool {
        !self.events.is_empty()
    }

    /// Take the next event if present.
    pub fn next_event(&self) -> Option<Http3ServerEvent> {
        self.events.next_event()
    }

    /// Insert a `Headers` event.
    pub(crate) fn headers(
        &self,
        request: Http3OrWebTransportStream,
        headers: Vec<Header>,
        fin: bool,
    ) {
        self.events.push(Http3ServerEvent::Headers {
            stream: request,
            headers,
            fin,
        });
    }

    /// Insert a `StateChange` event.
    pub(crate) fn connection_state_change(&self, conn: ConnectionRef, state: Http3State) {
        self.events
            .push(Http3ServerEvent::StateChange { conn, state });
    }

    /// Insert an `OutgoingDatagramSpaceAvailable` event.
    pub(crate) fn datagram_space_available(&self, conn: ConnectionRef, session_id: StreamId) {
        self.events
            .push(Http3ServerEvent::OutgoingDatagramSpaceAvailable { conn, session_id });
    }

    /// Insert a `Data` event.
    pub(crate) fn data(
        &self,
        conn: ConnectionRef,
        handler: Rc<RefCell<Http3ServerHandler>>,
        stream_info: Http3StreamInfo,
        data: Vec<u8>,
        fin: bool,
    ) {
        self.events.push(Http3ServerEvent::Data {
            stream: Http3OrWebTransportStream::new(conn, handler, stream_info),
            data,
            fin,
        });
    }

    pub(crate) fn data_writable(
        &self,
        conn: ConnectionRef,
        handler: Rc<RefCell<Http3ServerHandler>>,
        stream_info: Http3StreamInfo,
    ) {
        self.events.push(Http3ServerEvent::DataWritable {
            stream: Http3OrWebTransportStream::new(conn, handler, stream_info),
        });
    }

    pub(crate) fn stream_reset(
        &self,
        conn: ConnectionRef,
        handler: Rc<RefCell<Http3ServerHandler>>,
        stream_info: Http3StreamInfo,
        error: AppError,
    ) {
        self.events.push(Http3ServerEvent::StreamReset {
            stream: Http3OrWebTransportStream::new(conn, handler, stream_info),
            error,
        });
    }

    pub(crate) fn stream_stop_sending(
        &self,
        conn: ConnectionRef,
        handler: Rc<RefCell<Http3ServerHandler>>,
        stream_info: Http3StreamInfo,
        error: AppError,
    ) {
        self.events.push(Http3ServerEvent::StreamStopSending {
            stream: Http3OrWebTransportStream::new(conn, handler, stream_info),
            error,
        });
    }

    pub(crate) fn priority_update(&self, stream_id: StreamId, priority: Priority) {
        self.events.push(Http3ServerEvent::PriorityUpdate {
            stream_id,
            priority,
        });
    }
}
