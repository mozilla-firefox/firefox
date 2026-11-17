// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_http3::{Http3Client, Http3Server, Priority, StreamId};
use test_fixture::*;

/// Connect a client and server, send a GET request from the client,
/// and exchange packets so the server receives it.
pub fn connect_and_send_request(close_sending_side: bool) -> (Http3Client, Http3Server, StreamId) {
    let mut client = default_http3_client();
    let mut server = default_http3_server();
    let dgram = connect_peers(&mut client, &mut server);

    let stream_id = client
        .fetch(
            now(),
            "GET",
            &("https", "something.com", "/"),
            &[],
            Priority::default(),
        )
        .unwrap();

    if close_sending_side {
        client.stream_close_send(stream_id, now()).unwrap();
    }

    let out = client.process(dgram, now());
    let out = server.process(out.dgram(), now());
    drop(client.process(out.dgram(), now()));

    (client, server, stream_id)
}
