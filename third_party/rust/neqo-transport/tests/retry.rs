// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg(not(feature = "disable-encryption"))]

mod common;

use std::{
    net::{IpAddr, Ipv4Addr, SocketAddr},
    time::Duration,
};

use common::{assert_dscp, connected_server, default_server, generate_ticket};
use neqo_common::{Datagram, Encoder, Role, hex::HexWithLen};
use neqo_transport::{
    CloseReason, ConnectionParameters, Error, MIN_INITIAL_PACKET_SIZE, State, StreamType,
    server::ValidateAddress,
};
use nss::{AuthenticationStatus, RecordProtectionOps as _, generate_ech_keys};
use test_fixture::{
    CountingConnectionIdGenerator, assertions, damage_ech_config, datagram, default_client,
    header_protection::{self, decode_initial_header, initial_aead_and_hp},
    now,
};
use tracing::{debug, trace};

#[test]
fn retry_basic() {
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();

    let dgram = client.process_output(now()).dgram(); // Initial
    let dgram2 = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some() && dgram2.is_some());
    _ = server.process(dgram, now()).dgram().unwrap(); // Retry
    let dgram = server.process(dgram2, now()).dgram().unwrap(); // Retry
    assertions::assert_retry(&dgram);

    let dgram = client.process(Some(dgram), now()).dgram(); // Initial w/token
    let dgram2 = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some() && dgram2.is_some());
    _ = server.process(dgram, now()).dgram().unwrap();
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram(); // Initial, HS
    assert!(dgram.is_some());
    drop(client.process(dgram, now()).dgram()); // Ingest, drop any ACK.
    client.authenticated(AuthenticationStatus::Ok, now());
    let dgram = client.process_output(now()).dgram(); // Send Finished
    assert!(dgram.is_some());
    assert_eq!(*client.state(), State::Connected);
    let dgram = server.process(dgram, now()).dgram(); // (done)
    assert!(dgram.is_some()); // Note that this packet will be dropped...
    connected_server(&server);
    assert_dscp(&client.stats());
}

/// Verify that ECH fallback works, even when there is a retry.
///
/// This is necessary to demonstrate that the transport parameters
/// in the outer `ClientHello` are sufficient to establish a connection.
#[test]
fn retry_ech_fallback() {
    const CONFIG_ID: u8 = 12;
    const PUBLIC_NAME: &str = "public.name.example";

    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();

    let (sk, pk) = generate_ech_keys().unwrap();
    server.enable_ech(CONFIG_ID, PUBLIC_NAME, &sk, &pk).unwrap();
    client
        .client_enable_ech(damage_ech_config(server.ech_config()))
        .unwrap();

    let dgram = client.process_output(now()).dgram(); // Initial
    let dgram2 = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some() && dgram2.is_some());
    _ = server.process(dgram, now()).dgram().unwrap(); // Retry
    let dgram = server.process(dgram2, now()).dgram().unwrap(); // Retry
    assertions::assert_retry(&dgram);

    let dgram = client.process(Some(dgram), now()).dgram(); // Initial w/token
    let dgram2 = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some() && dgram2.is_some());
    _ = server.process(dgram, now()).dgram().unwrap();
    let dgram = server.process(dgram2, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram(); // Initial, HS
    assert!(dgram.is_some());
    drop(client.process(dgram, now()).dgram()); // Ingest, drop any ACK.
    client.authenticated(AuthenticationStatus::Ok, now());
    let dgram = client.process_output(now()).dgram(); // Send Finished
    assert!(dgram.is_some());
    let State::Closing { error: err, .. } = client.state() else {
        panic!("client should be closing");
    };
    let CloseReason::Transport(Error::EchRetry(fallback_config)) = err else {
        panic!("client should provide fallback config");
    };
    assert_eq!(fallback_config, server.ech_config());
}

/// Receiving a Retry is enough to infer something about the RTT.
/// Probably.
#[test]
fn implicit_rtt_retry() {
    const RTT: Duration = Duration::from_secs(2);
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();
    let mut now = now();

    let dgram = client.process_output(now).dgram();
    now += RTT / 2;
    let dgram = server.process(dgram, now).dgram();
    assertions::assert_retry(dgram.as_ref().unwrap());
    now += RTT / 2;
    client.process_input(dgram.unwrap(), now);

    assert_eq!(client.stats().rtt, RTT);
    assert_dscp(&client.stats());
}

