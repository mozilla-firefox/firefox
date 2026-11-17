// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cell::RefCell, num::NonZeroUsize, rc::Rc, time::Duration};

use neqo_common::{Encoder, event::Provider as _, to_u64};

use super::{
    AT_LEAST_PTO, assert_error, connect_force_idle, default_server, new_client, new_server, now,
};
use crate::{
    CloseReason, Connection, ConnectionParameters, Error, MAX_DATAGRAM_FRAME_SIZE,
    MIN_INITIAL_PACKET_SIZE, Pmtud, Stats, StreamId, StreamType,
    connection::tests::{DEFAULT_ADDR, default_client},
    datagram_queue::{DatagramQueueOutcome, DroppedDatagrams, default_max_age},
    events::{ConnectionEvent, OutgoingDatagramOutcome},
    frame::FrameType,
    packet,
    quic_datagrams::DATAGRAM_FRAME_TYPE_VARINT_LEN,
    recovery,
    send_stream::{RetransmissionPriority, TransmissionPriority},
    streams::SendGroupId,
};

/// Minimum overhead for a short header packet carrying a DATAGRAM frame:
/// - 8 bytes: minimum connection ID length (from `CountingConnectionIdGenerator`)
/// - 1 byte: short header (header form, spin, reserved, key phase, PN length)
/// - 1 byte: minimum packet number encoding
/// - 1 byte: DATAGRAM frame type
/// - 16 bytes: AEAD authentication tag
const MIN_DATAGRAM_PACKET_OVERHEAD: usize = 8 + 1 + 1 + 1 + 16;
const DATAGRAM_LEN_MTU: usize =
    Pmtud::default_plpmtu(DEFAULT_ADDR.ip()) - MIN_DATAGRAM_PACKET_OVERHEAD;
const DATA_MTU: &[u8] = &[1; DATAGRAM_LEN_MTU];
const DATA_BIGGER_THAN_MTU: &[u8] = &[0; 2 * DATAGRAM_LEN_MTU];
const _: () = assert!(DATA_BIGGER_THAN_MTU.len() > DATAGRAM_LEN_MTU);
const DATAGRAM_LEN_SMALLER_THAN_MTU: u64 = to_u64(MIN_INITIAL_PACKET_SIZE);
const _: () = assert!(DATAGRAM_LEN_SMALLER_THAN_MTU < to_u64(DATAGRAM_LEN_MTU));
const DATA_SMALLER_THAN_MTU: &[u8] = &[0; MIN_INITIAL_PACKET_SIZE - DATAGRAM_FRAME_TYPE_VARINT_LEN];
const _: () = assert!(DATA_SMALLER_THAN_MTU.len() < DATAGRAM_LEN_MTU);
const DATA_SMALLER_THAN_MTU_2: &[u8] = &[0; MIN_INITIAL_PACKET_SIZE / 2];
const _: () = assert!(DATA_SMALLER_THAN_MTU_2.len() < DATA_SMALLER_THAN_MTU.len());

/// A peer limit well below the path MTU, so that it, not the MTU, bounds the
/// DATAGRAM frame.
const FRAME_LIMIT: usize = 500;
const DATA_AT_FRAME_LIMIT: &[u8] = &[0; FRAME_LIMIT];
const _: () = assert!(FRAME_LIMIT < DATAGRAM_LEN_MTU);
const LENGTH_FIELD_LEN: usize = Encoder::varint_len(to_u64(FRAME_LIMIT));
const _: () = assert!(
    Encoder::varint_len(to_u64(
        FRAME_LIMIT - LENGTH_FIELD_LEN - DATAGRAM_FRAME_TYPE_VARINT_LEN
    )) == LENGTH_FIELD_LEN
);

struct InsertDatagram<'a> {
    data: &'a [u8],
}

impl crate::connection::test_internal::FrameWriter for InsertDatagram<'_> {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        builder.encode_varint(FrameType::Datagram);
        builder.encode(self.data);
    }
}

struct InsertEmptyDatagram;

impl crate::connection::test_internal::FrameWriter for InsertEmptyDatagram {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        builder.encode_varint(FrameType::DatagramWithLen);
        builder.encode_vvec(&[]);
    }
}

fn connect_datagram() -> (Connection, Connection) {
    let mut client =
        new_client(ConnectionParameters::default().datagram_size(MAX_DATAGRAM_FRAME_SIZE));
    let mut server =
        new_server(ConnectionParameters::default().datagram_size(MAX_DATAGRAM_FRAME_SIZE));
    connect_force_idle(&mut client, &mut server);
    (client, server)
}

#[test]
fn datagram_disabled_both() {
    let mut client = new_client(ConnectionParameters::default().datagram_size(0));
    let mut server = new_server(ConnectionParameters::default().datagram_size(0));
    connect_force_idle(&mut client, &mut server);

    assert_eq!(client.max_datagram_size(), Err(Error::NotAvailable));
    assert_eq!(server.max_datagram_size(), Err(Error::NotAvailable));
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Err(Error::TooMuchData)
    );
    assert_eq!(server.stats().frame_tx.datagram, 0);
    assert_eq!(
        server.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Err(Error::TooMuchData)
    );
    assert_eq!(server.stats().frame_tx.datagram, 0);
}

