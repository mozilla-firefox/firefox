// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

mod anticipated_streams;
mod datagrams;
mod negotiation;
mod sessions;
mod streams;
use std::{cell::RefCell, rc::Rc, time::Duration};

use neqo_common::{event::Provider as _, header::HeadersExt as _, to_u64};
use neqo_transport::{
    ConnectionParameters, Pmtud, StreamId, StreamType, recv_stream, send_stream,
    streams::SendGroupId,
};
use nss::AuthenticationStatus;
use test_fixture::{
    CountingConnectionIdGenerator, DEFAULT_ADDR, DEFAULT_ALPN_H3, DEFAULT_KEYS,
    DEFAULT_SERVER_NAME, anti_replay, fixture_init, now,
};

use crate::{
    Error, Header, Http3Client, Http3ClientEvent, Http3OrWebTransportStream, Http3Parameters,
    Http3Server, Http3ServerEvent, Http3State, SessionAcceptAction, WebTransportEvent,
    features::extended_connect::CloseReason,
    webtransport::{ClientSession as _, ServerEvent, ServerSession},
};

// Leave space for large QUIC header.
const DATAGRAM_SIZE: u64 = to_u64(Pmtud::default_plpmtu(DEFAULT_ADDR.ip())) - 40;

pub fn wt_default_parameters() -> Http3Parameters {
    Http3Parameters::default()
        .webtransport(true)
        .connection_parameters(ConnectionParameters::default().datagram_size(DATAGRAM_SIZE))
}

pub fn default_http3_client(client_params: Http3Parameters) -> Http3Client {
    fixture_init();
    Http3Client::new(
        DEFAULT_SERVER_NAME,
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        DEFAULT_ADDR,
        DEFAULT_ADDR,
        client_params,
        now(),
    )
    .expect("create a default client")
}

pub fn default_http3_server(server_params: Http3Parameters) -> Http3Server {
    Http3Server::new(
        now(),
        DEFAULT_KEYS,
        DEFAULT_ALPN_H3,
        anti_replay(),
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        server_params,
        None,
    )
    .expect("create a server")
}

pub fn assert_wt(headers: &[Header]) {
    assert!(
        headers.contains_header(":method", "CONNECT")
            && headers.contains_header(":protocol", "webtransport")
    );
}

fn exchange_packets(client: &mut Http3Client, server: &mut Http3Server) {
    let mut out = None;
    loop {
        out = client.process(out, now()).dgram();
        out = server.process(out, now()).dgram();
        if out.is_none() {
            break;
        }
    }
}

// Perform only QUIC transport handshake.
fn connect_with(client: &mut Http3Client, server: &mut Http3Server) {
    assert_eq!(client.state(), Http3State::Initializing);
    let out = client.process_output(now());
    let out2 = client.process_output(now());
    assert_eq!(client.state(), Http3State::Initializing);

    _ = server.process(out.dgram(), now());
    let out = server.process(out2.dgram(), now());
    let out = client.process(out.dgram(), now());
    let out = server.process(out.dgram(), now());
    let out = client.process(out.dgram(), now());
    let out = server.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_none());

    let authentication_needed = |e| matches!(e, Http3ClientEvent::AuthenticationNeeded);
    assert!(client.events().any(authentication_needed));
    client.authenticated(AuthenticationStatus::Ok, now());

    let out = client.process(out.dgram(), now());
    let connected = |e| matches!(e, Http3ClientEvent::StateChange(Http3State::Connected));
    assert!(client.events().any(connected));

    assert_eq!(client.state(), Http3State::Connected);

    // Exchange H3 settings
    let out = server.process(out.dgram(), now());
    let out = client.process(out.dgram(), now());
    let out = server.process(out.dgram(), now());
    let out = client.process(out.dgram(), now());
    let out = server.process(out.dgram(), now());
    drop(client.process(out.dgram(), now()));
}

fn connect(
    client_params: Http3Parameters,
    server_params: Http3Parameters,
) -> (Http3Client, Http3Server) {
    let mut client = default_http3_client(client_params);
    let mut server = default_http3_server(server_params);
    connect_with(&mut client, &mut server);
    (client, server)
}

