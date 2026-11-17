// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::time::Duration;

use neqo_common::{Decoder, Dscp, Encoder, event::Provider as _};
use test_fixture::{
    assertions::{self, assert_initial, assert_version},
    datagram, now, split_datagram, strip_padding,
};

#[cfg(not(feature = "disable-encryption"))]
use super::{super::Connection, assert_error, forged_server_initial};
use super::{
    super::{CloseReason, ConnectionEvent, Output, State, ZeroRttState},
    CryptoWriter, connect, connect_fail, default_client, default_server, exchange_ticket,
    new_client, new_server, send_something,
};
#[cfg(not(feature = "disable-encryption"))]
use crate::frame::FrameType;
use crate::{
    ConnectionParameters, ERROR_VERSION_NEGOTIATION, Error, MIN_INITIAL_PACKET_SIZE, Stats,
    Version,
    connection::tests::{AT_LEAST_PTO, connect_rtt_idle_with_modifier},
    packet,
    tparams::{TransportParameter, TransportParameterId::*},
};

// The expected PTO duration after the first Initial is sent.
const INITIAL_PTO: Duration = Duration::from_millis(300);

/// # Panics
///
/// When the count of received packets doesn't match the count of received packets with the
/// (default) DSCP.
pub fn assert_dscp(stats: &Stats) {
    assert_eq!(stats.dscp_rx[Dscp::Cs0], stats.packets_rx);
}

#[test]
fn unknown_version() {
    let mut client = default_client();
    // Start the handshake.
    drop(client.process_output(now()).dgram());

    let mut unknown_version_packet = vec![0x80, 0x1a, 0x1a, 0x1a, 0x1a];
    unknown_version_packet.resize(MIN_INITIAL_PACKET_SIZE, 0x0);
    drop(client.process(Some(datagram(unknown_version_packet)), now()));
    assert_eq!(1, client.stats().dropped_rx);
    assert_dscp(&client.stats());
}

#[test]
fn server_receive_unknown_first_packet() {
    let mut server = default_server();

    let mut unknown_version_packet = vec![0x80, 0x1a, 0x1a, 0x1a, 0x1a];
    unknown_version_packet.resize(MIN_INITIAL_PACKET_SIZE, 0x0);

    assert_eq!(
        server.process(Some(datagram(unknown_version_packet)), now()),
        Output::None
    );

    assert_eq!(1, server.stats().dropped_rx);
    assert_dscp(&server.stats());
}

fn create_vn(initial_pkt: &[u8], versions: &[u32]) -> Vec<u8> {
    let mut dec = Decoder::from(&initial_pkt[5..]); // Skip past version.
    let dst_cid = dec.decode_vec(1).expect("client DCID");
    let src_cid = dec.decode_vec(1).expect("client SCID");

    let mut encoder = Encoder::default();
    encoder.encode_byte(packet::BIT_LONG);
    encoder.encode([0; 4]); // Zero version == VN.
    encoder.encode_vec(1, src_cid);
    encoder.encode_vec(1, dst_cid);

    for v in versions {
        encoder.encode_uint(4, *v);
    }
    encoder.into()
}

#[test]
fn version_negotiation_current_version() {
    let mut client = default_client();
    // Start the handshake.
    let initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    _ = client.process_output(now()).dgram().expect("a datagram");

    let vn = create_vn(
        &initial_pkt,
        &[0x1a1a_1a1a, Version::default().wire_version()],
    );

    let dgram = datagram(vn);
    let mut delay = client.process(Some(dgram), now()).callback();
    delay += client.process_output(now() + delay).callback(); // Pacing delay clears before PTO fires.
    assert_eq!(delay, INITIAL_PTO);
    assert_eq!(*client.state(), State::WaitInitial);
    assert_eq!(1, client.stats().dropped_rx);
    assert_dscp(&client.stats());
}

#[test]
fn version_negotiation_version0() {
    let mut client = default_client();
    // Start the handshake.
    let initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    _ = client.process_output(now()).dgram().expect("a datagram");

    let vn = create_vn(&initial_pkt, &[0, 0x1a1a_1a1a]);

    let dgram = datagram(vn);
    let mut delay = client.process(Some(dgram), now()).callback();
    delay += client.process_output(now() + delay).callback(); // Pacing delay clears before PTO fires.
    assert_eq!(delay, INITIAL_PTO);
    assert_eq!(*client.state(), State::WaitInitial);
    assert_eq!(1, client.stats().dropped_rx);
    assert_dscp(&client.stats());
}

