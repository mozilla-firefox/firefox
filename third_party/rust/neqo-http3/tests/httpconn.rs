// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg(test)]

mod common;

use std::time::{Duration, Instant};

use http::Uri;
use neqo_common::{Datagram, event::Provider as _, expect_usize};
use neqo_http3::{
    Header, Http3Client, Http3ClientEvent, Http3OrWebTransportStream, Http3Parameters, Http3Server,
    Http3ServerEvent, Http3State, Priority,
};
use neqo_transport::{CloseReason, ConnectionParameters, Error, Output, StreamDataLimit};
use nss::{AuthenticationStatus, ResumptionToken};
use test_fixture::*;
use tracing::trace;

const RESPONSE_DATA: &[u8] = &[0x61, 0x62, 0x63];

fn receive_request(server: &Http3Server) -> Option<Http3OrWebTransportStream> {
    while let Some(event) = server.next_event() {
        if let Http3ServerEvent::Headers {
            stream,
            headers,
            fin,
        } = event
        {
            assert_eq!(
                &headers,
                &[
                    Header::new(":method", "GET"),
                    Header::new(":scheme", "https"),
                    Header::new(":authority", "something.com"),
                    Header::new(":path", "/")
                ]
            );
            assert!(fin);

            return Some(stream);
        }
    }
    None
}

fn set_response(request: &Http3OrWebTransportStream, now: Instant) {
    request
        .send_headers(&[
            Header::new(":status", "200"),
            Header::new("content-length", "3"),
        ])
        .unwrap();
    request.send_data(RESPONSE_DATA, now).unwrap();
    request.stream_close_send(now).unwrap();
}

fn process_server_events(server: &Http3Server, now: Instant) {
    let request = receive_request(server).unwrap();
    set_response(&request, now);
}

fn process_client_events(conn: &mut Http3Client) {
    let mut response_header_found = false;
    let mut response_data_found = false;
    while let Some(event) = conn.next_event() {
        match event {
            Http3ClientEvent::HeaderReady { headers, fin, .. } => {
                assert_eq!(
                    &headers,
                    &[
                        Header::new(":status", "200"),
                        Header::new("content-length", "3"),
                    ]
                );
                assert!(!fin);
                response_header_found = true;
            }
            Http3ClientEvent::DataReadable { stream_id } => {
                let mut buf = [0u8; 100];
                let (amount, fin) = conn.read_data(now(), stream_id, &mut buf).unwrap();
                assert!(fin);
                assert_eq!(amount, RESPONSE_DATA.len());
                assert_eq!(&buf[..RESPONSE_DATA.len()], RESPONSE_DATA);
                response_data_found = true;
            }
            _ => {}
        }
    }
    assert!(response_header_found);
    assert!(response_data_found);
}

fn connect_peers_with_network_propagation_delay(
    hconn_c: &mut Http3Client,
    hconn_s: &mut Http3Server,
    net_delay: u64,
) -> (Option<Datagram>, Instant) {
    let net_delay = Duration::from_millis(net_delay);
    assert_eq!(hconn_c.state(), Http3State::Initializing);
    let mut now = now();
    let out = hconn_c.process_output(now); // Initial
    let out2 = hconn_c.process_output(now); // Initial
    now += net_delay;
    _ = hconn_s.process(out.dgram(), now); // ACK
    let out = hconn_s.process(out2.dgram(), now);
    now += net_delay;
    let out = hconn_c.process(out.dgram(), now);
    now += net_delay;
    let out = hconn_s.process(out.dgram(), now);
    now += net_delay;
    let out = hconn_c.process(out.dgram(), now); // ACK
    now += net_delay;
    let out = hconn_s.process(out.dgram(), now); // consume ACK
    assert!(out.dgram().is_none());
    let authentication_needed = |e| matches!(e, Http3ClientEvent::AuthenticationNeeded);
    assert!(hconn_c.events().any(authentication_needed));
    now += net_delay;
    hconn_c.authenticated(AuthenticationStatus::Ok, now);
    let out = hconn_c.process_output(now); // Handshake
    assert_eq!(hconn_c.state(), Http3State::Connected);
    now += net_delay;
    let out = hconn_s.process(out.dgram(), now); // HANDSHAKE_DONE
    now += net_delay;
    let out = hconn_c.process(out.dgram(), now); // Consume HANDSHAKE_DONE, send control streams.
    now += net_delay;
    let out = hconn_s.process(out.dgram(), now); // consume and send control streams.
    now += net_delay;
    let out = hconn_c.process(out.dgram(), now); // consume control streams.
    (out.dgram(), now)
}

