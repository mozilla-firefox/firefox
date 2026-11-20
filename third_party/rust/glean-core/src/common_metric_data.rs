// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#[cfg(feature = "sqlite")]
use std::collections::BTreeMap;
use std::fmt::Display;
use std::sync::atomic::{AtomicU8, Ordering};

use malloc_size_of_derive::MallocSizeOf;

#[cfg(feature = "sqlite")]
use rusqlite::Transaction;

use crate::error::{Error, ErrorKind};
#[cfg(feature = "sqlite")]
use crate::error_recording::{record_error, record_error_sqlite};

#[cfg(feature = "sqlite")]
use crate::metrics::Metric;
#[cfg(feature = "sqlite")]
use crate::metrics::{
    dual_labeled_counter::validate_dual_label_in_memory,
    dual_labeled_counter::validate_dual_label_sqlite, labeled::validate_dynamic_label_in_memory,
    labeled::validate_dynamic_label_sqlite,
};

#[cfg(not(feature = "sqlite"))]
use crate::metrics::{
    dual_labeled_counter::validate_dual_label_rkv,
    labeled::{combine_base_identifier_and_label, validate_dynamic_label_rkv},
};
#[cfg(feature = "sqlite")]
use crate::ErrorType;
use crate::Glean;
use serde::{Deserialize, Serialize};

/// The supported metrics' lifetimes.
///
/// A metric's lifetime determines when its stored data gets reset.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Deserialize, Serialize, Default, MallocSizeOf)]
#[repr(i32)] // Use i32 to be compatible with our JNA definition
#[serde(rename_all = "lowercase")]
pub enum Lifetime {
    /// The metric is reset with each sent ping
    #[default]
    Ping,
    /// The metric is reset on application restart
    Application,
    /// The metric is reset with each user profile
    User,
}

impl Lifetime {
    /// String representation of the lifetime.
    pub fn as_str(self) -> &'static str {
        match self {
            Lifetime::Ping => "ping",
            Lifetime::Application => "app",
            Lifetime::User => "user",
        }
    }
}

impl TryFrom<i32> for Lifetime {
    type Error = Error;

    fn try_from(value: i32) -> Result<Lifetime, Self::Error> {
        match value {
            0 => Ok(Lifetime::Ping),
            1 => Ok(Lifetime::Application),
            2 => Ok(Lifetime::User),
            e => Err(ErrorKind::Lifetime(e).into()),
        }
    }
}

/// The common set of data shared across all different metric types.
#[derive(Default, Debug, Clone, Deserialize, Serialize, MallocSizeOf)]
pub struct CommonMetricData {
    /// The metric's name.
    pub name: String,
    /// The metric's category.
    pub category: String,
    /// List of ping names to include this metric in.
    pub send_in_pings: Vec<String>,
    /// The metric's lifetime.
    pub lifetime: Lifetime,
    /// Whether or not the metric is disabled.
    ///
    /// Disabled metrics are never recorded.
    pub disabled: bool,
    /// Whether this metric is inside of the session scope.
    ///
    /// `false` (the default) means this metric bypasses session sampling and
    /// does not carry session metadata. Non-event metrics should use the default
    /// for now. Event metrics that participate in session tracking must
    /// explicitly set this to `true`.
    pub in_session: bool,

    /// Label for this metric.
    ///
    /// When a [`LabeledMetric<T>`](crate::metrics::LabeledMetric) factory
    /// or [`DualLabeledCounterMetric`](crate::metrics::DualLabeledCounterMetric)
    /// creates the specific metric to be recorded to,
    /// labels are stored in the metric data
    /// so that it can validated against the database later.
    pub label: Option<MetricLabel>,
}

/// The type of dynamic label applied to a base metric. Used to help identify
/// the necessary validation to be performed.
#[derive(Debug, Clone, Deserialize, Serialize, MallocSizeOf, uniffi::Enum)]
pub enum MetricLabel {
    /// Static Label -- no validation required
    Static(String),
    /// A dynamic label applied from a `LabeledMetric`
    Label(String),
    /// A label applied by a `DualLabeledCounter` that contains a dynamic key and static category
    KeyOnly(String, String),
    /// A label applied by a `DualLabeledCounter` that contains a static key and dynamic category
    CategoryOnly(String, String),
    /// A label applied by a `DualLabeledCounter` that contains a dynamic key and category
    KeyAndCategory(String, String),
}

