// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cmp::max, collections::HashMap, fmt::Debug, num::NonZeroUsize};

use neqo_common::{Role, event::Provider as _, to_u64};
use test_fixture::now;
use tracing::debug;

use super::{
    super::State, DEFAULT_STREAM_DATA, assert_error, connect, connect_force_idle, default_client,
    default_server, maybe_authenticate, new_client, new_server, send_something, send_with_extra,
};
use crate::{
    CloseReason, Connection, ConnectionParameters, Error, StreamDataLimit, StreamId, StreamType,
    connection::params::INITIAL_LOCAL_MAX_STREAM_DATA,
    events::ConnectionEvent,
    frame::FrameType,
    packet,
    send_stream::{self, OrderGroup},
    streams::SendOrder,
    tparams::{TransportParameter, TransportParameterId::*},
};

#[test]
fn stream_create() {
    let mut client = default_client();

    let out = client.process_output(now());
    let out2 = client.process_output(now());
    let mut server = default_server();
    server.process_input(out.dgram().unwrap(), now());
    let out = server.process(out2.dgram(), now());

    let out = client.process(out.dgram(), now());
    let out = server.process(out.dgram(), now());
    let out = client.process(out.dgram(), now());
    drop(server.process(out.dgram(), now()));
    assert!(maybe_authenticate(&mut client));
    let out = client.process_output(now());

    // client now in State::Connected
    assert_eq!(client.stream_create(StreamType::UniDi).unwrap(), 2);
    assert_eq!(client.stream_create(StreamType::UniDi).unwrap(), 6);
    assert_eq!(client.stream_create(StreamType::BiDi).unwrap(), 0);
    assert_eq!(client.stream_create(StreamType::BiDi).unwrap(), 4);

    drop(server.process(out.dgram(), now()));
    // server now in State::Connected
    assert_eq!(server.stream_create(StreamType::UniDi).unwrap(), 3);
    assert_eq!(server.stream_create(StreamType::UniDi).unwrap(), 7);
    assert_eq!(server.stream_create(StreamType::BiDi).unwrap(), 1);
    assert_eq!(server.stream_create(StreamType::BiDi).unwrap(), 5);
}

#[test]
// tests stream send/recv after connection is established.
fn transfer() {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    debug!("---- client sends");
    // Send
    let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id, &[6; 100]).unwrap();
    client.stream_send(client_stream_id, &[7; 40]).unwrap();
    client.stream_send(client_stream_id, &[8; 4000]).unwrap();

    // Send to another stream but some data after fin has been set
    let client_stream_id2 = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id2, &[6; 60]).unwrap();
    client.stream_close_send(client_stream_id2).unwrap();
    client.stream_send(client_stream_id2, &[7; 50]).unwrap_err();
    // Sending this much takes a few datagrams.
    let mut datagrams = vec![];
    let mut out = client.process_output(now());
    while let Some(d) = out.dgram() {
        datagrams.push(d);
        out = client.process_output(now());
    }
    assert_eq!(datagrams.len(), 4);
    assert_eq!(*client.state(), State::Confirmed);

    debug!("---- server receives");
    for d in datagrams {
        let out = server.process(Some(d), now());
        // With an RTT of zero, the server will acknowledge every packet immediately.
        assert!(out.as_dgram_ref().is_some());
        debug!("Output={:0x?}", out.as_dgram_ref());
    }
    assert_eq!(*server.state(), State::Confirmed);

    let mut buf = vec![0; 4000];

    let mut stream_ids = server.events().filter_map(|evt| match evt {
        ConnectionEvent::NewStream { stream_id, .. } => Some(stream_id),
        _ => None,
    });
    let first_stream = stream_ids.next().expect("should have a new stream event");
    let second_stream = stream_ids
        .next()
        .expect("should have a second new stream event");
    assert!(stream_ids.next().is_none());
    let (received1, fin1) = server.stream_recv(first_stream, &mut buf).unwrap();
    assert_eq!(received1, 4000);
    assert!(!fin1);
    let (received2, fin2) = server.stream_recv(first_stream, &mut buf).unwrap();
    assert_eq!(received2, 140);
    assert!(!fin2);

    let (received3, fin3) = server.stream_recv(second_stream, &mut buf).unwrap();
    assert_eq!(received3, 60);
    assert!(fin3);
}

// tests stream sendorder prioritization
fn sendorder_test(order_of_sendorder: &[Option<SendOrder>]) {
    let mut client = default_client();
    let mut server = default_server();
    connect_force_idle(&mut client, &mut server);

    debug!("---- client sends");
    // open all streams and set the sendorders
    let mut ordered = Vec::new();
    let mut streams = Vec::<StreamId>::new();
    for sendorder in order_of_sendorder {
        let id = client.stream_create(StreamType::UniDi).unwrap();
        streams.push(id);
        ordered.push((id, *sendorder));
        // must be set before sendorder
        client.streams.set_fairness(id, true).unwrap();
        client.streams.set_sendorder(id, *sendorder).unwrap();
    }
    // Write some data to all the streams
    for stream_id in streams {
        client.stream_send(stream_id, &[6; 100]).unwrap();
    }

    // Sending this much takes a few datagrams.
    // Note: this test uses an RTT of 0 which simplifies things (no pacing)
    let mut datagrams = Vec::new();
    let mut out = client.process_output(now());
    while let Some(d) = out.dgram() {
        datagrams.push(d);
        out = client.process_output(now());
    }
    assert_eq!(*client.state(), State::Confirmed);

    debug!("---- server receives");
    for d in datagrams {
        let out = server.process(Some(d), now());
        debug!("Output={:0x?}", out.as_dgram_ref());
    }
    assert_eq!(*server.state(), State::Confirmed);

    // Reception order: the sequence in which streams first became readable on the server.
    let mut reception_order = Vec::new();
    for evt in server.events() {
        if let ConnectionEvent::RecvStreamReadable { stream_id } = evt
            && !reception_order.contains(&stream_id)
        {
            reception_order.push(stream_id);
        }
    }

    // Nothing is starved: every stream is eventually delivered.
    assert_eq!(reception_order.len(), ordered.len());

    // Per the WebTransport send-order rules, strictly-ordered streams (those with a
    // sendOrder) within a send group -- here all streams are in the null group -- must be
    // sent highest-sendOrder first. Streams without a sendOrder are not strictly ordered:
    // they must not starve, but their position relative to ordered streams is
    // implementation-defined, so it is not asserted here.
    let sendorder_of: HashMap<StreamId, Option<SendOrder>> = ordered.iter().copied().collect();
    let mut prev_order: Option<SendOrder> = None;
    for stream_id in &reception_order {
        let Some(order) = sendorder_of[stream_id] else {
            continue;
        };
        if let Some(prev) = prev_order {
            assert!(
                order <= prev,
                "stream with sendOrder {order} delivered after higher sendOrder {prev}"
            );
        }
        prev_order = Some(order);
    }
}

