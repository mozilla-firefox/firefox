// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

mod common;

use std::{cell::RefCell, net::SocketAddr, num::NonZeroUsize, rc::Rc, time::Duration};

use common::{connect, connected_server, default_server, find_ticket, generate_ticket, new_server};
use neqo_common::{Datagram, Decoder, Encoder, Role};
use neqo_transport::{
    CloseReason, Connection, ConnectionParameters, Error, MIN_INITIAL_PACKET_SIZE, Output, State,
    StreamType, Version,
    server::{ConnectionRef, Server, ValidateAddress},
    version,
};
use nss::{
    AllowZeroRtt, AuthenticationStatus, RecordProtectionOps as _, TLS_AES_256_GCM_SHA384,
    ZeroRttCheckResult, ZeroRttChecker, generate_ech_keys,
};
use test_fixture::{
    CountingConnectionIdGenerator, assertions, datagram, default_client,
    header_protection::{self, decode_initial_header, initial_aead_and_hp},
    new_client, now, split_datagram,
};
use tracing::trace;

/// Take a pair of connections in any state and complete the handshake.
/// The `datagram` argument is a packet that was received from the server.
/// See `connect` for what this returns.
///
/// # Panics
///
/// Only when the connection fails.
pub fn complete_connection(
    client: &mut Connection,
    server: &mut Server,
    mut datagram: Option<Datagram>,
) -> ConnectionRef {
    let is_done = |c: &Connection| {
        matches!(
            c.state(),
            State::Confirmed | State::Closing { .. } | State::Closed(..)
        )
    };
    while !is_done(client) {
        _ = test_fixture::maybe_authenticate(client);
        let out = client.process(datagram, now());
        let out = server.process(out.dgram(), now());
        datagram = out.dgram();
    }

    assert_eq!(*client.state(), State::Confirmed);
    connected_server(server)
}

#[test]
fn single_client() {
    let mut server = default_server();
    let mut client = default_client();
    connect(&mut client, &mut server);
}

#[test]
fn connect_single_version_both() {
    fn connect_one_version(version: Version) {
        let mut server =
            new_server(ConnectionParameters::default().versions(version, vec![version]));
        let mut client = new_client::<CountingConnectionIdGenerator>(
            ConnectionParameters::default().versions(version, vec![version]),
        );
        let server_conn = connect(&mut client, &mut server);
        assert_eq!(client.version(), version);
        assert_eq!(server_conn.borrow().version(), version);
    }

    for v in Version::all() {
        println!("Connecting with {v:?}");
        connect_one_version(v);
    }
}

#[test]
fn connect_single_version_client() {
    fn connect_one_version(version: Version) {
        let mut server = default_server();

        let mut client = new_client::<CountingConnectionIdGenerator>(
            ConnectionParameters::default().versions(version, vec![version]),
        );
        let server_conn = connect(&mut client, &mut server);
        assert_eq!(client.version(), version);
        assert_eq!(server_conn.borrow().version(), version);
    }

    for v in Version::all() {
        println!("Connecting with {v:?}");
        connect_one_version(v);
    }
}

#[test]
fn connect_single_version_server() {
    fn connect_one_version(version: Version) {
        let mut server =
            new_server(ConnectionParameters::default().versions(version, vec![version]));

        let mut client = default_client();

        if client.version() != version {
            // Run the version negotiation exchange if necessary.
            let out = client.process_output(now());
            assert!(out.as_dgram_ref().is_some());
            let dgram = server.process(out.dgram(), now()).dgram();
            assertions::assert_vn(dgram.as_ref().unwrap());
            client.process_input(dgram.unwrap(), now());
        }

        let server_conn = connect(&mut client, &mut server);
        assert_eq!(client.version(), version);
        assert_eq!(server_conn.borrow().version(), version);
    }

    for v in Version::all() {
        println!("Connecting with {v:?}");
        connect_one_version(v);
    }
}

#[test]
fn duplicate_initial() {
    let mut server = default_server();
    let mut client = default_client();

    assert_eq!(*client.state(), State::Init);
    let initial = client.process_output(now());
    assert!(initial.as_dgram_ref().is_some());

    // The server should ignore a packets with the same remote address and
    // destination connection ID as an existing connection attempt.
    let server_initial = server
        .process(initial.as_dgram_ref().cloned(), now())
        .dgram();
    assert!(server_initial.is_some());
    let dgram = server.process(initial.dgram(), now()).dgram();
    assert!(dgram.is_none());

    assert_eq!(server.active_connections().len(), 1);
    complete_connection(&mut client, &mut server, server_initial);
}

