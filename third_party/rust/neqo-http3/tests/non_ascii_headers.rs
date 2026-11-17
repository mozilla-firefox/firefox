// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![expect(clippy::unwrap_used, reason = "OK in test code.")]

use neqo_common::{event::Provider as _, header::HeadersExt as _};
use neqo_http3::{Header, Http3ClientEvent, Http3ServerEvent, Priority};
use test_fixture::{default_http3_client, default_http3_server, exchange_packets, now};

fn echo_header(request_header_name: &str, response_header_name: &str, test_data: &[u8]) {
    // Connect a client and a server.
    let mut client = default_http3_client();
    let mut server = default_http3_server();
    let out = test_fixture::connect_peers(&mut client, &mut server);
    assert_eq!(server.process(out, now()).dgram(), None);

    // Ignore all events so far.
    drop(server.events());

    // Create a header with the test data
    let custom_header = Header::new(request_header_name, test_data);

    // Have client send a GET request with the custom header
    let stream_id = client
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[custom_header],
            Priority::default(),
        )
        .unwrap();
    client.stream_close_send(stream_id, now()).unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    // Server receives the request - loop through events to find Headers
    let mut received_stream = None;
    let mut received_headers = None;
    while let Some(event) = server.next_event() {
        if let Http3ServerEvent::Headers {
            stream, headers, ..
        } = event
        {
            received_stream = Some(stream);
            received_headers = Some(headers);
            break;
        }
    }

    let stream = received_stream.expect("No Headers event received from server");
    let headers = received_headers.expect("No headers received");

    // Verify the server received the header correctly
    let received_header = headers
        .find_header(request_header_name)
        .expect("Custom header not found");
    assert_eq!(received_header.value(), test_data);

    // Server echoes the value back in a different header
    stream
        .send_headers(&[
            Header::new(":status", "200"),
            Header::new(response_header_name, received_header.value()),
        ])
        .unwrap();
    stream.stream_close_send(now()).unwrap();
    exchange_packets(&mut client, &mut server, false, None);

    // Client receives the response
    let mut response_headers = None;
    while let Some(event) = client.next_event() {
        if let Http3ClientEvent::HeaderReady { headers, .. } = event {
            response_headers = Some(headers);
            break;
        }
    }

    let headers = response_headers.expect("No response headers received");

    // Verify the echoed header contains the original data
    let echoed_header = headers
        .find_header(response_header_name)
        .expect("Echoed header not found");
    assert_eq!(echoed_header.value(), test_data);
}

#[test]
fn extended_ascii_non_utf8_header_echo() {
    // Create a header with binary data
    let test_bytes: Vec<u8> = vec![0xE4]; // "ä" in extended ASCII (ISO-8859-1), invalid UTF-8
    echo_header("x-custom-data", "x-echoed-data", &test_bytes);
}

#[test]
fn non_ascii_emoji_header_echo() {
    // Create a header with non-ASCII but valid UTF-8 (emojis: rocket + star + laptop)
    let emoji_data = b"\xF0\x9F\x9A\x80\xF0\x9F\x8C\x9F\xF0\x9F\x92\xBB";
    echo_header("x-emoji-data", "x-echoed-emoji", emoji_data);
}
