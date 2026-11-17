// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Display, Formatter},
    num::NonZeroUsize,
    ops::Deref,
    rc::Rc,
    time::{Duration, Instant},
};

use neqo_common::{Bytes, Encoder, Header, to_u64};
use neqo_transport::{
    Connection, DatagramQueueCapacity, DatagramQueueOutcome, DatagramTracking, StreamId,
    StreamType, recv_stream, send_stream, server::ConnectionRef, streams::SendOrder,
};
use tracing::{debug, info, trace};

use crate::{
    Error, Http3Client, Http3OrWebTransportStream, Http3ServerEvent, Http3State, Http3StreamInfo,
    Http3StreamType, Res, SendGroupId, SessionAcceptAction,
    connection::Http3Connection,
    connection_server::Http3ServerHandler,
    features::extended_connect,
    request_target::RequestTarget,
    server_events::{Http3ServerEvents, StreamHandler},
};

pub trait ClientSession {
    /// Whether WebTransport has been enabled at the connection level.
    #[must_use]
    fn webtransport_enabled(&self) -> bool;

    /// Get the negotiated subprotocol for a WebTransport session.
    ///
    /// Returns the parsed protocol string from the server's `wt-protocol` response header
    /// (an [RFC 8941 Item](https://www.rfc-editor.org/rfc/rfc8941.html#name-items)),
    /// or `None` if the server did not include a `wt-protocol` header (or its value was
    /// not a valid sf-string).
    ///
    /// **Note:** this returns the server's selected protocol without validating it against the
    /// list of protocols offered by the client.  Callers are responsible for checking that the
    /// returned protocol was among those originally offered.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid.
    fn webtransport_session_protocol(&self, session_id: StreamId) -> Res<Option<String>>;

    /// Returns the current max size of a datagram that can fit into a packet.
    /// The value will change over time depending on the encoded size of the
    /// packet number, ack frames, etc.
    ///
    /// # Errors
    ///
    /// The function returns `NotAvailable` if datagrams are not enabled.
    ///
    /// # Panics
    ///
    /// This cannot panic. The max varint length is 8.
    fn webtransport_max_datagram_size(&self, session_id: StreamId) -> Res<u64>;

