/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt::Display;

use super::common::{code_type, quoted};
use crate::backends::{CodeOracle, CodeType, LiteralRenderer, TypeIdentifier, VariablesType};
use crate::intermediate_representation::Literal;

pub(crate) struct TextCodeType;

impl CodeType for TextCodeType {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, _oracle: &dyn CodeOracle) -> String {
        "String".into()
    }

    fn property_getter(
        &self,
        oracle: &dyn CodeOracle,
        vars: &dyn Display,
        prop: &dyn Display,
        default: &dyn Display,
    ) -> String {
        code_type::property_getter(self, oracle, vars, prop, default)
    }

    fn value_getter(
        &self,
        oracle: &dyn CodeOracle,
        vars: &dyn Display,
        prop: &dyn Display,
    ) -> String {
        code_type::value_getter(self, oracle, vars, prop)
    }

    fn value_mapper(&self, oracle: &dyn CodeOracle) -> Option<String> {
        code_type::value_mapper(self, oracle)
    }

    /// The name of the type as it's represented in the `Variables` object.
    /// The string return may be used to combine with an identifier, e.g. a `Variables` method name.
    fn variables_type(&self, _oracle: &dyn CodeOracle) -> VariablesType {
        VariablesType::Text
    }

    /// A representation of the given literal for this type.
    /// N.B. `Literal` is aliased from `serde_json::Value`.
    fn literal(
        &self,
        _oracle: &dyn CodeOracle,
        _ctx: &dyn Display,
        _renderer: &dyn LiteralRenderer,
        literal: &Literal,
    ) -> String {
        match literal {
            serde_json::Value::String(v) => quoted(v),
            _ => unreachable!("Expecting a string"),
        }
    }

    fn defaults_type(&self, oracle: &dyn CodeOracle) -> String {
        oracle.find(&TypeIdentifier::String).type_label(oracle)
    }

    fn defaults_mapper(
        &self,
        _oracle: &dyn CodeOracle,
        value: &dyn Display,
        vars: &dyn Display,
    ) -> Option<String> {
        Some(format!(
            "{vars}.resourceBundles.getString(named: {value}) ?? {value}",
            vars = vars,
            value = value
        ))
    }
}

pub(crate) struct ImageCodeType;

impl CodeType for ImageCodeType {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, _oracle: &dyn CodeOracle) -> String {
        "UIImage".into()
    }

    fn property_getter(
        &self,
        oracle: &dyn CodeOracle,
        vars: &dyn Display,
        prop: &dyn Display,
        default: &dyn Display,
    ) -> String {
        code_type::property_getter(self, oracle, vars, prop, default)
    }

    fn value_getter(
        &self,
        oracle: &dyn CodeOracle,
        vars: &dyn Display,
        prop: &dyn Display,
    ) -> String {
        code_type::value_getter(self, oracle, vars, prop)
    }

    fn value_mapper(&self, oracle: &dyn CodeOracle) -> Option<String> {
        code_type::value_mapper(self, oracle)
    }

    /// The name of the type as it's represented in the `Variables` object.
    /// The string return may be used to combine with an identifier, e.g. a `Variables` method name.
    fn variables_type(&self, _oracle: &dyn CodeOracle) -> VariablesType {
        VariablesType::Image
    }

    fn as_json_transform(&self, _oracle: &dyn CodeOracle, prop: &dyn Display) -> Option<String> {
        Some(format!("{prop}.encodableImageName"))
    }

    /// A representation of the given literal for this type.
    /// N.B. `Literal` is aliased from `serde_json::Value`.
    fn literal(
        &self,
        _oracle: &dyn CodeOracle,
        _ctx: &dyn Display,
        _renderer: &dyn LiteralRenderer,
        literal: &Literal,
    ) -> String {
        match literal {
            serde_json::Value::String(v) => quoted(v),
            _ => unreachable!("Expecting a string matching an image/drawable resource"),
        }
    }

    fn defaults_type(&self, oracle: &dyn CodeOracle) -> String {
        oracle.find(&TypeIdentifier::String).type_label(oracle)
    }

    fn defaults_mapper(
        &self,
        _oracle: &dyn CodeOracle,
        value: &dyn Display,
        vars: &dyn Display,
    ) -> Option<String> {
        Some(format!(
            // UIKit does not provide any compile time safety for bundled images. The string name isn't found to be missing
            // until runtime.
            // For these fallback images, if they are missing, we consider it a programmer error,
            // so `getImageNotNull(image:)` fatalErrors if the image doesn't exist.
            //
            // The assumption here is that the developer will discover this
            // early in the cycle, and provide the image or change the name.
            "{vars}.resourceBundles.getImageNotNull(named: {value})",
            vars = vars,
            value = value
        ))
    }

    fn imports(&self, _oracle: &dyn CodeOracle) -> Option<Vec<String>> {
        Some(vec!["UIKit".to_string()])
    }
}
