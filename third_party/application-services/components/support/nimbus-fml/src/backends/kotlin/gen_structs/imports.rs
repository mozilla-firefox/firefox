/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt::Display;

use super::{filters, object::object_literal};
use crate::{
    backends::{CodeDeclaration, CodeOracle, LiteralRenderer, TypeIdentifier},
    intermediate_representation::{ImportedModule, Literal, TypeFinder},
};
use askama::Template;

#[derive(Template)]
#[template(
    syntax = "kt",
    escape = "none",
    path = "ImportedModuleInitializationTemplate.kt"
)]
pub(crate) struct ImportedModuleInitialization<'a> {
    pub(crate) inner: ImportedModule<'a>,
}

impl<'a> ImportedModuleInitialization<'a> {
    pub(crate) fn new(inner: ImportedModule<'a>) -> Self {
        Self { inner }
    }
}

impl CodeDeclaration for ImportedModuleInitialization<'_> {
    fn imports(&self, oracle: &dyn CodeOracle) -> Option<Vec<String>> {
        let p = self.inner.about().nimbus_package_name()?;
        Some(
            self.inner
                .fm
                .all_types()
                .iter()
                .filter_map(|t| oracle.find(t).imports(oracle))
                .flatten()
                .chain(vec![format!("{}.*", p)])
                .collect::<Vec<_>>(),
        )
    }

    fn initialization_code(&self, _oracle: &dyn CodeOracle) -> Option<String> {
        Some(self.render().unwrap())
    }

    fn definition_code(&self, _oracle: &dyn CodeOracle) -> Option<String> {
        None
    }
}

impl LiteralRenderer for ImportedModuleInitialization<'_> {
    fn literal(
        &self,
        oracle: &dyn CodeOracle,
        typ: &TypeIdentifier,
        value: &Literal,
        ctx: &dyn Display,
    ) -> String {
        object_literal(self.inner.fm, ctx, &self, oracle, typ, value)
    }
}
