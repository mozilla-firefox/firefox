// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#![cfg(feature = "sqlite")]

mod common;
use std::fs;

use crate::common::*;

use glean_core::metrics::*;
use glean_core::CommonMetricData;
use glean_core::Glean;
use glean_core::Lifetime;
use glean_core::SessionMode;
use rusqlite::params;
use rusqlite::TransactionBehavior;
use uuid::uuid;

fn clientid_metric() -> UuidMetric {
    UuidMetric::new(CommonMetricData {
        name: "client_id".into(),
        category: "".into(),
        send_in_pings: vec!["glean_client_info".into()],
        lifetime: Lifetime::User,
        ..Default::default()
    })
}

fn load_error_metric() -> StringMetric {
    StringMetric::new(CommonMetricData {
        name: "load_error".into(),
        category: "glean.database".into(),
        send_in_pings: vec!["metrics".into(), "health".into()],
        lifetime: Lifetime::Ping,
        ..Default::default()
    })
}

#[test]
fn database_file_is_not_sqlite() {
    let temp = {
        let (glean, temp) = new_glean(None);
        drop(glean);
        temp
    };

    {
        let path = temp.path().join("db").join("glean.sqlite");
        fs::remove_file(&path).unwrap();
        fs::write(&path, "not sqlite").unwrap();
    }

    let (glean, _temp) = new_glean(Some(temp));

    let client_id = clientid_metric().get_value(&glean, None);
    assert!(client_id.is_some());

    let load_error = load_error_metric().get_value(&glean, None).unwrap();
    assert_eq!("database file corrupt", load_error);
}

#[test]
fn database_contains_wrong_table() {
    let temp = {
        let (glean, temp) = new_glean(None);
        drop(glean);
        temp
    };

    {
        let path = temp.path().join("db").join("glean.sqlite");
        fs::remove_file(&path).unwrap();

        let conn = rusqlite::Connection::open(path).unwrap();
        conn.execute("CREATE TABLE telemetry (a TEXT)", ()).unwrap();
    }

    let (glean, _temp) = new_glean(Some(temp));

    let client_id = clientid_metric().get_value(&glean, None);
    assert!(client_id.is_some());

    let load_error = load_error_metric().get_value(&glean, None).unwrap();
    assert!(load_error.starts_with("sql error:"));
}

#[test]
fn database_contains_correct_user_version_but_wrong_table() {
    let temp = {
        let (glean, temp) = new_glean(None);
        drop(glean);
        temp
    };

    {
        let path = temp.path().join("db").join("glean.sqlite");
        let conn = rusqlite::Connection::open(path).unwrap();
        conn.execute("DROP TABLE telemetry", ()).unwrap();
        conn.execute("CREATE TABLE telemetry (a TEXT)", ()).unwrap();
    }

    let (glean, _temp) = new_glean(Some(temp));

    let client_id = clientid_metric().get_value(&glean, None);
    assert!(client_id.is_some());

    let load_error = load_error_metric().get_value(&glean, None).unwrap();
    assert!(load_error.starts_with("sql error:"));
}

#[test]
fn invalid_msgpack_value() {
    let (first_client_id, temp) = {
        let (glean, temp) = new_glean(None);
        let client_id = clientid_metric().get_value(&glean, None).unwrap();
        drop(glean);
        (client_id, temp)
    };

    {
        let path = temp.path().join("db").join("glean.sqlite");
        let conn = rusqlite::Connection::open(path).unwrap();
        conn.execute(
            "UPDATE telemetry SET value = ?1 WHERE id = 'client_id'",
            params![b"c0ffeec0-ffee-c0ff-eec0-ffeec0ffeec0"],
        )
        .unwrap();

        // Also remove the client_id.txt so the client_id is not re-set from it.
        fs::remove_file(temp.path().join("client_id.txt")).unwrap();
    }

    let (glean, _temp) = new_glean(Some(temp));

    let client_id = clientid_metric().get_value(&glean, None).unwrap();
    let known_id = uuid!("c0ffeec0-ffee-c0ff-eec0-ffeec0ffeec0");
    assert_ne!(known_id, client_id);
    assert_ne!(first_client_id, client_id);
}

