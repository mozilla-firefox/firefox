/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.LocationListUpdateState
import mozilla.components.feature.ipprotection.store.state.LocationState

internal object ServiceReducer {
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.ServiceAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.ServiceAction.ServiceStateChanged -> {
                val newPendingActivationRequest =
                    state.newPendingActivationRequest(
                        newProxyStatus = state.proxyStatus,
                        newServiceState = action.serviceState,
                    )
                val newAccountStatus = state.newAccountStatus(action.serviceState)
                val newLocationState = state.newLocationState(action.serviceState)

                state.copy(
                    serviceStatus = action.serviceState,
                    accountState = state.accountState.copy(status = newAccountStatus),
                    pendingActivationRequest = newPendingActivationRequest,
                    locationState = newLocationState,
                )
            }
        }
}

private fun IPProtectionState.newAccountStatus(newServiceState: ServiceState): AccountStatus {
    // Apart from the first enrollment where the user goes through the enrollment process,
    // we rely on the service state to be the source of truth for entitlement.
    // UNLESS the user is signed out: we could still intermittently get a ServiceState.Ready
    // report before the engine updates itself with the new account status.
    return if (newServiceState == ServiceState.Ready && accountState.status != AccountStatus.NoAccount) {
        AccountStatus.EnrolledAndEntitled
    } else {
        accountState.status
    }
}

private fun IPProtectionState.newLocationState(newServiceState: ServiceState): LocationState {
    return if (
        newServiceState == ServiceState.Ready && locationState.updateState == LocationListUpdateState.NotRequested
    ) {
        locationState.copy(updateState = LocationListUpdateState.Requested)
    } else {
        locationState
    }
}
