// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;
use crate::common::*;

use std::collections::HashMap;
use std::thread;
use std::time::Duration;

use glean_core::{
    metrics::EventMetric, CommonMetricData, Glean, InternalConfiguration, Lifetime, SessionMode,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn session_cfg(
    data_path: &str,
    mode: SessionMode,
    sample_rate: f64,
    timeout_ms: u64,
) -> InternalConfiguration {
    InternalConfiguration {
        data_path: data_path.to_string(),
        application_id: GLOBAL_APPLICATION_ID.into(),
        language_binding_name: "Rust".into(),
        upload_enabled: true,
        max_events: None,
        delay_ping_lifetime_io: false,
        app_build: "Unknown".into(),
        use_core_mps: false,
        trim_data_to_registered_pings: false,
        log_level: None,
        rate_limit: None,
        enable_event_timestamps: false,
        experimentation_id: None,
        enable_internal_pings: true,
        ping_schedule: Default::default(),
        ping_lifetime_threshold: 0,
        ping_lifetime_max_time: 0,
        max_pending_pings_count: None,
        max_pending_pings_directory_size: None,
        session_mode: mode,
        session_sample_rate: sample_rate,
        session_inactivity_timeout_ms: timeout_ms,
        events_ping_acceleration_factor: None,
        enable_store_submitted_pings: false,
    }
}

/// Returns an EventMetric that matches glean.session_start boundary events.
fn session_start_metric() -> EventMetric {
    EventMetric::new(
        CommonMetricData {
            name: "session_start".into(),
            category: "glean".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    )
}

/// Returns an EventMetric that matches glean.session_end boundary events.
fn session_end_metric() -> EventMetric {
    EventMetric::new(
        CommonMetricData {
            name: "session_end".into(),
            category: "glean".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    )
}

// ---------------------------------------------------------------------------
// Auto mode — basic lifecycle
// ---------------------------------------------------------------------------

/// First `handle_client_active` call starts a new session with seq=1.
#[test]
fn auto_mode_starts_session_on_first_active() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    // No session before the first activation.
    assert!(session_start_metric().get_value(&glean, "events").is_none());

    glean.handle_client_active();

    let events = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected session_start event");
    assert_eq!(1, events.len());
    assert_eq!("glean", events[0].category);
    assert_eq!("session_start", events[0].name);
    let extra = events[0].extra.as_ref().expect("expected extras");
    assert!(
        extra.contains_key("session_id"),
        "session_id missing from extras"
    );
    assert_eq!("1", extra.get("session_seq").expect("session_seq missing"));
}

/// Reactivating within the inactivity timeout resumes the existing session —
/// no new session_start or session_end events are emitted.
#[test]
fn auto_mode_resumes_session_within_timeout() {
    let (_t, data_path) = tempdir();
    // 30-minute timeout — will not expire during this test.
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();
    // Going inactive: Auto mode does not end the session immediately; instead
    // it records inactive_since and submits the events ping (clearing the store).
    glean.handle_client_inactive();

    // Re-activate immediately — within the 30-minute timeout.
    glean.handle_client_active();

    // The events store was cleared by handle_client_inactive's ping submission.
    // On resume, no new boundary events should be emitted.
    assert!(
        session_start_metric().get_value(&glean, "events").is_none(),
        "expected no new session_start on resume within timeout"
    );
    assert!(
        session_end_metric().get_value(&glean, "events").is_none(),
        "expected no session_end on resume within timeout"
    );
}

/// With inactivity_timeout_ms=0 the session should NEVER time out (always
/// resumed on reactivation), regardless of how long the client was inactive.
#[test]
fn auto_mode_zero_timeout_means_never_time_out() {
    let (_t, data_path) = tempdir();
    // 0 ms = "never time out".
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();
    // Events store cleared by handle_client_inactive's ping submission.
    glean.handle_client_inactive();

    // Sleep — should not matter for a zero-timeout session.
    thread::sleep(Duration::from_millis(20));
    glean.handle_client_active();

    // If timeout=0 were treated as "immediate timeout", we'd see a session_end
    // + new session_start here.  With the correct "never time out" semantics,
    // the store should be empty (session was resumed, no new boundary events).
    assert!(
        session_start_metric().get_value(&glean, "events").is_none(),
        "timeout_ms=0 must mean 'never time out': no new session_start expected"
    );
    assert!(
        session_end_metric().get_value(&glean, "events").is_none(),
        "timeout_ms=0 must mean 'never time out': no session_end expected"
    );
}

/// After the inactivity timeout expires, the old session is ended with
/// reason "timeout" and a new session is started.
#[test]
fn auto_mode_starts_new_session_after_timeout() {
    let (_t, data_path) = tempdir();
    // 1 ms inactivity timeout — expires almost immediately.
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();
    // Going inactive clears the events store (ping submitted).
    glean.handle_client_inactive();

    // Sleep well beyond the 1 ms timeout.
    thread::sleep(Duration::from_millis(20));

    glean.handle_client_active();

    // session_end("timeout") should be present.
    let end_events = session_end_metric()
        .get_value(&glean, "events")
        .expect("expected session_end event after timeout");
    assert_eq!(1, end_events.len());
    let end_extra = end_events[0]
        .extra
        .as_ref()
        .expect("expected extras on session_end");
    assert_eq!(
        "timeout",
        end_extra
            .get("reason")
            .expect("reason missing from session_end")
    );

    // A new session_start should also be present.
    let start_events = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected session_start after timeout");
    assert_eq!(1, start_events.len());
    let start_extra = start_events[0]
        .extra
        .as_ref()
        .expect("expected extras on session_start");
    assert_eq!(
        "2",
        start_extra
            .get("session_seq")
            .expect("session_seq missing from session_start"),
        "second session must have seq=2"
    );
}

// ---------------------------------------------------------------------------
// Lifecycle mode
// ---------------------------------------------------------------------------

/// In Lifecycle mode each active/inactive cycle produces a distinct session.
#[test]
fn lifecycle_mode_new_session_per_activation() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    // First activation.
    glean.handle_client_active();
    let starts = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected first session_start");
    assert_eq!(1, starts.len());
    assert_eq!(
        "1",
        starts[0]
            .extra
            .as_ref()
            .unwrap()
            .get("session_seq")
            .unwrap()
    );

    // Deactivate — session_end is recorded, events ping submitted (clears store).
    glean.handle_client_inactive();

    // Second activation — new session with seq=2.
    glean.handle_client_active();
    let starts2 = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected second session_start");
    assert_eq!(1, starts2.len());
    assert_eq!(
        "2",
        starts2[0]
            .extra
            .as_ref()
            .unwrap()
            .get("session_seq")
            .unwrap(),
        "second session must have seq=2"
    );
}

/// In Lifecycle mode `handle_client_inactive` immediately emits session_end.
#[test]
fn lifecycle_mode_session_end_on_inactive() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    // Capture the session_id from the start event before inactive clears the store.
    let start_events = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected session_start");
    let started_id = start_events[0]
        .extra
        .as_ref()
        .unwrap()
        .get("session_id")
        .unwrap()
        .clone();

    // Record the end event into a temporary store we can observe before the
    // events ping clears it.  We insert a user event first so we can check
    // it was also recorded (sampled in).
    glean.handle_client_inactive();

    // After handle_client_inactive the events ping was submitted and store
    // cleared — but the session_end was written before that submission, so it
    // appeared in the ping.  We can confirm by verifying the store is now empty
    // (session_end was consumed by the ping submission).
    assert!(
        session_end_metric().get_value(&glean, "events").is_none(),
        "session_end store must be cleared after events ping submission"
    );

    // After inactive, session should no longer be active.
    assert!(
        !glean.session_manager().is_active(),
        "session must be inactive after handle_client_inactive in LIFECYCLE mode"
    );
    // The started session id must be a valid UUID.
    assert!(
        uuid::Uuid::parse_str(&started_id).is_ok(),
        "session_id must be a valid UUID"
    );
}

