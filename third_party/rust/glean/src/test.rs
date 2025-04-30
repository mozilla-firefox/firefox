// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::io::Read;
use std::sync::{Arc, Barrier, Mutex};
use std::thread::{self, ThreadId};
use std::time::{Duration, Instant};

use crossbeam_channel::RecvTimeoutError;
use flate2::read::GzDecoder;
use glean_core::glean_test_get_experimentation_id;
use serde_json::Value as JsonValue;

use crate::private::PingType;
use crate::private::{BooleanMetric, CounterMetric, EventMetric, StringMetric, TextMetric};

use super::*;
use crate::common_test::{lock_test, new_glean, GLOBAL_APPLICATION_ID};

fn new_test_ping(name: &str) -> PingType {
    PingType::new(
        name,
        true,
        true,
        true,
        true,
        true,
        vec![],
        vec![],
        true,
        vec![],
    )
}

#[test]
fn send_a_ping() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<net::PingUploadRequest>(1);

    // Define a fake uploader that reports back the ping upload request.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<net::PingUploadRequest>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Define a new ping and submit it.
    const PING_NAME: &str = "test-ping";
    let custom_ping = new_test_ping(PING_NAME);
    custom_ping.submit(None);

    // Wait for the ping to arrive.
    let upload_request = r.recv().unwrap();
    assert!(upload_request.body_has_info_sections);
    assert_eq!(upload_request.ping_name, PING_NAME);
    assert!(upload_request.url.contains(PING_NAME));
}

#[test]
fn send_a_ping_without_info_sections() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<net::PingUploadRequest>(1);

    // Define a fake uploader that reports back the ping upload request.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<net::PingUploadRequest>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Define a new ping and submit it.
    const PING_NAME: &str = "noinfo-ping";
    let custom_ping = PingType::new(
        PING_NAME,
        true,
        true,
        true,
        /* include_info_sections */ false,
        true,
        vec![],
        vec![],
        true,
        vec![],
    );
    custom_ping.submit(None);

    // Wait for the ping to arrive.
    let upload_request = r.recv().unwrap();
    assert!(!upload_request.body_has_info_sections);
    assert_eq!(upload_request.ping_name, PING_NAME);
}

#[test]
fn disabling_upload_disables_metrics_recording() {
    let _lock = lock_test();

    let _t = new_glean(None, true);

    let metric = BooleanMetric::new(CommonMetricData {
        name: "bool_metric".into(),
        category: "test".into(),
        send_in_pings: vec!["store1".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        dynamic_label: None,
    });

    crate::set_upload_enabled(false);

    assert!(metric.test_get_value(Some("store1".into())).is_none())
}

#[test]
fn test_experiments_recording() {
    let _lock = lock_test();

    let _t = new_glean(None, true);

    set_experiment_active("experiment_test".to_string(), "branch_a".to_string(), None);
    let mut extra = HashMap::new();
    extra.insert("test_key".to_string(), "value".to_string());
    set_experiment_active(
        "experiment_api".to_string(),
        "branch_b".to_string(),
        Some(extra),
    );
    assert!(test_is_experiment_active("experiment_test".to_string()));
    assert!(test_is_experiment_active("experiment_api".to_string()));
    set_experiment_inactive("experiment_test".to_string());
    assert!(!test_is_experiment_active("experiment_test".to_string()));
    assert!(test_is_experiment_active("experiment_api".to_string()));
    let stored_data = test_get_experiment_data("experiment_api".to_string()).unwrap();
    assert_eq!("branch_b", stored_data.branch);
    assert_eq!("value", stored_data.extra.unwrap()["test_key"]);
}

#[test]
fn test_experiments_recording_before_glean_inits() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    destroy_glean(true, &tmpname);

    set_experiment_active(
        "experiment_set_preinit".to_string(),
        "branch_a".to_string(),
        None,
    );
    set_experiment_active(
        "experiment_preinit_disabled".to_string(),
        "branch_a".to_string(),
        None,
    );
    set_experiment_inactive("experiment_preinit_disabled".to_string());

    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    assert!(test_is_experiment_active(
        "experiment_set_preinit".to_string()
    ));
    assert!(!test_is_experiment_active(
        "experiment_preinit_disabled".to_string()
    ));
}

