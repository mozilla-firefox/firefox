// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeMap;

use serde_json::Value;

use crate::intermediate_representation::{
    EnumDef, GeckoPrefDef, ObjectDef, PrefBranch, PropDef, TypeRef, VariantDef,
};

pub(crate) mod intermediate_representation;

impl EnumDef {
    pub(crate) fn new(nm: &str, variants: &[&str]) -> Self {
        let variants = variants
            .iter()
            .map(|s| VariantDef {
                name: s.to_string(),
                doc: format!("Documentation for {s}"),
            })
            .collect();
        Self {
            name: nm.to_string(),
            doc: format!("{nm} documentation"),
            variants,
        }
    }

    pub(crate) fn into_map(value: &[Self]) -> BTreeMap<String, Self> {
        value.iter().map(|def| (def.name(), def.clone())).collect()
    }
}

impl ObjectDef {
    pub(crate) fn new(nm: &str, props: &[PropDef]) -> Self {
        Self {
            name: nm.to_string(),
            doc: nm.to_string(),
            props: props.into(),
        }
    }

    pub(crate) fn into_map(value: &[Self]) -> BTreeMap<String, Self> {
        value.iter().map(|def| (def.name(), def.clone())).collect()
    }
}

impl PropDef {
    pub(crate) fn new(nm: &str, typ: &TypeRef, value: &Value) -> Self {
        Self {
            name: nm.to_string(),
            typ: typ.clone(),
            default: value.clone(),
            doc: format!("{nm} property of type {typ}"),
            gecko_pref: None,
            string_alias: None,
        }
    }

    pub(crate) fn new_with_gecko_pref(
        nm: &str,
        typ: &TypeRef,
        value: &Value,
        pref: &str,
        branch: PrefBranch,
    ) -> Self {
        Self {
            name: nm.to_string(),
            typ: typ.clone(),
            default: value.clone(),
            doc: format!("{nm} property of type {typ}"),
            gecko_pref: Some(GeckoPrefDef {
                pref: pref.into(),
                branch,
            }),
            string_alias: None,
        }
    }

    pub(crate) fn with_string_alias(nm: &str, typ: &TypeRef, value: &Value, sa: &TypeRef) -> Self {
        PropDef {
            name: nm.to_string(),
            typ: typ.clone(),
            default: value.clone(),
            doc: nm.to_string(),
            gecko_pref: None,
            string_alias: Some(sa.clone()),
        }
    }

    pub(crate) fn with_doc(nm: &str, doc: &str, typ: &TypeRef, default: &Value) -> Self {
        PropDef {
            name: nm.to_string(),
            doc: doc.to_string(),
            typ: typ.clone(),
            default: default.clone(),
            gecko_pref: None,
            string_alias: None,
        }
    }
}
