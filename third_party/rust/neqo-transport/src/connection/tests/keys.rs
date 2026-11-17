// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::Datagram;
use test_fixture::{
    assertions::{is_handshake, is_initial},
    now, split_datagram,
};
use tracing::debug;

use super::{
    super::{
        super::{CloseReason, ERROR_AEAD_LIMIT_REACHED},
        Connection, ConnectionParameters, Error, Output, State, StreamType,
    },
    AT_LEAST_PTO, assert_error, connect, connect_force_idle, default_client, default_server,
    exchange_ticket, maybe_authenticate, resumed_server, send_and_receive, send_something,
};
use crate::{
    MIN_INITIAL_PACKET_SIZE,
    connection::test_internal::FrameWriter,
    crypto::{OVERWRITE_INVOCATIONS, UPDATE_WRITE_KEYS_AT},
    frame::FrameType,
    packet,
};

fn check_discarded(
    peer: &mut Connection,
    pkt: &Datagram,
    response: bool,
    expected_drops: usize,
    expected_dups: usize,
) {
    // Make sure to flush any saved datagrams before doing this.
    drop(peer.process_output(now()));

    let before = peer.stats();
    let out = peer.process(Some(pkt.clone()), now());
    assert_eq!(out.as_dgram_ref().is_some(), response);
    let after = peer.stats();
    assert_eq!(expected_drops, after.dropped_rx - before.dropped_rx);
    assert_eq!(expected_dups, after.dups_rx - before.dups_rx);
}

fn assert_update_blocked(c: &mut Connection) {
    assert_eq!(
        c.initiate_key_update().unwrap_err(),
        Error::KeyUpdateBlocked
    );
}

fn overwrite_invocations(n: packet::Number) {
    OVERWRITE_INVOCATIONS.set(Some(n));
}

#[test]
fn discarded_initial_keys() {
    debug!("---- client: generate CH");
    let mut client = default_client();
    let c_hs_1 = client.process_output(now()).dgram();
    let c_hs_2 = client.process_output(now()).dgram();
    assert!(c_hs_1.is_some() && c_hs_2.is_some());
    assert_eq!(c_hs_1.as_ref().unwrap().len(), client.plpmtu());
    assert_eq!(c_hs_2.as_ref().unwrap().len(), client.plpmtu());

    debug!("---- server: CH -> SH, EE, CERT, CV, FIN");
    let mut server = default_server();
    server.process_input(c_hs_1.clone().unwrap(), now());
    let s_hs_1 = server.process(c_hs_2, now()).dgram();
    assert!(s_hs_1.is_some());
    let s_hs_2 = server.process_output(now()).dgram();

    debug!("---- client: cert verification");
    client.process_input(s_hs_1.clone().unwrap(), now());
    let out = client.process(s_hs_2, now()).dgram();
    assert!(out.is_some());

    // The client has received a handshake packet. It will remove the Initial keys.
    // Check this by processing the Intial part of `s_hs_1` a second time.
    // That packet should be dropped.
    // The client will generate a Handshake packet here to avoid stalling.
    let (s_init, _s_hs) = split_datagram(&s_hs_1.unwrap());
    check_discarded(&mut client, &s_init, true, 1, 0);

    assert!(maybe_authenticate(&mut client));

    // The server has not removed the Initial keys yet, because it has not yet received a Handshake
    // packet from the client.
    // Check this by processing the Initial part of `c_hs_1` a second time.
    let (c_init, _c_hs) = split_datagram(c_hs_1.as_ref().unwrap());
    check_discarded(&mut server, &c_init, false, 0, 1);

    debug!("---- client: SH..FIN -> FIN");
    let c_fin = client.process_output(now()).dgram();
    assert!(c_fin.is_some());

    // The server will process the first Handshake packet.
    // After this the Initial keys will be dropped.
    let s_done = server.process(c_fin, now()).dgram();
    assert!(s_done.is_some());

    // The Initial keys are dropped at the server.
    // Check this by processing the Initial part of `c_hs_1` a third time.
    check_discarded(&mut server, &c_init, false, 1, 0);
}

