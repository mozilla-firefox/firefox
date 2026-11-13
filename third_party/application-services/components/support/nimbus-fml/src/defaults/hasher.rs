/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::schema::TypeQuery;
use crate::{
    intermediate_representation::{FeatureDef, ObjectDef, PropDef, TypeRef},
    schema::Sha256Hasher,
};
use serde_json::Value;
use std::{
    collections::{BTreeMap, BTreeSet, HashSet},
    hash::{Hash, Hasher},
};

pub(crate) struct DefaultsHasher<'a> {
    object_defs: &'a BTreeMap<String, ObjectDef>,
}

impl<'a> DefaultsHasher<'a> {
    pub(crate) fn new(objs: &'a BTreeMap<String, ObjectDef>) -> Self {
        Self { object_defs: objs }
    }

    pub(crate) fn hash(&self, feature_def: &FeatureDef) -> u64 {
        let mut hasher = Sha256Hasher::default();
        feature_def.defaults_hash(&mut hasher);

        let types = self.all_types(feature_def);

        // We iterate through the object_defs because they are both
        // ordered, and we want to maintain a stable ordering.
        // By contrast, `types`, a HashSet, definitely does not have a stable ordering.
        for (name, obj_def) in self.object_defs {
            if types.contains(&TypeRef::Object(name.clone())) {
                obj_def.defaults_hash(&mut hasher);
            }
        }

        hasher.finish()
    }

    fn all_types(&self, feature_def: &FeatureDef) -> HashSet<TypeRef> {
        TypeQuery::new(self.object_defs).all_types(feature_def)
    }
}

trait DefaultsHash {
    fn defaults_hash<H: Hasher>(&self, state: &mut H);
}

impl DefaultsHash for FeatureDef {
    fn defaults_hash<H: Hasher>(&self, state: &mut H) {
        self.props.defaults_hash(state);
    }
}

impl DefaultsHash for Vec<PropDef> {
    fn defaults_hash<H: Hasher>(&self, state: &mut H) {
        let mut vec = self.iter().collect::<Vec<_>>();
        vec.sort_by_key(|item| &item.name);

        for item in vec {
            item.defaults_hash(state);
        }
    }
}

impl DefaultsHash for PropDef {
    fn defaults_hash<H: Hasher>(&self, state: &mut H) {
        self.name.hash(state);
        self.default.defaults_hash(state);
    }
}

impl DefaultsHash for ObjectDef {
    fn defaults_hash<H: Hasher>(&self, state: &mut H) {
        self.props.defaults_hash(state);
    }
}

