// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    num::NonZeroUsize,
    time::{Duration, Instant},
};

use neqo_common::Encoder;
use test_fixture::now;
use tracing::{trace, warn};

use super::{
    super::{Connection, ConnectionParameters, IdleTimeout, Output, State},
    AT_LEAST_PTO, DEFAULT_RTT, DEFAULT_STREAM_DATA, connect, connect_force_idle, connect_rtt_idle,
    connect_with_rtt, default_client, default_server, maybe_authenticate, new_client, new_server,
    send_and_receive, send_something,
};
use crate::{
    CloseReason, Error, packet, recovery,
    stats::FrameStats,
    stream_id::{StreamId, StreamType},
    tparams::{TransportParameter, TransportParameterId},
    tracking::PacketNumberSpace,
};

fn default_timeout() -> Duration {
    ConnectionParameters::default().get_idle_timeout()
}

fn keep_alive_timeout() -> Duration {
    default_timeout() / 2
}

fn test_idle_timeout(client: &mut Connection, server: &mut Connection, timeout: Duration) {
    assert!(timeout > Duration::from_secs(1));
    connect_force_idle(client, server);

    let now = now();

    let res = client.process_output(now);
    assert_eq!(res, Output::Callback(timeout));

    // Still connected after timeout-1 seconds. Idle timer not reset
    drop(client.process_output(now + timeout.checked_sub(Duration::from_secs(1)).unwrap()));
    assert!(matches!(client.state(), State::Confirmed));

    drop(client.process_output(now + timeout));

    // Not connected after timeout.
    assert!(matches!(client.state(), State::Closed(_)));
}

#[test]
fn idle_timeout() {
    let mut client = default_client();
    let mut server = default_server();
    test_idle_timeout(&mut client, &mut server, default_timeout());
}

#[test]
fn idle_timeout_custom_client() {
    const IDLE_TIMEOUT: Duration = Duration::from_secs(5);
    let mut client = new_client(ConnectionParameters::default().idle_timeout(IDLE_TIMEOUT));
    let mut server = default_server();
    test_idle_timeout(&mut client, &mut server, IDLE_TIMEOUT);
}

#[test]
fn idle_timeout_custom_server() {
    const IDLE_TIMEOUT: Duration = Duration::from_secs(5);
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().idle_timeout(IDLE_TIMEOUT));
    test_idle_timeout(&mut client, &mut server, IDLE_TIMEOUT);
}

#[test]
fn idle_timeout_custom_both() {
    const LOWER_TIMEOUT: Duration = Duration::from_secs(5);
    const HIGHER_TIMEOUT: Duration = Duration::from_secs(10);
    let mut client = new_client(ConnectionParameters::default().idle_timeout(HIGHER_TIMEOUT));
    let mut server = new_server(ConnectionParameters::default().idle_timeout(LOWER_TIMEOUT));
    test_idle_timeout(&mut client, &mut server, LOWER_TIMEOUT);
}

#[test]
fn asymmetric_idle_timeout() {
    const LOWER_TIMEOUT_MS: u64 = 1000;
    const LOWER_TIMEOUT: Duration = Duration::from_millis(LOWER_TIMEOUT_MS);
    // Sanity check the constant.
    assert!(LOWER_TIMEOUT < default_timeout());

    let mut client = default_client();
    let mut server = default_server();

    // Overwrite the default at the server.
    server
        .tps
        .borrow_mut()
        .local_mut()
        .set_integer(TransportParameterId::IdleTimeout, LOWER_TIMEOUT_MS);
    server.idle_timeout = IdleTimeout::new(LOWER_TIMEOUT);

    // Now connect and force idleness manually.
    // We do that by following what `force_idle` does and have each endpoint
    // send two packets, which are delivered out of order.  See `force_idle`.
    connect(&mut client, &mut server);
    let c1 = send_something(&mut client, now());
    let c2 = send_something(&mut client, now());
    server.process_input(c2, now());
    server.process_input(c1, now());
    let s1 = send_something(&mut server, now());
    let s2 = send_something(&mut server, now());
    client.process_input(s2, now());
    let ack = client.process(Some(s1), now()).dgram();
    assert!(ack.is_some());
    // Now both should have received ACK frames so should be idle.
    assert_eq!(server.process(ack, now()), Output::Callback(LOWER_TIMEOUT));
    assert_eq!(
        client.process_output(now()),
        Output::Callback(LOWER_TIMEOUT)
    );
}