/// A side that advertises no datagram support can still send to a peer that
/// does, and the peer's limit, when below the MTU, is what `max_datagram_size`
/// reports.
#[test]
fn datagram_enabled_on_client() {
    let mut client =
        new_client(ConnectionParameters::default().datagram_size(DATAGRAM_LEN_SMALLER_THAN_MTU));
    let mut server = new_server(ConnectionParameters::default().datagram_size(0));
    connect_force_idle(&mut client, &mut server);

    assert_eq!(client.max_datagram_size(), Err(Error::NotAvailable));
    assert_eq!(
        server.max_datagram_size(),
        Ok(DATAGRAM_LEN_SMALLER_THAN_MTU - to_u64(DATAGRAM_FRAME_TYPE_VARINT_LEN))
    );
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Err(Error::TooMuchData)
    );
    let dgram_sent = server.stats().frame_tx.datagram;
    assert_eq!(
        server.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let out = server.process_output(now()).dgram().unwrap();
    assert_eq!(server.stats().frame_tx.datagram, dgram_sent + 1);

    client.process_input(out, now());
    assert!(matches!(
        client.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == DATA_SMALLER_THAN_MTU
    ));
}

#[test]
fn datagram_enabled_on_server() {
    let mut client = new_client(ConnectionParameters::default().datagram_size(0));
    let mut server =
        new_server(ConnectionParameters::default().datagram_size(DATAGRAM_LEN_SMALLER_THAN_MTU));
    connect_force_idle(&mut client, &mut server);

    assert_eq!(
        client.max_datagram_size(),
        Ok(DATAGRAM_LEN_SMALLER_THAN_MTU - to_u64(DATAGRAM_FRAME_TYPE_VARINT_LEN))
    );
    assert_eq!(server.max_datagram_size(), Err(Error::NotAvailable));
    assert_eq!(
        server.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Err(Error::TooMuchData)
    );
    let dgram_sent = client.stats().frame_tx.datagram;
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let out = client.process_output(now()).dgram().unwrap();
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);

    server.process_input(out, now());
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == DATA_SMALLER_THAN_MTU
    ));
}

#[test]
fn mtu_limit() {
    let (client, server) = connect_datagram();

    assert_eq!(
        client.max_datagram_size(),
        Ok((DATAGRAM_LEN_MTU).try_into().unwrap())
    );
    assert_eq!(
        server.max_datagram_size(),
        Ok((DATAGRAM_LEN_MTU).try_into().unwrap())
    );
}

#[test]
fn limit_data_size() {
    let (mut client, mut server) = connect_datagram();

    // Datagram can be queued because they are smaller than allowed by the peer,
    // but they cannot be sent.
    assert_eq!(
        server.enqueue_datagram(
            StreamId::new(0),
            DATA_BIGGER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );

    let dgram_dropped_s = server.stats().datagram_tx.dropped_too_big;
    let dgram_sent_s = server.stats().frame_tx.datagram;
    assert!(server.process_output(now()).dgram().is_none());
    assert_eq!(
        server.stats().datagram_tx.dropped_too_big,
        dgram_dropped_s + 1
    );
    assert_eq!(server.stats().frame_tx.datagram, dgram_sent_s);
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::OutgoingDatagramOutcome { id, outcome } if id == 1 && outcome == OutgoingDatagramOutcome::DroppedTooBig
    ));

    // The same test for the client side.
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_BIGGER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let dgram_sent_c = client.stats().frame_tx.datagram;
    assert!(client.process_output(now()).dgram().is_none());
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent_c);
    assert!(matches!(
        client.next_event().unwrap(),
        ConnectionEvent::OutgoingDatagramOutcome { id, outcome } if id == 1 && outcome == OutgoingDatagramOutcome::DroppedTooBig
    ));
}

#[test]
fn after_dgram_dropped_continue_writing_frames() {
    let (mut client, _) = connect_datagram();

    // Both are queued: the first is too big for any packet and is dropped
    // when the write loop reaches it, which must not stop the second,
    // smaller one from being written by the same `write_frames` pass. The
    // single `process_output` below covers both.
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_BIGGER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(2),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );

    let datagram_dropped = |e| {
        matches!(
        e,
        ConnectionEvent::OutgoingDatagramOutcome { id, outcome } if id == 1 && outcome == OutgoingDatagramOutcome::DroppedTooBig)
    };

    let dgram_dropped_c = client.stats().datagram_tx.dropped_too_big;
    let dgram_sent_c = client.stats().frame_tx.datagram;

    assert!(client.process_output(now()).dgram().is_some());
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent_c + 1);
    assert_eq!(
        client.stats().datagram_tx.dropped_too_big,
        dgram_dropped_c + 1
    );
    assert!(client.events().any(datagram_dropped));
}

#[test]
fn datagram_acked() {
    let (mut client, mut server) = connect_datagram();

    let dgram_sent = client.stats().frame_tx.datagram;
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let out = client.process_output(now()).dgram();
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);

    let dgram_received = server.stats().frame_rx.datagram;
    server.process_input(out.unwrap(), now());
    assert_eq!(server.stats().frame_rx.datagram, dgram_received + 1);
    let now = now() + AT_LEAST_PTO;
    // Ack should be sent
    let ack_sent = server.stats().frame_tx.ack;
    let out = server.process_output(now).dgram();
    assert_eq!(server.stats().frame_tx.ack, ack_sent + 1);

    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == DATA_SMALLER_THAN_MTU
    ));

    client.process_input(out.unwrap(), now);
    assert!(matches!(
        client.next_event().unwrap(),
        ConnectionEvent::OutgoingDatagramOutcome { id, outcome } if id == 1 && outcome == OutgoingDatagramOutcome::Acked
    ));
}

fn send_packet_and_get_server_event(
    client: &mut Connection,
    server: &mut Connection,
) -> ConnectionEvent {
    let out = client.process_output(now()).dgram();
    server.process_input(out.unwrap(), now());
    let mut events: Vec<_> = server
        .events()
        .filter_map(|evt| match evt {
            ConnectionEvent::RecvStreamReadable { .. } | ConnectionEvent::Datagram { .. } => {
                Some(evt)
            }
            _ => None,
        })
        .collect();
    // We should only get one event - either RecvStreamReadable or Datagram.
    assert_eq!(events.len(), 1);
    events.remove(0)
}

