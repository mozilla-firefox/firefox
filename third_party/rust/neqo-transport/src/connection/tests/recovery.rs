// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::time::{Duration, Instant};

use nss::AuthenticationStatus;
use test_fixture::{
    assertions::{
        assert_contains_handshake, assert_handshake, assert_initial, is_handshake, is_initial,
    },
    now, split_datagram,
};
use tracing::debug;

use super::{
    super::{Connection, ConnectionParameters, Output, State},
    AT_LEAST_PTO, DEFAULT_ADDR, DEFAULT_RTT, DEFAULT_STREAM_DATA, POST_HANDSHAKE_CWND,
    assert_error, assert_full_cwnd, connect, connect_force_idle, connect_rtt_idle,
    connect_with_rtt, cwnd, default_client, default_server, fill_cwnd, maybe_authenticate,
    new_client, new_client_with_qlog, new_server, send_and_receive, send_something,
};
use crate::{
    CloseReason, Error, Pmtud, Stats, StreamType,
    connection::{test_internal::FrameWriter, tests::cwnd_min},
    frame::FrameType,
    packet,
    recovery::{
        FAST_PTO_SCALE, MAX_OUTSTANDING_UNACK, MAX_PTO_PACKET_COUNT, MIN_OUTSTANDING_UNACK,
    },
    rtt::GRANULARITY,
    tparams::{TransportParameter, TransportParameterId::*},
    tracking::{DEFAULT_LOCAL_ACK_DELAY, DEFAULT_REMOTE_ACK_DELAY},
};

#[test]
fn pto_works_basic() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut now = now();

    let cb = client.process_output(now).callback();
    assert_eq!(cb, ConnectionParameters::DEFAULT_IDLE_TIMEOUT);

    // Send data on two streams
    let stream1 = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.stream_send(stream1, b"hello").unwrap(), 5);
    assert_eq!(client.stream_send(stream1, b" world!").unwrap(), 7);

    let stream2 = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.stream_send(stream2, b"there!").unwrap(), 6);

    // Send a packet after some time.
    now += Duration::from_secs(10);
    let out = client.process_output(now);
    assert!(out.dgram().is_some());

    // Nothing to do, should return callback
    let out = client.process_output(now);
    assert!(matches!(out, Output::Callback(_)));

    // One second later, it should want to send PTO packet
    now += AT_LEAST_PTO;
    let out = client.process_output(now);

    let stream_before = server.stats().frame_rx.stream;
    server.process_input(out.dgram().unwrap(), now);
    assert_eq!(server.stats().frame_rx.stream, stream_before + 2);
}

#[test]
fn pto_works_full_cwnd() {
    let mut client = default_client();
    let mut server = default_server();
    let now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);

    // Send lots of data.
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    let (dgrams, now) = fill_cwnd(&mut client, stream_id, now);
    assert_full_cwnd(&dgrams, POST_HANDSHAKE_CWND, client.plpmtu());

    // Fill the CWND after waiting for a PTO.
    let (dgrams, now) = fill_cwnd(&mut client, stream_id, now + AT_LEAST_PTO);
    // Two packets in the PTO.
    // The first should be full sized; the second might be small.
    assert_eq!(dgrams.len(), 2);
    assert_eq!(dgrams[0].len(), client.plpmtu());

    // Both datagrams contain one or more STREAM frames.
    for d in dgrams {
        let stream_before = server.stats().frame_rx.stream;
        server.process_input(d, now);
        assert!(server.stats().frame_rx.stream > stream_before);
    }
}

/// Basic coverage for the byte/RTT stats fields: they should reflect real
/// values after a round-tripped transfer, not just their defaults.
#[test]
fn stats_populated_after_transfer() {
    let mut client = default_client();
    let mut server = default_server();
    let now = connect_with_rtt(&mut client, &mut server, now(), DEFAULT_RTT);

    let ack = send_and_receive(&mut client, &mut server, now).expect("server should ack");
    client.process_input(ack, now);

    let client_stats = client.stats();
    assert!(
        client_stats.bytes_acked > 0,
        "sender should have acked bytes"
    );
    assert!(
        client_stats.cc.cwnd > 0,
        "cwnd snapshot should be populated"
    );
    // min_rtt tracks the smallest observed sample, so it can never exceed the
    // (EWMA-smoothed) rtt estimate.
    assert!(
        client_stats.min_rtt <= client_stats.rtt,
        "min_rtt {:?} should not exceed rtt {:?}",
        client_stats.min_rtt,
        client_stats.rtt,
    );

    let server_stats = server.stats();
    assert!(
        server_stats.bytes_rx > 0,
        "receiver should have counted received bytes"
    );
}

#[test]
fn pto_works_ping() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now() + Duration::from_secs(10);

    // Send a few packets from the client.
    let pkt0 = send_something(&mut client, now);
    let pkt1 = send_something(&mut client, now);
    let pkt2 = send_something(&mut client, now);
    let pkt3 = send_something(&mut client, now);

    // Nothing to do, should return callback
    let cb = client.process_output(now).callback();
    // The PTO timer is calculated with:
    //   RTT + max(rttvar * 4, GRANULARITY) + max_ack_delay
    // With zero RTT and rttvar, max_ack_delay is minimum too (GRANULARITY)
    assert_eq!(cb, GRANULARITY * 2);

    // Process these by server, skipping pkt0
    let srv0 = server.process(Some(pkt1), now).dgram();
    assert!(srv0.is_some()); // ooo, ack client pkt1

    now += Duration::from_millis(20);

    // process pkt2 (immediate ack because last ack was more than an RTT ago; RTT=0)
    let srv1 = server.process(Some(pkt2), now).dgram();
    assert!(srv1.is_some()); // this is now dropped

    now += Duration::from_millis(20);
    // process pkt3 (acked for same reason)
    let srv2 = server.process(Some(pkt3), now).dgram();
    // ack client pkt 2 & 3
    assert!(srv2.is_some());

    // client processes ack
    let pkt4 = client.process(srv2, now).dgram();
    // client resends data from pkt0
    assert!(pkt4.is_some());

    // server sees ooo pkt0 and generates immediate ack
    let srv3 = server.process(Some(pkt0), now).dgram();
    assert!(srv3.is_some());

    // Accept the acknowledgment.
    let pkt5 = client.process(srv3, now).dgram();
    assert!(pkt5.is_none());

    now += Duration::from_millis(70);
    // PTO expires. No unacked data. Only send PING.
    let client_pings = client.stats().frame_tx.ping;
    let pkt6 = client.process_output(now).dgram();
    assert_eq!(client.stats().frame_tx.ping, client_pings + 1);

    let server_pings = server.stats().frame_rx.ping;
    server.process_input(pkt6.unwrap(), now);
    assert_eq!(server.stats().frame_rx.ping, server_pings + 1);
}