#[test]
fn tiny_idle_timeout() {
    const RTT: Duration = Duration::from_millis(500);
    const LOWER_TIMEOUT_MS: u64 = 100;
    const LOWER_TIMEOUT: Duration = Duration::from_millis(LOWER_TIMEOUT_MS);
    // We won't respect a value that is lower than 3*PTO, sanity check.
    assert!(LOWER_TIMEOUT < 3 * RTT);

    let mut client = default_client();
    let mut server = default_server();

    // Overwrite the default at the server.
    server
        .set_local_tparam(
            TransportParameterId::IdleTimeout,
            TransportParameter::Integer(LOWER_TIMEOUT_MS),
        )
        .unwrap();
    server.idle_timeout = IdleTimeout::new(LOWER_TIMEOUT);

    // Now connect with an RTT and force idleness manually.
    let mut now = connect_with_rtt(&mut client, &mut server, now(), RTT);
    let c1 = send_something(&mut client, now);
    let c2 = send_something(&mut client, now);
    now += RTT / 2;
    server.process_input(c2, now);
    server.process_input(c1, now);
    let s1 = send_something(&mut server, now);
    let s2 = send_something(&mut server, now);
    now += RTT / 2;
    client.process_input(s2, now);
    let ack = client.process(Some(s1), now).dgram();
    assert!(ack.is_some());

    // The client should be idle now, but with a different timer.
    if let Output::Callback(t) = client.process_output(now) {
        assert!(t > LOWER_TIMEOUT);
    } else {
        panic!("Client not idle");
    }

    // The server should go idle after the ACK, but again with a larger timeout.
    now += RTT / 2;
    if let Output::Callback(t) = client.process(ack, now) {
        assert!(t > LOWER_TIMEOUT);
    } else {
        panic!("Client not idle");
    }
}

#[test]
fn idle_send_packet1() {
    const DELTA: Duration = Duration::from_millis(10);

    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();
    connect_force_idle(&mut client, &mut server);

    let timeout = client.process_output(now).callback();
    assert_eq!(timeout, default_timeout());

    now += Duration::from_secs(10);
    let dgram = send_and_receive(&mut client, &mut server, now);
    assert!(dgram.is_some()); // the server will want to ACK, we can drop that.

    // Still connected after 39 seconds because idle timer reset by the
    // outgoing packet.
    now += default_timeout().checked_sub(DELTA).unwrap();
    let dgram = client.process_output(now).dgram();
    assert!(dgram.is_some()); // PTO
    assert!(client.state().connected());

    // Not connected after 40 seconds.
    now += DELTA;
    let out = client.process_output(now);
    assert!(matches!(out, Output::None));
    assert!(client.state().closed());
}

#[test]
fn idle_send_packet2() {
    const GAP: Duration = Duration::from_secs(10);
    const DELTA: Duration = Duration::from_millis(10);

    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut now = now();

    let timeout = client.process_output(now).callback();
    assert_eq!(timeout, default_timeout());

    // First transmission at t=GAP.
    now += GAP;
    drop(send_something(&mut client, now));

    // Second transmission at t=2*GAP.
    drop(send_something(&mut client, now + GAP));
    assert!((GAP * 2 + DELTA) < default_timeout());

    // Still connected just before GAP + default_timeout().
    now += default_timeout().checked_sub(DELTA).unwrap();
    let dgram = client.process_output(now).dgram();
    assert!(dgram.is_some()); // PTO
    assert!(matches!(client.state(), State::Confirmed));

    // Not connected after 40 seconds because timer not reset by second
    // outgoing packet
    now += DELTA;
    let out = client.process_output(now);
    assert!(matches!(out, Output::None));
    assert!(matches!(client.state(), State::Closed(_)));
}

