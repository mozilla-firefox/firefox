// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::convert::identity;

use neqo_common::{Datagram, Ecn, Tos, event::Provider as _};
use strum::IntoEnumIterator as _;
use test_fixture::{
    DEFAULT_ADDR_V4,
    assertions::{assert_v4_path, assert_v6_path},
    fixture_init, now,
};

use crate::{
    ConnectionEvent, ConnectionId, ConnectionParameters, Output, StreamType,
    connection::tests::{
        DEFAULT_RTT, assert_path_challenge_min_len, connect_force_idle,
        connect_force_idle_with_modifier, connect_rtt_idle, default_client, default_server,
        handshake_with_modifier, migration::get_cid, new_client, new_server, send_and_receive,
        send_something, send_something_with_modifier, send_with_modifier_and_receive,
    },
    ecn, packet,
    path::Path,
    tracking::DEFAULT_LOCAL_ACK_DELAY,
};

fn assert_ecn_enabled(tos: Tos) {
    assert!(tos.is_ecn_marked());
}

fn assert_ecn_disabled(tos: Tos) {
    assert!(!tos.is_ecn_marked());
}

fn set_tos(mut d: Datagram, ecn: Ecn) -> Datagram {
    d.set_tos(ecn.into());
    d
}

fn bleach(d: Datagram) -> Datagram {
    set_tos(d, Ecn::NotEct)
}

fn remark(d: Datagram) -> Datagram {
    if d.tos().is_ecn_marked() {
        set_tos(d, Ecn::Ect1)
    } else {
        d
    }
}

fn ce(d: Datagram) -> Datagram {
    if d.tos().is_ecn_marked() {
        set_tos(d, Ecn::Ce)
    } else {
        d
    }
}

fn drop_ecn_marked_datagrams(d: Datagram) -> Option<Datagram> {
    (!d.tos().is_ecn_marked()).then_some(d)
}

/// Given that ECN validation only starts after the handshake, it does not delay
/// connection establishment.
#[test]
fn handshake_delay_with_ecn_blackhole() {
    let start = now();
    // `handshake_with_modifier` with-multi packet Intial flights will throw off the RTT calculation
    // below.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    let finish = handshake_with_modifier(
        &mut client,
        &mut server,
        start,
        DEFAULT_RTT,
        drop_ecn_marked_datagrams,
    );

    assert!(client.state().connected());
    assert!(server.state().connected());

    assert_eq!(
        (finish - start).as_millis() / DEFAULT_RTT.as_millis(),
        3,
        "expect ECN path validation to start after handshake",
    );
}

#[test]
fn request_response_delay_after_handshake_with_ecn_blackhole() {
    let mut now = now();
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    now = handshake_with_modifier(
        &mut client,
        &mut server,
        now,
        DEFAULT_RTT,
        drop_ecn_marked_datagrams,
    );

    let start = now;
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, b"ping").unwrap();
    client.stream_close_send(stream_id).unwrap();

    // Wait for client to send a non-ECN-marked datagram.
    let client_dg = loop {
        match client.process_output(now) {
            Output::Datagram(dg) if !dg.tos().is_ecn_marked() => break dg,
            Output::Callback(dur) => now += dur,
            _ => {}
        }
    };

    server.process_input(client_dg, now);
    let stream_id = server
        .events()
        .find_map(|e| match e {
            ConnectionEvent::RecvStreamReadable { stream_id, .. } => Some(stream_id),
            _ => None,
        })
        .unwrap();
    let mut buf = vec![];
    server.stream_recv(stream_id, &mut buf).unwrap();
    server.stream_send(stream_id, b"pong").unwrap();
    server.stream_close_send(stream_id).unwrap();

    // Wait for server to send a non-ECN-marked datagram.
    let server_dg = loop {
        match server.process_output(now) {
            Output::Datagram(dg) if !dg.tos().is_ecn_marked() => break dg,
            Output::Callback(dur) => now += dur,
            _ => {}
        }
    };

    client.process_input(server_dg, now);
    client
        .events()
        .find_map(|e| match e {
            ConnectionEvent::RecvStreamReadable { stream_id, .. } => Some(stream_id),
            _ => None,
        })
        .unwrap();

    assert_eq!(
        (now - start).as_millis() / DEFAULT_RTT.as_millis(),
        8,
        "expect ECN path validation to start after handshake",
    );
}

