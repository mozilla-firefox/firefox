// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::{event::Provider as _, header::HeadersExt as _};
use neqo_http3::{Error, Header, Http3ClientEvent, Http3ServerEvent, Priority};
use test_fixture::{default_http3_client, default_http3_server, exchange_packets, now};

const AUTHORITY: &str = "something.com:443";

#[test]
fn classic_connect() {
    // Connect a client and a server.
    let mut client = default_http3_client();
    let mut server = default_http3_server();
    let out = test_fixture::connect_peers(&mut client, &mut server);
    assert_eq!(server.process(out, now()).dgram(), None);

    // Ignore all events so far.
    drop(server.events());
    client.events();

    // Have client send a CONNECT request.
    let stream_id = client
        .connect(now(), AUTHORITY, &[], Priority::default())
        .unwrap();
    client.send_data(stream_id, b"ping", now()).unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    let Some(Http3ServerEvent::Headers { headers, .. }) = server.next_event() else {
        panic!("Expected Headers event");
    };
    assert_eq!(
        headers.find_header(":method").map(Header::value),
        Some(b"CONNECT".as_ref())
    );
    assert_eq!(
        headers.find_header(":authority").map(Header::value),
        Some(AUTHORITY.as_bytes())
    );
    // > The :scheme and :path pseudo-header fields are omitted
    //
    // <https://datatracker.ietf.org/doc/html/rfc9114#section-4.4>
    assert_eq!(headers.find_header(":scheme"), None);
    assert_eq!(headers.find_header(":path"), None);

    let Some(Http3ServerEvent::Data { stream, data, .. }) = server.next_event() else {
        panic!("Expected Data event");
    };
    assert_eq!(stream.stream_id(), stream_id);
    assert_eq!(data, b"ping");

    // Have server respond.
    stream
        .send_headers(&[Header::new(":status", "200")])
        .unwrap();
    stream.send_data(b"pong", now()).unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    // Ignore some client events.
    let mut next_event = None;
    while let Some(event) = client.next_event() {
        match event {
            Http3ClientEvent::StateChange { .. }
            | Http3ClientEvent::RequestsCreatable
            | Http3ClientEvent::ResumptionToken { .. }
            | Http3ClientEvent::DataWritable { .. } => {}
            e => {
                next_event = Some(e);
                break;
            }
        }
    }

    let Http3ClientEvent::HeaderReady {
        stream_id, headers, ..
    } = next_event.unwrap()
    else {
        unreachable!()
    };
    assert_eq!(stream_id, stream.stream_id());
    assert_eq!(
        headers.find_header(":status").map(Header::value),
        Some(b"200".as_ref())
    );

    let Some(Http3ClientEvent::DataReadable { stream_id }) = client.next_event() else {
        unreachable!()
    };
    assert_eq!(stream_id, stream.stream_id());
    let mut data = vec![0; 4];
    let (amount, _fin) = client.read_data(now(), stream_id, &mut data).unwrap();
    assert_eq!(&data[..amount], b"pong");
}

#[test]
fn classic_connect_via_fetch_panics_in_debug() {
    let mut client = default_http3_client();
    let mut server = default_http3_server();
    let out = test_fixture::connect_peers(&mut client, &mut server);
    assert_eq!(server.process(out, now()).dgram(), None);

    let res = client.fetch(
        now(),
        "CONNECT",
        &("https", AUTHORITY, "/"),
        &[],
        Priority::default(),
    );
    assert_eq!(res, Err(Error::InvalidInput));
}

#[test]
fn classic_connect_invalid_header_value() {
    let mut client = default_http3_client();
    let mut server = default_http3_server();
    let out = test_fixture::connect_peers(&mut client, &mut server);
    assert_eq!(server.process(out, now()).dgram(), None);

    let headers = [Header::new("x", "a\rb")];
    let res = client.connect(now(), AUTHORITY, &headers, Priority::default());
    assert_eq!(res, Err(Error::InvalidInput));
    let res = client.connect(now(), AUTHORITY, &[], Priority::default());
    assert_eq!(res.unwrap(), 0);
}
