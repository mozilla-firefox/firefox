// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::{Bytes, Encoder, event::Provider as _, header::HeadersExt as _};
use neqo_transport::{
    StreamId, StreamType,
    streams::{SendGroupId, SendOrder},
};
use test_fixture::now;

use crate::{
    ConnectUdpEvent, Error, Header, Http3ClientEvent, Http3OrWebTransportStream, Http3Server,
    Http3ServerEvent, Http3State, Http3StreamInfo, Http3StreamType, Priority, SessionAcceptAction,
    WebTransportEvent,
    connect_udp::ClientSession as _,
    features::extended_connect::{
        CloseReason,
        stats::SessionStats,
        tests::webtransport::{
            WtTest, assert_wt, default_http3_client, default_http3_server, wt_default_parameters,
        },
    },
    frames::{WebTransportFrame, capsule::Capsule},
    server_events::StreamHandler,
    webtransport::{ClientSession as _, ServerEvent, ServerSession},
};

fn wt_with_session() -> (WtTest, StreamId) {
    let mut wt = WtTest::new();
    let session_id = wt.create_wt_session().stream_id();
    (wt, session_id)
}

fn register_group(wt: &mut WtTest, session_id: StreamId) -> SendGroupId {
    wt.client
        .webtransport_create_send_group(session_id)
        .unwrap()
}

fn data_stream_ids(wt: &WtTest) -> Vec<StreamId> {
    let mut ids = Vec::new();
    while let Some(event) = wt.server.next_event() {
        if let Http3ServerEvent::Data { stream, .. } = event {
            ids.push(stream.stream_id());
        }
    }
    ids
}

fn create_stream(wt: &mut WtTest, session_id: StreamId, group: Option<SendGroupId>) -> StreamId {
    wt.client
        .webtransport_create_stream_with_send_group(session_id, StreamType::UniDi, group)
        .unwrap()
}

fn set_sendorder(wt: &mut WtTest, stream: StreamId, sendorder: SendOrder) {
    wt.client
        .webtransport_set_sendorder(stream, Some(sendorder))
        .unwrap();
}

fn send_all(wt: &mut WtTest, stream: StreamId, data: &[u8]) {
    assert_eq!(
        wt.client.send_data(stream, data, now()).unwrap(),
        data.len()
    );
}

#[test]
fn wt_session() {
    let mut wt = WtTest::new();
    drop(wt.create_wt_session());
}

#[test]
fn wt_session_reject() {
    let mut wt = WtTest::new();
    let headers = vec![Header::new(":status", "404")];
    let accept_res = SessionAcceptAction::Reject(headers.clone());
    let (wt_session_id, _wt_session) = wt.negotiate_wt_session(&accept_res);

    wt.check_session_closed_event_client(wt_session_id, &CloseReason::Status(404), Some(&headers));
}

#[test]
fn wt_session_create_invalid_header_value() {
    let mut wt = WtTest::new();
    let headers = [Header::new("x", "a\rb")];
    let target = ("https", "something.com", "/");
    let client = &mut wt.client;
    let res = client.webtransport_create_session(now(), &target, &headers);
    assert_eq!(res, Err(Error::InvalidInput));
    let first = StreamId::new(0);
    let protocol = client.webtransport_session_protocol(first);
    assert_eq!(protocol, Err(Error::InvalidStreamId));
    assert_eq!(wt.create_wt_session().stream_id(), first);
}

fn session_response_invalid_header_value(accept: &SessionAcceptAction) {
    let mut wt = WtTest::new();
    let session_id = wt
        .client
        .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();
    wt.exchange_packets();
    let session = wt
        .server
        .events()
        .find_map(|e| match e {
            Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, .. }) => {
                Some(session)
            }
            _ => None,
        })
        .unwrap();
    let before = wt.client.transport_stats().frame_rx;
    assert_eq!(session.response(accept, now()), Err(Error::InvalidInput));
    wt.exchange_packets();
    let after = wt.client.transport_stats().frame_rx;
    assert_eq!(after.reset_stream, before.reset_stream + 1);
    assert_eq!(after.stop_sending, before.stop_sending + 1);
    assert!(wt.server.events().any(|e| matches!(e,
        Http3ServerEvent::StreamReset { stream, error }
            if stream.stream_id() == session_id && error == Error::HttpRequestRejected.code())));
    let reason = CloseReason::Error(Error::HttpRequestRejected.code());
    wt.check_session_closed_event_client(session_id, &reason, None);
}

#[test]
fn wt_session_accept_with_invalid_header_value() {
    let headers = vec![Header::new("x", "a\rb")];
    session_response_invalid_header_value(&SessionAcceptAction::AcceptWith(headers));
}

#[test]
fn wt_session_reject_invalid_header_value() {
    let headers = vec![Header::new(":status", "404"), Header::new("x", "a\rb")];
    session_response_invalid_header_value(&SessionAcceptAction::Reject(headers));
}

#[test]
fn wt_session_close_client() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt.cancel_session_client(wt_session.stream_id());
    wt.check_session_closed_event_server(&wt_session, &CloseReason::Error(Error::HttpNone.code()));
}

