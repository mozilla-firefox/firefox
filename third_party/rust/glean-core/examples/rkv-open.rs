// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::PathBuf;
use std::{collections::HashMap, env};

use glean_core::{
    glean_initialize, glean_set_test_mode, ClientInfoMetrics, InternalConfiguration, OnGleanEvents,
};

struct EventHandler;

impl OnGleanEvents for EventHandler {
    fn initialize_finished(&self) {}

    fn trigger_upload(&self) -> glean_core::Result<(), glean_core::CallbackError> {
        Ok(())
    }

    fn start_metrics_ping_scheduler(&self) -> bool {
        false
    }

    fn cancel_uploads(&self) -> glean_core::Result<(), glean_core::CallbackError> {
        Ok(())
    }
}

fn main() {
    env_logger::init();

    let mut args = env::args().skip(1);
    let data_path = PathBuf::from(args.next().expect("need data path"));

    let config = InternalConfiguration {
        upload_enabled: true,
        data_path: data_path.display().to_string(),
        application_id: "rkv.open.test".to_string(),
        language_binding_name: "rust".to_string(),
        max_events: None,
        delay_ping_lifetime_io: false,
        app_build: "0".to_string(),
        use_core_mps: false,
        trim_data_to_registered_pings: false,
        log_level: None,
        rate_limit: None,
        enable_event_timestamps: true,
        experimentation_id: None,
        enable_internal_pings: true,
        ping_schedule: HashMap::default(),
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

    let client_info = ClientInfoMetrics::unknown();

    glean_set_test_mode(true);
    glean_initialize(config, client_info, Box::new(EventHandler), None);
}
