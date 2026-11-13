/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Internal implementation details for the fxa_client crate.

use self::{
    config::Config,
    oauth::{AuthCircuitBreaker, OAuthFlow, OAUTH_WEBCHANNEL_REDIRECT},
    state_manager::StateManager,
    state_persistence::PersistedState,
    telemetry::FxaTelemetry,
};
use crate::{DeviceConfig, Error, FxaConfig, FxaRustAuthState, FxaServer, FxaState, Result};
use serde_derive::*;
use std::{
    collections::{HashMap, HashSet},
    sync::Arc,
};
use url::Url;

// We once had an "integration_test" feature this module was gated on.
// We still keep the code around for now, in-case it turns out to be useful.
// pub mod auth;
mod close_tabs;
mod commands;
pub mod config;
pub mod device;
mod http_client;
mod oauth;
mod profile;
mod push;
mod scoped_keys;
mod scopes;
mod send_tab;
mod state_manager;
mod state_persistence;
mod telemetry;
mod util;

type FxAClient = dyn http_client::FxAClient + Sync + Send;

// FIXME: https://github.com/myelin-ai/mockiato/issues/106.
#[cfg(test)]
unsafe impl Send for http_client::MockFxAClient {}
#[cfg(test)]
unsafe impl Sync for http_client::MockFxAClient {}

// It this struct is modified, please check if the
// `FirefoxAccount.start_over` function also needs
// to be modified.
pub struct FirefoxAccount {
    client: Arc<FxAClient>,
    state: StateManager,
    attached_clients_cache: Option<CachedResponse<Vec<http_client::GetAttachedClientResponse>>>,
    devices_cache: Option<CachedResponse<Vec<http_client::GetDeviceResponse>>>,
    auth_circuit_breaker: AuthCircuitBreaker,
    telemetry: FxaTelemetry,
    // TODO: Cleanup our usage of the word "state" and change this field name to `state`
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1868610
    pub(crate) auth_state: FxaState,
    // Set via `FxaEvent::Initialize`
    pub(crate) device_config: Option<DeviceConfig>,
}

impl FirefoxAccount {
    fn from_state(state: PersistedState) -> Self {
        Self {
            client: Arc::new(http_client::Client::new()),
            state: StateManager::new(state),
            attached_clients_cache: None,
            devices_cache: None,
            auth_circuit_breaker: Default::default(),
            telemetry: FxaTelemetry::new(),
            auth_state: FxaState::Uninitialized,
            device_config: None,
        }
    }

    /// Create a new `FirefoxAccount` instance using a `Config`.
    pub fn with_config(config: Config) -> Self {
        Self::from_state(PersistedState {
            config,
            refresh_token: None,
            scoped_keys: HashMap::new(),
            last_handled_command: None,
            commands_data: HashMap::new(),
            device_capabilities: HashSet::new(),
            server_local_device_info: None,
            session_token: None,
            current_device_id: None,
            last_seen_profile: None,
            access_token_cache: HashMap::new(),
            logged_out_from_auth_issues: false,
            last_auth_check_time: None,
        })
    }

    /// Create a new `FirefoxAccount` instance.
    pub fn new(config: FxaConfig) -> Self {
        Self::with_config(config.into())
    }

    #[cfg(test)]
    pub(crate) fn set_client(&mut self, client: Arc<FxAClient>) {
        self.client = client;
    }

    /// Restore a `FirefoxAccount` instance from a serialized state
    /// created using `to_json`.
    pub fn from_json(data: &str) -> Result<Self> {
        let state = state_persistence::state_from_json(data)?;
        Ok(Self::from_state(state))
    }

    /// Serialize a `FirefoxAccount` instance internal state
    /// to be restored later using `from_json`.
    pub fn to_json(&self) -> Result<String> {
        self.state.serialize_persisted_state()
    }

    /// Clear the attached clients and devices cache
    pub fn clear_devices_and_attached_clients_cache(&mut self) {
        self.attached_clients_cache = None;
        self.devices_cache = None;
    }

