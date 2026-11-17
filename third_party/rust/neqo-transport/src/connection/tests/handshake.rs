// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![allow(
    clippy::missing_asserts_for_indexing,
    clippy::unwrap_in_result,
    reason = "OK in tests"
)]

use std::{
    cell::RefCell,
    mem,
    net::{IpAddr, Ipv6Addr, SocketAddr},
    rc::Rc,
    time::{Duration, Instant},
};

use neqo_common::{Datagram, Decoder, Role, event::Provider as _, hex::Hex, to_u64};
use nss::{
    AuthenticationStatus, HandshakeMessage,
    constants::TLS_CHACHA20_POLY1305_SHA256,
    ext::{ExtensionHandler, ExtensionWriterResult},
    generate_ech_keys,
};
#[cfg(not(feature = "disable-encryption"))]
use test_fixture::datagram;
use test_fixture::{
    DEFAULT_ADDR,
    assertions::{assert_coalesced_0rtt, assert_handshake, assert_initial, assert_version},
    damage_ech_config, fixture_init,
    header_protection::{decode_initial_header, rewrite_initial},
    now, split_datagram, strip_padding,
};
use tracing::debug;

use super::{
    super::{Connection, Output, State, test_internal},
    AT_LEAST_PTO, CountingConnectionIdGenerator, CryptoWriter, DEFAULT_RTT, DEFAULT_STREAM_DATA,
    assert_error, connect, connect_fail, connect_force_idle, connect_with_rtt, default_client,
    default_server, drain, drop_flight, flush_to, get_tokens, handshake, handshake_with_modifier,
    maybe_authenticate, next_dgram, resumed_server, send_something, zero_len_cid_client,
};
use crate::{
    CloseReason, ConnectionParameters, DEFAULT_INITIAL_RTT, EmptyConnectionIdGenerator, Error,
    Pmtud, StreamId, StreamType, Version,
    connection::{
        AddressValidation,
        tests::{exchange_ticket, new_client, new_server, new_server_with_qlog},
    },
    datagram_queue::DatagramQueueOutcome,
    events::ConnectionEvent,
    frame::FrameType,
    packet,
    quic_datagrams::DATAGRAM_FRAME_TYPE_VARINT_LEN,
    server::ValidateAddress,
    stats::FrameStats,
    streams::SendGroupId,
    tparams::{
        TransportParameter,
        TransportParameterId::{self, *},
    },
    tracking::DEFAULT_LOCAL_ACK_DELAY,
};

const ECH_CONFIG_ID: u8 = 7;
const ECH_PUBLIC_NAME: &str = "public.example";

fn full_handshake(pmtud: bool) {
    debug!("---- client: generate CH");
    let mut client = new_client(ConnectionParameters::default().pmtud(pmtud));
    let out = client.process_output(now());
    let out2 = client.process_output(now());
    assert!(out.as_dgram_ref().is_some() && out2.as_dgram_ref().is_some());
    assert_eq!(out.as_dgram_ref().unwrap().len(), client.plpmtu());
    assert_eq!(out2.as_dgram_ref().unwrap().len(), client.plpmtu());

    debug!("---- server: CH -> SH, EE, CERT, CV, FIN");
    let mut server = new_server(ConnectionParameters::default().pmtud(pmtud));
    server.process_input(out.dgram().unwrap(), now());
    let out = server.process(out2.dgram(), now());
    assert!(out.as_dgram_ref().is_some());
    assert_eq!(out.as_dgram_ref().unwrap().len(), server.plpmtu());

    debug!("---- client: cert verification");
    let out = client.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());

    let out = server.process(out.dgram(), now());
    let out = client.process(out.dgram(), now());

    let out = server.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_none());

    assert!(maybe_authenticate(&mut client));

    debug!("---- client: SH..FIN -> FIN");
    let out = client.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());
    assert_eq!(*client.state(), State::Connected);

    debug!("---- server: FIN -> ACKS");
    let out = server.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());
    assert_eq!(*server.state(), State::Confirmed);

    debug!("---- client: ACKS -> 0");
    let out = client.process(out.dgram(), now());
    if pmtud {
        // PMTUD causes a PING probe to be sent here
        let pkt = out.dgram().unwrap();
        assert!(pkt.len() > client.plpmtu());
    } else {
        assert!(out.as_dgram_ref().is_none());
    }
    assert_eq!(*client.state(), State::Confirmed);
}

#[test]
fn handshake_no_pmtud() {
    full_handshake(false);
}

#[test]
fn handshake_pmtud() {
    full_handshake(true);
}

#[test]
fn handshake_failed_authentication() {
    debug!("---- client: generate CH");
    let mut client = default_client();
    let out = client.process_output(now());
    let out2 = client.process_output(now());
    assert!(out.as_dgram_ref().is_some() && out2.as_dgram_ref().is_some());

    debug!("---- server: CH -> SH, EE, CERT, CV, FIN");
    let mut server = default_server();
    server.process_input(out.dgram().unwrap(), now());
    let out = server.process(out2.dgram(), now());
    assert!(out.as_dgram_ref().is_some());

    debug!("---- client: cert verification");
    let out = client.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());

    let out = server.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());
    let out = client.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());
    let out = server.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_none());

    let authentication_needed = |e| matches!(e, ConnectionEvent::AuthenticationNeeded);
    assert!(client.events().any(authentication_needed));
    debug!("---- client: Alert(certificate_revoked)");
    client.authenticated(AuthenticationStatus::CertRevoked, now());

    debug!("---- client: -> Alert(certificate_revoked)");
    let out = client.process_output(now());
    assert!(out.as_dgram_ref().is_some());

    debug!("---- server: Alert(certificate_revoked)");
    let out = server.process(out.dgram(), now());
    assert!(out.as_dgram_ref().is_some());
    assert_error(&client, &CloseReason::Transport(Error::CryptoAlert(44)));
    assert_error(&server, &CloseReason::Transport(Error::Peer(300)));
}

#[test]
fn no_alpn() {
    let mut client = default_client();
    client.set_alpn(&["bad-alpn"]).unwrap();
    let mut server = default_server();

    handshake(&mut client, &mut server, now(), Duration::new(0, 0));
    assert_error(&client, &CloseReason::Transport(Error::Peer(376)));
    assert_error(&server, &CloseReason::Transport(Error::CryptoAlert(120)));
}

#[test]
fn dup_server_flight1() {
    let mut client = default_client();
    let c_hs_1 = client.process_output(now()).dgram();
    let c_hs_2 = client.process_output(now()).dgram();
    assert!(c_hs_1.is_some() && c_hs_2.is_some());
    assert_eq!(c_hs_1.as_ref().unwrap().len(), client.plpmtu());
    assert_eq!(c_hs_2.as_ref().unwrap().len(), client.plpmtu());

    let mut server = default_server();
    server.process_input(c_hs_1.unwrap(), now());
    let s_hs_1 = server.process(c_hs_2, now()).dgram().unwrap();
    let s_hs_2 = server.process_output(now()).dgram().unwrap();
    let s_hs_1 = strip_padding(s_hs_1);
    let s_hs_2 = strip_padding(s_hs_2);

    client.process_input(s_hs_1.clone(), now());
    client.process_input(s_hs_2.clone(), now());
    assert!(maybe_authenticate(&mut client));

    let out = client.process_output(now()).dgram();
    assert!(out.is_some());

    let before = client.stats();
    assert_eq!(0, before.dups_rx);
    assert_eq!(0, before.dropped_rx);

    // By now, the client should have dropped Initial keys.
    // So it should drop any of those that it receives.
    // We have three cases to cover:
    // 1. s_hs_1 contains Initial and Handshake, s_hs_2 contains Handshake
    // 2. s_hs_1 contains Initial, s_hs_2 contains Handshake
    // 3. s_hs_1 contains Initial, s_hs_2 contains Initial and Handshake
    client.process_input(s_hs_1, now());
    let in_between = client.stats();
    assert_eq!(1, in_between.dropped_rx);
    assert!((0..=1).contains(&in_between.dups_rx));

    client.process_input(s_hs_2, now());
    let after = client.stats();
    assert!((1..=2).contains(&after.dropped_rx));
    // In cases 1 and 2, there is only one duplicated packet.
    assert!(after.dropped_rx == 1 || after.dups_rx == 1);
    assert_eq!(in_between.dups_rx + 1, after.dups_rx);
}

// Test that we split crypto data if they cannot fit into one packet.
// To test this we will use a long server certificate.
#[test]
fn crypto_frame_split() {
    // This test has its own logic for generating large CRYPTO frames, so turn off MLKEM.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));

    let mut server = Connection::new_server(
        test_fixture::LONG_CERT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        ConnectionParameters::default(),
    )
    .expect("create a server");

    let client1 = client.process_output(now());
    assert!(client1.as_dgram_ref().is_some());

    // The entire server flight doesn't fit in a single packet because the
    // certificate is large, therefore the server will produce 2 packets.
    let server1 = server.process(client1.dgram(), now());
    assert!(server1.as_dgram_ref().is_some());
    let server2 = server.process_output(now());
    assert!(server2.as_dgram_ref().is_some());

    let client2 = client.process(server1.dgram(), now());
    // This is an ack.
    assert!(client2.as_dgram_ref().is_some());
    // The client might have the certificate now, so we can't guarantee that
    // this will work.
    let auth1 = maybe_authenticate(&mut client);
    assert_eq!(*client.state(), State::Handshaking);

    // let server process the ack for the first packet.
    let server3 = server.process(client2.dgram(), now());
    assert!(server3.as_dgram_ref().is_none());

    // Consume the second packet from the server.
    let client3 = client.process(server2.dgram(), now());

    // Check authentication.
    let auth2 = maybe_authenticate(&mut client);
    assert!(auth1 ^ auth2);
    // Now client has all data to finish handshake.
    assert_eq!(*client.state(), State::Connected);

    let client4 = client.process(server3.dgram(), now());
    // One of these will contain data depending on whether Authentication was completed
    // after the first or second server packet.
    assert!(client3.as_dgram_ref().is_some() ^ client4.as_dgram_ref().is_some());

    drop(server.process(client3.dgram(), now()));
    drop(server.process(client4.dgram(), now()));

    assert_eq!(*client.state(), State::Connected);
    assert_eq!(*server.state(), State::Confirmed);
}

/// Run a single ChaCha20-Poly1305 test and get a PTO.
#[test]
fn chacha20poly1305() {
    let mut server = default_server();
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    client.set_ciphers(&[TLS_CHACHA20_POLY1305_SHA256]).unwrap();
    connect_force_idle(&mut client, &mut server);
}

/// Test that a server can send 0.5 RTT application data.
#[test]
fn send_05rtt() {
    let mut client = default_client();
    let mut server = default_server();

    let c1 = client.process_output(now()).dgram();
    let c2 = client.process_output(now()).dgram();
    assert!(c1.is_some() && c2.is_some());
    server.process_input(c1.unwrap(), now());
    let s1 = server.process(c2, now()).dgram().unwrap();
    assert_eq!(s1.len(), server.plpmtu());

    // The server should accept writes at this point.
    let s2 = send_something(&mut server, now());

    // Complete the handshake at the client.
    client.process_input(s1, now());

    // The client should receive the 0.5-RTT data now.
    client.process_input(s2, now());
    maybe_authenticate(&mut client);
    assert_eq!(*client.state(), State::Connected);
    let mut buf = vec![0; DEFAULT_STREAM_DATA.len() + 1];
    let stream_id = client
        .events()
        .find_map(|e| {
            if let ConnectionEvent::RecvStreamReadable { stream_id } = e {
                Some(stream_id)
            } else {
                None
            }
        })
        .unwrap();
    let (l, ended) = client.stream_recv(stream_id, &mut buf).unwrap();
    assert_eq!(&buf[..l], DEFAULT_STREAM_DATA);
    assert!(ended);
}