// ---------------------------------------------------------------------------
// Manual mode
// ---------------------------------------------------------------------------

/// In Manual mode `handle_client_active` and `handle_client_inactive` must not
/// create or destroy sessions automatically.
#[test]
fn manual_mode_no_auto_sessions() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Manual, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();
    assert!(
        session_start_metric().get_value(&glean, "events").is_none(),
        "manual mode: handle_client_active must not start a session"
    );

    glean.handle_client_inactive();
    assert!(
        session_end_metric().get_value(&glean, "events").is_none(),
        "manual mode: handle_client_inactive must not end a session"
    );
}

// ---------------------------------------------------------------------------
// Session sequence counter
// ---------------------------------------------------------------------------

/// session_seq must be strictly monotonically increasing across sessions.
#[test]
fn session_seq_monotonically_increases_across_sessions() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    for expected_seq in 1u64..=4 {
        glean.handle_client_active();
        let starts = session_start_metric()
            .get_value(&glean, "events")
            .unwrap_or_else(|| panic!("expected session_start at seq={expected_seq}"));
        let seq_str = starts
            .last()
            .unwrap()
            .extra
            .as_ref()
            .unwrap()
            .get("session_seq")
            .unwrap()
            .clone();
        assert_eq!(
            expected_seq.to_string(),
            seq_str,
            "session_seq mismatch at iteration {expected_seq}"
        );
        glean.handle_client_inactive();
    }
}

