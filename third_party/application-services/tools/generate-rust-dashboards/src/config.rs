/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{collections::BTreeSet, fmt};

pub use crate::component_config::Component;
pub use crate::schema::Unit;
use crate::util::slug;

/// Dashboard configuration for a team
pub struct TeamConfig {
    /// Display name for the team.
    ///
    /// This is what shows up on your dashboard titles.  Spell it out however you want.
    pub team_name: &'static str,

    /// Components that your team manages.
    pub components: Vec<Component>,

    /// Track component errors
    ///
    /// This adds a panel to the main dashboard as well as creates a extra dashboard for error
    /// details.
    pub component_errors: bool,

    /// Track sync metrics
    ///
    /// This adds a panel to the main dashboard as well as creates a extra dashboard for sync
    /// errors.
    pub sync_metrics: bool,

    /// Metric to include on your main dashboard
    pub main_dashboard_metrics: Vec<Metric>,

    /// Extra dashboards to generate
    pub extra_dashboards: Vec<ExtraDashboard>,
}

/// Extra dashboard to generate for a team
pub struct ExtraDashboard {
    pub name: &'static str,
    /// Metrics to include in the dashboard
    pub metrics: Vec<Metric>,
}

/// Metric to add to your team dashboard
///
/// Metrics will add panels to your overview dashboard and/or create secondary dashboards.
pub enum Metric {
    Counter(CounterMetric),
    LabeledCounter(LabeledCounterMetric),
    Distribution(DistributionMetric),
    LabeledDistribution(LabeledDistributionMetric),
    Events(EventsMetric),
}

/// Glean counter
///
/// This will create time-series panels for the counter
pub struct CounterMetric {
    /// Name to display on the dashboard
    pub display_name: &'static str,
    /// Name of the ping ("metrics" by default)
    pub ping: &'static str,
    /// Category name (top-level key in metrics.yaml)
    pub category: &'static str,
    /// Metric name (key for the metric)
    pub metric: &'static str,
    // Which applications report this metric
    pub applications: Vec<Application>,
}

/// Glean labeled counter
///
/// This will create time-series panels for the counter, partitioned by the label
pub struct LabeledCounterMetric {
    /// Name to display on the dashboard
    pub display_name: &'static str,
    /// Name of the ping ("metrics" by default)
    pub ping: &'static str,
    /// Category name (top-level key in metrics.yaml)
    pub category: &'static str,
    /// Metric name (key for the metric)
    pub metric: &'static str,
    // Which applications report this metric
    pub applications: Vec<Application>,
}

/// Glean timing/memory distribution
///
/// This will create time-series panels for the 5th, 50th and 95th percentile.
pub struct DistributionMetric {
    pub kind: DistributionMetricKind,
    /// Name to display on the dashboard
    pub display_name: &'static str,
    /// Label describing what we're measure
    pub axis_label: &'static str,
    /// Unit label
    pub unit: Option<Unit>,
    /// Name of the ping ("metrics" by default)
    pub ping: &'static str,
    /// Category name (top-level key in metrics.yaml)
    pub category: &'static str,
    /// Metric name (key for the metric)
    pub metric: &'static str,
    // Which applications report this metric
    pub applications: Vec<Application>,
    // Divide each value by this amount
    //
    // Note:
    // * Timing distributions are always stored in nanoseconds, regardless of the unit listed in
    //   `metrics.yaml`
    // * Memory distributions are always stored in bytes, regardless of the unit listed in
    //   `metrics.yaml`
    pub value_divisor: Option<u64>,
    // Link to an extra dashboard, the inner value is the name of the dashboard
    pub link_to: Option<&'static str>,
}

