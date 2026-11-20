// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::any::Any;
use std::borrow::Cow;
#[cfg(feature = "sqlite")]
use std::collections::BTreeMap;
use std::collections::HashSet;
use std::collections::{hash_map::Entry, HashMap};
use std::mem;
use std::sync::{Arc, Mutex};

use malloc_size_of::MallocSizeOf;
#[cfg(feature = "sqlite")]
use rusqlite::params;

#[cfg(feature = "sqlite")]
use crate::common_metric_data::LabelCheck;
use crate::common_metric_data::{CommonMetricData, CommonMetricDataInternal, MetricLabel};
use crate::database::EMPTY_LABEL;
use crate::error_recording::{test_get_num_recorded_errors, ErrorType};
use crate::histogram::HistogramType;
use crate::metrics::dual_labeled_counter::RECORD_SEPARATOR;
use crate::metrics::{
    BooleanMetric, CounterMetric, CustomDistributionMetric, MemoryDistributionMetric, MemoryUnit,
    MetricType, QuantityMetric, StringMetric, TestGetValue, TimeUnit, TimingDistributionMetric,
};
use crate::storage::StorageManager;
#[cfg(not(feature = "sqlite"))]
use crate::{error_recording::record_error, metrics::Metric, Glean};

const MAX_LABELS: usize = 16;
const OTHER_LABEL: &str = "__other__";
const MAX_LABEL_LENGTH: usize = 111;

/// A labeled counter.
pub type LabeledCounter = LabeledMetric<CounterMetric>;

/// A labeled boolean.
pub type LabeledBoolean = LabeledMetric<BooleanMetric>;

/// A labeled string.
pub type LabeledString = LabeledMetric<StringMetric>;

/// A labeled custom_distribution.
pub type LabeledCustomDistribution = LabeledMetric<CustomDistributionMetric>;

/// A labeled memory_distribution.
pub type LabeledMemoryDistribution = LabeledMetric<MemoryDistributionMetric>;

/// A labeled timing_distribution.
pub type LabeledTimingDistribution = LabeledMetric<TimingDistributionMetric>;

/// A labeled quantity
pub type LabeledQuantity = LabeledMetric<QuantityMetric>;

/// The metric data needed to construct inner submetrics.
///
/// Different Labeled metrics require different amounts and kinds of information to
/// be constructed.
pub enum LabeledMetricData {
    /// The common case: just a CMD.
    #[allow(missing_docs)]
    Common { cmd: CommonMetricData },
    /// The custom_distribution-specific case.
    #[allow(missing_docs)]
    CustomDistribution {
        cmd: CommonMetricData,
        range_min: i64,
        range_max: i64,
        bucket_count: i64,
        histogram_type: HistogramType,
    },
    /// The memory_distribution-specific case.
    #[allow(missing_docs)]
    MemoryDistribution {
        cmd: CommonMetricData,
        unit: MemoryUnit,
    },
    /// The timing_distribution-specific case.
    #[allow(missing_docs)]
    TimingDistribution {
        cmd: CommonMetricData,
        unit: TimeUnit,
    },
}

/// A labeled metric.
///
/// Labeled metrics allow to record multiple sub-metrics of the same type under different string labels.
#[derive(Debug)]
pub struct LabeledMetric<T> {
    labels: Option<Vec<Cow<'static, str>>>,
    /// Type of the underlying metric
    /// We hold on to an instance of it, which is cloned to create new modified instances.
    submetric: T,

    /// A map from a unique ID for the labeled submetric to a handle of an instantiated
    /// metric type.
    label_map: Mutex<HashMap<String, Arc<T>>>,
}

impl<T: MallocSizeOf> ::malloc_size_of::MallocSizeOf for LabeledMetric<T> {
    fn size_of(&self, ops: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        let map = self.label_map.lock().unwrap();

        // Copy of `MallocShallowSizeOf` implementation for `HashMap<K, V>` in `wr_malloc_size_of`.
        // Note: An instantiated submetric is behind an `Arc`.
        // `size_of` should only be called from a single thread to avoid double-counting.
        let shallow_size = if ops.has_malloc_enclosing_size_of() {
            map.values()
                .next()
                .map_or(0, |v| unsafe { ops.malloc_enclosing_size_of(v) })
        } else {
            map.capacity()
                * (mem::size_of::<String>() + mem::size_of::<T>() + mem::size_of::<usize>())
        };

        let mut map_size = shallow_size;
        for (k, v) in map.iter() {
            map_size += k.size_of(ops);
            map_size += v.size_of(ops);
        }

        self.labels.size_of(ops) + self.submetric.size_of(ops) + map_size
    }
}

