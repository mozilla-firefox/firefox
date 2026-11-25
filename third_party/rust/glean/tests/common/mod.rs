// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// #[allow(dead_code)] is required on this module as a workaround for
// https://github.com/rust-lang/rust/issues/46379
#![allow(dead_code)]

use std::{panic, process};

use glean::{private::PingType, ClientInfoMetrics, Configuration};

/// Initialize the env logger for a test environment.
///
/// When testing we want all logs to go to stdout/stderr by default.
pub fn enable_test_logging() {
    let _ = env_logger::builder().is_test(true).try_init();
}

/// Install a panic handler that exits the whole process when a panic occurs.
///
/// This causes the process to exit even if a thread panics.
/// This is similar to the `panic=abort` configuration, but works in the default configuration
/// (as used by `cargo test`).
fn install_panic_handler() {
    let orig_hook = panic::take_hook();
    panic::set_hook(Box::new(move |panic_info| {
        // invoke the default handler and exit the process
        orig_hook(panic_info);
        process::exit(1);
    }));
}

/// Create a new instance of Glean.
pub fn initialize(cfg: Configuration) {
    // Ensure panics in threads, such as the init thread or the dispatcher, cause the process to
    // exit.
    //
    // Otherwise in case of a panic in a thread the integration test will just hang.
    // CI will terminate it after a timeout, but why stick around if we know nothing is happening?
    install_panic_handler();

    // Use some default values to make our life easier a bit.
    let client_info = ClientInfoMetrics {
        app_build: "1.0.0".to_string(),
        app_display_version: "1.0.0".to_string(),
        channel: Some("testing".to_string()),
        locale: Some("xx-XX".to_string()),
        os_version: None,
    };

    _ = PingBuilder::new("store1").with_send_if_empty(true).build();
    glean::initialize(cfg, client_info, None);
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

    pub fn with_uploader_capabilities(mut self, value: Vec<String>) -> Self {
        self.uploader_capabilities = value;
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

    pub fn with_include_client_id(mut self, value: bool) -> Self {
        self.include_client_id = value;
        self
    }
}