#[test]
fn key_update_client() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    assert_eq!(client.get_epochs(), (Some(3), Some(3))); // (write, read)
    assert_eq!(server.get_epochs(), (Some(3), Some(3)));

    assert!(client.initiate_key_update().is_ok());
    assert_update_blocked(&mut client);

    // Initiating an update should only increase the write epoch.
    assert_eq!(
        ConnectionParameters::DEFAULT_IDLE_TIMEOUT,
        client.process_output(now).callback()
    );
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));

    // Send something to propagate the update.
    // Note that the server will acknowledge immediately when RTT is zero.
    assert!(send_and_receive(&mut client, &mut server, now).is_some());

    // The server should now be waiting to discharge read keys.
    assert_eq!(server.get_epochs(), (Some(4), Some(3)));
    let res = server.process_output(now);
    if let Output::Callback(t) = res {
        assert!(t < ConnectionParameters::DEFAULT_IDLE_TIMEOUT);
    } else {
        panic!("server should now be waiting to clear keys");
    }

    // Without having had time to purge old keys, more updates are blocked.
    // The spec would permits it at this point, but we are more conservative.
    assert_update_blocked(&mut client);
    // The server can't update until it receives an ACK for a packet.
    assert_update_blocked(&mut server);

    // Waiting now for at least a PTO should cause the server to drop old keys.
    // But at this point the client hasn't received a key update from the server.
    // It will be stuck with old keys.
    now += AT_LEAST_PTO;
    let dgram = client.process_output(now).dgram();
    assert!(dgram.is_some()); // Drop this packet.
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));
    drop(server.process_output(now));
    assert_eq!(server.get_epochs(), (Some(4), Some(4)));

    // Even though the server has updated, it hasn't received an ACK yet.
    assert_update_blocked(&mut server);

    // Now get an ACK from the server.
    // The previous PTO packet (see above) was dropped, so we should get an ACK here.
    let dgram = send_and_receive(&mut client, &mut server, now);
    assert!(dgram.is_some());
    let res = client.process(dgram, now);
    // This is the first packet that the client has received from the server
    // with new keys, so its read timer just started.
    if let Output::Callback(t) = res {
        assert!(t < ConnectionParameters::DEFAULT_IDLE_TIMEOUT);
    } else {
        panic!("client should now be waiting to clear keys");
    }

    assert_update_blocked(&mut client);
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));
    // The server can't update until it gets something from the client.
    assert_update_blocked(&mut server);

    now += AT_LEAST_PTO;
    drop(client.process_output(now));
    assert_eq!(client.get_epochs(), (Some(4), Some(4)));
}

#[test]
fn key_update_consecutive() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let now = now();

    assert!(server.initiate_key_update().is_ok());
    assert_eq!(server.get_epochs(), (Some(4), Some(3)));

    // Server sends something.
    // Send twice and drop the first to induce an ACK from the client.
    drop(send_something(&mut server, now)); // Drop this.

    // Another packet from the server will cause the client to ACK and update keys.
    let dgram = send_and_receive(&mut server, &mut client, now);
    assert!(dgram.is_some());
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));

    // Have the server process the ACK.
    if let Output::Callback(_) = server.process(dgram, now) {
        assert_eq!(server.get_epochs(), (Some(4), Some(3)));
        // Now move the server temporarily into the future so that it
        // rotates the keys.  The client stays in the present.
        drop(server.process_output(now + AT_LEAST_PTO));
        assert_eq!(server.get_epochs(), (Some(4), Some(4)));
    } else {
        panic!("server should have a timer set");
    }

    // Now update keys on the server again.
    assert!(server.initiate_key_update().is_ok());
    assert_eq!(server.get_epochs(), (Some(5), Some(4)));

    let dgram = send_something(&mut server, now + AT_LEAST_PTO);

    // However, as the server didn't wait long enough to update again, the
    // client hasn't rotated its keys, so the packet gets dropped.
    check_discarded(&mut client, &dgram, false, 1, 0);
}

// Receiving the same key update packet twice must not be treated as two key
// updates. The packet carrying a new key phase is decrypted with a key state
// that has recorded a single packet number, which is how `needs_update` detects
// a key update. Redelivering the packet leaves that state unchanged, so
// `needs_update` fires again before the read keys rotate.
#[test]
fn key_update_received_twice() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let now = now();

    // The server initiates a key update, so its next packet carries the new key
    // phase.
    assert!(server.initiate_key_update().is_ok());
    assert_eq!(server.get_epochs(), (Some(4), Some(3)));
    let dgram = send_something(&mut server, now);

    // The client updates its write keys and arms the read-key timer, but does
    // not rotate its read keys until that timer fires.
    client.process_input(dgram.clone(), now);
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));

    // The same packet again, before the timer fires, used to trip a debug
    // assertion in `CryptoStates::maybe_update_write`. It must be a no-op.
    client.process_input(dgram, now);
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));
}

