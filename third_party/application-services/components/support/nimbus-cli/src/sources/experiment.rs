// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use anyhow::{bail, Result};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;
use std::{
    fmt::Display,
    path::{Path, PathBuf},
};

use crate::value_utils::{read_from_file, try_find_mut_features_from_branch, CliUtils, Patch};
use crate::{
    cli::{Cli, CliCommand, ExperimentArgs},
    config, feature_utils,
    sources::ExperimentListSource,
    value_utils, NimbusApp, USER_AGENT,
};

use super::experiment_list::decode_list_slug;

#[derive(Clone, Debug, PartialEq)]
pub(crate) enum ExperimentSource {
    FromList {
        slug: String,
        list: ExperimentListSource,
    },
    FromFeatureFiles {
        app: NimbusApp,
        feature_id: String,
        files: Vec<PathBuf>,
    },
    FromApiV6 {
        slug: String,
        endpoint: String,
    },
    WithPatchFile {
        patch: PathBuf,
        inner: Box<ExperimentSource>,
    },
    #[cfg(test)]
    FromTestFixture {
        file: PathBuf,
    },
}

// Create ExperimentSources from &str and Cli.

impl ExperimentSource {
    fn try_from_slug<'a>(
        value: &'a str,
        production: &'a str,
        stage: &'a str,
    ) -> Result<(&'a str, &'a str, bool)> {
        let tokens: Vec<&str> = value.splitn(3, '/').collect();

        let (is_production, is_preview) = match tokens.as_slice() {
            [_] => decode_list_slug("")?,
            [first, _] => decode_list_slug(first)?,
            [first, second, _] => decode_list_slug(&format!("{first}/{second}"))?,
            _ => unreachable!(),
        };

        let endpoint = if is_production { production } else { stage };

        Ok(match tokens.last() {
            Some(slug) => (slug, endpoint, is_preview),
            _ => bail!(format!(
                "Can't unpack '{value}' into an experiment; try stage/SLUG, or SLUG"
            )),
        })
    }

    fn try_from_rs(value: &str) -> Result<Self> {
        let p = config::rs_production_server();
        let s = config::rs_stage_server();
        let (slug, endpoint, is_preview) = Self::try_from_slug(value, &p, &s)?;
        Ok(Self::FromList {
            slug: slug.to_string(),
            list: ExperimentListSource::FromRemoteSettings {
                endpoint: endpoint.to_string(),
                is_preview,
            },
        })
    }

    fn try_from_url(value: &str) -> Result<Self> {
        if !value.contains("://") {
            anyhow::bail!("A URL must start with https://, '{value}' does not");
        }
        let value = value.replacen("://", "/", 1);

        let parts: Vec<&str> = value.split('/').collect();

        Ok(match parts.as_slice() {
            [scheme, endpoint, "nimbus", slug]
            | [scheme, endpoint, "nimbus", slug, _]
            | [scheme, endpoint, "nimbus", slug, _, ""]
            | [scheme, endpoint, "api", "v6", "experiments", slug, ""] => Self::FromApiV6 {
                slug: slug.to_string(),
                endpoint: format!("{scheme}://{endpoint}"),
            },
            _ => anyhow::bail!("Unrecognized URL from which to to get an experiment"),
        })
    }

    fn try_from_api(value: &str) -> Result<Self> {
        let p = config::api_v6_production_server();
        let s = config::api_v6_stage_server();
        let (slug, endpoint, _) = Self::try_from_slug(value, &p, &s)?;
        Ok(Self::FromApiV6 {
            slug: slug.to_string(),
            endpoint: endpoint.to_string(),
        })
    }

    pub(crate) fn try_from_file(file: &Path, slug: &str) -> Result<Self> {
        Ok(ExperimentSource::FromList {
            slug: slug.to_string(),
            list: file.try_into()?,
        })
    }

    #[cfg(test)]
    pub(crate) fn from_fixture(filename: &str) -> Self {
        let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let file = dir.join("test/fixtures").join(filename);
        Self::FromTestFixture { file }
    }
}

