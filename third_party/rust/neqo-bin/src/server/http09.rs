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
    slice,
    time::Instant,
};

use neqo_common::{Datagram, event::Provider as _};
use neqo_http3::Error;
use neqo_transport::{
    ConnectionEvent, ConnectionIdGenerator, OutputBatch, State, StreamId,
    server::{ConnectionRef, Server},
};
use nss::{AllowZeroRtt, AntiReplay};
use rustc_hash::FxHashMap as HashMap;
use tracing::{debug, warn};

use super::Args;
use crate::{
    STREAM_IO_BUFFER_SIZE,
    send_data::{SendData, SendResult},
    server::StatsReporter,
};

/// Create only in `NewStream`; `or_default()` elsewhere re-creates state dropped on `STOP_SENDING`.
#[derive(Default)]
struct HttpStreamState {
    writable: bool,
    data_to_send: Option<SendData>,
}

pub struct HttpServer {
    server: Server,
    write_state: HashMap<(ConnectionRef, StreamId), HttpStreamState>,
    read_state: HashMap<(ConnectionRef, StreamId), Vec<u8>>,
    is_qns_test: bool,
    read_buffer: Vec<u8>,
    stats: StatsReporter,
}

impl HttpServer {
    pub fn new(
        args: &Args,
        anti_replay: AntiReplay,
        cid_manager: Rc<RefCell<dyn ConnectionIdGenerator>>,
    ) -> Result<Self, Error> {
        let mut server = Server::new(
            args.now(),
            slice::from_ref(&args.key),
            slice::from_ref(&args.shared.alpn),
            anti_replay,
            Box::new(AllowZeroRtt {}),
            cid_manager,
            args.shared.quic_parameters.get(&args.shared.alpn),
        )?;

        super::configure_server(&mut server, args);

        Ok(Self {
            server,
            write_state: HashMap::default(),
            read_state: HashMap::default(),
            is_qns_test: args.shared.qns_test.is_some(),
            read_buffer: vec![0; STREAM_IO_BUFFER_SIZE],
            stats: StatsReporter::new(args.shared.stats_enabled(), args.shared.stats_file.clone()),
        })
    }

    fn save_partial(&mut self, stream_id: StreamId, partial: Vec<u8>, conn: &ConnectionRef) {
        if partial.len() < 4096 {
            debug!("Saving partial URL: {}", String::from_utf8_lossy(&partial));
            self.read_state.insert((conn.clone(), stream_id), partial);
        } else {
            debug!(
                "Giving up on partial URL {}",
                String::from_utf8_lossy(&partial)
            );
            _ = conn.borrow_mut().stream_stop_sending(stream_id, 0); // Stream may be closed; ignore errors.
        }
    }

    /// Parse a complete HQ request buffer and return the path component.
    ///
    /// Returns `None` on non-UTF-8 input, missing `GET /` prefix, or a path
    /// that doesn't pass the filter for the current mode (QNS vs. non-QNS).
    fn parse_path(buf: &[u8], is_qns_test: bool) -> Option<&str> {
        let msg = str::from_utf8(buf).ok()?;
        msg.strip_prefix("GET /")
            .and_then(|s| s.lines().next())
            .filter(|p| {
                if is_qns_test {
                    !p.chars().any(char::is_whitespace)
                } else {
                    p.chars().all(|c| c.is_ascii_digit())
                }
            })
    }

    fn stream_readable(&mut self, stream_id: StreamId, conn: &ConnectionRef) {
        if !stream_id.is_client_initiated() || !stream_id.is_bidi() {
            debug!("Stream {stream_id} not client-initiated bidi, ignoring");
            return;
        }
        let (sz, fin) = conn
            .borrow_mut()
            .stream_recv(stream_id, &mut self.read_buffer)
            .expect("Read should succeed");

        // A zero-length read with no FIN is unexpected but harmless; leave any
        // buffered partial data untouched and wait for more.
        if sz == 0 && !fin {
            debug!("size 0 but !fin");
            return;
        }

        let key = (conn.clone(), stream_id);
        let mut buf = self.read_state.remove(&key).unwrap_or_default();
        buf.extend_from_slice(&self.read_buffer[..sz]);

        // HQ requests are terminated by stream FIN. Never process a request
        // before FIN: partial data could look like a valid truncated path,
        // causing the server to serve the wrong (or non-existent) file.
        if !fin {
            self.save_partial(stream_id, buf, conn);
            return;
        }

        // FIN is set: the request is complete. If no data was received (either
        // now or buffered from a prior read), there is nothing to serve.
        if buf.is_empty() {
            self.write_state.remove(&key);
            return;
        }

        // Non-UTF-8 or unrecognised format cannot be recovered by waiting for
        // more data; reset the stream so the client gets a clean signal.
        let Some(path) = Self::parse_path(&buf, self.is_qns_test) else {
            _ = conn.borrow_mut().stream_reset_send(stream_id, 0);
            self.write_state.remove(&key);
            return;
        };

        debug!("Path = '{path}'");
        let resp = super::response_for_path(path, self.is_qns_test)
            .unwrap_or_else(|()| b"404".as_slice().into());

        let Some(stream_state) = self.write_state.get_mut(&key) else {
            debug!("Stream {stream_id} no longer sendable, dropping request");
            return;
        };
        if stream_state.data_to_send.is_none() {
            stream_state.data_to_send = Some(resp);
        } else {
            debug!("Data already set, doing nothing");
        }
        let writable = stream_state.writable;
        if writable {
            self.stream_writable(stream_id, conn);
        }
    }

