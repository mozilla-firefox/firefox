// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    num::NonZeroUsize,
    time::{Duration, Instant},
};

use neqo_common::{Encoder, event::Provider as _, to_u64};
use neqo_transport::{ConnectionParameters, DatagramQueueOutcome, StreamId, streams::SendGroupId};
use test_fixture::now;

use crate::{
    Http3ClientEvent, Http3ServerEvent, Res, SessionAcceptAction, WebTransportEvent,
    features::extended_connect::{
        stats::SessionStats,
        tests::webtransport::{DATAGRAM_SIZE, WtTest, wt_default_parameters},
    },
    frames::WebTransportFrame,
    webtransport::{ClientSession as _, ServerEvent, ServerSession},
};

const DGRAM: &[u8] = &[0, 100];
const DATAGRAM_FRAME_TYPE_VARINT_LEN: u64 = 1;

fn do_datagram_test(wt: &mut WtTest, wt_session: &ServerSession) {
    assert_eq!(
        wt_session.max_datagram_size(),
        Ok(DATAGRAM_SIZE
            - DATAGRAM_FRAME_TYPE_VARINT_LEN
            - to_u64(Encoder::varint_len(wt_session.stream_id().as_u64())))
    );
    assert_eq!(
        wt.max_datagram_size(wt_session.stream_id()),
        Ok(DATAGRAM_SIZE
            - DATAGRAM_FRAME_TYPE_VARINT_LEN
            - to_u64(Encoder::varint_len(wt_session.stream_id().as_u64())))
    );

    assert_eq!(
        wt_session.send_datagram(DGRAM, None, now(), SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::Ok)
    );
    assert_eq!(wt.send_datagram(wt_session.stream_id(), DGRAM), Ok(()));

    wt.exchange_packets();
    wt.check_datagram_received_client(wt_session.stream_id(), DGRAM);
    wt.check_datagram_received_server(wt_session, DGRAM);
}

#[test]
fn datagrams() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    do_datagram_test(&mut wt, &wt_session);
}

#[test]
fn datagrams_multiple_session() {
    let mut wt = WtTest::new();

    let wt_session1 = wt.create_wt_session();
    do_datagram_test(&mut wt, &wt_session1);

    let wt_session_2 = wt.create_wt_session();
    do_datagram_test(&mut wt, &wt_session_2);
}

// A peer is allowed to advertise a max_datagram_frame_size smaller than the
// per-datagram quarter-stream-id prefix. Once a session lands on a stream id
// whose quarter stream id needs a longer varint than the available datagram
// size (quarter stream id >= 64, i.e. stream id >= 256, needs two bytes), the
// prefix subtraction must clamp to zero instead of wrapping.
#[test]
fn max_datagram_size_smaller_than_session_prefix() {
    let params = || {
        wt_default_parameters()
            .connection_parameters(ConnectionParameters::default().datagram_size(1))
    };
    let mut wt = WtTest::new_with_params(params(), params());

    let mut wt_session = wt.create_wt_session();
    while wt_session.stream_id().as_u64() < 256 {
        wt_session = wt.create_wt_session();
    }
    assert_eq!(Encoder::varint_len(wt_session.stream_id().as_u64() >> 2), 2);

    assert_eq!(wt_session.max_datagram_size(), Ok(0));
    assert_eq!(wt.max_datagram_size(wt_session.stream_id()), Ok(0));
}

#[test]
fn datagram_expires_before_being_sent() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let t0 = now();

    wt_session
        .set_datagram_max_age(Some(Duration::from_millis(5)), t0)
        .unwrap();
    assert_eq!(
        wt_session.send_datagram(DGRAM, Some(1), t0, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::Ok)
    );
    assert_eq!(wt_session.datagram_queue_capacity().queued_datagrams, 1);

    // No packets ever need to be built in between: expiry must not wait on
    // that. Driving the server's own HTTP/3 tick (not exchange_packets,
    // which uses its own clock) is enough on its own.
    let later = t0 + Duration::from_millis(10);
    drop(wt.server.process_output(later));

    assert_eq!(
        wt_session.datagram_queue_capacity().queued_datagrams,
        0,
        "the stale datagram must be gone before it is ever handed to the QUIC layer"
    );
    assert_eq!(wt_session.stats().outgoing_datagrams.expired, 1);
    assert_eq!(wt_session.stats().outgoing_datagrams.sent, 0);
}

