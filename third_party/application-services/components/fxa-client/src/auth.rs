/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Signing in and out
//!
//! Signing in and out is driven through the state machine: by sending the relevant
//! [`FxaEvent`] to [`FirefoxAccount::process_event`].
//!
//! The Firefox Accounts system supports two methods for connecting an application
//! to a user's account:
//!
//!    - A traditional OAuth flow, where the user is directed to a webpage to enter
//!      their account credentials and then redirected back to the application.
//!      This is driven by the [`FxaEvent::BeginOAuthFlow`] and
//!      [`FxaEvent::CompleteOAuthFlow`] events.
//!
//!    - A device pairing flow, where the user scans a QRCode presented by another
//!      app that is already connected to the account, which then directs them to
//!      a webpage for a simplified signing flow. This is driven by the
//!      [`FxaEvent::BeginPairingFlow`] event.
//!
//! Technical details of the pairing flow can be found in the [Firefox Accounts
//! documentation hub](https://mozilla.github.io/ecosystem-platform/docs/features/firefox-accounts/pairing).

use crate::{ApiResult, DeviceConfig, Error, FirefoxAccount};
use error_support::handle_error;

#[uniffi::export]
impl FirefoxAccount {
    /// Get the current state
    pub fn get_state(&self) -> FxaState {
        self.internal.lock().get_state()
    }

    /// Process an event (login, logout, etc).
    ///
    /// On success, returns the new state.
    /// On error, the state will remain the same.
    #[handle_error(Error)]
    pub fn process_event(&self, event: FxaEvent) -> ApiResult<FxaState> {
        self.internal.lock().process_event(event)
    }

    /// Get the high-level authentication state of the client
    ///
    /// TODO: remove this and the FxaRustAuthState type from the public API
    /// https://bugzilla.mozilla.org/show_bug.cgi?id=1868614
    pub fn get_auth_state(&self) -> FxaRustAuthState {
        self.internal.lock().get_auth_state()
    }

    /// Stores the session token from a WebChannel login JSON payload without exposing it
    /// to the browser layer.
    ///
    /// The `json_payload` is the `data` object from the `fxaccounts:login` WebChannel
    /// command. The session token is extracted and stored internally; callers never hold
    /// the raw token value.
    ///
    /// **💾 This method alters the persisted account state.**
    #[handle_error(Error)]
    pub fn handle_web_channel_login(&self, json_payload: String) -> ApiResult<()> {
        self.internal.lock().handle_web_channel_login(&json_payload)
    }

    /// Get the URL at which to begin a device-pairing signin flow.
    ///
    /// If the user wants to sign in using device pairing, call this method and then
    /// direct them to visit the resulting URL on an already-signed-in device. Doing
    /// so will trigger the other device to show a QR code to be scanned, and the result
    /// from said QR code can be passed to the [`FxaEvent::BeginPairingFlow`] event.
    #[handle_error(Error)]
    pub fn get_pairing_authority_url(&self) -> ApiResult<String> {
        self.internal.lock().get_pairing_authority_url()
    }

    /// Check authorization status for this application.
    ///
    /// **💾 This method alters the persisted account state.**
    ///
    /// Applications may call this method to check with the FxA server about the status
    /// of their authentication tokens. It returns an [`AuthorizationInfo`] struct
    /// with details about whether the tokens are still active.
    #[handle_error(Error)]
    pub fn check_authorization_status(&self) -> ApiResult<AuthorizationInfo> {
        Ok(self.internal.lock().check_authorization_status()?.into())
    }

    /// Disconnect from the user's account.
    ///
    /// **💾 This method alters the persisted account state.**
    ///
    /// This method destroys any tokens held by the client, effectively disconnecting
    /// from the user's account. Applications should call this when the user opts to
    /// sign out.
    ///
    /// The persisted account state after calling this method will contain only the
    /// user's last-seen profile information, if any. This may be useful in helping
    /// the user to reconnect to their account. If reconnecting to the same account
    /// is not desired then the application should discard the persisted account state.
    pub fn disconnect(&self) {
        self.internal.lock().disconnect()
    }

    /// Update the state based on authentication issues.
    ///
    /// **💾 This method alters the persisted account state.**
    ///
    /// Call this if you know there's an authentication / authorization issue that requires the
    /// user to re-authenticated.  It transitions the user to the [FxaRustAuthState.AuthIssues] state.
    pub fn on_auth_issues(&self) {
        self.internal.lock().on_auth_issues()
    }

    /// Reset the timer indicating time since last auth issues were encountered.
    ///
    /// **💾 This method alters the persisted account state.**
    ///
    /// Call this if we have encountered the [FxaRustAuthState.AuthIssues] state as a result of a
    /// failure happening (i.e. not as a result of initialization simply loading that state from a
    /// previous failure).
    /// Most likely, this should not need to be called externally except in testing since the state
    /// machine's `transition` function should generally call the internal version of this function
    /// when necessary.
    pub fn reset_auth_recheck_timer(&self) {
        self.internal.lock().reset_auth_recheck_timer()
    }

    /// Used by the application to test auth token issues
    pub fn simulate_temporary_auth_token_issue(&self) {
        self.internal.lock().simulate_temporary_auth_token_issue()
    }

    /// Used by the application to test auth token issues
    pub fn simulate_permanent_auth_token_issue(&self) {
        self.internal.lock().simulate_permanent_auth_token_issue()
    }
}