/// Test that a client buffers 0.5-RTT data when it arrives early.
#[test]
fn reorder_05rtt() {
    // This test makes too many assumptions about single-packet PTOs for multi-packet MLKEM flights
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();

    let c1 = client.process_output(now()).dgram();
    assert!(c1.is_some());
    let s1 = server.process(c1, now()).dgram().unwrap();

    // The server should accept writes at this point.
    let s2 = send_something(&mut server, now());

    // We can't use the standard facility to complete the handshake, so
    // drive it as aggressively as possible.
    assert_eq!(client.stats().saved_datagrams, 0);
    assert_eq!(client.stats().packets_rx, 0);
    client.process_input(s2, now());
    assert_eq!(client.stats().saved_datagrams, 1);
    assert_eq!(client.stats().packets_rx, 0);

    // After processing the first packet, the client should go back and
    // process the 0.5-RTT packet data, which should make data available.
    client.process_input(s1, now());
    // We can't use `maybe_authenticate` here as that consumes events.
    client.authenticated(AuthenticationStatus::Ok, now());
    assert_eq!(*client.state(), State::Connected);

    let mut buf = vec![0; DEFAULT_STREAM_DATA.len() + 1];
    let stream_id = client
        .events()
        .find_map(|e| {
            if let ConnectionEvent::RecvStreamReadable { stream_id } = e {
                Some(stream_id)
            } else {
                None
            }
        })
        .unwrap();
    let (l, ended) = client.stream_recv(stream_id, &mut buf).unwrap();
    assert_eq!(&buf[..l], DEFAULT_STREAM_DATA);
    assert!(ended);
}

#[test]
fn reorder_05rtt_with_0rtt() {
    const RTT: Duration = Duration::from_millis(100);

    let mut client = default_client();
    let mut server = default_server();
    let validation = AddressValidation::new(now(), ValidateAddress::NoToken).unwrap();
    let validation = Rc::new(RefCell::new(validation));
    server.set_validation(&validation);
    let mut now = connect_with_rtt(&mut client, &mut server, now(), RTT);

    // Include RTT in sending the ticket or the ticket age reported by the
    // client is wrong, which causes the server to reject 0-RTT.
    now += RTT / 2;
    server.send_ticket(now, &[]).unwrap();
    let ticket = server.process_output(now).dgram().unwrap();
    now += RTT / 2;
    client.process_input(ticket, now);

    let token = get_tokens(&mut client).pop().unwrap();
    // This test makes too many assumptions about what's in the packets to work with multi-packet
    // MLKEM flights.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    client.enable_resumption(now, token).unwrap();
    let mut server = resumed_server(&client);

    // Send ClientHello and some 0-RTT.
    let c1 = send_something(&mut client, now);
    assert_coalesced_0rtt(&c1[..]);
    // Drop the 0-RTT from the coalesced datagram, so that the server
    // acknowledges the next 0-RTT packet.
    let (c1, _) = split_datagram(&c1);
    let c2 = send_something(&mut client, now);

    // Handle the first packet and send 0.5-RTT in response.  Drop the response.
    now += RTT / 2;
    drop(server.process(Some(c1), now).dgram().unwrap());
    // The gap in 0-RTT will result in this 0.5 RTT containing an ACK.
    server.process_input(c2, now);
    let s2 = send_something(&mut server, now);

    // Save the 0.5 RTT.
    now += RTT / 2;
    client.process_input(s2, now);
    assert_eq!(client.stats().saved_datagrams, 1);

    // Now PTO at the client and cause the server to re-send handshake packets.
    now += AT_LEAST_PTO;
    let c3 = client.process_output(now).dgram();
    assert_coalesced_0rtt(c3.as_ref().unwrap());

    now += RTT / 2;
    let s3 = server.process(c3, now).dgram().unwrap();

    // The client should be able to process the 0.5 RTT now.
    // This should contain an ACK, so we are processing an ACK from the past.
    now += RTT / 2;
    client.process_input(s3, now);
    maybe_authenticate(&mut client);
    let c4 = client.process_output(now).dgram();
    assert_eq!(*client.state(), State::Connected);
    assert_eq!(client.paths.rtt(), RTT);

    now += RTT / 2;
    server.process_input(c4.unwrap(), now);
    assert_eq!(*server.state(), State::Confirmed);
    // Don't check server RTT as it will be massively inflated by a
    // poor initial estimate received when the server dropped the
    // Initial packet number space.
}

/// Test that a server that coalesces 0.5 RTT with handshake packets
/// doesn't cause the client to drop application data.
#[test]
fn coalesce_05rtt() {
    const RTT: Duration = Duration::from_millis(100);
    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();

    // The first exchange doesn't offer a chance for the server to send.
    // So drop the server flight and wait for the PTO.
    let c1 = client.process_output(now).dgram();
    let c11 = client.process_output(now).dgram();
    assert!(c1.is_some() && c11.is_some());
    now += RTT / 2;
    server.process_input(c1.unwrap(), now);
    let s1 = server.process(c11, now).dgram();
    assert!(s1.is_some());

    // Drop the server flight.  Then send some data.
    let stream_id = server.stream_create(StreamType::UniDi).unwrap();
    assert!(server.stream_send(stream_id, DEFAULT_STREAM_DATA).is_ok());
    assert!(server.stream_close_send(stream_id).is_ok());

    // Now after a PTO the client can send another packet.
    // The server should then send its entire flight again,
    // including the application data, which it sends in a 1-RTT packet.
    now += AT_LEAST_PTO;
    let c2_1 = client.process_output(now).dgram();
    let c2_2 = client.process_output(now).dgram();
    assert!(c2_1.is_some() && c2_2.is_some());
    now += RTT / 2;
    server.process_input(c2_2.unwrap(), now);
    let s2 = server.process(c2_1, now).dgram();

    // s2 is just an Initial, which might be padded.
    let dgram = client.process(s2.map(strip_padding), now).dgram();
    // Padding was stripped, so a dropped packet would indicate a problem.
    assert_eq!(client.stats().dropped_rx, 0);
    let s2 = server.process(dgram, now).dgram();

    // The client should process the datagram.  It can't process the 1-RTT
    // packet until authentication completes though.  So it saves it.
    now += RTT / 2;
    drop(client.process(s2, now).dgram());
    // This packet will contain an ACK, but we can ignore it.
    assert_eq!(client.stats().packets_rx, 3);
    assert_eq!(client.stats().saved_datagrams, 1);

    // After (successful) authentication, the packet is processed.
    maybe_authenticate(&mut client);
    let c3 = client.process_output(now).dgram();
    assert!(c3.is_some());
    assert_eq!(client.stats().packets_rx, 4);
    assert_eq!(client.stats().saved_datagrams, 1);

    client
        .events()
        .find(|e| matches!(e, ConnectionEvent::RecvStreamReadable { .. }))
        .expect(" client not to drop 0.5 RTT coalesced application data");

    // Allow the handshake to complete.
    now += RTT / 2;
    let s3 = server.process(c3, now).dgram();
    assert!(s3.is_some());
    assert_eq!(*server.state(), State::Confirmed);
    now += RTT / 2;
    drop(client.process(s3, now).dgram());
    assert_eq!(*client.state(), State::Confirmed);

    // The client should never have received padding.
    assert_eq!(client.stats().dropped_rx, 0);
}

#[test]
fn reorder_handshake() {
    const V2_INITIAL: u8 = 0b1001_0000;
    const RTT: Duration = Duration::from_millis(100);
    // Disable packet number randomization for deterministic packet counts.
    let mut client = new_client(ConnectionParameters::default().randomize_first_pn(false));
    let mut server = new_server(ConnectionParameters::default().randomize_first_pn(false));
    let mut now = now();

    let c1 = client.process_output(now).dgram();
    let c2 = client.process_output(now).dgram();
    assert!(c1.is_some() && c2.is_some());

    now += RTT / 2;
    server.process_input(c1.unwrap(), now);
    let _s_initial = server.process(c2, now).dgram().unwrap();
    let s_handshake_1 = server.process_output(now).dgram().unwrap();
    let s_hs1_has_initial = s_handshake_1[0] & 0b1011_0000 == V2_INITIAL;

    // Pass just the handshake packet in and the client can't handle it yet.
    // It can only send another Initial packet.
    now += RTT + RTT / 2; // With multi-packet MLKEM flights, client needs more time here.
    let dgram = client.process(Some(s_handshake_1), now).dgram();
    assert_initial(dgram.as_ref().unwrap(), false);
    assert_eq!(client.stats().saved_datagrams, 1);
    assert_eq!(client.stats().packets_rx, usize::from(s_hs1_has_initial));

    // Get the server to try again.
    // Though we currently allow the server to arm its PTO timer, use
    // a second client Initial packet to cause it to send again.
    now += AT_LEAST_PTO;
    let c2 = client.process_output(now).dgram();
    now += RTT / 2;
    // When Handshake PTO fires, Initial packets are also marked for retransmission.
    // The server sends three Initial datagrams, then a Handshake datagram.
    let s_pkt_tx_before = server.stats().packets_tx;
    let s_initial_2 = server.process(c2, now).dgram().unwrap();
    assert_initial(&s_initial_2, false);
    let s_initial_3 = server.process_output(now).dgram().unwrap();
    assert_initial(&s_initial_3, false);
    let s_initial_4 = server.process_output(now).dgram().unwrap();
    assert_initial(&s_initial_4, false);
    // Count the number of packets sent, so we can account for any Handshake packets.
    let s_pkt_tx = server.stats().packets_tx - s_pkt_tx_before;
    let s_handshake_2 = server.process_output(now).dgram().unwrap();
    let s_hs2_has_initial = s_handshake_2[0] & 0b1011_0000 == V2_INITIAL;

    // Processing the Handshake packet first should save it.
    now += RTT / 2;
    client.process_input(s_handshake_2, now);

    let c_stats_before = client.stats();
    assert_eq!(c_stats_before.saved_datagrams, 2);
    // There's a chance that each "handshake" datagram contained a little bit of an Initial packet.
    // That will have been processed by the client.
    assert_eq!(
        c_stats_before.packets_rx,
        usize::from(s_hs1_has_initial) + usize::from(s_hs2_has_initial)
    );
    assert_eq!(c_stats_before.dropped_rx, 0);

    // Deliver all Initial packets.
    client.process_input(s_initial_2, now);
    client.process_input(s_initial_3, now);
    client.process_input(s_initial_4, now);
    // The client should have received and processed `s_pkt_tx`,
    // plus the two saved datagrams that were saved
    // and any garbage that was dropped.
    let c_stats_after = client.stats();
    assert_eq!(
        c_stats_after.packets_rx - c_stats_before.packets_rx,
        s_pkt_tx + 2 + c_stats_after.dropped_rx
    );
    maybe_authenticate(&mut client);
    let c3 = client.process_output(now).dgram();
    assert!(c3.is_some());

    // Note that though packets were saved and processed very late,
    // they don't cause the RTT to change.
    now += RTT / 2;
    let s3 = server.process(c3, now).dgram();
    assert_eq!(*server.state(), State::Confirmed);
    // Don't check server RTT estimate as it will be inflated due to
    // it making a guess based on retransmissions when it dropped
    // the Initial packet number space.

    now += RTT / 2;
    client.process_input(s3.unwrap(), now);
    assert_eq!(*client.state(), State::Confirmed);
    assert_eq!(client.paths.rtt(), RTT);
}