/// Write a datagram that is big enough to fill a packet, but then see that
/// normal priority stream data is sent first.
#[test]
fn datagram_after_stream_data() {
    let (mut client, mut server) = connect_datagram();

    // Write a datagram first.
    let dgram_sent = client.stats().frame_tx.datagram;
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );

    // Create a stream with normal priority and send some data.
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client
        .stream_send(stream_id, &[6; MIN_INITIAL_PACKET_SIZE])
        .unwrap();

    assert!(
        matches!(send_packet_and_get_server_event(&mut client, &mut server), ConnectionEvent::RecvStreamReadable { stream_id: s } if s == stream_id)
    );
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent);

    if let ConnectionEvent::Datagram(data) =
        &send_packet_and_get_server_event(&mut client, &mut server)
    {
        assert_eq!(data, DATA_MTU);
    } else {
        panic!();
    }
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);
}

#[test]
fn datagram_before_stream_data() {
    let (mut client, mut server) = connect_datagram();

    // Create a stream with low priority and send some data before datagram.
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client
        .stream_priority(
            stream_id,
            TransmissionPriority::Low,
            RetransmissionPriority::default(),
        )
        .unwrap();
    client
        .stream_send(stream_id, &[6; MIN_INITIAL_PACKET_SIZE])
        .unwrap();

    // Write a datagram.
    let dgram_sent = client.stats().frame_tx.datagram;
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );

    if let ConnectionEvent::Datagram(data) =
        &send_packet_and_get_server_event(&mut client, &mut server)
    {
        assert_eq!(data, DATA_MTU);
    } else {
        panic!();
    }
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);

    assert!(
        matches!(send_packet_and_get_server_event(&mut client, &mut server), ConnectionEvent::RecvStreamReadable { stream_id: s } if s == stream_id)
    );
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);
}

#[test]
fn datagram_lost() {
    let (mut client, _) = connect_datagram();

    let dgram_sent = client.stats().frame_tx.datagram;
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let _out = client.process_output(now()).dgram(); // This packet will be lost.
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);

    // Wait for PTO
    let now = now() + AT_LEAST_PTO;
    let dgram_sent2 = client.stats().frame_tx.datagram;
    let pings_sent = client.stats().frame_tx.ping;
    let dgram_lost = client.stats().datagram_tx.lost;
    let out = client.process_output(now).dgram();
    assert!(out.is_some()); // PING probing
    // Datagram is not sent again.
    assert_eq!(client.stats().frame_tx.ping, pings_sent + 1);
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent2);
    assert_eq!(client.stats().datagram_tx.lost, dgram_lost + 1);

    assert!(matches!(
        client.next_event().unwrap(),
        ConnectionEvent::OutgoingDatagramOutcome { id, outcome } if id == 1 && outcome == OutgoingDatagramOutcome::Lost
    ));
}

#[test]
fn datagram_sent_once() {
    let (mut client, _) = connect_datagram();

    let dgram_sent = client.stats().frame_tx.datagram;
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let _out = client.process_output(now()).dgram();
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);

    // Call process_output again should not send any new Datagram.
    assert!(client.process_output(now()).dgram().is_none());
    assert_eq!(client.stats().frame_tx.datagram, dgram_sent + 1);
}

/// RFC 9221, Section 3: `max_datagram_frame_size` is "the maximum size of a
/// DATAGRAM frame (including the frame type, length, and payload)". A
/// payload of `max_datagram_size()` bytes must therefore still fit the peer's
/// limit once the frame type is added.
#[test]
fn max_dgram_size_leaves_room_for_frame_header() {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().datagram_size(to_u64(FRAME_LIMIT)));
    connect_force_idle(&mut client, &mut server);

    let max = client
        .max_datagram_size()
        .expect("the peer enabled DATAGRAM frames");
    assert_eq!(max, to_u64(FRAME_LIMIT - DATAGRAM_FRAME_TYPE_VARINT_LEN));
    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            vec![0; usize::try_from(max).unwrap()],
            None,
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );

    let out = client.process_output(now()).dgram().unwrap();
    assert_eq!(client.stats().frame_tx.datagram, 1);
    server.process_input(out, now());
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data.len() == usize::try_from(max).unwrap()
    ));
}

/// The receiving side of the same rule (RFC 9221, Section 3): "An endpoint
/// that receives a DATAGRAM frame that is larger than the value it sent in
/// its `max_datagram_frame_size` transport parameter MUST terminate the
/// connection with an error of type `PROTOCOL_VIOLATION`."  The injected frame
/// has a payload of exactly the limit, so with its type byte it is one over.
#[test]
fn dgram_frame_over_local_limit_is_protocol_violation() {
    let mut client = new_client(ConnectionParameters::default().datagram_size(to_u64(FRAME_LIMIT)));
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    let out = server
        .test_write_frames(
            InsertDatagram {
                data: DATA_AT_FRAME_LIMIT,
            },
            now(),
        )
        .dgram()
        .unwrap();
    client.process_input(out, now());

    assert_error(&client, &CloseReason::Transport(Error::ProtocolViolation));
}