#[test]
fn wt_session_close_server() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt.cancel_session_server(&wt_session);
    wt.check_session_closed_event_client(
        wt_session.stream_id(),
        &CloseReason::Error(Error::HttpNone.code()),
        None,
    );
}

#[test]
fn wt_session_close_server_close_send() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt_session.stream_close_send(now()).unwrap();
    wt.exchange_packets();
    wt.check_session_closed_event_client(
        wt_session.stream_id(),
        &CloseReason::Clean {
            error: 0,
            message: String::new(),
        },
        None,
    );
}

#[test]
fn wt_session_close_server_stop_sending() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt_session
        .stream_stop_sending(Error::HttpNone.code())
        .unwrap();
    wt.exchange_packets();
    wt.check_session_closed_event_client(
        wt_session.stream_id(),
        &CloseReason::Error(Error::HttpNone.code()),
        None,
    );
}

#[test]
fn wt_session_close_server_reset() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt_session
        .stream_reset_send(Error::HttpNone.code())
        .unwrap();
    wt.exchange_packets();
    wt.check_session_closed_event_client(
        wt_session.stream_id(),
        &CloseReason::Error(Error::HttpNone.code()),
        None,
    );
}

#[test]
fn wt_session_response_with_1xx() {
    let mut wt = WtTest::new();

    let wt_session_id = wt
        .client
        .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();
    wt.exchange_packets();

    let mut wt_server_session = None;
    while let Some(event) = wt.server.next_event() {
        if let Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, headers }) = event
        {
            assert_wt(&headers);
            wt_server_session = Some(session);
        }
    }

    let wt_server_session = wt_server_session.unwrap();

    // Send interim response.
    wt_server_session
        .send_headers(&[Header::new(":status", "111")])
        .unwrap();
    wt_server_session
        .response(&SessionAcceptAction::Accept, now())
        .unwrap();

    wt.exchange_packets();

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
    assert!(wt.client.events().any(wt_session_negotiated_event));

    assert_eq!(wt_session_id, wt_server_session.stream_id());
}

#[test]
fn wt_session_response_with_redirect() {
    let headers = [Header::new(":status", "302"), Header::new("location", "/")].to_vec();
    let mut wt = WtTest::new();

    let accept_res = SessionAcceptAction::Reject(headers.clone());

    let (wt_session_id, _wt_session) = wt.negotiate_wt_session(&accept_res);

    wt.check_session_closed_event_client(wt_session_id, &CloseReason::Status(302), Some(&headers));
}

#[test]
fn wt_session_respone_200_with_fin() {
    let mut wt = WtTest::new();

    let wt_session_id = wt
        .client
        .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();
    wt.exchange_packets();
    let mut wt_server_session = None;
    while let Some(event) = wt.server.next_event() {
        if let Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, headers }) = event
        {
            assert_wt(&headers);
            wt_server_session = Some(session);
        }
    }

    let wt_server_session = wt_server_session.unwrap();
    wt_server_session
        .response(&SessionAcceptAction::Accept, now())
        .unwrap();
    wt_server_session.stream_close_send(now()).unwrap();

    wt.exchange_packets();

    let wt_session_close_event = |e| {
        matches!(
            e,
            Http3ClientEvent::WebTransport(WebTransportEvent::SessionClosed{
                stream_id,
                reason,
                headers,
                ..
            }) if (
                stream_id == wt_session_id &&
                reason == CloseReason::Clean{ error: 0, message: String::new()} &&
                headers.is_none()
            )
        )
    };
    assert!(wt.client.events().any(wt_session_close_event));

    assert_eq!(wt_session_id, wt_server_session.stream_id());
}

fn wt_session_request(wt: &mut WtTest) -> (StreamId, ServerSession) {
    let wt_session_id = wt
        .client
        .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();
    wt.exchange_packets();
    let wt_server_session = wt
        .server
        .events()
        .find_map(|e| match e {
            Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, .. }) => {
                Some(session)
            }
            _ => None,
        })
        .unwrap();
    wt.client.events().for_each(drop);
    (wt_session_id, wt_server_session)
}

fn send_headers_and_close_frame(wt_server_session: &ServerSession, headers: &[Header]) {
    let mut enc = Encoder::default();
    WebTransportFrame::CloseSession {
        error: 1,
        message: String::new(),
    }
    .encode(&mut enc);
    wt_server_session.send_headers(headers).unwrap();
    wt_server_session.send_data(enc.as_ref(), now()).unwrap();
}

fn wt_client_events(wt: &mut WtTest) -> Vec<WebTransportEvent> {
    wt.client
        .events()
        .filter_map(|e| match e {
            Http3ClientEvent::WebTransport(e) => Some(e),
            _ => None,
        })
        .collect()
}

const fn rejected(stream_id: StreamId, headers: Vec<Header>) -> WebTransportEvent {
    WebTransportEvent::SessionClosed {
        stream_id,
        reason: CloseReason::Status(403),
        headers: Some(headers),
    }
}

#[test]
fn wt_session_reject_with_close_frame() {
    let mut wt = WtTest::new();
    let (wt_session_id, wt_server_session) = wt_session_request(&mut wt);
    let headers = vec![Header::new(":status", "403")];
    send_headers_and_close_frame(&wt_server_session, &headers);
    wt.exchange_packets();

    let closed = rejected(wt_session_id, headers);
    assert_eq!(wt_client_events(&mut wt), [closed]);
    let res = wt
        .client
        .webtransport_close_session(wt_session_id, 0, "", now());
    assert_eq!(res, Err(Error::InvalidStreamId));
}

