// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use inherent::inherent;

use crate::ipc::with_ipc_payload;
use crate::private::{
    BaseMetric, BaseMetricId, BaseMetricResult, DistributionData, MemoryDistributionMetric,
    MetricMetadataGetterImpl,
};
use glean::traits::MemoryDistribution;
use std::collections::HashMap;

#[cfg(feature = "with_gecko")]
use super::profiler_utils::{
    truncate_vector_for_marker, DistributionMetricMarker, DistributionValues,
    TelemetryProfilerCategory,
};

/// A memory distribution metric that knows it's a labeled memory distribution's submetric.
///
/// It has special work to do when in a non-parent process.
/// (When on the parent process it merely dispatches calls to the normal MemoryDistributionMetric.)
#[derive(Clone)]
pub enum LabeledMemoryDistributionMetric {
    Parent(MemoryDistributionMetric),
    Child { id: BaseMetricId, label: String },
}

define_metric_metadata_getter!(
    MemoryDistributionMetric,
    LabeledMemoryDistributionMetric,
    MEMORY_DISTRIBUTION_MAP,
    LABELED_MEMORY_DISTRIBUTION_MAP
);

define_metric_namer!(LabeledMemoryDistributionMetric, LABELED);

impl LabeledMemoryDistributionMetric {
    #[cfg(test)]
    pub(crate) fn metric_id(&self) -> crate::private::MetricId {
        match self {
            LabeledMemoryDistributionMetric::Parent(p) => p.metric_id(),
            LabeledMemoryDistributionMetric::Child { id, .. } => (*id).into(),
        }
    }

    pub fn accumulate_samples_unsigned(&self, samples: Vec<u64>) {
        match self {
            LabeledMemoryDistributionMetric::Parent(p) => p.accumulate_samples_unsigned(samples),
            LabeledMemoryDistributionMetric::Child { id, label } => {
                #[cfg(feature = "with_gecko")]
                if gecko_profiler::current_thread_is_being_profiled_for_markers() {
                    gecko_profiler::add_marker(
                        "MemoryDistribution::accumulate",
                        TelemetryProfilerCategory,
                        Default::default(),
                        DistributionMetricMarker::<LabeledMemoryDistributionMetric, u64>::new(
                            (*id).into(),
                            Some(label.clone()),
                            DistributionValues::Samples(truncate_vector_for_marker(&samples)),
                        ),
                    );
                }
                with_ipc_payload(move |payload| {
                    if let Some(map) = payload.labeled_memory_samples.get_mut(id) {
                        if let Some(v) = map.get_mut(label) {
                            v.extend(samples);
                        } else {
                            map.insert(label.to_string(), samples);
                        }
                    } else {
                        let mut map = HashMap::new();
                        map.insert(label.to_string(), samples);
                        payload.labeled_memory_samples.insert(*id, map);
                    }
                });
            }
        }
    }
}

#[inherent]
impl MemoryDistribution for LabeledMemoryDistributionMetric {
    pub fn accumulate(&self, sample: u64) {
        match self {
            LabeledMemoryDistributionMetric::Parent(p) => p.accumulate(sample),
            LabeledMemoryDistributionMetric::Child { id, label } => {
                #[cfg(feature = "with_gecko")]
                if gecko_profiler::current_thread_is_being_profiled_for_markers() {
                    gecko_profiler::add_marker(
                        "MemoryDistribution::accumulate",
                        TelemetryProfilerCategory,
                        Default::default(),
                        DistributionMetricMarker::<LabeledMemoryDistributionMetric, u64>::new(
                            (*id).into(),
                            Some(label.clone()),
                            DistributionValues::Sample(sample),
                        ),
                    );
                }
                with_ipc_payload(move |payload| {
                    if let Some(map) = payload.labeled_memory_samples.get_mut(id) {
                        if let Some(v) = map.get_mut(label) {
                            v.push(sample);
                        } else {
                            map.insert(label.to_string(), vec![sample]);
                        }
                    } else {
                        let mut map = HashMap::new();
                        map.insert(label.to_string(), vec![sample]);
                        payload.labeled_memory_samples.insert(*id, map);
                    }
                });
            }
        }
    }

    pub fn accumulate_samples(&self, samples: Vec<i64>) {
        self.accumulate_samples_unsigned(samples.into_iter().map(|s| s as _).collect());
    }