#[test]
fn datagram_larger_than_peers_limit_is_rejected_synchronously() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    let max = wt_session
        .max_datagram_size()
        .expect("datagrams are enabled by default");
    let oversized = vec![0; usize::try_from(max).unwrap() + 1];

    assert_eq!(
        wt_session.send_datagram(&oversized, Some(1), now(), SendGroupId::new(0), 0),
        Err(crate::Error::Transport(neqo_transport::Error::TooMuchData)),
        "an oversized datagram must fail before ever reaching the queue"
    );
    assert_eq!(wt_session.datagram_queue_capacity().queued_datagrams, 0);
}

/// The other side of the boundary: a datagram of exactly `max_datagram_size`
/// is accepted.
#[test]
fn datagram_of_exactly_the_peers_limit_is_accepted() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    let max = wt_session
        .max_datagram_size()
        .expect("datagrams are enabled by default");
    let largest = vec![0; usize::try_from(max).unwrap()];

    assert_eq!(
        wt_session.send_datagram(&largest, Some(1), now(), SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::Ok)
    );
    assert_eq!(wt_session.datagram_queue_capacity().queued_datagrams, 1);
}

/// `OutgoingDatagramStats::sent` must count a datagram sent
/// without a tracking id.
#[test]
fn untracked_datagram_sent_is_counted_in_aggregate_stats() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    assert_eq!(
        client_send(&mut wt, session_id, DGRAM, None, now()),
        Ok(DatagramQueueOutcome::Ok)
    );
    wt.exchange_packets();

    let stats = client_stats(&wt, session_id);
    assert_eq!(stats.outgoing_datagrams.sent, 1);
    assert_eq!(stats.outgoing_datagrams.dropped, 0);
}

/// [`Http3Client::webtransport_datagram_queue_capacity`] reports this
/// session's own queue, and no other session's.
#[test]
fn datagram_queue_capacity_is_per_session() {
    const BURST: u8 = 20;

    let mut wt = WtTest::new();
    let session_id = wt.create_wt_session().stream_id();
    let other_session_id = wt.create_wt_session().stream_id();

    let before = wt
        .client
        .webtransport_datagram_queue_capacity(session_id)
        .unwrap();
    assert_eq!(before.queued_datagrams, 0);
    assert_eq!(before.remaining_bytes, before.max_queued_bytes);

    for i in 0..BURST {
        _ = client_send(&mut wt, session_id, &[0, i], Some(u64::from(i)), now()).unwrap();
    }

    let after = wt
        .client
        .webtransport_datagram_queue_capacity(session_id)
        .unwrap();
    assert_eq!(after.queued_datagrams, usize::from(BURST));
    assert!(
        after.remaining_bytes < before.remaining_bytes,
        "enqueuing datagrams must consume some of the byte budget"
    );
    assert_eq!(after.max_queued_bytes, before.max_queued_bytes);

    assert_eq!(
        wt.client
            .webtransport_datagram_queue_capacity(other_session_id)
            .unwrap(),
        before,
        "another session's queue must be untouched by this session's burst"
    );
    assert_eq!(
        wt.client
            .webtransport_datagram_queue_capacity(StreamId::new(1_000_000)),
        Err(crate::Error::InvalidStreamId)
    );
}