#[test]
fn duplicate_initial_new_path() {
    let mut server = default_server();
    let mut client = default_client();

    assert_eq!(*client.state(), State::Init);
    let initial = client.process_output(now()).dgram().unwrap();
    let other = Datagram::new(
        SocketAddr::new(initial.source().ip(), initial.source().port() ^ 0b1_01110), // 23
        initial.destination(),
        initial.tos(),
        &initial[..],
    );

    let server_initial = server.process(Some(initial), now()).dgram();
    assert!(server_initial.is_some());

    // The server should ignore a packet with the same destination connection ID.
    let dgram = server.process(Some(other), now()).dgram();
    assert!(dgram.is_none());

    assert_eq!(server.active_connections().len(), 1);
    complete_connection(&mut client, &mut server, server_initial);
}

#[test]
fn different_initials_same_path() {
    let mut server = default_server();
    let mut client1 = default_client();
    let mut client2 = default_client();

    let client_initial1 = client1.process_output(now());
    assert!(client_initial1.as_dgram_ref().is_some());
    let client_initial2 = client2.process_output(now());
    assert!(client_initial2.as_dgram_ref().is_some());

    // The server should respond to both as these came from different addresses.
    let server_initial1 = server.process(client_initial1.dgram(), now()).dgram();
    assert!(server_initial1.is_some());

    let server_initial2 = server.process(client_initial2.dgram(), now()).dgram();
    assert!(server_initial2.is_some());

    assert_eq!(server.active_connections().len(), 2);
    complete_connection(&mut client1, &mut server, server_initial1);
    complete_connection(&mut client2, &mut server, server_initial2);
}

#[test]
fn same_initial_after_connected() {
    let mut server = default_server();
    let mut client = default_client();

    let client_initial = client.process_output(now());
    assert!(client_initial.as_dgram_ref().is_some());

    let server_initial = server
        .process(client_initial.as_dgram_ref().cloned(), now())
        .dgram();
    assert!(server_initial.is_some());
    complete_connection(&mut client, &mut server, server_initial);
    assert_eq!(server.active_connections().len(), 1);

    // Now make a new connection using the exact same initial as before.
    // The server should respond to an attempt to connect with the same Initial.
    let dgram = server.process(client_initial.dgram(), now()).dgram();
    assert!(dgram.is_some());
    // The server should make a new connection object.
    assert_eq!(server.active_connections().len(), 2);
}

#[test]
fn drop_non_initial() {
    const CID: &[u8] = &[55; 8]; // not a real connection ID
    let mut server = default_server();

    // This is big enough to look like an Initial, but it uses the Retry type.
    let mut header = Encoder::with_capacity(MIN_INITIAL_PACKET_SIZE);
    header
        .encode_byte(0xfa)
        .encode_uint(4, Version::default().wire_version())
        .encode_vec(1, CID)
        .encode_vec(1, CID);
    let mut bogus_data: Vec<u8> = header.into();
    bogus_data.resize(MIN_INITIAL_PACKET_SIZE, 66);

    let bogus = datagram(bogus_data);
    assert!(server.process(Some(bogus), now()).dgram().is_none());
}

#[test]
fn drop_short_initial() {
    const CID: &[u8] = &[55; 8]; // not a real connection ID
    let mut server = default_server();

    // This too small to be an Initial, but it is otherwise plausible.
    let mut header = Encoder::with_capacity(1199);
    header
        .encode_byte(0xca)
        .encode_uint(4, Version::default().wire_version())
        .encode_vec(1, CID)
        .encode_vec(1, CID);
    let mut bogus_data: Vec<u8> = header.into();
    bogus_data.resize(1199, 66);

    let bogus = datagram(bogus_data);
    assert!(server.process(Some(bogus), now()).dgram().is_none());
}

#[test]
fn drop_short_header_packet_for_unknown_connection() {
    const CID: &[u8] = &[55; 8]; // not a real connection ID
    let mut server = default_server();

    let mut header = Encoder::with_capacity(MIN_INITIAL_PACKET_SIZE);
    header
        .encode_byte(0x40) // short header
        .encode_vec(1, CID)
        .encode_byte(1);
    let mut bogus_data: Vec<u8> = header.into();
    bogus_data.resize(MIN_INITIAL_PACKET_SIZE, 66);

    let bogus = datagram(bogus_data);
    assert!(server.process(Some(bogus), now()).dgram().is_none());
}

