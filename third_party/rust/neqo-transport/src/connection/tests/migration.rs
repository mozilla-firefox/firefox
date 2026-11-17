// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![allow(
    clippy::allow_attributes,
    clippy::unwrap_in_result,
    reason = "OK in tests."
)]

use std::{
    cell::RefCell,
    cmp::min,
    net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    rc::Rc,
    time::{Duration, Instant},
};

use neqo_common::{Datagram, Decoder, event::Provider as _, to_u64};
use test_fixture::{
    DEFAULT_ADDR, DEFAULT_ADDR_V4,
    assertions::{assert_v4_path, assert_v6_path},
    fixture_init, new_neqo_qlog, now, split_datagram,
};
use tracing::debug;

use super::{
    super::{Connection, Output, State, StreamType},
    AT_LEAST_PTO, CountingConnectionIdGenerator, DEFAULT_RTT, assert_error, connect_fail,
    connect_force_idle, connect_rtt_idle, cwnd_avail, default_client, default_server,
    exchange_ticket, fill_cwnd, fill_stream, handshake, maybe_authenticate, new_client, new_server,
    resumed_server, send_something, zero_len_cid_client, zero_len_cid_server,
};
use crate::{
    CloseReason, ConnectionEvent, ConnectionId, ConnectionIdDecoder, ConnectionIdGenerator,
    ConnectionIdRef, ConnectionParameters, EmptyConnectionIdGenerator, Error,
    MIN_INITIAL_PACKET_SIZE,
    addr_valid::{AddressValidation, ValidateAddress},
    cid::ConnectionIdManager,
    connection::{
        state::StateSignaling,
        test_internal,
        tests::{
            assert_path_challenge_min_len, connect, send_something_paced, send_with_extra,
            stream::Writer,
        },
    },
    frame::FrameType,
    packet,
    path::{Path, PathRef},
    pmtud::Pmtud,
    recovery::MAX_PTO_PACKET_COUNT,
    stats::FrameStats,
    tparams::{PreferredAddress, TransportParameter, TransportParameterId},
    tracking::PacketNumberSpace::ApplicationData,
};

/// This should be a valid-seeming transport parameter.
/// And it should have different values to `addr` and `addr_v4`.
const SAMPLE_PREFERRED_ADDRESS: &[u8] = &[
    0xc0, 0x00, 0x02, 0x02, 0x01, 0xbb, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0xbb, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
];

/// Supplies only the initial CID, then returns `None`. Panics after a bounded number of calls
/// so a regression that keeps requesting CIDs fails instead of hanging the test suite.
struct ExhaustibleConnectionIdGenerator {
    inner: Option<CountingConnectionIdGenerator>,
    attempts_left: usize,
}

impl ConnectionIdDecoder for ExhaustibleConnectionIdGenerator {
    fn decode_cid<'a>(&self, dec: &mut Decoder<'a>) -> Option<ConnectionIdRef<'a>> {
        // Decoding is stateless and must still work after the generator has been consumed.
        CountingConnectionIdGenerator::default().decode_cid(dec)
    }
}

impl ConnectionIdGenerator for ExhaustibleConnectionIdGenerator {
    fn generate_cid(&mut self) -> Option<ConnectionId> {
        assert!(
            self.attempts_left > 0,
            "CID generation did not stop after exhaustion"
        );
        self.attempts_left -= 1;
        self.inner.take()?.generate_cid()
    }
}

/// Exhausting either endpoint's generator after its initial CID must not block the handshake
/// or subsequent stream output. Each case exhausts a different endpoint; both message directions
/// use that same connection pair to check that it can still send and receive while exhausted.
#[test]
fn cid_generator_exhaustion_allows_connection_progress() {
    fn transfer(sender: &mut Connection, receiver: &mut Connection) {
        const MESSAGE: &[u8] = b"hello";

        let stream_id = sender.stream_create(StreamType::UniDi).unwrap();
        assert_eq!(
            sender.stream_send(stream_id, MESSAGE).unwrap(),
            MESSAGE.len()
        );
        sender.stream_close_send(stream_id).unwrap();
        let datagram = sender.process_output(now()).dgram().unwrap();
        receiver.process_input(datagram, now());
        let mut buf = [0; MESSAGE.len()];
        assert_eq!(
            receiver.stream_recv(stream_id, &mut buf).unwrap(),
            (MESSAGE.len(), true)
        );
        assert_eq!(buf, MESSAGE);
    }

    fixture_init();
    for exhausted_client in [false, true] {
        let generator: Rc<RefCell<dyn ConnectionIdGenerator>> =
            Rc::new(RefCell::new(ExhaustibleConnectionIdGenerator {
                inner: Some(CountingConnectionIdGenerator::default()),
                attempts_left: 100,
            }));
        let (mut client, mut server) = if exhausted_client {
            let client = Connection::new_client(
                test_fixture::DEFAULT_SERVER_NAME,
                test_fixture::DEFAULT_ALPN,
                generator,
                DEFAULT_ADDR,
                DEFAULT_ADDR,
                ConnectionParameters::default(),
                now(),
            )
            .unwrap();
            (client, default_server())
        } else {
            let server = Connection::new_server(
                test_fixture::DEFAULT_KEYS,
                test_fixture::DEFAULT_ALPN,
                generator,
                ConnectionParameters::default(),
            )
            .unwrap();
            (default_client(), server)
        };
        connect(&mut client, &mut server);

        transfer(&mut client, &mut server);
        transfer(&mut server, &mut client);
        assert_eq!(*client.state(), State::Confirmed);
        assert_eq!(*server.state(), State::Confirmed);
        let exhausted = if exhausted_client { &client } else { &server };
        assert_eq!(exhausted.stats().frame_tx.new_connection_id, 0);
    }
}

// These tests generally use two paths:
// The connection is established on a path with the same IPv6 address on both ends.
// Migrations move to a path with the same IPv4 address on both ends.
// This simplifies validation as the same assertions can be used for client and server.
// The risk is that there is a place where source/destination local/remote is inverted.

const fn loopback() -> SocketAddr {
    SocketAddr::new(IpAddr::V6(Ipv6Addr::LOCALHOST), 443)
}

const fn mapped(ip: Ipv4Addr) -> SocketAddr {
    SocketAddr::new(IpAddr::V6(ip.to_ipv6_mapped()), 443)
}

fn change_path(d: &Datagram, a: SocketAddr) -> Datagram {
    Datagram::new(a, a, d.tos(), &d[..])
}

const fn new_port(a: SocketAddr) -> SocketAddr {
    let (port, _) = a.port().overflowing_add(410);
    SocketAddr::new(a.ip(), port)
}

fn assert_path_challenge(
    c: &Connection,
    d: &Datagram,
    before: &FrameStats,
    dst: SocketAddr,
    padded: bool,
) {
    let after = c.stats().frame_tx;
    assert_eq!(after.path_challenge, before.path_challenge + 1);
    assert_eq!(d.source(), DEFAULT_ADDR);
    assert_eq!(d.destination(), dst);
    if padded {
        assert!(d.len() >= MIN_INITIAL_PACKET_SIZE);
    } else {
        assert!(d.len() < MIN_INITIAL_PACKET_SIZE);
    }
}

fn assert_path_response(c: &Connection, d: &Datagram, before: &FrameStats) {
    let after = c.stats().frame_tx;
    assert_eq!(after.path_response, before.path_response + 1);
    assert_eq!(d.source(), DEFAULT_ADDR);
    assert_eq!(d.destination(), DEFAULT_ADDR);
}

fn local_address(c: &Connection) -> SocketAddr {
    c.paths.primary().unwrap().borrow().local_address()
}

