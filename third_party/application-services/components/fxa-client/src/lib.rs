/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Firefox Accounts Client
//!
//! The fxa-client component lets applications integrate with the
//! [Firefox Accounts](https://mozilla.github.io/ecosystem-platform/docs/features/firefox-accounts/fxa-overview)
//! identity service. The shape of a typical integration would look
//! something like:
//!
//! * Out-of-band, register your application with the Firefox Accounts service,
//!   providing an OAuth `redirect_uri` controlled by your application and
//!   obtaining an OAuth `client_id`.
//!
//! * On application startup, create a [`FirefoxAccount`] object to represent the
//!   signed-in state of the application.
//!     * On first startup, a new [`FirefoxAccount`] can be created by calling
//!       [`FirefoxAccount::new`] and passing the application's `client_id`.
//!     * For subsequent startups the object can be persisted using the
//!       [`to_json`](FirefoxAccount::to_json) method and re-created by
//!       calling [`FirefoxAccount::from_json`].
//!
//! * When the user wants to sign in to your application, direct them through
//!   a web-based OAuth flow by sending the [`FxaEvent::BeginOAuthFlow`] or
//!   [`FxaEvent::BeginPairingFlow`] event to [`FirefoxAccount::process_event`]; when they
//!   return to your registered `redirect_uri`, pass the resulting authorization state back
//!   via the [`FxaEvent::CompleteOAuthFlow`] event to sign them in.
//!
//! * Display information about the signed-in user by using the data from
//!   [`get_profile`](FirefoxAccount::get_profile).
//!
//! * Access account-related services on behalf of the user by obtaining OAuth
//!   access tokens via [`get_access_token`](FirefoxAccount::get_access_token).
//!
//! * If the user opts to sign out of the application, calling [`disconnect`](FirefoxAccount::disconnect)
//!   and then discarding any persisted account data.

mod account;
mod auth;
mod device;
mod error;
mod internal;
mod profile;
mod push;
mod state_machine;
mod storage;
mod telemetry;
mod token;

use std::fmt;

pub use sync15::DeviceType;
use url::Url;

pub use auth::{AuthorizationInfo, FxaEvent, FxaRustAuthState, FxaState};
pub use device::{
    AttachedClient, CloseTabsResult, Device, DeviceCapability, DeviceConfig, LocalDevice,
};
pub use error::{Error, FxaError};
// reexport logging helpers.
pub use error_support::{debug, error, info, trace, warn};

use parking_lot::Mutex;
pub use profile::Profile;
pub use push::{
    AccountEvent, CloseTabsPayload, DevicePushSubscription, IncomingDeviceCommand, SendTabPayload,
    TabHistoryEntry,
};
pub use token::{AccessTokenInfo, AuthorizationParameters, ScopedKey};

/// Result returned by internal functions
pub type Result<T> = std::result::Result<T, Error>;
/// Result returned by public-facing API functions
pub type ApiResult<T> = std::result::Result<T, FxaError>;

#[derive(uniffi::Object)]
/// Object representing the signed-in state of an application.
///
/// The `FirefoxAccount` object is the main interface provided by this crate.
/// It represents the signed-in state of an application that may be connected to
/// user's Firefox Account, and provides methods for inspecting the state of the
/// account and accessing other services on behalf of the user.
pub struct FirefoxAccount {
    // For now, we serialize all access on a single `Mutex` for thread safety across
    // the FFI. We should make the locking more granular in future.
    internal: Mutex<internal::FirefoxAccount>,
}

#[uniffi::export]
impl FirefoxAccount {
    /// Create a new [`FirefoxAccount`] instance, not connected to any account.
    ///
    /// **💾 This method alters the persisted account state.**
    ///
    /// This method constructs as new [`FirefoxAccount`] instance configured to connect
    /// the application to a user's account.
    #[uniffi::constructor]
    pub fn new(config: FxaConfig) -> FirefoxAccount {
        FirefoxAccount {
            internal: Mutex::new(internal::FirefoxAccount::new(config)),
        }
    }

    /// Used by the application to test auth token issues
    pub fn simulate_network_error(&self) {
        self.internal.lock().simulate_network_error()
    }
}