#[test]
fn idle_recv_packet() {
    const FUDGE: Duration = Duration::from_millis(10);

    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut now = now();

    let res = client.process_output(now);
    assert_eq!(res, Output::Callback(default_timeout()));

    let stream = client.stream_create(StreamType::BiDi).unwrap();
    assert_eq!(stream, 0);
    assert_eq!(client.stream_send(stream, b"hello").unwrap(), 5);

    // Respond with another packet.
    // Note that it is important that this not result in the RTT increasing above 0.
    // Otherwise, the eventual timeout will be extended (and we're not testing that).
    now += Duration::from_secs(10);
    let out = client.process_output(now);
    server.process_input(out.dgram().unwrap(), now);
    assert_eq!(server.stream_send(stream, b"world").unwrap(), 5);
    let out = server.process_output(now);
    assert_ne!(out.as_dgram_ref(), None);
    drop(client.process(out.dgram(), now));
    assert!(matches!(client.state(), State::Confirmed));

    // Add a little less than the idle timeout and we're still connected.
    now += default_timeout().checked_sub(FUDGE).unwrap();
    drop(client.process_output(now));
    assert!(matches!(client.state(), State::Confirmed));

    now += FUDGE;
    drop(client.process_output(now));

    assert!(matches!(client.state(), State::Closed(_)));
}

/// A duplicate packet does not restart the idle timer.
#[test]
fn idle_recv_duplicate_packet() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut now = now();
    let dgram = send_something(&mut server, now);
    client.process_input(dgram.clone(), now);

    now += default_timeout() / 2;
    let dups_rx = client.stats().dups_rx;
    client.process_input(dgram, now);
    assert_eq!(client.stats().dups_rx, dups_rx + 1);

    now += default_timeout() / 2;
    drop(client.process_output(now));
    assert!(matches!(client.state(), State::Closed(_)));
}

/// Caching packets should not cause the connection to become idle.
/// This requires a few tricks to keep the connection from going
/// idle while preventing any progress on the handshake.
#[test]
fn idle_caching() {
    let mut client = default_client();
    let mut server = default_server();
    let start = now();
    let mut builder =
        packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);

    // Perform the first round trip, but drop the Initial from the server.
    // The client then caches the Handshake packet.
    let dgram = client.process_output(start).dgram();
    let dgram2 = client.process_output(start).dgram();
    server.process_input(dgram.unwrap(), start);
    let server_initial = server.process(dgram2, start).dgram().unwrap();
    let server_handshake = server.process_output(start).dgram().unwrap();
    client.process_input(server_handshake, start);

    // Perform an exchange and keep the connection alive.
    let middle = start + AT_LEAST_PTO;
    // This is the RTX of the client Initial.
    let dgram = client.process_output(middle).dgram();

    // Get the server to send its first probe and throw that away.
    drop(server.process_output(middle).dgram());
    // Now let the server process the RTX'ed client Initial.  This causes the server
    // to send CRYPTO frames again, so manually extract and discard those.
    server.process_input(dgram.unwrap(), middle);
    let mut tokens = recovery::Tokens::new();
    server.crypto.streams_mut().write_frame(
        PacketNumberSpace::Initial,
        server.conn_params.sni_slicing_enabled(),
        &mut builder,
        &mut tokens,
        &mut FrameStats::default(),
    );
    assert_eq!(tokens.len(), 1);
    tokens.clear();
    server.crypto.streams_mut().write_frame(
        PacketNumberSpace::Initial,
        server.conn_params.sni_slicing_enabled(),
        &mut builder,
        &mut tokens,
        &mut FrameStats::default(),
    );
    assert!(tokens.is_empty());
    let dgram = server.process_output(middle).dgram();

    // The packet may contain CRYPTO if a PTO marked Initial packets for retransmission.
    // The important thing for this test is that an ACK is received, keeping the connection alive.
    let ack_before = client.stats().frame_rx.ack;
    client.process_input(dgram.unwrap(), middle);
    assert_eq!(client.stats().frame_rx.ack, ack_before + 1);

    let end = start + default_timeout() + (AT_LEAST_PTO / 2);
    // Now let the server Initial through, with the CRYPTO frame.
    warn!("client ingests initial, finally");
    drop(client.process(Some(server_initial), end));
    maybe_authenticate(&mut client);
    let dgram = client.process_output(end).dgram().unwrap();
    let dgram = server.process(Some(dgram), end).dgram();
    client.process_input(dgram.unwrap(), end);
    assert_eq!(*client.state(), State::Confirmed);
    assert_eq!(*server.state(), State::Confirmed);
}

