// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Display},
    num::NonZeroUsize,
    rc::Rc,
    slice,
    time::Instant,
};

use neqo_common::{Datagram, Header, header::HeadersExt as _};
use neqo_http3::{
    Http3OrWebTransportStream, Http3Parameters, Http3Server, Http3ServerEvent, Http3State,
};
use neqo_transport::{ConnectionIdGenerator, OutputBatch};
use nss::AntiReplay;
use rustc_hash::FxHashMap as HashMap;
use tracing::{debug, error};

use super::Args;
use crate::{
    now,
    send_data::{SendData, SendResult},
    server::StatsReporter,
};

pub struct HttpServer {
    server: Http3Server,
    /// Progress writing to each stream.
    remaining_data: HashMap<Http3OrWebTransportStream, SendData>,
    /// Tracks POST requests: (bytes received, optional response size from path)
    posts: HashMap<Http3OrWebTransportStream, (usize, Option<usize>)>,
    is_qns_test: bool,
    stats: StatsReporter,
}

impl HttpServer {
    /// Send a response on the given stream. If the stream was closed (e.g., by
    /// `STOP_SENDING`), this logs the error and returns gracefully per RFC 9000.
    fn send_response(
        &mut self,
        stream: &Http3OrWebTransportStream,
        mut response: SendData,
        now: Instant,
    ) {
        if stream
            .send_headers(&[
                Header::new(":status", "200"),
                Header::new("content-length", response.len().to_string()),
            ])
            .is_err()
        {
            error!("Stream {stream} closed by peer, not sending response");
            _ = stream.stream_reset_send(neqo_http3::Error::HttpNone.code());
            return;
        }
        if let Some(remaining) = Self::send_response_body(stream, &mut response, now) {
            self.remaining_data.insert(stream.clone(), remaining);
        }
    }

    /// Send response body data. Returns `None` if done or stream closed,
    /// `Some(data)` if more data remains to send.
    fn send_response_body(
        stream: &Http3OrWebTransportStream,
        response: &mut SendData,
        now: Instant,
    ) -> Option<SendData> {
        match response.send(|chunk| stream.send_data(chunk, now)) {
            SendResult::StreamClosed => {
                error!("Stream {stream} closed");
                _ = stream.stream_reset_send(neqo_http3::Error::HttpNone.code());
                None
            }
            SendResult::Done => {
                _ = stream.stream_close_send(now); // Stream may be closed; ignore errors.
                None
            }
            SendResult::MoreData => Some(std::mem::take(response)),
        }
    }

    fn post_response(received: usize, response_size: Option<usize>) -> SendData {
        response_size.map_or_else(
            || SendData::from(received.to_string().into_bytes()),
            SendData::zeroes,
        )
    }

    pub fn new(
        args: &Args,
        anti_replay: AntiReplay,
        cid_mgr: Rc<RefCell<dyn ConnectionIdGenerator>>,
    ) -> Self {
        let mut server = Http3Server::new(
            args.now(),
            slice::from_ref(&args.key),
            slice::from_ref(&args.shared.alpn),
            anti_replay,
            cid_mgr,
            Http3Parameters::default()
                .connection_parameters(args.shared.quic_parameters.get(&args.shared.alpn))
                .max_table_size_encoder(args.shared.max_table_size_encoder)
                .max_table_size_decoder(args.shared.max_table_size_decoder)
                .max_blocked_streams(args.shared.max_blocked_streams),
            None,
        )
        .expect("We cannot make a server!");

        super::configure_server(&mut server, args);
        Self {
            server,
            remaining_data: HashMap::default(),
            posts: HashMap::default(),
            is_qns_test: args.shared.qns_test.is_some(),
            stats: StatsReporter::new(args.shared.stats_enabled(), args.shared.stats_file.clone()),
        }
    }
}