#[test]
fn sendorder_0() {
    sendorder_test(&[None, Some(1), Some(2), Some(3)]);
}
#[test]
fn sendorder_1() {
    sendorder_test(&[Some(3), Some(2), Some(1), None]);
}
#[test]
fn sendorder_2() {
    sendorder_test(&[Some(3), None, Some(2), Some(1)]);
}
#[test]
fn sendorder_3() {
    sendorder_test(&[Some(1), Some(2), None, Some(3)]);
}
#[test]
fn sendorder_4() {
    sendorder_test(&[
        Some(1),
        Some(2),
        Some(1),
        None,
        Some(3),
        Some(1),
        Some(3),
        None,
    ]);
}

// Tests stream sendorder prioritization
// Converts Vecs of u64's into StreamIds
fn fairness_test<S, R>(source: S, number_iterates: usize, truncate_to: usize, result_array: &R)
where
    S: IntoIterator,
    S::Item: Into<StreamId>,
    R: IntoIterator + Debug,
    R::Item: Into<StreamId>,
    Vec<u64>: PartialEq<R>,
{
    // test the OrderGroup code used for fairness
    let mut group: OrderGroup = OrderGroup::default();
    for stream_id in source {
        group.insert(stream_id.into());
    }
    {
        let mut iterator1 = group.iter();
        // advance_by() would help here
        let mut n = number_iterates;
        while n > 0 {
            iterator1.next();
            n -= 1;
        }
        // let iterator1 go out of scope
    }
    group.truncate(truncate_to);

    let iterator2 = group.iter();
    let result: Vec<u64> = iterator2.map(StreamId::as_u64).collect();
    assert_eq!(result, *result_array);
}

#[test]
fn ordergroup_0() {
    let source: [u64; 0] = [];
    let result: [u64; 0] = [];
    fairness_test(source, 1, usize::MAX, &result);
}

#[test]
fn ordergroup_1() {
    let source: [u64; 6] = [0, 1, 2, 3, 4, 5];
    let result: [u64; 6] = [1, 2, 3, 4, 5, 0];
    fairness_test(source, 1, usize::MAX, &result);
}

#[test]
fn ordergroup_2() {
    let source: [u64; 6] = [0, 1, 2, 3, 4, 5];
    let result: [u64; 6] = [2, 3, 4, 5, 0, 1];
    fairness_test(source, 2, usize::MAX, &result);
}

#[test]
fn ordergroup_3() {
    let source: [u64; 6] = [0, 1, 2, 3, 4, 5];
    let result: [u64; 6] = [0, 1, 2, 3, 4, 5];
    fairness_test(source, 10, usize::MAX, &result);
}

#[test]
fn ordergroup_4() {
    let source: [u64; 6] = [0, 1, 2, 3, 4, 5];
    let result: [u64; 6] = [0, 1, 2, 3, 4, 5];
    fairness_test(source, 0, usize::MAX, &result);
}

#[test]
fn ordergroup_5() {
    let source: [u64; 1] = [0];
    let result: [u64; 1] = [0];
    fairness_test(source, 1, usize::MAX, &result);
}

#[test]
fn ordergroup_6() {
    let source: [u64; 6] = [0, 1, 2, 3, 4, 5];
    let result: [u64; 6] = [5, 0, 1, 2, 3, 4];
    fairness_test(source, 5, usize::MAX, &result);
}

#[test]
fn ordergroup_7() {
    let source: [u64; 6] = [0, 1, 2, 3, 4, 5];
    let result: [u64; 3] = [0, 1, 2];
    fairness_test(source, 5, 3, &result);
}

#[test]
// Send fin even if a peer closes a remote bidi send stream before sending any data.
fn report_fin_when_stream_closed_wo_data() {
    // Note that the two servers in this test will get different anti-replay filters.
    // That's OK because we aren't testing anti-replay.
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // create a stream
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out = client.process_output(now());
    drop(server.process(out.dgram(), now()));

    server.stream_close_send(stream_id).unwrap();
    let out = server.process_output(now());
    drop(client.process(out.dgram(), now()));
    let stream_readable = |e| matches!(e, ConnectionEvent::RecvStreamReadable { .. });
    assert!(client.events().any(stream_readable));
}

fn exchange_data(client: &mut Connection, server: &mut Connection) {
    let mut input = None;
    loop {
        let out = client.process(input, now()).dgram();
        let c_done = out.is_none();
        let out = server.process(out, now()).dgram();
        if out.is_none() && c_done {
            break;
        }
        input = out;
    }
}

#[test]
fn sending_max_data() {
    const SMALL_MAX_DATA: usize = 2048;

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.events().count(), 2); // SendStreamWritable, StateChange(connected)
    assert_eq!(stream_id, 2);
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );

    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA + 1])
            .unwrap(),
        SMALL_MAX_DATA
    );

    exchange_data(&mut client, &mut server);

    let mut buf = vec![0; 40000];
    let (received, fin) = server.stream_recv(stream_id, &mut buf).unwrap();
    assert_eq!(received, SMALL_MAX_DATA);
    assert!(!fin);

    let out = server.process_output(now()).dgram();
    client.process_input(out.unwrap(), now());

    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA + 1])
            .unwrap(),
        SMALL_MAX_DATA
    );
}