#[test]
fn test_experimentation_id_recording() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    destroy_glean(true, &tmpname);

    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_experimentation_id("alpha-beta-gamma-delta".to_string())
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    let exp_id = glean_test_get_experimentation_id().expect("Experimentation id must not be None");
    assert_eq!(
        "alpha-beta-gamma-delta".to_string(),
        exp_id,
        "Experimentation id must match"
    );
}

#[test]
fn sending_of_foreground_background_pings() {
    let _lock = lock_test();

    let click: EventMetric<traits::NoExtraKeys> = private::EventMetric::new(CommonMetricData {
        name: "click".into(),
        category: "ui".into(),
        send_in_pings: vec!["events".into()],
        lifetime: Lifetime::Ping,
        disabled: false,
        ..Default::default()
    });

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<String>(3);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Simulate becoming active.
    handle_client_active();

    // We expect a baseline ping to be generated here (reason: 'active').
    let url = r.recv().unwrap();
    assert!(url.contains("baseline"));

    // Recording an event so that an "events" ping will contain data.
    click.record(None);

    // Simulate becoming inactive
    handle_client_inactive();

    // Wait for the pings to arrive.
    let mut expected_pings = vec!["baseline", "events"];
    for _ in 0..2 {
        let url = r.recv().unwrap();
        // If the url contains the expected reason, remove it from the list.
        expected_pings.retain(|&name| !url.contains(name));
    }
    // We received all the expected pings.
    assert_eq!(0, expected_pings.len());

    // Simulate becoming active again.
    handle_client_active();

    // We expect a baseline ping to be generated here (reason: 'active').
    let url = r.recv().unwrap();
    assert!(url.contains("baseline"));
}

#[test]
fn sending_of_startup_baseline_ping() {
    let _lock = lock_test();

    // Create an instance of Glean and then flip the dirty
    // bit to true.
    let data_dir = new_glean(None, true);

    glean_core::glean_set_dirty_flag(true);

    // Restart glean and wait for a baseline ping to be generated.
    let (s, r) = crossbeam_channel::bounded::<String>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let tmpname = data_dir.path().to_path_buf();

    // Now reset Glean: it should still send a baseline ping with reason
    // dirty_startup when starting, because of the dirty bit being set.
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    // Wait for the ping to arrive.
    let url = r.recv().unwrap();
    assert!(url.contains("baseline"));
}

#[test]
fn no_dirty_baseline_on_clean_shutdowns() {
    let _lock = lock_test();

    // Create an instance of Glean, wait for init and then flip the dirty
    // bit to true.
    let data_dir = new_glean(None, true);

    glean_core::glean_set_dirty_flag(true);

    crate::shutdown();

    // Restart glean and wait for a baseline ping to be generated.
    let (s, r) = crossbeam_channel::bounded::<String>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let tmpname = data_dir.path().to_path_buf();

    // Now reset Glean: it should not send a baseline ping, because
    // we cleared the dirty bit.
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    // We don't expect a startup ping.
    assert_eq!(r.try_recv(), Err(crossbeam_channel::TryRecvError::Empty));
}

#[test]
fn initialize_must_not_crash_if_data_dir_is_messed_up() {
    let _lock = lock_test();

    let dir = tempfile::tempdir().unwrap();
    let tmpdirname = dir.path();
    // Create a file in the temporary dir and use that as the
    // name of the Glean data dir.
    let file_path = tmpdirname.to_path_buf().join("notadir");
    std::fs::write(file_path.clone(), "test").expect("The test Glean dir file must be created");

    let cfg = ConfigurationBuilder::new(true, file_path, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .build();

    test_reset_glean(cfg, ClientInfoMetrics::unknown(), false);

    // We don't need to sleep here.
    // The `test_reset_glean` already waited on the initialize task.
}

#[test]
fn queued_recorded_metrics_correctly_record_during_init() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    destroy_glean(true, &tmpname);

    let metric = CounterMetric::new(CommonMetricData {
        name: "counter_metric".into(),
        category: "test".into(),
        send_in_pings: vec!["store1".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        dynamic_label: None,
    });

    // This will queue 3 tasks that will add to the metric value once Glean is initialized
    for _ in 0..3 {
        metric.add(1);
    }

    // TODO: To be fixed in bug 1677150.
    // Ensure that no value has been stored yet since the tasks have only been queued
    // and not executed yet

    // Calling `new_glean` here will cause Glean to be initialized and should cause the queued
    // tasks recording metrics to execute
    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .build();
    let _t = new_glean(Some(cfg), false);

    // Verify that the callback was executed by testing for the correct value
    assert!(metric.test_get_value(None).is_some(), "Value must exist");
    assert_eq!(3, metric.test_get_value(None).unwrap(), "Value must match");
}

#[test]
fn initializing_twice_is_a_noop() {
    let _lock = lock_test();

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
        true,
    );

    // Glean was initialized and it waited for a full initialization to finish.
    // We now just want to try to initialize again.
    // This will bail out early.

    crate::initialize(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
    );

    // We don't need to sleep here.
    // The `test_reset_glean` already waited on the initialize task,
    // and the 2nd initialize will bail out early.
    //
    // All we tested is that this didn't crash.
}