#[test]
fn queued_datagram_exceeding_updated_limit_is_dropped() {
    let (mut client, _) = connect_datagram();
    client
        .quic_datagrams
        .set_remote_datagram_size(to_u64(FRAME_LIMIT));
    let max = client
        .max_datagram_size()
        .expect("the peer enabled DATAGRAM frames");

    assert_eq!(
        client.enqueue_datagram(
            StreamId::new(0),
            vec![0; usize::try_from(max).unwrap()],
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );

    client
        .quic_datagrams
        .set_remote_datagram_size(to_u64(FRAME_LIMIT - 1));

    assert!(client.process_output(now()).dgram().is_none());
    assert_eq!(client.stats().frame_tx.datagram, 0);
    assert_eq!(client.stats().datagram_tx.dropped_too_big, 1);
    assert!(matches!(
        client.next_event().unwrap(),
        ConnectionEvent::OutgoingDatagramOutcome { id, outcome }
            if id == 1 && outcome == OutgoingDatagramOutcome::DroppedTooBig
    ));
}

#[test]
fn dgram_with_length_field_at_frame_limit() {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().datagram_size(to_u64(FRAME_LIMIT)));
    connect_force_idle(&mut client, &mut server);

    // Leave space for length field.
    let data1 = DATA_AT_FRAME_LIMIT
        [..FRAME_LIMIT - LENGTH_FIELD_LEN - DATAGRAM_FRAME_TYPE_VARINT_LEN]
        .to_vec();
    let data2 = vec![0; packet::Builder::MINIMUM_FRAME_SIZE];

    for data in [data1.clone(), data2.clone()] {
        assert_eq!(
            client.enqueue_datagram(StreamId::new(0), data, None, now(), SendGroupId::new(0), 0),
            Ok(DatagramQueueOutcome::Ok)
        );
    }

    let out = client.process_output(now()).dgram().unwrap();
    assert!(client.process_output(now()).dgram().is_none());

    server.process_input(out, now());
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == data1
    ));
    // The second datagram in the same packet proves that the first one used
    // the length-bearing form.
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == data2
    ));
}

#[test]
fn dgram_with_length_field_above_frame_limit() {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().datagram_size(to_u64(FRAME_LIMIT)));
    connect_force_idle(&mut client, &mut server);

    // DATAGRAM without space for the length field.
    let data1 = DATA_AT_FRAME_LIMIT[..FRAME_LIMIT - DATAGRAM_FRAME_TYPE_VARINT_LEN].to_vec();
    let data2 = vec![0; packet::Builder::MINIMUM_FRAME_SIZE];

    for data in [data1.clone(), data2.clone()] {
        assert_eq!(
            client.enqueue_datagram(StreamId::new(0), data, None, now(), SendGroupId::new(0), 0),
            Ok(DatagramQueueOutcome::Ok)
        );
    }

    let out = client.process_output(now()).dgram().unwrap();
    server.process_input(out, now());
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == data1
    ));
    assert!(server.next_event().is_none());

    // The second datagram is in a new packet because the first datagram frame would exceed
    // the peer's limit if the length field was added.
    let out = client.process_output(now()).dgram().unwrap();
    server.process_input(out, now());
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == data2
    ));
}

#[test]
fn dgram_unsupported() {
    let mut client = new_client(ConnectionParameters::default().datagram_size(0));
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    // The client advertised max_datagram_frame_size=0, so any DATAGRAM frame,
    // including an empty one, is a connection error (RFC 9221, Section 3).
    let out = server
        .test_write_frames(InsertEmptyDatagram, now())
        .dgram()
        .unwrap();
    client.process_input(out, now());

    assert_error(&client, &CloseReason::Transport(Error::ProtocolViolation));
}

fn send_datagram(sender: &mut Connection, receiver: &mut Connection, data: Vec<u8>) {
    let dgram_sent = sender.stats().frame_tx.datagram;
    assert_eq!(
        sender.enqueue_datagram(
            StreamId::new(0),
            data,
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let out = sender.process_output(now()).dgram().unwrap();
    assert_eq!(sender.stats().frame_tx.datagram, dgram_sent + 1);

    let dgram_received = receiver.stats().frame_rx.datagram;
    receiver.process_input(out, now());
    assert_eq!(receiver.stats().frame_rx.datagram, dgram_received + 1);
}

#[test]
fn multiple_datagram_events() {
    const DATA_SIZE: usize = MIN_INITIAL_PACKET_SIZE;
    const FIRST_DATAGRAM: &[u8] = &[0; DATA_SIZE];
    const SECOND_DATAGRAM: &[u8] = &[1; DATA_SIZE];
    const THIRD_DATAGRAM: &[u8] = &[2; DATA_SIZE];
    const FOURTH_DATAGRAM: &[u8] = &[3; DATA_SIZE];

    let mut client = new_client(
        ConnectionParameters::default()
            .datagram_size(to_u64(DATA_SIZE + DATAGRAM_FRAME_TYPE_VARINT_LEN)),
    );
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    send_datagram(&mut server, &mut client, FIRST_DATAGRAM.to_vec());
    send_datagram(&mut server, &mut client, SECOND_DATAGRAM.to_vec());
    send_datagram(&mut server, &mut client, THIRD_DATAGRAM.to_vec());

    let mut datagrams = client.events().filter_map(|evt| {
        if let ConnectionEvent::Datagram(d) = evt {
            Some(d)
        } else {
            None
        }
    });
    assert_eq!(datagrams.next().unwrap(), FIRST_DATAGRAM);
    assert_eq!(datagrams.next().unwrap(), SECOND_DATAGRAM);
    assert_eq!(datagrams.next().unwrap(), THIRD_DATAGRAM);
    assert!(datagrams.next().is_none());

    // New events can be queued.
    send_datagram(&mut server, &mut client, FOURTH_DATAGRAM.to_vec());
    let mut datagrams = client.events().filter_map(|evt| {
        if let ConnectionEvent::Datagram(d) = evt {
            Some(d)
        } else {
            None
        }
    });
    assert_eq!(datagrams.next().unwrap(), FOURTH_DATAGRAM);
    assert!(datagrams.next().is_none());
}

/// Datagrams that are close to the capacity of the packet need special
/// handling.  They need to use the packet-filling frame type and
/// they cannot allow other frames to follow.
fn datagram_overfill(client: &mut Connection, server: &mut Connection, gap: usize) {
    /// This `FrameWriter` should not be invoked.
    struct PanickingFrameWriter {}
    impl crate::connection::test_internal::FrameWriter for PanickingFrameWriter {
        fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
            panic!(
                "builder invoked with {} bytes remaining",
                builder.remaining()
            );
        }
    }

    // Work out how much space we have for a datagram.
    let space = {
        let p = client.paths.primary().unwrap();
        let path = p.borrow();
        // Minimum overhead is connection ID length, 1 byte short header, 1 byte packet number,
        // 1 byte for the DATAGRAM frame type, and 16 bytes for the AEAD.
        path.plpmtu() - path.remote_cid().unwrap().len() - 19
    };
    assert!(space >= 64); // Unlikely, but this test depends on the datagram being this large.

    // This should not be called.
    if client.test_frame_writer.is_none() {
        client.test_frame_writer = Some(Box::new(PanickingFrameWriter {}));
    }

    // This will completely fill available space, so the packet is completely full.
    send_datagram(client, server, vec![9; space - gap]);
}

#[test]
#[should_panic(expected = "test_frame_writer set on full packet")]
fn datagram_fill_gap0() {
    let (mut client, mut server) = connect_datagram();
    datagram_overfill(&mut client, &mut server, 0);
}

#[test]
#[should_panic(expected = "test_frame_writer set on full packet")]
fn datagram_fill_gap1() {
    let (mut client, mut server) = connect_datagram();
    datagram_overfill(&mut client, &mut server, 1);
}

#[test]
#[should_panic(expected = "test_frame_writer set on full packet")]
fn datagram_fill_gap2() {
    let (mut client, mut server) = connect_datagram();
    datagram_overfill(&mut client, &mut server, 2);
}

#[test]
#[should_panic(expected = "test_frame_writer set on full packet")]
fn datagram_fill_gap3() {
    let (mut client, mut server) = connect_datagram();
    datagram_overfill(&mut client, &mut server, 3);
}

#[test]
fn datagram_fill_gap4() {
    struct TrackingFrameWriter {
        called: Rc<RefCell<bool>>,
    }
    impl crate::connection::test_internal::FrameWriter for TrackingFrameWriter {
        fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
            assert_eq!(builder.remaining(), 2);
            *self.called.borrow_mut() = true;
        }
    }

    let (mut client, mut server) = connect_datagram();

    // Four bytes free is enough space for another frame.
    let called = Rc::new(RefCell::new(false));
    client.test_frame_writer = Some(Box::new(TrackingFrameWriter {
        called: Rc::clone(&called),
    }));
    datagram_overfill(&mut client, &mut server, 4);
    assert!(*called.borrow());
}