fn rebind(
    client: &mut Connection,
    server: &mut Connection,
    cur_path: fn(&Datagram) -> Datagram,
    new_path: fn(&Datagram) -> Datagram,
    mut now: Instant,
) -> Instant {
    debug!("Rebinding");
    let c1 = send_something(client, now);
    let c1_new = new_path(&c1);
    debug!("Rebinding to {}", c1_new.source());

    // Server will reply to modified datagram with a PATH_CHALLENGE.
    // Due to the amplification limit, this will not be padded to MIN_INITIAL_PACKET_SIZE.
    let before = server.stats().frame_tx;
    let s1 = server.process(Some(c1_new.clone()), now).dgram().unwrap();
    assert_path_challenge(server, &s1, &before, c1_new.source(), false);

    // Restore the original source address, so to the client it looks like the path has not changed.
    let s1_reb = change_destination(&s1, local_address(client));

    // The client should respond to the PATH_CHALLENGE, without changing paths.
    let before = client.stats().frame_tx;
    let c2 = client.process(Some(s1_reb), now).dgram().unwrap();
    assert_path_response(client, &c2, &before);

    // The server should now see the response on the new path.
    // It will send another PATH_CHALLENGE padded to MIN_INITIAL_PACKET_SIZE.
    let c2_new = new_path(&c2);
    let before = server.stats().frame_tx;
    let s2 = server.process(Some(c2_new.clone()), now).dgram().unwrap();
    assert_path_challenge(server, &s2, &before, c2_new.source(), true);

    // Restore the original source address, so to the client it looks like the path has not changed.
    let s2_reb = change_destination(&s2, local_address(client));

    // The client should respond to the PATH_CHALLENGE, without changing paths.
    let before = client.stats().frame_tx;
    let c3 = client.process(Some(s2_reb.clone()), now).dgram().unwrap();
    assert_path_response(client, &s2_reb, &before);

    // The server should now see the second response on the new path.
    // It will then try to probe the old path.
    let c3_new = new_path(&c3);
    let c3_cur = cur_path(&c3);
    let before = server.stats().frame_tx;
    let s3 = server.process(Some(c3_new.clone()), now).dgram().unwrap();
    assert_path_challenge(server, &s3, &before, c3_cur.source(), true);

    // Do not deliver this probe to the client.

    // Server will now ACK on the new path.
    let before = server.stats().frame_tx;
    let s4 = server.process_output(now).dgram().unwrap();
    let after = server.stats().frame_tx;
    assert_eq!(after.ack, before.ack + 1);
    assert_eq!(s4.source(), c3_new.destination());
    assert_eq!(s4.destination(), c3_new.source());

    // Restore the original source address, so to the client it looks like the path has not changed.
    let s4_reb = change_destination(&s4, local_address(client));

    // The client should process the ACK and go idle.
    let delay = client.process(Some(s4_reb), now).callback();
    assert_eq!(delay, ConnectionParameters::DEFAULT_IDLE_TIMEOUT);

    let client_uses_zero_len_cid = client
        .paths
        .primary()
        .unwrap()
        .borrow()
        .local_cid()
        .unwrap()
        .is_empty();
    let mut total_delay = Duration::new(0, 0);
    loop {
        let before = server.stats().frame_tx;
        match server.process_output(now) {
            Output::Callback(t) => {
                total_delay += t;
                if total_delay == ConnectionParameters::DEFAULT_IDLE_TIMEOUT {
                    // Server should only hit the idle timeout here when the client uses a zero-len
                    // CID.
                    assert!(client_uses_zero_len_cid);
                    break;
                }
                now += t;
            }
            Output::Datagram(sx) => {
                total_delay = Duration::new(0, 0);
                if sx.destination() == c3_cur.source() {
                    // Old path gets path challenges.
                    assert_path_challenge(server, &sx, &before, c3_cur.source(), true);
                    // Don't deliver them.
                } else {
                    let after = server.stats().frame_tx;
                    // If the client uses a zero-len CID, the server will only PING.
                    // Otherwise, it will PING or send a RETIRE_CONNECTION_ID.
                    if client_uses_zero_len_cid {
                        assert_eq!(after.ping, before.ping + 1);
                    } else {
                        assert!(
                            after.retire_connection_id == before.retire_connection_id + 1
                                || after.ping == before.ping + 1
                        );
                    }
                    // Restore the original source address, so to the client it looks like
                    // the path has not changed.
                    let sx_r = change_destination(&sx, local_address(client));
                    let before = client.stats().frame_tx;
                    let cx = client.process(Some(sx_r), now).dgram().unwrap();
                    let after = client.stats().frame_tx;
                    assert_eq!(after.ack, before.ack + 1);
                    // Also deliver the ACK.
                    let cx_n = new_path(&cx);
                    server.process_input(cx_n, now);
                    if !client_uses_zero_len_cid
                        && after.new_connection_id == before.new_connection_id + 1
                    {
                        // Declare victory once the client has sent a new connection ID.
                        break;
                    }
                }
            }
            Output::None => panic!(),
        }
    }

    if !client_uses_zero_len_cid {
        // Eat up any delays before returning.
        now += client.process_output(now).callback();
        now += server.process_output(now).callback();
    }

    debug!("Rebinding done");
    now
}

fn inc_port(port: u16, i: usize) -> u16 {
    port.overflowing_add(i.overflowing_mul(11).0.try_into().unwrap())
        .0
}

fn inc_addr(ip: IpAddr, i: usize) -> IpAddr {
    let inc: u8 = i.overflowing_mul(11).0.try_into().unwrap();
    match ip {
        IpAddr::V4(ip) => IpAddr::V4(Ipv4Addr::from_octets(
            ip.octets().map(|b| b.overflowing_add(inc).0),
        )),
        IpAddr::V6(ip) => IpAddr::V6(Ipv6Addr::from_octets(
            ip.octets().map(|b| b.overflowing_add(inc).0),
        )),
    }
}

pub fn change_source_port(d: &Datagram, i: usize) -> Datagram {
    Datagram::new(
        SocketAddr::new(d.source().ip(), inc_port(d.source().port(), i)),
        d.destination(),
        d.tos(),
        &d[..],
    )
}

fn change_destination_port(d: &Datagram) -> Datagram {
    change_destination(d, new_port(d.destination()))
}

fn change_destination(d: &Datagram, a: SocketAddr) -> Datagram {
    Datagram::new(d.source(), a, d.tos(), &d[..])
}

fn change_source_address_and_port(d: &Datagram, i: usize) -> Datagram {
    Datagram::new(
        SocketAddr::new(inc_addr(d.source().ip(), i), inc_port(d.source().port(), i)),
        d.destination(),
        d.tos(),
        &d[..],
    )
}

fn rebind_port_with_client(client: &mut Connection) {
    let mut server = default_server();
    connect_force_idle(client, &mut server);
    let mut now = now();

    now = rebind(
        client,
        &mut server,
        |d| change_source_port(d, 0),
        |d| change_source_port(d, 1),
        now,
    );
    _ = rebind(
        client,
        &mut server,
        |d| change_source_port(d, 1),
        |d| change_source_port(d, 2),
        now,
    );
}

fn rebind_address_and_port_with_client(client: &mut Connection) {
    let mut server = default_server();
    connect_force_idle(client, &mut server);
    let mut now = now();

    now = rebind(
        client,
        &mut server,
        |d| change_source_address_and_port(d, 0),
        |d| change_source_address_and_port(d, 1),
        now,
    );
    _ = rebind(
        client,
        &mut server,
        |d| change_source_address_and_port(d, 1),
        |d| change_source_address_and_port(d, 2),
        now,
    );
}

#[test]
fn rebind_port() {
    let mut client = default_client();
    rebind_port_with_client(&mut client);
}

#[test]
fn rebind_port_zero_len_cid() {
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    rebind_port_with_client(&mut client);
}

#[test]
fn rebind_address_and_port() {
    let mut client = default_client();
    rebind_address_and_port_with_client(&mut client);
}

#[test]
fn rebind_address_and_port_zero_len_cid() {
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    rebind_address_and_port_with_client(&mut client);
}

/// This simulates an attack where a valid packet is forwarded on
/// a different path.  This shows how both paths are probed and the
/// server eventually returns to the original path.
#[test]
fn path_forwarding_attack() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    let dgram = send_something(&mut client, now);
    let dgram = change_path(&dgram, DEFAULT_ADDR_V4);
    server.process_input(dgram, now);

    // The server now probes the new (primary) path.
    let new_probe = server.process_output(now).dgram().unwrap();
    assert_eq!(server.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&server, &new_probe, now);
    assert_v4_path(&new_probe, false); // Can't be padded.

    // The server also probes the old path.
    let old_probe = server.process_output(now).dgram().unwrap();
    assert_path_challenge_min_len(&server, &old_probe, now);
    assert_eq!(server.stats().frame_tx.path_challenge, 2);
    assert_v6_path(&old_probe, true);

    // New data from the server is sent on the new path, but that is
    // now constrained by the amplification limit.
    let stream_id = server.stream_create(StreamType::UniDi).unwrap();
    server.stream_close_send(stream_id).unwrap();
    assert!(server.process_output(now).dgram().is_none());

    // The client should respond to the challenge on the new path.
    // The server couldn't pad, so the client is also amplification limited.
    let new_resp = client.process(Some(new_probe), now).dgram().unwrap();
    assert_eq!(client.stats().frame_rx.path_challenge, 1);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    assert_eq!(client.stats().frame_tx.path_response, 1);
    assert_v4_path(&new_resp, false);

    // The client also responds to probes on the old path.
    let old_resp = client.process(Some(old_probe), now).dgram().unwrap();
    assert_eq!(client.stats().frame_rx.path_challenge, 2);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    assert_eq!(client.stats().frame_tx.path_response, 2);
    assert_v6_path(&old_resp, true);

    // But the client still sends data on the old path.
    let client_data1 = send_something(&mut client, now);
    assert_v6_path(&client_data1, false); // Just data.

    // Receiving the PATH_RESPONSE from the client opens the amplification
    // limit enough for the server to respond.
    // This is padded because it includes PATH_CHALLENGE.
    let server_data1 = server.process(Some(new_resp), now).dgram().unwrap();
    assert_v4_path(&server_data1, true);
    assert_eq!(server.stats().frame_tx.path_challenge, 3);
    assert_path_challenge_min_len(&server, &server_data1, now);

    // The client responds to this probe on the new path.
    client.process_input(server_data1, now);
    let stream_before = client.stats().frame_tx.stream;
    let padded_resp = send_something(&mut client, now);
    assert_eq!(stream_before, client.stats().frame_tx.stream);
    assert_v4_path(&padded_resp, true); // This is padded!

    // But new data from the client stays on the old path.
    let client_data2 = client.process_output(now).dgram().unwrap();
    assert_v6_path(&client_data2, false);

    // The server keeps sending on the new path.
    let server_data2 = send_something(&mut server, now);
    assert_v4_path(&server_data2, false);

    // Until new data is received from the client on the old path.
    server.process_input(client_data2, now);
    // The server sends a probe on the new path.
    let server_data3 = send_something(&mut server, now);
    assert_v4_path(&server_data3, true);
    // But switches data transmission to the old path.
    let server_data4 = server.process_output(now).dgram().unwrap();
    assert_v6_path(&server_data4, false);
}

/// After a NAT rebinding whose old mapping still delivers, the server's `PATH_CHALLENGE`s on the
/// new and the old path both reach the client on one path. Each needs a `PATH_RESPONSE`.
#[test]
fn path_challenges_on_one_path_each_get_a_response() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    let dgram = send_something(&mut client, now);
    server.process_input(change_source_port(&dgram, 1), now);
    let new_probe = server.process_output(now).dgram().unwrap();
    let old_probe = server.process_output(now).dgram().unwrap();
    assert_eq!(server.stats().frame_tx.path_challenge, 2);

    // The NAT maps the new path back to the client's address.
    let new_probe = change_destination(&new_probe, DEFAULT_ADDR);
    client.process_input(new_probe, now);
    client.process_input(old_probe, now);
    assert_eq!(client.stats().frame_rx.path_challenge, 2);

    while client.process_output(now).dgram().is_some() {}
    assert_eq!(client.stats().frame_tx.path_response, 2);
}

