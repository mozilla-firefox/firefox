/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::{BTreeMap, BTreeSet, HashMap};

use serde_json::Value;

use crate::intermediate_representation::{EnumDef, FeatureDef, PropDef, TypeRef};

pub(crate) struct ValuesFinder<'a> {
    enum_defs: &'a BTreeMap<String, EnumDef>,
    string_aliases: HashMap<&'a str, &'a PropDef>,
    feature_value: &'a Value,
}

impl<'a> ValuesFinder<'a> {
    pub(crate) fn new(
        enum_defs: &'a BTreeMap<String, EnumDef>,
        feature_def: &'a FeatureDef,
        feature_value: &'a Value,
    ) -> Self {
        Self {
            enum_defs,
            string_aliases: feature_def.get_string_aliases(),
            feature_value,
        }
    }

    pub(crate) fn all_specific_strings(&self, type_ref: &TypeRef) -> BTreeSet<String> {
        match type_ref {
            TypeRef::StringAlias(_) => self.get_string_alias_values(type_ref),
            TypeRef::Enum(type_name) => self.get_enum_values(type_name),
            _ => Default::default(),
        }
    }

    #[allow(dead_code)]
    #[cfg(feature = "client-lib")]
    pub(crate) fn all_placeholders(&self, type_ref: &TypeRef) -> BTreeSet<String> {
        let strings: &[&str] = match type_ref {
            TypeRef::Boolean => &["true", "false"],
            TypeRef::Int => &["0"],
            TypeRef::String | TypeRef::BundleText | TypeRef::BundleImage => &["\"\""],
            TypeRef::List(_) => &["[]"],
            TypeRef::Object(_) | TypeRef::EnumMap(_, _) | TypeRef::StringMap(_) => &["{}"],

            _ => &[],
        };

        strings.iter().cloned().map(String::from).collect()
    }
}

impl ValuesFinder<'_> {
    fn get_enum_values(&self, type_name: &str) -> BTreeSet<String> {
        if let Some(def) = self.enum_defs.get(type_name) {
            def.variants.iter().map(|v| v.name()).collect()
        } else {
            Default::default()
        }
    }

    fn get_string_alias_values(&self, alias_type: &TypeRef) -> BTreeSet<String> {
        let type_name = alias_type.name().unwrap();
        let prop = self.string_aliases[type_name];

        let def_type = &prop.typ;
        let def_value = self.feature_value.get(&prop.name).unwrap();

        let mut set = BTreeSet::new();
        collect_string_alias_values(alias_type, def_type, def_value, &mut set);
        set
    }
}

/// Takes
/// - a string-alias type, StringAlias("TeammateName") / TeamMateName
/// - a type definition of a wider collection of teammates: e.g. Map<TeamMateName, TeamMate>
/// - an a value for the collection of teammates: e.g. {"Alice": {}, "Bonnie": {}, "Charlie": {}, "Dawn"}
///
/// and fills a hash set with the full set of TeamMateNames, in this case: ["Alice", "Bonnie", "Charlie", "Dawn"]
fn collect_string_alias_values(
    alias_type: &TypeRef,
    def_type: &TypeRef,
    def_value: &Value,
    set: &mut BTreeSet<String>,
) {
    match (def_type, def_value) {
        (TypeRef::StringAlias(_), Value::String(s)) if alias_type == def_type => {
            set.insert(s.clone());
        }
        (TypeRef::Option(dt), dv) if dv != &Value::Null => {
            collect_string_alias_values(alias_type, dt, dv, set);
        }
        (TypeRef::EnumMap(kt, _), Value::Object(map)) if alias_type == &**kt => {
            set.extend(map.keys().cloned());
        }
        (TypeRef::EnumMap(_, vt), Value::Object(map))
        | (TypeRef::StringMap(vt), Value::Object(map)) => {
            for item in map.values() {
                collect_string_alias_values(alias_type, vt, item, set);
            }
        }
        (TypeRef::List(vt), Value::Array(array)) => {
            for item in array {
                collect_string_alias_values(alias_type, vt, item, set);
            }
        }
        _ => {}
    }
}

#[cfg(test)]
mod string_alias {

    use super::*;
    use serde_json::json;

    fn test_set(alias_type: &TypeRef, def_type: &TypeRef, def_value: &Value, set: &[&str]) {
        let mut observed = BTreeSet::new();
        collect_string_alias_values(alias_type, def_type, def_value, &mut observed);

        let expected: BTreeSet<_> = set.iter().map(|s| s.to_owned().to_owned()).collect();
        assert_eq!(expected, observed);
    }

    // Does this string belong in the type definition?
    #[test]
    fn test_validate_value() {
        let sa = TypeRef::StringAlias("Name".to_string());

        // type definition is Name
        let def = sa.clone();
        let value = json!("yes");
        test_set(&sa, &def, &value, &["yes"]);

        // type definition is Name?
        let def = TypeRef::Option(Box::new(sa.clone()));
        let value = json!("yes");
        test_set(&sa, &def, &value, &["yes"]);

        let value = json!(null);
        test_set(&sa, &def, &value, &[]);

        // type definition is Map<Name, Boolean>
        let def = TypeRef::EnumMap(Box::new(sa.clone()), Box::new(TypeRef::Boolean));
        let value = json!({
            "yes": true,
            "YES": false,
        });
        test_set(&sa, &def, &value, &["yes", "YES"]);

        // type definition is Map<String, Name>
        let def = TypeRef::EnumMap(Box::new(TypeRef::String), Box::new(sa.clone()));
        let value = json!({
            "ok": "yes",
            "OK": "YES",
        });
        test_set(&sa, &def, &value, &["yes", "YES"]);

        // type definition is List<String>
        let def = TypeRef::List(Box::new(sa.clone()));
        let value = json!(["yes", "YES"]);
        test_set(&sa, &def, &value, &["yes", "YES"]);

        // type definition is List<Map<String, Name>>
        let def = TypeRef::List(Box::new(TypeRef::StringMap(Box::new(sa.clone()))));
        let value = json!([{"y": "yes"}, {"Y": "YES"}]);
        test_set(&sa, &def, &value, &["yes", "YES"]);

        // type definition is Map<String, List<Name>>
        let def = TypeRef::StringMap(Box::new(TypeRef::List(Box::new(sa.clone()))));
        let value = json!({"y": ["yes"], "Y": ["YES"]});
        test_set(&sa, &def, &value, &["yes", "YES"]);
    }
}