#[test]
fn dont_handle_events_when_uninitialized() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
        true,
    );

    // Ensure there's at least one event recorded,
    // otherwise the ping is not sent.
    let click: EventMetric<traits::NoExtraKeys> = private::EventMetric::new(CommonMetricData {
        name: "click".into(),
        category: "ui".into(),
        send_in_pings: vec!["events".into()],
        lifetime: Lifetime::Ping,
        disabled: false,
        ..Default::default()
    });
    click.record(None);
    // Wait for the dispatcher.
    assert_ne!(None, click.test_get_value(None));

    // Now destroy Glean. We test submission when not initialized.
    destroy_glean(false, &tmpname);

    // We reach into `glean_core` to test this,
    // only there we can synchronously submit and get a return value.
    assert!(!glean_core::glean_submit_ping_by_name_sync(
        "events".to_string(),
        None
    ));
}

// TODO: Should probably move into glean-core.
#[test]
fn the_app_channel_must_be_correctly_set_if_requested() {
    let _lock = lock_test();

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    // Internal metric, replicated here for testing.
    let app_channel = StringMetric::new(CommonMetricData {
        name: "app_channel".into(),
        category: "".into(),
        send_in_pings: vec!["glean_client_info".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        ..Default::default()
    });

    // No app_channel reported.
    let client_info = ClientInfoMetrics {
        channel: None,
        ..ClientInfoMetrics::unknown()
    };
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        client_info,
        true,
    );
    assert!(app_channel.test_get_value(None).is_none());

    // Custom app_channel reported.
    let client_info = ClientInfoMetrics {
        channel: Some("testing".into()),
        ..ClientInfoMetrics::unknown()
    };
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        client_info,
        true,
    );
    assert_eq!("testing", app_channel.test_get_value(None).unwrap());
}

#[test]
fn ping_collection_must_happen_after_concurrently_scheduled_metrics_recordings() {
    // Given the following block of code:
    //
    // Metric.A.set("SomeTestValue")
    // Glean.submitPings(listOf("custom-ping-1"))
    //
    // This test ensures that "custom-ping-1" contains "metric.a" with a value of "SomeTestValue"
    // when the ping is collected.

    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<(String, JsonValue)>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let net::PingUploadRequest { body, url, .. } =
                upload_request.capable(|_| true).unwrap();
            // Decode the gzipped body.
            let mut gzip_decoder = GzDecoder::new(&body[..]);
            let mut s = String::with_capacity(body.len());

            let data = gzip_decoder
                .read_to_string(&mut s)
                .ok()
                .map(|_| &s[..])
                .or_else(|| std::str::from_utf8(&body).ok())
                .and_then(|payload| serde_json::from_str(payload).ok())
                .unwrap();
            self.sender.send((url, data)).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .build(),
        ClientInfoMetrics::unknown(),
        true,
    );

    let ping_name = "custom_ping_1";
    let ping = new_test_ping(ping_name);
    let metric = private::StringMetric::new(CommonMetricData {
        name: "string_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec![ping_name.into()],
        lifetime: Lifetime::Ping,
        disabled: false,
        ..Default::default()
    });

    let test_value = "SomeTestValue";
    metric.set(test_value.to_string());
    ping.submit(None);

    // Wait for the ping to arrive.
    let (url, body) = r.recv().unwrap();
    assert!(url.contains(ping_name));

    assert_eq!(
        test_value,
        body["metrics"]["string"]["telemetry.string_metric"]
    );
}