/// Probes that no longer fit the anti-amplification limit of a new path do not stop the client
/// from sending on its primary path.
#[test]
fn unfit_probe_does_not_block_primary_path() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    // A small datagram with only a PATH_CHALLENGE, from a new server address.
    let mut frame = vec![FrameType::PathChallenge.into()];
    frame.resize(9, 0);
    let challenge = server
        .test_write_frames(Writer(frame), now)
        .dgram()
        .unwrap();
    let challenge = Datagram::new(
        new_port(DEFAULT_ADDR),
        challenge.destination(),
        challenge.tos(),
        &challenge[..],
    );
    client.process_input(challenge, now);

    // Each new byte of stream data reaches the server on the primary path.
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    for _ in 0..2 * Path::MAX_PROBES {
        client.stream_send(stream_id, &[0]).unwrap();
        let (dgrams, timer) = drain(&mut client, now);
        for d in dgrams
            .into_iter()
            .filter(|d| d.destination() == DEFAULT_ADDR)
        {
            server.process_input(d, now);
        }
        assert_eq!(
            server.stream_recv(stream_id, &mut [0; 2]).unwrap(),
            (1, false)
        );
        now += timer;
    }
}

/// A `PATH_RESPONSE` that does not fit the anti-amplification limit of a new path is dropped.
#[test]
fn unfit_path_response_is_dropped() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    // Two small datagrams with only a PATH_CHALLENGE, from a new server address.
    let mut challenge = || {
        let mut frame = vec![FrameType::PathChallenge.into()];
        frame.resize(9, 0);
        let d = server
            .test_write_frames(Writer(frame), now)
            .dgram()
            .unwrap();
        Datagram::new(new_port(DEFAULT_ADDR), d.destination(), d.tos(), &d[..])
    };
    let (first, second) = (challenge(), challenge());

    // The first challenge arrives when the new path has no budget left.
    client.process_input(first, now);
    let path = client.paths.select_path().unwrap();
    let limit = path.borrow().amplification_limit();
    path.borrow_mut().add_sent(limit);
    drain(&mut client, now);

    // The second brings budget, but only it gets a response.
    client.process_input(second, now);
    drain(&mut client, now);
    assert_eq!(client.stats().frame_tx.path_response, 1);
}

/// A delayed ACK that the anti-amplification limit holds back on an unvalidated primary path
/// leaves no overdue timer.
#[test]
fn delayed_ack_on_amplification_limited_path() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    // A full datagram from a new client port, while the server has plenty to send.
    let stream_id = server.stream_create(StreamType::UniDi).unwrap();
    server.stream_send(stream_id, &[0; 10_000]).unwrap();
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(stream_id, &[0; 2_000]).unwrap();
    let rebound = change_source_port(&client.process_output(now).dgram().unwrap(), 1);
    let (new_addr, budget) = (rebound.source(), 3 * rebound.len());
    server.process_input(rebound, now);

    // The server spends the budget of the new path before its ACK is due.
    let (dgrams, ack_delay) = drain(&mut server, now);
    let sent = dgrams.iter().filter(|d| d.destination() == new_addr);
    assert_eq!(sent.map(Datagram::len).sum::<usize>(), budget);

    // When the ACK is due, the next timer is the path probe.
    now += ack_delay;
    let pto = server
        .paths
        .primary()
        .unwrap()
        .borrow()
        .rtt()
        .pto(server.confirmed());
    assert_eq!(Some(drain(&mut server, now).1), pto.checked_sub(ack_delay));
}

/// An unvalidated primary path that the anti-amplification limit keeps from probing is abandoned.
#[test]
fn unprobed_primary_path_is_abandoned() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    // A full datagram from a new client port, while the server has plenty to send.
    let stream_id = server.stream_create(StreamType::UniDi).unwrap();
    server.stream_send(stream_id, &[0; 10_000]).unwrap();
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(stream_id, &[0; 2_000]).unwrap();
    let rebound = change_source_port(&client.process_output(now).dgram().unwrap(), 1);
    let new_addr = rebound.source();
    server.process_input(rebound, now);
    let pto = server
        .paths
        .primary()
        .unwrap()
        .borrow()
        .rtt()
        .pto(server.confirmed());

    // Nothing more arrives, so the new path never gets the budget for another probe.
    let end = now + pto * u32::try_from(2 * Path::MAX_PROBES + 1).unwrap();
    while now < end {
        match server.process_output(now) {
            Output::Datagram(_) => {}
            Output::Callback(t) => now = (now + t).min(end),
            Output::None => break,
        }
    }
    assert!(
        server
            .paths
            .primary()
            .is_none_or(|p| p.borrow().remote_address() != new_addr)
    );
}

/// Probes that the anti-amplification limit holds back do not count as an ECN blackhole.
#[test]
fn unsent_probes_are_not_an_ecn_blackhole() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    // Probe from a new local port; the first probe spends the budget of the new path.
    let local = new_port(DEFAULT_ADDR);
    client
        .migrate(Some(local), Some(DEFAULT_ADDR), false, now)
        .unwrap();
    let path = client
        .paths
        .find_path(local, DEFAULT_ADDR, &client.conn_params, now);
    assert!(path.borrow().tos().is_ecn_marked());
    assert!(client.process_output(now).dgram().is_some());

    // A small datagram arrives on the new path, which caps its budget below what was sent.
    let mut frame = vec![FrameType::PathChallenge.into()];
    frame.resize(9, 0);
    let small = server
        .test_write_frames(Writer(frame), now)
        .dgram()
        .unwrap();
    client.process_input(
        Datagram::new(DEFAULT_ADDR, local, small.tos(), &small[..]),
        now,
    );

    // Nothing more arrives on the new path.
    let end = now + AT_LEAST_PTO * u32::try_from(2 * Path::MAX_PROBES + 1).unwrap();
    while now < end {
        match client.process_output(now) {
            Output::Datagram(_) => {}
            Output::Callback(t) => now = (now + t).min(end),
            Output::None => break,
        }
    }
    assert!(path.borrow().tos().is_ecn_marked());
}

/// A path that is validated by a `PATH_RESPONSE` on another path is no longer blocked.
#[test]
fn blocked_path_validated_elsewhere_is_kept() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    // Probe from a new local port.
    let local = new_port(DEFAULT_ADDR);
    client
        .migrate(Some(local), Some(DEFAULT_ADDR), false, now)
        .unwrap();
    let probe = client.process_output(now).dgram().unwrap();
    assert_eq!(probe.source(), local);

    // A small datagram on the new path leaves no budget to respond to its PATH_CHALLENGE.
    let mut frame = vec![FrameType::PathChallenge.into()];
    frame.resize(9, 0);
    let small = server
        .test_write_frames(Writer(frame), now)
        .dgram()
        .unwrap();
    client.process_input(
        Datagram::new(DEFAULT_ADDR, local, small.tos(), &small[..]),
        now,
    );
    drain(&mut client, now);

    // The PATH_RESPONSE for the new path arrives on the old one.
    server.process_input(probe, now);
    let (dgrams, _) = drain(&mut server, now);
    let response = dgrams
        .into_iter()
        .find(|d| d.destination() == local)
        .unwrap();
    client.process_input(
        Datagram::new(DEFAULT_ADDR, DEFAULT_ADDR, response.tos(), &response[..]),
        now,
    );
    assert_eq!(
        client.paths.primary().unwrap().borrow().local_address(),
        local
    );

    let end = now + AT_LEAST_PTO * u32::try_from(Path::MAX_PROBES + 1).unwrap();
    while now < end {
        match client.process_output(now) {
            Output::Datagram(_) => {}
            Output::Callback(t) => now = (now + t).min(end),
            Output::None => break,
        }
    }
    assert_eq!(
        client.paths.primary().unwrap().borrow().local_address(),
        local
    );
}

/// The datagrams `c` sends now, and its timer after them.
fn drain(c: &mut Connection, now: Instant) -> (Vec<Datagram>, Duration) {
    let mut dgrams = Vec::new();
    loop {
        match c.process_output(now) {
            Output::Datagram(d) => dgrams.push(d),
            Output::Callback(t) => return (dgrams, t),
            Output::None => panic!("there should be a timer"),
        }
    }
}

/// Old-path packets don't contribute to congestion control for the new path (RFC 9000 §9.4).
#[test]
fn old_path_loss_is_not_a_congestion_event() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();
    let loss_events = server.stats().cc.congestion_events.loss;
    let lost = server.stats().lost;

    // Lost on the old path.
    for _ in 0..3 {
        drop(send_something(&mut server, now));
    }

    let dgram = change_source_port(&send_something(&mut client, now), 1);
    let new_addr = dgram.source();
    server.process_input(dgram, now);
    while let Some(d) = server.process_output(now).dgram() {
        // Drop old-path datagrams; the NAT maps new-path ones to the client.
        if d.destination() == new_addr {
            client.process_input(change_destination(&d, DEFAULT_ADDR), now);
        }
    }

    // The ACK for the packet carrying the new path's `PATH_CHALLENGE` declares the first old-path
    // packet lost.
    while let Some(d) = client.process_output(now).dgram() {
        server.process_input(change_source_port(&d, 1), now);
    }
    assert_eq!(server.stats().lost, lost + 1);
    assert_eq!(server.stats().cc.congestion_events.loss, loss_events);
}

#[test]
fn migrate_immediate() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), true, now)
        .unwrap();
    assert!(client.events().any(|e| matches!(
        e,
        ConnectionEvent::PathMigrated { local, remote }
        if local == DEFAULT_ADDR_V4 && remote == DEFAULT_ADDR_V4
    )));

    let client1 = send_something(&mut client, now);
    assert_v4_path(&client1, true); // Contains PATH_CHALLENGE.
    assert_path_challenge_min_len(&client, &client1, now);

    let client2 = send_something(&mut client, now);
    assert_v4_path(&client2, false); // Doesn't.

    let server_delayed = send_something(&mut server, now);

    // The server accepts the first packet and migrates (but probes).
    let server1 = server.process(Some(client1), now).dgram().unwrap();
    assert_v4_path(&server1, true);
    assert!(
        server
            .events()
            .any(|e| matches!(e, ConnectionEvent::PathMigrated { .. }))
    );
    let server2 = server.process_output(now).dgram().unwrap();
    assert_v6_path(&server2, true);

    // The second packet has no real effect, it just elicits an ACK.
    let all_before = server.stats().frame_tx.all();
    let ack_before = server.stats().frame_tx.ack;
    let server3 = server.process(Some(client2), now).dgram();
    assert!(server3.is_some());
    assert_eq!(server.stats().frame_tx.all(), all_before + 1);
    assert_eq!(server.stats().frame_tx.ack, ack_before + 1);

    // Receiving a packet sent by the server before migration doesn't change path.
    client.process_input(server_delayed, now);
    // The client has sent two unpaced packets and this new path has no RTT estimate
    // so this might be paced.
    let (client3, _t) = send_something_paced(&mut client, now, true);
    assert_v4_path(&client3, false);
}