/// With a mark of 2 already set, the first datagram `send` queues is `Ok`
/// and the second crosses the mark.
fn assert_second_datagram_crosses_a_mark_of_two(
    mut send: impl FnMut(u64) -> Result<DatagramQueueOutcome, crate::Error>,
) {
    assert_eq!(send(1), Ok(DatagramQueueOutcome::Ok));
    assert_eq!(
        send(2),
        Ok(DatagramQueueOutcome::MaxBufferedReached),
        "the second datagram crosses the max-buffered limit"
    );
}

#[test]
fn max_buffered_datagrams_signals_backpressure() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();

    wt_session
        .set_max_buffered_datagrams(Some(NonZeroUsize::new(2).unwrap()))
        .unwrap();
    assert_second_datagram_crosses_a_mark_of_two(|id| {
        wt_session.send_datagram(DGRAM, Some(id), now(), SendGroupId::new(0), 0)
    });
}

/// Draining a queue that reported `MaxBufferedReached` must surface the resume
/// event on both sides: [`Http3ClientEvent::OutgoingDatagramSpaceAvailable`]
/// to the client and [`Http3ServerEvent::OutgoingDatagramSpaceAvailable`] to
/// the server.
#[test]
fn outgoing_datagram_space_available_forwarded() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();
    let t0 = now();

    wt.client
        .webtransport_set_max_buffered_datagrams(session_id, Some(NonZeroUsize::new(1).unwrap()))
        .unwrap();
    assert_eq!(
        client_send(&mut wt, session_id, DGRAM, None, t0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );
    assert!(
        !wt.client
            .events()
            .any(|e| matches!(e, Http3ClientEvent::OutgoingDatagramSpaceAvailable { .. })),
        "client resume event fired before the queue drained"
    );

    wt_session
        .set_max_buffered_datagrams(Some(NonZeroUsize::new(1).unwrap()))
        .unwrap();
    assert_eq!(
        wt_session.send_datagram(DGRAM, Some(1), t0, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::MaxBufferedReached)
    );
    assert!(
        !wt.server
            .events()
            .any(|e| matches!(e, Http3ServerEvent::OutgoingDatagramSpaceAvailable { .. })),
        "server resume event fired before the queue drained"
    );

    wt.exchange_packets();

    assert!(
        wt.client
            .events()
            .any(|e| matches!(e, Http3ClientEvent::OutgoingDatagramSpaceAvailable { session_id: id } if id == session_id)),
        "OutgoingDatagramSpaceAvailable was not forwarded to the HTTP/3 client"
    );
    assert!(
        wt.server
            .events()
            .any(|e| matches!(e, Http3ServerEvent::OutgoingDatagramSpaceAvailable { session_id: id, .. } if id == session_id)),
        "OutgoingDatagramSpaceAvailable was not forwarded to the HTTP/3 server"
    );
}

#[test]
fn close_session_stats_include_datagrams_expired_since_the_last_sweep() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let t0 = now();

    wt_session
        .set_datagram_max_age(Some(Duration::from_millis(5)), t0)
        .unwrap();
    assert_eq!(
        wt_session.send_datagram(DGRAM, Some(1), t0, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::Ok)
    );

    let stats = wt_session
        .close_session(0, "", t0 + Duration::from_millis(10))
        .unwrap();
    assert_eq!(stats.outgoing_datagrams.expired, 1);
}

#[test]
fn shrinking_max_age_counts_expired_datagrams_immediately() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let t0 = now();

    assert_eq!(
        wt_session.send_datagram(DGRAM, Some(1), t0, SendGroupId::new(0), 0),
        Ok(DatagramQueueOutcome::Ok)
    );
    wt_session
        .set_datagram_max_age(
            Some(Duration::from_millis(5)),
            t0 + Duration::from_millis(10),
        )
        .unwrap();

    assert_eq!(wt_session.stats().outgoing_datagrams.expired, 1);
}

