// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::collections::BTreeSet;

use neqo_common::{Encoder, event::Provider as _};
use neqo_transport::{ConnectionParameters, StreamId, StreamType};
use test_fixture::now;

use crate::{
    Error, Http3ClientEvent, Http3State, connection::HTTP3_UNI_CONTROL_STREAMS,
    features::extended_connect::tests::webtransport::WtTest, frames::WebTransportFrame,
    webtransport::ClientSession as _,
};

const BOTH: [StreamType; 2] = [StreamType::UniDi, StreamType::BiDi];

/// Connection-level baseline for stream limits. Values at or below this require no
/// `MAX_STREAMS` frame, since the transport already granted that much.
fn stream_limit_default(stream_type: StreamType) -> u16 {
    u16::try_from(ConnectionParameters::default().get_max_streams(stream_type)).unwrap()
}

fn client_max_streams_sent(wt: &WtTest) -> usize {
    wt.client.transport_stats().frame_tx.max_streams
}

fn set_anticipated(
    wt: &mut WtTest,
    stream_type: StreamType,
    session_id: StreamId,
    value: u16,
) -> crate::Res<()> {
    match stream_type {
        StreamType::UniDi => wt
            .client
            .webtransport_set_anticipated_incoming_uni_streams(session_id, value),
        StreamType::BiDi => wt
            .client
            .webtransport_set_anticipated_incoming_bidi_streams(session_id, value),
    }
}

#[test]
fn anticipated_invalid_session() {
    for st in BOTH {
        let mut wt = WtTest::new();
        assert_eq!(
            set_anticipated(&mut wt, st, StreamId::from(9999), 10),
            Err(Error::InvalidStreamId),
            "{st:?}"
        );
    }
}

// Set before the session finishes negotiating, as the W3C `WebTransportOptions`
// anticipated-stream members are: they are supplied at construction time.
#[test]
fn anticipated_uni_before_session_negotiated() {
    let mut wt = WtTest::new();
    let before = client_max_streams_sent(&wt);
    let session_id = wt
        .client
        .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
        .unwrap();
    set_anticipated(
        &mut wt,
        StreamType::UniDi,
        session_id,
        stream_limit_default(StreamType::UniDi) + 1,
    )
    .unwrap();
    wt.exchange_packets();
    assert!(
        client_max_streams_sent(&wt) > before,
        "a session set up before negotiation completes must still raise the limit"
    );
}

// Setting a value that, after adding control streams, stays at or below the default
// should not trigger a MAX_STREAMS frame.
#[test]
fn anticipated_uni_at_default_sends_no_frame() {
    let mut wt = WtTest::new();
    let session = wt.create_wt_session();
    let before = client_max_streams_sent(&wt);

    set_anticipated(
        &mut wt,
        StreamType::UniDi,
        session.stream_id(),
        stream_limit_default(StreamType::UniDi) - u16::try_from(HTTP3_UNI_CONTROL_STREAMS).unwrap(),
    )
    .unwrap();
    wt.exchange_packets();

    assert_eq!(
        client_max_streams_sent(&wt),
        before,
        "no MAX_STREAMS frame should be sent when value plus HTTP/3's own streams equals the default"
    );
}

// One more than that must send a frame, which it only does if HTTP/3's own streams are
// added on top.
#[test]
fn anticipated_uni_above_default_minus_control_sends_frame() {
    let mut wt = WtTest::new();
    let session = wt.create_wt_session();
    let before = client_max_streams_sent(&wt);

    set_anticipated(
        &mut wt,
        StreamType::UniDi,
        session.stream_id(),
        stream_limit_default(StreamType::UniDi) - u16::try_from(HTTP3_UNI_CONTROL_STREAMS).unwrap()
            + 1,
    )
    .unwrap();
    wt.exchange_packets();

    assert!(
        client_max_streams_sent(&wt) > before,
        "HTTP/3's own streams must be added to the anticipated value"
    );
}

#[test]
fn anticipated_above_default_sends_frame() {
    for st in BOTH {
        let mut wt = WtTest::new();
        let session = wt.create_wt_session();
        let before = client_max_streams_sent(&wt);

        set_anticipated(
            &mut wt,
            st,
            session.stream_id(),
            stream_limit_default(st) + 1,
        )
        .unwrap();
        wt.exchange_packets();

        assert!(
            client_max_streams_sent(&wt) > before,
            "{st:?}: a MAX_STREAMS frame should be sent when value exceeds the default"
        );
    }
}