/// RTT estimates for paths should be preserved across migrations.
#[test]
fn migrate_rtt() {
    const RTT: Duration = Duration::from_millis(20);
    let mut client = default_client();
    let mut server = default_server();
    let now = connect_rtt_idle(&mut client, &mut server, RTT);

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), true, now)
        .unwrap();
    // The RTT might be increased for the new path, so allow a little flexibility.
    let rtt = client.paths.rtt();
    assert!(rtt > RTT);
    assert!(rtt < RTT * 2);
}

#[test]
fn migrate_immediate_fail() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), true, now)
        .unwrap();

    let probe = client.process_output(now).dgram().unwrap();
    assert_v4_path(&probe, true); // Contains PATH_CHALLENGE.
    assert_path_challenge_min_len(&client, &probe, now);

    // -1 because first PATH_CHALLENGE already sent above
    for _ in 0..Path::MAX_PROBES * 2 - 1 {
        let cb = client.process_output(now).callback();
        assert_ne!(cb, Duration::new(0, 0));
        now += cb;

        let before = client.stats().frame_tx;
        let probe = client.process_output(now).dgram().unwrap();
        assert_v4_path(&probe, true); // Contains PATH_CHALLENGE.
        assert_path_challenge_min_len(&client, &probe, now);
        let after = client.stats().frame_tx;
        assert_eq!(after.path_challenge, before.path_challenge + 1);
        assert_eq!(after.padding, before.padding + 1);
        assert_eq!(after.all(), before.all() + 2);

        // This might be a PTO, which will result in sending a probe.
        if let Some(probe) = client.process_output(now).dgram() {
            assert_v4_path(&probe, false); // Contains PING.
            let after = client.stats().frame_tx;
            assert_eq!(after.path_challenge, before.path_challenge + 1);
            assert_eq!(after.ping, before.ping + 1);
            assert_eq!(after.all(), before.all() + 3);
        }
    }

    let pto = client.process_output(now).callback();
    assert_ne!(pto, Duration::new(0, 0));
    now += pto;

    // The client should fall back to the original path and retire the connection ID.
    let fallback = client.process_output(now).dgram();
    assert_v6_path(&fallback.unwrap(), false);
    assert_eq!(client.stats().frame_tx.retire_connection_id, 1);
}

/// Migrating to the same path shouldn't do anything special,
/// except that the path is probed.
#[test]
fn migrate_same() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    client
        .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), true, now)
        .unwrap();

    let probe = client.process_output(now).dgram().unwrap();
    assert_v6_path(&probe, true); // Contains PATH_CHALLENGE.
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&client, &probe, now);

    let resp = server.process(Some(probe), now).dgram().unwrap();
    assert_v6_path(&resp, true);
    assert_eq!(server.stats().frame_tx.path_response, 1);
    assert_eq!(server.stats().frame_tx.path_challenge, 0);

    // Everything continues happily.
    client.process_input(resp, now);
    let contd = send_something(&mut client, now);
    assert_v6_path(&contd, false);
}

/// Migrating to the same path, if it fails, causes the connection to fail.
#[test]
fn migrate_same_fail() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    client
        .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), true, now)
        .unwrap();

    let probe = client.process_output(now).dgram().unwrap();
    assert_v6_path(&probe, true); // Contains PATH_CHALLENGE.
    assert_path_challenge_min_len(&client, &probe, now);

    // -1 because first PATH_CHALLENGE already sent above
    for _ in 0..Path::MAX_PROBES * 2 - 1 {
        let cb = client.process_output(now).callback();
        assert_ne!(cb, Duration::new(0, 0));
        now += cb;

        let before = client.stats().frame_tx;
        let probe = client.process_output(now).dgram().unwrap();
        assert_v6_path(&probe, true); // Contains PATH_CHALLENGE.
        assert_path_challenge_min_len(&client, &probe, now);
        let after = client.stats().frame_tx;
        assert_eq!(after.path_challenge, before.path_challenge + 1);
        assert_eq!(after.padding, before.padding + 1);
        assert_eq!(after.all(), before.all() + 2);

        // This might be a PTO, which will result in sending a probe.
        if let Some(probe) = client.process_output(now).dgram() {
            assert_v6_path(&probe, false); // Contains PING.
            let after = client.stats().frame_tx;
            assert_eq!(after.path_challenge, before.path_challenge + 1);
            assert_eq!(after.ping, before.ping + 1);
            assert_eq!(after.all(), before.all() + 3);
        }
    }

    let pto = client.process_output(now).callback();
    assert_ne!(pto, Duration::new(0, 0));
    now += pto;

    // The client should mark this path as failed and close immediately.
    let res = client.process_output(now);
    assert!(matches!(res, Output::None));
    assert!(matches!(
        client.state(),
        State::Closed(CloseReason::Transport(Error::NoAvailablePath))
    ));
}

/// This gets the connection ID from a datagram using the default
/// connection ID generator/decoder.
pub fn get_cid(d: &Datagram) -> ConnectionIdRef<'_> {
    let r#gen = CountingConnectionIdGenerator::default();
    assert_eq!(d[0] & 0x80, 0); // Only support short packets for now.
    r#gen.decode_cid(&mut Decoder::from(&d[1..])).unwrap()
}

fn migration(mut client: Connection) {
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), false, now)
        .unwrap();

    let probe = client.process_output(now).dgram().unwrap();
    assert_v4_path(&probe, true); // Contains PATH_CHALLENGE.
    assert_path_challenge_min_len(&client, &probe, now);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    let probe_cid = ConnectionId::from(get_cid(&probe));

    let resp = server.process(Some(probe), now).dgram().unwrap();
    assert_v4_path(&resp, true);
    assert_path_challenge_min_len(&server, &resp, now);
    assert_eq!(server.stats().frame_tx.path_response, 1);
    assert_eq!(server.stats().frame_tx.path_challenge, 1);

    // Data continues to be exchanged on the new path.
    let client_data = send_something(&mut client, now);
    assert_ne!(get_cid(&client_data), probe_cid);
    assert_v6_path(&client_data, false);
    server.process_input(client_data, now);
    let server_data = send_something(&mut server, now);
    assert_v6_path(&server_data, false);

    // Once the client receives the probe response, it migrates to the new path.
    client.process_input(resp, now);
    assert!(client.events().any(|e| matches!(
        e,
        ConnectionEvent::PathMigrated { local, remote }
        if local == DEFAULT_ADDR_V4 && remote == DEFAULT_ADDR_V4
    )));
    assert_eq!(client.stats().frame_rx.path_challenge, 1);
    let migrate_client = send_something(&mut client, now);
    assert_v4_path(&migrate_client, true); // Responds to server probe.

    // The server now sees the migration and will switch over.
    // However, it will probe the old path again, even though it has just
    // received a response to its last probe, because it needs to verify
    // that the migration is genuine.
    server.process_input(migrate_client, now);
    let stream_before = server.stats().frame_tx.stream;
    let probe_old_server = send_something(&mut server, now);
    // This is just the double-check probe; no STREAM frames.
    assert_v6_path(&probe_old_server, true);
    assert_path_challenge_min_len(&server, &probe_old_server, now);
    assert_eq!(server.stats().frame_tx.path_challenge, 2);
    assert_eq!(server.stats().frame_tx.stream, stream_before);

    // The server then sends data on the new path.
    let migrate_server = server.process_output(now).dgram().unwrap();
    assert_v4_path(&migrate_server, false);
    assert_eq!(server.stats().frame_tx.path_challenge, 2);
    assert_eq!(server.stats().frame_tx.stream, stream_before + 1);

    // The client receives these checks and responds to the probe, but uses the new path.
    client.process_input(migrate_server, now);
    client.process_input(probe_old_server, now);
    let old_probe_resp = send_something(&mut client, now);
    assert_v6_path(&old_probe_resp, true);
    let client_confirmation = client.process_output(now).dgram().unwrap();
    assert_v4_path(&client_confirmation, false);

    // The server has now sent 2 packets, so it is blocked on the pacer.  Wait.
    let server_pacing = server.process_output(now).callback();
    assert_ne!(server_pacing, Duration::new(0, 0));
    // ... then confirm that the server sends on the new path still.
    let server_confirmation = send_something(&mut server, now + server_pacing);
    assert_v4_path(&server_confirmation, false);
}

#[test]
fn migration_graceful() {
    migration(default_client());
}

/// A client should be able to migrate when it has a zero-length connection ID.
#[test]
fn migration_client_empty_cid() {
    fixture_init();
    let client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    migration(client);
}

/// Drive the handshake in the most expeditious fashion.
/// Returns the packet containing `HANDSHAKE_DONE` from the server.
fn fast_handshake(client: &mut Connection, server: &mut Connection) -> Option<Datagram> {
    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    client.process_input(dgram.unwrap(), now());
    assert!(maybe_authenticate(client));
    let dgram = client.process_output(now()).dgram();
    server.process(dgram, now()).dgram()
}

