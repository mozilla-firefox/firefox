/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use askama::Template;
use std::collections::HashSet;

use crate::{
    backends::{CodeDeclaration, CodeOracle, CodeType, TypeIdentifier},
    intermediate_representation::{FeatureDef, FeatureManifest, TypeFinder},
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
#[template(
    syntax = "swift",
    escape = "none",
    path = "FeatureManifestTemplate.swift"
)]
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
            .chain(fm.iter_imported_files().iter().map(|inner| {
                Box::new(imports::ImportedModuleInitialization::new(inner))
                    as Box<dyn CodeDeclaration>
            }))
            .collect()
    }

    pub fn iter_feature_defs(&self) -> Vec<&FeatureDef> {
        self.fm.iter_feature_defs().collect::<_>()
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
        // Filter out our own module.
        let my_module = &self.fm.about.nimbus_module_name();
        // Get the app-services module from an environment variable.
        // If it doesn't exist, then we don't add it.
        let as_module = std::env::var("MOZ_APPSERVICES_MODULE")
            .map(|s| vec![s])
            .unwrap_or_else(|_| vec![]);
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
            .chain(as_module)
            .chain(vec!["Foundation".to_string()])
            .filter(|i| i != my_module)
            .collect::<HashSet<String>>()
            .into_iter()
            .collect();

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