#[derive(uniffi::Record)]
/// Information about the authorization state of the application.
///
/// This struct represents metadata about whether the application is currently
/// connected to the user's account.
pub struct AuthorizationInfo {
    pub active: bool,
}

#[derive(uniffi::Enum, Clone, Copy, Debug, PartialEq, Eq)]
/// High-level view of the authorization state
///
/// This is named `FxaRustAuthState` because it doesn't track all the states we want yet and needs
/// help from the wrapper code.  The wrapper code defines the actual `FxaAuthState` type based on
/// this, adding the extra data.
///
/// In the long-term, we should track that data in Rust, remove the wrapper, and rename this to
/// `FxaAuthState`.
pub enum FxaRustAuthState {
    Disconnected,
    Connected,
    AuthIssues,
}

#[derive(uniffi::Enum, Clone, Debug, PartialEq, Eq)]
/// Fxa state
///
/// These are the states of [crate::FxaStateMachine] that consumers observe.
pub enum FxaState {
    /// The state machine needs to be initialized via [Event::Initialize].
    Uninitialized,
    /// User has not connected to FxA or has logged out
    Disconnected,
    /// User is currently performing an OAuth flow - our existing initial state
    /// when we transition to this state will influence what this means exactly.
    Authenticating {
        oauth_url: String,
        initial_state: FxaRustAuthState,
    },
    /// User is currently connected to FxA
    Connected,
    /// User was connected to FxA, but we observed issues with the auth tokens.
    /// The user needs to reauthenticate before the account can be used.
    AuthIssues,
}

impl From<FxaRustAuthState> for FxaState {
    fn from(value: FxaRustAuthState) -> Self {
        match value {
            FxaRustAuthState::Connected => FxaState::Connected,
            FxaRustAuthState::Disconnected => FxaState::Disconnected,
            FxaRustAuthState::AuthIssues => FxaState::AuthIssues,
        }
    }
}

#[derive(uniffi::Enum, Clone, Debug, PartialEq, Eq)]
/// Fxa event
///
/// These are the events that consumers send to [crate::FxaStateMachine::process_event]
pub enum FxaEvent {
    /// Initialize the state machine.  This must be the first event sent.
    Initialize { device_config: DeviceConfig },
    /// Begin an oauth flow
    ///
    /// If successful, the state machine will transition the [FxaState::Authenticating].  The next
    /// step is to navigate the user to the `oauth_url` and let them sign and authorize the client.
    ///
    /// This event is valid for the `Disconnected`, `AuthIssues`, and `Authenticating` states.  If
    /// the state machine is in the `Authenticating` state, then this will forget the current OAuth
    /// flow and start a new one.
    BeginOAuthFlow {
        service: String,
        scopes: Vec<String>,
        entrypoint: String,
    },
    /// Begin an oauth flow using a URL from a pairing code
    ///
    /// If successful, the state machine will transition the [FxaState::Authenticating].  The next
    /// step is to navigate the user to the `oauth_url` and let them sign and authorize the client.
    ///
    /// This event is valid for the `Disconnected`, `AuthIssues`, and `Authenticating` states.  If
    /// the state machine is in the `Authenticating` state, then this will forget the current OAuth
    /// flow and start a new one.
    BeginPairingFlow {
        pairing_url: String,
        service: String,
        scopes: Vec<String>,
        entrypoint: String,
    },
    /// Complete an OAuth flow.
    ///
    /// Send this event after the user has navigated through the OAuth flow and has reached the
    /// redirect URI.  Extract `code` and `state` from the query parameters or web channel.  If
    /// successful the state machine will transition to [FxaState::Connected].
    ///
    /// This event is valid for the `Authenticating` state.
    CompleteOAuthFlow { code: String, state: String },
    /// Cancel an OAuth flow.
    ///
    /// Use this to cancel an in-progress OAuth, returning to [FxaState::Disconnected] so the
    /// process can begin again.
    ///
    /// This event is valid for the `Authenticating` state.
    CancelOAuthFlow,
    /// Check the authorization status for a connected account.
    ///
    /// Send this when issues are detected with the auth tokens for a connected account.  It will
    /// double check for authentication issues with the account.  If it detects them, the state
    /// machine will transition to [FxaState::AuthIssues].  From there you can start an OAuth flow
    /// again to re-connect the user.
    ///
    /// This event is valid for the `Connected` state.
    CheckAuthorizationStatus,
    /// An `fxaccounts:change_password` WebChannel message arrived on the device that just changed
    /// its password. `json_payload` is the `data` object of that message and contains the new
    /// session token. The state machine swaps the session token for a new refresh token and
    /// re-initialises the device record.
    ///
    /// This event is valid for the `Connected` and `AuthIssues` states. In `Authenticating` it
    /// is a no-op so the in-progress OAuth flow is not disrupted.
    WebChannelPasswordChange { json_payload: String },
    /// Disconnect the user
    ///
    /// Send this when the user is asking to be logged out.  The state machine will transition to
    /// [FxaState::Disconnected].
    ///
    /// This event is valid for the `Connected` state.
    Disconnect,
    /// Force a call to [FirefoxAccount::get_profile]
    ///
    /// This is used for testing the auth/network retry code, since it hits the network and
    /// requires and auth token.
    ///
    /// This event is valid for the `Connected` state.
    CallGetProfile,
}