fn preferred_address(hs_client: SocketAddr, hs_server: SocketAddr, preferred: SocketAddr) {
    let mtu = Pmtud::default_plpmtu(hs_client.ip());
    let assert_orig_path = |d: &Datagram, full_mtu: bool| {
        assert_eq!(
            d.destination(),
            if d.source() == hs_client {
                hs_server
            } else if d.source() == hs_server {
                hs_client
            } else {
                panic!();
            }
        );
        if full_mtu {
            assert_eq!(d.len(), mtu);
        }
    };
    let assert_toward_spa = |d: &Datagram, full_mtu: bool| {
        assert_eq!(d.destination(), preferred);
        assert_eq!(d.source(), hs_client);
        if full_mtu {
            assert_eq!(d.len(), mtu);
        }
    };
    let assert_from_spa = |d: &Datagram, full_mtu: bool| {
        assert_eq!(d.destination(), hs_client);
        assert_eq!(d.source(), preferred);
        if full_mtu {
            assert_eq!(d.len(), mtu);
        }
    };

    fixture_init();
    let (log, _contents) = new_neqo_qlog();
    let mut client = zero_len_cid_client(hs_client, hs_server);
    client.set_qlog(log);
    let spa = match preferred {
        SocketAddr::V6(v6) => PreferredAddress::new(None, Some(v6)),
        SocketAddr::V4(v4) => PreferredAddress::new(Some(v4), None),
    };
    let mut server = new_server(ConnectionParameters::default().preferred_address(spa));

    let dgram = fast_handshake(&mut client, &mut server);

    // The client is about to process HANDSHAKE_DONE.
    // It should start probing toward the server's preferred address.
    let probe = client.process(dgram, now()).dgram().unwrap();
    assert_toward_spa(&probe, true);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&client, &probe, now());
    assert_ne!(client.process_output(now()).callback(), Duration::new(0, 0));

    // Data continues on the main path for the client.
    let data = send_something(&mut client, now());
    assert_orig_path(&data, false);

    // The server responds to the probe.
    let resp = server.process(Some(probe), now()).dgram().unwrap();
    assert_from_spa(&resp, true);
    assert_eq!(server.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&server, &resp, now());
    assert_eq!(server.stats().frame_tx.path_response, 1);

    // Data continues on the main path for the server.
    server.process_input(data, now());
    let data = send_something(&mut server, now());
    assert_orig_path(&data, false);

    // Client gets the probe response back and it migrates.
    client.process_input(resp, now());
    client.process_input(data, now());
    let data = send_something(&mut client, now());
    assert_toward_spa(&data, true);
    assert_eq!(client.stats().frame_tx.stream, 2);
    assert_eq!(client.stats().frame_tx.path_response, 1);

    // The server sees the migration and probes the old path.
    let probe = server.process(Some(data), now()).dgram().unwrap();
    assert_orig_path(&probe, true);
    assert_eq!(server.stats().frame_tx.path_challenge, 2);
    assert_path_challenge_min_len(&server, &probe, now());

    // But data now goes on the new path.
    let data = send_something(&mut server, now());
    assert_from_spa(&data, false);
}

/// Migration works for a new port number.
#[test]
fn preferred_address_new_port() {
    let a = DEFAULT_ADDR;
    preferred_address(a, a, new_port(a));
}

/// Migration works for a new address too.
#[test]
fn preferred_address_new_address() {
    let mut preferred = DEFAULT_ADDR;
    preferred.set_ip(IpAddr::V6(Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 2)));
    preferred_address(DEFAULT_ADDR, DEFAULT_ADDR, preferred);
}

/// Migration works for IPv4 addresses.
#[test]
fn preferred_address_new_port_v4() {
    let a = DEFAULT_ADDR_V4;
    preferred_address(a, a, new_port(a));
}

/// Migrating to a loopback address is OK if we started there.
#[test]
fn preferred_address_loopback() {
    let a = loopback();
    preferred_address(a, a, new_port(a));
}

/// Returns a client, the server's preferred address and the packet containing `HANDSHAKE_DONE`.
fn preferred_address_handshake() -> (Connection, SocketAddr, Option<Datagram>) {
    let preferred = new_port(DEFAULT_ADDR);
    let mut client = default_client();
    let spa = PreferredAddress::new_any(None, Some(preferred));
    let mut server = new_server(ConnectionParameters::default().preferred_address(spa));

    let dgram = fast_handshake(&mut client, &mut server);
    (client, preferred, dgram)
}

/// Only the path probe goes to the preferred address.
#[test]
fn probed_path_sends_only_probe() {
    let (mut client, preferred, dgram) = preferred_address_handshake();
    client.process_input(dgram.unwrap(), now());
    let probe = client
        .process_multiple_output(now(), 2.try_into().unwrap())
        .dgram()
        .unwrap();
    assert_eq!(probe.destination(), preferred);
    assert_eq!(probe.num_datagrams(), 1);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
}

/// A path probe doesn't use up the primary path's PTO packet.
#[test]
fn path_probe_keeps_pto_packet() {
    let (mut client, preferred, dgram) = preferred_address_handshake();
    let stream = client.stream_create(StreamType::UniDi).unwrap();
    let (_, now) = fill_cwnd(&mut client, stream, now());
    client.process_input(dgram.unwrap(), now);

    let now = now + AT_LEAST_PTO;
    let probe = client
        .process_multiple_output(now, 2.try_into().unwrap())
        .dgram()
        .unwrap();
    assert_eq!(probe.destination(), preferred);
    let stream_before = client.stats().frame_tx.stream;
    while let Some(dgram) = client.process_output(now).dgram() {
        assert_eq!(dgram.destination(), DEFAULT_ADDR);
    }
    let pto_stream = client.stats().frame_tx.stream - stream_before;
    assert_eq!(pto_stream, MAX_PTO_PACKET_COUNT - 1);
}

fn expect_no_migration(client: &mut Connection, server: &mut Connection) {
    let dgram = fast_handshake(client, server);

    // The client won't probe now, though it could; it remains idle.
    let out = client.process(dgram, now());
    assert_ne!(out.callback(), Duration::new(0, 0));

    // Data continues on the main path for the client.
    let data = send_something(client, now());
    assert_v6_path(&data, false);
    assert_eq!(client.stats().frame_tx.path_challenge, 0);
}

fn preferred_address_ignored(spa: PreferredAddress) {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().preferred_address(spa));

    expect_no_migration(&mut client, &mut server);
}

/// Using a loopback address in the preferred address is ignored.
#[test]
fn preferred_address_ignore_loopback() {
    preferred_address_ignored(PreferredAddress::new_any(None, Some(loopback())));
}

/// Using an IPv4-mapped loopback address in the preferred address is ignored.
#[test]
fn preferred_address_ignore_mapped_loopback() {
    let spa = PreferredAddress::new_any(None, Some(mapped(Ipv4Addr::LOCALHOST)));
    preferred_address_ignored(spa);
}

/// An IPv4-mapped loopback preferred address is ignored with an unspecified local address.
#[test]
fn preferred_address_ignore_mapped_loopback_unspecified_local() {
    let local = SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), DEFAULT_ADDR.port());
    let mut client = zero_len_cid_client(local, DEFAULT_ADDR);
    let spa = PreferredAddress::new_any(None, Some(mapped(Ipv4Addr::LOCALHOST)));
    let mut server = new_server(ConnectionParameters::default().preferred_address(spa));

    let dgram = fast_handshake(&mut client, &mut server);
    _ = client.process(dgram, now());
    assert_eq!(client.stats().frame_tx.path_challenge, 0);
}

/// A preferred address in the wrong address family is ignored.
#[test]
fn preferred_address_ignore_different_family() {
    preferred_address_ignored(PreferredAddress::new_any(Some(DEFAULT_ADDR_V4), None));
}

/// Disabling preferred addresses at the client means that it ignores a perfectly
/// good preferred address.
#[test]
fn preferred_address_disabled_client() {
    let mut client = new_client(ConnectionParameters::default().disable_preferred_address());
    let mut preferred = DEFAULT_ADDR;
    preferred.set_ip(IpAddr::V6(Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 2)));
    let spa = PreferredAddress::new_any(None, Some(preferred));
    let mut server = new_server(ConnectionParameters::default().preferred_address(spa));

    expect_no_migration(&mut client, &mut server);
}

struct HandshakeDoneWriter {}

impl test_internal::FrameWriter for HandshakeDoneWriter {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        builder.encode_varint(FrameType::HandshakeDone);
    }
}

/// Has the client probe the preferred address, then receive a second `HANDSHAKE_DONE`.
/// Returns the client, the server and the probe.
fn repeated_handshake_done() -> (Connection, Connection, Datagram) {
    let mut client = default_client();
    let spa = PreferredAddress::new_any(None, Some(new_port(DEFAULT_ADDR)));
    let mut server = new_server(ConnectionParameters::default().preferred_address(spa));
    let dgram = fast_handshake(&mut client, &mut server);
    let probe = client.process(dgram, now()).dgram().unwrap();
    let done = send_with_extra(&mut server, HandshakeDoneWriter {}, now());
    client.process_input(done, now());
    assert_eq!(*client.state(), State::Confirmed);
    assert_eq!(client.stats().frame_rx.handshake_done, 2);
    (client, server, probe)
}

/// A repeated `HANDSHAKE_DONE` does not restart probing of the preferred address.
#[test]
fn preferred_address_repeated_handshake_done_no_probe() {
    let (mut client, _server, _probe) = repeated_handshake_done();
    while client.process_output(now()).dgram().is_some() {}
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
}

/// After a repeated `HANDSHAKE_DONE`, the client migrates to the preferred address and accepts
/// the connection ID that replaces the one it retires.
#[test]
fn preferred_address_repeated_handshake_done_migrate() {
    let (mut client, mut server, probe) = repeated_handshake_done();
    server.process_input(probe, now());

    // Run until the client retires a connection ID and neither peer has anything to send.
    let mut now = now();
    loop {
        let c = match client.process_output(now) {
            Output::Datagram(d) => {
                server.process_input(d, now);
                continue;
            }
            c => c.callback(),
        };
        match server.process_output(now) {
            Output::Datagram(d) => client.process_input(d, now),
            s if client.stats().frame_tx.retire_connection_id == 0 => now += min(c, s.callback()),
            _ => break,
        }
    }
    assert_eq!(*client.state(), State::Confirmed);
    assert_eq!(client.stats().frame_tx.retire_connection_id, 1);
    let new_cids = server.stats().frame_tx.new_connection_id;
    assert_eq!(client.stats().frame_rx.new_connection_id, new_cids);
}

#[test]
fn preferred_address_empty_cid() {
    fixture_init();

    let spa = PreferredAddress::new_any(None, Some(new_port(DEFAULT_ADDR)));
    let res = Connection::new_server(
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::new(RefCell::new(EmptyConnectionIdGenerator::default())),
        ConnectionParameters::default().preferred_address(spa),
    );
    assert_eq!(res.unwrap_err(), Error::ConnectionIdsExhausted);
}

