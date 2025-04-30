// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::borrow::Cow;
use std::collections::{hash_map::Entry, HashMap};
use std::sync::{Arc, Mutex};

use crate::common_metric_data::{CommonMetricData, CommonMetricDataInternal};
use crate::error_recording::{record_error, test_get_num_recorded_errors, ErrorType};
use crate::histogram::HistogramType;
use crate::metrics::{
    BooleanMetric, CounterMetric, CustomDistributionMetric, MemoryDistributionMetric, MemoryUnit,
    Metric, MetricType, QuantityMetric, StringMetric, TimeUnit, TimingDistributionMetric,
};
use crate::Glean;

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
    fn new_metric_with_name(&self, name: String) -> T {
        self.submetric.with_name(name)
    }

    /// Creates a new metric with a specific label.
    ///
    /// This is used for dynamic labels where we have to actually validate and correct the
    /// label later when we have a Glean object.
    fn new_metric_with_dynamic_label(&self, label: String) -> T {
        self.submetric.with_dynamic_label(label)
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
                        self.new_metric_with_name(combine_base_identifier_and_label(
                            &self.submetric.meta().inner.name,
                            label,
                        ))
                    }
                    None => self.new_metric_with_dynamic_label(label.to_string()),
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

/// Combines a metric's base identifier and label
pub fn combine_base_identifier_and_label(base_identifer: &str, label: &str) -> String {
    format!("{}/{}", base_identifer, label)
}

/// Strips the label off of a complete identifier
pub fn strip_label(identifier: &str) -> &str {
    identifier.split_once('/').map_or(identifier, |s| s.0)
}

/// Validates a dynamic label, changing it to `OTHER_LABEL` if it's invalid.
///
/// Checks the requested label against limitations, such as the label length and allowed
/// characters.
///
/// # Arguments
///
/// * `label` - The requested label
///
/// # Returns
///
/// The entire identifier for the metric, including the base identifier and the corrected label.
/// The errors are logged.
pub fn validate_dynamic_label(
    glean: &Glean,
    meta: &CommonMetricDataInternal,
    base_identifier: &str,
    label: &str,
) -> String {
    let key = combine_base_identifier_and_label(base_identifier, label);
    for store in &meta.inner.send_in_pings {
        if glean.storage().has_metric(meta.inner.lifetime, store, &key) {
            return key;
        }
    }

    let mut label_count = 0;
    let prefix = &key[..=base_identifier.len()];
    let mut snapshotter = |_: &[u8], _: &Metric| {
        label_count += 1;
    };

    let lifetime = meta.inner.lifetime;
    for store in &meta.inner.send_in_pings {
        glean
            .storage()
            .iter_store_from(lifetime, store, Some(prefix), &mut snapshotter);
    }

    let error = if label_count >= MAX_LABELS {
        true
    } else if label.len() > MAX_LABEL_LENGTH {
        let msg = format!(
            "label length {} exceeds maximum of {}",
            label.len(),
            MAX_LABEL_LENGTH
        );
        record_error(glean, meta, ErrorType::InvalidLabel, msg, None);
        true
    } else if label.chars().any(|c| !c.is_ascii() || c.is_ascii_control()) {
        let msg = format!("label must be printable ascii, got '{}'", label);
        record_error(glean, meta, ErrorType::InvalidLabel, msg, None);
        true
    } else {
        false
    };

    if error {
        combine_base_identifier_and_label(base_identifier, OTHER_LABEL)
    } else {
        key
    }
}