#[test]
fn retry_expired() {
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();
    let mut now = now();

    let dgram = client.process_output(now).dgram(); // Initial
    assert!(dgram.is_some());
    let dgram = server.process(dgram, now).dgram(); // Retry
    assert!(dgram.is_some());

    assertions::assert_retry(dgram.as_ref().unwrap());

    let dgram = client.process(dgram, now).dgram(); // Initial w/token
    assert!(dgram.is_some());

    now += Duration::from_mins(1); // Too long for Retry.
    let dgram = server.process(dgram, now).dgram(); // Initial, HS
    assert!(dgram.is_none());
    assert_dscp(&client.stats());
}

// Attempt a retry with 0-RTT, and have 0-RTT packets sent with the second ClientHello.
#[test]
fn retry_0rtt() {
    let mut server = default_server();
    let token = generate_ticket(&mut server);
    server.set_validation(ValidateAddress::Always);

    let mut client = default_client();
    client.enable_resumption(now(), &token).unwrap();

    let client_stream = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream, &[1, 2, 3]).unwrap();

    let dgram = client.process_output(now()).dgram(); // Initial
    let dgram2 = client.process_output(now()).dgram(); // Initial w/0-RTT
    assert!(dgram.is_some() && dgram2.is_some());
    assertions::assert_coalesced_0rtt(dgram2.as_ref().unwrap());
    _ = server.process(dgram, now()).dgram(); // Retry
    let dgram = server.process(dgram2, now()).dgram(); // Retry
    assert!(dgram.is_some());
    assertions::assert_retry(dgram.as_ref().unwrap());

    // After retry, there should be a token and still coalesced 0-RTT.
    let dgram = client.process(dgram, now()).dgram(); // Initial
    let dgram2 = client.process_output(now()).dgram(); // Initial w/0-RTT
    assert!(dgram.is_some() && dgram2.is_some());
    assertions::assert_coalesced_0rtt(dgram2.as_ref().unwrap());

    _ = server.process(dgram, now()).dgram(); // ACK
    let dgram = server.process(dgram2, now()).dgram(); // Initial, HS
    assert!(dgram.is_some());
    let dgram = client.process(dgram, now()).dgram();
    let dgram = server.process(dgram, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    // Note: the client doesn't need to authenticate the server here
    // as there is no certificate; authentication is based on the ticket.
    assert!(dgram.is_some());
    assert_eq!(*client.state(), State::Connected);
    let dgram = server.process(dgram, now()).dgram(); // (done)
    assert!(dgram.is_some());
    connected_server(&server);
    assert!(client.tls_info().unwrap().resumed());
    assert_dscp(&client.stats());
}

#[test]
fn retry_different_ip() {
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();

    let dgram = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some());
    let dgram = server.process(dgram, now()).dgram(); // Retry
    assert!(dgram.is_some());

    assertions::assert_retry(dgram.as_ref().unwrap());

    let dgram = client.process(dgram, now()).dgram(); // Initial w/token
    assert!(dgram.is_some());

    // Change the source IP on the address from the client.
    let dgram = dgram.unwrap();
    let other_v4 = IpAddr::V4(Ipv4Addr::new(127, 0, 0, 2));
    let other_addr = SocketAddr::new(other_v4, 443);
    let from_other = Datagram::new(other_addr, dgram.destination(), dgram.tos(), &dgram[..]);
    let dgram = server.process(Some(from_other), now()).dgram();
    assert!(dgram.is_none());
    assert_dscp(&client.stats());
}

#[test]
fn new_token_different_ip() {
    let mut server = default_server();
    let token = generate_ticket(&mut server);
    server.set_validation(ValidateAddress::NoToken);

    let mut client = default_client();
    client.enable_resumption(now(), &token).unwrap();

    let dgram = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some());
    assertions::assert_initial(dgram.as_ref().unwrap(), true);

    // Now rewrite the source address.
    let d = dgram.unwrap();
    let src = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 2)), d.source().port());
    let dgram = Some(Datagram::new(src, d.destination(), d.tos(), &d[..]));
    let dgram = server.process(dgram, now()).dgram(); // Retry
    assert!(dgram.is_some());
    assertions::assert_retry(dgram.as_ref().unwrap());
    assert_dscp(&client.stats());
}

