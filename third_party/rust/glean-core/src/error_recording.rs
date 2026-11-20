// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! # Error Recording
//!
//! Glean keeps track of errors that occured due to invalid labels or invalid values when recording
//! other metrics.
//!
//! Error counts are stored in labeled counters in the `glean.error` category.
//! The labeled counter metrics that store the errors are defined in the `metrics.yaml` for documentation purposes,
//! but are not actually used directly, since the `send_in_pings` value needs to match the pings of the metric that is erroring (plus the "metrics" ping),
//! not some constant value that we could define in `metrics.yaml`.

use std::fmt::Display;
#[cfg(feature = "sqlite")]
use std::sync::atomic::AtomicU8;

#[cfg(feature = "sqlite")]
use rusqlite::Transaction;

use crate::common_metric_data::CommonMetricDataInternal;
use crate::error::{Error, ErrorKind};
use crate::metrics::CounterMetric;
#[cfg(feature = "sqlite")]
use crate::metrics::Metric;
use crate::Glean;
use crate::Lifetime;
use crate::{CommonMetricData, MetricLabel};

/// The possible error types for metric recording.
///
/// Note: the cases in this enum must be kept in sync with the ones
/// in the platform-specific code (e.g. `ErrorType.kt`) and with the
/// metrics in the registry files.
// When adding a new error type ensure it's also added to `ErrorType::iter()` below.
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ErrorType {
    /// For when the value to be recorded does not match the metric-specific restrictions
    InvalidValue,
    /// For when the label of a labeled metric does not match the restrictions
    InvalidLabel,
    /// For when the metric caught an invalid state while recording
    InvalidState,
    /// For when the value to be recorded overflows the metric-specific upper range
    InvalidOverflow,
}

impl ErrorType {
    /// The error type's metric id
    pub fn as_str(&self) -> &'static str {
        match self {
            ErrorType::InvalidValue => "invalid_value",
            ErrorType::InvalidLabel => "invalid_label",
            ErrorType::InvalidState => "invalid_state",
            ErrorType::InvalidOverflow => "invalid_overflow",
        }
    }

    /// Return an iterator over all possible error types.
    ///
    /// ```
    /// # use glean_core::ErrorType;
    /// let errors = ErrorType::iter();
    /// let all_errors = errors.collect::<Vec<_>>();
    /// assert_eq!(4, all_errors.len());
    /// ```
    pub fn iter() -> impl Iterator<Item = Self> {
        // N.B.: This has no compile-time guarantees that it is complete.
        // New `ErrorType` variants will need to be added manually.
        [
            ErrorType::InvalidValue,
            ErrorType::InvalidLabel,
            ErrorType::InvalidState,
            ErrorType::InvalidOverflow,
        ]
        .iter()
        .copied()
    }
}

impl TryFrom<i32> for ErrorType {
    type Error = Error;

    fn try_from(value: i32) -> Result<ErrorType, Self::Error> {
        match value {
            0 => Ok(ErrorType::InvalidValue),
            1 => Ok(ErrorType::InvalidLabel),
            2 => Ok(ErrorType::InvalidState),
            3 => Ok(ErrorType::InvalidOverflow),
            e => Err(ErrorKind::Lifetime(e).into()),
        }
    }
}

/// For a given metric, get the metric in which to record errors
fn get_error_metric_for_metric(meta: &CommonMetricDataInternal, error: ErrorType) -> CounterMetric {
    // Can't use meta.identifier here, since that might cause infinite recursion
    // if the label on this metric needs to report an error.
    let name = meta.base_identifier();

    // Record errors in the pings the metric is in, as well as the metrics ping.
    let mut send_in_pings = meta.inner.send_in_pings.clone();
    let ping_name = "metrics".to_string();
    if !send_in_pings.contains(&ping_name) {
        send_in_pings.push(ping_name);
    }
    send_in_pings.retain(|elem| elem != "glean_internal_info" && elem != "glean_client_info");

    CounterMetric::new(CommonMetricData {
        name: error.as_str().to_string(),
        category: "glean.error".into(),
        lifetime: Lifetime::Ping,
        send_in_pings,
        label: Some(MetricLabel::Label(name.to_string())),
        ..Default::default()
    })
}

/// Records an error into Glean.
///
/// Errors are recorded as labeled counters in the `glean.error` category.
///
/// *Note*: We do make assumptions here how labeled metrics are encoded, namely by having the name
/// `<name>/<label>`.
/// Errors do not adhere to the usual "maximum label" restriction.
///
/// # Arguments
///
/// * `glean` - The Glean instance containing the database
/// * `meta` - The metric's meta data
/// * `error` -  The error type to record
/// * `message` - The message to log. This message is not sent with the ping.
///             It does not need to include the metric id, as that is automatically prepended to the message.
/// * `num_errors` - The number of errors of the same type to report.
pub fn record_error<O: Into<Option<i32>>>(
    glean: &Glean,
    meta: &CommonMetricDataInternal,
    error: ErrorType,
    message: impl Display,
    num_errors: O,
) {
    let metric = get_error_metric_for_metric(meta, error);

    log::warn!("{}: {}", meta.base_identifier(), message);
    let to_report = num_errors.into().unwrap_or(1);
    debug_assert!(to_report > 0);
    metric.add_sync(glean, to_report);
}