/// When a compatible version upgrade occurs, the server needs to handle
/// Initial packets from both versions.  Check that it doesn't drop them,
/// which would be recoverable, but wasteful.
#[test]
fn interleave_versions_server() {
    let mut client = new_client(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let mut server = default_server();
    let mut now = now();

    let c1 = client.process_output(now).dgram();
    let c2 = client.process_output(now).dgram();
    assert!(c1.is_some() && c2.is_some());

    now += AT_LEAST_PTO;
    let cspare = client.process_output(now).dgram();
    assert_version(cspare.as_ref().unwrap(), Version::Version1.wire_version());
    assert_initial(cspare.as_ref().unwrap(), false);

    server.process_input(c1.unwrap(), now);
    let s1 = server.process(c2, now).dgram().unwrap();
    let s2 = server.process_output(now).dgram().unwrap();

    client.process_input(s1, now);
    client.process_input(s2, now);
    maybe_authenticate(&mut client);
    let chandshake = client.process_output(now).dgram();
    assert_version(
        chandshake.as_ref().unwrap(),
        Version::Version2.wire_version(),
    );

    // Now send in the v2 and v1 packets out of order.
    // Both should be accepted, even though the version is now set to v2.
    assert!(server.has_version());
    assert_eq!(server.version(), Version::Version2);

    let before = server.stats();
    server.process_input(chandshake.unwrap(), now);
    let after = server.stats();
    assert!(before.packets_rx < after.packets_rx); // Some number of packets went in.
    assert_eq!(before.dropped_rx, after.dropped_rx); // None were dropped.

    let before = server.stats();
    server.process_input(cspare.unwrap(), now);
    let after = server.stats();
    assert!(before.packets_rx < after.packets_rx); // Some number of packets went in.
    assert_eq!(before.dropped_rx + 1, after.dropped_rx); // This packet was padded, so we drop 1.

    let done = server.process_output(now).dgram();
    assert_eq!(*server.state(), State::Confirmed);
    client.process_input(done.unwrap(), now);
    assert_eq!(*client.state(), State::Confirmed);
}

/// When a compatible version upgrade occurs, the client also needs to handle
/// Initial packets from both versions.
#[test]
fn interleave_versions_client() {
    let mut client = new_client(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let mut server = default_server();
    let now = now();

    let c1 = client.process_output(now).dgram();
    let c2 = client.process_output(now).dgram();
    assert!(c1.is_some() && c2.is_some());

    // The server will ACK the packet, but that's it.
    let s1 = server.process(c1, now).dgram();
    assert_initial(s1.as_ref().unwrap(), false);
    assert_version(s1.as_ref().unwrap(), Version::Version1.wire_version());
    assert!(!server.has_version());

    // Once it has all the packets the server can choose a version.
    let s2 = server.process(c2, now).dgram();
    assert_initial(s2.as_ref().unwrap(), false);
    assert_version(s2.as_ref().unwrap(), Version::Version2.wire_version());
    assert!(server.has_version());

    // Receiving the first packet (no CRYPTO) doesn't set the version.
    client.process_input(s1.unwrap(), now);
    let client_stats = client.stats();
    assert_eq!(client_stats.packets_rx, 1); // Just an Initial packet for now.
    assert_eq!(client_stats.frame_rx.crypto, 0); // No CRYPTO
    assert!(!client.has_version());

    // The second does.
    client.process_input(s2.unwrap(), now);
    let s3 = server.process_output(now).dgram().unwrap();
    if s3[0] & 0b1011_0000 == 0b1001_0000 {
        // The Initial (v2!) spilled over into another datagram.
        let (extra, _) = split_datagram(&s3);
        client.process_input(extra, now);
    }
    assert!(client.has_version());
    assert_eq!(client.version(), Version::Version2);

    // Let the server finish its handshake.
    client.process_input(s3, now);

    // The client finishes with v2 packets.
    maybe_authenticate(&mut client);
    let chandshake = client.process_output(now).dgram();
    assert_version(
        chandshake.as_ref().unwrap(),
        Version::Version2.wire_version(),
    );

    let done = server.process(chandshake, now).dgram();
    assert_eq!(*server.state(), State::Confirmed);
    client.process_input(done.unwrap(), now);
    assert_eq!(*client.state(), State::Confirmed);
}

#[test]
fn reorder_1rtt() {
    const RTT: Duration = Duration::from_millis(100);
    const PACKETS: usize = 4; // Many, but not enough to overflow cwnd.
    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();

    let c1 = client.process_output(now).dgram().map(strip_padding);
    let c2 = client.process_output(now).dgram().map(strip_padding);
    assert!(c1.is_some() && c2.is_some());

    now += RTT / 2;
    server.process_input(c1.unwrap(), now);
    let s1 = server.process(c2, now).dgram().map(strip_padding);
    assert!(s1.is_some());

    now += RTT / 2;
    let dgram = client.process(s1, now).dgram().map(strip_padding);

    now += RTT / 2;
    let dgram = server.process(dgram, now).dgram().map(strip_padding);

    now += RTT / 2;
    client.process_input(dgram.unwrap(), now);
    maybe_authenticate(&mut client);
    let c2 = client.process_output(now).dgram().map(strip_padding);
    assert!(c2.is_some());

    // Now get a bunch of packets from the client.
    // Give them to the server before giving it `c2`.
    for _ in 0..PACKETS {
        let d = send_something(&mut client, now);
        server.process_input(strip_padding(d), now + RTT / 2);
    }
    // The server has now received those packets, and saved them.
    // It has only been given the three handshake packets we gave it.
    assert_eq!(server.stats().packets_rx, 3);
    assert_eq!(server.stats().saved_datagrams, PACKETS);
    assert_eq!(server.stats().dropped_rx, 0);

    now += RTT / 2;
    let s2 = server.process(c2, now).dgram();
    // The server has now received those packets, and saved them.
    // The two additional are an Initial w/ACK, a Handshake w/ACK and a 1-RTT (w/
    // NEW_CONNECTION_ID).
    assert!(server.stats().packets_rx > PACKETS);
    assert_eq!(server.stats().saved_datagrams, PACKETS);
    assert_eq!(server.stats().dropped_rx, 0);
    assert_eq!(*server.state(), State::Confirmed);
    assert_eq!(server.paths.rtt(), RTT);

    now += RTT / 2;
    client.process_input(s2.unwrap(), now);
    assert_eq!(client.paths.rtt(), RTT);

    // All the stream data that was sent should now be available.
    let streams = server
        .events()
        .filter_map(|e| {
            if let ConnectionEvent::RecvStreamReadable { stream_id } = e {
                Some(stream_id)
            } else {
                None
            }
        })
        .collect::<Vec<_>>();
    assert_eq!(streams.len(), PACKETS);
    for stream_id in streams {
        let mut buf = vec![0; DEFAULT_STREAM_DATA.len() + 1];
        let (recvd, fin) = server.stream_recv(stream_id, &mut buf).unwrap();
        assert_eq!(recvd, DEFAULT_STREAM_DATA.len());
        assert!(fin);
    }
}

#[cfg(not(feature = "disable-encryption"))]
#[test]
fn corrupted_initial() {
    let mut client = default_client();
    let mut server = default_server();

    let d = client.process_output(now()).dgram().unwrap();
    let mut corrupted = Vec::from(&d[..]);
    // Find the last non-padding value and corrupt that.
    let (idx, _) = corrupted
        .iter()
        .enumerate()
        .rev()
        .find(|&(_, &v)| v != 0)
        .unwrap();
    corrupted[idx] ^= 0x76;

    let dgram = Datagram::new(d.source(), d.destination(), d.tos(), corrupted);
    server.process_input(dgram, now());
    // The server should have received two "packets", both corrupted.
    assert_eq!(server.stats().packets_rx, 2);
    assert_eq!(server.stats().dropped_rx, 2);
}

#[test]
// Absent path PTU discovery, max v6 packet size should be PATH_MTU_V6.
fn verify_pkt_honors_mtu() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let now = now();

    let cb = client.process_output(now).callback();
    assert_eq!(cb, ConnectionParameters::DEFAULT_IDLE_TIMEOUT);

    // Try to send a large stream and verify first packet is correctly sized
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.stream_send(stream_id, &[0xbb; 2000]).unwrap(), 2000);
    let pkt0 = client.process_output(now);
    assert!(matches!(pkt0, Output::Datagram(_)));
    assert_eq!(pkt0.as_dgram_ref().unwrap().len(), client.plpmtu());
}

#[test]
fn extra_initial_hs() {
    // Disable MLKEM here because we need to have the client Initial in a single packet.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    let mut now = now();

    let c_init = client.process_output(now).dgram();
    assert!(c_init.is_some());
    now += DEFAULT_RTT / 2;
    let s_init = server.process(c_init, now).dgram().unwrap();
    now += DEFAULT_RTT / 2;

    let (mut undecryptable, _) = split_datagram(&s_init);
    assert_eq!(undecryptable[0] & 0x80, 0x80, "is long header packet");
    // Turn the Initial packet from the server into a Handshake packet.
    // It will be a badly formatted one, but the client will save it
    // and send back an Initial each time.
    undecryptable[0] += 0x20;
    // Feed that undecryptable packet into the client a few times.
    // Do that MAX_SAVED_DATAGRAMS times and each time the client will emit
    // another Initial packet.
    for _ in 0..crate::saved::SavedDatagrams::CAPACITY {
        let c_init = match client.process(Some(undecryptable.clone()), now) {
            Output::None => unreachable!(),
            Output::Datagram(c_init) => Some(c_init),
            Output::Callback(duration) => {
                now += duration;
                client.process_output(now).dgram()
            }
        };
        assert_initial(c_init.as_ref().unwrap(), false);
        now += DEFAULT_RTT / 10;
    }

    // After MAX_SAVED_DATAGRAMS, the client stops sending Initial packets.
    // This is why we disable MLKEM: a large Initial would force the client
    // to send two packets in response to each undecryptable packet.
    // In that case, the client would still be probing the Initial space on PTO.
    let nothing = client.process(Some(undecryptable), now);
    assert!(nothing.as_dgram_ref().is_none());

    // Until PTO, where another Initial can be used to complete the handshake.
    now += nothing.callback();
    let c_init = client.process_output(now).dgram();
    assert_initial(c_init.as_ref().unwrap(), false);
    now += DEFAULT_RTT / 2;
    let s_init = server.process(c_init, now).dgram();
    now += DEFAULT_RTT / 2;
    client.process_input(s_init.unwrap(), now);
    maybe_authenticate(&mut client);
    let c_fin = client.process_output(now).dgram();
    assert_eq!(*client.state(), State::Connected);
    now += DEFAULT_RTT / 2;
    server.process_input(c_fin.unwrap(), now);
    assert_eq!(*server.state(), State::Confirmed);
}

#[test]
fn extra_initial_invalid_cid() {
    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();

    let c_init = client.process_output(now).dgram();
    let c_init2 = client.process_output(now).dgram();
    assert!(c_init.is_some() && c_init2.is_some());
    now += DEFAULT_RTT / 2;
    server.process_input(c_init.unwrap(), now);
    let s_init = server.process(c_init2, now).dgram();
    assert!(s_init.is_some());
    let s_hs = server.process_output(now).dgram().unwrap();
    now += DEFAULT_RTT / 2;

    // If the client receives a packet that contains the wrong connection
    // ID, it won't send another Initial.
    let mut copy = s_hs.to_vec();
    assert_ne!(copy[5], 0); // The DCID should be non-zero length.
    copy[6] ^= 0xc4;
    let dgram_copy = Datagram::new(s_hs.destination(), s_hs.source(), s_hs.tos(), copy);
    let nothing = client.process(Some(dgram_copy), now).dgram();
    assert!(nothing.is_none());
}

#[test]
fn connect_one_version() {
    fn connect_v(version: Version) {
        fixture_init();
        let mut client = Connection::new_client(
            test_fixture::DEFAULT_SERVER_NAME,
            test_fixture::DEFAULT_ALPN,
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            DEFAULT_ADDR,
            DEFAULT_ADDR,
            ConnectionParameters::default().versions(version, vec![version]),
            now(),
        )
        .unwrap();
        let mut server = Connection::new_server(
            test_fixture::DEFAULT_KEYS,
            test_fixture::DEFAULT_ALPN,
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            ConnectionParameters::default().versions(version, vec![version]),
        )
        .unwrap();
        connect_force_idle(&mut client, &mut server);
        assert_eq!(client.version(), version);
        assert_eq!(server.version(), version);
    }

    for v in Version::all() {
        println!("Connecting with {v:?}");
        connect_v(v);
    }
}