    /// Check if this account was created from a config
    pub fn matches_server(&self, server: &FxaServer) -> Result<bool> {
        // This check depends on the fact that we get the `content_url` when constructing our
        // internal config and never change it.  It's slightly brittle, but that's okay since it's
        // only used by the CLI.
        let content_url = self.state.config().content_url()?.to_string();
        let server_url = server.content_url();
        // Ignore trailing slashes when comparing the URLs.
        Ok(content_url.strip_suffix("/").unwrap_or(&content_url)
            == server_url.strip_suffix("/").unwrap_or(server_url))
    }

    /// Get the Sync Token Server endpoint URL.
    pub fn get_token_server_endpoint_url(&self) -> Result<String> {
        Ok(self.state.config().token_server_endpoint_url()?.into())
    }

    /// Get the pairing URL to navigate to on the Auth side (typically
    /// a computer).
    pub fn get_pairing_authority_url(&self) -> Result<String> {
        // Special case for the production server, we use the shorter firefox.com/pair URL.
        let content_url_release = Url::parse(config::CONTENT_URL_RELEASE)
            .expect("hardcoded CONTENT_URL_RELEASE failed to parse");
        if self.state.config().content_url()? == content_url_release {
            return Ok("https://firefox.com/pair".to_owned());
        }
        Ok(self.state.config().pair_url()?.into())
    }

    /// Get the "connection succeeded" page URL.
    /// It is typically used to redirect the user after
    /// having intercepted the OAuth login-flow state/code
    /// redirection.
    pub fn get_connection_success_url(&self) -> Result<String> {
        let mut url = self.state.config().connect_another_device_url()?;
        url.query_pairs_mut()
            .append_pair("showSuccessMessage", "true");
        Ok(url.into())
    }

    /// Get the "manage account" page URL.
    /// It is typically used in the application's account status UI,
    /// to link the user out to a webpage where they can manage
    /// all the details of their account.
    ///
    /// * `entrypoint` - Application-provided string identifying the UI touchpoint
    ///   through which the page was accessed, for metrics purposes.
    pub fn get_manage_account_url(&mut self, entrypoint: &str) -> Result<String> {
        let mut url = self.state.config().settings_url()?;
        url.query_pairs_mut().append_pair("entrypoint", entrypoint);
        if self.state.config().redirect_uri == OAUTH_WEBCHANNEL_REDIRECT {
            url.query_pairs_mut()
                .append_pair("context", "oauth_webchannel_v1");
        }
        self.add_account_identifiers_to_url(url)
    }

    /// Get the "manage devices" page URL.
    /// It is typically used in the application's account status UI,
    /// to link the user out to a webpage where they can manage
    /// the devices connected to their account.
    ///
    /// * `entrypoint` - Application-provided string identifying the UI touchpoint
    ///   through which the page was accessed, for metrics purposes.
    pub fn get_manage_devices_url(&mut self, entrypoint: &str) -> Result<String> {
        let mut url = self.state.config().settings_clients_url()?;
        url.query_pairs_mut().append_pair("entrypoint", entrypoint);
        self.add_account_identifiers_to_url(url)
    }

    fn add_account_identifiers_to_url(&mut self, mut url: Url) -> Result<String> {
        let profile = self.get_profile(false)?;
        url.query_pairs_mut()
            .append_pair("uid", &profile.uid)
            .append_pair("email", &profile.email);
        Ok(url.into())
    }

    fn get_refresh_token(&self) -> Result<&str> {
        match self.state.refresh_token() {
            Some(token_info) => Ok(&token_info.token),
            None => Err(Error::NoRefreshToken),
        }
    }

    pub fn get_auth_state(&self) -> FxaRustAuthState {
        self.state.get_auth_state()
    }

    /// Disconnect from the account and optionally destroy our device record. This will
    /// leave the account object in a state where it can eventually reconnect to the same user.
    /// This is a "best effort" infallible method: e.g. if the network is unreachable,
    /// the device could still be in the FxA devices manager.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn disconnect(&mut self) {
        let current_device_result;
        {
            current_device_result = self.get_current_device();
        }

