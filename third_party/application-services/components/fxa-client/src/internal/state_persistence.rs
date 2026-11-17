/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Serialization of `FirefoxAccount` state to/from a JSON string.
//!
//! This module implements the ability to serialize a `FirefoxAccount` struct to and from
//! a JSON string. The idea is that calling code will use this to persist the account state
//! to storage.
//!
//! Many of the details here are a straightforward use of `serde`, with all persisted data being
//! a field on a `State` struct. This is, however, some additional complexity around handling data
//! migrations - we need to be able to evolve the internal details of the `State` struct while
//! gracefully handing users who are upgrading from an older version of a consuming app, which has
//! stored account state from an older version of this component.
//!
//! Data migration is handled by explicitly naming different versions of the state struct to
//! correspond to different incompatible changes to the data representation, e.g. `StateV1` and
//! `StateV2`. We then wrap this in a `PersistedStateTagged` enum whose serialization gets explicitly
//! tagged with the corresponding state version number.
//!
//! For backwards-compatible changes to the data (such as adding a new field that has a sensible
//! default) we keep the current `State` struct, but modify it in such a way that `serde` knows
//! how to do the right thing.
//!
//! For backwards-incompatible changes to the data (such as removing or significantly refactoring
//! fields) we define a new `StateV{X+1}` struct, and use the `From` trait to define how to update
//! from older struct versions.
//! For an example how the conversion works, [we can look at `StateV1` which was deliberately removed](https://github.com/mozilla/application-services/issues/3912)
//! The code that was deleted demonstrates how we can implement the migration

use serde_derive::*;
use std::collections::{HashMap, HashSet};

use super::{
    config::Config,
    oauth::{AccessTokenInfo, RefreshToken},
    profile::Profile,
    CachedResponse, Result,
};
use crate::{DeviceCapability, LocalDevice, ScopedKey};

// These are the public API for working with the persisted state.

pub(crate) type PersistedState = StateV2;

/// Parse a `State` from a JSON string, performing migrations if necessary.
///
pub(crate) fn state_from_json(data: &str) -> Result<PersistedState> {
    let stored_state: PersistedStateTagged = serde_json::from_str(data)?;
    upgrade_state(stored_state)
}

/// Serialize a `State` to a JSON string.
///
pub(crate) fn state_to_json(state: &PersistedState) -> Result<String> {
    let state = PersistedStateTagged::V2(state.clone());
    serde_json::to_string(&state).map_err(Into::into)
}

fn upgrade_state(in_state: PersistedStateTagged) -> Result<PersistedState> {
    match in_state {
        PersistedStateTagged::V2(state) => Ok(state),
    }
}

/// `PersistedStateTagged` is a tagged container for one of the state versions.
/// Serde picks the right `StructVX` to deserialized based on the schema_version tag.
///
#[derive(Serialize, Deserialize)]
#[serde(tag = "schema_version")]
#[allow(clippy::large_enum_variant)]
enum PersistedStateTagged {
    V2(StateV2),
}

/// `StateV2` is the current state schema. It and its fields all need to be public
/// so that they can be used directly elsewhere in the crate.
///
/// If you want to modify what gets stored in the state, consider the following:
///
///   * Is the change backwards-compatible with previously-serialized data?
///     If so then you'll need to tell serde how to fill in a suitable default.
///     If not then you'll need to make a new `StateV3` and implement an explicit migration.
///
///   * How does the new field need to be modified when the user disconnects from the account or is
///     logged out from auth issues? Update [state_manager.disconnect] and
///     [state_manager.on_auth_issues].
///
#[derive(Clone, Serialize, Deserialize)]
pub(crate) struct StateV2 {
    pub(crate) config: Config,
    pub(crate) current_device_id: Option<String>,
    pub(crate) refresh_token: Option<RefreshToken>,
    pub(crate) scoped_keys: HashMap<String, ScopedKey>,
    pub(crate) last_handled_command: Option<u64>,
    // Everything below here was added after `StateV2` was initially defined,
    // and hence needs to have a suitable default value.
    // We can remove serde(default) when we define a `StateV3`.
    #[serde(default)]
    pub(crate) commands_data: HashMap<String, String>,
    #[serde(default)]
    pub(crate) device_capabilities: HashSet<DeviceCapability>,
    #[serde(default)]
    pub(crate) access_token_cache: HashMap<String, AccessTokenInfo>,
    pub(crate) session_token: Option<String>, // Hex-formatted string.
    pub(crate) last_seen_profile: Option<CachedResponse<Profile>>,
    // The last LocalDevice info sent back from the server
    #[serde(default)]
    pub(crate) server_local_device_info: Option<LocalDevice>,