/// A packet declared lost that is later acknowledged was never really lost, so the
/// loss counters have to give the bytes and the count back.
#[test]
fn spurious_loss_is_taken_back() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now() + Duration::from_secs(10);

    let pkt0 = send_something(&mut client, now);
    let pkt0_len = pkt0.len();
    let pkt1 = send_something(&mut client, now);
    let pkt2 = send_something(&mut client, now);
    let pkt3 = send_something(&mut client, now);

    // Acknowledge everything except pkt0, so the reordering threshold declares it lost.
    let srv = server.process(Some(pkt1), now).dgram();
    assert!(srv.is_some());
    now += Duration::from_millis(20);
    server.process_input(pkt2, now);
    now += Duration::from_millis(20);
    let srv = server.process(Some(pkt3), now).dgram();
    client.process_input(srv.unwrap(), now);

    assert_eq!(client.stats().lost, 1);
    assert_eq!(client.stats().bytes_lost, pkt0_len);
    assert_eq!(client.stats().late_ack, 0);

    // pkt0 arrives after all, and the acknowledgment comes back.
    let acked_before = client.stats().bytes_acked;
    let srv = server.process(Some(pkt0), now).dgram();
    client.process_input(srv.unwrap(), now);

    assert_eq!(client.stats().late_ack, 1);
    assert_eq!(client.stats().lost, 0, "packet was not really lost");
    assert_eq!(client.stats().bytes_lost, 0, "bytes were not really lost");
    // The bytes moved from the loss counter into the acked counter. Without the
    // decrement above they would have been counted as both lost and acknowledged.
    assert_eq!(
        client.stats().bytes_acked - acked_before,
        pkt0_len,
        "late-acked bytes should be counted exactly once"
    );
}

#[test]
fn pto_initial() {
    const INITIAL_PTO: Duration = Duration::from_millis(300);
    let mut now = now();

    // This test makes too many assumptions about single-packet PTOs for multi-packet MLKEM flights
    debug!("---- client: generate CH");
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let pkt1 = client.process_output(now).dgram();
    assert!(pkt1.is_some());
    assert_eq!(pkt1.clone().unwrap().len(), client.plpmtu());

    let delay = client.process_output(now).callback();
    assert_eq!(delay, INITIAL_PTO);

    // Resend initial after PTO.
    now += delay;
    let pkt2 = client.process_output(now).dgram();
    assert!(pkt2.is_some());
    assert_eq!(pkt2.unwrap().len(), client.plpmtu());

    let delay = client.process_output(now).callback();
    // PTO has doubled.
    assert_eq!(delay, INITIAL_PTO * 2);

    // Server process the first initial pkt.
    let mut server = default_server();
    let out = server.process(pkt1, now).dgram();
    assert!(out.is_some());

    // Client receives ack for the first initial packet as well a Handshake packet.
    // After the handshake packet the initial keys and the crypto stream for the initial
    // packet number space will be discarded.
    // Here only an ack for the Handshake packet will be sent.
    let out = client.process(out, now).dgram();
    assert!(out.is_some());

    // We do not have PTO for the resent initial packet any more, but
    // the Handshake PTO timer should be armed.  As the RTT is apparently
    // the same as the initial PTO value, and there is only one sample,
    // the PTO will be 3x the INITIAL PTO.
    let delay = client.process_output(now).callback();
    assert_eq!(delay, INITIAL_PTO * 3);
}

