/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub use super::http_client::ProfileResponse as Profile;
use super::{scopes, util, CachedResponse, FirefoxAccount};
use crate::{Error, Result};

// A cached profile response is considered fresh for `PROFILE_FRESHNESS_THRESHOLD` ms.
const PROFILE_FRESHNESS_THRESHOLD: u64 = 120_000; // 2 minutes

impl FirefoxAccount {
    /// Fetch the profile for the user.
    /// This method will error-out if the `profile` scope is not
    /// authorized for the current refresh token or or if we do
    /// not have a valid refresh token.
    ///
    /// * `ignore_cache` - If set to true, bypass the in-memory cache
    ///   and fetch the entire profile data from the server.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn get_profile(&mut self, ignore_cache: bool) -> Result<Profile> {
        self.with_auth_and_network_retry(scopes::PROFILE, "get_profile", |s| {
            s._get_profile(ignore_cache)
        })
    }

    fn _get_profile(&mut self, ignore_cache: bool) -> Result<Profile> {
        let mut etag = None;
        if let Some(cached_profile) = self.state.last_seen_profile() {
            if !ignore_cache && util::now() < cached_profile.cached_at + PROFILE_FRESHNESS_THRESHOLD
            {
                return Ok(cached_profile.response.clone());
            }
            etag = Some(cached_profile.etag.clone());
        }
        let profile_access_token = self.get_access_token(scopes::PROFILE, true)?.token;
        match self
            .client
            .get_profile(self.state.config(), &profile_access_token, etag)?
        {
            Some(response_and_etag) => {
                if let Some(etag) = response_and_etag.etag {
                    self.state.set_last_seen_profile(CachedResponse {
                        response: response_and_etag.response.clone(),
                        cached_at: util::now(),
                        etag,
                    });
                }
                Ok(response_and_etag.response)
            }
            None => {
                match self.state.last_seen_profile() {
                    Some(cached_profile) => {
                        let response = cached_profile.response.clone();
                        // Update `cached_at` timestamp.
                        let new_cached_profile = CachedResponse {
                            response: cached_profile.response.clone(),
                            cached_at: util::now(),
                            etag: cached_profile.etag.clone(),
                        };
                        self.state.set_last_seen_profile(new_cached_profile);
                        Ok(response)
                    }
                    None => Err(Error::ApiClientError(
                        "Got a 304 without having sent an eTag.",
                    )),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::internal::{
        http_client::*,
        oauth::{AccessTokenInfo, RefreshToken},
        Config,
    };
    use mockall::predicate::always;
    use mockall::predicate::eq;
    use std::sync::Arc;

    impl FirefoxAccount {
        pub fn add_cached_profile(&mut self, uid: &str, email: &str) {
            self.state.set_last_seen_profile(CachedResponse {
                response: Profile {
                    uid: uid.into(),
                    email: email.into(),
                    display_name: None,
                    avatar: "".into(),
                    avatar_default: true,
                },
                cached_at: util::now(),
                etag: "fake etag".into(),
            });
        }
    }

    #[test]
    fn test_fetch_profile() {
        let config = Config::stable_dev("12345678", "https://foo.bar");
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

        let mut client = MockFxAClient::new();
        client
            .expect_get_profile()
            .with(always(), eq("profiletok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(Some(ResponseAndETag {
                    response: ProfileResponse {
                        uid: "12345ab".to_string(),
                        email: "foo@bar.com".to_string(),
                        display_name: None,
                        avatar: "https://foo.avatar".to_string(),
                        avatar_default: true,
                    },
                    etag: Some("123".to_string()),
                }))
            });
        fxa.set_client(Arc::new(client));

        let p = fxa.get_profile(false).unwrap();
        assert_eq!(p.email, "foo@bar.com");
        // do it again to check our caching - if it wasn't cached and we hit our mock
        // of fetching the profile we'd still fail due to the mock only expecting 1 call.
        let p = fxa.get_profile(false).unwrap();
        assert_eq!(p.email, "foo@bar.com");

        // make sure the cached value doesn't survive a disconnect.
        fxa.disconnect();
        // now we should have no cached value, but wont even try to hit the server as we
        // have no session token.
        assert!(matches!(fxa.get_profile(false), Err(Error::NoSessionToken)));
    }

    #[test]
    fn test_expired_access_token_refetch() {
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);

        fxa.add_cached_token(
            "profile",
            AccessTokenInfo {
                scope: "profile".to_string(),
                token: "bad_access_token".to_string(),
                key: None,
                expires_at: u64::MAX,
            },
        );
        let mut refresh_token_scopes = std::collections::HashSet::new();
        refresh_token_scopes.insert("profile".to_owned());
        fxa.state.force_refresh_token(RefreshToken {
            token: "refreshtok".to_owned(),
            scopes: refresh_token_scopes,
        });

        let mut client = MockFxAClient::new();
        // First call to profile() we fail with 401.
        client
            .expect_get_profile()
            .with(always(), eq("bad_access_token"), always())
            .times(1)
            .returning(|_, _, _| Err(Error::RemoteError{
                code: 401,
                errno: 110,
                error: "Unauthorized".to_owned(),
                message: "Invalid authentication token in request signature".to_owned(),
                info: "https://github.com/mozilla/fxa-auth-server/blob/master/docs/api.md#response-format".to_owned(),
            }));
        // Then we'll try to get a new access token.
        client
            .expect_create_access_token_using_refresh_token()
            .with(always(), eq("refreshtok"), always(), always())
            .times(1)
            .returning(|_, _, _, _| {
                Ok(OAuthTokenResponse {
                    keys_jwe: None,
                    refresh_token: None,
                    expires_in: 6_000_000,
                    scope: "profile".to_owned(),
                    access_token: "good_profile_token".to_owned(),
                    session_token: None,
                })
            });
        // Then hooray it works!
        client
            .expect_get_profile()
            .with(always(), eq("good_profile_token"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(Some(ResponseAndETag {
                    response: ProfileResponse {
                        uid: "12345ab".to_string(),
                        email: "foo@bar.com".to_string(),
                        display_name: None,
                        avatar: "https://foo.avatar".to_string(),
                        avatar_default: true,
                    },
                    etag: None,
                }))
            });
        fxa.set_client(Arc::new(client));

        let p = fxa.get_profile(false).unwrap();
        assert_eq!(p.email, "foo@bar.com");
    }
}
