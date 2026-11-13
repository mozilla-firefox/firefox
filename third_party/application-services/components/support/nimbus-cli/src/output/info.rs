// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::fmt::Display;
use std::io::Write;
use std::path::Path;

use anyhow::Result;
use serde_json::Value;
use termcolor::{Color, ColorChoice, ColorSpec, StandardStream, WriteColor};

use crate::{
    sources::{ExperimentListSource, ExperimentSource},
    value_utils::{self, CliUtils},
};

#[derive(serde::Serialize, Debug, Default)]
pub(crate) struct ExperimentInfo<'a> {
    pub(crate) slug: &'a str,
    pub(crate) app_name: &'a str,
    pub(crate) channel: &'a str,
    pub(crate) branches: Vec<&'a str>,
    pub(crate) features: Vec<&'a str>,
    pub(crate) targeting: &'a str,
    pub(crate) bucketing: u64,
    pub(crate) is_rollout: bool,
    pub(crate) user_facing_name: &'a str,
    pub(crate) user_facing_description: &'a str,
    pub(crate) enrollment: DateRange<'a>,
    pub(crate) is_enrollment_paused: bool,
    pub(crate) duration: DateRange<'a>,
}

impl<'a> ExperimentInfo<'a> {
    pub(crate) fn enrollment(&self) -> &DateRange<'a> {
        &self.enrollment
    }

    pub(crate) fn active(&self) -> &DateRange<'a> {
        &self.duration
    }

    fn bucketing_percent(&self) -> String {
        format!("{: >3.0} %", self.bucketing / 100)
    }
}

#[derive(serde::Serialize, Debug, Default)]
pub(crate) struct DateRange<'a> {
    start: Option<&'a str>,
    end: Option<&'a str>,
    proposed: Option<i64>,
}

impl<'a> DateRange<'a> {
    fn new(start: Option<&'a Value>, end: Option<&'a Value>, duration: Option<&'a Value>) -> Self {
        let start = start.map(Value::as_str).unwrap_or_default();
        let end = end.map(Value::as_str).unwrap_or_default();
        let proposed = duration.map(Value::as_i64).unwrap_or_default();
        Self {
            start,
            end,
            proposed,
        }
    }

    pub(crate) fn contains(&self, date: &str) -> bool {
        let start = self.start.unwrap_or("9999-99-99");
        let end = self.end.unwrap_or("9999-99-99");

        start <= date && date <= end
    }
}

impl Display for DateRange<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match (self.start, self.end, self.proposed) {
            (Some(s), Some(e), _) => f.write_str(&format!("{s} ➞ {e}")),
            (Some(s), _, Some(d)) => f.write_str(&format!("{s}, proposed ending after {d} days")),
            (Some(s), _, _) => f.write_str(&format!("{s} ➞ ?")),
            (None, Some(e), Some(d)) => {
                f.write_str(&format!("ending {e}, started {d} days before"))
            }
            (None, Some(e), _) => f.write_str(&format!("ending {e}")),
            _ => f.write_str("unknown"),
        }
    }
}

impl<'a> TryFrom<&'a Value> for ExperimentInfo<'a> {
    type Error = anyhow::Error;

    fn try_from(exp: &'a Value) -> Result<Self> {
        let features: Vec<_> = exp
            .get_array("featureIds")?
            .iter()
            .flat_map(|f| f.as_str())
            .collect();
        let branches: Vec<_> = exp
            .get_array("branches")?
            .iter()
            .flat_map(|b| {
                b.get("slug")
                    .expect("Expecting a branch with a slug")
                    .as_str()
            })
            .collect();

        let config = exp.get_object("bucketConfig")?;

        Ok(Self {
            slug: exp.get_str("slug")?,
            app_name: exp.get_str("appName")?,
            channel: exp.get_str("channel")?,
            branches,
            features,
            targeting: exp.get_str("targeting")?,
            bucketing: config.get_u64("count")?,
            is_rollout: exp.get_bool("isRollout")?,
            user_facing_name: exp.get_str("userFacingName")?,
            user_facing_description: exp.get_str("userFacingDescription")?,
            enrollment: DateRange::new(
                exp.get("startDate"),
                exp.get("enrollmentEndDate"),
                exp.get("proposedEnrollment"),
            ),
            is_enrollment_paused: exp.get_bool("isEnrollmentPaused")?,
            duration: DateRange::new(
                exp.get("startDate"),
                exp.get("endDate"),
                exp.get("proposedDuration"),
            ),
        })
    }
}