#[test]
fn basic_metrics_should_be_cleared_when_disabling_uploading() {
    let _lock = lock_test();

    let _t = new_glean(None, false);

    let metric = private::StringMetric::new(CommonMetricData {
        name: "string_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["store1".into()],
        lifetime: Lifetime::Ping,
        disabled: false,
        ..Default::default()
    });

    assert!(metric.test_get_value(None).is_none());

    metric.set("TEST VALUE".into());
    assert!(metric.test_get_value(None).is_some());

    set_upload_enabled(false);
    assert!(metric.test_get_value(None).is_none());
    metric.set("TEST VALUE".into());
    assert!(metric.test_get_value(None).is_none());

    set_upload_enabled(true);
    assert!(metric.test_get_value(None).is_none());
    metric.set("TEST VALUE".into());
    assert_eq!("TEST VALUE", metric.test_get_value(None).unwrap());
}

#[test]
fn core_metrics_are_not_cleared_when_disabling_and_enabling_uploading() {
    let _lock = lock_test();

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    // No app_channel reported.
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
        true,
    );

    // Internal metric, replicated here for testing.
    let os_version = StringMetric::new(CommonMetricData {
        name: "os_version".into(),
        category: "".into(),
        send_in_pings: vec!["glean_client_info".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        ..Default::default()
    });

    assert!(os_version.test_get_value(None).is_some());
    let initial_value = os_version.test_get_value(None).unwrap();

    set_upload_enabled(false);
    assert_eq!(initial_value, os_version.test_get_value(None).unwrap());

    set_upload_enabled(true);
    assert_eq!(initial_value, os_version.test_get_value(None).unwrap());
}

#[test]
fn sending_deletion_ping_if_disabled_outside_of_run() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<String>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .build();

    let _t = new_glean(Some(cfg), true);

    // Now reset Glean and disable upload: it should still send a deletion request
    // ping even though we're just starting.
    test_reset_glean(
        ConfigurationBuilder::new(false, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    // Wait for the ping to arrive.
    let url = r.recv().unwrap();
    assert!(url.contains("deletion-request"));
}

#[test]
fn no_sending_of_deletion_ping_if_unchanged_outside_of_run() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<String>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .build();

    let _t = new_glean(Some(cfg), true);

    // Now reset Glean and keep upload enabled: no deletion-request
    // should be sent.
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    assert_eq!(0, r.len());
}

#[test]
fn deletion_request_ping_contains_experimentation_id() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<JsonValue>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<JsonValue>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            let body = upload_request.body;
            let mut gzip_decoder = GzDecoder::new(&body[..]);
            let mut body_str = String::with_capacity(body.len());
            let data: JsonValue = gzip_decoder
                .read_to_string(&mut body_str)
                .ok()
                .map(|_| &body_str[..])
                .or_else(|| std::str::from_utf8(&body).ok())
                .and_then(|payload| serde_json::from_str(payload).ok())
                .unwrap();
            self.sender.send(data).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_experimentation_id("alpha-beta-gamma-delta".to_string())
        .build();

    let _t = new_glean(Some(cfg), true);

    // Now reset Glean and disable upload: it should still send a deletion request
    // ping even though we're just starting.
    test_reset_glean(
        ConfigurationBuilder::new(false, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .with_experimentation_id("alpha-beta-gamma-delta".to_string())
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    // Wait for the ping to arrive and check the experimentation id matches
    let url = r.recv().unwrap();
    let metrics = url.get("metrics").unwrap();
    let strings = metrics.get("string").unwrap();
    assert_eq!(
        "alpha-beta-gamma-delta",
        strings
            .get("glean.client.annotation.experimentation_id")
            .unwrap()
    );
}

#[test]
fn test_sending_of_startup_baseline_ping_with_application_lifetime_metric() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<(String, JsonValue)>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let net::PingUploadRequest { url, body, .. } =
                upload_request.capable(|_| true).unwrap();
            // Decode the gzipped body.
            let mut gzip_decoder = GzDecoder::new(&body[..]);
            let mut s = String::with_capacity(body.len());

            let data = gzip_decoder
                .read_to_string(&mut s)
                .ok()
                .map(|_| &s[..])
                .or_else(|| std::str::from_utf8(&body).ok())
                .and_then(|payload| serde_json::from_str(payload).ok())
                .unwrap();
            self.sender.send((url, data)).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname.clone(), GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
        true,
    );

    // Reaching into the core.
    glean_core::glean_set_dirty_flag(true);

    let metric = private::StringMetric::new(CommonMetricData {
        name: "app_lifetime".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["baseline".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        ..Default::default()
    });
    let test_value = "HELLOOOOO!";
    metric.set(test_value.into());
    assert_eq!(test_value, metric.test_get_value(None).unwrap());

    // Restart glean and don't clear the stores.
    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .with_uploader(FakeUploader { sender: s })
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    let (url, body) = r.recv().unwrap();
    assert!(url.contains("/baseline/"));

    // We set the dirty bit above.
    assert_eq!("dirty_startup", body["ping_info"]["reason"]);
    assert_eq!(
        test_value,
        body["metrics"]["string"]["telemetry.app_lifetime"]
    );
}

