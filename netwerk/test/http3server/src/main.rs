// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use base64::prelude::*;
use neqo_bin::server::{HttpServer, Runner};
use neqo_common::Bytes;
use neqo_common::{event::Provider, Datagram, Header};
use nss_rs::{generate_ech_keys, init_db, AllowZeroRtt, AntiReplay};
use neqo_http3::{
    connect_udp::{ServerEvent as ConnectUdpServerEvent, ServerSession as ConnectUdpRequest},
    webtransport::{ServerEvent as WebTransportServerEvent, ServerSession as WebTransportRequest},
    Error, Http3OrWebTransportStream, Http3Parameters, Http3Server, Http3ServerEvent,
    SendGroupId, SessionAcceptAction, StreamId,
};
use neqo_transport::server::ConnectionRef;
use neqo_transport::{
    ConnectionEvent, ConnectionParameters, DatagramQueueOutcome, OutputBatch,
    RandomConnectionIdGenerator, StreamType,
};
use std::env;
use std::pin::Pin;
use std::task::{Context, Poll};
use tokio::io::AsyncWriteExt;
use tokio::io::ReadBuf;
use tokio::task::LocalSet;
use tracing::{debug, error, info, trace};

use std::cell::RefCell;
use std::io;
use std::num::NonZeroUsize;
use std::path::PathBuf;
use std::process::exit;
use std::rc::Rc;
use std::thread;
use std::time::{Duration, Instant};

use cfg_if::cfg_if;

cfg_if! {
    if #[cfg(not(target_os = "android"))] {
        use std::sync::mpsc::{channel, Receiver, TryRecvError};
        use http_body_util::{BodyExt, Full};
        use hyper::header::{HeaderName, HeaderValue};
        use hyper::Method;
        use hyper_util::client::legacy::Client;
    }
}

use std::cmp::min;
use std::collections::hash_map::DefaultHasher;
use std::collections::HashSet;
use std::collections::{HashMap, VecDeque};
use std::hash::{Hash, Hasher};
use std::net::SocketAddr;

const MAX_TABLE_SIZE: u64 = 65536;
const MAX_BLOCKED_STREAMS: u16 = 10;
const PROTOCOLS: &[&str] = &["h3"];
const ECH_CONFIG_ID: u8 = 7;
const ECH_PUBLIC_NAME: &str = "public.example";

const HTTP_RESPONSE_WITH_WRONG_FRAME: &[u8] = &[
    0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x37, // headers
    0x0, 0x3, 0x61, 0x62, 0x63, // the first data frame
    0x3, 0x1, 0x5, // a cancel push frame that is not allowed
];
struct Http3TestServer {
    server: Http3Server,
    // This a map from a post request to amount of data ithas been received on the request.
    // The respons will carry the amount of data received.
    posts: HashMap<Http3OrWebTransportStream, usize>,
    responses: HashMap<Http3OrWebTransportStream, Vec<u8>>,
    connections_to_close: HashMap<Instant, Vec<ConnectionRef>>,
    sessions_to_close: HashMap<Instant, Vec<WebTransportRequest>>,
    sessions_to_create_stream: Vec<(WebTransportRequest, StreamType, Option<Vec<u8>>)>,
    // Server-initiated bidi WebTransport sessions for which we create a stream
    // and then, in a later flight, send STOP_SENDING(0x100). Regression test for
    // bug 2043946.
    sessions_to_create_bidi_and_stop_sending: Vec<WebTransportRequest>,
    streams_to_stop_sending: HashMap<Instant, Vec<Http3OrWebTransportStream>>,
    webtransport_bidi_stream: HashSet<Http3OrWebTransportStream>,
    wt_unidi_conn_to_stream: HashMap<ConnectionRef, Http3OrWebTransportStream>,
    wt_unidi_echo_back: HashMap<Http3OrWebTransportStream, Http3OrWebTransportStream>,
    received_datagram: Option<Bytes>,
    // Sessions opened on /echo_datagrams, which echo every datagram back.
    // Keyed by session ID only, as ServerSession has no identity; the tests
    // using this run sequentially.
    echo_datagram_sessions: HashSet<StreamId>,
    // Sessions opened on /flood_datagrams/<count>/<size>, with the payload
    // and the number of datagrams still to send.
    datagram_floods: Vec<(WebTransportRequest, Vec<u8>, usize)>,
    // When true, server will stop processing datagrams after accepting 0-RTT,
    // simulating a stuck ZERORTT session that never transitions to CONNECTED.
    stuck_0rtt_mode: bool,
    stuck_0rtt_activated: bool,
}

impl ::std::fmt::Display for Http3TestServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "{}", self.server)
    }
}
impl Http3TestServer {
    pub fn new(server: Http3Server) -> Self {
        Self {
            server,
            posts: HashMap::new(),
            responses: HashMap::new(),
            connections_to_close: HashMap::new(),
            sessions_to_close: HashMap::new(),
            sessions_to_create_stream: Vec::new(),
            sessions_to_create_bidi_and_stop_sending: Vec::new(),
            streams_to_stop_sending: HashMap::new(),
            webtransport_bidi_stream: HashSet::new(),
            wt_unidi_conn_to_stream: HashMap::new(),
            wt_unidi_echo_back: HashMap::new(),
            received_datagram: None,
            echo_datagram_sessions: HashSet::new(),
            datagram_floods: Vec::new(),
            stuck_0rtt_mode: false,
            stuck_0rtt_activated: false,
        }
    }

    fn respond_to_post(
        &mut self,
        stream: Http3OrWebTransportStream,
        received_len: usize,
        now: Instant,
    ) {
        let default_ret = b"Hello World".to_vec();
        stream
            .send_headers(&[
                Header::new(":status", "200"),
                Header::new("cache-control", "no-cache"),
                Header::new("x-data-received-length", received_len.to_string()),
                Header::new("content-length", default_ret.len().to_string()),
            ])
            .unwrap();
        self.new_response(stream, default_ret, now);
    }

    fn new_response(&mut self, stream: Http3OrWebTransportStream, mut data: Vec<u8>, now: Instant) {
        if data.len() == 0 {
            let _ = stream.stream_close_send(now);
            return;
        }
        match stream.send_data(&data, now) {
            Ok(sent) => {
                if sent < data.len() {
                    self.responses.insert(stream, data.split_off(sent));
                } else {
                    let _ = stream.stream_close_send(now);
                }
            }
            Err(e) => {
                eprintln!("error is {:?}", e);
            }
        }
    }

    fn handle_stream_writable(&mut self, stream: Http3OrWebTransportStream, now: Instant) {
        if let Some(data) = self.responses.get_mut(&stream) {
            match stream.send_data(&data, now) {
                Ok(sent) => {
                    if sent < data.len() {
                        let new_d = (*data).split_off(sent);
                        *data = new_d;
                    } else {
                        stream.stream_close_send(now).unwrap();
                        self.responses.remove(&stream);
                    }
                }
                Err(_) => {
                    eprintln!("Unexpected error");
                }
            }
        }
    }

    fn maybe_close_session(&mut self, now: Instant) {
        for (expires, sessions) in self.sessions_to_close.iter_mut() {
            if *expires <= now {
                for s in sessions.iter_mut() {
                    drop(s.close_session(0, "", now));
                }
            }
        }
        self.sessions_to_close.retain(|expires, _| *expires >= now);
    }

    fn maybe_close_connection(&mut self) {
        let now = Instant::now();
        for (expires, connections) in self.connections_to_close.iter_mut() {
            if *expires <= now {
                for c in connections.iter_mut() {
                    c.borrow_mut().close(now, 0x0100, "");
                }
            }
        }
        self.connections_to_close
            .retain(|expires, _| *expires >= now);
    }

