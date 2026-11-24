/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{Error, FxaState, Result};

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

/// Attaches a target [`FxaState`] to a `Result<T, Error>` so transitions can
/// land somewhere specific on failure with `?`.
///
/// Programming errors (logic / invalid-transition) bypass `target` and are
/// wrapped as [`StateMachineErr::Fatal`] so the driver propagates them.
///
/// ```ignore
/// account.complete_oauth_flow(&code, &state)
///     .to_state_machine_err(|| initial_state)?;
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

#[cfg(test)]
mod tests {
    use super::*;
    use viaduct::ViaductError;

    fn network_error() -> Error {
        Error::RequestError(ViaductError::NetworkError("test".into()))
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
}
