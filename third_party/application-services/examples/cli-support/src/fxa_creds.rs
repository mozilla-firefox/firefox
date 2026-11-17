/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Utilities for command-line utilities which want to use fxa credentials.
use std::{collections::HashMap, fs, io::Write};

use anyhow::Result;
use url::Url;

use fxa_client::{DeviceConfig, DeviceType, FirefoxAccount, FxaConfig, FxaEvent, FxaState};
use sync15::{client::Sync15StorageClientInit, KeyBundle};

use crate::{prompt::prompt_string, workspace_root_dir};

// Defaults for the release FxA server
const CLIENT_ID: &str = "3c49430b43dfba77";
const REDIRECT_URI: &str = "https://accounts.firefox.com/oauth/success/3c49430b43dfba77";

pub const CREDENTIALS_FILENAME: &str = ".fxa-cli-credentials.json";
pub const PROFILE_SCOPE: &str = "profile";
pub const SYNC_SCOPE: &str = "https://identity.mozilla.com/apps/oldsync";
pub const SESSION_SCOPE: &str = "https://identity.mozilla.com/tokens/session";
pub const RELAY_SCOPE: &str = "https://identity.mozilla.com/apps/relay";
pub const VPN_SCOPE: &str = "https://identity.mozilla.com/apps/vpn";
pub const MONITOR_SCOPE: &str = "https://identity.mozilla.com/apps/monitor";
pub const SUBSCRIPTIONS_SCOPE: &str = "https://identity.mozilla.com/account/subscriptions";

pub const WELL_KNOWN_SCOPES: [&str; 7] = [
    PROFILE_SCOPE,
    SYNC_SCOPE,
    SESSION_SCOPE,
    RELAY_SCOPE,
    VPN_SCOPE,
    MONITOR_SCOPE,
    SUBSCRIPTIONS_SCOPE,
];

const DEVICE_NAME: &str = "app-services example cli client";
const OAUTH_ENTRYPOINT: &str = "fxa_creds";

pub fn get_default_fxa_config() -> FxaConfig {
    FxaConfig::release(CLIENT_ID, REDIRECT_URI)
}

/// Everything a sync consumer needs, built on demand by [`CliFxa::sync_info`].
pub struct CliSyncInfo {
    pub client_init: Sync15StorageClientInit,
    pub key_bundle: KeyBundle,
    pub auth_info: sync_manager::SyncAuthInfo,
    pub tokenserver_url: Url,
}

/// Manages FxA credentials for CLI examples and tests.
///
/// Typical usage:
/// ```no_run
/// let mut cli = CliFxa::new(get_default_fxa_config(), None)?;
/// cli.ensure_logged_in(&[SYNC_SCOPE])?;
/// let sync = cli.sync_info()?.expect("logged in with SYNC_SCOPE");
/// ```
pub struct CliFxa {
    config: FxaConfig,
    cred_path: String,
    account: Option<FirefoxAccount>,
}

impl CliFxa {
    /// Create a `CliFxa`, loading saved credentials if they exist.
    ///
    /// `cred_path` defaults to `CREDENTIALS_FILENAME` in the workspace root.
    pub fn new(config: FxaConfig, cred_path: Option<&str>) -> Result<Self> {
        let cred_path = cred_path.map(|s| s.to_owned()).unwrap_or_else(|| {
            workspace_root_dir()
                .join(CREDENTIALS_FILENAME)
                .to_string_lossy()
                .to_string()
        });

        let account = match load_account(&cred_path, &config) {
            Ok(acct) => {
                crate::info!("Loaded saved FxA credentials from {cred_path}");
                match acct.check_authorization_status() {
                    Ok(info) if info.active => {
                        crate::info!("fxa stored credentials are active");
                        Some(acct)
                    }
                    Ok(_) => {
                        crate::warn!("fxa stored credentials are no longer active; re-login will be required");
                        None
                    }
                    Err(e) => {
                        crate::warn!("FxA: could not verify authorization status ({e}); will attempt to use stored credentials");
                        Some(acct)
                    }
                }
            }
            Err(e) => {
                crate::info!("No saved FxA credentials at {cred_path} ({e}); login required");
                None
            }
        };

        Ok(Self {
            config,
            cred_path,
            account,
        })
    }

