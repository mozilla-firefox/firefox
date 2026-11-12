// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use inherent::inherent;

use super::{BaseMetricId, ChildMetricMeta, CommonMetricData};

use glean::traits::StringList;

use crate::ipc::{need_ipc, with_ipc_payload};

/// A string list metric.
///
/// This allows appending a string value with arbitrary content to a list.
#[derive(Clone)]
pub enum StringListMetric {
    Parent {
        /// The metric's ID. Used for testing and profiler markers. String
        /// list metrics canot be labeled, so we only store a BaseMetricId. If
        /// this changes, this should be changed to a MetricId to
        /// distinguish between metrics and sub-metrics.
        id: BaseMetricId,
        inner: glean::private::StringListMetric,
    },
    Child(ChildMetricMeta),
}

define_metric_metadata_getter!(StringListMetric, STRING_LIST_MAP);
define_metric_namer!(StringListMetric);

impl StringListMetric {
    /// Create a new string list metric.
    pub fn new(id: BaseMetricId, meta: CommonMetricData) -> Self {
        if need_ipc() {
            StringListMetric::Child(ChildMetricMeta::from_common_metric_data(id, meta))
        } else {
            let inner = glean::private::StringListMetric::new(meta);
            StringListMetric::Parent { id, inner }
        }
    }

    #[cfg(test)]
    pub(crate) fn child_metric(&self) -> Self {
        match self {
            StringListMetric::Parent { id, inner } => {
                StringListMetric::Child(ChildMetricMeta::from_metric_identifier(*id, inner))
            }
            StringListMetric::Child(_) => panic!("Can't get a child metric from a child metric"),
        }
    }
}

#[inherent]
impl StringList for StringListMetric {
    /// Add a new string to the list.
    ///
    /// ## Arguments
    ///
    /// * `value` - The string to add.
    ///
    /// ## Notes
    ///
    /// Truncates the value if it is longer than `MAX_STRING_LENGTH` bytes and logs an error.
    /// See [String list metric limits](https://mozilla.github.io/glean/book/user/metrics/string_list.html#limits).
    pub fn add<S: Into<String>>(&self, value: S) {
        match self {
            #[allow(unused)]
            StringListMetric::Parent { id, inner } => {
                let value = value.into();
                #[cfg(feature = "with_gecko")]
                gecko_profiler::lazy_add_marker!(
                    "StringList::add",
                    super::profiler_utils::TelemetryProfilerCategory,
                    super::profiler_utils::StringLikeMetricMarker::<StringListMetric>::new(
                        (*id).into(),
                        &value
                    )
                );
                inner.add(value);
            }
            StringListMetric::Child(meta) => {
                with_ipc_payload(move |payload| {
                    if let Some(v) = payload.string_lists.get_mut(&meta.id) {
                        v.push(value.into());
                    } else {
                        let v = vec![value.into()];
                        payload.string_lists.insert(meta.id, v);
                    }
                });
            }
        }
    }

    /// Set to a specific list of strings.
    ///
    /// ## Arguments
    ///
    /// * `value` - The list of string to set the metric to.
    ///
    /// ## Notes
    ///
    /// If passed an empty list, records an error and returns.
    /// Truncates the list if it is longer than `MAX_LIST_LENGTH` and logs an error.
    /// Truncates any value in the list if it is longer than `MAX_STRING_LENGTH` and logs an error.
    pub fn set(&self, value: Vec<String>) {
        match self {
            #[allow(unused)]
            StringListMetric::Parent { id, inner } => {
                #[cfg(feature = "with_gecko")]
                gecko_profiler::lazy_add_marker!(
                    "StringList::set",
                    super::profiler_utils::TelemetryProfilerCategory,
                    super::profiler_utils::StringLikeMetricMarker::<StringListMetric>::new_owned(
                        (*id).into(),
                        format!("[{}]", value.clone().join(","))
                    )
                );
                inner.set(value);
            }
            StringListMetric::Child(meta) => {
                log::error!(
                    "Unable to set string list metric {:?} in non-main process. This operation will be ignored.",
                    meta.id
                );
                // If we're in automation we can panic so the instrumentor knows they've gone wrong.
                // This is a deliberate violation of Glean's "metric APIs must not throw" design.assert!(!crate::ipc::is_in_automation());
                assert!(!crate::ipc::is_in_automation(), "Attempted to set string list metric in non-main process, which is forbidden. This panics in automation.");
                // TODO: Record an error.
            }
        }
    }

    /// **Exported for test purposes.**
    ///
    /// Gets the number of recorded errors for the given error type.
    ///
    /// # Arguments
    ///
    /// * `error` - The type of error
    /// * `ping_name` - represents the optional name of the ping to retrieve the
    ///   metric for. Defaults to the first value in `send_in_pings`.
    ///
    /// # Returns
    ///
    /// The number of errors recorded.
    pub fn test_get_num_recorded_errors(&self, error: glean::ErrorType) -> i32 {
        match self {
            StringListMetric::Parent { inner, .. } => inner.test_get_num_recorded_errors(error),
            StringListMetric::Child(meta) => panic!(
                "Cannot get the number of recorded errors for {:?} in non-parent process!",
                meta.id
            ),
        }
    }
}

#[inherent]
impl glean::TestGetValue for StringListMetric {
    type Output = Vec<String>;

    /// **Test-only API.**
    ///
    /// Get the currently stored values.
    /// This doesn't clear the stored value.
    ///
    /// ## Arguments
    ///
    /// * `storage_name` - the storage name to look into.
    ///
    /// ## Return value
    ///
    /// Returns the stored value or `None` if nothing stored.
    pub fn test_get_value(&self, ping_name: Option<String>) -> Option<Vec<String>> {
        match self {
            StringListMetric::Parent { inner, .. } => inner.test_get_value(ping_name),
            StringListMetric::Child(meta) => {
                panic!(
                    "Cannot get test value for {:?} in non-parent process!",
                    meta.id
                )
            }
        }
    }
}

#[cfg(test)]
mod test {
    use crate::{common_test::*, ipc, metrics};

    #[test]
    fn sets_string_list_value() {
        let _lock = lock_test();

        let metric = &metrics::test_only_ipc::a_string_list;

        metric.set(vec!["test_string_value".to_string()]);
        metric.add("another test value");

        assert_eq!(
            vec!["test_string_value", "another test value"],
            metric
                .test_get_value(Some("test-ping".to_string()))
                .unwrap()
        );
    }

    #[test]
    fn string_list_ipc() {
        // StringListMetric supports IPC only for `add`, not `set`.
        let _lock = lock_test();

        let parent_metric = &metrics::test_only_ipc::a_string_list;

        parent_metric.set(vec!["test_string_value".to_string()]);
        parent_metric.add("another test value");

        {
            let child_metric = parent_metric.child_metric();

            // scope for need_ipc RAII
            let _raii = ipc::test_set_need_ipc(true);

            // Recording APIs do not panic, even when they don't work.
            child_metric.set(vec!["not gonna be set".to_string()]);

            child_metric.add("child_value");
        }

        // Perform the replay and assert we have the values from both "processes".
        assert!(ipc::replay_from_buf(&ipc::take_buf().unwrap()).is_ok());
        assert_eq!(
            vec!["test_string_value", "another test value", "child_value"],
            parent_metric
                .test_get_value(Some("test-ping".to_string()))
                .unwrap()
        );
    }
}
