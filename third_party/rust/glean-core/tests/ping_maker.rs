// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;
use crate::common::*;
use glean_core::{AttributionMetrics, DistributionMetrics};
use serde_json::json;

use glean_core::metrics::*;
use glean_core::ping::PingMaker;
use glean_core::{CommonMetricData, Glean, Lifetime};

fn set_up_basic_ping() -> (Glean, PingMaker, PingType, tempfile::TempDir) {
    let (tempdir, _) = tempdir();
    let (mut glean, t) = new_glean(Some(tempdir));
    let ping_maker = PingMaker::new();
    let ping_type = new_test_ping(&mut glean, "store1");

    // Record something, so the ping will have data
    let metric = BooleanMetric::new(CommonMetricData {
        name: "boolean_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["store1".into()],
        disabled: false,
        lifetime: Lifetime::User,
        ..Default::default()
    });
    metric.set_sync(&glean, true);

    (glean, ping_maker, ping_type, t)
}

#[test]
fn ping_info_must_contain_a_nonempty_start_and_end_time() {
    let (glean, ping_maker, ping_type, _t) = set_up_basic_ping();

    let ping = ping_maker
        .collect(&glean, &ping_type, None, "", "")
        .unwrap();
    let ping_info = ping.content["ping_info"].as_object().unwrap();

    let start_time_str = ping_info["start_time"].as_str().unwrap();
    let start_time_date = iso8601_to_chrono(&iso8601::datetime(start_time_str).unwrap());

    let end_time_str = ping_info["end_time"].as_str().unwrap();
    let end_time_date = iso8601_to_chrono(&iso8601::datetime(end_time_str).unwrap());

    assert!(start_time_date <= end_time_date);
}

#[test]
fn get_ping_info_must_report_all_the_required_fields() {
    let (glean, ping_maker, ping_type, _t) = set_up_basic_ping();

    let ping = ping_maker
        .collect(&glean, &ping_type, None, "", "")
        .unwrap();
    let ping_info = ping.content["ping_info"].as_object().unwrap();

    assert!(ping_info.get("start_time").is_some());
    assert!(ping_info.get("end_time").is_some());
    assert!(ping_info.get("seq").is_some());
}

#[test]
fn get_client_info_must_report_all_the_available_data() {
    let (glean, ping_maker, ping_type, _t) = set_up_basic_ping();

    let ping = ping_maker
        .collect(&glean, &ping_type, None, "", "")
        .unwrap();
    let client_info = ping.content["client_info"].as_object().unwrap();

    client_info["telemetry_sdk_build"].as_str().unwrap();
}

#[test]
fn test_metrics_must_report_experimentation_id() {
    let (tempdir, _) = tempdir();
    let mut glean = Glean::new(glean_core::InternalConfiguration {
        data_path: tempdir.path().display().to_string(),
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
        experimentation_id: Some("test-experimentation-id".to_string()),
        enable_internal_pings: true,
        ping_schedule: Default::default(),
        ping_lifetime_threshold: 0,
        ping_lifetime_max_time: 0,
    })
    .unwrap();
    let ping_maker = PingMaker::new();
    let ping_type = PingBuilder::new("store1").build();
    glean.register_ping_type(&ping_type);

    // Record something, so the ping will have data
    let metric = BooleanMetric::new(CommonMetricData {
        name: "boolean_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["store1".into()],
        disabled: false,
        lifetime: Lifetime::User,
        ..Default::default()
    });
    metric.set_sync(&glean, true);

    let ping = ping_maker
        .collect(&glean, &ping_type, None, "", "")
        .unwrap();
    let metrics = ping.content["metrics"].as_object().unwrap();
    println!("TLDEBUG Metrics:\n{:?}", metrics);

    let strings = metrics["string"].as_object().unwrap();
    assert_eq!(
        strings["glean.client.annotation.experimentation_id"]
            .as_str()
            .unwrap(),
        "test-experimentation-id",
        "experimentation ids must match"
    );
}

#[test]
fn experimentation_id_is_removed_if_send_if_empty_is_false() {
    // Initialize Glean with an experimentation id, it should be removed if the ping is empty
    // and send_if_empty is false.
    let (tempdir, _) = tempdir();
    let mut glean = Glean::new(glean_core::InternalConfiguration {
        data_path: tempdir.path().display().to_string(),
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
        experimentation_id: Some("test-experimentation-id".to_string()),
        enable_internal_pings: true,
        ping_schedule: Default::default(),
        ping_lifetime_threshold: 0,
        ping_lifetime_max_time: 0,
    })
    .unwrap();
    let ping_maker = PingMaker::new();

    let unknown_ping_type = PingBuilder::new("unknown").build();
    glean.register_ping_type(&unknown_ping_type);

    assert!(ping_maker
        .collect(&glean, &unknown_ping_type, None, "", "")
        .is_none());
}

#[test]
fn collect_must_report_none_when_no_data_is_stored() {
    // NOTE: This is a behavior change from glean-ac which returned an empty
    // string in this case. As this is an implementation detail and not part of
    // the public API, it's safe to change this.

    let (mut glean, ping_maker, ping_type, _t) = set_up_basic_ping();

    let unknown_ping_type = PingBuilder::new("unknown").build();
    glean.register_ping_type(&ping_type);

    assert!(ping_maker
        .collect(&glean, &unknown_ping_type, None, "", "")
        .is_none());
}