/// Verify that the server can read 0-RTT properly.  A more robust server would buffer
/// 0-RTT before the handshake begins and let 0-RTT arrive for a short period after
/// the handshake completes, but ours is for testing so it only allows 0-RTT while
/// the handshake is running.
#[test]
fn zero_rtt() {
    let mut server = default_server();
    let token = generate_ticket(&mut server);

    // Discharge the old connection so that we don't have to worry about it.
    let mut now = now();
    let t = server.process_output(now).callback();
    now += t;
    assert_eq!(server.process_output(now), Output::None);
    assert_eq!(server.active_connections().len(), 0);

    let start_time = now;
    let mut client = default_client();
    client.enable_resumption(now, &token).unwrap();

    let mut client_send = || {
        let client_stream = client.stream_create(StreamType::UniDi).unwrap();
        client.stream_send(client_stream, &[1, 2, 3]).unwrap();
        match client.process_output(now) {
            Output::Datagram(d) => d,
            Output::Callback(t) => {
                // Pacing...
                now += t;
                client.process_output(now).dgram().unwrap()
            }
            Output::None => panic!(),
        }
    };

    // Now generate a bunch of 0-RTT packets...
    let c0 = client_send();
    let c1 = client_send();
    assertions::assert_coalesced_0rtt(&c1);
    let c2 = client_send();
    let c3 = client_send();
    let c4 = client_send();

    // 0-RTT packets that arrive before the handshake get dropped.
    drop(server.process(Some(c2), now));
    assert!(server.active_connections().is_empty());

    // Now handshake and let another 0-RTT packet in.
    _ = server.process(Some(c0), now);
    let shs = server.process(Some(c1), now);
    drop(server.process(Some(c3), now));
    // The server will have received three STREAM frames now if it processed both packets.
    #[expect(
        clippy::mutable_key_type,
        reason = "ActiveConnectionRef::Hash doesn't access any of the interior mutable types."
    )]
    let active = server.active_connections();
    assert_eq!(active.len(), 1);
    assert_eq!(
        active
            .iter()
            .next()
            .unwrap()
            .borrow()
            .stats()
            .frame_rx
            .stream,
        3
    );

    // Complete the handshake.  As the client was pacing 0-RTT packets, extend the time
    // a little so that the pacer doesn't prevent the Finished from being sent.
    now += now - start_time;
    let cfin = client.process(shs.dgram(), now);
    drop(server.process(cfin.dgram(), now));

    // The server will drop this last 0-RTT packet.
    drop(server.process(Some(c4), now));
    #[expect(
        clippy::mutable_key_type,
        reason = "ActiveConnectionRef::Hash doesn't access any of the interior mutable types."
    )]
    let active = server.active_connections();
    assert_eq!(active.len(), 1);
    assert_eq!(
        active
            .iter()
            .next()
            .unwrap()
            .borrow()
            .stats()
            .frame_rx
            .stream,
        4
    );
}

#[test]
fn new_token_0rtt() {
    let mut server = default_server();
    let token = generate_ticket(&mut server);
    server.set_validation(ValidateAddress::NoToken);

    let mut client = default_client();
    client.enable_resumption(now(), &token).unwrap();

    let client_stream = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream, &[1, 2, 3]).unwrap();

    let out = client.process_output(now());
    let out2 = client.process_output(now()); // Initial w/0-RTT
    assert!(out.as_dgram_ref().is_some() && out2.as_dgram_ref().is_some());
    assertions::assert_initial(out.as_dgram_ref().unwrap(), true);
    assertions::assert_coalesced_0rtt(out2.as_dgram_ref().unwrap());
    _ = server.process(out.dgram(), now()); // Initial
    let out = server.process(out2.dgram(), now()); // Initial
    assert!(out.as_dgram_ref().is_some());
    assertions::assert_initial(out.as_dgram_ref().unwrap(), false);

    let dgram = client.process(out.as_dgram_ref().cloned(), now());
    let dgram = server.process(dgram.as_dgram_ref().cloned(), now());
    let dgram = client.process(dgram.as_dgram_ref().cloned(), now());
    // Note: the client doesn't need to authenticate the server here
    // as there is no certificate; authentication is based on the ticket.
    assert!(out.as_dgram_ref().is_some());
    assert_eq!(*client.state(), State::Connected);
    let dgram = server.process(dgram.dgram(), now()); // (done)
    assert!(dgram.as_dgram_ref().is_some());
    connected_server(&server);
    assert!(client.tls_info().unwrap().resumed());
}