    pub fn test_get_num_recorded_errors(&self, error: glean::ErrorType) -> i32 {
        match self {
            LabeledMemoryDistributionMetric::Parent(p) => p.test_get_num_recorded_errors(error),
            LabeledMemoryDistributionMetric::Child {id, .. } => panic!(
                "Cannot get the number of recorded errors for labeled_memory_distribution {:?} in non-parent process!",
                id
            ),
        }
    }
}

#[inherent]
impl glean::TestGetValue for LabeledMemoryDistributionMetric {
    type Output = DistributionData;

    pub fn test_get_value(&self, ping_name: Option<String>) -> Option<DistributionData> {
        match self {
            LabeledMemoryDistributionMetric::Parent(p) => p.test_get_value(ping_name),
            LabeledMemoryDistributionMetric::Child { id, .. } => {
                panic!("Cannot get test value for labeled_memory_distribution {:?} in non-parent process!", id)
            }
        }
    }
}

impl BaseMetric for LabeledMemoryDistributionMetric {
    type BaseMetricT = MemoryDistributionMetric;
    fn get_base_metric<'a>(&'a self) -> BaseMetricResult<'a, Self::BaseMetricT> {
        match self {
            LabeledMemoryDistributionMetric::Parent(memory_distribution_metric) => {
                BaseMetricResult::BaseMetric(memory_distribution_metric)
            }
            LabeledMemoryDistributionMetric::Child { id, label } => {
                BaseMetricResult::IndexLabelPair(*id, label)
            }
        }
    }
}

#[cfg(test)]
mod test {
    use crate::{common_test::*, ipc, metrics};

    #[test]
    fn smoke_test_labeled_memory_distribution() {
        let _lock = lock_test();

        let metric = &metrics::test_only::what_do_you_remember;

        metric.get("happy").accumulate(42);

        let metric_data = metric.get("happy").test_get_value(None).unwrap();
        assert_eq!(1, metric_data.values[&43514714]);
        assert_eq!(44040192, metric_data.sum);
    }

    #[test]
    fn memory_distribution_child() {
        let _lock = lock_test();

        let label = "nostalgic";

        let parent_metric = &metrics::test_only::what_do_you_remember;
        parent_metric.get(label).accumulate(42);

        {
            // scope for need_ipc RAII
            let _raii = ipc::test_set_need_ipc(true);

            // clear the per-process submetric cache,
            // or else we'll be given the parent-process child metric.
            {
                let mut map =
                    crate::metrics::__glean_metric_maps::submetric_maps::MEMORY_DISTRIBUTION_MAP
                        .write()
                        .expect("Write lock for MEMORY_DISTRIBUTION_MAP was poisoned");
                map.clear();
            }

            let child_metric = parent_metric.get(label);
            child_metric.accumulate(13 * 9);

            ipc::with_ipc_payload(move |payload| {
                assert_eq!(
                    vec![13 * 9],
                    *payload
                        .labeled_memory_samples
                        .get(
                            &child_metric
                                .metric_id()
                                .base_metric_id()
                                .expect("Cannot perform IPC calls without a BaseMetricId")
                        )
                        .unwrap()
                        .get(label)
                        .unwrap(),
                    "Stored the correct value in the ipc payload"
                );
            });

            // clear the per-process submetric cache again,
            // or else we'll be given the child-process child metric below.
            {
                let mut map =
                    crate::metrics::__glean_metric_maps::submetric_maps::MEMORY_DISTRIBUTION_MAP
                        .write()
                        .expect("Write lock for MEMORY_DISTRIBUTION_MAP was poisoned");
                map.clear();
            }
        }

        let metric_data = parent_metric.get(label).test_get_value(None).unwrap();
        assert_eq!(1, metric_data.values[&43514714]);
        assert_eq!(44040192, metric_data.sum);

        // Single-process IPC machine goes brrrrr...
        let buf = ipc::take_buf().unwrap();
        assert!(buf.len() > 0);
        assert!(ipc::replay_from_buf(&buf).is_ok());

        let data = parent_metric
            .get(label)
            .test_get_value(None)
            .expect("must have data");
        assert_eq!(2, data.values.values().fold(0, |acc, count| acc + count));
        assert_eq!(1, data.values[&43514714]);
        assert_eq!(1, data.values[&117860087]);
        assert_eq!(166723584, data.sum);
    }
}