#[test]
fn seq_number_must_be_sequential() {
    let (mut glean, ping_maker, _ping_type, _t) = set_up_basic_ping();

    let metric = BooleanMetric::new(CommonMetricData {
        name: "boolean_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["store2".into()],
        disabled: false,
        lifetime: Lifetime::User,
        ..Default::default()
    });
    metric.set_sync(&glean, true);

    for i in 0..=1 {
        for ping_name in ["store1", "store2"].iter() {
            let ping_type = PingBuilder::new(ping_name).build();
            let ping = ping_maker
                .collect(&glean, &ping_type, None, "", "")
                .unwrap();
            let seq_num = ping.content["ping_info"]["seq"].as_i64().unwrap();
            // Ensure sequence numbers in different stores are independent of
            // each other
            assert_eq!(i, seq_num);
        }
    }

    // Test that ping sequence numbers increase independently.
    {
        let ping_type = new_test_ping(&mut glean, "store1");

        // 3rd ping of store1
        let ping = ping_maker
            .collect(&glean, &ping_type, None, "", "")
            .unwrap();
        let seq_num = ping.content["ping_info"]["seq"].as_i64().unwrap();
        assert_eq!(2, seq_num);

        // 4th ping of store1
        let ping = ping_maker
            .collect(&glean, &ping_type, None, "", "")
            .unwrap();
        let seq_num = ping.content["ping_info"]["seq"].as_i64().unwrap();
        assert_eq!(3, seq_num);
    }

    {
        let ping_type = new_test_ping(&mut glean, "store2");

        // 3rd ping of store2
        let ping = ping_maker
            .collect(&glean, &ping_type, None, "", "")
            .unwrap();
        let seq_num = ping.content["ping_info"]["seq"].as_i64().unwrap();
        assert_eq!(2, seq_num);
    }

    {
        let ping_type = new_test_ping(&mut glean, "store1");

        // 5th ping of store1
        let ping = ping_maker
            .collect(&glean, &ping_type, None, "", "")
            .unwrap();
        let seq_num = ping.content["ping_info"]["seq"].as_i64().unwrap();
        assert_eq!(4, seq_num);
    }
}

#[test]
fn clear_pending_pings() {
    let (mut glean, _t) = new_glean(None);
    let ping_maker = PingMaker::new();
    let ping_type = new_test_ping(&mut glean, "store1");

    // Record something, so the ping will have data
    let metric = BooleanMetric::new(CommonMetricData {
        name: "boolean_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["store1".into()],
        disabled: false,
        lifetime: Lifetime::User,
        ..Default::default()
    });
    metric.set_sync(&glean, true);

    assert!(ping_type.submit_sync(&glean, None));
    assert_eq!(1, get_queued_pings(glean.get_data_path()).unwrap().len());

    let disabled_pings = &["store1"][..];
    assert!(ping_maker
        .clear_pending_pings(glean.get_data_path(), disabled_pings)
        .is_ok());
    assert_eq!(0, get_queued_pings(glean.get_data_path()).unwrap().len());
}

#[test]
fn no_pings_submitted_if_upload_disabled() {
    // Regression test, bug 1603571

    let (mut glean, _t) = new_glean(None);
    let ping_type = PingBuilder::new("store1").with_send_if_empty(true).build();
    glean.register_ping_type(&ping_type);

    assert!(ping_type.submit_sync(&glean, None));
    assert_eq!(1, get_queued_pings(glean.get_data_path()).unwrap().len());

    // Disable upload, then try to sumbit
    glean.set_upload_enabled(false);

    // Test again through the direct call
    assert!(!ping_type.submit_sync(&glean, None));
    assert_eq!(0, get_queued_pings(glean.get_data_path()).unwrap().len());
}

#[test]
fn metadata_is_correctly_added_when_necessary() {
    let (mut glean, _t) = new_glean(None);
    glean.set_debug_view_tag("valid-tag");
    let ping_type = PingBuilder::new("store1").with_send_if_empty(true).build();
    glean.register_ping_type(&ping_type);

    assert!(ping_type.submit_sync(&glean, None));

    let (_, _, metadata) = &get_queued_pings(glean.get_data_path()).unwrap()[0];
    let headers = metadata.as_ref().unwrap().get("headers").unwrap();
    assert_eq!(headers.get("X-Debug-ID").unwrap(), "valid-tag");
}

#[test]
fn attribution_and_distribution_appear_in_client_info() {
    let (glean, ping_maker, ping_type, _t) = set_up_basic_ping();

    let attribution = AttributionMetrics {
        source: Some("source".into()),
        medium: Some("medium".into()),
        campaign: Some("campaign".into()),
        term: Some("term".into()),
        content: Some("content".into()),
    };
    let distribution = DistributionMetrics {
        name: Some("name".into()),
    };
    glean.update_attribution(attribution);
    glean.update_distribution(distribution);

    let ping = ping_maker
        .collect(&glean, &ping_type, None, "", "")
        .unwrap();
    let client_info = ping.content["client_info"].as_object().unwrap();

    assert_eq!(json!({"name": "name"}), client_info["distribution"]);
    assert_eq!(
        json!({
            "source": "source",
            "medium": "medium",
            "campaign": "campaign",
            "term": "term",
            "content": "content",
        }),
        client_info["attribution"]
    );

    // Now let's test updated values.
    let attribution_update = AttributionMetrics {
        content: Some("what a boring word".into()),
        ..Default::default()
    };
    let distribution_update = DistributionMetrics {
        name: Some("what's in a name".into()),
    };
    glean.update_attribution(attribution_update);
    glean.update_distribution(distribution_update);

    let ping = ping_maker
        .collect(&glean, &ping_type, None, "", "")
        .unwrap();
    let client_info = ping.content["client_info"].as_object().unwrap();

    assert_eq!(
        json!({"name": "what's in a name"}),
        client_info["distribution"]
    );
    assert_eq!(
        json!({
            "source": "source",
            "medium": "medium",
            "campaign": "campaign",
            "term": "term",
            "content": "what a boring word",
        }),
        client_info["attribution"]
    );
}