#[test]
fn migration_delay_to_ecn_blackhole() {
    let mut now = now();
    let mut client = default_client();
    let mut server = default_server();

    // Do a handshake.
    connect_force_idle(&mut client, &mut server);

    // Migrate the client.
    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), false, now)
        .unwrap();

    // The client should send Path::MAX_PROBES path challenges with ECN enabled, and then another
    // Path::MAX_PROBES without ECN.
    let mut probes = 0;
    while probes < Path::MAX_PROBES * 2 {
        match client.process_output(now) {
            Output::Callback(t) => {
                now += t;
            }
            Output::Datagram(d) => {
                // The new path is IPv4.
                if d.source().is_ipv4() {
                    // This should be a PATH_CHALLENGE.
                    probes += 1;
                    assert_eq!(client.stats().frame_tx.path_challenge, probes);
                    assert_path_challenge_min_len(&client, &d, now);
                    if probes <= Path::MAX_PROBES {
                        // The first probes should be sent with ECN.
                        assert_ecn_enabled(d.tos());
                    } else {
                        // The next probes should be sent without ECN.
                        assert_ecn_disabled(d.tos());
                    }
                }
            }
            Output::None => panic!("unexpected output"),
        }
    }
}

#[test]
fn stats() {
    let now = now();
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    for _ in 0..ecn::TEST_COUNT {
        let ack = send_and_receive(&mut client, &mut server, now);
        client.process_input(ack.unwrap(), now);
    }

    for _ in 0..ecn::TEST_COUNT {
        let ack = send_and_receive(&mut server, &mut client, now);
        server.process_input(ack.unwrap(), now);
    }

    for stats in [client.stats(), server.stats()] {
        for (outcome, count) in stats.ecn_path_validation.iter() {
            match outcome {
                ecn::ValidationOutcome::Capable => assert_eq!(*count, 1),
                ecn::ValidationOutcome::NotCapable(_) => assert_eq!(*count, 0),
            }
        }

        for packet_type in packet::Type::iter() {
            for codepoint in [Ecn::Ect1, Ecn::Ce] {
                assert_eq!(stats.ecn_tx[packet_type][codepoint], 0);
                assert_eq!(stats.ecn_tx_acked[packet_type][codepoint], 0);
                assert_eq!(stats.ecn_rx[packet_type][codepoint], 0);
            }
        }
    }

    for packet_type in packet::Type::iter() {
        assert!(
            client.stats().ecn_tx_acked[packet_type][Ecn::Ect0]
                <= server.stats().ecn_rx[packet_type][Ecn::Ect0]
        );
        assert!(
            server.stats().ecn_tx_acked[packet_type][Ecn::Ect0]
                <= client.stats().ecn_rx[packet_type][Ecn::Ect0]
        );
        assert_eq!(
            client.stats().ecn_tx[packet_type][Ecn::Ect0],
            server.stats().ecn_rx[packet_type][Ecn::Ect0]
        );
        assert_eq!(
            server.stats().ecn_tx[packet_type][Ecn::Ect0],
            client.stats().ecn_rx[packet_type][Ecn::Ect0]
        );
    }
}

#[test]
fn disables_on_loss() {
    let now = now();
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    // Right after the handshake, the ECN validation should still be in progress.
    let client_pkt = send_something(&mut client, now);
    assert_ecn_enabled(client_pkt.tos());

    for _ in 0..ecn::TEST_COUNT {
        send_something(&mut client, now);
    }

    // ECN should now be disabled.
    let client_pkt = send_something(&mut client, now);
    assert_ecn_disabled(client_pkt.tos());
}

#[test]
fn disables_on_remark() {
    let now = now();
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    for _ in 0..ecn::TEST_COUNT {
        if let Some(ack) = send_with_modifier_and_receive(&mut client, &mut server, now, remark) {
            client.process_input(ack, now);
        }
    }

    // ECN should now be disabled.
    let client_pkt = send_something(&mut client, now);
    assert_ecn_disabled(client_pkt.tos());
}

