/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use error_support::handle_error;
use parking_lot::{Mutex, MutexGuard, RwLock, RwLockReadGuard};

use crate::container::Container;
use crate::data::{ContainersData, Identity, MAX_USER_CONTEXT_ID};
use crate::defaults::{self, DefaultIdentity};
use crate::definitions::{ContainerColor, ContainerIcon};
use crate::error::{InitError, StoreError};
use crate::format::{parse, serialize};

/// How the store reaches the outside world.
///
/// The callback may call `serialize`, but mutating the store from it deadlocks
/// on the callback lock. Mutate on a later turn.
#[uniffi::export(callback_interface)]
pub trait ContainersCallback: Send + Sync {
    fn persist(&self);
}

struct NoopCallback;

impl ContainersCallback for NoopCallback {
    fn persist(&self) {}
}

#[derive(Clone, Debug, PartialEq, Eq, uniffi::Record)]
pub struct SiteAssociation {
    pub site: String,
    pub user_context_id: u32,
}

#[derive(uniffi::Object)]
pub struct ContainersStore {
    data: Mutex<ContainersData>,
    default_identities: Vec<DefaultIdentity>,
    callback: RwLock<Box<dyn ContainersCallback>>,
}

#[uniffi::export]
impl ContainersStore {
    /// Seeds the store from a stored document, or from the default identities
    /// when `bytes` is `None`. If a migration ran, the document is persisted
    /// before this returns.
    ///
    /// `default_identities` replaces the shipped public identities, for
    /// embedders that let enterprise policy define them. They are consulted
    /// whether or not there is a stored document: the label of a default
    /// container the user has not renamed comes from them, since it is not
    /// stored.
    #[uniffi::constructor]
    #[handle_error(InitError)]
    pub fn new(
        bytes: Option<Vec<u8>>,
        default_identities: Option<Vec<DefaultIdentity>>,
        callback: Box<dyn ContainersCallback>,
    ) -> Result<Self, InitError> {
        let default_identities = default_identities.unwrap_or_else(defaults::shipped_defaults);

        let (data, migrated) = match bytes {
            Some(bytes) => parse(&bytes)?,
            None => (defaults::defaults_with(&default_identities), true),
        };

        let store = Self {
            data: Mutex::new(data),
            default_identities,
            callback: RwLock::new(callback),
        };

        if migrated {
            store.persist();
        }

        Ok(store)
    }

    /// Drops the callback, so that a late mutation during teardown cannot reach
    /// an embedder that is already gone.
    ///
    /// One way: there is no putting it back. From here on the store keeps
    /// working in memory but persists nothing.
    pub fn unset_callback(&self) {
        *self.callback.write() = Box::new(NoopCallback);
    }

    /// The document as it stands, for the embedder to write.
    pub fn serialize(&self) -> Vec<u8> {
        serialize(&self.data())
    }

    pub fn public_identities(&self) -> Vec<Container> {
        self.data()
            .public_identities()
            .map(|identity| self.container(identity))
            .collect()
    }

    pub fn public_user_context_ids(&self) -> Vec<u32> {
        self.data()
            .public_identities()
            .map(|identity| identity.user_context_id)
            .collect()
    }

    pub fn private_user_context_ids(&self) -> Vec<u32> {
        self.data()
            .private_identities()
            .map(|identity| identity.user_context_id)
            .collect()
    }

    pub fn public_identity_from_id(&self, user_context_id: u32) -> Option<Container> {
        self.data()
            .identities
            .iter()
            .find(|identity| identity.public && identity.user_context_id == user_context_id)
            .map(|identity| self.container(identity))
    }

    pub fn private_identity(&self, name: &str) -> Option<Container> {
        self.data()
            .find_private_by_name(name)
            .map(|identity| self.container(identity))
    }

