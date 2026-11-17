/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod access_token;
pub mod attached_clients;
use super::scopes;
use super::{
    http_client::{
        AuthorizationRequestParameters, IntrospectResponse as IntrospectInfo, OAuthTokenResponse,
    },
    scoped_keys::ScopedKeysFlow,
    util, FirefoxAccount,
};
use crate::{debug, info, warn, AuthorizationParameters, Error, FxaServer, Result};
pub use access_token::AccessTokenInfo;
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use jwcrypto::{EncryptionAlgorithm, EncryptionParameters};
use rate_limiter::RateLimiter;
use rc_crypto::digest;
use serde_derive::*;
use std::collections::{HashMap, HashSet};
use url::Url;
// Special redirect urn based on the OAuth native spec, signals that the
// WebChannel flow is used
pub const OAUTH_WEBCHANNEL_REDIRECT: &str = "urn:ietf:wg:oauth:2.0:oob:oauth-redirect-webchannel";

impl FirefoxAccount {
    /// Check whether every requested scope has been granted to the account's refresh token.
    pub fn has_scope(&self, scope: &str) -> bool {
        let mut requested = scope.split_ascii_whitespace().peekable();
        if requested.peek().is_none() {
            return false;
        }
        match self.state.refresh_token() {
            Some(refresh_token) => requested.all(|s| refresh_token.scopes.contains(s)),
            None => false,
        }
    }

    /// Extracts and stores the session token from a WebChannel login JSON payload.
    /// The JSON payload is the `data` object from the `fxaccounts:login` WebChannel command.
    pub fn handle_web_channel_login(&mut self, json_payload: &str) -> Result<()> {
        let data: serde_json::Value = serde_json::from_str(json_payload)?;
        let token = data
            .get("sessionToken")
            .and_then(|v| v.as_str())
            .ok_or(Error::NoSessionToken)?;
        self.state.set_session_token(token.to_string());
        Ok(())
    }

    /// Extracts the session token from a WebChannel password change JSON payload, exchanges it
    /// for a new refresh token.
    pub fn handle_web_channel_password_change(&mut self, json_payload: &str) -> Result<()> {
        let data: serde_json::Value = serde_json::from_str(json_payload)?;
        let token = data
            .get("sessionToken")
            .and_then(|v| v.as_str())
            .ok_or(Error::NoSessionToken)?;
        // Grab the current device before the token swap since destroying the old refresh token
        // tears down its associated device record server-side.
        let old_device_info = match self.get_current_device() {
            Ok(maybe_device) => maybe_device,
            Err(err) => {
                warn!(
                    "Error fetching current device before password change: {:?}",
                    err
                );
                None
            }
        };
        self.handle_session_token_change(token)?;
        if let Some(ref device_info) = old_device_info {
            if let Err(err) = self.replace_device(
                &device_info.display_name,
                &device_info.device_type,
                &device_info.push_subscription,
                &device_info.available_commands,
            ) {
                warn!(
                    "Device information restoration failed after password change: {:?}",
                    err
                );
            } else {
                info!("Restored device information with new refresh token");
            }
        }
        Ok(())
    }

    /// Retrieve the current session token from state
    pub fn get_session_token(&self) -> Result<String> {
        match self.state.session_token() {
            Some(session_token) => Ok(session_token.to_string()),
            None => Err(Error::NoSessionToken),
        }
    }

    /// Builds a complete `signedInUser` JSON object for a WebChannel `fxaccounts:fxa_status`
    /// response. Returns `None` if no session token is stored.
    /// `email` and `uid` are read from the cached profile; `verified` is always true because
    /// the account state machine only completes authentication for verified accounts.
    pub fn get_signed_in_user_for_web_channel(&self) -> Option<String> {
        let token = self.state.session_token()?;
        let profile = self.state.last_seen_profile();
        let email = profile.map(|p| p.response.email.as_str());
        let uid = profile.map(|p| p.response.uid.as_str());
        Some(
            serde_json::json!({
                "sessionToken": token,
                "email": email,
                "uid": uid,
                "verified": true,
            })
            .to_string(),
        )
    }

    /// Check whether user is authorized using our refresh token.
    pub fn check_authorization_status(&mut self) -> Result<IntrospectInfo> {
        let resp = match self.state.refresh_token() {
            Some(refresh_token) => {
                self.auth_circuit_breaker.check()?;
                self.client
                    .check_refresh_token_status(self.state.config(), &refresh_token.token)?
            }
            None => return Err(Error::NoRefreshToken),
        };
        Ok(IntrospectInfo {
            active: resp.active,
        })
    }

    /// Initiate a pairing flow and return a URL that should be navigated to.
    ///
    /// * `pairing_url` - A pairing URL obtained by scanning a QR code produced by
    ///   the pairing authority.
    /// * `scopes` - Space-separated list of requested scopes by the pairing supplicant.
    /// * `entrypoint` - The entrypoint to be used for data collection
    /// * `metrics` - Optional parameters for metrics
    pub fn begin_pairing_flow(
        &mut self,
        pairing_url: &str,
        service: &str,
        scopes: &[&str],
        entrypoint: &str,
    ) -> Result<String> {
        let mut url = self.state.config().pair_supp_url()?;
        url.query_pairs_mut().append_pair("entrypoint", entrypoint);
        if !service.is_empty() {
            url.query_pairs_mut().append_pair("service", service);
        }
        let pairing_url = util::parse_url(pairing_url, "begin_pairing_flow")?;
        if url.host_str() != pairing_url.host_str() {
            let fxa_server = FxaServer::from(&url);
            let pairing_fxa_server = FxaServer::from(&pairing_url);
            return Err(Error::OriginMismatch(format!(
                "fxa-server: {fxa_server}, pairing-url-fxa-server: {pairing_fxa_server}"
            )));
        }
        url.set_fragment(pairing_url.fragment());
        self.oauth_flow(url, scopes)
    }