/// A complete handshake that involves two PTOs in the Handshake space.
#[test]
fn pto_handshake_complete() {
    const HALF_RTT: Duration = Duration::from_millis(10);

    let mut now = now();
    // start handshake
    let mut client = default_client();
    let mut server = default_server();

    let pkt = client.process_output(now).dgram();
    let pkt2 = client.process_output(now).dgram();
    assert_initial(pkt.as_ref().unwrap(), false);
    assert_initial(pkt2.as_ref().unwrap(), false);
    let cb = client.process_output(now).callback();
    assert_eq!(cb, Duration::from_millis(5)); // Pacing delay

    now += HALF_RTT;
    server.process_input(pkt.unwrap(), now);
    let pkt = server.process(pkt2, now).dgram();
    assert_initial(pkt.as_ref().unwrap(), false);

    now += HALF_RTT;
    let pkt = client.process(pkt, now).dgram();
    assert_initial(pkt.as_ref().unwrap(), false);

    now += HALF_RTT;
    let pkt = server.process(pkt, now).dgram();
    // This datagram contains a Handshake packet, but it might also have
    // extra Initial data coalesced at the start.
    assert_contains_handshake(pkt.as_ref().unwrap());

    now += HALF_RTT;
    let pkt = client.process(pkt, now).dgram();
    // ...and, if the Initial was sent in that last one, this will acknowledge
    // it, so a coalesced Initial may sit in front of the Handshake.
    assert_contains_handshake(pkt.as_ref().unwrap());

    let cb = client.process_output(now).callback();
    // The client now has a single RTT estimate (20ms), so
    // the handshake PTO is set based on that.
    let pto = HALF_RTT * 6;
    assert_eq!(cb, pto);

    now += HALF_RTT;
    let pkt = server.process(pkt, now).dgram();
    assert!(pkt.is_none());

    now += HALF_RTT;
    client.authenticated(AuthenticationStatus::Ok, now);

    let pkt1 = client.process_output(now).dgram();
    assert_handshake(pkt1.as_ref().unwrap());
    assert_eq!(*client.state(), State::Connected);

    let cb = client.process_output(now).callback();
    assert_eq!(cb, pto);

    let mut pto_counts = [0; Stats::MAX_PTO_COUNTS];
    assert_eq!(client.stats.borrow().pto_counts, pto_counts);

    // Wait for PTO to expire and resend a handshake packet.
    // Wait long enough that the 1-RTT PTO also fires.
    now += pto;
    let pkt2 = client.process_output(now).dgram();
    assert_handshake(pkt2.as_ref().unwrap());

    // Discard the ping that follows.
    let ping = client.process_output(now).dgram();
    assert_eq!(ping.as_ref().unwrap()[0] & 0x80, 0);

    pto_counts[0] = 1;
    assert_eq!(client.stats.borrow().pto_counts, pto_counts);

    // PTO has been doubled.
    let pto = 2 * pto;
    let cb = client.process_output(now).callback();
    assert_eq!(cb, pto);

    // Get a second PTO packet.
    // Add some application data to this datagram, then split the 1-RTT off.
    // We'll use that packet to force the server to acknowledge 1-RTT.
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_close_send(stream_id).unwrap();
    now += pto;
    let pkt3 = client.process_output(now).dgram();
    assert_handshake(pkt3.as_ref().unwrap());
    let (pkt3_hs, pkt3_1rtt) = split_datagram(&pkt3.unwrap());
    assert_handshake(&pkt3_hs);
    assert!(pkt3_1rtt.is_some());

    // Discard the ping that follows.
    let ping = client.process_output(now).dgram();
    assert_eq!(ping.as_ref().unwrap()[0] & 0x80, 0);

    // PTO has been doubled.
    let pto = pto * 2;
    let cb = client.process_output(now).callback();
    assert_eq!(cb, pto);

    // Not one but two PTOs in a row, thus resetting first bit and increasing
    // second bit.
    pto_counts[0] = 0;
    pto_counts[1] = 1;
    assert_eq!(client.stats.borrow().pto_counts, pto_counts);

    now += HALF_RTT;
    // Now let the server have pkt1 and expect an immediate Handshake ACK.
    // The output will be a Handshake packet with ACK and 1-RTT packet with
    // HANDSHAKE_DONE and (because of pkt3_1rtt) an ACK.
    // This should remove the 1-RTT PTO from messing this test up.
    let server_acks = server.stats().frame_tx.ack;
    let server_done = server.stats().frame_tx.handshake_done;
    server.process_input(pkt3_1rtt.unwrap(), now);
    let ack = server.process(pkt1, now).dgram();
    assert!(ack.is_some());
    assert_eq!(server.stats().frame_tx.ack, server_acks + 2);
    assert_eq!(server.stats().frame_tx.handshake_done, server_done + 1);

    // Check that the other packets (pkt2, pkt3) are Handshake packets.
    // The server discarded the Handshake keys already, therefore they are dropped.
    // Note that these don't include 1-RTT packets, because 1-RTT isn't send on PTO.
    let (pkt2_hs, pkt2_1rtt) = split_datagram(&pkt2.unwrap());
    assert_handshake(&pkt2_hs);
    assert!(pkt2_1rtt.is_some());
    let dropped_before1 = server.stats().dropped_rx;
    let server_frames = server.stats().frame_rx.all();
    server.process_input(pkt2_hs, now);
    assert_eq!(1, server.stats().dropped_rx - dropped_before1);
    assert_eq!(server.stats().frame_rx.all(), server_frames);

    server.process_input(pkt2_1rtt.unwrap(), now);
    let server_frames2 = server.stats().frame_rx.all();
    let dropped_before2 = server.stats().dropped_rx;
    server.process_input(pkt3_hs, now);
    assert_eq!(1, server.stats().dropped_rx - dropped_before2);
    assert_eq!(server.stats().frame_rx.all(), server_frames2);

    now += HALF_RTT;

    // Let the client receive the ACK.
    // The client now waits in order to delay its ACK of the HANDSHAKE_DONE.
    let _retransmissions = client.process(ack, now);
    let cb = client.process_output(now).callback();
    // The default ack delay is the RTT divided by the default ACK ratio of 4.
    let expected_ack_delay = HALF_RTT * 2 / 4;
    assert_eq!(cb, expected_ack_delay);

    // Let the ACK delay timer expire.
    now += cb;
    let out = client.process_output(now).dgram();
    assert!(out.is_some());
}

/// Test that PTO in the Handshake space contains the right frames.
#[test]
fn pto_handshake_frames() {
    let mut now = now();
    debug!("---- client: generate CH");
    let mut client = default_client();
    let pkt = client.process_output(now);
    let pkt2 = client.process_output(now);

    now += Duration::from_millis(10);
    debug!("---- server: CH -> SH, EE, CERT, CV, FIN");
    let mut server = default_server();
    server.process_input(pkt.dgram().unwrap(), now);
    let pkt = server.process(pkt2.dgram(), now);

    now += Duration::from_millis(10);
    debug!("---- client: cert verification");
    let pkt = client.process(pkt.dgram(), now);

    now += Duration::from_millis(10);
    let pkt = server.process(pkt.dgram(), now);
    now += Duration::from_millis(10);
    let pkt = client.process(pkt.dgram(), now);

    now += Duration::from_millis(10);
    drop(server.process(pkt.dgram(), now));

    now += Duration::from_millis(10);
    client.authenticated(AuthenticationStatus::Ok, now);

    let stream = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(stream, 2);
    assert_eq!(client.stream_send(stream, b"zero").unwrap(), 4);
    debug!("---- client: SH..FIN -> FIN and 1RTT packet");
    let pkt1 = client.process_output(now).dgram();
    assert!(pkt1.is_some());

    // Get PTO timer.
    let out = client.process_output(now);
    assert_eq!(out, Output::Callback(Duration::from_millis(60)));

    // Wait for PTO to expire and resend a handshake packet.
    now += Duration::from_millis(60);
    let pkt2 = client.process_output(now).dgram();
    assert!(pkt2.is_some());

    now += Duration::from_millis(10);
    let crypto_before = server.stats().frame_rx.crypto;
    server.process_input(pkt2.unwrap(), now);
    assert_eq!(server.stats().frame_rx.crypto, crypto_before + 1);
}