#[test]
fn version_negotiation_only_reserved() {
    let mut client = default_client();
    // Start the handshake.
    let initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();

    let vn = create_vn(&initial_pkt, &[0x1a1a_1a1a, 0x2a2a_2a2a]);

    let dgram = datagram(vn);
    assert_eq!(client.process(Some(dgram), now()), Output::None);
    match client.state() {
        State::Closed(err) => {
            assert_eq!(*err, CloseReason::Transport(Error::VersionNegotiation));
        }
        _ => panic!("Invalid client state"),
    }
    assert_dscp(&client.stats());
}

#[test]
fn version_negotiation_corrupted() {
    let mut client = default_client();
    // Start the handshake.
    let initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    _ = client.process_output(now()).dgram().expect("a datagram");

    let vn = create_vn(&initial_pkt, &[0x1a1a_1a1a, 0x2a2a_2a2a]);

    let dgram = datagram(vn[..vn.len() - 1].to_vec());
    let mut delay = client.process(Some(dgram), now()).callback();
    delay += client.process_output(now() + delay).callback(); // Pacing delay clears before PTO fires.
    assert_eq!(delay, INITIAL_PTO);
    assert_eq!(*client.state(), State::WaitInitial);
    assert_eq!(1, client.stats().dropped_rx);
    assert_dscp(&client.stats());
}

#[test]
fn version_negotiation_empty() {
    let mut client = default_client();
    // Start the handshake.
    let initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    _ = client.process_output(now()).dgram().expect("a datagram");

    let vn = create_vn(&initial_pkt, &[]);

    let dgram = datagram(vn);
    let mut delay = client.process(Some(dgram), now()).callback();
    delay += client.process_output(now() + delay).callback(); // Pacing delay clears before PTO fires.
    assert_eq!(delay, INITIAL_PTO);
    assert_eq!(*client.state(), State::WaitInitial);
    assert_eq!(1, client.stats().dropped_rx);
    assert_dscp(&client.stats());
}

#[test]
fn version_negotiation_not_supported() {
    let mut client = default_client();
    // Start the handshake.
    let initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();

    let vn = create_vn(&initial_pkt, &[0x1a1a_1a1a, 0x2a2a_2a2a, 0xff00_0001]);
    let dgram = datagram(vn);
    assert_eq!(client.process(Some(dgram), now()), Output::None);
    match client.state() {
        State::Closed(err) => {
            assert_eq!(*err, CloseReason::Transport(Error::VersionNegotiation));
        }
        _ => panic!("Invalid client state"),
    }
    assert_dscp(&client.stats());
}

#[test]
fn version_negotiation_bad_cid() {
    let mut client = default_client();
    // Start the handshake.
    let mut initial_pkt = client
        .process_output(now())
        .dgram()
        .expect("a datagram")
        .to_vec();
    _ = client.process_output(now()).dgram().expect("a datagram");

    initial_pkt[6] ^= 0xc4;
    let vn = create_vn(&initial_pkt, &[0x1a1a_1a1a, 0x2a2a_2a2a, 0xff00_0001]);

    let dgram = datagram(vn);
    let mut delay = client.process(Some(dgram), now()).callback();
    delay += client.process_output(now() + delay).callback(); // Pacing delay clears before PTO fires.
    assert_eq!(delay, INITIAL_PTO);
    assert_eq!(*client.state(), State::WaitInitial);
    assert_eq!(1, client.stats().dropped_rx);
    assert_dscp(&client.stats());
}

#[test]
fn compatible_upgrade() {
    let mut client = default_client();
    let mut server = default_server();

    connect(&mut client, &mut server);
    assert_eq!(client.version(), Version::Version2);
    assert_eq!(server.version(), Version::Version2);
    assert_dscp(&client.stats());
    assert_dscp(&server.stats());
}