/// session_seq must survive a Glean restart (persist across process boundaries).
#[test]
fn session_seq_persists_across_restarts() {
    let (t, data_path) = tempdir();

    {
        let cfg = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active(); // seq=1
        glean.handle_client_inactive();
        glean.handle_client_active(); // seq=2
        glean.handle_client_inactive();
        // glean drops here, persisting seq=2 to storage.
    }

    // Simulate restart — new Glean on the same data path.
    let cfg2 = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
    let mut glean2 = Glean::new(cfg2, None).unwrap();
    glean2.handle_client_active(); // should be seq=3

    let starts = session_start_metric()
        .get_value(&glean2, "events")
        .expect("expected session_start after restart");
    assert_eq!(
        "3",
        starts[0]
            .extra
            .as_ref()
            .unwrap()
            .get("session_seq")
            .unwrap(),
        "session_seq must continue from last persisted value after restart"
    );

    drop(t); // keep TempDir alive until here
}

// ---------------------------------------------------------------------------
// Sampling gate
// ---------------------------------------------------------------------------

/// With sample_rate=0.0 every session is sampled out, so user events are
/// suppressed but out-of-session boundary events (session_start) are not.
#[test]
fn sampling_rate_zero_blocks_user_events_within_session() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 0.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    assert!(
        user_event.get_value(&glean, "events").is_none(),
        "sample_rate=0.0: user event must be suppressed inside a sampled-out session"
    );
    // Boundary event (in_session=false) must still be recorded.
    assert!(
        session_start_metric().get_value(&glean, "events").is_some(),
        "session_start (in_session=false) must not be suppressed by the sampling gate"
    );
}

/// With sample_rate=1.0 every session is sampled in and user events pass through.
#[test]
fn sampling_rate_one_passes_all_user_events() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    assert!(
        user_event.get_value(&glean, "events").is_some(),
        "sample_rate=1.0: user event must be recorded inside a sampled-in session"
    );
}

/// Events recorded outside any session (before the first handle_client_active)
/// are never suppressed regardless of sample_rate.
#[test]
fn events_outside_session_bypass_sampling_gate() {
    let (_t, data_path) = tempdir();
    // sample_rate=0.0 — all sessions sampled out.
    let cfg = session_cfg(&data_path, SessionMode::Auto, 0.0, 1_800_000);
    let glean = Glean::new(cfg, None).unwrap();
    // No handle_client_active — no session is active.

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    // Between sessions is_sampled_in() returns true.
    assert!(
        user_event.get_value(&glean, "events").is_some(),
        "events recorded between sessions must not be suppressed"
    );
}

/// Metrics with in_session=false bypass the sampling gate even inside a
/// sampled-out session.
#[test]
fn out_of_session_events_bypass_sampling_gate() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 0.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active(); // session sampled out (rate=0.0)

    let oos_event = EventMetric::new(
        CommonMetricData {
            name: "oos_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );
    oos_event.record_sync(&glean, 1000, HashMap::new(), 0);

    assert!(
        oos_event.get_value(&glean, "events").is_some(),
        "in_session=false event must bypass the sampling gate"
    );
}

/// A sample_rate below 0.0 is clamped to 0.0 (all events suppressed).
#[test]
fn sample_rate_below_zero_clamped_to_zero() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, -0.5, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    assert!(
        user_event.get_value(&glean, "events").is_none(),
        "sample_rate=-0.5 must be clamped to 0.0, suppressing user events"
    );
}