    /// Ensure the user is logged in, running interactive OAuth if needed.
    ///
    /// Uses the state machine: `Initialize` → `BeginOAuthFlow` if needed → `CompleteOAuthFlow`.
    /// Persists credentials after a successful login.
    pub fn ensure_logged_in(&mut self, scopes: &[&str]) -> Result<&FirefoxAccount> {
        let service = ""; // TODO: expose this.
        if self.account.is_none() {
            crate::info!("Creating new FxA account object");
            self.account = Some(FirefoxAccount::new(self.config.clone()));
        }

        let device_config = DeviceConfig {
            name: DEVICE_NAME.to_owned(),
            device_type: DeviceType::Desktop,
            capabilities: vec![],
        };

        let account = self.account.as_mut().unwrap();

        let state = account.process_event(FxaEvent::Initialize { device_config })?;

        match state {
            FxaState::Connected => {
                let have_all_scopes = account.has_scope(&scopes.join(" "));
                crate::info!("FxA: already connected, all scopes is {have_all_scopes}");
                if !have_all_scopes {
                    self.handle_oauth_flow(service, scopes)?;
                }
            }
            FxaState::Disconnected | FxaState::AuthIssues => {
                crate::info!("FxA: need to authenticate (state was {state:?})");
                self.handle_oauth_flow(service, scopes)?;
            }
            other => {
                anyhow::bail!("Unexpected FxA state after Initialize: {other:?}");
            }
        }

        Ok(self.account.as_ref().unwrap())
    }

    /// Get the account reference if logged in.
    pub fn account(&self) -> Option<&FirefoxAccount> {
        self.account.as_ref()
    }

    /// Get everything needed to sync.
    ///
    /// Returns `Ok(None)` if the sync scope key is not available (e.g., not logged in with
    /// `SYNC_SCOPE`). Returns `Err` for real errors.
    pub fn sync_info(&self) -> Result<Option<CliSyncInfo>> {
        let account = self
            .account
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("not logged in; call ensure_logged_in first"))?;

        let token = account.get_access_token(SYNC_SCOPE, true)?;
        let key = match token.key {
            Some(k) => k,
            None => {
                crate::info!("No sync key in token — not logged in with SYNC_SCOPE");
                return Ok(None);
            }
        };

        let tokenserver_url = Url::parse(&account.get_token_server_endpoint_url()?)?;
        let client_init = Sync15StorageClientInit {
            key_id: key.kid.clone(),
            access_token: token.token.clone(),
            tokenserver_url: tokenserver_url.clone(),
        };
        let key_bundle = KeyBundle::from_ksync_bytes(&key.key_bytes()?)?;
        let auth_info = sync_manager::SyncAuthInfo {
            kid: key.kid.clone(),
            sync_key: key.k.clone(),
            fxa_access_token: token.token.clone(),
            tokenserver_url: tokenserver_url.to_string(),
        };

        Ok(Some(CliSyncInfo {
            client_init,
            key_bundle,
            auth_info,
            tokenserver_url,
        }))
    }

    /// Persist account state to disk.
    ///
    /// Called automatically after login. Call this explicitly after any direct
    /// account operation (e.g. `set_device_name`, `poll_device_commands`) that
    /// may update local state.
    pub fn persist(&self) -> Result<()> {
        if let Some(account) = &self.account {
            let json = account.to_json()?;
            let mut file = fs::File::create(&self.cred_path)?;
            write!(file, "{json}")?;
            file.flush()?;
            crate::info!("Saved FxA credentials to {}", self.cred_path);
        }
        Ok(())
    }

    /// Run the interactive browser OAuth flow and complete the login.
    fn handle_oauth_flow(&mut self, service: &str, scopes: &[&str]) -> Result<()> {
        let scopes: Vec<String> = scopes.iter().map(|s| s.to_string()).collect();

        let state = self
            .account
            .as_mut()
            .unwrap()
            .process_event(FxaEvent::BeginOAuthFlow {
                service: service.to_owned(),
                scopes,
                entrypoint: OAUTH_ENTRYPOINT.to_owned(),
            })?;

        let oauth_url = match state {
            FxaState::Authenticating { oauth_url, .. } => oauth_url,
            other => anyhow::bail!("Unexpected FxA state after BeginOAuthFlow: {other:?}"),
        };

        println!("In a (probably private) browser window, please open:");
        println!(" {oauth_url}\n");
        println!("paste the final 'Connected' URL from a successful flow below.");
        let final_url = Url::parse(&prompt_string("Final URL").unwrap_or_default())?;
        let query_params: HashMap<String, String> = final_url.query_pairs().into_owned().collect();

        let state = self
            .account
            .as_mut()
            .unwrap()
            .process_event(FxaEvent::CompleteOAuthFlow {
                code: query_params["code"].clone(),
                state: query_params["state"].clone(),
            })?;

        match state {
            FxaState::Connected => {
                crate::info!("FxA: OAuth flow complete, now connected");
                self.persist()
            }
            FxaState::Disconnected => {
                crate::warn!("FxA: OAuth flow failed to complete, account is not connected");
                self.persist()
            }
            other => anyhow::bail!("Unexpected FxA state after CompleteOAuthFlow: {other:?}"),
        }
    }
}

fn load_account(path: &str, config: &FxaConfig) -> Result<FirefoxAccount> {
    let s = fs::read_to_string(path)?;
    let account = FirefoxAccount::from_json(&s)?;
    if !account.matches_server(&config.server)? {
        anyhow::bail!(
            "Stored credentials don't match configured server.\n\
             Delete {path} to start over or use a different server arg"
        )
    }
    Ok(account)
}