#[test]
fn setting_debug_view_tag_before_initialization_should_not_crash() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    destroy_glean(true, &tmpname);

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<Vec<(String, String)>>(1);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<Vec<(String, String)>>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.headers).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Attempt to set a debug view tag before Glean is initialized.
    set_debug_view_tag("valid-tag");

    // Create a custom configuration to use a fake uploader.
    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Submit a baseline ping.
    submit_ping_by_name("baseline", Some("inactive"));

    // Wait for the ping to arrive.
    let headers = r.recv().unwrap();
    assert_eq!(
        "valid-tag",
        headers.iter().find(|&kv| kv.0 == "X-Debug-ID").unwrap().1
    );
}

#[test]
fn setting_source_tags_before_initialization_should_not_crash() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    destroy_glean(true, &tmpname);
    //assert!(!was_initialize_called());

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<Vec<(String, String)>>(1);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<Vec<(String, String)>>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.headers).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Attempt to set source tags before Glean is initialized.
    set_source_tags(vec!["valid-tag1".to_string(), "valid-tag2".to_string()]);

    // Create a custom configuration to use a fake uploader.
    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Submit a baseline ping.
    submit_ping_by_name("baseline", Some("inactive"));

    // Wait for the ping to arrive.
    let headers = r.recv().unwrap();
    assert_eq!(
        "valid-tag1,valid-tag2",
        headers
            .iter()
            .find(|&kv| kv.0 == "X-Source-Tags")
            .unwrap()
            .1
    );
}

#[test]
fn setting_source_tags_after_initialization_should_not_crash() {
    let _lock = lock_test();

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<Vec<(String, String)>>(1);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<Vec<(String, String)>>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.headers).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Attempt to set source tags after `Glean.initialize` is called,
    // but before Glean is fully initialized.
    //assert!(was_initialize_called());
    set_source_tags(vec!["valid-tag1".to_string(), "valid-tag2".to_string()]);

    // Submit a baseline ping.
    submit_ping_by_name("baseline", Some("inactive"));

    // Wait for the ping to arrive.
    let headers = r.recv().unwrap();
    assert_eq!(
        "valid-tag1,valid-tag2",
        headers
            .iter()
            .find(|&kv| kv.0 == "X-Source-Tags")
            .unwrap()
            .1
    );
}

#[test]
fn flipping_upload_enabled_respects_order_of_events() {
    // NOTES(janerik):
    // I'm reasonably sure this test is excercising the right code paths
    // and from the log output it does the right thing:
    //
    // * It fully initializes with the assumption uploadEnabled=true
    // * It then disables upload
    // * Then it submits the custom ping, which rightfully is ignored because uploadEnabled=false.
    //
    // The test passes.
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<String>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    // We create a ping and a metric before we initialize Glean
    let sample_ping = new_test_ping("sample-ping-1");
    let metric = private::StringMetric::new(CommonMetricData {
        name: "string_metric".into(),
        category: "telemetry".into(),
        send_in_pings: vec!["sample-ping-1".into()],
        lifetime: Lifetime::Ping,
        disabled: false,
        ..Default::default()
    });

    let _t = new_glean(Some(cfg), true);

    // Glean might still be initializing. Disable upload.
    set_upload_enabled(false);

    // Set data and try to submit a custom ping.
    metric.set("some-test-value".into());
    sample_ping.submit(None);

    // Wait for the ping to arrive.
    let url = r.recv().unwrap();
    assert!(url.contains("deletion-request"));
}