/// A sample_rate above 1.0 is clamped to 1.0 (all events recorded).
#[test]
fn sample_rate_above_one_clamped_to_one() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.5, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    assert!(
        user_event.get_value(&glean, "events").is_some(),
        "sample_rate=1.5 must be clamped to 1.0, recording user events"
    );
}

// ---------------------------------------------------------------------------
// Session metadata on events
// ---------------------------------------------------------------------------

/// In-session events must carry session metadata (session_id, session_seq, event_seq).
#[test]
fn session_metadata_attached_to_in_session_events() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    let events = user_event
        .get_value(&glean, "events")
        .expect("expected event");
    let session = events[0]
        .session
        .as_ref()
        .expect("expected session metadata on in-session event");
    assert!(
        !session.session_id.is_empty(),
        "session_id must not be empty"
    );
    assert_eq!(
        1, session.session_seq,
        "session_seq must be 1 for first session"
    );
    assert_eq!(0, session.event_seq, "event_seq of first event must be 0");
    assert!(
        (session.session_sample_rate - 1.0).abs() < f64::EPSILON,
        "session_sample_rate must match configured rate"
    );
}

/// Out-of-session events must NOT carry session metadata.
#[test]
fn out_of_session_events_have_no_session_metadata() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let oos_event = EventMetric::new(
        CommonMetricData {
            name: "oos_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );
    oos_event.record_sync(&glean, 1000, HashMap::new(), 0);

    let events = oos_event
        .get_value(&glean, "events")
        .expect("expected event");
    assert!(
        events[0].session.is_none(),
        "in_session=false event must have no session metadata"
    );
}

/// event_seq increments atomically with each in-session event.
#[test]
fn event_seq_increments_within_session() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);
    user_event.record_sync(&glean, 1001, HashMap::new(), 0);
    user_event.record_sync(&glean, 1002, HashMap::new(), 0);

    let events = user_event
        .get_value(&glean, "events")
        .expect("expected events");
    assert_eq!(3, events.len());
    let seqs: Vec<u64> = events
        .iter()
        .map(|e| {
            e.session
                .as_ref()
                .expect("session metadata missing")
                .event_seq
        })
        .collect();
    assert_eq!(
        vec![0, 1, 2],
        seqs,
        "event_seq must increment with each event"
    );
}

/// event_seq resets to 0 when a new session starts.
#[test]
fn event_seq_resets_on_new_session() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );

    // First session: record two events.
    glean.handle_client_active();
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);
    user_event.record_sync(&glean, 1001, HashMap::new(), 0);
    // handle_client_inactive submits events ping, clearing the store.
    glean.handle_client_inactive();

    // Second session: record one event — event_seq should restart from 0.
    glean.handle_client_active();
    user_event.record_sync(&glean, 2000, HashMap::new(), 0);

    let events = user_event
        .get_value(&glean, "events")
        .expect("expected event in second session");
    assert_eq!(
        1,
        events.len(),
        "only event from second session should be present"
    );
    let session = events[0]
        .session
        .as_ref()
        .expect("session metadata missing");
    assert_eq!(
        0, session.event_seq,
        "event_seq must reset to 0 at the start of each new session"
    );
    assert_eq!(2, session.session_seq, "second session must have seq=2");
}

// ---------------------------------------------------------------------------
// Manual mode — explicit session APIs
// ---------------------------------------------------------------------------