/// A server cannot include a preferred address if it chooses an empty connection ID.
#[test]
fn preferred_address_server_empty_cid() {
    let mut client = default_client();
    let mut server = zero_len_cid_server();

    server
        .set_local_tparam(
            TransportParameterId::PreferredAddress,
            TransportParameter::Bytes(SAMPLE_PREFERRED_ADDRESS.to_vec()),
        )
        .unwrap();

    connect_fail(
        &mut client,
        &mut server,
        Error::TransportParameter,
        Error::Peer(Error::TransportParameter.code()),
    );
}

/// A client shouldn't send a preferred address transport parameter.
#[test]
fn preferred_address_client() {
    let mut client = default_client();
    let mut server = default_server();

    client
        .set_local_tparam(
            TransportParameterId::PreferredAddress,
            TransportParameter::Bytes(SAMPLE_PREFERRED_ADDRESS.to_vec()),
        )
        .unwrap();

    connect_fail(
        &mut client,
        &mut server,
        Error::Peer(Error::TransportParameter.code()),
        Error::TransportParameter,
    );
}

/// Test that migration isn't permitted if the connection isn't in the right state.
#[test]
fn migration_invalid_state() {
    let mut client = default_client();
    assert!(
        client
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), false, now())
            .is_err()
    );

    let mut server = default_server();
    assert!(
        server
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), false, now())
            .is_err()
    );
    connect_force_idle(&mut client, &mut server);

    assert!(
        server
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), false, now())
            .is_err()
    );

    client.close(now(), 0, "closing");
    assert!(
        client
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), false, now())
            .is_err()
    );
    let close = client.process_output(now()).dgram();

    let dgram = server.process(close, now()).dgram();
    assert!(
        server
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), false, now())
            .is_err()
    );

    client.process_input(dgram.unwrap(), now());
    assert!(
        client
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), false, now())
            .is_err()
    );
}

#[test]
fn migration_disabled() {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().disable_migration(true));
    connect(&mut client, &mut server);
    assert_eq!(
        client
            .migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR), true, now())
            .unwrap_err(),
        Error::InvalidMigration
    );
}

#[test]
fn migration_invalid_address() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let mut cant_migrate = |local, remote| {
        assert_eq!(
            client.migrate(local, remote, true, now()).unwrap_err(),
            Error::InvalidMigration
        );
    };

    // Providing neither address is pointless and therefore an error.
    cant_migrate(None, None);

    // Providing a zero port number isn't valid.
    let mut zero_port = DEFAULT_ADDR;
    zero_port.set_port(0);
    cant_migrate(None, Some(zero_port));
    cant_migrate(Some(zero_port), None);

    // An unspecified remote address is bad.
    let mut remote_unspecified = DEFAULT_ADDR;
    remote_unspecified.set_ip(IpAddr::V6(Ipv6Addr::from(0)));
    cant_migrate(None, Some(remote_unspecified));
    cant_migrate(None, Some(mapped(Ipv4Addr::UNSPECIFIED)));

    // Mixed address families is bad.
    cant_migrate(Some(DEFAULT_ADDR), Some(DEFAULT_ADDR_V4));
    cant_migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR));

    // Loopback to non-loopback is bad.
    cant_migrate(Some(DEFAULT_ADDR), Some(loopback()));
    cant_migrate(Some(loopback()), Some(DEFAULT_ADDR));
    cant_migrate(Some(DEFAULT_ADDR), Some(mapped(Ipv4Addr::LOCALHOST)));
    cant_migrate(Some(mapped(Ipv4Addr::LOCALHOST)), Some(DEFAULT_ADDR));
    assert_eq!(
        client
            .migrate(Some(DEFAULT_ADDR), Some(loopback()), true, now())
            .unwrap_err(),
        Error::InvalidMigration
    );
    assert_eq!(
        client
            .migrate(Some(loopback()), Some(DEFAULT_ADDR), true, now())
            .unwrap_err(),
        Error::InvalidMigration
    );
}

/// Writes `count` `NEW_CONNECTION_ID` frames with consecutive sequence numbers,
/// each retiring every previously issued connection ID.
pub struct NewConnectionIds {
    count: u64,
    cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>>,
}

impl NewConnectionIds {
    // Use a sequence number that is large enough that all existing values
    // will be lower (so they get retired).
    const SEQNO: u64 = 100;

    fn new(count: u64, cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>>) -> Self {
        Self { count, cid_gen }
    }

    /// A single `NEW_CONNECTION_ID` frame that retires every previously issued
    /// connection ID.
    pub fn retire_all(cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>>) -> Self {
        Self::new(1, cid_gen)
    }
}

impl test_internal::FrameWriter for NewConnectionIds {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        if builder.packet_empty() {
            return; // E.g., the Initial ahead of 0-RTT, which can't carry NEW_CONNECTION_ID.
        }
        for i in 0..self.count {
            let seqno = Self::SEQNO + i;
            let cid = self.cid_gen.borrow_mut().generate_cid().unwrap();
            let mut srt = [0; 16];
            srt[..8].copy_from_slice(&seqno.to_be_bytes());
            builder
                .encode_varint(FrameType::NewConnectionId)
                .encode_varint(seqno)
                .encode_varint(seqno) // Retire Prior To
                .encode_vec(1, &cid)
                .encode(srt);
        }
    }
}

/// Before sending with Handshake keys again, a server answers `PATH_CHALLENGE`
/// on a new path, but has nothing to send there with Handshake keys.
#[test]
fn path_challenge_before_handshake_keys_discarded() {
    let mut client = default_client();
    let mut server = default_server();
    while let Some(dgram) = client.process_output(now()).dgram() {
        server.process_input(dgram, now());
    }
    while let Some(dgram) = server.process_output(now()).dgram() {
        client.process_input(dgram, now());
    }
    assert!(maybe_authenticate(&mut client));
    let finished = client.process_output(now()).dgram().unwrap();
    let challenge = client
        .test_write_frames(ZeroRttPathChallenge { initial: true }, now())
        .dgram()
        .unwrap();
    let challenge = change_source_address_and_port(&challenge, 1);

    server.process_input(finished, now());
    assert_eq!(*server.state(), State::Confirmed);
    server.process_input(challenge.clone(), now());
    assert_eq!(server.stats().frame_rx.path_challenge, 1);

    let response = server.process_output(now()).dgram().unwrap();
    assert_eq!(response.destination(), challenge.source());
    assert_eq!(server.stats().frame_tx.path_response, 1);
}

/// A client drops a datagram from a new address before its handshake completes.
#[test]
fn client_discards_initial_from_new_address() {
    let mut client = default_client();
    let mut server = default_server();
    let client_hs = client.process_output(now()).dgram();
    let client_hs2 = client.process_output(now()).dgram();
    server.process_input(client_hs.unwrap(), now());
    let server_hs = server.process(client_hs2, now()).dgram().unwrap();
    let before = client.stats();
    client.process_input(change_source_port(&server_hs, 1), now());
    assert_eq!(*client.state(), State::WaitInitial);
    assert_eq!(client.stats().packets_rx, before.packets_rx);
    assert_eq!(client.stats().dropped_rx, before.dropped_rx + 1);

    client.process_input(server_hs, now());
    handshake(&mut client, &mut server, now(), DEFAULT_RTT);
    assert_eq!(*client.state(), State::Confirmed);
}

/// Test that forcing retirement of connection IDs forces retirement of all active
/// connection IDs and the use of of newer one.
#[test]
fn retire_all() {
    let mut client = default_client();
    let cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>> =
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    let mut server = Connection::new_server(
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::clone(&cid_gen),
        ConnectionParameters::default(),
    )
    .unwrap();
    connect_force_idle(&mut client, &mut server);

    let original_cid = ConnectionId::from(get_cid(&send_something(&mut client, now())));

    let new_cid_before = client.stats().frame_rx.new_connection_id;
    let retire_cid_before = client.stats().frame_tx.retire_connection_id;
    let ncid = send_with_extra(&mut server, NewConnectionIds::retire_all(cid_gen), now());
    client.process_input(ncid, now());
    let retire = send_something(&mut client, now());
    assert_eq!(
        client.stats().frame_rx.new_connection_id,
        new_cid_before + 1
    );
    assert_eq!(
        client.stats().frame_tx.retire_connection_id,
        retire_cid_before + ConnectionIdManager::ACTIVE_LIMIT
    );

    assert_ne!(get_cid(&retire), original_cid);
}

/// Sends one `NEW_CONNECTION_ID` frame from `sender`.
fn send_new_connection_id(sender: &mut Connection) -> Datagram {
    let cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>> =
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    send_with_extra(sender, NewConnectionIds::new(1, cid_gen), now())
}

/// Delivers `dgram` from `sender` to `receiver` and checks that both close with
/// `PROTOCOL_VIOLATION`.
fn assert_protocol_violation(sender: &mut Connection, receiver: &mut Connection, dgram: Datagram) {
    let close = receiver.process(Some(dgram), now()).dgram();
    assert_error(receiver, &CloseReason::Transport(Error::ProtocolViolation));
    sender.process_input(close.unwrap(), now());
    let peer_error = Error::Peer(Error::ProtocolViolation.code());
    assert_error(sender, &CloseReason::Transport(peer_error));
}

/// RFC 9000, Section 19.15: an endpoint that is sending packets with a zero-length
/// Destination Connection ID treats receipt of a `NEW_CONNECTION_ID` frame as a
/// connection error of type `PROTOCOL_VIOLATION`.
#[test]
fn new_connection_id_to_zero_len_cid_client() {
    let mut client = default_client();
    let mut server = zero_len_cid_server();
    connect_force_idle(&mut client, &mut server);
    let ncid = send_new_connection_id(&mut server);
    assert_protocol_violation(&mut server, &mut client, ncid);
}

#[test]
fn new_connection_id_to_zero_len_cid_server() {
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let ncid = send_new_connection_id(&mut client);
    assert_protocol_violation(&mut client, &mut server, ncid);
}

