/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.PendingActivationRequest
import mozilla.components.feature.ipprotection.store.state.ProxyActivation

internal object AccountReducer {
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.AccountAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.AccountAction.AccountStateChanged -> {
                state.copy(accountState = state.accountState.copy(status = action.state))
            }

            is IPProtectionAction.AccountAction.CheckAccount -> {
                if (state.accountState.status == AccountStatus.NeedsAuthorization) {
                    // When we "try again" we signal to the IPProtectionHandler to attempt retrieving an access
                    // token. If that request fails, we catch the exception and return back into a
                    // `NeedsAuthorization` state.
                    state.copy(accountState = state.accountState.copy(status = AccountStatus.TryAgain))
                } else {
                    state
                }
            }

            is IPProtectionAction.AccountAction.AccountManagerStateChanged -> {
                // Only the AccountManager should only change the states that put the
                // account into a "ready-to-use" state. The remaining are part of
                // AccountStatus that represents the combined requirements for the
                // account and the IP protection service, and those are moved into
                // from other parts of the system.
                //
                // To avoid potential conflicts, we limit which states this action
                // can perform.
                when (action.status) {
                    AccountStatus.RequestingAuthentication,
                    AccountStatus.RequestingAuthorization,
                    AccountStatus.TryAgain,
                    AccountStatus.AwaitingAuthentication,
                    AccountStatus.AwaitingAuthorization,
                    AccountStatus.AwaitingEnrollment,
                    AccountStatus.EnrolledAndEntitled -> state

                    AccountStatus.Uninitialized,
                    AccountStatus.NeedsAuthentication,
                    AccountStatus.NeedsAuthorization,
                    AccountStatus.Authenticated -> {
                        state.copy(accountState = state.accountState.copy(status = action.status))
                    }

                    AccountStatus.WarmingUp -> {
                        if (state.accountState.status == AccountStatus.Uninitialized) {
                            state.copy(accountState = state.accountState.copy(status = action.status))
                        } else {
                            state
                        }
                    }

                    AccountStatus.AuthFailed -> {
                        state.copy(accountState = state.accountState.copy(status = AccountStatus.NeedsAuthentication))
                    }

                    AccountStatus.NoAccount -> state.clearProfileData(action)
                }
            }

            is IPProtectionAction.AccountAction.AccountReadyForEnrollment -> {
                state.copy(accountState = state.accountState.copy(status = AccountStatus.AwaitingEnrollment))
            }

            // Do nothing while we wait for our pending authentication to change.
            is IPProtectionAction.AccountAction.AwaitingAuth ->
                state.copy(accountState = state.accountState.copy(status = action.status))

            // The auth UI flow has finished; if the status is still "awaiting", we roll back into
            // the "requires auth" states. Otherwise, the status moved into enrollment phase, which
            // is handled elsewhere.
            is IPProtectionAction.AccountAction.FinishingAuthFlow -> {
                val newAccountStatus =
                    when (state.accountState.status) {
                        AccountStatus.AwaitingAuthentication,
                        AccountStatus.WarmingUp,
                        AccountStatus.Uninitialized,
                        AccountStatus.NoAccount -> {
                            AccountStatus.NeedsAuthentication
                        }

                        AccountStatus.AwaitingAuthorization -> {
                            AccountStatus.NeedsAuthorization
                        }

                        else -> state.accountState.status
                    }
                return state.copy(accountState = state.accountState.copy(status = newAccountStatus))
            }

            is IPProtectionAction.AccountAction.FinishingEnrollment -> state.handleFinishingEnrollment(action)
        }
}

private fun IPProtectionState.clearProfileData(
    action: IPProtectionAction.AccountAction.AccountManagerStateChanged
): IPProtectionState {
    return copy(
        usageQuota = null,
        proxyActivation = ProxyActivation.Idle,
        pendingActivationRequest = PendingActivationRequest.Deactivate,
        accountState = accountState.copy(status = action.status),
        locationState = LocationState(),
    )
}

private fun IPProtectionState.handleFinishingEnrollment(
    action: IPProtectionAction.AccountAction.FinishingEnrollment
): IPProtectionState {
    return if (action.success) {
        copy(
            accountState = accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            pendingActivationRequest =
                PendingActivationRequest.Activate(selectedLocationCode = locationState.selectedLocation.countryCode),
        )
    } else {
        copy(accountState = accountState.copy(status = AccountStatus.NeedsAuthorization))
    }
}