#[test]
fn anti_amplification() {
    // This test has its own logic for generating large CRYPTO frames, so turn off MLKEM.
    let mut client = new_client(ConnectionParameters::default().mlkem(false));
    let mut server = default_server();
    let mut now = now();

    // With a gigantic transport parameter, the server is unable to complete
    // the handshake within the amplification limit.
    let very_big = TransportParameter::Bytes(vec![0; Pmtud::default_plpmtu(DEFAULT_ADDR.ip()) * 3]);
    server
        .set_local_tparam(TestTransportParameter, very_big)
        .unwrap();

    let c_init = client.process_output(now).dgram();
    now += DEFAULT_RTT / 2;
    let s_init1 = server.process(c_init, now).dgram().unwrap();
    assert_eq!(s_init1.len(), server.plpmtu());
    let s_init2 = server.process_output(now).dgram().unwrap();
    assert_eq!(s_init2.len(), server.plpmtu());
    let s_init3 = server.process_output(now).dgram().unwrap();
    assert_eq!(s_init3.len(), server.plpmtu());
    let cb = server.process_output(now).callback();

    // We are blocked by the amplification limit now.
    assert_eq!(cb, server.conn_params.get_idle_timeout());

    now += DEFAULT_RTT / 2;
    client.process_input(s_init1, now);
    client.process_input(s_init2, now);
    let ack_count = client.stats().frame_tx.ack;
    let frame_count = client.stats().frame_tx.all();
    let ack = client.process(Some(s_init3), now).dgram().unwrap();
    assert!(!maybe_authenticate(&mut client)); // No need yet.

    // The client sends a padded datagram, with just ACKs for Initial and Handshake.
    // Per RFC 9000 Section 14.1, datagrams containing Initial packets must be
    // at least 1200 bytes, even when coalesced with Handshake packets.
    assert_eq!(client.stats().frame_tx.ack, ack_count + 2);
    assert_eq!(client.stats().frame_tx.all(), frame_count + 2);
    assert_eq!(ack.len(), client.plpmtu()); // Must be padded (contains Initial).

    now += DEFAULT_RTT / 2;
    let remainder = server.process(Some(ack), now).dgram();

    now += DEFAULT_RTT / 2;
    client.process_input(remainder.unwrap(), now);
    assert!(maybe_authenticate(&mut client)); // OK, we have all of it.
    let fin = client.process_output(now).dgram();
    assert_eq!(*client.state(), State::Connected);

    now += DEFAULT_RTT / 2;
    server.process_input(fin.unwrap(), now);
    assert_eq!(*server.state(), State::Confirmed);
}

#[cfg(not(feature = "disable-encryption"))]
#[test]
fn garbage_initial() {
    let mut client = default_client();
    let mut server = default_server();

    let dgram = client.process_output(now()).dgram().unwrap();
    let (initial, rest) = split_datagram(&dgram);
    let mut corrupted = Vec::from(&initial[..initial.len() - 1]);
    corrupted.push(initial[initial.len() - 1] ^ 0xb7);
    corrupted.extend_from_slice(rest.as_ref().map_or(&[], |r| &r[..]));
    let garbage = datagram(corrupted);
    assert_eq!(Output::None, server.process(Some(garbage), now()));
}

#[test]
fn drop_initial_packet_from_wrong_address() {
    let mut client = default_client();
    let out = client.process_output(now());
    let out2 = client.process_output(now());
    assert!(out.as_dgram_ref().is_some() && out2.as_dgram_ref().is_some());

    let mut server = default_server();
    server.process_input(out.dgram().unwrap(), now());
    let out = server.process(out2.dgram(), now());
    assert!(out.as_dgram_ref().is_some());

    let p = out.dgram().unwrap();
    let dgram = Datagram::new(
        SocketAddr::new(IpAddr::V6(Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 2)), 443),
        p.destination(),
        p.tos(),
        &p[..],
    );

    let out = client.process(Some(dgram), now());
    assert!(out.as_dgram_ref().is_none());
}

#[test]
fn drop_handshake_packet_from_wrong_address() {
    let mut client = default_client();
    let out = client.process_output(now());
    let out2 = client.process_output(now());
    assert!(out.as_dgram_ref().is_some() && out2.as_dgram_ref().is_some());

    let mut server = default_server();
    server.process_input(out.dgram().unwrap(), now());
    let out = server.process(out2.dgram(), now());
    assert!(out.as_dgram_ref().is_some());

    let (s_in, s_hs) = split_datagram(&out.dgram().unwrap());

    // Pass the initial packet to the client.
    client.process_input(s_in, now());

    // The server packet might be all Initial.  If it is, then ask for another one.
    let s_hs = s_hs.unwrap_or_else(|| {
        let dgram = server.process_output(now()).dgram();
        let (s_in, s_hs) = split_datagram(&dgram.unwrap());
        // Let the client process any Initial, then keep the Handshake packet.
        if let Some(s_hs) = s_hs {
            client.process_input(s_in, now());
            s_hs
        } else {
            s_in // This is Handshake, not Initial
        }
    });

    // Let the client acknowledge the packet(s) it received.
    drop(client.process_output(now()));

    let dgram = Datagram::new(
        SocketAddr::new(IpAddr::V6(Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 2)), 443),
        s_hs.destination(),
        s_hs.tos(),
        &s_hs[..],
    );
    let out = client.process(Some(dgram), now());
    assert!(out.as_dgram_ref().is_none());
}

#[test]
fn ech() {
    let mut server = default_server();
    let (sk, pk) = generate_ech_keys().unwrap();
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();

    let mut client = default_client();
    client.client_enable_ech(server.ech_config()).unwrap();

    connect(&mut client, &mut server);

    assert!(client.tls_info().unwrap().ech_accepted());
    assert!(server.tls_info().unwrap().ech_accepted());
    assert!(client.tls_preinfo().unwrap().ech_accepted().unwrap());
    assert!(server.tls_preinfo().unwrap().ech_accepted().unwrap());
}

/// See <https://github.com/mozilla/neqo/pull/2789> for details.
#[test]
fn ech_no_partial_ech_behavior_on_invalid_config() {
    let mut server = default_server();
    let (sk, pk) = generate_ech_keys().unwrap();
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();

    let mut client = default_client();
    let ech_config = server.ech_config().to_vec();
    let invalid_ech_config = ech_config[..ech_config.len() - 1].to_vec();

    client
        .client_enable_ech(invalid_ech_config)
        .expect_err("client ignores invalid ECH config");

    // Establish a non-ECH connection.
    connect(&mut client, &mut server);

    // Expect the server to be able to create a stream to the client.
    // This checks that the client did not fall back to a partial ECH handshake,
    // ensuring all required transport parameters (such as `InitialMaxStreamsBidi`)
    // were present in the non-ECH ClientHello.
    server.stream_create(StreamType::BiDi).unwrap();
}

#[test]
fn ech_retry() {
    fixture_init();
    let mut server = default_server();
    let (sk, pk) = generate_ech_keys().unwrap();
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();

    let mut client = default_client();
    client
        .client_enable_ech(damage_ech_config(server.ech_config()))
        .unwrap();

    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    client.process_input(dgram.unwrap(), now());
    let auth_event = ConnectionEvent::EchFallbackAuthenticationNeeded {
        public_name: String::from(ECH_PUBLIC_NAME),
    };
    assert!(client.events().any(|e| e == auth_event));
    client.authenticated(AuthenticationStatus::Ok, now());
    assert!(client.state().error().is_some());

    // Tell the server about the error.
    let dgram = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    assert_eq!(
        server.state().error(),
        Some(&CloseReason::Transport(Error::Peer(0x100 + 121)))
    );

    let Some(CloseReason::Transport(Error::EchRetry(updated_config))) = client.state().error()
    else {
        panic!(
            "Client state should be failed with EchRetry, is {:?}",
            client.state()
        );
    };

    let mut server = default_server();
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();
    let mut client = default_client();
    client.client_enable_ech(updated_config).unwrap();

    connect(&mut client, &mut server);

    assert!(client.tls_info().unwrap().ech_accepted());
    assert!(server.tls_info().unwrap().ech_accepted());
    assert!(client.tls_preinfo().unwrap().ech_accepted().unwrap());
    assert!(server.tls_preinfo().unwrap().ech_accepted().unwrap());
}

#[test]
fn ech_retry_fallback_rejected() {
    fixture_init();
    let mut server = default_server();
    let (sk, pk) = generate_ech_keys().unwrap();
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();

    let mut client = default_client();
    client
        .client_enable_ech(damage_ech_config(server.ech_config()))
        .unwrap();

    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    client.process_input(dgram.unwrap(), now());
    let auth_event = ConnectionEvent::EchFallbackAuthenticationNeeded {
        public_name: String::from(ECH_PUBLIC_NAME),
    };
    assert!(client.events().any(|e| e == auth_event));
    client.authenticated(AuthenticationStatus::PolicyRejection, now());
    assert!(client.state().error().is_some());

    if let Some(CloseReason::Transport(Error::EchRetry(_))) = client.state().error() {
        panic!("Client should not get EchRetry error");
    }

    // Pass the error on.
    let dgram = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    assert_eq!(
        server.state().error(),
        Some(&CloseReason::Transport(Error::Peer(298)))
    ); // A bad_certificate alert.
}

#[test]
fn bad_min_ack_delay() {
    const EXPECTED_ERROR: CloseReason = CloseReason::Transport(Error::TransportParameter);
    let mut server = default_server();
    let max_ad = u64::try_from(DEFAULT_LOCAL_ACK_DELAY.as_micros()).unwrap();
    server
        .set_local_tparam(MinAckDelay, TransportParameter::Integer(max_ad + 1))
        .unwrap();
    let mut client = default_client();

    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    client.process_input(dgram.unwrap(), now());
    client.authenticated(AuthenticationStatus::Ok, now());
    assert_eq!(client.state().error(), Some(&EXPECTED_ERROR));
    let dgram = client.process_output(now()).dgram();

    server.process_input(dgram.unwrap(), now());
    assert_eq!(
        server.state().error(),
        Some(&CloseReason::Transport(Error::Peer(
            Error::TransportParameter.code()
        )))
    );
}

/// The server closes with `TRANSPORT_PARAMETER_ERROR` when the client sends `tp` with `value`.
fn bad_client_tp(tp: TransportParameterId, value: TransportParameter) {
    let mut client = default_client();
    let mut server = default_server();
    client.set_local_tparam(tp, value).unwrap();
    connect_fail(
        &mut client,
        &mut server,
        Error::Peer(Error::TransportParameter.code()),
        Error::TransportParameter,
    );
}

/// The client closes with `TRANSPORT_PARAMETER_ERROR` when the server sends `tp` with `value`.
fn bad_server_tp(tp: TransportParameterId, value: TransportParameter) {
    let mut client = default_client();
    // No compatible upgrade, which needs a well-formed local `version_information`.
    let mut server = new_server(
        ConnectionParameters::default().versions(Version::default(), vec![Version::default()]),
    );
    server.set_local_tparam(tp, value).unwrap();
    connect_fail(
        &mut client,
        &mut server,
        Error::TransportParameter,
        Error::Peer(Error::TransportParameter.code()),
    );
}

/// Both endpoints close with `TRANSPORT_PARAMETER_ERROR` when the peer sends `tp` with `value`.
fn bad_tp(tp: TransportParameterId, value: TransportParameter) {
    bad_client_tp(tp, value.clone());
    bad_server_tp(tp, value);
}

#[test]
fn initial_max_streams_too_large() {
    bad_tp(
        InitialMaxStreamsBidi,
        TransportParameter::Integer((1 << 60) + 1),
    );
}

#[test]
fn active_connection_id_limit_too_small() {
    bad_tp(ActiveConnectionIdLimit, TransportParameter::Integer(1));
}

#[test]
fn truncated_tp() {
    bad_tp(MaxAckDelay, TransportParameter::Bytes(vec![0x40]));
}

#[test]
fn trailing_bytes_tp() {
    bad_tp(InitialMaxData, TransportParameter::Bytes(vec![0x01, 0x00]));
}

#[test]
fn stateless_reset_token_from_client() {
    bad_client_tp(StatelessResetToken, TransportParameter::Bytes(vec![1; 16]));
}

#[test]
fn malformed_version_information() {
    bad_tp(VersionInformation, TransportParameter::Bytes(vec![0, 0, 0]));
}