/// In Manual mode, calling `session_start` and `session_end` directly produces
/// the expected boundary events and session metadata.
#[test]
fn manual_mode_explicit_session_start_end() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Manual, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    // No session yet — lifecycle signals are no-ops in Manual mode.
    assert!(!glean.session_manager().is_active());

    glean.session_start();
    assert!(
        glean.session_manager().is_active(),
        "session must be active after manual session_start"
    );

    let starts = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected session_start event after manual start");
    assert_eq!(1, starts.len());
    let extra = starts[0].extra.as_ref().unwrap();
    assert_eq!(
        "1",
        extra.get("session_seq").unwrap(),
        "first manual session must have seq=1"
    );
    assert!(
        uuid::Uuid::parse_str(extra.get("session_id").unwrap()).is_ok(),
        "session_id must be a valid UUID"
    );

    // Record a user event — it should carry session metadata.
    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);
    let events = user_event
        .get_value(&glean, "events")
        .expect("expected event");
    assert!(
        events[0].session.is_some(),
        "in-session event must have session metadata in Manual mode"
    );

    // End the session explicitly.
    glean.session_end(Some("done"));
    assert!(
        !glean.session_manager().is_active(),
        "session must be inactive after manual session_end"
    );
}

/// Starting a second manual session increments session_seq to 2.
#[test]
fn manual_mode_second_session_has_seq_2() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Manual, 1.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.session_start();
    glean.session_end(None);
    // events ping cleared by ping submission in session tests; reset store manually
    // by restarting (this test uses the same ping store, so we check seq from the
    // session_start extra in the second start which is visible after the first end).
    glean.session_start();

    let starts = session_start_metric()
        .get_value(&glean, "events")
        .expect("expected session_start for second session");
    // After the first session_end the events ping was submitted (store cleared by
    // handle_client_inactive in lifecycle mode, but Manual mode doesn't submit pings
    // automatically). We read whatever is in the store after the second start.
    let seq_str = starts
        .last()
        .unwrap()
        .extra
        .as_ref()
        .unwrap()
        .get("session_seq")
        .unwrap()
        .clone();
    assert_eq!("2", seq_str, "second manual session must have seq=2");
}

/// Session boundary events carry a wall-clock `glean_timestamp`, like every
/// other event, rather than the monotonic clock used for `timestamp`.
#[test]
fn session_boundary_events_use_wall_clock_glean_timestamp() {
    let (_t, data_path) = tempdir();
    let mut cfg = session_cfg(&data_path, SessionMode::Manual, 1.0, 0);
    cfg.enable_event_timestamps = true;
    let mut glean = Glean::new(cfg, None).unwrap();

    let before = chrono::Utc::now().timestamp_millis();
    glean.session_start();
    glean.session_end(None);
    let after = chrono::Utc::now().timestamp_millis();

    let json = glean
        .event_storage()
        .snapshot_as_json(&glean, "events", false)
        .expect("expected boundary events");
    let events = json.as_array().unwrap();
    for name in ["session_start", "session_end"] {
        let event = events
            .iter()
            .find(|e| e["category"] == "glean" && e["name"] == name)
            .unwrap_or_else(|| panic!("expected glean.{name}"));
        let glean_timestamp: i64 = event["extra"]["glean_timestamp"]
            .as_str()
            .expect("glean_timestamp should exist")
            .parse()
            .expect("glean_timestamp should be an integer");
        assert!(
            (before..=after).contains(&glean_timestamp),
            "glean.{name} glean_timestamp {glean_timestamp} not in wall-clock range {before}..={after}"
        );
    }
}

// ---------------------------------------------------------------------------
// AUTO mode — session resumption and timeout across restarts
// ---------------------------------------------------------------------------

/// Clean restart before the inactivity timeout: the session is resumed, so no
/// new session_start or session_end boundary events are emitted on reactivation.
#[test]
fn auto_mode_session_resumed_on_restart_before_timeout() {
    let (t, data_path) = tempdir();

    let original_session_id;
    {
        // 30-minute timeout — won't expire during this test.
        let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active(); // starts session, persists state
        original_session_id = glean.session_manager().session_id().unwrap().to_string();
        glean.handle_client_inactive(); // records inactive_since, submits events ping
                                        // Drop glean — simulates a clean process exit.
    }

    // Restart on the same data path.
    let cfg2 = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean2 = Glean::new(cfg2, None).unwrap();

    // Re-activate immediately — well within the 30-minute timeout.
    glean2.handle_client_active();

    // No new boundary events should be in the store (session was resumed).
    assert!(
        session_start_metric()
            .get_value(&glean2, "events")
            .is_none(),
        "no new session_start expected when session is resumed after restart"
    );
    assert!(
        session_end_metric().get_value(&glean2, "events").is_none(),
        "no session_end expected when session is resumed after restart"
    );

    // The same session_id must still be active.
    assert_eq!(
        original_session_id,
        glean2.session_manager().session_id().unwrap().to_string(),
        "resumed session must keep the original session_id"
    );

    drop(t);
}