#[test]
fn new_token_expired() {
    let mut server = default_server();
    let token = generate_ticket(&mut server);
    server.set_validation(ValidateAddress::NoToken);

    let mut client = default_client();
    client.enable_resumption(now(), &token).unwrap();

    let dgram = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some());
    assertions::assert_initial(dgram.as_ref().unwrap(), true);

    // Now move into the future.
    // We can't go too far or we'll overflow our field.  Not when checking,
    // but when trying to generate another Retry.  A month is fine.
    let the_future = now() + Duration::from_hours(24 * 30);
    let d = dgram.unwrap();
    let src = SocketAddr::new(d.source().ip(), d.source().port() + 1);
    let dgram = Some(Datagram::new(src, d.destination(), d.tos(), &d[..]));
    let dgram = server.process(dgram, the_future).dgram(); // Retry
    assert!(dgram.is_some());
    assertions::assert_retry(dgram.as_ref().unwrap());
    assert_dscp(&client.stats());
}

#[test]
fn retry_after_initial() {
    neqo_common::log::init(None);
    let mut server = default_server();
    let mut retry_server = default_server();
    retry_server.set_validation(ValidateAddress::Always);
    let mut client = default_client();

    let cinit = client.process_output(now()).dgram(); // Initial
    let cinit2 = client.process_output(now()).dgram(); // Initial
    assert!(cinit.is_some() && cinit2.is_some());
    _ = server.process(cinit.clone(), now()).dgram(); // Initial
    let server_initial = server.process(cinit2, now()).dgram().unwrap();
    let server_handshake = server.process_output(now()).dgram().unwrap();

    // We need to have the client just process the Initial.
    let dgram = client.process(Some(server_initial), now()).dgram();
    assert!(dgram.is_some());
    assert_ne!(*client.state(), State::Connected);

    let retry = retry_server.process(cinit, now()).dgram(); // Retry!
    assert!(retry.is_some());
    assertions::assert_retry(retry.as_ref().unwrap());

    // The client should ignore the retry.
    let junk = client.process(retry, now()).dgram();
    assert!(junk.is_none());

    // Either way, the client should still be able to process the server handshake and connect.
    let dgram = client.process(Some(server_handshake), now()).dgram();
    assert!(dgram.is_some()); // Drop this one.
    assert!(test_fixture::maybe_authenticate(&mut client));
    let dgram = server.process(dgram, now()).dgram();
    let dgram = client.process(dgram, now()).dgram();
    assert!(dgram.is_some());

    assert_eq!(*client.state(), State::Connected);
    let dgram = server.process(dgram, now()).dgram(); // (done)
    assert!(dgram.is_some());
    connected_server(&server);
    assert_dscp(&client.stats());
}

#[test]
fn retry_bad_integrity() {
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();

    let dgram = client.process_output(now()).dgram(); // Initial
    let dgram2 = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some() && dgram2.is_some());
    _ = server.process(dgram, now()).dgram(); // Retry
    let dgram = server.process(dgram2, now()).dgram(); // Retry
    assert!(dgram.is_some());

    let retry = &dgram.as_ref().unwrap();
    assertions::assert_retry(retry);

    let mut tweaked = retry.to_vec();
    tweaked[retry.len() - 1] ^= 0x45; // damage the auth tag
    let tweaked_packet = Datagram::new(retry.source(), retry.destination(), retry.tos(), tweaked);

    // The client should ignore this packet.
    let dgram = client.process(Some(tweaked_packet), now()).dgram();
    assert!(dgram.is_none());
    assert_dscp(&client.stats());
}

#[test]
fn retry_bad_token() {
    let mut client = default_client();
    let mut retry_server = default_server();
    retry_server.set_validation(ValidateAddress::Always);
    let mut server = default_server();

    // Send a retry to one server, then replay it to the other.
    let client_initial1 = client.process_output(now()).dgram();
    assert!(client_initial1.is_some());
    let retry = retry_server.process(client_initial1, now()).dgram();
    assert!(retry.is_some());
    let client_initial2 = client.process(retry, now()).dgram();
    assert!(client_initial2.is_some());

    let dgram = server.process(client_initial2, now()).dgram();
    assert!(dgram.is_none());
    assert_dscp(&client.stats());
}