#[test]
fn wt_session_reject_qpack_blocked_with_close_frame() {
    let mut wt = WtTest::new();
    let (other_id, other_server_session) = wt_session_request(&mut wt);
    let (wt_session_id, wt_server_session) = wt_session_request(&mut wt);
    let headers = vec![
        Header::new(":status", "403"),
        Header::new("my-header", "my-header"),
    ];
    let reject = SessionAcceptAction::Reject(headers.clone());
    other_server_session.response(&reject, now()).unwrap();
    let encoder_inst = wt.server.process_output(now()).dgram().unwrap();
    send_headers_and_close_frame(&wt_server_session, &headers);
    let response = wt.server.process_output(now()).dgram().unwrap();

    wt.client.process_input(response, now());
    assert_eq!(wt_client_events(&mut wt), []);
    wt.client.process_input(encoder_inst, now());

    let closed = [
        rejected(wt_session_id, headers.clone()),
        rejected(other_id, headers),
    ];
    assert_eq!(wt_client_events(&mut wt), closed);
}

#[test]
fn connect_udp_session_reject_with_datagram_capsule() {
    let params = wt_default_parameters().connect(true);
    let mut wt = WtTest::new_with_params(params.clone(), params);
    let wt_session = wt.create_wt_session();
    let udp_session_id = wt
        .client
        .connect_udp_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();
    wt.exchange_packets();
    // No server API sends data after a non-2xx response, so borrow the WT handler's connection.
    let udp_server_session = StreamHandler {
        stream_info: Http3StreamInfo::new(udp_session_id, Http3StreamType::Http),
        ..(*wt_session).clone()
    };

    let headers = vec![Header::new(":status", "403")];
    let mut enc = Encoder::default();
    Capsule::Datagram {
        payload: Bytes::from(vec![0, 1]),
    }
    .encode(&mut enc);
    udp_server_session.send_headers(&headers).unwrap();
    udp_server_session.send_data(enc.as_ref(), now()).unwrap();
    wt.exchange_packets();

    let events: Vec<_> = wt
        .client
        .events()
        .filter_map(|e| match e {
            Http3ClientEvent::ConnectUdp(e) => Some(e),
            _ => None,
        })
        .collect();
    let closed = ConnectUdpEvent::SessionClosed {
        stream_id: udp_session_id,
        reason: CloseReason::Status(403),
        headers: Some(headers),
    };
    assert_eq!(events, [closed]);
}

#[test]
fn wt_session_close_frame_client() {
    const ERROR_NUM: u32 = 23;
    const ERROR_MESSAGE: &str = "Something went wrong";
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt.session_close_frame_client(wt_session.stream_id(), ERROR_NUM, ERROR_MESSAGE);
    wt.exchange_packets();

    wt.check_session_closed_event_server(
        &wt_session,
        &CloseReason::Clean {
            error: ERROR_NUM,
            message: ERROR_MESSAGE.to_string(),
        },
    );
}

#[test]
fn wt_session_close_frame_server() {
    const ERROR_NUM: u32 = 23;
    const ERROR_MESSAGE: &str = "Something went wrong";
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    WtTest::session_close_frame_server(&wt_session, ERROR_NUM, ERROR_MESSAGE);
    wt.exchange_packets();

    wt.check_session_closed_event_client(
        wt_session.stream_id(),
        &CloseReason::Clean {
            error: ERROR_NUM,
            message: ERROR_MESSAGE.to_string(),
        },
        None,
    );
}

#[test]
fn wt_unknown_session_frame_client() {
    const UNKNOWN_FRAME_LEN: usize = 832;
    const BUF: &[u8] = &[0; 10];
    const ERROR_NUM: u32 = 23;
    const ERROR_MESSAGE: &str = "Something went wrong";
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    // Send an unknown frame.
    let mut enc = Encoder::with_capacity(UNKNOWN_FRAME_LEN + 4);
    enc.encode_varint(1028_u64); // Arbitrary type.
    enc.encode_len(UNKNOWN_FRAME_LEN);
    let mut buf: Vec<_> = enc.into();
    buf.resize(UNKNOWN_FRAME_LEN + buf.len(), 0);
    wt.client
        .send_data(wt_session.stream_id(), &buf, now())
        .unwrap();
    wt.exchange_packets();

    // The session is still active
    let unidi_server = WtTest::create_wt_stream_server(&wt_session, StreamType::UniDi);
    wt.send_data_server(&unidi_server, BUF);
    wt.receive_data_client(unidi_server.stream_id(), true, BUF, false);

    // Now close the session.
    wt.session_close_frame_client(wt_session.stream_id(), ERROR_NUM, ERROR_MESSAGE);
    wt.exchange_packets();

    wt.check_events_after_closing_session_client(
        &[unidi_server.stream_id()],
        Some(Error::HttpRequestCancelled.code()),
        &[],
        None,
        false,
        None,
    );
    wt.check_events_after_closing_session_server(
        &[],
        None,
        &[unidi_server.stream_id()],
        Some(Error::HttpRequestCancelled.code()),
        Some(&(
            wt_session.stream_id(),
            CloseReason::Clean {
                error: ERROR_NUM,
                message: ERROR_MESSAGE.to_string(),
            },
        )),
    );
}

