/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt::Display;

use askama::Template;

use super::filters;
use super::object::object_literal;
use crate::{
    backends::{CodeDeclaration, CodeOracle, LiteralRenderer, TypeIdentifier},
    intermediate_representation::{FeatureDef, FeatureManifest, Literal},
};

#[derive(Template)]
#[template(syntax = "swift", escape = "none", path = "FeatureTemplate.swift")]
pub(crate) struct FeatureCodeDeclaration {
    inner: FeatureDef,
    fm: FeatureManifest,
}

impl FeatureCodeDeclaration {
    pub fn new(fm: &FeatureManifest, inner: &FeatureDef) -> Self {
        Self {
            inner: inner.clone(),
            fm: fm.clone(),
        }
    }
    pub fn inner(&self) -> &FeatureDef {
        &self.inner
    }
}

impl CodeDeclaration for FeatureCodeDeclaration {
    fn definition_code(&self, _oracle: &dyn CodeOracle) -> Option<String> {
        Some(self.render().unwrap())
    }
}

impl LiteralRenderer for FeatureCodeDeclaration {
    fn literal(
        &self,
        oracle: &dyn CodeOracle,
        typ: &TypeIdentifier,
        value: &Literal,
        ctx: &dyn Display,
    ) -> String {
        object_literal(&self.fm, &self, oracle, typ, value, ctx)
    }
}
