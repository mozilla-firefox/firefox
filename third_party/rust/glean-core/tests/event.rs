// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;
use crate::common::*;

use std::collections::HashMap;
use std::fs;

use serde_json::json;

use glean_core::{
    get_timestamp_ms, test_get_num_recorded_errors, CommonMetricData, ErrorType, Lifetime,
};
use glean_core::{metrics::*, Glean};

#[test]
fn record_properly_records_without_optional_arguments() {
    let store_names = vec!["store1".into(), "store2".into()];

    let (glean, _t) = new_glean(None);

    let metric = EventMetric::new(
        CommonMetricData {
            name: "test_event_no_optional".into(),
            category: "telemetry".into(),
            send_in_pings: store_names.clone(),
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    metric.record_sync(&glean, 1000, HashMap::new(), 0);

    for store_name in store_names {
        let events = metric.get_value(&glean, &*store_name).unwrap();
        assert_eq!(1, events.len());
        assert_eq!("telemetry", events[0].category);
        assert_eq!("test_event_no_optional", events[0].name);
        assert!(events[0].extra.is_none());
    }
}

#[test]
fn record_properly_records_with_optional_arguments() {
    let (glean, _t) = new_glean(None);

    let store_names = vec!["store1".into(), "store2".into()];

    let metric = EventMetric::new(
        CommonMetricData {
            name: "test_event_no_optional".into(),
            category: "telemetry".into(),
            send_in_pings: store_names.clone(),
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["key1".into(), "key2".into()],
    );

    let extra = [
        ("key1".into(), "value1".into()),
        ("key2".into(), "value2".into()),
    ]
    .iter()
    .cloned()
    .collect();

    metric.record_sync(&glean, 1000, extra, 0);

    for store_name in store_names {
        let events = metric.get_value(&glean, &*store_name).unwrap();
        let event = events[0].clone();
        assert_eq!(1, events.len());
        assert_eq!("telemetry", event.category);
        assert_eq!("test_event_no_optional", event.name);
        let extra = event.extra.unwrap();
        assert_eq!(2, extra.len());
        assert_eq!("value1", extra["key1"]);
        assert_eq!("value2", extra["key2"]);
    }
}

// SKIPPED record() computes the correct time between events
// Timing is now handled in the language-specific part.

#[test]
fn snapshot_returns_none_if_nothing_is_recorded_in_the_store() {
    let (glean, _t) = new_glean(None);

    assert!(glean
        .event_storage()
        .snapshot_as_json(&glean, "store1", false)
        .is_none())
}

#[test]
fn snapshot_correctly_clears_the_stores() {
    let (glean, _t) = new_glean(None);

    let store_names = vec!["store1".into(), "store2".into()];

    let metric = EventMetric::new(
        CommonMetricData {
            name: "test_event_clear".into(),
            category: "telemetry".into(),
            send_in_pings: store_names,
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    metric.record_sync(&glean, 1000, HashMap::new(), 0);

    let snapshot = glean
        .event_storage()
        .snapshot_as_json(&glean, "store1", true);
    assert!(snapshot.is_some());

    assert!(glean
        .event_storage()
        .snapshot_as_json(&glean, "store1", false)
        .is_none());

    let files: Vec<fs::DirEntry> = fs::read_dir(&glean.event_storage().path)
        .unwrap()
        .filter_map(|x| x.ok())
        .collect();
    assert_eq!(1, files.len());
    assert_eq!("store2", files[0].file_name());

    let snapshot2 = glean
        .event_storage()
        .snapshot_as_json(&glean, "store2", false);
    for s in [snapshot, snapshot2] {
        assert!(s.is_some());
        let s = s.unwrap();
        assert_eq!(1, s.as_array().unwrap().len());
        assert_eq!("telemetry", s[0]["category"]);
        assert_eq!("test_event_clear", s[0]["name"]);
        assert!(s[0].get("extra").is_none());
    }
}

// SKIPPED: Events are serialized in the correct JSON format (no extra)
// SKIPPED: Events are serialized in the correct JSON format (with extra)
// This test won't work as-is since Rust doesn't maintain the insertion order in
// a JSON object, therefore you can't check the JSON output directly against a
// string.  This check is redundant with other tests, anyway, and checking against
// the schema is much more useful.

#[test]
fn test_sending_of_event_ping_when_it_fills_up() {
    let (mut glean, _t) = new_glean(None);

    let store_names: Vec<String> = vec!["events".into()];

    for store_name in &store_names {
        glean.register_ping_type(
            &PingBuilder::new(store_name)
                .with_reasons(vec!["max_capacity".to_string()])
                .build(),
        );
    }

    let click = EventMetric::new(
        CommonMetricData {
            name: "click".into(),
            category: "ui".into(),
            send_in_pings: store_names,
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["test_event_number".into()],
    );

    // We send 510 events. We expect to get the first 500 in the ping and 10
    // remaining afterward
    for i in 0..510 {
        let mut extra = HashMap::new();
        extra.insert("test_event_number".to_string(), i.to_string());
        click.record_sync(&glean, i, extra, 0);
    }

    assert_eq!(10, click.get_value(&glean, "events").unwrap().len());

    let (url, json, _) = &get_queued_pings(glean.get_data_path()).unwrap()[0];
    assert!(url.starts_with(format!("/submit/{}/events/", glean.get_application_id()).as_str()));
    assert_eq!(500, json["events"].as_array().unwrap().len());
    assert_eq!(
        "max_capacity",
        json["ping_info"].as_object().unwrap()["reason"]
            .as_str()
            .unwrap()
    );

    for i in 0..500 {
        let event = &json["events"].as_array().unwrap()[i];
        assert_eq!(i.to_string(), event["extra"]["test_event_number"]);
    }

    let snapshot = glean
        .event_storage()
        .snapshot_as_json(&glean, "events", false)
        .unwrap();
    assert_eq!(10, snapshot.as_array().unwrap().len());
    for i in 0..10 {
        let event = &snapshot.as_array().unwrap()[i];
        assert_eq!((i + 500).to_string(), event["extra"]["test_event_number"]);
    }
}

#[test]
fn test_server_knobs_config_changing_max_events() {
    let (mut glean, _t) = new_glean(None);

    let store_names: Vec<String> = vec!["events".into()];

    for store_name in &store_names {
        glean.register_ping_type(
            &PingBuilder::new(store_name)
                .with_reasons(vec!["max_capacity".to_string()])
                .build(),
        );
    }

    // 1. Set up an event to record
    let click = EventMetric::new(
        CommonMetricData {
            name: "click".into(),
            category: "ui".into(),
            send_in_pings: store_names,
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["test_event_number".into()],
    );

    // 2. Set a Server Knobs configuration to disable the metrics
    let remote_settings_config = json!(
        {
            "event_threshold": 50
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 3. Record 51 events. We expect to get the first 50 in the first ping and 1
    // remaining afterward
    for i in 0..51 {
        let mut extra = HashMap::new();
        extra.insert("test_event_number".to_string(), i.to_string());
        click.record_sync(&glean, i, extra, 0);
    }

    assert_eq!(1, click.get_value(&glean, "events").unwrap().len());

    let (url, json, _) = &get_queued_pings(glean.get_data_path()).unwrap()[0];
    assert!(url.starts_with(format!("/submit/{}/events/", glean.get_application_id()).as_str()));
    assert_eq!(50, json["events"].as_array().unwrap().len());
    assert_eq!(
        "max_capacity",
        json["ping_info"].as_object().unwrap()["reason"]
            .as_str()
            .unwrap()
    );

    for i in 0..50 {
        let event = &json["events"].as_array().unwrap()[i];
        assert_eq!(i.to_string(), event["extra"]["test_event_number"]);
    }

    let snapshot = glean
        .event_storage()
        .snapshot_as_json(&glean, "events", false)
        .unwrap();
    assert_eq!(1, snapshot.as_array().unwrap().len());
    let event = &snapshot.as_array().unwrap()[0];
    assert_eq!(50.to_string(), event["extra"]["test_event_number"]);
}

#[test]
fn extra_keys_must_be_recorded_and_truncated_if_needed() {
    let (glean, _t) = new_glean(None);

    let store_names: Vec<String> = vec!["store1".into()];

    let test_event = EventMetric::new(
        CommonMetricData {
            name: "testEvent".into(),
            category: "ui".into(),
            send_in_pings: store_names,
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["extra1".into(), "truncatedExtra".into()],
    );

    let test_value = "LeanGleanByFrank";
    let test_value_long = test_value.to_string().repeat(32);
    // max length for extra values.
    let test_value_cap = 500;
    assert!(
        test_value_long.len() > test_value_cap,
        "test value is not long enough"
    );
    let mut extra = HashMap::new();
    extra.insert("extra1".into(), test_value.to_string());
    extra.insert("truncatedExtra".into(), test_value_long.clone());

    test_event.record_sync(&glean, 0, extra, 0);

    let snapshot = glean
        .event_storage()
        .snapshot_as_json(&glean, "store1", false)
        .unwrap();
    assert_eq!(1, snapshot.as_array().unwrap().len());
    let event = &snapshot.as_array().unwrap()[0];
    assert_eq!("ui", event["category"]);
    assert_eq!("testEvent", event["name"]);
    assert_eq!(2, event["extra"].as_object().unwrap().len());
    assert_eq!(test_value, event["extra"]["extra1"]);
    assert_eq!(
        test_value_long[0..test_value_cap],
        event["extra"]["truncatedExtra"]
    );
}

#[test]
fn snapshot_sorts_the_timestamps() {
    let (glean, _t) = new_glean(None);

    let metric = EventMetric::new(
        CommonMetricData {
            name: "test_event_clear".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    metric.record_sync(&glean, 1000, HashMap::new(), 0);
    metric.record_sync(&glean, 100, HashMap::new(), 0);
    metric.record_sync(&glean, 10000, HashMap::new(), 0);

    let snapshot = glean
        .event_storage()
        .snapshot_as_json(&glean, "store1", true)
        .unwrap();

    assert_eq!(
        0,
        snapshot.as_array().unwrap()[0]["timestamp"]
            .as_i64()
            .unwrap()
    );
    assert_eq!(
        900,
        snapshot.as_array().unwrap()[1]["timestamp"]
            .as_i64()
            .unwrap()
    );
    assert_eq!(
        9900,
        snapshot.as_array().unwrap()[2]["timestamp"]
            .as_i64()
            .unwrap()
    );
}

#[test]
fn ensure_custom_ping_events_dont_overflow() {
    let (glean, _dir) = new_glean(None);

    let store_name = "store1";
    let event_meta = CommonMetricData {
        name: "name".into(),
        category: "category".into(),
        send_in_pings: vec![store_name.into()],
        lifetime: Lifetime::Ping,
        ..Default::default()
    };
    let event = EventMetric::new(event_meta.clone(), vec![]);

    assert!(test_get_num_recorded_errors(
        &glean,
        &(event_meta.clone()).into(),
        ErrorType::InvalidOverflow
    )
    .is_err());

    for _ in 0..500 {
        event.record_sync(&glean, 0, HashMap::new(), 0);
    }
    assert!(test_get_num_recorded_errors(
        &glean,
        &(event_meta.clone()).into(),
        ErrorType::InvalidOverflow
    )
    .is_err());

    // That should top us right up to the limit. Now for going over.
    event.record_sync(&glean, 0, HashMap::new(), 0);
    assert!(
        test_get_num_recorded_errors(&glean, &event_meta.into(), ErrorType::InvalidOverflow)
            .is_err()
    );
    assert_eq!(501, event.get_value(&glean, store_name).unwrap().len());
}

/// Ensure that events from multiple runs serialize properly.
///
/// Records an event once each in two separate sessions,
/// ensuring they both show (and an inserted `glean.restarted` event)
/// in the serialized result.
#[test]
fn ensure_custom_ping_events_from_multiple_runs_work() {
    let (mut tempdir, _) = tempdir();

    let store_name = "store1";
    let event = EventMetric::new(
        CommonMetricData {
            name: "name".into(),
            category: "category".into(),
            send_in_pings: vec![store_name.into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    {
        let (glean, dir) = new_glean(Some(tempdir));
        // We don't need a full init. Just to deal with on-disk events:
        assert!(!glean
            .event_storage()
            .flush_pending_events_on_startup(&glean, false));
        tempdir = dir;

        event.record_sync(&glean, 10, HashMap::new(), 0);
    }

    {
        let (glean, _dir) = new_glean(Some(tempdir));
        // We don't need a full init. Just to deal with on-disk events:
        assert!(!glean
            .event_storage()
            .flush_pending_events_on_startup(&glean, false));

        // Gotta use get_timestamp_ms or this event won't happen "after" the injected
        // glean.restarted event from `flush_pending_events_on_startup`.
        event.record_sync(&glean, get_timestamp_ms(), HashMap::new(), 0);

        let json = glean
            .event_storage()
            .snapshot_as_json(&glean, store_name, false)
            .unwrap();
        assert_eq!(json.as_array().unwrap().len(), 3);
        assert_eq!(json[0]["category"], "category");
        assert_eq!(json[0]["name"], "name");
        assert_eq!(json[1]["category"], "glean");
        assert_eq!(json[1]["name"], "restarted");
        assert_eq!(json[2]["category"], "category");
        assert_eq!(json[2]["name"], "name");
    }
}

/// Ensure events in an unregistered, non-"events" (ie Custom) ping are trimmed on a subsequent init
/// when we pass `true ` for `trim_data_to_registered_pings` in `on_ready_to_submit_pings`.
#[test]
fn event_storage_trimming() {
    let (mut tempdir, _) = tempdir();

    let store_name = "store-name";
    let store_name_2 = "store-name-2";
    let event = EventMetric::new(
        CommonMetricData {
            name: "name".into(),
            category: "category".into(),
            send_in_pings: vec![store_name.into(), store_name_2.into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    let new_ping = |glean: &mut Glean, ping: &str| {
        // In Rust, pings are registered via construction.
        // But that's done asynchronously, so we do it synchronously here:
        glean.register_ping_type(&PingBuilder::new(ping).build());
    };

    // First, register both pings, so that we can record the event in the two pings.
    {
        let (mut glean, dir) = new_glean(Some(tempdir));
        tempdir = dir;

        new_ping(&mut glean, store_name);
        new_ping(&mut glean, store_name_2);

        event.record_sync(&glean, 10, HashMap::new(), 0);

        assert_eq!(1, event.get_value(&glean, store_name).unwrap().len());
        assert_eq!(1, event.get_value(&glean, store_name_2).unwrap().len());
    }
    // Second, construct (but don't init) Glean again.
    // Register exactly one of the two pings.
    // Then process the part of init that does the trimming (`on_ready_to_submit_pings`).
    {
        let (mut glean, _dir) = new_glean(Some(tempdir));
        new_ping(&mut glean, store_name);

        glean.on_ready_to_submit_pings(true);

        assert_eq!(1, event.get_value(&glean, store_name).unwrap().len());
        assert!(event.get_value(&glean, store_name_2).is_none());
    }
}

#[test]
fn with_event_timestamps() {
    use glean_core::{Glean, InternalConfiguration};

    let dir = tempfile::tempdir().unwrap();
    let cfg = InternalConfiguration {
        data_path: dir.path().display().to_string(),
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
        enable_event_timestamps: true,
        experimentation_id: None, // Enabling event timestamps
        enable_internal_pings: true,
        ping_schedule: Default::default(),
        ping_lifetime_threshold: 0,
        ping_lifetime_max_time: 0,
        max_pending_pings_count: None,
        max_pending_pings_directory_size: None,
        session_mode: glean_core::SessionMode::Auto,
        session_sample_rate: 1.0,
        session_inactivity_timeout_ms: 1_800_000,
        events_ping_acceleration_factor: None,
        enable_store_submitted_pings: false,
    };
    let mut glean = Glean::new(cfg, None).unwrap();
    let ping = PingBuilder::new("store1").build();
    glean.register_ping_type(&ping);

    let store_name = "store1";
    let event = EventMetric::new(
        CommonMetricData {
            name: "name".into(),
            category: "category".into(),
            send_in_pings: vec![store_name.into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    event.record_sync(&glean, get_timestamp_ms(), HashMap::new(), 12345);

    let json = glean
        .event_storage()
        .snapshot_as_json(&glean, store_name, false)
        .unwrap();
    assert_eq!(json.as_array().unwrap().len(), 1);
    assert_eq!(json[0]["category"], "category");
    assert_eq!(json[0]["name"], "name");

    let glean_timestamp = json[0]["extra"]["glean_timestamp"]
        .as_str()
        .expect("glean_timestamp should exist");
    let glean_timestamp: i64 = glean_timestamp
        .parse()
        .expect("glean_timestamp should be an integer");
    assert_eq!(12345, glean_timestamp);
}

#[test]
fn doesnt_crash_on_inaccessible_event_store_file() {
    let store_names = vec!["store1".into(), "store2".into()];

    let (glean, t) = new_glean(None);

    // put a directory in the way of the event storage file
    let event_path = t.path().join("events").join("store1");
    fs::create_dir_all(&event_path).unwrap();

    let metric = EventMetric::new(
        CommonMetricData {
            name: "test_event_no_optional".into(),
            category: "telemetry".into(),
            send_in_pings: store_names.clone(),
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    metric.record_sync(&glean, 1000, HashMap::new(), 0);

    for store_name in store_names {
        let events = metric.get_value(&glean, &*store_name).unwrap();
        assert_eq!(1, events.len());
        assert_eq!("telemetry", events[0].category);
        assert_eq!("test_event_no_optional", events[0].name);
        assert!(events[0].extra.is_none());
    }
}

#[test]
fn test_server_knobs_config_events_factor() {
    let (mut glean, _t) = new_glean(None);

    let store_names: Vec<String> = vec!["events".into()];

    glean.register_ping_type(
        &PingBuilder::new(&store_names[0])
            .with_reasons(vec!["max_capacity".to_string()])
            .build(),
    );

    // 1. Set up an event to record
    let click = EventMetric::new(
        CommonMetricData {
            name: "click".into(),
            category: "ui".into(),
            send_in_pings: store_names,
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["test_event_number".into()],
    );

    // 2. Set a Server Knobs configuration to accelerate the ping
    let remote_settings_config = json!(
        {
            "events_ping_acceleration_factor": 5
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 3. Record 21 events. We expect to get the first 20 in the first ping and 1
    // remaining afterward
    for i in 0..21 {
        let mut extra = HashMap::new();
        extra.insert("test_event_number".to_string(), i.to_string());
        click.record_sync(&glean, i, extra, 0);
    }

    assert_eq!(1, click.get_value(&glean, "events").unwrap().len());

    let (url, json, _) = &get_queued_pings(glean.get_data_path()).unwrap()[0];
    assert!(url.starts_with(format!("/submit/{}/events/", glean.get_application_id()).as_str()));
    assert_eq!(20, json["events"].as_array().unwrap().len());
    assert_eq!(
        "max_capacity",
        json["ping_info"].as_object().unwrap()["reason"]
            .as_str()
            .unwrap()
    );

    for i in 0..20 {
        let event = &json["events"].as_array().unwrap()[i];
        assert_eq!(i.to_string(), event["extra"]["test_event_number"]);
    }

    let snapshot = glean
        .event_storage()
        .snapshot_as_json(&glean, "events", false)
        .unwrap();
    assert_eq!(1, snapshot.as_array().unwrap().len());
    let event = &snapshot.as_array().unwrap()[0];
    assert_eq!(20.to_string(), event["extra"]["test_event_number"]);
}

#[test]
fn test_server_knobs_config_events_factor_decrease() {
    let (mut glean, _t) = new_glean(None);

    let store_names: Vec<String> = vec!["events".into()];

    glean.register_ping_type(
        &PingBuilder::new(&store_names[0])
            .with_reasons(vec!["max_capacity".to_string()])
            .build(),
    );

    // 1. Set up an event to record
    let click = EventMetric::new(
        CommonMetricData {
            name: "click".into(),
            category: "ui".into(),
            send_in_pings: store_names,
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["test_event_number".into()],
    );

    // 2. Record 21 events. We expect no "events" ping to yet be submitted (threshold's 500).
    for i in 0..21 {
        let mut extra = HashMap::new();
        extra.insert("test_event_number".to_string(), i.to_string());
        click.record_sync(&glean, i, extra, 0);
    }

    assert_eq!(21, click.get_value(&glean, "events").unwrap().len());

    // 3. Set a Server Knobs configuration to accelerate the ping
    let remote_settings_config = json!(
        {
            "events_ping_acceleration_factor": 5
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 4. We expect the ping to still not have been sent, even though the threshold's now 20.
    // (Maybe in the future this will change.)
    assert_eq!(21, click.get_value(&glean, "events").unwrap().len());

    // 5. We record one more event (total: 22). A ping is submitted. All events are on it.
    let mut extra = HashMap::new();
    extra.insert("test_event_number".to_string(), 21.to_string());
    click.record_sync(&glean, 21, extra, 0);
    assert_eq!(None, click.get_value(&glean, "events"));

    let (url, json, _) = &get_queued_pings(glean.get_data_path()).unwrap()[0];
    assert!(url.starts_with(format!("/submit/{}/events/", glean.get_application_id()).as_str()));
    assert_eq!(22, json["events"].as_array().unwrap().len());
    assert_eq!(
        "max_capacity",
        json["ping_info"].as_object().unwrap()["reason"]
            .as_str()
            .unwrap()
    );

    for i in 0..22 {
        let event = &json["events"].as_array().unwrap()[i];
        assert_eq!(i.to_string(), event["extra"]["test_event_number"]);
    }
}

#[test]
fn events_factor_discounts_startup_ping() {
    let (mut tempdir, _) = tempdir();

    let store_name = "events";
    let event = EventMetric::new(
        CommonMetricData {
            name: "name".into(),
            category: "category".into(),
            send_in_pings: vec![store_name.into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec!["test_event_number".into()],
    );

    {
        let (glean, dir) = new_glean(Some(tempdir));
        // We don't need a full init. Just to deal with on-disk events:
        assert!(!glean
            .event_storage()
            .flush_pending_events_on_startup(&glean, false));
        tempdir = dir;

        event.record_sync(&glean, 10, HashMap::new(), 0);
    }

    {
        let (glean, _dir) = new_glean(Some(tempdir));
        // Set a Server Knobs configuration to accelerate pings
        let remote_settings_config = json!(
            {
                "events_ping_acceleration_factor": 5
            }
        )
        .to_string();
        glean.apply_server_knobs_config(
            RemoteSettingsConfig::try_from(remote_settings_config).unwrap(),
        );

        // Ensure we flush the last session's event.
        assert!(glean
            .event_storage()
            .flush_pending_events_on_startup(&glean, false));

        // Record 21 events. We expect an "events" ping to be submitted at 20, with 1 left over.
        for i in 0..21 {
            let mut extra = HashMap::new();
            extra.insert("test_event_number".to_string(), i.to_string());
            event.record_sync(&glean, i, extra, 0);
        }
        assert_eq!(1, event.get_value(&glean, "events").unwrap().len());

        let pings = get_queued_pings(glean.get_data_path()).unwrap();
        assert!(
            pings.len() >= 2,
            "We expect at least the two 'events' pings"
        );
        let pattern = format!("/submit/{}/events/", glean.get_application_id());
        let events_pings: Vec<(String, JsonValue, _)> = pings
            .into_iter()
            .filter(|p| p.0.starts_with(&pattern))
            .collect();
        assert_eq!(events_pings.len(), 2);

        let startup_events_ping = events_pings
            .iter()
            .filter(|p| {
                p.1["ping_info"].as_object().unwrap()["reason"]
                    .as_str()
                    .unwrap()
                    == "startup"
            })
            .collect::<Vec<_>>()[0];
        assert_eq!(1, startup_events_ping.1["events"].as_array().unwrap().len());

        let accelerated_events_ping = events_pings
            .iter()
            .filter(|p| {
                p.1["ping_info"].as_object().unwrap()["reason"]
                    .as_str()
                    .unwrap()
                    == "max_capacity"
            })
            .collect::<Vec<_>>()[0];
        assert_eq!(
            20,
            accelerated_events_ping.1["events"]
                .as_array()
                .unwrap()
                .len()
        );
        for i in 0..20 {
            let event = &accelerated_events_ping.1["events"].as_array().unwrap()[i];
            assert_eq!(i.to_string(), event["extra"]["test_event_number"]);
        }

        let snapshot = glean
            .event_storage()
            .snapshot_as_json(&glean, "events", false)
            .unwrap();
        assert_eq!(1, snapshot.as_array().unwrap().len());
        let event = &snapshot.as_array().unwrap()[0];
        assert_eq!(20.to_string(), event["extra"]["test_event_number"]);
    }
}

#[test]
fn storage_is_cleared_when_ping_is_disabled() {
    let (mut glean, dir) = new_glean(None);

    let ping_name = "custom-events";
    let ping = new_test_ping(&mut glean, ping_name);
    let event = EventMetric::new(
        CommonMetricData {
            name: "name".into(),
            category: "category".into(),
            send_in_pings: vec![ping_name.into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        vec![],
    );

    event.record_sync(&glean, 10, HashMap::new(), 0);

    // The event is stored on disk now
    let event_store = dir.path().join("events").join(ping_name);
    assert!(event_store.exists());

    // After disabling the ping, the on-disk file is gone
    glean.set_ping_enabled(&ping, false);
    assert!(!event_store.exists());

    assert_eq!(None, event.get_value(&glean, None));

    // After re-enabling no data file created until recording
    glean.set_ping_enabled(&ping, true);
    assert!(!event_store.exists());

    event.record_sync(&glean, 20, HashMap::new(), 0);
    assert!(event_store.exists());

    let records = event.get_value(&glean, None).unwrap();
    assert_eq!(1, records.len());
    assert_eq!(20, records[0].timestamp);
}
