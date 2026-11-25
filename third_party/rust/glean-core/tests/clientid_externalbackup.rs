// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
mod common;
use crate::common::*;

use std::fs;
use std::path::Path;
use std::sync::{Arc, Mutex};

use uuid::Uuid;

use glean_core::metrics::*;
use glean_core::CommonMetricData;
use glean_core::ExternalBackup;
use glean_core::Glean;
use glean_core::Lifetime;

fn clientid_metric() -> UuidMetric {
    UuidMetric::new(CommonMetricData {
        name: "client_id".into(),
        category: "".into(),
        send_in_pings: vec!["glean_client_info".into()],
        lifetime: Lifetime::User,
        disabled: false,
        label: None,
        in_session: false,
    })
}

fn clientid_from_file(data_path: &Path) -> Option<Uuid> {
    let path = data_path.join("client_id.txt");
    let uuid_str = fs::read_to_string(path).ok()?;
    Uuid::parse_str(uuid_str.trim_end()).ok()
}

#[derive(Debug)]
struct ClientIDBackup {
    pub client_id: Arc<Mutex<Option<String>>>,
    pub should_apply: bool,
}

impl ExternalBackup for ClientIDBackup {
    fn store(&self, key: String, value: String) {
        assert_eq!("client_id".to_string(), key);
        *self.client_id.lock().unwrap() = Some(value);
    }

    fn load(&self, key: String) -> Option<String> {
        assert_eq!("client_id".to_string(), key);
        self.client_id.lock().unwrap().clone()
    }

    fn clear(&self, key: String) {
        assert_eq!("client_id".to_string(), key);
        *self.client_id.lock().unwrap() = None;
    }

    fn clear_all(&self) {
        self.clear("client_id".to_string());
    }

    fn should_apply_stored_client_id(&self) -> bool {
        self.should_apply
    }
}

fn new_glean_with_backup(
    tempdir: Option<tempfile::TempDir>,
    backup: Option<Box<dyn ExternalBackup>>,
) -> (Glean, tempfile::TempDir) {
    let dir = tempdir.unwrap_or_else(|| tempfile::tempdir().unwrap());

    let cfg = glean_core::InternalConfiguration {
        data_path: dir.path().display().to_string(),
        application_id: GLOBAL_APPLICATION_ID.into(),
        language_binding_name: "Rust".into(),
        upload_enabled: true,
        max_events: None,
        delay_ping_lifetime_io: true, // Differs from `new_glean`
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
        session_mode: glean_core::SessionMode::Auto,
        session_sample_rate: 1.0,
        session_inactivity_timeout_ms: 1_800_000,
        events_ping_acceleration_factor: None,
        enable_store_submitted_pings: false,
    };
    let mut glean = Glean::new(cfg, backup).unwrap();
    _ = new_test_ping(&mut glean, "store1");

    (glean, dir)
}

#[test]
fn normal_init() {
    let backup_client_id = Arc::new(Mutex::new(None));
    let backup = Some(Box::new(ClientIDBackup {
        client_id: backup_client_id.clone(),
        should_apply: false,
    }) as _);
    let (glean, temp) = new_glean_with_backup(None, backup);

    let db_client_id = clientid_metric().get_value(&glean, None).unwrap();
    let file_client_id = clientid_from_file(temp.path()).unwrap();

    assert_eq!(file_client_id, db_client_id);
    assert_eq!(
        file_client_id,
        Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
            .ok()
            .unwrap(),
        "The empty backup ought to have been filled by now."
    );

    glean.submit_ping_by_name("health", Some("pre_init"));
    let mut pending = get_queued_pings(temp.path()).unwrap();
    assert_eq!(1, pending.len());
    let payload = pending.pop().unwrap().1;

    let client_id = payload["client_info"]["client_id"].as_str();
    assert_eq!(
        Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
        client_id
    );
    let state = &payload["metrics"]["string"]["glean.health.exception_state"];
    assert_eq!(&serde_json::Value::Null, state);
    let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
        ["none"]
        .as_i64()
        .unwrap();
    assert_eq!(1, status);
}