#[must_use]
pub fn connect() -> (Http3Client, Http3Server, Option<Datagram>) {
    let mut hconn_c = default_http3_client();
    let mut hconn_s = default_http3_server();

    let out = connect_peers(&mut hconn_c, &mut hconn_s);
    (hconn_c, hconn_s, out)
}

#[test]
fn simple_connect() {
    let (_hconn_c, _hconn_s, _d) = connect();
}

#[test]
fn fetch() {
    let (mut hconn_c, mut hconn_s, dgram) = connect();

    trace!("-----client");
    let req = hconn_c
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    assert_eq!(req, 0);
    hconn_c.stream_close_send(req, now()).unwrap();
    let out = hconn_c.process(dgram, now());
    trace!("-----server");
    let out = hconn_s.process(out.dgram(), now());
    drop(hconn_c.process(out.dgram(), now()));
    process_server_events(&hconn_s, now());
    let out = hconn_s.process(None::<Datagram>, now());

    trace!("-----client");
    drop(hconn_c.process(out.dgram(), now()));
    let out = hconn_s.process(None::<Datagram>, now());
    drop(hconn_c.process(out.dgram(), now()));
    process_client_events(&mut hconn_c);
}

fn fetch_uri(uri: &str) -> (Http3Client, Http3Server, Http3OrWebTransportStream) {
    let (mut hconn_c, mut hconn_s, dgram) = connect();
    let uri = uri.parse::<Uri>().unwrap();
    let req = hconn_c
        .fetch(now(), "GET", &uri, &[], Priority::default())
        .unwrap();
    hconn_c.stream_close_send(req, now()).unwrap();
    let out = hconn_c.process(dgram, now());
    _ = hconn_s.process(out.dgram(), now());
    let request = receive_request(&hconn_s).unwrap();
    (hconn_c, hconn_s, request)
}

#[test]
fn fetch_uri_drops_userinfo() {
    fetch_uri("https://u:p@something.com/");
}

#[test]
fn fetch_invalid() {
    let (mut hconn_c, _hconn_s, _dgram) = connect();
    for uri in ["/", "https://a.example/\x7f", "https://a.example/?\x7f"] {
        let uri = uri.parse::<Uri>().unwrap();
        let res = hconn_c.fetch(now(), "GET", &uri, &[], Priority::default());
        assert_eq!(res, Err(neqo_http3::Error::InvalidInput));
    }
    let bad_header = [Header::new("x", "a\rb")];
    for (target, headers) in [
        (("https", "something.com", "/x#y"), &[][..]),
        (("https", ":443", "/"), &[]),
        (("https", "something.com:65536", "/"), &[]),
        (("https", "something.com", "/"), &bad_header),
    ] {
        let res = hconn_c.fetch(now(), "GET", &target, headers, Priority::default());
        assert_eq!(res, Err(neqo_http3::Error::InvalidInput));
    }
    let target = ("https", "something.com", "/s?a[]=1&q={x}|y");
    let res = hconn_c.fetch(now(), "GET", &target, &[], Priority::default());
    assert_eq!(res.unwrap(), 0);
}

#[test]
fn response_invalid_header_value() {
    let (_hconn_c, _hconn_s, request) = fetch_uri("https://something.com/");
    let res = request.send_headers(&[Header::new(":status", "200"), Header::new("x", "a\rb")]);
    assert_eq!(res, Err(neqo_http3::Error::InvalidInput));
    let res = request.send_headers(&[Header::new(":status", "101")]);
    assert_eq!(res, Err(neqo_http3::Error::InvalidInput));
}

#[test]
fn response_invalid_status() {
    let (_hconn_c, _hconn_s, request) = fetch_uri("https://something.com/");
    for status in ["101", "abc"] {
        let res = request.send_headers(&[Header::new(":status", status)]);
        assert_eq!(res, Err(neqo_http3::Error::InvalidInput));
    }
}

