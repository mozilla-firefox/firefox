/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! State machine helpers: error wrapper, target-state extension trait, and
//! a `RetryingAccount` wrapper.
//!
//! Holding `&mut RetryingAccount` instead of `&mut FirefoxAccount` makes it
//! structurally impossible to call into the network without retry.

use crate::{
    internal::FirefoxAccount, DeviceCapability, DeviceType, Error, FxaError, FxaRustAuthState,
    FxaState, LocalDevice, Result,
};
use error_support::GetErrorHandling;

/// Error returned from `transition()`.
///
/// Causes are boxed because `Error` is ~120 bytes; keeping the variants small
/// avoids inflating every transition's `Result`.
#[derive(Debug)]
pub enum StateMachineErr {
    /// Operational error (network, auth). Driver logs the cause and commits
    /// `target` as the new public state.
    Handled { cause: Box<Error>, target: FxaState },
    /// Programming error. Driver returns `Err(*cause)`; public state unchanged.
    Fatal(Box<Error>),
}

impl StateMachineErr {
    /// Programmatic errors (logic / invalid-transition) become `Fatal` and
    /// ignore `target_if_handled`; everything else becomes `Handled`.
    pub fn new(cause: Error, target_if_handled: FxaState) -> Self {
        match cause {
            Error::StateMachineLogicError(_) | Error::InvalidStateTransition(_) => {
                Self::Fatal(Box::new(cause))
            }
            other => Self::Handled {
                cause: Box::new(other),
                target: target_if_handled,
            },
        }
    }
}

/// Attaches a target [`FxaState`] to a `Result<T, Error>` so transitions can
/// land somewhere specific on failure with `?`.
///
/// Programming errors (logic / invalid-transition) bypass `target` and are
/// wrapped as [`StateMachineErr::Fatal`] so the driver propagates them.
///
/// ```ignore
/// account.complete_oauth_flow(&code, &state)
///     .to_state_machine_err(|| initial_state.into())?;
/// ```
pub trait ResultExt<T> {
    fn to_state_machine_err(
        self,
        f: impl FnOnce() -> FxaState,
    ) -> std::result::Result<T, StateMachineErr>;
}

impl<T> ResultExt<T> for Result<T> {
    fn to_state_machine_err(
        self,
        f: impl FnOnce() -> FxaState,
    ) -> std::result::Result<T, StateMachineErr> {
        self.map_err(|cause| match cause {
            Error::StateMachineLogicError(_) | Error::InvalidStateTransition(_) => {
                StateMachineErr::Fatal(Box::new(cause))
            }
            other => StateMachineErr::Handled {
                cause: Box::new(other),
                target: f(),
            },
        })
    }
}

/// Retry policy applied by [`RetryingAccount`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RetryPolicy {
    /// Number of times to retry a call after a transient network error.
    pub network_retries: u8,
    /// Whether an authentication error triggers a single recovery attempt
    /// (clear access token cache → check_authorization_status → retry on success).
    pub auth_retry_with_cache_clear: bool,
}

impl RetryPolicy {
    pub const DEFAULT: RetryPolicy = RetryPolicy {
        network_retries: 3,
        auth_retry_with_cache_clear: true,
    };
}

/// Exposes only the [`FirefoxAccount`] methods the FSM uses, each with retry
/// policy applied. Holding `&mut RetryingAccount` rather than `&mut FirefoxAccount`
/// makes bypassing retry a compile error.
pub struct RetryingAccount<'a> {
    inner: &'a mut FirefoxAccount,
    policy: RetryPolicy,
}

impl<'a> RetryingAccount<'a> {
    pub fn new(inner: &'a mut FirefoxAccount) -> Self {
        Self {
            inner,
            policy: RetryPolicy::DEFAULT,
        }
    }

    pub fn complete_oauth_flow(&mut self, code: &str, state: &str) -> Result<()> {
        self.with_auth_recovery(|a| a.complete_oauth_flow(code, state))
    }

    pub fn handle_web_channel_password_change(&mut self, json_payload: &str) -> Result<()> {
        self.with_auth_recovery(|a| a.handle_web_channel_password_change(json_payload))
    }

    /// Cancels any existing OAuth flow before starting a new one.
    pub fn begin_oauth_flow(
        &mut self,
        service: &str,
        scopes: &[&str],
        entrypoint: &str,
    ) -> Result<String> {
        self.inner.cancel_existing_oauth_flows();
        self.with_auth_recovery(|a| a.begin_oauth_flow(service, scopes, entrypoint))
    }