#[test]
fn pto_retransmits_previous_frames_across_datagrams() {
    const NUM_PACKETS_BEFORE_PTO: usize = 10;

    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut now = now();

    // Send multiple tiny stream frames, each in separate UDP datagrams.
    let mut client_stream_frame_tx = client.stats().frame_tx.stream;
    for _ in 0..NUM_PACKETS_BEFORE_PTO {
        let stream = client.stream_create(StreamType::UniDi).unwrap();
        assert_eq!(client.stream_send(stream, b"42").unwrap(), 2);

        let lost = client.process_output(now);
        assert!(lost.dgram().is_some());

        assert_eq!(client.stats().frame_tx.stream, client_stream_frame_tx + 1);
        client_stream_frame_tx = client.stats().frame_tx.stream;
    }

    // Nothing to do, should return callback.
    let out = client.process_output(now);
    assert!(matches!(out, Output::Callback(_)));

    // One second later, it should want to send PTO packet.
    now += AT_LEAST_PTO;
    let client_pto = client.process_output(now);

    // Expect single `client_pto` datagram to retransmit all stream frames
    // previously sent in separate datagrams, as each is tiny.
    let server_stream_frame_rx = server.stats().frame_rx.stream;
    server.process_input(client_pto.dgram().unwrap(), now);
    assert_eq!(
        server.stats().frame_rx.stream,
        server_stream_frame_rx + NUM_PACKETS_BEFORE_PTO
    );
}

/// In the case that the Handshake takes too many packets, the server might
/// be stalled on the anti-amplification limit.  If a Handshake ACK from the
/// client is lost, the client has to keep the PTO timer armed or the server
/// might be unable to send anything, causing a deadlock.
#[test]
fn handshake_ack_pto() {
    const RTT: Duration = Duration::from_millis(10);
    let mut now = now();
    // This test makes too many assumptions about single-packet PTOs for multi-packet MLKEM flights
    // to work.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    // This is a greasing transport parameter, and large enough that the
    // server needs to send two Handshake packets.
    let big = TransportParameter::Bytes(vec![0; Pmtud::default_plpmtu(DEFAULT_ADDR.ip())]);
    server
        .set_local_tparam(TestTransportParameter, big)
        .unwrap();

    let c1 = client.process_output(now).dgram();

    now += RTT / 2;
    let s1 = server.process(c1, now).dgram();
    assert!(s1.is_some());
    let s2 = server.process_output(now).dgram();
    assert!(s1.is_some());

    // Now let the client have the Initial, but drop the first coalesced Handshake packet.
    now += RTT / 2;
    let (initial, _) = split_datagram(&s1.unwrap());
    client.process_input(initial, now);
    let c2 = client.process(s2, now).dgram();
    assert!(c2.is_some()); // This is an ACK.  Drop it.
    let delay = client.process_output(now).callback();
    assert_eq!(delay, RTT * 3);

    let mut pto_counts = [0; Stats::MAX_PTO_COUNTS];
    assert_eq!(client.stats.borrow().pto_counts, pto_counts);

    // Wait for the PTO and ensure that the client generates a packet.
    now += delay;
    let c3 = client.process_output(now).dgram();
    assert!(c3.is_some());

    now += RTT / 2;
    let ping_before = server.stats().frame_rx.ping;
    server.process_input(c3.unwrap(), now);
    assert_eq!(server.stats().frame_rx.ping, ping_before + 1);

    pto_counts[0] = 1;
    assert_eq!(client.stats.borrow().pto_counts, pto_counts);

    // Now complete the handshake as cheaply as possible.
    let dgram = server.process_output(now).dgram();
    client.process_input(dgram.unwrap(), now);
    maybe_authenticate(&mut client);
    let dgram = client.process_output(now).dgram();
    assert_eq!(*client.state(), State::Connected);
    let dgram = server.process(dgram, now).dgram();
    assert_eq!(*server.state(), State::Confirmed);
    client.process_input(dgram.unwrap(), now);
    assert_eq!(*client.state(), State::Confirmed);

    assert_eq!(client.stats.borrow().pto_counts, pto_counts);
}

#[test]
fn loss_recovery_crash() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let now = now();

    // The server sends something, but we will drop this.
    drop(send_something(&mut server, now));

    // Then send something again, but let it through.
    let ack = send_and_receive(&mut server, &mut client, now);
    assert!(ack.is_some());

    // Have the server process the ACK.
    let cb = server.process(ack, now).callback();
    assert!(cb > Duration::from_secs(0));

    // Now we leap into the future.  The server should regard the first
    // packet as lost based on time alone.
    let dgram = server.process_output(now + AT_LEAST_PTO).dgram();
    assert!(dgram.is_some());

    // This crashes.
    drop(send_something(&mut server, now + AT_LEAST_PTO));
}

// If we receive packets after the PTO timer has fired, we won't clear
// the PTO state, but we might need to acknowledge those packets.
// This shouldn't happen, but we found that some implementations do this.
#[test]
fn ack_after_pto() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut now = now();

    // The client sends and is forced into a PTO.
    drop(send_something(&mut client, now));

    // Jump forward to the PTO and drain the PTO packets.
    now += AT_LEAST_PTO;
    // We can use MAX_PTO_PACKET_COUNT, because we know the handshake is over.
    for _ in 0..MAX_PTO_PACKET_COUNT {
        let dgram = client.process_output(now).dgram();
        assert!(dgram.is_some());
    }
    assert!(client.process_output(now).dgram().is_none());

    // The server now needs to send something that will cause the
    // client to want to acknowledge it.  A little out of order
    // delivery is just the thing.
    // Note: The server can't ACK anything here, but none of what
    // the client has sent so far has been transferred.
    drop(send_something(&mut server, now));
    let dgram = send_something(&mut server, now);

    // The client is now after a PTO, but if it receives something
    // that demands acknowledgment, it will send just the ACK.
    let ack = client.process(Some(dgram), now).dgram();
    assert!(ack.is_some());

    // Make sure that the packet only contained an ACK frame.
    let all_frames_before = server.stats().frame_rx.all();
    let ack_before = server.stats().frame_rx.ack;
    server.process_input(ack.unwrap(), now);
    assert_eq!(server.stats().frame_rx.all(), all_frames_before + 1);
    assert_eq!(server.stats().frame_rx.ack, ack_before + 1);
}

