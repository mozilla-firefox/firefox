/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::BTreeSet;

use url::Url;

use crate::{frontend::DocumentationLink, intermediate_representation::FeatureDef, FmlClient};

#[derive(Debug, PartialEq, Default)]
pub struct FmlFeatureDescriptor {
    pub(crate) id: String,
    pub(crate) description: String,
    pub(crate) is_coenrolling: bool,
    pub(crate) documentation: Vec<DocumentationLink>,
    pub(crate) contacts: Vec<String>,
    pub(crate) meta_bug: Option<Url>,
    pub(crate) events: Vec<Url>,
    pub(crate) configurator: Option<Url>,
}

impl From<&FeatureDef> for FmlFeatureDescriptor {
    fn from(f: &FeatureDef) -> Self {
        Self {
            id: f.name(),
            description: f.doc(),
            is_coenrolling: f.allow_coenrollment,
            documentation: f.metadata.documentation.clone(),
            contacts: f.metadata.contacts.clone(),
            meta_bug: f.metadata.meta_bug.clone(),
            events: f.metadata.events.clone(),
            configurator: f.metadata.configurator.clone(),
        }
    }
}

impl FmlClient {
    pub fn get_feature_ids(&self) -> Vec<String> {
        let mut res: BTreeSet<String> = Default::default();
        for (_, f) in self.manifest.iter_all_feature_defs() {
            res.insert(f.name());
        }
        res.into_iter().collect()
    }

    pub fn get_feature_descriptor(&self, id: String) -> Option<FmlFeatureDescriptor> {
        let (_, f) = self.manifest.find_feature(&id)?;
        Some(f.into())
    }

    pub fn get_feature_descriptors(&self) -> Vec<FmlFeatureDescriptor> {
        let mut res: Vec<_> = Default::default();
        for (_, f) in self.manifest.iter_all_feature_defs() {
            res.push(f.into());
        }
        res
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use crate::{client::test_helper::client, error::Result};

    #[test]
    fn test_feature_ids() -> Result<()> {
        let client = client("./bundled_resouces.yaml", "testing")?;
        let result = client.get_feature_ids();

        assert_eq!(result, vec!["my_images", "my_strings"]);
        Ok(())
    }

    #[test]
    fn test_get_feature() -> Result<()> {
        let client = client("./bundled_resouces.yaml", "testing")?;

        let result = client.get_feature_descriptor("my_strings".to_string());
        assert!(result.is_some());
        assert_eq!(
            result.unwrap(),
            FmlFeatureDescriptor {
                id: "my_strings".to_string(),
                description: "Testing all the ways bundled text can work".to_string(),
                is_coenrolling: false,
                ..Default::default()
            }
        );

        let result = client.get_feature_descriptor("my_images".to_string());
        assert!(result.is_some());
        assert_eq!(
            result.unwrap(),
            FmlFeatureDescriptor {
                id: "my_images".to_string(),
                description: "Testing all the ways bundled images can work".to_string(),
                is_coenrolling: false,
                ..Default::default()
            }
        );

        Ok(())
    }
}
