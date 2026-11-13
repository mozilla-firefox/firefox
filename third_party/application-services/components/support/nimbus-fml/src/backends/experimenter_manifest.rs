/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Display;

use serde::{Deserialize, Serialize};

use crate::{
    command_line::commands::GenerateExperimenterManifestCmd,
    error::{FMLError, Result},
    intermediate_representation::{
        FeatureDef, FeatureManifest, GeckoPrefDef, PropDef, TargetLanguage, TypeRef,
    },
};

pub(crate) type ExperimenterManifest = BTreeMap<String, ExperimenterFeature>;

#[derive(Debug, Default, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct ExperimenterFeature {
    description: String,
    has_exposure: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    exposure_description: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    is_early_startup: Option<bool>,
    variables: BTreeMap<String, ExperimenterFeatureProperty>,
}

#[derive(Debug, Default, Clone, Serialize, Deserialize)]
pub(crate) struct ExperimenterFeatureProperty {
    #[serde(rename = "type")]
    property_type: String,
    description: String,

    #[serde(rename = "enum")]
    #[serde(skip_serializing_if = "Option::is_none")]
    variants: Option<BTreeSet<String>>,

    #[serde(rename = "setPref")]
    #[serde(skip_serializing_if = "Option::is_none")]
    set_pref: Option<GeckoPrefDef>,
}

impl TryFrom<FeatureManifest> for ExperimenterManifest {
    type Error = crate::error::FMLError;
    fn try_from(fm: FeatureManifest) -> Result<Self> {
        fm.iter_all_feature_defs()
            .map(|(fm, f)| Ok((f.name(), fm.create_experimenter_feature(f)?)))
            .collect()
    }
}

impl FeatureManifest {
    fn create_experimenter_feature(&self, feature: &FeatureDef) -> Result<ExperimenterFeature> {
        Ok(ExperimenterFeature {
            description: feature.doc(),
            has_exposure: true,
            is_early_startup: None,
            // TODO: Add exposure description to the IR so
            // we can use it here if it's needed
            exposure_description: Some("".into()),
            variables: self.props_to_variables(&feature.props)?,
        })
    }

    fn props_to_variables(
        &self,
        props: &[PropDef],
    ) -> Result<BTreeMap<String, ExperimenterFeatureProperty>> {
        // Ideally this would be implemented as a `TryFrom<Vec<PropDef>>`
        // however, we need a reference to the `FeatureManifest` to get the valid
        // variants of an enum
        let mut map = BTreeMap::new();
        props.iter().try_for_each(|prop| -> Result<()> {
            let typ = ExperimentManifestPropType::from(prop.typ()).to_string();

            let variants = match prop.typ() {
                TypeRef::Enum(e) => {
                    let enum_def = self
                        .find_enum(&e)
                        .ok_or(FMLError::InternalError("Found enum with no definition"))?;

                    Some(
                        enum_def
                            .variants
                            .iter()
                            .map(|variant| variant.name())
                            .collect::<BTreeSet<String>>(),
                    )
                }
                _ => None,
            };

            map.insert(
                prop.name(),
                ExperimenterFeatureProperty {
                    variants,
                    description: prop.doc(),
                    property_type: typ,
                    set_pref: prop.gecko_pref.clone(),
                },
            );
            Ok(())
        })?;
        Ok(map)
    }
}

enum ExperimentManifestPropType {
    Json,
    Boolean,
    Int,
    String,
}

impl Display for ExperimentManifestPropType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            ExperimentManifestPropType::Boolean => "boolean",
            ExperimentManifestPropType::Int => "int",
            ExperimentManifestPropType::Json => "json",
            ExperimentManifestPropType::String => "string",
        };
        write!(f, "{}", s)
    }
}

impl From<TypeRef> for ExperimentManifestPropType {
    fn from(typ: TypeRef) -> Self {
        match typ {
            TypeRef::Object(_)
            | TypeRef::EnumMap(_, _)
            | TypeRef::StringMap(_)
            | TypeRef::List(_) => Self::Json,
            TypeRef::Boolean => Self::Boolean,
            TypeRef::Int => Self::Int,
            TypeRef::String
            | TypeRef::BundleImage
            | TypeRef::BundleText
            | TypeRef::StringAlias(_)
            | TypeRef::Enum(_) => Self::String,
            TypeRef::Option(inner) => Self::from(inner),
        }
    }
}

impl From<Box<TypeRef>> for ExperimentManifestPropType {
    fn from(typ: Box<TypeRef>) -> Self {
        (*typ).into()
    }
}

pub(crate) fn generate_manifest(
    ir: FeatureManifest,
    cmd: &GenerateExperimenterManifestCmd,
) -> Result<()> {
    let experiment_manifest: ExperimenterManifest = ir.try_into()?;
    let output_str = match cmd.language {
        TargetLanguage::ExperimenterJSON => serde_json::to_string_pretty(&experiment_manifest)?,
        // This is currently just a re-render of the JSON in YAML.
        // However, the YAML format will diverge in time, so experimenter can support
        // a richer manifest format (probably involving generating schema that can validate
        // JSON patches in the FeatureConfig.)
        TargetLanguage::ExperimenterYAML => serde_yaml::to_string(&experiment_manifest)?,

        // If in doubt, output the previously generated default.
        _ => serde_json::to_string(&experiment_manifest)?,
    };

    std::fs::write(&cmd.output, output_str)?;
    Ok(())
}
