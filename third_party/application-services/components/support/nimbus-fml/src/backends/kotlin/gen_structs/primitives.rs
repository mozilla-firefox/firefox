/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fmt::Display;

use super::common::{code_type, quoted};
use crate::backends::{CodeOracle, CodeType, LiteralRenderer, VariablesType};
use crate::intermediate_representation::Literal;

pub(crate) struct BooleanCodeType;

impl CodeType for BooleanCodeType {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, _oracle: &dyn CodeOracle) -> String {
        "Boolean".into()
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
        VariablesType::Bool
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
            serde_json::Value::Bool(v) => {
                if *v {
                    "true".to_string()
                } else {
                    "false".to_string()
                }
            }
            _ => unreachable!("Expecting a boolean"),
        }
    }

    fn preference_getter(
        &self,
        _oracle: &dyn CodeOracle,
        prefs: &dyn Display,
        pref_key: &dyn Display,
    ) -> Option<String> {
        Some(format!("{prefs}.getBoolean({}, false)", quoted(pref_key)))
    }
}

pub(crate) struct IntCodeType;

impl CodeType for IntCodeType {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, _oracle: &dyn CodeOracle) -> String {
        "Int".into()
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
        VariablesType::Int
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
            serde_json::Value::Number(v) => {
                format!("{:.0}", v)
            }
            _ => unreachable!("Expecting a number"),
        }
    }

    fn preference_getter(
        &self,
        _oracle: &dyn CodeOracle,
        prefs: &dyn Display,
        pref_key: &dyn Display,
    ) -> Option<String> {
        Some(format!("{prefs}.getInt({}, 0)", quoted(pref_key)))
    }
}

pub(crate) struct StringCodeType;

impl CodeType for StringCodeType {
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
        VariablesType::String
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
                // Usually, we'd be wanting to escape this, for security reasons. However, this is
                // will cause a kotlinc compile time error when the app is built if the string is malformed
                // in the manifest.
                quoted(v)
            }
            _ => unreachable!("Expecting a string"),
        }
    }

    fn preference_getter(
        &self,
        _oracle: &dyn CodeOracle,
        prefs: &dyn Display,
        pref_key: &dyn Display,
    ) -> Option<String> {
        Some(format!("{prefs}.getString({}, \"\")", quoted(pref_key)))
    }
}

#[cfg(test)]
mod unit_tests {

    use serde_json::json;

    use crate::backends::TypeIdentifier;

    use super::*;

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

    fn bool_type() -> Box<dyn CodeType> {
        Box::new(BooleanCodeType) as Box<dyn CodeType>
    }

    fn string_type() -> Box<dyn CodeType> {
        Box::new(StringCodeType) as Box<dyn CodeType>
    }

    fn int_type() -> Box<dyn CodeType> {
        Box::new(IntCodeType) as Box<dyn CodeType>
    }

    #[test]
    fn test_type_label() {
        let oracle = &*oracle();

        let ct = bool_type();
        assert_eq!("Boolean".to_string(), ct.type_label(oracle));

        let ct = string_type();
        assert_eq!("String".to_string(), ct.type_label(oracle));

        let ct = int_type();
        assert_eq!("Int".to_string(), ct.type_label(oracle));
    }

    #[test]
    fn test_literal() {
        let oracle = &*oracle();
        let finder = &TestRenderer;

        let ct = bool_type();
        let ctx = "context".to_string();
        assert_eq!(
            "true".to_string(),
            ct.literal(oracle, &ctx, finder, &json!(true))
        );
        assert_eq!(
            "false".to_string(),
            ct.literal(oracle, &ctx, finder, &json!(false))
        );

        let ct = string_type();
        assert_eq!(
            r#""no""#.to_string(),
            ct.literal(oracle, &ctx, finder, &json!("no"))
        );
        assert_eq!(
            r#""yes""#.to_string(),
            ct.literal(oracle, &ctx, finder, &json!("yes"))
        );

        let ct = int_type();
        assert_eq!("1".to_string(), ct.literal(oracle, &ctx, finder, &json!(1)));
        assert_eq!("2".to_string(), ct.literal(oracle, &ctx, finder, &json!(2)));
    }

    #[test]
    fn test_get_value() {
        let oracle = &*oracle();

        let ct = bool_type();
        assert_eq!(
            r#"v.getBool("the-property")"#.to_string(),
            ct.value_getter(oracle, &"v", &"the-property")
        );

        let ct = string_type();
        assert_eq!(
            r#"v.getString("the-property")"#.to_string(),
            ct.value_getter(oracle, &"v", &"the-property")
        );

        let ct = int_type();
        assert_eq!(
            r#"v.getInt("the-property")"#.to_string(),
            ct.value_getter(oracle, &"v", &"the-property")
        );
    }
}