/// An endpoint that provides a zero-length connection ID still sends a non-empty
/// Destination Connection ID to a peer that does not, so it accepts `NEW_CONNECTION_ID`.
#[test]
fn new_connection_id_from_non_zero_len_cid_server() {
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let before = client.stats().frame_rx.new_connection_id;
    let ncid = send_new_connection_id(&mut server);
    client.process_input(ncid, now());
    assert_eq!(client.stats().frame_rx.new_connection_id, before + 1);
    assert_eq!(*client.state(), State::Confirmed);
}

/// RFC 9000, Section 5.1.2: an endpoint SHOULD bound the number of connection IDs it
/// has retired but not yet had acknowledged, and MAY treat exceeding that bound as a
/// `CONNECTION_ID_LIMIT_ERROR`.  A peer that streams `NEW_CONNECTION_ID` frames with an
/// ever-increasing Retire Prior To value, while the victim cannot flush its
/// `RETIRE_CONNECTION_ID` frames, must not be able to grow that queue without bound.
#[test]
fn retire_cid_queue_bounded() {
    let mut client = default_client();
    let cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>> =
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    let mut server = Connection::new_server(
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::clone(&cid_gen),
        ConnectionParameters::default(),
    )
    .unwrap();
    connect_force_idle(&mut client, &mut server);

    // The client never runs `process_output` here, so it cannot send (or get
    // acknowledgement for) any `RETIRE_CONNECTION_ID` frames: its pending-retirement
    // queue only grows.  Because every frame retires all prior connection IDs, the
    // store stays at one active connection ID and the existing
    // active-connection-ID-limit check never fires; only a dedicated bound on the
    // retirement queue can stop this.
    let count = to_u64(ConnectionIdManager::MAX_RETIRE_QUEUE) + 1;
    let ncids = send_with_extra(&mut server, NewConnectionIds::new(count, cid_gen), now());
    client.process_input(ncids, now());

    assert!(matches!(
        client.state(),
        State::Closing {
            error: CloseReason::Transport(Error::ConnectionIdLimitExceeded),
            ..
        }
    ));
}

/// Writes `count` `NEW_CONNECTION_ID` frames with consecutive sequence numbers
/// starting at `first`, retiring none.
struct AddConnectionIds {
    first: u64,
    count: u64,
}

impl test_internal::FrameWriter for AddConnectionIds {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        for seqno in self.first..self.first + self.count {
            let cid = seqno.to_be_bytes(); // Unlike `CountingConnectionIdGenerator` CIDs, starts with 0.
            let mut srt = [0; 16];
            srt[..8].copy_from_slice(&seqno.to_be_bytes());
            builder
                .encode_varint(FrameType::NewConnectionId)
                .encode_varint(seqno)
                .encode_varint(0u64) // Retire Prior To
                .encode_vec(1, &cid)
                .encode(srt);
        }
    }
}

/// RFC 9000, Section 5.1.1: exceeding `active_connection_id_limit` is a
/// `CONNECTION_ID_LIMIT_ERROR`.
#[test]
fn active_cid_limit_exceeded() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    // The connection ID in use is not in `cids`, so this reaches the limit exactly.
    let first = NewConnectionIds::SEQNO;
    let count = to_u64(ConnectionIdManager::ACTIVE_LIMIT - 1 - client.cids.len());
    let ncids = send_with_extra(&mut server, AddConnectionIds { first, count }, now());
    client.process_input(ncids, now());
    assert_eq!(*client.state(), State::Confirmed);

    let one_more = AddConnectionIds {
        first: first + count,
        count: 1,
    };
    let ncid = send_with_extra(&mut server, one_more, now());
    client.process_input(ncid, now());
    let err = CloseReason::Transport(Error::ConnectionIdLimitExceeded);
    assert_error(&client, &err);
    let close = client.process_output(now()).dgram();
    server.process_input(close.unwrap(), now());
    assert_error(&server, &CloseReason::Transport(Error::Peer(9)));
}

/// Injects a `RETIRE_CONNECTION_ID` frame for a given sequence number.
struct RetireConnectionId(u64);

impl test_internal::FrameWriter for RetireConnectionId {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        builder
            .encode_varint(FrameType::RetireConnectionId)
            .encode_varint(self.0);
    }
}

/// RFC 9000, Section 19.16: a `RETIRE_CONNECTION_ID` frame carrying a sequence number
/// greater than any connection ID the receiver has issued is a connection error.
#[test]
fn retire_unissued_connection_id() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    // The largest sequence number the client could have issued is far below this,
    // so it has never been sent to the server.
    let retire = send_with_extra(&mut server, RetireConnectionId(1000), now());
    client.process_input(retire, now());
    assert!(matches!(
        client.state(),
        State::Closing {
            error: CloseReason::Transport(Error::ProtocolViolation),
            ..
        }
    ));
}

/// RFC 9000, Section 19.16: an endpoint that provides a zero-length connection ID
/// treats receipt of a `RETIRE_CONNECTION_ID` frame as a connection error.
#[test]
fn retire_zero_len_connection_id() {
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let retire = send_with_extra(&mut server, RetireConnectionId(0), now()); // Sequence number of the zero-length ID.
    assert_protocol_violation(&mut server, &mut client, retire);
}

/// As `retire_zero_len_connection_id`, for a server that uses zero-length connection IDs.
#[test]
fn retire_zero_len_connection_id_server() {
    let mut client = default_client();
    let mut server = zero_len_cid_server();
    connect_force_idle(&mut client, &mut server);
    let retire = send_with_extra(&mut client, RetireConnectionId(0), now()); // Sequence number of the zero-length ID.
    assert_protocol_violation(&mut client, &mut server, retire);
}

/// During a graceful migration, if the probed path can't get a new connection ID due
/// to being forced to retire the one it is using, the migration will fail.
#[test]
fn retire_prior_to_migration_failure() {
    let mut client = default_client();
    let cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>> =
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    let mut server = Connection::new_server(
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::clone(&cid_gen),
        ConnectionParameters::default(),
    )
    .unwrap();
    connect_force_idle(&mut client, &mut server);

    let original_cid = ConnectionId::from(get_cid(&send_something(&mut client, now())));

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), false, now())
        .unwrap();

    // The client now probes the new path.
    let probe = client.process_output(now()).dgram().unwrap();
    assert_v4_path(&probe, true);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&client, &probe, now());
    let probe_cid = ConnectionId::from(get_cid(&probe));
    assert_ne!(original_cid, probe_cid);

    // Have the server receive the probe, but separately have it decide to
    // retire all of the available connection IDs.
    let retire_all = send_with_extra(&mut server, NewConnectionIds::retire_all(cid_gen), now());

    let resp = server.process(Some(probe), now()).dgram().unwrap();
    assert_v4_path(&resp, true);
    assert_eq!(server.stats().frame_tx.path_response, 1);
    assert_eq!(server.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&server, &resp, now());

    // Have the client receive the NEW_CONNECTION_ID with Retire Prior To.
    client.process_input(retire_all, now());
    // This packet contains the probe response, which should be fine, but it
    // also includes PATH_CHALLENGE for the new path, and the client can't
    // respond without a connection ID.  We treat this as a connection error.
    client.process_input(resp, now());
    assert!(matches!(
        client.state(),
        State::Closing {
            error: CloseReason::Transport(Error::InvalidMigration),
            ..
        }
    ));
}

/// The timing of when frames arrive can mean that the migration path can
/// get the last available connection ID.
#[test]
fn retire_prior_to_migration_success() {
    let mut client = default_client();
    let cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>> =
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    let mut server = Connection::new_server(
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::clone(&cid_gen),
        ConnectionParameters::default(),
    )
    .unwrap();
    connect_force_idle(&mut client, &mut server);

    let original_cid = ConnectionId::from(get_cid(&send_something(&mut client, now())));

    client
        .migrate(Some(DEFAULT_ADDR_V4), Some(DEFAULT_ADDR_V4), false, now())
        .unwrap();

    // The client now probes the new path.
    let probe = client.process_output(now()).dgram().unwrap();
    assert_v4_path(&probe, true);
    assert_eq!(client.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&client, &probe, now());
    let probe_cid = ConnectionId::from(get_cid(&probe));
    assert_ne!(original_cid, probe_cid);

    // Have the server receive the probe, but separately have it decide to
    // retire all of the available connection IDs.
    let retire_all = send_with_extra(&mut server, NewConnectionIds::retire_all(cid_gen), now());

    let resp = server.process(Some(probe), now()).dgram().unwrap();
    assert_v4_path(&resp, true);
    assert_eq!(server.stats().frame_tx.path_response, 1);
    assert_eq!(server.stats().frame_tx.path_challenge, 1);
    assert_path_challenge_min_len(&server, &resp, now());

    // Have the client receive the NEW_CONNECTION_ID with Retire Prior To second.
    // As this occurs in a very specific order, migration succeeds.
    client.process_input(resp, now());
    client.process_input(retire_all, now());

    // Migration succeeds and the new path gets the last connection ID.
    let dgram = send_something(&mut client, now());
    assert_v4_path(&dgram, false);
    assert_ne!(get_cid(&dgram), original_cid);
    assert_ne!(get_cid(&dgram), probe_cid);
}

struct GarbageWriter {}

impl test_internal::FrameWriter for GarbageWriter {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        // Not a valid frame type.
        builder.encode_varint(u32::MAX);
    }
}

/// Test the case that we run out of connection ID and receive an invalid frame
/// from a new path.
#[test]
fn error_on_new_path_with_no_connection_id() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let cid_gen: Rc<RefCell<dyn ConnectionIdGenerator>> =
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default()));
    let retire_all = send_with_extra(&mut server, NewConnectionIds::retire_all(cid_gen), now());

    client.process_input(retire_all, now());

    let garbage = send_with_extra(&mut server, GarbageWriter {}, now());

    let dgram = change_path(&garbage, DEFAULT_ADDR_V4);
    client.process_input(dgram, now());

    // See issue #1697. We had a crash when the client had a temporary path and
    // process_output is called.
    let closing_frames = client.stats().frame_tx.connection_close;
    drop(client.process_output(now()));
    assert!(matches!(
        client.state(),
        State::Closing {
            error: CloseReason::Transport(Error::UnknownFrameType),
            ..
        }
    ));
    // Wait until the connection is closed.
    let mut now = now();
    now += client.process_output(now).callback();
    _ = client.process_output(now);
    // No closing frames should be sent, and the connection should be closed.
    assert_eq!(client.stats().frame_tx.connection_close, closing_frames);
    assert!(matches!(
        client.state(),
        State::Closed(CloseReason::Transport(Error::UnknownFrameType))
    ));
}

