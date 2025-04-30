// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use inherent::inherent;

use super::{
    BaseMetricId, ErrorType, LabeledBooleanMetric, LabeledCounterMetric,
    LabeledCustomDistributionMetric, LabeledMemoryDistributionMetric, LabeledMetricData,
    LabeledQuantityMetric, LabeledStringMetric, LabeledTimingDistributionMetric, SubMetricId,
};
use crate::ipc::need_ipc;
use crate::metrics::__glean_metric_maps::submetric_maps;
use std::borrow::Cow;
use std::marker::PhantomData;
use std::sync::Arc;

/// Sealed traits protect against downstream implementations.
///
/// We wrap it in a private module that is inaccessible outside of this module.
mod private {
    use super::{
        need_ipc, submetric_maps, BaseMetricId, LabeledBooleanMetric, LabeledCounterMetric,
        LabeledCustomDistributionMetric, LabeledMemoryDistributionMetric, LabeledQuantityMetric,
        LabeledStringMetric, LabeledTimingDistributionMetric, SubMetricId,
    };
    use crate::private::labeled_timing_distribution::LabeledTimingDistributionMetricKind;
    use crate::private::{
        BooleanMetric, ChildMetricMeta, CounterMetric, CustomDistributionMetric,
        MemoryDistributionMetric, TimeUnit, TimingDistributionMetric,
    };
    use std::sync::{atomic::Ordering, Arc};