/// RFC 9000, Section 7.4: a duplicate transport parameter is a `TRANSPORT_PARAMETER_ERROR`.
#[test]
fn duplicate_tp_from_client() {
    // `TestTransportParameter` (four-byte varint ID), length 1, value 1.
    const TEST_TP: &[u8] = &[0x80, 0x00, 0xce, 0x16, 0x01, 0x01];
    // `InitialMaxData` as a four-byte varint, so the rewrite keeps all lengths.
    const INITIAL_MAX_DATA: &[u8] = &[0x80, 0x00, 0x00, 0x04];

    let mut client = new_client(
        ConnectionParameters::default()
            .versions(Version::Version1, vec![Version::Version1])
            .mlkem(false)
            .sni_slicing(false),
    );
    let mut server = default_server();
    client
        .set_local_tparam(TestTransportParameter, TransportParameter::Bytes(vec![1]))
        .unwrap();

    let mut first = true;
    handshake_with_modifier(&mut client, &mut server, now(), Duration::ZERO, |c_init| {
        if !mem::take(&mut first) {
            return Some(c_init);
        }
        let (_, dcid, _, _) = decode_initial_header(&c_init, Role::Client).unwrap();
        Some(rewrite_initial(&c_init, dcid, Role::Client, |plaintext| {
            let pos = plaintext
                .windows(TEST_TP.len())
                .position(|w| w == TEST_TP)
                .unwrap();
            plaintext[pos..pos + INITIAL_MAX_DATA.len()].copy_from_slice(INITIAL_MAX_DATA);
        }))
    });
    let peer_error = Error::Peer(Error::TransportParameter.code());
    assert_error(&client, &CloseReason::Transport(peer_error));
    assert_error(&server, &CloseReason::Transport(Error::TransportParameter));
}

/// Ensure that the client probes correctly if it only receives Initial packets
/// from the server.
#[test]
fn only_server_initial() {
    let mut server = default_server();
    let mut client = default_client();
    let mut now = now();

    let client_dgram = client.process_output(now).dgram();
    let client_dgram2 = client.process_output(now).dgram();

    // Now fetch two flights of messages from the server.
    server.process_input(client_dgram.unwrap(), now);
    let server_initial1 = server.process(client_dgram2, now).dgram().unwrap();
    let server_handshake1 = server.process_output(now).dgram().unwrap();

    let server_initial2 = server.process_output(now + AT_LEAST_PTO).dgram().unwrap();
    let _server_handshake2 = server.process_output(now + AT_LEAST_PTO).dgram().unwrap();

    // The client sends an Initial ACK.
    let (s_init_1, s_hs_1) = split_datagram(&server_initial1);
    assert_eq!(client.stats().frame_tx.ack, 0);
    let probe = client.process(Some(s_init_1), now).dgram();
    assert_initial(&probe.unwrap(), false);
    assert_eq!(client.stats().dropped_rx, 0);
    assert_eq!(client.stats().frame_tx.ack, 1);

    // The same happens after a PTO.
    now += AT_LEAST_PTO;
    let (s_init_2, _s_hs_2) = split_datagram(&server_initial2);
    let probe = client.process(Some(s_init_2), now).dgram();
    assert_initial(&probe.unwrap(), false);
    assert_eq!(client.stats().frame_tx.ack, 2);
    assert_eq!(client.stats().dropped_rx, 0);

    // Pass the Handshake packet(s) and complete the handshake.
    if let Some(s_hs_1) = s_hs_1 {
        client.process_input(s_hs_1, now);
    }
    client.process_input(server_handshake1, now);
    maybe_authenticate(&mut client);
    let dgram = client.process_output(now).dgram();
    let dgram = server.process(dgram, now).dgram();
    client.process_input(dgram.unwrap(), now);

    assert_eq!(*client.state(), State::Confirmed);
    assert_eq!(*server.state(), State::Confirmed);
}

// Collect a few spare Initial packets as the handshake is exchanged.
// Later, replay those packets to see if they result in additional probes; they should not.
#[test]
fn no_extra_probes_after_confirmed() {
    let mut server = default_server();
    let mut client = default_client();
    let mut now = now();

    // First, collect a client Initial.
    let spare_initial = client.process_output(now).dgram();
    let spare_initial2 = client.process_output(now).dgram();
    assert!(spare_initial.is_some() && spare_initial2.is_some());

    // Collect ANOTHER client Initial.
    now += AT_LEAST_PTO;
    let dgram1 = client.process_output(now).dgram();
    _ = client.process_output(now).dgram();
    let (replay_initial, _) = split_datagram(dgram1.as_ref().unwrap());

    // Finally, run the handshake.
    now += AT_LEAST_PTO * 2;
    let dgram = client.process_output(now).dgram();
    let dgram2 = client.process_output(now).dgram();
    server.process_input(dgram.unwrap(), now);
    let dgram = server.process(dgram2, now).dgram();

    // The server should have dropped the Initial keys now, so passing in the Initial
    // should elicit a retransmit rather than having it completely ignored.
    let spare_handshake = server.process(Some(replay_initial), now).dgram();
    assert!(spare_handshake.is_some());

    let dgram = client.process(dgram, now).dgram();
    let dgram = server.process(dgram, now).dgram();
    client.process_input(dgram.unwrap(), now);
    maybe_authenticate(&mut client);
    let dgram = client.process_output(now).dgram();
    let dgram = server.process(dgram, now).dgram();
    client.process_input(dgram.unwrap(), now);

    assert_eq!(*client.state(), State::Confirmed);
    assert_eq!(*server.state(), State::Confirmed);

    let probe = server.process(spare_initial, now).dgram();
    assert!(probe.is_none());
    let probe = client.process(spare_handshake, now).dgram();
    assert!(probe.is_none());
}

#[test]
fn implicit_rtt_server() {
    const RTT: Duration = Duration::from_secs(2);
    let mut server = default_server();
    let mut client = default_client();
    let mut now = now();

    let dgram = client.process_output(now).dgram();
    let dgram2 = client.process_output(now).dgram();
    now += RTT / 2;
    server.process_input(dgram.unwrap(), now);
    let dgram = server.process(dgram2, now).dgram().unwrap();
    let dgram2 = server.process_output(now).dgram();
    now += RTT / 2;
    client.process_input(dgram, now);
    let dgram = client.process(dgram2, now).dgram();
    let (initial, handshake) = split_datagram(dgram.as_ref().unwrap());
    assert_initial(&initial, false);
    assert_handshake(handshake.as_ref().unwrap());
    now += RTT / 2;
    server.process_input(initial, now);

    // The server doesn't receive any acknowledgments, but it can infer
    // an RTT estimate from having discarded the Initial packet number space.
    assert_eq!(server.stats().rtt, RTT);
}

#[test]
fn emit_authentication_needed_once() {
    let mut client = default_client();

    let mut server = Connection::new_server(
        test_fixture::LONG_CERT_KEYS,
        test_fixture::DEFAULT_ALPN,
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        // Disable pacing to allow sending multiple packets without inter-packet delays.
        ConnectionParameters::default().pacing(false),
    )
    .expect("create a server");

    let client1 = client.process_output(now());
    let client2 = client.process_output(now());
    assert!(client1.as_dgram_ref().is_some() && client2.as_dgram_ref().is_some());

    // The entire server flight doesn't fit in a single packet because the
    // certificate is large, therefore the server will produce 2 packets.
    _ = server.process(client1.dgram(), now());
    let server1 = server.process(client2.dgram(), now());
    assert!(server1.as_dgram_ref().is_some());
    let server2 = server.process_output(now());
    assert!(server2.as_dgram_ref().is_some());
    let server3 = server.process_output(now());
    assert!(server3.as_dgram_ref().is_some());

    let authentication_needed_count = |client: &mut Connection| {
        client
            .events()
            .filter(|e| matches!(e, ConnectionEvent::AuthenticationNeeded))
            .count()
    };

    // Upon receiving the first two packet, the client has the server certificate,
    // but not yet all required handshake data. It moves to
    // `HandshakeState::AuthenticationPending` and emits a
    // `ConnectionEvent::AuthenticationNeeded` event.
    //
    // Note that this is a tiny bit fragile in that it depends on having a certificate
    // that is within a fairly narrow range of sizes.  It has to fit in a single
    // packet, but be large enough that the CertificateVerify message does not
    // also fit in the same packet.  Our default test setup achieves this, but
    // changes to the setup might invalidate this test.
    _ = client.process(server1.dgram(), now());
    _ = client.process(server2.dgram(), now());
    assert_eq!(1, authentication_needed_count(&mut client));
    assert!(client.peer_certificate().is_some());

    // The `AuthenticationNeeded` event is still pending a call to
    // `Connection::authenticated`. On receiving the second packet from the
    // server, the client must not emit a another
    // `ConnectionEvent::AuthenticationNeeded`.
    _ = client.process(server3.dgram(), now());
    assert_eq!(0, authentication_needed_count(&mut client));
}

#[test]
fn client_initial_retransmits_identical() {
    let mut now = now();
    // Disable pacing so the PTO timer is the only callback, simplifying assertions.
    let mut client = new_client(ConnectionParameters::default().pacing(false));

    // Force the client to retransmit its Initial flight a number of times and make sure the
    // retranmissions are identical to the original. Also, verify the PTO durations.
    for i in 1..=5 {
        let ci = client.process_output(now).dgram().unwrap();
        assert_eq!(ci.len(), client.plpmtu());
        let ci2 = client.process_output(now).dgram().unwrap();
        assert_eq!(ci2.len(), client.plpmtu());
        assert_eq!(
            client.stats().frame_tx,
            FrameStats {
                crypto: 3 * i,
                ..Default::default()
            }
        );
        let pto = client.process_output(now).callback();
        assert_eq!(pto, DEFAULT_RTT * 3 * (1 << (i - 1)));
        now += pto;
    }
}

#[test]
fn client_initial_pto_matches_custom_initial_rtt() {
    let custom_initial_rtt = Duration::from_millis(500);
    let now = now();
    let mut client = new_client(
        ConnectionParameters::default()
            .initial_rtt(custom_initial_rtt)
            .pacing(false),
    );

    let ci = client.process_output(now).dgram().unwrap();
    assert_eq!(ci.len(), client.plpmtu());
    let ci2 = client.process_output(now).dgram().unwrap();
    assert_eq!(ci2.len(), client.plpmtu());
    assert_eq!(
        client.stats().frame_tx,
        FrameStats {
            crypto: 3,
            ..Default::default()
        }
    );
    let pto = client.process_output(now).callback();
    assert_eq!(pto, custom_initial_rtt * 3);
}

#[test]
fn server_initial_retransmits_identical() {
    let mut now = now();
    // We calculate largest_acked, which is difficult with packet number randomization.
    let mut client = new_client(ConnectionParameters::default().randomize_first_pn(false));
    let ci = client.process_output(now).dgram().unwrap();
    let ci2 = client.process_output(now).dgram().unwrap();

    // Force the server to retransmit its Initial flight a number of times and make sure the
    // retranmissions are identical to the original. Also, verify the PTO durations.
    let mut server = new_server(ConnectionParameters::default().pacing(false));
    server.process_input(ci, now);
    server.process_input(ci2, now);

    let mut total_ptos = Duration::from_secs(0);
    // Count for any extra packets in each flight due to coalescing.
    let mut extra = 0;
    for i in 1..=3 {
        println!("==== iteration {i} ====");
        let d1 = server.process_output(now).dgram().unwrap();
        if let (_, Some(dh)) = split_datagram(&d1) {
            extra += usize::from(dh[0] & 0b1011_0000 == 0b1010_0000); // count extra Handshake
        }
        let d2 = server.process_output(now).dgram().unwrap();
        extra += usize::from(d2[0] & 0b1011_0000 == 0b1001_0000); // count extra Initial
        assert_eq!(
            server.stats().frame_tx,
            FrameStats {
                // base count for CRYPTO is two per flight, plus any extra
                crypto: i * 2 + extra,
                ack: i,
                largest_acknowledged: to_u64(i - i.saturating_sub(1)),
                ..Default::default()
            }
        );

        let pto = server.process_output(now).callback();
        now += pto;
        total_ptos += pto;
    }

    // Server is amplification-limited now.
    let pto = server.process_output(now).callback();
    assert_eq!(
        pto,
        server
            .conn_params
            .get_idle_timeout()
            .checked_sub(total_ptos)
            .expect("doesn't underflow")
    );
}

