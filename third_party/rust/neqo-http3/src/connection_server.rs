// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    fmt::{self, Display, Formatter},
    rc::Rc,
    time::Instant,
};

use neqo_common::{Header, MessageType, Role, event::Provider as _};
use neqo_transport::{AppError, Connection, ConnectionEvent, StreamId};
use tracing::{debug, info, trace};

use crate::{
    Error, Http3Parameters, Http3StreamType, NewStreamType, Priority, PriorityHandler,
    ReceiveOutput, Res,
    connection::{Http3Connection, Http3State},
    frames::HFrame,
    recv_message::{RecvMessage, RecvMessageInfo},
    send_message::SendMessage,
    server_connection_events::{Http3ServerConnEvent, Http3ServerConnEvents},
};

#[derive(Debug)]
pub struct Http3ServerHandler {
    base_handler: Http3Connection,
    events: Http3ServerConnEvents,
    needs_processing: bool,
    goaway_push_id: Option<u64>,
}

impl Display for Http3ServerHandler {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Http3 server connection")
    }
}

impl Http3ServerHandler {
    pub(crate) fn new(http3_parameters: Http3Parameters) -> Self {
        Self {
            base_handler: Http3Connection::new(http3_parameters, Role::Server),
            events: Http3ServerConnEvents::default(),
            needs_processing: false,
            goaway_push_id: None,
        }
    }

    #[must_use]
    pub fn state(&self) -> Http3State {
        self.base_handler.state().clone()
    }

    pub(crate) const fn base_handler_mut(&mut self) -> &mut Http3Connection {
        &mut self.base_handler
    }

    pub(crate) const fn server_events(&self) -> &Http3ServerConnEvents {
        &self.events
    }

    pub(crate) const fn mark_needs_processing(&mut self) {
        self.needs_processing = true;
    }

    /// Supply a response for a request.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist,
    /// `AlreadyClosed` if the stream has already been closed.
    /// `TransportStreamDoesNotExist` if the transport stream does not exist (this may happen if
    /// `process_output` has not been called when needed, and HTTP3 layer has not picked up the
    /// info that the stream has been closed.) `InvalidInput` if an empty buffer has been
    /// supplied.
    pub(crate) fn send_data(
        &mut self,
        stream_id: StreamId,
        data: &[u8],
        conn: &mut Connection,
        now: Instant,
    ) -> Res<usize> {
        let n = self
            .base_handler
            .send_streams_mut()
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .send_data(conn, data, now)?;
        if n > 0 {
            self.base_handler.stream_has_pending_data(stream_id);
        }
        self.needs_processing = true;
        Ok(n)
    }

    /// Supply response heeaders for a request.
    pub(crate) fn send_headers(
        &mut self,
        stream_id: StreamId,
        headers: &[Header],
        conn: &mut Connection,
    ) -> Res<()> {
        self.base_handler
            .send_streams_mut()
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .http_stream()
            .ok_or(Error::InvalidStreamId)?
            .send_headers(headers, conn)?;
        self.base_handler.stream_has_pending_data(stream_id);
        self.needs_processing = true;
        Ok(())
    }

