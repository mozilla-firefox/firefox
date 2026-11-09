/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.fxaclient.FxaException
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.ipprotection.IPProtectionDelegate
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.auth.IPProtectionAuthProvider
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow.Companion.SCOPE_IPPROTECTION
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.middleware.IPProtectionEngineMiddleware
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.LocationListUpdateState
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.SCOPE_PROFILE
import mozilla.components.support.base.log.logger.Logger

/**
 * Feature that coordinates the IP protection proxy service. It observes [IPProtectionStore] for eligibility and account
 * state changes and registers with the [Engine]. [IPProtectionEngineMiddleware] sends the proxy requests.
 *
 * See [mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow] and
 * [mozilla.components.feature.ipprotection.storage.IPProtectionStorageSynchronizer] helpers that complement this
 * feature.
 *
 * Call [initialize] once at startup to begin observing. The feature manages its own lifecycle internally and does not
 * need to be stopped.
 *
 * @param store [IPProtectionStore] that holds the feature state.
 * @param engine [Engine] used to register the IP protection delegate and obtain the handler.
 * @param accountManager [FxaAccountManager] used to supply FxA tokens to the proxy service.
 * @param engineMiddleware [IPProtectionEngineMiddleware] given the [IPProtectionHandler] on registration.
 * @param extraAuthProvider Optional [IPProtectionAuthProvider] used to compose extra capabilities.
 * @param mainDispatcher [CoroutineDispatcher] on which state observations and engine calls run.
 */
