// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg(test)]

use std::sync::OnceLock;

use neqo_common::event::Provider as _;
use neqo_http3::{
    Error, Header, Http3Client, Http3ClientEvent, Http3OrWebTransportStream, Http3Server,
    Http3ServerEvent, Priority,
};
use nss::AuthenticationStatus;
use test_fixture::*;

const RESPONSE_DATA: &[u8] = &[0x61, 0x62, 0x63];

fn response_header_no_data() -> &'static Vec<Header> {
    static HEADERS: OnceLock<Vec<Header>> = OnceLock::new();
    HEADERS.get_or_init(|| vec![Header::new(":status", "200"), Header::new("something", "3")])
}

fn response_header_103() -> &'static Vec<Header> {
    static HEADERS: OnceLock<Vec<Header>> = OnceLock::new();
    HEADERS.get_or_init(|| vec![Header::new(":status", "103"), Header::new("link", "...")])
}

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

fn send_trailers(request: &Http3OrWebTransportStream) -> Result<(), Error> {
    request.send_headers(&[
        Header::new("something1", "something"),
        Header::new("something2", "3"),
    ])
}

fn send_informational_headers(request: &Http3OrWebTransportStream) -> Result<(), Error> {
    request.send_headers(response_header_103())
}

fn send_headers(request: &Http3OrWebTransportStream) -> Result<(), Error> {
    request.send_headers(&[
        Header::new(":status", "200"),
        Header::new("content-length", "3"),
    ])
}

fn process_client_events(conn: &mut Http3Client) {
    let mut response_header_found = false;
    let mut response_data_found = false;
    while let Some(event) = conn.next_event() {
        match event {
            Http3ClientEvent::HeaderReady { headers, fin, .. } => {
                assert!(
                    (headers.as_ref()
                        == [
                            Header::new(":status", "200"),
                            Header::new("content-length", "3"),
                        ])
                        || (headers.as_ref() == *response_header_103())
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

fn process_client_events_no_data(conn: &mut Http3Client) {
    let mut response_header_found = false;
    let mut fin_received = false;
    while let Some(event) = conn.next_event() {
        match event {
            Http3ClientEvent::HeaderReady { headers, fin, .. } => {
                assert_eq!(headers.as_ref(), *response_header_no_data());
                fin_received = fin;
                response_header_found = true;
            }
            Http3ClientEvent::DataReadable { stream_id } => {
                let mut buf = [0u8; 100];
                let (amount, fin) = conn.read_data(now(), stream_id, &mut buf).unwrap();
                assert!(fin);
                fin_received = true;
                assert_eq!(amount, 0);
            }
            _ => {}
        }
    }
    assert!(response_header_found);
    assert!(fin_received);
}

fn connect() -> (Http3Client, Http3Server) {
    let mut hconn_c = default_http3_client();
    let mut hconn_s = default_http3_server();

    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    let authentication_needed = |e| matches!(e, Http3ClientEvent::AuthenticationNeeded);
    assert!(hconn_c.events().any(authentication_needed));
    hconn_c.authenticated(AuthenticationStatus::Ok, now());
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    (hconn_c, hconn_s)
}

fn send_and_receive_request(
    hconn_c: &mut Http3Client,
    hconn_s: &mut Http3Server,
) -> Http3OrWebTransportStream {
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
    exchange_packets(hconn_c, hconn_s, false, None);

    receive_request(hconn_s).unwrap()
}

fn connect_send_and_receive_request() -> (Http3Client, Http3Server, Http3OrWebTransportStream) {
    let (mut hconn_c, mut hconn_s) = connect();
    let request = send_and_receive_request(&mut hconn_c, &mut hconn_s);
    (hconn_c, hconn_s, request)
}

#[test]
fn response_trailers1() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    send_trailers(&request).unwrap();
    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn response_trailers2() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    send_trailers(&request).unwrap();
    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn response_trailers3() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    send_trailers(&request).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn response_trailers_no_data() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    request.send_headers(response_header_no_data()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    send_trailers(&request).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events_no_data(&mut hconn_c);
}

#[test]
fn multiple_response_trailers() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    send_trailers(&request).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    assert_eq!(send_trailers(&request), Err(Error::InvalidInput));

    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn data_after_trailer() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    send_trailers(&request).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    assert_eq!(
        request.send_data(RESPONSE_DATA, now()),
        Err(Error::InvalidInput)
    );

    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn trailers_after_close() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    request.stream_close_send(now()).unwrap();

    assert_eq!(send_trailers(&request), Err(Error::InvalidStreamId));

    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn multiple_response_headers() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    request.send_headers(response_header_no_data()).unwrap();

    assert_eq!(
        request.send_headers(response_header_no_data()),
        Err(Error::InvalidHeader)
    );

    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events_no_data(&mut hconn_c);
}

#[test]
fn informational_after_response_headers() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    request.send_headers(response_header_no_data()).unwrap();

    assert_eq!(
        send_informational_headers(&request),
        Err(Error::InvalidHeader)
    );

    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events_no_data(&mut hconn_c);
}

#[test]
fn data_after_informational() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_informational_headers(&request).unwrap();

    assert_eq!(
        request.send_data(RESPONSE_DATA, now()),
        Err(Error::InvalidInput)
    );

    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn non_trailers_headers_after_data() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);

    assert_eq!(
        request.send_headers(response_header_no_data()),
        Err(Error::InvalidHeader)
    );

    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn data_before_headers() {
    let (mut hconn_c, mut hconn_s, request) = connect_send_and_receive_request();
    assert_eq!(
        request.send_data(RESPONSE_DATA, now()),
        Err(Error::InvalidInput)
    );

    send_headers(&request).unwrap();
    request.send_data(RESPONSE_DATA, now()).unwrap();
    request.stream_close_send(now()).unwrap();
    exchange_packets(&mut hconn_c, &mut hconn_s, false, None);
    process_client_events(&mut hconn_c);
}

#[test]
fn server_send_single_udp_datagram() {
    let (mut hconn_c, mut hconn_s, request_1) = connect_send_and_receive_request();

    send_headers(&request_1).unwrap();
    request_1.send_data(RESPONSE_DATA, now()).unwrap();

    let request_2 = send_and_receive_request(&mut hconn_c, &mut hconn_s);

    // Request 1 has no pending data. This call goes straight to the QUIC layer.
    request_1.stream_close_send(now()).unwrap();
    // This adds pending data to request 2 on the HTTP/3 layer.
    send_headers(&request_2).unwrap();

    // Expect server to pack request 1 close frame and request 2 data frame into
    // single UDP datagram.
    hconn_s.process_output(now()).dgram().unwrap();
    assert_eq!(hconn_s.process_output(now()).dgram(), None);
}