/// This function performs a handshake over a path that modifies packets via `orig_path_modifier`.
/// It then sends `burst` packets on that path, and then migrates to a new path that
/// modifies packets via `new_path_modifier`.  It sends `burst` packets on the new path.
/// The function returns the TOS value of the last packet sent on the old path and the TOS value
/// of the last packet sent on the new path to allow for verification of correct behavior.
pub fn migration_with_modifiers(
    orig_path_modifier: fn(Datagram) -> Datagram,
    new_path_modifier: fn(Datagram) -> Datagram,
    drop_new_path: bool,
    burst: usize,
) -> (Tos, Tos, bool) {
    fixture_init();
    let mut client = new_client(ConnectionParameters::default().max_streams(StreamType::UniDi, 64));
    let mut server = new_server(ConnectionParameters::default().max_streams(StreamType::UniDi, 64));

    connect_force_idle_with_modifier(&mut client, &mut server, orig_path_modifier);
    let mut now = now();

    // Right after the handshake, the ECN validation should be in progress.
    let client_pkt = send_something(&mut client, now);
    assert_ecn_enabled(client_pkt.tos());
    server.process_input(orig_path_modifier(client_pkt), now);

    // Send some data on the current path.
    for _ in 0..burst {
        let client_pkt = send_something_with_modifier(&mut client, now, orig_path_modifier);
        server.process_input(client_pkt, now);
    }

    if let Some(ack) = server.process_output(now).dgram() {
        client.process_input(ack, now);
    }

    let client_pkt = send_something(&mut client, now);
    let tos_before_migration = client_pkt.tos();
    server.process_input(orig_path_modifier(client_pkt), now);

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), false, now)
        .unwrap();

    let mut migrated = false;
    let probe = new_path_modifier(client.process_output(now).dgram().unwrap());
    if !drop_new_path {
        assert_v4_path(&probe, true); // Contains PATH_CHALLENGE.
        assert_path_challenge_min_len(&client, &probe, now);
        assert_eq!(client.stats().frame_tx.path_challenge, 1);
        let probe_cid = ConnectionId::from(get_cid(&probe));

        let resp = new_path_modifier(server.process(Some(probe), now).dgram().unwrap());
        assert_v4_path(&resp, true);
        assert_eq!(server.stats().frame_tx.path_response, 1);
        assert_eq!(server.stats().frame_tx.path_challenge, 1);
        assert_path_challenge_min_len(&server, &resp, now);

        // Data continues to be exchanged on the old path.
        let client_data = send_something_with_modifier(&mut client, now, orig_path_modifier);
        assert_ne!(get_cid(&client_data), probe_cid);
        assert_v6_path(&client_data, false);
        server.process_input(client_data, now);
        let server_data = send_something_with_modifier(&mut server, now, orig_path_modifier);
        assert_v6_path(&server_data, false);
        client.process_input(server_data, now);

        // Once the client receives the probe response, it migrates to the new path.
        client.process_input(resp, now);
        assert_eq!(client.stats().frame_rx.path_challenge, 1);
        migrated = true;

        let migrate_client = send_something_with_modifier(&mut client, now, new_path_modifier);
        assert_v4_path(&migrate_client, true); // Responds to server probe.

        // The server now sees the migration and will switch over.
        // However, it will probe the old path again, even though it has just
        // received a response to its last probe, because it needs to verify
        // that the migration is genuine.
        server.process_input(migrate_client, now);
    }

    let stream_before = server.stats().frame_tx.stream;
    let probe_old_server = send_something_with_modifier(&mut server, now, orig_path_modifier);
    // This is just the double-check probe; no STREAM frames.
    assert_v6_path(&probe_old_server, migrated);
    assert_eq!(
        server.stats().frame_tx.path_challenge,
        if migrated { 2 } else { 0 }
    );
    assert_eq!(migrated, !drop_new_path);
    if migrated {
        assert_path_challenge_min_len(&server, &probe_old_server, now);
    }

    assert_eq!(
        server.stats().frame_tx.stream,
        if migrated { stream_before } else { 1 }
    );

    if migrated {
        // The server then sends data on the new path.
        let migrate_server = new_path_modifier(server.process_output(now).dgram().unwrap());
        assert_v4_path(&migrate_server, false);
        assert_eq!(server.stats().frame_tx.path_challenge, 2);
        assert_eq!(server.stats().frame_tx.stream, stream_before + 1);

        // The client receives these checks and responds to the probe, but uses the new path.
        client.process_input(migrate_server, now);
        client.process_input(probe_old_server, now);
        let old_probe_resp = send_something_with_modifier(&mut client, now, new_path_modifier);
        assert_v6_path(&old_probe_resp, true);
        let client_confirmation = client.process_output(now).dgram().unwrap();
        assert_v4_path(&client_confirmation, false);

        // At its delayed ACK time, if any, the server still sends on the new path.
        let server_now = server.acks.ack_time(now).unwrap_or(now);
        let server_confirmation =
            send_something_with_modifier(&mut server, server_now, new_path_modifier);
        assert_v4_path(&server_confirmation, false);
        client.process_input(server_confirmation, now);

        // Send some data on the new path.
        for _ in 0..burst {
            now += client.process_output(now).callback();
            let client_pkt = send_something_with_modifier(&mut client, now, new_path_modifier);
            server.process_input(client_pkt, now);
        }

        if let Some(ack) = server.process_output(now).dgram() {
            client.process_input(ack, now);
        }
    }

    now += client.process_output(now).callback();
    let mut client_pkt = send_something(&mut client, now);
    while !migrated && client_pkt.source() == DEFAULT_ADDR_V4 {
        client_pkt = send_something(&mut client, now);
    }
    let tos_after_migration = client_pkt.tos();
    (tos_before_migration, tos_after_migration, migrated)
}