/// When the first packet from the client is gigantic, the server might generate acknowledgment
/// packets in version 1.  Both client and server need to handle that gracefully.
#[test]
fn compatible_upgrade_large_initial() {
    let params = ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    );
    let mut client = new_client(params.clone());
    client
        .set_local_tparam(
            TestTransportParameter,
            TransportParameter::Bytes(vec![0; 2048]),
        )
        .unwrap();
    let mut server = new_server(params);

    // Client Initial should take 2 packets.
    // Each should elicit a Version 1 ACK from the server.
    let dgram = client.process_output(now()).dgram().map(strip_padding);
    let dgram2 = client.process_output(now()).dgram().map(strip_padding);
    assert!(dgram.is_some() && dgram2.is_some());
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram().map(strip_padding);
    assert!(dgram.is_some());
    // The server doesn't have a full Initial, so it should stick with v1.
    assert_version(dgram.as_ref().unwrap(), Version::Version1.wire_version());
    client.process_input(dgram.unwrap(), now());

    // Connect, but strip padding from all the packets to keep the accounting tight.
    connect_rtt_idle_with_modifier(&mut client, &mut server, Duration::new(0, 0), strip_padding);
    assert_eq!(client.version(), Version::Version2);
    assert_eq!(server.version(), Version::Version2);
    // We removed padding, so no packets should be dropped.
    assert_eq!(client.stats().dropped_rx, 0);
    assert_eq!(server.stats().dropped_rx, 0);
    assert_dscp(&client.stats());
    assert_dscp(&server.stats());
}

/// A server that supports versions 1 and 2 might prefer version 1 and that's OK.
/// This one starts with version 1 and stays there.
#[test]
fn compatible_no_upgrade() {
    let mut client = new_client(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let mut server = new_server(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version1, Version::Version2],
    ));

    connect(&mut client, &mut server);
    assert_eq!(client.version(), Version::Version1);
    assert_eq!(server.version(), Version::Version1);
}

/// A server that supports versions 1 and 2 might prefer version 1 and that's OK.
/// This one starts with version 2 and downgrades to version 1.
#[test]
fn compatible_downgrade() {
    let mut client = new_client(ConnectionParameters::default().versions(
        Version::Version2,
        vec![Version::Version2, Version::Version1],
    ));
    let mut server = new_server(ConnectionParameters::default().versions(
        Version::Version2,
        vec![Version::Version1, Version::Version2],
    ));

    connect(&mut client, &mut server);
    assert_eq!(client.version(), Version::Version1);
    assert_eq!(server.version(), Version::Version1);
}

/// A client configured for version 1 but resuming from a version 2 ticket downgrades to version 1.
#[test]
fn compatible_downgrade_resumed() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let token = exchange_ticket(&mut client, &mut server, now());

    let mut client = default_client();
    client.enable_resumption(now(), token).unwrap();
    assert_eq!(client.version(), Version::Version2);
    let mut server = new_server(ConnectionParameters::default().versions(
        Version::Version2,
        vec![Version::Version1, Version::Version2],
    ));

    connect(&mut client, &mut server);
    assert_eq!(client.version(), Version::Version1);
    assert_eq!(server.version(), Version::Version1);
}

/// Inject a Version Negotiation packet, which the client detects when it validates the
/// server `version_negotiation` transport parameter.
#[test]
fn version_negotiation_downgrade() {
    const DOWNGRADE: Version = Version::Draft29;

    let mut client = default_client();
    // The server sets the current version in the transport parameter and
    // protects Initial packets with the version in its configuration.
    // When a server `Connection` is created by a `Server`, the configuration is set
    // to match the version of the packet it first receives.  This replicates that.
    let mut server =
        new_server(ConnectionParameters::default().versions(DOWNGRADE, Version::all()));

    // Start the handshake and spoof a VN packet.
    let initial = client.process_output(now()).dgram().unwrap();
    let vn = create_vn(&initial, &[DOWNGRADE.wire_version()]);
    let dgram = datagram(vn);
    client.process_input(dgram, now());

    connect_fail(
        &mut client,
        &mut server,
        Error::VersionNegotiation,
        Error::Peer(ERROR_VERSION_NEGOTIATION),
    );
}

/// A server connection needs to be configured with the version that the client attempts.
/// Otherwise, it will object to the client transport parameters and not do anything.
#[test]
fn invalid_server_version() {
    let mut client =
        new_client(ConnectionParameters::default().versions(Version::Version1, Version::all()));
    let mut server =
        new_server(ConnectionParameters::default().versions(Version::Version2, Version::all()));

    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    server.process_input(dgram2.unwrap(), now());

    // Three packets received (one is zero padding).
    assert_eq!(server.stats().packets_rx, 3);
    // One dropped (the zero padding).
    assert_eq!(server.stats().dropped_rx, 1);
    assert_eq!(server.stats().saved_datagrams, 0);
    // The server effectively hasn't reacted here.
    match server.state() {
        State::Closed(err) => {
            assert_eq!(*err, CloseReason::Transport(Error::VersionNegotiation));
        }
        _ => panic!("invalid server state"),
    }
}

