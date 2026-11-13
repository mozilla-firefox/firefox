// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::{cli::ExperimentListFilterArgs, output::info::ExperimentInfo};
use anyhow::Result;
use serde_json::Value;

#[derive(Clone, Debug, Default, PartialEq)]
pub(crate) struct ExperimentListFilter {
    slug_pattern: Option<String>,
    app: Option<String>,
    feature_pattern: Option<String>,
    active_on: Option<String>,
    enrolling_on: Option<String>,
    channel: Option<String>,
    is_rollout: Option<bool>,
}

impl From<&ExperimentListFilterArgs> for ExperimentListFilter {
    fn from(value: &ExperimentListFilterArgs) -> Self {
        ExperimentListFilter {
            slug_pattern: value.slug.clone(),
            feature_pattern: value.feature.clone(),
            active_on: value.active_on.clone(),
            enrolling_on: value.enrolling_on.clone(),
            channel: value.channel.clone(),
            is_rollout: value.is_rollout,
            ..Default::default()
        }
    }
}

impl ExperimentListFilter {
    pub(crate) fn for_app(app: &str) -> Self {
        ExperimentListFilter {
            app: Some(app.to_string()),
            ..Default::default()
        }
    }
}

#[cfg(test)]
impl ExperimentListFilter {
    pub(crate) fn for_feature(feature_pattern: &str) -> Self {
        ExperimentListFilter {
            feature_pattern: Some(feature_pattern.to_string()),
            ..Default::default()
        }
    }

    pub(crate) fn for_active_on(date: &str) -> Self {
        ExperimentListFilter {
            active_on: Some(date.to_string()),
            ..Default::default()
        }
    }

    pub(crate) fn for_enrolling_on(date: &str) -> Self {
        ExperimentListFilter {
            enrolling_on: Some(date.to_string()),
            ..Default::default()
        }
    }
}

impl ExperimentListFilter {
    pub(crate) fn is_empty(&self) -> bool {
        self == &Default::default()
    }

    pub(crate) fn matches(&self, value: &Value) -> Result<bool> {
        let info: ExperimentInfo = match value.try_into() {
            Ok(e) => e,
            _ => return Ok(false),
        };
        Ok(self.matches_info(info))
    }

    fn matches_info(&self, info: ExperimentInfo) -> bool {
        match self.slug_pattern.as_deref() {
            Some(s) if !info.slug.contains(s) => return false,
            _ => (),
        };

        match self.app.as_deref() {
            Some(s) if s != info.app_name => return false,
            _ => (),
        };

        match self.channel.as_deref() {
            Some(s) if s != info.channel => return false,
            _ => (),
        };

        match self.is_rollout {
            Some(s) if s != info.is_rollout => return false,
            _ => (),
        };

        match self.feature_pattern.as_deref() {
            Some(f) if !info.features.iter().any(|s| s.contains(f)) => return false,
            _ => (),
        };

        match self.active_on.as_deref() {
            Some(date) if !info.active().contains(date) => return false,
            _ => (),
        };

        match self.enrolling_on.as_deref() {
            Some(date) if !info.enrollment().contains(date) => return false,
            _ => (),
        };

        true
    }
}

#[cfg(test)]
mod unit_tests {
    use crate::output::info::DateRange;

    use super::*;

    #[test]
    fn test_matches_app() -> Result<()> {
        let filter = ExperimentListFilter {
            app: Some("my-app".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            app_name: "my-app",
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            app_name: "not-my-app",
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_slug() -> Result<()> {
        let filter = ExperimentListFilter {
            slug_pattern: Some("my-app".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            slug: "my-app",
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            slug: "my-other-app",
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_channel() -> Result<()> {
        let filter = ExperimentListFilter {
            channel: Some("release".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            channel: "release",
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            channel: "beta",
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_is_rollout() -> Result<()> {
        let filter = ExperimentListFilter {
            is_rollout: Some(false),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            is_rollout: false,
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            is_rollout: true,
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_conjunction() -> Result<()> {
        let filter = ExperimentListFilter {
            app: Some("my-app".to_string()),
            channel: Some("release".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            app_name: "my-app",
            channel: "release",
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            app_name: "not-my-app",
            channel: "release",
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        let negative = ExperimentInfo {
            app_name: "my-app",
            channel: "not-release",
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_features() -> Result<()> {
        let filter = ExperimentListFilter {
            feature_pattern: Some("another".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            features: vec!["my-feature", "another-feature"],
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            features: vec!["my-feature", "not-this-feature"],
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_enrolling_on() -> Result<()> {
        let filter = ExperimentListFilter {
            enrolling_on: Some("2023-07-18".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            enrollment: DateRange::from_str("2023-07-01", "2023-07-31", 0),
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            enrollment: DateRange::from_str("2023-06-01", "2023-06-30", 0),
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }

    #[test]
    fn test_matches_active_on() -> Result<()> {
        let filter = ExperimentListFilter {
            active_on: Some("2023-07-18".to_string()),
            ..Default::default()
        };

        let positive = ExperimentInfo {
            duration: DateRange::from_str("2023-07-01", "2023-07-31", 0),
            ..Default::default()
        };
        assert!(filter.matches_info(positive));

        let negative = ExperimentInfo {
            duration: DateRange::from_str("2023-06-01", "2023-06-30", 0),
            ..Default::default()
        };
        assert!(!filter.matches_info(negative));

        Ok(())
    }
}