#[test]
fn only_backup_id_but_dont_apply_it() {
    let valid_client_id = "00000000-0000-4000-0000-000000000000".to_string();
    let backup_client_id = Arc::new(Mutex::new(Some(valid_client_id.clone())));
    let backup = Some(Box::new(ClientIDBackup {
        client_id: backup_client_id.clone(),
        should_apply: false,
    }) as _);
    let (glean, temp) = new_glean_with_backup(None, backup);

    let db_client_id = clientid_metric().get_value(&glean, None).unwrap();
    let file_client_id = clientid_from_file(temp.path()).unwrap();

    assert_eq!(file_client_id, db_client_id);
    assert_eq!(
        file_client_id,
        Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
            .ok()
            .unwrap()
    );
    assert_ne!(
        backup_client_id.lock().unwrap().as_ref().unwrap(),
        &valid_client_id,
        "Though the backup was valid, and the SDK had no ID to use, still generate and backup a new ID."
    );

    glean.submit_ping_by_name("health", Some("pre_init"));
    let mut pending = get_queued_pings(temp.path()).unwrap();
    assert_eq!(1, pending.len());
    let payload = pending.pop().unwrap().1;

    let client_id = payload["client_info"]["client_id"].as_str();
    assert_eq!(
        Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
        client_id
    );
    let state = &payload["metrics"]["string"]["glean.health.exception_state"];
    assert_eq!(&serde_json::Value::Null, state);
    let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
        ["backup_only"]
        .as_i64()
        .unwrap();
    assert_eq!(1, status);
    let recovered = payload["metrics"]["string"]["glean.health.recovered_client_id"].as_str();
    assert_eq!(None, recovered);
}

#[test]
fn only_backup_id_and_apply_it() {
    let valid_client_id = "00000000-0000-4000-0000-000000000000".to_string();
    let backup_client_id = Arc::new(Mutex::new(Some(valid_client_id.clone())));
    let backup = Some(Box::new(ClientIDBackup {
        client_id: backup_client_id.clone(),
        should_apply: true,
    }) as _);
    let (glean, temp) = new_glean_with_backup(None, backup);

    let db_client_id = clientid_metric().get_value(&glean, None).unwrap();
    let file_client_id = clientid_from_file(temp.path()).unwrap();

    assert_eq!(file_client_id, db_client_id);
    assert_eq!(
        file_client_id,
        Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
            .ok()
            .unwrap()
    );
    assert_eq!(
        backup_client_id.lock().unwrap().as_ref().unwrap(),
        &valid_client_id
    );

    glean.submit_ping_by_name("health", Some("pre_init"));
    let mut pending = get_queued_pings(temp.path()).unwrap();
    assert_eq!(1, pending.len());
    let payload = pending.pop().unwrap().1;

    let client_id = payload["client_info"]["client_id"].as_str();
    assert_eq!(
        Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
        client_id
    );
    let state = &payload["metrics"]["string"]["glean.health.exception_state"];
    assert_eq!(&serde_json::Value::Null, state);
    let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
        ["backup_only"]
        .as_i64()
        .unwrap();
    assert_eq!(1, status);
    let recovered = payload["metrics"]["uuid"]["glean.health.recovered_client_id"].as_str();
    assert_eq!(
        Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
        recovered
    );
}

#[test]
fn invalid_backup_id_cases() {
    let invalid_client_ids = &[
        "00000000-0000-0000-0000-00000000000", // One character short.
        "obviously not a UUID",                // Not even Magritte.
    ];
    for value in invalid_client_ids {
        let backup_client_id = Arc::new(Mutex::new(Some(value.to_string())));
        let backup = Some(Box::new(ClientIDBackup {
            client_id: backup_client_id.clone(),
            should_apply: true,
        }) as _);
        let (glean, temp) = new_glean_with_backup(None, backup);

        let db_client_id = clientid_metric().get_value(&glean, None).unwrap();
        let file_client_id = clientid_from_file(temp.path()).unwrap();

        assert_eq!(file_client_id, db_client_id);
        assert_eq!(
            file_client_id,
            Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
                .ok()
                .unwrap()
        );
        assert_ne!(
            backup_client_id.lock().unwrap().as_ref().unwrap(),
            value,
            "Ensure we overrode the invalid backup with the newly-generated-and-valid ID."
        );

        glean.submit_ping_by_name("health", Some("pre_init"));
        let mut pending = get_queued_pings(temp.path()).unwrap();
        assert_eq!(1, pending.len());
        let payload = pending.pop().unwrap().1;

        let client_id = payload["client_info"]["client_id"].as_str();
        assert_eq!(
            Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
            client_id
        );
        let state = &payload["metrics"]["string"]["glean.health.exception_state"];
        assert_eq!(&serde_json::Value::Null, state);
        let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
            ["parse"]
            .as_i64()
            .unwrap();
        assert_eq!(1, status);
        let recovered = &payload["metrics"]["uuid"]["glean.health.recovered_client_id"];
        assert_eq!(&serde_json::Value::Null, recovered);
    }
}

