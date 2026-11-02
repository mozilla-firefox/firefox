/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

//! Metric recording for the ads client.
//!
//! Telemetry is a process-wide singleton: recording a metric is a plain
//! function call from wherever the interesting thing happened, much like a
//! `tracing` macro. Nothing has to be threaded through the clients, stores or
//! transports.
//!
//! This is the same API the crate will expose once metrics are recorded
//! straight from Rust through [`glean-sym`]. Until then, the backend forwards
//! to the [`Telemetry`] implementation the FFI layer installs, which wraps the
//! callback the consumer hands to `MozAdsClientBuilder::telemetry()`; see
//! `telemetry/backend.rs`. Moving to
//! glean-sym means swapping that backend for one that talks to the generated
//! metrics, and none of the recording sites have to change.
//!
//! Every function here is infallible and silent by design. Telemetry must never
//! change the outcome of the operation it is describing.
//!
//! [`glean-sym`]: https://github.com/mozilla/glean/tree/main/glean-core/glean-sym

use std::fmt::Display;

use crate::http_cache::{CacheOutcome, HttpCacheBuilderError};

mod backend;

#[cfg(test)]
pub(crate) use backend::test_lock;
pub use backend::Telemetry;
pub(crate) use backend::{install, uninstall};

/// A client operation, as labeled by `ads_client.client_operation_total` and
/// `ads_client.client_error`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ClientOperation {
    New,
    RecordClick,
    RecordImpression,
    ReportAd,
    RequestAds,
}

impl ClientOperation {
    fn label(self) -> &'static str {
        match self {
            Self::New => "new",
            Self::RecordClick => "record_click",
            Self::RecordImpression => "record_impression",
            Self::ReportAd => "report_ad",
            Self::RequestAds => "request_ads",
        }
    }
}

/// A label of `ads_client.build_cache_error`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BuildCacheErrorLabel {
    DatabaseError,
    EmptyDbPath,
    InvalidMaxSize,
    InvalidTtl,
    // TODO: these are not in `metrics.yaml`, so Glean records them as
    // `__other__`. Give the ads store its own metric.
    #[cfg(feature = "stateful")]
    StoreDatabaseError,
    #[cfg(feature = "stateful")]
    StoreEmptyDbPath,
    #[cfg(feature = "stateful")]
    StoreInvalidMaxSize,
}

impl BuildCacheErrorLabel {
    fn as_str(self) -> &'static str {
        match self {
            Self::DatabaseError => "database_error",
            Self::EmptyDbPath => "empty_db_path",
            Self::InvalidMaxSize => "invalid_max_size",
            Self::InvalidTtl => "invalid_ttl",
            #[cfg(feature = "stateful")]
            Self::StoreDatabaseError => "store_database_error",
            #[cfg(feature = "stateful")]
            Self::StoreEmptyDbPath => "store_empty_db_path",
            #[cfg(feature = "stateful")]
            Self::StoreInvalidMaxSize => "store_invalid_max_size",
        }
    }
}

/// A label of `ads_client.deserialization_error`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeserializationErrorLabel {
    InvalidAdItem,
}

impl DeserializationErrorLabel {
    fn as_str(self) -> &'static str {
        match self {
            Self::InvalidAdItem => "invalid_ad_item",
        }
    }
}

/// A label of `ads_client.http_cache_outcome`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HttpCacheOutcomeLabel {
    CleanupFailed,
    Hit,
    LookupFailed,
    MissNotCacheable,
    MissStored,
    NoCache,
    StoreFailed,
    TrimFailed,
}

impl HttpCacheOutcomeLabel {
    fn as_str(self) -> &'static str {
        match self {
            Self::CleanupFailed => "cleanup_failed",
            Self::Hit => "hit",
            Self::LookupFailed => "lookup_failed",
            Self::MissNotCacheable => "miss_not_cacheable",
            Self::MissStored => "miss_stored",
            Self::NoCache => "no_cache",
            Self::StoreFailed => "store_failed",
            Self::TrimFailed => "trim_failed",
        }
    }
}

// By design: a free function, so recording a metric is one call from anywhere.
/// Records an attempted operation against `ads_client.client_operation_total`.
pub fn record_client_operation(operation: ClientOperation) {
    backend::client_operation_total(operation.label());
}

// By design: a free function, so recording a metric is one call from anywhere.
/// Records a failed operation against `ads_client.client_error`.
///
/// Errors are recorded here even when they are also propagated to the consumer.
pub fn record_client_error(operation: ClientOperation, error: &impl Display) {
    backend::client_error(operation.label(), error.to_string());
}