/// A client that resets the session takes the server's queued datagrams
/// with it.
#[test]
fn session_reset_by_client_drops_the_servers_queued_datagrams() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    // One datagram per packet, so the server's reply cannot flush them all.
    let largest = vec![0; usize::try_from(wt_session.max_datagram_size().unwrap()).unwrap()];
    for id in 0..3 {
        assert_eq!(
            wt_session.send_datagram(&largest, Some(id), now(), SendGroupId::new(0), 0),
            Ok(DatagramQueueOutcome::Ok)
        );
    }
    assert_eq!(wt_session.transport_queued_datagrams(), 3);
    wt.client
        .cancel_fetch(session_id, crate::Error::HttpNone.code())
        .unwrap();
    let reset = wt.client.process_output(now()).dgram().unwrap();
    drop(wt.server.process(Some(reset), now()));

    assert_eq!(wt_session.transport_queued_datagrams(), 0);
}

/// A close capsule without a FIN leaves the server's session in `FinPending`
/// until the FIN arrives, which may be never; its queue must not wait for it.
#[test]
fn close_capsule_without_fin_drops_the_servers_queued_datagrams() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();
    let t0 = now();

    // One datagram per packet, so the server's reply cannot flush them all.
    let largest = vec![0; usize::try_from(wt_session.max_datagram_size().unwrap()).unwrap()];
    for id in 0..3 {
        assert_eq!(
            wt_session.send_datagram(&largest, Some(id), t0, SendGroupId::new(0), 0),
            Ok(DatagramQueueOutcome::Ok)
        );
    }

    let mut enc = Encoder::default();
    WebTransportFrame::CloseSession {
        error: 0,
        message: String::new(),
    }
    .encode(&mut enc);
    wt.client.send_data(session_id, enc.as_ref(), t0).unwrap();
    let close = wt.client.process_output(t0).dgram().unwrap();
    drop(wt.server.process(Some(close), t0));

    assert_eq!(wt_session.transport_queued_datagrams(), 0);
}

#[test]
fn datagram_send_order_controls_priority() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let t0 = now();

    // Enqueue the low-priority one first; both ungrouped (SendGroupId::new(0)),
    // so send_order alone must decide delivery order.
    assert_eq!(
        wt_session.send_datagram(b"low", Some(1), t0, SendGroupId::new(0), 1),
        Ok(DatagramQueueOutcome::Ok)
    );
    assert_eq!(
        wt_session.send_datagram(b"high", Some(2), t0, SendGroupId::new(0), 10),
        Ok(DatagramQueueOutcome::Ok)
    );

    wt.exchange_packets();

    let received: Vec<Vec<u8>> = wt
        .client
        .events()
        .filter_map(|e| match e {
            Http3ClientEvent::WebTransport(WebTransportEvent::Datagram { datagram, .. }) => {
                Some(datagram.as_ref().to_vec())
            }
            _ => None,
        })
        .collect();
    assert_eq!(
        received,
        vec![b"high".to_vec(), b"low".to_vec()],
        "the higher send_order datagram must be delivered first"
    );
}

#[test]
fn session_close_counts_queued_datagrams_as_dropped() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let t0 = now();

    _ = wt_session
        .send_datagram(DGRAM, Some(3), t0, SendGroupId::new(0), 0)
        .unwrap();
    let stats = wt_session.close_session(0, "bye", t0).unwrap();

    assert_eq!(
        stats.outgoing_datagrams.dropped, 1,
        "the stats close_session returns must already count the drop"
    );
}

/// Enqueueing only checks the peer's datagram limit, which can exceed the
/// path MTU; a datagram between the two is dropped at packet-build time.
#[test]
fn datagram_too_big_for_the_path_mtu_is_counted_as_dropped() {
    let server_params = wt_default_parameters()
        .connection_parameters(ConnectionParameters::default().datagram_size(2 * DATAGRAM_SIZE));
    let mut wt = WtTest::new_with_params(wt_default_parameters(), server_params);
    let session_id = wt.create_wt_session().stream_id();
    let too_big = vec![0; usize::try_from(DATAGRAM_SIZE + 100).unwrap()];

    assert_eq!(
        client_send(&mut wt, session_id, &too_big, None, now()),
        Ok(DatagramQueueOutcome::Ok)
    );
    wt.exchange_packets();

    let stats = client_stats(&wt, session_id);
    assert_eq!(stats.outgoing_datagrams.dropped, 1);
    assert_eq!(stats.outgoing_datagrams.sent, 0);
}

