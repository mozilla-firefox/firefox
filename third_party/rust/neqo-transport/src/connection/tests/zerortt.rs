// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cell::RefCell, rc::Rc, time::Duration};

use neqo_common::{event::Provider as _, to_u64};
use nss::{AllowZeroRtt, AntiReplay};
use test_fixture::{assertions, now};
use tracing::debug;

use super::{
    super::Connection, CountingConnectionIdGenerator, Output, connect, default_client,
    default_server, exchange_ticket, migration::NewConnectionIds, new_server, resumed_server,
};
use crate::{
    ConnectionParameters, Error, MIN_INITIAL_PACKET_SIZE, State, StreamDataLimit, StreamType,
    Version, events::ConnectionEvent,
};

#[test]
fn zero_rtt_negotiate() {
    // Note that the two servers in this test will get different anti-replay filters.
    // That's OK because we aren't testing anti-replay.
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client
        .enable_resumption(now(), token)
        .expect("should set token");
    let mut server = resumed_server(&client);
    connect(&mut client, &mut server);
    assert!(client.tls_info().unwrap().early_data_accepted());
    assert!(server.tls_info().unwrap().early_data_accepted());
}

#[test]
fn zero_rtt_send_recv() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client
        .enable_resumption(now(), token)
        .expect("should set token");
    let mut server = resumed_server(&client);

    // Send ClientHello.
    let client_hs = client.process_output(now());
    let client_hs2 = client.process_output(now());
    assert!(client_hs.as_dgram_ref().is_some() && client_hs2.as_dgram_ref().is_some());

    // Wait
    let delay = client.process_output(now()).callback();

    // Now send a 0-RTT packet.
    let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id, &[1, 2, 3]).unwrap();
    let client_0rtt = client.process_output(now() + delay);
    assert!(client_0rtt.as_dgram_ref().is_some());
    // 0-RTT packets on their own shouldn't be padded to MIN_INITIAL_PACKET_SIZE.
    assert!(client_0rtt.as_dgram_ref().unwrap().len() < MIN_INITIAL_PACKET_SIZE);

    server.process_input(client_hs.dgram().unwrap(), now());
    let server_hs = server.process(client_hs2.dgram(), now());
    assert!(server_hs.as_dgram_ref().is_some()); // ServerHello, etc...

    let all_frames = server.stats().frame_tx.all();
    let ack_frames = server.stats().frame_tx.ack;
    let server_process_0rtt = server.process(client_0rtt.dgram(), now());
    assert!(server_process_0rtt.dgram().is_some());
    assert_eq!(server.stats().frame_tx.all(), all_frames + 4);
    assert_eq!(server.stats().frame_tx.ack, ack_frames + 1);
    assert_eq!(server.stats().frame_tx.padding, 1); // In the 1-RTT packet.

    let server_stream_id = server
        .events()
        .find_map(|evt| match evt {
            ConnectionEvent::NewStream { stream_id, .. } => Some(stream_id),
            _ => None,
        })
        .expect("should have received a new stream event");
    assert_eq!(client_stream_id, server_stream_id.as_u64());
}

#[test]
fn zero_rtt_send_coalesce() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client
        .enable_resumption(now(), token)
        .expect("should set token");
    let mut server = resumed_server(&client);

    // Write 0-RTT before generating any packets.
    // This should result in a datagram that coalesces Initial and 0-RTT.
    let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id, &[1, 2, 3]).unwrap();
    let client_init = client.process_output(now());
    assert!(client_init.as_dgram_ref().is_some());
    let client_0rtt = client.process_output(now());
    assert!(client_0rtt.as_dgram_ref().is_some());

    assertions::assert_coalesced_0rtt(&client_0rtt.as_dgram_ref().unwrap()[..]);

    _ = server.process(client_init.dgram(), now());
    let server_hs = server.process(client_0rtt.dgram(), now());
    assert!(server_hs.as_dgram_ref().is_some()); // Should produce ServerHello etc...

    let server_stream_id = server
        .events()
        .find_map(|evt| match evt {
            ConnectionEvent::NewStream { stream_id } => Some(stream_id),
            _ => None,
        })
        .expect("should have received a new stream event");
    assert_eq!(client_stream_id, server_stream_id.as_u64());
}