#[test]
fn response_103() {
    let (mut hconn_c, mut hconn_s, dgram) = connect();

    let req = hconn_c
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    assert_eq!(req, 0);
    hconn_c.stream_close_send(req, now()).unwrap();
    let out = hconn_c.process(dgram, now());

    let out = hconn_s.process(out.dgram(), now());
    drop(hconn_c.process(out.dgram(), now()));
    let request = receive_request(&hconn_s).unwrap();
    let info_headers = [
        Header::new(":status", "103"),
        Header::new("link", "</style.css>; rel=preload; as=style"),
    ];
    // Send 103
    request.send_headers(&info_headers).unwrap();
    let out = hconn_s.process(None::<Datagram>, now());

    drop(hconn_c.process(out.dgram(), now()));

    let info_headers_event = |e| {
        matches!(e, Http3ClientEvent::HeaderReady { headers,
                    interim,
                    fin, .. } if !fin && interim && headers.as_ref() == info_headers)
    };
    assert!(hconn_c.events().any(info_headers_event));

    set_response(&request, now());
    let out = hconn_s.process(None::<Datagram>, now());
    drop(hconn_c.process(out.dgram(), now()));
    process_client_events(&mut hconn_c);
}

/// Test [`neqo_http3::SendMessage::send_data`] to set
/// [`neqo_transport::Connection::stream_set_writable_event_low_watermark`].
#[test]
fn data_writable_events_low_watermark() -> Result<(), Box<dyn std::error::Error>> {
    const STREAM_LIMIT: u64 = 5000;
    const DATA_FRAME_HEADER_SIZE: usize = 3;

    // Create a client and a server.
    let mut hconn_c = http3_client_with_params(Http3Parameters::default().connection_parameters(
        ConnectionParameters::default().max_stream_data(StreamDataLimit::BiDiLocal, STREAM_LIMIT),
    ));
    let mut hconn_s = default_http3_server();
    drop(connect_peers(&mut hconn_c, &mut hconn_s));

    // Client sends GET to server.
    let stream_id = hconn_c.fetch(
        now(),
        "GET",
        &("https", "something.com", "/"),
        &[],
        Priority::default(),
    )?;
    hconn_c.stream_close_send(stream_id, now())?;
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    // Server receives GET and responds with headers.
    let request = receive_request(&hconn_s).unwrap();
    request.send_headers(&[Header::new(":status", "200")])?;

    // Sending these headers clears the server's send stream buffer and thus
    // emits a DataWritable event.
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    let data_writable = |e| {
        matches!(
            e,
            Http3ServerEvent::DataWritable {
                stream
            } if stream.stream_id() == stream_id
        )
    };
    assert!(hconn_s.events().any(data_writable));

    // Have server fill entire send buffer minus 1 byte.
    let all_but_one = request.available()? - DATA_FRAME_HEADER_SIZE - 1;
    let buf = vec![1; all_but_one];
    let sent = request.send_data(&buf, now())?;
    assert_eq!(sent, all_but_one);
    assert_eq!(request.available()?, 1);

    // Sending the buffered data clears the send stream buffer and thus emits a
    // DataWritable event.
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    assert!(hconn_s.events().any(data_writable));

    // Sending more fails, given that each data frame needs to be preceded by a
    // header, i.e. needs more than 1 byte of send space to send 1 byte payload.
    assert_eq!(request.available()?, 1);
    assert_eq!(request.send_data(&buf, now())?, 0);

    // Have the client read all the pending data.
    let mut recv_buf = vec![0_u8; all_but_one];
    let (recvd, _) = hconn_c.read_data(now(), stream_id, &mut recv_buf)?;
    assert_eq!(sent, recvd);
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    // Expect the server's available send space to be back to the stream limit.
    assert_eq!(request.available()?, expect_usize(STREAM_LIMIT));

    // Expect the server to emit a DataWritable event, even though it always had
    // at least 1 byte available to send, i.e. it never exhausted the entire
    // available send space.
    assert!(hconn_s.events().any(data_writable));

    Ok(())
}