class IPProtectionFeature(
    private val store: IPProtectionStore,
    private val engine: Engine,
    private val accountManager: FxaAccountManager,
    private val engineMiddleware: IPProtectionEngineMiddleware,
    private val extraAuthProvider: IPProtectionAuthProvider? = null,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) {
    private val logger = Logger("IPP:Feature")
    private val mainScope = CoroutineScope(mainDispatcher)
    private var handler: IPProtectionHandler? = null

    /** Starts observing eligibility and account state. Must be called once at application startup. */
    fun initialize() {
        mainScope.launch {
            observeEligibilityAndService(store, mainDispatcher)
        }
        mainScope.launch {
            observeAccount(store, mainDispatcher)
        }
        mainScope.launch {
            observeLocationUpdates(store, mainDispatcher)
        }
    }

    private fun observeEligibilityAndService(
        store: IPProtectionStore,
        mainDispatcher: CoroutineDispatcher,
    ) {
        store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow
                .map { Triple(it.eligibilityStatus, it.serviceStatus, it.accountState.status) }
                .distinctUntilChanged()
                .collectLatest { (eligibilityStatus, serviceStatus, accountStatus) ->
                    when (eligibilityStatus) {
                        EligibilityStatus.Eligible -> {
                            if (serviceStatus == ServiceState.Uninitialized) {
                                logger.info("Registering and initializing with IPProtectionController.")
                                registerAndInit()
                            }

                            // When the app starts with an already signed-in user, the account state
                            // is likely to be ready faster than the service, so we should notify
                            // a ready account state after initializing the handler.
                            if (
                                serviceStatus == ServiceState.Unauthenticated &&
                                    accountStatus == AccountStatus.Authenticated
                            ) {
                                handler?.notifyAccountStatus(true)
                            }
                        }

                        EligibilityStatus.Ineligible,
                        EligibilityStatus.UnsupportedRegion -> {
                            uninit()
                        }

                        EligibilityStatus.Unknown -> {
                            // no-op, initializing or no region set.
                        }
                    }
                }
        }
    }

    private fun observeAccount(store: IPProtectionStore, mainDispatcher: CoroutineDispatcher) {
        store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow
                .distinctUntilChangedBy { it.accountState.status }
                .collect { state ->
                    when (state.accountState.status) {
                        AccountStatus.AuthFailed,
                        AccountStatus.NoAccount -> {
                            handler?.notifyAccountStatus(false)
                        }

                        AccountStatus.Authenticated -> {
                            handler?.notifyAccountStatus(true)
                        }

                        AccountStatus.AwaitingEnrollment -> {
                            handler?.notifyAccountStatus(true)
                            handler?.enroll { enrollInfo ->
                                store.dispatch(
                                    IPProtectionAction.AccountAction.FinishingEnrollment(
                                        success = enrollInfo.isEnrolledAndEntitled
                                    )
                                )
                            }
                        }

                        AccountStatus.TryAgain -> {
                            handler?.notifyAccountStatus(true)
                        }

                        AccountStatus.Uninitialized,
                        AccountStatus.WarmingUp,
                        AccountStatus.NeedsAuthentication,
                        AccountStatus.RequestingAuthentication,
                        AccountStatus.NeedsAuthorization,
                        AccountStatus.RequestingAuthorization,
                        AccountStatus.AwaitingAuthentication,
                        AccountStatus.AwaitingAuthorization,
                        AccountStatus.EnrolledAndEntitled -> {
                            // no-op when we are in transient states.
                        }
                    }
                }
        }
    }

    private fun observeLocationUpdates(store: IPProtectionStore, mainDispatcher: CoroutineDispatcher) {
        store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow
                .map { it.serviceStatus to it.locationState.updateState }
                .distinctUntilChanged()
                .filter { (service, update) ->
                    service == ServiceState.Ready && update == LocationListUpdateState.Requested
                }
                .collect {
                    handler?.updateCountryList { error ->
                        if (error != null) {
                            store.dispatch(IPProtectionAction.LocationAction.LocationUpdateFailed(error))
                        }
                    }
                }
        }
    }

    private suspend fun registerAndInit() =
        withContext(mainDispatcher) {
            handler =
                engine.registerIPProtectionDelegate(
                    object : IPProtectionDelegate {
                        override fun onServiceStateChanged(serviceState: ServiceState) {
                            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(serviceState))
                        }

                        override fun onProxyStateChanged(
                            proxyState: ProxyState,
                            lastError: String?,
                        ) {
                            store.dispatch(
                                IPProtectionAction.ProxyAction.ProxyStateChanged(
                                    proxyState = proxyState,
                                    lastError = lastError,
                                )
                            )
                        }

                        override fun onUsageChanged(
                            remaining: Long,
                            max: Long,
                            resetTime: String?,
                        ) {
                            store.dispatch(
                                IPProtectionAction.UsageAction.UsageChanged(
                                    remaining = remaining,
                                    max = max,
                                    resetTime = resetTime,
                                )
                            )
                        }

                        override fun onCountryListChanged(countries: List<IPProtectionHandler.Country>) {
                            store.dispatch(IPProtectionAction.LocationAction.CountryListChanged(countries))
                        }
                    }
                )
            engineMiddleware.handler = handler
            handler?.run {
                setAuthProvider(
                    object : IPProtectionHandler.AuthProvider {
                        override fun getToken(onComplete: (String?) -> Unit) {
                            mainScope.launch {
                                try {
                                    val tokenInfo =
                                        accountManager
                                            .authenticatedAccount()
                                            ?.getAccessToken("$SCOPE_PROFILE $SCOPE_IPPROTECTION")
                                    onComplete(tokenInfo?.token)
                                } catch (e: FxaException.Forbidden) {
                                    logger.error(
                                        "We don't have a scope that gives us an token. Moving to needs authorization.",
                                        e,
                                    )
                                    store.dispatch(
                                        IPProtectionAction.AccountAction.AccountStateChanged(
                                            AccountStatus.NeedsAuthorization
                                        )
                                    )
                                    onComplete(null)
                                }
                            }
                        }
                    }
                )
                extraAuthProvider?.configure(this, mainScope)
                // Initialization needs to be done ASAP whether we are using the service or not to avoid start-up
                // delays.
                // We do need to register our listeners first to avoid dropping a message because,
                // as a side effect, the init call triggers `IPProtectionController#onServiceStateChanged`
                // that can trigger the account manager that leads to `AuthProvider#getToken`.
                init()
            }
        }

    private suspend fun uninit() =
        withContext(mainDispatcher) {
            handler?.uninit()
        }
}