impl Display for HttpServer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.server.fmt(f)
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

    fn process_events(&mut self, _now: Instant) {
        let now = now();
        while let Some(event) = self.server.next_event() {
            match event {
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin,
                } => {
                    debug!("Headers (request={stream} fin={fin}): {headers:?}");

                    if headers.contains_header(":method", b"POST") {
                        let response_size = headers.find_header(":path").and_then(|path| {
                            path.value_utf8()
                                .ok()?
                                .trim_matches('/')
                                .parse::<usize>()
                                .ok()
                        });
                        if fin {
                            self.send_response(&stream, Self::post_response(0, response_size), now);
                        } else {
                            self.posts.insert(stream, (0, response_size));
                        }
                        continue;
                    }

                    let Some(path) = headers.find_header(":path") else {
                        _ = stream.cancel_fetch(neqo_http3::Error::HttpRequestIncomplete.code()); // Stream may be closed; ignore errors.
                        continue;
                    };

                    let response = if self.is_qns_test {
                        let path_str = path.value_utf8().unwrap_or("/");
                        let Ok(data) = super::response_for_path(path_str, true) else {
                            // Stream may be closed; ignore errors.
                            if stream
                                .send_headers(&[Header::new(":status", "404")])
                                .is_ok()
                            {
                                _ = stream.stream_close_send(now);
                            } else {
                                _ = stream.stream_reset_send(neqo_http3::Error::HttpNone.code());
                            }
                            continue;
                        };
                        data
                    } else {
                        // Non-QNS: path is a byte count; fall back to raw bytes for
                        // non-numeric or non-UTF-8 paths.
                        path.value_utf8()
                            .ok()
                            .and_then(|s| {
                                s.trim_matches('/')
                                    .parse::<usize>()
                                    .ok()
                                    .map(SendData::zeroes)
                            })
                            .unwrap_or_else(|| SendData::from(path.value()))
                    };

                    self.send_response(&stream, response, now);
                }
                Http3ServerEvent::DataWritable { stream } => {
                    if self.posts.get_mut(&stream).is_none()
                        && let Some(mut remaining) = self.remaining_data.remove(&stream)
                        && let Some(data) = Self::send_response_body(&stream, &mut remaining, now)
                    {
                        self.remaining_data.insert(stream, data);
                    }
                }

                Http3ServerEvent::Data { stream, data, fin } => {
                    if let Some((received, _)) = self.posts.get_mut(&stream) {
                        *received += data.len();
                    }
                    if fin && let Some((received, response_size)) = self.posts.remove(&stream) {
                        self.send_response(
                            &stream,
                            Self::post_response(received, response_size),
                            now,
                        );
                    }
                }
                Http3ServerEvent::StreamReset { stream, .. } => {
                    self.posts.remove(&stream);
                }
                Http3ServerEvent::StreamStopSending { stream, .. } => {
                    self.remaining_data.remove(&stream);
                }
                // `Closing` fires immediately; `Closed` only after the drain timeout.
                Http3ServerEvent::StateChange {
                    conn,
                    state: Http3State::Closing(_),
                } => self.stats.report(&conn.connection()),
                _ => {}
            }
        }
        self.posts
            .retain(|stream, _| !stream.conn.borrow().state().closed());
        self.remaining_data
            .retain(|stream, _| !stream.conn.borrow().state().closed());
    }

    fn has_events(&self) -> bool {
        self.server.has_events()
    }
}

#[cfg(test)]
mod tests {
    use std::{cell::RefCell, rc::Rc};

    use neqo_common::event::Provider as _;
    use neqo_http3::{Error, Http3Client, Http3ClientEvent, Priority, StreamId};
    use neqo_transport::RandomConnectionIdGenerator;
    use test_fixture::{
        ProcessServer, anti_replay, connect_peers, default_http3_client, exchange_packets,
        fixture_init, now,
    };

    use super::{Args, HttpServer};
    use crate::server::{
        HttpServer as _,
        test_support::{StatsServer, blocked_path, reported_on_close},
    };

    fn make_server(args: &Args) -> HttpServer {
        fixture_init();
        HttpServer::new(
            args,
            anti_replay(),
            Rc::new(RefCell::new(RandomConnectionIdGenerator::new(10))),
        )
    }

    impl StatsServer for HttpServer {
        fn transport(&mut self) -> &mut dyn ProcessServer {
            &mut self.server
        }
    }

    fn connect(server: &mut HttpServer) -> Http3Client {
        let mut client = default_http3_client();
        let out = connect_peers(&mut client, &mut server.server);
        exchange_packets(&mut client, &mut server.server, false, out);
        client
    }

    fn setup() -> (HttpServer, Http3Client) {
        let mut server = make_server(&Args::default());
        let client = connect(&mut server);
        (server, client)
    }

    fn fetch(client: &mut Http3Client, method: &str, path: &str) -> StreamId {
        let target = ("https", "something.com", path);
        client
            .fetch(now(), method, &target, &[], Priority::default())
            .unwrap()
    }

    fn deliver(server: &mut HttpServer, client: &mut Http3Client) {
        exchange_packets(client, &mut server.server, false, None);
        server.process_events(now());
    }

    fn entries(server: &HttpServer) -> (usize, usize) {
        (server.posts.len(), server.remaining_data.len())
    }

    #[test]
    fn reports_stats_once_on_close() {
        assert_eq!(reported_on_close(make_server), 1);
    }

    #[test]
    fn response_state_per_connection() {
        let mut server = make_server(&Args::default());
        let mut clients = [connect(&mut server), connect(&mut server)];
        for client in &mut clients {
            fetch(client, "GET", &blocked_path());
            deliver(&mut server, client);
        }
        assert_eq!(entries(&server), (0, clients.len()));
    }

    #[test]
    fn empty_post_gets_response() {
        let (mut server, mut client) = setup();
        let stream_id = fetch(&mut client, "POST", "/");
        client.stream_close_send(stream_id, now()).unwrap();
        deliver(&mut server, &mut client);
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
        let response =
            |e| matches!(e, Http3ClientEvent::HeaderReady { stream_id: s, .. } if s == stream_id);
        assert!(client.events().any(response));
    }

    #[test]
    fn reset_post_is_dropped() {
        let (mut server, mut client) = setup();
        let stream_id = fetch(&mut client, "POST", "/");
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (1, 0));
        client
            .stream_reset_send(stream_id, Error::HttpRequestCancelled.code())
            .unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }

    #[test]
    fn stop_sending_drops_response() {
        let (mut server, mut client) = setup();
        let stream_id = fetch(&mut client, "GET", &blocked_path());
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 1));
        client
            .stream_stop_sending(stream_id, Error::HttpRequestCancelled.code())
            .unwrap();
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }

    #[test]
    fn closed_connection_drops_state() {
        let (mut server, mut client) = setup();
        fetch(&mut client, "POST", "/");
        fetch(&mut client, "GET", &blocked_path());
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (1, 1));
        client.close(now(), 0, "bye");
        deliver(&mut server, &mut client);
        assert_eq!(entries(&server), (0, 0));
    }
}