        if let Some(refresh_token) = self.state.refresh_token() {
            // Delete the current device (which deletes the refresh token), or
            // the refresh token directly if we don't have a device.
            let destroy_result = match current_device_result {
                // If we get an error trying to fetch our device record we'll at least
                // still try to delete the refresh token itself.
                Ok(Some(device)) => self.client.destroy_device_record(
                    self.state.config(),
                    &refresh_token.token,
                    &device.id,
                ),
                _ => self
                    .client
                    .destroy_refresh_token(self.state.config(), &refresh_token.token),
            };
            if let Err(e) = destroy_result {
                crate::warn!("Error while destroying the device: {}", e);
            }
        }
        self.state.disconnect();
        self.clear_devices_and_attached_clients_cache();
        self.telemetry = FxaTelemetry::new();
    }

    /// Update the state based on authentication issues.
    ///
    /// **💾 This method alters the persisted account state.**
    ///
    /// Call this if you know there's an authentication / authorization issue that requires the
    /// user to re-authenticated.  It transitions the user to the [FxaRustAuthState.AuthIssues] state.
    pub fn on_auth_issues(&mut self) {
        self.state.on_auth_issues();
        self.clear_devices_and_attached_clients_cache();
        self.telemetry = FxaTelemetry::new();
    }

    pub fn on_recovered_from_auth_issues(&mut self) {
        self.state.on_recovered_from_auth_issues();
    }

    pub fn simulate_network_error(&mut self) {
        self.client.simulate_network_error();
    }

    pub fn simulate_temporary_auth_token_issue(&mut self) {
        self.state.simulate_temporary_auth_token_issue()
    }

    pub fn simulate_permanent_auth_token_issue(&mut self) {
        self.state.simulate_permanent_auth_token_issue()
    }

    /// Checks if enough time has passed since the last auth attempt that we should try checking the
    /// auth again.
    pub fn should_recheck_auth(&self) -> bool {
        self.state.should_recheck_auth()
    }

    /// Set the last time we re-checked our authentication after a failure to the current time.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn reset_auth_recheck_timer(&mut self) {
        self.state.reset_auth_recheck_timer();
    }
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub(crate) struct CachedResponse<T> {
    response: T,
    cached_at: u64,
    etag: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::internal::device::*;
    use crate::internal::http_client::MockFxAClient;
    use crate::internal::oauth::*;
    use mockall::predicate::always;
    use mockall::predicate::eq;

    #[test]
    fn test_fxa_is_send() {
        fn is_send<T: Send>() {}
        is_send::<FirefoxAccount>();
    }

    #[test]
    fn test_serialize_deserialize() {
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let fxa1 = FirefoxAccount::with_config(config);
        let fxa1_json = fxa1.to_json().unwrap();
        drop(fxa1);
        let fxa2 = FirefoxAccount::from_json(&fxa1_json).unwrap();
        let fxa2_json = fxa2.to_json().unwrap();
        assert_eq!(fxa1_json, fxa2_json);
    }

    #[test]
    fn test_get_connection_success_url() {
        let config = Config::new("https://stable.dev.lcip.org", "12345678", "https://foo.bar");
        let fxa = FirefoxAccount::with_config(config);
        let url = fxa.get_connection_success_url().unwrap();
        assert_eq!(
            url,
            "https://stable.dev.lcip.org/connect_another_device?showSuccessMessage=true"
                .to_string()
        );
    }

    #[test]
    fn test_get_manage_account_url() {
        let config = Config::new("https://stable.dev.lcip.org", "12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        // No current user -> Error.
        match fxa.get_manage_account_url("test").unwrap_err() {
            Error::NoSessionToken => {}
            _ => panic!("error not NoSessionToken"),
        };
        // With current user -> expected Url.
        fxa.add_cached_profile("123", "test@example.com");
        let url = fxa.get_manage_account_url("test").unwrap();
        assert_eq!(
            url,
            "https://stable.dev.lcip.org/settings?entrypoint=test&uid=123&email=test%40example.com"
                .to_string()
        );
    }

    #[test]
    fn test_get_manage_account_url_with_webchannel_redirect() {
        let config = Config::new(
            "https://stable.dev.lcip.org",
            "12345678",
            OAUTH_WEBCHANNEL_REDIRECT,
        );
        let mut fxa = FirefoxAccount::with_config(config);
        fxa.add_cached_profile("123", "test@example.com");
        let url = fxa.get_manage_account_url("test").unwrap();
        assert_eq!(
            url,
            "https://stable.dev.lcip.org/settings?entrypoint=test&context=oauth_webchannel_v1&uid=123&email=test%40example.com"
                .to_string()
        );
    }

    #[test]
    fn test_get_manage_devices_url() {
        let config = Config::new("https://stable.dev.lcip.org", "12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        // No current user -> Error.
        match fxa.get_manage_devices_url("test").unwrap_err() {
            Error::NoSessionToken => {}
            _ => panic!("error not NoSessionToken"),
        };
        // With current user -> expected Url.
        fxa.add_cached_profile("123", "test@example.com");
        let url = fxa.get_manage_devices_url("test").unwrap();
        assert_eq!(
            url,
            "https://stable.dev.lcip.org/settings/clients?entrypoint=test&uid=123&email=test%40example.com"
                .to_string()
        );
    }

    #[test]
    fn test_disconnect_no_refresh_token() {
        let config = Config::new("https://stable.dev.lcip.org", "12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fxa.add_cached_token(
            "profile",
            AccessTokenInfo {
                scope: "profile".to_string(),
                token: "profiletok".to_string(),
                key: None,
                expires_at: u64::MAX,
            },
        );

        let client = MockFxAClient::new();
        fxa.set_client(Arc::new(client));

        assert!(!fxa.state.is_access_token_cache_empty());
        fxa.disconnect();
        assert!(fxa.state.is_access_token_cache_empty());
    }

    #[test]
    fn test_disconnect_device() {
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fxa.state.force_refresh_token(RefreshToken {
            token: "refreshtok".to_string(),
            scopes: HashSet::default(),
        });

        let mut client = MockFxAClient::new();
        client
            .expect_get_devices()
            .with(always(), eq("refreshtok"))
            .times(1)
            .returning(|_, _| {
                Ok(vec![
                    Device {
                        common: http_client::DeviceResponseCommon {
                            id: "1234a".to_owned(),
                            display_name: "My Device".to_owned(),
                            device_type: sync15::DeviceType::Mobile,
                            push_subscription: None,
                            available_commands: HashMap::default(),
                            push_endpoint_expired: false,
                        },
                        is_current_device: true,
                        location: http_client::DeviceLocation {
                            city: None,
                            country: None,
                            state: None,
                            state_code: None,
                        },
                        last_access_time: None,
                    },
                    Device {
                        common: http_client::DeviceResponseCommon {
                            id: "a4321".to_owned(),
                            display_name: "My Other Device".to_owned(),
                            device_type: sync15::DeviceType::Desktop,
                            push_subscription: None,
                            available_commands: HashMap::default(),
                            push_endpoint_expired: false,
                        },
                        is_current_device: false,
                        location: http_client::DeviceLocation {
                            city: None,
                            country: None,
                            state: None,
                            state_code: None,
                        },
                        last_access_time: None,
                    },
                ])
            });
        client
            .expect_destroy_device_record()
            .with(always(), eq("refreshtok"), eq("1234a"))
            .times(1)
            .returning(|_, _, _| Ok(()));
        fxa.set_client(Arc::new(client));

        assert!(fxa.state.refresh_token().is_some());
        fxa.disconnect();
        assert!(fxa.state.refresh_token().is_none());
    }

    #[test]
    fn test_disconnect_no_device() {
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fxa.state.force_refresh_token(RefreshToken {
            token: "refreshtok".to_string(),
            scopes: HashSet::default(),
        });

        let mut client = MockFxAClient::new();
        client
            .expect_get_devices()
            .with(always(), eq("refreshtok"))
            .times(1)
            .returning(|_, _| {
                Ok(vec![Device {
                    common: http_client::DeviceResponseCommon {
                        id: "a4321".to_owned(),
                        display_name: "My Other Device".to_owned(),
                        device_type: sync15::DeviceType::Desktop,
                        push_subscription: None,
                        available_commands: HashMap::default(),
                        push_endpoint_expired: false,
                    },
                    is_current_device: false,
                    location: http_client::DeviceLocation {
                        city: None,
                        country: None,
                        state: None,
                        state_code: None,
                    },
                    last_access_time: None,
                }])
            });
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("refreshtok"))
            .times(1)
            .returning(|_, _| Ok(()));
        fxa.set_client(Arc::new(client));

        assert!(fxa.state.refresh_token().is_some());
        fxa.disconnect();
        assert!(fxa.state.refresh_token().is_none());
    }

    #[test]
    fn test_disconnect_network_errors() {
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fxa.state.force_refresh_token(RefreshToken {
            token: "refreshtok".to_string(),
            scopes: HashSet::default(),
        });

        let mut client = MockFxAClient::new();
        client
            .expect_get_devices()
            .with(always(), eq("refreshtok"))
            .times(1)
            .returning(|_, _| Ok(vec![]));
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("refreshtok"))
            .times(1)
            .returning(|_, _| {
                Err(Error::RemoteError {
                    code: 500,
                    errno: 101,
                    error: "Did not work!".to_owned(),
                    message: "Did not work!".to_owned(),
                    info: "Did not work!".to_owned(),
                })
            });
        fxa.set_client(Arc::new(client));

        assert!(fxa.state.refresh_token().is_some());
        fxa.disconnect();
        assert!(fxa.state.refresh_token().is_none());
    }

    #[test]
    fn test_on_auth_issues() {
        let config = Config::new("https://stable.dev.lcip.org", "12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fxa.state.force_refresh_token(RefreshToken {
            token: "refresh_token".to_owned(),
            scopes: HashSet::new(),
        });
        fxa.state.force_current_device_id("original-device-id");
        assert_eq!(fxa.get_auth_state(), FxaRustAuthState::Connected);

        fxa.on_auth_issues();
        assert_eq!(fxa.get_auth_state(), FxaRustAuthState::AuthIssues);

        fxa.state.complete_oauth_flow(
            vec![],
            RefreshToken {
                token: "refreshtok".to_owned(),
                scopes: HashSet::default(),
            },
            None,
        );
        assert_eq!(fxa.get_auth_state(), FxaRustAuthState::Connected);

        // The device ID should be the same as before `on_auth_issues` was called.  This
        // way, methods like `ensure_capabilities` and `set_device_name`, can re-use it and we
        // won't try to create a new device record.
        assert_eq!(fxa.state.current_device_id(), Some("original-device-id"));
    }

    #[test]
    fn test_get_auth_state() {
        let config = Config::new("https://stable.dev.lcip.org", "12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fn assert_auth_state(fxa: &FirefoxAccount, correct_state: FxaRustAuthState) {
            assert_eq!(fxa.get_auth_state(), correct_state);

            let persisted = FirefoxAccount::from_json(&fxa.to_json().unwrap()).unwrap();
            assert_eq!(persisted.get_auth_state(), correct_state);
        }

        // The state starts as disconnected
        assert_auth_state(&fxa, FxaRustAuthState::Disconnected);

        // When we get the refresh tokens the state changes to connected
        fxa.state.force_refresh_token(RefreshToken {
            token: "refresh_token".to_owned(),
            scopes: HashSet::new(),
        });
        assert_auth_state(&fxa, FxaRustAuthState::Connected);

        fxa.disconnect();
        assert_auth_state(&fxa, FxaRustAuthState::Disconnected);

        fxa.disconnect();
        assert_auth_state(&fxa, FxaRustAuthState::Disconnected);
    }

    #[test]
    fn test_get_pairing_authority_url() {
        let config = Config::new("https://foo.bar", "12345678", "https://foo.bar");
        let fxa = FirefoxAccount::with_config(config);
        assert_eq!(
            fxa.get_pairing_authority_url().unwrap().as_str(),
            "https://foo.bar/pair"
        );

        let config = Config::release("12345678", "https://foo.bar");
        let fxa = FirefoxAccount::with_config(config);
        assert_eq!(
            fxa.get_pairing_authority_url().unwrap().as_str(),
            "https://firefox.com/pair"
        );
    }
}