struct WtTest {
    client: Http3Client,
    server: Http3Server,
}

impl WtTest {
    pub fn new() -> Self {
        let (client, server) = connect(wt_default_parameters(), wt_default_parameters());
        Self { client, server }
    }

    pub fn new_with_params(client_params: Http3Parameters, server_params: Http3Parameters) -> Self {
        let (client, server) = connect(client_params, server_params);
        Self { client, server }
    }

    pub fn new_with(mut client: Http3Client, mut server: Http3Server) -> Self {
        connect_with(&mut client, &mut server);
        Self { client, server }
    }

    fn negotiate_wt_session(
        &mut self,
        accept: &SessionAcceptAction,
    ) -> (StreamId, Option<ServerSession>) {
        let wt_session_id = self
            .client
            .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
            .unwrap();
        self.exchange_packets();

        let mut wt_server_session = None;
        while let Some(event) = self.server.next_event() {
            match event {
                Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, headers }) => {
                    assert_wt(&headers);
                    session.response(accept, now()).unwrap();
                    wt_server_session = Some(session);
                }
                Http3ServerEvent::Data { .. } => {
                    panic!("There should not be any data events!");
                }
                _ => {}
            }
        }

        self.exchange_packets();
        (wt_session_id, wt_server_session)
    }

    fn create_wt_session(&mut self) -> ServerSession {
        let (wt_session_id, wt_server_session) =
            self.negotiate_wt_session(&SessionAcceptAction::Accept);
        let wt_session_negotiated_event = |e| {
            matches!(
                e,
                Http3ClientEvent::WebTransport(WebTransportEvent::NewSession{
                    stream_id,
                    status,
                    headers,
                }) if (
                    stream_id == wt_session_id &&
                    status == 200 &&
                    headers.contains_header(":status", "200")
                )
            )
        };
        assert!(self.client.events().any(wt_session_negotiated_event));

        let wt_server_session = wt_server_session.unwrap();
        assert_eq!(wt_session_id, wt_server_session.stream_id());
        wt_server_session
    }

    fn create_second_wt_session(&mut self) -> StreamId {
        let id = self
            .client
            .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
            .unwrap();
        self.exchange_packets();
        while let Some(event) = self.server.next_event() {
            if let Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, .. }) = event {
                session
                    .response(&SessionAcceptAction::Accept, now())
                    .unwrap();
            }
        }
        self.exchange_packets();
        id
    }

    fn exchange_packets(&mut self) {
        const RTT: Duration = Duration::from_millis(10);
        let mut out = None;
        let mut now = now();
        loop {
            now += RTT / 2;
            out = self.client.process(out, now).dgram();
            let client_none = out.is_none();
            now += RTT / 2;
            out = self.server.process(out, now).dgram();
            if client_none && out.is_none() {
                break;
            }
        }
    }

    pub fn cancel_session_client(&mut self, wt_stream_id: StreamId) {
        self.client
            .cancel_fetch(wt_stream_id, Error::HttpNone.code())
            .unwrap();
        self.exchange_packets();
    }

    fn session_closed_client(
        e: &Http3ClientEvent,
        id: StreamId,
        expected_reason: &CloseReason,
        expected_headers: Option<&Vec<Header>>,
    ) -> bool {
        if let Http3ClientEvent::WebTransport(WebTransportEvent::SessionClosed {
            stream_id,
            reason,
            headers,
        }) = e
        {
            *stream_id == id && reason == expected_reason && headers.as_ref() == expected_headers
        } else {
            false
        }
    }

    pub fn check_session_closed_event_client(
        &mut self,
        wt_session_id: StreamId,
        expected_reason: &CloseReason,
        expected_headers: Option<&Vec<Header>>,
    ) {
        let mut event_found = false;

        while let Some(event) = self.client.next_event() {
            event_found = Self::session_closed_client(
                &event,
                wt_session_id,
                expected_reason,
                expected_headers,
            );
            if event_found {
                break;
            }
        }
        assert!(event_found);
    }

    pub fn cancel_session_server(&mut self, wt_session: &ServerSession) {
        wt_session.cancel_fetch(Error::HttpNone.code()).unwrap();
        self.exchange_packets();
    }

    fn session_closed_server(
        e: &Http3ServerEvent,
        id: StreamId,
        expected_reason: &CloseReason,
    ) -> bool {
        if let Http3ServerEvent::WebTransport(ServerEvent::SessionClosed {
            session,
            reason,
            headers,
        }) = e
        {
            session.stream_id() == id && reason == expected_reason && headers.is_none()
        } else {
            false
        }
    }

    pub fn check_session_closed_event_server(
        &self,
        wt_session: &ServerSession,
        expected_reason: &CloseReason,
    ) {
        let event = self.server.next_event().unwrap();
        assert!(Self::session_closed_server(
            &event,
            wt_session.stream_id(),
            expected_reason
        ));
    }

    fn create_wt_stream_client(
        &mut self,
        wt_session_id: StreamId,
        stream_type: StreamType,
    ) -> StreamId {
        self.client
            .webtransport_create_stream(wt_session_id, stream_type)
            .unwrap()
    }

    fn send_data_client(&mut self, wt_stream_id: StreamId, data: &[u8]) {
        assert_eq!(
            self.client.send_data(wt_stream_id, data, now()).unwrap(),
            data.len()
        );
        self.exchange_packets();
    }

    fn send_stream_stats(&mut self, wt_stream_id: StreamId) -> Result<send_stream::Stats, Error> {
        self.client.webtransport_send_stream_stats(wt_stream_id)
    }

    fn recv_stream_stats(&mut self, wt_stream_id: StreamId) -> Result<recv_stream::Stats, Error> {
        self.client.webtransport_recv_stream_stats(wt_stream_id)
    }

    fn receive_data_client(
        &mut self,
        expected_stream_id: StreamId,
        new_stream: bool,
        expected_data: &[u8],
        expected_fin: bool,
    ) {
        let mut new_stream_received = false;
        let mut data_received = false;
        while let Some(event) = self.client.next_event() {
            match event {
                Http3ClientEvent::WebTransport(WebTransportEvent::NewStream {
                    stream_id, ..
                }) => {
                    assert_eq!(stream_id, expected_stream_id);
                    new_stream_received = true;
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, expected_stream_id);
                    let mut buf = [0; 100];
                    let (amount, fin) = self.client.read_data(now(), stream_id, &mut buf).unwrap();
                    assert_eq!(fin, expected_fin);
                    assert_eq!(amount, expected_data.len());
                    assert_eq!(&buf[..amount], expected_data);
                    data_received = true;
                }
                _ => {}
            }
        }
        assert!(data_received);
        assert_eq!(new_stream, new_stream_received);
    }

    fn close_stream_sending_client(&mut self, wt_stream_id: StreamId) {
        self.client.stream_close_send(wt_stream_id, now()).unwrap();
        self.exchange_packets();
    }

    fn reset_stream_client(&mut self, wt_stream_id: StreamId) {
        self.client
            .stream_reset_send(wt_stream_id, Error::HttpNone.code())
            .unwrap();
        self.exchange_packets();
    }

    fn commit_stream_client(&mut self, wt_stream_id: StreamId) {
        self.client.stream_commit(wt_stream_id, now()).unwrap();
    }

    fn receive_reset_client(&mut self, expected_stream_id: StreamId) {
        let wt_reset_event = |e| {
            matches!(
                e,
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local
                } if stream_id == expected_stream_id && error == Error::HttpNone.code() && !local
            )
        };
        assert!(self.client.events().any(wt_reset_event));
    }

    fn stream_stop_sending_client(&mut self, stream_id: StreamId) {
        self.client
            .stream_stop_sending(stream_id, Error::HttpNone.code())
            .unwrap();
        self.exchange_packets();
    }

    fn receive_stop_sending_client(&mut self, expected_stream_id: StreamId) {
        let wt_stop_sending_event = |e| {
            matches!(
                e,
                Http3ClientEvent::StopSending {
                    stream_id,
                    error
                } if stream_id == expected_stream_id && error == Error::HttpNone.code()
            )
        };
        assert!(self.client.events().any(wt_stop_sending_event));
    }

    fn check_events_after_closing_session_client(
        &mut self,
        expected_reset_ids: &[StreamId],
        expected_error_stream_reset: Option<u64>,
        expected_stop_sending_ids: &[StreamId],
        expected_error_stream_stop_sending: Option<u64>,
        expected_local: bool,
        expected_session_close: Option<&(StreamId, CloseReason)>,
    ) {
        let mut reset_ids_count = 0;
        let mut stop_sending_ids_count = 0;
        let mut close_event = false;
        while let Some(event) = self.client.next_event() {
            match event {
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local,
                } => {
                    assert!(expected_reset_ids.contains(&stream_id));
                    assert_eq!(expected_error_stream_reset.unwrap(), error);
                    assert_eq!(expected_local, local);
                    reset_ids_count += 1;
                }
                Http3ClientEvent::StopSending { stream_id, error } => {
                    assert!(expected_stop_sending_ids.contains(&stream_id));
                    assert_eq!(expected_error_stream_stop_sending.unwrap(), error);
                    stop_sending_ids_count += 1;
                }
                Http3ClientEvent::WebTransport(WebTransportEvent::SessionClosed {
                    stream_id,
                    reason,
                    headers,
                }) => {
                    close_event = true;
                    assert_eq!(stream_id, expected_session_close.as_ref().unwrap().0);
                    assert_eq!(expected_session_close.as_ref().unwrap().1, reason);
                    assert!(headers.is_none());
                }
                _ => {}
            }
        }
        assert_eq!(reset_ids_count, expected_reset_ids.len());
        assert_eq!(stop_sending_ids_count, expected_stop_sending_ids.len());
        assert_eq!(close_event, expected_session_close.is_some());
    }

    fn create_wt_stream_server(
        wt_server_session: &ServerSession,
        stream_type: StreamType,
    ) -> Http3OrWebTransportStream {
        wt_server_session.create_stream(stream_type).unwrap()
    }

    fn send_data_server(&mut self, wt_stream: &Http3OrWebTransportStream, data: &[u8]) {
        assert_eq!(wt_stream.send_data(data, now()).unwrap(), data.len());
        self.exchange_packets();
    }

    fn receive_data_server(
        &mut self,
        stream_id: StreamId,
        new_stream: bool,
        expected_data: &[u8],
        expected_fin: bool,
    ) -> Http3OrWebTransportStream {
        self.exchange_packets();
        let mut new_stream_received = false;
        let mut data_received = false;
        let mut wt_stream = None;
        let mut stream_closed = false;
        let mut recv_data = Vec::new();
        while let Some(event) = self.server.next_event() {
            match event {
                Http3ServerEvent::WebTransport(ServerEvent::NewStream(request)) => {
                    assert_eq!(stream_id, request.stream_id());
                    new_stream_received = true;
                }
                Http3ServerEvent::Data {
                    mut data,
                    fin,
                    stream,
                } => {
                    recv_data.append(&mut data);
                    stream_closed = fin;
                    data_received = true;
                    wt_stream = Some(stream);
                }
                _ => {}
            }
        }
        assert_eq!(&recv_data[..], expected_data);
        assert!(data_received);
        assert_eq!(new_stream, new_stream_received);
        assert_eq!(stream_closed, expected_fin);
        wt_stream.unwrap()
    }

    fn close_stream_sending_server(&mut self, wt_stream: &Http3OrWebTransportStream) {
        wt_stream.stream_close_send(now()).unwrap();
        self.exchange_packets();
    }

    fn reset_stream_server(&mut self, wt_stream: &Http3OrWebTransportStream) {
        wt_stream.stream_reset_send(Error::HttpNone.code()).unwrap();
        self.exchange_packets();
    }

    fn stream_stop_sending_server(&mut self, wt_stream: &Http3OrWebTransportStream) {
        wt_stream
            .stream_stop_sending(Error::HttpNone.code())
            .unwrap();
        self.exchange_packets();
    }

    fn receive_reset_server(&self, expected_stream_id: StreamId, expected_error: u64) {
        let stream_reset = |e| {
            matches!(
                e,
                Http3ServerEvent::StreamReset {
                    stream,
                    error
                } if stream.stream_id() == expected_stream_id && error == expected_error
            )
        };
        assert!(self.server.events().any(stream_reset));
    }

    fn receive_stop_sending_server(&self, expected_stream_id: StreamId, expected_error: u64) {
        let stop_sending = |e| {
            matches!(
                e,
                Http3ServerEvent::StreamStopSending {
                    stream,
                    error
                } if stream.stream_id() == expected_stream_id && error == expected_error
            )
        };
        assert!(self.server.events().any(stop_sending));
    }

    fn check_events_after_closing_session_server(
        &self,
        expected_reset_ids: &[StreamId],
        expected_error_stream_reset: Option<u64>,
        expected_stop_sending_ids: &[StreamId],
        expected_error_stream_stop_sending: Option<u64>,
        expected_session_close: Option<&(StreamId, CloseReason)>,
    ) {
        let mut reset_ids_count = 0;
        let mut stop_sending_ids_count = 0;
        let mut close_event = false;
        while let Some(event) = self.server.next_event() {
            match event {
                Http3ServerEvent::StreamReset { stream, error } => {
                    assert!(expected_reset_ids.contains(&stream.stream_id()));
                    assert_eq!(expected_error_stream_reset.unwrap(), error);
                    reset_ids_count += 1;
                }
                Http3ServerEvent::StreamStopSending { stream, error } => {
                    assert!(expected_stop_sending_ids.contains(&stream.stream_id()));
                    assert_eq!(expected_error_stream_stop_sending.unwrap(), error);
                    stop_sending_ids_count += 1;
                }
                Http3ServerEvent::WebTransport(ServerEvent::SessionClosed {
                    session,
                    reason,
                    headers,
                }) => {
                    close_event = true;
                    assert_eq!(
                        session.stream_id(),
                        expected_session_close.as_ref().unwrap().0
                    );
                    assert_eq!(expected_session_close.as_ref().unwrap().1, reason);
                    assert!(headers.is_none());
                }
                _ => {}
            }
        }
        assert_eq!(reset_ids_count, expected_reset_ids.len());
        assert_eq!(stop_sending_ids_count, expected_stop_sending_ids.len());
        assert_eq!(close_event, expected_session_close.is_some());
    }

    pub fn session_close_frame_client(&mut self, session_id: StreamId, error: u32, message: &str) {
        self.client
            .webtransport_close_session(session_id, error, message, now())
            .unwrap();
    }

    pub fn session_close_frame_server(wt_session: &ServerSession, error: u32, message: &str) {
        wt_session.close_session(error, message, now()).unwrap();
    }

    fn max_datagram_size(&self, stream_id: StreamId) -> Result<u64, Error> {
        self.client.webtransport_max_datagram_size(stream_id)
    }

    fn send_datagram(&mut self, stream_id: StreamId, buf: &[u8]) -> Result<(), Error> {
        self.client
            .webtransport_send_datagram(stream_id, buf, None, now(), SendGroupId::new(0), 0)
            .map(|_outcome| ())
    }

    fn check_datagram_received_client(
        &mut self,
        expected_stream_id: StreamId,
        expected_dgram: &[u8],
    ) {
        let wt_datagram_event = |e| {
            matches!(
                e,
                Http3ClientEvent::WebTransport(WebTransportEvent::Datagram {
                    session_id,
                    datagram
                }) if session_id == expected_stream_id && datagram.as_ref() == expected_dgram
            )
        };
        assert!(self.client.events().any(wt_datagram_event));
    }

    fn check_datagram_received_server(
        &self,
        expected_session: &ServerSession,
        expected_dgram: &[u8],
    ) {
        let wt_datagram_event = |e| {
            matches!(
                e,
                Http3ServerEvent::WebTransport(ServerEvent::Datagram {
                    session,
                    datagram
                }) if session.stream_id() == expected_session.stream_id() && datagram.as_ref() == expected_dgram
            )
        };
        assert!(self.server.events().any(wt_datagram_event));
    }
}