#[test]
fn new_token_different_port() {
    let mut server = default_server();
    let token = generate_ticket(&mut server);
    server.set_validation(ValidateAddress::NoToken);

    let mut client = default_client();
    client.enable_resumption(now(), &token).unwrap();

    let dgram = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some());
    assertions::assert_initial(dgram.as_ref().unwrap(), true);

    // Now rewrite the source port, which should not change that the token is OK.
    let d = dgram.unwrap();
    let src = SocketAddr::new(d.source().ip(), d.source().port() + 1);
    let dgram = Some(Datagram::new(src, d.destination(), d.tos(), &d[..]));
    let dgram = server.process(dgram, now()).dgram(); // Retry
    assert!(dgram.is_some());
    assertions::assert_initial(dgram.as_ref().unwrap(), false);
}

#[test]
fn bad_client_initial() {
    const PN_LEN: usize = 2;
    let mut client =
        new_client::<CountingConnectionIdGenerator>(ConnectionParameters::default().mlkem(false));
    // There's some precise size counting we do in this test, so disable randomization
    // of packet numbers.
    let mut server = new_server(ConnectionParameters::default().randomize_first_pn(false));

    let dgram = client.process_output(now()).dgram().expect("a datagram");
    let (header, d_cid, s_cid, payload) = decode_initial_header(&dgram, Role::Client).unwrap();
    let (aead_enc, aead_dec, hp) = initial_aead_and_hp(d_cid, Role::Client);
    let (fixed_header, pn) = header_protection::remove(&hp, header, payload);
    let payload = &payload[(fixed_header.len() - header.len())..];

    let mut plaintext_buf = vec![0; dgram.len()];
    let plaintext = aead_dec
        .decrypt(pn, &fixed_header, payload, &mut plaintext_buf)
        .unwrap();

    let mut payload_enc = Encoder::from(plaintext);
    payload_enc.encode([0x08, 0x02, 0x00, 0x00]); // Add a stream frame.

    // Make a new header with a 1 byte packet number length.
    let mut header_enc = Encoder::default();
    header_enc
        .encode_byte(0xc1) // Initial with 2 byte packet number.
        .encode_uint(4, Version::Version1.wire_version())
        .encode_vec(1, d_cid)
        .encode_vec(1, s_cid)
        .encode_vvec(&[])
        .encode_len(payload_enc.len() + aead_enc.expansion() + PN_LEN)
        .encode_byte(u8::try_from(pn >> 8).unwrap())
        .encode_byte(u8::try_from(pn & 0xff).unwrap());

    let mut ciphertext = header_enc.as_ref().to_vec();
    ciphertext.resize(
        header_enc.len() + payload_enc.len() + aead_enc.expansion(),
        0,
    );
    let v = aead_enc
        .encrypt(
            pn,
            header_enc.as_ref(),
            payload_enc.as_ref(),
            &mut ciphertext[header_enc.len()..],
        )
        .unwrap();
    assert_eq!(header_enc.len() + v.len(), ciphertext.len());
    // Pad with zero to get up to MIN_INITIAL_PACKET_SIZE.
    ciphertext.resize(MIN_INITIAL_PACKET_SIZE, 0);

    header_protection::apply(
        &hp,
        &mut ciphertext,
        (header_enc.len() - PN_LEN)..header_enc.len(),
    );
    let bad_dgram = Datagram::new(dgram.source(), dgram.destination(), dgram.tos(), ciphertext);

    // The server should reject this.
    let response = server.process(Some(bad_dgram), now());
    let close_dgram = response.dgram().unwrap();
    // The resulting datagram might contain multiple packets, but each is small.
    let (initial_close, rest) = split_datagram(&close_dgram);
    // Allow for large connection IDs and a 32 byte CONNECTION_CLOSE.
    assert!(initial_close.len() <= 100);
    let (handshake_close, short_close) = split_datagram(&rest.unwrap());
    // The Handshake packet containing the close is the same size as the Initial,
    // plus 1 byte for the Token field in the Initial.
    assert_eq!(initial_close.len(), handshake_close.len() + 1);
    assert!(short_close.unwrap().len() <= 73);

    // The client should accept this new and stop trying to connect.
    // It will generate a CONNECTION_CLOSE first though.
    let response = client.process(Some(close_dgram), now()).dgram();
    assert!(response.is_some());
    // The client will now wait out its closing period.
    let delay = client.process_output(now()).callback();
    assert_ne!(delay, Duration::from_secs(0));
    assert!(matches!(
        *client.state(),
        State::Draining { error: CloseReason::Transport(Error::Peer(code)), .. } if code == Error::ProtocolViolation.code()
    ));

    #[expect(
        clippy::iter_over_hash_type,
        reason = "OK to loop over active connections in an undefined order."
    )]
    for server in server.active_connections() {
        assert_eq!(
            *server.borrow().state(),
            State::Closed(CloseReason::Transport(Error::ProtocolViolation))
        );
    }

    // After sending the CONNECTION_CLOSE, the server goes idle.
    let res = server.process_output(now());
    assert_eq!(res, Output::None);
}