/// This function opens a bidirectional stream and leaves both endpoints
/// idle, with the stream left open.
/// The stream ID of that stream is returned (along with the new time).
fn create_stream_idle_rtt(
    initiator: &mut Connection,
    responder: &mut Connection,
    mut now: Instant,
    rtt: Duration,
) -> (Instant, StreamId) {
    let check_idle = |endpoint: &mut Connection, now: Instant| {
        let delay = endpoint.process_output(now).callback();
        trace!("[{endpoint}] idle timeout {delay:?}");
        if rtt < default_timeout() / 4 {
            assert_eq!(default_timeout(), delay);
        } else {
            assert!(delay > default_timeout());
        }
    };

    // Exchange a message each way on a stream.
    let stream = initiator.stream_create(StreamType::BiDi).unwrap();
    _ = initiator.stream_send(stream, DEFAULT_STREAM_DATA).unwrap();
    let req = initiator.process_output(now).dgram();
    now += rtt / 2;
    responder.process_input(req.unwrap(), now);

    // Reordering two packets from the responder forces the initiator to be idle.
    _ = responder.stream_send(stream, DEFAULT_STREAM_DATA).unwrap();
    let resp1 = responder.process_output(now).dgram();
    _ = responder.stream_send(stream, DEFAULT_STREAM_DATA).unwrap();
    let resp2 = responder.process_output(now).dgram();

    now += rtt / 2;
    initiator.process_input(resp2.unwrap(), now);
    initiator.process_input(resp1.unwrap(), now);
    let ack = initiator.process_output(now).dgram();
    assert!(ack.is_some());
    check_idle(initiator, now);

    // Receiving the ACK should return the responder to idle too.
    now += rtt / 2;
    responder.process_input(ack.unwrap(), now);
    check_idle(responder, now);

    (now, stream)
}

fn create_stream_idle(initiator: &mut Connection, responder: &mut Connection) -> StreamId {
    let (_, stream) = create_stream_idle_rtt(initiator, responder, now(), Duration::new(0, 0));
    stream
}

fn assert_idle(endpoint: &mut Connection, now: Instant, expected: Duration) {
    assert_eq!(endpoint.process_output(now).callback(), expected);
}

/// The creator of a stream marks it as important enough to use a keep-alive.
#[test]
fn keep_alive_initiator() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut server, &mut client);
    let mut now = now();

    // Marking the stream for keep-alive changes the idle timeout.
    server.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut server, now, keep_alive_timeout());

    // Wait that long and the server should send a PING frame.
    now += keep_alive_timeout();
    let pings_before = server.stats().frame_tx.ping;
    let ping = server.process_output(now).dgram();
    assert!(ping.is_some());
    assert_eq!(server.stats().frame_tx.ping, pings_before + 1);

    // Exchange ack for the PING.
    let out = client.process(ping, now).dgram();
    let out = server.process(out, now).dgram();
    assert!(client.process(out, now).dgram().is_none());

    // Check that there will be next keep-alive ping after keep_alive_timeout().
    assert_idle(&mut server, now, keep_alive_timeout());
    now += keep_alive_timeout();
    let pings_before2 = server.stats().frame_tx.ping;
    let ping = server.process_output(now).dgram();
    assert!(ping.is_some());
    assert_eq!(server.stats().frame_tx.ping, pings_before2 + 1);
}