#[test]
fn registering_pings_before_init_must_work() {
    let _lock = lock_test();

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<String>(1);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom ping and attempt its registration.
    let sample_ping = new_test_ping("pre-register");

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Submit a test ping.
    sample_ping.submit(None);

    // Wait for the ping to arrive.
    let url = r.recv().unwrap();
    assert!(url.contains("pre-register"));
}

#[test]
fn test_a_ping_before_submission() {
    let _lock = lock_test();

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<String>(1);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Create a custom ping and register it.
    let sample_ping = new_test_ping("custom1");

    let metric = CounterMetric::new(CommonMetricData {
        name: "counter_metric".into(),
        category: "test".into(),
        send_in_pings: vec!["custom1".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        dynamic_label: None,
    });

    metric.add(1);

    sample_ping.test_before_next_submit(move |reason| {
        assert_eq!(None, reason);
        assert_eq!(1, metric.test_get_value(None).unwrap());
    });

    // Submit a baseline ping.
    sample_ping.submit(None);

    // Wait for the ping to arrive.
    let url = r.recv().unwrap();
    assert!(url.contains("custom1"));
}

#[test]
fn test_boolean_get_num_errors() {
    let _lock = lock_test();

    let _t = new_glean(None, false);

    let metric = BooleanMetric::new(CommonMetricData {
        name: "counter_metric".into(),
        category: "test".into(),
        send_in_pings: vec!["custom1".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        dynamic_label: Some(str::to_string("asdf")),
    });

    // Check specifically for an invalid label
    let result = metric.test_get_num_recorded_errors(ErrorType::InvalidLabel);

    assert_eq!(result, 0);
}

#[test]
fn test_text_can_hold_long_string() {
    let _lock = lock_test();

    let _t = new_glean(None, false);

    let metric = TextMetric::new(CommonMetricData {
        name: "text_metric".into(),
        category: "test".into(),
        send_in_pings: vec!["custom1".into()],
        lifetime: Lifetime::Application,
        disabled: false,
        dynamic_label: Some(str::to_string("text")),
    });

    // 216 characters, which would overflow StringMetric
    metric.set("I've seen things you people wouldn't believe. Attack ships on fire off the shoulder of Orion. I watched C-beams glitter in the dark near the Tannhäuser Gate. All those moments will be lost in time, like tears in rain".into());

    let result = metric.test_get_num_recorded_errors(ErrorType::InvalidValue);
    assert_eq!(result, 0);

    let result = metric.test_get_num_recorded_errors(ErrorType::InvalidOverflow);
    assert_eq!(result, 0);
}

#[test]
fn signaling_done() {
    let _lock = lock_test();

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        barrier: Arc<Barrier>,
        counter: Arc<Mutex<HashMap<ThreadId, u32>>>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, _upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let mut map = self.counter.lock().unwrap();
            *map.entry(thread::current().id()).or_insert(0) += 1;

            // Wait for the sync.
            self.barrier.wait();

            // Signal that this uploader thread is done.
            net::UploadResult::done()
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    // We use a barrier to sync this test thread with the uploader thread.
    let barrier = Arc::new(Barrier::new(2));
    // We count how many times `upload` was invoked per thread.
    let call_count = Arc::new(Mutex::default());

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader {
            barrier: Arc::clone(&barrier),
            counter: Arc::clone(&call_count),
        })
        .build();

    let _t = new_glean(Some(cfg), true);

    // Define a new ping and submit it.
    const PING_NAME: &str = "test-ping";
    let custom_ping = new_test_ping(PING_NAME);
    custom_ping.submit(None);
    custom_ping.submit(None);

    // Sync up with the upload thread.
    barrier.wait();

    // Submit another ping and wait for it to do work.
    custom_ping.submit(None);

    // Sync up with the upload thread again.
    // This will not be the same thread as the one before (hopefully).
    barrier.wait();

    // No one's ever gonna wait for the uploader thread (the RLB doesn't store the handle to it),
    // so all we can do is hope it finishes within time.
    std::thread::sleep(std::time::Duration::from_millis(100));

    let map = call_count.lock().unwrap();
    assert_eq!(2, map.len(), "should have launched 2 uploader threads");
    for &count in map.values() {
        assert_eq!(1, count, "each thread should call upload only once");
    }
}

#[test]
fn configure_ping_throttling() {
    let _lock = lock_test();

    let (s, r) = crossbeam_channel::bounded::<String>(1);

    // Define a fake uploader that reports back the submission URL
    // using a crossbeam channel.
    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
        done: Arc<std::sync::atomic::AtomicBool>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            if self.done.load(std::sync::atomic::Ordering::SeqCst) {
                // If we've outlived the test, just lie.
                return net::UploadResult::http_status(200);
            }
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let done = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let mut cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader {
            sender: s,
            done: Arc::clone(&done),
        })
        .build();
    let pings_per_interval = 10;
    cfg.rate_limit = Some(crate::PingRateLimit {
        seconds_per_interval: 1,
        pings_per_interval,
    });

    let _t = new_glean(Some(cfg), true);

    // Define a new ping.
    const PING_NAME: &str = "test-ping";
    let custom_ping = new_test_ping(PING_NAME);

    // Submit and receive it `pings_per_interval` times.
    for _ in 0..pings_per_interval {
        custom_ping.submit(None);

        // Wait for the ping to arrive.
        let url = r.recv().unwrap();
        assert!(url.contains(PING_NAME));
    }

    // Submit one ping more than the rate limit permits.
    custom_ping.submit(None);

    // We'd expect it to be received within 250ms if it weren't throttled.
    let now = Instant::now();
    assert_eq!(
        r.recv_deadline(now + Duration::from_millis(250)),
        Err(RecvTimeoutError::Timeout)
    );

    // We still have to deal with that eleventh ping.
    // When it eventually processes after the throttle interval, this'll tell
    // it that it's done.
    done.store(true, std::sync::atomic::Ordering::SeqCst);
    // Unfortunately, we'll still be stuck waiting the full
    // `seconds_per_interval` before running the next test, since shutting down
    // will wait for the queue to clear.
}