/// Glean labeled timing/memory distribution
///
/// This will create time-series panels for the 5th, 50th and 95th percentile.
/// Percentiles will be partitioned by the metric label.
pub struct LabeledDistributionMetric {
    pub kind: DistributionMetricKind,
    /// Name to display on the dashboard
    pub display_name: &'static str,
    /// Label describing what we're measure
    pub axis_label: &'static str,
    /// Unit label
    pub unit: Option<Unit>,
    /// Name of the ping ("metrics" by default)
    pub ping: &'static str,
    /// Category name (top-level key in metrics.yaml)
    pub category: &'static str,
    /// Metric name (key for the metric)
    pub metric: &'static str,
    // Which applications report this metric
    pub applications: Vec<Application>,
    // Divide each value by this amount
    //
    // Note:
    // * Timing distributions are always stored in nanoseconds, regardless of the unit listed in
    //   `metrics.yaml`
    // * Memory distributions are always stored in bytes, regardless of the unit listed in
    //   `metrics.yaml`
    pub value_divisor: Option<u64>,
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum DistributionMetricKind {
    Memory,
    Timing,
    Custom,
}

/// Track multiple Glean events together
///
/// This will create time-series panels with event counts for each event
pub struct EventsMetric {
    /// Name to display on the dashboard
    pub display_name: &'static str,
    /// Name of the ping ("metrics" by default)
    pub ping: &'static str,
    /// Category name (top-level key in metrics.yaml)
    pub category: &'static str,
    /// Metric name (key for the metric)
    pub metrics: Vec<&'static str>,
    // Which applications report this metric
    pub applications: Vec<Application>,
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Application {
    Android,
    Ios,
    Desktop,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ReleaseChannel {
    Nightly,
    Beta,
    Release,
}

impl TeamConfig {
    pub fn applications(&self) -> BTreeSet<Application> {
        self.components
            .iter()
            .flat_map(Component::applications)
            .cloned()
            .collect()
    }

    pub fn team_slug(&self) -> String {
        slug(self.team_name)
    }
}

impl Application {
    pub fn slug(&self) -> &'static str {
        match self {
            Self::Android => "firefox_android",
            Self::Ios => "firefox_ios",
            Self::Desktop => "firefox_desktop",
        }
    }

    pub fn bigquery_dataset(&self) -> &'static str {
        // There's a few datasets we can use, these were chosen because they seem to include
        // release, beta, and nightly data
        match self {
            Self::Android => "fenix",
            Self::Ios => "firefox_ios",
            Self::Desktop => "firefox_desktop",
        }
    }

    pub fn display_name(&self, channel: ReleaseChannel) -> String {
        format!("{self} ({channel})")
    }
}

impl ReleaseChannel {
    pub fn all() -> impl Iterator<Item = Self> {
        [Self::Nightly, Self::Beta, Self::Release].into_iter()
    }

    pub fn slug(&self) -> &'static str {
        match self {
            Self::Nightly => "nightly",
            Self::Beta => "beta",
            Self::Release => "release",
        }
    }
}

impl fmt::Display for Application {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Android => write!(f, "Android"),
            Self::Ios => write!(f, "iOS"),
            Self::Desktop => write!(f, "Desktop"),
        }
    }
}

impl fmt::Display for ReleaseChannel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Nightly => write!(f, "nightly"),
            Self::Beta => write!(f, "beta"),
            Self::Release => write!(f, "release"),
        }
    }
}

impl From<CounterMetric> for Metric {
    fn from(m: CounterMetric) -> Self {
        Self::Counter(m)
    }
}

impl From<LabeledCounterMetric> for Metric {
    fn from(m: LabeledCounterMetric) -> Self {
        Self::LabeledCounter(m)
    }
}

impl From<DistributionMetric> for Metric {
    fn from(m: DistributionMetric) -> Self {
        Self::Distribution(m)
    }
}

impl From<LabeledDistributionMetric> for Metric {
    fn from(m: LabeledDistributionMetric) -> Self {
        Self::LabeledDistribution(m)
    }
}

impl From<EventsMetric> for Metric {
    fn from(m: EventsMetric) -> Self {
        Self::Events(m)
    }
}