#[test]
fn bad_client_initial_connection_close() {
    // This test needs to decrypt the CI; turn off MLKEM and random client initial packet numbers.
    let mut client = new_client::<CountingConnectionIdGenerator>(
        ConnectionParameters::default()
            .mlkem(false)
            .randomize_first_pn(false),
    );
    let mut server = default_server();

    let dgram = client.process_output(now()).dgram().expect("a datagram");
    let (header, d_cid, s_cid, payload) = decode_initial_header(&dgram, Role::Client).unwrap();
    let (aead, _, hp) = initial_aead_and_hp(d_cid, Role::Client);
    let (_, pn) = header_protection::remove(&hp, header, payload);

    let mut payload_enc = Encoder::with_capacity(MIN_INITIAL_PACKET_SIZE);
    payload_enc.encode([0x1c, 0x01, 0x00, 0x00]); // Add a CONNECTION_CLOSE frame.

    // Make a new header with a 1 byte packet number length.
    let mut header_enc = Encoder::default();
    header_enc
        .encode_byte(0xc0) // Initial with 1 byte packet number.
        .encode_uint(4, Version::default().wire_version())
        .encode_vec(1, d_cid)
        .encode_vec(1, s_cid)
        .encode_vvec(&[])
        .encode_len(payload_enc.len() + aead.expansion() + 1)
        .encode_byte(u8::try_from(pn).unwrap());

    let mut ciphertext = header_enc.as_ref().to_vec();
    ciphertext.resize(header_enc.len() + payload_enc.len() + aead.expansion(), 0);
    let v = aead
        .encrypt(
            pn,
            header_enc.as_ref(),
            payload_enc.as_ref(),
            &mut ciphertext[header_enc.len()..],
        )
        .unwrap();
    assert_eq!(header_enc.len() + v.len(), ciphertext.len());
    // Pad with zero to get up to MIN_INITIAL_PACKET_SIZE.
    ciphertext.resize(MIN_INITIAL_PACKET_SIZE, 0);

    header_protection::apply(
        &hp,
        &mut ciphertext,
        (header_enc.len() - 1)..header_enc.len(),
    );
    let bad_dgram = Datagram::new(dgram.source(), dgram.destination(), dgram.tos(), ciphertext);

    // The server should ignore this and go to Draining.
    let mut now = now();
    let response = server.process(Some(bad_dgram), now);
    now += response.callback();
    let response = server.process_output(now);
    assert_eq!(response, Output::None);
}

#[test]
fn version_negotiation_ignored() {
    let mut server = default_server();
    let mut client = default_client();

    // Any packet will do, but let's make something that looks real.
    let dgram = client.process_output(now()).dgram().expect("a datagram");
    _ = client.process_output(now()).dgram().expect("a datagram");
    let mut input = dgram.to_vec();
    input[1] ^= 0x12;
    let damaged = Datagram::new(
        dgram.source(),
        dgram.destination(),
        dgram.tos(),
        input.clone(),
    );
    let vn = server.process(Some(damaged), now()).dgram();

    let mut dec = Decoder::from(&input[5..]); // Skip past version.
    let d_cid = dec.decode_vec(1).expect("client DCID").to_vec();
    let s_cid = dec.decode_vec(1).expect("client SCID").to_vec();

    // We should have received a VN packet.
    let vn = vn.expect("a vn packet");
    let mut dec = Decoder::from(&vn[1..]); // Skip first byte.

    assert_eq!(dec.decode_uint::<u32>().expect("VN"), 0);
    assert_eq!(dec.decode_vec(1).expect("VN DCID"), &s_cid[..]);
    assert_eq!(dec.decode_vec(1).expect("VN SCID"), &d_cid[..]);
    let mut found = false;
    while dec.remaining() > 0 {
        let v = dec
            .decode_uint::<version::Wire>()
            .expect("supported version");
        found |= v == Version::default().wire_version();
    }
    assert!(found, "valid version not found");

    // Client ignores VN packet that contain negotiated version.
    let res = client.process(Some(vn), now());
    assert!(res.callback() > Duration::new(0, 120));
    assert_eq!(client.state(), &State::WaitInitial);
}

