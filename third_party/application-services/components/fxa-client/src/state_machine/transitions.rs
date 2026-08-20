/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! FxA state machine transition function.
//!
//! Each `match` arm reads top-to-bottom as imperative Rust. Use
//! `.to_state_machine_err(|| target)?` to attach the landing state on failure.

use crate::{Error, FxaEvent, FxaRustAuthState, FxaState};

use super::helpers::{ResultExt, RetryingAccount, StateMachineErr};

/// Transition the FSM from `from` given `event`.
pub fn transition(
    account: &mut RetryingAccount<'_>,
    from: FxaState,
    event: FxaEvent,
) -> std::result::Result<FxaState, StateMachineErr> {
    use FxaEvent as E;
    use FxaState as S;

    match (from, event) {
        // ── From Uninitialized ──────────────────────────────────────────
        (S::Uninitialized, E::Initialize { device_config }) => match account.get_auth_state() {
            FxaRustAuthState::Disconnected => Ok(S::Disconnected),
            FxaRustAuthState::AuthIssues => Ok(S::AuthIssues),
            FxaRustAuthState::Connected => {
                match account.finish_initialize(&device_config.capabilities) {
                    Ok(()) => Ok(S::Connected),
                    Err(cause) => Err(StateMachineErr::new(cause, S::AuthIssues)),
                }
            }
        },

        // ── From Disconnected ───────────────────────────────────────────
        (
            S::Disconnected,
            E::BeginOAuthFlow {
                service,
                scopes,
                entrypoint,
            },
        ) => {
            let scope_refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
            let oauth_url = account
                .begin_oauth_flow(&service, &scope_refs, &entrypoint)
                .to_state_machine_err(|| S::Disconnected)?;
            Ok(S::Authenticating {
                oauth_url,
                initial_state: FxaRustAuthState::Disconnected,
            })
        }
        (
            S::Disconnected,
            E::BeginPairingFlow {
                pairing_url,
                service,
                scopes,
                entrypoint,
            },
        ) => {
            let scope_refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
            let oauth_url = account
                .begin_pairing_flow(&pairing_url, &service, &scope_refs, &entrypoint)
                .to_state_machine_err(|| S::Disconnected)?;
            Ok(S::Authenticating {
                oauth_url,
                initial_state: FxaRustAuthState::Disconnected,
            })
        }

        // ── From Authenticating ─────────────────────────────────────────
        (S::Authenticating { initial_state, .. }, E::CompleteOAuthFlow { code, state }) => {
            account
                .complete_oauth_flow(&code, &state)
                .to_state_machine_err(|| initial_state.into())?;

            // Initial state was Connected: device is already initialized, skip the call.
            if !matches!(initial_state, FxaRustAuthState::Connected) {
                let dc = account.device_config().clone();
                account
                    .initialize_device(&dc.name, dc.device_type, &dc.capabilities)
                    .to_state_machine_err(|| FxaState::Disconnected)?;
            }

            Ok(S::Connected)
        }
        (S::Authenticating { initial_state, .. }, E::CancelOAuthFlow) => Ok(initial_state.into()),
        (S::Authenticating { .. }, E::Disconnect) => {
            account.disconnect();
            Ok(S::Disconnected)
        }
        (
            S::Authenticating { initial_state, .. },
            E::BeginOAuthFlow {
                service,
                scopes,
                entrypoint,
            },
        ) => {
            let scope_refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
            let oauth_url = account
                .begin_oauth_flow(&service, &scope_refs, &entrypoint)
                .to_state_machine_err(|| initial_state.into())?;
            Ok(S::Authenticating {
                oauth_url,
                initial_state,
            })
        }
        (
            S::Authenticating { initial_state, .. },
            E::BeginPairingFlow {
                pairing_url,
                service,
                scopes,
                entrypoint,
            },
        ) => {
            let scope_refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
            let oauth_url = account
                .begin_pairing_flow(&pairing_url, &service, &scope_refs, &entrypoint)
                .to_state_machine_err(|| initial_state.into())?;
            Ok(S::Authenticating {
                oauth_url,
                initial_state,
            })
        }
        // A WebChannel password change while an OAuth flow is in progress
        // is a no-op; let the flow finish. Should be rare in practice.
        (s @ S::Authenticating { .. }, E::WebChannelPasswordChange { .. }) => {
            crate::warn!("WebChannel password change received while Authenticating; ignoring");
            Ok(s)
        }

        // ── From Connected ──────────────────────────────────────────────
        (S::Connected, E::Disconnect) => {
            account.disconnect();
            Ok(S::Disconnected)
        }
        (S::Connected, E::CheckAuthorizationStatus) => {
            // check_authorization_status() failing should leave us in the original state.
            let active = account
                .check_authorization_status()
                .to_state_machine_err(|| S::Connected)?;
            Ok(if active { S::Connected } else { S::AuthIssues })
        }
        (S::Connected, E::CallGetProfile) => {
            account
                .get_profile()
                .to_state_machine_err(|| S::AuthIssues)?;
            Ok(S::Connected)
        }
        (
            S::Connected,
            E::BeginOAuthFlow {
                service,
                scopes,
                entrypoint,
            },
        ) => {
            // OAuth flow from a connected user (e.g. authorizing additional scopes).
            // Lands back at Connected after CompleteOAuthFlow since the device is
            // already initialized.
            let scope_refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
            let oauth_url = account
                .begin_oauth_flow(&service, &scope_refs, &entrypoint)
                .to_state_machine_err(|| S::Connected)?;
            Ok(S::Authenticating {
                oauth_url,
                initial_state: FxaRustAuthState::Connected,
            })
        }
        (S::Connected, E::WebChannelPasswordChange { json_payload }) => {
            // The inner call swaps the session token for a new refresh token and re-registers
            // the device record (push subscription, commands, etc) against the new token.
            account
                .handle_web_channel_password_change(&json_payload)
                .to_state_machine_err(|| S::AuthIssues)?;
            Ok(S::Connected)
        }

        // ── From AuthIssues ─────────────────────────────────────────────
        (
            S::AuthIssues,
            E::BeginOAuthFlow {
                service,
                scopes,
                entrypoint,
            },
        ) => {
            let scope_refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
            let oauth_url = account
                .begin_oauth_flow(&service, &scope_refs, &entrypoint)
                .to_state_machine_err(|| S::AuthIssues)?;
            Ok(S::Authenticating {
                oauth_url,
                initial_state: FxaRustAuthState::AuthIssues,
            })
        }
        (S::AuthIssues, E::Disconnect) => {
            account.disconnect();
            Ok(S::Disconnected)
        }
        (S::AuthIssues, E::WebChannelPasswordChange { json_payload }) => {
            // A concurrent sync/401 may have pushed us here before the webchannel ran. The new
            // session token recovers us; device re-registration will be handled inside the inner call.
            account
                .handle_web_channel_password_change(&json_payload)
                .to_state_machine_err(|| S::AuthIssues)?;
            Ok(S::Connected)
        }
        (S::AuthIssues, E::CheckAuthorizationStatus) => {
            let active = account
                .check_authorization_status()
                .to_state_machine_err(|| S::AuthIssues)?;
            Ok(if active { S::Connected } else { S::AuthIssues })
        }

        // ── Other transitions  ─────────────────────────────────
        (from_state, E::CheckAuthorizationStatus) => {
            // Ignore `CheckAuthorizationStatus` from other states.
            // We want the app to be able to send this event whenever they want,
            // without generating an error.
            // If we're in a state where we can't run a check, then just ignore it.
            error_support::debug!("Ignoring `CheckAuthorizationStatus` from {from_state:?}");
            Ok(from_state)
        }
        (S::Disconnected, FxaEvent::Disconnect) => {
            // Ignore Disconnect from the Disconnected state.
            //
            // This makes it idempotent and safe to send from any state.
            // https://bugzilla.mozilla.org/show_bug.cgi?id=2061224
            Ok(S::Disconnected)
        }

        // ── Invalid (state, event) pair ─────────────────────────────────
        (state, event) => Err(StateMachineErr::Fatal(Box::new(
            Error::InvalidStateTransition(format!("{state} -> {event}")),
        ))),
    }
}