#[test]
fn per_session_queues_round_robin_across_sessions() {
    let (mut client, mut server) = connect_datagram();
    let now = now();

    for (i, (session, tag)) in [(0, b'A'), (4, b'B'), (0, b'A'), (4, b'B')]
        .into_iter()
        .enumerate()
    {
        _ = client.enqueue_datagram(
            StreamId::new(session),
            vec![tag],
            Some(u64::try_from(i).unwrap() + 1),
            now,
            SendGroupId::new(0),
            0,
        );
    }

    let out = client
        .process_output(now)
        .dgram()
        .expect("four 1-byte datagrams fit in one packet");
    server.process_input(out, now);

    let order: Vec<u8> = server
        .events()
        .filter_map(|e| match e {
            ConnectionEvent::Datagram(d) => d.first().copied(),
            _ => None,
        })
        .collect();
    assert_eq!(
        order, b"ABAB",
        "sessions must be served round-robin, not one drained before the other starts"
    );
}

#[test]
fn expire_session_datagrams_leaves_other_sessions_alone() {
    let (mut client, _server) = connect_datagram();
    let now = now();

    let session_a = StreamId::new(0);
    let session_b = StreamId::new(4);
    client.set_datagram_max_age(session_a, Some(Duration::from_millis(5)), now);
    client.set_datagram_max_age(session_b, Some(Duration::from_millis(5)), now);
    _ = client.enqueue_datagram(session_a, vec![1], Some(1), now, SendGroupId::new(0), 0);
    _ = client.enqueue_datagram(session_b, vec![2], Some(2), now, SendGroupId::new(0), 0);

    let later = now + Duration::from_millis(10);
    assert_eq!(
        client.expire_session_datagrams(session_a, later),
        1,
        "a session-scoped sweep must not count another session's datagrams"
    );
    assert_eq!(
        client.datagram_queue_capacity(session_b).queued_datagrams,
        1,
        "nor expire them"
    );
    assert_eq!(client.expire_session_datagrams(session_b, later), 1);
}

#[test]
fn expire_session_datagrams_on_an_unknown_session_is_zero() {
    let (mut client, _server) = connect_datagram();
    let now = now();

    assert_eq!(client.expire_session_datagrams(StreamId::new(8), now), 0);
}

#[test]
fn timer_expiry_is_counted_by_the_next_session_sweep() {
    // Whether the timer or the per-session sweep sheds a datagram must not
    // matter to the count: the timer runs first here, and the sweep still
    // reports the expiry, exactly once.
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    _ = client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0);

    let later = now + Duration::from_millis(10);
    client.process_timer(later);
    assert_eq!(client.datagram_queue_capacity(session).queued_datagrams, 0);

    assert_eq!(client.expire_session_datagrams(session, later), 1);
    assert_eq!(
        client.expire_session_datagrams(session, later),
        0,
        "counted once, not again"
    );
}

#[test]
fn take_session_expired_datagrams_drains_without_sweeping() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    _ = client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0);
    _ = client.enqueue_datagram(session, vec![2], Some(2), now, SendGroupId::new(0), 0);
    client.process_timer(now + Duration::from_millis(10));

    assert_eq!(client.take_session_expired_datagrams(session), 2);
    assert_eq!(client.take_session_expired_datagrams(session), 0);
    assert_eq!(client.take_session_expired_datagrams(StreamId::new(8)), 0);
}

