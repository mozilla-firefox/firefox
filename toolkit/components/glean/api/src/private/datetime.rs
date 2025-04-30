// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use inherent::inherent;

use super::{BaseMetricId, ChildMetricMeta, CommonMetricData};

use super::TimeUnit;
use crate::ipc::need_ipc;
use chrono::{FixedOffset, TimeZone};
use glean::traits::Datetime;

#[cfg(feature = "with_gecko")]
use super::profiler_utils::{
    glean_to_chrono_datetime, local_now_with_offset, stream_identifiers_by_id,
    TelemetryProfilerCategory,
};

#[cfg(feature = "with_gecko")]
#[derive(serde::Serialize, serde::Deserialize, Debug)]
struct DatetimeMetricMarker {
    id: BaseMetricId,
    time: chrono::DateTime<FixedOffset>,
}

#[cfg(feature = "with_gecko")]
impl gecko_profiler::ProfilerMarker for DatetimeMetricMarker {
    fn marker_type_name() -> &'static str {
        "DatetimeMetric"
    }

    fn marker_type_display() -> gecko_profiler::MarkerSchema {
        use gecko_profiler::schema::*;
        let mut schema = MarkerSchema::new(&[Location::MarkerChart, Location::MarkerTable]);
        schema.set_tooltip_label("{marker.data.cat}.{marker.data.id} {marker.data.time}");
        schema.set_table_label("{marker.data.cat}.{marker.data.id}: {marker.data.time}");
        schema.add_key_label_format_searchable(
            "cat",
            "Category",
            Format::UniqueString,
            Searchable::Searchable,
        );
        schema.add_key_label_format_searchable(
            "id",
            "Metric",
            Format::UniqueString,
            Searchable::Searchable,
        );
        // Note: there is no native profiler format for timestamps.
        // Bug 1926644 tracks the work of adding this.
        schema.add_key_label_format("time", "Time", Format::String);
        schema
    }

    fn stream_json_marker_data(&self, json_writer: &mut gecko_profiler::JSONWriter) {
        stream_identifiers_by_id::<DatetimeMetric>(&self.id.into(), json_writer);
        // We need to be careful formatting our datestring so that we can match
        // it to an equivalently formatted string in JavaScript when we're
        // testing these markers. JavaScript's `toISOString` *always* converts
        // to a string in line with the "date time string format", which
        // is /like/ but not the /same/ as ISO 8601 format. It is simplified,
        // and when printing with `toISOString`, JS always prints with the zone
        // offset set to `Z`, i.e. UTC. Therefore, we need to convert the date
        // into a naive date time, i.e. one that is unaware of timezones, and
        // then format it according to the JS date time format. For more
        // details, see the following spec:
        // https://tc39.es/ecma262/multipage/numbers-and-dates.html#sec-date-time-string-format
        let naive = self.time.naive_utc();
        // We could use the default `%+` formatting for ISO 8601, but let's be
        // specific instead, and use the precise JS format, which is
        // `YYYY-MM-DDTHH:mm:ss.sssZ`. Note that this is expressed differently
        // in the language of chrono's formatting library. For details see:
        // https://docs.rs/chrono/latest/chrono/format/strftime/index.html#specifiers
        let datestring = format!("{}", naive.format("%Y-%m-%dT%H:%M:%S%.3fZ"));
        json_writer.string_property("time", datestring.as_str());
    }
}

/// A datetime metric of a certain resolution.
///
/// Datetimes are used to make record when something happened according to the
/// client's clock.
#[derive(Clone)]
pub enum DatetimeMetric {
    Parent {
        /// The metric's ID. Date time metrics cannot be labeled, so we only
        /// store a BaseMetricId. If this changes, this should be changed to a
        /// MetricId to distinguish between metrics and sub-metrics.
        id: BaseMetricId,
        inner: glean::private::DatetimeMetric,
    },
    Child(ChildMetricMeta),
}

crate::define_metric_metadata_getter!(DatetimeMetric, DATETIME_MAP);
crate::define_metric_namer!(DatetimeMetric);

impl DatetimeMetric {
    /// Create a new datetime metric.
    pub fn new(id: BaseMetricId, meta: CommonMetricData, time_unit: TimeUnit) -> Self {
        if need_ipc() {
            DatetimeMetric::Child(ChildMetricMeta::from_common_metric_data(id, meta))
        } else {
            DatetimeMetric::Parent {
                id,
                inner: glean::private::DatetimeMetric::new(meta, time_unit),
            }
        }
    }

    #[cfg(test)]
    pub(crate) fn child_metric(&self) -> Self {
        match self {
            DatetimeMetric::Parent { id, inner } => {
                DatetimeMetric::Child(ChildMetricMeta::from_metric_identifier(*id, inner))
            }
            DatetimeMetric::Child(_) => panic!("Can't get a child metric from a child metric"),
        }
    }