#[test]
fn zero_rtt_before_resumption_token() {
    let mut client = default_client();
    assert!(client.stream_create(StreamType::BiDi).is_err());
}

/// A resuming client and a server whose fresh anti-replay context makes it
/// reject that client's 0-RTT. `max_data` sets the resumed server's
/// flow-control limit; it has no bearing on the rejection itself, which the
/// fresh anti-replay context forces regardless.
fn resumed_pair_that_rejects_0rtt(max_data: usize) -> (Connection, Connection) {
    const MESSAGE: &[u8] = &[1, 2, 3];
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(MESSAGE.len())));
    connect(&mut client, &mut server);

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client
        .enable_resumption(now(), token)
        .expect("should set token");
    let mut server = Connection::new_server(
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        ConnectionParameters::default()
            .max_data(to_u64(max_data))
            .versions(client.version(), Version::all()),
    )
    .unwrap();
    // Using a freshly initialized anti-replay context
    // should result in the server rejecting 0-RTT.
    let ar =
        AntiReplay::new(now(), test_fixture::ANTI_REPLAY_WINDOW, 1, 3).expect("setup anti-replay");
    server
        .server_enable_0rtt(&ar, AllowZeroRtt {})
        .expect("enable 0-RTT");
    (client, server)
}

#[test]
fn zero_rtt_send_reject() {
    for max_data in [2, 3, 6] {
        for stream_type in [StreamType::UniDi, StreamType::BiDi] {
            zero_rtt_send_reject_with_max_data(max_data, stream_type);
        }
    }
}

fn zero_rtt_send_reject_with_max_data(max_data: usize, stream_type: StreamType) {
    const MESSAGE: &[u8] = &[1, 2, 3];

    let (mut client, mut server) = resumed_pair_that_rejects_0rtt(max_data);

    // Exhaust the connection's remembered flow-control limit with 0-RTT data.
    let stream_id = client.stream_create(stream_type).unwrap();
    assert_eq!(
        client.stream_send(stream_id, MESSAGE).unwrap(),
        MESSAGE.len()
    );
    assert_eq!(client.stream_avail_send_space(stream_id).unwrap(), 0);

    // Send ClientHello.
    let client_hs = client.process_output(now());
    assert!(client_hs.as_dgram_ref().is_some());

    let client_0rtt = client.process_output(now());
    assert!(client_0rtt.as_dgram_ref().is_some());

    let server_hs = server.process(client_hs.dgram(), now());
    assert!(server_hs.as_dgram_ref().is_some()); // Should produce ServerHello etc...
    let server_hs2 = server.process(client_0rtt.dgram(), now());

    // The server shouldn't receive that 0-RTT data.
    let recvd_stream_evt = |e| matches!(e, ConnectionEvent::NewStream { .. });
    assert!(!server.events().any(recvd_stream_evt));

    _ = client.process(server_hs.dgram(), now());
    let dgram = client.process(server_hs2.dgram(), now()).dgram();
    let dgram = server.process(dgram, now()).dgram();

    // Client should get a rejection.
    let client_fin = client.process(dgram, now());
    let recvd_0rtt_reject = |e| e == ConnectionEvent::ZeroRttRejected;
    assert!(client.events().any(recvd_0rtt_reject));

    // Server consume client_fin
    let server_ack = server.process(client_fin.dgram(), now());
    assert!(server_ack.as_dgram_ref().is_some());
    let client_out = client.process(server_ack.dgram(), now());
    assert!(client_out.as_dgram_ref().is_none());

    // ...and the client stream should be gone.
    let res = client.stream_send(stream_id, MESSAGE);
    assert!(res.is_err());
    assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

    // A new stream can use the full 1-RTT limit, even if it is lower than before.
    let stream_id_after_reject = client.stream_create(stream_type).unwrap();
    assert_eq!(stream_id, stream_id_after_reject);
    assert_eq!(
        client
            .stream_avail_send_space(stream_id_after_reject)
            .unwrap(),
        max_data
    );
    let message = vec![1; max_data];
    assert_eq!(
        client
            .stream_send(stream_id_after_reject, &message)
            .unwrap(),
        message.len()
    );
    let client_after_reject = client.process_output(now()).dgram();
    assert!(client_after_reject.is_some());

    // The server should receive new stream
    server.process_input(client_after_reject.unwrap(), now());
    assert!(server.events().any(recvd_stream_evt));
    let mut received = vec![0; max_data];
    assert_eq!(
        server
            .stream_recv(stream_id_after_reject, &mut received)
            .unwrap(),
        (message.len(), false)
    );
    assert_eq!(received, message);
}