/// Sealed traits protect against downstream implementations.
///
/// We wrap it in a private module that is inaccessible outside of this module.
mod private {
    use super::LabeledMetricData;
    use crate::metrics::{
        BooleanMetric, CounterMetric, CustomDistributionMetric, MemoryDistributionMetric,
        QuantityMetric, StringMetric, TimingDistributionMetric,
    };

    /// The sealed labeled trait.
    ///
    /// This also allows us to hide methods, that are only used internally
    /// and should not be visible to users of the object implementing the
    /// `Labeled<T>` trait.
    pub trait Sealed {
        /// Create a new `glean_core` metric from the metadata.
        fn new_inner(meta: LabeledMetricData) -> Self;
    }

    impl Sealed for CounterMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::Common { cmd } => Self::new(cmd),
                _ => panic!("Incorrect construction of Labeled<CounterMetric>"),
            }
        }
    }

    impl Sealed for BooleanMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::Common { cmd } => Self::new(cmd),
                _ => panic!("Incorrect construction of Labeled<BooleanMetric>"),
            }
        }
    }

    impl Sealed for StringMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::Common { cmd } => Self::new(cmd),
                _ => panic!("Incorrect construction of Labeled<StringMetric>"),
            }
        }
    }

    impl Sealed for CustomDistributionMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::CustomDistribution {
                    cmd,
                    range_min,
                    range_max,
                    bucket_count,
                    histogram_type,
                } => Self::new(cmd, range_min, range_max, bucket_count, histogram_type),
                _ => panic!("Incorrect construction of Labeled<CustomDistributionMetric>"),
            }
        }
    }

    impl Sealed for MemoryDistributionMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::MemoryDistribution { cmd, unit } => Self::new(cmd, unit),
                _ => panic!("Incorrect construction of Labeled<MemoryDistributionMetric>"),
            }
        }
    }

    impl Sealed for TimingDistributionMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::TimingDistribution { cmd, unit } => Self::new(cmd, unit),
                _ => panic!("Incorrect construction of Labeled<TimingDistributionMetric>"),
            }
        }
    }

    impl Sealed for QuantityMetric {
        fn new_inner(meta: LabeledMetricData) -> Self {
            match meta {
                LabeledMetricData::Common { cmd } => Self::new(cmd),
                _ => panic!("Incorrect construction of Labeled<QuantityMetric>"),
            }
        }
    }
}

/// Trait for metrics that can be nested inside a labeled metric.
pub trait AllowLabeled: MetricType {
    /// Create a new labeled metric.
    fn new_labeled(meta: LabeledMetricData) -> Self;
}

// Implement the trait for everything we marked as allowed.
impl<T> AllowLabeled for T
where
    T: MetricType,
    T: private::Sealed,
{
    fn new_labeled(meta: LabeledMetricData) -> Self {
        T::new_inner(meta)
    }
}