#[test]
fn enqueue_datagram_rejects_more_than_the_peers_limit() {
    let mut client = new_client(ConnectionParameters::default().datagram_size(0));
    let mut server =
        new_server(ConnectionParameters::default().datagram_size(DATAGRAM_LEN_SMALLER_THAN_MTU));
    connect_force_idle(&mut client, &mut server);
    let session = StreamId::new(0);
    let limit = usize::try_from(DATAGRAM_LEN_SMALLER_THAN_MTU).unwrap();

    assert_eq!(
        client.enqueue_datagram(
            session,
            vec![0; limit],
            Some(1),
            now(),
            SendGroupId::new(0),
            0
        ),
        Err(Error::TooMuchData)
    );
    assert_eq!(
        client.enqueue_datagram(
            session,
            vec![0; limit - DATAGRAM_FRAME_TYPE_VARINT_LEN],
            Some(2),
            now(),
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    // The client advertised no datagram support, so nothing fits.
    assert_eq!(
        server.enqueue_datagram(session, vec![0], Some(3), now(), SendGroupId::new(0), 0),
        Err(Error::TooMuchData)
    );
    assert_eq!(server.datagram_queue_capacity(session).queued_datagrams, 0);
}

#[test]
fn expiring_a_blocked_queue_via_the_session_sweep_signals_space_available() {
    // The connection-wide timer sweep is covered by
    // `expiring_a_blocked_session_queue_signals_space_available`; the
    // per-session sweep shares its resume logic and must behave the same.
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    assert_eq!(
        client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );

    assert_eq!(
        client.expire_session_datagrams(session, now + Duration::from_millis(10)),
        1
    );
    assert!(
        client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { stream_id } if stream_id == session)),
        "a queue emptied by the per-session sweep must resume the sender"
    );
}

#[test]
fn space_available_only_for_unblocked_session() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let [first, second, still_blocked] = [0, 4, 8].map(StreamId::new);

    for session in [first, second, still_blocked] {
        client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
        client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
        assert_eq!(
            client.enqueue_datagram(session, vec![1], None, now, SendGroupId::new(0), 0),
            Ok(DatagramQueueOutcome::MaxBufferedReached)
        );
    }

    let later = now + Duration::from_millis(10);
    assert_eq!(client.expire_session_datagrams(first, later), 1);
    assert_eq!(client.expire_session_datagrams(second, later), 1);
    let resumed: Vec<_> = client
        .events()
        .filter_map(|e| match e {
            ConnectionEvent::OutgoingDatagramSpaceAvailable { stream_id } => Some(stream_id),
            _ => None,
        })
        .collect();
    assert_eq!(resumed, [first, second]);
}

#[test]
fn drop_session_datagrams_removes_only_that_sessions_entries() {
    let (mut client, _server) = connect_datagram();
    let now = now();

    let session_a = StreamId::new(0);
    let session_b = StreamId::new(4);
    _ = client.enqueue_datagram(session_a, vec![1], Some(1), now, SendGroupId::new(0), 0);
    _ = client.enqueue_datagram(session_b, vec![2], Some(2), now, SendGroupId::new(0), 0);

    assert_eq!(client.drop_session_datagrams(session_a).queued, 1);
    assert_eq!(
        client.datagram_queue_capacity(session_a).queued_datagrams,
        0
    );
    assert_eq!(
        client.datagram_queue_capacity(session_b).queued_datagrams,
        1,
        "session B's queue must be untouched by session A's teardown"
    );
}

#[test]
fn datagram_queue_expiry_drives_next_delay() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    let outcome =
        client.enqueue_datagram(session, vec![1, 2, 3], Some(1), now, SendGroupId::new(0), 0);
    assert_eq!(outcome, Ok(DatagramQueueOutcome::Ok));

    assert_eq!(
        client.next_delay(now, false),
        Duration::from_millis(5),
        "the queued datagram's max-age must drive the callback timer, \
         shorter than every other pending timer on a freshly idle connection"
    );
}

#[test]
fn default_max_age_drives_next_delay_when_none_is_set() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    _ = client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0);

    assert_eq!(
        client.next_delay(now, false),
        default_max_age(client.min_rtt()),
        "with no explicit max-age the RTT-derived default must set the deadline"
    );
}

#[test]
fn next_delay_takes_the_earliest_expiry_across_sessions() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session_a = StreamId::new(0);
    let session_b = StreamId::new(4);

    client.set_datagram_max_age(session_a, Some(Duration::from_millis(50)), now);
    client.set_datagram_max_age(session_b, Some(Duration::from_millis(5)), now);
    _ = client.enqueue_datagram(session_a, vec![1], Some(1), now, SendGroupId::new(0), 0);
    _ = client.enqueue_datagram(session_b, vec![2], Some(2), now, SendGroupId::new(0), 0);

    assert_eq!(client.next_delay(now, false), Duration::from_millis(5));
}

