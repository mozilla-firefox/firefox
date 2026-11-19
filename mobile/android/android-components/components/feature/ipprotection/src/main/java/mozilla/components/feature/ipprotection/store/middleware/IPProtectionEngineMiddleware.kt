/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.middleware

import kotlin.coroutines.resume
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.ActivationOperation
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.ProxyRequest
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * [Middleware] that sends the proxy requests to [IPProtectionHandler], one at a time, and reports the running request
 * as [IPProtectionState.proxyRequest].
 *
 * @param scope [CoroutineScope] on which the requests run.
 */
class IPProtectionEngineMiddleware(
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
) : Middleware<IPProtectionState, IPProtectionAction> {

    internal var handler: IPProtectionHandler? = null

    private val requestLock = Mutex()
    private val jobs = mutableSetOf<Job>()
    private var latestRequest: ProxyRequest? = null
    private var latestJob: Job? = null

    private val unfinishedRequest: ProxyRequest?
        get() = latestRequest.takeIf { latestJob?.isActive == true }

    private var isProxyActive = false
    private var connectedCountryCode: String? = null

    override fun invoke(
        store: Store<IPProtectionState, IPProtectionAction>,
        next: (IPProtectionAction) -> Unit,
        action: IPProtectionAction,
    ) {
        val before = store.state
        next(action)

        when (action) {
            is IPProtectionAction.ActivationAction.Toggle -> toggle(store)

            is IPProtectionAction.LocationAction.LocationChanged -> locationChanged(store, action.location.countryCode)

            is IPProtectionAction.AccountAction.FinishingEnrollment -> {
                if (action.success) {
                    run(store, ProxyRequest.Activate(store.state.locationState.selectedLocation.countryCode))
                }
            }

            is IPProtectionAction.AccountAction.AccountManagerStateChanged -> {
                if (action.status == AccountStatus.NoAccount && unfinishedRequest != ProxyRequest.Deactivate) {
                    cancelAndDeactivate(store)
                }
            }

            is IPProtectionAction.ServiceAction.ServiceStateChanged -> serviceStateChanged(store, before)

            is IPProtectionAction.ProxyAction.ProxyStateChanged -> {
                if (store.state.proxyStatus != Authorized.Active && store.state.proxyStatus != Authorized.Activating) {
                    isProxyActive = false
                    connectedCountryCode = null
                }

                if (store.state.serviceStatus.isUnavailableOrUnauthenticated()) {
                    deactivateIfRunning(store, before)
                }
            }

            is IPProtectionAction.ActivationAction.ToggleFailed -> {
                if (action.operation == ActivationOperation.Deactivate && latestRequest == ProxyRequest.Deactivate) {
                    latestRequest = null
                }
            }

            is IPProtectionAction.EligibilityAction.EligibilityChanged,
            is IPProtectionAction.UsageAction.UsageChanged,
            is IPProtectionAction.AccountAction.AccountStateChanged,
            is IPProtectionAction.AccountAction.CheckAccount,
            is IPProtectionAction.AccountAction.AccountReadyForEnrollment,
            is IPProtectionAction.AccountAction.FinishingAuthFlow,
            is IPProtectionAction.AccountAction.AwaitingAuth,
            is IPProtectionAction.ActivationAction.ProxyActivationShown,
            is IPProtectionAction.ActivationAction.ProxyRequestStarted,
            is IPProtectionAction.ActivationAction.ProxyRequestFinished,
            is IPProtectionAction.LocationAction.CountryListChanged,
            is IPProtectionAction.LocationAction.LocationReset,
            is IPProtectionAction.LocationAction.PersistedLocationUnavailable,
            is IPProtectionAction.LocationAction.LocationSwitchFailed,
            is IPProtectionAction.LocationAction.LocationUpdateFailed,
            is IPProtectionAction.LocationAction.CheckLocations -> {
                // no-op
            }
        }
    }

    private fun toggle(store: Store<IPProtectionState, IPProtectionAction>) {
        if (store.state.serviceStatus != ServiceState.Ready) {
            return
        }

        when (unfinishedRequest) {
            is ProxyRequest.Activate -> cancelAndDeactivate(store)
            ProxyRequest.Deactivate -> {
                // no-op
            }
            else ->
                when (store.state.proxyStatus) {
                    Authorized.Idle ->
                        run(store, ProxyRequest.Activate(store.state.locationState.selectedLocation.countryCode))
                    Authorized.Activating,
                    Authorized.Active,
                    Authorized.ConnectionError -> run(store, ProxyRequest.Deactivate)
                    else -> {
                        // no-op
                    }
                }
        }
    }

    private fun locationChanged(
        store: Store<IPProtectionState, IPProtectionAction>,
        countryCode: String?,
    ) {
        when (unfinishedRequest) {
            is ProxyRequest.Activate,
            ProxyRequest.Deactivate -> {
                // no-op
            }
            else ->
                if (store.state.proxyStatus == Authorized.Active) {
                    run(store, ProxyRequest.SwitchLocation(countryCode))
                }
        }
    }

    private fun serviceStateChanged(
        store: Store<IPProtectionState, IPProtectionAction>,
        before: IPProtectionState,
    ) {
        when (store.state.serviceStatus) {
            ServiceState.Uninitialized -> {
                cancelAll()
                isProxyActive = false
                connectedCountryCode = null
            }
            ServiceState.Unavailable,
            ServiceState.Unauthenticated -> deactivateIfRunning(store, before)
            ServiceState.Ready -> {
                // no-op
            }
        }
    }

    private fun deactivateIfRunning(
        store: Store<IPProtectionState, IPProtectionAction>,
        before: IPProtectionState,
    ) {
        val unfinishedRequest = unfinishedRequest
        val wasRunning =
            unfinishedRequest is ProxyRequest.Activate ||
                unfinishedRequest is ProxyRequest.SwitchLocation ||
                before.proxyStatus == Authorized.Activating ||
                before.proxyStatus == Authorized.Active ||
                before.proxyStatus == Authorized.ConnectionError ||
                before.proxyStatus == Authorized.DataLimitReached

        if (wasRunning && latestRequest != ProxyRequest.Deactivate) {
            cancelAndDeactivate(store)
        }
    }

    private fun cancelAndDeactivate(store: Store<IPProtectionState, IPProtectionAction>) {
        cancelAll()
        run(store, ProxyRequest.Deactivate)
    }

    private fun cancelAll() {
        jobs.toList().forEach { it.cancel() }
    }

    private fun run(
        store: Store<IPProtectionState, IPProtectionAction>,
        request: ProxyRequest,
    ) {
        jobs.removeAll { it.isCompleted }
        val job = scope.launch {
            requestLock.withLock {
                if (request !is ProxyRequest.SwitchLocation) {
                    store.dispatch(IPProtectionAction.ActivationAction.ProxyRequestStarted(request))
                }
                try {
                    send(store, request)
                } finally {
                    store.dispatch(IPProtectionAction.ActivationAction.ProxyRequestFinished)
                }
            }
        }

        jobs += job
        latestRequest = request
        latestJob = job
    }

    private suspend fun send(
        store: Store<IPProtectionState, IPProtectionAction>,
        request: ProxyRequest,
    ) {
        val handler = handler ?: return

        when (request) {
            is ProxyRequest.Activate -> {
                val error = handler.activateAwait(request.countryCode)
                if (error != null) {
                    store.dispatch(
                        IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate, error)
                    )
                    return
                }

                isProxyActive = true
                connectedCountryCode = request.countryCode
                followSelectedLocation(store, handler)
            }

            is ProxyRequest.SwitchLocation -> {
                if (store.state.proxyStatus != Authorized.Active) {
                    return
                }

                if (
                    isProxyActive ||
                        switchLocation(store, handler, store.state.locationState.selectedLocation.countryCode)
                ) {
                    followSelectedLocation(store, handler)
                }
            }

            is ProxyRequest.Deactivate -> {
                val error = handler.deactivateAwait()
                if (error != null) {
                    store.dispatch(
                        IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Deactivate, error)
                    )
                    followSelectedLocation(store, handler)
                    return
                }

                isProxyActive = false
            }
        }
    }

    private suspend fun followSelectedLocation(
        store: Store<IPProtectionState, IPProtectionAction>,
        handler: IPProtectionHandler,
    ) {
        while (isProxyActive) {
            val selectedCountryCode = store.state.locationState.selectedLocation.countryCode
            if (selectedCountryCode == connectedCountryCode || !switchLocation(store, handler, selectedCountryCode)) {
                return
            }
        }
    }

    private suspend fun switchLocation(
        store: Store<IPProtectionState, IPProtectionAction>,
        handler: IPProtectionHandler,
        countryCode: String?,
    ): Boolean {
        store.dispatch(
            IPProtectionAction.ActivationAction.ProxyRequestStarted(ProxyRequest.SwitchLocation(countryCode))
        )
        val error = handler.activateAwait(countryCode)
        if (error != null) {
            isProxyActive = false
            store.dispatch(IPProtectionAction.LocationAction.LocationSwitchFailed(error))
            return false
        }

        isProxyActive = true
        connectedCountryCode = countryCode
        return true
    }
}

private suspend fun IPProtectionHandler.activateAwait(countryCode: String?): Throwable? =
    suspendCancellableCoroutine { continuation ->
        activate(countryCode) { error -> continuation.resume(error) }
    }

private suspend fun IPProtectionHandler.deactivateAwait(): Throwable? = suspendCancellableCoroutine { continuation ->
    deactivate { error -> continuation.resume(error) }
}

private fun ServiceState.isUnavailableOrUnauthenticated() =
    this == ServiceState.Unavailable || this == ServiceState.Unauthenticated
