// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::{RefCell, RefMut},
    fmt::{self, Display, Formatter},
    num::NonZeroUsize,
    path::PathBuf,
    rc::Rc,
    time::Instant,
};

use neqo_common::Datagram;
use neqo_transport::{
    ConnectionIdGenerator, Output, OutputBatch, State,
    server::{ConnectionRef, Server, ValidateAddress},
};
use nss::{AntiReplay, Cipher, PrivateKey, PublicKey, ZeroRttChecker};
use rustc_hash::FxHashMap as HashMap;
use tracing::trace;

use crate::{
    Http3Parameters, Http3StreamInfo, Res,
    connect_udp::{self, ServerEvents as _},
    connection::Http3State,
    connection_server::Http3ServerHandler,
    server_connection_events::{ConnectUdpEvent, Http3ServerConnEvent, WebTransportEvent},
    server_events::{Http3OrWebTransportStream, Http3ServerEvent, Http3ServerEvents},
    settings::HttpZeroRttChecker,
    webtransport::{ServerEvents as _, ServerSession},
};

type HandlerRef = Rc<RefCell<Http3ServerHandler>>;

/// Maximum stream data per [`Http3ServerEvent::Data`], and the read buffer's size.
///
/// A tuning knob, kept in sync with `neqo_bin::STREAM_IO_BUFFER_SIZE`.
const MAX_EVENT_DATA_SIZE: usize = 32 * 1024;

pub struct Http3Server {
    server: Server,
    http3_parameters: Http3Parameters,
    http3_handlers: HashMap<ConnectionRef, HandlerRef>,
    events: Http3ServerEvents,
    /// Reused across events, so only each event's exactly-sized copy is allocated.
    read_buf: Vec<u8>,
}

impl Display for Http3Server {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Http3 server ")
    }
}

impl Http3Server {
    /// # Errors
    ///
    /// Making a `neqo_transport::Server` may produce an error. This can only be a crypto error if
    /// the socket can't be created or configured.
    pub fn new<A: AsRef<str>, A1: AsRef<str>>(
        now: Instant,
        certs: &[A],
        protocols: &[A1],
        anti_replay: AntiReplay,
        cid_manager: Rc<RefCell<dyn ConnectionIdGenerator>>,
        http3_parameters: Http3Parameters,
        zero_rtt_checker: Option<Box<dyn ZeroRttChecker>>,
    ) -> Res<Self> {
        Ok(Self {
            server: Server::new(
                now,
                certs,
                protocols,
                anti_replay,
                zero_rtt_checker
                    .unwrap_or_else(|| Box::new(HttpZeroRttChecker::new(http3_parameters.clone()))),
                cid_manager,
                http3_parameters.get_connection_parameters().clone(),
            )?,
            http3_parameters,
            http3_handlers: HashMap::default(),
            events: Http3ServerEvents::default(),
            read_buf: vec![0; MAX_EVENT_DATA_SIZE],
        })
    }

    pub fn set_qlog_dir(&mut self, dir: Option<PathBuf>) {
        self.server.set_qlog_dir(dir);
    }

    pub fn set_validation(&self, v: ValidateAddress) {
        self.server.set_validation(v);
    }

    pub fn set_ciphers<A: AsRef<[Cipher]>>(&mut self, ciphers: A) {
        self.server.set_ciphers(ciphers);
    }

    /// Enable encrypted client hello (ECH).
    ///
    /// # Errors
    ///
    /// Only when NSS can't serialize a configuration.
    pub fn enable_ech(
        &mut self,
        config: u8,
        public_name: &str,
        sk: &PrivateKey,
        pk: &PublicKey,
    ) -> Res<()> {
        self.server.enable_ech(config, public_name, sk, pk)?;
        Ok(())
    }

    #[must_use]
    pub fn ech_config(&self) -> &[u8] {
        self.server.ech_config()
    }

    /// Short-hand for [`Http3Server::process`] with no input datagram.
    pub fn process_output(&mut self, now: Instant) -> Output {
        self.process(None::<Datagram>, now)
    }

    /// Wrapper around [`Http3Server::process_multiple`] that processes a single
    /// output datagram only.
    #[expect(clippy::missing_panics_doc, reason = "see expect()")]
    pub fn process<A: AsRef<[u8]> + AsMut<[u8]>, I: IntoIterator<Item = Datagram<A>>>(
        &mut self,
        dgrams: I,
        now: Instant,
    ) -> Output {
        self.process_multiple(dgrams, now, NonZeroUsize::MIN)
            .try_into()
            .expect("max_datagrams is 1")
    }

    pub fn process_multiple<A: AsRef<[u8]> + AsMut<[u8]>, I: IntoIterator<Item = Datagram<A>>>(
        &mut self,
        dgrams: I,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        trace!("[{self}] Process");
        let out = self.server.process_multiple_input(dgrams, now);
        self.process_http3(now);
        // Try again if input processing did not already produce a datagram.
        if let OutputBatch::DatagramBatch(d) = out {
            trace!("[{self}] Send packet: {d:?}");
            OutputBatch::DatagramBatch(d)
        } else {
            let out = self
                .server
                .process_multiple(Option::<Datagram>::None, now, max_datagrams);
            if !matches!(out, OutputBatch::DatagramBatch(_)) {
                self.http3_handlers.retain(|c, _| {
                    if let State::Closed(error) = c.borrow().state().clone() {
                        self.events
                            .connection_state_change(c.clone(), Http3State::Closed(error));
                        false
                    } else {
                        true
                    }
                });
            }
            out
        }
    }