    #[serde(default)]
    pub(crate) logged_out_from_auth_issues: bool,

    // If we had a refresh token which we dropped due to auth issues, this
    // is the set of scopes the refresh token had and what we should ask for
    // when reauthenticating.
    // Not yet wired up to the reauth flows, but we track the value now.
    #[serde(default)]
    pub(crate) last_refresh_token_scopes: HashSet<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_invalid_schema_version() {
        let state_v1_json = "{\"schema_version\":\"V1\",\"client_id\":\"98adfa37698f255b\",\"redirect_uri\":\"https://lockbox.firefox.com/fxa/ios-redirect.html\",\"config\":{\"content_url\":\"https://accounts.firefox.com\",\"auth_url\":\"https://api.accounts.firefox.com/\",\"oauth_url\":\"https://oauth.accounts.firefox.com/\",\"profile_url\":\"https://profile.accounts.firefox.com/\",\"token_server_endpoint_url\":\"https://token.services.mozilla.com/1.0/sync/1.5\",\"authorization_endpoint\":\"https://accounts.firefox.com/authorization\",\"issuer\":\"https://accounts.firefox.com\",\"jwks_uri\":\"https://oauth.accounts.firefox.com/v1/jwks\",\"token_endpoint\":\"https://oauth.accounts.firefox.com/v1/token\",\"userinfo_endpoint\":\"https://profile.accounts.firefox.com/v1/profile\"},\"oauth_cache\":{\"https://identity.mozilla.com/apps/oldsync https://identity.mozilla.com/apps/lockbox profile\":{\"access_token\":\"bef37ec0340783356bcac67a86c4efa23a56f2ddd0c7a6251d19988bab7bdc99\",\"keys\":\"{\\\"https://identity.mozilla.com/apps/oldsync\\\":{\\\"kty\\\":\\\"oct\\\",\\\"scope\\\":\\\"https://identity.mozilla.com/apps/oldsync\\\",\\\"k\\\":\\\"kMtwpVC0ZaYFJymPza8rXK_0CgCp3KMwRStwGfBRBDtL6hXRDVJgQFaoOQ2dimw0Bko5WVv2gNTy7RX5zFYZHg\\\",\\\"kid\\\":\\\"1542236016429-Ox1FbJfFfwTe5t-xq4v2hQ\\\"},\\\"https://identity.mozilla.com/apps/lockbox\\\":{\\\"kty\\\":\\\"oct\\\",\\\"scope\\\":\\\"https://identity.mozilla.com/apps/lockbox\\\",\\\"k\\\":\\\"Qk4K4xF2PgQ6XvBXW8X7B7AWwWgW2bHQov9NHNd4v-k\\\",\\\"kid\\\":\\\"1231014287-KDVj0DFaO3wGpPJD8oPwVg\\\"}}\",\"refresh_token\":\"bed5532f4fea7e39c5c4f609f53603ee7518fd1c103cc4034da3618f786ed188\",\"expires_at\":1543474657,\"scopes\":[\"https://identity.mozilla.com/apps/oldsync\",\"https://identity.mozilla.com/apps/lockbox\",\"profile\"]}}}";
        if state_from_json(state_v1_json).is_ok() {
            panic!("Invalid schema passed the conversion from json")
        }
    }

