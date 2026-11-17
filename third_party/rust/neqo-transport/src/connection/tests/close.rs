// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::time::Duration;

use neqo_common::{Datagram, event::Provider as _};
use test_fixture::{datagram, now};

use super::{
    super::{Connection, Output, State},
    connect, connect_force_idle, default_client, default_server,
    migration::change_source_port,
    send_something, ticket_with_new_token,
};
use crate::{
    AppError, CloseReason, ConnectionEvent, ERROR_APPLICATION_CLOSE, Error, StreamType,
    stateless_reset::Token as Srt,
    tparams::{TransportParameter, TransportParameterId::StatelessResetToken},
};

fn assert_draining(c: &Connection, expected: &Error) {
    assert!(c.state().closed());
    if let State::Draining {
        error: CloseReason::Transport(error),
        ..
    } = c.state()
    {
        assert_eq!(error, expected);
    } else {
        panic!();
    }
}

#[test]
fn connection_close() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let now = now();

    client.close(now, 42, "");

    let stats_before = client.stats().frame_tx;
    let out = client.process_output(now);
    let stats_after = client.stats().frame_tx;
    assert_eq!(
        stats_after.connection_close,
        stats_before.connection_close + 1
    );
    assert_eq!(stats_after.ack, stats_before.ack + 1);

    server.process_input(out.dgram().unwrap(), now);
    assert_draining(&server, &Error::PeerApplication(42));
}

#[test]
fn draining_clears_events() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    server.events().for_each(drop);

    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, &[0; 10]).unwrap();
    let data = client.process_output(now()).dgram().unwrap();
    client.close(now(), 42, "");
    let close = client.process_output(now()).dgram().unwrap();

    server.process_multiple_input([data, close], now());
    assert_draining(&server, &Error::PeerApplication(42));
    let state_change = ConnectionEvent::StateChange(server.state().clone());
    assert_eq!(server.events().collect::<Vec<_>>(), [state_change]);
}

fn drain_with_ticket(client: &mut Connection, server: &mut Connection) {
    let ticket = ticket_with_new_token(server, now());
    server.close(now(), 42, "");
    let close = server.process_output(now()).dgram().unwrap();
    client.process_multiple_input([ticket, close], now());
    assert_draining(client, &Error::PeerApplication(42));
}

fn assert_only_resumption_token(client: &mut Connection) {
    let state_change = ConnectionEvent::StateChange(client.state().clone());
    let events = client.events().collect::<Vec<_>>();
    assert!(
        matches!(&events[..], [ConnectionEvent::ResumptionToken(_), e] if *e == state_change),
        "{events:?}"
    );
}

#[test]
fn draining_keeps_resumption_token() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    client.events().for_each(drop);

    drain_with_ticket(&mut client, &mut server);
    assert_only_resumption_token(&mut client);
}

#[test]
fn closing_keeps_resumption_token() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    client.events().for_each(drop);

    client.process_input(ticket_with_new_token(&mut server, now()), now());
    client.close(now(), 42, "");
    assert!(matches!(client.state(), State::Closing { .. }));
    assert_only_resumption_token(&mut client);
}

#[test]
fn closed_keeps_resumption_token() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    client.events().for_each(drop);

    drain_with_ticket(&mut client, &mut server);
    let State::Draining { timeout, .. } = *client.state() else {
        panic!("not draining: {:?}", client.state());
    };
    drop(client.process_output(timeout));

    assert_eq!(
        *client.state(),
        State::Closed(CloseReason::Transport(Error::PeerApplication(42)))
    );
    assert_only_resumption_token(&mut client);
}

#[test]
fn connection_close_with_long_reason_string() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let now = now();
    // Create a long string and use it as the close reason.
    let long_reason = String::from_utf8([0x61; 2048].to_vec()).unwrap();
    client.close(now, 42, long_reason);

    let stats_before = client.stats().frame_tx;
    let out = client.process_output(now);
    let stats_after = client.stats().frame_tx;
    assert_eq!(
        stats_after.connection_close,
        stats_before.connection_close + 1
    );
    assert_eq!(stats_after.ack, stats_before.ack + 1);

    server.process_input(out.dgram().unwrap(), now);
    assert_draining(&server, &Error::PeerApplication(42));
}

// During the handshake, an application close should be sanitized.
#[test]
fn early_application_close() {
    let mut client = default_client();
    let mut server = default_server();

    // One flight each.
    let dgram = client.process_output(now()).dgram();
    assert!(dgram.is_some());
    let dgram = server.process(dgram, now()).dgram();
    assert!(dgram.is_some());

    server.close(now(), 77, String::new());
    assert!(server.state().closed());
    let dgram = server.process_output(now()).dgram();
    assert!(dgram.is_some());

    client.process_input(dgram.unwrap(), now());
    assert_draining(&client, &Error::Peer(ERROR_APPLICATION_CLOSE));
}