#[test]
fn max_data() {
    const SMALL_MAX_DATA: usize = 16383;

    let mut client = default_client();
    let mut server = default_server();

    server
        .set_local_tparam(
            InitialMaxData,
            TransportParameter::Integer(to_u64(SMALL_MAX_DATA)),
        )
        .unwrap();

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.events().count(), 2); // SendStreamWritable, StateChange(connected)
    assert_eq!(stream_id, 2);
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );
    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA + 1])
            .unwrap(),
        SMALL_MAX_DATA
    );
    assert_eq!(client.events().count(), 0);

    assert_eq!(client.stream_send(stream_id, b"hello").unwrap(), 0);
    client
        .streams
        .get_send_stream_mut(stream_id)
        .unwrap()
        .mark_as_sent(0, 4096, false);
    assert_eq!(client.events().count(), 0);
    client
        .streams
        .get_send_stream_mut(stream_id)
        .unwrap()
        .mark_as_acked(0, 4096, false);
    assert_eq!(client.events().count(), 0);

    assert_eq!(client.stream_send(stream_id, b"hello").unwrap(), 0);
    // no event because still limited by conn max data
    assert_eq!(client.events().count(), 0);

    // Increase max data. Avail space now limited by stream credit
    client.streams.handle_max_data(100_000_000);
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        INITIAL_LOCAL_MAX_STREAM_DATA - SMALL_MAX_DATA
    );

    let evts = client.events().collect::<Vec<_>>();
    assert_eq!(evts.len(), 1);
    assert!(matches!(
        evts[0],
        ConnectionEvent::SendStreamWritable { .. }
    ));
}

/// `MAX_DATA` wakes exactly the streams whose low watermark the previous limit did not exceed.
#[test]
fn max_data_low_watermark() {
    const SMALL_MAX_DATA: usize = 1024;
    const WATERMARK: usize = 100;

    fn writable_after_max_data(client: &mut Connection, max_data: usize) -> Vec<StreamId> {
        client.streams.handle_max_data(to_u64(max_data));
        client
            .events()
            .filter_map(|e| match e {
                ConnectionEvent::SendStreamWritable { stream_id } => Some(stream_id),
                _ => None,
            })
            .collect()
    }

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));
    connect(&mut client, &mut server);

    let low = client.stream_create(StreamType::UniDi).unwrap();
    let high = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_send(low, &[b'a'; SMALL_MAX_DATA]).unwrap(),
        SMALL_MAX_DATA
    );
    _ = client.events().count();

    let limit = SMALL_MAX_DATA + WATERMARK / 2;
    assert_eq!(writable_after_max_data(&mut client, limit), [low, high]);

    client
        .stream_set_writable_event_low_watermark(high, NonZeroUsize::new(WATERMARK).unwrap())
        .unwrap();
    let limit = SMALL_MAX_DATA + WATERMARK;
    assert_eq!(writable_after_max_data(&mut client, limit), [high]);
    let limit = SMALL_MAX_DATA + WATERMARK * 3 / 2;
    assert_eq!(writable_after_max_data(&mut client, limit), [high]);
    let limit = SMALL_MAX_DATA + WATERMARK * 3;
    assert!(writable_after_max_data(&mut client, limit).is_empty());
}

#[test]
fn exceed_max_data() {
    const SMALL_MAX_DATA: usize = 1024;

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.events().count(), 2); // SendStreamWritable, StateChange(connected)
    assert_eq!(stream_id, 2);
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );
    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA + 1])
            .unwrap(),
        SMALL_MAX_DATA
    );

    assert_eq!(client.stream_send(stream_id, b"hello").unwrap(), 0);

    // Artificially trick the client to think that it has more flow control credit.
    client.streams.handle_max_data(100_000_000);
    assert_eq!(client.stream_send(stream_id, b"h").unwrap(), 1);

    exchange_data(&mut client, &mut server);

    assert_error(
        &client,
        &CloseReason::Transport(Error::Peer(Error::FlowControl.code())),
    );
    assert_error(&server, &CloseReason::Transport(Error::FlowControl));
}

#[test]
// If we send a stop_sending to the peer, we should not accept more data from the peer.
fn do_not_accept_data_after_stop_sending() {
    // Note that the two servers in this test will get different anti-replay filters.
    // That's OK because we aren't testing anti-replay.
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // create a stream
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out = client.process_output(now());
    drop(server.process(out.dgram(), now()));

    let stream_readable = |e| matches!(e, ConnectionEvent::RecvStreamReadable { .. });
    assert!(server.events().any(stream_readable));

    // Send one more packet from client. The packet should arrive after the server
    // has already requested stop_sending.
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out_second_data_frame = client.process_output(now());
    // Call stop sending.
    assert_eq!(
        Ok(()),
        server.stream_stop_sending(stream_id, Error::None.code())
    );

    // Receive the second data frame. The frame should be ignored and
    // DataReadable events shouldn't be posted.
    let out = server.process(out_second_data_frame.dgram(), now());
    assert!(!server.events().any(stream_readable));

    drop(client.process(out.dgram(), now()));
    assert_eq!(
        Err(Error::FinalSize),
        client.stream_send(stream_id, &[0x00])
    );
}

pub(super) struct Writer(pub Vec<u64>);

impl crate::connection::test_internal::FrameWriter for Writer {
    fn write_frames(&mut self, builder: &mut packet::Builder<&mut Vec<u8>>) {
        builder.write_varint_frame(&self.0);
    }
}

#[test]
/// Server sends a number of stream-related frames for a client-initiated stream that is not yet
/// created. This should cause the client to close the connection.
fn illegal_stream_related_frames() {
    fn test_with_illegal_frame(frame: &[u64]) {
        let mut client = default_client();
        let mut server = default_server();
        connect(&mut client, &mut server);
        let dgram = send_with_extra(&mut server, Writer(frame.to_vec()), now());
        client.process_input(dgram, now());
        assert!(client.state().closed());
    }

    // 0 = Client-Initiated, Bidirectional; 2 = Client-Initiated, Unidirectional
    for stream_id in [0, 2] {
        for frame_type in [
            FrameType::ResetStream,
            FrameType::StopSending,
            FrameType::MaxStreamData,
            FrameType::StreamDataBlocked,
            FrameType::Stream,
        ] {
            // The slice contains an extra 0 that is only needed for a RESET_STREAM frame.
            // It's ignored for the other frame types as PADDING.
            test_with_illegal_frame(&[frame_type.into(), stream_id, 0, 0]);
        }
    }
}