    #[handle_error(StoreError)]
    pub fn create(
        &self,
        name: &str,
        icon: ContainerIcon,
        color: ContainerColor,
    ) -> Result<Container, StoreError> {
        if name.trim().is_empty() {
            return Err(StoreError::EmptyName);
        }

        let identity = {
            let mut data = self.data();

            let identity = Identity {
                user_context_id: next_user_context_id(&mut data)?,
                public: true,
                icon: icon.name().to_string(),
                color: color.name().to_string(),
                name: Some(name.to_string()),
                policy: false,
                policy_id: None,
                extra: Default::default(),
            };
            data.identities.push(identity.clone());

            identity
        };

        self.persist();

        Ok(self.container(&identity))
    }

    #[handle_error(StoreError)]
    pub fn create_for_policy(&self, policy_id: &str) -> Result<Container, StoreError> {
        if policy_id.trim().is_empty() {
            return Err(StoreError::EmptyPolicyId);
        }

        let identity = {
            let mut data = self.data();

            let identity = Identity {
                user_context_id: next_user_context_id(&mut data)?,
                public: false,
                icon: String::new(),
                color: String::new(),
                name: Some(policy_id.to_string()),
                policy: true,
                policy_id: Some(policy_id.to_string()),
                extra: Default::default(),
            };
            data.identities.push(identity.clone());

            identity
        };

        self.persist();

        Ok(self.container(&identity))
    }

    pub fn policy_identities(&self) -> Vec<Container> {
        self.data()
            .policy_identities()
            .map(|identity| self.container(identity))
            .collect()
    }

    pub fn policy_identity(&self, policy_id: &str) -> Option<Container> {
        self.data()
            .find_policy_by_id(policy_id)
            .map(|identity| self.container(identity))
    }

    pub fn remove_policy_identity(&self, user_context_id: u32) -> Option<Container> {
        let identity = {
            let mut data = self.data();
            let index = data.identities.iter().position(|identity| {
                identity.policy && identity.user_context_id == user_context_id
            })?;

            // A site can only be bound to a public container, so there should be
            // nothing to drop here. Cheap enough to not depend on that.
            data.site_associations
                .retain(|_, id| *id != user_context_id);

            data.identities.remove(index)
        };

        self.persist();

        Some(self.container(&identity))
    }

    #[handle_error(StoreError)]
    pub fn update(
        &self,
        user_context_id: u32,
        name: &str,
        icon: ContainerIcon,
        color: ContainerColor,
    ) -> Result<Option<Container>, StoreError> {
        if name.trim().is_empty() {
            return Err(StoreError::EmptyName);
        }

        let identity =
            {
                let mut data = self.data();
                let Some(identity) = data.identities.iter_mut().find(|identity| {
                    identity.public && identity.user_context_id == user_context_id
                }) else {
                    return Ok(None);
                };

                identity.name = Some(name.to_string());
                identity.icon = icon.name().to_string();
                identity.color = color.name().to_string();

                identity.clone()
            };

        self.persist();

        Ok(Some(self.container(&identity)))
    }

    /// Removes a public container. Clearing the data stored under its origin
    /// attributes is the embedder's job.
    pub fn remove(&self, user_context_id: u32) -> Option<Container> {
        let identity = {
            let mut data = self.data();
            let index = data.identities.iter().position(|identity| {
                identity.public && identity.user_context_id == user_context_id
            })?;

            data.site_associations
                .retain(|_, id| *id != user_context_id);

            data.identities.remove(index)
        };

        self.persist();

        Some(self.container(&identity))
    }

    /// Moves the given public containers before `position`, or to the end when
    /// it is -1. Positions below -1 are rejected.
    pub fn move_containers(&self, user_context_ids: Vec<u32>, position: i64) -> bool {
        if position < -1 {
            return false;
        }

        {
            let mut data = self.data();

            let moved: Vec<Identity> = data
                .identities
                .iter()
                .filter(|identity| {
                    identity.public && user_context_ids.contains(&identity.user_context_id)
                })
                .cloned()
                .collect();

            if moved.is_empty() {
                return false;
            }

            let mut destination = if position == -1 {
                data.identities.len() as i64
            } else {
                position
            };

            // Skip over the private identities that sit before the destination.
            for (index, identity) in data.identities.iter().enumerate() {
                if !identity.public && destination >= index as i64 {
                    destination += 1;
                }
            }

            data.identities.retain(|identity| {
                !identity.public || !user_context_ids.contains(&identity.user_context_id)
            });

            let destination = (destination.max(0) as usize).min(data.identities.len());
            for (offset, identity) in moved.into_iter().enumerate() {
                data.identities.insert(destination + offset, identity);
            }
        }

        self.persist();

        true
    }