#[test]
fn ecn_migration_zero_burst_all_cases() {
    for orig_path_mod in [identity, bleach, remark, ce] {
        for new_path_mod in [identity, bleach, remark, ce] {
            let (before, after, migrated) =
                migration_with_modifiers(orig_path_mod, new_path_mod, false, 0);
            // Too few packets sent before and after migration to conclude ECN validation.
            assert_ecn_enabled(before);
            assert_ecn_enabled(after);
            assert!(migrated);
        }

        let (before, after, migrated) = migration_with_modifiers(orig_path_mod, identity, true, 0);
        assert_ecn_enabled(before);
        assert_ecn_enabled(after);
        assert!(!migrated);
    }
}

#[test]
fn ecn_migration_noop_bleach_data() {
    let (before, after, migrated) =
        migration_with_modifiers(identity, bleach, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration.
    assert_ecn_disabled(after); // ECN validation fails after migration due to bleaching.
    assert!(migrated);
}

#[test]
fn ecn_migration_noop_remark_data() {
    let (before, after, migrated) =
        migration_with_modifiers(identity, remark, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration.
    assert_ecn_disabled(after); // ECN validation fails after migration due to remarking.
    assert!(migrated);
}

#[test]
fn ecn_migration_noop_ce_data() {
    let (before, after, migrated) = migration_with_modifiers(identity, ce, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration.
    assert_ecn_enabled(after); // ECN validation concludes after migration, despite all CE marks.
    assert!(migrated);
}

#[test]
fn ecn_migration_noop_drop_data() {
    let (before, after, migrated) =
        migration_with_modifiers(identity, identity, true, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration.
    assert_ecn_enabled(after); // Migration failed, ECN on original path is still validated.
    assert!(!migrated);
}

#[test]
fn ecn_migration_bleach_noop_data() {
    let (before, after, migrated) =
        migration_with_modifiers(bleach, identity, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to bleaching.
    assert_ecn_enabled(after); // ECN validation concludes after migration.
    assert!(migrated);
}

#[test]
fn ecn_migration_bleach_bleach_data() {
    let (before, after, migrated) =
        migration_with_modifiers(bleach, bleach, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to bleaching.
    assert_ecn_disabled(after); // ECN validation fails after migration due to bleaching.
    assert!(migrated);
}

#[test]
fn ecn_migration_bleach_remark_data() {
    let (before, after, migrated) =
        migration_with_modifiers(bleach, remark, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to bleaching.
    assert_ecn_disabled(after); // ECN validation fails after migration due to remarking.
    assert!(migrated);
}

#[test]
fn ecn_migration_bleach_ce_data() {
    let (before, after, migrated) = migration_with_modifiers(bleach, ce, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to bleaching.
    assert_ecn_enabled(after); // ECN validation concludes after migration, despite all CE marks.
    assert!(migrated);
}

#[test]
fn ecn_migration_bleach_drop_data() {
    let (before, after, migrated) =
        migration_with_modifiers(bleach, identity, true, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to bleaching.
    // Migration failed, ECN on original path is still disabled.
    assert_ecn_disabled(after);
    assert!(!migrated);
}

#[test]
fn ecn_migration_remark_noop_data() {
    let (before, after, migrated) =
        migration_with_modifiers(remark, identity, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to remarking.
    assert_ecn_enabled(after); // ECN validation succeeds after migration.
    assert!(migrated);
}

#[test]
fn ecn_migration_remark_bleach_data() {
    let (before, after, migrated) =
        migration_with_modifiers(remark, bleach, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to remarking.
    assert_ecn_disabled(after); // ECN validation fails after migration due to bleaching.
    assert!(migrated);
}

#[test]
fn ecn_migration_remark_remark_data() {
    let (before, after, migrated) =
        migration_with_modifiers(remark, remark, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to remarking.
    assert_ecn_disabled(after); // ECN validation fails after migration due to remarking.
    assert!(migrated);
}

#[test]
fn ecn_migration_remark_ce_data() {
    let (before, after, migrated) = migration_with_modifiers(remark, ce, false, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to remarking.
    assert_ecn_enabled(after); // ECN validation concludes after migration, despite all CE marks.
    assert!(migrated);
}

#[test]
fn ecn_migration_remark_drop_data() {
    let (before, after, migrated) =
        migration_with_modifiers(remark, identity, true, ecn::TEST_COUNT);
    assert_ecn_disabled(before); // ECN validation fails before migration due to remarking.
    assert_ecn_disabled(after); // Migration failed, ECN on original path is still disabled.
    assert!(!migrated);
}

#[test]
fn ecn_migration_ce_noop_data() {
    let (before, after, migrated) = migration_with_modifiers(ce, identity, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration, despite all CE marks.
    assert_ecn_enabled(after); // ECN validation concludes after migration.
    assert!(migrated);
}

#[test]
fn ecn_migration_ce_bleach_data() {
    let (before, after, migrated) = migration_with_modifiers(ce, bleach, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration, despite all CE marks.
    assert_ecn_disabled(after); // ECN validation fails after migration due to bleaching
    assert!(migrated);
}

#[test]
fn ecn_migration_ce_remark_data() {
    let (before, after, migrated) = migration_with_modifiers(ce, remark, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration, despite all CE marks.
    assert_ecn_disabled(after); // ECN validation fails after migration due to remarking.
    assert!(migrated);
}

#[test]
fn ecn_migration_ce_ce_data() {
    let (before, after, migrated) = migration_with_modifiers(ce, ce, false, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration, despite all CE marks.
    assert_ecn_enabled(after); // ECN validation concludes after migration, despite all CE marks.
    assert!(migrated);
}

#[test]
fn ecn_migration_ce_drop_data() {
    let (before, after, migrated) = migration_with_modifiers(ce, identity, true, ecn::TEST_COUNT);
    assert_ecn_enabled(before); // ECN validation concludes before migration, despite all CE marks.
    // Migration failed, ECN on original path is still enabled.
    assert_ecn_enabled(after);
    assert!(!migrated);
}

/// RFC 9000 Section 13.2.1: CE-marked ack-eliciting packets are acknowledged immediately.
#[test]
fn ce_acked_immediately() {
    let mut client = default_client();
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);
    // The server last acknowledged more than an RTT ago, so it acknowledges at once.
    let ack = send_and_receive(&mut client, &mut server, now).unwrap();
    client.process_input(ack, now);

    // Without a CE mark, the ACK is delayed.
    assert!(send_and_receive(&mut client, &mut server, now).is_none());
    let delay = server.process_output(now).callback();
    assert_eq!(delay, DEFAULT_LOCAL_ACK_DELAY);
    now += delay;
    let ack = server.process_output(now).dgram().unwrap();
    client.process_input(ack, now);

    let acks = server.stats().frame_tx.ack;
    send_with_modifier_and_receive(&mut client, &mut server, now, ce);
    assert_eq!(server.stats().ecn_rx[packet::Type::Short][Ecn::Ce], 1);
    assert_eq!(server.stats().frame_tx.ack, acks + 1);
}

/// RFC 9000 Section 13.2.1: a CE-marked packet that is not ack-eliciting does not elicit an ACK.
#[test]
fn ce_ack_only_not_acked() {
    let mut client = default_client();
    let mut server = default_server();
    let mut now = connect_rtt_idle(&mut client, &mut server, DEFAULT_RTT);
    let dgram = send_something(&mut server, now);
    now += client.process(Some(dgram), now).callback();
    let ack = client.process_output(now).dgram().unwrap();

    server.process_input(ce(ack), now);
    assert_eq!(server.stats().ecn_rx[packet::Type::Short][Ecn::Ce], 1);
    assert_eq!(server.acks.ack_time(now), None);
    assert!(server.process_output(now).dgram().is_none());
}
