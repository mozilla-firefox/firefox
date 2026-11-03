/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.storage

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.LocationListUpdateState
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.PendingActivationRequest
import mozilla.components.feature.ipprotection.store.state.ProxyActivation
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.support.test.middleware.CaptureActionsMiddleware

internal class FakeEligibilityStorage : IPProtectionEligibilityStorage {
    private val statusFlow = MutableSharedFlow<EligibilityStatus>(replay = 1)

    var initCalled = false
        private set

    override val eligibilityStatus: Flow<EligibilityStatus> = statusFlow

    override fun init() {
        initCalled = true
    }

    fun emit(status: EligibilityStatus) {
        statusFlow.tryEmit(status)
    }
}

internal typealias IPProtectionTestMiddleware = CaptureActionsMiddleware<IPProtectionState, IPProtectionAction>

internal fun buildStore(
    initialState: IPProtectionState = buildIPProtectionState()
): Pair<IPProtectionStore, IPProtectionTestMiddleware> {
    val middleware = IPProtectionTestMiddleware()
    val store = IPProtectionStore(initialState = initialState, middleware = listOf(middleware))
    return store to middleware
}

internal fun buildIPProtectionState(
    accountStatus: AccountStatus = AccountStatus.Authenticated,
    serviceStatus: ServiceState = ServiceState.Uninitialized,
    proxyStatus: ProxyStatus = Uninitialized,
    eligibilityStatus: EligibilityStatus = EligibilityStatus.Unknown,
    updateState: LocationListUpdateState = LocationListUpdateState.NotRequested,
    pendingActivationRequest: PendingActivationRequest? = null,
    proxyActivation: ProxyActivation = ProxyActivation.Idle,
): IPProtectionState {
    return IPProtectionState(
        accountState = AccountState(accountStatus),
        serviceStatus = serviceStatus,
        proxyStatus = proxyStatus,
        eligibilityStatus = eligibilityStatus,
        locationState = LocationState(updateState = updateState),
        proxyActivation = proxyActivation,
        pendingActivationRequest = pendingActivationRequest,
    )
}
