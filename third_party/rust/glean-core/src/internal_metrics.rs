// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::borrow::Cow;

use super::{metrics::*, CommonMetricData, LabeledMetricData, Lifetime};

#[derive(Debug)]
pub struct CoreMetrics {
    pub client_id: UuidMetric,
    pub first_run_date: DatetimeMetric,
    pub os: StringMetric,
    pub attribution_source: StringMetric,
    pub attribution_medium: StringMetric,
    pub attribution_campaign: StringMetric,
    pub attribution_term: StringMetric,
    pub attribution_content: StringMetric,
    pub distribution_name: StringMetric,
}

#[derive(Debug)]
pub struct AdditionalMetrics {
    /// The number of times we encountered an IO error
    /// when writing a pending ping to disk.
    pub io_errors: CounterMetric,

    /// A count of the pings submitted, by ping type.
    pub pings_submitted: LabeledMetric<CounterMetric>,

    /// Time waited for the uploader at shutdown.
    pub shutdown_wait: TimingDistributionMetric,

    /// Time waited for the dispatcher to unblock during shutdown.
    pub shutdown_dispatcher_wait: TimingDistributionMetric,

    /// An experimentation identifier derived and provided by the application
    /// for the purpose of experimentation enrollment.
    pub experimentation_id: StringMetric,
}