/// PTOs count against the anti-amplification limit (RFC 9002, Section 6.2.2.1).
#[test]
fn server_pto_respects_amplification_limit() {
    let mut now = now();
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().pacing(false));
    let c1 = client.process_output(now).dgram().unwrap();
    let c2 = client.process_output(now).dgram().unwrap();
    // Discarded datagrams count too (RFC 9000, Section 8.1); this leaves PTO 3 short of two.
    let junk = Datagram::new(c1.source(), c1.destination(), c1.tos(), &c1[..400]);
    let budget = 3 * (c1.len() + c2.len() + junk.len());
    for d in [c1, c2, junk] {
        server.process_input(d, now);
    }

    // The datagram sizes the server sends now, and its timer after them.
    let send = |server: &mut Connection, mut now| {
        let mut sizes = Vec::new();
        let Output::Callback(t) = drain(server, &mut now, |d, _| sizes.push(d.len())) else {
            panic!("the server should have a timer");
        };
        (sizes, t)
    };
    let mtu = server.plpmtu();
    let pto = 3 * DEFAULT_INITIAL_RTT; // RTT plus four times its initial variance, half the RTT.

    let (first, mut timer) = send(&mut server, now);
    assert_eq!((first.len(), timer), (2, pto));
    for _ in 0..2 {
        now += timer;
        timer *= 2;
        assert_eq!(send(&mut server, now), (vec![mtu, mtu], timer));
    }
    now += timer;

    // A full probe, then the rest of the budget as Handshake only.
    let mut last = Vec::new();
    let Output::Callback(timer) = drain(&mut server, &mut now, |d, _| last.push(d)) else {
        panic!("the server should have a timer");
    };
    let rest = budget - first.iter().sum::<usize>() - 5 * mtu;
    assert_eq!(
        last.iter().map(Datagram::len).collect::<Vec<_>>(),
        vec![mtu, rest]
    );
    assert_handshake(&last[1]);
    // Amplification-limited, so only the idle timer is left.
    assert_eq!(timer + 7 * pto, server.conn_params.get_idle_timeout());
}

/// An overdue PTO does not fire while amplification-limited (RFC 9002, Section 6.2.2.1).
#[test]
fn server_overdue_pto_amplification_limited() {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().pacing(false));
    // The server cannot complete its flight within the amplification limit.
    let very_big =
        TransportParameter::Bytes(vec![0; 10 * Pmtud::default_plpmtu(DEFAULT_ADDR.ip())]);
    server
        .set_local_tparam(TestTransportParameter, very_big)
        .unwrap();
    let now = now();
    server.process_input(client.process_output(now).dgram().unwrap(), now);
    server.process_input(client.process_output(now).dgram().unwrap(), now);
    while server.process_output(now).dgram().is_some() {}

    let idle = server.conn_params.get_idle_timeout();
    assert_eq!(server.process_output(now).callback(), idle);
    let half_idle = idle / 2;
    assert_eq!(
        server.process_output(now + half_idle).callback(),
        idle.checked_sub(half_idle).unwrap()
    );
}

/// ACK-only packets count against the anti-amplification limit (RFC 9000, Section 8.1).
#[cfg(not(feature = "disable-encryption"))]
#[test]
fn server_ack_only_respects_amplification_limit() {
    use neqo_common::Encoder;

    use crate::{
        crypto::{CryptoDxDirection, CryptoDxState},
        frame::FrameType,
        packet,
    };

    // A shortest (8-byte) original Destination CID keeps the forged packets small.
    let (client, c1) = loop {
        let mut client = new_client(ConnectionParameters::default().mlkem(false));
        let c1 = client.process_output(now()).dgram().unwrap();
        if c1[5] == 8 {
            break (client, c1);
        }
    };
    let mut server = new_server(ConnectionParameters::default().pacing(false));
    // Skip the first byte and version; the original Destination CID seeds the Initial keys.
    let seed = Decoder::from(&c1[5..]).decode_vec(1).unwrap();
    let version = client.version();
    let mut received = c1.len();
    server.process_input(c1.clone(), now());
    let mut sent = 0;

    for i in 1..=20 {
        while let Some(d) = server.process_output(now()).dgram() {
            sent += d.len();
        }
        let limit = server
            .paths
            .primary()
            .unwrap()
            .borrow()
            .amplification_limit();
        assert_eq!(3 * received, sent + limit);

        // A small forged client Initial PING; the packet number gap makes each ACK range large.
        let pn = i * ((1 << 30) + 2);
        let mut write =
            CryptoDxState::new_initial(version, CryptoDxDirection::Write, "client in", seed, pn)
                .unwrap();
        let mut builder = packet::Builder::long(
            Encoder::default(),
            packet::Type::Initial,
            version,
            Some(seed),
            Some([]),
            packet::LIMIT,
        );
        builder.initial_token(&[]);
        builder.pn(pn, 4);
        builder.encode_varint(FrameType::Ping);
        let ping = builder.build(&mut write).unwrap();
        received += ping.len();
        server.process_input(
            Datagram::new(c1.source(), c1.destination(), c1.tos(), ping),
            now(),
        );
    }
}

#[test]
fn grease_quic_bit_transport_parameter() {
    fn get_remote_tp(conn: &Connection) -> bool {
        conn.tps
            .borrow()
            .remote_handshake()
            .as_ref()
            .unwrap()
            .get_empty(GreaseQuicBit)
    }

    for client_grease in [true, false] {
        for server_grease in [true, false] {
            let mut client = new_client(ConnectionParameters::default().grease(client_grease));
            let mut server = new_server(ConnectionParameters::default().grease(server_grease));

            connect(&mut client, &mut server);

            assert_eq!(client_grease, get_remote_tp(&server));
            assert_eq!(server_grease, get_remote_tp(&client));
        }
    }
}

#[test]
fn zero_rtt_with_ech() {
    let mut server = default_server();
    let (sk, pk) = generate_ech_keys().unwrap();
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();

    let mut client = default_client();
    client.client_enable_ech(server.ech_config()).unwrap();

    connect(&mut client, &mut server);

    assert!(client.tls_info().unwrap().ech_accepted());
    assert!(server.tls_info().unwrap().ech_accepted());

    let token = exchange_ticket(&mut client, &mut server, now());
    let mut client = default_client();
    client.client_enable_ech(server.ech_config()).unwrap();
    client
        .enable_resumption(now(), token)
        .expect("should set token");

    let mut server = resumed_server(&client);
    server
        .server_enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
        .unwrap();

    connect(&mut client, &mut server);
    assert!(client.tls_info().unwrap().ech_accepted());
    assert!(server.tls_info().unwrap().ech_accepted());
    assert!(client.tls_info().unwrap().early_data_accepted());
    assert!(server.tls_info().unwrap().early_data_accepted());
}

fn scone(enable: bool) {
    const PERIOD: Duration = crate::scone::Scone::PERIOD;

    fn add_scone(d: &Datagram, signal: u8) -> Datagram {
        const SCONE: &[u8] = &[0xff, 0x6f, 0x7d, 0xc0, 0xfd, 0x00, 0x00];
        let mut sconed = SCONE.to_vec();
        sconed[0] = 0b1100_0000 | ((signal >> 1) & 0x3f);
        sconed[1] |= (signal << 7) & 0x80;
        sconed.extend_from_slice(&d[..]);
        Datagram::new(d.source(), d.destination(), d.tos(), sconed)
    }
    let got_scone = |e| matches!(e, ConnectionEvent::SconeUpdated(_));

    // This test needs to keep connections alive long past the default idle timeout.
    let mut client = new_client(
        ConnectionParameters::default()
            .idle_timeout(PERIOD * 10)
            .scone(enable),
    );
    let mut server = new_server(
        ConnectionParameters::default()
            .idle_timeout(PERIOD * 10)
            .scone(enable),
    );
    let mut now = now();

    let ci = client.process_output(now).dgram().unwrap();
    let ci_len = ci.len();
    if enable {
        assert_eq!(
            &ci[ci_len - Connection::SCONE_INDICATION.len()..],
            Connection::SCONE_INDICATION,
            "Client should send indication"
        );
    } else {
        assert_ne!(
            &ci[ci_len - Connection::SCONE_INDICATION.len()..],
            Connection::SCONE_INDICATION,
            "Client should not send indication when SCONE is disabled"
        );
    }
    server.process_input(ci, now);

    connect(&mut client, &mut server);
    assert_eq!(client.tps.borrow_mut().remote().get_empty(Scone), enable);
    assert_eq!(server.tps.borrow_mut().remote().get_empty(Scone), enable);

    let client_stats = client.stats();
    let server_stats = server.stats();
    let d = send_something(&mut client, now);
    server.process_input(add_scone(&d, 0x7f), now);
    assert!(!server.events().any(got_scone), "no event for unknown");
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x31), now);
    assert!(client.events().any(got_scone));

    // The SCONE packets are effectively invisible.
    assert_eq!(server.stats().packets_rx, server_stats.packets_rx + 1);
    assert_eq!(client.stats().packets_rx, client_stats.packets_rx + 1);

    // Now check that events are correctly generated (or not).

    // A duplicate packet means no event, even with a rate decrease.
    client.process_input(add_scone(&d, 0x2), now);
    assert!(!client.events().any(got_scone));

    // A repeated signal means no event.
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x31), now);
    assert!(!client.events().any(got_scone));

    // A noop signal means no event.
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x7f), now);
    assert!(!client.events().any(got_scone));

    // A higher signal means no event.
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x42), now);
    assert!(!client.events().any(got_scone));

    // A lower signal generates an event.
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x17), now);
    assert!(client.events().any(got_scone));

    // Elapsed time results in an event, even with a rate increase.
    now += PERIOD;
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x42), now);
    assert!(client.events().any(got_scone));

    // Same for a shift to the unknown rate,
    // which doesn't require a SCONE packet.
    now += PERIOD;
    let d = send_something(&mut server, now);
    client.process_input(d, now);
    assert!(client.events().any(got_scone));

    // No event when a SCONE packet with unknown rate is immediately received.
    let d = send_something(&mut server, now);
    client.process_input(add_scone(&d, 0x7f), now);
    assert!(!client.events().any(got_scone));
}

#[test]
fn scone_enabled() {
    scone(true);
}

/// With the connection parameter disabled, the client does not send the SCONE
/// indicator in Initial padding and does not advertise the SCONE transport
/// parameter. Inbound SCONE packets are still tolerated but do not generate
/// `SconeUpdated` events, since neither peer has negotiated the feature.
#[test]
fn scone_disabled() {
    scone(false);
}

/// RFC 9287 Section 3.1 states: "A server MUST NOT remember that a client negotiated
/// the extension in a previous connection and set the QUIC Bit to 0 based on that information."
///
/// This test verifies that the client complies with RFC 9287 Section 3.1 by ensuring
/// it does not grease the QUIC Bit based on cached (0-RTT) transport parameters.
/// Regression test for the `handshakeloss` interop test failure, where client Initial
/// packets with the fixed bit cleared (due to cached parameters) were discarded by the server.
#[test]
fn grease_quic_bit_respects_current_handshake() {
    fixture_init();

    // Create a client connection.
    let client = Connection::new_client(
        test_fixture::DEFAULT_SERVER_NAME,
        test_fixture::DEFAULT_ALPN,
        Rc::new(RefCell::new(EmptyConnectionIdGenerator::default())),
        DEFAULT_ADDR,
        DEFAULT_ADDR,
        ConnectionParameters::default(),
        now(),
    )
    .unwrap();

    // Simulate having cached 0-RTT transport parameters that include grease_quic_bit.
    // In reality, this would come from a previous connection's session ticket.
    let mut tp = crate::tparams::TransportParameters::default();
    tp.set_empty(GreaseQuicBit);
    client.tps.borrow_mut().set_remote_0rtt(Some(tp));

    // At this point:
    // - We have remote_0rtt params with GreaseQuicBit
    // - We do NOT have remote_handshake params (no current handshake confirmation)

    // With only cached 0-RTT params, no greasing is allowed.
    assert!(
        !client.can_grease_quic_bit(),
        "Must not grease with only cached 0-RTT params (RFC 9287 Section 3.1)"
    );
}