impl<T> LabeledMetric<T>
where
    T: AllowLabeled + Clone,
{
    /// Creates a new labeled metric from the given metric instance and optional list of labels.
    ///
    /// See [`get`](LabeledMetric::get) for information on how static or dynamic labels are handled.
    pub fn new(
        meta: LabeledMetricData,
        labels: Option<Vec<Cow<'static, str>>>,
    ) -> LabeledMetric<T> {
        let submetric = T::new_labeled(meta);
        LabeledMetric::new_inner(submetric, labels)
    }

    fn new_inner(submetric: T, labels: Option<Vec<Cow<'static, str>>>) -> LabeledMetric<T> {
        let label_map = Default::default();
        LabeledMetric {
            labels,
            submetric,
            label_map,
        }
    }

    /// Creates a new metric with a specific label.
    ///
    /// This is used for static labels where we can just set the name to be `name/label`.
    fn new_metric_with_label(&self, label: MetricLabel) -> T {
        self.submetric.with_label(label)
    }

    /// Creates a new metric with a specific label.
    ///
    /// This is used for dynamic labels where we have to actually validate and correct the
    /// label later when we have a Glean object.
    ///
    /// TODO: Consolidate with `new_metric_with_label` above.
    fn new_metric_with_dynamic_label(&self, label: MetricLabel) -> T {
        self.submetric.with_label(label)
    }

    /// Creates a static label.
    ///
    /// # Safety
    ///
    /// Should only be called when static labels are available on this metric.
    ///
    /// # Arguments
    ///
    /// * `label` - The requested label
    ///
    /// # Returns
    ///
    /// The requested label if it is in the list of allowed labels.
    /// Otherwise `OTHER_LABEL` is returned.
    fn static_label<'a>(&self, label: &'a str) -> &'a str {
        debug_assert!(self.labels.is_some());
        let labels = self.labels.as_ref().unwrap();
        if labels.iter().any(|l| l == label) {
            label
        } else {
            OTHER_LABEL
        }
    }

    /// Gets a specific metric for a given label.
    ///
    /// If a set of acceptable labels were specified in the `metrics.yaml` file,
    /// and the given label is not in the set, it will be recorded under the special `OTHER_LABEL` label.
    ///
    /// If a set of acceptable labels was not specified in the `metrics.yaml` file,
    /// only the first 16 unique labels will be used.
    /// After that, any additional labels will be recorded under the special `OTHER_LABEL` label.
    ///
    /// Labels must have a maximum of 111 characters, and may comprise any printable ASCII characters.
    /// If an invalid label is used, the metric will be recorded in the special `OTHER_LABEL` label.
    pub fn get<S: AsRef<str>>(&self, label: S) -> Arc<T> {
        let label = label.as_ref();

        // The handle is a unique number per metric.
        // The label identifies the submetric.
        let id = format!("{}/{}", self.submetric.meta().base_identifier(), label);

        let mut map = self.label_map.lock().unwrap();
        match map.entry(id) {
            Entry::Occupied(entry) => Arc::clone(entry.get()),
            Entry::Vacant(entry) => {
                // We have 2 scenarios to consider:
                // * Static labels. No database access needed. We just look at what is in memory.
                // * Dynamic labels. We look up in the database all previously stored
                //   labels in order to keep a maximum of allowed labels. This is done later
                //   when the specific metric is actually recorded, when we are guaranteed to have
                //   an initialized Glean object.
                let metric = match self.labels {
                    Some(_) => {
                        let label = self.static_label(label);
                        self.new_metric_with_label(MetricLabel::Static(label.to_string()))
                    }
                    None => {
                        self.new_metric_with_dynamic_label(MetricLabel::Label(label.to_string()))
                    }
                };
                let metric = Arc::new(metric);
                entry.insert(Arc::clone(&metric));
                metric
            }
        }
    }

    /// **Exported for test purposes.**
    ///
    /// Gets the number of recorded errors for the given metric and error type.
    ///
    /// # Arguments
    ///
    /// * `error` - The type of error
    ///
    /// # Returns
    ///
    /// The number of errors reported.
    pub fn test_get_num_recorded_errors(&self, error: ErrorType) -> i32 {
        crate::block_on_dispatcher();
        crate::core::with_glean(|glean| {
            test_get_num_recorded_errors(glean, self.submetric.meta(), error).unwrap_or(0)
        })
    }
}

impl<T, S> TestGetValue for LabeledMetric<T>
where
    T: AllowLabeled + TestGetValue<Output = S> + Clone,
    S: Any,
{
    type Output = HashMap<String, S>;

    fn test_get_value(&self, ping_name: Option<String>) -> Option<HashMap<String, S>> {
        // We get the labels from the db because our in-memory cache is not guaranteed to be complete.
        crate::block_on_dispatcher();
        let labels = crate::core::with_glean(|glean| {
            let queried_ping_name = ping_name
                .as_ref()
                .unwrap_or_else(|| &self.submetric.meta().inner.send_in_pings[0]);
            StorageManager.snapshot_labels(
                glean.storage(),
                queried_ping_name,
                &self.submetric.meta().base_identifier(),
                self.submetric.meta().inner.lifetime,
            )
        });
        let mut out = HashMap::new();
        labels.iter().for_each(|label| {
            if let Some(v) = self.get(label).test_get_value(ping_name.clone()) {
                out.insert(label.to_owned(), v);
            }
        });
        Some(out)
    }
}

#[cfg(feature = "sqlite")]
pub fn validate_dynamic_label_sqlite(
    tx: &rusqlite::Connection,
    base_identifier: &str,
    label: &str,
) -> LabelCheck {
    let existing_labels_sql = "SELECT DISTINCT labels FROM telemetry WHERE id = ?1";

    let mut label_already_used = false;
    let mut label_count = 0;
    {
        let Ok(mut stmt) = tx.prepare(existing_labels_sql) else {
            // If we can't fetch from the database, assume the label is ok to use
            return LabelCheck::Label(label.to_string());
        };

        let Ok(mut rows) = stmt.query(params![base_identifier]) else {
            // If we can't fetch from the database, assume the label is ok to use
            return LabelCheck::Label(label.to_string());
        };

        while let Ok(Some(row)) = rows.next() {
            let existing_label: String = row.get(0).unwrap();

            label_count += 1;
            if existing_label == label {
                label_already_used = true;
                break;
            }
        }
    }

    if !label_already_used && label_count >= MAX_LABELS {
        LabelCheck::Label(String::from(OTHER_LABEL))
    } else if label.len() > MAX_LABEL_LENGTH {
        log::warn!(
            "Metric {:?}: label length {} exceeds maximum of {}",
            base_identifier,
            label.len(),
            MAX_LABEL_LENGTH
        );
        LabelCheck::Error(String::from(OTHER_LABEL), 1)
    } else if label.contains(RECORD_SEPARATOR) {
        log::warn!(
            "Metric {:?}: Label cannot contain the ASCII record separator character (0x1E)",
            base_identifier
        );
        LabelCheck::Error(String::from(OTHER_LABEL), 1)
    } else if label.contains(EMPTY_LABEL) {
        log::warn!(
            "Metric {:?}: label can't contain DEL (`\\x7F`)",
            base_identifier,
        );
        LabelCheck::Error(String::from(OTHER_LABEL), 1)
    } else {
        LabelCheck::Label(label.to_string())
    }
}

