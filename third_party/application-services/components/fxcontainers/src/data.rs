/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub(crate) const LATEST_VERSION: u32 = 9;

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
#[serde(rename_all = "camelCase", from = "StoredSiteAssociation")]
pub(crate) struct SiteAssociationData {
    pub user_context_id: u32,
    pub include_subdomains: bool,
    #[serde(flatten)]
    pub extra: Map<String, Value>,
}

impl SiteAssociationData {
    pub(crate) fn new(user_context_id: u32, include_subdomains: bool) -> Self {
        Self {
            user_context_id,
            include_subdomains,
            extra: Default::default(),
        }
    }
}

#[derive(Deserialize)]
#[serde(untagged)]
enum StoredSiteAssociation {
    Legacy(u32),
    #[serde(rename_all = "camelCase")]
    Current {
        #[serde(default)]
        user_context_id: u32,
        #[serde(default)]
        include_subdomains: bool,
        #[serde(flatten)]
        extra: Map<String, Value>,
    },
}

impl From<StoredSiteAssociation> for SiteAssociationData {
    fn from(stored: StoredSiteAssociation) -> Self {
        match stored {
            StoredSiteAssociation::Legacy(user_context_id) => Self::new(user_context_id, false),
            StoredSiteAssociation::Current {
                user_context_id,
                include_subdomains,
                extra,
            } => Self {
                user_context_id,
                include_subdomains,
                extra,
            },
        }
    }
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
    pub site_associations: BTreeMap<String, SiteAssociationData>,
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