#[derive(uniffi::Record, Clone, Debug)]
pub struct FxaConfig {
    /// FxaServer to connect with
    pub server: FxaServer,
    /// Registered OAuth client id of the application.
    pub client_id: String,
    /// `redirect_uri` - the registered OAuth redirect URI of the application.
    pub redirect_uri: String,
    ///  URL for the user's Sync Tokenserver. This can be used to support users who self-host their
    ///  sync data. If `None` then it will default to the Mozilla-hosted Sync server.
    ///
    ///  Note: this lives here for historical reasons, but probably shouldn't.  Applications pass
    ///  the token server URL they get from `fxa-client` to `SyncManager`.  It would be simpler to
    ///  cut out `fxa-client` out of the middle and have applications send the overridden URL
    ///  directly to `SyncManager`.
    #[uniffi(default=None)]
    pub token_server_url_override: Option<String>,
}

#[derive(uniffi::Enum, Clone, Debug, PartialEq, Eq)]
pub enum FxaServer {
    Release,
    Stable,
    Stage,
    China,
    LocalDev,
    Custom { url: String },
}

impl FxaServer {
    pub fn content_url(&self) -> &str {
        match self {
            Self::Release | Self::China => "https://accounts.firefox.com",
            Self::Stable => "https://stable.dev.lcip.org",
            Self::Stage => "https://accounts.stage.mozaws.net",
            Self::LocalDev => "http://127.0.0.1:3030",
            Self::Custom { url } => url,
        }
    }
}

impl From<&Url> for FxaServer {
    fn from(url: &Url) -> Self {
        let origin = url.origin();
        // Note: we can call unwrap() below because parsing content_url for known servers should
        // never fail and `test_from_url` tests this.
        if origin == Url::parse(Self::Release.content_url()).unwrap().origin() {
            Self::Release
        } else if origin == Url::parse(Self::Stable.content_url()).unwrap().origin() {
            Self::Stable
        } else if origin == Url::parse(Self::Stage.content_url()).unwrap().origin() {
            Self::Stage
        } else if origin
            == Url::parse("https://accounts.firefox.com.cn")
                .unwrap()
                .origin()
        {
            // FxaServer::China is now the same as FxaServer::Release, but we may still end up being passed the old .cn domain.
            // This is a little odd as we will not correctly round-trip in this scenario, but it seems better than the
            // .cn domain being treated as `Self::Custom`.
            Self::Release
        } else if origin == Url::parse(Self::LocalDev.content_url()).unwrap().origin() {
            Self::LocalDev
        } else {
            Self::Custom {
                url: origin.ascii_serialization(),
            }
        }
    }
}

/// Display impl
///
/// This identifies the variant, without recording the URL for custom servers.  It's good for
/// Sentry reports when we don't want to give away any PII.
impl fmt::Display for FxaServer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let variant_name = match self {
            Self::Release => "Release",
            Self::Stable => "Stable",
            Self::Stage => "Stage",
            Self::China => "China",
            Self::LocalDev => "LocalDev",
            Self::Custom { .. } => "Custom",
        };
        write!(f, "{variant_name}")
    }
}

impl FxaConfig {
    pub fn release(client_id: impl ToString, redirect_uri: impl ToString) -> Self {
        Self {
            server: FxaServer::Release,
            client_id: client_id.to_string(),
            redirect_uri: redirect_uri.to_string(),
            token_server_url_override: None,
        }
    }

    pub fn stable(client_id: impl ToString, redirect_uri: impl ToString) -> Self {
        Self {
            server: FxaServer::Stable,
            client_id: client_id.to_string(),
            redirect_uri: redirect_uri.to_string(),
            token_server_url_override: None,
        }
    }

    pub fn stage(client_id: impl ToString, redirect_uri: impl ToString) -> Self {
        Self {
            server: FxaServer::Stage,
            client_id: client_id.to_string(),
            redirect_uri: redirect_uri.to_string(),
            token_server_url_override: None,
        }
    }

    pub fn dev(client_id: impl ToString, redirect_uri: impl ToString) -> Self {
        Self {
            server: FxaServer::LocalDev,
            client_id: client_id.to_string(),
            redirect_uri: redirect_uri.to_string(),
            token_server_url_override: None,
        }
    }
}

uniffi::setup_scaffolding!("fxa_client");

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_url() {
        let test_cases = [
            ("https://accounts.firefox.com", FxaServer::Release),
            ("https://stable.dev.lcip.org", FxaServer::Stable),
            ("https://accounts.stage.mozaws.net", FxaServer::Stage),
            ("https://accounts.firefox.com.cn", FxaServer::Release),
            ("http://127.0.0.1:3030", FxaServer::LocalDev),
            (
                "http://my-fxa-server.com",
                FxaServer::Custom {
                    url: "http://my-fxa-server.com".to_owned(),
                },
            ),
        ];
        for (content_url, expected_result) in test_cases {
            let url = Url::parse(content_url).unwrap();
            assert_eq!(FxaServer::from(&url), expected_result);
        }
    }
}