    /// Initiate an OAuth login flow and return a URL that should be navigated to.
    ///
    /// * `scopes` - Space-separated list of requested scopes.
    /// * `entrypoint` - The entrypoint to be used for metrics
    /// * `metrics` - Optional metrics parameters
    ///
    /// Note that you can use this to either perform an initial signin, or use this on
    /// an already signed in account to get more scopes for that account.
    /// When obtaining more scopes, only the new scopes needed should be requested
    /// rather than the union of all scopes - this is because asking for a scope with
    /// keys (eg, sync) would force the UI to go through a different UI flow - eg, always
    /// asking for your password, even though the new scopes requested doesn't actually
    /// require that. This code therefore knows how to merge the scopes at the end of the
    /// flow, so the end result remains a new refresh token with the union of scopes.
    pub fn begin_oauth_flow(
        &mut self,
        service: &str,
        scopes: &[&str],
        entrypoint: &str,
    ) -> Result<String> {
        let needs_reauth =
            self.state.last_seen_profile().is_some() && self.state.session_token().is_none();
        let mut url = if needs_reauth {
            // must be in a needs-reauth or other odd state. Not clear this is strictly needed.
            // further, this is still somewhat wrong in a "needs reauth" state - there we will be
            // looking to get back all scopes we previously had - and it's not really expected the client
            // knows that. We probably need to stash the old scopes when we enter the needsreauth
            // state. But that's a todo.
            self.state.config().oauth_force_auth_url()?
        } else {
            self.state.config().authorization_endpoint()?
        };

        info!("starting oauth flow via {url} for service={service:?}, scopes={scopes:?}, entrypoint={entrypoint:?}");
        url.query_pairs_mut()
            .append_pair("action", "email")
            .append_pair("response_type", "code")
            .append_pair("entrypoint", entrypoint);

        if !service.is_empty() {
            url.query_pairs_mut().append_pair("service", service);
        }
        if let Some(cached_profile) = self.state.last_seen_profile() {
            url.query_pairs_mut()
                .append_pair("email", &cached_profile.response.email);
        }

        debug!("oauth flow final set of requested scopes now {scopes:?}");
        self.oauth_flow(url, scopes)
    }

    /// Fetch an OAuth code for a particular client using a session token from the account state.
    ///
    /// * `auth_params` Authorization parameters  which includes:
    ///     *  `client_id` - OAuth client id.
    ///     *  `scope` - list of requested scopes.
    ///     *  `state` - OAuth state.
    ///     *  `access_type` - Type of OAuth access, can be "offline" and "online"
    ///     *  `pkce_params` - Optional PKCE parameters for public clients (`code_challenge` and `code_challenge_method`)
    ///     *  `keys_jwk` - Optional JWK used to encrypt scoped keys
    pub fn authorize_code_using_session_token(
        &self,
        auth_params: AuthorizationParameters,
    ) -> Result<String> {
        let session_token = self.get_session_token()?;

        // Validate request to ensure that the client is actually allowed to request
        // the scopes they requested
        let allowed_scopes = self.client.get_scoped_key_data(
            self.state.config(),
            &session_token,
            &auth_params.client_id,
            &auth_params.scope.join(" "),
        )?;

        if let Some(not_allowed_scope) = auth_params
            .scope
            .iter()
            .find(|scope| !allowed_scopes.contains_key(*scope))
        {
            return Err(Error::ScopeNotAllowed(
                auth_params.client_id.clone(),
                not_allowed_scope.clone(),
            ));
        }

        let keys_jwe = if let Some(keys_jwk) = auth_params.keys_jwk {
            let mut scoped_keys = HashMap::new();
            allowed_scopes
                .iter()
                .try_for_each(|(scope, _)| -> Result<()> {
                    scoped_keys.insert(
                        scope,
                        self.state
                            .get_scoped_key(scope)
                            .ok_or_else(|| Error::NoScopedKey(scope.clone()))?,
                    );
                    Ok(())
                })?;
            let scoped_keys = serde_json::to_string(&scoped_keys)?;
            let keys_jwk = URL_SAFE_NO_PAD.decode(keys_jwk)?;
            let jwk = serde_json::from_slice(&keys_jwk)?;
            Some(jwcrypto::encrypt_to_jwe(
                scoped_keys.as_bytes(),
                EncryptionParameters::ECDH_ES {
                    enc: EncryptionAlgorithm::A256GCM,
                    peer_jwk: &jwk,
                },
            )?)
        } else {
            None
        };
        let auth_request_params = AuthorizationRequestParameters {
            client_id: auth_params.client_id,
            scope: auth_params.scope.join(" "),
            state: auth_params.state,
            access_type: auth_params.access_type,
            code_challenge: auth_params.code_challenge,
            code_challenge_method: auth_params.code_challenge_method,
            keys_jwe,
        };

        let resp = self.client.create_authorization_code_using_session_token(
            self.state.config(),
            &session_token,
            auth_request_params,
        )?;

        Ok(resp.code)
    }

    fn oauth_flow(&mut self, mut url: Url, scopes: &[&str]) -> Result<String> {
        self.clear_access_token_cache();
        let state = util::random_base64_url_string(16)?;
        let code_verifier = util::random_base64_url_string(43)?;
        let code_challenge = digest::digest(&digest::SHA256, code_verifier.as_bytes())?;
        let code_challenge = URL_SAFE_NO_PAD.encode(code_challenge);
        let scoped_keys_flow = ScopedKeysFlow::with_random_key()?;
        let jwk = scoped_keys_flow.get_public_key_jwk()?;
        let jwk_json = serde_json::to_string(&jwk)?;
        let keys_jwk = URL_SAFE_NO_PAD.encode(jwk_json);
        url.query_pairs_mut()
            .append_pair("client_id", &self.state.config().client_id)
            .append_pair("state", &state)
            .append_pair("code_challenge_method", "S256")
            .append_pair("code_challenge", &code_challenge)
            .append_pair("access_type", "offline")
            .append_pair("keys_jwk", &keys_jwk);

        if !scopes.is_empty() {
            url.query_pairs_mut()
                .append_pair("scope", &scopes.join(" "));
        }
        if self.state.config().redirect_uri == OAUTH_WEBCHANNEL_REDIRECT {
            url.query_pairs_mut()
                .append_pair("context", "oauth_webchannel_v1");
        } else {
            url.query_pairs_mut()
                .append_pair("redirect_uri", &self.state.config().redirect_uri);
        }

        self.state.begin_oauth_flow(
            state,
            OAuthFlow {
                scoped_keys_flow: Some(scoped_keys_flow),
                code_verifier,
            },
        );
        Ok(url.to_string())
    }