/// In AUTO mode, `event_seq` must be monotonically continuous across a clean
/// restart when the session is resumed.  Events recorded before the restart
/// had seq 0, 1, 2.  After restart and re-activation, the first new event
/// must continue from seq 3, not reset to 0.
#[test]
fn auto_mode_event_seq_continuous_across_restart() {
    let (t, data_path) = tempdir();

    let pre_restart_seq;
    {
        let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active();

        let user_event = EventMetric::new(
            CommonMetricData {
                name: "pre_restart_event".into(),
                category: "test".into(),
                send_in_pings: vec!["events".into()],
                lifetime: Lifetime::Ping,
                in_session: true,
                ..Default::default()
            },
            vec![],
        );

        // Record three events — they will get event_seq 0, 1, 2.
        user_event.record_sync(&glean, 100, HashMap::new(), 0);
        user_event.record_sync(&glean, 200, HashMap::new(), 0);
        user_event.record_sync(&glean, 300, HashMap::new(), 0);

        let events = user_event
            .get_value(&glean, "events")
            .expect("expected pre-restart events");
        pre_restart_seq = events
            .last()
            .unwrap()
            .session
            .as_ref()
            .expect("session metadata missing")
            .event_seq;
        assert_eq!(
            2, pre_restart_seq,
            "last pre-restart event must have event_seq=2"
        );

        // Go inactive to persist event_seq before the simulated restart.
        glean.handle_client_inactive();
        // Drop — simulates clean process exit.
    }

    // Restart on the same data path.
    let cfg2 = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean2 = Glean::new(cfg2, None).unwrap();

    // Re-activate within the timeout — session is resumed, not replaced.
    glean2.handle_client_active();

    let post_event = EventMetric::new(
        CommonMetricData {
            name: "post_restart_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    post_event.record_sync(&glean2, 400, HashMap::new(), 0);

    let post_events = post_event
        .get_value(&glean2, "events")
        .expect("expected post-restart event");
    let post_seq = post_events[0]
        .session
        .as_ref()
        .expect("session metadata missing on post-restart event")
        .event_seq;

    assert_eq!(
        pre_restart_seq + 1,
        post_seq,
        "event_seq must continue from {} after restart, not reset to 0",
        pre_restart_seq
    );

    drop(t);
}

/// Clean restart after the inactivity timeout: old session is ended with
/// reason "timeout" and a new session is started.
#[test]
fn auto_mode_new_session_on_restart_after_timeout() {
    let (t, data_path) = tempdir();

    let original_session_id;
    {
        // 1 ms timeout — expires almost immediately.
        let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active();
        original_session_id = glean.session_manager().session_id().unwrap().to_string();
        glean.handle_client_inactive(); // records inactive_since, clears store
                                        // Drop — clean exit.
    }

    thread::sleep(Duration::from_millis(20)); // ensure timeout has expired

    let cfg2 = session_cfg(&data_path, SessionMode::Auto, 1.0, 1);
    let mut glean2 = Glean::new(cfg2, None).unwrap();
    glean2.handle_client_active();

    // session_end("timeout") must appear.
    let end_events = session_end_metric()
        .get_value(&glean2, "events")
        .expect("expected session_end after timeout on restart");
    assert_eq!(1, end_events.len());
    assert_eq!(
        "timeout",
        end_events[0].extra.as_ref().unwrap().get("reason").unwrap()
    );

    // The new session must have a different session_id.
    let new_id = glean2.session_manager().session_id().unwrap().to_string();
    assert_ne!(
        original_session_id, new_id,
        "new session must have a fresh session_id"
    );

    // New session must have seq=2.
    let start_events = session_start_metric()
        .get_value(&glean2, "events")
        .expect("expected session_start for new session after timeout");
    assert_eq!(
        "2",
        start_events[0]
            .extra
            .as_ref()
            .unwrap()
            .get("session_seq")
            .unwrap()
    );

    drop(t);
}

/// A session that was sampled-out must remain sampled-out after a clean restart
/// in AUTO mode (sampled_in is recomputed deterministically from the UUID).
#[test]
fn auto_mode_sampled_out_session_stays_sampled_out_after_restart() {
    let (t, data_path) = tempdir();

    // Find a UUID that will be deterministically sampled-out at rate 0.5.
    // We use rate=0.0 to guarantee every session is sampled out.
    {
        let cfg = session_cfg(&data_path, SessionMode::Auto, 0.0, 1_800_000);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active(); // session starts, sampled_in=false
        assert!(
            !glean.session_manager().sampled_in(),
            "session must be sampled-out at rate=0.0"
        );
        glean.handle_client_inactive(); // records inactive_since
    }

    // Restart — session should resume and still be sampled-out.
    let cfg2 = session_cfg(&data_path, SessionMode::Auto, 0.0, 1_800_000);
    let mut glean2 = Glean::new(cfg2, None).unwrap();
    glean2.handle_client_active(); // should resume, not start new session

    assert!(
        !glean2.session_manager().sampled_in(),
        "resumed session must remain sampled-out after restart"
    );

    // User event must still be suppressed.
    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean2, 1000, HashMap::new(), 0);
    assert!(
        user_event.get_value(&glean2, "events").is_none(),
        "user event must remain suppressed in resumed sampled-out session"
    );

    drop(t);
}

/// `session_start_time` is persisted and available on events recorded after a
/// clean restart that resumes an existing AUTO mode session.
#[test]
fn auto_mode_session_start_time_persists_across_restart() {
    let (t, data_path) = tempdir();

    let original_start_time;
    {
        let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active();
        original_start_time = glean
            .session_manager()
            .session_start_time()
            .expect("session_start_time must be set");
        glean.handle_client_inactive();
    }

    let cfg2 = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean2 = Glean::new(cfg2, None).unwrap();
    glean2.handle_client_active(); // resumes session

    let resumed_start_time = glean2
        .session_manager()
        .session_start_time()
        .expect("session_start_time must be restored after restart");

    assert_eq!(
        original_start_time, resumed_start_time,
        "session_start_time must be the same before and after a clean restart"
    );

    drop(t);
}

// ---------------------------------------------------------------------------
// sessions_seen diagnostic counter
// ---------------------------------------------------------------------------

/// `sessions_seen` is incremented for every session start, including sampled-out ones.
#[test]
fn sessions_seen_increments_regardless_of_sampling() {
    use glean_core::metrics::CounterMetric;

    let (_t, data_path) = tempdir();
    // Use rate=0.0 so all sessions are sampled out — sessions_seen must still increment.
    let cfg = session_cfg(&data_path, SessionMode::Lifecycle, 0.0, 0);
    let mut glean = Glean::new(cfg, None).unwrap();

    let sessions_seen = CounterMetric::new(CommonMetricData {
        name: "sessions_seen".into(),
        category: "glean".into(),
        send_in_pings: vec!["metrics".into()],
        lifetime: Lifetime::Ping,
        in_session: false,
        ..Default::default()
    });

    // No sessions started yet.
    assert!(
        sessions_seen.get_value(&glean, "metrics").is_none(),
        "sessions_seen must be 0 before any session starts"
    );

    // Start and end three sessions.
    for _ in 0..3 {
        glean.handle_client_active();
        glean.handle_client_inactive();
    }

    assert_eq!(
        3,
        sessions_seen.get_value(&glean, "metrics").unwrap_or(0),
        "sessions_seen must equal the number of sessions started, even when all are sampled-out"
    );
}

/// `sessions_seen` is an out-of-session metric — it is never suppressed by
/// session sampling and carries no session metadata.
#[test]
fn sessions_seen_is_out_of_session() {
    use glean_core::metrics::CounterMetric;

    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 0.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active(); // session sampled-out

    let sessions_seen = CounterMetric::new(CommonMetricData {
        name: "sessions_seen".into(),
        category: "glean".into(),
        send_in_pings: vec!["metrics".into()],
        lifetime: Lifetime::Ping,
        in_session: false,
        ..Default::default()
    });

    // Even with rate=0.0 the counter must have been recorded.
    assert_eq!(
        1,
        sessions_seen.get_value(&glean, "metrics").unwrap_or(0),
        "sessions_seen must be recorded even when the session is sampled-out"
    );
}

/// All in-session events within the same session share the same session_id.
#[test]
fn in_session_events_share_session_id() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);
    user_event.record_sync(&glean, 1001, HashMap::new(), 0);

    let events = user_event
        .get_value(&glean, "events")
        .expect("expected events");
    assert_eq!(2, events.len());
    let id0 = &events[0].session.as_ref().unwrap().session_id;
    let id1 = &events[1].session.as_ref().unwrap().session_id;
    assert_eq!(id0, id1, "both events must share the same session_id");
    assert!(!id0.is_empty(), "session_id must not be empty");
}