/// Test a keep-alive ping is retransmitted if lost.
#[test]
fn keep_alive_lost() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut server, &mut client);
    let mut now = now();

    server.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut server, now, keep_alive_timeout());

    // Wait that long and the server should send a PING frame.
    now += keep_alive_timeout();
    let pings_before = server.stats().frame_tx.ping;
    let ping = server.process_output(now).dgram();
    assert!(ping.is_some());
    assert_eq!(server.stats().frame_tx.ping, pings_before + 1);

    // Wait for ping to be marked lost.
    assert!(server.process_output(now).callback() < AT_LEAST_PTO);
    now += AT_LEAST_PTO;
    let pings_before2 = server.stats().frame_tx.ping;
    let ping = server.process_output(now).dgram();
    assert!(ping.is_some());
    assert_eq!(server.stats().frame_tx.ping, pings_before2 + 1);

    // Exchange ack for the PING.
    let out = client.process(ping, now).dgram();

    now += Duration::from_millis(20);
    let out = server.process(out, now).dgram();

    assert!(client.process(out, now).dgram().is_none());

    // Advance past the recovery timeout. Without this, the server returns a small
    // timeout for recovery even though it has no outstanding data.
    now += AT_LEAST_PTO;
    assert_idle(
        &mut server,
        now,
        keep_alive_timeout().checked_sub(AT_LEAST_PTO).unwrap(),
    );
}

/// The other peer can also keep it alive.
#[test]
fn keep_alive_responder() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut server, &mut client);
    let mut now = now();

    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now, keep_alive_timeout());

    // Wait that long and the client should send a PING frame.
    now += keep_alive_timeout();
    eprintln!("after wait");
    let pings_before = client.stats().frame_tx.ping;
    let ping = client.process_output(now).dgram();
    assert!(ping.is_some());
    assert_eq!(client.stats().frame_tx.ping, pings_before + 1);
}

/// Unmark a stream as being keep-alive.
#[test]
fn keep_alive_unmark() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut client, &mut server);

    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    client.stream_keep_alive(stream, false).unwrap();
    assert_idle(&mut client, now(), default_timeout());
}

/// The sender has something to send.  Make it send it
/// and cause the receiver to become idle by sending something
/// else, reordering the packets, and consuming the ACK.
/// Note that the sender might not be idle if the thing that it
/// sends results in something in addition to an ACK.
fn transfer_force_idle(sender: &mut Connection, receiver: &mut Connection) {
    let dgram = sender.process_output(now()).dgram();
    let chaff = send_something(sender, now());
    receiver.process_input(chaff, now());
    receiver.process_input(dgram.unwrap(), now());
    let ack = receiver.process_output(now()).dgram();
    sender.process_input(ack.unwrap(), now());
}

/// Receiving the end of the stream stops keep-alives for that stream.
/// Even if that data hasn't been read.
#[test]
fn keep_alive_close() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut client, &mut server);

    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    client.stream_close_send(stream).unwrap();
    transfer_force_idle(&mut client, &mut server);
    assert_idle(&mut client, now(), keep_alive_timeout());

    server.stream_close_send(stream).unwrap();
    transfer_force_idle(&mut server, &mut client);
    assert_idle(&mut client, now(), default_timeout());
}

/// Receiving `RESET_STREAM` stops keep-alives for that stream, but only once
/// the sending side is also closed.
#[test]
fn keep_alive_reset() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut client, &mut server);

    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    client.stream_close_send(stream).unwrap();
    transfer_force_idle(&mut client, &mut server);
    assert_idle(&mut client, now(), keep_alive_timeout());

    server.stream_reset_send(stream, 0).unwrap();
    transfer_force_idle(&mut server, &mut client);
    assert_idle(&mut client, now(), default_timeout());

    // The client will fade away from here.
    let t = now() + keep_alive_timeout();
    assert_eq!(client.process_output(t).callback(), keep_alive_timeout());
    let t = now() + default_timeout();
    assert_eq!(client.process_output(t), Output::None);
}