    fn maybe_create_wt_stream(&mut self, now: Instant) {
        while let Some(tuple) = self.sessions_to_create_stream.pop() {
            let session = tuple.0;
            let wt_server_stream = session.create_stream(tuple.1).unwrap();
            if tuple.1 == StreamType::UniDi {
                if let Some(data) = tuple.2 {
                    self.new_response(wt_server_stream, data, now);
                } else {
                    self.wt_unidi_conn_to_stream
                        .insert(wt_server_stream.conn.clone(), wt_server_stream);
                }
            } else {
                if let Some(data) = tuple.2 {
                    self.new_response(wt_server_stream, data, now);
                } else {
                    self.webtransport_bidi_stream.insert(wt_server_stream);
                }
            }
        }
    }

    // Sends until the session's outgoing datagram queue reports backpressure;
    // OutgoingDatagramSpaceAvailable brings us back here to continue.
    fn maybe_flood_datagrams(&mut self, now: Instant) {
        self.datagram_floods.retain_mut(|(session, data, remaining)| {
            while *remaining > 0 {
                match session.send_datagram(data, None, now, SendGroupId::new(0), 0) {
                    Ok(DatagramQueueOutcome::Ok) => *remaining -= 1,
                    Ok(DatagramQueueOutcome::Rejected) => break,
                    Ok(_) => {
                        *remaining -= 1;
                        break;
                    }
                    Err(_) => return false,
                }
            }
            *remaining > 0
        });
    }

    // Regression test for bug 2043946: open a server-initiated bidi WebTransport
    // stream, send a byte so the client registers it in mStreamIdHash, then
    // schedule a STOP_SENDING(0x100) for a later flight.
    fn maybe_create_wt_stream_and_stop_sending(&mut self, now: Instant) {
        if self.sessions_to_create_bidi_and_stop_sending.is_empty() {
            return;
        }
        let session = self
            .sessions_to_create_bidi_and_stop_sending
            .pop()
            .unwrap();
        let wt_server_stream = session.create_stream(StreamType::BiDi).unwrap();
        let _ = wt_server_stream.send_data(b"h", now);
        // The STOP_SENDING must arrive after the client has processed the
        // NewStream event, so defer it to a separate flight.
        let expires = now + Duration::from_millis(200);
        self.streams_to_stop_sending
            .entry(expires)
            .or_insert_with(Vec::new)
            .push(wt_server_stream);
    }

    fn maybe_stop_sending(&mut self, now: Instant) {
        for (expires, streams) in self.streams_to_stop_sending.iter_mut() {
            if *expires <= now {
                for s in streams.iter_mut() {
                    let _ = s.stream_stop_sending(Error::HttpNone.code());
                }
            }
        }
        self.streams_to_stop_sending
            .retain(|expires, _| *expires > now);
    }
}

impl HttpServer for Http3TestServer {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        dgrams: D,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        // If stuck_0rtt_mode is enabled and we've already processed datagrams once,
        // stop processing to simulate a connection stuck in ZERORTT state.
        if self.stuck_0rtt_mode && self.stuck_0rtt_activated {
            info!("Stuck 0-RTT mode active - ignoring datagrams to keep session in ZERORTT");
            // Return Callback to keep the server loop running but don't process datagrams
            return OutputBatch::Callback(Duration::from_millis(100));
        }

        let output = self.server.process_multiple(dgrams, now, max_datagrams);

        // If we just processed datagrams with stuck mode enabled, mark it as activated
        if self.stuck_0rtt_mode && !self.stuck_0rtt_activated {
            info!("Stuck 0-RTT mode activated - next datagrams will be ignored");
            self.stuck_0rtt_activated = true;
        }

        let output = if self.sessions_to_close.is_empty()
            && self.connections_to_close.is_empty()
            && self.streams_to_stop_sending.is_empty()
        {
            output
        } else {
            // In case there are pending sessions to close, use a shorter
            // timeout to make process_events() to be called earlier.
            const MIN_INTERVAL: Duration = Duration::from_millis(100);

            match output {
                OutputBatch::None => OutputBatch::Callback(MIN_INTERVAL),
                o @ OutputBatch::DatagramBatch(_) => o,
                OutputBatch::Callback(d) => OutputBatch::Callback(min(d, MIN_INTERVAL)),
            }
        };