/// Test that if the server doesn't support a version it will signal with a
/// Version Negotiation packet and the client will use that version.
#[test]
fn version_negotiation() {
    const VN_VERSION: Version = Version::Draft29;
    assert_ne!(VN_VERSION, Version::default());
    assert!(!Version::default().is_compatible(VN_VERSION));

    let mut server =
        new_server(ConnectionParameters::default().versions(VN_VERSION, vec![VN_VERSION]));
    let mut client = default_client();

    // `connect()` runs a fixed exchange, so manually run the Version Negotiation.
    let dgram = client.process_output(now()).dgram();
    assert!(dgram.is_some());
    let dgram = server.process(dgram, now()).dgram();
    assertions::assert_vn(dgram.as_ref().unwrap());
    client.process_input(dgram.unwrap(), now());

    let sconn = connect(&mut client, &mut server);
    assert_eq!(client.version(), VN_VERSION);
    assert_eq!(sconn.borrow().version(), VN_VERSION);
}

/// Test that the client can pick a version from a Version Negotiation packet,
/// which is then subsequently upgraded to a compatible version by the server.
#[test]
fn version_negotiation_and_compatible() {
    const ORIG_VERSION: Version = Version::Draft29;
    const VN_VERSION: Version = Version::Version1;
    const COMPAT_VERSION: Version = Version::Version2;
    assert!(!ORIG_VERSION.is_compatible(VN_VERSION));
    assert!(!ORIG_VERSION.is_compatible(COMPAT_VERSION));
    assert!(VN_VERSION.is_compatible(COMPAT_VERSION));

    let mut server = new_server(
        ConnectionParameters::default().versions(VN_VERSION, vec![COMPAT_VERSION, VN_VERSION]),
    );
    // Note that the order of versions at the client only determines what it tries first.
    // The server will pick between VN_VERSION and COMPAT_VERSION.
    let mut client = new_client::<CountingConnectionIdGenerator>(
        ConnectionParameters::default()
            .versions(ORIG_VERSION, vec![ORIG_VERSION, VN_VERSION, COMPAT_VERSION]),
    );

    // Run the full exchange so that we can observe the versions in use.

    // Version Negotiation
    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    assert!(dgram.is_some() && dgram2.is_some());
    assertions::assert_version(dgram.as_ref().unwrap(), ORIG_VERSION.wire_version());
    _ = server.process(dgram, now()).dgram();
    let dgram = server.process(dgram2, now()).dgram();
    assertions::assert_vn(dgram.as_ref().unwrap());
    client.process_input(dgram.unwrap(), now());

    let dgram = client.process_output(now()).dgram(); // ClientHello
    let dgram2 = client.process_output(now()).dgram(); // ClientHello
    assertions::assert_version(dgram.as_ref().unwrap(), VN_VERSION.wire_version());
    _ = server.process(dgram, now()).dgram(); // ServerHello...
    let dgram = server.process(dgram2, now()).dgram(); // ServerHello...
    assertions::assert_version(dgram.as_ref().unwrap(), COMPAT_VERSION.wire_version());
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    client.process_input(dgram.unwrap(), now());

    client.authenticated(AuthenticationStatus::Ok, now());
    let dgram = client.process_output(now()).dgram();
    assertions::assert_version(dgram.as_ref().unwrap(), COMPAT_VERSION.wire_version());
    assert_eq!(*client.state(), State::Connected);
    let dgram = server.process(dgram, now()).dgram(); // ACK + HANDSHAKE_DONE + NST
    client.process_input(dgram.unwrap(), now());
    assert_eq!(*client.state(), State::Confirmed);

    let sconn = connected_server(&server);
    assert_eq!(client.version(), COMPAT_VERSION);
    assert_eq!(sconn.borrow().version(), COMPAT_VERSION);
}