#[test]
fn bad_tls_version() {
    let mut client = default_client();
    // Do a bad, bad thing.
    client
        .crypto
        .tls_mut()
        .set_option(nss::Opt::Tls13CompatMode, true)
        .unwrap();
    let mut server = default_server();

    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    assert!(dgram.is_some() && dgram2.is_some());
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram();
    assert_eq!(
        *server.state(),
        State::Closed(CloseReason::Transport(Error::ProtocolViolation))
    );
    assert!(dgram.is_some());
    client.process_input(dgram.unwrap(), now());
    assert_draining(&client, &Error::Peer(Error::ProtocolViolation.code()));
}

/// Test the interaction between the loss recovery timer
/// and the closing timer.
#[test]
fn closing_timers_interation() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let mut now = now();

    // We're going to induce time-based loss recovery so that timer is set.
    let _p1 = send_something(&mut client, now);
    let p2 = send_something(&mut client, now);
    let ack = server.process(Some(p2), now).dgram();
    assert!(ack.is_some()); // This is an ACK.

    // After processing the ACK, we should be on the loss recovery timer.
    let cb = client.process(ack, now).callback();
    assert_ne!(cb, Duration::from_secs(0));
    now += cb;

    // Rather than let the timer pop, close the connection.
    client.close(now, 0, "");
    let client_close = client.process_output(now).dgram();
    assert!(client_close.is_some());
    // This should now report the end of the closing period, not a
    // zero-duration wait driven by the (now defunct) loss recovery timer.
    let client_close_timer = client.process_output(now).callback();
    assert_ne!(client_close_timer, Duration::from_secs(0));
}

#[test]
fn closing_and_draining() {
    const APP_ERROR: AppError = 7;
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // Save a packet from the client for later.
    let p1 = send_something(&mut client, now());

    // Close the connection.
    client.close(now(), APP_ERROR, "");
    let client_close = client.process_output(now()).dgram();
    assert!(client_close.is_some());
    let client_close_timer = client.process_output(now()).callback();
    assert_ne!(client_close_timer, Duration::from_secs(0));
    // The client will spit out the same packet in response to anything it receives.
    let p3 = send_something(&mut server, now());
    let client_close2 = client.process(Some(p3), now()).dgram();
    assert_eq!(
        client_close.as_ref().unwrap().len(),
        client_close2.as_ref().unwrap().len()
    );

    // After this time, the client should transition to closed.
    let end = client.process_output(now() + client_close_timer);
    assert_eq!(end, Output::None);
    assert_eq!(
        *client.state(),
        State::Closed(CloseReason::Application(APP_ERROR))
    );

    // When the server receives the close, it too should generate CONNECTION_CLOSE.
    let server_close = server.process(client_close, now()).dgram();
    assert!(server.state().closed());
    assert!(server_close.is_some());
    // .. but it ignores any further close packets.
    let server_close_timer = server.process(client_close2, now()).callback();
    assert_ne!(server_close_timer, Duration::from_secs(0));
    // Even a legitimate packet without a close in it.
    let server_close_timer2 = server.process(Some(p1), now()).callback();
    assert_eq!(server_close_timer, server_close_timer2);

    let end = server.process_output(now() + server_close_timer);
    assert_eq!(end, Output::None);
    assert_eq!(
        *server.state(),
        State::Closed(CloseReason::Transport(Error::PeerApplication(APP_ERROR)))
    );
}

/// The closing state limits the rate of responses to incoming packets (RFC 9000, Section 10.2.1).
#[test]
fn closing_rate_limited() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    client.close(now(), 7, "");
    assert!(client.process_output(now()).dgram().is_some());

    let trigger = send_something(&mut server, now());
    let responses: Vec<_> = (1..=16)
        .filter(|_| {
            client.process_input(trigger.clone(), now());
            client.process_output(now()).dgram().is_some()
        })
        .collect();
    assert_eq!(responses, [1, 2, 4, 8, 16]);
}

/// A closing endpoint only answers packets that it attributes to the connection.
#[test]
fn closing_ignores_unattributed_packets() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    server.close(now(), 0, "");
    assert!(server.process_output(now()).dgram().is_some());
    let dropped = server.stats().dropped_rx;

    let p = send_something(&mut client, now());
    let mut junk = p.to_vec();
    junk[2] ^= 0xff; // First connection ID byte after the length.
    let junk = Datagram::new(p.source(), p.destination(), p.tos(), junk);
    assert!(server.process(Some(junk), now()).dgram().is_none());
    assert_eq!(server.stats().dropped_rx, dropped + 1);

    let moved = change_source_port(&p, 1);
    assert!(server.process(Some(moved), now()).dgram().is_none());
    assert_eq!(server.stats().dropped_rx, dropped + 2);

    assert!(server.process(Some(p), now()).dgram().is_some());
}

/// Test that a client can handle a stateless reset correctly.
#[test]
fn stateless_reset_client() {
    let mut client = default_client();
    let mut server = default_server();
    server
        .set_local_tparam(
            StatelessResetToken,
            TransportParameter::Bytes(vec![77; Srt::LEN]),
        )
        .unwrap();
    connect_force_idle(&mut client, &mut server);

    client.process_input(datagram(vec![77; 21]), now());
    assert_draining(&client, &Error::StatelessReset);
}