    /// Process HTTP3 layer.
    fn process_http3(&mut self, now: Instant) {
        trace!("[{self}] Process http3 internal");
        #[expect(
            clippy::mutable_key_type,
            reason = "ActiveConnectionRef::Hash doesn't access any of the interior mutable types."
        )]
        let mut active_conns = self.server.active_connections();
        active_conns.extend(
            self.http3_handlers
                .iter()
                .filter(|(_, handler)| handler.borrow_mut().should_be_processed())
                .map(|(c, _)| c)
                .cloned(),
        );

        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to loop over active connections in an undefined order."
        )]
        for conn in active_conns {
            self.process_events(&conn, now);
        }
    }

    #[expect(
        clippy::too_many_lines,
        reason = "Function is mostly a match statement."
    )]
    fn process_events(&mut self, conn: &ConnectionRef, now: Instant) {
        let mut remove = false;
        let http3_parameters = &self.http3_parameters;
        {
            let handler = self.http3_handlers.entry(conn.clone()).or_insert_with(|| {
                Rc::new(RefCell::new(Http3ServerHandler::new(
                    http3_parameters.clone(),
                )))
            });
            handler
                .borrow_mut()
                .process_http3(&mut conn.borrow_mut(), now);
            let mut handler_borrowed = handler.borrow_mut();
            while let Some(e) = handler_borrowed.next_event() {
                match e {
                    Http3ServerConnEvent::Headers {
                        stream_info,
                        headers,
                        fin,
                    } => self.events.headers(
                        Http3OrWebTransportStream::new(
                            conn.clone(),
                            Rc::clone(handler),
                            stream_info,
                        ),
                        headers,
                        fin,
                    ),
                    Http3ServerConnEvent::DataReadable { stream_info } => {
                        prepare_data(
                            stream_info,
                            &mut handler_borrowed,
                            conn,
                            handler,
                            now,
                            &self.events,
                            &mut self.read_buf,
                        );
                    }
                    Http3ServerConnEvent::DataWritable { stream_info } => self
                        .events
                        .data_writable(conn.clone(), Rc::clone(handler), stream_info),
                    Http3ServerConnEvent::StreamReset { stream_info, error } => {
                        self.events.stream_reset(
                            conn.clone(),
                            Rc::clone(handler),
                            stream_info,
                            error,
                        );
                    }
                    Http3ServerConnEvent::StreamStopSending { stream_info, error } => {
                        self.events.stream_stop_sending(
                            conn.clone(),
                            Rc::clone(handler),
                            stream_info,
                            error,
                        );
                    }
                    Http3ServerConnEvent::StateChange(state) => {
                        self.events
                            .connection_state_change(conn.clone(), state.clone());
                        if let Http3State::Closed { .. } = state {
                            remove = true;
                        }
                    }
                    Http3ServerConnEvent::OutgoingDatagramSpaceAvailable { session_id } => {
                        self.events
                            .datagram_space_available(conn.clone(), session_id);
                    }
                    Http3ServerConnEvent::PriorityUpdate {
                        stream_id,
                        priority,
                    } => {
                        self.events.priority_update(stream_id, priority);
                    }
                    Http3ServerConnEvent::WebTransport(WebTransportEvent::Session {
                        stream_id,
                        headers,
                    }) => {
                        self.events.webtransport_new_session(
                            ServerSession::new(conn.clone(), Rc::clone(handler), stream_id),
                            headers,
                        );
                    }
                    Http3ServerConnEvent::ConnectUdp(ConnectUdpEvent::Session {
                        stream_id,
                        headers,
                    }) => {
                        self.events.connect_udp_new_session(
                            connect_udp::ServerSession::new(
                                conn.clone(),
                                Rc::clone(handler),
                                stream_id,
                            ),
                            headers,
                        );
                    }
                    Http3ServerConnEvent::WebTransport(WebTransportEvent::SessionClosed {
                        stream_id,
                        reason,
                        headers,
                        ..
                    }) => self.events.webtransport_session_closed(
                        ServerSession::new(conn.clone(), Rc::clone(handler), stream_id),
                        reason,
                        headers,
                    ),
                    Http3ServerConnEvent::ConnectUdp(ConnectUdpEvent::SessionClosed {
                        stream_id,
                        reason,
                        headers,
                        ..
                    }) => self.events.connect_udp_session_closed(
                        connect_udp::ServerSession::new(
                            conn.clone(),
                            Rc::clone(handler),
                            stream_id,
                        ),
                        reason,
                        headers,
                    ),
                    Http3ServerConnEvent::WebTransport(WebTransportEvent::NewStream(
                        stream_info,
                    )) => self
                        .events
                        .webtransport_new_stream(Http3OrWebTransportStream::new(
                            conn.clone(),
                            Rc::clone(handler),
                            stream_info,
                        )),
                    Http3ServerConnEvent::WebTransport(WebTransportEvent::Datagram {
                        session_id,
                        datagram,
                    }) => {
                        self.events.webtransport_datagram(
                            ServerSession::new(conn.clone(), Rc::clone(handler), session_id),
                            datagram,
                        );
                    }
                    Http3ServerConnEvent::ConnectUdp(ConnectUdpEvent::Datagram {
                        session_id,
                        datagram,
                    }) => {
                        self.events.connect_udp_datagram(
                            connect_udp::ServerSession::new(
                                conn.clone(),
                                Rc::clone(handler),
                                session_id,
                            ),
                            datagram,
                        );
                    }
                }
            }
        }
        if remove {
            self.http3_handlers.remove(&conn.clone());
        }
    }

    /// Get all current events. Best used just in debug/testing code, use
    /// `next_event` instead.
    pub fn events(&self) -> impl Iterator<Item = Http3ServerEvent> {
        self.events.events()
    }

    /// Return true if there are outstanding events.
    #[must_use]
    pub fn has_events(&self) -> bool {
        self.events.has_events()
    }

    /// Get events that indicate state changes on the connection. This method
    /// correctly handles cases where handling one event can obsolete
    /// previously-queued events, or cause new events to be generated.
    #[must_use]
    pub fn next_event(&self) -> Option<Http3ServerEvent> {
        self.events.next_event()
    }

    /// Queue a GOAWAY frame to be sent to all connected clients.
    ///
    /// `stream_id` is the first stream ID that was **not** processed.
    ///
    /// **Note:** The same `stream_id` is sent to every connection. Callers
    /// must ensure that `stream_id` is valid across all connections and that
    /// subsequent calls use non-increasing values (per RFC 9114 §5.2).
    pub fn send_goaway(&self, stream_id: neqo_transport::StreamId) {
        debug_assert!(
            !stream_id.is_uni() && !stream_id.is_server_initiated(),
            "GOAWAY stream ID must be client-initiated bidirectional (RFC 9114 S5.2)"
        );
        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to iterate over handlers in undefined order for goaway"
        )]
        for handler in self.http3_handlers.values() {
            handler.borrow_mut().queue_goaway(stream_id);
        }
    }
}
fn prepare_data(
    stream_info: Http3StreamInfo,
    handler_borrowed: &mut RefMut<Http3ServerHandler>,
    conn: &ConnectionRef,
    handler: &HandlerRef,
    now: Instant,
    events: &Http3ServerEvents,
    read_buf: &mut [u8],
) {
    debug_assert!(!read_buf.is_empty(), "read buffer must not be empty");
    loop {
        let Ok((amount, fin)) = handler_borrowed.read_data(
            &mut conn.borrow_mut(),
            now,
            stream_info.stream_id(),
            read_buf,
        ) else {
            // Any error will closed the handler, just ignore this event, the next event must
            // be a state change event.
            break;
        };
        if amount > 0 || fin {
            let data = read_buf[..amount].to_vec();
            events.data(conn.clone(), Rc::clone(handler), stream_info, data, fin);
        }
        if amount < read_buf.len() || fin {
            break;
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{
        collections::HashMap,
        ops::{Deref, DerefMut},
        time::Duration,
    };

    use neqo_common::{Datagram, Encoder, event::Provider as _};
    use neqo_qpack as qpack;
    use neqo_transport::{
        CloseReason, Connection, ConnectionEvent, State, StreamId, StreamType, ZeroRttState,
    };
    use nss::{AuthenticationStatus, ResumptionToken, ZeroRttCheckResult, ZeroRttChecker};
    use test_fixture::{
        CountingConnectionIdGenerator, DEFAULT_ALPN, DEFAULT_KEYS, anti_replay, default_client,
        fixture_init, now,
    };

    use super::{Http3Server, Http3ServerEvent, Http3State, Rc, RefCell};
    use crate::{Error, HFrame, Header, Http3Parameters, Priority};

    fn qpack_defaults() -> qpack::Settings {
        qpack::Settings::default()
            .max_table_size_encoder(100)
            .max_table_size_decoder(100)
            .max_blocked_streams(100)
    }

    fn http3params(qpack_settings: qpack::Settings) -> Http3Parameters {
        Http3Parameters::default().qpack(qpack_settings)
    }

    pub fn create_server(conn_params: Http3Parameters) -> Http3Server {
        fixture_init();
        Http3Server::new(
            now(),
            DEFAULT_KEYS,
            DEFAULT_ALPN,
            anti_replay(),
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            conn_params,
            None,
        )
        .expect("create a server")
    }

    /// Create a http3 server with default configuration.
    pub fn default_server() -> Http3Server {
        create_server(http3params(qpack_defaults()))
    }

    fn assert_closed(hconn: &Http3Server, expected: &Error) {
        let err = CloseReason::Application(expected.code());
        let closed = |e| matches!(e, Http3ServerEvent::StateChange{ state: Http3State::Closing(e) | Http3State::Closed(e), .. } if e == err);
        assert!(hconn.events().any(closed));
    }

    fn assert_connected(hconn: &Http3Server) {
        let connected = |e| {
            matches!(
                e,
                Http3ServerEvent::StateChange {
                    state: Http3State::Connected,
                    ..
                }
            )
        };
        assert!(hconn.events().any(connected));
    }

    fn assert_not_closed(hconn: &Http3Server) {
        let closed = |e| {
            matches!(
                e,
                Http3ServerEvent::StateChange {
                    state: Http3State::Closing(..),
                    ..
                }
            )
        };
        assert!(!hconn.events().any(closed));
    }

    const CLIENT_SIDE_CONTROL_STREAM_ID: StreamId = StreamId::new(2);
    const CLIENT_SIDE_ENCODER_STREAM_ID: StreamId = StreamId::new(6);
    const CLIENT_SIDE_DECODER_STREAM_ID: StreamId = StreamId::new(10);
    const SERVER_SIDE_CONTROL_STREAM_ID: StreamId = StreamId::new(3);
    const SERVER_SIDE_ENCODER_STREAM_ID: StreamId = StreamId::new(7);
    const SERVER_SIDE_DECODER_STREAM_ID: StreamId = StreamId::new(11);

    fn connect_transport(server: &mut Http3Server, client: &mut Connection, resume: bool) {
        let c1 = client.process_output(now());
        let c11 = client.process_output(now());
        _ = server.process(c1.dgram(), now());
        let s1 = server.process(c11.dgram(), now());
        let c2 = client.process(s1.dgram(), now());
        let s2 = server.process(c2.dgram(), now());
        let c2 = client.process(s2.dgram(), now());
        let needs_auth = client
            .events()
            .any(|e| e == ConnectionEvent::AuthenticationNeeded);
        let c3 = if needs_auth {
            assert!(!resume);
            // c2 should just be an ACK, so absorb that.
            let s_ack = server.process(c2.dgram(), now());
            assert!(s_ack.dgram().is_none());

            client.authenticated(AuthenticationStatus::Ok, now());
            client.process_output(now())
        } else {
            assert!(resume);
            let s3 = server.process(c2.dgram(), now()).dgram();
            client.process(s3, now())
        };
        assert!(client.state().connected());
        let s4 = server.process(c3.dgram(), now());
        assert_eq!(server.process_output(now()).dgram(), None);
        assert_connected(server);
        assert_eq!(client.process(s4.dgram(), now()).dgram(), None);
    }

    // Start a client/server and check setting frame.
    fn connect_and_receive_settings_with_server(
        server: &mut Http3Server,
    ) -> (Connection, ResumptionToken) {
        const CONTROL_STREAM_DATA: &[u8] = &[0x0, 0x4, 0x12, 0x1, 0x40, 0x64, 0x7, 0x40, 0x64];

        let mut client = default_client();
        connect_transport(server, &mut client, false);

        let mut connected = false;
        let mut token = None;
        while let Some(e) = client.next_event() {
            match e {
                ConnectionEvent::NewStream { stream_id } => {
                    assert!(
                        (stream_id == SERVER_SIDE_CONTROL_STREAM_ID)
                            || (stream_id == SERVER_SIDE_ENCODER_STREAM_ID)
                            || (stream_id == SERVER_SIDE_DECODER_STREAM_ID)
                    );
                    assert_eq!(stream_id.stream_type(), StreamType::UniDi);
                }
                ConnectionEvent::RecvStreamReadable { stream_id } => {
                    if stream_id == CLIENT_SIDE_CONTROL_STREAM_ID
                        || stream_id == SERVER_SIDE_CONTROL_STREAM_ID
                    {
                        // the control stream
                        let mut buf = [0_u8; 100];
                        let (_, fin) = client.stream_recv(stream_id, &mut buf).unwrap();
                        assert!(!fin);
                        assert_eq!(&buf[..CONTROL_STREAM_DATA.len()], CONTROL_STREAM_DATA);
                    } else if stream_id == CLIENT_SIDE_ENCODER_STREAM_ID
                        || stream_id == SERVER_SIDE_ENCODER_STREAM_ID
                    {
                        let mut buf = [0_u8; 100];
                        let (amount, fin) = client.stream_recv(stream_id, &mut buf).unwrap();
                        assert!(!fin);
                        assert_eq!(amount, 1);
                        assert_eq!(buf[..1], [0x2]);
                    } else if stream_id == CLIENT_SIDE_DECODER_STREAM_ID
                        || stream_id == SERVER_SIDE_DECODER_STREAM_ID
                    {
                        let mut buf = [0_u8; 100];
                        let (amount, fin) = client.stream_recv(stream_id, &mut buf).unwrap();
                        assert!(!fin);
                        assert_eq!(amount, 1);
                        assert_eq!(buf[..1], [0x3]);
                    } else {
                        panic!("unexpected event");
                    }
                }
                ConnectionEvent::SendStreamWritable { stream_id } => {
                    assert!(
                        (stream_id == CLIENT_SIDE_CONTROL_STREAM_ID)
                            || (stream_id == CLIENT_SIDE_ENCODER_STREAM_ID)
                            || (stream_id == CLIENT_SIDE_DECODER_STREAM_ID)
                    );
                }
                ConnectionEvent::StateChange(State::Connected) => connected = true,
                ConnectionEvent::ResumptionToken(t) => token = Some(t),
                ConnectionEvent::StateChange(_) | ConnectionEvent::SendStreamCreatable { .. } => (),
                e => panic!("unexpected event: {e:?}"),
            }
        }
        assert!(connected);
        (client, token.unwrap())
    }

    /// `Closed` must fire once, even when another connection's traffic delays retirement.
    #[test]
    fn closed_event_fires_once_alongside_other_traffic() {
        let mut server = default_server();
        let (mut a, _) = connect_and_receive_settings_with_server(&mut server);
        let (mut b, _) = connect_and_receive_settings_with_server(&mut server);

        a.close(now(), 0, "done");
        let out = a.process_output(now());
        _ = server.process(out.dgram(), now());

        // Give B something to send, so the server owes it an ACK on the next call.
        let sid = b.stream_create(StreamType::BiDi).unwrap();
        _ = b.stream_send(sid, b"ping");
        let bout = b.process_output(now());

        // Past A's drain period; B's ACK makes the output step return early, skipping retain.
        let t = now() + Duration::from_secs(30);
        _ = server.process(bout.dgram(), t);

        let mut closed_count = 0;
        for i in 0..5 {
            _ = server.process_output(t + Duration::from_millis(i + 1));
            closed_count += server
                .events()
                .filter(|e| {
                    matches!(
                        e,
                        Http3ServerEvent::StateChange {
                            state: Http3State::Closed(_),
                            ..
                        }
                    )
                })
                .count();
        }
        assert_eq!(closed_count, 1, "Closed must be emitted exactly once");
    }

    /// A handler must not be retained once its connection is closed.
    #[test]
    fn handlers_do_not_accumulate() {
        const CONNECTIONS: usize = 5;
        let mut server = default_server();
        for _ in 0..CONNECTIONS {
            let (mut client, _) = connect_and_receive_settings_with_server(&mut server);
            client.close(now(), 0, "done");
            let out = client.process_output(now());
            _ = server.process(out.dgram(), now());
            // Past the draining period.
            _ = server.process(Option::<Datagram>::None, now() + Duration::from_secs(30));
        }
        assert!(
            server.http3_handlers.is_empty(),
            "{} handlers left for {CONNECTIONS} closed connections",
            server.http3_handlers.len()
        );
        let closed_count = server
            .events()
            .filter(|e| {
                matches!(
                    e,
                    Http3ServerEvent::StateChange {
                        state: Http3State::Closed(_),
                        ..
                    }
                )
            })
            .count();
        assert_eq!(closed_count, 1, "dropping a handler must deliver `Closed`");
    }

    fn connect_and_receive_settings() -> (Http3Server, Connection, ResumptionToken) {
        // Create a server and connect it to a client.
        // We will have a http3 server on one side and a neqo_transport
        // connection on the other side so that we can check what the http3
        // side sends and also to simulate an incorrectly behaving http3
        // client.

        let mut server = default_server();
        let (client, token) = connect_and_receive_settings_with_server(&mut server);
        (server, client, token)
    }

    // Test http3 connection initialization.
    // The server will open the control and qpack streams and send SETTINGS frame.
    #[test]
    fn server_connect() {
        drop(connect_and_receive_settings());
    }

    struct PeerConnection {
        conn: Connection,
        control_stream_id: StreamId,
    }

    impl PeerConnection {
        /// A shortcut for sending on the control stream.
        fn control_send(&mut self, data: &[u8]) {
            let res = self.conn.stream_send(self.control_stream_id, data);
            assert_eq!(res, Ok(data.len()));
        }
    }

    impl Deref for PeerConnection {
        type Target = Connection;
        fn deref(&self) -> &Self::Target {
            &self.conn
        }
    }

    impl DerefMut for PeerConnection {
        fn deref_mut(&mut self) -> &mut Self::Target {
            &mut self.conn
        }
    }

    // Connect transport, send and receive settings.
    fn connect_to(server: &mut Http3Server) -> (PeerConnection, ResumptionToken) {
        let (mut neqo_trans_conn, token) = connect_and_receive_settings_with_server(server);
        let control_stream = neqo_trans_conn.stream_create(StreamType::UniDi).unwrap();
        let mut sent = neqo_trans_conn.stream_send(
            control_stream,
            &[0x0, 0x4, 0x6, 0x1, 0x40, 0x64, 0x7, 0x40, 0x64],
        );
        assert_eq!(sent, Ok(9));
        let mut encoder = qpack::Encoder::new(
            &qpack::Settings::default()
                .max_table_size_encoder(100)
                .max_table_size_decoder(0)
                .max_blocked_streams(0)
                .max_tracked_streams(4096),
            true,
        );
        encoder
            .add_send_stream(neqo_trans_conn.stream_create(StreamType::UniDi).unwrap())
            .unwrap();
        encoder.send_encoder_updates(&mut neqo_trans_conn).unwrap();
        let decoder_stream = neqo_trans_conn.stream_create(StreamType::UniDi).unwrap();
        sent = neqo_trans_conn.stream_send(decoder_stream, &[0x3]);
        assert_eq!(sent, Ok(1));
        let out1 = neqo_trans_conn.process_output(now());
        let out2 = server.process(out1.dgram(), now());
        drop(neqo_trans_conn.process(out2.dgram(), now()));

        // assert no error occurred.
        assert_not_closed(server);

        (
            PeerConnection {
                conn: neqo_trans_conn,
                control_stream_id: control_stream,
            },
            token,
        )
    }

    fn connect() -> (Http3Server, PeerConnection) {
        let (server, client, _token) = connect_with_token();
        (server, client)
    }

    fn connect_with_token() -> (Http3Server, PeerConnection, ResumptionToken) {
        let mut server = default_server();
        let (client, token) = connect_to(&mut server);
        (server, client, token)
    }

    // Server: Test receiving a new control stream and a SETTINGS frame.
    #[test]
    fn server_receive_control_frame() {
        drop(connect());
    }

    // Server: Test that the connection will be closed if control stream
    // has been closed.
    #[test]
    fn server_close_control_stream() {
        let (mut hconn, mut peer_conn) = connect();
        let control = peer_conn.control_stream_id;
        peer_conn.stream_close_send(control).unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    // Server: test missing SETTINGS frame
    // (the first frame sent is a GOAWAY frame).
    #[test]
    fn server_missing_settings() {
        let (mut hconn, mut neqo_trans_conn, _token) = connect_and_receive_settings();
        // Create client control stream.
        let control_stream = neqo_trans_conn.stream_create(StreamType::UniDi).unwrap();
        // Send a GOAWAY frame instead.
        let sent = neqo_trans_conn.stream_send(control_stream, &[0x0, 0x7, 0x1, 0x5]);
        assert_eq!(sent, Ok(4));
        let out = neqo_trans_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpMissingSettings);
    }

    // Server: receiving SETTINGS frame twice causes connection close
    // with error HTTP_UNEXPECTED_FRAME.
    #[test]
    fn server_receive_settings_twice() {
        let (mut hconn, mut peer_conn) = connect();
        // send the second SETTINGS frame.
        peer_conn.control_send(&[0x4, 0x6, 0x1, 0x40, 0x64, 0x7, 0x40, 0x64]);
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpFrameUnexpected);
    }

    fn priority_update_check_id(stream_id: StreamId, valid: bool) {
        let (mut hconn, mut peer_conn) = connect();
        // send a priority update
        let frame = HFrame::PriorityUpdateRequest {
            element_id: stream_id.as_u64(),
            priority: Priority::default(),
        };
        let mut e = Encoder::default();
        frame.encode(&mut e);
        peer_conn.control_send(e.as_ref());
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        // check if the given connection got closed on invalid stream ids
        if valid {
            assert_not_closed(&hconn);
        } else {
            assert_closed(&hconn, &Error::HttpId);
        }
    }

    #[test]
    fn priority_update_valid_id_0() {
        // Client-Initiated, Bidirectional
        priority_update_check_id(StreamId::new(0), true);
    }
    #[test]
    fn priority_update_invalid_id_1() {
        // Server-Initiated, Bidirectional
        priority_update_check_id(StreamId::new(1), false);
    }
    #[test]
    fn priority_update_invalid_id_2() {
        // Client-Initiated, Unidirectional
        priority_update_check_id(StreamId::new(2), false);
    }
    #[test]
    fn priority_update_invalid_id_3() {
        // Server-Initiated, Unidirectional
        priority_update_check_id(StreamId::new(3), false);
    }

    #[test]
    fn priority_update_invalid_large_id() {
        // Server-Initiated, Unidirectional (divisible by 4)
        priority_update_check_id(StreamId::new(1_000_000_000), false);
    }

    fn test_wrong_frame_on_control_stream(v: &[u8]) {
        let (mut hconn, mut peer_conn) = connect();

        // receive a frame that is not allowed on the control stream.
        peer_conn.control_send(v);

        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpFrameUnexpected);
    }

    // send DATA frame on a control stream
    #[test]
    fn server_data_frame_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x0, 0x2, 0x1, 0x2]);
    }

    // send HEADERS frame on a control stream
    #[test]
    fn server_headers_frame_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x1, 0x2, 0x1, 0x2]);
    }

    // Server push is not supported. A server that does not push has nothing to do with CANCEL_PUSH,
    // so it is unexpected.
    #[test]
    fn server_cancel_push_frame_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x03, 0x01, 0x05]);
    }

    // Server: a server that does not push ignores MAX_PUSH_ID (RFC 9114, Section 7.2.7) rather than
    // treating it as an error.
    #[test]
    fn server_max_push_id_frame_ignored() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn.control_send(&[0x0d, 0x01, 0x05]);
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_not_closed(&hconn);
    }

    // Server: a client GOAWAY carries a push ID (RFC 9114, Section 7.2.6).
    #[test]
    fn server_goaway_frame_accepted() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn.control_send(&[0x7, 0x1, 0x1]);
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_not_closed(&hconn);
    }

    #[test]
    fn server_goaway_push_id_not_increased() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn.control_send(&[0x7, 0x1, 0x2, 0x7, 0x1, 0x2, 0x7, 0x1, 0x1]);
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_not_closed(&hconn);
    }

    #[test]
    fn server_goaway_push_id_increased() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn.control_send(&[0x7, 0x1, 0x1, 0x7, 0x1, 0x2]);
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpId);
    }

    // Server: receive unknown stream type
    // also test getting stream id that does not fit into a single byte.
    #[test]
    fn server_received_unknown_stream() {
        let (mut hconn, mut peer_conn) = connect();

        // create a stream with unknown type.
        let new_stream_id = peer_conn.stream_create(StreamType::UniDi).unwrap();
        _ = peer_conn
            .stream_send(new_stream_id, &[0x41, 0x19, 0x4, 0x4, 0x6, 0x0, 0x8, 0x0])
            .unwrap();
        let out = peer_conn.process_output(now());
        let out = hconn.process(out.dgram(), now());
        drop(peer_conn.process(out.dgram(), now()));
        let out = hconn.process_output(now());
        drop(peer_conn.process(out.dgram(), now()));

        // check for stop-sending with Error::HttpStreamCreation.
        let mut stop_sending_event_found = false;
        while let Some(e) = peer_conn.next_event() {
            if let ConnectionEvent::SendStreamStopSending {
                stream_id,
                app_error,
            } = e
            {
                stop_sending_event_found = true;
                assert_eq!(stream_id, new_stream_id);
                assert_eq!(app_error, Error::HttpStreamCreation.code());
            }
        }
        assert!(stop_sending_event_found);
        assert_not_closed(&hconn);
    }

    // Server: a push stream (unidirectional type 0x1) is H3_STREAM_CREATION_ERROR
    // (RFC 9114, Section 6.2.2); a server must never receive one.
    #[test]
    fn server_received_push_stream() {
        let (mut hconn, mut peer_conn) = connect();
        let push_stream = peer_conn.stream_create(StreamType::UniDi).unwrap();
        _ = peer_conn.stream_send(push_stream, &[0x01]).unwrap();
        let out = peer_conn.process_output(now());
        drop(hconn.process(out.dgram(), now()));
        assert_closed(&hconn, &Error::HttpStreamCreation);
    }

    /// Test reading of a slowly streamed frame. bytes are received one by one
    #[test]
    fn server_frame_reading() {
        let (mut hconn, mut peer_conn, _token) = connect_and_receive_settings();

        // create a control stream.
        let control_stream = peer_conn.stream_create(StreamType::UniDi).unwrap();

        // send the stream type
        let mut sent = peer_conn.stream_send(control_stream, &[0x0]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        // start sending SETTINGS frame
        sent = peer_conn.stream_send(control_stream, &[0x4]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        sent = peer_conn.stream_send(control_stream, &[0x4]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        sent = peer_conn.stream_send(control_stream, &[0x6]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        sent = peer_conn.stream_send(control_stream, &[0x0]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        sent = peer_conn.stream_send(control_stream, &[0x8]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        sent = peer_conn.stream_send(control_stream, &[0x0]);
        assert_eq!(sent, Ok(1));
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        assert_not_closed(&hconn);
    }

    // Test reading of a slowly streamed frame. bytes are received one by one
    fn test_incomplete_frame(res: &[u8]) {
        let (mut hconn, mut peer_conn, _token) = connect_and_receive_settings();

        // send an incomplete request.
        let stream_id = peer_conn.stream_create(StreamType::BiDi).unwrap();
        peer_conn.stream_send(stream_id, res).unwrap();
        peer_conn.stream_close_send(stream_id).unwrap();

        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        assert_closed(&hconn, &Error::HttpFrame);
    }

    const REQUEST_WITH_BODY: &[u8] = &[
        // headers
        0x01, 0x10, 0x00, 0x00, 0xd1, 0xd7, 0x50, 0x89, 0x41, 0xe9, 0x2a, 0x67, 0x35, 0x53, 0x2e,
        0x43, 0xd3, 0xc1, // the first data frame.
        0x0, 0x3, 0x61, 0x62, 0x63, // the second data frame.
        0x0, 0x3, 0x64, 0x65, 0x66,
    ];
    const REQUEST_BODY: &[u8] = &[0x61, 0x62, 0x63, 0x64, 0x65, 0x66];

    const RESPONSE_BODY: &[u8] = &[0x67, 0x68, 0x69];

    // HEADERS frame carrying only `:method: GET`.
    const REQUEST_METHOD_ONLY: &[u8] = &[0x01, 0x03, 0x00, 0x00, 0xd1];

    fn check_request_header(header: &[Header]) {
        let expected_request_header = &[
            Header::new(":method", "GET"),
            Header::new(":scheme", "https"),
            Header::new(":authority", "something.com"),
            Header::new(":path", "/"),
        ];
        assert_eq!(header, expected_request_header);
    }

    // Incomplete DATA frame
    #[test]
    fn server_incomplete_data_frame() {
        test_incomplete_frame(&REQUEST_WITH_BODY[..22]);
    }

    // Incomplete HEADERS frame
    #[test]
    fn server_incomplete_headers_frame() {
        test_incomplete_frame(&REQUEST_WITH_BODY[..10]);
    }

    #[test]
    fn server_incomplete_unknown_frame() {
        test_incomplete_frame(&[0x21]);
    }

    #[test]
    fn server_request_with_body() {
        let (mut hconn, mut peer_conn) = connect();

        let stream_id = peer_conn.stream_create(StreamType::BiDi).unwrap();
        peer_conn.stream_send(stream_id, REQUEST_WITH_BODY).unwrap();
        peer_conn.stream_close_send(stream_id).unwrap();

        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        // Check connection event. There should be 1 Header and 2 data events.
        let mut headers_frames = 0;
        let mut data_received = 0;
        while let Some(event) = hconn.next_event() {
            match event {
                Http3ServerEvent::Headers { headers, fin, .. } => {
                    check_request_header(&headers);
                    assert!(!fin);
                    headers_frames += 1;
                }
                Http3ServerEvent::Data { stream, data, fin } => {
                    assert_eq!(data, REQUEST_BODY);
                    assert!(fin);
                    stream
                        .send_headers(&[
                            Header::new(":status", "200"),
                            Header::new("content-length", "3"),
                        ])
                        .unwrap();
                    stream.send_data(RESPONSE_BODY, now()).unwrap();
                    data_received += 1;
                }
                Http3ServerEvent::DataWritable { .. }
                | Http3ServerEvent::StreamReset { .. }
                | Http3ServerEvent::StreamStopSending { .. }
                | Http3ServerEvent::StateChange { .. }
                | Http3ServerEvent::PriorityUpdate { .. }
                | Http3ServerEvent::WebTransport(_)
                | Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. }
                | Http3ServerEvent::ConnectUdp(_) => {}
            }
        }
        assert_eq!(headers_frames, 1);
        assert_eq!(data_received, 1);
    }

    #[test]
    fn server_malformed_request_reset() {
        let (mut hconn, mut peer_conn) = connect();

        let stream_id = peer_conn.stream_create(StreamType::BiDi).unwrap();
        peer_conn
            .stream_send(stream_id, REQUEST_METHOD_ONLY)
            .unwrap();
        let out = peer_conn.process_output(now());
        let out = hconn.process(out.dgram(), now());
        drop(peer_conn.process(out.dgram(), now()));

        let app_error = Error::HttpMessage.code();
        let events = peer_conn.events().collect::<Vec<_>>();
        assert!(events.contains(&ConnectionEvent::SendStreamStopSending {
            stream_id,
            app_error
        }));
        assert!(events.contains(&ConnectionEvent::RecvStreamReset {
            stream_id,
            app_error
        }));
        assert!(hconn.events().any(|e| matches!(e,
            Http3ServerEvent::StreamStopSending { stream, error }
                if stream.stream_id() == stream_id && error == app_error)));
    }

    #[test]
    fn server_request_with_body_send_stop_sending() {
        let (mut hconn, mut peer_conn) = connect();

        let stream_id = peer_conn.stream_create(StreamType::BiDi).unwrap();
        // Send only request headers for now.
        peer_conn
            .stream_send(stream_id, &REQUEST_WITH_BODY[..20])
            .unwrap();

        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        // Check connection event. There should be 1 Header and no data events.
        let mut headers_frames = 0;
        while let Some(event) = hconn.next_event() {
            match event {
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin,
                } => {
                    check_request_header(&headers);
                    assert!(!fin);
                    headers_frames += 1;
                    stream.stream_stop_sending(Error::HttpNone.code()).unwrap();
                    stream
                        .send_headers(&[
                            Header::new(":status", "200"),
                            Header::new("content-length", "3"),
                        ])
                        .unwrap();
                    stream.send_data(RESPONSE_BODY, now()).unwrap();
                }
                Http3ServerEvent::Data { .. } => {
                    panic!("We should not have a Data event");
                }
                Http3ServerEvent::DataWritable { .. }
                | Http3ServerEvent::StreamReset { .. }
                | Http3ServerEvent::StreamStopSending { .. }
                | Http3ServerEvent::StateChange { .. }
                | Http3ServerEvent::PriorityUpdate { .. }
                | Http3ServerEvent::WebTransport(_)
                | Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. }
                | Http3ServerEvent::ConnectUdp(_) => {}
            }
        }
        let out = hconn.process_output(now());

        // Send data.
        peer_conn
            .stream_send(stream_id, &REQUEST_WITH_BODY[20..])
            .unwrap();
        peer_conn.stream_close_send(stream_id).unwrap();

        let out = peer_conn.process(out.dgram(), now());
        hconn.process(out.dgram(), now());

        while let Some(event) = hconn.next_event() {
            match event {
                Http3ServerEvent::Headers { .. } => {
                    panic!("We should not have a Header event");
                }
                Http3ServerEvent::Data { .. } => {
                    panic!("We should not have a Data event");
                }
                Http3ServerEvent::DataWritable { .. }
                | Http3ServerEvent::StreamReset { .. }
                | Http3ServerEvent::StreamStopSending { .. }
                | Http3ServerEvent::StateChange { .. }
                | Http3ServerEvent::PriorityUpdate { .. }
                | Http3ServerEvent::WebTransport(_)
                | Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. }
                | Http3ServerEvent::ConnectUdp(_) => {}
            }
        }
        assert_eq!(headers_frames, 1);
    }

    #[test]
    fn server_request_with_body_server_reset() {
        let (mut hconn, mut peer_conn) = connect();

        let request_stream_id = peer_conn.stream_create(StreamType::BiDi).unwrap();
        // Send only request headers for now.
        peer_conn
            .stream_send(request_stream_id, &REQUEST_WITH_BODY[..20])
            .unwrap();

        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        // Check connection event. There should be 1 Header and no data events.
        // The server will reset the stream.
        let mut headers_frames = 0;
        while let Some(event) = hconn.next_event() {
            match event {
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin,
                } => {
                    check_request_header(&headers);
                    assert!(!fin);
                    headers_frames += 1;
                    stream
                        .cancel_fetch(Error::HttpRequestRejected.code())
                        .unwrap();
                }
                Http3ServerEvent::Data { .. } => {
                    panic!("We should not have a Data event");
                }
                Http3ServerEvent::DataWritable { .. }
                | Http3ServerEvent::StreamReset { .. }
                | Http3ServerEvent::StreamStopSending { .. }
                | Http3ServerEvent::StateChange { .. }
                | Http3ServerEvent::PriorityUpdate { .. }
                | Http3ServerEvent::WebTransport(_)
                | Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. }
                | Http3ServerEvent::ConnectUdp(_) => {}
            }
        }
        let out = hconn.process_output(now());

        let out = peer_conn.process(out.dgram(), now());
        hconn.process(out.dgram(), now());

        // Check that STOP_SENDING and REET has been received.
        let mut reset = 0;
        let mut stop_sending = 0;
        while let Some(event) = peer_conn.next_event() {
            match event {
                ConnectionEvent::RecvStreamReset { stream_id, .. } => {
                    assert_eq!(request_stream_id, stream_id);
                    reset += 1;
                }
                ConnectionEvent::SendStreamStopSending { stream_id, .. } => {
                    assert_eq!(request_stream_id, stream_id);
                    stop_sending += 1;
                }
                _ => {}
            }
        }
        assert_eq!(headers_frames, 1);
        assert_eq!(reset, 1);
        assert_eq!(stop_sending, 1);
    }

    // Server: Test that the connection will be closed if the local control stream
    // has been reset.
    #[test]
    fn server_reset_control_stream() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn
            .stream_reset_send(CLIENT_SIDE_CONTROL_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    // Server: Test that the connection will be closed if the client side encoder stream
    // has been reset.
    #[test]
    fn server_reset_client_side_encoder_stream() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn
            .stream_reset_send(CLIENT_SIDE_ENCODER_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    // Server: Test that the connection will be closed if the client side decoder stream
    // has been reset.
    #[test]
    fn server_reset_client_side_decoder_stream() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn
            .stream_reset_send(CLIENT_SIDE_DECODER_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    // Server: Test that the connection will be closed if the local control stream
    // has received a stop_sending.
    #[test]
    fn client_stop_sending_control_stream() {
        let (mut hconn, mut peer_conn) = connect();

        peer_conn
            .stream_stop_sending(SERVER_SIDE_CONTROL_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    // Server: Test that the connection will be closed if the server side encoder stream
    // has received a stop_sending.
    #[test]
    fn server_stop_sending_encoder_stream() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn
            .stream_stop_sending(SERVER_SIDE_ENCODER_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    // Server: Test that the connection will be closed if the server side decoder stream
    // has received a stop_sending.
    #[test]
    fn server_stop_sending_decoder_stream() {
        let (mut hconn, mut peer_conn) = connect();
        peer_conn
            .stream_stop_sending(SERVER_SIDE_DECODER_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());
        assert_closed(&hconn, &Error::HttpClosedCriticalStream);
    }

    /// Perform a handshake, then another with the token from the first.
    /// The second should always resume, but it might not always accept early data.
    fn zero_rtt_with_settings(conn_params: Http3Parameters, zero_rtt: ZeroRttState) {
        let (_, _, token) = connect_with_token();

        let mut server = create_server(conn_params);
        let mut client = default_client();
        client.enable_resumption(now(), token).unwrap();

        connect_transport(&mut server, &mut client, true);
        assert!(client.tls_info().unwrap().resumed());
        assert_eq!(client.zero_rtt_state(), zero_rtt);
    }

    #[test]
    fn zero_rtt() {
        zero_rtt_with_settings(http3params(qpack_defaults()), ZeroRttState::AcceptedClient);
    }

    /// A larger QPACK decoder table size isn't an impediment to 0-RTT.
    #[test]
    fn zero_rtt_larger_decoder_table() {
        let qpack_dflt = qpack_defaults();
        zero_rtt_with_settings(
            http3params(
                qpack_dflt.max_table_size_decoder(qpack_dflt.get_max_table_size_decoder() + 1),
            ),
            ZeroRttState::AcceptedClient,
        );
    }

    /// A smaller QPACK decoder table size prevents 0-RTT.
    #[test]
    fn zero_rtt_smaller_decoder_table() {
        let qpack_dflt = qpack_defaults();
        zero_rtt_with_settings(
            http3params(
                qpack_dflt.max_table_size_decoder(qpack_dflt.get_max_table_size_decoder() - 1),
            ),
            ZeroRttState::Rejected,
        );
    }

    /// More blocked streams does not prevent 0-RTT.
    #[test]
    fn zero_rtt_more_blocked_streams() {
        let qpack_dflt = qpack_defaults();
        zero_rtt_with_settings(
            http3params(qpack_dflt.max_blocked_streams(qpack_dflt.get_max_blocked_streams() + 1)),
            ZeroRttState::AcceptedClient,
        );
    }

    /// A lower number of blocked streams also prevents 0-RTT.
    #[test]
    fn zero_rtt_fewer_blocked_streams() {
        let qpack_dflt = qpack_defaults();
        zero_rtt_with_settings(
            http3params(qpack_dflt.max_blocked_streams(qpack_dflt.get_max_blocked_streams() - 1)),
            ZeroRttState::Rejected,
        );
    }

    /// The size of the encoder table is local and therefore doesn't prevent 0-RTT.
    #[test]
    fn zero_rtt_smaller_encoder_table() {
        let qpack_dflt = qpack_defaults();
        zero_rtt_with_settings(
            http3params(
                qpack_dflt.max_table_size_encoder(qpack_dflt.get_max_table_size_encoder() - 1),
            ),
            ZeroRttState::AcceptedClient,
        );
    }

    #[test]
    fn client_request_hash() {
        let (mut hconn, mut peer_conn) = connect();

        let request_stream_id_1 = peer_conn.stream_create(StreamType::BiDi).unwrap();
        // Send only request headers for now.
        peer_conn
            .stream_send(request_stream_id_1, REQUEST_WITH_BODY)
            .unwrap();

        let request_stream_id_2 = peer_conn.stream_create(StreamType::BiDi).unwrap();
        // Send only request headers for now.
        peer_conn
            .stream_send(request_stream_id_2, REQUEST_WITH_BODY)
            .unwrap();

        let out = peer_conn.process_output(now());
        hconn.process(out.dgram(), now());

        let mut requests = HashMap::new();
        while let Some(event) = hconn.next_event() {
            match event {
                Http3ServerEvent::Headers { stream, .. } => {
                    assert!(!requests.contains_key(&stream.stream_id()));
                    requests.insert(stream.stream_id(), 0);
                }
                Http3ServerEvent::Data { stream, .. } => {
                    assert!(requests.contains_key(&stream.stream_id()));
                }
                Http3ServerEvent::DataWritable { .. }
                | Http3ServerEvent::StreamReset { .. }
                | Http3ServerEvent::StreamStopSending { .. }
                | Http3ServerEvent::StateChange { .. }
                | Http3ServerEvent::PriorityUpdate { .. }
                | Http3ServerEvent::WebTransport(_)
                | Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. }
                | Http3ServerEvent::ConnectUdp(_) => {}
            }
        }
        assert_eq!(requests.len(), 2);
    }

    #[derive(Debug, Default)]
    pub struct RejectZeroRtt {}
    impl ZeroRttChecker for RejectZeroRtt {
        fn check(&self, _token: &[u8]) -> ZeroRttCheckResult {
            ZeroRttCheckResult::Reject
        }
    }

    #[test]
    fn reject_zero_server() {
        fixture_init();
        let mut server = Http3Server::new(
            now(),
            DEFAULT_KEYS,
            DEFAULT_ALPN,
            anti_replay(),
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            http3params(qpack_defaults()),
            Some(Box::<RejectZeroRtt>::default()),
        )
        .expect("create a server");
        let (_, token) = connect_to(&mut server);

        let mut client = default_client();
        client.enable_resumption(now(), token).unwrap();

        connect_transport(&mut server, &mut client, true);
        assert!(client.tls_info().unwrap().resumed());
        assert_eq!(client.zero_rtt_state(), ZeroRttState::Rejected);
    }
}