#[test]
fn wt_close_session_frame_broken_client() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    // Send a incorrect CloseSession frame.
    let mut enc = Encoder::default();
    WebTransportFrame::CloseSession {
        error: 5,
        message: "Hello".to_string(),
    }
    .encode(&mut enc);
    let mut buf: Vec<_> = enc.into();
    // Corrupt the string.
    buf[9] = 0xff;
    wt.client
        .send_data(wt_session.stream_id(), &buf, now())
        .unwrap();
    wt.exchange_packets();

    // check that the webtransport session is closed.
    wt.check_session_closed_event_client(
        wt_session.stream_id(),
        &CloseReason::Error(Error::HttpGeneralProtocolStream.code()),
        None,
    );
    wt.check_session_closed_event_server(
        &wt_session,
        &CloseReason::Error(Error::HttpGeneralProtocolStream.code()),
    );

    // The Http3 session is still working.
    assert_eq!(wt.client.state(), Http3State::Connected);
    assert_eq!(wt_session.state(), Http3State::Connected);
}

fn receive_request(server: &Http3Server) -> Option<Http3OrWebTransportStream> {
    while let Some(event) = server.next_event() {
        if let Http3ServerEvent::Headers { stream, .. } = event {
            return Some(stream);
        }
    }
    None
}

#[test]
#[ignore = "Is panicking at wt.create_wt_stream_client; see issue #1386"]
fn wt_close_session_cannot_be_sent_at_once() {
    const BUF: &[u8] = &[0; 443];
    const ERROR_NUM: u32 = 23;
    const ERROR_MESSAGE: &str = "Something went wrong";

    let client = default_http3_client(wt_default_parameters());
    let server = default_http3_server(wt_default_parameters());
    let mut wt = WtTest::new_with(client, server);

    let wt_session = wt.create_wt_session();

    // Fill the flow control window using an unrelated http stream.
    let req_id = wt
        .client
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    assert_eq!(req_id, 4);
    wt.exchange_packets();
    let req = receive_request(&wt.server).unwrap();
    req.send_headers(&[
        Header::new(":status", "200"),
        Header::new("content-length", BUF.len().to_string()),
    ])
    .unwrap();
    req.send_data(BUF, now()).unwrap();

    // Now close the session.
    WtTest::session_close_frame_server(&wt_session, ERROR_NUM, ERROR_MESSAGE);
    // server cannot create new streams.
    assert_eq!(
        wt_session.create_stream(StreamType::UniDi),
        Err(Error::InvalidStreamId)
    );

    let out = wt.server.process_output(now());
    let out = wt.client.process(out.dgram(), now());

    // Client has not received the full CloseSession frame and it can create more streams.
    let unidi_client = wt.create_wt_stream_client(wt_session.stream_id(), StreamType::UniDi);

    let out = wt.server.process(out.dgram(), now());
    let out = wt.client.process(out.dgram(), now());
    let out = wt.server.process(out.dgram(), now());
    let out = wt.client.process(out.dgram(), now());
    let out = wt.server.process(out.dgram(), now());
    let _out = wt.client.process(out.dgram(), now());

    wt.check_events_after_closing_session_client(
        &[],
        None,
        &[unidi_client],
        Some(Error::HttpRequestCancelled.code()),
        false,
        Some(&(
            wt_session.stream_id(),
            CloseReason::Clean {
                error: ERROR_NUM,
                message: ERROR_MESSAGE.to_string(),
            },
        )),
    );
    wt.check_events_after_closing_session_server(&[], None, &[], None, None);
}

/// Per §4.6 of the WebTransport over HTTP/3 spec, a GOAWAY from the server is
/// "a signal to applications to initiate shutdown for all WebTransport sessions":
/// <https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-13.html#name-interaction-with-the-http-3>
///
/// An active session (stream ID < `goaway_stream_id`) should receive a `Draining`
/// event and remain open — no `SessionClosed`.
#[test]
fn wt_goaway_draining_active_session() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    // Send GOAWAY with a stream ID higher than the active session, so the
    // session is considered processed (still active, but connection draining).
    let goaway_stream_id = StreamId::new(session_id.as_u64() + 4);
    wt.server.send_goaway(goaway_stream_id);
    wt.exchange_packets();

    let events: Vec<Http3ClientEvent> = wt.client.events().collect();

    let has_draining = events.iter().any(|e| {
        matches!(
            e,
            Http3ClientEvent::WebTransport(WebTransportEvent::Draining { stream_id: sid })
                if *sid == session_id
        )
    });
    assert!(has_draining, "expected Draining event for active session");

    // The active session should NOT have been closed.
    let has_closed = events.iter().any(|e| {
        matches!(
            e,
            Http3ClientEvent::WebTransport(WebTransportEvent::SessionClosed { .. })
        )
    });
    assert!(!has_closed, "active session should not be closed by GOAWAY");
}