/// A limit raised before 0-RTT is rejected survives the rejection and is re-advertised in 1-RTT.
#[test]
fn zero_rtt_reject_after_set_remote_max_streams() {
    let (mut client, mut server) = resumed_pair_that_rejects_0rtt(3);

    client.set_remote_max_streams(StreamType::BiDi, 1000);
    client.set_remote_max_streams(StreamType::UniDi, 1000);

    let client_hs = client.process_output(now());
    assert!(client_hs.as_dgram_ref().is_some());
    let client_0rtt = client.process_output(now());
    assert!(client_0rtt.as_dgram_ref().is_some());

    let server_hs = server.process(client_hs.dgram(), now());
    let server_hs2 = server.process(client_0rtt.dgram(), now());

    _ = client.process(server_hs.dgram(), now());
    let dgram = client.process(server_hs2.dgram(), now()).dgram();
    let dgram = server.process(dgram, now()).dgram();

    // `zero_rtt_rejected` runs here with the limits already raised.
    let client_fin = client.process(dgram, now());
    let recvd_0rtt_reject = |e| e == ConnectionEvent::ZeroRttRejected;
    assert!(client.events().any(recvd_0rtt_reject));

    // The MAX_STREAMS frames went out in the rejected 0-RTT packet; they
    // must be re-sent in 1-RTT so the server ends up with the raised limit.
    let mut dgram = client_fin.dgram();
    while let Some(d) = dgram {
        dgram = server.process(Some(d), now()).dgram();
        dgram = dgram.and_then(|d| client.process(Some(d), now()).dgram());
    }
    for st in [StreamType::BiDi, StreamType::UniDi] {
        let opened = std::iter::repeat_with(|| server.stream_create(st))
            .take_while(Result::is_ok)
            .take(1001)
            .count();
        assert_eq!(opened, 1000, "{st:?}");
    }
}

#[test]
fn zero_rtt_update_flow_control() {
    const LOW: u64 = 3;
    const HIGH: u64 = 10;
    #[expect(clippy::cast_possible_truncation, reason = "small value will fit")]
    const MESSAGE: &[u8] = &[0; HIGH as usize];

    let mut client = default_client();
    let mut server = new_server(
        ConnectionParameters::default()
            .max_stream_data(StreamDataLimit::UniDi, LOW)
            .max_stream_data(StreamDataLimit::BiDiRemote, LOW),
    );
    connect(&mut client, &mut server);

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client
        .enable_resumption(now(), token)
        .expect("should set token");
    let mut server = new_server(
        ConnectionParameters::default()
            .max_stream_data(StreamDataLimit::UniDi, HIGH)
            .max_stream_data(StreamDataLimit::BiDiRemote, HIGH)
            .versions(client.version, Version::all()),
    );

    // Stream limits should be low for 0-RTT.
    let client_hs = client.process_output(now()).dgram();
    let client_hs2 = client.process_output(now()).dgram();
    let uni_stream = client.stream_create(StreamType::UniDi).unwrap();
    assert!(!client.stream_send_atomic(uni_stream, MESSAGE).unwrap());
    let bidi_stream = client.stream_create(StreamType::BiDi).unwrap();
    assert!(!client.stream_send_atomic(bidi_stream, MESSAGE).unwrap());

    // Now get the server transport parameters.
    server.process_input(client_hs.unwrap(), now());
    let server_hs = server.process(client_hs2, now()).dgram();
    let client_hs3 = client.process(server_hs, now()).dgram();
    let server_hs2 = server.process(client_hs3, now()).dgram();
    client.process_input(server_hs2.unwrap(), now());

    // The streams should report a writeable event.
    let mut uni_stream_event = false;
    let mut bidi_stream_event = false;
    for e in client.events() {
        if let ConnectionEvent::SendStreamWritable { stream_id } = e {
            if stream_id.is_uni() {
                uni_stream_event = true;
            } else {
                bidi_stream_event = true;
            }
        }
    }
    assert!(uni_stream_event);
    assert!(bidi_stream_event);
    // But no MAX_STREAM_DATA frame was received.
    assert_eq!(client.stats().frame_rx.max_stream_data, 0);

    // And the new limit applies.
    assert!(client.stream_send_atomic(uni_stream, MESSAGE).unwrap());
    assert!(client.stream_send_atomic(bidi_stream, MESSAGE).unwrap());
}

