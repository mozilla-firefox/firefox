// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;
use crate::common::*;

use std::collections::HashMap;
use std::io::Read;

use flate2::read::GzDecoder;
use jsonschema_valid::schemas::Draft;
use serde_json::Value;

use glean::net::{CapablePingUploadRequest, UploadResult};
use glean::private::*;
use glean::AttributionMetrics;
use glean::LabeledMetricData;
use glean::{
    traits, ClientInfoMetrics, CommonMetricData, ConfigurationBuilder, HistogramType, MemoryUnit,
    TimeUnit,
};

const SCHEMA_JSON: &str = include_str!("../../../glean.1.schema.json");

fn load_schema() -> Value {
    serde_json::from_str(SCHEMA_JSON).unwrap()
}

const GLOBAL_APPLICATION_ID: &str = "org.mozilla.glean.test.app";

struct SomeExtras {
    extra1: Option<String>,
    extra2: Option<bool>,
}

impl traits::ExtraKeys for SomeExtras {
    const ALLOWED_KEYS: &'static [&'static str] = &["extra1", "extra2"];

    fn into_ffi_extra(self) -> HashMap<String, String> {
        let mut map = HashMap::new();

        self.extra1
            .and_then(|val| map.insert("extra1".to_string(), val));
        self.extra2
            .and_then(|val| map.insert("extra2".to_string(), val.to_string()));

        map
    }
}

#[test]
fn validate_against_schema() {
    let _ = env_logger::builder().try_init();

    let schema = load_schema();

    let (s, r) = crossbeam_channel::bounded::<Vec<u8>>(1);

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

    // Create a custom configuration to use a validating uploader.
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().to_path_buf();

    let cfg = ConfigurationBuilder::new(true, tmpname, GLOBAL_APPLICATION_ID)
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

    const PING_NAME: &str = "test-ping";

    let common = |name: &str| CommonMetricData {
        category: "test".into(),
        name: name.into(),
        send_in_pings: vec![PING_NAME.into()],
        ..Default::default()
    };

    // Test each of the metric types, just for basic smoke testing against the
    // schema

    // TODO: 1695762 Test all of the metric types against the schema from Rust

    let counter_metric = CounterMetric::new(common("counter"));
    counter_metric.add(42);

    let bool_metric = BooleanMetric::new(common("bool"));
    bool_metric.set(true);

    let string_metric = StringMetric::new(common("string"));
    string_metric.set("test".into());

    let stringlist_metric = StringListMetric::new(common("stringlist"));
    stringlist_metric.add("one".into());
    stringlist_metric.add("two".into());

    // Let's make sure an empty array is accepted.
    let stringlist_metric2 = StringListMetric::new(common("stringlist2"));
    stringlist_metric2.set(vec![]);

    let timespan_metric = TimespanMetric::new(common("timespan"), TimeUnit::Nanosecond);
    timespan_metric.start();
    timespan_metric.stop();

    let timing_dist = TimingDistributionMetric::new(common("timing_dist"), TimeUnit::Nanosecond);
    let id = timing_dist.start();
    timing_dist.stop_and_accumulate(id);

    let memory_dist = MemoryDistributionMetric::new(common("memory_dist"), MemoryUnit::Byte);
    memory_dist.accumulate(100);

    let uuid_metric = UuidMetric::new(common("uuid"));
    // chosen by fair dic roll (`uuidgen`)
    uuid_metric.set("3ee4db5f-ee26-4557-9a66-bc7425d7893f".into());

    // We can't test the URL metric,
    // because the regex used in the schema uses a negative lookahead,
    // which the regex crate doesn't handle.
    //
    //let url_metric = UrlMetric::new(common("url"));
    //url_metric.set("https://mozilla.github.io/glean/");

    let datetime_metric = DatetimeMetric::new(common("datetime"), TimeUnit::Day);
    datetime_metric.set(None);

    let event_metric = EventMetric::<SomeExtras>::new(common("event"));
    event_metric.record(None);
    event_metric.record(SomeExtras {
        extra1: Some("test".into()),
        extra2: Some(false),
    });

    let custom_dist =
        CustomDistributionMetric::new(common("custom_dist"), 1, 100, 100, HistogramType::Linear);
    custom_dist.accumulate_samples(vec![50, 51]);

    let quantity_metric = QuantityMetric::new(common("quantity"));
    quantity_metric.set(0);

    let rate_metric = RateMetric::new(common("rate"));
    rate_metric.add_to_numerator(1);
    rate_metric.add_to_denominator(1);

    let numerator_metric1 = NumeratorMetric::new(common("num1"));
    let numerator_metric2 = NumeratorMetric::new(common("num2"));
    let denominator_metric =
        DenominatorMetric::new(common("den"), vec![common("num1"), common("num2")]);

    numerator_metric1.add_to_numerator(1);
    numerator_metric2.add_to_numerator(2);
    denominator_metric.add(3);

    let text_metric = TextMetric::new(common("text"));
    text_metric.set("loooooong text".repeat(100));

    let labeled_bool = LabeledMetric::<BooleanMetric>::new(
        LabeledMetricData::Common {
            cmd: common("labeled_bool"),
        },
        None,
    );
    labeled_bool.get("label1").set(true);

    let labeled_counter = LabeledMetric::<CounterMetric>::new(
        LabeledMetricData::Common {
            cmd: common("labeled_counter"),
        },
        None,
    );
    labeled_counter.get("label1").add(1);
    // empty labels are fine
    labeled_counter.get("").add(42);

    let labeled_custom_dist = LabeledMetric::<CustomDistributionMetric>::new(
        LabeledMetricData::CustomDistribution {
            cmd: common("labeled_custom_dist"),
            range_min: 0,
            range_max: 100,
            bucket_count: 100,
            histogram_type: HistogramType::Linear,
        },
        None,
    );
    labeled_custom_dist
        .get("label1")
        .accumulate_single_sample(1);

    // string JUST outside the limits, forcing this to record an error.
    let term = "a".repeat(255) + "b";
    glean::update_attribution(AttributionMetrics {
        source: Some("rlb-tests".into()),
        medium: Some("cargo-test".into()),
        campaign: Some("testing".into()),
        term: Some(term),
        content: None,
    });

    // Populate every field so `ping_info.server_knobs_config` is validated too.
    // Field coverage is checked in `glean-core/tests/metrics_sync.rs`.
    glean::glean_apply_server_knobs_config(
        serde_json::json!({
            "metrics_enabled": { "test.counter": true },
            "pings_enabled": { PING_NAME: true },
            "event_threshold": 500,
            "session_sample_rate": 1.0,
            "events_ping_acceleration_factor": 5,
        })
        .to_string(),
    );

    // Define a new ping and submit it.
    let custom_ping = PingBuilder::new(PING_NAME).with_send_if_empty(true).build();
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