        output
    }

    fn process_events(&mut self, now: Instant) {
        self.maybe_close_connection();
        self.maybe_close_session(now);
        self.maybe_create_wt_stream(now);
        self.maybe_create_wt_stream_and_stop_sending(now);
        self.maybe_stop_sending(now);
        self.maybe_flood_datagrams(now);

        while let Some(event) = self.server.next_event() {
            trace!("Event: {:?}", event);
            match event {
                Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. } => {}
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin,
                } => {
                    trace!("Headers (request={} fin={}): {:?}", stream, fin, headers);

                    let connection_hash = {
                        let mut hasher = DefaultHasher::new();
                        stream.conn.hash(&mut hasher);
                        hasher.finish()
                    };

                    // Some responses do not have content-type. This is on purpose to exercise
                    // UnknownDecoder code.
                    let default_ret = b"Hello World".to_vec();
                    let default_headers = vec![
                        Header::new(":status", "200"),
                        Header::new("cache-control", "no-cache"),
                        Header::new("content-length", default_ret.len().to_string()),
                        Header::new("x-http3-conn-hash", connection_hash.to_string()),
                    ];

                    // Mimic a server that rejects a raw non-ASCII cookie by
                    // closing the whole connection with H3_FRAME_ERROR.
                    if headers
                        .iter()
                        .any(|h| h.name() == "cookie" && !h.value().is_ascii())
                    {
                        stream.conn.borrow_mut().close(
                            now,
                            0x0106,
                            "http3.invalid_header_field",
                        );
                        continue;
                    }

                    let path_hdr = headers.iter().find(|&h| h.name() == ":path");
                    match path_hdr {
                        Some(ph) if !ph.value().is_empty() => {
                            let path = ph.value();
                            trace!(
                                "Serve request {:?}",
                                ph.value_utf8().unwrap_or("<invalid utf8>")
                            );
                            if path == b"/Response421" {
                                let response_body = b"0123456789".to_vec();
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "421"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new(
                                            "content-length",
                                            response_body.len().to_string(),
                                        ),
                                    ])
                                    .unwrap();
                                self.new_response(stream, response_body, now);
                            } else if path == b"/RequestCancelled" {
                                stream
                                    .stream_stop_sending(Error::HttpRequestCancelled.code())
                                    .unwrap();
                                stream
                                    .stream_reset_send(Error::HttpRequestCancelled.code())
                                    .unwrap();
                            } else if path == b"/VersionFallback" {
                                stream
                                    .stream_stop_sending(Error::HttpVersionFallback.code())
                                    .unwrap();
                                stream
                                    .stream_reset_send(Error::HttpVersionFallback.code())
                                    .unwrap();
                            } else if path == b"/EarlyResponse" {
                                stream.stream_stop_sending(Error::HttpNone.code()).unwrap();
                            } else if path == b"/SetStuckZeroRtt" {
                                info!("Enabling stuck 0-RTT mode - next connection will be stuck in ZERORTT");
                                self.stuck_0rtt_mode = true;
                                let response_body = b"Stuck 0-RTT mode enabled".to_vec();
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new(
                                            "content-length",
                                            response_body.len().to_string(),
                                        ),
                                    ])
                                    .unwrap();
                                self.new_response(stream, response_body, now);
                            } else if path == b"/RequestRejected" {
                                stream
                                    .stream_stop_sending(Error::HttpRequestRejected.code())
                                    .unwrap();
                                stream
                                    .stream_reset_send(Error::HttpRequestRejected.code())
                                    .unwrap();
                            } else if path == b"/UnknownReset" {
                                // Reset with an unrecognized application error code.
                                stream.stream_stop_sending(0xfe).unwrap();
                                stream.stream_reset_send(0xfe).unwrap();
                            } else if path == b"/closeafter1000ms" {
                                let response_body = b"0123456789".to_vec();
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new(
                                            "content-length",
                                            response_body.len().to_string(),
                                        ),
                                    ])
                                    .unwrap();
                                let expires = Instant::now() + Duration::from_millis(1000);
                                if !self.connections_to_close.contains_key(&expires) {
                                    self.connections_to_close.insert(expires, Vec::new());
                                }
                                self.connections_to_close
                                    .get_mut(&expires)
                                    .unwrap()
                                    .push(stream.conn.clone());

                                self.new_response(stream, response_body, now);
                            } else if path == b"/.well-known/http-opportunistic" {
                                let host_hdr = headers.iter().find(|&h| h.name() == ":authority");
                                match host_hdr {
                                    Some(host) if !host.value().is_empty() => {
                                        let mut content = b"[\"http://".to_vec();
                                        content.extend(host.value());
                                        content.extend(b"\"]");
                                        stream
                                            .send_headers(&[
                                                Header::new(":status", "200"),
                                                Header::new("cache-control", "no-cache"),
                                                Header::new("content-type", "application/json"),
                                                Header::new(
                                                    "content-length",
                                                    content.len().to_string(),
                                                ),
                                            ])
                                            .unwrap();
                                        self.new_response(stream, content, now);
                                    }
                                    _ => {
                                        stream.send_headers(&default_headers).unwrap();
                                        self.new_response(stream, default_ret, now);
                                    }
                                }
                            } else if path == b"/no_body" {
                                debug!("Request for no_body");
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                    ])
                                    .unwrap();
                                stream.stream_close_send(now).unwrap();
                            } else if path == b"/no_content_length" {
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                    ])
                                    .unwrap();
                                self.new_response(stream, vec![b'a'; 4000], now);
                            } else if path == b"/content_length_smaller" {
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new("content-length", 4000.to_string()),
                                    ])
                                    .unwrap();
                                self.new_response(stream, vec![b'a'; 8000], now);
                            } else if path == b"/post" {
                                if fin {
                                    // An empty request body finishes on the
                                    // HEADERS frame and never fires a Data
                                    // event, so respond immediately.
                                    self.respond_to_post(stream, 0, now);
                                } else {
                                    // Read all data before responding.
                                    self.posts.insert(stream, 0);
                                }
                            } else if path == b"/priority_mirror" {
                                if let Some(priority) =
                                    headers.iter().find(|h| h.name() == "priority")
                                {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                            Header::new("content-type", "text/plain"),
                                            Header::new(
                                                "priority-mirror",
                                                priority.value_utf8().unwrap(),
                                            ),
                                            Header::new(
                                                "content-length",
                                                priority.value().len().to_string(),
                                            ),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, priority.value().to_vec(), now);
                                } else {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                        ])
                                        .unwrap();
                                    stream.stream_close_send(now).unwrap();
                                }
                            } else if path == b"/103_response" {
                                if let Some(early_hint) =
                                    headers.iter().find(|h| h.name() == "link-to-set")
                                {
                                    for l in early_hint.value_utf8().unwrap().split(',') {
                                        stream
                                            .send_headers(&[
                                                Header::new(":status", "103"),
                                                Header::new("link", l),
                                            ])
                                            .unwrap();
                                    }
                                }
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-length", "0"),
                                    ])
                                    .unwrap();
                                stream.stream_close_send(now).unwrap();
                            } else if path == b"/get_webtransport_datagram" {
                                if let Some(dgram) = self.received_datagram.take() {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("content-length", dgram.len().to_string()),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, dgram.as_ref().to_vec(), now);
                                } else {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "404"),
                                            Header::new("cache-control", "no-cache"),
                                        ])
                                        .unwrap();
                                    stream.stream_close_send(now).unwrap();
                                }
                            } else if path == b"/alt_svc_header" {
                                if let Some(alt_svc) =
                                    headers.iter().find(|h| h.name() == "x-altsvc")
                                {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                            Header::new("content-type", "text/plain"),
                                            Header::new("content-length", 100.to_string()),
                                            Header::new(
                                                "alt-svc",
                                                format!("h3={}", alt_svc.value_utf8().unwrap()),
                                            ),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, vec![b'a'; 100], now);
                                } else {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, vec![b'a'; 100], now);
                                }
                            } else {
                                match ph.value_utf8().ok().and_then(|s| {
                                    s.trim_matches(|p| p == '/').parse::<usize>().ok()
                                }) {
                                    Some(v) => {
                                        stream
                                            .send_headers(&[
                                                Header::new(":status", "200"),
                                                Header::new("cache-control", "no-cache"),
                                                Header::new("content-type", "text/plain"),
                                                Header::new("content-length", v.to_string()),
                                            ])
                                            .unwrap();
                                        self.new_response(stream, vec![b'a'; v], now);
                                    }
                                    None => {
                                        stream.send_headers(&default_headers).unwrap();
                                        self.new_response(stream, default_ret, now);
                                    }
                                }
                            }
                        }
                        _ => {
                            stream.send_headers(&default_headers).unwrap();
                            self.new_response(stream, default_ret, now);
                        }
                    }
                }
                Http3ServerEvent::Data { stream, data, fin } => {
                    // echo bidirectional input back to client
                    if self.webtransport_bidi_stream.contains(&stream) {
                        if stream.handler.borrow().state().active() {
                            self.new_response(stream, data, now);
                        }
                        break;
                    }

                    // echo unidirectional input to back to client
                    // need to close or we hang
                    if self.wt_unidi_echo_back.contains_key(&stream) {
                        let echo_back = self.wt_unidi_echo_back.remove(&stream).unwrap();
                        echo_back.send_data(&data, now).unwrap();
                        echo_back.stream_close_send(now).unwrap();
                        break;
                    }

                    if let Some(r) = self.posts.get_mut(&stream) {
                        *r += data.len();
                    }
                    if fin {
                        if let Some(r) = self.posts.remove(&stream) {
                            self.respond_to_post(stream, r, now);
                        }
                    }
                }
                Http3ServerEvent::DataWritable { stream } => {
                    self.handle_stream_writable(stream, now)
                }
                Http3ServerEvent::StateChange { .. } => {}
                Http3ServerEvent::PriorityUpdate { .. } => {}
                Http3ServerEvent::StreamReset { stream, error } => {
                    trace!("Http3ServerEvent::StreamReset {:?} {:?}", stream, error);
                }
                Http3ServerEvent::StreamStopSending { stream, error } => {
                    trace!(
                        "Http3ServerEvent::StreamStopSending {:?} {:?}",
                        stream,
                        error
                    );
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::NewSession {
                    session,
                    headers,
                }) => {
                    debug!(
                        "WebTransportServerEvent::NewSession {:?} {:?}",
                        session, headers
                    );
                    let path_hdr = headers.iter().find(|&h| h.name() == ":path");
                    match path_hdr {
                        Some(ph) if !ph.value().is_empty() => {
                            let path = ph.value();
                            trace!(
                                "Serve request {:?}",
                                ph.value_utf8().unwrap_or("<invalid utf8>")
                            );
                            if path == b"/success" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                            } else if path == b"/redirect" {
                                session
                                    .response(
                                        &SessionAcceptAction::Reject(
                                            [
                                                Header::new(":status", "302"),
                                                Header::new("location", "/"),
                                            ]
                                            .to_vec(),
                                        ),
                                        now,
                                    )
                                    .unwrap();
                            } else if path == b"/reject" {
                                session
                                    .response(
                                        &SessionAcceptAction::Reject(
                                            [Header::new(":status", "404")].to_vec(),
                                        ),
                                        now,
                                    )
                                    .unwrap();
                            } else if path == b"/closeafter0ms" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                if !self.sessions_to_close.contains_key(&now) {
                                    self.sessions_to_close.insert(now, Vec::new());
                                }
                                self.sessions_to_close.get_mut(&now).unwrap().push(session);
                            } else if path == b"/closeafter100ms" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                let expires = Instant::now() + Duration::from_millis(100);
                                if !self.sessions_to_close.contains_key(&expires) {
                                    self.sessions_to_close.insert(expires, Vec::new());
                                }
                                self.sessions_to_close
                                    .get_mut(&expires)
                                    .unwrap()
                                    .push(session);
                            } else if path == b"/create_unidi_stream" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::UniDi,
                                    None,
                                ));
                            } else if path == b"/create_unidi_stream_and_hello" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::UniDi,
                                    Some(Vec::from("qwerty")),
                                ));
                            } else if path.starts_with(b"/create_unidi_streams/") {
                                let count: usize = std::str::from_utf8(&path[22..])
                                    .unwrap()
                                    .parse()
                                    .unwrap();
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                for i in 0..count {
                                    self.sessions_to_create_stream.push((
                                        session.clone(),
                                        StreamType::UniDi,
                                        Some(format!("stream{i}").into_bytes()),
                                    ));
                                }
                            } else if path.starts_with(b"/create_bidi_streams/") {
                                let count: usize = std::str::from_utf8(&path[21..])
                                    .unwrap()
                                    .parse()
                                    .unwrap();
                                self.webtransport_bidi_stream.clear();
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                for i in 0..count {
                                    self.sessions_to_create_stream.push((
                                        session.clone(),
                                        StreamType::BiDi,
                                        Some(format!("stream{i}").into_bytes()),
                                    ));
                                }
                            } else if path == b"/create_bidi_stream" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::BiDi,
                                    None,
                                ));
                            } else if path == b"/create_bidi_stream_and_hello" {
                                self.webtransport_bidi_stream.clear();
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::BiDi,
                                    Some(Vec::from("asdfg")),
                                ));
                            } else if path == b"/create_bidi_stream_and_large_data" {
                                self.webtransport_bidi_stream.clear();
                                let data: Vec<u8> = vec![1u8; 32 * 1024 * 1024];
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::BiDi,
                                    Some(data),
                                ));
                            } else if path.starts_with(b"/flood_datagrams/") {
                                let params = std::str::from_utf8(&path[17..]).unwrap();
                                let (count, size) = params.split_once('/').unwrap();
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                // Without a count limit, the queue only reports
                                // backpressure once its byte budget overflows,
                                // which evicts a datagram.
                                session
                                    .set_max_buffered_datagrams(NonZeroUsize::new(64))
                                    .unwrap();
                                let size = if size == "max" {
                                    session.max_datagram_size().unwrap() as usize
                                } else {
                                    size.parse().unwrap()
                                };
                                self.datagram_floods.push((
                                    session,
                                    vec![42; size],
                                    count.parse().unwrap(),
                                ));
                            } else if path == b"/echo_datagrams" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.echo_datagram_sessions.insert(session.stream_id());
                            } else if path == b"/create_bidi_stream_and_stop_sending" {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                                self.sessions_to_create_bidi_and_stop_sending
                                    .push(session);
                            } else {
                                session.response(&SessionAcceptAction::Accept, now).unwrap();
                            }
                        }
                        _ => {
                            session
                                .response(
                                    &SessionAcceptAction::Reject(
                                        [Header::new(":status", "404")].to_vec(),
                                    ),
                                    now,
                                )
                                .unwrap();
                        }
                    }
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::SessionClosed {
                    session,
                    reason,
                    headers: _,
                }) => {
                    debug!(
                        "WebTransportServerEvent::SessionClosed {:?} {:?}",
                        session, reason
                    );
                    self.echo_datagram_sessions.remove(&session.stream_id());
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::NewStream(stream)) => {
                    // new stream could be from client-outgoing unidirectional
                    // or bidirectional
                    if !stream.stream_info.is_http() {
                        if stream.stream_id().is_bidi() {
                            self.webtransport_bidi_stream.insert(stream);
                        } else {
                            // Newly created stream happens on same connection
                            // as the stream creation for client's incoming stream.
                            // Link the streams with map for echo back
                            if self.wt_unidi_conn_to_stream.contains_key(&stream.conn) {
                                let s = self.wt_unidi_conn_to_stream.remove(&stream.conn).unwrap();
                                self.wt_unidi_echo_back.insert(stream, s);
                            }
                        }
                    }
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::Datagram {
                    session,
                    datagram,
                }) => {
                    debug!(
                        "WebTransportServerEvent::Datagram {:?} {:?}",
                        session, datagram
                    );
                    if self.echo_datagram_sessions.contains(&session.stream_id()) {
                        // A full outgoing queue drops the echo, which the
                        // client counts as loss.
                        _ = session.send_datagram(
                            datagram.as_ref(),
                            None,
                            now,
                            SendGroupId::new(0),
                            0,
                        );
                    } else {
                        self.received_datagram = Some(datagram);
                    }
                }
                Http3ServerEvent::ConnectUdp(_) => {
                    unimplemented!()
                }
            }
        }
    }

    fn has_events(&self) -> bool {
        self.server.has_events()
    }
}