/// When a client resumes it remembers the version that the connection last used.
/// A subsequent connection will use that version, but if it then receives
/// a version negotiation packet, it should validate based on what it attempted
/// not what it was originally configured for.
#[test]
fn compatible_upgrade_resumption_and_vn() {
    // Start at v1, compatible upgrade to v2.
    const ORIG_VERSION: Version = Version::Version1;
    const COMPAT_VERSION: Version = Version::Version2;
    const RESUMPTION_VERSION: Version = Version::Draft29;

    let client_params = ConnectionParameters::default().versions(
        ORIG_VERSION,
        vec![COMPAT_VERSION, ORIG_VERSION, RESUMPTION_VERSION],
    );
    let mut client = new_client::<CountingConnectionIdGenerator>(client_params.clone());
    assert_eq!(client.version(), ORIG_VERSION);

    let mut server = default_server();
    let server_conn = connect(&mut client, &mut server);
    assert_eq!(client.version(), COMPAT_VERSION);
    assert_eq!(server_conn.borrow().version(), COMPAT_VERSION);

    server_conn.borrow_mut().send_ticket(now(), &[]).unwrap();
    let dgram = server.process_output(now()).dgram();
    client.process_input(dgram.unwrap(), now()); // Consume ticket, ignore output.
    let ticket = find_ticket(&mut client);

    // This new server will reject the ticket, but it will also generate a VN packet.
    let mut client = new_client::<CountingConnectionIdGenerator>(client_params);
    let mut server = new_server(
        ConnectionParameters::default().versions(RESUMPTION_VERSION, vec![RESUMPTION_VERSION]),
    );
    client.enable_resumption(now(), ticket).unwrap();

    // The version negotiation exchange.
    let dgram = client.process_output(now()).dgram();
    assert!(dgram.is_some());
    assertions::assert_version(dgram.as_ref().unwrap(), COMPAT_VERSION.wire_version());
    let dgram = server.process(dgram, now()).dgram();
    assertions::assert_vn(dgram.as_ref().unwrap());
    client.process_input(dgram.unwrap(), now());

    let server_conn = connect(&mut client, &mut server);
    assert_eq!(client.version(), RESUMPTION_VERSION);
    assert_eq!(server_conn.borrow().version(), RESUMPTION_VERSION);
}

#[test]
fn closed() {
    // Let a server connection idle and it should be removed.
    let mut server = default_server();
    let mut client = default_client();
    connect(&mut client, &mut server);

    // The server will have sent a few things, so it will be on PTO.
    let res = server.process_output(now());
    assert!(res.callback() > Duration::new(0, 0));
    // The client will be on the delayed ACK timer.
    let res = client.process_output(now());
    assert!(res.callback() > Duration::new(0, 0));

    trace!("60s later");
    let res = server.process_output(now() + Duration::from_mins(1));
    assert_eq!(res, Output::None);
}

#[cfg(test)]
fn can_create_streams(c: &mut Connection, t: StreamType, n: u64) {
    for _ in 0..n {
        c.stream_create(t).unwrap();
    }
    assert_eq!(c.stream_create(t), Err(Error::StreamLimit));
}

#[test]
fn max_streams() {
    const MAX_STREAMS: u64 = 40;
    let mut server = Server::new(
        now(),
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        test_fixture::anti_replay(),
        Box::new(AllowZeroRtt {}),
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        ConnectionParameters::default()
            .max_streams(StreamType::BiDi, MAX_STREAMS)
            .max_streams(StreamType::UniDi, MAX_STREAMS),
    )
    .expect("should create a server");

    let mut client = default_client();
    connect(&mut client, &mut server);

    // Make sure that we can create MAX_STREAMS uni- and bidirectional streams.
    can_create_streams(&mut client, StreamType::UniDi, MAX_STREAMS);
    can_create_streams(&mut client, StreamType::BiDi, MAX_STREAMS);
}

#[test]
fn max_streams_default() {
    let mut server = Server::new(
        now(),
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        test_fixture::anti_replay(),
        Box::new(AllowZeroRtt {}),
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        ConnectionParameters::default(),
    )
    .expect("should create a server");

    let mut client = default_client();
    connect(&mut client, &mut server);

    // Make sure that we can create streams up to the local limit.
    let local_limit_unidi = ConnectionParameters::default().get_max_streams(StreamType::UniDi);
    can_create_streams(&mut client, StreamType::UniDi, local_limit_unidi);
    let local_limit_bidi = ConnectionParameters::default().get_max_streams(StreamType::BiDi);
    can_create_streams(&mut client, StreamType::BiDi, local_limit_bidi);
}

#[derive(Debug)]
struct RejectZeroRtt {}
impl ZeroRttChecker for RejectZeroRtt {
    fn check(&self, _token: &[u8]) -> ZeroRttCheckResult {
        ZeroRttCheckResult::Reject
    }
}