    #[test]
    fn test_v2_ignores_unknown_fields_introduced_by_future_changes_to_the_schema() {
        // This is a snapshot of what some persisted StateV2 data would look before any backwards-compatible changes
        // were made. It's very important that you don't modify this string, which would defeat the point of the test!
        let state_v2_json = "{\"schema_version\":\"V2\",\"config\":{\"client_id\":\"98adfa37698f255b\",\"redirect_uri\":\"https://lockbox.firefox.com/fxa/ios-redirect.html\",\"content_url\":\"https://accounts.firefox.com\",\"remote_config\":{\"auth_url\":\"https://api.accounts.firefox.com/\",\"oauth_url\":\"https://oauth.accounts.firefox.com/\",\"profile_url\":\"https://profile.accounts.firefox.com/\",\"token_server_endpoint_url\":\"https://token.services.mozilla.com/1.0/sync/1.5\",\"authorization_endpoint\":\"https://accounts.firefox.com/authorization\",\"issuer\":\"https://accounts.firefox.com\",\"jwks_uri\":\"https://oauth.accounts.firefox.com/v1/jwks\",\"token_endpoint\":\"https://oauth.accounts.firefox.com/v1/token\",\"userinfo_endpoint\":\"https://profile.accounts.firefox.com/v1/profile\"}},\"refresh_token\":{\"token\":\"bed5532f4fea7e39c5c4f609f53603ee7518fd1c103cc4034da3618f786ed188\",\"scopes\":[\"https://identity.mozilla.com/apps/oldysnc\"]},\"scoped_keys\":{\"https://identity.mozilla.com/apps/oldsync\":{\"kty\":\"oct\",\"scope\":\"https://identity.mozilla.com/apps/oldsync\",\"k\":\"kMtwpVC0ZaYFJymPza8rXK_0CgCp3KMwRStwGfBRBDtL6hXRDVJgQFaoOQ2dimw0Bko5WVv2gNTy7RX5zFYZHg\",\"kid\":\"1542236016429-Ox1FbJfFfwTe5t-xq4v2hQ\"}},\"login_state\":{\"Unknown\":null},\"a_new_field\":42}";
        let state = state_from_json(state_v2_json).unwrap();
        let refresh_token = state.refresh_token.unwrap();
        assert_eq!(
            refresh_token.token,
            "bed5532f4fea7e39c5c4f609f53603ee7518fd1c103cc4034da3618f786ed188"
        );
    }

    #[test]
    fn test_v2_creates_an_empty_access_token_cache_if_its_missing() {
        let state_v2_json = "{\"schema_version\":\"V2\",\"config\":{\"client_id\":\"98adfa37698f255b\",\"redirect_uri\":\"https://lockbox.firefox.com/fxa/ios-redirect.html\",\"content_url\":\"https://accounts.firefox.com\"},\"refresh_token\":{\"token\":\"bed5532f4fea7e39c5c4f609f53603ee7518fd1c103cc4034da3618f786ed188\",\"scopes\":[\"https://identity.mozilla.com/apps/oldysnc\"]},\"scoped_keys\":{\"https://identity.mozilla.com/apps/oldsync\":{\"kty\":\"oct\",\"scope\":\"https://identity.mozilla.com/apps/oldsync\",\"k\":\"kMtwpVC0ZaYFJymPza8rXK_0CgCp3KMwRStwGfBRBDtL6hXRDVJgQFaoOQ2dimw0Bko5WVv2gNTy7RX5zFYZHg\",\"kid\":\"1542236016429-Ox1FbJfFfwTe5t-xq4v2hQ\"}},\"login_state\":{\"Unknown\":null}}";
        let state = state_from_json(state_v2_json).unwrap();
        let refresh_token = state.refresh_token.unwrap();
        assert_eq!(
            refresh_token.token,
            "bed5532f4fea7e39c5c4f609f53603ee7518fd1c103cc4034da3618f786ed188"
        );
        assert_eq!(state.access_token_cache.len(), 0);
    }
}