#[test]
/// Server sends a stream-related frame for the wrong half of an existing
/// unidirectional stream. This should cause the client to close the connection
/// with `STREAM_STATE_ERROR`.
fn wrong_directional_stream_frames() {
    // The directional check only fires once the targeted half exists, so the
    // creating role makes the stream before the offending frame is injected.
    fn client_rejects(frame_type: FrameType, stream_creator: Role) {
        let mut client = default_client();
        let mut server = default_server();
        connect(&mut client, &mut server);
        let creator = match stream_creator {
            Role::Client => &mut client,
            Role::Server => &mut server,
        };
        let stream_id = creator.stream_create(StreamType::UniDi).unwrap().as_u64();
        // The trailing 0s are PADDING for the frames that don't need them.
        let dgram = send_with_extra(
            &mut server,
            Writer(vec![frame_type.into(), stream_id, 0, 0, 0]),
            now(),
        );
        client.process_input(dgram, now());
        assert_error(&client, &CloseReason::Transport(Error::StreamState));
    }

    // Frames a sender may not receive, on a client-initiated send-only stream.
    for frame_type in [
        FrameType::ResetStream,
        FrameType::ResetStreamAt,
        FrameType::Stream,
        FrameType::StreamDataBlocked,
    ] {
        client_rejects(frame_type, Role::Client);
    }

    // Frames a receiver may not receive, on a server-initiated receive-only stream.
    for frame_type in [FrameType::StopSending, FrameType::MaxStreamData] {
        client_rejects(frame_type, Role::Server);
    }
}

#[test]
/// Regression <https://github.com/mozilla/neqo/pull/2358>.
fn legal_out_of_order_frame_on_remote_initiated_closed_stream() {
    const REQUEST: &[u8] = b"ping";
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // Client sends request and closes stream.
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    _ = client.stream_send(stream_id, REQUEST).unwrap();
    client.stream_close_send(stream_id).unwrap();
    let dgram = client.process_output(now()).dgram();

    // Server reads request and closes stream.
    server.process_input(dgram.unwrap(), now());
    let mut buf = [0; REQUEST.len()];
    server.stream_recv(stream_id, &mut buf).unwrap();
    server.stream_close_send(stream_id).unwrap();
    let dgram = server.process_output(now()).dgram();
    client.process_input(dgram.unwrap(), now());

    // Client ACKs server's close stream, thus server forgetting about stream.
    let dgram = send_something(&mut client, now());
    let dgram = server.process(Some(dgram), now()).dgram();
    client.process_input(dgram.unwrap(), now());

    // Deliver an out-of-order `FRAME_TYPE_MAX_STREAM_DATA` on forgotten stream.
    let dgram = send_with_extra(
        &mut client,
        Writer(vec![
            u64::from(FrameType::MaxStreamData),
            stream_id.as_u64(),
            0,
            0,
        ]),
        now(),
    );
    server.process_input(dgram, now());

    assert!(
        !server.state().closed(),
        "expect server to ignore out-of-order frame on forgotten stream"
    );
}

#[test]
// Server sends stop_sending, the client simultaneous sends reset.
fn simultaneous_stop_sending_and_reset() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // create a stream
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out = client.process_output(now());
    let ack = server.process(out.dgram(), now()).dgram();

    let stream_readable =
        |e| matches!(e, ConnectionEvent::RecvStreamReadable { stream_id: id } if id == stream_id);
    assert!(server.events().any(stream_readable));

    // The client resets the stream. The packet with reset should arrive after the server
    // has already requested stop_sending.
    client.stream_reset_send(stream_id, 0).unwrap();
    let out_reset_frame = client.process(ack, now()).dgram();

    // Send something out of order to force the server to generate an
    // acknowledgment at the next opportunity.
    let force_ack = send_something(&mut client, now());
    server.process_input(force_ack, now());

    // Call stop sending.
    server.stream_stop_sending(stream_id, 0).unwrap();
    // Receive the second data frame. The frame should be ignored and
    // DataReadable events shouldn't be posted.
    let ack = server.process(out_reset_frame, now()).dgram();
    assert!(ack.is_some());
    assert!(!server.events().any(stream_readable));

    // The client gets the STOP_SENDING frame.
    client.process_input(ack.unwrap(), now());
    assert_eq!(
        Err(Error::InvalidStreamId),
        client.stream_send(stream_id, &[0x00])
    );
}

#[test]
/// Make a stream data or control frame arrive after the stream has been used and cleared.
fn late_stream_related_frames() {
    fn late_stream_related_frame(frame_type: FrameType) {
        let mut client = default_client();
        let mut server = default_server();
        connect(&mut client, &mut server);

        // Client creates a stream and sends some data.
        let stream_id = client.stream_create(StreamType::BiDi).unwrap();
        client.stream_send(stream_id, &[0x00]).unwrap();
        let out = client.process_output(now());
        _ = server.process(out.dgram(), now()).dgram();

        // Make the server generate a packet containing the test frame.
        let before = server.stats().frame_tx;
        match frame_type {
            FrameType::ResetStream => {
                server.stream_reset_send(stream_id, 0).unwrap();
            }
            FrameType::StopSending => {
                server.stream_stop_sending(stream_id, 0).unwrap();
            }
            FrameType::Stream => {
                server.stream_send(stream_id, &[0x00]).unwrap();
                server.stream_close_send(stream_id).unwrap();
            }
            FrameType::MaxStreamData => {
                server
                    .streams
                    .get_recv_stream_mut(stream_id)
                    .unwrap()
                    .set_stream_max_data(u32::MAX.into());
            }
            FrameType::StreamDataBlocked => {
                let internal_stream = server.streams.get_send_stream_mut(stream_id).unwrap();
                if let send_stream::State::Ready { fc, .. } = internal_stream.state() {
                    fc.blocked();
                } else {
                    panic!("unexpected stream state");
                }
            }
            _ => panic!("unexpected frame type"),
        }
        let tester = server.process_output(now()).dgram();
        let after = server.stats().frame_tx;
        match frame_type {
            FrameType::ResetStream => {
                assert_eq!(after.reset_stream, before.reset_stream + 1);
            }
            FrameType::StopSending => {
                assert_eq!(after.stop_sending, before.stop_sending + 1);
            }
            FrameType::Stream => {
                assert_eq!(after.stream, before.stream + 1);
            }
            FrameType::MaxStreamData => {
                assert_eq!(after.max_stream_data, before.max_stream_data + 1);
            }
            FrameType::StreamDataBlocked => {
                assert_eq!(after.stream_data_blocked, before.stream_data_blocked + 1);
            }
            _ => panic!("unexpected frame type"),
        }

        // Now clear the streams on the client, and then deliver the test frame.
        client.streams.clear_streams();
        let (ss, rs) = client.streams.obtain_stream(stream_id).unwrap();
        assert!(ss.is_none() && rs.is_none());
        _ = client.process(tester, now()).dgram();

        // Make sure this worked, i.e., the connection didn't close.
        assert_eq!(*client.state(), State::Confirmed);
    }

    for frame_type in [
        FrameType::ResetStream,
        FrameType::StopSending,
        FrameType::MaxStreamData,
        FrameType::StreamDataBlocked,
        FrameType::Stream,
    ] {
        late_stream_related_frame(frame_type);
    }
}

