// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::Path;

use anyhow::Result;
use nimbus_fml::intermediate_representation::FeatureManifest;
use serde_json::Value;

use crate::{
    sources::{ExperimentSource, ManifestSource},
    value_utils::{self, CliUtils},
};

impl ManifestSource {
    pub(crate) fn print_defaults<P>(
        &self,
        feature_id: Option<&String>,
        output: Option<P>,
    ) -> Result<bool>
    where
        P: AsRef<Path>,
    {
        let manifest: FeatureManifest = self.try_into()?;
        let json = self.get_defaults_json(&manifest, feature_id)?;
        value_utils::write_to_file_or_print(output, &json)?;
        Ok(true)
    }

    fn get_defaults_json(
        &self,
        fm: &FeatureManifest,
        feature_id: Option<&String>,
    ) -> Result<Value> {
        Ok(match feature_id {
            Some(id) => {
                let (_, feature) = fm.find_feature(id).ok_or_else(|| {
                    anyhow::Error::msg(format!("Feature '{id}' does not exist in this manifest"))
                })?;
                feature.default_json()
            }
            _ => fm.default_json(),
        })
    }
}

impl ExperimentSource {
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn print_features<P>(
        &self,
        branch: &String,
        manifest_source: &ManifestSource,
        feature_id: Option<&String>,
        validate: bool,
        multi: bool,
        output: Option<P>,
    ) -> Result<bool>
    where
        P: AsRef<Path>,
    {
        let json = self.get_features_json(manifest_source, feature_id, branch, validate, multi)?;
        value_utils::write_to_file_or_print(output, &json)?;
        Ok(true)
    }

    fn get_features_json(
        &self,
        manifest_source: &ManifestSource,
        feature_id: Option<&String>,
        branch: &String,
        validate: bool,
        multi: bool,
    ) -> Result<Value> {
        let value = self.try_into()?;

        // Find the named branch.
        let branches = value_utils::try_find_branches_from_experiment(&value)?;
        let b = branches
            .iter()
            .find(|b| b.get_str("slug").unwrap() == branch)
            .ok_or_else(|| anyhow::format_err!("Branch '{branch}' does not exist"))?;

        // Find the features for this branch: there may be more than one.
        let feature_values = value_utils::try_find_features_from_branch(b)?;

        // Now extract the relevant features out of the branches.
        let mut result = serde_json::value::Map::new();
        for f in feature_values {
            let id = f.get_str("featureId")?;
            let value = f
                .get("value")
                .ok_or_else(|| anyhow::format_err!("Branch {branch} feature {id} has no value"))?;
            match feature_id {
                None => {
                    // If the user hasn't specified a feature, then just add it.
                    result.insert(id.to_string(), value.clone());
                }
                Some(feature_id) if feature_id == id => {
                    // If the user has specified a feature, and this is it, then also add it.
                    result.insert(id.to_string(), value.clone());
                }
                // Otherwise, the user has specified a feature, and this wasn't it.
                _ => continue,
            }
        }

        // By now: we have all the features that we need, and no more.

        // If validating, then we should merge with the defaults from the manifest.
        // If not, then nothing more is needed to be done: we're delivering the partial feature configuration.
        if validate {
            let fm: FeatureManifest = manifest_source.try_into()?;
            let mut new = serde_json::value::Map::new();
            for (id, value) in result {
                let def = fm.validate_feature_config(&id, value)?;
                new.insert(id.to_owned(), def.default_json());
            }
            result = new;
        }

        Ok(if !multi && result.len() == 1 {
            // By default, if only a single feature is being displayed,
            // we can output just the feature config.
            match (result.values().find(|_| true), feature_id) {
                (Some(v), _) => v.to_owned(),
                (_, Some(id)) => anyhow::bail!(
                    "The '{id}' feature is not involved in '{branch}' branch of '{self}'"
                ),
                (_, _) => {
                    anyhow::bail!("No features available in '{branch}' branch of '{self}'")
                }
            }
        } else {
            // Otherwise, we can output the `{ featureId: featureValue }` in its entirety.
            Value::Object(result)
        })
    }
}