#[test]
fn max_streams_after_0rtt_rejection() {
    const MAX_STREAMS_BIDI: u64 = 40;
    const MAX_STREAMS_UNIDI: u64 = 30;
    let mut server = Server::new(
        now(),
        test_fixture::DEFAULT_KEYS,
        test_fixture::DEFAULT_ALPN,
        test_fixture::anti_replay(),
        Box::new(RejectZeroRtt {}),
        Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
        ConnectionParameters::default()
            .max_streams(StreamType::BiDi, MAX_STREAMS_BIDI)
            .max_streams(StreamType::UniDi, MAX_STREAMS_UNIDI),
    )
    .expect("should create a server");
    let token = generate_ticket(&mut server);

    let mut client = default_client();
    client.enable_resumption(now(), &token).unwrap();
    _ = client.stream_create(StreamType::BiDi).unwrap();
    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    _ = server.process(dgram, now()).dgram();
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    assert!(dgram.is_some()); // We're far enough along to complete the test now.

    // Make sure that we can create MAX_STREAMS uni- and bidirectional streams.
    can_create_streams(&mut client, StreamType::UniDi, MAX_STREAMS_UNIDI);
    can_create_streams(&mut client, StreamType::BiDi, MAX_STREAMS_BIDI);
}

#[test]
fn ech() {
    // Check that ECH can be used.
    let mut server = default_server();
    let (sk, pk) = generate_ech_keys().unwrap();
    server.enable_ech(0x4a, "public.example", &sk, &pk).unwrap();

    let mut client = default_client();
    client.client_enable_ech(server.ech_config()).unwrap();
    let server_instance = connect(&mut client, &mut server);

    assert!(client.tls_info().unwrap().ech_accepted());
    assert!(server_instance.borrow().tls_info().unwrap().ech_accepted());
    assert!(client.tls_preinfo().unwrap().ech_accepted().unwrap());
    assert!(
        server_instance
            .borrow()
            .tls_preinfo()
            .unwrap()
            .ech_accepted()
            .unwrap()
    );
}

#[test]
fn has_active_connections() {
    let mut server = default_server();
    let mut client = default_client();

    assert!(!server.has_active_connections());

    let initial = client.process_output(now());
    _ = server.process(initial.dgram(), now()).dgram();

    assert!(server.has_active_connections());
}

/// If a server has to react immediately to a datagram in a batch, it will
/// service the remaining datagrams in consecutive calls.
#[test]
fn saved_datagrams() {
    let mut server = default_server();

    let valid_dgram = {
        let mut client = default_client();
        client.process_output(now()).dgram().expect("a datagram")
    };

    // Any packet will do, but let's make something that looks real.
    let invalid_dgram = || {
        let mut client = default_client();
        let dgram = client.process_output(now()).dgram().expect("a datagram");
        let mut input = dgram.to_vec();
        input[1] ^= 0x12;
        Datagram::new(
            dgram.source(),
            dgram.destination(),
            dgram.tos(),
            input.clone(),
        )
    };

    // Server sends a version negotation immediately. Saves second and third
    // input datagram for later.
    server
        .process_multiple(
            vec![invalid_dgram(), valid_dgram, invalid_dgram()],
            now(),
            NonZeroUsize::MIN,
        )
        .dgram()
        .expect("first packet triggers first vn");

    // Server processes the second (valid) datagram which doesn't require an
    // immediate response. Server then processes the third (invalid) datagram
    // which does require an immediate response. It thereby has to save the
    // fourth (new) datagram for the next call.
    server
        .process_multiple(Some(invalid_dgram()), now(), NonZeroUsize::MIN)
        .dgram()
        .expect("third packet triggers second vn");

    // Server processes the fourth datagram.
    server
        .process_multiple(Vec::<Datagram>::new(), now(), NonZeroUsize::MIN)
        .dgram()
        .expect("fourth packet triggers third vn");
}

#[test]
fn server_ciphers() {
    let mut server = default_server();
    server.set_ciphers([TLS_AES_256_GCM_SHA384]);
    let mut client = default_client();
    let server_instance = connect(&mut client, &mut server);

    let client_cipher = client.tls_info().unwrap().cipher_suite();
    assert_eq!(client_cipher, TLS_AES_256_GCM_SHA384);
    let server_cipher = server_instance.borrow().tls_info().unwrap().cipher_suite();
    assert_eq!(server_cipher, TLS_AES_256_GCM_SHA384);
}

#[test]
fn server_ciphers_unknown() {
    let mut server = default_server();
    server.set_ciphers([0x0a0a]); // RFC 8701 GREASE value, unknown to NSS.
    let mut client = default_client();

    let initial = client.process_output(now()).dgram();
    assert!(server.process(initial, now()).dgram().is_none());
    assert!(!server.has_active_connections());
}