#[test]
fn certificate_compression() {
    use std::sync::Mutex;

    use nss::cert::CertificateCompressor;

    // These statics work for concurrent test execution because the certificate is
    // effectively a fixed value. A more robust approach would use a hash-based lookup,
    // but that's unnecessary given the current test setup.
    static ORIGINAL: Mutex<Vec<u8>> = Mutex::new(Vec::new());
    static DECODED: Mutex<Vec<u8>> = Mutex::new(Vec::new());

    struct Xor;
    impl CertificateCompressor for Xor {
        const ID: u16 = 0x1234;
        const NAME: &std::ffi::CStr = c"xor";
        const ENABLE_ENCODING: bool = true;
        fn decode(input: &[u8], output: &mut [u8]) -> nss::Res<()> {
            output
                .iter_mut()
                .zip(input)
                .for_each(|(o, &i)| *o = i ^ 0xAA);
            *DECODED.lock().unwrap() = output[..input.len()].to_vec();
            Ok(())
        }
        fn encode(input: &[u8], output: &mut [u8]) -> nss::Res<usize> {
            *ORIGINAL.lock().unwrap() = input.to_vec();
            output
                .iter_mut()
                .zip(input)
                .for_each(|(o, &i)| *o = i ^ 0xAA);
            Ok(input.len())
        }
    }

    let mut client = default_client();
    client.set_certificate_compression::<Xor>().unwrap();
    let mut server = default_server();
    server.set_certificate_compression::<Xor>().unwrap();
    connect(&mut client, &mut server);

    assert!(!ORIGINAL.lock().unwrap().is_empty());
    assert_eq!(*ORIGINAL.lock().unwrap(), *DECODED.lock().unwrap());
}

/// Test that Initial CRYPTO can be retransmitted even when PTO fires for Handshake space.
///
/// This reproduces the bug from QNS L1/C1 test failures where:
/// 1. Client sends `ClientHello` split across multiple Initial packets
/// 2. Server receives first packet but second is lost
/// 3. Server ACKs what it received
/// 4. Client detects loss and retransmits, but packets keep getting lost
/// 5. Initial PTO fires, which primes the Handshake PTO timer
/// 6. Handshake PTO fires
/// 7. BUG: `ack_only(Initial)` returns true, blocking CRYPTO retransmission
/// 8. Client cannot complete handshake, times out
///
/// RFC 9002 Section 6.2.4 requires sending probes in packet number spaces
/// with in-flight data. The client must be able to retransmit lost Initial
/// CRYPTO frames even when PTO fires for Handshake space.
#[test]
fn initial_crypto_retransmit_during_handshake_pto() {
    let mut now = now();

    // Use default client which has MLKEM enabled, causing CRYPTO to be split
    // across multiple Initial packets.
    let mut client = new_client(ConnectionParameters::default().pacing(false));
    let mut server = new_server(ConnectionParameters::default().pacing(false));

    // Client sends Initial packets. With MLKEM, this will be 2 packets.
    let c_init_1 = client.process_output(now).dgram().unwrap();
    let c_init_2 = client.process_output(now).dgram().unwrap();
    assert_initial(&c_init_1, false);
    assert_initial(&c_init_2, false);

    // Record the initial CRYPTO frame count. With default settings (MLKEM + SNI slicing),
    // the ClientHello is split: 2 CRYPTO frames in the first packet, 1 in the second.
    let crypto_before = client.stats().frame_tx.crypto;
    assert_eq!(crypto_before, 3);

    // Deliver only the FIRST Initial packet to server. The second is "lost".
    now += DEFAULT_RTT / 2;
    server.process_input(c_init_1, now);

    // Server sends ACK for what it received (incomplete ClientHello).
    // Server is waiting for more CRYPTO data to complete the ClientHello.
    let s_ack = server.process_output(now).dgram();
    assert!(s_ack.is_some(), "Server should ACK the partial Initial");

    // Deliver server's ACK to client.
    now += DEFAULT_RTT / 2;
    client.process_input(s_ack.unwrap(), now);

    // Client should detect that c_init_2 was lost (server ACKed c_init_1 but not c_init_2).
    // The client is now in a state where:
    // - It has received an Initial ACK (knows peer is alive)
    // - It still has lost Initial CRYPTO data to retransmit
    // - Handshake space may get primed for PTO

    // Fire PTOs multiple times. Without further ACKs, the PTO mechanism must
    // continue to allow CRYPTO retransmission even when Handshake PTO fires.
    //
    // The bug manifests when:
    // 1. Initial PTO fires, priming Handshake PTO
    // 2. Handshake PTO fires (before Initial PTO, since Initial keeps sending)
    // 3. Without the fix: Initial packets aren't marked for retransmission, and ack_only(Initial)
    //    blocks CRYPTO frames
    for pto_count in 1..=5 {
        now += client.process_output(now).callback();

        let crypto_before_pto = client.stats().frame_tx.crypto;

        // Collect all packets sent on this PTO.
        let mut packets_sent = 0;
        while client.process_output(now).dgram().is_some() {
            packets_sent += 1;
        }

        let crypto_after_pto = client.stats().frame_tx.crypto;

        // The client MUST send packets on PTO.
        assert!(
            packets_sent > 0,
            "PTO {pto_count}: Client should send packets on PTO"
        );

        // The client MUST include CRYPTO frames, not just PINGs/ACKs.
        assert!(
            crypto_after_pto > crypto_before_pto,
            "PTO {pto_count}: Client must retransmit CRYPTO frames, not just ACKs/PINGs. \
             CRYPTO frames before: {crypto_before_pto}, after: {crypto_after_pto}, \
             packets sent: {packets_sent}"
        );
    }
}

#[test]
fn export_keying_material_basic() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let mut material = vec![0u8; 32];
    client
        .export_keying_material("EXPORTER-WebTransport", &[], &mut material)
        .expect("export should succeed after handshake");
    assert_ne!(material, vec![0u8; 32]);
}

#[test]
fn export_keying_material_same_both_sides() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let label = "EXPORTER-WebTransport";
    let context = b"session-context";

    let mut client_material = vec![0u8; 32];
    client
        .export_keying_material(label, context, &mut client_material)
        .expect("client export should succeed");
    let mut server_material = vec![0u8; 32];
    server
        .export_keying_material(label, context, &mut server_material)
        .expect("server export should succeed");

    assert_eq!(
        client_material, server_material,
        "client and server must export identical keying material"
    );
}

#[test]
fn export_keying_material_before_handshake() {
    let client = default_client();
    let result = client.export_keying_material("EXPORTER-WebTransport", &[], &mut [0u8; 32]);
    assert!(matches!(result, Err(Error::NotConnected)));
}

#[test]
fn export_keying_material_zero_length() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    assert!(matches!(
        client.export_keying_material("EXPORTER-WebTransport", &[], &mut []),
        Err(Error::InvalidInput)
    ));
}

#[test]
fn export_keying_material_empty_label() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    assert!(matches!(
        client.export_keying_material("", &[], &mut [0u8; 32]),
        Err(Error::InvalidInput)
    ));
}

// Export should succeed while the connection is closing or draining.
#[test]
fn export_keying_material_while_closing() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    client.close(now(), 0, "");
    assert!(client.state().closing());

    let mut material = vec![0u8; 32];
    client
        .export_keying_material("EXPORTER-WebTransport", &[], &mut material)
        .expect("export should succeed while closing");

    let close_pkt = client.process_output(now()).dgram().unwrap();
    server.process_input(close_pkt, now());
    assert!(server.state().closing());

    let mut material = vec![0u8; 32];
    server
        .export_keying_material("EXPORTER-WebTransport", &[], &mut material)
        .expect("export should succeed while draining");
}

// Export should fail once the connection is fully closed.
#[test]
fn export_keying_material_after_closed() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    client.close(now(), 0, "");
    let close_pkt = client.process_output(now()).dgram();
    let close_timer = client.process_output(now()).callback();
    drop(client.process_output(now() + close_timer));
    assert!(matches!(*client.state(), State::Closed(..)));

    assert!(matches!(
        client.export_keying_material("EXPORTER-WebTransport", &[], &mut [0u8; 32]),
        Err(Error::NotConnected)
    ));
    let _server_close = server.process(close_pkt, now()).dgram();
    let drain_timer = server.process_output(now()).callback();
    drop(server.process_output(now() + drain_timer));
    assert!(matches!(*server.state(), State::Closed(..)));
    assert!(matches!(
        server.export_keying_material("EXPORTER-WebTransport", &[], &mut [0u8; 32]),
        Err(Error::NotConnected)
    ));
}

/// The Initial that `c` sends first, as bytes.
#[cfg(not(feature = "disable-encryption"))]
fn client_initial(c: &mut Connection) -> Vec<u8> {
    c.process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec()
}

/// Read the DCID and SCID from the client's Initial, past the first byte and the 4-byte version.
#[cfg(not(feature = "disable-encryption"))]
fn initial_cids(initial: &[u8]) -> (Vec<u8>, Vec<u8>) {
    let mut dec = Decoder::from(&initial[5..]);
    let dcid = dec.decode_vec(1).expect("client DCID").to_vec();
    let scid = dec.decode_vec(1).expect("client SCID").to_vec();
    assert!(!dcid.is_empty(), "client DCID is non-empty");
    assert!(!scid.is_empty(), "client SCID is non-empty");
    (dcid, scid)
}

/// Build a Retry that the client which sent `initial` will accept.
#[cfg(not(feature = "disable-encryption"))]
fn retry_for(initial: &[u8]) -> Datagram {
    let (dcid, scid) = initial_cids(initial);
    let mut server_scid = dcid.clone();
    server_scid[0] ^= 0xff;
    datagram(
        packet::Builder::retry(Version::default(), &scid, &server_scid, &[0x01], &dcid)
            .expect("build retry"),
    )
}

/// RFC 9000, Section 17.2.5.2: a client MUST discard a Retry packet whose Source
/// Connection ID is identical to the Destination Connection ID of its Initial. A
/// Retry with a distinct Source Connection ID is still processed.
#[cfg(not(feature = "disable-encryption"))]
#[test]
fn retry_scid_matching_initial_dcid() {
    // A Retry whose Source Connection ID equals our Initial's Destination
    // Connection ID is dropped, leaving the client in its initial state.
    let mut client = default_client();
    let initial = client_initial(&mut client);
    let (dcid, scid) = initial_cids(&initial);
    let retry = packet::Builder::retry(
        Version::default(),
        &scid,   // Destination CID: copied from the client as required
        &dcid,   // Source CID: copied from client (bad!)
        &[0x01], // non-empty token
        &dcid,   // as required: a seed for authenticating the Retry
    )
    .expect("build retry");
    drop(client.process(Some(datagram(retry)), now()));
    assert_eq!(client.stats().dropped_rx, 1);
    assert_eq!(*client.state(), State::WaitInitial);

    // The same Retry with a distinct Source Connection ID is accepted.
    let mut client = default_client();
    let initial = client_initial(&mut client);
    drop(client.process(Some(retry_for(&initial)), now()));
    assert_eq!(client.stats().dropped_rx, 0);
}

/// RFC 9000, Section 17.2.2: a server MUST set the Token Length field of an Initial
/// to 0; a client that receives an Initial with a non-zero Token Length MUST discard
/// it. A token-free server Initial is still processed.
#[cfg(not(feature = "disable-encryption"))]
#[test]
fn client_initial_with_token() {
    use super::forged_server_initial;
    use crate::frame::FrameType;

    // A server Initial addressed to the client, encrypted with the Initial keys
    // seeded by the client's original Destination CID, carrying `token` and a PING.
    fn server_initial(client_initial: &[u8], token: &[u8]) -> Vec<u8> {
        let ping = [FrameType::Ping.into()]; // PING, so the packet is ack-eliciting
        forged_server_initial(client_initial, Version::default(), 0, token, &ping)
    }

    // A token-free server Initial decrypts and is processed (also confirming the
    // forged packet above is well-formed).
    let mut client = default_client();
    let ci = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    let dropped = client.stats().dropped_rx;
    client.process_input(datagram(server_initial(&ci, &[])), now());
    assert_eq!(client.stats().dropped_rx, dropped);

    // The same Initial carrying a non-empty token is discarded.
    let mut client = default_client();
    let ci = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    let dropped = client.stats().dropped_rx;
    client.process_input(datagram(server_initial(&ci, &[0x01])), now());
    assert_eq!(client.stats().dropped_rx, dropped + 1);
}