#[test]
fn datagram_expires_on_the_implementation_defined_default_max_age() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let t0 = now();

    // No set_datagram_max_age call: outgoingMaxAge is left at its default.
    _ = wt_session
        .send_datagram(DGRAM, Some(13), t0, SendGroupId::new(0), 0)
        .unwrap();

    // Well past the default, so it expires before it can be sent.
    drop(wt.server.process_output(t0 + Duration::from_secs(1)));

    assert_eq!(wt_session.stats().outgoing_datagrams.expired, 1);
    assert_eq!(wt_session.stats().outgoing_datagrams.sent, 0);
}

#[test]
fn client_set_datagram_max_age_expires_queued_datagrams() {
    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();
    let t0 = now();

    assert_eq!(
        client_send(&mut wt, session_id, DGRAM, Some(7), t0),
        Ok(DatagramQueueOutcome::Ok)
    );

    let t1 = t0 + Duration::from_millis(200);
    wt.client
        .webtransport_set_datagram_max_age(session_id, Some(Duration::from_millis(100)), t1)
        .unwrap();

    assert_eq!(
        client_stats(&wt, session_id).outgoing_datagrams.expired,
        1,
        "shortening max_age past an already-queued datagram must expire it immediately"
    );
}

/// A burst exceeding the byte budget, with a mix of send-order priorities,
/// must evict low-priority datagrams to make room for high-priority ones.
#[test]
fn datagram_burst_exceeding_byte_budget_preserves_priority_through_a_live_connection() {
    const HIGH_PRIORITY_COUNT: usize = 5;

    let mut wt = WtTest::new();
    let wt_session = wt.create_wt_session();
    let session_id = wt_session.stream_id();

    // Fill the byte budget with low-priority (order=0) datagrams, without
    // draining, until eviction starts - i.e. until the budget is full.
    let mut low_priority_ids = Vec::new();
    let mut total_evicted: usize = 0;
    let mut next_id: u64 = 0;
    loop {
        let outcome = client_send(
            &mut wt,
            session_id,
            &next_id.to_le_bytes(),
            Some(next_id),
            now(),
        )
        .unwrap();
        low_priority_ids.push(next_id);
        next_id += 1;
        if let DatagramQueueOutcome::Overflowed { dropped } = outcome {
            total_evicted += dropped;
            break;
        }
        assert_eq!(
            outcome,
            DatagramQueueOutcome::Ok,
            "unexpected outcome before the byte budget is hit"
        );
        assert!(
            next_id < 1_000_000,
            "byte budget should have been hit by now"
        );
    }

    // Now send a few high-priority datagrams, each forced to evict at least
    // one more low-priority datagram to make room.
    let high_priority_ids: Vec<u64> = (0..to_u64(HIGH_PRIORITY_COUNT))
        .map(|i| next_id + i)
        .collect();
    for &id in &high_priority_ids {
        let outcome = wt
            .client
            .webtransport_send_datagram(
                session_id,
                &id.to_le_bytes(),
                Some(id),
                now(),
                SendGroupId::new(0),
                10,
            )
            .unwrap();
        match outcome {
            DatagramQueueOutcome::Overflowed { dropped } => total_evicted += dropped,
            other => panic!("expected an eviction for the high-priority datagram: {other:?}"),
        }
    }

    // The backlog is tens of thousands of bytes deep; a real connection's
    // pacer/congestion window won't drain all of it in one exchange, so
    // this only checks what has arrived so far - not full delivery.
    wt.exchange_packets();

    let received: Vec<u64> = wt
        .server
        .events()
        .filter_map(|e| match e {
            Http3ServerEvent::WebTransport(ServerEvent::Datagram { session, datagram })
                if session.stream_id() == session_id =>
            {
                Some(u64::from_le_bytes(*datagram.as_ref().as_array().unwrap()))
            }
            _ => None,
        })
        .collect();

    for &id in &high_priority_ids {
        assert!(
            received.contains(&id),
            "high-priority datagram {id} must be sent ahead of the low-priority backlog"
        );
    }

    // Whatever low-priority datagrams made it out in this exchange, in
    // delivery order, must be exactly the earliest survivors - proving both
    // that eviction took the oldest ones first (a gap here would mean some
    // other one was evicted instead) and that delivery still respects FIFO
    // within the order-0 bucket. `DatagramQueueOutcome::Overflowed` no
    // longer names which ids it evicted, so this is the only ground truth
    // left for that check.
    let received_low: Vec<u64> = received
        .iter()
        .copied()
        .filter(|id| !high_priority_ids.contains(id))
        .collect();
    assert!(
        !received_low.is_empty(),
        "at least some of the surviving low-priority datagrams must have been delivered"
    );
    assert_eq!(
        received_low,
        low_priority_ids[total_evicted..total_evicted + received_low.len()],
        "surviving low-priority datagrams must be exactly the oldest ones eviction spared, in FIFO order"
    );
}