impl CoreMetrics {
    pub fn new() -> CoreMetrics {
        CoreMetrics {
            client_id: UuidMetric::new(CommonMetricData {
                name: "client_id".into(),
                category: "".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),

            first_run_date: DatetimeMetric::new(
                CommonMetricData {
                    name: "first_run_date".into(),
                    category: "".into(),
                    send_in_pings: vec!["glean_client_info".into()],
                    lifetime: Lifetime::User,
                    disabled: false,
                    dynamic_label: None,
                },
                TimeUnit::Day,
            ),

            os: StringMetric::new(CommonMetricData {
                name: "os".into(),
                category: "".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::Application,
                disabled: false,
                dynamic_label: None,
            }),

            attribution_source: StringMetric::new(CommonMetricData {
                name: "source".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),

            attribution_medium: StringMetric::new(CommonMetricData {
                name: "medium".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),

            attribution_campaign: StringMetric::new(CommonMetricData {
                name: "campaign".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),

            attribution_term: StringMetric::new(CommonMetricData {
                name: "term".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),

            attribution_content: StringMetric::new(CommonMetricData {
                name: "content".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),

            distribution_name: StringMetric::new(CommonMetricData {
                name: "name".into(),
                category: "distribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                disabled: false,
                dynamic_label: None,
            }),
        }
    }
}

impl AdditionalMetrics {
    pub fn new() -> AdditionalMetrics {
        AdditionalMetrics {
            io_errors: CounterMetric::new(CommonMetricData {
                name: "io".into(),
                category: "glean.error".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                disabled: false,
                dynamic_label: None,
            }),

            pings_submitted: LabeledMetric::<CounterMetric>::new(
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "pings_submitted".into(),
                        category: "glean.validation".into(),
                        send_in_pings: vec!["metrics".into(), "baseline".into()],
                        lifetime: Lifetime::Ping,
                        disabled: false,
                        dynamic_label: None,
                    },
                },
                None,
            ),

            shutdown_wait: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "shutdown_wait".into(),
                    category: "glean.validation".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                TimeUnit::Millisecond,
            ),

            shutdown_dispatcher_wait: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "shutdown_dispatcher_wait".into(),
                    category: "glean.validation".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                TimeUnit::Millisecond,
            ),

            // This uses a `send_in_pings` that contains "all-ping".
            // This works because all of our other current "all-pings" metrics
            // have special handling internally and are not actually processed
            // into a store quite like this identifier is.
            //
            // This could become an issue if we ever decide to start generating
            // code from the internal Glean metrics.yaml (there aren't currently
            // any plans for this).
            experimentation_id: StringMetric::new(CommonMetricData {
                name: "experimentation_id".into(),
                category: "glean.client.annotation".into(),
                send_in_pings: vec!["all-pings".into()],
                lifetime: Lifetime::Application,
                disabled: false,
                dynamic_label: None,
            }),
        }
    }
}

#[derive(Debug)]
pub struct UploadMetrics {
    pub ping_upload_failure: LabeledMetric<CounterMetric>,
    pub discarded_exceeding_pings_size: MemoryDistributionMetric,
    pub pending_pings_directory_size: MemoryDistributionMetric,
    pub deleted_pings_after_quota_hit: CounterMetric,
    pub pending_pings: CounterMetric,
    pub send_success: TimingDistributionMetric,
    pub send_failure: TimingDistributionMetric,
    pub in_flight_pings_dropped: CounterMetric,
    pub missing_send_ids: CounterMetric,
}

impl UploadMetrics {
    pub fn new() -> UploadMetrics {
        UploadMetrics {
            ping_upload_failure: LabeledMetric::<CounterMetric>::new(
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "ping_upload_failure".into(),
                        category: "glean.upload".into(),
                        send_in_pings: vec!["metrics".into()],
                        lifetime: Lifetime::Ping,
                        disabled: false,
                        dynamic_label: None,
                    },
                },
                Some(vec![
                    Cow::from("status_code_4xx"),
                    Cow::from("status_code_5xx"),
                    Cow::from("status_code_unknown"),
                    Cow::from("unrecoverable"),
                    Cow::from("recoverable"),
                    Cow::from("incapable"),
                ]),
            ),

            discarded_exceeding_pings_size: MemoryDistributionMetric::new(
                CommonMetricData {
                    name: "discarded_exceeding_ping_size".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                MemoryUnit::Kilobyte,
            ),

            pending_pings_directory_size: MemoryDistributionMetric::new(
                CommonMetricData {
                    name: "pending_pings_directory_size".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                MemoryUnit::Kilobyte,
            ),

            deleted_pings_after_quota_hit: CounterMetric::new(CommonMetricData {
                name: "deleted_pings_after_quota_hit".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                disabled: false,
                dynamic_label: None,
            }),

            pending_pings: CounterMetric::new(CommonMetricData {
                name: "pending_pings".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                disabled: false,
                dynamic_label: None,
            }),

            send_success: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "send_success".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                TimeUnit::Millisecond,
            ),

            send_failure: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "send_failure".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                TimeUnit::Millisecond,
            ),

            in_flight_pings_dropped: CounterMetric::new(CommonMetricData {
                name: "in_flight_pings_dropped".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                disabled: false,
                dynamic_label: None,
            }),

            missing_send_ids: CounterMetric::new(CommonMetricData {
                name: "missing_send_ids".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                disabled: false,
                dynamic_label: None,
            }),
        }
    }
}

#[derive(Debug)]
pub struct DatabaseMetrics {
    pub size: MemoryDistributionMetric,

    /// RKV's load result, indicating success or relaying the detected error.
    pub rkv_load_error: StringMetric,

    /// The time it takes for a write-commit for the Glean database.
    pub write_time: TimingDistributionMetric,
}

impl DatabaseMetrics {
    pub fn new() -> DatabaseMetrics {
        DatabaseMetrics {
            size: MemoryDistributionMetric::new(
                CommonMetricData {
                    name: "size".into(),
                    category: "glean.database".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: false,
                    dynamic_label: None,
                },
                MemoryUnit::Byte,
            ),

            rkv_load_error: StringMetric::new(CommonMetricData {
                name: "rkv_load_error".into(),
                category: "glean.error".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                disabled: false,
                dynamic_label: None,
            }),

            write_time: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "write_time".into(),
                    category: "glean.database".into(),
                    send_in_pings: vec!["metrics".into()],
                    lifetime: Lifetime::Ping,
                    disabled: true,
                    dynamic_label: None,
                },
                TimeUnit::Microsecond,
            ),
        }
    }
}
