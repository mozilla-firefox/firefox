/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.store

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo.Companion.PROXY_STATE_ACTIVATING
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo.Companion.PROXY_STATE_ACTIVE
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo.Companion.PROXY_STATE_ERROR
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo.Companion.PROXY_STATE_PAUSED
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo.Companion.PROXY_STATE_READY
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.buildIPProtectionState
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.ProxyActivation
import mozilla.components.feature.ipprotection.store.state.Recommended
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertThrows
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class IPProtectionReducerTest {

    @Test
    fun `WHEN EligibilityChanged is dispatched THEN eligibilityStatus is updated`() {
        val state = buildIPProtectionState()
        assertEquals(
            state.copy(eligibilityStatus = EligibilityStatus.Eligible),
            iPProtectionReducer(
                state,
                IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible),
            ),
        )
    }

    @Test
    fun `WHEN ToolkitStateChanged is dispatched THEN data fields are updated`() {
        val state = buildIPProtectionState()
        val info =
            StateInfo(
                serviceState = ServiceState.Uninitialized,
                remaining = 1000L,
                max = 5000L,
                resetTime = "2026-06-01T00:00:00Z",
            )
        assertEquals(
            state.copy(
                remainingDataBytes = 1000L,
                maxDataBytes = 5000L,
                resetDate = "2026-06-01T00:00:00Z",
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN service is ready and proxy state is ready THEN proxyStatus is Idle`() {
        val state = buildIPProtectionState()
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)
        assertEquals(
            state.copy(
                serviceStatus = ServiceState.Ready,
                proxyStatus = Authorized.Idle,
                accountState = state.accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN service is ready and proxy state is activating THEN proxyStatus is Activating`() {
        val state = buildIPProtectionState()
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ACTIVATING)
        assertEquals(
            state.copy(
                serviceStatus = ServiceState.Ready,
                proxyStatus = Authorized.Activating,
                accountState = state.accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN service is ready and proxy state is active THEN proxyStatus is Active`() {
        val state = buildIPProtectionState()
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ACTIVE)
        assertEquals(
            state.copy(
                serviceStatus = ServiceState.Ready,
                proxyStatus = Authorized.Active,
                proxyActivation = ProxyActivation.TurningOn,
                accountState = state.accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN service is ready and proxy state is paused THEN proxyStatus is DataLimitReached`() {
        val state = buildIPProtectionState()
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_PAUSED)
        assertEquals(
            state.copy(
                serviceStatus = ServiceState.Ready,
                proxyStatus = Authorized.DataLimitReached,
                accountState = state.accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN service is ready and proxy state is error THEN proxyStatus is ConnectionError`() {
        val state = buildIPProtectionState()
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ERROR)
        assertEquals(
            state.copy(
                serviceStatus = ServiceState.Ready,
                proxyStatus = Authorized.ConnectionError,
                accountState = state.accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN ToggleFailed is dispatched THEN activate is cleared`() {
        val state = buildIPProtectionState().copy(activate = true)
        assertEquals(
            state.copy(activate = null),
            iPProtectionReducer(state, IPProtectionAction.ToggleFailed()),
        )
    }

    @Test
    fun `GIVEN user has already finished auth flow successfully but service is still unauthenticated WHEN ToggleFailed is dispatched THEN activate is cleared and account is set for another check`() {
        val state =
            buildIPProtectionState()
                .copy(
                    activate = true,
                    accountState = AccountState(status = AccountStatus.EnrolledAndEntitled),
                    serviceStatus = ServiceState.Unauthenticated,
                )
        assertEquals(
            state.copy(activate = null, accountState = state.accountState.copy(status = AccountStatus.TryAgain)),
            iPProtectionReducer(state, IPProtectionAction.ToggleFailed()),
        )
    }

    @Test
    fun `GIVEN user is entitled and service is ready WHEN ToggleFailed is dispatched THEN activate is cleared and account does not do extra checks`() {
        val state =
            buildIPProtectionState()
                .copy(
                    activate = true,
                    accountState = AccountState(status = AccountStatus.EnrolledAndEntitled),
                    serviceStatus = ServiceState.Ready,
                )
        assertEquals(
            state.copy(activate = null, accountState = state.accountState),
            iPProtectionReducer(state, IPProtectionAction.ToggleFailed()),
        )
    }

    @Test
    fun `WHEN ProxyActivationShown is dispatched from TurningOn THEN proxyActivation is Idle`() {
        val state = buildIPProtectionState().copy(proxyActivation = ProxyActivation.TurningOn)
        assertEquals(
            state.copy(proxyActivation = ProxyActivation.Idle),
            iPProtectionReducer(state, IPProtectionAction.ProxyActivationShown),
        )
    }

    @Test
    fun `WHEN ProxyActivationShown is dispatched from TurningOff THEN proxyActivation is Idle`() {
        val state = buildIPProtectionState().copy(proxyActivation = ProxyActivation.TurningOff)
        assertEquals(
            state.copy(proxyActivation = ProxyActivation.Idle),
            iPProtectionReducer(state, IPProtectionAction.ProxyActivationShown),
        )
    }

    @Test
    fun `WHEN proxy transitions into active from idle THEN proxyActivation is TurningOn`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Idle)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ACTIVE)
        assertEquals(
            state.copy(
                serviceStatus = ServiceState.Ready,
                proxyStatus = Authorized.Active,
                proxyActivation = ProxyActivation.TurningOn,
                accountState = state.accountState.copy(status = AccountStatus.EnrolledAndEntitled),
            ),
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)),
        )
    }

    @Test
    fun `WHEN proxy transitions into active from a connection error THEN proxyActivation is TurningOn`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.ConnectionError)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ACTIVE)
        assertEquals(
            ProxyActivation.TurningOn,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN proxy stays active THEN a pending proxyActivation is left untouched`() {
        val state =
            buildIPProtectionState(proxyStatus = Authorized.Active).copy(proxyActivation = ProxyActivation.TurningOn)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ACTIVE)
        assertEquals(
            ProxyActivation.TurningOn,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN proxy goes from active to activating THEN a pending proxyActivation is preserved`() {
        // A reconnect hop shouldn't drop a not-yet-shown "on" animation.
        val state =
            buildIPProtectionState(proxyStatus = Authorized.Active).copy(proxyActivation = ProxyActivation.TurningOn)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ACTIVATING)
        assertEquals(
            ProxyActivation.TurningOn,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN engine settles to ready while a pending activate was queued THEN activate is cleared`() {
        val state =
            buildIPProtectionState(
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Authorized.Activating,
                )
                .copy(activate = true)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)
        val nextState = iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info))
        assertEquals(
            null,
            nextState.activate,
        )
    }

    @Test
    fun `WHEN proxy becomes idle after being active THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Active)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)
        assertEquals(
            ProxyActivation.TurningOff,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN proxy becomes idle after a connection error THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.ConnectionError)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)
        assertEquals(
            ProxyActivation.TurningOff,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN proxy hits the data limit after being active THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Active)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_PAUSED)
        assertEquals(
            ProxyActivation.TurningOff,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN proxy hits the data limit after a connection error THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.ConnectionError)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_PAUSED)
        assertEquals(
            ProxyActivation.TurningOff,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `WHEN proxy becomes idle after activating THEN proxyActivation stays Idle`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Activating)
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)
        assertEquals(
            ProxyActivation.Idle,
            iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info)).proxyActivation,
        )
    }

    @Test
    fun `GIVEN AccountStatus is AwaitingAuthentication WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthentication`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingAuthentication)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is AwaitingAuthorization WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthorization`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingAuthorization)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthorization, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is AwaitingEnrollment WHEN FinishingAuthFlow is dispatched THEN AccountStatus does not change`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingEnrollment)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingAuthFlow)

        assertEquals(AccountStatus.AwaitingEnrollment, resultState.accountState.status)
    }

    @Test
    fun `GIVEN successful enrollment WHEN FinishingEnrollment is dispatched THEN user is entitled to use the feature and the feature starts activation`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingEnrollment)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingEnrollment(true))

        assertEquals(AccountStatus.EnrolledAndEntitled, resultState.accountState.status)
        assertEquals(true, resultState.activate)
    }

    @Test
    fun `GIVEN unsuccessful enrollment WHEN FinishingEnrollment is dispatched THEN user has to authorize again`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingEnrollment)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingEnrollment(false))

        assertEquals(AccountStatus.NeedsAuthorization, resultState.accountState.status)
    }

    @Test
    fun `WHEN AccountManagerStateChanged to Uninitialized is dispatched THEN data and proxy flags are reset to defaults`() {
        val dirtyState =
            buildIPProtectionState(
                    accountStatus = AccountStatus.EnrolledAndEntitled,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Authorized.Active,
                )
                .copy(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    remainingDataBytes = 1000L,
                    maxDataBytes = 5000L,
                    resetDate = "2026-06-01T00:00:00Z",
                    proxyActivation = ProxyActivation.TurningOn,
                    activate = true,
                )

        val resultState =
            iPProtectionReducer(
                dirtyState,
                InternalAction.AccountManagerStateChanged(AccountStatus.NoAccount),
            )

        assertEquals(
            dirtyState.copy(
                remainingDataBytes = -1L,
                maxDataBytes = -1L,
                resetDate = null,
                proxyActivation = ProxyActivation.Idle,
                activate = false,
                accountState = AccountState(AccountStatus.NoAccount),
            ),
            resultState,
        )
    }

    @Test
    fun `WHEN Toggle is dispatched while the service cannot run THEN the state is left untouched`() {
        // There is nothing to turn on or off until the service reaches a usable state.
        listOf(ServiceState.OptedOut, ServiceState.Unavailable, ServiceState.Uninitialized).forEach { serviceStatus ->
            val state = buildIPProtectionState(serviceStatus = serviceStatus)

            assertEquals(
                "Toggle should be a no-op for $serviceStatus",
                state,
                iPProtectionReducer(state, IPProtectionAction.Toggle),
            )
        }
    }

    @Test
    fun `WHEN Toggle is dispatched while the proxy is idle THEN activation is requested`() {
        val state = buildIPProtectionState(serviceStatus = ServiceState.Ready, proxyStatus = Authorized.Idle)

        val resultState = iPProtectionReducer(state, IPProtectionAction.Toggle)

        assertEquals(true, resultState.activate)
    }

    @Test
    fun `WHEN Toggle is dispatched while the proxy is active or errored THEN deactivation is requested`() {
        listOf(Authorized.Active, Authorized.ConnectionError).forEach { proxyStatus ->
            val state = buildIPProtectionState(serviceStatus = ServiceState.Ready, proxyStatus = proxyStatus)

            val resultState = iPProtectionReducer(state, IPProtectionAction.Toggle)

            assertEquals("Toggle should turn off a $proxyStatus proxy", false, resultState.activate)
        }
    }

    @Test
    fun `WHEN Toggle is dispatched mid-transition or at the data limit THEN the request is ignored`() {
        // Activating is already settling and DataLimitReached cannot be re-enabled by the user, so toggling is a no-op.
        listOf(Authorized.Activating, Authorized.DataLimitReached, Uninitialized).forEach { proxyStatus ->
            val state = buildIPProtectionState(serviceStatus = ServiceState.Ready, proxyStatus = proxyStatus)

            assertEquals(
                "Toggle should be a no-op for a $proxyStatus proxy",
                state,
                iPProtectionReducer(state, IPProtectionAction.Toggle),
            )
        }
    }

    @Test
    fun `WHEN Toggle is dispatched while unauthenticated and the account is not in good standing THEN authentication is requested`() {
        // The user must sign in before the proxy can start.
        listOf(
                AccountStatus.NeedsAuthentication,
                AccountStatus.Uninitialized,
                AccountStatus.WarmingUp,
                AccountStatus.NoAccount,
            )
            .forEach { accountStatus ->
                val state =
                    buildIPProtectionState(
                        accountStatus = accountStatus,
                        serviceStatus = ServiceState.Unauthenticated,
                    )

                val resultState = iPProtectionReducer(state, IPProtectionAction.Toggle)

                assertEquals(
                    "Toggle from $accountStatus should request authentication",
                    AccountStatus.RequestingAuthentication,
                    resultState.accountState.status,
                )
            }
    }

    @Test
    fun `WHEN Toggle is dispatched while unauthenticated and the account only needs authorization THEN authorization is requested`() {
        // The account itself is valid, but the VPN scope still needs to be authorized.
        val state =
            buildIPProtectionState(
                accountStatus = AccountStatus.NeedsAuthorization,
                serviceStatus = ServiceState.Unauthenticated,
            )

        val resultState = iPProtectionReducer(state, IPProtectionAction.Toggle)

        assertEquals(AccountStatus.RequestingAuthorization, resultState.accountState.status)
    }

    @Test
    fun `WHEN Toggle is dispatched while unauthenticated and the account check is in progress THEN authorization is still requested`() {
        // The account itself is valid, but we don't know yet if the VPN scope needs to be authorized.
        val state =
            buildIPProtectionState(
                accountStatus = AccountStatus.TryAgain,
                serviceStatus = ServiceState.Unauthenticated,
            )

        val resultState = iPProtectionReducer(state, IPProtectionAction.Toggle)

        assertEquals(AccountStatus.RequestingAuthorization, resultState.accountState.status)
    }

    @Test
    fun `WHEN Toggle is dispatched while unauthenticated but the account is already authenticated THEN the invalid state is rejected`() {
        // An authenticated account with an unauthenticated service is a contradiction the state machine cannot resolve.
        val state =
            buildIPProtectionState(
                accountStatus = AccountStatus.Authenticated,
                serviceStatus = ServiceState.Unauthenticated,
            )

        assertThrows(IllegalStateException::class.java) {
            iPProtectionReducer(state, IPProtectionAction.Toggle)
        }
    }

    @Test
    fun `WHEN AccountStateChanged is dispatched THEN the account status reflects the external signal`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                IPProtectionAction.AccountStateChanged(AccountStatus.Authenticated),
            )

        assertEquals(AccountStatus.Authenticated, resultState.accountState.status)
    }

    @Test
    fun `WHEN the engine reports it cannot authenticate THEN a queued activation is dropped`() {
        // The engine cannot honour the pending activation, so the request must not linger.
        listOf(ServiceState.Unauthenticated, ServiceState.OptedOut, ServiceState.Unavailable).forEach { serviceStatus ->
            val state = buildIPProtectionState().copy(activate = true)
            val info = StateInfo(serviceState = serviceStatus, proxyState = PROXY_STATE_READY)

            val resultState = iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info))

            assertEquals("Pending activation should be dropped for $serviceStatus", false, resultState.activate)
        }
    }

    @Test
    fun `WHEN the engine resets to uninitialized THEN a queued activation is cleared so a later request reads as new`() {
        val state = buildIPProtectionState().copy(activate = true)
        val info = StateInfo(serviceState = ServiceState.Uninitialized)

        val resultState = iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info))

        assertEquals(null, resultState.activate)
    }

    @Test
    fun `WHEN the engine is not ready THEN the existing account status is preserved`() {
        // Entitlement is only asserted once the service reports Ready, so other states leave it alone.
        val state = buildIPProtectionState(accountStatus = AccountStatus.NeedsAuthentication)
        val info = StateInfo(serviceState = ServiceState.Unauthenticated)

        val resultState = iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info))

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN the user is signed in WHEN the service reports Ready THEN entitlement is short-circuited`() {
        val state =
            buildIPProtectionState(
                accountStatus = AccountStatus.Authenticated,
                serviceStatus = ServiceState.Unauthenticated,
            )
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)

        val resultState = iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info))

        assertEquals(AccountStatus.EnrolledAndEntitled, resultState.accountState.status)
    }

    @Test
    fun `GIVEN the user is signed out WHEN the service reports Ready THEN entitlement is not short-circuited`() {
        val state =
            buildIPProtectionState(
                accountStatus = AccountStatus.NoAccount,
                serviceStatus = ServiceState.Unauthenticated,
            )
        // A stale Ready update can intermittently arrive right after sign-out, before the engine
        // reflects the new account status
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_READY)

        val resultState = iPProtectionReducer(state, IPProtectionAction.EngineStateChanged(info))

        assertEquals(AccountStatus.NoAccount, resultState.accountState.status)
    }

    @Test
    fun `WHEN the engine reports an error message THEN it is surfaced as the last error`() {
        val info = StateInfo(serviceState = ServiceState.Ready, proxyState = PROXY_STATE_ERROR, lastError = "boom")

        val resultState = iPProtectionReducer(buildIPProtectionState(), IPProtectionAction.EngineStateChanged(info))

        assertEquals("boom", resultState.lastError)
    }

    @Test
    fun `WHEN the engine reports an unrecognized proxy state THEN the proxy is treated as uninitialized`() {
        val info = StateInfo(serviceState = ServiceState.Ready)

        val resultState = iPProtectionReducer(buildIPProtectionState(), IPProtectionAction.EngineStateChanged(info))

        assertEquals(Uninitialized, resultState.proxyStatus)
    }

    @Test
    fun `WHEN AccountManagerStateChanged reports an in-flight auth state THEN the status is left to the auth flow`() {
        // Only the auth flow itself may advance these; the account manager must not clobber an in-flight UI state.
        listOf(
                AccountStatus.RequestingAuthentication,
                AccountStatus.RequestingAuthorization,
                AccountStatus.TryAgain,
                AccountStatus.AwaitingAuthentication,
                AccountStatus.AwaitingAuthorization,
                AccountStatus.AwaitingEnrollment,
            )
            .forEach { incomingStatus ->
                val state = buildIPProtectionState(accountStatus = AccountStatus.WarmingUp)

                val resultState =
                    iPProtectionReducer(
                        state,
                        InternalAction.AccountManagerStateChanged(incomingStatus),
                    )

                assertEquals(
                    "In-flight status $incomingStatus should be ignored",
                    AccountStatus.WarmingUp,
                    resultState.accountState.status,
                )
            }
    }

    @Test
    fun `WHEN AccountManagerStateChanged reports an authoritative account standing THEN it is applied`() {
        // These statuses reflect the account manager's authoritative view of the account.
        listOf(
                AccountStatus.WarmingUp,
                AccountStatus.NoAccount,
                AccountStatus.NeedsAuthentication,
                AccountStatus.NeedsAuthorization,
                AccountStatus.Authenticated,
            )
            .forEach { incomingStatus ->
                val resultState =
                    iPProtectionReducer(
                        buildIPProtectionState(),
                        InternalAction.AccountManagerStateChanged(incomingStatus),
                    )

                assertEquals(
                    "Status $incomingStatus should be applied directly",
                    incomingStatus,
                    resultState.accountState.status,
                )
            }
    }

    @Test
    fun `WHEN AccountManagerStateChanged reports AuthFailed THEN the user is rolled back to needing authentication`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                InternalAction.AccountManagerStateChanged(AccountStatus.AuthFailed),
            )

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `WHEN internal EligibilityChanged is dispatched THEN eligibilityStatus is updated`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                InternalAction.EligibilityChanged(EligibilityStatus.Eligible),
            )

        assertEquals(EligibilityStatus.Eligible, resultState.eligibilityStatus)
    }

    @Test
    fun `WHEN AccountReadyForEnrollment is dispatched THEN the account moves into enrollment`() {
        val resultState = iPProtectionReducer(buildIPProtectionState(), InternalAction.AccountReadyForEnrollment)

        assertEquals(AccountStatus.AwaitingEnrollment, resultState.accountState.status)
    }

    @Test
    fun `WHEN UpdateServiceState is dispatched THEN serviceStatus records the snapshot`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                InternalAction.UpdateServiceState(ServiceState.Ready),
            )

        assertEquals(ServiceState.Ready, resultState.serviceStatus)
    }

    @Test
    fun `WHEN AwaitingAuth is dispatched THEN the account is parked at the given intermediary status`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                InternalAction.AwaitingAuth(AccountStatus.AwaitingAuthentication),
            )

        assertEquals(AccountStatus.AwaitingAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is WarmingUp WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthentication`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.WarmingUp)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is Uninitialized WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthentication`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.Uninitialized)

        val resultState = iPProtectionReducer(initialState, InternalAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `WHEN CountryListChanged is dispatched THEN countries are added to location list and recommended option is preserved`() {
        val initialState = buildIPProtectionState()
        val countries =
            listOf(
                IPProtectionHandler.Country(code = "DK", available = true),
                IPProtectionHandler.Country(code = "FR", available = true),
                IPProtectionHandler.Country(code = "GB", available = false),
                IPProtectionHandler.Country(code = "US", available = true),
            )

        assertEquals(LocationState(), initialState.locationState)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.CountryListChanged(countries),
            )

        countries.forEach { country ->
            assertNotNull(resultState.locationState.locations.find { it.countryCode == country.code })
        }

        assert(resultState.locationState.locations.contains(Recommended))
    }

    @Test
    fun `WHEN CountrySelected is dispatched THEN user selected country is updated`() {
        val updatedLocation = Country("JP", available = true)
        val initialState = buildIPProtectionState()

        assertEquals(Recommended, initialState.locationState.selectedLocation)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationChanged(updatedLocation),
            )

        assertEquals(updatedLocation, resultState.locationState.selectedLocation)
    }

    @Test
    fun `GIVEN an active proxy connection WHEN when user changes the location THEN the feature activates the selected location`() {
        val updatedLocation = Country("JP", available = true)
        val initialState = buildIPProtectionState(serviceStatus = ServiceState.Ready, proxyStatus = Authorized.Active)

        assertEquals(Recommended, initialState.locationState.selectedLocation)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationChanged(updatedLocation),
            )

        assertEquals(updatedLocation, resultState.locationState.selectedLocation)
        assertEquals(true, resultState.activate)
    }

    @Test
    fun `GIVEN no active proxy connection WHEN when user changes the location THEN the feature does not activate the selected location`() {
        val updatedLocation = Country("JP", available = true)
        val initialState = buildIPProtectionState(serviceStatus = ServiceState.Ready, proxyStatus = Authorized.Idle)

        assertEquals(Recommended, initialState.locationState.selectedLocation)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationChanged(updatedLocation),
            )

        assertEquals(updatedLocation, resultState.locationState.selectedLocation)
        assertEquals(null, resultState.activate)
    }
}