#[test]
fn wt_goaway_keeps_streams_of_active_session() {
    const BUF: &[u8] = &[0; 10];
    let (mut wt, session_id) = wt_with_session();
    let wt_stream = wt.create_wt_stream_client(session_id, StreamType::BiDi);
    wt.send_data_client(wt_stream, BUF);
    wt.receive_data_server(wt_stream, true, BUF, false);
    wt.client.events().for_each(drop);

    wt.server.send_goaway(wt_stream);
    wt.exchange_packets();
    assert_eq!(
        wt.client.events().collect::<Vec<_>>(),
        [
            Http3ClientEvent::WebTransport(WebTransportEvent::Draining {
                stream_id: session_id
            }),
            Http3ClientEvent::GoawayReceived
        ]
    );

    wt.send_data_client(wt_stream, BUF);
    wt.receive_data_server(wt_stream, false, BUF, false);
}

/// Repeated GOAWAYs with decreasing stream IDs (per RFC 9114 §5.2) should
/// not produce duplicate Draining events for the same session.
#[test]
fn wt_goaway_repeated_no_duplicate_draining() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    // First GOAWAY — session stays active.
    let first = StreamId::new(session_id.as_u64() + 8);
    wt.server.send_goaway(first);
    wt.exchange_packets();
    wt.client.events().for_each(drop);

    // Second GOAWAY with lower stream ID — still above the session.
    let second = StreamId::new(session_id.as_u64() + 4);
    wt.server.send_goaway(second);
    wt.exchange_packets();

    let events: Vec<Http3ClientEvent> = wt.client.events().collect();
    let draining_count = events
        .iter()
        .filter(|e| {
            matches!(
                e,
                Http3ClientEvent::WebTransport(WebTransportEvent::Draining { .. })
            )
        })
        .count();
    assert_eq!(
        draining_count, 0,
        "second GOAWAY should not produce a duplicate Draining event"
    );
}

#[test]
fn wt_lower_goaway_rejects_drained_session() {
    let (mut wt, session_id) = wt_with_session();
    let first = StreamId::new(session_id.as_u64() + 4);
    wt.server.send_goaway(first);
    wt.exchange_packets();
    wt.client.events().for_each(drop);

    wt.server.send_goaway(session_id);
    wt.exchange_packets();
    assert_eq!(
        wt.client.events().collect::<Vec<_>>(),
        [Http3ClientEvent::WebTransport(
            WebTransportEvent::SessionClosed {
                stream_id: session_id,
                reason: CloseReason::Error(Error::HttpRequestRejected.code()),
                headers: None
            }
        )]
    );
}

/// Per §4.6 of the WebTransport over HTTP/3 spec, a GOAWAY from the server is
/// "a signal to applications to initiate shutdown for all WebTransport sessions":
/// <https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-13.html#name-interaction-with-the-http-3>
///
/// A rejected session (stream ID >= `goaway_stream_id`) should receive both a
/// `Draining` event and eventually a `SessionClosed` event.
#[test]
fn wt_goaway_draining_rejected_session() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    // Send GOAWAY with a stream ID equal to the session's stream ID, so the
    // session is considered NOT processed (rejected).
    wt.server.send_goaway(session_id);
    wt.exchange_packets();

    // Collect all events.
    let events: Vec<Http3ClientEvent> = wt.client.events().collect();

    let draining_pos = events
        .iter()
        .position(|e| {
            matches!(
                e,
                Http3ClientEvent::WebTransport(WebTransportEvent::Draining { stream_id: sid })
                    if *sid == session_id
            )
        })
        .expect("expected Draining event for rejected session");
    let closed_pos = events
        .iter()
        .position(|e| {
            matches!(
                e,
                Http3ClientEvent::WebTransport(WebTransportEvent::SessionClosed {
                    stream_id: sid, ..
                }) if *sid == session_id
            )
        })
        .expect("expected SessionClosed event for rejected session");
    assert!(
        draining_pos < closed_pos,
        "Draining must be emitted before SessionClosed for rejected sessions"
    );
}

#[test]
fn wt_session_protocol_none_when_no_header() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();
    assert_eq!(
        wt.client.webtransport_session_protocol(session_id).unwrap(),
        None
    );
}

#[test]
fn wt_session_protocol_quoted_value() {
    let mut wt = WtTest::new();
    let (session_id, _) =
        wt.negotiate_wt_session(&SessionAcceptAction::AcceptWith(vec![Header::new(
            "wt-protocol",
            r#""myproto""#,
        )]));
    assert_eq!(
        wt.client.webtransport_session_protocol(session_id).unwrap(),
        Some("myproto".to_string())
    );
}

#[test]
fn wt_session_protocol_parameters_stripped() {
    let mut wt = WtTest::new();
    let (session_id, _) =
        wt.negotiate_wt_session(&SessionAcceptAction::AcceptWith(vec![Header::new(
            "wt-protocol",
            r#""myproto"; foo=bar; baz=2"#,
        )]));
    assert_eq!(
        wt.client.webtransport_session_protocol(session_id).unwrap(),
        Some("myproto".to_string())
    );
}

#[test]
fn wt_session_protocol_malformed_unquoted_rejected() {
    let mut wt = WtTest::new();
    let (session_id, _) =
        wt.negotiate_wt_session(&SessionAcceptAction::AcceptWith(vec![Header::new(
            "wt-protocol",
            "myproto",
        )]));
    assert_eq!(
        wt.client.webtransport_session_protocol(session_id).unwrap(),
        None
    );
}