// Reducing the value below a previously-set value must not send a frame (QUIC stream
// limits can only increase), but the credit already granted must remain usable.
#[test]
fn anticipated_uni_decrease_sends_no_frame() {
    for lowered in [stream_limit_default(StreamType::UniDi) + 10, 0] {
        let mut wt = WtTest::new();
        let session = wt.create_wt_session();

        set_anticipated(
            &mut wt,
            StreamType::UniDi,
            session.stream_id(),
            stream_limit_default(StreamType::UniDi) + 50,
        )
        .unwrap();
        wt.exchange_packets();
        let after_increase = client_max_streams_sent(&wt);

        set_anticipated(&mut wt, StreamType::UniDi, session.stream_id(), lowered).unwrap();
        wt.exchange_packets();

        assert_eq!(
            client_max_streams_sent(&wt),
            after_increase,
            "lowering anticipated streams to {lowered} must not send another MAX_STREAMS frame"
        );

        // Frame counts alone would not catch a *lowered* `max_active`: once the peer's
        // streams retire, the credit it gets back must still reflect the raised limit.
        let raised = stream_limit_default(StreamType::UniDi) + 50;
        for _ in 0..raised {
            let stream = WtTest::create_wt_stream_server(&session, StreamType::UniDi);
            stream.send_data(&[0; 10], now()).unwrap();
            stream.stream_close_send(now()).unwrap();
        }
        wt.exchange_packets();
        // A stream whose frames straddle a packet boundary gets more than one
        // `DataReadable`, and reading the FIN removes it, so read each stream once.
        let readable: BTreeSet<_> = wt
            .client
            .events()
            .filter_map(|event| match event {
                Http3ClientEvent::DataReadable { stream_id } => Some(stream_id),
                _ => None,
            })
            .collect();
        assert_eq!(readable.len(), usize::from(raised));
        let mut buf = [0; 10];
        for stream_id in readable {
            assert_eq!(
                wt.client.read_data(now(), stream_id, &mut buf).unwrap(),
                (10, true)
            );
        }
        wt.exchange_packets();
        for _ in 0..raised {
            WtTest::create_wt_stream_server(&session, StreamType::UniDi);
        }
    }
}

// With two sessions, the connection limit should be the sum of their
// anticipated values, not just the last-set value.
#[test]
fn anticipated_two_sessions_sums_values() {
    for st in BOTH {
        let mut wt = WtTest::new();
        let session_a = wt.create_wt_session();
        let session_b = wt.create_wt_session();
        let before = client_max_streams_sent(&wt);

        set_anticipated(
            &mut wt,
            st,
            session_a.stream_id(),
            stream_limit_default(st) + 1,
        )
        .unwrap();
        wt.exchange_packets();
        let after_a = client_max_streams_sent(&wt);
        assert!(
            after_a > before,
            "{st:?}: first session should raise the limit"
        );

        // The new total (A+B) exceeds the previously-sent limit, so another
        // MAX_STREAMS frame must be sent.
        set_anticipated(
            &mut wt,
            st,
            session_b.stream_id(),
            stream_limit_default(st) + 1,
        )
        .unwrap();
        wt.exchange_packets();

        assert!(
            client_max_streams_sent(&wt) > after_a,
            "{st:?}: a second session's anticipated streams should raise the limit again"
        );
    }
}

// Setting both uni and bidi at the same time triggers exactly two MAX_STREAMS
// frames (one for each type).
#[test]
fn anticipated_uni_and_bidi_both_send_frames() {
    let mut wt = WtTest::new();
    let session = wt.create_wt_session();
    let before = client_max_streams_sent(&wt);

    for st in BOTH {
        set_anticipated(
            &mut wt,
            st,
            session.stream_id(),
            stream_limit_default(st) + 1,
        )
        .unwrap();
    }
    wt.exchange_packets();

    assert_eq!(
        client_max_streams_sent(&wt),
        before + 2,
        "one MAX_STREAMS frame for uni and one for bidi"
    );
}

// A session the client has cancelled is gone from the maps.
#[test]
fn anticipated_rejected_after_session_cancelled() {
    for st in BOTH {
        let mut wt = WtTest::new();
        let session_id = wt.create_wt_session().stream_id();
        wt.cancel_session_client(session_id);

        assert_eq!(
            set_anticipated(&mut wt, st, session_id, 10),
            Err(Error::InvalidStreamId),
            "{st:?}"
        );
    }
}

