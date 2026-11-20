// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::sync::Arc;

use crate::common_metric_data::{CommonMetricDataInternal, MetricLabel};
use crate::error_recording::{test_get_num_recorded_errors, ErrorType};
use crate::metrics::Metric;
use crate::metrics::MetricType;
use crate::util::truncate_string_at_boundary_with_error;
use crate::Glean;
use crate::{CommonMetricData, TestGetValue};

pub const MAX_LENGTH_VALUE: usize = 255;

/// A string metric.
///
/// Record an Unicode string value with arbitrary content.
/// Strings are length-limited to `MAX_LENGTH_VALUE` bytes.
#[derive(Clone, Debug)]
pub struct StringMetric {
    meta: Arc<CommonMetricDataInternal>,
}

impl MetricType for StringMetric {
    fn meta(&self) -> &CommonMetricDataInternal {
        &self.meta
    }

    fn with_name(&self, name: String) -> Self {
        let mut meta = (*self.meta).clone();
        meta.inner.name = name;
        Self {
            meta: Arc::new(meta),
        }
    }

    fn with_label(&self, label: MetricLabel) -> Self {
        let mut meta = (*self.meta).clone();
        meta.inner.label = Some(label);
        Self {
            meta: Arc::new(meta),
        }
    }
}

// IMPORTANT:
//
// When changing this implementation, make sure all the operations are
// also declared in the related trait in `../traits/`.
impl StringMetric {
    /// Creates a new string metric.
    pub fn new(meta: CommonMetricData) -> Self {
        Self {
            meta: Arc::new(meta.into()),
        }
    }

    /// Sets to the specified value.
    ///
    /// # Arguments
    ///
    /// * `value` - The string to set the metric to.
    ///
    /// ## Notes
    ///
    /// Truncates the value if it is longer than `MAX_LENGTH_VALUE` bytes and logs an error.
    pub fn set(&self, value: String) {
        let metric = self.clone();
        crate::launch_with_glean(move |glean| metric.set_sync(glean, &value))
    }

    /// Sets to the specified value synchronously.
    #[doc(hidden)]
    pub fn set_sync<S: Into<String>>(&self, glean: &Glean, value: S) {
        self.set_sync_with_max_length(glean, value, MAX_LENGTH_VALUE)
    }

    /// Sets the value synchronously with a custom byte limit for internal metrics.
    pub(crate) fn set_sync_with_max_length<S: Into<String>>(
        &self,
        glean: &Glean,
        value: S,
        max_length: usize,
    ) {
        if !self.should_record(glean) {
            return;
        }

        let s = truncate_string_at_boundary_with_error(glean, &self.meta, value, max_length);

        let value = Metric::String(s);
        glean.storage().record(glean, &self.meta, &value)
    }

    /// Gets the current-stored value as a string, or None if there is no value.
    #[doc(hidden)]
    pub fn get_value<'a, S: Into<Option<&'a str>>>(
        &self,
        glean: &Glean,
        ping_name: S,
    ) -> Option<String> {
        let queried_ping_name = ping_name
            .into()
            .unwrap_or_else(|| &self.meta().inner.send_in_pings[0]);

        match glean.storage().get_metric(
            #[cfg(not(feature = "sqlite"))]
            glean,
            self.meta(),
            queried_ping_name,
        ) {
            Some(Metric::String(s)) => Some(s),
            _ => None,
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

        crate::core::with_glean(|glean| self.test_get_num_recorded_errors_sync(glean, error))
    }

    /// Gets the number of recorded errors synchronously for the given Glean instance.
    pub(crate) fn test_get_num_recorded_errors_sync(&self, glean: &Glean, error: ErrorType) -> i32 {
        test_get_num_recorded_errors(glean, self.meta(), error).unwrap_or(0)
    }
}

impl TestGetValue for StringMetric {
    type Output = String;

    /// **Test-only API (exported for FFI purposes).**
    ///
    /// Gets the currently stored value as a string.
    ///
    /// This doesn't clear the stored value.
    ///
    /// # Arguments
    ///
    /// * `ping_name` - the optional name of the ping to retrieve the metric
    ///                 for. Defaults to the first value in `send_in_pings`.
    ///
    /// # Returns
    ///
    /// The stored value or `None` if nothing stored.
    fn test_get_value(&self, ping_name: Option<String>) -> Option<String> {
        crate::block_on_dispatcher();
        crate::core::with_glean(|glean| self.get_value(glean, ping_name.as_deref()))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::tests::new_glean;
    use crate::util::truncate_string_at_boundary;
    use crate::Lifetime;

    #[test]
    fn setting_a_long_string_records_an_error() {
        let (glean, _t) = new_glean(None);

        let metric = StringMetric::new(CommonMetricData {
            name: "string_metric".into(),
            category: "test".into(),
            send_in_pings: vec!["store1".into()],
            lifetime: Lifetime::Application,
            disabled: false,
            label: None,
            in_session: false,
        });

        let sample_string = "0123456789".repeat(26);
        metric.set_sync(&glean, sample_string.clone());

        let truncated = truncate_string_at_boundary(sample_string, MAX_LENGTH_VALUE);
        assert_eq!(truncated, metric.get_value(&glean, "store1").unwrap());

        assert_eq!(
            1,
            test_get_num_recorded_errors(&glean, metric.meta(), ErrorType::InvalidOverflow)
                .unwrap()
        );
    }
}