impl TryFrom<&ExperimentArgs> for ExperimentSource {
    type Error = anyhow::Error;

    fn try_from(value: &ExperimentArgs) -> Result<Self> {
        let experiment = &value.experiment;
        let is_urlish = experiment.contains("://");
        let experiment = match &value.file {
            Some(_) if is_urlish => {
                anyhow::bail!("Cannot load an experiment from a file and a URL at the same time")
            }
            None if is_urlish => Self::try_from_url(experiment.as_str())?,
            Some(file) => Self::try_from_file(file, experiment)?,
            _ if value.use_rs => Self::try_from_rs(experiment)?,
            _ => Self::try_from_api(experiment.as_str())?,
        };
        Ok(match &value.patch {
            Some(file) => Self::WithPatchFile {
                patch: file.clone(),
                inner: Box::new(experiment),
            },
            _ => experiment,
        })
    }
}

impl TryFrom<&Cli> for ExperimentSource {
    type Error = anyhow::Error;

    fn try_from(value: &Cli) -> Result<Self> {
        Ok(match &value.command {
            CliCommand::Validate { experiment, .. }
            | CliCommand::Enroll { experiment, .. }
            | CliCommand::Features { experiment, .. } => experiment.try_into()?,
            CliCommand::TestFeature {
                feature_id,
                files,
                patch,
                ..
            } => {
                let experiment = Self::FromFeatureFiles {
                    app: value.into(),
                    feature_id: feature_id.clone(),
                    files: files.clone(),
                };
                match patch {
                    Some(f) => Self::WithPatchFile {
                        patch: f.clone(),
                        inner: Box::new(experiment),
                    },
                    _ => experiment,
                }
            }
            _ => unreachable!("Cli Arg not supporting getting an experiment source"),
        })
    }
}

// Get the experiment itself from the experiment source.

impl Display for ExperimentSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::FromList { slug, .. } | Self::FromApiV6 { slug, .. } => f.write_str(slug),
            Self::FromFeatureFiles { feature_id, .. } => {
                f.write_str(&format!("{feature_id}-experiment"))
            }
            Self::WithPatchFile { inner, .. } => f.write_str(&format!("{inner} (patched)")),
            #[cfg(test)]
            Self::FromTestFixture { file } => f.write_str(&format!("{file:?}")),
        }
    }
}

impl TryFrom<&ExperimentSource> for Value {
    type Error = anyhow::Error;

    fn try_from(value: &ExperimentSource) -> Result<Value> {
        Ok(match value {
            ExperimentSource::FromList { slug, list } => {
                let value = Value::try_from(list)?;
                value_utils::try_find_experiment(&value, slug)?
            }
            ExperimentSource::FromApiV6 { slug, endpoint } => {
                let url = format!("{endpoint}/api/v6/experiments/{slug}/");
                let req = viaduct::Request::get(viaduct::parse_url(&url)?)
                    .header("User-Agent", USER_AGENT)?;
                req.send()?.json()?
            }
            ExperimentSource::FromFeatureFiles {
                app,
                feature_id,
                files,
            } => feature_utils::create_experiment(app, feature_id, files)?,

            ExperimentSource::WithPatchFile { patch, inner } => patch_experiment(inner, patch)?,

            #[cfg(test)]
            ExperimentSource::FromTestFixture { file } => value_utils::read_from_file(file)?,
        })
    }
}

fn patch_experiment(experiment: &ExperimentSource, patch: &PathBuf) -> Result<Value> {
    let mut value: Value = experiment
        .try_into()
        .map_err(|e| anyhow::Error::msg(format!("Problem loading experiment: {e}")))?;

    let patch: FeatureDefaults = read_from_file(patch)
        .map_err(|e| anyhow::Error::msg(format!("Problem loading patch file: {e}")))?;

    for b in value.get_mut_array("branches")? {
        for (feature_id, value) in try_find_mut_features_from_branch(b)? {
            match patch.features.get(&feature_id) {
                Some(v) => value.patch(v),
                _ => true,
            };
        }
    }
    Ok(value)
}

