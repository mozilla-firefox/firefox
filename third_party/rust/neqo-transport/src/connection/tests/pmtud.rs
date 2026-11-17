// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    time::Duration,
};

use neqo_common::{Datagram, Tos};
use test_fixture::{DEFAULT_ADDR, DEFAULT_ADDR_V4, fixture_init, now};

use super::Connection;
use crate::{
    ConnectionParameters, Output, Pmtud, Stats, StreamType,
    connection::tests::{
        DEFAULT_RTT, connect, default_server, fill_stream, new_client, new_client_with_addr,
        new_server, send_something,
    },
};

/// Test that one can reach the maximum MTU with GSO enabled.
#[test]
fn gso_with_max_mtu() {
    neqo_common::log::init(None);
    let mut client = new_client_with_addr(
        ConnectionParameters::default()
            .pmtud(true)
            .pmtud_iface_mtu(false),
        DEFAULT_ADDR_V4,
    );

    let mut server = default_server();

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    // Increase MTU to the max.
    loop {
        fill_stream(&mut client, stream_id);
        let mut pkts = client
            .process_multiple_output(now(), 2.try_into().unwrap())
            .dgram()
            .unwrap();
        if pkts.datagram_size().get() == 65507 {
            // Success. It reached the maximum IPv4 UDP MTU.
            break;
        }
        assert!(pkts.datagram_size().get() < 65507);

        server.process_multiple_input(pkts.iter_mut(), now());
        let ack = server.process_output(now()).dgram();
        client.process_input(ack.unwrap(), now());
    }
}

/// Use an IPv6 address since the default test connection uses IPv6.
const VPN_ADDR: SocketAddr = SocketAddr::new(
    IpAddr::V6(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1)),
    12345,
);

/// Simulates VPN by changing the source address of a datagram to an IPv6 VPN endpoint.
fn via_vpn(d: &Datagram) -> Datagram {
    Datagram::new(VPN_ADDR, d.destination(), d.tos(), &d[..])
}

/// Exchanges packets between sender and receiver until PMTUD settles,
/// dropping any packets larger than `mtu`.
fn drive_pmtud(
    sender: &mut Connection,
    receiver: &mut Connection,
    mtu: usize,
    mut now: std::time::Instant,
) -> std::time::Instant {
    if let Ok(stream_id) = sender.stream_create(StreamType::UniDi) {
        fill_stream(sender, stream_id);
    }
    loop {
        match sender.process_output(now) {
            Output::Datagram(d) => {
                if d.len() <= mtu {
                    receiver.process_input(d, now);
                }
            }
            Output::Callback(t) => {
                // Get ACKs from receiver.
                while let Some(d) = receiver.process_output(now).dgram() {
                    if d.len() <= mtu {
                        sender.process_input(d, now);
                    }
                }
                if t >= DEFAULT_RTT {
                    break; // PMTUD has settled (waiting for PTO or raise timer).
                }
                now += t;
            }
            Output::None => break,
        }
    }
    now
}