/// Stopping sending also cancels the keep-alive.
#[test]
fn keep_alive_stop_sending() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut client, &mut server);

    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    client.stream_close_send(stream).unwrap();
    client.stream_stop_sending(stream, 0).unwrap();
    transfer_force_idle(&mut client, &mut server);
    // The server will have sent RESET_STREAM, which the client will
    // want to acknowledge, so force that out.
    let junk = send_something(&mut server, now());
    let ack = client.process(Some(junk), now()).dgram();
    assert!(ack.is_some());

    // Now the client should be idle.
    assert_idle(&mut client, now(), default_timeout());
}

/// Multiple active streams are tracked properly.
#[test]
fn keep_alive_multiple_stop() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let stream = create_stream_idle(&mut client, &mut server);

    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    let other = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_keep_alive(other, true).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    client.stream_keep_alive(stream, false).unwrap();
    assert_idle(&mut client, now(), keep_alive_timeout());

    client.stream_keep_alive(other, false).unwrap();
    assert_idle(&mut client, now(), default_timeout());
}

/// If the RTT is too long relative to the idle timeout, the keep-alive is large too.
#[test]
fn keep_alive_large_rtt() {
    let mut client = default_client();
    let mut server = default_server();
    // Use an RTT that is large enough to cause the PTO timer to exceed half
    // the idle timeout.
    let rtt = default_timeout() * 3 / 4;
    let now = connect_with_rtt(&mut client, &mut server, now(), rtt);
    let (now, stream) = create_stream_idle_rtt(&mut server, &mut client, now, rtt);

    // Calculating PTO here is tricky as RTTvar has eroded after multiple round trips.
    // Just check that the delay is larger than the baseline and the RTT.
    for endpoint in &mut [client, server] {
        endpoint.stream_keep_alive(stream, true).unwrap();
        let delay = endpoint.process_output(now).callback();
        trace!("[{endpoint}] new delay {delay:?}");
        assert!(delay > keep_alive_timeout());
        assert!(delay > rtt);
    }
}

/// Only the recipient of a unidirectional stream can keep it alive.
#[test]
fn keep_alive_uni() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let stream = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_keep_alive(stream, true).unwrap_err();
    _ = client.stream_send(stream, DEFAULT_STREAM_DATA).unwrap();
    let dgram = client.process_output(now()).dgram();

    server.process_input(dgram.unwrap(), now());
    server.stream_keep_alive(stream, true).unwrap();
}