    /// Cancels any existing OAuth flow before starting a new one.
    pub fn begin_pairing_flow(
        &mut self,
        pairing_url: &str,
        service: &str,
        scopes: &[&str],
        entrypoint: &str,
    ) -> Result<String> {
        self.inner.cancel_existing_oauth_flows();
        self.with_auth_recovery(|a| a.begin_pairing_flow(pairing_url, service, scopes, entrypoint))
    }

    /// Auth errors land the FSM at `Disconnected`; no operation-level recovery.
    pub fn initialize_device(
        &mut self,
        name: &str,
        device_type: DeviceType,
        capabilities: &[DeviceCapability],
    ) -> Result<LocalDevice> {
        self.with_retry(|a| a.initialize_device(name, device_type, capabilities))
    }

    /// Finish initializing a connected account
    pub fn finish_initialize(&mut self, capabilities: &[DeviceCapability]) -> Result<()> {
        self.with_auth_recovery(|a| {
            a.ensure_capabilities(capabilities)?;
            Ok(())
        })
    }

    pub fn check_authorization_status(&mut self) -> Result<bool> {
        self.with_retry(|a| a.check_authorization_status())
            .map(|info| info.active)
    }

    pub fn get_profile(&mut self) -> Result<()> {
        self.with_auth_recovery(|a| {
            a.get_profile(true)?;
            Ok(())
        })
    }

    pub fn get_auth_state(&mut self) -> FxaRustAuthState {
        self.inner.get_auth_state()
    }

    pub fn disconnect(&mut self) {
        self.inner.disconnect()
    }

    /// Panics if called before the driver has processed `Initialize`.
    pub fn device_config(&self) -> &crate::DeviceConfig {
        self.inner
            .device_config
            .as_ref()
            .expect("device_config must be set before transition runs (Initialize event seeds it)")
    }

    fn with_retry<T>(&mut self, mut op: impl FnMut(&mut FirefoxAccount) -> Result<T>) -> Result<T> {
        let mut network_retries: u8 = 0;
        loop {
            match op(self.inner) {
                Ok(v) => return Ok(v),
                Err(e) => {
                    if matches!(e, Error::StateMachineLogicError(_)) {
                        return Err(e);
                    }
                    crate::warn!("handling error: {e}");
                    match e.get_error_handling().err {
                        FxaError::Network if network_retries < self.policy.network_retries => {
                            network_retries += 1;
                            continue;
                        }
                        _ => return Err(e),
                    }
                }
            }
        }
    }

    /// Like `with_retry`, plus a single auth-error recovery: clear the access
    /// token cache, check authorization, and retry if the account is still
    /// active server-side.
    fn with_auth_recovery<T>(
        &mut self,
        mut op: impl FnMut(&mut FirefoxAccount) -> Result<T>,
    ) -> Result<T> {
        let mut network_retries: u8 = 0;
        let mut auth_retried = false;
        loop {
            match op(self.inner) {
                Ok(v) => return Ok(v),
                Err(e) => {
                    if matches!(e, Error::StateMachineLogicError(_)) {
                        return Err(e);
                    }
                    crate::warn!("handling error: {e}");
                    match e.get_error_handling().err {
                        FxaError::Network if network_retries < self.policy.network_retries => {
                            network_retries += 1;
                            continue;
                        }
                        FxaError::Authentication
                            if self.policy.auth_retry_with_cache_clear && !auth_retried =>
                        {
                            self.inner.clear_access_token_cache();
                            match self.inner.check_authorization_status() {
                                Ok(status) if status.active => {
                                    auth_retried = true;
                                    continue;
                                }
                                _ => return Err(e),
                            }
                        }
                        _ => return Err(e),
                    }
                }
            }
        }
    }

    /// Checks if enough time has passed since the last auth attempt that we should try checking the
    /// auth again.
    pub fn should_recheck_auth(&self) -> bool {
        self.inner.should_recheck_auth()
    }

    /// Set the last time we re-checked our authentication after a failure to the current time.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn reset_auth_recheck_timer(&mut self) {
        self.inner.reset_auth_recheck_timer();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use viaduct::ViaductError;

    fn network_error() -> Error {
        Error::RequestError(ViaductError::NetworkError("test".into()))
    }

    fn auth_error() -> Error {
        Error::NoRefreshToken
    }

    #[test]
    fn default_policy_pinned() {
        assert_eq!(
            RetryPolicy::DEFAULT,
            RetryPolicy {
                network_retries: 3,
                auth_retry_with_cache_clear: true,
            }
        );
    }