    /// Set `outgoingMaxBufferedDatagrams`, or clear it with `None` (also the
    /// initial state; the embedder supplies the spec's initial value).  Accepted
    /// before `ready`, since the attribute can be set then; ignored once the
    /// session is closing.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    fn webtransport_set_max_buffered_datagrams(
        &mut self,
        session_id: StreamId,
        max_buffered_datagrams: Option<NonZeroUsize>,
    ) -> Res<()>;

    /// A snapshot of this session's outgoing-datagram queue; see
    /// [`DatagramQueueCapacity`].  The `outgoingMaxBufferedDatagrams` limit is not included.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    fn webtransport_datagram_queue_capacity(
        &self,
        session_id: StreamId,
    ) -> Res<DatagramQueueCapacity>;

    /// Set the number of concurrent incoming unidirectional streams a WebTransport session
    /// anticipates (`anticipatedConcurrentIncomingUnidirectionalStreams`).
    ///
    /// This raises a connection-wide stream limit computed as the sum of the anticipated values
    /// set for all not-yet-closed WebTransport sessions on this connection, capped at
    /// [`crate::MAX_ANTICIPATED_INCOMING_STREAMS`] streams per direction; it does not create a
    /// per-session limit.  Closed sessions are excluded so a stale one cannot keep inflating the
    /// total; sessions still negotiating are included, since the W3C API supplies this value at
    /// construction.  A `value` that would push the sum past the cap is accepted but only takes
    /// effect up to it. The transport limit only ever rises, so lowering `value` does not reduce
    /// it. The limit is a concurrency window, not a cumulative budget: as the peer closes streams
    /// it opened, it may open further streams without another call here.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid, is not a WebTransport session, or is closing.
    fn webtransport_set_anticipated_incoming_uni_streams(
        &mut self,
        session_id: StreamId,
        value: u16,
    ) -> Res<()>;

    /// Bidirectional counterpart to
    /// [`Self::webtransport_set_anticipated_incoming_uni_streams`]
    /// (`anticipatedConcurrentIncomingBidirectionalStreams`).
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid, is not a WebTransport session, or is closing.
    fn webtransport_set_anticipated_incoming_bidi_streams(
        &mut self,
        session_id: StreamId,
        value: u16,
    ) -> Res<()>;

    /// Set the outgoing-datagram queue's `outgoingMaxAge`, or clear it back
    /// to the implementation-defined default with `None`.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    fn webtransport_set_datagram_max_age(
        &mut self,
        session_id: StreamId,
        max_age: Option<Duration>,
        now: Instant,
    ) -> Res<()>;

    /// Sets the `SendOrder` for a given stream
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    fn webtransport_set_sendorder(
        &mut self,
        stream_id: StreamId,
        sendorder: Option<SendOrder>,
    ) -> Res<()>;

    /// Sets the [`SendGroupId`] for a given WebTransport stream.
    ///
    /// # Errors
    ///
    /// It may return [`Error::InvalidStreamId`] if a stream does not exist anymore,
    /// or [`Error::Unavailable`] if the stream is not a WebTransport send stream.
    fn webtransport_set_sendgroup(
        &mut self,
        stream_id: StreamId,
        sendgroup: SendGroupId,
    ) -> Res<()>;

    /// Clears the [`SendGroupId`] for a given WebTransport stream.
    ///
    /// # Errors
    ///
    /// It may return [`Error::InvalidStreamId`] if a stream does not exist anymore,
    /// or [`Error::Unavailable`] if the stream is not a WebTransport send stream.
    fn webtransport_clear_sendgroup(&mut self, stream_id: StreamId) -> Res<()>;

    /// Sets the `Fairness` for a given stream
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    //
    // TODO: Currently not called in neqo or gecko. It should likely be called at least from gecko.
    fn webtransport_set_fairness(&mut self, stream_id: StreamId, fairness: bool) -> Res<()>;

    /// Returns the current `send_stream::Stats` of a `WebTransportSendStream`.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist.
    fn webtransport_send_stream_stats(&mut self, stream_id: StreamId) -> Res<send_stream::Stats>;

    /// Returns the current `recv_stream::Stats` of a `WebTransportRecvStream`.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist.
    fn webtransport_recv_stream_stats(&mut self, stream_id: StreamId) -> Res<recv_stream::Stats>;

    /// Export WebTransport keying material per
    /// [draft-ietf-webtrans-http3 §4.8](https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-15.html#section-4.8).
    ///
    /// Derives keying material scoped to a specific WebTransport session
    /// by calling the TLS exporter with label `"EXPORTER-WebTransport"`
    /// and a context struct that binds the session ID, application label,
    /// and application context together.
    ///
    /// # Errors
    ///
    /// Returns `Error::InvalidStreamId` if `session_id` does not
    /// correspond to an active WebTransport session,
    /// `Error::InvalidInput` if `out` is empty or `label`/`context`
    /// exceed 255 bytes, or `Error::Transport` on TLS export failure.
    fn webtransport_export_keying_material(
        &self,
        session_id: StreamId,
        label: &[u8],
        context: &[u8],
        out: &mut [u8],
    ) -> Res<()>;

    /// Create a `WebTransport` session.
    ///
    /// # Errors
    ///
    /// If `WebTransport` cannot be created, e.g. the `WebTransport` support is
    /// not negotiated or the HTTP/3 connection is closed.
    fn webtransport_create_session<T: RequestTarget>(
        &mut self,
        now: Instant,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId>;

    /// Close a `WebTransport` session cleanly.
    ///
    /// Returns a snapshot of the session's statistics taken at close time.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist,
    /// `TransportStreamDoesNotExist` if the transport stream does not exist (this may happen if
    /// `process_output` has not been called when needed, and HTTP3 layer has not picked up the
    /// info that the stream has been closed.) `InvalidInput` if an empty buffer has been
    /// supplied.
    fn webtransport_close_session(
        &mut self,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats>;

    /// Create a `WebTransport` stream.
    ///
    /// # Errors
    ///
    /// This may return an error if the particular session does not exist
    /// or the connection is not in the active state.
    fn webtransport_create_stream(
        &mut self,
        session_id: StreamId,
        stream_type: StreamType,
    ) -> Res<StreamId>;

    /// Send a `WebTransport` datagram.
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
    /// [`OutgoingDatagramSpaceAvailable`]: crate::Http3ClientEvent::OutgoingDatagramSpaceAvailable
    fn webtransport_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome>;
}

impl ClientSession for Http3Client {
    fn webtransport_enabled(&self) -> bool {
        self.handler().webtransport_enabled()
    }

    fn webtransport_session_protocol(&self, session_id: StreamId) -> Res<Option<String>> {
        self.handler().webtransport_session_protocol(session_id)
    }

    fn webtransport_max_datagram_size(&self, session_id: StreamId) -> Res<u64> {
        let qsid_len = Encoder::varint_len(session_id.as_u64() >> 2);
        Ok(self
            .connection()
            .max_datagram_size()?
            .saturating_sub(to_u64(qsid_len)))
    }

    fn webtransport_set_max_buffered_datagrams(
        &mut self,
        session_id: StreamId,
        max_buffered_datagrams: Option<NonZeroUsize>,
    ) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_session_set_max_buffered_datagrams(
            conn,
            session_id,
            max_buffered_datagrams,
        )
    }

    fn webtransport_datagram_queue_capacity(
        &self,
        session_id: StreamId,
    ) -> Res<DatagramQueueCapacity> {
        self.handler()
            .webtransport_datagram_queue_capacity(self.connection(), session_id)
    }

    fn webtransport_set_anticipated_incoming_uni_streams(
        &mut self,
        session_id: StreamId,
        value: u16,
    ) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_set_anticipated_incoming_streams(
            conn,
            session_id,
            StreamType::UniDi,
            value,
        )
    }

    fn webtransport_set_anticipated_incoming_bidi_streams(
        &mut self,
        session_id: StreamId,
        value: u16,
    ) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_set_anticipated_incoming_streams(
            conn,
            session_id,
            StreamType::BiDi,
            value,
        )
    }

    fn webtransport_set_datagram_max_age(
        &mut self,
        session_id: StreamId,
        max_age: Option<Duration>,
        now: Instant,
    ) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_session_set_datagram_max_age(conn, session_id, max_age, now)
    }

    fn webtransport_set_sendorder(
        &mut self,
        stream_id: StreamId,
        sendorder: Option<SendOrder>,
    ) -> Res<()> {
        Http3Connection::stream_set_sendorder(self.connection_mut(), stream_id, sendorder)
    }

    fn webtransport_set_sendgroup(
        &mut self,
        stream_id: StreamId,
        sendgroup: SendGroupId,
    ) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        // Update the HTTP3 layer first: it owns the group-registration check, so a
        // rejection here must not leave the transport scheduler already mutated (that
        // would persistently diverge the layers — sendOrder would route through the
        // grouped transport path the HTTP3 layer rejected). If the subsequent
        // transport update fails (e.g. the stream was closed), that is self-healing:
        // stream teardown clears both layers.
        handler.stream_set_sendgroup(stream_id, sendgroup)?;
        conn.stream_sendgroup(stream_id, Some(sendgroup))
            .map_err(|_| Error::InvalidStreamId)
    }

    fn webtransport_clear_sendgroup(&mut self, stream_id: StreamId) -> Res<()> {
        let (conn, handler) = self.connection_and_handler();
        // See comment in webtransport_set_sendgroup for ordering rationale.
        handler.stream_clear_sendgroup(stream_id)?;
        conn.stream_sendgroup(stream_id, None)
            .map_err(|_| Error::InvalidStreamId)
    }

    fn webtransport_set_fairness(&mut self, stream_id: StreamId, fairness: bool) -> Res<()> {
        Http3Connection::stream_set_fairness(self.connection_mut(), stream_id, fairness)
    }

    fn webtransport_send_stream_stats(&mut self, stream_id: StreamId) -> Res<send_stream::Stats> {
        let (conn, handler) = self.connection_and_handler();
        handler
            .send_streams_mut()
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .stats(conn)
    }

    fn webtransport_recv_stream_stats(&mut self, stream_id: StreamId) -> Res<recv_stream::Stats> {
        let (conn, handler) = self.connection_and_handler();
        handler
            .recv_streams_mut()
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .stats(conn)
    }

    fn webtransport_export_keying_material(
        &self,
        session_id: StreamId,
        label: &[u8],
        context: &[u8],
        out: &mut [u8],
    ) -> Res<()> {
        self.handler()
            .validate_extended_connect_session(session_id)?;
        self.connection()
            .webtransport_export_keying_material(session_id, label, context, out)
    }

    fn webtransport_create_session<T: RequestTarget>(
        &mut self,
        now: Instant,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId> {
        let events = Box::new(self.client_events().clone());
        let output = {
            let (conn, handler) = self.connection_and_handler();
            handler.webtransport_create_session(conn, events, target, headers)
        };

        if let Err(e) = &output
            && e.connection_error()
        {
            self.close(now, e.code(), "");
        }
        output
    }

    fn webtransport_close_session(
        &mut self,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats> {
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_close_session(conn, session_id, error, message, now)
    }

    fn webtransport_create_stream(
        &mut self,
        session_id: StreamId,
        stream_type: StreamType,
    ) -> Res<StreamId> {
        let send_events = Box::new(self.client_events().clone());
        let recv_events = Box::new(self.client_events().clone());
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_create_stream_local(
            conn,
            session_id,
            stream_type,
            send_events,
            recv_events,
        )
    }

    fn webtransport_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        trace!("webtransport_send_datagram session:{session_id:?}");
        let (conn, handler) = self.connection_and_handler();
        handler.webtransport_send_datagram(
            session_id,
            conn,
            buf,
            id,
            now,
            send_group_id,
            send_order,
        )
    }
}