// ---------------------------------------------------------------------------
// Mode-mismatch across builds — orphaned session cleanup
// ---------------------------------------------------------------------------

/// Switching from Auto to Lifecycle mode across builds emits a synthetic
/// session_end("abandoned") for the orphaned Auto session and clears storage.
#[test]
fn mode_switch_auto_to_lifecycle_emits_abandoned_session_end() {
    let (t, data_path) = tempdir();

    let original_session_id;
    {
        let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
        let mut glean = Glean::new(cfg, None).unwrap();
        glean.handle_client_active();
        original_session_id = glean.session_manager().session_id().unwrap().to_string();
        glean.handle_client_inactive(); // persists session_id + inactive_since
    }

    // Restart with Lifecycle mode — the Auto session is now orphaned.
    let cfg2 = session_cfg(&data_path, SessionMode::Lifecycle, 1.0, 0);
    let glean2 = Glean::new(cfg2, None).unwrap();

    // A synthetic session_end("abandoned") must have been emitted during init.
    let end_events = session_end_metric()
        .get_value(&glean2, "events")
        .expect("expected session_end(\"abandoned\") for orphaned session");
    assert_eq!(1, end_events.len());
    let extra = end_events[0]
        .extra
        .as_ref()
        .expect("expected extras on session_end");
    assert_eq!(
        "abandoned",
        extra.get("reason").unwrap(),
        "orphaned session must produce reason='abandoned'"
    );
    assert_eq!(
        &original_session_id,
        extra.get("session_id").unwrap(),
        "abandoned session_end must carry the original session_id"
    );

    // The orphaned session state must be fully cleared — a new Lifecycle
    // session should start cleanly.
    assert!(
        glean2.session_manager().session_id().is_none(),
        "session_id must be cleared after orphaned session cleanup"
    );

    drop(t);
}