#[test]
fn wt_create_send_group() {
    // Test that we can create a send group for a WebTransport session.
    let (mut wt, session_id) = wt_with_session();

    let group = register_group(&mut wt, session_id);
    assert!(group.as_u64() > 0);
}

#[test]
fn wt_validate_send_group() {
    // Test that we can validate a send group belongs to a session.
    let (mut wt, session_id) = wt_with_session();

    let group = register_group(&mut wt, session_id);

    // Validate that the group belongs to this session
    let valid = wt
        .client
        .webtransport_validate_send_group(session_id, group);
    assert!(valid.is_ok());
    assert!(valid.unwrap());
}

#[test]
fn wt_cross_session_send_group_rejected() {
    // Test that a send group from one session is not valid for another session.
    let (mut wt, session_id1) = wt_with_session();

    // Create second session
    let wt_session2_id = wt.create_second_wt_session();

    // Create send group for session 1
    let group1 = register_group(&mut wt, session_id1);

    // Try to validate group1 with session2 - should return false
    let valid = wt
        .client
        .webtransport_validate_send_group(wt_session2_id, group1);
    assert!(valid.is_ok());
    assert!(!valid.unwrap());
}

#[test]
fn wt_session_protocol_invalid_stream_id() {
    let wt = WtTest::new();
    assert_eq!(
        wt.client
            .webtransport_session_protocol(StreamId::new(9999))
            .unwrap_err(),
        Error::InvalidStreamId
    );
}

#[test]
fn wt_session_protocol_non_webtransport_session() {
    let mut wt = WtTest::new();
    let stream_id = wt
        .client
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();
    assert_eq!(
        wt.client
            .webtransport_session_protocol(stream_id)
            .unwrap_err(),
        Error::InvalidStreamId
    );
}

#[test]
fn wt_create_stream_with_send_group() {
    // Test that we can create a stream with a send group.
    let (mut wt, session_id) = wt_with_session();

    // Create a send group
    let group = register_group(&mut wt, session_id);

    // Create a stream with the send group
    let stream = wt.client.webtransport_create_stream_with_send_group(
        session_id,
        StreamType::UniDi,
        Some(group),
    );
    assert!(stream.is_ok());
}

#[test]
fn wt_create_stream_without_send_group() {
    // Test that we can create a stream without a send group (backward compatibility).
    let (mut wt, session_id) = wt_with_session();

    // Create a stream without a send group
    let stream =
        wt.client
            .webtransport_create_stream_with_send_group(session_id, StreamType::UniDi, None);
    assert!(stream.is_ok());
}

#[test]
fn wt_create_stream_with_invalid_send_group() {
    // Test that creating a stream with an invalid send group fails.
    let (mut wt, session_id1) = wt_with_session();

    let wt_session2_id = wt.create_second_wt_session();

    // Create send group for session 1
    let group1 = register_group(&mut wt, session_id1);

    // Try to create stream in session2 with group from session1 - should fail
    let result = wt.client.webtransport_create_stream_with_send_group(
        wt_session2_id,
        StreamType::UniDi,
        Some(group1),
    );
    assert_eq!(result.unwrap_err(), Error::InvalidState);
}

#[test]
fn wt_set_sendgroup_on_existing_stream() {
    // Move an already-created (ungrouped) stream into a send group, then verify it is
    // still served by the scheduler.
    const DATA: &[u8] = &[0x42; 2000];
    let (mut wt, session_id) = wt_with_session();
    let group = register_group(&mut wt, session_id);

    let stream = create_stream(&mut wt, session_id, None);
    wt.client.webtransport_set_sendgroup(stream, group).unwrap();

    send_all(&mut wt, stream, DATA);
    wt.exchange_packets();
    assert!(
        data_stream_ids(&wt).contains(&stream),
        "stream moved into a send group should still be served"
    );
}

#[test]
fn wt_clear_sendgroup_keeps_stream_served() {
    // Clearing a stream's send group must move it back to the ungrouped (null-group)
    // queue, not orphan it from the scheduler.
    const DATA: &[u8] = &[0x42; 2000];
    let (mut wt, session_id) = wt_with_session();
    let group = register_group(&mut wt, session_id);

    let stream = create_stream(&mut wt, session_id, Some(group));
    wt.client.webtransport_clear_sendgroup(stream).unwrap();

    send_all(&mut wt, stream, DATA);
    wt.exchange_packets();
    assert!(
        data_stream_ids(&wt).contains(&stream),
        "stream cleared of its send group should still be served"
    );
}

#[test]
fn wt_move_stream_between_send_groups() {
    // Moving a stream from one explicit send group to another exercises the
    // remove-from-old-group then insert-into-new-group path in set_sendgroup.
    const DATA: &[u8] = &[0x42; 2000];
    let (mut wt, session_id) = wt_with_session();
    let group_a = register_group(&mut wt, session_id);
    let group_b = register_group(&mut wt, session_id);

    let stream = create_stream(&mut wt, session_id, Some(group_a));
    wt.client
        .webtransport_set_sendgroup(stream, group_b)
        .unwrap();

    send_all(&mut wt, stream, DATA);
    wt.exchange_packets();
    assert!(
        data_stream_ids(&wt).contains(&stream),
        "stream moved between send groups should still be served"
    );
}