#[test]
fn invalid_current_version_client() {
    const OTHER_VERSION: Version = Version::Draft29;

    let mut client = default_client();
    let mut server = default_server();

    assert_ne!(OTHER_VERSION, client.version());
    client
        .set_local_tparam(
            VersionInformation,
            TransportParameter::Versions {
                current: OTHER_VERSION.wire_version(),
                other: Version::all()
                    .iter()
                    .copied()
                    .map(Version::wire_version)
                    .collect(),
            },
        )
        .unwrap();

    connect_fail(
        &mut client,
        &mut server,
        Error::Peer(Error::VersionNegotiation.code()),
        Error::VersionNegotiation,
    );
}

/// To test this, we need to disable compatible upgrade so that the server doesn't update
/// its transport parameters.  Then, we can overwrite its transport parameters without
/// them being overwritten.  Otherwise, it would be hard to find a window during which
/// the transport parameter can be modified.
#[test]
fn invalid_current_version_server() {
    const OTHER_VERSION: Version = Version::Draft29;

    let mut client = default_client();
    let mut server = new_server(
        ConnectionParameters::default().versions(Version::default(), vec![Version::default()]),
    );

    assert!(!Version::default().is_compatible(OTHER_VERSION));
    server
        .set_local_tparam(
            VersionInformation,
            TransportParameter::Versions {
                current: OTHER_VERSION.wire_version(),
                other: vec![OTHER_VERSION.wire_version()],
            },
        )
        .unwrap();

    connect_fail(
        &mut client,
        &mut server,
        Error::VersionNegotiation,
        Error::Peer(Error::VersionNegotiation.code()),
    );
}

/// The server chooses a version that the client knows but did not offer.
#[test]
fn chosen_version_not_offered_by_client() {
    let just_v1 =
        ConnectionParameters::default().versions(Version::Version1, vec![Version::Version1]);
    let mut client = new_client(just_v1.clone());
    let mut server = new_server(just_v1);
    server
        .set_local_tparam(
            VersionInformation,
            TransportParameter::Versions {
                current: Version::Version2.wire_version(),
                other: vec![Version::Version2.wire_version()],
            },
        )
        .unwrap();

    connect_fail(
        &mut client,
        &mut server,
        Error::VersionNegotiation,
        Error::Peer(Error::VersionNegotiation.code()),
    );
}

#[test]
fn no_compatible_version() {
    const OTHER_VERSION: Version = Version::Draft29;

    let mut client = default_client();
    let mut server = default_server();

    assert_ne!(OTHER_VERSION, client.version());
    client
        .set_local_tparam(
            VersionInformation,
            TransportParameter::Versions {
                current: Version::default().wire_version(),
                other: vec![OTHER_VERSION.wire_version()],
            },
        )
        .unwrap();

    connect_fail(
        &mut client,
        &mut server,
        Error::Peer(Error::TransportParameter.code()),
        Error::TransportParameter,
    );
}

/// When a compatible upgrade chooses a different version, 0-RTT is rejected.
#[test]
fn compatible_upgrade_0rtt_rejected() {
    // This is the baseline configuration where v1 is attempted and v2 preferred.
    let prefer_v2 = ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    );
    let mut client = new_client(prefer_v2.clone());
    // The server will start with this so that the client resumes with v1.
    let just_v1 =
        ConnectionParameters::default().versions(Version::Version1, vec![Version::Version1]);
    let mut server = new_server(just_v1);

    connect(&mut client, &mut server);
    assert_eq!(client.version(), Version::Version1);
    let token = exchange_ticket(&mut client, &mut server, now());

    // Now upgrade the server to the preferred configuration.
    let mut client = new_client(prefer_v2.clone());
    let mut server = new_server(prefer_v2);
    client.enable_resumption(now(), token).unwrap();

    // Create a packet with 0-RTT from the client.
    let initial = send_something(&mut client, now());
    let initial2 = send_something(&mut client, now());
    assert_version(&initial, Version::Version1.wire_version());
    assertions::assert_coalesced_0rtt(&initial2);
    server.process_input(initial, now());
    server.process_input(initial2, now());
    assert!(
        !server
            .events()
            .any(|e| matches!(e, ConnectionEvent::NewStream { .. }))
    );

    // Finalize the connection.  Don't use connect() because it uses
    // maybe_authenticate() too liberally and that eats the events we want to check.
    let dgram = server.process_output(now()).dgram(); // ServerHello flight
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    let dgram = client.process(dgram, now()).dgram(); // Client Finished (note: no authentication)
    let dgram = server.process(dgram, now()).dgram(); // HANDSHAKE_DONE
    client.process_input(dgram.unwrap(), now());

    assert!(matches!(client.state(), State::Confirmed));
    assert!(matches!(server.state(), State::Confirmed));

    assert!(client.events().any(|e| {
        println!(" client event: {e:?}");
        matches!(e, ConnectionEvent::ZeroRttRejected)
    }));
    assert_eq!(client.zero_rtt_state(), ZeroRttState::Rejected);
}