#[test]
fn zero_rtt_loss_accepted() {
    // This test requires a wider anti-replay window than other tests
    // because the dropped 0-RTT packets add a bunch of delay.
    const WINDOW: Duration = Duration::from_secs(20);
    for i in 0..5 {
        let mut client = default_client();
        let mut server = default_server();
        connect(&mut client, &mut server);

        let mut now = now();
        let earlier = now;

        let token = exchange_ticket(&mut client, &mut server, now);

        now += WINDOW;
        let mut client = default_client();
        client.enable_resumption(now, token).unwrap();
        let mut server = resumed_server(&client);
        let anti_replay = AntiReplay::new(earlier, WINDOW, 1, 3).unwrap();
        server
            .server_enable_0rtt(&anti_replay, AllowZeroRtt {})
            .unwrap();

        // Make CI/0-RTT
        let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
        client.stream_send(client_stream_id, &[1, 2, 3]).unwrap();
        let mut ci = client.process_output(now);
        assert!(ci.as_dgram_ref().is_some());
        let mut c0rtt = client.process_output(now);
        assert!(c0rtt.as_dgram_ref().is_some());
        assertions::assert_coalesced_0rtt(&c0rtt.as_dgram_ref().unwrap()[..]);

        // Drop CI/0-RTT a number of times
        debug!("Drop CI/0-RTT {i} extra times");
        for _ in 0..i {
            now += client.process_output(now).callback();
            ci = client.process_output(now);
            assert!(ci.as_dgram_ref().is_some());
            c0rtt = client.process_output(now);
            assert!(c0rtt.as_dgram_ref().is_some());
        }

        // Process CI/0-RTT
        server.process_input(ci.dgram().unwrap(), now);
        let si = server.process(c0rtt.dgram(), now);
        assert!(si.as_dgram_ref().is_some());

        let server_stream_id = server
            .events()
            .find_map(|evt| match evt {
                ConnectionEvent::NewStream { stream_id } => Some(stream_id),
                _ => None,
            })
            .expect("should have received a new stream event");
        assert_eq!(client_stream_id, server_stream_id.as_u64());

        // 0-RTT should be accepted
        client.process_input(si.dgram().unwrap(), now);
        let recvd_0rtt_reject = |e| e == ConnectionEvent::ZeroRttRejected;
        assert!(
            !client.events().any(recvd_0rtt_reject),
            "rejected 0-RTT after {i} extra dropped packets"
        );
    }
}

