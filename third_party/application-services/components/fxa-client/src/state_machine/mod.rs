/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! FxA state machine
//!
//! This presents a high-level API for logging in, logging out, dealing with authentication token issues, etc.

use error_support::{breadcrumb, convert_log_report_error};

use crate::{internal::FirefoxAccount, Error, FxaEvent, FxaState, Result};

mod display;
mod helpers;
mod transitions;

use helpers::{RetryingAccount, StateMachineErr};

impl FirefoxAccount {
    /// Get the current state
    pub fn get_state(&self) -> FxaState {
        self.auth_state.clone()
    }

    /// Process an event (login, logout, etc).
    ///
    /// On success, returns the new state.
    /// On error, the state will remain the same.
    pub fn process_event(&mut self, event: FxaEvent) -> Result<FxaState> {
        // Must run before transition() — side effects read `device_config`.
        self.handle_state_machine_initialization(&event)?;

        let was_in_auth_issues = matches!(self.auth_state, FxaState::AuthIssues);
        let from_state = self.auth_state.clone();

        breadcrumb!("FxaStateMachine.process_event starting: {event}");

        let mut retrying = RetryingAccount::new(self);
        let new_state = match transitions::transition(&mut retrying, from_state, event) {
            Ok(s) => {
                breadcrumb!("FxaStateMachine.process_event finished (Done({s}))");
                s
            }
            Err(StateMachineErr::Handled { cause, target }) => {
                breadcrumb!("FxaStateMachine.process_event finished (handled -> {target})");
                let _ = convert_log_report_error(*cause);
                target
            }
            Err(StateMachineErr::Fatal(cause)) => {
                breadcrumb!("FxaStateMachine.process_event finished (fatal — state unchanged)");
                return Err(*cause);
            }
        };

        self.auth_state = new_state.clone();
        if !was_in_auth_issues && matches!(new_state, FxaState::AuthIssues) {
            self.on_auth_issues();
        } else if was_in_auth_issues && !matches!(new_state, FxaState::AuthIssues) {
            self.on_recovered_from_auth_issues();
        }
        Ok(new_state)
    }

    /// Seeds `device_config` from the first `Initialize`; rejects other events
    /// before that and a second `Initialize` afterwards.
    fn handle_state_machine_initialization(&mut self, event: &FxaEvent) -> Result<()> {
        match event {
            FxaEvent::Initialize { device_config } => match self.device_config {
                Some(_) => Err(Error::InvalidStateTransition(
                    "Initialize already sent".to_owned(),
                )),
                None => {
                    self.device_config = Some(device_config.clone());
                    Ok(())
                }
            },
            _ => match &self.device_config {
                Some(_) => Ok(()),
                None => Err(Error::InvalidStateTransition(
                    "Initialize not yet sent".to_owned(),
                )),
            },
        }
    }
}

#[cfg(test)]
mod driver_tests {
    //! End-to-end `process_event` tests covering the init gate, fatal errors
    //! leaving state unchanged, and successful transitions committing it.
    use crate::{
        internal::{config::Config, FirefoxAccount},
        DeviceCapability, DeviceConfig, DeviceType, Error, FxaEvent, FxaState,
    };

    fn mock_account() -> FirefoxAccount {
        FirefoxAccount::with_config(Config::new_with_mock_well_known_fxa_client_configuration(
            "https://mock-fxa.example.com",
            "12345678",
            "https://foo.bar",
        ))
    }

    fn device_config() -> DeviceConfig {
        DeviceConfig {
            name: "test-device".to_owned(),
            device_type: DeviceType::Mobile,
            capabilities: vec![DeviceCapability::SendTab],
        }
    }

    #[test]
    fn process_event_initialize_from_uninitialized_lands_at_disconnected() {
        // Fresh mock account has no refresh token, so it lands at Disconnected.
        nss_as::ensure_initialized();
        let mut account = mock_account();
        assert_eq!(account.get_state(), FxaState::Uninitialized);

        let result = account.process_event(FxaEvent::Initialize {
            device_config: device_config(),
        });

        assert_eq!(result.unwrap(), FxaState::Disconnected);
        assert_eq!(account.get_state(), FxaState::Disconnected);
    }

    #[test]
    fn process_event_initialize_twice_returns_err_unchanged_state() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let _ = account
            .process_event(FxaEvent::Initialize {
                device_config: device_config(),
            })
            .unwrap();
        let state_before = account.get_state();

        let result = account.process_event(FxaEvent::Initialize {
            device_config: device_config(),
        });

        match result {
            Err(Error::InvalidStateTransition(_)) => {}
            other => panic!("expected InvalidStateTransition, got {other:?}"),
        }
        assert_eq!(account.get_state(), state_before);
    }

    #[test]
    fn process_event_non_initialize_before_initialize_returns_err_unchanged_state() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        assert_eq!(account.get_state(), FxaState::Uninitialized);

        let result = account.process_event(FxaEvent::Disconnect);

        match result {
            Err(Error::InvalidStateTransition(_)) => {}
            other => panic!("expected InvalidStateTransition, got {other:?}"),
        }
        assert_eq!(account.get_state(), FxaState::Uninitialized);
    }

    #[test]
    fn process_event_invalid_state_event_pair_returns_err_unchanged_state() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let _ = account
            .process_event(FxaEvent::Initialize {
                device_config: device_config(),
            })
            .unwrap();
        assert_eq!(account.get_state(), FxaState::Disconnected);

        let result = account.process_event(FxaEvent::CompleteOAuthFlow {
            code: "test".into(),
            state: "test".into(),
        });

        match result {
            Err(Error::InvalidStateTransition(_)) => {}
            other => panic!("expected InvalidStateTransition, got {other:?}"),
        }
        assert_eq!(account.get_state(), FxaState::Disconnected);
    }

    #[test]
    fn process_event_connected_initialize_returns_err_unchanged_state() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let _ = account
            .process_event(FxaEvent::Initialize {
                device_config: device_config(),
            })
            .unwrap();
        // Force Connected without going through OAuth.
        account.auth_state = FxaState::Connected;

        let result = account.process_event(FxaEvent::Initialize {
            device_config: device_config(),
        });

        match result {
            Err(Error::InvalidStateTransition(_)) => {}
            other => panic!("expected InvalidStateTransition, got {other:?}"),
        }
        assert_eq!(account.get_state(), FxaState::Connected);
    }

    #[test]
    fn process_event_authenticating_cancel_oauth_returns_to_initial_state() {
        // initial_state must round-trip through CancelOAuthFlow.
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let _ = account
            .process_event(FxaEvent::Initialize {
                device_config: device_config(),
            })
            .unwrap();
        account.auth_state = FxaState::Authenticating {
            oauth_url: "https://example.com/oauth".to_owned(),
            initial_state: crate::FxaRustAuthState::AuthIssues,
        };

        let result = account.process_event(FxaEvent::CancelOAuthFlow);

        assert_eq!(result.unwrap(), FxaState::AuthIssues);
        assert_eq!(account.get_state(), FxaState::AuthIssues);
    }
}
