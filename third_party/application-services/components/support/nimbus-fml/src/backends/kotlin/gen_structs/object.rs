/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use askama::Template;
use std::fmt::Display;

use crate::backends::{
    CodeDeclaration, CodeOracle, CodeType, LiteralRenderer, TypeIdentifier, VariablesType,
};
use crate::intermediate_representation::{FeatureManifest, Literal, ObjectDef};

use super::filters;

use super::common::{self, code_type};

pub struct ObjectCodeType {
    id: String,
}

impl ObjectCodeType {
    pub fn new(id: String) -> Self {
        Self { id }
    }
}

impl CodeType for ObjectCodeType {
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

    /// The language specific expression that gets a value of the `prop` from the `vars` object.
    ///
    /// The name of the type as it's represented in the `Variables` object.
    /// The string return may be used to combine with an identifier, e.g. a `Variables` method name.
    fn variables_type(&self, _oracle: &dyn CodeOracle) -> VariablesType {
        VariablesType::Variables
    }

    fn create_transform(&self, oracle: &dyn CodeOracle) -> Option<String> {
        Some(format!("{}::create", self.type_label(oracle)))
    }

    fn merge_transform(&self, oracle: &dyn CodeOracle) -> Option<String> {
        Some(format!("{}::mergeWith", self.type_label(oracle)))
    }

    fn value_merger(&self, _oracle: &dyn CodeOracle, default: &dyn Display) -> Option<String> {
        Some(format!("_mergeWith({})", default))
    }

    fn as_json_transform(&self, _oracle: &dyn CodeOracle, prop: &dyn Display) -> Option<String> {
        Some(format!("{}.toJSONObject()", prop))
    }

    fn literal(
        &self,
        oracle: &dyn CodeOracle,
        ctx: &dyn Display,
        renderer: &dyn LiteralRenderer,
        literal: &Literal,
    ) -> String {
        renderer.literal(
            oracle,
            &TypeIdentifier::Object(self.id.clone()),
            literal,
            ctx,
        )
    }
}

#[derive(Template)]
#[template(syntax = "kt", escape = "none", path = "ObjectTemplate.kt")]
pub(crate) struct ObjectCodeDeclaration {
    inner: ObjectDef,
    fm: FeatureManifest,
}

impl ObjectCodeDeclaration {
    pub fn new(fm: &FeatureManifest, inner: &ObjectDef) -> Self {
        Self {
            fm: fm.clone(),
            inner: inner.clone(),
        }
    }
    pub fn inner(&self) -> ObjectDef {
        self.inner.clone()
    }
}

impl CodeDeclaration for ObjectCodeDeclaration {
    fn definition_code(&self, _oracle: &dyn CodeOracle) -> Option<String> {
        Some(self.render().unwrap())
    }

    fn imports(&self, _oracle: &dyn CodeOracle) -> Option<Vec<String>> {
        Some(vec![
            "org.mozilla.experiments.nimbus.internal.FMLObjectInterface".to_string(),
        ])
    }
}

impl LiteralRenderer for ObjectCodeDeclaration {
    fn literal(
        &self,
        oracle: &dyn CodeOracle,
        typ: &TypeIdentifier,
        value: &Literal,
        ctx: &dyn Display,
    ) -> String {
        object_literal(&self.fm, ctx, &self, oracle, typ, value)
    }
}

pub(crate) fn object_literal(
    fm: &FeatureManifest,
    ctx: &dyn Display,
    renderer: &dyn LiteralRenderer,
    oracle: &dyn CodeOracle,
    typ: &TypeIdentifier,
    value: &Literal,
) -> String {
    let id = if let TypeIdentifier::Object(id) = typ {
        id
    } else {
        return oracle.find(typ).literal(oracle, ctx, renderer, value);
    };
    let literal_map = if let Literal::Object(map) = value {
        map
    } else {
        unreachable!(
            "An JSON object is expected for {} object literal",
            oracle.find(typ).type_label(oracle)
        )
    };

    let def = fm.find_object(id).unwrap();

    let args: Vec<String> = literal_map
        .iter()
        .map(|(k, v)| {
            let prop = def.find_prop(k);

            format!(
                "{var_name} = {var_value}",
                var_name = common::var_name(k),
                var_value = oracle.find(&prop.typ).literal(oracle, ctx, renderer, v)
            )
        })
        .collect();

    format!(
        "{typelabel}({args})",
        typelabel = oracle.find(typ).type_label(oracle),
        args = args.join(", ")
    )
}

#[cfg(test)]
mod unit_tests {
    use serde_json::json;

    use crate::{backends::TypeIdentifier, intermediate_representation::Literal};

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
            typ: &TypeIdentifier,
            _value: &Literal,
            _ctx: &dyn Display,
        ) -> String {
            if let TypeIdentifier::Object(nm) = typ {
                format!("{}()", nm)
            } else {
                unreachable!()
            }
        }
    }

    fn oracle() -> Box<dyn CodeOracle> {
        Box::new(TestCodeOracle) as Box<dyn CodeOracle>
    }

    fn code_type(name: &str) -> Box<dyn CodeType> {
        Box::new(ObjectCodeType::new(name.to_string())) as Box<dyn CodeType>
    }

    fn getter_with_fallback(
        ct: &dyn CodeType,
        vars: &dyn Display,
        prop: &dyn Display,
        def: &dyn Display,
    ) -> String {
        let oracle = &*oracle();
        ct.property_getter(oracle, vars, prop, def)
    }

    #[test]
    fn test_type_label() {
        let ct = code_type("AnObject");
        let oracle = &*oracle();
        assert_eq!("AnObject".to_string(), ct.type_label(oracle))
    }

    #[test]
    fn test_literal() {
        let ct = code_type("AnObject");
        let oracle = &*oracle();
        let finder = &TestRenderer;
        let ctx = "ctx".to_string();
        assert_eq!(
            "AnObject()".to_string(),
            ct.literal(oracle, &ctx, finder, &json!({}))
        );
    }

    #[test]
    fn test_get_value() {
        let ct = code_type("AnObject");
        let oracle = &*oracle();

        assert_eq!(
            r#"v.getVariables("the-property")"#.to_string(),
            ct.value_getter(oracle, &"v", &"the-property")
        );
    }

    #[test]
    fn test_getter_with_fallback() {
        let ct = code_type("AnObject");
        assert_eq!(
            r#"vars.getVariables("the-property")?.let(AnObject::create)?._mergeWith(default) ?: default"#
            .to_string(),
            getter_with_fallback(&*ct, &"vars", &"the-property", &"default"));
    }
}
