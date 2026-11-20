// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;

use std::io::Read;

use flate2::read::GzDecoder;
use jsonschema_valid::schemas::Draft;
use serde_json::Value;

use glean::net::{CapablePingUploadRequest, UploadResult};
use glean::private::*;
use glean::{ClientInfoMetrics, ConfigurationBuilder};

const SCHEMA_JSON: &str = include_str!("../../../glean.1.schema.json");

fn load_schema() -> Value {
    serde_json::from_str(SCHEMA_JSON).unwrap()
}

const GLOBAL_APPLICATION_ID: &str = "org.mozilla.glean.test.app";

// Define a fake uploader that reports back the submitted payload
// using a crossbeam channel.
#[derive(Debug)]
pub struct ValidatingUploader {
    sender: crossbeam_channel::Sender<Vec<u8>>,
}
impl glean::net::PingUploader for ValidatingUploader {
    fn upload(&self, ping_request: CapablePingUploadRequest) -> UploadResult {
        let ping_request = ping_request.capable(|_| true).unwrap();
        self.sender.send(ping_request.body).unwrap();
        UploadResult::http_status(200)
    }
}

#[test]
fn custom_ping_is_valid_when_upload_disabled() {
    let _ = env_logger::builder().try_init();

    let schema = load_schema();

    let (s, r) = crossbeam_channel::bounded::<Vec<u8>>(1);
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();
    let cfg = ConfigurationBuilder::new(false, tmpname, GLOBAL_APPLICATION_ID)
        .with_server_endpoint("invalid-test-host")
        .with_uploader(ValidatingUploader { sender: s })
        .build();

    let client_info = ClientInfoMetrics {
        app_build: env!("CARGO_PKG_VERSION").to_string(),
        app_display_version: env!("CARGO_PKG_VERSION").to_string(),
        channel: Some("testing".to_string()),
        locale: Some("xx-XX".to_string()),
        os_version: None,
    };

    glean::initialize(cfg, client_info);

    let custom_ping = PingType::new(
        "custom",
        true,
        true,
        true,
        true,
        true,
        vec![],
        vec![],
        false,
        vec![],
    );
    custom_ping.set_enabled(true);

    // Submit the custom ping.
    custom_ping.submit(None);

    // Wait for the ping to arrive.
    let raw_body = r.recv().unwrap();

    // Decode the gzipped body.
    let mut gzip_decoder = GzDecoder::new(&raw_body[..]);
    let mut s = String::with_capacity(raw_body.len());

    let data = gzip_decoder
        .read_to_string(&mut s)
        .ok()
        .map(|_| &s[..])
        .or_else(|| std::str::from_utf8(&raw_body).ok())
        .and_then(|payload| serde_json::from_str(payload).ok())
        .unwrap();

    // Now validate against the vendored schema
    let cfg = jsonschema_valid::Config::from_schema(&schema, Some(Draft::Draft6)).unwrap();
    let validation = cfg.validate(&data);
    match validation {
        Ok(()) => {}
        Err(errors) => {
            let mut msg = format!("Data: {data:#?}\n Errors:\n");
            for (idx, error) in errors.enumerate() {
                msg.push_str(&format!("Error {}: ", idx + 1));
                msg.push_str(&error.to_string());
                msg.push('\n');
            }
            panic!("{}", msg);
        }
    }
}