#[test]
fn client_fin_reorder() {
    let mut client = default_client();
    let mut server = default_server();

    // Send ClientHello.
    let client_hs = client.process_output(now());
    let client_hs2 = client.process_output(now());
    assert!(client_hs.as_dgram_ref().is_some() && client_hs2.as_dgram_ref().is_some());

    server.process_input(client_hs.dgram().unwrap(), now());
    let server_hs = server.process(client_hs2.dgram(), now());
    assert!(server_hs.as_dgram_ref().is_some()); // ServerHello, etc...

    let client_ack = client.process(server_hs.dgram(), now());
    assert!(client_ack.as_dgram_ref().is_some());

    let dgram = server.process(client_ack.dgram(), now());
    let dgram = client.process(dgram.dgram(), now());

    let server_out = server.process(dgram.dgram(), now());
    assert!(server_out.as_dgram_ref().is_none());

    assert!(maybe_authenticate(&mut client));
    assert_eq!(*client.state(), State::Connected);

    let client_fin = client.process_output(now());
    assert!(client_fin.as_dgram_ref().is_some());

    let client_stream_id = client.stream_create(StreamType::UniDi).unwrap();
    client.stream_send(client_stream_id, &[1, 2, 3]).unwrap();
    let client_stream_data = client.process_output(now());
    assert!(client_stream_data.as_dgram_ref().is_some());

    // Now stream data gets before client_fin
    let server_out = server.process(client_stream_data.dgram(), now());
    assert!(server_out.as_dgram_ref().is_none()); // the packet will be discarded

    assert_eq!(*server.state(), State::Handshaking);
    let server_out = server.process(client_fin.dgram(), now());
    assert!(server_out.as_dgram_ref().is_some());
}

#[test]
fn after_fin_is_read_conn_events_for_stream_should_be_removed() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let id = server.stream_create(StreamType::BiDi).unwrap();
    server.stream_send(id, &[6; 10]).unwrap();
    server.stream_close_send(id).unwrap();
    let out = server.process_output(now()).dgram();
    assert!(out.is_some());

    drop(client.process(out, now()));

    // read from the stream before checking connection events.
    let mut buf = vec![0; 4000];
    let (_, fin) = client.stream_recv(id, &mut buf).unwrap();
    assert!(fin);

    // Make sure we do not have RecvStreamReadable events for the stream when fin has been read.
    let readable_stream_evt =
        |e| matches!(e, ConnectionEvent::RecvStreamReadable { stream_id } if stream_id == id);
    assert!(!client.events().any(readable_stream_evt));
}

#[test]
fn after_stream_stop_sending_is_called_conn_events_for_stream_should_be_removed() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let id = server.stream_create(StreamType::BiDi).unwrap();
    server.stream_send(id, &[6; 10]).unwrap();
    server.stream_close_send(id).unwrap();
    let out = server.process_output(now()).dgram();
    assert!(out.is_some());

    drop(client.process(out, now()));

    // send stop sending.
    client.stream_stop_sending(id, Error::None.code()).unwrap();

    // Make sure we do not have RecvStreamReadable events for the stream after stream_stop_sending
    // has been called.
    let readable_stream_evt =
        |e| matches!(e, ConnectionEvent::RecvStreamReadable { stream_id } if stream_id == id);
    assert!(!client.events().any(readable_stream_evt));
}

#[test]
fn stream_data_blocked_generates_max_stream_data() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let now = now();

    // Send some data and consume some flow control.
    let stream_id = server.stream_create(StreamType::UniDi).unwrap();
    _ = server.stream_send(stream_id, DEFAULT_STREAM_DATA).unwrap();
    let dgram = server.process_output(now).dgram();
    assert!(dgram.is_some());

    // Consume the data.
    client.process_input(dgram.unwrap(), now);
    let mut buf = [0; 10];
    let (count, end) = client.stream_recv(stream_id, &mut buf[..]).unwrap();
    assert_eq!(count, DEFAULT_STREAM_DATA.len());
    assert!(!end);

    // Now send `STREAM_DATA_BLOCKED`.
    let internal_stream = server.streams.get_send_stream_mut(stream_id).unwrap();
    if let send_stream::State::Send { fc, .. } = internal_stream.state() {
        fc.blocked();
    } else {
        panic!("unexpected stream state");
    }
    let dgram = server.process_output(now).dgram();
    assert!(dgram.is_some());

    let sdb_before = client.stats().frame_rx.stream_data_blocked;
    let dgram = client.process(dgram, now).dgram();
    assert_eq!(client.stats().frame_rx.stream_data_blocked, sdb_before + 1);
    assert!(dgram.is_some());

    // Client should have sent a MAX_STREAM_DATA frame with just a small increase
    // on the default window size.
    let msd_before = server.stats().frame_rx.max_stream_data;
    server.process_input(dgram.unwrap(), now);
    assert_eq!(server.stats().frame_rx.max_stream_data, msd_before + 1);

    // Test that the entirety of the receive buffer is available now.
    let mut written = 0;
    loop {
        const LARGE_BUFFER: &[u8] = &[0; 1024];
        let amount = server.stream_send(stream_id, LARGE_BUFFER).unwrap();
        if amount == 0 {
            break;
        }
        written += amount;
    }
    assert_eq!(written, INITIAL_LOCAL_MAX_STREAM_DATA);
}