#[test]
fn wt_set_invalid_sendgroup_on_existing_stream() {
    // A send group from another session cannot be assigned to an existing stream.
    let (mut wt, session_id1) = wt_with_session();
    let wt_session2_id = wt.create_second_wt_session();

    // group1 belongs to session 1.
    let group1 = register_group(&mut wt, session_id1);

    // A stream in session 2 must not accept session 1's group.
    let stream2 = create_stream(&mut wt, wt_session2_id, None);
    assert_eq!(
        wt.client
            .webtransport_set_sendgroup(stream2, group1)
            .unwrap_err(),
        Error::InvalidState
    );
}

#[test]
fn wt_multiple_streams_same_send_group() {
    // Test that multiple streams can belong to the same send group.
    let (mut wt, session_id) = wt_with_session();

    // Create a send group
    let group = register_group(&mut wt, session_id);

    // Create multiple streams with the same send group
    let stream1 = create_stream(&mut wt, session_id, Some(group));
    let stream2 = create_stream(&mut wt, session_id, Some(group));
    let stream3 = wt
        .client
        .webtransport_create_stream_with_send_group(session_id, StreamType::BiDi, Some(group))
        .unwrap();

    // All streams should be created successfully
    assert_ne!(stream1, stream2);
    assert_ne!(stream2, stream3);
    assert_ne!(stream1, stream3);
}

#[test]
fn wt_send_group_with_sendorder() {
    // Test that send groups work with sendOrder.
    // This test verifies streams can be created with both send groups and sendOrder set.
    let (mut wt, session_id) = wt_with_session();

    // Create two send groups
    let group1 = register_group(&mut wt, session_id);
    let group2 = register_group(&mut wt, session_id);

    // Create streams with sendOrder. According to spec, sendOrder is evaluated within the
    // context of the send group, so the two group1 streams compete with each other while
    // the group2 and ungrouped streams (same sendOrder value) live in independent namespaces.
    for (group, sendorder) in [
        (Some(group1), 100),
        (Some(group1), 200),
        (Some(group2), 100),
        (None, 100),
    ] {
        let stream = create_stream(&mut wt, session_id, group);
        set_sendorder(&mut wt, stream, sendorder);
    }

    // All operations should succeed
    // Note: The actual prioritization logic (treating groups equally, sendOrder within groups)
    // would require transport-layer changes and is beyond the scope of this test.
    // This test verifies the API works correctly.
}

#[test]
fn wt_send_groups_fair_bandwidth_allocation() {
    // Test that two send groups receive interleaved bandwidth, even when streams in
    // different groups have very different sendOrder values.
    //
    // Spec: "The user agent considers WebTransportSendGroups as equals when allocating
    // bandwidth for sending WebTransportSendStreams."
    //
    // group_high has sendOrder 1000/900; group_low has sendOrder 100/50.
    // Without group-level fairness the scheduler would prefer group_high streams
    // globally and completely starve group_low.  With per-group round-robin, the
    // server receives Data events from both groups interleaved throughout the transfer.
    //
    // We verify fairness by checking that the FIRST group_low Data event arrives
    // before the LAST group_high Data event — proving they overlap in delivery
    // (group_low is not held back until after group_high finishes).
    //
    // DATA_SIZE must produce multiple packets per stream so the interleaving is visible:
    // 10 KB / ~1280 B packets ≈ 8 packets per stream → 16+ Data events per group.
    const DATA_SIZE: usize = 10_000;
    const BUF: &[u8] = &[0x42; DATA_SIZE];

    let (mut wt, session_id) = wt_with_session();

    let group_high = register_group(&mut wt, session_id);
    let group_low = register_group(&mut wt, session_id);

    let mut grouped_streams = |group, sendorders: [SendOrder; 2]| {
        sendorders.map(|sendorder| {
            let stream = create_stream(&mut wt, session_id, Some(group));
            set_sendorder(&mut wt, stream, sendorder);
            stream
        })
    };
    let high = grouped_streams(group_high, [1000, 900]);
    let low = grouped_streams(group_low, [100, 50]);

    for &stream in high.iter().chain(low.iter()) {
        send_all(&mut wt, stream, BUF);
    }

    wt.exchange_packets();

    // Collect Data events in arrival order.  With fair round-robin scheduling the
    // server sees data from both groups interleaved (not group_high completely first).
    let events_in_order = data_stream_ids(&wt);

    // Both groups must have received data.
    let first_group_low = events_in_order.iter().position(|id| low.contains(id));
    let last_group_high = events_in_order.iter().rposition(|id| high.contains(id));

    assert!(
        first_group_low.is_some(),
        "group_low received no data; it was starved by group_high \
         (total events: {}, all for group_high)",
        events_in_order.len()
    );
    assert!(
        last_group_high.is_some(),
        "group_high received no data (total events: {})",
        events_in_order.len()
    );

    // The key fairness invariant: group_low's first delivery overlaps with group_high's
    // ongoing delivery.  If group_low was starved, its first event would come *after*
    // group_high's last event.
    assert!(
        first_group_low.unwrap() < last_group_high.unwrap(),
        "group_low was starved: first group_low event at position {} comes after \
         last group_high event at position {} (total {} events)",
        first_group_low.unwrap(),
        last_group_high.unwrap(),
        events_in_order.len()
    );
}