/// Test a keep-alive ping is send if there are outstanding ack-eliciting packets and that
/// the connection is closed after the idle timeout passes.
#[test]
fn keep_alive_with_ack_eliciting_packet_lost() {
    const RTT: Duration = Duration::from_millis(500); // PTO will be ~1.1125s

    // The idle time  out  will be  set to ~ 5 * PTO. (IDLE_TIMEOUT/2 > pto and IDLE_TIMEOUT/2 < pto
    // + 2pto) After handshake all packets will be lost. The following steps will happen after
    // the handshake:
    //  - data will be sent on a stream that is marked for keep-alive, (at start time)
    //  - PTO timer will trigger first, and the data will be retransmitted together with a PING, (at
    //    the start time + pto)
    //  - keep-alive timer will trigger and a keep-alive PING will be sent, (at the start time +
    //    IDLE_TIMEOUT / 2)
    //  - PTO timer will trigger again. (at the start time + pto + 2*pto)
    //  - Idle time out  will trigger (at the timeout + IDLE_TIMEOUT)
    const IDLE_TIMEOUT: Duration = Duration::from_secs(6);

    // This test makes too many assumptions about single-packet flights and PTOs for multi-packet
    // MLKEM flights to work.
    let mut client = new_client(
        ConnectionParameters::default()
            .idle_timeout(IDLE_TIMEOUT)
            .mlkem(false),
    );
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, RTT);
    // connect_rtt_idle increase now by RTT / 2;
    now -= RTT / 2;
    assert_idle(&mut client, now, IDLE_TIMEOUT);

    // Create a stream.
    let stream = client.stream_create(StreamType::BiDi).unwrap();
    // Marking the stream for keep-alive changes the idle timeout.
    client.stream_keep_alive(stream, true).unwrap();
    assert_idle(&mut client, now, IDLE_TIMEOUT / 2);

    // Send data on the stream that will be lost.
    _ = client.stream_send(stream, DEFAULT_STREAM_DATA).unwrap();
    let _lost_packet = client.process_output(now).dgram();

    let pto = client.process_output(now).callback();
    // Wait for packet to be marked lost.
    assert!(pto < IDLE_TIMEOUT / 2);
    now += pto;
    let retransmit = client.process_output(now).dgram();
    assert!(retransmit.is_some());
    let retransmit = client.process_output(now).dgram();
    assert!(retransmit.is_some());

    // The next callback should be for an idle PING.
    assert_eq!(
        client.process_output(now).callback(),
        (IDLE_TIMEOUT / 2).checked_sub(pto).unwrap()
    );

    // Wait that long and the client should send a PING frame.
    now += (IDLE_TIMEOUT / 2).checked_sub(pto).unwrap();
    let pings_before = client.stats().frame_tx.ping;
    let ping = client.process_output(now).dgram();
    assert!(ping.is_some());
    assert_eq!(client.stats().frame_tx.ping, pings_before + 1);

    // The next callback is for a PTO, the PTO timer is 2 * pto now.
    assert_eq!(client.process_output(now).callback(), pto * 2);
    now += pto * 2;
    // Now we will retransmit stream data.
    let retransmit = client.process_output(now).dgram();
    assert!(retransmit.is_some());
    let retransmit = client.process_output(now).dgram();
    assert!(retransmit.is_some());

    // The next callback will be an idle timeout.
    assert_eq!(
        client.process_output(now).callback(),
        (IDLE_TIMEOUT / 2).checked_sub(2 * pto).unwrap()
    );

    now += (IDLE_TIMEOUT / 2).checked_sub(2 * pto).unwrap();
    let out = client.process_output(now);
    assert!(matches!(out, Output::None));
    assert!(matches!(client.state(), State::Closed(_)));
}

#[test]
fn keep_alive_no_unnecessary_ping() {
    const RTT: Duration = Duration::from_millis(500); // PTO will be ~1.1125s

    let mut client = default_client();
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, RTT);

    // Create a stream and send data on it that will be lost.
    let stream = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_keep_alive(stream, true).unwrap();
    _ = client.stream_send(stream, DEFAULT_STREAM_DATA).unwrap();
    let _lost_packet = client.process_output(now).dgram();

    // Client returns PTO timer.
    assert!(matches!(client.process_output(now), Output::Callback(_)));

    // Wait for idle timeout. This includes PTO. Thus both PTO and idle timeout
    // are firing now.
    now += default_timeout() / 2;
    let retransmit = client.process_output(now).dgram();
    assert!(retransmit.is_some());
    let pings_before = client.stats().frame_tx.ping;
    let pto_ping = client.process_output(now).dgram();
    assert!(pto_ping.is_some());
    assert_eq!(client.stats().frame_tx.ping, pings_before + 1);

    // Expect no additional idle timeout ping, given that a PTO ping was just
    // sent. I.e. expect idle timer to piggy back on PTO ping.
    let pings_before = client.stats().frame_tx.ping;
    assert!(client.process_output(now).dgram().is_none());
    assert_eq!(client.stats().frame_tx.ping, pings_before);
}

#[test]
fn keep_alive_with_unresponsive_server() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let mut now = now();
    let client_stream = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_keep_alive(client_stream, true).unwrap();

    for _ in 0..100 {
        if client.stream_send(client_stream, &[0x0; 500]).is_err() {
            break;
        }
        if let Output::Callback(t) = client.process_output(now) {
            now += t;
        }
    }
    // Connection should be closed due to idle timeout.
    assert!(matches!(client.state(), State::Closed(_)));
}