    /// Complete an OAuth flow initiated in `begin_oauth_flow` or `begin_pairing_flow`.
    /// The `code` and `state` parameters can be obtained by parsing out the
    /// redirect URL after a successful login.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn complete_oauth_flow(&mut self, code: &str, state: &str) -> Result<()> {
        self.clear_access_token_cache();
        let oauth_flow = match self.state.pop_oauth_flow(state) {
            Some(oauth_flow) => oauth_flow,
            None => return Err(Error::UnknownOAuthState),
        };
        // This new flow is going to end up with us having a refresh token, but with only the newly
        // requested scopes. We'll then exchange that for one with the old scopes added.
        let resp = self.client.create_refresh_token_using_authorization_code(
            self.state.config(),
            self.state.session_token(),
            code,
            &oauth_flow.code_verifier,
        )?;
        info!(
            "complete oauth flow - new session token={}, new refresh token={}",
            resp.session_token.is_some(),
            resp.refresh_token.is_some()
        );
        self.handle_oauth_response(resp, oauth_flow.scoped_keys_flow)?;
        Ok(())
    }

    /// Cancel any in-progress oauth flows
    pub fn cancel_existing_oauth_flows(&mut self) {
        self.state.clear_oauth_flows();
    }

    pub(crate) fn handle_oauth_response(
        &mut self,
        resp: OAuthTokenResponse,
        scoped_keys_flow: Option<ScopedKeysFlow>,
    ) -> Result<()> {
        // These are the keys granted by *this* response - any invariants about scopes vs keys
        // must be checked after we've fully merged the scopes and keys.
        let scoped_keys = match resp.keys_jwe {
            Some(ref jwe) => {
                let scoped_keys_flow = scoped_keys_flow.ok_or(Error::ApiClientError(
                    "Got a JWE but have no JWK to decrypt it.",
                ))?;
                let decrypted_keys = scoped_keys_flow.decrypt_keys_jwe(jwe)?;
                let scoped_keys: serde_json::Map<String, serde_json::Value> =
                    serde_json::from_str(&decrypted_keys)?;
                scoped_keys
                    .into_iter()
                    .map(|(scope, key)| Ok((scope, serde_json::from_value(key)?)))
                    .collect::<Result<Vec<_>>>()?
            }
            None => vec![],
        };

        // We are only interested in the refresh token at this time because we
        // don't want to return an over-scoped access token.
        // Let's be good citizens and destroy this access token.
        if let Err(err) = self
            .client
            .destroy_access_token(self.state.config(), &resp.access_token)
        {
            warn!("Access token destruction failure: {:?}", err);
        }
        let old_refresh_token = self.state.refresh_token().cloned();
        let mut new_refresh_token = RefreshToken::new(
            resp.refresh_token
                .ok_or(Error::ApiClientError("No refresh token in response"))?,
            resp.scope,
        );
        // Destroying a refresh token also destroys its associated device,
        // grab the device information for replication later.
        let old_device_info = match old_refresh_token {
            Some(_) => match self.get_current_device() {
                Ok(maybe_device) => maybe_device,
                Err(err) => {
                    warn!("Error while getting previous device information: {:?}", err);
                    None
                }
            },
            None => None,
        };

        if let Some(ref old_refresh_token) = old_refresh_token {
            // As described in the docs for `begin_oauth_flow`, we now have a new refresh token,
            // but only with new scopes we explicitly requested.
            // We possibly had an old refresh token with only the scopes we had before.
            // In that scenario, we need to create yet another refresh token with merged scopes.
            let existing_scopes = &old_refresh_token.scopes;
            let all_scopes: HashSet<_> = existing_scopes
                .union(&new_refresh_token.scopes)
                .cloned()
                .collect();
            if all_scopes != new_refresh_token.scopes {
                if let Some(session_token) = self.state.session_token() {
                    info!("New refresh token is missing some of our old scopes, upgrading");
                    // We'd prefer to call `exchange_token_for_scope` instead of `create_refresh_token_using_session_token`,
                    // but that's not currently setup correctly for this.
                    // NOTE: when we *do* call `exchange_token_for_scope` we shouldn't need to do the device reregistration
                    // this as that's handled by the server in that scenario.
                    let scopes_slice = all_scopes.iter().map(|s| s.as_ref()).collect::<Vec<&str>>();
                    let merged_refresh_token_resp =
                        self.client.create_refresh_token_using_session_token(
                            self.state.config(),
                            session_token,
                            &scopes_slice,
                        )?;
                    let Some(merged_refresh_token_str) = merged_refresh_token_resp.refresh_token
                    else {
                        log::error!("server failed to give a new refresh token");
                        return Err(Error::NoRefreshToken);
                    };

                    // now destroy the one we got from this response.
                    if let Err(err) = self
                        .client
                        .destroy_refresh_token(self.state.config(), &new_refresh_token.token)
                    {
                        warn!(
                            "Refresh token destruction failure of new refresh token: {:?}",
                            err
                        );
                    }

                    new_refresh_token = RefreshToken::new(
                        merged_refresh_token_str,
                        merged_refresh_token_resp.scope,
                    );
                } else {
                    warn!("New refresh token is missing some of our old scopes, but don't have a session token to use to upgrade");
                }
            } else {
                // this seems odd, but I guess not bad?
                info!("New refresh token has the same scopes we started with");
            }

            // In order to keep 1 and only 1 refresh token alive per client instance,
            // we also destroy the old refresh token.
            if let Err(err) = self
                .client
                .destroy_refresh_token(self.state.config(), &old_refresh_token.token)
            {
                warn!(
                    "Refresh token destruction failure of old refresh token: {:?}",
                    err
                );
            }
            // and clear the old refresh token from our state, just in case we encounter an error before
            // we've set the new one as current.
            self.state.clear_refresh_token();
        }

        // Evaluate the sync-key invariant against the final state: the scopes the merged
        // refresh token actually carries, and every key we'll hold afterwards (keys from this
        // response plus keys we already had for other scopes).
        let sync_scope_granted = new_refresh_token.scopes.contains(scopes::OLD_SYNC);
        let have_sync_key = scoped_keys
            .iter()
            .any(|(scope, _)| scope == scopes::OLD_SYNC)
            || self.state.get_scoped_key(scopes::OLD_SYNC).is_some();
        if sync_scope_granted && !have_sync_key {
            error_support::report_error!(
                "fxaclient-scoped-key",
                "Sync scope granted, but no sync scoped key held (final scopes: {})",
                new_refresh_token
                    .scopes
                    .iter()
                    .cloned()
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }

        self.state
            .complete_oauth_flow(scoped_keys, new_refresh_token, resp.session_token);
        if let Some(ref device_info) = old_device_info {
            if let Err(err) = self.replace_device(
                &device_info.display_name,
                &device_info.device_type,
                &device_info.push_subscription,
                &device_info.available_commands,
            ) {
                warn!("Device information restoration failed: {:?}", err);
            }
            info!("restored device information with new refresh token");
        }
        Ok(())
    }

    /// Typically called during a password change flow.
    /// Invalidates all tokens and fetches a new refresh token.
    /// Because the old refresh token is not valid anymore, we can't do like `handle_oauth_response`
    /// and re-create the device, so it is the responsibility of the caller to do so after we're
    /// done.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn handle_session_token_change(&mut self, session_token: &str) -> Result<()> {
        let old_refresh_token = self.state.refresh_token().ok_or(Error::NoRefreshToken)?;
        let scopes: Vec<&str> = old_refresh_token.scopes.iter().map(AsRef::as_ref).collect();
        let resp = self.client.create_refresh_token_using_session_token(
            self.state.config(),
            session_token,
            &scopes,
        )?;
        let new_refresh_token = resp
            .refresh_token
            .ok_or(Error::ApiClientError("No refresh token in response"))?;
        self.state.update_tokens(
            session_token.to_owned(),
            RefreshToken {
                token: new_refresh_token,
                scopes: resp.scope.split(' ').map(ToString::to_string).collect(),
            },
        );
        self.clear_devices_and_attached_clients_cache();
        Ok(())
    }
}

