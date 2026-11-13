/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt::Display;

use askama::Template;

use super::common;
use super::common::code_type;
use super::filters;
use crate::backends::{CodeDeclaration, CodeOracle, CodeType, LiteralRenderer, VariablesType};
use crate::intermediate_representation::{EnumDef, FeatureManifest, Literal};

pub(crate) struct EnumCodeType {
    id: String,
}

impl EnumCodeType {
    pub(crate) fn new(id: String) -> Self {
        Self { id }
    }
}

impl CodeType for EnumCodeType {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, _oracle: &dyn CodeOracle) -> String {
        common::class_name(&self.id)
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
        VariablesType::String
    }

    /// A function handle that is capable of turning the variables type to the TypeRef type.
    fn create_transform(&self, oracle: &dyn CodeOracle) -> Option<String> {
        Some(format!(
            "{enum_type}::enumValue",
            enum_type = self.type_label(oracle)
        ))
    }

    fn as_json_transform(&self, _oracle: &dyn CodeOracle, prop: &dyn Display) -> Option<String> {
        Some(format!("{}.toJSONString()", prop))
    }

    /// A representation of the given literal for this type.
    /// N.B. `Literal` is aliased from `serde_json::Value`.
    fn literal(
        &self,
        oracle: &dyn CodeOracle,
        _ctx: &dyn Display,
        _renderer: &dyn LiteralRenderer,
        literal: &Literal,
    ) -> String {
        let variant = match literal {
            serde_json::Value::String(v) => v,
            _ => unreachable!(),
        };

        format!(
            "{}.{}",
            self.type_label(oracle),
            common::enum_variant_name(variant)
        )
    }
}
#[derive(Template)]
#[template(syntax = "kt", escape = "none", path = "EnumTemplate.kt")]
pub(crate) struct EnumCodeDeclaration {
    inner: EnumDef,
}

impl EnumCodeDeclaration {
    pub fn new(_fm: &FeatureManifest, inner: &EnumDef) -> Self {
        Self {
            inner: inner.clone(),
        }
    }
    fn inner(&self) -> EnumDef {
        self.inner.clone()
    }
}

impl CodeDeclaration for EnumCodeDeclaration {
    fn definition_code(&self, _oracle: &dyn CodeOracle) -> Option<String> {
        Some(self.render().unwrap())
    }
}

#[cfg(test)]
mod unit_tests {

    use serde_json::json;

    use super::*;
    use crate::backends::TypeIdentifier;

    struct TestCodeOracle;
    impl CodeOracle for TestCodeOracle {
        fn find(&self, _type_: &TypeIdentifier) -> Box<dyn CodeType> {
            unreachable!()
        }
    }

    struct TestRenderer;
    impl LiteralRenderer for TestRenderer {
        fn literal(
            &self,
            _oracle: &dyn CodeOracle,
            _typ: &TypeIdentifier,
            _value: &Literal,
            _ctx: &dyn Display,
        ) -> String {
            unreachable!()
        }
    }

    fn oracle() -> Box<dyn CodeOracle> {
        Box::new(TestCodeOracle) as Box<dyn CodeOracle>
    }

    fn code_type(name: &str) -> Box<dyn CodeType> {
        Box::new(EnumCodeType::new(name.to_string())) as Box<dyn CodeType>
    }

    #[test]
    fn test_type_label() {
        let ct = code_type("AEnum");
        let oracle = &*oracle();
        assert_eq!("AEnum".to_string(), ct.type_label(oracle))
    }

    #[test]
    fn test_literal() {
        let ct = code_type("AEnum");
        let oracle = &*oracle();
        let finder = &TestRenderer;
        let ctx = String::from("ctx");
        assert_eq!(
            "AEnum.FOO".to_string(),
            ct.literal(oracle, &ctx, finder, &json!("foo"))
        );
        assert_eq!(
            "AEnum.BAR_BAZ".to_string(),
            ct.literal(oracle, &ctx, finder, &json!("barBaz"))
        );
        assert_eq!(
            "AEnum.A_B_C".to_string(),
            ct.literal(oracle, &ctx, finder, &json!("a-b-c"))
        );
    }

    #[test]
    fn test_get_value() {
        let ct = code_type("AEnum");
        let oracle = &*oracle();

        assert_eq!(
            r#"v.getString("the-property")"#.to_string(),
            ct.value_getter(oracle, &"v", &"the-property")
        );
    }

    #[test]
    fn test_getter_with_fallback() {
        let ct = code_type("AEnum");
        let oracle = &*oracle();

        assert_eq!(
            r#"v.getString("the-property")?.let(AEnum::enumValue) ?: def"#.to_string(),
            ct.property_getter(oracle, &"v", &"the-property", &"def")
        );
    }
}