#[test]
fn pings_ride_along_builtin_pings() {
    let _lock = lock_test();

    // Define a fake uploader that reports back the submission headers
    // using a crossbeam channel.
    let (s, r) = crossbeam_channel::bounded::<String>(3);

    #[derive(Debug)]
    pub struct FakeUploader {
        sender: crossbeam_channel::Sender<String>,
    }
    impl net::PingUploader for FakeUploader {
        fn upload(&self, upload_request: net::CapablePingUploadRequest) -> net::UploadResult {
            let upload_request = upload_request.capable(|_| true).unwrap();
            self.sender.send(upload_request.url).unwrap();
            net::UploadResult::http_status(200)
        }
    }

    // Create a custom configuration to use a fake uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let ping_schedule = HashMap::from([("baseline".to_string(), vec!["ride-along".to_string()])]);

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(FakeUploader { sender: s })
        .with_ping_schedule(ping_schedule)
        .build();

    let _t = new_glean(Some(cfg), true);

    let _ride_along_ping = new_test_ping("ride-along");

    // Simulate becoming active.
    handle_client_active();

    // We expect a baseline ping to be generated here (reason: 'active').
    let url = r.recv().unwrap();
    assert!(url.contains("baseline"));

    // We expect a ride-along ping to ride along.
    let url = r.recv().unwrap();
    assert!(url.contains("ride-along"));
}

#[test]
fn test_attribution_and_distribution_updates_before_glean_inits() {
    let _lock = lock_test();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    destroy_glean(true, &tmpname);

    // No Glean, let's set and update some attribution + distribution.
    let mut attribution = AttributionMetrics {
        source: Some("source".into()),
        medium: Some("medium".into()),
        campaign: Some("campaign".into()),
        term: Some("term".into()),
        content: Some("content".into()),
    };
    let distribution = DistributionMetrics {
        name: Some("name".into()),
    };
    update_attribution(attribution.clone());
    update_distribution(distribution);

    let attribution_update = AttributionMetrics {
        term: Some("new term".into()),
        ..Default::default()
    };
    let distribution_update = DistributionMetrics {
        name: Some("different name".into()),
    };
    update_attribution(attribution_update);
    update_distribution(distribution_update.clone());

    test_reset_glean(
        ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
            .with_server_endpoint("invalid-test-host")
            .build(),
        ClientInfoMetrics::unknown(),
        false,
    );

    // Ensure the updated attribution + distribution are correctly stored.
    attribution.term = Some("new term".into());
    assert_eq!(attribution, test_get_attribution());
    assert_eq!(distribution_update, test_get_distribution());
}