/// See <https://github.com/mozilla/neqo/issues/871>
#[test]
fn max_streams_after_bidi_closed() {
    const REQUEST: &[u8] = b"ping";
    const RESPONSE: &[u8] = b"pong";
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    while client.stream_create(StreamType::BiDi).is_ok() {
        // Exhaust the stream limit.
    }
    // Write on the one stream and send that out.
    _ = client.stream_send(stream_id, REQUEST).unwrap();
    client.stream_close_send(stream_id).unwrap();
    let dgram = client.process_output(now()).dgram();

    // Now handle the stream and send an incomplete response.
    server.process_input(dgram.unwrap(), now());
    server.stream_send(stream_id, RESPONSE).unwrap();
    let dgram = server.process_output(now()).dgram();

    // The server shouldn't have released more stream credit.
    client.process_input(dgram.unwrap(), now());
    let e = client.stream_create(StreamType::BiDi).unwrap_err();
    assert!(matches!(e, Error::StreamLimit));

    // Closing the stream isn't enough.
    server.stream_close_send(stream_id).unwrap();
    let dgram = server.process_output(now()).dgram();
    client.process_input(dgram.unwrap(), now());
    assert!(client.stream_create(StreamType::BiDi).is_err());

    // The server needs to see an acknowledgment from the client for its
    // response AND the server has to read all of the request.
    // and the server needs to read all the data.  Read first.
    let mut buf = [0; REQUEST.len()];
    let (count, fin) = server.stream_recv(stream_id, &mut buf).unwrap();
    assert_eq!(&buf[..count], REQUEST);
    assert!(fin);

    // We need an ACK from the client now, but that isn't guaranteed,
    // so give the client one more packet just in case.
    let dgram = send_something(&mut server, now());
    client.process_input(dgram, now());

    // Now get the client to send the ACK and have the server handle that.
    let dgram = send_something(&mut client, now());
    let dgram = server.process(Some(dgram), now()).dgram();
    client.process_input(dgram.unwrap(), now());
    assert!(client.stream_create(StreamType::BiDi).is_ok());
    assert!(client.stream_create(StreamType::BiDi).is_err());
}

/// Raising the incoming stream limit at runtime is advertised to the peer via
/// `MAX_STREAMS`, so it can open streams beyond the limit the handshake set.
#[test]
fn set_remote_max_streams_lets_peer_open_more_streams() {
    for st in [StreamType::BiDi, StreamType::UniDi] {
        let mut client = default_client();
        let mut server = new_server(ConnectionParameters::default().max_streams(st, 1));
        connect(&mut client, &mut server);

        let first = client.stream_create(st).unwrap();
        assert_eq!(client.stream_create(st).unwrap_err(), Error::StreamLimit);

        server.set_remote_max_streams(st, 3);
        let dgram = server.process_output(now()).dgram();
        client.process_input(dgram.expect("MAX_STREAMS is sent right away"), now());

        assert!(client.stream_create(st).is_ok());
        assert!(client.stream_create(st).is_ok());
        assert_eq!(client.stream_create(st).unwrap_err(), Error::StreamLimit);

        // Monotonic: a lower value changes nothing and sends nothing, so
        // retiring a stream still frees exactly one more.
        server.set_remote_max_streams(st, 2);
        assert!(server.process_output(now()).dgram().is_none());

        client.stream_close_send(first).unwrap();
        let dgram = client.process_output(now()).dgram();
        server.process_input(dgram.unwrap(), now());
        let mut buf = [0; 16];
        assert_eq!(server.stream_recv(first, &mut buf).unwrap(), (0, true));
        if st == StreamType::BiDi {
            server.stream_close_send(first).unwrap();
            let dgram = server.process_output(now()).dgram();
            client.process_input(dgram.unwrap(), now());
            let dgram = send_something(&mut client, now());
            server.process_input(dgram, now());
        }
        let dgram = server.process_output(now()).dgram();
        client.process_input(dgram.expect("retiring a stream sends MAX_STREAMS"), now());

        assert!(client.stream_create(st).is_ok());
        assert_eq!(client.stream_create(st).unwrap_err(), Error::StreamLimit);
    }
}

/// A closed stream still frees a slot after a raise: the next `MAX_STREAMS`
/// is the streams retired so far plus the raised limit.
#[test]
fn set_remote_max_streams_then_retire_frees_another_stream() {
    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_streams(StreamType::UniDi, 1));
    connect(&mut client, &mut server);

    server.set_remote_max_streams(StreamType::UniDi, 2);
    let dgram = server.process_output(now()).dgram();
    client.process_input(dgram.unwrap(), now());

    let first = client.stream_create(StreamType::UniDi).unwrap();
    assert!(client.stream_create(StreamType::UniDi).is_ok());
    assert_eq!(
        client.stream_create(StreamType::UniDi).unwrap_err(),
        Error::StreamLimit
    );

    client.stream_close_send(first).unwrap();
    let dgram = client.process_output(now()).dgram();
    server.process_input(dgram.unwrap(), now());
    let mut buf = [0; 16];
    assert_eq!(server.stream_recv(first, &mut buf).unwrap(), (0, true));
    let dgram = server.process_output(now()).dgram();
    client.process_input(dgram.expect("retiring a stream sends MAX_STREAMS"), now());

    assert!(client.stream_create(StreamType::UniDi).is_ok());
    assert_eq!(
        client.stream_create(StreamType::UniDi).unwrap_err(),
        Error::StreamLimit
    );
}

#[test]
fn no_dupdata_readable_events() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // create a stream
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out = client.process_output(now());
    drop(server.process(out.dgram(), now()));

    // We have a data_readable event.
    let stream_readable = |e| matches!(e, ConnectionEvent::RecvStreamReadable { .. });
    assert!(server.events().any(stream_readable));

    // Send one more data frame from client. The previous stream data has not been read yet,
    // therefore there should not be a new DataReadable event.
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out_second_data_frame = client.process_output(now());
    drop(server.process(out_second_data_frame.dgram(), now()));
    assert!(!server.events().any(stream_readable));

    // One more frame with a fin will not produce a new DataReadable event, because the
    // previous stream data has not been read yet.
    client.stream_send(stream_id, &[0x00]).unwrap();
    client.stream_close_send(stream_id).unwrap();
    let out_third_data_frame = client.process_output(now());
    drop(server.process(out_third_data_frame.dgram(), now()));
    assert!(!server.events().any(stream_readable));
}