#[test]
fn datagram_is_too_big_only_for_an_empty_full_mtu_packet() {
    let (mut client, _server) = connect_datagram();
    let session = StreamId::new(0);
    _ = client.enqueue_datagram(
        session,
        vec![0; 100],
        Some(1),
        now(),
        SendGroupId::new(0),
        0,
    );

    let mut tokens = recovery::Tokens::new();
    let mut stats = Stats::default();
    let mut buf = Vec::new();
    // Room for the short header and a few bytes: what a nearly exhausted
    // congestion window leaves. The datagram would fit a full packet, so it
    // must stay queued rather than be dropped.
    let mut builder = packet::Builder::short(
        Encoder::new_borrowed_vec(&mut buf),
        false,
        None::<&[u8]>,
        60,
    );
    builder.pn(0, 1);
    client
        .quic_datagrams
        .write_frames(&mut builder, &mut tokens, &mut stats, false);
    assert_eq!(client.datagram_queue_capacity(session).queued_datagrams, 1);
    assert_eq!(stats.datagram_tx.dropped_too_big, 0);
    assert!(tokens.is_empty());

    // The same otherwise-empty packet at the full MTU: now it never fits.
    client
        .quic_datagrams
        .write_frames(&mut builder, &mut tokens, &mut stats, true);
    assert_eq!(client.datagram_queue_capacity(session).queued_datagrams, 0);
    assert_eq!(stats.datagram_tx.dropped_too_big, 1);
    assert!(client.events().any(|e| matches!(
        e,
        ConnectionEvent::OutgoingDatagramOutcome {
            id: 1,
            outcome: OutgoingDatagramOutcome::DroppedTooBig
        }
    )));
}

#[test]
fn enqueue_expires_stale_datagrams_before_charging_the_new_one() {
    // The application can write again after the deadline but before the
    // timer tick that would have expired the previous datagram. That stale
    // entry must not count against the max-buffered limit, and a sender it blocked
    // must be resumed.
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    assert_eq!(
        client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );

    let later = now + Duration::from_millis(10);
    assert_eq!(
        client.enqueue_datagram(session, vec![2], Some(2), later, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached),
        "the new datagram alone fills a mark of 1"
    );
    assert_eq!(
        client.datagram_queue_capacity(session).queued_datagrams,
        1,
        "the stale datagram must have been expired, not kept alongside the new one"
    );
    assert!(
        client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
        "shedding the stale datagram unblocked the queue, which the sender must be told"
    );
}

#[test]
fn process_timer_expires_a_stale_datagram_with_no_packets_pending() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    _ = client.enqueue_datagram(session, vec![1, 2, 3], Some(1), now, SendGroupId::new(0), 0);
    assert_eq!(client.datagram_queue_capacity(session).queued_datagrams, 1);

    client.process_timer(now + Duration::from_millis(10));

    assert_eq!(
        client.datagram_queue_capacity(session).queued_datagrams,
        0,
        "expiry must not wait on there being packets ready to build"
    );
}

#[test]
fn expiring_a_blocked_session_queue_signals_space_available() {
    // Expiry, not a send, is how one of these queues is expected to shed
    // load: a sender waiting above the max-buffered limit for a queue that then
    // ages out entirely would otherwise wait forever, since no send will ever
    // revisit an empty queue.
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    assert_eq!(
        client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );

    client.process_timer(now + Duration::from_millis(10));

    assert!(
        client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
        "a queue emptied by expiry must resume the sender"
    );
}

#[test]
fn shrinking_max_age_signals_space_available() {
    // Same for the expiry that `set_datagram_max_age` runs on the spot.
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
    assert_eq!(
        client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );

    client.set_datagram_max_age(
        session,
        Some(Duration::from_millis(1)),
        now + Duration::from_millis(10),
    );

    assert!(
        client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
        "a queue emptied by a shrunken max age must resume the sender"
    );
}

#[test]
fn resume_signal_fires_once_a_blocked_queue_drains_below_max_buffered() {
    let (mut client, mut server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
    assert_eq!(
        client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );
    assert!(
        !client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
        "resume event fired before the queue drained"
    );

    let out = client
        .process_output(now)
        .dgram()
        .expect("one small datagram fits in a packet");
    server.process_input(out, now);

    assert!(
        client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
        "draining a blocked queue back below its max-buffered limit must fire a resume signal"
    );
}

/// With a max-buffered limit above one, a send that leaves the queue still at
/// the mark must not resume the sender, or it would write straight back into
/// a full queue; the resume fires once, when the queue first drops below the
/// mark, and not again for the rest of the drain.
#[test]
fn partial_drain_at_the_max_buffered_limit_does_not_resume() {
    let (mut client, mut server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(2).unwrap()));
    // Each is too big to share a packet with the next, so they go out one
    // per `process_output`.
    assert_eq!(
        client.enqueue_datagram(
            session,
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now,
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    assert_eq!(
        client.enqueue_datagram(
            session,
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(2),
            now,
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );
    assert_eq!(
        client.enqueue_datagram(
            session,
            DATA_MTU.to_vec(),
            Some(3),
            now,
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::MaxBufferedReached),
        "a datagram queued above the mark is still accepted, not dropped"
    );
    assert!(client.next_event().is_none());

    let resumed = |c: &mut Connection| {
        c.events()
            .filter(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. }))
            .count()
    };

    server.process_input(client.process_output(now).dgram().unwrap(), now);
    assert_eq!(
        client.datagram_queue_capacity(session).queued_datagrams,
        2,
        "one datagram per packet"
    );
    assert_eq!(
        resumed(&mut client),
        0,
        "resume signalled while the queue was still at the mark"
    );

    server.process_input(client.process_output(now).dgram().unwrap(), now);
    assert_eq!(client.datagram_queue_capacity(session).queued_datagrams, 1);
    assert_eq!(
        resumed(&mut client),
        1,
        "resume not signalled after the queue dropped below the mark"
    );

    server.process_input(client.process_output(now).dgram().unwrap(), now);
    assert_eq!(client.datagram_queue_capacity(session).queued_datagrams, 0);
    assert_eq!(resumed(&mut client), 0, "resume signalled more than once");

    let delivered = server
        .events()
        .filter(|e| matches!(e, ConnectionEvent::Datagram(_)))
        .count();
    assert_eq!(delivered, 3, "nothing queued above the mark may be dropped");
}

/// Each fill past the mark re-arms the resume signal, so a sender that
/// alternates between filling and waiting is told to resume every time.
#[test]
fn space_available_event_fires_on_each_fill_drain_cycle() {
    let (mut client, mut server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(2).unwrap()));
    for cycle in 0..3 {
        assert_eq!(
            client.enqueue_datagram(
                session,
                DATA_SMALLER_THAN_MTU_2.to_vec(),
                Some(1),
                now,
                SendGroupId::new(0),
                0
            ),
            Ok(DatagramQueueOutcome::Ok)
        );
        assert_eq!(
            client.enqueue_datagram(
                session,
                DATA_SMALLER_THAN_MTU_2.to_vec(),
                Some(2),
                now,
                SendGroupId::new(0),
                0
            ),
            Ok(DatagramQueueOutcome::MaxBufferedReached)
        );

        while let Some(out) = client.process_output(now).dgram() {
            server.process_input(out, now);
        }

        assert!(
            client
                .events()
                .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
            "resume not signalled on fill/drain cycle {cycle}"
        );
    }
}