#[derive(Deserialize, Serialize)]
struct FeatureDefaults {
    #[serde(flatten)]
    features: BTreeMap<String, Value>,
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    #[test]
    fn test_experiment_source_from_rs() -> Result<()> {
        let release = ExperimentListSource::try_from_rs("")?;
        let stage = ExperimentListSource::try_from_rs("stage")?;
        let release_preview = ExperimentListSource::try_from_rs("preview")?;
        let stage_preview = ExperimentListSource::try_from_rs("stage/preview")?;
        let slug = "my-slug".to_string();
        assert_eq!(
            ExperimentSource::try_from_rs("my-slug")?,
            ExperimentSource::FromList {
                list: release.clone(),
                slug: slug.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_rs("release/my-slug")?,
            ExperimentSource::FromList {
                list: release,
                slug: slug.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_rs("stage/my-slug")?,
            ExperimentSource::FromList {
                list: stage,
                slug: slug.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_rs("preview/my-slug")?,
            ExperimentSource::FromList {
                list: release_preview.clone(),
                slug: slug.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_rs("release/preview/my-slug")?,
            ExperimentSource::FromList {
                list: release_preview,
                slug: slug.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_rs("stage/preview/my-slug")?,
            ExperimentSource::FromList {
                list: stage_preview,
                slug
            }
        );

        assert!(ExperimentSource::try_from_rs("not-real/preview/my-slug").is_err());
        assert!(ExperimentSource::try_from_rs("release/not-real/my-slug").is_err());

        Ok(())
    }

    #[test]
    fn test_experiment_source_from_api() -> Result<()> {
        let release = config::api_v6_production_server();
        let stage = config::api_v6_stage_server();
        let slug = "my-slug".to_string();
        assert_eq!(
            ExperimentSource::try_from_api("my-slug")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: release.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_api("release/my-slug")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: release.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_api("stage/my-slug")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: stage.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_api("preview/my-slug")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: release.clone()
            }
        );
        assert_eq!(
            ExperimentSource::try_from_api("release/preview/my-slug")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: release
            }
        );
        assert_eq!(
            ExperimentSource::try_from_api("stage/preview/my-slug")?,
            ExperimentSource::FromApiV6 {
                slug,
                endpoint: stage
            }
        );

        Ok(())
    }

    #[test]
    fn test_experiment_source_from_url() -> Result<()> {
        let endpoint = "https://example.com";
        let slug = "my-slug";
        assert_eq!(
            ExperimentSource::try_from_url("https://example.com/nimbus/my-slug/summary")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: endpoint.to_string(),
            }
        );
        assert_eq!(
            ExperimentSource::try_from_url("https://example.com/nimbus/my-slug/summary/")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: endpoint.to_string(),
            }
        );
        assert_eq!(
            ExperimentSource::try_from_url("https://example.com/nimbus/my-slug/results#overview")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: endpoint.to_string(),
            }
        );
        assert_eq!(
            ExperimentSource::try_from_url("https://example.com/api/v6/experiments/my-slug/")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: endpoint.to_string(),
            }
        );
        let endpoint = "http://localhost:8080";
        assert_eq!(
            ExperimentSource::try_from_url("http://localhost:8080/nimbus/my-slug/summary")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: endpoint.to_string(),
            }
        );
        assert_eq!(
            ExperimentSource::try_from_url("http://localhost:8080/api/v6/experiments/my-slug/")?,
            ExperimentSource::FromApiV6 {
                slug: slug.to_string(),
                endpoint: endpoint.to_string(),
            }
        );

        Ok(())
    }
}
