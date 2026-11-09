/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.ProxyActivation
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Uninitialized

internal object ProxyReducer {
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.ProxyAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.ProxyAction.ProxyStateChanged -> {
                val newProxyStatus = action.proxyState.asProxyStatus()
                val newProxyActivation = state.newProxyActivation(newProxyStatus)

                state.copy(
                    proxyStatus = newProxyStatus,
                    proxyActivation = newProxyActivation,
                    lastError = action.lastError,
                )
            }
        }
}

private fun IPProtectionState.newProxyActivation(newProxyStatus: ProxyStatus): ProxyActivation {
    // Whether the proxy was already active before this update.
    // UNLESS it's in ConnectionError: that's a temporary network problem, not a real "off"
    // state, so we still treat it as active for this check.
    val wasActive = proxyStatus == Authorized.Active || proxyStatus == Authorized.ConnectionError

    // Whether the new status means the proxy just stopped being active after going Idle
    // or reaching its data limit.
    val stoppedBeingActive = newProxyStatus == Authorized.Idle || newProxyStatus == Authorized.DataLimitReached

    // Whether the proxy just turned on when transitioning into Active from a non-active state.
    val hasActivated = newProxyStatus == Authorized.Active && proxyStatus != Authorized.Active

    // Whether the proxy just turned off after being active (or in a temporary error) and then stopping.
    val hasDeactivated = wasActive && stoppedBeingActive

    return when {
        hasActivated -> ProxyActivation.TurningOn
        hasDeactivated -> ProxyActivation.TurningOff
        else -> proxyActivation
    }
}

private fun ProxyState.asProxyStatus(): ProxyStatus {
    return when (this) {
        ProxyState.UNKNOWN,
        ProxyState.NOT_READY -> Uninitialized
        ProxyState.READY -> Authorized.Idle
        ProxyState.ACTIVATING -> Authorized.Activating
        ProxyState.ACTIVE -> Authorized.Active
        ProxyState.PAUSED -> Authorized.DataLimitReached
        ProxyState.ERROR -> Authorized.ConnectionError
    }
}