trait ExportKeyingMaterial {
    /// Export keying material for WebTransport.
    ///
    /// # Errors
    /// When the input is invalid or the underlying connection fails to export.
    fn webtransport_export_keying_material(
        &self,
        session_id: StreamId,
        label: &[u8],
        context: &[u8],
        out: &mut [u8],
    ) -> Res<()>;
}

impl ExportKeyingMaterial for Connection {
    fn webtransport_export_keying_material(
        &self,
        session_id: StreamId,
        label: &[u8],
        context: &[u8],
        out: &mut [u8],
    ) -> Res<()> {
        // encode_vec(1, …) uses a 1-byte length prefix, so max 255 bytes.
        if out.is_empty() || label.len() > 255 || context.len() > 255 {
            return Err(Error::InvalidInput);
        }

        let mut wt_context = Encoder::with_capacity(8 + 1 + label.len() + 1 + context.len());
        wt_context.encode_uint(8, session_id.as_u64());
        wt_context.encode_vec(1, label);
        wt_context.encode_vec(1, context);

        Ok(self.export_keying_material("EXPORTER-WebTransport", wt_context.as_ref(), out)?)
    }
}

/// Connection-level `WebTransport` operations shared by the client and server.
trait Handler {
    fn webtransport_create_session<T: RequestTarget>(
        &mut self,
        conn: &mut Connection,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId>;

    fn webtransport_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        accept_res: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()>;

    fn webtransport_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats>;

    #[expect(
        clippy::too_many_arguments,
        reason = "mirrors ClientSession::webtransport_send_datagram plus session_id/conn"
    )]
    fn webtransport_send_datagram<I: Into<DatagramTracking>>(
        &self,
        session_id: StreamId,
        conn: &mut Connection,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome>;
}