    /// Sets the metric to a date/time including the timezone offset.
    ///
    /// # Arguments
    ///
    /// * `year` - the year to set the metric to.
    /// * `month` - the month to set the metric to (1-12).
    /// * `day` - the day to set the metric to (1-based).
    /// * `hour` - the hour to set the metric to (0-23).
    /// * `minute` - the minute to set the metric to.
    /// * `second` - the second to set the metric to.
    /// * `nano` - the nanosecond fraction to the last whole second.
    /// * `offset_seconds` - the timezone difference, in seconds, for the Eastern
    ///   Hemisphere. Negative seconds mean Western Hemisphere.
    #[cfg_attr(not(feature = "with_gecko"), allow(dead_code))]
    #[allow(clippy::too_many_arguments)]
    #[allow(deprecated)] // use of deprecated chrono functions.
    pub(crate) fn set_with_details(
        &self,
        year: i32,
        month: u32,
        day: u32,
        hour: u32,
        minute: u32,
        second: u32,
        nano: u32,
        offset_seconds: i32,
    ) {
        match self {
            #[allow(unused)]
            DatetimeMetric::Parent { id, inner } => {
                let tz = FixedOffset::east_opt(offset_seconds);
                if tz.is_none() {
                    log::error!(
                        "Unable to set datetime metric with invalid offset seconds {}",
                        offset_seconds
                    );
                    // TODO: Record an error
                    return;
                }

                let value = FixedOffset::east(offset_seconds)
                    .ymd_opt(year, month, day)
                    .and_hms_nano_opt(hour, minute, second, nano);
                match value.single() {
                    Some(d) => {
                        #[cfg(feature = "with_gecko")]
                        if gecko_profiler::can_accept_markers() {
                            gecko_profiler::add_marker(
                                "Datetime::set",
                                TelemetryProfilerCategory,
                                Default::default(),
                                DatetimeMetricMarker { id: *id, time: d },
                            );
                        }
                        inner.set(Some(d.into()));
                    }
                    _ => {
                        log::error!("Unable to construct datetime");
                        // TODO: Record an error
                        // Record a simple text marker with this error.
                        // We expect this to happen exceedingly rarely,
                        // so use the (slightly) expensive function to get
                        // the metric's name here.
                        #[cfg(feature = "with_gecko")]
                        if gecko_profiler::can_accept_markers() {
                            let name = id.get_name();
                            let payload = format!(
                                "Conversion failed for metric {}: {} {} {} {} {} {} {} {}",
                                &name, year, month, day, hour, minute, second, nano, offset_seconds
                            );
                            gecko_profiler::add_text_marker(
                                "Datetime::set",
                                TelemetryProfilerCategory,
                                Default::default(),
                                payload.as_str(),
                            );
                        }
                    }
                }
            }
            DatetimeMetric::Child(_) => {
                log::error!("Unable to set datetime metric in non-main process. This operation will be ignored.");
                // If we're in automation we can panic so the instrumentor knows they've gone wrong.
                // This is a deliberate violation of Glean's "metric APIs must not throw" design.
                assert!(!crate::ipc::is_in_automation(), "Attempted to set datetime in non-main process, which is forbidden. This panics in automation.");
                // TODO: Record an error.
            }
        }
    }
}

#[inherent]
impl Datetime for DatetimeMetric {
    /// Sets the metric to a date/time which including the timezone offset.
    ///
    /// ## Arguments
    ///
    /// - `value` - The date and time and timezone value to set.
    ///             If None we use the current local time.
    pub fn set(&self, value: Option<glean::Datetime>) {
        match self {
            #[allow(unused)]
            DatetimeMetric::Parent { id, inner } => {
                // The underlying glean impl will use the time *now* if value
                // is None, so we re-produce the behaviour here so that the
                // marker reflects what's actually being recorded.
                #[cfg(feature = "with_gecko")]
                if gecko_profiler::can_accept_markers() {
                    // first, make sure that we actually have a value
                    match value {
                        Some(ref d) => {
                            // If we do, try and turn it into a chrono::DateTime
                            // Only record a marker if we succeed.
                            glean_to_chrono_datetime(d)
                                .and_then(|c| c.single())
                                .map(|c| {
                                    gecko_profiler::add_marker(
                                        "Datetime::set",
                                        TelemetryProfilerCategory,
                                        Default::default(),
                                        DatetimeMetricMarker { id: *id, time: c },
                                    );
                                });
                        }
                        None => {
                            // Otherwise, record the marker with the time now
                            gecko_profiler::add_marker(
                                "Datetime::set",
                                TelemetryProfilerCategory,
                                Default::default(),
                                DatetimeMetricMarker {
                                    id: *id,
                                    time: local_now_with_offset(),
                                },
                            );
                        }
                    };
                }
                inner.set(value);
            }
            DatetimeMetric::Child(_) => {
                log::error!(
                    "Unable to set datetime metric DatetimeMetric in non-main process. This operation will be ignored."
                );
                // If we're in automation we can panic so the instrumentor knows they've gone wrong.
                // This is a deliberate violation of Glean's "metric APIs must not throw" design.
                assert!(!crate::ipc::is_in_automation(), "Attempted to set datetime metric in non-main process, which is forbidden. This panics in automation.");
                // TODO: Record an error.
            }
        }
    }