#[test]
fn client_set_max_buffered_datagrams_signals_backpressure() {
    let mut wt = WtTest::new();
    let session_id = wt.create_wt_session().stream_id();

    wt.client
        .webtransport_set_max_buffered_datagrams(session_id, Some(NonZeroUsize::new(2).unwrap()))
        .unwrap();
    assert_second_datagram_crosses_a_mark_of_two(|id| {
        client_send(&mut wt, session_id, DGRAM, Some(id), now())
    });
}

/// Raising the mark over a blocked queue resumes the sender with nothing
/// sent; lowering it below the queue's occupancy does not (no sender was
/// waiting), it only makes the next send report `MaxBufferedReached`.
#[test]
fn client_changing_the_max_buffered_limit_resumes_only_a_blocked_queue() {
    let mut wt = WtTest::new();
    let session_id = wt.create_wt_session().stream_id();
    let t0 = now();
    let mark = |wt: &mut WtTest, n| {
        wt.client
            .webtransport_set_max_buffered_datagrams(session_id, NonZeroUsize::new(n))
            .unwrap();
    };
    let send = |wt: &mut WtTest, id| client_send(wt, session_id, DGRAM, Some(id), t0).unwrap();
    // Read the transport's events directly: building a packet (as every
    // HTTP/3 `process_*` path would) sends the datagram, which resumes the
    // sender on its own.
    let resumed = |wt: &mut WtTest| {
        wt.client.connection_mut().events().any(|e| {
            matches!(
                e,
                neqo_transport::ConnectionEvent::OutgoingDatagramSpaceAvailable { .. }
            )
        })
    };

    mark(&mut wt, 1);
    assert_eq!(send(&mut wt, 1), DatagramQueueOutcome::MaxBufferedReached);
    assert!(!resumed(&mut wt));

    mark(&mut wt, 3);
    assert!(
        resumed(&mut wt),
        "raising the mark must resume a blocked sender"
    );

    assert_eq!(send(&mut wt, 2), DatagramQueueOutcome::Ok);
    mark(&mut wt, 1);
    assert!(!resumed(&mut wt), "no sender was waiting");
    assert_eq!(send(&mut wt, 3), DatagramQueueOutcome::MaxBufferedReached);
}