impl Default for MetricLabel {
    fn default() -> Self {
        Self::Label(String::new())
    }
}

impl Display for MetricLabel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        use crate::metrics::dual_labeled_counter::RECORD_SEPARATOR;
        match self {
            MetricLabel::Static(label) | MetricLabel::Label(label) => write!(f, "{label}"),
            MetricLabel::KeyOnly(key, category)
            | MetricLabel::CategoryOnly(key, category)
            | MetricLabel::KeyAndCategory(key, category) => {
                write!(f, "{key}{RECORD_SEPARATOR}{category}")
            }
        }
    }
}

#[derive(Default, Debug, MallocSizeOf)]
pub struct CommonMetricDataInternal {
    pub inner: CommonMetricData,
    pub disabled: AtomicU8,
}

impl Clone for CommonMetricDataInternal {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
            disabled: AtomicU8::new(self.disabled.load(Ordering::Relaxed)),
        }
    }
}

impl From<CommonMetricData> for CommonMetricDataInternal {
    fn from(input_data: CommonMetricData) -> Self {
        let disabled = input_data.disabled;
        Self {
            inner: input_data,
            disabled: AtomicU8::new(u8::from(disabled)),
        }
    }
}

/// A label checked for validity against label rules and against the database (for a specific metric).
#[cfg(feature = "sqlite")]
pub enum LabelCheck {
    /// No label was supplied, no check done.
    NoLabel,
    /// Label was validated and is usable.
    Label(String),
    /// Label check errored, the provided replacement label should be used.
    /// Errors can be recorded using [`LabelCheck::record_error`].
    Error(String, i32),
}

#[cfg(feature = "sqlite")]
impl LabelCheck {
    /// Extract the label to be used (or `None` if no label was checked)
    pub fn label(&self) -> Option<&str> {
        use LabelCheck::*;
        match self {
            NoLabel => None,
            Label(label) | Error(label, _) => Some(label),
        }
    }

    /// Record the number of errors that were detected during the label check.
    pub fn record_error(
        &self,
        glean: &Glean,
        tx: &mut Transaction,
        metric_name: &str,
        send_in_pings: &[String],
    ) {
        let LabelCheck::Error(_, count) = self else {
            return;
        };

        #[cfg(feature = "sqlite")]
        record_error_sqlite(
            glean,
            tx,
            metric_name,
            send_in_pings,
            ErrorType::InvalidLabel,
            *count,
        );
    }

    pub fn record_error_metric(&self, glean: &Glean, meta: &CommonMetricDataInternal) {
        let LabelCheck::Error(_, count) = self else {
            return;
        };

        record_error(
            glean,
            meta,
            ErrorType::InvalidLabel,
            "invalid label",
            *count,
        );
    }

    /// Maps a `LabelCheck` by applying a function to the contained label (if any).
    ///
    /// This only maps over the already checked label.
    /// No further label check is done.
    fn map(self, mut f: impl FnMut(String) -> String) -> Self {
        use LabelCheck::*;

        match self {
            NoLabel => NoLabel,
            Label(s) => Label(f(s)),
            Error(s, cnt) => Error(f(s), cnt),
        }
    }
}

impl CommonMetricDataInternal {
    /// Creates a new metadata object.
    pub fn new<A: Into<String>, B: Into<String>, C: Into<String>>(
        category: A,
        name: B,
        ping_name: C,
    ) -> CommonMetricDataInternal {
        CommonMetricDataInternal {
            inner: CommonMetricData {
                name: name.into(),
                category: category.into(),
                send_in_pings: vec![ping_name.into()],
                ..Default::default()
            },
            disabled: AtomicU8::new(0),
        }
    }

    /// The metric's base identifier, including the category and name, but not the label.
    ///
    /// If `category` is empty, it's ommitted.
    /// Otherwise, it's the combination of the metric's `category` and `name`.
    pub(crate) fn base_identifier(&self) -> String {
        if self.inner.category.is_empty() {
            self.inner.name.clone()
        } else {
            let mut id =
                String::with_capacity(self.inner.category.len() + self.inner.name.len() + 1);
            id.push_str(&self.inner.category);
            id.push('.');
            id.push_str(&self.inner.name);
            id
        }
    }