#[test]
fn no_dupdata_readable_events_empty_last_frame() {
    let mut client = default_client();
    let mut server = default_server();
    connect(&mut client, &mut server);

    // create a stream
    let stream_id = client.stream_create(StreamType::BiDi).unwrap();
    client.stream_send(stream_id, &[0x00]).unwrap();
    let out = client.process_output(now());
    drop(server.process(out.dgram(), now()));

    // We have a data_readable event.
    let stream_readable = |e| matches!(e, ConnectionEvent::RecvStreamReadable { .. });
    assert!(server.events().any(stream_readable));

    // An empty frame with a fin will not produce a new DataReadable event, because
    // the previous stream data has not been read yet.
    client.stream_close_send(stream_id).unwrap();
    let out_second_data_frame = client.process_output(now());
    drop(server.process(out_second_data_frame.dgram(), now()));
    assert!(!server.events().any(stream_readable));
}

fn change_flow_control(stream_type: StreamType, new_fc: u64) {
    const RECV_BUFFER_START: u64 = 300;

    let mut client = new_client(
        ConnectionParameters::default()
            .max_stream_data(StreamDataLimit::BiDiRemote, RECV_BUFFER_START)
            .max_stream_data(StreamDataLimit::UniDi, RECV_BUFFER_START),
    );
    let mut server = default_server();
    connect(&mut client, &mut server);

    // create a stream
    let stream_id = server.stream_create(stream_type).unwrap();
    let written1 = server.stream_send(stream_id, &[0x0; 10000]).unwrap();
    assert_eq!(to_u64(written1), RECV_BUFFER_START);

    // Send the stream to the client.
    let out = server.process_output(now());
    drop(client.process(out.dgram(), now()));

    // change max_stream_data for stream_id.
    client.set_stream_max_data(stream_id, new_fc).unwrap();

    // server should receive a MAX_SREAM_DATA frame if the flow control window is updated.
    let out2 = client.process_output(now());
    let out3 = server.process(out2.dgram(), now());
    let expected = usize::from(RECV_BUFFER_START < new_fc);
    assert_eq!(server.stats().frame_rx.max_stream_data, expected);

    // If the flow control window has been increased, server can write more data.
    let written2 = server.stream_send(stream_id, &[0x0; 10000]).unwrap();
    if RECV_BUFFER_START < new_fc {
        assert_eq!(to_u64(written2), new_fc - RECV_BUFFER_START);
    } else {
        assert_eq!(written2, 0);
    }

    // Exchange packets so that client gets all data.
    let out4 = client.process(out3.dgram(), now());
    let out5 = server.process(out4.dgram(), now());
    drop(client.process(out5.dgram(), now()));

    // read all data by client
    let mut buf = [0x0; 10000];
    let (read, _) = client.stream_recv(stream_id, &mut buf).unwrap();
    assert_eq!(to_u64(read), max(RECV_BUFFER_START, new_fc));

    let out4 = client.process_output(now());
    drop(server.process(out4.dgram(), now()));

    let written3 = server.stream_send(stream_id, &[0x0; 10000]).unwrap();
    assert_eq!(to_u64(written3), new_fc);
}

#[test]
fn increase_decrease_flow_control() {
    const RECV_BUFFER_NEW_BIGGER: u64 = 400;
    const RECV_BUFFER_NEW_SMALLER: u64 = 200;

    change_flow_control(StreamType::UniDi, RECV_BUFFER_NEW_BIGGER);
    change_flow_control(StreamType::BiDi, RECV_BUFFER_NEW_BIGGER);

    change_flow_control(StreamType::UniDi, RECV_BUFFER_NEW_SMALLER);
    change_flow_control(StreamType::BiDi, RECV_BUFFER_NEW_SMALLER);
}

#[test]
fn session_flow_control_stop_sending_state_recv() {
    const SMALL_MAX_DATA: usize = 1024;

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );

    // send 1 byte so that the server learns about the stream.
    assert_eq!(client.stream_send(stream_id, b"a").unwrap(), 1);

    exchange_data(&mut client, &mut server);

    server
        .stream_stop_sending(stream_id, Error::None.code())
        .unwrap();

    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA])
            .unwrap(),
        SMALL_MAX_DATA - 1
    );

    // In this case the final size is only known after RESET frame is received.
    // The server sends STOP_SENDING -> the client sends RESET -> the server
    // sends MAX_DATA.
    let out = server.process_output(now()).dgram();
    let out = client.process(out, now()).dgram();
    // the client is still limited.
    let stream_id2 = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(client.stream_avail_send_space(stream_id2).unwrap(), 0);
    let out = server.process(out, now()).dgram();
    client.process_input(out.unwrap(), now());
    assert_eq!(
        client.stream_avail_send_space(stream_id2).unwrap(),
        SMALL_MAX_DATA
    );
}

#[test]
fn session_flow_control_stop_sending_state_size_known() {
    const SMALL_MAX_DATA: usize = 1024;

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );

    // send 1 byte so that the server learns about the stream.
    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA + 1])
            .unwrap(),
        SMALL_MAX_DATA
    );

    let out1 = client.process_output(now()).dgram();
    // Delay this packet and let the server receive fin first (it will enter SizeKnown state).
    client.stream_close_send(stream_id).unwrap();
    let out2 = client.process_output(now()).dgram();

    server.process_input(out2.unwrap(), now());

    server
        .stream_stop_sending(stream_id, Error::None.code())
        .unwrap();

    // In this case the final size is known when stream_stop_sending is called
    // and the server releases flow control immediately and sends STOP_SENDING and
    // MAX_DATA in the same packet.
    let out = server.process(out1, now()).dgram();
    client.process_input(out.unwrap(), now());

    // The flow control should have been updated and the client can again send
    // SMALL_MAX_DATA.
    let stream_id2 = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id2).unwrap(),
        SMALL_MAX_DATA
    );
}