struct PathChallengeWriter {}

impl test_internal::FrameWriter for PathChallengeWriter {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        builder.encode_varint(FrameType::PathChallenge);
        builder.encode([0; 8]);
    }
}

/// A `PATH_RESPONSE` does not cause padding in a packet limited by the congestion window.
#[test]
fn path_response_not_padded_when_cwnd_limited() {
    let mut client = new_client(ConnectionParameters::default().pacing(false));
    let mut server = default_server();
    connect(&mut client, &mut server);

    // Leave less than a full MTU of congestion window, with nothing left to send.
    let id = client.stream_create(StreamType::UniDi).unwrap();
    while cwnd_avail(&client) >= client.plpmtu() {
        client.stream_send(id, &[0; 500]).unwrap();
        while client.process_output(now()).dgram().is_some() {}
    }

    let challenge = send_with_extra(&mut server, PathChallengeWriter {}, now());
    client.process_input(challenge, now());
    let before = client.stats().frame_tx;
    client.stream_send(id, &[0; 10]).unwrap();
    assert!(client.process_output(now()).dgram().is_some());
    let after = client.stats().frame_tx;
    assert_eq!(after.path_response, before.path_response + 1);
    assert_eq!(after.padding, before.padding);
}

/// Writes a padded `PATH_CHALLENGE` into every packet but the first, a long header packet.
struct ZeroRttPathChallenge {
    initial: bool,
}

impl test_internal::FrameWriter for ZeroRttPathChallenge {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        if self.initial {
            self.initial = false;
        } else {
            builder
                .encode_varint(FrameType::PathChallenge)
                .encode([7; 8]);
            builder.enable_padding(true);
        }
    }
}

/// A 0-RTT `PATH_CHALLENGE` from a new address that uses the server's connection ID
/// does not add a path, let alone a validated one, before the handshake completes.
#[test]
fn zero_rtt_path_challenge_on_new_path() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let token = exchange_ticket(&mut client, &mut server, now());

    let mut client = default_client();
    client.enable_resumption(now(), token).unwrap();
    let mut server = resumed_server(&client);

    let c1 = client.process_output(now()).dgram();
    let c2 = client.process_output(now()).dgram();
    server.process_input(c1.unwrap(), now());
    let s1 = server.process(c2, now()).dgram();
    let s2 = server.process_output(now()).dgram();

    // The client learns the server's connection ID from its first packet.
    let ack = client.process(s1, now()).dgram();
    server.process_input(ack.unwrap(), now());

    let challenge = send_with_extra(&mut client, ZeroRttPathChallenge { initial: true }, now());
    let challenge = change_source_address_and_port(&challenge, 1);
    let before = server.stats();
    server.process_input(challenge.clone(), now());
    let after = server.stats();
    assert_eq!(after.dropped_rx, before.dropped_rx + 1);
    assert_eq!(after.packets_rx, before.packets_rx);
    assert_eq!(after.frame_rx.path_challenge, 0);
    assert_eq!(*server.state(), State::Handshaking);

    // The handshake completes on the original path and the challenge is not answered.
    client.process_input(s2.unwrap(), now());
    connect(&mut client, &mut server);
    assert_eq!(server.stats().frame_tx.path_response, 0);

    // If the client moves to the other address, the server validates it.
    let c3 = change_source_address_and_port(&send_something(&mut client, now()), 1);
    let before = server.stats().frame_tx;
    let s3 = server.process(Some(c3), now()).dgram().unwrap();
    assert_path_challenge(&server, &s3, &before, challenge.source(), false);
}

/// Ask the server to send a ticket and check that it doesn't provide a `NEW_TOKEN` token
/// for the address of its primary path, which has not been validated.
fn assert_no_new_token(server: &mut Connection) {
    assert!(!server.paths.primary().unwrap().borrow().is_valid());
    let validation = AddressValidation::new(now(), ValidateAddress::NoToken).unwrap();
    let validation = Rc::new(RefCell::new(validation));
    server.set_validation(&validation);
    server.send_ticket(now(), &[]).unwrap();
    while server.process_output(now()).dgram().is_some() {}
    assert_eq!(server.stats().frame_tx.new_token, 0);
}

/// A server follows the client to a new address before it validates that address,
/// so it cannot issue a token for that address in the meantime.
#[test]
fn no_new_token_for_unvalidated_path() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    // Send a full datagram so that the amplification limit doesn't stop the server.
    let stream = client.stream_create(StreamType::UniDi).unwrap();
    fill_stream(&mut client, stream);
    let dgram = client.process_output(now()).dgram().unwrap();
    server.process_input(change_source_address_and_port(&dgram, 1), now());
    assert_no_new_token(&mut server);
}

/// A server drops a datagram with the client's Finished from a new address.
#[test]
fn server_discards_handshake_from_new_address() {
    let mut client = default_client();
    let mut server = default_server();
    while let Some(dgram) = client.process_output(now()).dgram() {
        server.process_input(dgram, now());
    }
    while let Some(dgram) = server.process_output(now()).dgram() {
        client.process_input(dgram, now());
    }
    assert!(maybe_authenticate(&mut client));

    let dgram = client.process_output(now()).dgram().unwrap();
    let before = server.stats();
    server.process_input(change_source_address_and_port(&dgram, 1), now());
    assert_eq!(*server.state(), State::Handshaking);
    assert_eq!(server.stats().dropped_rx, before.dropped_rx + 1);

    server.process_input(dgram, now());
    assert_eq!(*server.state(), State::Confirmed);
}

/// An unknown frame type closes the connection with `FRAME_ENCODING_ERROR`.
#[test]
fn unknown_frame_type() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let garbage = send_with_extra(&mut server, GarbageWriter {}, now());
    let close = client.process(Some(garbage), now()).dgram();
    assert_error(&client, &CloseReason::Transport(Error::UnknownFrameType));
    server.process_input(close.unwrap(), now());
    assert_error(&server, &CloseReason::Transport(Error::Peer(7)));
}

/// An invalid frame from a new path closes the connection on that path.
#[test]
fn close_on_new_path() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let garbage = send_with_extra(&mut server, GarbageWriter {}, now());
    let garbage = change_destination_port(&garbage);
    client.process_input(garbage.clone(), now());

    let close = client.process_output(now()).dgram().unwrap();
    assert_eq!(close.source(), garbage.destination());
    assert_eq!(client.stats().frame_tx.connection_close, 1);
}

/// Make `client` close on an invalid frame from a new path; return the size of that datagram.
fn error_on_new_path(client: &mut Connection, server: &mut Connection) -> usize {
    connect_force_idle(client, server);
    let garbage = send_with_extra(server, GarbageWriter {}, now());
    let dgram = change_path(&garbage, DEFAULT_ADDR_V4);
    let len = dgram.len();
    client.process_input(dgram, now());
    len
}

fn closing_path(c: &Connection) -> PathRef {
    let (StateSignaling::Closing(close) | StateSignaling::CloseSent(Some(close))) =
        &c.state_signaling
    else {
        panic!("not closing");
    };
    Rc::clone(close.path())
}

/// `CONNECTION_CLOSE` on an unvalidated path counts against the anti-amplification limit
/// (RFC 9000, Section 10.2.1).
#[test]
fn error_on_new_path_amplification_limit() {
    let mut client = default_client();
    let mut server = default_server();
    let budget = 3 * error_on_new_path(&mut client, &mut server);

    let close = client.process_output(now()).dgram().unwrap();
    assert_eq!(close.destination(), DEFAULT_ADDR_V4);
    let mut sent = close.len();
    // Every datagram from the server draws another CONNECTION_CLOSE, until the budget is spent.
    let trigger = send_something(&mut server, now());
    for _ in 0..10 {
        client.process_input(trigger.clone(), now());
        sent += client.process_output(now()).dgram().map_or(0, |d| d.len());
    }

    let path = closing_path(&client);
    assert!(!path.borrow().is_valid());
    assert!(sent <= budget);
}

/// A closing budget smaller than the AEAD expansion leaves room for no packet at all.
#[test]
fn error_on_new_path_tiny_amplification_limit() {
    let mut client = default_client();
    let mut server = default_server();
    error_on_new_path(&mut client, &mut server);

    let path = closing_path(&client);
    let (_, tx) = client
        .crypto
        .states()
        .select_tx(client.version, ApplicationData)
        .unwrap();
    let left = tx.expansion() - 1;
    let limit = path.borrow().amplification_limit();
    path.borrow_mut().add_sent(limit - left);
    assert!(client.process_output(now()).dgram().is_none());
    assert_eq!(path.borrow().amplification_limit(), left);
}

/// A Handshake packet validates the client address (RFC 9000, Section 8.1), even if it causes an
/// error.
#[test]
fn server_handshake_error_validates_path() {
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    server.process_input(client.process_output(now()).dgram().unwrap(), now());
    while let Some(d) = server.process_output(now()).dgram() {
        client.process_input(d, now());
    }
    client.test_frame_writer = Some(Box::new(GarbageWriter {}));
    let (_, handshake) = split_datagram(&client.process_output(now()).dgram().unwrap());

    server.process_input(handshake.unwrap(), now());
    assert!(matches!(
        server.state(),
        State::Closing {
            error: CloseReason::Transport(Error::UnknownFrameType),
            ..
        }
    ));
    assert!(server.paths.primary().unwrap().borrow().is_valid());
}

/// After the handshake, an error on a new path leaves it unvalidated, even if the packet uses the
/// server's initial connection ID.
#[test]
fn server_error_on_new_path_not_validated() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let garbage = send_with_extra(&mut client, GarbageWriter {}, now());
    let cid = &server.local_initial_source_cid;
    assert_eq!(&garbage[1..=cid.len()], cid.as_ref());

    let dgram = change_source_port(&garbage, 1);
    let remote = dgram.source();
    server.process_input(dgram, now());
    let path = closing_path(&server);
    assert_eq!(path.borrow().remote_address(), remote);
    assert!(!path.borrow().is_valid());
}
