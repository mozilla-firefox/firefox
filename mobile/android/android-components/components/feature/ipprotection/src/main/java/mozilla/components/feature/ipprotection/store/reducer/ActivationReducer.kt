/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.PendingActivationRequest
import mozilla.components.feature.ipprotection.store.state.ProxyActivation
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.feature.ipprotection.store.state.isActivationInFlight

internal object ActivationReducer {
    @Suppress("CognitiveComplexMethod", "LongMethod", "ForbiddenSuppress")
    // FIXME(IPP) break this up into more smaller parts only if the changes are meaningful.
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.ActivationAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.ActivationAction.Toggle -> {
                when (state.serviceStatus) {
                    ServiceState.Unavailable,
                    ServiceState.Uninitialized -> {
                        return state
                    }

                    ServiceState.Ready -> {
                        return when (state.proxyStatus) {
                            Authorized.Idle -> {
                                state.copy(
                                    pendingActivationRequest =
                                        PendingActivationRequest.Activate(
                                            selectedLocationCode = state.locationState.selectedLocation.countryCode
                                        )
                                )
                            }

                            Authorized.ConnectionError,
                            Authorized.Active -> {
                                state.copy(pendingActivationRequest = PendingActivationRequest.Deactivate)
                            }

                            Authorized.Activating,
                            Authorized.DataLimitReached,
                            Uninitialized -> state
                        }
                    }

                    ServiceState.Unauthenticated -> {
                        val status = state.accountState.status

                        // We need to authenticate first because we haven't done so before or
                        // our account is in a wonky state.
                        val requiresAuthentication =
                            status == AccountStatus.NeedsAuthentication ||
                                status == AccountStatus.Uninitialized ||
                                status == AccountStatus.WarmingUp ||
                                status == AccountStatus.NoAccount

                        if (requiresAuthentication) {
                            return state.copy(
                                accountState = state.accountState.copy(status = AccountStatus.RequestingAuthentication)
                            )
                        }

                        // We have an account in good standing, but we haven't enrolled the service before,
                        // so we need to authorize the service first to get the account ready to request
                        // enrollment keys.
                        if (status == AccountStatus.NeedsAuthorization) {
                            return state.copy(
                                accountState = state.accountState.copy(status = AccountStatus.RequestingAuthorization)
                            )
                        }

                        // It is a bit of an edge case, but if we hit a toggle action while the account
                        // check is still in progress, we do want to move forward with authorization flow.
                        //
                        // An account check can be triggered, that will move the state into either entitled
                        // or needs authorization state. But if the check is taking longer, then the toggle
                        // action should move the state into requesting auth anyway.
                        //
                        // Ideally, we want to have an explicit state transition path for an account check;
                        // for now, that is what we ship with.
                        if (status == AccountStatus.TryAgain) {
                            return state.copy(
                                accountState = state.accountState.copy(status = AccountStatus.RequestingAuthorization)
                            )
                        }

                        if (status == AccountStatus.Authenticated) {
                            throw IllegalStateException("VPN state machine is in a bad state")
                        }
                    }
                }

                state
            }

            is IPProtectionAction.ActivationAction.ProxyActivationShown -> {
                state.copy(proxyActivation = ProxyActivation.Idle)
            }

            is IPProtectionAction.ActivationAction.ToggleFailed -> {
                // There could be a race condition where a signed-in user is able to start the vpn auth flow
                // while their account manager is still in "warming up" state (e.g. it's still updating fxa
                // token after those expire). In that case, the user might finish auth flow in "entitled"
                // account state, but ip service was never informed about an eligible account.
                val accountState =
                    if (
                        state.accountState.status == AccountStatus.EnrolledAndEntitled &&
                            state.serviceStatus == ServiceState.Unauthenticated
                    ) {
                        state.accountState.copy(status = AccountStatus.TryAgain)
                    } else {
                        state.accountState
                    }

                // Reset `activate` so the next Toggle reads as a fresh edge in observeToggle().
                state.copy(pendingActivationRequest = null, accountState = accountState)
            }

            is IPProtectionAction.ActivationAction.ActivationRequestCompleted -> {
                // Only clear the request this reply is for. A different one may have been queued in the meantime.
                if (state.pendingActivationRequest == action.request) {
                    state.copy(pendingActivationRequest = null)
                } else {
                    state
                }
            }
        }
}

private fun IPProtectionState.maybeDeactivate(): PendingActivationRequest.Deactivate? {
    if (isActivationInFlight) {
        return PendingActivationRequest.Deactivate
    }

    return when (proxyStatus) {
        Authorized.Active,
        Authorized.ConnectionError,
        Authorized.DataLimitReached -> PendingActivationRequest.Deactivate
        else -> null
    }
}

internal fun IPProtectionState.newPendingActivationRequest(
    newProxyStatus: ProxyStatus,
    newServiceState: ServiceState,
): PendingActivationRequest? {
    // Clear `activate` once the engine settles so a re-request reads as a new transition.
    return when (newServiceState) {
        ServiceState.Uninitialized -> null

        ServiceState.Unavailable,
        ServiceState.Unauthenticated -> maybeDeactivate()

        ServiceState.Ready ->
            when (newProxyStatus) {
                Authorized.Activating -> pendingActivationRequest
                else -> null
            }
    }
}