/// When we declare a packet as lost, we keep it around for a while for another loss period.
/// Those packets should not affect how we report the loss recovery timer.
/// As the loss recovery timer based on RTT we use that to drive the state.
#[test]
fn lost_but_kept_and_lr_timer() {
    const RTT: Duration = Duration::from_secs(1);
    let mut client = default_client();
    let mut server = default_server();
    let mut now = connect_with_rtt(&mut client, &mut server, now(), RTT);

    // Two packets (p1, p2) are sent at around t=0.  The first is lost.
    let _p1 = send_something(&mut client, now);
    let p2 = send_something(&mut client, now);

    // At t=RTT/2 the server receives the packet and ACKs it.
    now += RTT / 2;
    let ack = server.process(Some(p2), now).dgram();
    assert!(ack.is_some());
    // The client also sends another two packets (p3, p4), again losing the first.
    let _p3 = send_something(&mut client, now);
    let p4 = send_something(&mut client, now);

    // At t=RTT the client receives the ACK and goes into timed loss recovery.
    // The client doesn't call p1 lost at this stage, but it will soon.
    now += RTT / 2;
    let res = client.process(ack, now);
    // The client should be on a loss recovery timer as p1 is missing.
    let lr_timer = res.callback();
    // Loss recovery timer should be RTT/8, but only check for 0 or >=RTT/2.
    assert_ne!(lr_timer, Duration::from_secs(0));
    assert!(lr_timer < (RTT / 2));
    // The server also receives and acknowledges p4, again sending an ACK.
    let ack = server.process(Some(p4), now).dgram();
    assert!(ack.is_some());

    // At t=RTT*3/2 the client should declare p1 to be lost.
    now += RTT / 2;
    // So the client will send the data from p1 again.
    let res = client.process_output(now);
    assert!(res.dgram().is_some());
    // When the client processes the ACK, it should engage the
    // loss recovery timer for p3, not p1 (even though it still tracks p1).
    let res = client.process(ack, now);
    let lr_timer2 = res.callback();
    assert_eq!(lr_timer, lr_timer2);
}

/// We should not be setting the loss recovery timer based on packets
/// that are sent prior to the largest acknowledged.
/// Testing this requires that we construct a case where one packet
/// number space causes the loss recovery timer to be engaged.  At the same time,
/// there is a packet in another space that hasn't been acknowledged AND
/// that packet number space has not received acknowledgments for later packets.
#[test]
fn loss_time_past_largest_acked() {
    const RTT: Duration = Duration::from_secs(10);
    const INCR: Duration = Duration::from_millis(1);
    // This test makes too many assumptions about single-packet PTOs for multi-packet MLKEM flights
    // to work.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();

    let mut now = now();

    // Start the handshake.
    let c_in = client.process_output(now).dgram();
    now += RTT / 2;
    let s_hs1 = server.process(c_in, now).dgram();

    // Get some spare server handshake packets for the client to ACK.
    // This involves a time machine, so be a little cautious.
    // This test uses an RTT of 10s, but our server starts
    // with a much lower RTT estimate, so the PTO at this point should
    // be much smaller than an RTT and so the server shouldn't see
    // time go backwards.
    let s_pto = server.process_output(now).callback();
    assert_ne!(s_pto, Duration::from_secs(0));
    assert!(s_pto < RTT);
    let s_hs2 = server.process_output(now + s_pto).dgram();
    assert!(s_hs2.is_some());
    let s_pto = server.process_output(now).callback();
    assert_ne!(s_pto, Duration::from_secs(0));
    assert!(s_pto < RTT);
    let s_hs3 = server.process_output(now + s_pto).dgram();
    assert!(s_hs3.is_some());

    // We are blocked by the amplification limit now.
    let cb = server.process_output(now).callback();
    assert_eq!(cb, server.conn_params.get_idle_timeout());

    // Get some Handshake packets from the client.
    // We need one to be left unacknowledged before one that is acknowledged.
    // So that the client engages the loss recovery timer.
    // This is complicated by the fact that it is hard to cause the client
    // to generate an ack-eliciting packet.  For that, we use the Finished message.
    // Reordering delivery ensures that the later packet is also acknowledged.
    now += RTT / 2;
    let c_hs1 = client.process(s_hs1, now).dgram();
    assert!(c_hs1.is_some()); // This comes first, so it's useless.
    maybe_authenticate(&mut client);
    let c_hs2 = client.process_output(now).dgram();
    assert!(c_hs2.is_some()); // This one will elicit an ACK.

    // The we need the outstanding packet to be sent after the
    // application data packet, so space these out a tiny bit.
    let _p1 = send_something(&mut client, now + INCR);
    let c_hs3 = client.process(s_hs2, now + (INCR * 2)).dgram();
    assert!(c_hs3.is_some()); // This will be left outstanding.
    let c_hs4 = client.process(s_hs3, now + (INCR * 3)).dgram();
    assert!(c_hs4.is_some()); // This will be acknowledged.

    // Process c_hs2 and c_hs4, but skip c_hs3.
    // Then get an ACK for the client.
    now += RTT / 2;
    // Deliver c_hs4 first, but don't generate a packet.
    server.process_input(c_hs4.unwrap(), now);
    let s_ack = server.process(c_hs2, now).dgram();
    assert!(s_ack.is_some());
    // This includes an ACK, but it also includes HANDSHAKE_DONE,
    // which we need to remove because that will cause the Handshake loss
    // recovery state to be dropped.
    let (s_hs_ack, _s_ap_ack) = split_datagram(&s_ack.unwrap());

    // Now the client should start its loss recovery timer based on the ACK.
    now += RTT / 2;
    let _c_ack = client.process(Some(s_hs_ack), now).dgram();
    // This ACK triggers an immediate ACK, due to an ACK loss during handshake.
    let c_ack = client.process_output(now).dgram();
    assert!(c_ack.is_none());
    // The client should now have the loss recovery timer active.
    let lr_time = client.process_output(now).callback();
    assert_ne!(lr_time, Duration::from_secs(0));
    assert!(lr_time < (RTT / 2));
}