    /// The sealed trait.
    ///
    /// This allows us to define which FOG metrics can be used
    /// as labeled types.
    pub trait Sealed {
        type GleanMetric: glean::private::AllowLabeled + Clone;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId);
    }

    fn submetric_id_for(id: BaseMetricId, label: &str) -> SubMetricId {
        let label_owned = label.to_string();
        let tuple = (id, label_owned);
        let mut map = submetric_maps::LABELED_METRICS_TO_IDS
            .write()
            .expect("write lock of submetric ids was poisoned");

        (*map.entry(tuple).or_insert_with(|| {
            submetric_maps::NEXT_LABELED_SUBMETRIC_ID
                .fetch_add(1, Ordering::SeqCst)
                .into()
        }))
        .into()
    }

    // `LabeledMetric<LabeledBooleanMetric>` is possible.
    //
    // See [Labeled Booleans](https://mozilla.github.io/glean/book/user/metrics/labeled_booleans.html).
    impl Sealed for LabeledBooleanMetric {
        type GleanMetric = glean::private::BooleanMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::BOOLEAN_MAP
                .write()
                .expect("write lock of BOOLEAN_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    if permit_unordered_ipc {
                        LabeledBooleanMetric::UnorderedChild {
                            id: id.into(),
                            label: label.to_string(),
                        }
                    } else {
                        // TODO: Instrument this error.
                        LabeledBooleanMetric::Child
                    }
                } else {
                    LabeledBooleanMetric::Parent(BooleanMetric::Parent {
                        id: submetric_id.into(),
                        inner: metric.get(label),
                    })
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }

    // `LabeledMetric<LabeledStringMetric>` is possible.
    //
    // See [Labeled Strings](https://mozilla.github.io/glean/book/user/metrics/labeled_strings.html).
    impl Sealed for LabeledStringMetric {
        type GleanMetric = glean::private::StringMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            _permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::STRING_MAP
                .write()
                .expect("write lock of STRING_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    // TODO: Instrument this error.
                    LabeledStringMetric::Child(crate::private::string::StringMetricIpc)
                } else {
                    LabeledStringMetric::Parent {
                        id: submetric_id.into(),
                        inner: metric.get(label),
                    }
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }

    // `LabeledMetric<LabeledCounterMetric>` is possible.
    //
    // See [Labeled Counters](https://mozilla.github.io/glean/book/user/metrics/labeled_counters.html).
    impl Sealed for LabeledCounterMetric {
        type GleanMetric = glean::private::CounterMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            _permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::COUNTER_MAP
                .write()
                .expect("write lock of COUNTER_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    LabeledCounterMetric::Child {
                        id: id.into(),
                        label: label.to_string(),
                    }
                } else {
                    LabeledCounterMetric::Parent(CounterMetric::Parent {
                        id: submetric_id.into(),
                        inner: metric.get(label),
                    })
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }

    // `LabeledMetric<LabeledCustomDistributionMetric>` is possible.
    //
    // See [Labeled Custom Distributions](https://mozilla.github.io/glean/book/user/metrics/labeled_custom_distributions.html).
    impl Sealed for LabeledCustomDistributionMetric {
        type GleanMetric = glean::private::CustomDistributionMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            _permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::CUSTOM_DISTRIBUTION_MAP
                .write()
                .expect("write lock of CUSTOM_DISTRIBUTION_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    LabeledCustomDistributionMetric::Child {
                        id: id.into(),
                        label: label.to_string(),
                    }
                } else {
                    LabeledCustomDistributionMetric::Parent(CustomDistributionMetric::Parent {
                        id: submetric_id.into(),
                        inner: metric.get(label),
                    })
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }

    // `LabeledMetric<LabeledMemoryDistributionMetric>` is possible.
    //
    // See [Labeled Memory Distributions](https://mozilla.github.io/glean/book/user/metrics/labeled_memory_distributions.html).
    impl Sealed for LabeledMemoryDistributionMetric {
        type GleanMetric = glean::private::MemoryDistributionMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            _permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::MEMORY_DISTRIBUTION_MAP
                .write()
                .expect("write lock of MEMORY_DISTRIBUTION_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    LabeledMemoryDistributionMetric::Child {
                        id: id.into(),
                        label: label.to_string(),
                    }
                } else {
                    LabeledMemoryDistributionMetric::Parent(MemoryDistributionMetric::Parent {
                        id: submetric_id.into(),
                        inner: metric.get(label),
                    })
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }

    // `LabeledMetric<LabeledTimingDistributionMetric>` is possible.
    //
    // See [Labeled Timing Distributions](https://mozilla.github.io/glean/book/user/metrics/labeled_timing_distributions.html).
    impl Sealed for LabeledTimingDistributionMetric {
        type GleanMetric = glean::private::TimingDistributionMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            _permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::TIMING_DISTRIBUTION_MAP
                .write()
                .expect("write lock of TIMING_DISTRIBUTION_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    use glean::MetricIdentifier;
                    let m = metric.get(label);
                    let (name, category, _) = m.get_identifiers();
                    LabeledTimingDistributionMetric {
                        inner: Arc::new(TimingDistributionMetric::new_child(
                            ChildMetricMeta::from_name_category_pair(id, name, category),
                            TimeUnit::Millisecond,
                        )),
                        parent_id: id,
                        id: submetric_id,
                        label: label.to_string(),
                        kind: LabeledTimingDistributionMetricKind::Child,
                    }
                } else {
                    LabeledTimingDistributionMetric {
                        inner: Arc::new(TimingDistributionMetric::Parent {
                            id: submetric_id.into(),
                            gifft_time_unit: TimeUnit::Millisecond,
                            inner: metric.get(label),
                        }),
                        parent_id: id,
                        id: submetric_id,
                        label: label.to_string(),
                        kind: LabeledTimingDistributionMetricKind::Parent,
                    }
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }

    // `LabeledMetric<LabeledQuantityMetric>` is possible.
    //
    // See [Labeled Quantities](https://mozilla.github.io/glean/book/user/metrics/labeled_quantities.html).
    impl Sealed for LabeledQuantityMetric {
        type GleanMetric = glean::private::QuantityMetric;
        fn from_glean_metric(
            id: BaseMetricId,
            metric: &glean::private::LabeledMetric<Self::GleanMetric>,
            label: &str,
            _permit_unordered_ipc: bool,
        ) -> (Arc<Self>, SubMetricId) {
            let submetric_id = submetric_id_for(id, label);
            let mut map = submetric_maps::QUANTITY_MAP
                .write()
                .expect("write lock of QUANTITY_MAP was poisoned");
            let submetric = map.entry(submetric_id).or_insert_with(|| {
                let submetric = if need_ipc() {
                    // TODO: Instrument this error.
                    LabeledQuantityMetric::Child(crate::private::quantity::QuantityMetricIpc)
                } else {
                    LabeledQuantityMetric::Parent {
                        id: submetric_id.into(),
                        inner: metric.get(label),
                    }
                };
                Arc::new(submetric)
            });
            (Arc::clone(submetric), submetric_id)
        }
    }
}

/// Marker trait for metrics that can be nested inside a labeled metric.
///
/// This trait is sealed and cannot be implemented for types outside this crate.
pub trait AllowLabeled: private::Sealed {}

// Implement the trait for everything we marked as allowed.
impl<T> AllowLabeled for T where T: private::Sealed {}

/// A labeled metric.
///
/// Labeled metrics allow to record multiple sub-metrics of the same type under different string labels.
///
/// ## Example
///
/// The following piece of code will be generated by `glean_parser`:
///
/// ```rust,ignore
/// use glean::metrics::{LabeledMetric, BooleanMetric, CommonMetricData, LabeledMetricData, Lifetime};
/// use once_cell::sync::Lazy;
///
/// mod error {
///     pub static seen_one: Lazy<LabeledMetric<BooleanMetric, DynamicLabel>> = Lazy::new(|| LabeledMetric::new(LabeledMetricData::Common{ cmd: CommonMetricData {
///         name: "seen_one".into(),
///         category: "error".into(),
///         send_in_pings: vec!["ping".into()],
///         disabled: false,
///         lifetime: Lifetime::Ping,
///         ..Default::default()
///     }}, None));
/// }
/// ```
///
/// It can then be used with:
///
/// ```rust,ignore
/// errro::seen_one.get("upload").set(true);
/// ```
pub struct LabeledMetric<T: AllowLabeled, E> {
    /// The metric ID of the underlying metric.
    id: BaseMetricId,

    /// Wrapping the underlying core metric.
    ///
    /// We delegate all functionality to this and wrap it up again in our own metric type.
    core: glean::private::LabeledMetric<T::GleanMetric>,

    label_enum: PhantomData<E>,

    /// Whether this labeled_* metric is permitted to perform non-commutative
    /// metric operations over unordered IPC.
    permit_unordered_ipc: bool,
}

impl<T, E> LabeledMetric<T, E>
where
    T: AllowLabeled + Clone,
{
    /// Create a new labeled metric from the given metric instance and optional list of labels.
    ///
    /// See [`get`](#method.get) for information on how static or dynamic labels are handled.
    pub fn new(
        id: BaseMetricId,
        meta: LabeledMetricData,
        labels: Option<Vec<Cow<'static, str>>>,
    ) -> LabeledMetric<T, E> {
        let core = glean::private::LabeledMetric::new(meta, labels);
        LabeledMetric {
            id,
            core,
            label_enum: PhantomData,
            permit_unordered_ipc: false,
        }
    }

    pub fn with_unordered_ipc(
        id: BaseMetricId,
        meta: LabeledMetricData,
        labels: Option<Vec<Cow<'static, str>>>,
    ) -> LabeledMetric<T, E> {
        let core = glean::private::LabeledMetric::new(meta, labels);
        LabeledMetric {
            id,
            core,
            label_enum: PhantomData,
            permit_unordered_ipc: true,
        }
    }

    pub(crate) fn get_submetric_id(&self, label: &str) -> u32 {
        T::from_glean_metric(self.id, &self.core, label, self.permit_unordered_ipc)
            .1
             .0
    }
}

#[inherent]
impl<U, E> glean::traits::Labeled<Arc<U>> for LabeledMetric<U, E>
where
    U: AllowLabeled + Clone,
{
    /// Gets a specific metric for a given label.
    ///
    /// If a set of acceptable labels were specified in the `metrics.yaml` file,
    /// and the given label is not in the set, it will be recorded under the special `OTHER_LABEL` label.
    ///
    /// If a set of acceptable labels was not specified in the `metrics.yaml` file,
    /// only the first 16 unique labels will be used.
    /// After that, any additional labels will be recorded under the special `OTHER_LABEL` label.
    ///
    /// Labels must be `snake_case` and less than 30 characters.
    /// If an invalid label is used, the metric will be recorded in the special `OTHER_LABEL` label.
    pub fn get(&self, label: &str) -> Arc<U> {
        U::from_glean_metric(self.id, &self.core, label, self.permit_unordered_ipc).0
    }

    /// **Exported for test purposes.**
    ///
    /// Gets the number of recorded errors for the given metric and error type.
    ///
    /// # Arguments
    ///
    /// * `error` - The type of error
    /// * `ping_name` - represents the optional name of the ping to retrieve the
    ///   metric for. Defaults to the first value in `send_in_pings`.
    ///
    /// # Returns
    ///
    /// The number of errors reported.
    pub fn test_get_num_recorded_errors(&self, error: ErrorType) -> i32 {
        if need_ipc() {
            panic!("Use of labeled metrics in IPC land not yet implemented!");
        } else {
            self.core.test_get_num_recorded_errors(error)
        }
    }
}

#[cfg(test)]
mod test {
    use once_cell::sync::Lazy;

    use super::*;
    use crate::common_test::*;
    use crate::metrics::DynamicLabel;
    use crate::private::CommonMetricData;

    // Smoke test for what should be the generated code.
    static GLOBAL_METRIC: Lazy<LabeledMetric<LabeledBooleanMetric, DynamicLabel>> =
        Lazy::new(|| {
            LabeledMetric::new(
                0.into(),
                LabeledMetricData::Common {
                    cmd: CommonMetricData {
                        name: "global".into(),
                        category: "metric".into(),
                        send_in_pings: vec!["test-ping".into()],
                        disabled: false,
                        ..Default::default()
                    },
                },
                None,
            )
        });

    #[test]
    fn smoke_test_global_metric() {
        let _lock = lock_test();

        GLOBAL_METRIC.get("a_value").set(true);
        assert_eq!(
            true,
            GLOBAL_METRIC
                .get("a_value")
                .test_get_value("test-ping")
                .unwrap()
        );
    }

    #[test]
    fn sets_labeled_bool_metrics() {
        let _lock = lock_test();
        let store_names: Vec<String> = vec!["test-ping".into()];

        let metric: LabeledMetric<LabeledBooleanMetric, DynamicLabel> = LabeledMetric::new(
            0.into(),
            LabeledMetricData::Common {
                cmd: CommonMetricData {
                    name: "bool".into(),
                    category: "labeled".into(),
                    send_in_pings: store_names,
                    disabled: false,
                    ..Default::default()
                },
            },
            None,
        );

        metric.get("upload").set(true);

        assert!(metric.get("upload").test_get_value("test-ping").unwrap());
        assert_eq!(None, metric.get("download").test_get_value("test-ping"));
    }

    #[test]
    fn sets_labeled_string_metrics() {
        let _lock = lock_test();
        let store_names: Vec<String> = vec!["test-ping".into()];

        let metric: LabeledMetric<LabeledStringMetric, DynamicLabel> = LabeledMetric::new(
            0.into(),
            LabeledMetricData::Common {
                cmd: CommonMetricData {
                    name: "string".into(),
                    category: "labeled".into(),
                    send_in_pings: store_names,
                    disabled: false,
                    ..Default::default()
                },
            },
            None,
        );

        metric.get("upload").set("Glean");

        assert_eq!(
            "Glean",
            metric.get("upload").test_get_value("test-ping").unwrap()
        );
        assert_eq!(None, metric.get("download").test_get_value("test-ping"));
    }

    #[test]
    fn sets_labeled_counter_metrics() {
        let _lock = lock_test();
        let store_names: Vec<String> = vec!["test-ping".into()];

        let metric: LabeledMetric<LabeledCounterMetric, DynamicLabel> = LabeledMetric::new(
            0.into(),
            LabeledMetricData::Common {
                cmd: CommonMetricData {
                    name: "counter".into(),
                    category: "labeled".into(),
                    send_in_pings: store_names,
                    disabled: false,
                    ..Default::default()
                },
            },
            None,
        );

        metric.get("upload").add(10);

        assert_eq!(
            10,
            metric.get("upload").test_get_value("test-ping").unwrap()
        );
        assert_eq!(None, metric.get("download").test_get_value("test-ping"));
    }

    #[test]
    fn records_errors() {
        let _lock = lock_test();
        let store_names: Vec<String> = vec!["test-ping".into()];

        let metric: LabeledMetric<LabeledBooleanMetric, DynamicLabel> = LabeledMetric::new(
            0.into(),
            LabeledMetricData::Common {
                cmd: CommonMetricData {
                    name: "bool".into(),
                    category: "labeled".into(),
                    send_in_pings: store_names,
                    disabled: false,
                    ..Default::default()
                },
            },
            None,
        );

        metric.get(&"1".repeat(112)).set(true);

        assert_eq!(
            1,
            metric.test_get_num_recorded_errors(ErrorType::InvalidLabel)
        );
    }

    #[test]
    fn predefined_labels() {
        let _lock = lock_test();
        let store_names: Vec<String> = vec!["test-ping".into()];

        #[allow(dead_code)]
        enum MetricLabels {
            Label1 = 0,
            Label2 = 1,
        }
        let metric: LabeledMetric<LabeledBooleanMetric, MetricLabels> = LabeledMetric::new(
            0.into(),
            LabeledMetricData::Common {
                cmd: CommonMetricData {
                    name: "bool".into(),
                    category: "labeled".into(),
                    send_in_pings: store_names,
                    disabled: false,
                    ..Default::default()
                },
            },
            Some(vec!["label1".into(), "label2".into()]),
        );

        metric.get("label1").set(true);
        metric.get("label2").set(false);
        metric.get("not_a_label").set(true);

        assert_eq!(
            true,
            metric.get("label1").test_get_value("test-ping").unwrap()
        );
        assert_eq!(
            false,
            metric.get("label2").test_get_value("test-ping").unwrap()
        );
        // The label not in the predefined set is recorded to the `other` bucket.
        assert_eq!(
            true,
            metric.get("__other__").test_get_value("test-ping").unwrap()
        );

        assert_eq!(
            0,
            metric.test_get_num_recorded_errors(ErrorType::InvalidLabel)
        );
    }
}