#[test]
fn wt_sendorder_within_send_group() {
    // Test that sendOrder prioritizes streams WITHIN a send group, and that lower-
    // priority streams are not permanently starved once higher-priority data is sent.
    //
    // Spec: "sendOrder values are evaluated within the context of the send group."
    // Spec: "this sending MUST starve until all bytes queued for sending on
    //        WebTransportSendStreams with a non-null and higher [[SendOrder]]… have
    //        been sent."  After those bytes are sent, the lower stream must proceed.
    //
    // DATA_SIZE is large enough that stream_high's data spans many packets, making
    // the ordering deterministic: server Data events for stream_high will precede
    // those for stream_low regardless of congestion-window size.

    const DATA_SIZE: usize = 50_000;
    const BUF: &[u8] = &[0x42; DATA_SIZE];

    let (mut wt, session_id) = wt_with_session();

    let group = register_group(&mut wt, session_id);

    let stream_high = create_stream(&mut wt, session_id, Some(group));
    let stream_low = create_stream(&mut wt, session_id, Some(group));

    set_sendorder(&mut wt, stream_high, 1000);
    set_sendorder(&mut wt, stream_low, 10);

    send_all(&mut wt, stream_low, BUF);
    send_all(&mut wt, stream_high, BUF);

    wt.exchange_packets();

    // Collect all Data events in arrival order.
    let ids = data_stream_ids(&wt);
    let first_readable = ids.first().copied();
    let stream_high_delivered = ids.contains(&stream_high);
    let stream_low_delivered = ids.contains(&stream_low);

    // stream_high (sendOrder=1000) must be served before stream_low (sendOrder=10).
    assert_eq!(
        first_readable,
        Some(stream_high),
        "stream with higher sendOrder should be served first within the group"
    );

    // Both streams must eventually deliver all their data: stream_high because it has
    // highest priority, stream_low because it must not be permanently starved once
    // stream_high's queued bytes have been sent.
    assert!(stream_high_delivered, "stream_high data was not delivered");
    assert!(
        stream_low_delivered,
        "stream_low was permanently starved even after stream_high exhausted its data"
    );
}

#[test]
fn wt_ungrouped_streams_independent_namespace() {
    // Test that ungrouped streams have their own sendOrder namespace,
    // independent from grouped streams.

    const DATA_SIZE: usize = 5_000;
    const BUF: &[u8] = &[0x42; DATA_SIZE];

    let (mut wt, session_id) = wt_with_session();

    // Create a send group
    let group = register_group(&mut wt, session_id);

    // Create a grouped stream with sendOrder 100
    let stream_grouped = create_stream(&mut wt, session_id, Some(group));
    set_sendorder(&mut wt, stream_grouped, 100);

    // Create an ungrouped stream with sendOrder 100 (same value, different namespace)
    let stream_ungrouped = create_stream(&mut wt, session_id, None);
    set_sendorder(&mut wt, stream_ungrouped, 100);

    // Fill both streams
    send_all(&mut wt, stream_grouped, BUF);
    send_all(&mut wt, stream_ungrouped, BUF);

    wt.exchange_packets();

    // Both streams should become readable despite having the same sendOrder value,
    // because they're in different namespaces (grouped vs ungrouped).
    let ids = data_stream_ids(&wt);
    let grouped_readable = ids.contains(&stream_grouped);
    let ungrouped_readable = ids.contains(&stream_ungrouped);

    // Both should have received data
    assert!(grouped_readable, "grouped stream should receive data");
    assert!(ungrouped_readable, "ungrouped stream should receive data");
}

#[test]
fn wt_multiple_groups_separate_sendorder_namespaces() {
    // Test that different send groups maintain separate sendOrder namespaces.
    // Streams in different groups with the same sendOrder value should not interfere.

    const DATA_SIZE: usize = 5_000;
    const BUF: &[u8] = &[0x42; DATA_SIZE];

    let (mut wt, session_id) = wt_with_session();

    // Create three send groups, one stream in each, all with the SAME sendOrder value
    // (different namespaces!).
    let mut streams = Vec::new();
    for _ in 0..3 {
        let group = register_group(&mut wt, session_id);
        let stream = create_stream(&mut wt, session_id, Some(group));
        set_sendorder(&mut wt, stream, 500);
        streams.push(stream);
    }

    // Fill all streams
    for &stream in &streams {
        send_all(&mut wt, stream, BUF);
    }

    wt.exchange_packets();

    // All three streams should become readable, demonstrating that they're in
    // separate namespaces and don't compete with each other based on sendOrder alone.
    let ids = data_stream_ids(&wt);
    for (i, &stream) in streams.iter().enumerate() {
        assert!(
            ids.contains(&stream),
            "stream in group{} should receive data",
            i + 1
        );
    }
}

#[test]
fn wt_session_stats_initial() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    let stats = wt.client.webtransport_session_stats(session_id).unwrap();
    assert_eq!(stats, SessionStats::default());
}