struct Server(neqo_transport::server::Server);

impl ::std::fmt::Display for Server {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        self.0.fmt(f)
    }
}

impl HttpServer for Server {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        dgrams: D,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        self.0.process_multiple(dgrams, now, max_datagrams)
    }

    fn process_events(&mut self, _now: Instant) {
        let active_conns = self.0.active_connections();
        for acr in active_conns {
            loop {
                let event = match acr.borrow_mut().next_event() {
                    None => break,
                    Some(e) => e,
                };
                match event {
                    ConnectionEvent::RecvStreamReadable { stream_id } => {
                        if stream_id.is_bidi() && stream_id.is_client_initiated() {
                            // We are only interesting in request streams
                            acr.borrow_mut()
                                .stream_send(stream_id, HTTP_RESPONSE_WITH_WRONG_FRAME)
                                .expect("Read should succeed");
                        }
                    }
                    _ => {}
                }
            }
        }
    }

    fn has_events(&self) -> bool {
        self.0.has_active_connections()
    }
}

struct Http3ReverseProxyServer {
    server: Http3Server,
    responses: HashMap<Http3OrWebTransportStream, Vec<u8>>,
    server_port: i32,
    requests: HashMap<Http3OrWebTransportStream, (Vec<Header>, Vec<u8>)>,
    #[cfg(not(target_os = "android"))]
    response_to_send: HashMap<Http3OrWebTransportStream, Receiver<(Vec<Header>, Vec<u8>)>>,
}

impl ::std::fmt::Display for Http3ReverseProxyServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "{}", self.server)
    }
}