// The same key update packet, redelivered after the read keys have rotated,
// must not be treated as a fresh update. By then `read_update_time` is cleared,
// so a guard keyed on it would let the duplicate through: the client would roll
// its keys forward again (with assertions compiled out, no crash) and drive an
// unsolicited key update, ratcheting the key phase ahead of the server.
#[test]
fn key_update_received_after_rotation() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);
    let mut now = now();

    assert!(server.initiate_key_update().is_ok());
    let dgram = send_something(&mut server, now);

    // The client responds to the update and arms the read-key timer.
    client.process_input(dgram.clone(), now);
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));

    // Let the timer fire so the client rotates its read keys to the new epoch.
    now += AT_LEAST_PTO;
    drop(client.process_output(now));
    assert_eq!(client.get_epochs(), (Some(4), Some(4)));

    // Redelivering the packet now must not start another key update.
    client.process_input(dgram, now);
    assert_eq!(client.get_epochs(), (Some(4), Some(4)));
}

// Key updates can't be initiated too early.
#[test]
fn key_update_before_confirmed() {
    let mut client = default_client();
    assert_update_blocked(&mut client);
    let mut server = default_server();
    assert_update_blocked(&mut server);

    // Client Initial
    let dgram = client.process_output(now()).dgram();
    let dgram2 = client.process_output(now()).dgram();
    assert!(dgram.is_some() && dgram2.is_some());
    assert_update_blocked(&mut client);

    // Server Initial + Handshake
    server.process_input(dgram.unwrap(), now());
    let dgram = server.process(dgram2, now()).dgram();
    assert!(dgram.is_some());
    assert_update_blocked(&mut server);

    let dgram = client.process(dgram, now()).dgram();
    assert!(dgram.is_some());
    assert_update_blocked(&mut client);

    let dgram = server.process(dgram, now()).dgram();
    assert!(dgram.is_some());
    assert_update_blocked(&mut server);

    // Client Handshake
    client.process_input(dgram.unwrap(), now());
    assert_update_blocked(&mut client);

    assert!(maybe_authenticate(&mut client));
    assert_update_blocked(&mut client);

    let dgram = client.process_output(now()).dgram();
    assert!(dgram.is_some());
    assert_update_blocked(&mut client);

    // Server HANDSHAKE_DONE
    let dgram = server.process(dgram, now()).dgram();
    assert!(dgram.is_some());
    assert!(server.initiate_key_update().is_ok());

    // Client receives HANDSHAKE_DONE
    let dgram = client.process(dgram, now()).dgram();
    assert!(dgram.is_none());
    assert!(client.initiate_key_update().is_ok());
}

#[test]
fn exhaust_write_keys() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    overwrite_invocations(0);
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert!(client.stream_send(stream_id, b"explode!").is_ok());
    let dgram = client.process_output(now()).dgram();
    assert!(dgram.is_none());
    assert!(matches!(
        client.state(),
        State::Closed(CloseReason::Transport(Error::KeysExhausted))
    ));
}

#[test]
fn exhaust_read_keys() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let dgram = send_something(&mut client, now());

    overwrite_invocations(0);
    let dgram = server.process(Some(dgram), now()).dgram();
    assert!(matches!(
        server.state(),
        State::Closed(CloseReason::Transport(Error::KeysExhausted))
    ));

    client.process_input(dgram.unwrap(), now());
    assert!(matches!(
        client.state(),
        State::Draining {
            error: CloseReason::Transport(Error::Peer(ERROR_AEAD_LIMIT_REACHED)),
            ..
        }
    ));
}

#[test]
fn automatic_update_write_keys() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    overwrite_invocations(UPDATE_WRITE_KEYS_AT);
    drop(send_something(&mut client, now()));
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));
}

#[test]
fn automatic_update_write_keys_later() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    overwrite_invocations(UPDATE_WRITE_KEYS_AT + 2);
    // No update after the first.
    drop(send_something(&mut client, now()));
    assert_eq!(client.get_epochs(), (Some(3), Some(3)));
    // The second will update though.
    drop(send_something(&mut client, now()));
    assert_eq!(client.get_epochs(), (Some(4), Some(3)));
}

#[test]
fn automatic_update_write_keys_blocked() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    // An outstanding key update will block the automatic update.
    client.initiate_key_update().unwrap();

    overwrite_invocations(UPDATE_WRITE_KEYS_AT);
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert!(client.stream_send(stream_id, b"explode!").is_ok());
    let dgram = client.process_output(now()).dgram();
    // Not being able to update is fatal.
    assert!(dgram.is_none());
    assert!(matches!(
        client.state(),
        State::Closed(CloseReason::Transport(Error::KeysExhausted))
    ));
}