/// A queue that never reported anything but `Ok` has no sender waiting, so
/// draining it must not produce a resume signal.
#[test]
fn no_space_available_event_when_never_blocked() {
    let (mut client, mut server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(2).unwrap()));
    assert_eq!(
        client.enqueue_datagram(
            session,
            DATA_SMALLER_THAN_MTU.to_vec(),
            Some(1),
            now,
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::Ok)
    );
    let out = client.process_output(now).dgram();
    assert!(
        !client
            .events()
            .any(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. })),
        "resume signalled although the queue was never above the mark"
    );

    server.process_input(out.unwrap(), now);
    assert!(matches!(
        server.next_event().unwrap(),
        ConnectionEvent::Datagram(data) if data == DATA_SMALLER_THAN_MTU
    ));
}

/// A queue emptied entirely by [`OutgoingDatagramOutcome::DroppedTooBig`] must
/// still release a blocked sender: the resume signal has to fire even though no
/// datagram was sent.
#[test]
fn dropped_too_big_unblocks() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    // Within the peer's datagram-size limit (so accepted) but too big for any
    // packet (so dropped at send time); a max-buffered limit of one makes it block
    // the sender by itself.
    client.set_max_buffered_datagrams(session, Some(NonZeroUsize::new(1).unwrap()));
    assert_eq!(
        client.enqueue_datagram(
            session,
            DATA_BIGGER_THAN_MTU.to_vec(),
            Some(1),
            now,
            SendGroupId::new(0),
            0
        ),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );
    assert!(client.next_event().is_none());

    let dropped = client.stats().datagram_tx.dropped_too_big;
    // Nothing fits, so no packet carrying a datagram is produced, but the
    // datagram is dropped as too big, which empties the queue.
    assert!(client.process_output(now).dgram().is_none());
    assert_eq!(client.stats().datagram_tx.dropped_too_big, dropped + 1);

    assert_eq!(
        client
            .events()
            .filter(|e| matches!(e, ConnectionEvent::OutgoingDatagramSpaceAvailable { .. }))
            .count(),
        1,
        "resume must be signalled exactly once after the queue was emptied by a drop"
    );
}

#[test]
fn sent_count_covers_only_datagrams_written_into_a_packet() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    _ = client.enqueue_datagram(
        session,
        DATA_BIGGER_THAN_MTU.to_vec(),
        Some(1),
        now,
        SendGroupId::new(0),
        0,
    );
    _ = client.enqueue_datagram(session, vec![1], Some(2), now, SendGroupId::new(0), 0);
    assert!(client.process_output(now).dgram().is_some());

    assert_eq!(
        client.take_session_sent_datagrams(session),
        1,
        "the datagram dropped as too big must not count as sent"
    );
    assert_eq!(client.take_session_sent_datagrams(session), 0);
    assert_eq!(client.take_session_sent_datagrams(StreamId::new(8)), 0);
}

#[test]
fn untracked_datagram_too_big_for_the_path_mtu_is_counted() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    _ = client.enqueue_datagram(
        session,
        DATA_BIGGER_THAN_MTU.to_vec(),
        None,
        now,
        SendGroupId::new(0),
        0,
    );
    _ = client.enqueue_datagram(session, vec![1], None, now, SendGroupId::new(0), 0);
    assert!(client.process_output(now).dgram().is_some());

    assert_eq!(client.take_session_too_big_datagrams(session), 1);
    assert_eq!(client.take_session_too_big_datagrams(session), 0);
    assert_eq!(client.take_session_sent_datagrams(session), 1);
}

#[test]
fn drop_session_datagrams_returns_the_counts_not_yet_taken() {
    let (mut client, _server) = connect_datagram();
    let now = now();
    let session = StreamId::new(0);

    client.set_datagram_max_age(session, Some(Duration::from_millis(5)), now);
    _ = client.enqueue_datagram(
        session,
        DATA_BIGGER_THAN_MTU.to_vec(),
        None,
        now,
        SendGroupId::new(0),
        0,
    );
    _ = client.enqueue_datagram(session, vec![1], Some(1), now, SendGroupId::new(0), 0);
    assert!(client.process_output(now).dgram().is_some());
    let later = now + Duration::from_millis(1);
    _ = client.enqueue_datagram(session, vec![2], Some(2), later, SendGroupId::new(0), 0);
    _ = client.enqueue_datagram(session, vec![3], Some(3), later, SendGroupId::new(0), 0);
    client.process_timer(later + Duration::from_millis(5));
    _ = client.enqueue_datagram(session, vec![4], None, later, SendGroupId::new(0), 0);

    assert_eq!(
        client.drop_session_datagrams(session),
        DroppedDatagrams {
            queued: 1,
            sent: 1,
            expired: 2,
            too_big: 1,
        }
    );
    assert_eq!(
        client.drop_session_datagrams(session),
        DroppedDatagrams::default()
    );
}