impl Http3ReverseProxyServer {
    pub fn new(server: Http3Server, server_port: i32) -> Self {
        Self {
            server,
            responses: HashMap::new(),
            server_port,
            requests: HashMap::new(),
            #[cfg(not(target_os = "android"))]
            response_to_send: HashMap::new(),
        }
    }

    #[cfg(not(target_os = "android"))]
    fn new_response(&mut self, stream: Http3OrWebTransportStream, mut data: Vec<u8>, now: Instant) {
        if data.len() == 0 {
            let _ = stream.stream_close_send(now);
            return;
        }
        match stream.send_data(&data, now) {
            Ok(sent) => {
                if sent < data.len() {
                    self.responses.insert(stream, data.split_off(sent));
                } else {
                    stream.stream_close_send(now).unwrap();
                }
            }
            Err(e) => {
                eprintln!("error is {:?}, stream will be reset", e);
                let _ = stream.stream_reset_send(Error::HttpRequestCancelled.code());
            }
        }
    }

    fn handle_stream_writable(&mut self, stream: Http3OrWebTransportStream, now: Instant) {
        if let Some(data) = self.responses.get_mut(&stream) {
            match stream.send_data(&data, now) {
                Ok(sent) => {
                    if sent < data.len() {
                        let new_d = (*data).split_off(sent);
                        *data = new_d;
                    } else {
                        stream.stream_close_send(now).unwrap();
                        self.responses.remove(&stream);
                    }
                }
                Err(_) => {
                    eprintln!("Unexpected error");
                }
            }
        }
    }

    #[cfg(not(target_os = "android"))]
    async fn fetch_url(
        request: http::Request<Full<hyper::body::Bytes>>,
        out_header: &mut Vec<Header>,
        out_body: &mut Vec<u8>,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let client = Client::builder(hyper_util::rt::TokioExecutor::new()).build_http();
        let resp = client.request(request).await?;
        out_header.push(Header::new(":status", resp.status().as_str()));
        out_header.extend(resp.headers().iter().filter_map(|(key, value)| {
            let name = key.as_str().to_ascii_lowercase();
            // Connection-specific header fields make an HTTP/3 message malformed
            // (RFC 9114, Section 4.2: https://www.rfc-editor.org/rfc/rfc9114#section-4.2);
            // drop them when forwarding an HTTP/1.1 response.
            if matches!(
                name.as_str(),
                "connection" | "keep-alive" | "proxy-connection" | "transfer-encoding" | "upgrade"
            ) {
                return None;
            }
            Some(Header::new(name, value.to_str().unwrap_or("")))
        }));

        let mut body = resp.into_body();
        while let Some(frame) = body.frame().await {
            match frame {
                Ok(frame) => {
                    if let Ok(data) = frame.into_data() {
                        out_body.extend_from_slice(&data);
                    }
                }
                Err(_) => break,
            }
        }

        Ok(())
    }

    #[cfg(not(target_os = "android"))]
    fn fetch(
        &mut self,
        stream: Http3OrWebTransportStream,
        request_headers: &Vec<Header>,
        request_body: Vec<u8>,
    ) {
        let mut request: http::Request<Full<hyper::body::Bytes>> = http::Request::new(Full::new(hyper::body::Bytes::new()));
        let mut path = String::new();
        for hdr in request_headers.iter() {
            match hdr.name() {
                ":method" => {
                    *request.method_mut() = Method::from_bytes(hdr.value()).unwrap();
                }
                ":scheme" => {}
                ":authority" => {
                    request.headers_mut().insert(
                        hyper::header::HOST,
                        HeaderValue::from_bytes(hdr.value()).unwrap(),
                    );
                }
                ":path" => {
                    path = hdr.value_utf8().unwrap_or("/").to_string();
                }
                _ => {
                    if let Ok(hdr_name) = HeaderName::from_lowercase(hdr.name().as_bytes()) {
                        request
                            .headers_mut()
                            .insert(hdr_name, HeaderValue::from_bytes(hdr.value()).unwrap());
                    }
                }
            }
        }
        *request.body_mut() = Full::new(hyper::body::Bytes::from(request_body));
        *request.uri_mut() =
            match format!("http://127.0.0.1:{}{}", self.server_port.to_string(), path).parse() {
                Ok(uri) => uri,
                _ => {
                    eprintln!("invalid uri: {}", path);
                    stream
                        .send_headers(&[
                            Header::new(":status", "400"),
                            Header::new("cache-control", "no-cache"),
                            Header::new("content-length", "0"),
                        ])
                        .unwrap();
                    return;
                }
            };
        trace!("request header: {:?}", request);

        let (sender, receiver) = channel();
        thread::spawn(move || {
            let rt = tokio::runtime::Runtime::new().unwrap();
            let mut h: Vec<Header> = Vec::new();
            let mut data: Vec<u8> = Vec::new();
            let _ = rt.block_on(Self::fetch_url(request, &mut h, &mut data));
            trace!("response headers: {:?}", h);
            trace!("res data: {:02X?}", data);

            match sender.send((h, data)) {
                Ok(()) => {}
                _ => {
                    eprintln!("sender.send failed");
                }
            }
        });
        self.response_to_send.insert(stream, receiver);
    }

    #[cfg(target_os = "android")]
    fn fetch(
        &mut self,
        mut _stream: Http3OrWebTransportStream,
        _request_headers: &Vec<Header>,
        _request_body: Vec<u8>,
    ) {
        // do nothing
    }

    #[cfg(not(target_os = "android"))]
    fn maybe_process_response(&mut self, now: Instant) {
        let mut data_to_send = HashMap::new();
        self.response_to_send
            .retain(|id, receiver| match receiver.try_recv() {
                Ok((headers, body)) => {
                    data_to_send.insert(id.clone(), (headers.clone(), body.clone()));
                    false
                }
                Err(TryRecvError::Empty) => true,
                Err(TryRecvError::Disconnected) => false,
            });
        while let Some(stream) = data_to_send.keys().next().cloned() {
            let (header, data) = data_to_send.remove(&stream).unwrap();
            trace!("response headers: {:?}", header);
            match stream.send_headers(&header) {
                Ok(()) => {
                    self.new_response(stream, data, now);
                }
                _ => {}
            }
        }
    }
}

impl HttpServer for Http3ReverseProxyServer {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        dgrams: D,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        let output = self.server.process_multiple(dgrams, now, max_datagrams);

        #[cfg(not(target_os = "android"))]
        let output = if self.response_to_send.is_empty() {
            output
        } else {
            // In case there are pending responses to send, make sure a reasonable
            // callback is returned.
            const MIN_INTERVAL: Duration = Duration::from_millis(100);

            match output {
                OutputBatch::None => OutputBatch::Callback(MIN_INTERVAL),
                o @ OutputBatch::DatagramBatch(_) => o,
                OutputBatch::Callback(d) => OutputBatch::Callback(min(d, MIN_INTERVAL)),
            }
        };