    #[test]
    fn to_state_machine_err_attaches_target_state_for_operational_errors() {
        let res: Result<()> = Err(network_error());
        let mapped = res.to_state_machine_err(|| FxaState::AuthIssues);
        match mapped {
            Err(StateMachineErr::Handled { target, .. }) => {
                assert_eq!(target, FxaState::AuthIssues)
            }
            Err(StateMachineErr::Fatal(_)) => panic!("expected Handled, got Fatal"),
            Ok(_) => panic!("expected Err"),
        }
    }

    #[test]
    fn to_state_machine_err_promotes_logic_errors_to_fatal() {
        let res: Result<()> = Err(Error::StateMachineLogicError("boom".into()));
        let mapped = res.to_state_machine_err(|| FxaState::Disconnected);
        match mapped {
            Err(StateMachineErr::Fatal(cause)) => {
                assert!(matches!(*cause, Error::StateMachineLogicError(_)))
            }
            Err(StateMachineErr::Handled { .. }) => panic!("expected Fatal, got Handled"),
            Ok(_) => panic!("expected Err"),
        }
    }

    #[test]
    fn to_state_machine_err_promotes_invalid_state_transition_to_fatal() {
        let res: Result<()> = Err(Error::InvalidStateTransition("nope".into()));
        let mapped = res.to_state_machine_err(|| FxaState::Disconnected);
        match mapped {
            Err(StateMachineErr::Fatal(cause)) => {
                assert!(matches!(*cause, Error::InvalidStateTransition(_)))
            }
            _ => panic!("expected Fatal"),
        }
    }

    #[test]
    fn to_state_machine_err_passes_ok_through() {
        let res: Result<i32> = Ok(42);
        let mapped = res.to_state_machine_err(|| FxaState::Disconnected);
        assert_eq!(mapped.unwrap(), 42);
    }

    // The retry tests construct a real `FirefoxAccount` because `RetryingAccount`
    // requires one structurally; the test closures don't actually touch it.

    fn mock_account() -> FirefoxAccount {
        use crate::internal::config::Config;
        FirefoxAccount::with_config(Config::new_with_mock_well_known_fxa_client_configuration(
            "https://mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        ))
    }

    #[test]
    fn with_retry_succeeds_first_try() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result = wrapper.with_retry(|_| {
            calls += 1;
            Ok::<_, Error>(42)
        });
        assert_eq!(result.unwrap(), 42);
        assert_eq!(calls, 1);
    }

    #[test]
    fn with_retry_retries_network_errors_then_succeeds() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result = wrapper.with_retry(|_| {
            calls += 1;
            if calls <= 2 {
                Err(network_error())
            } else {
                Ok(7)
            }
        });
        assert_eq!(result.unwrap(), 7);
        assert_eq!(calls, 3);
    }

    #[test]
    fn with_retry_gives_up_after_network_retry_limit() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result: Result<i32> = wrapper.with_retry(|_| {
            calls += 1;
            Err(network_error())
        });
        assert!(result.is_err());
        assert_eq!(calls, 4); // 1 attempt + 3 retries
    }

    #[test]
    fn with_retry_does_not_retry_auth_errors() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result: Result<i32> = wrapper.with_retry(|_| {
            calls += 1;
            Err(auth_error())
        });
        assert!(result.is_err());
        assert_eq!(calls, 1);
    }

    #[test]
    fn with_retry_propagates_logic_errors_immediately() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result: Result<i32> = wrapper.with_retry(|_| {
            calls += 1;
            Err(Error::StateMachineLogicError("boom".into()))
        });
        assert!(matches!(result, Err(Error::StateMachineLogicError(_))));
        assert_eq!(calls, 1);
    }

    // The auth-recovery path itself can't be exercised here without a real
    // backend (it calls `check_authorization_status` against the mock); these
    // tests cover the paths that don't trigger recovery.

    #[test]
    fn with_auth_recovery_succeeds_first_try() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result = wrapper.with_auth_recovery(|_| {
            calls += 1;
            Ok::<_, Error>("ok")
        });
        assert_eq!(result.unwrap(), "ok");
        assert_eq!(calls, 1);
    }

    #[test]
    fn with_auth_recovery_retries_network_errors() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result = wrapper.with_auth_recovery(|_| {
            calls += 1;
            if calls <= 1 {
                Err(network_error())
            } else {
                Ok("ok")
            }
        });
        assert_eq!(result.unwrap(), "ok");
        assert_eq!(calls, 2);
    }

    #[test]
    fn with_auth_recovery_propagates_logic_errors_immediately() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let mut calls = 0;
        let result: Result<i32> = wrapper.with_auth_recovery(|_| {
            calls += 1;
            Err(Error::StateMachineLogicError("boom".into()))
        });
        assert!(matches!(result, Err(Error::StateMachineLogicError(_))));
        assert_eq!(calls, 1);
    }
}