// This is really a client test, but we need a server with Retry to test it.
// In this test, the client sends Initial on PTO.  The Retry should cause
// all loss recovery timers to be reset, but we had a bug where the PTO timer
// was not properly reset.  This tests that the client generates a new Initial
// in response to receiving a Retry, even after it sends the Initial on PTO.
#[test]
fn retry_after_pto() {
    let mut client = default_client();
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut now = now();

    let ci = client.process_output(now).dgram();
    let ci2 = client.process_output(now).dgram();
    assert!(ci.is_some() && ci2.is_some()); // sit on this for a bit

    // Let PTO fire on the client and then let it exhaust its PTO packets.
    now += Duration::from_secs(1);
    let pto = client.process_output(now).dgram();
    assert!(pto.unwrap().len() >= MIN_INITIAL_PACKET_SIZE);
    _ = client.process_output(now).dgram();
    let cb = client.process_output(now).callback();
    assert_ne!(cb, Duration::new(0, 0));

    _ = server.process(ci, now).dgram();
    let retry = server.process(ci2, now).dgram();
    assertions::assert_retry(retry.as_ref().unwrap());

    let ci2 = client.process(retry, now).dgram();
    assert!(ci2.unwrap().len() >= MIN_INITIAL_PACKET_SIZE);
    assert_dscp(&client.stats());
}

#[test]
fn vn_after_retry() {
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);
    let mut client = default_client();

    let dgram = client.process_output(now()).dgram(); // Initial
    let dgram2 = client.process_output(now()).dgram(); // Initial
    assert!(dgram.is_some() && dgram2.is_some());
    _ = server.process(dgram, now()).dgram(); // Retry
    let dgram = server.process(dgram2, now()).dgram(); // Retry
    assert!(dgram.is_some());

    assertions::assert_retry(dgram.as_ref().unwrap());

    let dgram = client.process(dgram, now()).dgram(); // Initial w/token
    assert!(dgram.is_some());
    let dgram = server.process(dgram, now()).dgram();
    _ = client.process(dgram, now()).dgram();

    let mut encoder = Encoder::default();
    encoder.encode_byte(0x80);
    encoder.encode([0; 4]); // Zero version == VN.
    encoder.encode_vec(1, &client.odcid().unwrap()[..]);
    encoder.encode_vec(1, &[]);
    encoder.encode_uint(4, 0x5a5a_6a6a_u64);
    let vn = datagram(encoder.into());

    assert_ne!(
        client.process(Some(vn), now()).callback(),
        Duration::from_secs(0)
    );
    assert_dscp(&client.stats());
}

// This tests a simulated on-path attacker that intercepts the first
// client Initial packet and spoofs a retry.
// The tricky part is in rewriting the second client Initial so that
// the server doesn't reject the Initial for having a bad token.
// The client is the only one that can detect this, and that is because
// the original connection ID is not in transport parameters.
//
// Note that this depends on having the server produce a CID that is
// at least 8 bytes long.  Otherwise, the second Initial won't have a
// long enough connection ID.
#[test]
fn mitm_retry() {
    // This test decrypts packets and hence does not work with MLKEM and packet number randomization
    // enabled.
    let mut client = test_fixture::new_client::<CountingConnectionIdGenerator>(
        ConnectionParameters::default().mlkem(false),
    );
    let mut retry_server = default_server();
    retry_server.set_validation(ValidateAddress::Always);
    let mut server = default_server();

    // Trigger initial and a second client Initial.
    let client_initial1 = client.process_output(now()).dgram();
    assert!(client_initial1.is_some());
    let retry = retry_server.process(client_initial1, now()).dgram();
    assert!(retry.is_some());
    let client_initial2 = client.process(retry, now()).dgram();
    assert!(client_initial2.is_some());

    // Now to start the epic process of decrypting the packet,
    // rewriting the header to remove the token, and then re-encrypting.
    let client_initial2 = client_initial2.unwrap();
    let (protected_header, d_cid, s_cid, payload) =
        decode_initial_header(&client_initial2, Role::Client).unwrap();

    // Now we have enough information to make keys.
    let (aead_enc, aead_dec, hp) = initial_aead_and_hp(d_cid, Role::Client);
    let (header, pn) = header_protection::remove(&hp, protected_header, payload);
    let pn_len = header.len() - protected_header.len();

    // Decrypt.
    let mut plaintext_buf = vec![0; client_initial2.len()];
    let plaintext = aead_dec
        .decrypt(pn, &header, &payload[pn_len..], &mut plaintext_buf)
        .unwrap();

    // Now re-encode without the token.
    let mut enc = Encoder::with_capacity(header.len());
    enc.encode(&header[..5])
        .encode_vec(1, d_cid)
        .encode_vec(1, s_cid)
        .encode_vvec(&[])
        .encode_len(payload.len());
    let pn_offset = enc.len();
    let notoken_header = enc.encode_uint(pn_len, pn).as_ref().to_vec();
    trace!("notoken_header={}", HexWithLen::new(&notoken_header));

    // Encrypt.
    let mut notoken_packet = Encoder::with_capacity(MIN_INITIAL_PACKET_SIZE)
        .encode(&notoken_header)
        .as_ref()
        .to_vec();
    notoken_packet.resize_with(MIN_INITIAL_PACKET_SIZE, u8::default);
    aead_enc
        .encrypt(
            pn,
            &notoken_header,
            plaintext,
            &mut notoken_packet[notoken_header.len()..],
        )
        .unwrap();
    // Unlike with decryption, don't truncate.
    // All MIN_INITIAL_PACKET_SIZE bytes are needed to reach the minimum datagram size.

    header_protection::apply(&hp, &mut notoken_packet, pn_offset..(pn_offset + pn_len));
    trace!("packet={}", HexWithLen::new(&notoken_packet));

    let new_datagram = Datagram::new(
        client_initial2.source(),
        client_initial2.destination(),
        client_initial2.tos(),
        notoken_packet,
    );
    debug!("passing modified Initial to the main server");
    let dgram = server.process(Some(new_datagram), now()).dgram();
    assert!(dgram.is_some());

    let dgram = client.process(dgram, now()).dgram(); // Generate an ACK.
    assert!(dgram.is_some());
    let dgram = server.process(dgram, now()).dgram();
    assert!(dgram.is_none());
    assert!(test_fixture::maybe_authenticate(&mut client));
    let dgram = client.process(dgram, now()).dgram();
    assert!(dgram.is_some()); // Client sending CLOSE_CONNECTIONs
    assert!(matches!(
        *client.state(),
        State::Closing {
            error: CloseReason::Transport(Error::ProtocolViolation),
            ..
        }
    ));
    assert_dscp(&client.stats());
}

