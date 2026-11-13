/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use askama::Template;
use std::collections::HashSet;

use crate::intermediate_representation::PropDef;
use crate::{
    backends::{CodeDeclaration, CodeOracle, CodeType, TypeIdentifier},
    intermediate_representation::{FeatureDef, FeatureManifest, GeckoPrefDef, TypeFinder},
};

mod bundled;
mod common;
mod enum_;
mod feature;
mod filters;
mod imports;
mod object;
mod primitives;
mod structural;
#[derive(Template)]
#[template(syntax = "kt", escape = "none", path = "FeatureManifestTemplate.kt")]
pub struct FeatureManifestDeclaration<'a> {
    fm: &'a FeatureManifest,
    oracle: ConcreteCodeOracle,
}
impl<'a> FeatureManifestDeclaration<'a> {
    pub fn new(fm: &'a FeatureManifest) -> Self {
        Self {
            fm,
            oracle: Default::default(),
        }
    }

    pub fn members(&self) -> Vec<Box<dyn CodeDeclaration + 'a>> {
        let fm = self.fm;

        fm.iter_feature_defs()
            .map(|inner| {
                Box::new(feature::FeatureCodeDeclaration::new(fm, inner))
                    as Box<dyn CodeDeclaration>
            })
            .chain(fm.iter_enum_defs().map(|inner| {
                Box::new(enum_::EnumCodeDeclaration::new(fm, inner)) as Box<dyn CodeDeclaration>
            }))
            .chain(fm.iter_object_defs().map(|inner| {
                Box::new(object::ObjectCodeDeclaration::new(fm, inner)) as Box<dyn CodeDeclaration>
            }))
            .chain(fm.iter_imported_files().into_iter().map(|inner| {
                Box::new(imports::ImportedModuleInitialization::new(inner))
                    as Box<dyn CodeDeclaration>
            }))
            .collect()
    }

    pub fn feature_properties(&self) -> Vec<PropDef> {
        let fm = self.fm;

        fm.iter_feature_defs()
            .flat_map(|feature| feature.props())
            .chain(
                fm.iter_object_defs()
                    .flat_map(|object| object.props.clone()),
            )
            .chain(fm.iter_imported_files().into_iter().flat_map(|inner| {
                inner
                    .fm
                    .iter_feature_defs()
                    .flat_map(|feature| feature.props())
            }))
            .collect()
    }

    pub fn iter_feature_defs(&self) -> Vec<&FeatureDef> {
        self.fm.iter_feature_defs().collect::<_>()
    }

    #[allow(unused)]
    pub fn iter_gecko_prefs(&self) -> Vec<&GeckoPrefDef> {
        self.fm.iter_gecko_prefs().collect::<_>()
    }

    #[allow(unused)]
    pub fn iter_features_with_prefs(&self) -> Vec<(String, Vec<(String, &GeckoPrefDef)>)> {
        self.fm.iter_features_with_prefs().collect::<_>()
    }

    pub fn initialization_code(&self) -> Vec<String> {
        let oracle = &self.oracle;
        self.members()
            .into_iter()
            .filter_map(|member| member.initialization_code(oracle))
            .collect()
    }

    pub fn declaration_code(&self) -> Vec<String> {
        let oracle = &self.oracle;
        self.members()
            .into_iter()
            .filter_map(|member| member.definition_code(oracle))
            .collect()
    }

    pub fn imports(&self) -> Vec<String> {
        let oracle = &self.oracle;
        // We'll filter out objects from the package we're in.
        let my_package = format!(
            "{}.*",
            self.fm.about.nimbus_package_name().unwrap_or_default()
        );
        let mut imports: Vec<String> = self
            .members()
            .into_iter()
            .filter_map(|member| member.imports(oracle))
            .flatten()
            .chain(
                self.fm
                    .all_types()
                    .into_iter()
                    .filter_map(|type_| self.oracle.find(&type_).imports(oracle))
                    .flatten(),
            )
            .chain(vec![
                "org.mozilla.experiments.nimbus.Variables".to_string(),
                "org.mozilla.experiments.nimbus.internal.FeatureHolder".to_string(),
                "org.mozilla.experiments.nimbus.internal.FeatureManifestInterface".to_string(),
                "org.mozilla.experiments.nimbus.internal.GeckoPref".to_string(),
                "org.mozilla.experiments.nimbus.internal.PrefBranch".to_string(),
                "org.mozilla.experiments.nimbus.FeaturesInterface".to_string(),
                "org.json.JSONObject".to_string(),
                "android.content.SharedPreferences".to_string(),
            ])
            .filter(|i| i != &my_package)
            .collect::<HashSet<String>>()
            .into_iter()
            .collect();

        let include_r: bool = self
            .feature_properties()
            .into_iter()
            .any(|prop| self.oracle.find(&prop.typ()).is_resource_id(&prop.default));
        if include_r {
            imports.push(format!("{}.R", self.fm.about.resource_package_name()))
        }

        imports.sort();
        imports
    }
}

#[derive(Default, Clone)]
pub struct ConcreteCodeOracle;

impl ConcreteCodeOracle {
    fn create_code_type(&self, type_: TypeIdentifier) -> Box<dyn CodeType> {
        match type_ {
            TypeIdentifier::Boolean => Box::new(primitives::BooleanCodeType),
            TypeIdentifier::String | TypeIdentifier::StringAlias(_) => {
                Box::new(primitives::StringCodeType)
            }
            TypeIdentifier::Int => Box::new(primitives::IntCodeType),

            TypeIdentifier::BundleText => Box::new(bundled::TextCodeType),
            TypeIdentifier::BundleImage => Box::new(bundled::ImageCodeType),

            TypeIdentifier::Enum(id) => Box::new(enum_::EnumCodeType::new(id)),
            TypeIdentifier::Object(id) => Box::new(object::ObjectCodeType::new(id)),

            TypeIdentifier::Option(ref inner) => Box::new(structural::OptionalCodeType::new(inner)),
            TypeIdentifier::List(ref inner) => Box::new(structural::ListCodeType::new(inner)),
            TypeIdentifier::StringMap(ref v_type) => {
                let k_type = &TypeIdentifier::String;
                Box::new(structural::MapCodeType::new(k_type, v_type))
            }
            TypeIdentifier::EnumMap(ref k_type, ref v_type) => {
                Box::new(structural::MapCodeType::new(k_type, v_type))
            }
        }
    }
}

impl CodeOracle for ConcreteCodeOracle {
    fn find(&self, type_: &TypeIdentifier) -> Box<dyn CodeType> {
        self.create_code_type(type_.clone())
    }
}