/// When the server confirms a new version the client will switch to using v2.
/// If the client has sent a v1 packet with a packet number larger
/// than 255 AND the server has acknowledged that packet,
/// the client might send a v2 Initial packet with a truncated packet number.
/// The server could fail to process that packet if it is not tracking
/// packet receipt correctly.
#[test]
fn server_initial_versions() {
    let mut client = new_client(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let mut server = new_server(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let now = now();

    // Two handshake packets.
    let c1 = client.process_output(now).dgram();
    let c2 = client.process_output(now).dgram();
    assert!(c1.is_some() && c2.is_some());

    // Processing the first produces a v1 ACK.
    let s1 = server.process(c1, now).dgram();
    assert_version(s1.as_ref().unwrap(), Version::Version1.wire_version());
    // The second confirms the use of v2.
    let s2 = server.process(c2, now).dgram();
    assert_version(s2.as_ref().unwrap(), Version::Version2.wire_version());

    // Feeding the client just the Initial packets will force it to ACK.
    // This should have a truncated packet number (if randomized enough).
    client.process_input(s1.unwrap(), now);
    let (s2_init, _s2_hs) = split_datagram(&s2.unwrap());
    client.process_input(s2_init, now);
    let c3 = client.process_output(now).dgram();
    assert_initial(c3.as_ref().unwrap(), false);

    // The server should be able to decrypt and process that packet.
    // To avoid having Initial padding fouling the drop count,
    // split the datagram so that it only includes the Initial packet.
    let (c3_init, _padding) = split_datagram(&c3.unwrap());
    let before = server.stats();
    server.process_input(c3_init, now);
    let after = server.stats();
    assert_eq!(before.packets_rx + 1, after.packets_rx);
    assert_eq!(before.dropped_rx, after.dropped_rx);
}

/// When the server confirms a new version it switches to sending v2 packets.
/// If the server has sent a v1 packet with a packet number larger
/// than 255 AND the client has acknowledged that packet,
/// the server might send a v2 Initial packet with a truncated packet number.
/// The client could fail to process that packet if it is not tracking
/// packet receipt correctly.
#[test]
fn client_initial_versions() {
    let mut client = new_client(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let mut server = new_server(ConnectionParameters::default().versions(
        Version::Version1,
        vec![Version::Version2, Version::Version1],
    ));
    let mut now = now();

    // Two handshake packets.
    let c1 = client.process_output(now).dgram();
    let c2 = client.process_output(now).dgram();
    assert!(c1.is_some() && c2.is_some());

    // Processing the first produces a v1 ACK.
    // But we also need to force the client to send an ACK for this packet.
    // We can do that with an injected CRYPTO frame.
    server.test_frame_writer = Some(Box::new(CryptoWriter {}));
    let s1 = server.process(c1, now).dgram();
    server.test_frame_writer = None;
    assert_version(s1.as_ref().unwrap(), Version::Version1.wire_version());

    // Receiving the ACK then letting a PTO lapse means that the client
    // sends the handshake again, with an ACK for the server.
    client.process_input(s1.unwrap(), now);
    now += AT_LEAST_PTO;
    let c3 = client.process_output(now).dgram();
    server.process_input(c3.unwrap(), now);

    // Let the server have the second client packet (it shouldn't need it).
    // The response should be a v2 packet with a truncated packet number.
    // We can't really test that though.
    let s2 = server.process(c2, now).dgram();
    assert_version(s2.as_ref().unwrap(), Version::Version2.wire_version());

    // The client should be able to decrypt and process that packet.
    // To avoid having Initial padding fouling the drop count,
    // split the datagram so that it only includes the Initial packet.
    let (s2_init, _padding) = split_datagram(&s2.unwrap());
    let before = client.stats();
    client.process_input(s2_init, now);
    let after = client.stats();
    assert_eq!(before.packets_rx + 1, after.packets_rx);
    assert_eq!(before.dropped_rx, after.dropped_rx);
}

/// This test configures the client and server so that they are unable to perform
/// key exchange with the shares that the client offers in the first attempt.
/// This produces CRYPTO frames in Initial packets from Version 1.
/// Previously, the client used the version of the Initial packets
/// that contain CRYPTO frames to decide the version.  That was not wise.
#[test]
fn tls_hello_retry_request() {
    use nss::constants::{TLS_GRP_EC_SECP256R1, TLS_GRP_EC_SECP384R1, TLS_GRP_EC_X25519};

    let mut client = default_client();
    let mut server = default_server();

    // This includes two groups the server doesn't have up front,
    // so that we get the server to spit out a HelloRetryRequest.
    client
        .set_groups(&[
            TLS_GRP_EC_X25519,
            TLS_GRP_EC_SECP384R1,
            TLS_GRP_EC_SECP256R1,
        ])
        .unwrap();
    server.set_groups(&[TLS_GRP_EC_SECP256R1]).unwrap();

    connect(&mut client, &mut server);
}

/// A client with a known first packet number that has sent its `ClientHello`,
/// and the first datagram it sent.
#[cfg(not(feature = "disable-encryption"))]
fn client_after_hello() -> (Connection, Vec<u8>) {
    let mut client = new_client(ConnectionParameters::default().randomize_first_pn(false));
    let ci = client.process_output(now()).dgram().unwrap().to_vec();
    assert!(client.process_output(now()).dgram().is_some());
    (client, ci)
}

/// A CRYPTO frame at offset 1, which leaves the client in `WaitVersion`.
#[cfg(not(feature = "disable-encryption"))]
fn crypto_with_gap() -> &'static [u64] {
    &[FrameType::Crypto as u64, 1, 1, 0]
}

#[cfg(not(feature = "disable-encryption"))]
fn ack(largest: packet::Number) -> [u64; 5] {
    [FrameType::Ack.into(), largest, 0, 0, 0]
}

/// A client cannot switch back to a version it has switched away from.
/// It closes the connection in the version it is using.
#[cfg(not(feature = "disable-encryption"))]
#[test]
fn compatible_upgrade_and_back() {
    let (mut client, ci) = client_after_hello();
    let v2_pn = packet::Number::try_from(client.stats().packets_tx).unwrap();

    let s1 = forged_server_initial(&ci, Version::Version2, 0, &[], crypto_with_gap());
    let c1 = client.process(Some(datagram(s1)), now()).dgram();
    assert_version(&c1.unwrap(), Version::Version2.wire_version());

    let s2 = forged_server_initial(&ci, Version::Version2, 1, &[], &ack(v2_pn));
    client.process_input(datagram(s2), now());

    let s3 = forged_server_initial(&ci, Version::Version1, 2, &[], crypto_with_gap());
    let close = client.process(Some(datagram(s3)), now()).dgram();
    assert_error(&client, &CloseReason::Transport(Error::PacketNumberOverlap));
    assert_eq!(client.version(), Version::Version2);
    assert_version(&close.unwrap(), Version::Version2.wire_version());
}

/// Interleaved Initial packets can make the first version switch fail.
/// The client closes the connection in the version it is using.
#[cfg(not(feature = "disable-encryption"))]
#[test]
fn compatible_upgrade_interleaved() {
    let (mut client, ci) = client_after_hello();
    let first_pn = 0; // Due to `randomize_first_pn(false)`.

    let s = [
        forged_server_initial(&ci, Version::Version2, 0, &[], &ack(first_pn)),
        forged_server_initial(&ci, Version::Version1, 1, &[], &[FrameType::Ping.into()]),
        forged_server_initial(&ci, Version::Version2, 2, &[], crypto_with_gap()),
    ]
    .concat();
    let close = client.process(Some(datagram(s)), now()).dgram();
    assert_error(&client, &CloseReason::Transport(Error::PacketNumberOverlap));
    assert_eq!(client.version(), Version::Version1);
    assert_version(&close.unwrap(), Version::Version1.wire_version());
}
