/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt::Display;

use super::common::{code_type, quoted};
use crate::backends::{CodeOracle, CodeType, LiteralRenderer, TypeIdentifier, VariablesType};
use crate::intermediate_representation::{Literal, TypeRef};
use heck::ToSnakeCase;
use icu_segmenter::GraphemeClusterSegmenter;

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

    fn defaults_type(&self, _oracle: &dyn CodeOracle) -> String {
        "StringHolder".to_string()
    }

    fn defaults_mapper(
        &self,
        _oracle: &dyn CodeOracle,
        value: &dyn Display,
        vars: &dyn Display,
    ) -> Option<String> {
        Some(format!(
            "{value}.toString({vars}.context)",
            vars = vars,
            value = value
        ))
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
            serde_json::Value::String(v) => {
                if !is_resource_id(v) {
                    format!("Res.string({literal})", literal = quoted(v))
                } else {
                    format!("Res.string(R.string.{id})", id = v.to_snake_case())
                }
            }
            _ => unreachable!("Expecting a string"),
        }
    }

    fn preference_getter(
        &self,
        oracle: &dyn CodeOracle,
        prefs: &dyn Display,
        pref_key: &dyn Display,
    ) -> Option<String> {
        let ct = oracle.find(&TypeRef::String);
        ct.preference_getter(oracle, prefs, pref_key)
    }

    fn is_resource_id(&self, literal: &Literal) -> bool {
        match literal {
            serde_json::Value::String(v) => is_resource_id(v),
            _ => unreachable!("Expecting a string"),
        }
    }

    fn imports(&self, _oracle: &dyn CodeOracle) -> Option<Vec<String>> {
        Some(vec![
            "android.content.Context".to_string(),
            "org.mozilla.experiments.nimbus.Res".to_string(),
            "org.mozilla.experiments.nimbus.StringHolder".to_string(),
        ])
    }
}

fn is_resource_id(string: &str) -> bool {
    // In Android apps, resource identifiers are [a-z_][a-z0-9_]*
    // We don't use the regex crate, so we need some code.
    let start = "abcdefghijklmnopqrstuvwxyz_";
    let rest = "abcdefghijklmnopqrstuvwxyz_0123456789";
    if string.is_empty() {
        return false;
    }

    let segmenter = GraphemeClusterSegmenter::new();
    let boundaries: Vec<usize> = segmenter.segment_str(string).collect();

    // Convert boundaries to grapheme slices and check
    boundaries.windows(2).enumerate().all(|(i, w)| {
        let c = &string[w[0]..w[1]];
        (i > 0 && rest.contains(c)) || start.contains(c)
    })
}

pub(crate) struct ImageCodeType;

impl CodeType for ImageCodeType {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, _oracle: &dyn CodeOracle) -> String {
        "Res<Drawable>".into()
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

    fn defaults_type(&self, oracle: &dyn CodeOracle) -> String {
        oracle.find(&TypeIdentifier::Int).type_label(oracle)
    }

    fn defaults_mapper(
        &self,
        _oracle: &dyn CodeOracle,
        value: &dyn Display,
        vars: &dyn Display,
    ) -> Option<String> {
        Some(format!(
            "Res.drawable({vars}.context, {value})",
            vars = vars,
            value = value
        ))
    }

    fn as_json_transform(&self, _oracle: &dyn CodeOracle, prop: &dyn Display) -> Option<String> {
        Some(format!("{}.resourceName", prop))
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
            serde_json::Value::String(v) if is_resource_id(v) => {
                format!(r#"R.drawable.{id}"#, id = v.to_snake_case())
            }
            _ => unreachable!("Expecting a string matching an image/drawable resource"),
        }
    }

    fn is_resource_id(&self, literal: &Literal) -> bool {
        match literal {
            serde_json::Value::String(v) => is_resource_id(v),
            _ => unreachable!(
                "Expecting a string matching an image resource, with pattern [a-z][a-z0-9_]*"
            ),
        }
    }

    fn imports(&self, _oracle: &dyn CodeOracle) -> Option<Vec<String>> {
        Some(vec![
            "android.graphics.drawable.Drawable".to_string(),
            "org.mozilla.experiments.nimbus.Res".to_string(),
        ])
    }
}

#[cfg(test)]
mod unit_tests {

    use super::*;
    use crate::error::Result;

    #[test]
    fn test_is_resource_id() -> Result<()> {
        assert!(is_resource_id("ok"));
        assert!(is_resource_id("_ok"));
        assert!(is_resource_id("ok_then"));
        assert!(!is_resource_id("https://foo.com"));
        assert!(!is_resource_id("Ok then"));
        assert!(!is_resource_id("ok then"));
        assert!(!is_resource_id("ok!"));
        assert!(!is_resource_id("1ok"));

        Ok(())
    }
}
