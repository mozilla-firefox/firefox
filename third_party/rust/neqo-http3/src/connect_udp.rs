// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Display, Formatter},
    num::NonZeroUsize,
    rc::Rc,
    time::Instant,
};

use neqo_common::{Bytes, Header};
use neqo_transport::{
    Connection, DatagramQueueOutcome, DatagramTracking, StreamId, server::ConnectionRef,
    streams::SendGroupId,
};
use tracing::{debug, info, trace};

use crate::{
    Error, Http3Client, Http3ServerEvent, Http3State, Http3StreamInfo, Http3StreamType, Res,
    SessionAcceptAction,
    connection::Http3Connection,
    connection_server::Http3ServerHandler,
    features::extended_connect,
    request_target::RequestTarget,
    server_events::{Http3ServerEvents, StreamHandler},
};

/// Connect-udp has no API of its own to set an outgoing-datagram max-buffered
/// limit (unlike `WebTransport`'s `outgoingMaxBufferedDatagrams`), so every
/// connect-udp session, created or accepted, gets this one.  It matches the
/// depth the legacy connection-wide datagram queue enforced for everyone
/// before per-session queues replaced it, so connect-udp keeps the
/// backpressure signal PR #3859 added.
const MAX_BUFFERED_DATAGRAMS: NonZeroUsize = NonZeroUsize::new(10).expect("nonzero");

pub trait ClientSession {
    /// Whether `CONNECT_UDP` is enabled on the connection.
    #[must_use]
    fn connect_udp_enabled(&self) -> bool;

