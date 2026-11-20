// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// #[allow(dead_code)] is required on this module as a workaround for
// https://github.com/rust-lang/rust/issues/46379
#![allow(dead_code)]
use glean_core::{Glean, PingType, Result};

use std::fs::{read_dir, File};
use std::io::{BufRead, BufReader};
use std::path::Path;

use serde_json::Value as JsonValue;

use ctor::ctor;

/// Initialize the logger for all tests without individual tests requiring to call the init code.
/// Log output can be controlled via the environment variable `RUST_LOG` for the `glean_core` crate,
/// e.g.:
///
/// ```
/// export RUST_LOG=glean_core=debug
/// ```
#[ctor]
fn enable_test_logging() {
    // When testing we want all logs to go to stdout/stderr by default,
    // without requiring each individual test to activate it.
    // This only applies to glean-core tests, users of the main library still need to call
    // `glean_enable_logging` of the FFI component (automatically done by the platform wrappers).
    let _ = env_logger::builder().is_test(true).try_init();
}

pub fn tempdir() -> (tempfile::TempDir, String) {
    let t = tempfile::tempdir().unwrap();
    let name = t.path().display().to_string();
    (t, name)
}

pub const GLOBAL_APPLICATION_ID: &str = "org.mozilla.glean.test.app";

/// Creates a new instance of Glean with a temporary directory, with `upload_enabled` specified.
///
/// We need to keep the `TempDir` alive, so that it's not deleted before we stop using it.
pub fn new_glean_with_upload(
    tempdir: Option<tempfile::TempDir>,
    upload_enabled: bool,
) -> (Glean, tempfile::TempDir) {
    let dir = match tempdir {
        Some(tempdir) => tempdir,
        None => tempfile::tempdir().unwrap(),
    };

    let cfg = glean_core::InternalConfiguration {
        data_path: dir.path().display().to_string(),
        application_id: GLOBAL_APPLICATION_ID.into(),
        language_binding_name: "Rust".into(),
        upload_enabled,
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
        enable_store_submitted_pings: false,
    };
    let mut glean = Glean::new(cfg).unwrap();

    // store{1,2} is used throughout tests
    _ = new_test_ping(&mut glean, "store1");
    _ = new_test_ping(&mut glean, "store2");

    (glean, dir)
}

pub fn new_glean_delayed_io() -> (Glean, tempfile::TempDir) {
    let dir = tempfile::tempdir().unwrap();

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
    let mut glean = Glean::new(cfg).unwrap();
    _ = new_test_ping(&mut glean, "store1");

    (glean, dir)
}

/// Creates a new instance of Glean with a temporary directory.
///
/// We need to keep the `TempDir` alive, so that it's not deleted before we stop using it.
pub fn new_glean(tempdir: Option<tempfile::TempDir>) -> (Glean, tempfile::TempDir) {
    new_glean_with_upload(tempdir, true)
}

pub fn new_test_ping(glean: &mut Glean, name: &str) -> PingType {
    let ping = PingBuilder::new(name).build();
    glean.register_ping_type(&ping);
    ping
}

pub struct PingBuilder {
    name: String,
    include_client_id: bool,
    send_if_empty: bool,
    precise_timestamps: bool,
    include_info_sections: bool,
    enabled: bool,
    schedules_pings: Vec<String>,
    reason_codes: Vec<String>,
    follows_collection_enabled: bool,
    uploader_capabilities: Vec<String>,
}

impl PingBuilder {
    pub fn new(name: &str) -> Self {
        Self {
            name: name.to_string(),
            include_client_id: true,
            send_if_empty: false,
            precise_timestamps: true,
            include_info_sections: true,
            enabled: true,
            schedules_pings: vec![],
            reason_codes: vec![],
            follows_collection_enabled: true,
            uploader_capabilities: vec![],
        }
    }

    pub fn build(self) -> PingType {
        PingType::new(
            self.name,
            self.include_client_id,
            self.send_if_empty,
            self.precise_timestamps,
            self.include_info_sections,
            self.enabled,
            self.schedules_pings,
            self.reason_codes,
            self.follows_collection_enabled,
            self.uploader_capabilities,
        )
    }

    pub fn with_send_if_empty(mut self, value: bool) -> Self {
        self.send_if_empty = value;
        self
    }

    pub fn with_include_info_sections(mut self, value: bool) -> Self {
        self.include_info_sections = value;
        self
    }

    pub fn with_enabled(mut self, value: bool) -> Self {
        self.enabled = value;
        self
    }

    pub fn with_follows_collection_enabled(mut self, value: bool) -> Self {
        self.follows_collection_enabled = value;
        self
    }

    pub fn with_schedules_pings(mut self, value: Vec<String>) -> Self {
        self.schedules_pings = value;
        self
    }

    pub fn with_reasons(mut self, value: Vec<String>) -> Self {
        self.reason_codes = value;
        self
    }
}

/// Gets a vector of the currently queued pings.
///
/// # Arguments
///
/// * `data_path` - Glean's data path, as returned from Glean::get_data_path()
///
/// # Returns
///
/// A vector of all queued pings.
///
/// Each entry is a pair `(url, json_data, metadata)`,
/// where `url` is the endpoint the ping will go to, `json_data` is the JSON payload
/// and metadata is optional persisted data related to the ping.
pub fn get_queued_pings(data_path: &Path) -> Result<Vec<(String, JsonValue, Option<JsonValue>)>> {
    get_pings(&data_path.join("pending_pings"))
}

/// Gets a vector of the currently queued `deletion-request` pings.
///
/// # Arguments
///
/// * `data_path` - Glean's data path, as returned from Glean::get_data_path()
///
/// # Returns
///
/// A vector of all queued pings.
///
/// Each entry is a pair `(url, json_data, metadata)`,
/// where `url` is the endpoint the ping will go to, `json_data` is the JSON payload
/// and metadata is optional persisted data related to the ping.
pub fn get_deletion_pings(data_path: &Path) -> Result<Vec<(String, JsonValue, Option<JsonValue>)>> {
    get_pings(&data_path.join("deletion_request"))
}

fn get_pings(pings_dir: &Path) -> Result<Vec<(String, JsonValue, Option<JsonValue>)>> {
    let entries = read_dir(pings_dir)?;
    Ok(entries
        .filter_map(|entry| entry.ok())
        .filter(|entry| match entry.file_type() {
            Ok(file_type) => file_type.is_file(),
            Err(_) => false,
        })
        .filter_map(|entry| File::open(entry.path()).ok())
        .filter_map(|file| {
            let mut lines = BufReader::new(file).lines();
            if let (Some(Ok(url)), Some(Ok(body)), Ok(metadata)) =
                (lines.next(), lines.next(), lines.next().transpose())
            {
                let parsed_metadata = metadata.map(|m| {
                    serde_json::from_str::<JsonValue>(&m).expect("metadata should be valid JSON")
                });
                if let Ok(parsed_body) = serde_json::from_str::<JsonValue>(&body) {
                    Some((url, parsed_body, parsed_metadata))
                } else {
                    None
                }
            } else {
                None
            }
        })
        .collect())
}