#[cfg(feature = "sqlite")]
pub fn record_error_sqlite(
    glean: &Glean,
    tx: &mut Transaction,
    metric_name: &str,
    send_in_pings: &[String],
    error: ErrorType,
    num_errors: i32,
) {
    debug_assert!(num_errors > 0);
    if num_errors <= 0 {
        log::warn!("Trying to record {num_errors} errors for {metric_name:?} (<= 0). Bailing out.");
        return;
    }

    // We explicitly don't use the `Counter` metric directly here.
    //
    // * This is called from within the recording functions in `sqlite.rs`
    // * That means a transaction is already opened. We can't open a new one.
    // * We can avoid some allocations by constructing only what we need and what we already have

    let ping_name = String::from("metrics");
    let mut send_in_pings = send_in_pings.to_vec();
    if !send_in_pings.contains(&ping_name) {
        send_in_pings.push(ping_name);
    }
    send_in_pings.retain(|elem| elem != "glean_internal_info" && elem != "glean_client_info");

    let lifetime = Lifetime::Ping;
    let transform = |old_value| match old_value {
        Some(Metric::Counter(old_value)) => Metric::Counter(old_value.saturating_add(num_errors)),
        _ => Metric::Counter(num_errors),
    };

    let inner = CommonMetricData {
        category: String::from("glean.error"),
        name: String::from(error.as_str()),
        send_in_pings,
        lifetime,
        label: Some(MetricLabel::Static(String::from(metric_name))),
        ..Default::default()
    };
    let cmd = CommonMetricDataInternal {
        inner,
        disabled: AtomicU8::new(0),
    };
    _ = glean
        .storage()
        .record_with_transaction(glean, tx, &cmd, transform);
}

/// Gets the number of recorded errors for the given metric and error type.
///
/// *Notes: This is a **test-only** API, but we need to expose it to be used in integration tests.
///
/// # Arguments
///
/// * `glean` - The Glean object holding the database
/// * `meta` - The metadata of the metric instance
/// * `error` - The type of error
///
/// # Returns
///
/// The number of errors reported.
pub fn test_get_num_recorded_errors(
    glean: &Glean,
    meta: &CommonMetricDataInternal,
    error: ErrorType,
) -> Result<i32, String> {
    let metric = get_error_metric_for_metric(meta, error);

    metric.get_value(glean, Some("metrics")).ok_or_else(|| {
        format!(
            "No error recorded for {} in 'metrics' store",
            meta.base_identifier(),
        )
    })
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::metrics::*;
    use crate::tests::new_glean;

    #[test]
    fn error_type_i32_mapping() {
        let error: ErrorType = std::convert::TryFrom::try_from(0).unwrap();
        assert_eq!(error, ErrorType::InvalidValue);
        let error: ErrorType = std::convert::TryFrom::try_from(1).unwrap();
        assert_eq!(error, ErrorType::InvalidLabel);
        let error: ErrorType = std::convert::TryFrom::try_from(2).unwrap();
        assert_eq!(error, ErrorType::InvalidState);
        let error: ErrorType = std::convert::TryFrom::try_from(3).unwrap();
        assert_eq!(error, ErrorType::InvalidOverflow);
    }

    #[test]
    fn recording_of_all_error_types() {
        let (glean, _t) = new_glean(None);

        let string_metric = StringMetric::new(CommonMetricData {
            name: "string_metric".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into(), "store2".into()],
            disabled: false,
            lifetime: Lifetime::User,
            ..Default::default()
        });

        let expected_invalid_values_errors: i32 = 1;
        let expected_invalid_labels_errors: i32 = 2;

        record_error(
            &glean,
            string_metric.meta(),
            ErrorType::InvalidValue,
            "Invalid value",
            None,
        );

        record_error(
            &glean,
            string_metric.meta(),
            ErrorType::InvalidLabel,
            "Invalid label",
            expected_invalid_labels_errors,
        );

        let invalid_val =
            get_error_metric_for_metric(string_metric.meta(), ErrorType::InvalidValue);
        let invalid_label =
            get_error_metric_for_metric(string_metric.meta(), ErrorType::InvalidLabel);
        for &store in &["store1", "store2", "metrics"] {
            assert_eq!(
                Some(expected_invalid_values_errors),
                invalid_val.get_value(&glean, Some(store))
            );

            assert_eq!(
                Some(expected_invalid_labels_errors),
                invalid_label.get_value(&glean, Some(store))
            );
        }
    }
}