/// Validates a dynamic label, changing it to `OTHER_LABEL` if it's invalid.
///
/// Checks the requested label against limitations, such as the label length and allowed
/// characters.
///
/// # Returns
///
/// Returns the corrected label.
/// The errors are logged.
#[cfg(feature = "sqlite")]
pub fn validate_dynamic_label_in_memory(
    map: &BTreeMap<String, crate::metrics::Metric>,
    meta: &CommonMetricDataInternal,
    base_identifier: &str,
    mut label: &str,
) -> LabelCheck {
    for store in &meta.inner.send_in_pings {
        let key = [store, base_identifier, label].join("|");
        if map.contains_key(&key) {
            return LabelCheck::Label(label.to_string());
        }
    }

    let mut labels = HashSet::new();
    for store in &meta.inner.send_in_pings {
        let prefix_key = [store, base_identifier, ""].join("|");

        for (key, _) in map
            .range(prefix_key.clone()..)
            .take_while(|(k, _)| k.starts_with(&prefix_key))
        {
            let mut parts = key.splitn(3, '|');
            let Some(_store) = parts.next() else { continue };
            let Some(_name) = parts.next() else { continue };
            let Some(label) = parts.next() else { continue };
            labels.insert(label);
        }
    }

    let label_count = labels.len();
    let error = if label_count >= MAX_LABELS {
        // When there's too many labels no error is recorded,
        // but the `__other__` label is still used!`
        label = OTHER_LABEL;
        false
    } else if label.len() > MAX_LABEL_LENGTH {
        true
    } else {
        false
    };

    if error {
        LabelCheck::Error(OTHER_LABEL.to_string(), 1)
    } else {
        LabelCheck::Label(label.to_string())
    }
}

/// Validates a dynamic label, changing it to `OTHER_LABEL` if it's invalid.
///
/// Checks the requested label against limitations, such as the label length and allowed
/// characters.
///
/// # Returns
///
/// Returns the corrected label.
/// The errors are logged.
#[cfg(not(feature = "sqlite"))]
pub fn validate_dynamic_label_rkv(
    glean: &Glean,
    meta: &CommonMetricDataInternal,
    base_identifier: &str,
    label: &str,
    record: bool,
) -> String {
    let key = combine_base_identifier_and_label(base_identifier, label);
    for store in &meta.inner.send_in_pings {
        if glean.storage().has_metric(meta.inner.lifetime, store, &key) {
            return label.to_string();
        }
    }

    let mut labels = HashSet::new();
    let mut snapshotter = |_metric_id: &[u8], metric_labels: &[&str], _: &Metric| {
        for &label in metric_labels {
            labels.insert(label.to_string());
        }
    };

    let lifetime = meta.inner.lifetime;
    for store in &meta.inner.send_in_pings {
        glean
            .storage()
            .iter_store_from(lifetime, store, Some(base_identifier), &mut snapshotter)
            .ok();
    }

    let label_count = labels.len();
    let error = if label_count >= MAX_LABELS {
        true
    } else if label.len() > MAX_LABEL_LENGTH {
        if record {
            let msg = format!(
                "label length {} exceeds maximum of {}",
                label.len(),
                MAX_LABEL_LENGTH
            );
            record_error(glean, meta, ErrorType::InvalidLabel, msg, None);
        }
        true
    } else if label.contains(RECORD_SEPARATOR) {
        if record {
            let msg =
                String::from("Label cannot contain the ASCII record separator character (0x1E)");
            record_error(glean, meta, ErrorType::InvalidLabel, msg, None);
        }
        true
    } else if label.contains(EMPTY_LABEL) {
        if record {
            let msg = String::from("label can't contain DEL (`\\x7F`)");
            record_error(glean, meta, ErrorType::InvalidLabel, msg, None);
        }
        true
    } else {
        false
    };

    if error {
        OTHER_LABEL.to_string()
    } else {
        label.to_string()
    }
}

/// Combines a metric's base identifier and label
#[cfg(not(feature = "sqlite"))]
pub fn combine_base_identifier_and_label(base_identifier: &str, label: &str) -> String {
    format!("{}/{}", base_identifier, label)
}