#[cfg(test)]
mod tests {
    //! Tests for the I/O-free transition arms (cancel-oauth, invalid combos).
    use super::*;
    use crate::{DeviceConfig, DeviceType};

    fn mock_account() -> crate::internal::FirefoxAccount {
        use crate::internal::config::Config;
        crate::internal::FirefoxAccount::with_config(
            Config::new_with_mock_well_known_fxa_client_configuration(
                "https://mock-fxa.example.com",
                "12345678",
                "https://foo.bar",
            ),
        )
    }

    fn authenticating_from(initial_state: FxaRustAuthState) -> FxaState {
        FxaState::Authenticating {
            oauth_url: "https://example.com/oauth".to_owned(),
            initial_state,
        }
    }

    #[test]
    fn cancel_oauth_returns_to_initial_state() {
        nss_as::ensure_initialized();
        for (initial, expected) in [
            (FxaRustAuthState::Disconnected, FxaState::Disconnected),
            (FxaRustAuthState::AuthIssues, FxaState::AuthIssues),
            (FxaRustAuthState::Connected, FxaState::Connected),
        ] {
            let mut account = mock_account();
            let mut wrapper = RetryingAccount::new(&mut account);
            let result = transition(
                &mut wrapper,
                authenticating_from(initial),
                FxaEvent::CancelOAuthFlow,
            );
            assert_eq!(result.unwrap(), expected);
        }
    }