/// Tests that when a client goes through a VPN (packets arrive from different IP),
/// the server initiates a path challenge, and both client and server run PMTUD
/// to discover the VPN's lower MTU.
///
/// Scenario:
/// 1. Connection established, both sides discover initial MTU (1500)
/// 2. VPN is brought up - all traffic now flows through VPN with lower MTU (1400)
/// 3. Server sees packets from new IP, sends `PATH_CHALLENGE`, creates new path, runs PMTUD
/// 4. Client receives `PATH_CHALLENGE`, which triggers PMTUD on its path
/// 5. Both sides discover the VPN's lower MTU
#[test]
fn vpn_migration_triggers_pmtud() {
    fixture_init();
    let mut now = now();
    let mut client = new_client(ConnectionParameters::default().pmtud(true));
    let mut server = new_server(ConnectionParameters::default().pmtud(true));
    let header_size = Pmtud::header_size(
        client
            .paths
            .primary()
            .unwrap()
            .borrow()
            .local_address()
            .ip(),
    );
    let initial_path_mtu = 1500 - header_size;
    let vpn_path_mtu = 1400 - header_size;

    connect(&mut client, &mut server);
    assert_eq!(client.plpmtu(), 1232, "PMTU should be IPv6 default");
    assert_eq!(server.plpmtu(), 1232, "PMTU should be IPv6 default");

    // Drive PMTUD on the initial path.
    now = drive_pmtud(&mut client, &mut server, initial_path_mtu, now);
    now = drive_pmtud(&mut server, &mut client, initial_path_mtu, now);
    assert_eq!(client.plpmtu(), initial_path_mtu);
    assert_eq!(server.plpmtu(), initial_path_mtu);

    // VPN is now brought up; client sends data, but from the server's perspective, it now arrives
    // from the VPN tunnel endpoint address.
    let c1 = send_something(&mut client, now);
    let c1_via_vpn = via_vpn(&c1);

    // Server receives packet from "new" source IP (VPN endpoint).
    // This triggers path validation (PATH_CHALLENGE) on a new path.
    let before_challenge = server.stats().frame_tx.path_challenge;
    let s1 = server.process(Some(c1_via_vpn), now).dgram();
    assert!(s1.is_some(), "Server should respond");
    assert_eq!(server.stats().frame_tx.path_challenge, before_challenge + 1);

    // Client receives the PATH_CHALLENGE. This triggers PMTUD restart on its path.
    let s1 = s1.unwrap();
    let s1_to_client = Datagram::new(s1.source(), c1.source(), s1.tos(), &s1[..]);
    let client_pmtud_tx_before_challenge = client.stats().pmtud_tx;
    let before_response = client.stats().frame_tx.path_response;
    let c2 = client.process(Some(s1_to_client), now).dgram();
    assert!(c2.is_some(), "Client should respond with PATH_RESPONSE");
    assert_eq!(client.stats().frame_tx.path_response, before_response + 1);

    // Server receives PATH_RESPONSE via VPN.
    let c2 = c2.unwrap();
    let c2_via_vpn = via_vpn(&c2);
    server.process_input(c2_via_vpn, now);

    // Record PMTUD probe counts before driving traffic on VPN path.
    let server_pmtud_tx_before = server.stats().pmtud_tx;

    // Drive PMTUD probing for both sides on the VPN path with smaller MTU.
    let now = drive_pmtud(&mut client, &mut server, vpn_path_mtu, now);
    drive_pmtud(&mut server, &mut client, vpn_path_mtu, now);

    // Verify server and client sent PMTUD probes on the new path.
    assert!(server.stats().pmtud_tx > server_pmtud_tx_before);
    assert!(client.stats().pmtud_tx > client_pmtud_tx_before_challenge);

    // Verify both sides' PMTU reflects the VPN path's smaller MTU.
    // vpn_path_mtu is 1400; the largest IPv6 search table entry <= 1400 is 1380.
    let expected_vpn_mtu = 1380 - header_size;
    assert_eq!(server.plpmtu(), expected_vpn_mtu);
    assert_eq!(client.plpmtu(), expected_vpn_mtu);
}

/// Delivers all datagrams `from` has ready at `now` to `to`.
fn deliver_all(from: &mut Connection, to: &mut Connection, now: std::time::Instant) {
    while let Some(d) = from.process_output(now).dgram() {
        to.process_input(d, now);
    }
}

/// A PMTUD probe that is merely delayed is declared lost and retried. When the delayed probe and
/// then its retry are acknowledged, the second ACK must not make the sender believe that the *next*
/// probe size works, since it has never been sent.
#[test]
fn delayed_probe_and_retry_do_not_confirm_untested_mtu() {
    fixture_init();
    let mut now = now();
    let mut client = new_client(ConnectionParameters::default().pmtud(true));
    let mut server = default_server();
    connect(&mut client, &mut server);

    // The ACK for the first probe, which was sent during the handshake, is still at the server.
    deliver_all(&mut server, &mut client, now);
    let confirmed = client.plpmtu();
    assert_eq!(client.stats().pmtud_ack, 1);

    // The next probe is delayed by the network.
    let delayed = client.process_output(now).dgram().unwrap();
    let probe_len = delayed.len();
    assert!(probe_len > confirmed);
    assert_eq!(client.stats().pmtud_tx, 2);

    // Later packets get through and are acknowledged. That makes the delayed probe look lost.
    for _ in 0..4 {
        let d = send_something(&mut client, now);
        server.process_input(d, now);
    }
    now += Duration::from_millis(30);
    deliver_all(&mut server, &mut client, now);
    assert_eq!(client.stats().pmtud_lost, 1);

    // The probe is retried.
    let retry = client.process_output(now).dgram().unwrap();
    assert_eq!(retry.len(), probe_len);
    assert_eq!(client.stats().pmtud_tx, 3);

    // The delayed probe is finally delivered, which confirms its size.
    server.process_input(delayed, now);
    now += Duration::from_millis(30);
    deliver_all(&mut server, &mut client, now);
    assert_eq!(client.stats().pmtud_ack, 2);
    assert_eq!(client.plpmtu(), probe_len);

    // The retry is delivered, too. It is for the same size, so it must not confirm a larger one.
    server.process_input(retry, now);
    now += Duration::from_millis(30);
    deliver_all(&mut server, &mut client, now);
    assert_eq!(client.stats().pmtud_ack, 2);
    assert_eq!(client.plpmtu(), probe_len, "an untested MTU was confirmed");

    // The next size is still being probed.
    let next = client.process_output(now).dgram().unwrap();
    assert!(next.len() > probe_len);
    assert_eq!(client.stats().pmtud_tx, 4);
}