        output
    }

    fn process_events(&mut self, now: Instant) {
        #[cfg(not(target_os = "android"))]
        self.maybe_process_response(now);
        while let Some(event) = self.server.next_event() {
            trace!("Event: {:?}", event);
            match event {
                Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. } => {}
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin: _,
                } => {
                    trace!("Headers {:?}", headers);
                    if self.server_port != -1 {
                        let method_hdr = headers.iter().find(|&h| h.name() == ":method");
                        match method_hdr {
                            Some(method) => match method.value() {
                                b"POST" => {
                                    let content_length =
                                        headers.iter().find(|&h| h.name() == "content-length");
                                    if let Some(length_str) = content_length {
                                        if let Ok(len) =
                                            length_str.value_utf8().unwrap_or("0").parse::<u32>()
                                        {
                                            if len > 0 {
                                                self.requests.insert(stream, (headers, Vec::new()));
                                            } else {
                                                self.fetch(stream, &headers, b"".to_vec());
                                            }
                                        }
                                    }
                                }
                                _ => {
                                    self.fetch(stream, &headers, b"".to_vec());
                                }
                            },
                            _ => {}
                        }
                    } else {
                        let path_hdr = headers.iter().find(|&h| h.name() == ":path");
                        match path_hdr {
                            Some(ph) if !ph.value().is_empty() => {
                                if let Some(path_str) = ph.value_utf8().ok() {
                                    if let Some(port_str) = path_str.strip_prefix("/port?") {
                                        let port = port_str.parse::<i32>().ok();
                                        if let Some(port) = port {
                                            trace!("got port {}", port);
                                            self.server_port = port;
                                        }
                                    }
                                }
                            }
                            _ => {}
                        }
                        stream
                            .send_headers(&[
                                Header::new(":status", "200"),
                                Header::new("cache-control", "no-cache"),
                                Header::new("content-length", "0"),
                            ])
                            .unwrap();
                    }
                }
                Http3ServerEvent::Data {
                    stream,
                    mut data,
                    fin,
                } => {
                    if let Some((_, body)) = self.requests.get_mut(&stream) {
                        body.append(&mut data);
                    }
                    if fin {
                        if let Some((headers, body)) = self.requests.remove(&stream) {
                            self.fetch(stream, &headers, body);
                        }
                    }
                }
                Http3ServerEvent::DataWritable { stream } => {
                    self.handle_stream_writable(stream, now)
                }
                Http3ServerEvent::StateChange { .. } | Http3ServerEvent::PriorityUpdate { .. } => {}
                Http3ServerEvent::StreamReset { stream, error } => {
                    trace!("Http3ServerEvent::StreamReset {:?} {:?}", stream, error);
                }
                Http3ServerEvent::StreamStopSending { stream, error } => {
                    trace!(
                        "Http3ServerEvent::StreamStopSending {:?} {:?}",
                        stream,
                        error
                    );
                }
                Http3ServerEvent::WebTransport(_) => {}
                Http3ServerEvent::ConnectUdp(_) => {}
            }
        }
    }

    fn has_events(&self) -> bool {
        self.server.has_events()
    }
}

struct Http3ConnectProxyServer {
    server: Http3Server,
    // Keyed by the stream rather than its StreamId, which is only unique within
    // a connection.
    tcp_streams: HashMap<Http3OrWebTransportStream, TcpStream>,
    udp_sockets: HashMap<StreamId, UdpSocket>,
    // When set, CONNECT requests without a Proxy-Authorization header get a 407.
    require_auth: bool,
}

impl ::std::fmt::Display for Http3ConnectProxyServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "{}", self.server)
    }
}

impl Http3ConnectProxyServer {
    pub fn new(server: Http3Server) -> Self {
        Self {
            server,
            tcp_streams: HashMap::new(),
            udp_sockets: HashMap::new(),
            require_auth: env::var("MOZ_HTTP3_PROXY_REQUIRE_AUTH").is_ok(),
        }
    }
}