/// The setters must work before the CONNECT response arrives.
#[test]
fn client_setters_work_before_the_connect_response() {
    let mut wt = WtTest::new();
    let session_id = wt
        .client
        .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();

    wt.client
        .webtransport_set_max_buffered_datagrams(session_id, Some(NonZeroUsize::new(2).unwrap()))
        .unwrap();
    wt.client
        .webtransport_set_datagram_max_age(session_id, Some(Duration::from_secs(1)), now())
        .unwrap();

    wt.exchange_packets();
    while let Some(event) = wt.server.next_event() {
        if let Http3ServerEvent::WebTransport(ServerEvent::NewSession { session, .. }) = event {
            session
                .response(&SessionAcceptAction::Accept, now())
                .unwrap();
        }
    }
    wt.exchange_packets();

    assert_second_datagram_crosses_a_mark_of_two(|id| {
        client_send(&mut wt, session_id, DGRAM, Some(id), now())
    });
}

/// Send `buf` from the client, ungrouped at `send_order` 0.
fn client_send(
    wt: &mut WtTest,
    session_id: StreamId,
    buf: &[u8],
    id: Option<u64>,
    now: Instant,
) -> Res<DatagramQueueOutcome> {
    wt.client
        .webtransport_send_datagram(session_id, buf, id, now, SendGroupId::new(0), 0)
}

fn client_stats(wt: &WtTest, session_id: StreamId) -> SessionStats {
    wt.client.webtransport_session_stats(session_id).unwrap()
}

/// Fill `session_id`'s queue with untracked `send_order` 10 datagrams until
/// the byte budget evicts one; returns how many were accepted.
fn fill_to_the_byte_budget(wt: &mut WtTest, session_id: StreamId) -> u64 {
    for n in 1.. {
        let outcome = wt
            .client
            .webtransport_send_datagram(session_id, DGRAM, None, now(), SendGroupId::new(0), 10)
            .unwrap();
        if matches!(outcome, DatagramQueueOutcome::Overflowed { .. }) {
            return n;
        }
        assert!(n < 1_000_000, "byte budget should have been hit by now");
    }
    unreachable!()
}

/// Untracked datagrams evicted at the byte budget, or refused for being the
/// lowest priority in a full queue, are counted as dropped.
#[test]
fn evicted_and_rejected_datagrams_are_counted_as_dropped() {
    let mut wt = WtTest::new();
    let session_id = wt.create_wt_session().stream_id();

    fill_to_the_byte_budget(&mut wt, session_id);
    assert_eq!(client_stats(&wt, session_id).outgoing_datagrams.dropped, 1);

    assert_eq!(
        client_send(&mut wt, session_id, DGRAM, None, now()),
        Ok(DatagramQueueOutcome::Rejected)
    );
    assert_eq!(client_stats(&wt, session_id).outgoing_datagrams.dropped, 2);
}

/// Every accepted datagram ends up sent, expired or dropped exactly once.
#[test]
fn every_accepted_datagram_is_counted_exactly_once() {
    let mut wt = WtTest::new();
    let session_id = wt.create_wt_session().stream_id();
    let t0 = now();

    assert_eq!(
        client_send(&mut wt, session_id, DGRAM, None, t0),
        Ok(DatagramQueueOutcome::Ok)
    );
    wt.exchange_packets();

    assert_eq!(
        client_send(&mut wt, session_id, DGRAM, None, t0),
        Ok(DatagramQueueOutcome::Ok)
    );
    let t1 = t0 + Duration::from_millis(10);
    wt.client
        .webtransport_set_datagram_max_age(session_id, Some(Duration::from_millis(1)), t1)
        .unwrap();
    wt.client
        .webtransport_set_datagram_max_age(session_id, None, t1)
        .unwrap();

    let queued = fill_to_the_byte_budget(&mut wt, session_id);
    let accepted = 2 + queued;

    let stats = wt
        .client
        .webtransport_close_session(session_id, 0, "", now())
        .unwrap();
    assert_eq!(stats.outgoing_datagrams.sent, 1);
    assert_eq!(stats.outgoing_datagrams.expired, 1);
    assert_eq!(stats.outgoing_datagrams.dropped, queued);
    assert_eq!(
        stats.outgoing_datagrams.sent
            + stats.outgoing_datagrams.expired
            + stats.outgoing_datagrams.dropped,
        accepted
    );
}