const AUTH_CIRCUIT_BREAKER_CAPACITY: u8 = 5;
const AUTH_CIRCUIT_BREAKER_RENEWAL_RATE: f32 = 3.0 / 60.0 / 1000.0; // 3 tokens every minute.

#[derive(Clone, Copy)]
pub(crate) struct AuthCircuitBreaker {
    rate_limiter: RateLimiter,
}

impl Default for AuthCircuitBreaker {
    fn default() -> Self {
        AuthCircuitBreaker {
            rate_limiter: RateLimiter::new(
                AUTH_CIRCUIT_BREAKER_CAPACITY,
                AUTH_CIRCUIT_BREAKER_RENEWAL_RATE,
            ),
        }
    }
}

impl AuthCircuitBreaker {
    pub(crate) fn check(&mut self) -> Result<()> {
        if !self.rate_limiter.check() {
            return Err(Error::AuthCircuitBreakerError);
        }
        Ok(())
    }
}

impl TryFrom<Url> for AuthorizationParameters {
    type Error = Error;

    fn try_from(url: Url) -> Result<Self> {
        let query_map: HashMap<String, String> = url.query_pairs().into_owned().collect();
        let scope = query_map
            .get("scope")
            .cloned()
            .ok_or(Error::MissingUrlParameter("scope"))?;
        let client_id = query_map
            .get("client_id")
            .cloned()
            .ok_or(Error::MissingUrlParameter("client_id"))?;
        let state = query_map
            .get("state")
            .cloned()
            .ok_or(Error::MissingUrlParameter("state"))?;
        let access_type = query_map
            .get("access_type")
            .cloned()
            .ok_or(Error::MissingUrlParameter("access_type"))?;
        let code_challenge = query_map.get("code_challenge").cloned();
        let code_challenge_method = query_map.get("code_challenge_method").cloned();
        let keys_jwk = query_map.get("keys_jwk").cloned();
        Ok(Self {
            client_id,
            scope: scope.split_whitespace().map(|s| s.to_string()).collect(),
            state,
            access_type,
            code_challenge,
            code_challenge_method,
            keys_jwk,
        })
    }
}

#[derive(Clone, Serialize, Deserialize)]
pub struct RefreshToken {
    pub token: String,
    pub scopes: HashSet<String>,
}

impl RefreshToken {
    pub fn new(token: String, scopes: String) -> Self {
        Self {
            token,
            scopes: scopes
                .split_ascii_whitespace()
                .map(ToString::to_string)
                .collect(),
        }
    }
}

impl std::fmt::Debug for RefreshToken {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("RefreshToken")
            .field("scopes", &self.scopes)
            .finish()
    }
}

pub struct OAuthFlow {
    pub scoped_keys_flow: Option<ScopedKeysFlow>,
    pub code_verifier: String,
}

impl From<IntrospectInfo> for crate::AuthorizationInfo {
    fn from(r: IntrospectInfo) -> Self {
        crate::AuthorizationInfo { active: r.active }
    }
}

#[cfg(test)]
impl FirefoxAccount {
    pub fn set_session_token(&mut self, session_token: &str) {
        self.state.set_session_token(session_token.to_owned());
    }
}

#[cfg(test)]
mod tests {
    use super::super::{http_client::*, Config};
    use super::*;
    use mockall::predicate::always;
    use mockall::predicate::eq;
    use std::borrow::Cow;
    use std::collections::HashMap;
    use std::sync::Arc;

    #[test]
    fn test_has_scope() {
        nss_as::ensure_initialized();
        let mut fxa =
            FirefoxAccount::with_config(Config::stable_dev("12345678", "https://foo.bar"));
        // No refresh token -> false.
        assert!(!fxa.has_scope("profile"));
        fxa.state.force_refresh_token(RefreshToken {
            token: "rt".to_owned(),
            scopes: ["profile", "sync"].iter().map(|s| s.to_string()).collect(),
        });
        assert!(fxa.has_scope("profile"));
        assert!(fxa.has_scope("sync profile"));
        assert!(fxa.has_scope("profile sync ")); // trailing whitespace too
        assert!(!fxa.has_scope("sync unknown")); // one missing -> false
        assert!(!fxa.has_scope("")); // empty -> false
    }

    #[test]
    fn test_oauth_flow_url_without_scopes() {
        nss_as::ensure_initialized();
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "https://mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        );
        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa
            .begin_oauth_flow("", &[], "test_oauth_flow_url")
            .unwrap();
        let flow_url = Url::parse(&url).unwrap();
        let params: HashMap<String, String> = flow_url.query_pairs().into_owned().collect();

        assert!(!params.contains_key("scope"));
    }

    #[test]
    fn test_oauth_flow_url() {
        nss_as::ensure_initialized();
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "https://mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        );
        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa
            .begin_oauth_flow("", &["profile"], "test_oauth_flow_url")
            .unwrap();
        let flow_url = Url::parse(&url).unwrap();

        assert_eq!(flow_url.path(), "/authorization");

        assert_eq!(flow_url.query_pairs().count(), 11);
        let params: HashMap<String, String> = flow_url.query_pairs().into_owned().collect();
        assert_eq!(params["action"], "email");
        assert_eq!(params["response_type"], "code");
        assert_eq!(params["entrypoint"], "test_oauth_flow_url");
        assert_eq!(params["client_id"], "12345678");
        assert_eq!(params["scope"], "profile");
        assert_eq!(params["state"].len(), 22);
        assert_eq!(params["code_challenge_method"], "S256");
        assert_eq!(params["code_challenge"].len(), 43);
        assert_eq!(params["access_type"], "offline");
        assert_eq!(params["keys_jwk"].len(), 168);
        assert_eq!(params["redirect_uri"], "https://foo.bar");
    }