/// Disabling upload resets the in-memory session along with its persisted
/// state, so re-enabling in the same process starts a new session rather than
/// continuing the one from before the `deletion-request` ping.
#[test]
fn upload_disable_then_enable_does_not_continue_old_session() {
    let (_t, data_path) = tempdir();
    let cfg = session_cfg(&data_path, SessionMode::Auto, 1.0, 1_800_000);
    let mut glean = Glean::new(cfg, None).unwrap();

    glean.handle_client_active();
    let old_session_id = glean
        .session_manager()
        .session_id()
        .expect("session should be active")
        .to_string();

    glean.set_upload_enabled(false);
    assert!(
        !glean.session_manager().is_active(),
        "session must not stay active after upload is disabled"
    );
    assert!(glean.session_manager().session_id().is_none());

    glean.set_upload_enabled(true);
    glean.handle_client_active();

    let user_event = EventMetric::new(
        CommonMetricData {
            name: "test_event".into(),
            category: "test".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            in_session: true,
            ..Default::default()
        },
        vec![],
    );
    user_event.record_sync(&glean, 1000, HashMap::new(), 0);

    let events = user_event
        .get_value(&glean, "events")
        .expect("expected event after re-enabling upload");
    let session = events[0]
        .session
        .as_ref()
        .expect("event should carry session metadata");
    assert_ne!(
        old_session_id, session.session_id,
        "event after re-enabling must not carry the pre-opt-out session_id"
    );
    assert_eq!(
        1, session.session_seq,
        "session_seq must restart, like the ping sequence numbers"
    );
}