impl Handler for Http3Connection {
    fn webtransport_create_session<T: RequestTarget>(
        &mut self,
        conn: &mut Connection,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        target: &T,
        headers: &[Header],
    ) -> Res<StreamId> {
        info!("[{self}] Create WebTransport");
        if !self.webtransport_enabled() {
            return Err(Error::Unavailable);
        }
        self.extended_connect_create_session(
            conn,
            events,
            target,
            headers,
            extended_connect::ExtendedConnectType::WebTransport,
        )
    }

    fn webtransport_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        events: Box<dyn extended_connect::ExtendedConnectEvents>,
        accept_res: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()> {
        trace!("Respond to WebTransport session with accept={accept_res}");
        if !self.webtransport_enabled() {
            return Err(Error::Unavailable);
        }
        self.extended_connect_session_accept(
            conn,
            stream_id,
            events,
            accept_res,
            extended_connect::ExtendedConnectType::WebTransport,
            now,
        )
    }

    fn webtransport_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats> {
        trace!("Close WebTransport session {session_id:?}");
        // Read the stats after closing, which counts the queue's expired and dropped datagrams.
        let session = self.extended_connect_close_session(
            conn,
            session_id,
            extended_connect::ExtendedConnectType::WebTransport,
            error,
            message,
            now,
        )?;
        let stats = session.borrow().stats();
        stats.ok_or(Error::InvalidStreamId)
    }

