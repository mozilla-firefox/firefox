/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;

use crate::{
    error::{FMLError, Result},
    frontend::FeatureMetadata,
    intermediate_representation::{FeatureDef, FeatureManifest},
    util::loaders::FilePath,
};

#[derive(Serialize, Debug)]
pub(crate) struct ManifestInfo {
    file: String,
    features: BTreeMap<String, FeatureInfo>,
}

impl ManifestInfo {
    pub(crate) fn from(path: &FilePath, fm: &FeatureManifest) -> Self {
        let mut features = BTreeMap::new();
        for (fm, feature_def) in fm.iter_all_feature_defs() {
            features.insert(
                feature_def.name.to_string(),
                FeatureInfo::from(fm, feature_def),
            );
        }
        Self {
            file: path.to_string(),
            features,
        }
    }

    pub(crate) fn from_feature(
        path: &FilePath,
        fm: &FeatureManifest,
        feature_id: &str,
    ) -> Result<Self> {
        let (fm, feature_def) = fm
            .find_feature(feature_id)
            .ok_or_else(|| FMLError::InvalidFeatureError(feature_id.to_string()))?;
        let info = FeatureInfo::from(fm, feature_def);
        let features = BTreeMap::from([(feature_id.to_string(), info)]);
        Ok(Self {
            file: path.to_string(),
            features,
        })
    }

    pub(crate) fn to_json(&self) -> Result<String> {
        Ok(serde_json::to_string_pretty(self)?)
    }

    pub(crate) fn to_yaml(&self) -> Result<String> {
        Ok(serde_yaml::to_string(self)?)
    }
}

#[derive(Serialize, Debug)]
pub(crate) struct FeatureInfo {
    #[serde(flatten)]
    metadata: FeatureMetadata,
    types: BTreeSet<String>,
    hashes: HashInfo,
}

impl FeatureInfo {
    fn from(fm: &FeatureManifest, feature_def: &FeatureDef) -> Self {
        let hashes = HashInfo::from(fm, feature_def);
        let types = fm
            .feature_types(feature_def)
            .iter()
            .map(|t| t.to_string())
            .collect();
        let metadata = feature_def.metadata.clone();
        Self {
            types,
            hashes,
            metadata,
        }
    }
}

#[derive(Serialize, Debug)]
pub(crate) struct HashInfo {
    schema: String,
    defaults: String,
}

impl HashInfo {
    fn from(fm: &FeatureManifest, feature_def: &FeatureDef) -> Self {
        let schema = fm.feature_schema_hash(feature_def);
        let defaults = fm.feature_defaults_hash(feature_def);
        HashInfo { schema, defaults }
    }
}