    /// This is called when application is done sending a request.
    ///
    /// # Errors
    ///
    /// An error will be returned if stream does not exist.
    pub fn stream_close_send(
        &mut self,
        stream_id: StreamId,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<()> {
        debug!("[{self}] Close sending side stream={stream_id}");
        self.base_handler.stream_close_send(conn, stream_id, now)?;
        self.needs_processing = true;
        Ok(())
    }

    /// An application may reset a stream(request).
    /// Both sides, sending and receiving side, will be closed.
    ///
    /// # Errors
    ///
    /// An error will be return if a stream does not exist.
    pub fn cancel_fetch(
        &mut self,
        stream_id: StreamId,
        error: AppError,
        conn: &mut Connection,
    ) -> Res<()> {
        info!("[{self}] cancel_fetch {stream_id} error={error}");
        self.needs_processing = true;
        self.base_handler.cancel_fetch(stream_id, error, conn)
    }

    pub fn stream_stop_sending(
        &mut self,
        stream_id: StreamId,
        error: AppError,
        conn: &mut Connection,
    ) -> Res<()> {
        info!("[{self}] stream_stop_sending {stream_id} error={error}");
        self.needs_processing = true;
        self.base_handler
            .stream_stop_sending(conn, stream_id, error)
    }

    pub fn stream_reset_send(
        &mut self,
        stream_id: StreamId,
        error: AppError,
        conn: &mut Connection,
    ) -> Res<()> {
        info!("[{self}] stream_reset_send {stream_id} error={error}");
        self.needs_processing = true;
        self.base_handler.stream_reset_send(conn, stream_id, error)
    }

    /// Commit to reliably delivering the stream data buffered so far.
    ///
    /// # Errors
    /// When the transport rejects the commitment (see
    /// [`neqo_transport::Connection::stream_commit`]).
    pub fn stream_commit(
        &mut self,
        stream_id: StreamId,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<()> {
        self.base_handler.stream_commit(conn, stream_id, now)
    }

    pub(crate) fn validate_extended_connect_session(&self, session_id: StreamId) -> Res<()> {
        self.base_handler
            .validate_extended_connect_session(session_id)
            .map(|_| ())
    }

    /// Process HTTTP3 layer.
    pub fn process_http3(&mut self, conn: &mut Connection, now: Instant) {
        trace!("[{self}] Process http3 internal");
        if matches!(self.base_handler.state(), Http3State::Closed(..)) {
            return;
        }

        self.base_handler.expire_datagram_queues(conn, now);
        let res = self.check_connection_events(conn, now);
        if !self.check_result(conn, now, &res) && self.base_handler.state().active() {
            let res = self.base_handler.process_sending(conn, now);
            self.check_result(conn, now, &res);
        }
    }

    /// Take the next available event.
    pub(crate) fn next_event(&self) -> Option<Http3ServerConnEvent> {
        self.events.next_event()
    }

    /// Queue a GOAWAY frame on the local control stream.
    pub(crate) fn queue_goaway(&mut self, stream_id: StreamId) {
        self.base_handler
            .queue_control_frame(&HFrame::Goaway { stream_id });
        self.needs_processing = true;
    }

    /// Whether this connection has events to process or data to send.
    pub(crate) fn should_be_processed(&mut self) -> bool {
        if self.needs_processing {
            self.needs_processing = false;
            return true;
        }
        self.base_handler.has_data_to_send() || self.events.has_events()
    }

    // This function takes the provided result and check for an error.
    // An error results in closing the connection.
    fn check_result<ERR>(&mut self, conn: &mut Connection, now: Instant, res: &Res<ERR>) -> bool {
        match &res {
            Err(e) => {
                self.close(conn, now, e);
                true
            }
            _ => false,
        }
    }

    fn close(&mut self, conn: &mut Connection, now: Instant, err: &Error) {
        info!("[{self}] Connection error: {err}");
        conn.close(now, err.code(), format!("{err}"));
        self.base_handler.close(err.code());
        self.events
            .connection_state_change(self.base_handler.state().clone());
    }

    // If this return an error the connection must be closed.
    fn check_connection_events(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        trace!("[{self}] Check connection events");
        while let Some(e) = conn.next_event() {
            debug!("[{self}] check_connection_events - event {e:?}");
            match e {
                ConnectionEvent::NewStream { stream_id } => {
                    self.base_handler.add_new_stream(stream_id);
                }
                ConnectionEvent::RecvStreamReadable { stream_id } => {
                    self.handle_stream_readable(conn, stream_id, now)?;
                }
                ConnectionEvent::RecvStreamReset {
                    stream_id,
                    app_error,
                } => {
                    self.base_handler
                        .handle_stream_reset(stream_id, app_error, conn)?;
                }
                ConnectionEvent::SendStreamStopSending {
                    stream_id,
                    app_error,
                } => self
                    .base_handler
                    .handle_stream_stop_sending(stream_id, app_error, conn)?,
                ConnectionEvent::StateChange(state) => {
                    if self.base_handler.handle_state_change(conn, &state)? {
                        if self.base_handler.state() == &Http3State::Connected {
                            let settings = self.base_handler.save_settings();
                            conn.send_ticket(now, &settings)?;
                        }
                        self.events
                            .connection_state_change(self.base_handler.state().clone());
                    }
                }
                ConnectionEvent::SendStreamWritable { stream_id } => {
                    if let Some(s) = self.base_handler.send_streams_mut().get_mut(&stream_id) {
                        s.stream_writable();
                    }
                }
                ConnectionEvent::Datagram(dgram) => self.base_handler.handle_datagram(dgram),
                ConnectionEvent::OutgoingDatagramSpaceAvailable { stream_id } => {
                    self.events.datagram_space_available(stream_id);
                }
                ConnectionEvent::AuthenticationNeeded
                | ConnectionEvent::EchFallbackAuthenticationNeeded { .. }
                | ConnectionEvent::ZeroRttRejected
                | ConnectionEvent::ResumptionToken(..) => return Err(Error::HttpInternal(4)),
                ConnectionEvent::SendStreamComplete { .. }
                | ConnectionEvent::SendStreamCreatable { .. }
                | ConnectionEvent::OutgoingDatagramOutcome { .. }
                | ConnectionEvent::SconeUpdated(_)
                | ConnectionEvent::PathMigrated { .. } => {}
            }
        }
        Ok(())
    }

    fn handle_stream_readable(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        now: Instant,
    ) -> Res<()> {
        match self
            .base_handler
            .handle_stream_readable(conn, stream_id, now)?
        {
            ReceiveOutput::NewStream(NewStreamType::Http(first_frame_type)) => {
                self.base_handler.add_streams(
                    stream_id,
                    Box::new(SendMessage::new(
                        MessageType::Response,
                        Http3StreamType::Http,
                        stream_id,
                        Rc::clone(self.base_handler.qpack_encoder()),
                        Box::new(self.events.clone()),
                    )),
                    Box::new(RecvMessage::new(
                        &RecvMessageInfo {
                            message_type: MessageType::Request,
                            stream_type: Http3StreamType::Http,
                            stream_id,
                            first_frame_type: Some(first_frame_type),
                        },
                        Rc::clone(self.base_handler.qpack_decoder()),
                        Box::new(self.events.clone()),
                        PriorityHandler::new(Priority::default(), false),
                    )),
                );
                let res = self
                    .base_handler
                    .handle_stream_readable(conn, stream_id, now)?;
                assert_eq!(ReceiveOutput::NoOutput, res);
                Ok(())
            }
            ReceiveOutput::NewStream(NewStreamType::WebTransportStream(session_id)) => {
                self.base_handler.webtransport_create_stream_remote(
                    StreamId::from(session_id),
                    stream_id,
                    Box::new(self.events.clone()),
                    Box::new(self.events.clone()),
                )?;
                let res = self
                    .base_handler
                    .handle_stream_readable(conn, stream_id, now)?;
                assert_eq!(ReceiveOutput::NoOutput, res);
                Ok(())
            }
            ReceiveOutput::ControlFrames(control_frames) => {
                for f in control_frames {
                    match f {
                        HFrame::Goaway { stream_id } => {
                            let push_id = stream_id.as_u64(); // A client GOAWAY carries a push ID.
                            if self.goaway_push_id.is_some_and(|last| push_id > last) {
                                return Err(Error::HttpId);
                            }
                            self.goaway_push_id = Some(push_id);
                            Ok(())
                        }
                        // Server push is not supported. A server that does not push ignores
                        // MAX_PUSH_ID (RFC 9114, Section 7.2.7); any other push frame is
                        // unexpected.
                        HFrame::MaxPushId => Ok(()),
                        HFrame::CancelPush | HFrame::PriorityUpdatePush => {
                            Err(Error::HttpFrameUnexpected)
                        }
                        HFrame::PriorityUpdateRequest {
                            element_id,
                            priority,
                        } => {
                            // check that the element_id references a request stream
                            // within the client-sided bidirectional stream limit
                            let element_stream_id = StreamId::new(element_id);
                            if !element_stream_id.is_bidi()
                                || !element_stream_id.is_client_initiated()
                                || !conn.is_stream_id_allowed(element_stream_id)
                            {
                                return Err(Error::HttpId);
                            }

                            self.events.priority_update(element_stream_id, priority);
                            Ok(())
                        }
                        _ => unreachable!(
                            "only Goaway, PriorityUpdateRequest and server-push frames are put into control_frames"
                        ),
                    }?;
                }
                Ok(())
            }
            _ => Ok(()),
        }
    }

    /// Response data are read directly into a buffer supplied as a parameter of this function to
    /// avoid copying data.
    ///
    /// # Errors
    ///
    /// It returns an error if a stream does not exist or an error happen while reading a stream,
    /// e.g. early close, protocol error, etc.
    pub fn read_data(
        &mut self,
        conn: &mut Connection,
        now: Instant,
        stream_id: StreamId,
        buf: &mut [u8],
    ) -> Res<(usize, bool)> {
        debug!("[{self}] read_data from stream {stream_id}");
        let res = self.base_handler.read_data(conn, stream_id, buf, now);
        if let Err(e) = &res
            && e.connection_error()
        {
            self.close(conn, now, e);
        }
        res
    }
}
