// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::event::Provider as _;
use neqo_http3::{
    Header, Http3Client, Http3ClientEvent, Http3Server, Http3ServerEvent, Http3State, Priority,
};
use nss::AuthenticationStatus;
use test_fixture::*;

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
    _ = server.process(out.dgram(), now());
}

fn connect() -> (Http3Client, Http3Server) {
    let mut client = default_http3_client();
    let mut server = default_http3_server();
    connect_with(&mut client, &mut server);
    (client, server)
}

#[test]
fn priority_update() {
    let (mut client, mut server) = connect();
    let stream_id = client
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[Header::new("priority", "u=4,i")],
            Priority::new(4, true),
        )
        .unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    // get event of the above request, skipping events of the connection setup
    let header_event = loop {
        let event = server.next_event().unwrap();
        if matches!(event, Http3ServerEvent::Headers { .. }) {
            break event;
        }
    };

    match header_event {
        Http3ServerEvent::Headers { headers, fin, .. } => {
            let expected_headers = &[
                Header::new(":method", "GET"),
                Header::new(":scheme", "https"),
                Header::new(":authority", "something.com"),
                Header::new(":path", "/"),
                Header::new("priority", "u=4,i"),
            ];
            assert_eq!(&headers, expected_headers);
            assert!(!fin);
        }
        other => panic!("unexpected server event: {other:?}"),
    }

    let update_priority = Priority::new(3, false);
    client.priority_update(stream_id, update_priority).unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    let found = server.events().any(|e| {
        if let Http3ServerEvent::PriorityUpdate {
            stream_id: update_id,
            priority,
        } = e
        {
            assert_eq!(update_id, stream_id);
            assert_eq!(priority, update_priority);
            true
        } else {
            false
        }
    });
    assert!(found);
}

#[test]
fn priority_update_dont_send_for_cancelled_stream() {
    let (mut client, mut server) = connect();
    let stream_id = client
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[Header::new("priority", "u=5")],
            Priority::new(5, false),
        )
        .unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    let update_priority = Priority::new(6, false);
    client.priority_update(stream_id, update_priority).unwrap();
    client.cancel_fetch(stream_id, 11).unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    while let Some(event) = server.next_event() {
        if matches!(event, Http3ServerEvent::PriorityUpdate { .. }) {
            panic!("Priority update sent on cancelled stream");
        }
    }
}