/// Test that when both Initial and Handshake packets are sent together due to PTO,
/// the resulting datagram is properly padded to `MIN_INITIAL_PACKET_SIZE` (1200 bytes).
///
/// See RFC 9000 14.1 <https://www.rfc-editor.org/rfc/rfc9000.html#name-initial-datagram-size>.
#[test]
fn initial_handshake_pto_padding() {
    let mut client = default_client();
    let mut now = now();

    let c_init1 = client.process_output(now).dgram();
    let c_init2 = client.process_output(now).dgram();
    assert!(c_init1.is_some() && c_init2.is_some());

    let mut server = default_server();
    server.process_input(c_init1.unwrap(), now);
    let s_hs1 = server.process(c_init2, now).dgram();
    assert!(s_hs1.is_some());
    let s_hs2 = server.process_output(now).dgram();
    assert!(s_hs2.is_some());

    // Client receives server handshake messages but we immediately advance time
    // to trigger PTO before allowing any client output. This simulates the
    // scenario where all client packets are lost.
    client.process_input(s_hs1.unwrap(), now);
    client.process_input(s_hs2.unwrap(), now);
    now += AT_LEAST_PTO;

    // Collect all PTO datagrams - there may be multiple.
    let mut pto_dgrams = Vec::new();
    while let Some(dgram) = client.process_output(now).dgram() {
        pto_dgrams.push(dgram);
    }
    assert!(!pto_dgrams.is_empty());

    // Iterate over all datagrams to find one with coalesced Initial+Handshake.
    // Any datagram containing an Initial packet must be properly padded.
    let mut found_coalesced = false;
    for dgram in &pto_dgrams {
        let (first, second) = split_datagram(dgram);
        if is_initial(&first, false) {
            assert!(dgram.len() >= MIN_INITIAL_PACKET_SIZE);
            if let Some(hs) = &second {
                found_coalesced |= is_handshake(hs);
            }
        }
    }
    assert!(found_coalesced);
}

/// Adds `HANDSHAKE_DONE` to the third packet: the 1-RTT packet after Initial and Handshake.
#[derive(Default)]
struct EarlyHandshakeDoneWriter {
    packets: usize,
}

impl FrameWriter for EarlyHandshakeDoneWriter {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        const LONG_HEADER_PACKETS: usize = 2; // Initial and Handshake.
        self.packets += 1;
        if self.packets == LONG_HEADER_PACKETS + 1 {
            builder.encode_varint(FrameType::HandshakeDone);
        }
    }
}

/// Delivers the server's flight with `HANDSHAKE_DONE` before the client sends any Handshake
/// packet.
fn early_handshake_done(client: &mut Connection, server: &mut Connection) {
    let c1 = client.process_output(now()).dgram();
    let c2 = client.process_output(now()).dgram();
    server.process_input(c1.unwrap(), now());
    let s1 = server.process(c2, now()).dgram();
    let s2 = server.test_write_frames(EarlyHandshakeDoneWriter::default(), now());
    client.process_input(s1.unwrap(), now());
    client.process_input(s2.dgram().unwrap(), now());
}

#[test]
fn early_handshake_done_resumption() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);
    let token = exchange_ticket(&mut client, &mut server, now());

    let mut client = default_client();
    client.enable_resumption(now(), token).unwrap();
    let mut server = resumed_server(&client);
    early_handshake_done(&mut client, &mut server);
    assert_eq!(client.stats().frame_rx.handshake_done, 1);
    assert_error(&client, &CloseReason::Transport(Error::ProtocolViolation));
}

/// The 1-RTT packet is saved until the client authenticates the server.
#[test]
fn early_handshake_done_authenticated() {
    let mut client = default_client();
    let mut server = default_server();
    early_handshake_done(&mut client, &mut server);
    assert_eq!(client.stats().saved_datagrams, 1);
    assert!(maybe_authenticate(&mut client));
    assert_eq!(client.stats().frame_rx.handshake_done, 1);
    assert_error(&client, &CloseReason::Transport(Error::ProtocolViolation));
}

/// An ACK-only Handshake packet sent while authentication is pending drops Initial,
/// but the client has still not sent its Finished.
#[test]
fn early_handshake_done_after_handshake_ack() {
    let mut client = default_client();
    let mut server = default_server();
    let c1 = client.process_output(now()).dgram();
    let c2 = client.process_output(now()).dgram();
    server.process_input(c1.unwrap(), now());
    let s1 = server.process(c2, now()).dgram();
    let s2 = server.test_write_frames(EarlyHandshakeDoneWriter::default(), now());
    let (s2_hs, s2_short) = split_datagram(&s2.dgram().unwrap());
    client.process_input(s1.unwrap(), now());
    client.process_input(s2_hs, now());
    assert!(client.process_output(now()).dgram().is_some());
    client.process_input(s2_short.unwrap(), now());
    assert!(maybe_authenticate(&mut client));
    assert_eq!(client.stats().frame_rx.handshake_done, 1);
    assert_error(&client, &CloseReason::Transport(Error::ProtocolViolation));
}