    fn webtransport_send_datagram<I: Into<DatagramTracking>>(
        &self,
        session_id: StreamId,
        conn: &mut Connection,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        self.extended_connect_send_datagram(
            session_id,
            conn,
            buf,
            id,
            now,
            send_group_id,
            send_order,
        )
    }
}

/// Server-handler `WebTransport` operations, exposed on [`Http3ServerHandler`].
pub(crate) trait ServerHandler {
    fn webtransport_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        accept: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()>;

    fn webtransport_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats>;

    fn webtransport_create_stream(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        stream_type: StreamType,
    ) -> Res<StreamId>;

    #[expect(
        clippy::too_many_arguments,
        reason = "mirrors ClientSession::webtransport_send_datagram plus session_id/conn"
    )]
    fn webtransport_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome>;

    /// See [`ClientSession::webtransport_set_max_buffered_datagrams`].
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    fn webtransport_set_max_buffered_datagrams(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        max_buffered_datagrams: Option<NonZeroUsize>,
    ) -> Res<()>;

    /// See [`ClientSession::webtransport_set_datagram_max_age`].
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    fn webtransport_set_datagram_max_age(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        max_age: Option<Duration>,
        now: Instant,
    ) -> Res<()>;
}

impl ServerHandler for Http3ServerHandler {
    fn webtransport_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        accept: &SessionAcceptAction,
        now: Instant,
    ) -> Res<()> {
        self.mark_needs_processing();
        let events = Box::new(self.server_events().clone());
        self.base_handler_mut()
            .webtransport_session_accept(conn, stream_id, events, accept, now)
    }

    fn webtransport_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats> {
        self.mark_needs_processing();
        self.base_handler_mut()
            .webtransport_close_session(conn, session_id, error, message, now)
    }

    fn webtransport_create_stream(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        stream_type: StreamType,
    ) -> Res<StreamId> {
        self.mark_needs_processing();
        let send_events = Box::new(self.server_events().clone());
        let recv_events = Box::new(self.server_events().clone());
        self.base_handler_mut().webtransport_create_stream_local(
            conn,
            session_id,
            stream_type,
            send_events,
            recv_events,
        )
    }

    fn webtransport_send_datagram<I: Into<DatagramTracking>>(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        self.mark_needs_processing();
        self.base_handler_mut().webtransport_send_datagram(
            session_id,
            conn,
            buf,
            id,
            now,
            send_group_id,
            send_order,
        )
    }

    fn webtransport_set_max_buffered_datagrams(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        max_buffered_datagrams: Option<NonZeroUsize>,
    ) -> Res<()> {
        self.base_handler_mut()
            .webtransport_session_set_max_buffered_datagrams(
                conn,
                session_id,
                max_buffered_datagrams,
            )
    }

    fn webtransport_set_datagram_max_age(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        max_age: Option<Duration>,
        now: Instant,
    ) -> Res<()> {
        self.base_handler_mut()
            .webtransport_session_set_datagram_max_age(conn, session_id, max_age, now)
    }
}

#[derive(Debug, Clone)]
pub struct ServerSession {
    stream_handler: StreamHandler,
}