    fn stream_writable(&mut self, stream_id: StreamId, conn: &ConnectionRef) {
        let key = (conn.clone(), stream_id);
        let Some(stream_state) = self.write_state.get_mut(&key) else {
            warn!("Unknown stream {stream_id}, ignoring event");
            return;
        };

        stream_state.writable = true;
        let remove = if let Some(resp) = &mut stream_state.data_to_send {
            match resp.send(|chunk| conn.borrow_mut().stream_send(stream_id, chunk)) {
                SendResult::StreamClosed => {
                    warn!("Stream {stream_id} closed by peer, stopping send");
                    true
                }
                SendResult::Done => {
                    _ = conn.borrow_mut().stream_close_send(stream_id); // Stream may be closed; ignore errors.
                    true
                }
                SendResult::MoreData => {
                    stream_state.writable = false;
                    false
                }
            }
        } else {
            false
        };
        if remove {
            self.write_state.remove(&key);
        }
    }
}

impl super::HttpServer for HttpServer {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        dgrams: D,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        self.server.process_multiple(dgrams, now, max_datagrams)
    }

    fn process_events(&mut self, now: Instant) {
        #[expect(
            clippy::mutable_key_type,
            reason = "ActiveConnectionRef::Hash doesn't access any of the interior mutable types"
        )]
        let active_conns = self.server.active_connections();
        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to loop over active connections in an undefined order."
        )]
        for acr in active_conns {
            loop {
                let Some(event) = acr.borrow_mut().next_event() else {
                    break;
                };
                match event {
                    ConnectionEvent::NewStream { stream_id } => {
                        self.write_state
                            .entry((acr.clone(), stream_id))
                            .or_default();
                    }
                    ConnectionEvent::RecvStreamReadable { stream_id } => {
                        self.stream_readable(stream_id, &acr);
                    }
                    ConnectionEvent::SendStreamWritable { stream_id } => {
                        self.stream_writable(stream_id, &acr);
                    }
                    ConnectionEvent::RecvStreamReset { stream_id, .. } => {
                        let key = (acr.clone(), stream_id);
                        self.read_state.remove(&key);
                        self.write_state.remove(&key);
                    }
                    ConnectionEvent::SendStreamStopSending { stream_id, .. } => {
                        self.write_state.remove(&(acr.clone(), stream_id));
                    }
                    ConnectionEvent::StateChange(State::Connected) => {
                        acr.connection()
                            .borrow_mut()
                            .send_ticket(now, b"hi!")
                            .unwrap();
                    }
                    // Exactly one of these occurs; `Closed` is dropped before it is seen.
                    ConnectionEvent::StateChange(
                        State::Closing { .. } | State::Draining { .. },
                    ) => {
                        self.stats.report(&acr.connection());
                    }
                    ConnectionEvent::StateChange(_)
                    | ConnectionEvent::SendStreamCreatable { .. }
                    | ConnectionEvent::SendStreamComplete { .. }
                    | ConnectionEvent::PathMigrated { .. } => (),
                    e => warn!("unhandled event {e:?}"),
                }
            }
        }
        self.read_state
            .retain(|(conn, _), _| !conn.borrow().state().closed());
        self.write_state
            .retain(|(conn, _), _| !conn.borrow().state().closed());
    }

    fn has_events(&self) -> bool {
        self.server.has_active_connections()
    }
}

impl Display for HttpServer {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Http 0.9 server ")
    }
}

#[cfg(test)]
mod tests {
    use std::{cell::RefCell, rc::Rc};

