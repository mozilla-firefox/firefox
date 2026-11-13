/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::BTreeMap;

use crate::intermediate_representation::{
    EnumDef, FeatureDef, FeatureManifest, ModuleId, ObjectDef, PropDef, TypeRef, VariantDef,
};
use serde_json::json;

pub(crate) fn get_simple_homescreen_feature() -> FeatureManifest {
    FeatureManifest {
        enum_defs: BTreeMap::from([(
            "HomeScreenSection".to_string(),
            EnumDef {
                name: "HomeScreenSection".into(),
                doc: "The sections of the homescreen".into(),
                variants: vec![
                    VariantDef::new("top-sites", "The original frecency sorted sites"),
                    VariantDef::new("jump-back-in", "Jump back in section"),
                    VariantDef::new("recently-saved", "Tabs that have been bookmarked recently"),
                    VariantDef::new("recent-explorations", "Tabs from another source"),
                    VariantDef::new("pocket", "Tabs from another source"),
                ],
            },
        )]),
        obj_defs: Default::default(),
        feature_defs: BTreeMap::from([(
            "homescreen".to_string(),
            FeatureDef::new(
                "homescreen",
                "Represents the homescreen feature",
                vec![PropDef::new(
                    "sections-enabled",
                    &TypeRef::EnumMap(
                        Box::new(TypeRef::Enum("HomeScreenSection".into())),
                        Box::new(TypeRef::Boolean),
                    ),
                    &json!({
                        "top-sites": true,
                        "jump-back-in": false,
                        "recently-saved": false,
                        "recent-explorations": false,
                        "pocket": false,
                    }),
                )],
                false,
            ),
        )]),
        ..Default::default()
    }
}

pub(crate) fn get_feature_manifest(
    obj_defs: Vec<ObjectDef>,
    enum_defs: Vec<EnumDef>,
    feature_defs: Vec<FeatureDef>,
    all_imports: BTreeMap<ModuleId, FeatureManifest>,
) -> FeatureManifest {
    FeatureManifest {
        enum_defs: map_from(enum_defs, |e| e.name()),
        obj_defs: map_from(obj_defs, |o| o.name()),
        feature_defs: map_from(feature_defs, |f| f.name()),
        all_imports,
        ..Default::default()
    }
}

pub(crate) fn get_one_prop_feature_manifest(
    obj_defs: Vec<ObjectDef>,
    enum_defs: Vec<EnumDef>,
    prop: &PropDef,
) -> FeatureManifest {
    FeatureManifest {
        enum_defs: map_from(enum_defs, |e| e.name()),
        obj_defs: map_from(obj_defs, |o| o.name()),
        feature_defs: BTreeMap::from([(
            "".to_string(),
            FeatureDef {
                props: vec![prop.clone()],
                ..Default::default()
            },
        )]),
        ..Default::default()
    }
}

pub(crate) fn get_one_prop_feature_manifest_with_imports(
    obj_defs: Vec<ObjectDef>,
    enum_defs: Vec<EnumDef>,
    prop: &PropDef,
    all_imports: BTreeMap<ModuleId, FeatureManifest>,
) -> FeatureManifest {
    let mut fm = FeatureManifest {
        enum_defs: map_from(enum_defs, |e| e.name()),
        obj_defs: map_from(obj_defs, |o| o.name()),
        all_imports,
        ..Default::default()
    };
    fm.add_feature(FeatureDef {
        props: vec![prop.clone()],
        ..Default::default()
    });
    fm
}

fn map_from<T, F, K>(list: Vec<T>, key: F) -> BTreeMap<K, T>
where
    K: Ord,
    F: Fn(&T) -> K,
{
    let mut res: BTreeMap<K, T> = Default::default();

    for t in list {
        let k = key(&t);
        res.insert(k, t);
    }

    res
}