    fn assert_fatal_invalid_transition(result: std::result::Result<FxaState, StateMachineErr>) {
        match result {
            Err(StateMachineErr::Fatal(cause)) => {
                assert!(matches!(*cause, Error::InvalidStateTransition(_)));
            }
            Err(StateMachineErr::Handled { .. }) => {
                panic!("expected Fatal(InvalidStateTransition), got Handled")
            }
            Ok(s) => panic!("expected InvalidStateTransition, got Ok({s:?})"),
        }
    }

    #[test]
    fn invalid_state_event_pair_returns_fatal_invalid_state_transition() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let device_config = DeviceConfig {
            name: "test-device".to_owned(),
            device_type: DeviceType::Mobile,
            capabilities: vec![],
        };
        let result = transition(
            &mut wrapper,
            FxaState::Connected,
            FxaEvent::Initialize { device_config },
        );
        assert_fatal_invalid_transition(result);
    }

    #[test]
    fn uninitialized_disconnect_invalid() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let result = transition(&mut wrapper, FxaState::Uninitialized, FxaEvent::Disconnect);
        assert_fatal_invalid_transition(result);
    }

    #[test]
    fn disconnect_is_idempotent() {
        nss_as::ensure_initialized();
        // `Disconnect` should be valid from all states and always result in the user being
        // disconnected.
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);

        assert_eq!(
            transition(&mut wrapper, FxaState::Connected, FxaEvent::Disconnect).unwrap(),
            FxaState::Disconnected
        );

        assert_eq!(
            transition(&mut wrapper, FxaState::AuthIssues, FxaEvent::Disconnect).unwrap(),
            FxaState::Disconnected
        );

        assert_eq!(
            transition(
                &mut wrapper,
                FxaState::Authenticating {
                    oauth_url: "test".into(),
                    initial_state: FxaRustAuthState::Disconnected
                },
                FxaEvent::Disconnect
            )
            .unwrap(),
            FxaState::Disconnected
        );

        assert_eq!(
            transition(&mut wrapper, FxaState::Disconnected, FxaEvent::Disconnect).unwrap(),
            FxaState::Disconnected
        );
    }

    fn assert_handled_lands_at(
        result: std::result::Result<FxaState, StateMachineErr>,
        expected: FxaState,
    ) {
        match result {
            Err(StateMachineErr::Handled { target, .. }) => assert_eq!(target, expected),
            Err(StateMachineErr::Fatal(cause)) => panic!("expected Handled, got Fatal({cause:?})"),
            Ok(s) => panic!("expected Handled, got Ok({s:?})"),
        }
    }

    #[test]
    fn connected_web_channel_password_change_is_valid_transition() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let result = transition(
            &mut wrapper,
            FxaState::Connected,
            FxaEvent::WebChannelPasswordChange {
                json_payload: "{}".to_owned(),
            },
        );
        assert_handled_lands_at(result, FxaState::AuthIssues);
    }

    #[test]
    fn auth_issues_web_channel_password_change_is_valid_transition() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let result = transition(
            &mut wrapper,
            FxaState::AuthIssues,
            FxaEvent::WebChannelPasswordChange {
                json_payload: "{}".to_owned(),
            },
        );
        assert_handled_lands_at(result, FxaState::AuthIssues);
    }

    #[test]
    fn authenticating_web_channel_password_change_stays_in_authenticating() {
        nss_as::ensure_initialized();
        let mut account = mock_account();
        let mut wrapper = RetryingAccount::new(&mut account);
        let from = authenticating_from(FxaRustAuthState::Connected);
        let result = transition(
            &mut wrapper,
            from.clone(),
            FxaEvent::WebChannelPasswordChange {
                json_payload: "{}".to_owned(),
            },
        );
        assert_eq!(result.unwrap(), from);
    }
}