#[test]
fn session_flow_control_stop_sending_state_data_recvd() {
    const SMALL_MAX_DATA: usize = 1024;

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );

    // send 1 byte so that the server learns about the stream.
    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA + 1])
            .unwrap(),
        SMALL_MAX_DATA
    );

    client.stream_close_send(stream_id).unwrap();

    exchange_data(&mut client, &mut server);

    // The stream is DataRecvd state
    server
        .stream_stop_sending(stream_id, Error::None.code())
        .unwrap();

    exchange_data(&mut client, &mut server);

    // The flow control should have been updated and the client can again send
    // SMALL_MAX_DATA.
    let stream_id2 = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id2).unwrap(),
        SMALL_MAX_DATA
    );
}

#[test]
fn session_flow_control_affects_all_streams() {
    const SMALL_MAX_DATA: usize = 1024;

    let mut client = default_client();
    let mut server = new_server(ConnectionParameters::default().max_data(to_u64(SMALL_MAX_DATA)));

    connect(&mut client, &mut server);

    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );

    let stream_id2 = client.stream_create(StreamType::UniDi).unwrap();
    assert_eq!(
        client.stream_avail_send_space(stream_id2).unwrap(),
        SMALL_MAX_DATA
    );

    assert_eq!(
        client
            .stream_send(stream_id, &[b'a'; SMALL_MAX_DATA / 2 + 1])
            .unwrap(),
        SMALL_MAX_DATA / 2 + 1
    );

    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA / 2 - 1
    );
    assert_eq!(
        client.stream_avail_send_space(stream_id2).unwrap(),
        SMALL_MAX_DATA / 2 - 1
    );

    exchange_data(&mut client, &mut server);

    let mut buf = [0x0; SMALL_MAX_DATA];
    let (read, _) = server.stream_recv(stream_id, &mut buf).unwrap();
    assert_eq!(read, SMALL_MAX_DATA / 2 + 1);

    exchange_data(&mut client, &mut server);

    assert_eq!(
        client.stream_avail_send_space(stream_id).unwrap(),
        SMALL_MAX_DATA
    );

    assert_eq!(
        client.stream_avail_send_space(stream_id2).unwrap(),
        SMALL_MAX_DATA
    );
}

fn connect_w_different_limit(bidi_limit: u64, unidi_limit: u64) {
    let mut client = default_client();
    let out = client.process_output(now());
    let out2 = client.process_output(now());
    let mut server = new_server(
        ConnectionParameters::default()
            .max_streams(StreamType::BiDi, bidi_limit)
            .max_streams(StreamType::UniDi, unidi_limit),
    );
    server.process_input(out.dgram().unwrap(), now());
    let out = server.process(out2.dgram(), now());

    let out = client.process(out.dgram(), now());
    let out = server.process(out.dgram(), now());
    let out = client.process(out.dgram(), now());
    drop(server.process(out.dgram(), now()));

    assert!(maybe_authenticate(&mut client));

    let mut bidi_events = 0;
    let mut unidi_events = 0;
    let mut connected_events = 0;
    for e in client.events() {
        match e {
            ConnectionEvent::SendStreamCreatable { stream_type } => {
                if stream_type == StreamType::BiDi {
                    bidi_events += 1;
                } else {
                    unidi_events += 1;
                }
            }
            ConnectionEvent::StateChange(State::Connected) => {
                connected_events += 1;
            }
            _ => {}
        }
    }
    assert_eq!(bidi_events, usize::from(bidi_limit > 0));
    assert_eq!(unidi_events, usize::from(unidi_limit > 0));
    assert_eq!(connected_events, 1);
}

#[test]
fn client_stream_creatable_event() {
    connect_w_different_limit(0, 0);
    connect_w_different_limit(0, 1);
    connect_w_different_limit(1, 0);
    connect_w_different_limit(1, 1);
}

/// Connect client and server, create a unidirectional stream, and exhaust the stream limit.
/// Returns the stream ID of the one open stream.
fn setup_exhausted_uni_streams(client: &mut Connection, server: &mut Connection) -> StreamId {
    connect(client, server);
    let stream_id = client.stream_create(StreamType::UniDi).unwrap();
    while client.stream_create(StreamType::UniDi).is_ok() {}
    stream_id
}

/// `stream_stop_sending` on a stream in `DataRecvd` transitions to `DataRead` (ended).
/// Verify the stream is cleaned up so the peer regains stream credit.
#[test]
fn uni_stream_cleaned_up_after_stop_sending_on_data_recvd() {
    let mut client = default_client();
    let mut server = default_server();
    let stream_id = setup_exhausted_uni_streams(&mut client, &mut server);

    // Send data + FIN; server reaches DataRecvd.
    client.stream_send(stream_id, b"hello").unwrap();
    client.stream_close_send(stream_id).unwrap();
    exchange_data(&mut client, &mut server);

    // stop_sending on DataRecvd → DataRead (ended); stream is cleaned up.
    server
        .stream_stop_sending(stream_id, Error::None.code())
        .unwrap();
    exchange_data(&mut client, &mut server);

    client.stream_create(StreamType::UniDi).unwrap();
}

/// `stop_sending_acked` on a stream in `AbortReading { final_size_reached: true }`
/// transitions to `ResetRecvd` (ended). Verify the stream is cleaned up.
#[test]
fn uni_stream_cleaned_up_after_stop_sending_acked_with_final_size_known() {
    let mut client = default_client();
    let mut server = default_server();
    let stream_id = setup_exhausted_uni_streams(&mut client, &mut server);

    // Send data and FIN in two separate packets; delay the data packet so the server
    // receives the FIN first and enters SizeKnown (final size known, data not yet complete).
    client.stream_send(stream_id, b"hello").unwrap();
    let data_pkt = client.process_output(now()).dgram();
    client.stream_close_send(stream_id).unwrap();
    let fin_pkt = client.process_output(now()).dgram();
    server.process_input(fin_pkt.unwrap(), now());

    // stop_sending in SizeKnown → AbortReading { final_size_reached: true }.
    server
        .stream_stop_sending(stream_id, Error::None.code())
        .unwrap();

    // Deliver the delayed data packet and complete the exchange; STOP_SENDING is acked,
    // triggering stop_sending_acked → ResetRecvd (ended); stream is cleaned up.
    server.process_input(data_pkt.unwrap(), now());
    exchange_data(&mut client, &mut server);

    client.stream_create(StreamType::UniDi).unwrap();
}