impl Display for ServerSession {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "WebTransport session {}", self.stream_handler)
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

    /// Respond to a `WebTransport` session request.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn response(&self, accept: &SessionAcceptAction, now: Instant) -> Res<()> {
        debug!("[{self}] Set a response for a WebTransport session");
        self.stream_handler
            .handler
            .borrow_mut()
            .webtransport_session_accept(
                &mut self.stream_handler.conn.borrow_mut(),
                self.stream_handler.stream_info.stream_id(),
                accept,
                now,
            )
    }

    /// Returns a snapshot of the session's statistics taken at close time.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    /// Also return an error if the stream was closed on the transport layer,
    /// but that information is not yet consumed on the http/3 layer.
    pub fn close_session(
        &self,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<extended_connect::stats::SessionStats> {
        self.stream_handler
            .handler
            .borrow_mut()
            .webtransport_close_session(
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

    /// Create `WebTransport` stream.
    ///
    /// # Errors
    ///
    /// It may return `InvalidStreamId` if a stream does not exist anymore.
    pub fn create_stream(&self, stream_type: StreamType) -> Res<Http3OrWebTransportStream> {
        let session_id = self.stream_handler.stream_id();
        let id = self
            .stream_handler
            .handler
            .borrow_mut()
            .webtransport_create_stream(
                &mut self.stream_handler.conn.borrow_mut(),
                session_id,
                stream_type,
            )?;

        Ok(Http3OrWebTransportStream::new(
            self.stream_handler.conn.clone(),
            Rc::clone(&self.stream_handler.handler),
            Http3StreamInfo::new(id, Http3StreamType::WebTransport(session_id)),
        ))
    }

    /// Send `WebTransport` datagram.
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
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow_mut()
            .webtransport_send_datagram(
                &mut self.stream_handler.conn.borrow_mut(),
                session_id,
                buf,
                id,
                now,
                send_group_id,
                send_order,
            )
    }

    /// Set the outgoing-datagram queue's max-buffered limit for this session.
    ///
    /// See [`ClientSession::webtransport_set_max_buffered_datagrams`].
    ///
    /// # Errors
    ///
    /// Returns `InvalidStreamId` if the session is not yet accepted or no
    /// longer exists.
    pub fn set_max_buffered_datagrams(
        &self,
        max_buffered_datagrams: Option<NonZeroUsize>,
    ) -> Res<()> {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow_mut()
            .webtransport_set_max_buffered_datagrams(
                &mut self.stream_handler.conn.borrow_mut(),
                session_id,
                max_buffered_datagrams,
            )
    }

    /// See [`ClientSession::webtransport_set_datagram_max_age`].
    ///
    /// # Errors
    ///
    /// Returns `InvalidStreamId` if the session is not yet accepted or no
    /// longer exists.
    pub fn set_datagram_max_age(&self, max_age: Option<Duration>, now: Instant) -> Res<()> {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow_mut()
            .webtransport_set_datagram_max_age(
                &mut self.stream_handler.conn.borrow_mut(),
                session_id,
                max_age,
                now,
            )
    }

    /// This session's statistics, e.g. `outgoing_datagrams.expired`.
    #[cfg(test)]
    pub(crate) fn stats(&self) -> extended_connect::stats::SessionStats {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow_mut()
            .base_handler_mut()
            .webtransport_session_stats(session_id)
            .expect("test session must exist")
    }

    /// Snapshot of the outgoing-datagram queue's current byte/count state.
    ///
    /// Production callers use
    /// [`ClientSession::webtransport_datagram_queue_capacity`].
    #[cfg(test)]
    pub(crate) fn datagram_queue_capacity(&self) -> DatagramQueueCapacity {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow_mut()
            .base_handler_mut()
            .webtransport_datagram_queue_capacity(&self.stream_handler.conn.borrow(), session_id)
            .expect("test session must exist")
    }

    /// What the transport still queues for this session, which can outlive
    /// the session itself.
    #[cfg(test)]
    pub(crate) fn transport_queued_datagrams(&self) -> usize {
        self.stream_handler
            .conn
            .borrow()
            .datagram_queue_capacity(self.stream_handler.stream_id())
            .queued_datagrams
    }

    // TODO: Currently not called in neqo or gecko. It should likely be called at least from gecko.
    #[must_use]
    pub fn remote_datagram_size(&self) -> u64 {
        self.stream_handler.conn.borrow().remote_datagram_size()
    }

    /// Returns the current max size of a datagram that can fit into a packet.
    /// The value will change over time depending on the encoded size of the
    /// packet number, ack frames, etc.
    ///
    /// # Errors
    ///
    /// The function returns `NotAvailable` if datagrams are not enabled.
    ///
    /// # Panics
    ///
    /// This cannot panic. The max varint length is 8.
    pub fn max_datagram_size(&self) -> Res<u64> {
        let qsid_len = Encoder::varint_len(self.stream_handler.stream_id().as_u64() >> 2);
        Ok(self
            .stream_handler
            .conn
            .borrow()
            .max_datagram_size()?
            .saturating_sub(to_u64(qsid_len)))
    }

    /// Export keying material for this WebTransport session
    /// (draft-ietf-webtrans-http3 §4.8).
    ///
    /// # Errors
    ///
    /// Returns `Error::InvalidStreamId` if the session is no longer active,
    /// `Error::InvalidInput` if `out` is empty or `label`/`context`
    /// exceed 255 bytes, or `Error::Transport` if the connection is not ready
    /// or the TLS export fails.
    pub fn export_keying_material(&self, label: &[u8], context: &[u8], out: &mut [u8]) -> Res<()> {
        let session_id = self.stream_handler.stream_id();
        self.stream_handler
            .handler
            .borrow()
            .validate_extended_connect_session(session_id)?;
        self.stream_handler
            .conn
            .borrow()
            .webtransport_export_keying_material(session_id, label, context, out)
    }
}

impl Deref for ServerSession {
    type Target = StreamHandler;
    fn deref(&self) -> &Self::Target {
        &self.stream_handler
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
    NewStream(Http3OrWebTransportStream),
    Datagram {
        session: ServerSession,
        datagram: Bytes,
    },
}

pub(crate) trait ServerEvents {
    fn webtransport_new_session(&self, session: ServerSession, headers: Vec<Header>);
    fn webtransport_session_closed(
        &self,
        session: ServerSession,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    );
    fn webtransport_new_stream(&self, stream: Http3OrWebTransportStream);
    fn webtransport_datagram(&self, session: ServerSession, datagram: Bytes);
}

impl ServerEvents for Http3ServerEvents {
    fn webtransport_new_session(&self, session: ServerSession, headers: Vec<Header>) {
        self.push(Http3ServerEvent::WebTransport(ServerEvent::NewSession {
            session,
            headers,
        }));
    }

    fn webtransport_session_closed(
        &self,
        session: ServerSession,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    ) {
        self.push(Http3ServerEvent::WebTransport(ServerEvent::SessionClosed {
            session,
            reason,
            headers,
        }));
    }

    fn webtransport_new_stream(&self, stream: Http3OrWebTransportStream) {
        self.push(Http3ServerEvent::WebTransport(ServerEvent::NewStream(
            stream,
        )));
    }

    fn webtransport_datagram(&self, session: ServerSession, datagram: Bytes) {
        self.push(Http3ServerEvent::WebTransport(ServerEvent::Datagram {
            session,
            datagram,
        }));
    }
}

#[cfg(test)]
mod tests {
    use neqo_transport::StreamId;
    use test_fixture::connect;

    use super::ExportKeyingMaterial as _;

    /// The exporter context must encode the session ID as a fixed 64-bit
    /// field, per draft-ietf-webtrans-http3 §4.8, Figure 6.
    #[test]
    fn exporter_context_session_id_is_64_bits() {
        let (client, _server) = connect();
        let session_id = StreamId::new(4);

        let mut wt = [0u8; 32];
        client
            .webtransport_export_keying_material(session_id, b"label", b"ctx", &mut wt)
            .unwrap();

        let mut expected_context = vec![0, 0, 0, 0, 0, 0, 0, 4];
        expected_context.push(5);
        expected_context.extend_from_slice(b"label");
        expected_context.push(3);
        expected_context.extend_from_slice(b"ctx");
        let mut raw = [0u8; 32];
        client
            .export_keying_material("EXPORTER-WebTransport", &expected_context, &mut raw)
            .unwrap();

        assert_eq!(wt, raw);
    }
}