    /// Create a MASQUE connect-udp session.
    ///
    /// # Errors
    ///
    /// If MASQUE connect-udp session cannot be created, e.g. the HTTP CONNECT
    /// setting is not negotiated or the HTTP/3 connection is closed.
    fn connect_udp_create_session<T: RequestTarget>(
        &mut self,
        now: Instant,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId>;

    /// Close a connect-udp session cleanly.
    ///
    /// # Errors
    ///
    /// [`Error::InvalidStreamId`] if the stream
    /// does not exist,
    /// [`Error::TransportStreamDoesNotExist`] if the
    /// transport stream does not exist (this may happen if [`Http3Client::process_output`] has
    /// not been called when needed, and HTTP3 layer has not picked up the info that the stream
    /// has been closed.)
    fn connect_udp_close_session(
        &mut self,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()>;

    /// Send a connect-udp datagram.
    ///
    /// # Returns
    ///
    /// The queue's backpressure signal (see
    /// [`DatagramQueueOutcome`]); the
    /// sender should wait for an [`OutgoingDatagramSpaceAvailable`] event
    /// before sending more once the outcome is no longer
    /// [`DatagramQueueOutcome::Ok`].
    ///
    /// # Errors
    ///
    /// It may return [`Error::InvalidStreamId`] if a stream does not exist anymore.
    /// The function returns `TooMuchData` if the supply buffer is bigger than
    /// the allowed remote datagram size.
    ///
    /// [`OutgoingDatagramSpaceAvailable`]: crate::Http3ClientEvent::OutgoingDatagramSpaceAvailable
    fn connect_udp_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome>;
}

impl ClientSession for Http3Client {
    fn connect_udp_enabled(&self) -> bool {
        self.handler().connect_udp_enabled()
    }

    fn connect_udp_create_session<T: RequestTarget>(
        &mut self,
        now: Instant,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId> {
        let events = Box::new(self.client_events().clone());
        let output = {
            let (conn, handler) = self.connection_and_handler();
            handler.connect_udp_create_session(conn, events, target, headers)
        };

        if let Err(e) = &output
            && e.connection_error()
        {
            self.close(now, e.code(), "");
        }
        output
    }

    fn connect_udp_close_session(
        &mut self,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        handler.connect_udp_close_session(conn, session_id, error, message, now)
    }

    fn connect_udp_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome> {
        trace!("connect_udp_send_datagram session:{session_id:?}");
        let (conn, handler) = self.connection_and_handler();
        handler.connect_udp_send_datagram(conn, session_id, buf, id, now)
    }
}

/// Connection-level connect-udp operations shared by the client and server.
trait Handler {
    fn connect_udp_create_session<T: RequestTarget>(
        &mut self,
        conn: &mut Connection,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId>;

    fn connect_udp_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        accept_res: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()>;

    fn connect_udp_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()>;

    fn connect_udp_send_datagram<I: Into<DatagramTracking>>(
        &self,
        conn: &mut Connection,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome>;
}

impl Handler for Http3Connection {
    fn connect_udp_create_session<T: RequestTarget>(
        &mut self,
        conn: &mut Connection,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId> {
        info!("[{self}] Create ConnectUdp");
        if !self.connect_udp_enabled() {
            return Err(Error::Unavailable);
        }
        let id = self.extended_connect_create_session(
            conn,
            events,
            target,
            headers,
            extended_connect::ExtendedConnectType::ConnectUdp,
        )?;
        conn.set_max_buffered_datagrams(id, Some(MAX_BUFFERED_DATAGRAMS));
        Ok(id)
    }

    fn connect_udp_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        accept_res: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()> {
        trace!("Respond to ConnectUdp session with accept={accept_res}");
        if !self.connect_udp_enabled() {
            return Err(Error::Unavailable);
        }
        self.extended_connect_session_accept(
            conn,
            stream_id,
            events,
            accept_res,
            extended_connect::ExtendedConnectType::ConnectUdp,
            now,
        )?;
        if !matches!(accept_res, SessionAcceptAction::Reject(_)) {
            conn.set_max_buffered_datagrams(stream_id, Some(MAX_BUFFERED_DATAGRAMS));
        }
        Ok(())
    }

    fn connect_udp_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()> {
        trace!("Close ConnectUdp session {session_id:?}");
        self.extended_connect_close_session(
            conn,
            session_id,
            extended_connect::ExtendedConnectType::ConnectUdp,
            error,
            message,
            now,
        )?;
        Ok(())
    }

    fn connect_udp_send_datagram<I: Into<DatagramTracking>>(
        &self,
        conn: &mut Connection,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome> {
        // connect-udp has no sendGroup/sendOrder concept of its own: always
        // ungrouped, default priority.
        self.extended_connect_send_datagram(session_id, conn, buf, id, now, SendGroupId::new(0), 0)
    }
}

/// Server-handler connect-udp operations, exposed on [`Http3ServerHandler`].
pub(crate) trait ServerHandler {
    fn connect_udp_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        accept: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()>;

    fn connect_udp_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()>;

    fn connect_udp_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome>;
}

impl ServerHandler for Http3ServerHandler {
    fn connect_udp_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        accept: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()> {
        self.mark_needs_processing();
        let events = Box::new(self.server_events().clone());
        self.base_handler_mut()
            .connect_udp_session_accept(conn, stream_id, events, accept, now)
    }

    fn connect_udp_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<()> {
        self.mark_needs_processing();
        self.base_handler_mut()
            .connect_udp_close_session(conn, session_id, error, message, now)
    }

    fn connect_udp_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome> {
        self.mark_needs_processing();
        self.base_handler_mut()
            .connect_udp_send_datagram(conn, session_id, buf, id, now)
    }
}

#[derive(Debug, Clone)]
pub struct ServerSession {
    stream_handler: StreamHandler,
}

impl Display for ServerSession {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "ConnectUdp session {}", self.stream_handler)
    }
}

impl ServerSession {
    pub(crate) const fn new(
        conn: ConnectionRef,
        handler: Rc<RefCell<Http3ServerHandler>>,
        stream_id: StreamId,
    ) -> Self {
        Self {
            stream_handler: StreamHandler {
                conn,
                handler,
                stream_info: Http3StreamInfo::new(stream_id, Http3StreamType::Http),
            },
        }
    }

    #[must_use]
    pub fn state(&self) -> Http3State {
        self.stream_handler.handler.borrow().state()
    }

    /// Respond to a `ConnectUdp` session request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn response(&self, accept: &SessionAcceptAction, now: Instant) -> Res<()> {
        debug!("[{self}] Set a response for a ConnectUdp session");
        self.stream_handler
            .handler
            .borrow_mut()
            .connect_udp_session_accept(
                &mut self.stream_handler.conn.borrow_mut(),
                self.stream_handler.stream_info.stream_id(),
                accept,
                now,
            )
    }

    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    /// Also return an error if the stream was closed on the transport layer,
    /// but that information is not yet consumed on the http/3 layer.
    pub fn close_session(&self, error: u32, message: &str, now: Instant) -> Res<()> {
        self.stream_handler
            .handler
            .borrow_mut()
            .connect_udp_close_session(
                &mut self.stream_handler.conn.borrow_mut(),
                self.stream_handler.stream_info.stream_id(),
                error,
                message,
                now,
            )
    }

    #[must_use]
    pub const fn stream_id(&self) -> StreamId {
        self.stream_handler.stream_id()
    }

    /// Send connect-udp datagram.
    ///
    /// # Returns
    ///
    /// The queue's backpressure signal (see [`DatagramQueueOutcome`]); the
    /// sender should wait for an [`OutgoingDatagramSpaceAvailable`] event
    /// before sending more once the outcome is no longer
    /// [`DatagramQueueOutcome::Ok`].
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    /// The function returns `TooMuchData` if the supply buffer is bigger than
    /// the allowed remote datagram size.
    ///
    /// [`OutgoingDatagramSpaceAvailable`]: crate::Http3ServerEvent::OutgoingDatagramSpaceAvailable
    pub fn send_datagram<I: Into<DatagramTracking>>(
        &self,
        buf: &[u8],
        id: I,
        now: Instant,
    ) -> Res<DatagramQueueOutcome> {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow_mut()
            .connect_udp_send_datagram(
                &mut self.stream_handler.conn.borrow_mut(),
                session_id,
                buf,
                id,
                now,
            )
    }

    #[must_use]
    pub fn remote_datagram_size(&self) -> u64 {
        self.stream_handler.conn.borrow().remote_datagram_size()
    }

    /// Used for testing only.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    #[doc(hidden)]
    pub fn reset_send(&self) -> Res<()> {
        self.stream_handler.handler.borrow_mut().stream_reset_send(
            self.stream_id(),
            Error::HttpNone.code(),
            &mut self.stream_handler.conn.borrow_mut(),
        )
    }
}

#[derive(Debug, Clone)]
pub enum ServerEvent {
    NewSession {
        session: ServerSession,
        headers: Vec<Header>,
    },
    SessionClosed {
        session: ServerSession,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    },
    Datagram {
        session: ServerSession,
        datagram: Bytes,
    },
}

pub(crate) trait ServerEvents {
    fn connect_udp_new_session(&self, session: ServerSession, headers: Vec<Header>);
    fn connect_udp_session_closed(
        &self,
        session: ServerSession,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    );
    fn connect_udp_datagram(&self, session: ServerSession, datagram: Bytes);
}

impl ServerEvents for Http3ServerEvents {
    fn connect_udp_new_session(&self, session: ServerSession, headers: Vec<Header>) {
        self.push(Http3ServerEvent::ConnectUdp(ServerEvent::NewSession {
            session,
            headers,
        }));
    }

    fn connect_udp_session_closed(
        &self,
        session: ServerSession,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    ) {
        self.push(Http3ServerEvent::ConnectUdp(ServerEvent::SessionClosed {
            session,
            reason,
            headers,
        }));
    }

    fn connect_udp_datagram(&self, session: ServerSession, datagram: Bytes) {
        self.push(Http3ServerEvent::ConnectUdp(ServerEvent::Datagram {
            session,
            datagram,
        }));
    }
}