#[test]
fn higher_user_version_upgrade_does_not_crash() {
    let (first_client_id, temp) = {
        let (glean, temp) = new_glean(None);
        let client_id = clientid_metric().get_value(&glean, None).unwrap();
        drop(glean);
        (client_id, temp)
    };

    {
        let path = temp.path().join("db").join("glean.sqlite");
        let conn = rusqlite::Connection::open(path).unwrap();
        conn.execute_batch("PRAGMA user_version = 99").unwrap();
    }

    let (glean, _temp) = new_glean(Some(temp));

    let client_id = clientid_metric().get_value(&glean, None).unwrap();
    assert_eq!(first_client_id, client_id);

    let load_error = load_error_metric().get_value(&glean, None);
    assert!(load_error.is_none());
}

// Permissions only really work on Unix systems, definitely not on Windows
#[cfg(unix)]
mod unix {
    use super::*;

    #[test]
    fn database_permission_error() {
        let temp = tempfile::tempdir().unwrap();

        let db_path = temp.path().join("db");
        fs::create_dir_all(&db_path).unwrap();
        let path = db_path.join("glean.sqlite");
        fs::write(&path, "").unwrap();
        let attr = fs::metadata(&path).unwrap();
        let original_permissions = attr.permissions();
        let mut permissions = original_permissions.clone();
        permissions.set_readonly(true);
        fs::set_permissions(&path, permissions).unwrap();

        let cfg = glean_core::InternalConfiguration {
            data_path: path.display().to_string(),
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
            session_mode: glean_core::SessionMode::Auto,
            session_sample_rate: 1.0,
            session_inactivity_timeout_ms: 1_800_000,
            events_ping_acceleration_factor: None,
            enable_store_submitted_pings: true,
        };
        let glean = Glean::new(cfg, None);
        assert!(glean.is_err());
    }
}

#[test]
fn database_externally_locked() {
    // This is NOT the usual case.
    // But if the database is already locked, there's little we can do.
    // This behavior is the same as if we don't have permissions to access the database file.

    let temp = {
        let (glean, temp) = new_glean(None);
        drop(glean);
        temp
    };

    let path = temp.path().join("db").join("glean.sqlite");
    let mut conn = rusqlite::Connection::open(&path).unwrap();
    let _tx = conn
        .transaction_with_behavior(TransactionBehavior::Immediate)
        .unwrap();

    let cfg = glean_core::InternalConfiguration {
        data_path: path.display().to_string(),
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
        session_inactivity_timeout_ms: 0,
        session_mode: SessionMode::Auto,
        session_sample_rate: 1.0,
        events_ping_acceleration_factor: None,
        enable_store_submitted_pings: true,
    };
    let glean = Glean::new(cfg, None);
    assert!(glean.is_err());
}

#[test]
fn latest_schema_is_applied() {
    let (first_client_id, temp) = {
        let (glean, temp) = new_glean(None);
        let client_id = clientid_metric().get_value(&glean, None).unwrap();
        drop(glean);

        let db_path = temp.path().join("db").join("glean.sqlite");
        let conn = rusqlite::Connection::open(db_path).unwrap();

        conn.execute("DROP TABLE migration", []).unwrap();

        // Reset to first schema version
        conn.execute("PRAGMA user_version = 1", []).unwrap();

        (client_id, temp)
    };

    let db_path = temp.path().join("db").join("glean.sqlite");
    let (glean, _temp) = new_glean(Some(temp));

    let client_id = clientid_metric().get_value(&glean, None).unwrap();
    assert_eq!(first_client_id, client_id);

    let conn = rusqlite::Connection::open(db_path).unwrap();
    let cur_user_version: u32 = conn
        .query_one("PRAGMA user_version", [], |row| row.get(0))
        .unwrap();
    assert_eq!(cur_user_version, 3);

    let migration_state: String = conn
        .query_one("SELECT state FROM migration", [], |row| row.get(0))
        .unwrap();
    assert_eq!(migration_state, "done");
}