impl ExperimentListSource {
    pub(crate) fn print_list(&self) -> Result<bool> {
        let value: Value = self.try_into()?;
        let array = value_utils::try_extract_data_list(&value)?;

        let mut stdout = StandardStream::stdout(ColorChoice::Auto);
        let style = {
            let mut style = ColorSpec::new();
            style.set_italic(true).set_underline(true);
            style
        };

        stdout.set_color(&style)?;
        write!(&mut stdout, "{: <66}", "Experiment slug")?;
        stdout.reset()?;
        write!(&mut stdout, "|")?;

        stdout.set_color(&style)?;
        write!(&mut stdout, "{: <9}", " Channel")?;
        stdout.reset()?;
        write!(&mut stdout, "|")?;

        stdout.set_color(&style)?;
        write!(&mut stdout, "{: >7}", " % ")?;
        stdout.reset()?;
        write!(&mut stdout, "|")?;

        stdout.set_color(&style)?;
        write!(&mut stdout, "{: <31}", "Features")?;
        stdout.reset()?;
        write!(&mut stdout, "|")?;

        stdout.set_color(&style)?;
        write!(&mut stdout, "   ")?;
        stdout.reset()?;
        write!(&mut stdout, "|")?;

        stdout.set_color(&style)?;
        writeln!(&mut stdout, "{: <20}", " Branches")?;
        stdout.reset()?;

        for exp in array {
            let info = match ExperimentInfo::try_from(&exp) {
                Ok(e) => e,
                _ => continue,
            };

            let is_rollout = if info.is_rollout { "R" } else { "" };

            writeln!(
                &mut stdout,
                " {slug: <65}| {channel: <8}| {bucketing: >5} | {features: <30}| {is_rollout: <1} | {branches}",
                slug = info.slug,
                channel = info.channel,
                bucketing = info.bucketing_percent(),
                features = info.features.join(", "),
                branches = info.branches.join(", ")
            )?;
        }

        Ok(true)
    }
}

impl ExperimentSource {
    pub(crate) fn print_info<P>(&self, output: Option<P>) -> Result<bool>
    where
        P: AsRef<Path>,
    {
        fn format_line(
            stream: &mut StandardStream,
            title: &str,
            detail: &str,
        ) -> std::io::Result<()> {
            stream.set_color(ColorSpec::new().set_italic(true))?;
            write!(stream, "{: <11}", title)?;
            stream.reset()?;
            write!(stream, " ")?;
            stream.set_color(ColorSpec::new().set_bold(true).set_fg(Some(Color::Cyan)))?;
            writeln!(stream, "{}", detail)?;
            stream.reset()?;

            Ok(())
        }

        let value = self.try_into()?;
        let info: ExperimentInfo = ExperimentInfo::try_from(&value)?;
        if output.is_some() {
            value_utils::write_to_file_or_print(output, &info)?;
            return Ok(true);
        }
        let url = match self {
            Self::FromApiV6 { slug, endpoint } => Some(format!("{endpoint}/nimbus/{slug}/summary")),
            _ => None,
        };

        let enrollment = format!(
            "{} ({})",
            info.enrollment,
            if info.is_enrollment_paused {
                "paused"
            } else {
                "enrolling"
            }
        );

        let is_rollout = if info.is_rollout {
            "Rollout".to_string()
        } else {
            let n = info.branches.len();
            let b = if n == 1 {
                "1 branch".to_string()
            } else {
                format!("{n} branches")
            };
            format!("Experiment with {b}")
        };

        let mut stdout = StandardStream::stdout(ColorChoice::Auto);
        format_line(&mut stdout, "Slug", info.slug)?;
        format_line(&mut stdout, "Name", info.user_facing_name)?;
        format_line(&mut stdout, "Description", info.user_facing_description)?;
        if let Some(url) = url {
            format_line(&mut stdout, "URL", &url)?;
        }
        format_line(&mut stdout, "App", info.app_name)?;
        format_line(&mut stdout, "Channel", info.channel)?;
        format_line(&mut stdout, "E/R", &is_rollout)?;

        format_line(&mut stdout, "Enrollment", &enrollment)?;
        format_line(&mut stdout, "Observing", &info.duration.to_string())?;
        format_line(
            &mut stdout,
            "Targeting",
            &format!(r#""{}""#, info.targeting),
        )?;
        format_line(&mut stdout, "Bucketing", &info.bucketing_percent())?;
        format_line(&mut stdout, "Branches", &info.branches.join(", "))?;
        format_line(&mut stdout, "Features", &info.features.join(", "))?;

        Ok(true)
    }
}

#[cfg(test)]
mod unit_tests {
    use serde_json::json;