/// `sender` sends a little, `receiver` acknowledges it.
/// Repeat until `count` acknowledgements are sent.
/// Returns the last packet containing acknowledgements, if any.
fn trickle(sender: &mut Connection, receiver: &mut Connection, mut count: usize, now: Instant) {
    let id = sender.stream_create(StreamType::UniDi).unwrap();
    let mut maybe_ack = None;
    while count > 0 {
        debug!("trickle: remaining={count}");
        assert_eq!(sender.stream_send(id, &[9]).unwrap(), 1);
        let dgram = sender.process(maybe_ack, now).dgram();

        maybe_ack = receiver.process(dgram, now).dgram();
        count -= usize::from(maybe_ack.is_some());
    }
    sender.process_input(maybe_ack.unwrap(), now);
}

/// Ensure that a PING frame is sent with ACK sometimes.
/// `fast` allows testing of when `MAX_OUTSTANDING_UNACK` packets are
/// outstanding (`fast` is `true`) within 1 PTO and when only
/// `MIN_OUTSTANDING_UNACK` packets arrive after 2 PTOs (`fast` is `false`).
fn ping_with_ack(fast: bool) {
    let mut sender = default_client();
    let mut receiver = default_server();
    let mut now = now();
    connect_force_idle(&mut sender, &mut receiver);
    let sender_acks_before = sender.stats().frame_tx.ack;
    let receiver_acks_before = receiver.stats().frame_tx.ack;
    let count = if fast {
        MAX_OUTSTANDING_UNACK
    } else {
        MIN_OUTSTANDING_UNACK
    };
    trickle(&mut sender, &mut receiver, count, now);
    assert_eq!(sender.stats().frame_tx.ack, sender_acks_before);
    assert_eq!(receiver.stats().frame_tx.ack, receiver_acks_before + count);
    assert_eq!(receiver.stats().frame_tx.ping, 0);

    if !fast {
        // Wait at least one PTO, from the reciever's perspective.
        // A receiver that hasn't received MAX_OUTSTANDING_UNACK won't send PING.
        now += receiver.pto() + Duration::from_micros(1);
        trickle(&mut sender, &mut receiver, 1, now);
        assert_eq!(receiver.stats().frame_tx.ping, 0);
    }

    // After a second PTO (or the first if fast), new acknowledgements come
    // with a PING frame and cause an ACK to be sent by the sender.
    now += receiver.pto() + Duration::from_micros(1);
    trickle(&mut sender, &mut receiver, 1, now);
    assert_eq!(receiver.stats().frame_tx.ping, 1);
    if let Output::Callback(t) = sender.process_output(now) {
        assert_eq!(t, DEFAULT_LOCAL_ACK_DELAY);
        assert!(sender.process_output(now + t).dgram().is_some());
    }
    assert_eq!(sender.stats().frame_tx.ack, sender_acks_before + 1);
}

#[test]
fn ping_with_ack_fast() {
    ping_with_ack(true);
}

#[test]
fn ping_with_ack_slow() {
    ping_with_ack(false);
}

#[test]
fn ping_with_ack_min() {
    const COUNT: usize = MIN_OUTSTANDING_UNACK - 2;
    let mut sender = default_client();
    let mut receiver = default_server();
    let mut now = now();
    connect_force_idle(&mut sender, &mut receiver);
    let sender_acks_before = sender.stats().frame_tx.ack;
    let receiver_acks_before = receiver.stats().frame_tx.ack;
    trickle(&mut sender, &mut receiver, COUNT, now);
    assert_eq!(sender.stats().frame_tx.ack, sender_acks_before);
    assert_eq!(receiver.stats().frame_tx.ack, receiver_acks_before + COUNT);
    assert_eq!(receiver.stats().frame_tx.ping, 0);

    // After 3 PTO, no PING because there are too few outstanding packets.
    now += receiver.pto() * 3 + Duration::from_micros(1);
    trickle(&mut sender, &mut receiver, 1, now);
    assert_eq!(receiver.stats().frame_tx.ping, 0);
}

/// This calculates the PTO timer immediately after connection establishment.
/// It depends on there only being 2 RTT samples in the handshake.
fn expected_pto(rtt: Duration) -> Duration {
    // PTO calculation is rtt + 4rttvar + ack delay.
    // rttvar should be (rtt + 4 * (rtt / 2) * (3/4)^n + 25ms)/2
    // where n is the number of round trips
    // This uses the default maximum ACK delay (25ms) as the ACK delay extension
    // is negotiated and no ACK_DELAY frame has been received.
    rtt + rtt * 9 / 8 + DEFAULT_REMOTE_ACK_DELAY
}

#[test]
fn fast_pto() {
    // This test makes too many assumptions about single-packet PTOs for multi-packet MLKEM flights
    // to work.
    let mut client = new_client(
        ConnectionParameters::default()
            .fast_pto(FAST_PTO_SCALE / 2)
            .mlkem(false),
    );
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);

    let res = client.process_output(now);
    let idle_timeout = ConnectionParameters::DEFAULT_IDLE_TIMEOUT
        .checked_sub(DEFAULT_RTT / 2)
        .unwrap();
    assert_eq!(res, Output::Callback(idle_timeout));

    // Send data on two streams
    let stream = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_send(stream, DEFAULT_STREAM_DATA).unwrap(),
        DEFAULT_STREAM_DATA.len()
    );

    // Send a packet after some time.
    now += idle_timeout / 2;
    let dgram = client.process_output(now).dgram();
    assert!(dgram.is_some());

    // Nothing to do, should return a callback.
    let cb = client.process_output(now).callback();
    assert_eq!(expected_pto(DEFAULT_RTT) / 2, cb);

    // Once the PTO timer expires, a PTO packet should be sent should want to send PTO packet.
    now += cb;
    let dgram = client.process_output(now).dgram();

    let stream_before = server.stats().frame_rx.stream;
    server.process_input(dgram.unwrap(), now);
    assert_eq!(server.stats().frame_rx.stream, stream_before + 1);
}