/// Migrates the server to an unvalidated path with a `len`-byte client packet, checks that it sends
/// no probe and stays within the amplification limit, and returns the PLPMTU and that limit.
fn no_probe_on_unvalidated_path(len: usize) -> (usize, usize) {
    fixture_init();
    let now = now();
    let mut client = new_client(ConnectionParameters::default().pmtud(true));
    let mut server = new_server(ConnectionParameters::default().pmtud(true));
    connect(&mut client, &mut server);
    let stream = server.stream_create(StreamType::UniDi).unwrap();
    fill_stream(&mut server, stream); // The server will fill whatever limit it is given.

    let c_stream = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(c_stream, &vec![0x42; len]).unwrap();
    let small = client.process_output(now).dgram().unwrap();
    server.process_input(via_vpn(&small), now);

    let (plpmtu, limit) = {
        let path = server.paths.primary().unwrap();
        let path = path.borrow();
        assert!(!path.is_valid() && path.pmtud().needs_probe());
        assert!(path.amplification_limit() < path.pmtud().probe_size());
        (path.plpmtu(), path.amplification_limit())
    };

    let pmtud_tx = server.stats().pmtud_tx;
    let d = server.process_output(now).dgram().unwrap();
    assert_eq!(d.destination(), VPN_ADDR);
    assert_eq!(server.stats().pmtud_tx, pmtud_tx);
    assert!(d.len() <= limit);
    (plpmtu, limit)
}

/// A deferred probe must not enlarge the packets sent meanwhile.
#[test]
fn deferred_probe_respects_amplification_limit() {
    let (plpmtu, limit) = no_probe_on_unvalidated_path(150);
    assert!(limit < plpmtu);
}

/// A probe must wait for a budget covering the probe, not just the PLPMTU.
#[test]
fn probe_waits_for_budget_covering_probe_size() {
    let (plpmtu, limit) = no_probe_on_unvalidated_path(400);
    assert!(plpmtu < limit);
}

const LOOPBACK: SocketAddr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 443);

fn connected_loopback(params: ConnectionParameters) -> (Connection, Connection) {
    let mut client = new_client_with_addr(params, LOOPBACK);
    let mut server = default_server();
    connect(&mut client, &mut server);
    (client, server)
}

fn loopback_iface_mtu() -> usize {
    mtu::interface_and_mtu(LOOPBACK.ip()).unwrap().1
}

#[test]
fn iface_mtu_for_first_path() {
    let client = new_client_with_addr(ConnectionParameters::default().pmtud(true), LOOPBACK);
    assert_eq!(client.stats().pmtud_iface_mtu, loopback_iface_mtu());
}

#[test]
fn no_iface_mtu_for_junk_from_unknown_address() {
    let mut client = new_client(ConnectionParameters::default().pmtud(true));
    let mut server = default_server();
    connect(&mut client, &mut server);
    let iface_mtu = client.stats().pmtud_iface_mtu;
    client.process_input(
        Datagram::new(LOOPBACK, DEFAULT_ADDR, Tos::default(), [0x40]),
        now(),
    );
    assert_eq!(client.stats().pmtud_iface_mtu, iface_mtu);
}

/// PMTUD settles where it would if the path dropped packets above the interface MTU.
#[test]
fn iface_mtu_caps_pmtud() {
    let (mut client, mut server) = connected_loopback(ConnectionParameters::default().pmtud(true));
    drive_pmtud(&mut client, &mut server, usize::MAX, now());
    let (mut lossy, mut server) = connected_loopback(
        ConnectionParameters::default()
            .pmtud(true)
            .pmtud_iface_mtu(false),
    );
    let path_mtu = loopback_iface_mtu() - Pmtud::header_size(LOOPBACK.ip());
    drive_pmtud(&mut lossy, &mut server, path_mtu, now());
    assert_eq!(client.plpmtu(), lossy.plpmtu());
}

#[test]
fn no_iface_mtu_without_pmtud() {
    let iface_mtu = Stats::default().pmtud_iface_mtu;
    let (client, _) = connected_loopback(ConnectionParameters::default().pmtud(false));
    assert_eq!(client.stats().pmtud_iface_mtu, iface_mtu);
}