    use super::*;

    impl<'a> DateRange<'a> {
        pub(crate) fn from_str(start: &'a str, end: &'a str, duration: i64) -> Self {
            Self {
                start: Some(start),
                end: Some(end),
                proposed: Some(duration),
            }
        }
    }

    #[test]
    fn test_date_range_to_string() -> Result<()> {
        let from = json!("2023-06-01");
        let to = json!("2023-06-19");
        let null = json!(null);
        let days28 = json!(28);

        let dr = DateRange::new(Some(&null), Some(&null), Some(&null));
        let expected = "unknown".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&null), Some(&null), Some(&days28));
        let expected = "unknown".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&null), Some(&to), Some(&null));
        let expected = "ending 2023-06-19".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&null), Some(&to), Some(&days28));
        let expected = "ending 2023-06-19, started 28 days before".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&from), Some(&null), Some(&null));
        let expected = "2023-06-01 ➞ ?".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&from), Some(&null), Some(&days28));
        let expected = "2023-06-01, proposed ending after 28 days".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&from), Some(&to), Some(&null));
        let expected = "2023-06-01 ➞ 2023-06-19".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);

        let dr = DateRange::new(Some(&from), Some(&to), Some(&days28));
        let expected = "2023-06-01 ➞ 2023-06-19".to_string();
        let observed = dr.to_string();
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_date_range_contains() -> Result<()> {
        let from = json!("2023-06-01");
        let to = json!("2023-06-19");
        let null = json!(null);

        let before = "2023-05-01";
        let during = "2023-06-03";
        let after = "2023-06-20";

        let dr = DateRange::new(Some(&null), Some(&null), Some(&null));
        assert!(!dr.contains(before));
        assert!(!dr.contains(during));
        assert!(!dr.contains(after));

        let dr = DateRange::new(Some(&null), Some(&to), Some(&null));
        assert!(!dr.contains(before));
        assert!(!dr.contains(during));
        assert!(!dr.contains(after));

        let dr = DateRange::new(Some(&from), Some(&null), Some(&null));
        assert!(!dr.contains(before));
        assert!(dr.contains(during));
        assert!(dr.contains(after));

        let dr = DateRange::new(Some(&from), Some(&to), Some(&null));
        assert!(!dr.contains(before));
        assert!(dr.contains(during));
        assert!(!dr.contains(after));

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_experiment_info() -> Result<()> {
        let exp = ExperimentSource::from_fixture("fenix-nimbus-validation-v3.json");
        let value: Value = Value::try_from(&exp)?;

        let info = ExperimentInfo::try_from(&value)?;

        assert_eq!("fenix-nimbus-validation-v3", info.slug);
        assert_eq!("Fenix Nimbus Validation v3", info.user_facing_name);
        assert_eq!(
            "Verify we can run A/A experiments and bucket.",
            info.user_facing_description
        );
        assert_eq!("fenix", info.app_name);
        assert_eq!("nightly", info.channel);
        assert!(!info.is_rollout);
        assert!(!info.is_enrollment_paused);
        assert_eq!("true", info.targeting);
        assert_eq!(8000, info.bucketing);
        assert_eq!(" 80 %", info.bucketing_percent());
        assert_eq!(vec!["a1", "a2"], info.branches);
        assert_eq!(vec!["no-feature-fenix"], info.features);

        Ok(())
    }
}