impl HttpServer for Http3ConnectProxyServer {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        dgrams: D,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        self.server.process_multiple(dgrams, now, max_datagrams)
    }

    fn process_events(&mut self, now: Instant) {
        while let Some(event) = self.server.next_event() {
            trace!("Event: {:?}", event);
            match event {
                Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. } => {}
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin: _,
                } => {
                    trace!("Headers {:?}", headers);
                    let method_hdr = headers.iter().find(|&h| h.name() == ":method").unwrap();
                    assert_eq!(
                        method_hdr.value(),
                        b"CONNECT",
                        "{:?} not supported",
                        method_hdr.value_utf8().unwrap_or("<invalid utf8>")
                    );
                    if self.require_auth
                        && !headers.iter().any(|h| h.name() == "proxy-authorization")
                    {
                        stream
                            .send_headers(&[
                                Header::new(":status", "407"),
                                Header::new("proxy-authenticate", "Basic realm='foo'"),
                            ])
                            .unwrap();
                        stream.stream_close_send(now).unwrap();
                        continue;
                    }

                    let host_hdr = headers.iter().find(|&h| h.name() == ":authority").unwrap();
                    let host_str = host_hdr.value_utf8().unwrap();

                    // Check if we should fallback to 127.0.0.1 before attempting connection
                    let host_without_port = if let Some(colon_pos) = host_str.rfind(':') {
                        &host_str[..colon_pos]
                    } else {
                        host_str
                    };

                    let should_fallback = matches!(
                        host_without_port,
                        "foo.example.com" | "alt1.example.com" | "alt2.example.com"
                    );

                    let target = if should_fallback {
                        if let Some(port_start) = host_str.rfind(':') {
                            format!("127.0.0.1:{}", &host_str[port_start + 1..])
                        } else {
                            // No port specified, assume default HTTP port 80
                            "127.0.0.1:80".to_string()
                        }
                    } else {
                        host_str.to_string()
                    };

                    let tcp_stream = match std::net::TcpStream::connect(&target) {
                        Ok(c) => c,
                        Err(_) => {
                            stream
                                .send_headers(&[
                                    Header::new(":status", "502"),
                                    Header::new("cache-control", "no-cache"),
                                ])
                                .unwrap();
                            stream.stream_close_send(now).unwrap();
                            return;
                        }
                    };

                    tcp_stream.set_nonblocking(true).unwrap();
                    trace!("tcp_stream to {:?} created", host_hdr);
                    stream
                        .send_headers(&[
                            Header::new(":status", "200"),
                            Header::new("cache-control", "no-cache"),
                        ])
                        .unwrap();
                    self.tcp_streams.insert(
                        stream.clone(),
                        TcpStream {
                            send_buffer: VecDeque::new(),
                            recv_buffer: VecDeque::new(),
                            stream: tokio::net::TcpStream::from_std(tcp_stream).unwrap(),
                            send_fin: false,
                            received_fin: false,
                            session: stream,
                        },
                    );
                }
                Http3ServerEvent::Data { stream, data, fin } => {
                    trace!("tcp_stream send to server len={}", data.len());
                    let tcp_stream = self.tcp_streams.get_mut(&stream).unwrap();
                    // TODO: extend() effectively breaks backpressure.
                    tcp_stream.send_buffer.extend(data);
                    tcp_stream.send_fin |= fin;
                }
                Http3ServerEvent::DataWritable { stream } => {
                    trace!(
                        "Http3ServerEvent::DataWritable streamid={}",
                        stream.stream_id()
                    );
                    let tcp_stream = self.tcp_streams.get_mut(&stream).unwrap();
                    while !tcp_stream.recv_buffer.is_empty() {
                        match stream.send_data(&tcp_stream.recv_buffer.make_contiguous(), now) {
                            Ok(sent) => {
                                trace!("tcp_stream send to client sent={}", sent);
                                if sent == 0 {
                                    // no progress possible right now — stop trying to send in this loop
                                    // (could also mark for later retry)
                                    break;
                                }
                                tcp_stream.recv_buffer.drain(0..sent);
                            }
                            Err(e) => {
                                eprintln!("send_data failed: {:?}", e);
                                break;
                            }
                        }
                    }
                }
                Http3ServerEvent::ConnectUdp(ConnectUdpServerEvent::NewSession {
                    session,
                    headers,
                }) => {
                    session.response(&SessionAcceptAction::Accept, now).unwrap();

                    let host_hdr = headers.iter().find(|&h| h.name() == ":path").unwrap();
                    let path_str = host_hdr.value_utf8().unwrap();
                    let path_parts: Vec<&str> = path_str.split('/').collect();

                    // Format is /.well-known/masque/udp/{target_host}/{target_port}/
                    if path_parts.len() < 6 {
                        panic!("{}", path_str)
                    }

                    let target_host = path_parts[4];
                    let target_port = match path_parts[5].trim_end_matches('/').parse::<u16>() {
                        Ok(port) => port,
                        Err(_) => {
                            panic!("{}", path_str)
                        }
                    };

                    // Replace target_host with 127.0.0.1 for specific hosts
                    let actual_host = match target_host {
                        "foo.example.com" | "alt1.example.com" | "alt2.example.com" => "127.0.0.1",
                        _ => target_host,
                    };

                    let host_port = format!("{}:{}", actual_host, target_port);
                    debug!("CONNECT-UDP to {}", host_port);

                    let socket = {
                        let s =
                            socket2::Socket::new(socket2::Domain::IPV4, socket2::Type::DGRAM, None)
                                .unwrap();
                        s.bind(&"0.0.0.0:0".parse::<SocketAddr>().unwrap().into())
                            .unwrap();
                        let s: std::net::UdpSocket = s.into();
                        s.connect((actual_host, target_port)).unwrap();
                        s.set_nonblocking(true).unwrap();
                        s.into()
                    };

                    self.udp_sockets.insert(
                        session.stream_id(),
                        UdpSocket {
                            session,
                            send_buffer: VecDeque::new(),
                            socket: tokio::net::UdpSocket::from_std(socket).unwrap(),
                        },
                    );
                }
                Http3ServerEvent::ConnectUdp(ConnectUdpServerEvent::Datagram {
                    session,
                    datagram,
                }) => {
                    let udp_socket = self.udp_sockets.get_mut(&session.stream_id()).unwrap();
                    // TODO: effectively breaks backpressure.
                    udp_socket.send_buffer.push_back(datagram);
                }
                Http3ServerEvent::ConnectUdp(ConnectUdpServerEvent::SessionClosed {
                    session,
                    reason,
                    headers: _,
                }) => {
                    debug!(
                        "ConnectUdp session closed: {:?} reason: {:?}",
                        session, reason
                    );
                    self.udp_sockets.remove(&session.stream_id());
                }
                Http3ServerEvent::StateChange { .. } | Http3ServerEvent::PriorityUpdate { .. } => {}
                Http3ServerEvent::StreamReset { stream, error } => {
                    trace!("Http3ServerEvent::StreamReset {:?} {:?}", stream, error);
                }
                Http3ServerEvent::StreamStopSending { stream, error } => {
                    trace!(
                        "Http3ServerEvent::StreamStopSending {:?} {:?}",
                        stream,
                        error
                    );
                }
                Http3ServerEvent::WebTransport(_) => {}
            }
        }
    }

    fn has_events(&self) -> bool {
        self.server.has_events()
    }

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
        let mut progressed = false;
        let mut failed_udp_sockets: Vec<StreamId> = Vec::new();

        for (_sessionid, stream) in &mut self.tcp_streams {
            if let Poll::Ready(Ok(())) = stream.stream.poll_read_ready(cx) {
                loop {
                    let mut buf = vec![0; 1024];
                    match stream.stream.try_read(&mut buf) {
                        Ok(0) => {
                            debug!("TCP: Received 0 bytes -FIN");
                            stream.received_fin = true;
                            // TODO: Reset CONNECT stream.
                            break;
                        }
                        Ok(n) => {
                            debug!("TCP: Received {} bytes from origin", n);
                            // TODO: extend() effectively breaks backpressure.
                            stream.recv_buffer.extend(&buf[0..n]);
                            while !stream.recv_buffer.is_empty() {
                                let sent = match stream.session.send_data(
                                    &stream.recv_buffer.make_contiguous(),
                                    Instant::now(),
                                ) {
                                    Ok(n) => n,
                                    Err(e) => {
                                        debug!("TCP: send_data failed: {}", e);
                                        break;
                                    }
                                };
                                debug!("TCP: stream send to client sent={}", sent);
                                if sent == 0 {
                                    break;
                                }
                                stream.recv_buffer.drain(0..sent);
                            }
                            progressed = true;
                        }
                        Err(e) => {
                            debug!("TCP read error: {e:?}");
                            stream.received_fin = true;
                            // TODO: Handle the error
                            break;
                        }
                    }
                }
            }

            if let Poll::Ready(Ok(())) = stream.stream.poll_write_ready(cx) {
                while !stream.send_buffer.is_empty() {
                    match stream
                        .stream
                        .try_write(&stream.send_buffer.make_contiguous())
                    {
                        Ok(0) => break,
                        Ok(n) => {
                            debug!("TCP: Sent {} bytes to origin", n);
                            stream.send_buffer.drain(0..n);
                            progressed = true;
                        }
                        Err(e) => {
                            debug!("TCP write error: {e:?}");
                            stream.received_fin = true;
                            // TODO: Handle the error
                            break;
                        }
                    }
                }
            }
            if stream.send_fin {
                let _ = stream.stream.shutdown();
            }
        }

        for (stream_id, socket) in &mut self.udp_sockets {
            loop {
                let mut buf = vec![0u8; u16::MAX as usize];
                let mut read_buf = ReadBuf::new(buf.as_mut());
                match socket.socket.poll_recv(cx, &mut read_buf) {
                    Poll::Ready(Ok(())) => {
                        let len = read_buf.filled().len();
                        info!("Received {} bytes from origin", len);
                        buf.resize(len, 0);
                        // TODO: Might overflow our current datagram buffer of 10
                        // https://github.com/mozilla/neqo/issues/2852
                        let _ = socket
                            .session
                            .send_datagram(buf.as_slice(), None, Instant::now())
                            .unwrap();
                        progressed = true;
                    }
                    Poll::Ready(Err(e)) => {
                        error!("Error receiving UDP datagram: {}, closing socket", e);
                        failed_udp_sockets.push(*stream_id);
                        break;
                    }
                    Poll::Pending => break,
                }
            }

            while let Some(datagram) = socket.send_buffer.pop_front() {
                match socket.socket.poll_send(cx, datagram.as_ref()) {
                    Poll::Ready(Ok(0)) | Poll::Pending => {
                        socket.send_buffer.push_front(datagram);
                        break;
                    }
                    Poll::Ready(Ok(n)) => {
                        assert_eq!(n, datagram.len());
                        info!("Sent {}/{} bytes to origin", n, datagram.len());
                        progressed = true;
                    }
                    Poll::Ready(Err(e)) => {
                        error!(
                            "Error sending UDP datagram: {} {:?}, closing socket",
                            e, socket.socket
                        );
                        failed_udp_sockets.push(*stream_id);
                        break;
                    }
                }
            }
        }

        // Remove failed UDP sockets from the list
        for stream_id in failed_udp_sockets {
            if let Some(socket) = self.udp_sockets.remove(&stream_id) {
                debug!("Removed failed UDP socket for stream {}", stream_id);
                // Close the session with an error code
                let _ = socket
                    .session
                    .close_session(0x0100, "UDP socket error", Instant::now());
            }
        }

        if progressed {
            return Poll::Ready(());
        }

        Poll::Pending
    }
}