#[test]
fn data_writable_events() {
    const STREAM_LIMIT: u64 = 5000;
    const DATA_AMOUNT: usize = 10000;

    let mut hconn_c = http3_client_with_params(Http3Parameters::default().connection_parameters(
        ConnectionParameters::default().max_stream_data(StreamDataLimit::BiDiLocal, STREAM_LIMIT),
    ));
    let mut hconn_s = default_http3_server();

    drop(connect_peers(&mut hconn_c, &mut hconn_s));

    // Create a request.
    let req = hconn_c
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    hconn_c.stream_close_send(req, now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    let request = receive_request(&hconn_s).unwrap();

    request
        .send_headers(&[
            Header::new(":status", "200"),
            Header::new("content-length", DATA_AMOUNT.to_string()),
        ])
        .unwrap();

    // Send a lot of data
    let buf = &[1; DATA_AMOUNT];
    let mut sent = request.send_data(buf, now()).unwrap();
    assert!(sent < DATA_AMOUNT);

    // Exchange packets and read the data on the client side.
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    let stream_id = request.stream_id();
    let mut recv_buf = [0_u8; DATA_AMOUNT];
    let (mut recvd, _) = hconn_c.read_data(now(), stream_id, &mut recv_buf).unwrap();
    assert_eq!(sent, recvd);
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    let data_writable = |e| {
        matches!(
            e,
            Http3ServerEvent::DataWritable {
                stream
            } if stream.stream_id() == stream_id
        )
    };
    // Make sure we have a DataWritable event.
    assert!(hconn_s.events().any(data_writable));
    // Data can be sent again.
    let s = request.send_data(&buf[sent..], now()).unwrap();
    assert!(s > 0);
    sent += s;

    // Exchange packets and read the data on the client side.
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    let (r, _) = hconn_c
        .read_data(now(), stream_id, &mut recv_buf[recvd..])
        .unwrap();
    recvd += r;
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    assert_eq!(sent, recvd);

    // One more DataWritable event.
    assert!(hconn_s.events().any(data_writable));
    // Send more data.
    let s = request.send_data(&buf[sent..], now()).unwrap();
    assert!(s > 0);
    sent += s;
    assert_eq!(sent, DATA_AMOUNT);

    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    let (r, _) = hconn_c
        .read_data(now(), stream_id, &mut recv_buf[recvd..])
        .unwrap();
    recvd += r;

    // Make sure all data is received by the client.
    assert_eq!(recvd, DATA_AMOUNT);
    assert_eq!(&recv_buf, buf);
}

fn get_token(client: &mut Http3Client) -> ResumptionToken {
    assert_eq!(client.state(), Http3State::Connected);
    client
        .events()
        .find_map(|e| {
            if let Http3ClientEvent::ResumptionToken(token) = e {
                Some(token)
            } else {
                None
            }
        })
        .unwrap()
}

#[test]
fn closing_keeps_resumption_token() {
    let (mut hconn_c, mut hconn_s, _) = connect();
    hconn_c.events().for_each(drop);
    let conn = hconn_s
        .events()
        .find_map(|e| match e {
            Http3ServerEvent::StateChange { conn, .. } => Some(conn),
            _ => None,
        })
        .unwrap();
    conn.borrow_mut().send_ticket(now(), &[]).unwrap();
    let ticket = hconn_s.process_output(now()).dgram().unwrap();
    conn.borrow_mut().close(now(), 42, "");
    let close = hconn_s.process_output(now()).dgram().unwrap();

    hconn_c.process_multiple_input([ticket, close], now());
    let state = Http3State::Closing(CloseReason::Transport(Error::PeerApplication(42)));
    assert_eq!(hconn_c.state(), state);
    let events = hconn_c.events().collect::<Vec<_>>();
    let [
        Http3ClientEvent::ResumptionToken(token),
        Http3ClientEvent::StateChange(state_change),
    ] = &events[..]
    else {
        panic!("unexpected events {events:?}");
    };
    assert_eq!(*state_change, state);

    let mut hconn_c = default_http3_client();
    hconn_c.enable_resumption(now(), token).unwrap();
    assert_eq!(hconn_c.state(), Http3State::ZeroRtt);
}

#[test]
fn zerortt() {
    let (mut hconn_c, _, dgram) = connect();
    let token = get_token(&mut hconn_c);

    // Create a new connection with a resumption token.
    let mut hconn_c = default_http3_client();
    hconn_c
        .enable_resumption(now(), &token)
        .expect("Set resumption token");
    let mut hconn_s = default_http3_server();

    // Create a request.
    let req = hconn_c
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    hconn_c.stream_close_send(req, now()).unwrap();

    let out = hconn_c.process(dgram, now());
    let out2 = hconn_c.process_output(now());
    _ = hconn_s.process(out.dgram(), now());
    let out = hconn_s.process(out2.dgram(), now());

    let out = hconn_c.process(out.dgram(), now());
    let out = hconn_s.process(out.dgram(), now());

    let mut request_stream = None;
    let mut zerortt_state_change = false;
    while let Some(event) = hconn_s.next_event() {
        match event {
            Http3ServerEvent::Headers {
                stream,
                headers,
                fin,
            } => {
                assert_eq!(
                    &headers,
                    &[
                        Header::new(":method", "GET"),
                        Header::new(":scheme", "https"),
                        Header::new(":authority", "something.com"),
                        Header::new(":path", "/")
                    ]
                );
                assert!(fin);

                request_stream = Some(stream);
            }
            Http3ServerEvent::StateChange { state, .. } => {
                assert_eq!(state, Http3State::ZeroRtt);
                zerortt_state_change = true;
            }
            _ => {}
        }
    }
    assert!(zerortt_state_change);
    let request_stream = request_stream.unwrap();

    // Send a response
    set_response(&request_stream, now());

    // Receive the response
    exchange_packets(&mut hconn_c, &mut hconn_s, false, out.dgram());
    process_client_events(&mut hconn_c);
}

#[test]
/// When a client has an outstanding fetch, it will send keepalives.
/// Test that it will successfully run until the connection times out.
fn fetch_noresponse_will_idletimeout() {
    let mut hconn_c = default_http3_client();
    let mut hconn_s = default_http3_server();

    let (dgram, mut now) =
        connect_peers_with_network_propagation_delay(&mut hconn_c, &mut hconn_s, 10);

    trace!("-----client");
    let req = hconn_c
        .fetch(
            now,
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    assert_eq!(req, 0);
    hconn_c.stream_close_send(req, now).unwrap();
    let _out = hconn_c.process(dgram, now);
    trace!("-----server");

    let mut done = false;
    while !done {
        while let Some(event) = hconn_c.next_event() {
            if let Http3ClientEvent::StateChange(
                Http3State::Closing(error_code) | Http3State::Closed(error_code),
            ) = event
            {
                assert_eq!(error_code, CloseReason::Transport(Error::IdleTimeout));
                done = true;
            }
        }

        if let Output::Callback(t) = hconn_c.process_output(now) {
            now += t;
        }
    }
}

// Server needs to gracefully handle out-of-order STOP_SENDING and STREAM frame arrivals.
fn server_stop_sending_and_stream_test(separate_packets: bool, stop_sending_first: bool) {
    let (mut client, mut server, stream_id) = common::connect_and_send_request(false);

    let send_stop_sending = |c: &mut Http3Client| c.stream_stop_sending(stream_id, 0).unwrap();
    let send_fin = |c: &mut Http3Client| c.stream_close_send(stream_id, now()).unwrap();

    if stop_sending_first {
        send_stop_sending(&mut client);
    } else {
        send_fin(&mut client);
    }
    if separate_packets {
        server.process(client.process_output(now()).dgram(), now());
    }
    if stop_sending_first {
        send_fin(&mut client);
    } else {
        send_stop_sending(&mut client);
    }
    server.process(client.process_output(now()).dgram(), now());

    let events: Vec<_> = server.events().collect();
    assert!(events.iter().any(|e| matches!(
        e, Http3ServerEvent::StreamStopSending { stream, .. } if stream.stream_id() == stream_id
    )));
    for event in events {
        if let Http3ServerEvent::Headers { stream, .. } = event {
            // The stream is dead; any attempt to send on it should fail.
            assert_eq!(
                stream.send_headers(&[Header::new(":status", "200")]),
                Err(neqo_http3::Error::InvalidStreamId)
            );
            assert_eq!(
                stream.stream_close_send(now()),
                Err(neqo_http3::Error::InvalidStreamId)
            );
        }
    }
}

#[test]
fn server_stop_sending_and_stream_combinations() {
    for separate_packets in [false, true] {
        for stop_sending_first in [false, true] {
            server_stop_sending_and_stream_test(separate_packets, stop_sending_first);
        }
    }
}

/// Confirm that a server can generate a reliable reset
/// and that the client receives the committed data before the reset.
#[test]
fn server_reliable_reset() {
    const APP_ERROR: u64 = 7;

    let (mut hconn_c, mut hconn_s, dgram) = connect();

    let req = hconn_c
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    assert_eq!(req, 0);
    hconn_c.stream_close_send(req, now()).unwrap();
    let out = hconn_c.process(dgram, now());

    // Generate a response, with the headers committed.
    let out = hconn_s.process(out.dgram(), now());
    if let Some(d) = out.dgram() {
        hconn_c.process_input(d, now());
    }
    let request = receive_request(&hconn_s).unwrap();
    request
        .send_headers(&[
            Header::new(":status", "200"),
            Header::new("content-length", "3"),
        ])
        .unwrap();
    request.stream_commit(now()).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    request.stream_reset_send(APP_ERROR).unwrap();
    let out = hconn_s.process_output(now());

    // Now check that the client gets the right events.
    drop(hconn_c.process(out.dgram(), now()));
    let mut got_headers = false;
    let mut got_reset = false;
    while let Some(event) = hconn_c.next_event() {
        match event {
            Http3ClientEvent::HeaderReady { headers, fin, .. } => {
                assert_eq!(
                    &headers,
                    &[
                        Header::new(":status", "200"),
                        Header::new("content-length", "3"),
                    ]
                );
                assert!(!fin);
                got_headers = true;
            }
            Http3ClientEvent::DataReadable { .. } => {
                panic!("should get reset before data");
            }
            Http3ClientEvent::Reset { error, local, .. } => {
                assert!(got_headers);
                assert_eq!(error, APP_ERROR);
                assert!(!local);
                got_reset = true;
            }
            _ => {}
        }
    }
    assert!(got_reset);
}

/// A reliable reset that commits a DATA frame delivers the committed body: the client sees
/// `DataReadable`, reads the committed bytes, and only then observes the reset.
#[test]
fn server_reliable_reset_with_body() {
    const APP_ERROR: u64 = 7;

    let (mut hconn_c, mut hconn_s, dgram) = connect();
    let req = hconn_c
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    hconn_c.stream_close_send(req, now()).unwrap();
    let out = hconn_c.process(dgram, now());
    let out = hconn_s.process(out.dgram(), now());
    if let Some(d) = out.dgram() {
        hconn_c.process_input(d, now());
    }
    let request = receive_request(&hconn_s).unwrap();
    request
        .send_headers(&[
            Header::new(":status", "200"),
            Header::new("content-length", "3"),
        ])
        .unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    // Commit the headers *and* the body, then reset.
    request.stream_commit(now()).unwrap();
    request.stream_reset_send(APP_ERROR).unwrap();
    let out = hconn_s.process_output(now());

    // The committed headers and body are delivered, but the reset is not.
    // That is delayed until after data is consumed.
    drop(hconn_c.process(out.dgram(), now()));
    let mut got_headers = false;
    let mut got_data = false;
    while let Some(event) = hconn_c.next_event() {
        match event {
            Http3ClientEvent::HeaderReady { .. } => got_headers = true,
            Http3ClientEvent::DataReadable { stream_id } => {
                let mut buf = [0; 10];
                let (amount, fin) = hconn_c.read_data(now(), stream_id, &mut buf).unwrap();
                assert_eq!(&buf[..amount], RESPONSE_DATA);
                assert!(!fin);
                got_data = true;
            }
            Http3ClientEvent::Reset { .. } => panic!("reset surfaced before the body was read"),
            _ => {}
        }
    }
    assert!(got_headers);
    assert!(got_data);

    // Reading the committed body drained the reliable data, so we should get a reset event.
    // The HTTP/3 stack needs to process events from the transport before that happens.
    drop(hconn_c.process_output(now()));
    let mut got_reset = false;
    while let Some(event) = hconn_c.next_event() {
        if let Http3ClientEvent::Reset { error, local, .. } = event {
            assert_eq!(error, APP_ERROR);
            assert!(!local);
            got_reset = true;
        }
    }
    assert!(got_reset);
}