    #[test]
    fn test_force_auth_url() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        let email = "test@example.com";
        fxa.add_cached_profile("123", email);
        let url = fxa
            .begin_oauth_flow("", &["profile"], "test_force_auth_url")
            .unwrap();
        let url = Url::parse(&url).unwrap();
        assert_eq!(url.path(), "/oauth/force_auth");
        let mut pairs = url.query_pairs();
        assert_eq!(
            pairs.find(|e| e.0 == "email"),
            Some((Cow::Borrowed("email"), Cow::Borrowed(email),))
        );
    }

    #[test]
    fn test_webchannel_context_url() {
        nss_as::ensure_initialized();
        const SCOPES: &[&str] = &["https://identity.mozilla.com/apps/oldsync"];
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "https://mock-fxa.example.com",
            "12345678",
            "urn:ietf:wg:oauth:2.0:oob:oauth-redirect-webchannel",
        );
        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa
            .begin_oauth_flow("", SCOPES, "test_webchannel_context_url")
            .unwrap();
        let url = Url::parse(&url).unwrap();
        let query_params: HashMap<_, _> = url.query_pairs().into_owned().collect();
        let context = &query_params["context"];
        assert_eq!(context, "oauth_webchannel_v1");
        assert_eq!(query_params.get("redirect_uri"), None);
    }

    #[test]
    fn test_webchannel_pairing_context_url() {
        nss_as::ensure_initialized();
        const SCOPES: &[&str] = &["https://identity.mozilla.com/apps/oldsync"];
        const PAIRING_URL: &str = "https://accounts.firefox.com/pair#channel_id=658db7fe98b249a5897b884f98fb31b7&channel_key=1hIDzTj5oY2HDeSg_jA2DhcOcAn5Uqq0cAYlZRNUIo4";

        let config = Config::new(
            "https://accounts.firefox.com",
            "12345678",
            "urn:ietf:wg:oauth:2.0:oob:oauth-redirect-webchannel",
        );
        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa
            .begin_pairing_flow(
                PAIRING_URL,
                "service",
                SCOPES,
                "test_webchannel_pairing_context_url",
            )
            .unwrap();
        let url = Url::parse(&url).unwrap();
        let query_params: HashMap<_, _> = url.query_pairs().into_owned().collect();
        let context = &query_params["context"];
        assert_eq!(context, "oauth_webchannel_v1");
        assert_eq!(query_params.get("redirect_uri"), None);
    }

    #[test]
    fn test_pairing_flow_url() {
        nss_as::ensure_initialized();
        const SCOPES: &[&str] = &["https://identity.mozilla.com/apps/oldsync"];
        const PAIRING_URL: &str = "https://accounts.firefox.com/pair#channel_id=658db7fe98b249a5897b884f98fb31b7&channel_key=1hIDzTj5oY2HDeSg_jA2DhcOcAn5Uqq0cAYlZRNUIo4";
        const EXPECTED_URL: &str = "https://accounts.firefox.com/pair/supp?client_id=12345678&redirect_uri=https%3A%2F%2Ffoo.bar&scope=https%3A%2F%2Fidentity.mozilla.com%2Fapps%2Foldsync&state=SmbAA_9EA5v1R2bgIPeWWw&code_challenge_method=S256&code_challenge=ZgHLPPJ8XYbXpo7VIb7wFw0yXlTa6MUOVfGiADt0JSM&access_type=offline&keys_jwk=eyJjcnYiOiJQLTI1NiIsImt0eSI6IkVDIiwieCI6Ing5LUltQjJveDM0LTV6c1VmbW5sNEp0Ti14elV2eFZlZXJHTFRXRV9BT0kiLCJ5IjoiNXBKbTB3WGQ4YXdHcm0zREl4T1pWMl9qdl9tZEx1TWlMb1RkZ1RucWJDZyJ9#channel_id=658db7fe98b249a5897b884f98fb31b7&channel_key=1hIDzTj5oY2HDeSg_jA2DhcOcAn5Uqq0cAYlZRNUIo4";

        let config = Config::new(
            "https://accounts.firefox.com",
            "12345678",
            "https://foo.bar",
        );

        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa
            .begin_pairing_flow(PAIRING_URL, "", SCOPES, "test_pairing_flow_url")
            .unwrap();
        let flow_url = Url::parse(&url).unwrap();
        let expected_parsed_url = Url::parse(EXPECTED_URL).unwrap();

        assert_eq!(flow_url.host_str(), Some("accounts.firefox.com"));
        assert_eq!(flow_url.path(), "/pair/supp");
        assert_eq!(flow_url.fragment(), expected_parsed_url.fragment());

        assert_eq!(flow_url.query_pairs().count(), 9);
        let params: HashMap<String, String> = flow_url.query_pairs().into_owned().collect();
        assert_eq!(params["entrypoint"], "test_pairing_flow_url");
        assert_eq!(params["client_id"], "12345678");
        assert_eq!(params["scope"], "https://identity.mozilla.com/apps/oldsync");
        assert_eq!(params["state"].len(), 22);
        assert_eq!(params["code_challenge_method"], "S256");
        assert_eq!(params["code_challenge"].len(), 43);
        assert_eq!(params["access_type"], "offline");
        assert_eq!(params["keys_jwk"].len(), 168);
        assert_eq!(params["redirect_uri"], "https://foo.bar");
    }

    #[test]
    fn test_pairing_flow_origin_mismatch() {
        nss_as::ensure_initialized();
        static PAIRING_URL: &str = "https://bad.origin.com/pair#channel_id=foo&channel_key=bar";
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa.begin_pairing_flow(
            PAIRING_URL,
            "service",
            &["https://identity.mozilla.com/apps/oldsync"],
            "test_pairiong_flow_origin_mismatch",
        );

        assert!(url.is_err());

        match url {
            Ok(_) => {
                panic!("should have error");
            }
            Err(err) => match err {
                Error::OriginMismatch { .. } => {}
                _ => panic!("error not OriginMismatch"),
            },
        }
    }

    #[test]
    fn test_check_authorization_status() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        let refresh_token_scopes = std::collections::HashSet::new();
        fxa.state.force_refresh_token(RefreshToken {
            token: "refresh_token".to_owned(),
            scopes: refresh_token_scopes,
        });

        let mut client = MockFxAClient::new();
        client
            .expect_check_refresh_token_status()
            .with(always(), eq("refresh_token"))
            .times(1)
            .returning(|_, _| Ok(IntrospectResponse { active: true }));
        fxa.set_client(Arc::new(client));

        let auth_status = fxa.check_authorization_status().unwrap();
        assert!(auth_status.active);
    }

    #[test]
    fn test_check_authorization_status_circuit_breaker() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        let refresh_token_scopes = std::collections::HashSet::new();
        fxa.state.force_refresh_token(RefreshToken {
            token: "refresh_token".to_owned(),
            scopes: refresh_token_scopes,
        });

        let mut client = MockFxAClient::new();
        // This copy-pasta (equivalent to `.returns(..).times(5)`) is there
        // because `Error` is not cloneable :/
        client
            .expect_check_refresh_token_status()
            .with(always(), eq("refresh_token"))
            .returning(|_, _| Ok(IntrospectResponse { active: true }));
        client
            .expect_check_refresh_token_status()
            .with(always(), eq("refresh_token"))
            .returning(|_, _| Ok(IntrospectResponse { active: true }));
        client
            .expect_check_refresh_token_status()
            .with(always(), eq("refresh_token"))
            .returning(|_, _| Ok(IntrospectResponse { active: true }));
        client
            .expect_check_refresh_token_status()
            .with(always(), eq("refresh_token"))
            .returning(|_, _| Ok(IntrospectResponse { active: true }));
        client
            .expect_check_refresh_token_status()
            .with(always(), eq("refresh_token"))
            .returning(|_, _| Ok(IntrospectResponse { active: true }));
        //mockall expects calls to be processed in the order they are registered. So, no need for to use a method like expect_check_refresh_token_status_calls_in_order()
        fxa.set_client(Arc::new(client));

        for _ in 0..5 {
            assert!(fxa.check_authorization_status().is_ok());
        }
        match fxa.check_authorization_status() {
            Ok(_) => unreachable!("should not happen"),
            Err(err) => assert!(matches!(err, Error::AuthCircuitBreakerError)),
        }

        // A tripped circuit breaker is inconclusive and must not move us to AuthIssues.
        fxa.device_config = Some(crate::DeviceConfig {
            name: "test".to_owned(),
            device_type: crate::DeviceType::Mobile,
            capabilities: vec![],
        });
        fxa.auth_state = crate::FxaState::Connected;
        let state = fxa
            .process_event(crate::FxaEvent::CheckAuthorizationStatus)
            .unwrap();
        assert_eq!(state, crate::FxaState::Connected);
    }

    use crate::internal::scopes::{self, OLD_SYNC};

    #[test]
    fn test_auth_code_pair_valid_not_allowed_scope() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        fxa.set_session_token("session");
        let mut client = MockFxAClient::new();
        let not_allowed_scope = "https://identity.mozilla.com/apps/lockbox";
        let expected_scopes = scopes::OLD_SYNC
            .chars()
            .chain(std::iter::once(' '))
            .chain(not_allowed_scope.chars())
            .collect::<String>();
        client
            .expect_get_scoped_key_data()
            .with(always(), eq("session"), eq("12345678"), eq(expected_scopes))
            .times(1)
            .returning(|_, _, _, _| {
                Err(Error::RemoteError {
                    code: 400,
                    errno: 163,
                    error: "Invalid Scopes".to_string(),
                    message: "Not allowed to request scopes".to_string(),
                    info: "fyi, there was a server error".to_string(),
                })
            });
        fxa.set_client(Arc::new(client));
        let auth_params = AuthorizationParameters {
            client_id: "12345678".to_string(),
            scope: vec![scopes::OLD_SYNC.to_string(), not_allowed_scope.to_string()],
            state: "somestate".to_string(),
            access_type: "offline".to_string(),
            code_challenge: None,
            code_challenge_method: None,
            keys_jwk: None,
        };
        let res = fxa.authorize_code_using_session_token(auth_params);
        assert!(res.is_err());
        let err = res.unwrap_err();
        if let Error::RemoteError {
            code,
            errno,
            error: _,
            message: _,
            info: _,
        } = err
        {
            assert_eq!(code, 400);
            assert_eq!(errno, 163); // Requested scopes not allowed
        } else {
            panic!("Should return an error from the server specifying that the requested scopes are not allowed");
        }
    }

    #[test]
    fn test_auth_code_pair_invalid_scope_not_allowed() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        fxa.set_session_token("session");
        let mut client = MockFxAClient::new();
        let invalid_scope = "IamAnInvalidScope";
        let expected_scopes = scopes::OLD_SYNC
            .chars()
            .chain(std::iter::once(' '))
            .chain(invalid_scope.chars())
            .collect::<String>();
        client
            .expect_get_scoped_key_data()
            .with(always(), eq("session"), eq("12345678"), eq(expected_scopes))
            .times(1)
            .returning(|_, _, _, _| {
                let mut server_ret = HashMap::new();
                server_ret.insert(
                    scopes::OLD_SYNC.to_string(),
                    ScopedKeyDataResponse {
                        key_rotation_secret: "IamASecret".to_string(),
                        key_rotation_timestamp: 100,
                        identifier: "".to_string(),
                    },
                );
                Ok(server_ret)
            });
        fxa.set_client(Arc::new(client));

        let auth_params = AuthorizationParameters {
            client_id: "12345678".to_string(),
            scope: vec![scopes::OLD_SYNC.to_string(), invalid_scope.to_string()],
            state: "somestate".to_string(),
            access_type: "offline".to_string(),
            code_challenge: None,
            code_challenge_method: None,
            keys_jwk: None,
        };
        let res = fxa.authorize_code_using_session_token(auth_params);
        assert!(res.is_err());
        let err = res.unwrap_err();
        if let Error::ScopeNotAllowed(client_id, scope) = err {
            assert_eq!(client_id, "12345678");
            assert_eq!(scope, "IamAnInvalidScope");
        } else {
            panic!("Should return an error that specifies the scope that is not allowed");
        }
    }

    #[test]
    fn test_auth_code_pair_scope_not_in_state() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        fxa.set_session_token("session");
        let mut client = MockFxAClient::new();
        client
            .expect_get_scoped_key_data()
            .with(
                always(),
                eq("session"),
                eq("12345678"),
                eq(scopes::OLD_SYNC),
            )
            .times(1)
            .returning(|_, _, _, _| {
                let mut server_ret = HashMap::new();
                server_ret.insert(
                    scopes::OLD_SYNC.to_string(),
                    ScopedKeyDataResponse {
                        key_rotation_secret: "IamASecret".to_string(),
                        key_rotation_timestamp: 100,
                        identifier: "".to_string(),
                    },
                );
                Ok(server_ret)
            });
        fxa.set_client(Arc::new(client));
        let auth_params = AuthorizationParameters {
            client_id: "12345678".to_string(),
            scope: vec![scopes::OLD_SYNC.to_string()],
            state: "somestate".to_string(),
            access_type: "offline".to_string(),
            code_challenge: None,
            code_challenge_method: None,
            keys_jwk: Some("IAmAVerySecretKeysJWkInBase64".to_string()),
        };
        let res = fxa.authorize_code_using_session_token(auth_params);
        assert!(res.is_err());
        let err = res.unwrap_err();
        if let Error::NoScopedKey(scope) = err {
            assert_eq!(scope, scopes::OLD_SYNC.to_string());
        } else {
            panic!("Should return an error that specifies the scope that is not in the state");
        }
    }

    #[test]
    fn test_handle_web_channel_login_sets_session_token() {
        nss_as::ensure_initialized();
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        fxa.handle_web_channel_login(
            r#"{"sessionToken":"mock_session_token","uid":"mock_uid","email":"mock@example.com","verified":true}"#,
        )
        .unwrap();
        assert_eq!(fxa.get_session_token().unwrap(), "mock_session_token");
    }

    #[test]
    fn test_oauth_request_sent_with_session_when_available() {
        nss_as::ensure_initialized();
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        );
        let mut fxa = FirefoxAccount::with_config(config);
        let url = fxa
            .begin_oauth_flow("", &[OLD_SYNC, "profile"], "test_entrypoint")
            .unwrap();
        let url = Url::parse(&url).unwrap();
        let state = url.query_pairs().find(|(name, _)| name == "state").unwrap();
        let mut client = MockFxAClient::new();

        client
            .expect_create_refresh_token_using_authorization_code()
            .withf(|_, session_token, code, _| {
                matches!(session_token, Some("mock_session_token")) && code == "mock_code"
            })
            .times(1)
            .returning(|_, _, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: Some("refresh_token".to_string()),
                    session_token: None,
                    expires_in: 1,
                    scope: "profile".to_string(),
                    access_token: "access_token".to_string(),
                })
            });
        client
            .expect_destroy_access_token()
            .with(always(), always())
            .times(1)
            .returning(|_, _| Ok(()));
        fxa.set_client(Arc::new(client));
        fxa.set_session_token("mock_session_token");

        fxa.complete_oauth_flow("mock_code", state.1.as_ref())
            .unwrap();
    }

    fn make_mock_device(name: &str) -> GetDeviceResponse {
        use sync15::DeviceType;
        GetDeviceResponse {
            common: DeviceResponseCommon {
                id: "device1".into(),
                display_name: name.to_string(),
                device_type: DeviceType::Desktop,
                push_subscription: None,
                available_commands: HashMap::new(),
                push_endpoint_expired: false,
            },
            is_current_device: true,
            location: DeviceLocation {
                city: None,
                country: None,
                state: None,
                state_code: None,
            },
            last_access_time: None,
        }
    }

    fn make_mock_update_device_response() -> UpdateDeviceResponse {
        use sync15::DeviceType;
        UpdateDeviceResponse {
            id: "device1".into(),
            display_name: "Test Device".to_string(),
            device_type: DeviceType::Desktop,
            push_subscription: None,
            available_commands: HashMap::new(),
            push_endpoint_expired: false,
        }
    }

    // Test that when we complete an oauth flow while already having a refresh token with
    // different scopes, the new token is merged with the old scopes and the device is restored.
    #[test]
    fn test_complete_oauth_flow_merges_scopes_and_restores_device() {
        nss_as::ensure_initialized();
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        );
        let mut fxa = FirefoxAccount::with_config(config);

        // Start a flow before setting state, to register the pending oauth flow.
        let url = fxa
            .begin_oauth_flow("", &["new_scope"], "test_entrypoint")
            .unwrap();
        let url = Url::parse(&url).unwrap();
        let state = url.query_pairs().find(|(name, _)| name == "state").unwrap();

        // Pre-populate: existing refresh token (different scope) and a session token.
        fxa.state.force_refresh_token(RefreshToken {
            token: "old_refresh".to_string(),
            scopes: ["profile".to_string()].into(),
        });
        fxa.set_session_token("mock_session_token");

        let mut client = MockFxAClient::new();

        // 1. Exchange auth code — returns narrow token with only the new scope.
        client
            .expect_create_refresh_token_using_authorization_code()
            .times(1)
            .returning(|_, _, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: Some("new_narrow_refresh".to_string()),
                    session_token: None,
                    expires_in: 3600,
                    scope: "new_scope".to_string(),
                    access_token: "access_token".to_string(),
                })
            });

        // 2. Destroy the over-scoped access token.
        client
            .expect_destroy_access_token()
            .with(always(), always())
            .times(1)
            .returning(|_, _| Ok(()));

        // 3. Fetch current device so it can be restored after token swap.
        client
            .expect_get_devices()
            .with(always(), eq("old_refresh"))
            .times(1)
            .returning(|_, _| Ok(vec![make_mock_device("Test Device")]));

        // 4. Get merged refresh token covering both old and new scopes.
        client
            .expect_create_refresh_token_using_session_token()
            .withf(|_, session_token, _| session_token == "mock_session_token")
            .times(1)
            .returning(|_, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: Some("merged_refresh".to_string()),
                    session_token: None,
                    expires_in: 3600,
                    scope: "profile new_scope".to_string(),
                    access_token: "access_token2".to_string(),
                })
            });

        // 5. Destroy the narrow new token (replaced by the merged one).
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("new_narrow_refresh"))
            .times(1)
            .returning(|_, _| Ok(()));

        // 6. Destroy the old refresh token.
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("old_refresh"))
            .times(1)
            .returning(|_, _| Ok(()));

        // 7. Restore the device record using the new merged refresh token.
        client
            .expect_update_device_record()
            .times(1)
            .returning(|_, _, _| Ok(make_mock_update_device_response()));

        fxa.set_client(Arc::new(client));

        fxa.complete_oauth_flow("mock_code", state.1.as_ref())
            .unwrap();

        let scopes = &fxa.state.refresh_token().unwrap().scopes;
        assert!(
            scopes.contains("profile"),
            "expected profile scope, got {scopes:?}"
        );
        assert!(
            scopes.contains("new_scope"),
            "expected new_scope, got {scopes:?}"
        );
        assert_eq!(scopes.len(), 2);
    }

    // Test that when the new refresh token already covers all existing scopes, no merge
    // is performed (no extra token request), but the old token is still destroyed and
    // the device is restored.
    #[test]
    fn test_complete_oauth_flow_no_merge_when_scopes_match() {
        nss_as::ensure_initialized();
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        );
        let mut fxa = FirefoxAccount::with_config(config);

        let url = fxa
            .begin_oauth_flow("", &["profile"], "test_entrypoint")
            .unwrap();
        let url = Url::parse(&url).unwrap();
        let state = url.query_pairs().find(|(name, _)| name == "state").unwrap();

        fxa.state.force_refresh_token(RefreshToken {
            token: "old_refresh".to_string(),
            scopes: ["profile".to_string()].into(),
        });
        fxa.set_session_token("mock_session_token");

        let mut client = MockFxAClient::new();

        // 1. Exchange auth code — returns token with same scopes as before.
        client
            .expect_create_refresh_token_using_authorization_code()
            .times(1)
            .returning(|_, _, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: Some("new_refresh".to_string()),
                    session_token: None,
                    expires_in: 3600,
                    scope: "profile".to_string(),
                    access_token: "access_token".to_string(),
                })
            });

        // 2. Destroy the over-scoped access token.
        client
            .expect_destroy_access_token()
            .with(always(), always())
            .times(1)
            .returning(|_, _| Ok(()));

        // 3. Fetch current device for restoration.
        client
            .expect_get_devices()
            .with(always(), eq("old_refresh"))
            .times(1)
            .returning(|_, _| Ok(vec![make_mock_device("Test Device")]));

        // No create_refresh_token_using_session_token — scopes already match.
        // No destroy of the new token — it becomes our token directly.

        // 4. Destroy only the old refresh token.
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("old_refresh"))
            .times(1)
            .returning(|_, _| Ok(()));

        // 5. Restore the device record.
        client
            .expect_update_device_record()
            .times(1)
            .returning(|_, _, _| Ok(make_mock_update_device_response()));

        fxa.set_client(Arc::new(client));

        fxa.complete_oauth_flow("mock_code", state.1.as_ref())
            .unwrap();

        let scopes = &fxa.state.refresh_token().unwrap().scopes;
        assert_eq!(scopes, &["profile".to_string()].into());
    }

    // Test that adding a non-sync scope to an account that is already signed in with sync
    // retains the sync scoped key we already hold, even though this response carries no keys.
    #[test]
    fn test_complete_oauth_flow_retains_existing_sync_key_when_adding_scope() {
        nss_as::ensure_initialized();
        let config = Config::new_with_mock_well_known_fxa_client_configuration(
            "mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        );
        let mut fxa = FirefoxAccount::with_config(config);

        // Start a flow requesting only a new, non-sync scope.
        let url = fxa
            .begin_oauth_flow("", &["new_scope"], "test_entrypoint")
            .unwrap();
        let url = Url::parse(&url).unwrap();
        let state = url.query_pairs().find(|(name, _)| name == "state").unwrap();

        // Pre-populate: signed in with the sync scope, holding its scoped key, plus a session
        // token so the scope merge can happen.
        fxa.state.force_refresh_token(RefreshToken {
            token: "old_refresh".to_string(),
            scopes: [OLD_SYNC.to_string()].into(),
        });
        fxa.state.insert_scoped_key(
            OLD_SYNC,
            crate::ScopedKey {
                kty: "oct".to_string(),
                scope: OLD_SYNC.to_string(),
                k: "existing_sync_key_material".to_string(),
                kid: "existing_sync_kid".to_string(),
            },
        );
        fxa.set_session_token("mock_session_token");

        let mut client = MockFxAClient::new();

        // 1. Exchange auth code — narrow token with only the new scope and no keys.
        client
            .expect_create_refresh_token_using_authorization_code()
            .times(1)
            .returning(|_, _, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: Some("new_narrow_refresh".to_string()),
                    session_token: None,
                    expires_in: 3600,
                    scope: "new_scope".to_string(),
                    access_token: "access_token".to_string(),
                })
            });

        // 2. Destroy the over-scoped access token.
        client
            .expect_destroy_access_token()
            .with(always(), always())
            .times(1)
            .returning(|_, _| Ok(()));

        // 3. Fetch current device so it can be restored after the token swap.
        client
            .expect_get_devices()
            .with(always(), eq("old_refresh"))
            .times(1)
            .returning(|_, _| Ok(vec![make_mock_device("Test Device")]));

        // 4. Get merged refresh token covering both the old sync scope and the new scope.
        client
            .expect_create_refresh_token_using_session_token()
            .withf(|_, session_token, _| session_token == "mock_session_token")
            .times(1)
            .returning(|_, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: Some("merged_refresh".to_string()),
                    session_token: None,
                    expires_in: 3600,
                    scope: format!("{OLD_SYNC} new_scope"),
                    access_token: "access_token2".to_string(),
                })
            });

        // 5. Destroy the narrow new token (replaced by the merged one).
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("new_narrow_refresh"))
            .times(1)
            .returning(|_, _| Ok(()));

        // 6. Destroy the old refresh token.
        client
            .expect_destroy_refresh_token()
            .with(always(), eq("old_refresh"))
            .times(1)
            .returning(|_, _| Ok(()));

        // 7. Restore the device record.
        client
            .expect_update_device_record()
            .times(1)
            .returning(|_, _, _| Ok(make_mock_update_device_response()));

        fxa.set_client(Arc::new(client));

        fxa.complete_oauth_flow("mock_code", state.1.as_ref())
            .unwrap();

        // The sync key we already held must survive, even though this flow carried no keys.
        let sync_key = fxa
            .state
            .get_scoped_key(OLD_SYNC)
            .expect("sync scoped key should be retained");
        assert_eq!(sync_key.k, "existing_sync_key_material");

        // And the merged refresh token should carry both scopes.
        let scopes = &fxa.state.refresh_token().unwrap().scopes;
        assert!(
            scopes.contains(OLD_SYNC),
            "expected sync scope, got {scopes:?}"
        );
        assert!(
            scopes.contains("new_scope"),
            "expected new_scope, got {scopes:?}"
        );
        assert_eq!(scopes.len(), 2);
    }
}