// A closed session's anticipated streams must not count toward the
// connection-wide total, or a stale session could inflate the advertised
// limit indefinitely.
#[test]
fn anticipated_excludes_closed_session_from_total() {
    for st in BOTH {
        let mut wt = WtTest::new();
        let session_a = wt.create_wt_session();
        let session_b = wt.create_wt_session();
        let before = client_max_streams_sent(&wt);

        // Session A anticipates far more streams than B ever will, raising the
        // limit well above what B alone would need.
        set_anticipated(
            &mut wt,
            st,
            session_a.stream_id(),
            stream_limit_default(st) + 100,
        )
        .unwrap();
        wt.exchange_packets();
        let after_a = client_max_streams_sent(&wt);
        assert!(after_a > before, "{st:?}: session A should raise the limit");

        wt.cancel_session_client(session_a.stream_id());

        // If A's stale value still counted, A + B would exceed the limit A
        // already set and trigger another MAX_STREAMS frame.
        set_anticipated(
            &mut wt,
            st,
            session_b.stream_id(),
            stream_limit_default(st) + 1,
        )
        .unwrap();
        wt.exchange_packets();

        assert_eq!(
            client_max_streams_sent(&wt),
            after_a,
            "{st:?}: closed session A's anticipated streams must not count toward the total"
        );
    }
}

// A session the server has closed without a FIN yet is closing but still in the maps.
#[test]
fn anticipated_ignores_closing_session() {
    for st in BOTH {
        let mut wt = WtTest::new();
        let session_a = wt.create_wt_session();
        let session_b = wt.create_wt_session();

        set_anticipated(
            &mut wt,
            st,
            session_a.stream_id(),
            stream_limit_default(st) + 100,
        )
        .unwrap();
        wt.exchange_packets();
        let after_a = client_max_streams_sent(&wt);

        let mut enc = Encoder::default();
        WebTransportFrame::CloseSession {
            error: 0,
            message: String::new(),
        }
        .encode(&mut enc);
        session_a.send_data(enc.as_ref(), now()).unwrap();
        wt.exchange_packets();

        assert_eq!(
            set_anticipated(&mut wt, st, session_a.stream_id(), 10),
            Err(Error::InvalidStreamId),
            "{st:?}"
        );
        set_anticipated(
            &mut wt,
            st,
            session_b.stream_id(),
            stream_limit_default(st) + 1,
        )
        .unwrap();
        wt.exchange_packets();
        assert_eq!(client_max_streams_sent(&wt), after_a, "{st:?}");
    }
}

// The application-controlled total must be clamped, or a page opening many sessions
// each anticipating u16::MAX streams could ratchet MAX_STREAMS arbitrarily high and,
// since the limit is monotonic, keep it there for the life of the connection.
#[test]
fn anticipated_uni_total_is_clamped() {
    let mut wt = WtTest::new();
    let session_a = wt.create_wt_session();
    let session_b = wt.create_wt_session();
    let before = client_max_streams_sent(&wt);

    set_anticipated(&mut wt, StreamType::UniDi, session_a.stream_id(), u16::MAX).unwrap();
    wt.exchange_packets();
    let after_a = client_max_streams_sent(&wt);
    assert!(after_a > before, "session A should raise the limit");

    // Session A alone already saturates the clamp, so adding session B's anticipated
    // value should not raise the advertised total any further.
    set_anticipated(&mut wt, StreamType::UniDi, session_b.stream_id(), u16::MAX).unwrap();
    wt.exchange_packets();

    assert_eq!(
        client_max_streams_sent(&wt),
        after_a,
        "the total must be clamped to MAX_ANTICIPATED_INCOMING_STREAMS"
    );
}

// Counting MAX_STREAMS frames only proves we advertised a bigger limit, and
// `frame_tx.max_streams` does not distinguish the two stream types. This checks the
// limit is actually usable: the peer must be able to open more streams than the
// transport default once the session anticipates them.
#[test]
fn anticipated_lets_peer_open_more_streams() {
    const BUF: &[u8] = &[0; 10];

    for st in BOTH {
        let default_limit = stream_limit_default(st);
        let mut wt = WtTest::new();
        let wt_session = wt.create_wt_session();

        set_anticipated(&mut wt, st, wt_session.stream_id(), default_limit + 1).unwrap();
        wt.exchange_packets();

        // The server opens one more stream than the transport default would have
        // allowed. For unidirectional streams HTTP/3's own control streams share the
        // same limit, so going past the default at all requires the raised limit to
        // have taken effect.
        for _ in 0..=default_limit {
            let stream = WtTest::create_wt_stream_server(&wt_session, st);
            wt.send_data_server(&stream, BUF);
        }
        wt.exchange_packets();

        assert_eq!(
            wt.client.state(),
            Http3State::Connected,
            "{st:?}: raising the anticipated stream count must not break the connection"
        );
    }
}