    use neqo_transport::{Connection, RandomConnectionIdGenerator, StreamId, StreamType};
    use test_fixture::{ProcessServer, anti_replay, fixture_init, now};

    use super::{Args, HttpServer};
    use crate::server::test_support::{
        StatsServer, alpn_args, blocked_path, connect, deliver, reported_on_close,
    };

    fn make_server(args: &Args) -> HttpServer {
        fixture_init();
        HttpServer::new(
            args,
            anti_replay(),
            Rc::new(RefCell::new(RandomConnectionIdGenerator::new(10))),
        )
        .expect("build server")
    }

    impl StatsServer for HttpServer {
        fn transport(&mut self) -> &mut dyn ProcessServer {
            &mut self.server
        }
    }

    fn setup() -> (HttpServer, Connection) {
        let mut server = make_server(&alpn_args());
        let client = connect(&mut server);
        (server, client)
    }

    fn request(server: &mut HttpServer, client: &mut Connection, data: &str) -> StreamId {
        let stream_id = client.stream_create(StreamType::BiDi).unwrap();
        client.stream_send(stream_id, data.as_bytes()).unwrap();
        deliver(server, client);
        stream_id
    }

    fn entries(server: &HttpServer) -> (usize, usize) {
        (server.read_state.len(), server.write_state.len())
    }

    #[test]
    fn reports_stats_once_on_close() {
        assert_eq!(reported_on_close(make_server), 1);
    }

    #[test]
    fn request_state_per_connection() {
        let mut server = make_server(&alpn_args());
        let mut clients = [connect(&mut server), connect(&mut server)];
        for client in &mut clients {
            request(&mut server, client, "GET /1");
        }
        assert_eq!(entries(&server), (clients.len(), clients.len()));
    }

    #[test]
    fn reset_request_is_dropped() {
        let (mut server, mut client) = setup();
        let stream_id = request(&mut server, &mut client, "GET /1");
        assert_eq!(entries(&server), (1, 1));
        client.stream_reset_send(stream_id, 0).unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }

    #[test]
    fn stop_sending_drops_response() {
        let (mut server, mut client) = setup();
        let stream_id = request(&mut server, &mut client, &format!("GET {}", blocked_path()));
        client.stream_close_send(stream_id).unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 1));
        client.stream_stop_sending(stream_id, 0).unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }

    #[test]
    fn stop_sending_before_fin_drops_request() {
        let (mut server, mut client) = setup();
        let stream_id = request(&mut server, &mut client, "GET /1");
        client.stream_stop_sending(stream_id, 0).unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (1, 0));
        client.stream_close_send(stream_id).unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }

    #[test]
    fn closed_connection_drops_state() {
        let (mut server, mut client) = setup();
        request(&mut server, &mut client, "GET /1");
        assert_eq!(entries(&server), (1, 1));
        client.close(now(), 0, "bye");
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }

    #[test]
    fn readable_stream_closed_before_events() {
        let (mut server, mut client) = setup();
        let stream_id = client.stream_create(StreamType::BiDi).unwrap();
        client.stream_send(stream_id, b"GET /1").unwrap();
        let data = client.process_output(now()).dgram();
        client.close(now(), 0, "bye");
        _ = server.server.process(data, now());
        deliver(&mut server, &mut client);
    }

    // Issue 1 (FIN-only frame after buffered partial data) is exercised by
    // the QNS zerortt interop test end-to-end; unit testing it would require
    // a real neqo_transport::server::ConnectionRef.

    #[test]
    fn parse_path_valid() {
        assert_eq!(HttpServer::parse_path(b"GET /1000\n", false), Some("1000"));
        assert_eq!(HttpServer::parse_path(b"GET /42\r\n", false), Some("42"));
        assert_eq!(
            HttpServer::parse_path(b"GET /index.html\n", true),
            Some("index.html")
        );
    }

    #[test]
    fn parse_path_invalid() {
        // Non-UTF-8 input must return None so the caller can reset the stream.
        assert_eq!(HttpServer::parse_path(b"\xff\xfe", false), None);
        // Wrong verb.
        assert_eq!(HttpServer::parse_path(b"HEAD /1000\n", false), None);
        // Non-digit in non-QNS mode.
        assert_eq!(HttpServer::parse_path(b"GET /foo\n", false), None);
        // Whitespace in QNS path.
        assert_eq!(HttpServer::parse_path(b"GET /foo bar\n", true), None);
        // Empty buffer: a FIN-only frame with no prior buffered data.
        assert_eq!(HttpServer::parse_path(b"", false), None);
    }
}
