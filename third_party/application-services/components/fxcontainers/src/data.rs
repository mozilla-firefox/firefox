/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub(crate) const LATEST_VERSION: u32 = 8;

/// Reserved for the IndexedDB backend of the extension storage.local API. Never
/// reassign it: extensions would lose access to data stored under it.
pub(crate) const MAX_USER_CONTEXT_ID: u32 = u32::MAX;

/// Fields that no version of the format knows about are round-tripped verbatim
/// through `extra`, so that a document written by a newer Firefox keeps its
/// data when an older one rewrites it.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct Identity {
    #[serde(default)]
    pub user_context_id: u32,
    #[serde(default)]
    pub public: bool,
    #[serde(default)]
    pub icon: String,
    #[serde(default)]
    pub color: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub policy: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub policy_id: Option<String>,
    #[serde(flatten)]
    pub extra: Map<String, Value>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct ContainersData {
    #[serde(default)]
    pub version: u32,
    #[serde(default)]
    pub last_user_context_id: u32,
    #[serde(default)]
    pub identities: Vec<Identity>,
    #[serde(default)]
    pub site_associations: BTreeMap<String, u32>,
    #[serde(flatten)]
    pub extra: Map<String, Value>,
}

impl ContainersData {
    pub(crate) fn public_identities(&self) -> impl Iterator<Item = &Identity> {
        self.identities.iter().filter(|identity| identity.public)
    }

    pub(crate) fn private_identities(&self) -> impl Iterator<Item = &Identity> {
        self.identities.iter().filter(|identity| !identity.public)
    }

    pub(crate) fn policy_identities(&self) -> impl Iterator<Item = &Identity> {
        self.identities.iter().filter(|identity| identity.policy)
    }

    pub(crate) fn find_private_by_name(&self, name: &str) -> Option<&Identity> {
        self.identities.iter().find(|identity| {
            !identity.public && !identity.policy && identity.name.as_deref() == Some(name)
        })
    }

    pub(crate) fn find_policy_by_id(&self, policy_id: &str) -> Option<&Identity> {
        self.identities
            .iter()
            .find(|identity| identity.policy && identity.policy_id.as_deref() == Some(policy_id))
    }
}