/// Before the first ACK within a packet space, there should be no time
/// threshold based loss detection:
///
/// > Once a later packet within the same packet number space has been
/// > acknowledged, an endpoint SHOULD declare an earlier packet lost if it was
/// > sent a threshold amount of time in the past.
///
/// <https://www.rfc-editor.org/rfc/rfc9002.html#section-6.1.2>
///
/// Previously Neqo client would prematurely declare 0-RTT stream data frame as
/// lost based on time threshold.
///
/// See <https://github.com/mozilla/neqo/issues/2476> for details.
#[test]
fn zero_rtt_no_time_threshold_loss_detection_before_first_ack() {
    const RTT: Duration = Duration::from_millis(4);
    let mut now = now();
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let token = exchange_ticket(&mut client, &mut server, now);

    let mut client = default_client();
    client
        .enable_resumption(now, token)
        .expect("should set token");
    let mut server = resumed_server(&client);

    // Send ClientHello.
    let client_hs = client.process_output(now);
    let client_hs2 = client.process_output(now);
    assert!(client_hs.as_dgram_ref().is_some() && client_hs2.as_dgram_ref().is_some());

    // Attempt sending 0-RTT stream data.
    let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id, &[1, 2, 3]).unwrap();
    // Blocked due to pacing.
    let delay = client.process_output(now).callback();
    // Wait for pacer, then send.
    let client_0rtt = client.process_output(now + delay);
    assert!(client_0rtt.as_dgram_ref().is_some());

    now += RTT / 2;

    // Receive ClientHello.
    server.process_input(client_hs.dgram().unwrap(), now);
    let server_hs = server.process(client_hs2.dgram(), now);
    assert!(server_hs.as_dgram_ref().is_some()); // ServerHello, etc...

    now += delay;

    // Receive 0-RTT stream data.
    let server_process_0rtt = server.process(client_0rtt.dgram(), now);
    assert!(server_process_0rtt.as_dgram_ref().is_some());

    let server_stream_id = server
        .events()
        .find_map(|evt| match evt {
            ConnectionEvent::NewStream { stream_id, .. } => Some(stream_id),
            _ => None,
        })
        .expect("should have received a new stream event");
    assert_eq!(client_stream_id, server_stream_id.as_u64());

    now += RTT / 2;

    // Process Server packets. Note that this will not contain an ACK for the
    // 0-RTT stream data, given that the server does not yet have 1-RTT keys and
    // it will delay the ACK.
    let _client_pkt1 = client.process(server_hs.dgram(), now);
    let _client_pkt2 = client.process(server_process_0rtt.dgram(), now);
    assert!(matches!(client.process_output(now), Output::Callback(_)));

    // Progress time by half an RTT. Thus ~1.5 RTT have elapsed since the client
    // sent the 0-RTT application data. ~1.5 RTT is past the time threshold
    // based loss detection (~ 9/8 RTT).
    now += RTT / 2;

    // Despite the missing ACK from the server, expect client to neither declare
    // 0-RTT stream data lost, nor attempt to retransmit it. Given that thus far
    // no Application space ACK has been received, time threshold based loss
    // detection does not yet apply.
    assert!(client.process_output(now).dgram().is_none());
    assert_eq!(client.stats().lost, 0);
}

/// A `NEW_CONNECTION_ID` frame in 0-RTT can retire the connection ID from the handshake.
#[test]
fn zero_rtt_retire_handshake_cid() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client
        .enable_resumption(now(), token)
        .expect("should set token");
    let mut server = resumed_server(&client);

    let client_hs = client.process_output(now());
    let client_hs2 = client.process_output(now());
    let delay = client.process_output(now()).callback();
    let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id, &[1, 2, 3]).unwrap();
    let cid_gen = Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    let client_0rtt =
        client.test_write_frames(NewConnectionIds::retire_all(cid_gen), now() + delay);

    server.process_input(client_hs.dgram().unwrap(), now());
    let server_hs = server.process(client_hs2.dgram(), now());
    let server_hs2 = server.process_output(now());
    let new_cid_before = server.stats().frame_rx.new_connection_id;
    server.process_input(client_0rtt.dgram().unwrap(), now());
    assert_eq!(
        server.stats().frame_rx.new_connection_id,
        new_cid_before + 1
    );
    assert_eq!(*server.state(), State::Handshaking);

    client.process_input(server_hs.dgram().unwrap(), now());
    let client_ack = client.process(server_hs2.dgram(), now());
    let client_fin = client.process_output(now());
    server.process_input(client_ack.dgram().unwrap(), now());
    server.process_input(client_fin.dgram().unwrap(), now());
    assert_eq!(*server.state(), State::Confirmed);

    let retire_cid_before = server.stats().frame_tx.retire_connection_id;
    assert!(server.process_output(now()).dgram().is_some());
    assert_eq!(
        server.stats().frame_tx.retire_connection_id,
        retire_cid_before + 2 // The handshake CID and the client's first NEW_CONNECTION_ID.
    );
}