struct TcpStream {
    send_buffer: VecDeque<u8>,
    recv_buffer: VecDeque<u8>,
    stream: tokio::net::TcpStream,
    send_fin: bool,
    received_fin: bool,
    session: Http3OrWebTransportStream,
}

struct UdpSocket {
    session: ConnectUdpRequest,
    send_buffer: VecDeque<Bytes>,
    socket: tokio::net::UdpSocket,
}

#[derive(Default)]
struct NonRespondingServer {}

impl ::std::fmt::Display for NonRespondingServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "NonRespondingServer")
    }
}

impl HttpServer for NonRespondingServer {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        _dgrams: D,
        _now: Instant,
        _max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        OutputBatch::None
    }

    fn process_events(&mut self, _now: Instant) {}

    fn has_events(&self) -> bool {
        false
    }
}

fn spawn_server<S: HttpServer + Unpin + 'static>(
    server: S,
    port: u16,
    task_set: &LocalSet,
    hosts: &mut Vec<SocketAddr>,
) -> Result<(), io::Error> {
    let addr: SocketAddr = if cfg!(target_os = "windows") {
        format!("127.0.0.1:{}", port).parse().unwrap()
    } else {
        format!("[::]:{}", port).parse().unwrap()
    };

    let socket = match neqo_bin::udp::Socket::bind(&addr) {
        Err(err) => {
            eprintln!("Unable to bind UDP socket: {}", err);
            exit(1)
        }
        Ok(s) => s,
    };

    let local_addr = socket.local_addr();

    task_set
        .spawn_local(Runner::new(server, Box::new(Instant::now), vec![socket]).run());
    hosts.push(local_addr);

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), io::Error> {
    neqo_common::log::init(None);

    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Wrong arguments.");
        exit(1)
    }

    // Read data from stdin and terminate the server if EOF is detected, which
    // means that runxpcshelltests.py ended without shutting down the server.
    thread::spawn(|| loop {
        let mut buffer = String::new();
        match io::stdin().read_line(&mut buffer) {
            Ok(n) => {
                if n == 0 {
                    exit(0);
                }
            }
            Err(_) => {
                exit(0);
            }
        }
    });

    init_db(PathBuf::from(args[1].clone())).unwrap();

    let local = LocalSet::new();
    let mut hosts = vec![];

    let proxy_port = match env::var("MOZ_HTTP3_PROXY_PORT") {
        Ok(val) => val.parse::<u16>().unwrap(),
        _ => 0,
    };

    let anti_replay = || {
        AntiReplay::new(Instant::now(), Duration::from_secs(10), 7, 14)
            .expect("unable to setup anti-replay")
    };
    let cid_mgr = Rc::new(RefCell::new(RandomConnectionIdGenerator::new(10)));

    spawn_server(
        Http3TestServer::new(
            Http3Server::new(
                Instant::now(),
                &[" HTTP2 Test Cert"],
                PROTOCOLS,
                anti_replay(),
                cid_mgr.clone(),
                Http3Parameters::default()
                    .max_table_size_encoder(MAX_TABLE_SIZE)
                    .max_table_size_decoder(MAX_TABLE_SIZE)
                    .max_blocked_streams(MAX_BLOCKED_STREAMS)
                    .webtransport(true)
                    .connection_parameters(ConnectionParameters::default().datagram_size(1200)),
                None,
            )
            .expect("We cannot make a server!"),
        ),
        0,
        &local,
        &mut hosts,
    )?;

    spawn_server(
        Server(
            neqo_transport::server::Server::new(
                Instant::now(),
                &[" HTTP2 Test Cert"],
                PROTOCOLS,
                anti_replay(),
                Box::new(AllowZeroRtt {}),
                cid_mgr.clone(),
                ConnectionParameters::default(),
            )
            .expect("We cannot make a server!"),
        ),
        0,
        &local,
        &mut hosts,
    )?;

    let ech_config = {
        let mut server = Http3TestServer::new(
            Http3Server::new(
                Instant::now(),
                &[" HTTP2 Test Cert"],
                PROTOCOLS,
                anti_replay(),
                cid_mgr.clone(),
                Http3Parameters::default()
                    .max_table_size_encoder(MAX_TABLE_SIZE)
                    .max_table_size_decoder(MAX_TABLE_SIZE)
                    .max_blocked_streams(MAX_BLOCKED_STREAMS),
                None,
            )
            .expect("We cannot make a server!"),
        );
        let (sk, pk) = generate_ech_keys().unwrap();
        server
            .server
            .enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
            .expect("unable to enable ech");
        let ech_config = server.server.ech_config().to_vec();
        spawn_server(server, 0, &local, &mut hosts)?;
        ech_config
    };

    spawn_server(
        {
            let server_config = if env::var("MOZ_HTTP3_MOCHITEST").is_ok() {
                ("mochitest-cert", 8888)
            } else {
                (" HTTP2 Test Cert", -1)
            };
            let server = Http3ReverseProxyServer::new(
                Http3Server::new(
                    Instant::now(),
                    &[server_config.0],
                    PROTOCOLS,
                    anti_replay(),
                    cid_mgr.clone(),
                    Http3Parameters::default()
                        .max_table_size_encoder(MAX_TABLE_SIZE)
                        .max_table_size_decoder(MAX_TABLE_SIZE)
                        .max_blocked_streams(MAX_BLOCKED_STREAMS)
                        .webtransport(true)
                        .connection_parameters(ConnectionParameters::default().datagram_size(1200)),
                    None,
                )
                .expect("We cannot make a server!"),
                server_config.1,
            );
            server
        },
        proxy_port,
        &local,
        &mut hosts,
    )?;

    spawn_server(NonRespondingServer::default(), 0, &local, &mut hosts)?;

    spawn_server(
        Http3ConnectProxyServer::new(
            Http3Server::new(
                Instant::now(),
                &[" HTTP2 Test Cert"],
                PROTOCOLS,
                anti_replay(),
                cid_mgr,
                Http3Parameters::default()
                    .max_table_size_encoder(MAX_TABLE_SIZE)
                    .connection_parameters(
                        ConnectionParameters::default()
                            // TODO: Restrict in size.
                            .datagram_size(u16::MAX as u64)
                            .pmtud(true),
                    )
                    .max_table_size_decoder(MAX_TABLE_SIZE)
                    .max_blocked_streams(MAX_BLOCKED_STREAMS)
                    .connect(true)
                    .http3_datagram(true),
                None,
            )
            .expect("We cannot make a server!"),
        ),
        0,
        &local,
        &mut hosts,
    )?;

    // Note this is parsed by test runner.
    // https://searchfox.org/mozilla-central/rev/e69f323af80c357d287fb6314745e75c62eab92a/testing/mozbase/mozserve/mozserve/servers.py#116-121
    println!(
        "HTTP3 server listening on ports {}, {}, {}, {}, {} and {}. EchConfig is @{}@",
        hosts[0].port(),
        hosts[1].port(),
        hosts[2].port(),
        hosts[3].port(),
        hosts[4].port(),
        hosts[5].port(),
        BASE64_STANDARD.encode(ech_config)
    );

    local.await;

    Ok(())
}

#[no_mangle]
extern "C" fn __tsan_default_suppressions() -> *const std::os::raw::c_char {
    // https://github.com/rust-lang/rust/issues/128769
    concat!(
        "race:<tokio::runtime::io::registration_set::RegistrationSet>::allocate\n",
        "race:tokio::runtime::io::registration_set::RegistrationSet::allocate\0",
    )
    .as_ptr() as *const _
}