    /// The metric's unique identifier, including the category, name and label.
    ///
    /// If `category` is empty, it's ommitted.
    /// Otherwise, it's the combination of the metric's `category`, `name` and `label`.
    #[cfg(not(feature = "sqlite"))]
    pub(crate) fn identifier(&self, glean: &Glean, record: bool) -> String {
        let base_identifier = self.base_identifier();

        if let Some(label) = &self.inner.label {
            let label = match label {
                MetricLabel::Static(label) => label.to_string(),
                MetricLabel::Label(label) => {
                    validate_dynamic_label_rkv(glean, self, &base_identifier, label, record)
                }
                MetricLabel::KeyOnly(..) => {
                    validate_dual_label_rkv(glean, self, &base_identifier, label, record)
                }
                MetricLabel::CategoryOnly(..) => {
                    validate_dual_label_rkv(glean, self, &base_identifier, label, record)
                }
                MetricLabel::KeyAndCategory(..) => {
                    validate_dual_label_rkv(glean, self, &base_identifier, label, record)
                }
            };

            combine_base_identifier_and_label(&base_identifier, &label)
        } else {
            base_identifier
        }
    }

    /// Check the label for validity against the database.
    ///
    /// Returns the result of the check.
    /// No error is recorded in the database if the check fails.
    /// Extract the validated label, if any, using [`LabelCheck::label`].
    /// Record an error, if any, using [`LabelCheck::record_error`].
    #[cfg(feature = "sqlite")]
    pub(crate) fn check_labels(&self, tx: &rusqlite::Connection) -> LabelCheck {
        let base_identifier = self.base_identifier();

        if let Some(label) = &self.inner.label {
            match label {
                MetricLabel::Static(label) => LabelCheck::Label(label.to_string()),
                MetricLabel::Label(label) => {
                    validate_dynamic_label_sqlite(tx, &base_identifier, label)
                }
                MetricLabel::KeyOnly(key, static_category) => {
                    validate_dual_label_sqlite(tx, &base_identifier, key, "")
                        .map(|key| [&key[..], &static_category[..]].concat())
                }
                MetricLabel::CategoryOnly(static_key, category) => {
                    validate_dual_label_sqlite(tx, &base_identifier, "", category)
                        .map(|category| [&static_key[..], &category[..]].concat())
                }
                MetricLabel::KeyAndCategory(key, category) => {
                    validate_dual_label_sqlite(tx, &base_identifier, key, category)
                }
            }
        } else {
            LabelCheck::NoLabel
        }
    }

    #[cfg(feature = "sqlite")]
    pub(crate) fn check_labels_in_memory(&self, map: &BTreeMap<String, Metric>) -> LabelCheck {
        let base_identifier = self.base_identifier();

        if let Some(label) = &self.inner.label {
            match label {
                MetricLabel::Static(label) => LabelCheck::Label(label.to_string()),
                MetricLabel::Label(label) => {
                    validate_dynamic_label_in_memory(map, self, &base_identifier, label)
                }
                MetricLabel::KeyOnly(key, static_category) => {
                    validate_dual_label_in_memory(map, self, &base_identifier, key, "")
                        .map(|key| format!("{key}{static_category}"))
                }
                MetricLabel::CategoryOnly(static_key, category) => {
                    validate_dual_label_in_memory(map, self, &base_identifier, "", category)
                        .map(|category| format!("{static_key}{category}"))
                }
                MetricLabel::KeyAndCategory(key, category) => {
                    validate_dual_label_in_memory(map, self, &base_identifier, key, category)
                }
            }
        } else {
            LabelCheck::NoLabel
        }
    }

    /// Whether or not the metric is `in_session`.
    ///
    /// Metrics that are `in_session` participate in session tracking and carry session metadata.
    /// This is mostly relevant for event metrics, other metric types should generally not be `in_session`
    /// and default to `false`.
    pub fn in_session(&self) -> bool {
        self.inner.in_session
    }

    /// The list of storages this metric should be recorded into.
    pub fn storage_names(&self) -> &[String] {
        &self.inner.send_in_pings
    }
}
