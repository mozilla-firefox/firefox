// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::borrow::Cow;

use malloc_size_of_derive::MallocSizeOf;
use serde::Serialize;

use super::{metrics::*, CommonMetricData, LabeledMetricData, Lifetime};

#[derive(Debug, MallocSizeOf)]
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

#[derive(Debug, MallocSizeOf)]
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

    /// The number of times we had to clamp an event timestamp
    /// for exceeding the range of a signed 64-bit integer (9223372036854775807).
    pub event_timestamp_clamped: CounterMetric,

    /// Server knobs configuration received from remote settings.
    pub server_knobs_config: ObjectMetric,

    /// The total number of sessions started during the current metrics ping
    /// window, regardless of sampling outcome.
    pub sessions_seen: CounterMetric,
}

impl CoreMetrics {
    pub fn new() -> CoreMetrics {
        CoreMetrics {
            client_id: UuidMetric::new(CommonMetricData {
                name: "client_id".into(),
                category: "".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),

            first_run_date: DatetimeMetric::new(
                CommonMetricData {
                    name: "first_run_date".into(),
                    category: "".into(),
                    send_in_pings: vec!["glean_client_info".into()],
                    lifetime: Lifetime::User,
                    ..Default::default()
                },
                TimeUnit::Day,
            ),

            os: StringMetric::new(CommonMetricData {
                name: "os".into(),
                category: "".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::Application,
                ..Default::default()
            }),

            attribution_source: StringMetric::new(CommonMetricData {
                name: "source".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),

            attribution_medium: StringMetric::new(CommonMetricData {
                name: "medium".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),

            attribution_campaign: StringMetric::new(CommonMetricData {
                name: "campaign".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),

            attribution_term: StringMetric::new(CommonMetricData {
                name: "term".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),

            attribution_content: StringMetric::new(CommonMetricData {
                name: "content".into(),
                category: "attribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),

            distribution_name: StringMetric::new(CommonMetricData {
                name: "name".into(),
                category: "distribution".into(),
                send_in_pings: vec!["glean_client_info".into()],
                lifetime: Lifetime::User,
                ..Default::default()
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
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            pings_submitted: LabeledMetric::<CounterMetric>::new(
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "pings_submitted".into(),
                        category: "glean.validation".into(),
                        send_in_pings: vec!["metrics".into(), "baseline".into(), "health".into()],
                        lifetime: Lifetime::Ping,
                        ..Default::default()
                    },
                },
                None,
            ),

            shutdown_wait: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "shutdown_wait".into(),
                    category: "glean.validation".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                TimeUnit::Millisecond,
            ),

            shutdown_dispatcher_wait: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "shutdown_dispatcher_wait".into(),
                    category: "glean.validation".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
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
                ..Default::default()
            }),

            event_timestamp_clamped: CounterMetric::new(CommonMetricData {
                name: "event_timestamp_clamped".into(),
                category: "glean.error".into(),
                send_in_pings: vec!["health".into()],
                lifetime: Lifetime::Ping,
                disabled: true,
                ..Default::default()
            }),

            sessions_seen: CounterMetric::new(CommonMetricData {
                name: "sessions_seen".into(),
                category: "glean".into(),
                send_in_pings: vec!["metrics".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            server_knobs_config: ObjectMetric::new(CommonMetricData {
                name: "server_knobs_config".into(),
                category: "glean.internal.metrics".into(),
                send_in_pings: vec!["glean_internal_info".into()],
                lifetime: Lifetime::Application,
                ..Default::default()
            }),
        }
    }
}

#[derive(Debug, MallocSizeOf)]
pub struct UploadMetrics {
    pub ping_upload_failure: LabeledMetric<CounterMetric>,
    pub discarded_exceeding_pings_size: MemoryDistributionMetric,
    pub pending_pings_directory_size: MemoryDistributionMetric,
    pub deleted_pings_after_quota_hit: CounterMetric,
    pub pending_pings_deleted: LabeledMetric<CounterMetric>,
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
                        send_in_pings: vec!["metrics".into(), "health".into()],
                        lifetime: Lifetime::Ping,
                        ..Default::default()
                    },
                },
                Some(vec![
                    Cow::from("status_code_4xx"),
                    Cow::from("status_code_408"),
                    Cow::from("status_code_429"),
                    Cow::from("status_code_5xx"),
                    Cow::from("status_code_unknown"),
                    Cow::from("unrecoverable"),
                    Cow::from("recoverable"),
                    Cow::from("incapable"),
                ]),
            ),

            discarded_exceeding_pings_size: MemoryDistributionMetric::new(
                CommonMetricData {
                    name: "discarded_exceeding_pings_size".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                MemoryUnit::Kilobyte,
            ),

            pending_pings_directory_size: MemoryDistributionMetric::new(
                CommonMetricData {
                    name: "pending_pings_directory_size".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                MemoryUnit::Kilobyte,
            ),

            deleted_pings_after_quota_hit: CounterMetric::new(CommonMetricData {
                name: "deleted_pings_after_quota_hit".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            pending_pings_deleted: LabeledMetric::<CounterMetric>::new(
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "pending_pings_deleted".into(),
                        category: "glean.upload".into(),
                        send_in_pings: vec!["health".into()],
                        lifetime: Lifetime::Ping,
                        disabled: false,
                        ..Default::default()
                    },
                },
                Some(vec![Cow::from("count_quota"), Cow::from("size_quota")]),
            ),

            pending_pings: CounterMetric::new(CommonMetricData {
                name: "pending_pings".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            send_success: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "send_success".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                TimeUnit::Millisecond,
            ),

            send_failure: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "send_failure".into(),
                    category: "glean.upload".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                TimeUnit::Millisecond,
            ),

            in_flight_pings_dropped: CounterMetric::new(CommonMetricData {
                name: "in_flight_pings_dropped".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            missing_send_ids: CounterMetric::new(CommonMetricData {
                name: "missing_send_ids".into(),
                category: "glean.upload".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),
        }
    }
}

#[derive(Debug, MallocSizeOf)]
pub struct DatabaseMetrics {
    pub size: MemoryDistributionMetric,

    /// sqlite's load result, indicating success or relaying the detected error.
    pub load_error: StringMetric,

    /// Rkv's load result, indicating success or relaying the detected error.
    pub rkv_load_error: StringMetric,

    /// The time it takes for a write-commit for the Glean database.
    pub write_time: TimingDistributionMetric,

    /// The number of metrics migrated from Rkv storage to SQLite storage
    pub migrated_metrics: CounterMetric,

    /// The number of metrics stored in SQLite after a migration run
    pub metrics_in_sqlite: CounterMetric,

    /// Number of metrics that failed to deserialize from storage
    /// while iterating the Rkv database for migration.
    pub failed_metrics: CounterMetric,

    /// The duration for one full migration run at startup
    pub migration_duration: TimingDistributionMetric,

    /// Number of times a migration was attempted and failed
    pub migration_error: CounterMetric,
}

impl DatabaseMetrics {
    pub fn new() -> DatabaseMetrics {
        DatabaseMetrics {
            size: MemoryDistributionMetric::new(
                CommonMetricData {
                    name: "size".into(),
                    category: "glean.database".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                MemoryUnit::Byte,
            ),

            load_error: StringMetric::new(CommonMetricData {
                name: "load_error".into(),
                category: "glean.database".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            rkv_load_error: StringMetric::new(CommonMetricData {
                name: "rkv_load_error".into(),
                category: "glean.database".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            write_time: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "write_time".into(),
                    category: "glean.database".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    disabled: true,
                    ..Default::default()
                },
                TimeUnit::Microsecond,
            ),

            migrated_metrics: CounterMetric::new(CommonMetricData {
                name: "migrated_metrics".into(),
                category: "glean.migration".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            metrics_in_sqlite: CounterMetric::new(CommonMetricData {
                name: "metrics_in_sqlite".into(),
                category: "glean.migration".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            failed_metrics: CounterMetric::new(CommonMetricData {
                name: "failed_metrics".into(),
                category: "glean.migration".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),

            migration_duration: TimingDistributionMetric::new(
                CommonMetricData {
                    name: "migration_duration".into(),
                    category: "glean.migration".into(),
                    send_in_pings: vec!["metrics".into(), "health".into()],
                    lifetime: Lifetime::Ping,
                    ..Default::default()
                },
                TimeUnit::Millisecond,
            ),

            migration_error: CounterMetric::new(CommonMetricData {
                name: "error".into(),
                category: "glean.migration".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),
        }
    }
}

/// Possible values for the `glean.health.exception_state` health metric.
pub enum ExceptionState {
    /// No database on disk, but the plaintext file contained a valid client ID.
    EmptyDb,
    /// Existing database, but no client ID, however a client ID in the plaintext file.
    RegenDb,
    /// The database contained a c0ffee client ID.
    C0ffeeInDb,
    /// The client IDs in the database and the plaintext file differ.
    ClientIdMismatch,
}

impl From<ExceptionState> for String {
    fn from(value: ExceptionState) -> Self {
        use ExceptionState::*;
        String::from(match value {
            EmptyDb => "empty-db",
            RegenDb => "regen-db",
            C0ffeeInDb => "c0ffee-in-db",
            ClientIdMismatch => "client-id-mismatch",
        })
    }
}

#[derive(Debug, MallocSizeOf)]
pub struct HealthMetrics {
    // Information about the data directory prior to Glean initialization.
    pub data_directory_info: ObjectMetric,
    // A running count of the number of initializations.
    pub init_count: CounterMetric,

    // An exceptional state was detected upon trying to laod the database.
    pub exception_state: StringMetric,
    // A client_id recovered from a `client_id.txt` file on disk.
    pub recovered_client_id: UuidMetric,

    pub file_read_error: LabeledCounter,
    pub file_write_error: LabeledCounter,
}

impl HealthMetrics {
    pub fn new() -> HealthMetrics {
        HealthMetrics {
            data_directory_info: ObjectMetric::new(CommonMetricData {
                name: "data_directory_info".into(),
                category: "glean.health".into(),
                send_in_pings: vec!["metrics".into(), "health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),
            init_count: CounterMetric::new(CommonMetricData {
                name: "init_count".into(),
                category: "glean.health".into(),
                send_in_pings: vec!["health".into()],
                lifetime: Lifetime::User,
                ..Default::default()
            }),
            exception_state: StringMetric::new(CommonMetricData {
                name: "exception_state".into(),
                category: "glean.health".into(),
                send_in_pings: vec!["health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),
            recovered_client_id: UuidMetric::new(CommonMetricData {
                name: "recovered_client_id".into(),
                category: "glean.health".into(),
                send_in_pings: vec!["health".into()],
                lifetime: Lifetime::Ping,
                ..Default::default()
            }),
            file_read_error: LabeledMetric::<CounterMetric>::new(
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "file_read_error".into(),
                        category: "glean.health".into(),
                        send_in_pings: vec!["health".into()],
                        lifetime: Lifetime::Ping,
                        ..Default::default()
                    },
                },
                Some(vec![
                    Cow::from("parse"),
                    Cow::from("permission-denied"),
                    Cow::from("io"),
                    Cow::from("c0ffee-in-file"),
                    Cow::from("file-not-found"),
                ]),
            ),
            file_write_error: LabeledMetric::<CounterMetric>::new(
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "file_write_error".into(),
                        category: "glean.health".into(),
                        send_in_pings: vec!["health".into()],
                        lifetime: Lifetime::Ping,
                        ..Default::default()
                    },
                },
                Some(vec![
                    Cow::from("not-found"),
                    Cow::from("permission-denied"),
                    Cow::from("io"),
                ]),
            ),
        }
    }
}

pub type DataDirectoryInfoObject = Vec<DataDirectoryInfoObjectItem>;

#[derive(Debug, Serialize)]
#[serde(deny_unknown_fields)]
pub struct DataDirectoryInfoObjectItem {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dir_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dir_exists: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dir_created: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dir_modified: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file_count: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error_message: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty", default = "Vec::new")]
    pub files: DataDirectoryInfoObjectItemItemFiles,
}

pub type DataDirectoryInfoObjectItemItemFiles = Vec<DataDirectoryInfoObjectItemItemFilesItem>;

#[derive(Debug, Serialize)]
#[serde(deny_unknown_fields)]
pub struct DataDirectoryInfoObjectItemItemFilesItem {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file_created: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file_modified: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file_size: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error_message: Option<String>,
}