    /// **Exported for test purposes.**
    ///
    /// Gets the currently stored value as a Datetime.
    ///
    /// The precision of this value is truncated to the `time_unit` precision.
    ///
    /// This doesn't clear the stored value.
    ///
    /// # Arguments
    ///
    /// * `ping_name` - represents the optional name of the ping to retrieve the
    ///   metric for. Defaults to the first value in `send_in_pings`.
    pub fn test_get_value<'a, S: Into<Option<&'a str>>>(
        &self,
        ping_name: S,
    ) -> Option<glean::Datetime> {
        let ping_name = ping_name.into().map(|s| s.to_string());
        match self {
            DatetimeMetric::Parent { inner, .. } => inner.test_get_value(ping_name),
            DatetimeMetric::Child(_) => {
                panic!("Cannot get test value for DatetimeMetric in non-main process!")
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
    /// * `ping_name` - represents the optional name of the ping to retrieve the
    ///   metric for. Defaults to the first value in `send_in_pings`.
    ///
    /// # Returns
    ///
    /// The number of errors reported.
    pub fn test_get_num_recorded_errors(&self, error: glean::ErrorType) -> i32 {
        match self {
            DatetimeMetric::Parent { inner, .. } => inner.test_get_num_recorded_errors(error),
            DatetimeMetric::Child(_) => panic!(
                "Cannot get the number of recorded errors for DatetimeMetric in non-main process!"
            ),
        }
    }
}

#[cfg(test)]
mod test {
    use chrono::{DateTime, FixedOffset, TimeZone};

    use crate::{common_test::*, ipc, metrics};

    #[test]
    #[allow(deprecated)] // use of deprecated chrono functions.
    fn sets_datetime_value() {
        let _lock = lock_test();

        let metric = &metrics::test_only_ipc::a_date;

        let a_datetime = FixedOffset::east(5 * 3600)
            .ymd(2020, 05, 07)
            .and_hms(11, 58, 00);
        metric.set(Some(a_datetime.into()));

        let expected: glean::Datetime = DateTime::parse_from_rfc3339("2020-05-07T11:58:00+05:00")
            .unwrap()
            .into();
        assert_eq!(expected, metric.test_get_value("test-ping").unwrap());
    }

    #[test]
    fn sets_datetime_value_with_details() {
        let _lock = lock_test();

        let metric = &metrics::test_only_ipc::a_date;

        metric.set_with_details(2020, 05, 07, 11, 58, 0, 0, 5 * 3600);

        let expected: glean::Datetime = DateTime::parse_from_rfc3339("2020-05-07T11:58:00+05:00")
            .unwrap()
            .into();
        assert_eq!(expected, metric.test_get_value("test-ping").unwrap());
    }

    #[test]
    #[allow(deprecated)] // use of deprecated chrono functions.
    fn datetime_ipc() {
        // DatetimeMetric doesn't support IPC.
        let _lock = lock_test();

        let parent_metric = &metrics::test_only_ipc::a_date;

        // Instrumentation calls do not panic.
        let a_datetime = FixedOffset::east(5 * 3600)
            .ymd(2020, 10, 13)
            .and_hms(16, 41, 00);
        parent_metric.set(Some(a_datetime.into()));

        {
            let child_metric = parent_metric.child_metric();

            let _raii = ipc::test_set_need_ipc(true);

            let a_datetime = FixedOffset::east(0).ymd(2018, 4, 7).and_hms(12, 01, 00);
            child_metric.set(Some(a_datetime.into()));

            // (They also shouldn't do anything,
            // but that's not something we can inspect in this test)
        }

        assert!(ipc::replay_from_buf(&ipc::take_buf().unwrap()).is_ok());

        let expected: glean::Datetime = DateTime::parse_from_rfc3339("2020-10-13T16:41:00+05:00")
            .unwrap()
            .into();
        assert_eq!(expected, parent_metric.test_get_value("test-ping").unwrap());
    }
}