/// A connection whose packets are all dropped (a black hole) is declared broken
/// after `max_pto` consecutive PTOs, and crucially does so earlier than the idle
/// timeout would have.
#[test]
fn declare_broken_after_max_pto() {
    const MAX_PTO: usize = 7;
    // For this test to prove anything, the whole PTO backoff ladder must fit inside
    // the idle timeout; otherwise "closed before the idle timeout" is vacuously true.
    // This checks the *last* PTO interval (a conservative sufficient condition, since
    // the 3*RTT factor overestimates the actual PTO in a zero-jitter test).
    assert!(DEFAULT_RTT * (1_u32 << (MAX_PTO - 1)) * 3 < default_timeout());

    let mut client =
        new_client(ConnectionParameters::default().max_pto(NonZeroUsize::new(MAX_PTO)));
    let mut server = default_server();
    let start = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);

    // The client sends data that never reaches the server: a black hole.
    let mut now = start;
    drop(send_something(&mut client, now));

    // Advance through PTOs, dropping every probe the client emits.
    for _ in 0..=MAX_PTO {
        let cb = loop {
            match client.process_output(now) {
                // Drop the PTO probe(s) without advancing time.
                Output::Datagram(_) => {}
                Output::Callback(t) => break t,
                Output::None => break Duration::ZERO,
            }
        };
        now += cb;
    }

    assert!(
        matches!(
            client.state(),
            State::Closed(CloseReason::Transport(Error::TooManyPtos))
        ),
        "expected TooManyPtos, got {:?}",
        client.state()
    );
    assert_eq!(client.loss_recovery.pto_count(), MAX_PTO);
    // The whole point: we gave up well before the idle timeout would have fired.
    assert!(
        now - start < default_timeout(),
        "closed after {:?}, which is not earlier than the {:?} idle timeout",
        now - start,
        default_timeout()
    );
}

/// A received acknowledgement resets the consecutive-PTO counter, so a transient
/// loss burst followed by recovery does not accumulate toward `max_pto`: a second,
/// independent black hole counts fresh from zero rather than adding to the first
/// (the property a fixed timer lacks). That a full fresh ladder then closes the
/// connection is covered by `declare_broken_after_max_pto`.
#[test]
fn max_pto_counter_resets_on_ack() {
    const MAX_PTO: usize = 7;
    let mut client =
        new_client(ConnectionParameters::default().max_pto(NonZeroUsize::new(MAX_PTO)));
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);

    // Black-hole a flight and let a couple of PTOs fire, staying below MAX_PTO.
    drop(send_something(&mut client, now));
    while client.loss_recovery.pto_count() < 2 {
        let cb = loop {
            match client.process_output(now) {
                // Drop the PTO probe(s) without advancing time.
                Output::Datagram(_) => {}
                Output::Callback(t) => break t,
                Output::None => break Duration::ZERO,
            }
        };
        now += cb;
    }
    assert!(!matches!(client.state(), State::Closed(_)));

    // A fresh client packet reaches the server, whose ACK reaches the client and
    // resets the counter. Flush any ACK the server delays behind its ack timer.
    let d = send_something(&mut client, now);
    let mut out = server.process(Some(d), now);
    let ack = loop {
        match out {
            Output::Datagram(d) => break Some(d),
            Output::Callback(t) => {
                now += t;
                out = server.process_output(now);
            }
            Output::None => break None,
        }
    };
    drop(client.process(ack, now));

    // The received ACK reset the counter, so the connection survives.
    assert_eq!(client.loss_recovery.pto_count(), 0);
    assert!(!matches!(client.state(), State::Closed(_)));

    // A second, independent black hole counts fresh from zero: after two more
    // PTOs the count is 2, not 2 + the earlier 2, so separate episodes do not
    // accumulate toward the threshold and the connection stays open.
    drop(send_something(&mut client, now));
    while client.loss_recovery.pto_count() < 2 {
        let cb = loop {
            match client.process_output(now) {
                Output::Datagram(_) => {}
                Output::Callback(t) => break t,
                Output::None => break Duration::ZERO,
            }
        };
        now += cb;
    }
    assert_eq!(client.loss_recovery.pto_count(), 2);
    assert!(!matches!(client.state(), State::Closed(_)));
}