/// Even if the PTO timer is slowed right down, persistent congestion is declared
/// based on the "true" value of the timer.
#[test]
fn fast_pto_persistent_congestion() {
    // This test makes too many assumptions about single-packet PTOs for multi-packet MLKEM flights
    // to work.
    let mut client = new_client(
        ConnectionParameters::default()
            .fast_pto(FAST_PTO_SCALE * 2)
            .mlkem(false),
    );
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);

    let res = client.process_output(now);
    let idle_timeout = ConnectionParameters::DEFAULT_IDLE_TIMEOUT
        .checked_sub(DEFAULT_RTT / 2)
        .unwrap();
    assert_eq!(res, Output::Callback(idle_timeout));

    // Send packets spaced by the PTO timer.  And lose them.
    // Note: This timing is a tiny bit higher than the client will use
    // to determine persistent congestion. The ACK below adds another RTT
    // estimate, which will reduce rttvar by 3/4, so persistent congestion
    // will occur at `rtt + rtt*27/32 + 25ms`.
    // That is OK as we're still showing that this interval is less than
    // six times the PTO, which is what would be used if the scaling
    // applied to the PTO used to determine persistent congestion.
    let pc_interval = expected_pto(DEFAULT_RTT) * 3;
    println!("pc_interval {pc_interval:?}");
    let _drop1 = send_something(&mut client, now);

    // Check that the PTO matches expectations.
    let cb = client.process_output(now).callback();
    assert_eq!(expected_pto(DEFAULT_RTT) * 2, cb);

    now += pc_interval;
    let _drop2 = send_something(&mut client, now);
    let _drop3 = send_something(&mut client, now);
    let _drop4 = send_something(&mut client, now);
    let dgram = send_something(&mut client, now);

    // Now acknowledge the tail packet and enter persistent congestion.
    now += DEFAULT_RTT / 2;
    let ack = server.process(Some(dgram), now).dgram();
    now += DEFAULT_RTT / 2;
    client.process_input(ack.unwrap(), now);
    assert_eq!(cwnd(&client), cwnd_min(&client));
}

/// Receiving an ACK frame for a packet number that was never sent is an error.
#[test]
fn ack_for_unsent() {
    /// This inserts an ACK frame into packets that ACKs a packet that was never sent.
    struct AckforUnsentWriter {}

    impl FrameWriter for AckforUnsentWriter {
        fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
            builder.encode_varint(FrameType::Ack);
            builder.encode_varint(666u16); // Largest ACKed
            builder.encode_varint(0u8); // ACK delay
            builder.encode_varint(0u8); // ACK block count
            builder.encode_varint(0u8); // ACK block length
        }
    }

    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let spoofed = server
        .test_write_frames(AckforUnsentWriter {}, now())
        .dgram()
        .unwrap();

    // Now deliver the packet with the spoofed ACK frame
    let close = client.process(Some(spoofed), now()).dgram();
    assert!(matches!(
        client.state(),
        State::Closing {
            error: CloseReason::Transport(Error::AckedUnsentPacket),
            ..
        }
    ));
    server.process_input(close.unwrap(), now());
    assert_error(
        &server,
        &CloseReason::Transport(Error::Peer(Error::ProtocolViolation.code())),
    );
}

/// Test that PTO fires for Handshake space even when no Handshake packets have been sent.
///
/// This reproduces a handshake loss scenario where:
/// 1. Client receives Server Hello → Handshake keys installed
/// 2. Server's Handshake flight (Certificate, etc.) is lost/corrupted
/// 3. Client never sends any Handshake packets (has nothing to send yet)
/// 4. Client's Handshake space has `last_ack_eliciting` = None
/// 5. PTO timer never arms for Handshake space
/// 6. Connection times out
///
/// The client should probe Handshake space to elicit server retransmission.
#[test]
fn pto_handshake_space_when_server_flight_lost() {
    const RTT: Duration = Duration::from_millis(10);
    let mut now = now();
    // This test assumes PTOs only involve single-packet flights, which is incompatible with
    // multi-packet flights as used by MLKEM.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    // This is a greasing transport parameter, and large enough that the
    // server needs to send two Handshake packets.
    let big = TransportParameter::Bytes(vec![0; Pmtud::default_plpmtu(DEFAULT_ADDR.ip())]);
    server
        .set_local_tparam(TestTransportParameter, big)
        .unwrap();

    let c1 = client.process_output(now).dgram();
    now += RTT / 2;

    // Collect all server output.
    let mut server_dgrams = Vec::new();
    server_dgrams.push(server.process(c1, now).dgram().unwrap());
    while let Some(dgram) = server.process_output(now).dgram() {
        server_dgrams.push(dgram);
    }
    assert!(!server_dgrams.is_empty());

    // Send all Initial packets to the client, but drop all Handshake packets.
    now += RTT / 2;
    let mut found_hs = false;
    for dgram in server_dgrams {
        let (first, second) = split_datagram(&dgram);
        if is_initial(&first, false) {
            assert!(!found_hs, "got Initial after Handshake");
            if let Some(hs) = second {
                found_hs |= is_handshake(&hs);
            }
            client.process_input(first, now);
        } else {
            found_hs |= is_handshake(&first);
        }
    }
    assert!(found_hs);

    // Client processes the Initial packets (which install Handshake keys).
    let c2 = client.process_output(now).dgram();
    assert!(c2.is_some()); // This is an ACK.  Drop it.
    assert_eq!(*client.state(), State::Handshaking);

    let pto = client.process_output(now).callback();
    assert_ne!(pto, Duration::ZERO);
    now += pto;

    // Drain all packets and get the next timeout.
    let next_timeout = loop {
        if let Output::Callback(callback) = client.process_output(now) {
            break callback;
        }
    };
    // Fire PTO - this should prime Handshake space.
    now += next_timeout;

    // Collect all packets from this timeout and verify PING was sent.
    let stats_before = client.stats().frame_tx;
    let mut pto_packets = Vec::new();
    while let Some(dgram) = client.process_output(now).dgram() {
        pto_packets.push(dgram);
    }

    // Check if any Handshake PING probes packets were sent.
    let mut has_handshake = false;
    for dgram in &pto_packets {
        let (first, second) = split_datagram(dgram);
        has_handshake |= is_handshake(&first) || second.as_ref().is_some_and(|s| is_handshake(s));
    }
    assert!(has_handshake && client.stats().frame_tx.ping > stats_before.ping);
}