impl DefaultsHash for Value {
    fn defaults_hash<H: Hasher>(&self, state: &mut H) {
        match self {
            Self::Null => 0_u8.hash(state),
            Self::Number(v) => v.hash(state),
            Self::Bool(v) => v.hash(state),
            Self::String(v) => v.hash(state),
            Self::Array(array) => {
                for v in array {
                    v.defaults_hash(state);
                }
            }
            Self::Object(map) => {
                let keys = map.keys().collect::<BTreeSet<_>>();
                for k in keys {
                    let v = map.get(k).unwrap();
                    v.defaults_hash(state);
                }
            }
        }
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use crate::error::Result;

    use serde_json::json;

    #[test]
    fn test_simple_feature_stable_over_time() -> Result<()> {
        let objs = Default::default();

        let feature_def = {
            let p1 = PropDef::new("my-int", &TypeRef::Int, &json!(1));
            let p2 = PropDef::new("my-bool", &TypeRef::Boolean, &json!(true));
            let p3 = PropDef::new("my-string", &TypeRef::String, &json!("string"));
            FeatureDef::new("test_feature", "", vec![p1, p2, p3], false)
        };

        let mut prev: Option<u64> = None;
        for _ in 0..100 {
            let hasher = DefaultsHasher::new(&objs);
            let hash = hasher.hash(&feature_def);
            if let Some(prev) = prev {
                assert_eq!(prev, hash);
            }
            prev = Some(hash);
        }

        Ok(())
    }

    #[test]
    fn test_simple_feature_is_stable_with_props_in_any_order() -> Result<()> {
        let objs = Default::default();

        let p1 = PropDef::new("my-int", &TypeRef::Int, &json!(1));
        let p2 = PropDef::new("my-bool", &TypeRef::Boolean, &json!(true));
        let p3 = PropDef::new("my-string", &TypeRef::String, &json!("string"));

        let f1 = FeatureDef::new(
            "test_feature",
            "",
            vec![p1.clone(), p2.clone(), p3.clone()],
            false,
        );
        let f2 = FeatureDef::new("test_feature", "", vec![p3, p2, p1], false);

        let hasher = DefaultsHasher::new(&objs);
        assert_eq!(hasher.hash(&f1), hasher.hash(&f2));
        Ok(())
    }

    #[test]
    fn test_simple_feature_is_stable_changing_types() -> Result<()> {
        let objs = Default::default();

        // unsure how you'd do this.
        let f1 = {
            let prop1 = PropDef::new("p1", &TypeRef::Int, &json!(42));
            let prop2 = PropDef::new("p2", &TypeRef::String, &json!("Yes"));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };

        let f2 = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!(42));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!("Yes"));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };

        let hasher = DefaultsHasher::new(&objs);
        assert_eq!(hasher.hash(&f1), hasher.hash(&f2));

        Ok(())
    }

    #[test]
    fn test_simple_feature_is_sensitive_to_change() -> Result<()> {
        let objs = Default::default();

        let f1 = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Yes"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };

        let hasher = DefaultsHasher::new(&objs);

        // Sensitive to change in type of properties
        let ne = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Nope"));
            let prop2 = PropDef::new("p2", &TypeRef::Boolean, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };
        assert_ne!(hasher.hash(&f1), hasher.hash(&ne));

        // Sensitive to change in name of properties
        let ne = {
            let prop1 = PropDef::new("p1_", &TypeRef::String, &json!("Yes"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };
        assert_ne!(hasher.hash(&f1), hasher.hash(&ne));

        // Not Sensitive to change in changes in coenrollment status
        let eq = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Yes"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], true)
        };
        assert_eq!(hasher.hash(&f1), hasher.hash(&eq));

        Ok(())
    }

    #[test]
    fn test_feature_is_sensitive_to_object_change() -> Result<()> {
        let obj_nm = "MyObject";
        let obj_t = TypeRef::Object(obj_nm.to_string());

        let f1 = {
            let prop1 = PropDef::new("p1", &obj_t, &json!({}));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let objs = {
            let obj_def = ObjectDef::new(
                obj_nm,
                &[PropDef::new("obj-p1", &TypeRef::Boolean, &json!(true))],
            );

            ObjectDef::into_map(&[obj_def])
        };

        let hasher = DefaultsHasher::new(&objs);
        // Get an original hash here.
        let h1 = hasher.hash(&f1);

        // Then change the object later on.
        let objs = {
            let obj_def = ObjectDef::new(
                obj_nm,
                &[PropDef::new("obj-p1", &TypeRef::Boolean, &json!(false))],
            );

            ObjectDef::into_map(&[obj_def])
        };

        let hasher = DefaultsHasher::new(&objs);
        let ne = hasher.hash(&f1);

        assert_ne!(h1, ne);

        Ok(())
    }

    #[test]
    fn test_hash_is_sensitive_to_nested_change() -> Result<()> {
        let obj1_nm = "MyObject";
        let obj1_t = TypeRef::Object(obj1_nm.to_string());

        let obj2_nm = "MyNestedObject";
        let obj2_t = TypeRef::Object(obj2_nm.to_string());

        let obj1_def = ObjectDef::new(obj1_nm, &[PropDef::new("p1-obj2", &obj2_t, &json!({}))]);

        let f1 = {
            let prop1 = PropDef::new("p1", &obj1_t.clone(), &json!({}));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let objs = {
            let obj2_def = ObjectDef::new(
                obj2_nm,
                &[PropDef::new("p1-string", &TypeRef::String, &json!("one"))],
            );
            ObjectDef::into_map(&[obj1_def.clone(), obj2_def])
        };

        let hasher = DefaultsHasher::new(&objs);
        // Get an original hash here.
        let h1 = hasher.hash(&f1);

        // Now change just the deeply nested object.
        let objs = {
            let obj2_def = ObjectDef::new(
                obj2_nm,
                &[PropDef::new("p1-string", &TypeRef::String, &json!("two"))],
            );
            ObjectDef::into_map(&[obj1_def.clone(), obj2_def])
        };
        let hasher = DefaultsHasher::new(&objs);
        let ne = hasher.hash(&f1);

        assert_ne!(h1, ne);
        Ok(())
    }
}