    #[handle_error(StoreError)]
    pub fn set_site_association(&self, site: &str, user_context_id: u32) -> Result<(), StoreError> {
        {
            let mut data = self.data();

            if !data
                .identities
                .iter()
                .any(|identity| identity.public && identity.user_context_id == user_context_id)
            {
                return Err(StoreError::NoSuchContainer { user_context_id });
            }

            let host = normalize_site(site).ok_or(StoreError::InvalidSite)?;

            if data.site_associations.get(&host) == Some(&user_context_id) {
                return Ok(());
            }

            data.site_associations.insert(host, user_context_id);
        }

        self.persist();

        Ok(())
    }

    pub fn remove_site_association(&self, site: &str) {
        let Some(host) = normalize_site(site) else {
            return;
        };

        if self.data().site_associations.remove(&host).is_none() {
            return;
        }

        self.persist();
    }

    /// The container bound to `site`, or 0 when there is none.
    pub fn get_site_association(&self, site: &str) -> u32 {
        normalize_site(site)
            .and_then(|host| self.data().site_associations.get(&host).copied())
            .unwrap_or(0)
    }

    pub fn get_site_associations(&self, user_context_id: Option<u32>) -> Vec<SiteAssociation> {
        self.data()
            .site_associations
            .iter()
            .filter(|(_, id)| user_context_id.is_none_or(|wanted| **id == wanted))
            .map(|(site, id)| SiteAssociation {
                site: site.clone(),
                user_context_id: *id,
            })
            .collect()
    }
}

/// Reserves the next id, or reports that there is none left. The counter is
/// shared by every kind of container, policy-owned ones included, so that an id
/// is never handed out twice.
fn next_user_context_id(data: &mut ContainersData) -> Result<u32, StoreError> {
    // The reserved id is the last valid one, so it has to stay free.
    if data.last_user_context_id >= MAX_USER_CONTEXT_ID - 1 {
        return Err(StoreError::IdSpaceExhausted);
    }

    data.last_user_context_id += 1;
    Ok(data.last_user_context_id)
}

/// Not exported: these hand out lock guards, which have no meaning across the
/// FFI boundary.
impl ContainersStore {
    fn data(&self) -> MutexGuard<'_, ContainersData> {
        self.data.lock()
    }

    /// Mirrors getUserContextLabel: only a public container falls back to the
    /// label of the default identity that owns its id.
    fn container(&self, identity: &Identity) -> Container {
        let default_label = identity
            .public
            .then(|| defaults::default_label(&self.default_identities, identity.user_context_id))
            .flatten();

        Container::from_identity(identity, default_label)
    }

    fn callback(&self) -> RwLockReadGuard<'_, Box<dyn ContainersCallback>> {
        self.callback.read()
    }

    /// Always called with the data lock released, so that the callback is free
    /// to serialize.
    fn persist(&self) {
        self.callback().persist();
    }
}

/// Lower cased, IDN-encoded host, or `None` when `site` cannot be bound to a
/// container.
#[uniffi::export]
pub fn normalize_site(site: &str) -> Option<String> {
    // Strict applies the STD3 ASCII rules, which is what keeps out everything
    // that is not a host: ports, paths, userinfo, percent escapes, brackets,
    // wildcards and whitespace. The lenient variant accepts all of them.
    //
    // It also rejects a trailing dot, which does name the DNS root and which
    // Gecko accepts, so validate without it and put it back.
    let (bare, root) = match site.strip_suffix('.') {
        Some(bare) => (bare, "."),
        None => (site, ""),
    };

    let host = idna::domain_to_ascii_strict(bare).ok()?;
    Some(format!("{host}{root}"))
}