// By design: a free function, so recording a metric is one call from anywhere.
/// Records a failure to build the HTTP cache against
/// `ads_client.build_cache_error`.
pub fn record_build_cache_error(error: &HttpCacheBuilderError) {
    let label = match error {
        HttpCacheBuilderError::Database(_) => BuildCacheErrorLabel::DatabaseError,
        HttpCacheBuilderError::DatabaseMemory(_) => BuildCacheErrorLabel::DatabaseError,
        HttpCacheBuilderError::EmptyDbPath => BuildCacheErrorLabel::EmptyDbPath,
        HttpCacheBuilderError::InvalidMaxSize { .. } => BuildCacheErrorLabel::InvalidMaxSize,
        HttpCacheBuilderError::InvalidTtl { .. } => BuildCacheErrorLabel::InvalidTtl,
    };
    let value = match error {
        HttpCacheBuilderError::Database(sql_support::open_database::Error::SqlError(
            rusqlite::Error::SqliteFailure(
                rusqlite::ffi::Error {
                    code: _,
                    extended_code,
                },
                Some(_),
            ),
        )) => format!("{0} ({1})", error, extended_code),
        _ => error.to_string(),
    };
    backend::build_cache_error(label.as_str(), value);
}

// By design: a free function, so recording a metric is one call from anywhere.
/// Records a failure to build the ads store against
/// `ads_client.build_cache_error`.
#[cfg(feature = "stateful")]
pub fn record_ads_store_error(error: &crate::ads_store::builder::AdsStoreBuilderError) {
    // By design: telemetry owns every label, even though the ads store
    // records through it.
    use crate::ads_store::builder::AdsStoreBuilderError;

    let label = match error {
        AdsStoreBuilderError::Database(_) => BuildCacheErrorLabel::StoreDatabaseError,
        AdsStoreBuilderError::EmptyDbPath => BuildCacheErrorLabel::StoreEmptyDbPath,
        AdsStoreBuilderError::InvalidMaxSize { .. } => BuildCacheErrorLabel::StoreInvalidMaxSize,
    };
    backend::build_cache_error(label.as_str(), error.to_string());
}

// By design: a free function, so recording a metric is one call from anywhere.
/// Records the result of an HTTP cache read against
/// `ads_client.http_cache_outcome`.
pub fn record_http_cache_outcome(outcome: &CacheOutcome) {
    let (label, value) = match outcome {
        CacheOutcome::CleanupFailed(e) => (HttpCacheOutcomeLabel::CleanupFailed, e.to_string()),
        CacheOutcome::Hit => (HttpCacheOutcomeLabel::Hit, String::new()),
        CacheOutcome::LookupFailed(e) => (HttpCacheOutcomeLabel::LookupFailed, e.to_string()),
        CacheOutcome::MissNotCacheable => (HttpCacheOutcomeLabel::MissNotCacheable, String::new()),
        CacheOutcome::MissStored => (HttpCacheOutcomeLabel::MissStored, String::new()),
        CacheOutcome::NoCache => (HttpCacheOutcomeLabel::NoCache, String::new()),
        CacheOutcome::StoreFailed(e) => (HttpCacheOutcomeLabel::StoreFailed, e.to_string()),
        CacheOutcome::TrimFailed(e) => (HttpCacheOutcomeLabel::TrimFailed, e.to_string()),
    };
    backend::http_cache_outcome(label.as_str(), value);
}

/// Records an ad item we skipped, because it did not deserialize or is not
/// the kind of ad asked for, against `ads_client.deserialization_error`.
pub fn record_invalid_ad_item(error: &impl Display) {
    backend::deserialization_error(
        DeserializationErrorLabel::InvalidAdItem.as_str(),
        error.to_string(),
    );
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_record_build_cache_error_includes_extended_sqlite_error_code() {
        // lock and set up test telemetry
        let _test_lock = backend::test_lock();
        let recording = backend::RecordingTelemetry::default();
        backend::install(recording.clone());

        let error_code = 4874;
        let error_without_message =
            HttpCacheBuilderError::Database(sql_support::open_database::Error::SqlError(
                rusqlite::Error::SqliteFailure(rusqlite::ffi::Error::new(error_code), None),
            ));
        let error_with_message = HttpCacheBuilderError::Database(
            sql_support::open_database::Error::SqlError(rusqlite::Error::SqliteFailure(
                rusqlite::ffi::Error::new(error_code),
                Some("Some obscure description".to_string()),
            )),
        );

        record_build_cache_error(&error_without_message);
        record_build_cache_error(&error_with_message);

        let records_lock = recording.build_cache_errors.lock();
        let records = records_lock.get("database_error").unwrap();
        assert_ne!(records[0], records[1]);
        assert!(records[0].contains(&error_code.to_string()));
        assert!(records[1].contains(&error_code.to_string()));

        // tear down test telemetry, locks drops out of scope
        backend::uninstall();
    }
}