#[test]
fn received_packet_reports_its_own_cids() {
    let mut client = default_client();
    let (mut server, contents) = new_server_with_qlog(ConnectionParameters::default());
    let ci = client.process_output(now()).dgram().expect("a datagram");
    let mut dec = Decoder::from(&ci[5..]); // Skip past version.
    let cids = ["dcid", "scid"]
        .map(|name| format!(r#""{name}":"{}""#, Hex::new(dec.decode_vec(1).expect(name))));

    server.process_input(ci, now());
    drop(server);

    // The client's random DCID, not the one the server chose for its path.
    let trace = contents.to_string();
    let packet = trace
        .lines()
        .find(|l| l.contains(r#""name":"quic:packet_received""#))
        .expect("a packet_received event");
    for cid in &cids {
        assert!(packet.contains(cid), "{trace}");
    }
}

/// Produce a datagram with one byte of CRYPTO at offset 0, added to every packet in it. Whether
/// that is a duplicate depends on what the receiver already holds. Waits out any pacing delay.
fn make_dummy_crypto(c: &mut Connection, now: &mut Instant) -> Datagram {
    next_dgram(now, |t| c.test_write_frames(CryptoWriter {}, t)).expect("a datagram")
}

/// Deliver `dgram` to `c` and return how many CRYPTO frames it sends in response.
fn crypto_sent_for(c: &mut Connection, dgram: Datagram, now: &mut Instant) -> usize {
    let before = c.stats().frame_tx.crypto;
    c.process_input(dgram, *now);
    drop_flight(c, now);
    c.stats().frame_tx.crypto - before
}

/// Duplicate CRYPTO in an Initial makes an endpoint resend its own unacknowledged CRYPTO ahead of
/// the PTO, per Section 6.2.3 of RFC 9002 -- but only once per connection.
#[test]
fn crypto_resent_at_most_once() {
    fixture_init();
    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();

    // The server gets the whole ClientHello and answers, leaving its own CRYPTO unacknowledged.
    flush_to(&mut client, &mut server, &mut now);
    drop_flight(&mut server, &mut now);

    // The first duplicate resends the server's flight.
    now += DEFAULT_RTT;
    let dup = make_dummy_crypto(&mut client, &mut now);
    assert!(crypto_sent_for(&mut server, dup, &mut now) >= 2);

    // The second changes nothing.
    now += DEFAULT_RTT;
    let dup = make_dummy_crypto(&mut client, &mut now);
    assert_eq!(crypto_sent_for(&mut server, dup, &mut now), 0);
}

/// Only duplicate CRYPTO in an *Initial* is a sign of loss, per Section 6.2.3 of RFC 9002.
#[test]
fn crypto_resent_only_for_initial() {
    fixture_init();
    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();

    // Bring the client to Handshake keys and flush its Finished without delivering it.
    flush_to(&mut client, &mut server, &mut now);
    flush_to(&mut server, &mut client, &mut now);
    assert!(maybe_authenticate(&mut client));
    drop_flight(&mut client, &mut now);

    // Deliver only the Handshake copy, a pure duplicate: the Initial copy is the trigger under
    // test and the 1-RTT copy is new data. The client must not resend its Finished.
    let dup = make_dummy_crypto(&mut server, &mut now);
    let (_initial, rest) = split_datagram(&dup);
    let (handshake, _short) = split_datagram(&rest.expect("a coalesced Handshake packet"));
    assert_handshake(&handshake);
    assert_eq!(crypto_sent_for(&mut client, handshake, &mut now), 0);
    assert!(!client.crypto.early_resend_used(), "allowance untouched");
}

/// A Retry restarts the connection process, so the allowance is available again for the new flight.
#[cfg(not(feature = "disable-encryption"))]
#[test]
fn retry_restores_resend_allowance() {
    let mut client = default_client();
    let initial = client_initial(&mut client);
    let retry = retry_for(&initial);

    client.crypto.resend_unacked_early();
    assert!(client.crypto.early_resend_used(), "allowance spent");
    client.process_input(retry, now());
    assert!(!client.crypto.early_resend_used(), "and available again");

    // ...and the re-sent ClientHello is something to spend it on.
    assert!(client.process_output(now()).dgram().is_some());
    client.crypto.resend_unacked_early();
    assert!(client.crypto.early_resend_used(), "spent again");
}

/// A duplicate that arrives while nothing is outstanding has nothing to resend, so it must not use
/// up the one allowance an endpoint gets to resend ahead of the PTO.
#[test]
fn crypto_resend_chance_survives_useless_duplicate() {
    fixture_init();
    let mut client = default_client();
    let mut server = default_server();
    let mut now = now();

    // Only the first ClientHello datagram; SNI slicing puts offset 0 in it, but not the rest.
    let ch1 = client.process_output(now).dgram().expect("a datagram");
    let ch2 = client.process_output(now).dgram().expect("a datagram");
    server.process_input(ch1, now);

    // The dummy byte at offset 0 is therefore a duplicate, and finds nothing to resend.
    let dup = make_dummy_crypto(&mut client, &mut now);
    assert_eq!(crypto_sent_for(&mut server, dup, &mut now), 0);

    // Complete the ClientHello and drop the server's answer, leaving its CRYPTO unacknowledged.
    server.process_input(ch2, now);
    drop_flight(&mut server, &mut now);

    // The allowance was not used above, so this duplicate resends the whole flight.
    now += DEFAULT_RTT;
    let dup = make_dummy_crypto(&mut client, &mut now);
    assert!(crypto_sent_for(&mut server, dup, &mut now) >= 2);
}

/// Returns a client whose next datagram carries Initial, Handshake, and 1-RTT packets, and its
/// server.
fn client_coalescing_1rtt(
    server_params: ConnectionParameters,
) -> (Connection, Connection, Instant) {
    // No NEW_CONNECTION_ID frames, so the 1-RTT packet carries only an ACK.
    let mut client = zero_len_cid_client(DEFAULT_ADDR, DEFAULT_ADDR);
    let mut server = new_server(server_params);

    let c1 = client.process_output(now()).dgram();
    let c2 = client.process_output(now()).dgram();
    server.process_input(c1.unwrap(), now());
    let s1 = server.process(c2, now()).dgram().unwrap();

    // Two 0.5-RTT packets, so the client acknowledges them immediately.
    let s2 = send_something(&mut server, now());
    let s3 = send_something(&mut server, now() + DEFAULT_RTT);
    let t = now() + DEFAULT_RTT;
    client.process_input(s1, t);
    client.process_input(s2, t);
    client.process_input(s3, t);
    assert!(maybe_authenticate(&mut client));
    (client, server, t)
}

/// Asserts that `server` decrypts every packet in `dgram`.
fn assert_decrypts(server: &mut Connection, dgram: Datagram, now: Instant) {
    let dropped = server.stats().dropped_rx;
    server.process_input(dgram, now);
    assert_eq!(server.stats().dropped_rx, dropped);
}

/// An Initial datagram that ends in a short header packet is expanded inside that packet.
#[test]
fn expand_datagram_that_ends_in_short_header() {
    let (mut client, mut server, t) = client_coalescing_1rtt(ConnectionParameters::default());

    let c3 = client.process_output(t).dgram().unwrap();
    assert_eq!(c3.len(), client.plpmtu()); // Padded for the Initial.
    let (_, rest) = split_datagram(&c3);
    let rest = rest.expect("Handshake is coalesced");
    let (_, short) = split_datagram(&rest);
    let short = short.expect("a 1-RTT packet is coalesced");
    assert_eq!(short[0] & 0x80, 0); // Ends in a short header packet.
    assert_eq!(client.stats().frame_tx.padding, 1); // Inside the 1-RTT packet.
    assert_eq!(client.stats().cc.bytes_in_flight, rest.len()); // Handshake and 1-RTT in flight.
    assert_decrypts(&mut server, c3, t);
}

/// Fills the third packet it writes to, short of its limit, and marks it full.
struct FillShortWriter(usize);

impl test_internal::FrameWriter for FillShortWriter {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        self.0 += 1;
        if self.0 == 3 {
            while builder.remaining() > 2 {
                builder.encode_varint(FrameType::Ping);
            }
            builder.mark_full();
        }
    }
}

/// Nothing is added after a short header packet that is full short of its limit.
#[test]
fn no_padding_after_full_short_header() {
    let (mut client, mut server, t) = client_coalescing_1rtt(ConnectionParameters::default());
    client.test_frame_writer = Some(Box::new(FillShortWriter(0)));
    let c3 = client.process_output(t).dgram().unwrap();
    assert_eq!(c3.len(), client.plpmtu() - 2);
    assert_decrypts(&mut server, c3, t);
}

/// A STREAM frame without a length that falls short of the end of the packet is padded before.
#[test]
fn expand_datagram_that_ends_in_short_stream_frame() {
    // A fixed first packet number keeps the size of the client's ACKs the same.
    let params = ConnectionParameters::default().randomize_first_pn(false);

    // Learn how much stream data fills the 1-RTT packet.
    let (mut client, mut server, t) = client_coalescing_1rtt(params.clone());
    let id = client.stream_create(StreamType::UniDi).unwrap();
    let mut buf = [0; 2000];
    client.stream_send(id, &buf).unwrap();
    server.process_input(client.process_output(t).dgram().unwrap(), t);
    let (fill, _) = server.stream_recv(id, &mut buf).unwrap();

    // One byte less is still sent without a length, as there is no room for one.
    let (mut client, mut server, t) = client_coalescing_1rtt(params);
    let id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(id, &buf[..fill - 1]).unwrap();
    let c3 = client.process_output(t).dgram().unwrap();
    assert_eq!(c3.len(), client.plpmtu());
    assert_decrypts(&mut server, c3, t);
    assert_eq!(client.stats().frame_tx.padding, 1);
    assert_eq!(server.stream_recv(id, &mut buf).unwrap(), (fill - 1, false));
}

/// A DATAGRAM frame without a length that falls short of the end of the packet is padded before.
#[test]
fn expand_datagram_that_ends_in_short_datagram_frame() {
    const MAX_FRAME: usize = 100;
    let (mut client, mut server, t) =
        client_coalescing_1rtt(ConnectionParameters::default().datagram_size(to_u64(MAX_FRAME)));
    // A length would make the frame exceed the peer's limit, so it has none.
    let data = vec![0; MAX_FRAME - DATAGRAM_FRAME_TYPE_VARINT_LEN];
    assert_eq!(
        client.enqueue_datagram(StreamId::new(0), data, Some(1), t, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::Ok)
    );
    let c3 = client.process_output(t).dgram().unwrap();
    assert_eq!(client.stats().frame_tx.datagram, 1);
    assert_eq!(c3.len(), client.plpmtu());
    let received = server.stats().frame_rx.datagram;
    assert_decrypts(&mut server, c3, t);
    assert_eq!(server.stats().frame_rx.datagram, received + 1);
    assert_eq!(client.stats().frame_tx.padding, 1);
}

struct OmitExtension;

impl ExtensionHandler for OmitExtension {
    fn write(&mut self, _: HandshakeMessage, _: bool, _: &mut [u8]) -> ExtensionWriterResult {
        ExtensionWriterResult::Skip
    }
}

/// Overwrite the QUIC transport parameters extension handler in TLS
/// with one that does not write an extension.
/// Must be called before the client writes its `ClientHello`.
fn omit_transport_parameters(client: &mut Connection) {
    client
        .crypto
        .tls_mut()
        .extension_handler(0x39, Rc::new(RefCell::new(OmitExtension)))
        .unwrap();
}

#[test]
fn client_omits_transport_parameters() {
    let mut client = default_client();
    omit_transport_parameters(&mut client);
    let mut server = default_server();
    let alert = Error::CryptoAlert(109); // missing_extension
    connect_fail(&mut client, &mut server, Error::Peer(alert.code()), alert);
}

#[test]
fn client_omits_transport_parameters_0rtt() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let token = exchange_ticket(&mut client, &mut server, now());

    let mut client = default_client();
    omit_transport_parameters(&mut client);
    client.enable_resumption(now(), token).unwrap();
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, DEFAULT_STREAM_DATA).unwrap();
    let mut server = resumed_server(&client);
    while let Some(d) = client.process_output(now()).dgram() {
        server.process_input(d, now());
    }
    assert_eq!(server.stats().frame_rx.stream, 0);
    assert_error(&server, &CloseReason::Transport(Error::CryptoAlert(109)));
}