/// `maybe_prime_handshake_pto` must not prime the Handshake PTO when the client
/// has no Handshake TX keys (split `ClientHello`, server only got the first
/// Initial packet).  Without the guard, after two PTO iterations the primed
/// Handshake baseline drifts behind Initial's; a server ACK then clears
/// `pto_state` and the stale Handshake PTO fires in the past.
#[test]
fn handshake_pto_not_primed_without_keys() {
    let mut now = now();
    let mut client = new_client(ConnectionParameters::default().pacing(false));
    let mut server = new_server(ConnectionParameters::default().pacing(false));

    // Split ClientHello across two Initial packets (MLKEM).
    let c_init_1 = client.process_output(now).dgram().unwrap();
    let c_init_2 = client.process_output(now).dgram().unwrap();
    assert!(is_initial(&c_init_2, false)); // intentionally dropped

    // Deliver only c_init_1.  Incomplete CH → server sends bare ACK.
    now += DEFAULT_RTT / 2;
    let s_ack = server.process(Some(c_init_1), now).dgram().unwrap();
    now += DEFAULT_RTT / 2;
    client.process_input(s_ack, now);
    while client.process_output(now).dgram().is_some() {}
    assert!(!client.crypto.has_handshake_keys());

    // Two PTO iterations (output dropped) push Initial baseline ahead of the
    // Handshake baseline that was primed during iteration 1.
    for _ in 0..2 {
        let t = client.process_output(now).callback();
        assert_ne!(t, Duration::ZERO);
        now += t;
        while client.process_output(now).dgram().is_some() {}
    }

    // Third PTO: deliver the PING so the server ACKs it, clearing pto_state.
    let t = client.process_output(now).callback();
    assert_ne!(t, Duration::ZERO);
    now += t;
    let ping = client.process_output(now).dgram().unwrap();
    while client.process_output(now).dgram().is_some() {}
    while server.process_output(now).dgram().is_some() {} // drain server PTO probes
    server.process_input(ping, now);
    let s_ack2 = server.process_output(now).dgram().unwrap();
    client.process_input(s_ack2, now);

    // With pto_state cleared the stale Handshake PTO would fire in the past ("earliest > now").
    drop(client.process_output(now));
}

/// Test that the server resends 1-RTT data when receiving undecryptable Handshake packets.
///
/// When the server is in Confirmed state and receives Handshake packets that
/// it cannot decrypt (because it has discarded Handshake keys per RFC 9001
/// Section 4.9.2), it should immediately resend its 1-RTT data (`HANDSHAKE_DONE`,
/// `NewSessionTicket`, etc.) rather than waiting for its PTO timer to fire.
#[test]
fn server_resends_1rtt_on_undecryptable_handshake() {
    const HALF_RTT: Duration = Duration::from_millis(10);
    let mut now = now();
    let mut client = default_client();
    let mut server = default_server();

    // Flight 1: Client Initial (2 datagrams)
    let c_init1 = client.process_output(now).dgram().unwrap();
    let c_init2 = client.process_output(now).dgram().unwrap();

    // Flight 2: Server Initial + Handshake (2 datagrams)
    now += HALF_RTT;
    server.process_input(c_init1, now);
    let s_hs1 = server.process(Some(c_init2), now).dgram().unwrap();
    let s_hs2 = server.process_output(now).dgram().unwrap();

    // Flight 3: Client receives server flight, reaches AuthenticationNeeded
    now += HALF_RTT;
    client.process_input(s_hs1, now);
    client.process_input(s_hs2, now);
    assert!(maybe_authenticate(&mut client));

    // Client sends Handshake Finished, enters Connected state.
    let client_finished = client.process_output(now).dgram();
    assert_eq!(*client.state(), State::Connected);

    // Server receives client Finished, enters Confirmed state.
    now += HALF_RTT;
    let server_confirmed_time = now;
    _ = server.process(client_finished, now).dgram();
    assert_eq!(*server.state(), State::Confirmed);
    assert_eq!(server.stats().frame_tx.handshake_done, 1);

    // Retransmit the Finished via PTO.
    now += client.process_output(now).callback();
    let c_hs_retrans = client.process_output(now).dgram().unwrap();

    // Split and send only the Handshake portion to the server.
    let (c_hs_only, _) = split_datagram(&c_hs_retrans);
    assert!(is_handshake(&c_hs_only));

    // Server receives the Handshake retransmission shortly after entering Confirmed
    // (back in time relative to the client, before server's PTO fires).
    let receive_time = server_confirmed_time + Duration::from_millis(1);
    let dropped_before = server.stats().dropped_rx;
    server.process_input(c_hs_only, receive_time);
    assert_eq!(server.stats().dropped_rx, dropped_before + 1);

    // Server should resend HANDSHAKE_DONE.
    let s_response = server.process_output(receive_time).dgram();
    assert!(s_response.is_some());
    assert_eq!(server.stats().frame_tx.handshake_done, 2);
}

#[test]
fn split_api_loss_timer_type() {
    let (mut client, qlog_contents) =
        new_client_with_qlog(ConnectionParameters::default().pacing(false));
    let mut server = new_server(ConnectionParameters::default().pacing(false));
    let mut now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);

    // Client sends two datagrams.
    let d0 = send_something(&mut client, now);
    let d1 = send_something(&mut client, now);

    // Server receives only d1 (d0 is "delayed in transit").
    now += DEFAULT_RTT / 2;
    server.process_input(d1, now);
    let ack1 = server.process_output(now).dgram().unwrap();

    // Client receives ACK for d1 — d0 becomes a loss candidate.
    now += DEFAULT_RTT / 2;
    client.process_input(ack1, now);
    _ = client.process_output(now);

    // d0 was sent at `now - DEFAULT_RTT`. Loss time = d0_sent + RTT * 9/8.
    let loss_time = now + DEFAULT_RTT / 8;

    // Server receives the "delayed" d0 and generates a new ACK covering both.
    let server_rx_time = loss_time.checked_sub(DEFAULT_RTT / 2).unwrap();
    server.process_input(d0, server_rx_time);
    let ack_all = server.process_output(server_rx_time).dgram().unwrap();

    // At loss_time, deliver the ACK via the split API.  This ACK clears the
    // loss candidate for d0.  Without the timer-type snapshot, timeout()
    // cannot determine that the Ack timer (not Pto) was due.
    client.process_input(ack_all, loss_time);
    _ = client.process_output(loss_time);

    drop(client);
    let log = qlog_contents.to_string();
    assert!(
        log.contains(r#""timer_type":"ack""#),
        "Expected loss_timer_expired with timer_type ack in qlog: {log}"
    );
}