#[test]
fn c0ffee_backup_id() {
    let known_client_id = "c0ffeec0-ffee-c0ff-eec0-ffeec0ffeec0";
    {
        let backup_client_id = Arc::new(Mutex::new(Some(known_client_id.to_string())));
        let backup = Some(Box::new(ClientIDBackup {
            client_id: backup_client_id.clone(),
            should_apply: true,
        }) as _);
        let (glean, temp) = new_glean_with_backup(None, backup);

        let db_client_id = clientid_metric().get_value(&glean, None).unwrap();
        let file_client_id = clientid_from_file(temp.path()).unwrap();

        assert_eq!(file_client_id, db_client_id);
        assert_eq!(
            file_client_id,
            Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
                .ok()
                .unwrap()
        );
        assert_ne!(
            backup_client_id.lock().unwrap().as_ref().unwrap(),
            known_client_id,
            "Ensure we overrode the c0ffee backup with the newly-generated ID."
        );

        glean.submit_ping_by_name("health", Some("pre_init"));
        let mut pending = get_queued_pings(temp.path()).unwrap();
        assert_eq!(1, pending.len());
        let payload = pending.pop().unwrap().1;

        let client_id = payload["client_info"]["client_id"].as_str();
        assert_eq!(
            Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
            client_id
        );
        let state = &payload["metrics"]["string"]["glean.health.exception_state"];
        assert_eq!(&serde_json::Value::Null, state);
        let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
            ["c0ffee"]
            .as_i64()
            .unwrap();
        assert_eq!(1, status);
        let recovered = &payload["metrics"]["uuid"]["glean.health.recovered_client_id"];
        assert_eq!(&serde_json::Value::Null, recovered);
    }
}

#[test]
fn backup_matches() {
    let backup_client_id = Arc::new(Mutex::new(None));
    let backup = Some(Box::new(ClientIDBackup {
        client_id: backup_client_id.clone(),
        should_apply: false,
    }) as _);
    let (glean, temp) = new_glean_with_backup(None, backup);

    let db_client_id = clientid_metric().get_value(&glean, None).unwrap();
    let file_client_id = clientid_from_file(temp.path()).unwrap();

    assert_eq!(file_client_id, db_client_id);
    assert_eq!(
        file_client_id,
        Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
            .ok()
            .unwrap()
    );

    glean.submit_ping_by_name("health", Some("pre_init"));
    let mut pending = get_queued_pings(temp.path()).unwrap();
    assert_eq!(1, pending.len());
    let payload = pending.pop().unwrap().1;

    let client_id = payload["client_info"]["client_id"].as_str();
    assert_eq!(
        Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
        client_id
    );
    let state = &payload["metrics"]["string"]["glean.health.exception_state"];
    assert_eq!(&serde_json::Value::Null, state);
    let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
        ["none"]
        .as_i64()
        .unwrap();
    assert_eq!(1, status);

    // Clearing out here so we can re-init using its directory later.
    drop(glean);

    // Remove the pending "health" ping.
    _ = fs::remove_dir_all(temp.path().join("pending_pings"));

    let backup = Some(Box::new(ClientIDBackup {
        client_id: backup_client_id.clone(),
        should_apply: false,
    }) as _);
    let (glean2, temp) = new_glean_with_backup(Some(temp), backup);
    let db_client_id = clientid_metric().get_value(&glean2, None).unwrap();
    let file_client_id = clientid_from_file(temp.path()).unwrap();

    assert_eq!(file_client_id, db_client_id);
    assert_eq!(
        file_client_id,
        Uuid::parse_str(backup_client_id.lock().unwrap().as_ref().unwrap())
            .ok()
            .unwrap()
    );

    glean2.submit_ping_by_name("health", Some("pre_init"));
    let mut pending = get_queued_pings(temp.path()).unwrap();
    assert_eq!(1, pending.len());
    let payload = pending.pop().unwrap().1;

    let client_id = payload["client_info"]["client_id"].as_str();
    assert_eq!(
        Some(backup_client_id.lock().unwrap().as_ref().unwrap().as_str()),
        client_id
    );
    let state = &payload["metrics"]["string"]["glean.health.exception_state"];
    assert_eq!(&serde_json::Value::Null, state);
    let status = payload["metrics"]["labeled_counter"]["glean.health.backup_client_id_status"]
        ["match"]
        .as_i64()
        .unwrap();
    assert_eq!(1, status);
}