/// Server should reject Initial with DCID < 8 bytes.
#[test]
fn retry_short_dcid() {
    let mut client = test_fixture::new_client::<CountingConnectionIdGenerator>(
        ConnectionParameters::default().mlkem(false),
    );
    let mut server = default_server();
    server.set_validation(ValidateAddress::Always);

    let client_initial1 = client.process_output(now()).dgram().unwrap();
    let (protected_header, d_cid, s_cid, payload) =
        decode_initial_header(&client_initial1, Role::Client).unwrap();

    let short_dcid = &[0x01, 0x02, 0x03, 0x04];

    // Decrypt with the original DCID.
    let (_, aead_orig, hp_orig) = initial_aead_and_hp(d_cid, Role::Client);
    let (header, pn) = header_protection::remove(&hp_orig, protected_header, payload);
    let pn_len = header.len() - protected_header.len();

    let mut plaintext_buf = vec![0; client_initial1.len()];
    let plaintext = aead_orig
        .decrypt(pn, &header, &payload[pn_len..], &mut plaintext_buf)
        .unwrap();

    // Re-encode with short DCID.
    let mut enc = Encoder::with_capacity(header.len());
    enc.encode(&header[..5])
        .encode_vec(1, short_dcid)
        .encode_vec(1, s_cid)
        .encode_vvec(&[])
        .encode_len(payload.len());
    let pn_offset = enc.len();
    let short_dcid_header = enc.encode_uint(pn_len, pn).as_ref().to_vec();

    // Encrypt with keys derived from short DCID.
    let (aead_short, _, hp_short) = initial_aead_and_hp(short_dcid, Role::Client);
    let mut short_dcid_packet = Encoder::with_capacity(MIN_INITIAL_PACKET_SIZE)
        .encode(&short_dcid_header)
        .as_ref()
        .to_vec();
    short_dcid_packet.resize_with(MIN_INITIAL_PACKET_SIZE, u8::default);
    aead_short
        .encrypt(
            pn,
            &short_dcid_header,
            plaintext,
            &mut short_dcid_packet[short_dcid_header.len()..],
        )
        .unwrap();
    header_protection::apply(
        &hp_short,
        &mut short_dcid_packet,
        pn_offset..(pn_offset + pn_len),
    );

    let dgram_with_short_dcid = Datagram::new(
        client_initial1.source(),
        client_initial1.destination(),
        client_initial1.tos(),
        short_dcid_packet,
    );

    let retry = server.process(Some(dgram_with_short_dcid), now()).dgram();
    assert!(
        retry.is_none(),
        "Server should drop Initial with short DCID"
    );
}
