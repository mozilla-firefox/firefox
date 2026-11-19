/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.store.reducer

import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.storage.buildIPProtectionState
import mozilla.components.feature.ipprotection.store.ActivationOperation
import mozilla.components.feature.ipprotection.store.CachedLocationStatus
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.LocationListUpdateState
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.ProxyActivation
import mozilla.components.feature.ipprotection.store.state.ProxyRequest
import mozilla.components.feature.ipprotection.store.state.Recommended
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import org.junit.Assert.assertEquals
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
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible),
            ),
        )
    }

    @Test
    fun `WHEN UsageChanged is dispatched THEN usage quota is metered`() {
        val usageQuota =
            UsageQuota.Metered(
                max = 5000.bytes,
                remaining = 1000.bytes,
                resetTime = Instant.parse("2026-06-01T00:00:00Z"),
            )
        val state = buildIPProtectionState().copy(usageQuota = usageQuota)

        val updatedQuota =
            UsageQuota.Metered(
                max = 10000.bytes,
                remaining = 999.bytes,
                resetTime = Instant.parse("2026-06-01T00:10:00Z"),
            )
        val resultState =
            iPProtectionReducer(
                state,
                IPProtectionAction.UsageAction.UsageChanged(999L, 10000L, "2026-06-01T00:10:00Z"),
            )

        assertEquals(updatedQuota, resultState.usageQuota)
    }

    @Test
    fun `WHEN UsageChanged has negative remaining bytes THEN usage quota is unavailable`() {
        val state = buildIPProtectionState()
        val result =
            iPProtectionReducer(state, IPProtectionAction.UsageAction.UsageChanged(-1L, 5000L, "2026-06-01T00:00:00Z"))
        assertNull(result.usageQuota)
    }

    @Test
    fun `WHEN UsageChanged has 0 remaining bytes THEN usage quota is metered with remaining volume 0`() {
        val state = buildIPProtectionState()
        val result =
            iPProtectionReducer(state, IPProtectionAction.UsageAction.UsageChanged(0L, 5000L, "2026-06-01T00:00:00Z"))
        assertEquals(
            UsageQuota.Metered(
                resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                max = 5000.bytes,
                remaining = DataVolume.Zero,
            ),
            result.usageQuota,
        )
    }

    @Test
    fun `WHEN UsageChanged has 0 max bytes THEN usage quota is unmetered`() {
        val state = buildIPProtectionState()
        val result =
            iPProtectionReducer(state, IPProtectionAction.UsageAction.UsageChanged(1000L, 0L, "2026-06-01T00:00:00Z"))
        assertEquals(
            UsageQuota.Unmetered(resetTime = Instant.parse("2026-06-01T00:00:00Z")),
            result.usageQuota,
        )
    }

    @Test
    fun `WHEN UsageChanged has a non-zero max THEN usage quota preserves the max volume`() {
        val state = buildIPProtectionState()
        val result =
            iPProtectionReducer(
                state,
                IPProtectionAction.UsageAction.UsageChanged(1000L, 6000L, "2026-06-01T00:00:00Z"),
            )
        assertEquals(
            UsageQuota.Metered(
                resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                max = 6000.bytes,
                remaining = 1000.bytes,
            ),
            result.usageQuota,
        )
    }

    @Test
    fun `WHEN UsageChanged has negative max bytes THEN usage quota is unavailable`() {
        val state = buildIPProtectionState()
        val result =
            iPProtectionReducer(state, IPProtectionAction.UsageAction.UsageChanged(1000L, -1L, "2026-06-01T00:00:00Z"))
        assertNull(result.usageQuota)
    }

    @Test
    fun `WHEN ServiceStateChanged or ProxyStateChanged is dispatched THEN data fields are unchanged`() {
        val usageQuota =
            UsageQuota.Metered(
                max = DataVolume(5000),
                remaining = DataVolume(1000),
                resetTime = Instant.parse("2026-06-01T00:00:00Z"),
            )
        val state = buildIPProtectionState().copy(usageQuota = usageQuota)

        val actions =
            ServiceState.entries.map { IPProtectionAction.ServiceAction.ServiceStateChanged(it) } +
                ProxyState.entries.map { IPProtectionAction.ProxyAction.ProxyStateChanged(it) }

        actions.forEach { action ->
            val resultState = iPProtectionReducer(state, action)

            assertEquals(usageQuota, resultState.usageQuota)
        }
    }

    @Test
    fun `WHEN the proxy reports a state THEN the proxy status follows it AND an unknown state reads as uninitialized`() {
        ProxyState.entries.forEach { proxyState ->
            val expected =
                when (proxyState) {
                    ProxyState.NOT_READY,
                    ProxyState.UNKNOWN -> Uninitialized
                    ProxyState.READY -> Authorized.Idle
                    ProxyState.ACTIVATING -> Authorized.Activating
                    ProxyState.ACTIVE -> Authorized.Active
                    ProxyState.PAUSED -> Authorized.DataLimitReached
                    ProxyState.ERROR -> Authorized.ConnectionError
                }

            val resultState =
                iPProtectionReducer(
                    buildIPProtectionState(),
                    IPProtectionAction.ProxyAction.ProxyStateChanged(proxyState),
                )

            assertEquals(
                "$proxyState should map to $expected",
                expected,
                resultState.proxyStatus,
            )
        }
    }

    @Test
    fun `WHEN the proxy reports an error THEN its message becomes the last error AND a later report without an error clears it`() {
        val state = buildIPProtectionState()
        val errorState =
            iPProtectionReducer(
                state,
                IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ERROR, "boom"),
            )

        assertEquals("boom", errorState.lastError)

        val recoveredState =
            iPProtectionReducer(errorState, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.READY))

        assertNull(recoveredState.lastError)
    }

    @Test
    fun `GIVEN a location was picked WHEN switching to it fails THEN the picked location stays selected`() {
        val pickedLocation = Country("CA", available = true)
        val state = buildIPProtectionState().copy(locationState = LocationState(selectedLocation = pickedLocation))

        val result = iPProtectionReducer(state, IPProtectionAction.LocationAction.LocationSwitchFailed())

        assertEquals(pickedLocation, result.locationState.selectedLocation)
    }

    @Test
    fun `GIVEN user has already finished auth flow successfully but service is still unauthenticated WHEN ToggleFailed is dispatched THEN account is set for another check`() {
        val state =
            buildIPProtectionState()
                .copy(
                    accountState = AccountState(status = AccountStatus.EnrolledAndEntitled),
                    serviceStatus = ServiceState.Unauthenticated,
                )
        assertEquals(
            state.copy(accountState = state.accountState.copy(status = AccountStatus.TryAgain)),
            iPProtectionReducer(state, IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate)),
        )
    }

    @Test
    fun `GIVEN user is entitled and service is ready WHEN ToggleFailed is dispatched THEN account does not do extra checks`() {
        val state =
            buildIPProtectionState()
                .copy(
                    accountState = AccountState(status = AccountStatus.EnrolledAndEntitled),
                    serviceStatus = ServiceState.Ready,
                )
        assertEquals(
            state,
            iPProtectionReducer(state, IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate)),
        )
    }

    @Test
    fun `WHEN ProxyActivationShown is dispatched from TurningOn THEN proxyActivation is Idle`() {
        val state = buildIPProtectionState().copy(proxyActivation = ProxyActivation.TurningOn)
        assertEquals(
            state.copy(proxyActivation = ProxyActivation.Idle),
            iPProtectionReducer(state, IPProtectionAction.ActivationAction.ProxyActivationShown),
        )
    }

    @Test
    fun `WHEN ProxyActivationShown is dispatched from TurningOff THEN proxyActivation is Idle`() {
        val state = buildIPProtectionState().copy(proxyActivation = ProxyActivation.TurningOff)
        assertEquals(
            state.copy(proxyActivation = ProxyActivation.Idle),
            iPProtectionReducer(state, IPProtectionAction.ActivationAction.ProxyActivationShown),
        )
    }

    @Test
    fun `WHEN proxy transitions into active from idle THEN proxyActivation is TurningOn`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Idle)

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))

        assertEquals(ProxyActivation.TurningOn, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy transitions into active from a connection error THEN proxyActivation is TurningOn`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.ConnectionError)

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))

        assertEquals(ProxyActivation.TurningOn, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy stays active THEN a pending proxyActivation is preserved`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Active, proxyActivation = ProxyActivation.TurningOn)

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVE))

        assertEquals(ProxyActivation.TurningOn, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy goes from active to activating THEN a pending proxyActivation is preserved`() {
        // A reconnect hop shouldn't drop a not-yet-shown "on" animation.
        val state = buildIPProtectionState(proxyStatus = Authorized.Active, proxyActivation = ProxyActivation.TurningOn)

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.ACTIVATING))

        assertEquals(ProxyActivation.TurningOn, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy becomes idle after being active THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Active)

        val resultState = iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.READY))

        assertEquals(ProxyActivation.TurningOff, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy becomes idle after a connection error THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.ConnectionError)

        val resultState = iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.READY))

        assertEquals(ProxyActivation.TurningOff, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy hits the data limit after being active THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Active)

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.PAUSED))

        assertEquals(ProxyActivation.TurningOff, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy hits the data limit after a connection error THEN proxyActivation is TurningOff`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.ConnectionError)

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.PAUSED))

        assertEquals(ProxyActivation.TurningOff, resultState.proxyActivation)
    }

    @Test
    fun `WHEN proxy becomes idle after activating THEN proxyActivation stays Idle`() {
        val state = buildIPProtectionState(proxyStatus = Authorized.Activating)

        val resultState = iPProtectionReducer(state, IPProtectionAction.ProxyAction.ProxyStateChanged(ProxyState.READY))

        assertEquals(ProxyActivation.Idle, resultState.proxyActivation)
    }

    @Test
    fun `GIVEN AccountStatus is AwaitingAuthentication WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthentication`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingAuthentication)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is AwaitingAuthorization WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthorization`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingAuthorization)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthorization, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is AwaitingEnrollment WHEN FinishingAuthFlow is dispatched THEN AccountStatus does not change`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingEnrollment)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingAuthFlow)

        assertEquals(AccountStatus.AwaitingEnrollment, resultState.accountState.status)
    }

    @Test
    fun `GIVEN successful enrollment WHEN FinishingEnrollment is dispatched THEN user is entitled to use the feature`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingEnrollment)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingEnrollment(true))

        assertEquals(AccountStatus.EnrolledAndEntitled, resultState.accountState.status)
    }

    @Test
    fun `GIVEN unsuccessful enrollment WHEN FinishingEnrollment is dispatched THEN user has to authorize again`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.AwaitingEnrollment)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingEnrollment(false))

        assertEquals(AccountStatus.NeedsAuthorization, resultState.accountState.status)
    }

    @Test
    fun `WHEN AccountManagerStateChanged to NoAccount is dispatched THEN data and proxy flags are reset to defaults`() {
        val dirtyState =
            buildIPProtectionState(
                    accountStatus = AccountStatus.EnrolledAndEntitled,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Authorized.Active,
                )
                .copy(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    usageQuota =
                        UsageQuota.Metered(
                            remaining = DataVolume(1000),
                            max = DataVolume(5000),
                            resetTime = Instant.parseOrNull("2026-06-01T00:00:00Z"),
                        ),
                    proxyActivation = ProxyActivation.TurningOn,
                )

        val resultState =
            iPProtectionReducer(
                dirtyState,
                IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.NoAccount),
            )

        assertEquals(
            dirtyState.copy(
                usageQuota = null,
                proxyActivation = ProxyActivation.Idle,
                accountState = AccountState(AccountStatus.NoAccount),
            ),
            resultState,
        )
    }

    @Test
    fun `WHEN Toggle is dispatched while the service is ready or cannot run THEN the state is left untouched`() {
        // There is nothing for the reducer to turn on or off.
        listOf(ServiceState.Unavailable, ServiceState.Uninitialized, ServiceState.Ready).forEach { serviceStatus ->
            val state = buildIPProtectionState(serviceStatus = serviceStatus)

            assertEquals(
                "Toggle should be a no-op for $serviceStatus",
                state,
                iPProtectionReducer(state, IPProtectionAction.ActivationAction.Toggle),
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

                val resultState = iPProtectionReducer(state, IPProtectionAction.ActivationAction.Toggle)

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

        val resultState = iPProtectionReducer(state, IPProtectionAction.ActivationAction.Toggle)

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

        val resultState = iPProtectionReducer(state, IPProtectionAction.ActivationAction.Toggle)

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
            iPProtectionReducer(state, IPProtectionAction.ActivationAction.Toggle)
        }
    }

    @Test
    fun `WHEN AccountStateChanged is dispatched THEN the account status reflects the external signal`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                IPProtectionAction.AccountAction.AccountStateChanged(AccountStatus.Authenticated),
            )

        assertEquals(AccountStatus.Authenticated, resultState.accountState.status)
    }

    @Test
    fun `WHEN the engine is not ready THEN the existing account status is preserved`() {
        // Entitlement is only asserted once the service reports Ready, so other states leave it alone.
        val state = buildIPProtectionState(accountStatus = AccountStatus.NeedsAuthentication)

        val resultState =
            iPProtectionReducer(
                state,
                IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated),
            )

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN the user is signed in WHEN the service reports Ready THEN entitlement is short-circuited`() {
        val state =
            buildIPProtectionState(
                accountStatus = AccountStatus.Authenticated,
                serviceStatus = ServiceState.Unauthenticated,
            )

        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready))

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
        val resultState =
            iPProtectionReducer(state, IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready))

        assertEquals(AccountStatus.NoAccount, resultState.accountState.status)
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
                        IPProtectionAction.AccountAction.AccountManagerStateChanged(incomingStatus),
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
                AccountStatus.NoAccount,
                AccountStatus.NeedsAuthentication,
                AccountStatus.NeedsAuthorization,
                AccountStatus.Authenticated,
            )
            .forEach { incomingStatus ->
                val resultState =
                    iPProtectionReducer(
                        buildIPProtectionState(),
                        IPProtectionAction.AccountAction.AccountManagerStateChanged(incomingStatus),
                    )

                assertEquals(
                    "Status $incomingStatus should be applied directly",
                    incomingStatus,
                    resultState.accountState.status,
                )
            }
    }

    @Test
    fun `GIVEN any account status WHEN the account manager reports WarmingUp THEN it replaces only Uninitialized`() {
        AccountStatus.entries.forEach { currentStatus ->
            val resultState =
                iPProtectionReducer(
                    buildIPProtectionState(accountStatus = currentStatus),
                    IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.WarmingUp),
                )
            val expected = if (currentStatus == AccountStatus.Uninitialized) AccountStatus.WarmingUp else currentStatus

            assertEquals("From $currentStatus", expected, resultState.accountState.status)
        }
    }

    @Test
    fun `WHEN AccountManagerStateChanged reports AuthFailed THEN the user is rolled back to needing authentication`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.AuthFailed),
            )

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `WHEN AccountReadyForEnrollment is dispatched THEN the account moves into enrollment`() {
        val resultState =
            iPProtectionReducer(buildIPProtectionState(), IPProtectionAction.AccountAction.AccountReadyForEnrollment)

        assertEquals(AccountStatus.AwaitingEnrollment, resultState.accountState.status)
    }

    @Test
    fun `WHEN AwaitingAuth is dispatched THEN the account is parked at the given intermediary status`() {
        val resultState =
            iPProtectionReducer(
                buildIPProtectionState(),
                IPProtectionAction.AccountAction.AwaitingAuth(AccountStatus.AwaitingAuthentication),
            )

        assertEquals(AccountStatus.AwaitingAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is WarmingUp WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthentication`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.WarmingUp)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingAuthFlow)

        assertEquals(AccountStatus.NeedsAuthentication, resultState.accountState.status)
    }

    @Test
    fun `GIVEN AccountStatus is Uninitialized WHEN FinishingAuthFlow is dispatched THEN AccountStatus is NeedsAuthentication`() {
        val initialState = buildIPProtectionState(accountStatus = AccountStatus.Uninitialized)

        val resultState = iPProtectionReducer(initialState, IPProtectionAction.AccountAction.FinishingAuthFlow)

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
                action = IPProtectionAction.LocationAction.CountryListChanged(countries),
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
                action = IPProtectionAction.LocationAction.LocationChanged(updatedLocation, userAction = true),
            )

        assertEquals(updatedLocation, resultState.locationState.selectedLocation)
    }

    @Test
    fun `WHEN a proxy request starts and then finishes THEN it is stored until it finishes`() {
        val request = ProxyRequest.SwitchLocation("JP")
        val initialState = buildIPProtectionState(serviceStatus = ServiceState.Ready, proxyStatus = Authorized.Active)

        val startedState =
            iPProtectionReducer(initialState, IPProtectionAction.ActivationAction.ProxyRequestStarted(request))
        val finishedState = iPProtectionReducer(startedState, IPProtectionAction.ActivationAction.ProxyRequestFinished)

        assertEquals(request, startedState.proxyRequest)
        assertEquals(null, finishedState.proxyRequest)
    }

    @Test
    fun `WHEN the service becomes ready THEN a location list update is requested`() {
        val initialState = buildIPProtectionState()

        assertEquals(LocationListUpdateState.NotRequested, initialState.locationState.updateState)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready),
            )

        assertEquals(LocationListUpdateState.Requested, resultState.locationState.updateState)
    }

    @Test
    fun `WHEN the service is not ready THEN no location list update is requested`() {
        val initialState = buildIPProtectionState()

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated),
            )

        assertEquals(LocationListUpdateState.NotRequested, resultState.locationState.updateState)
    }

    @Test
    fun `WHEN CountryListChanged is dispatched THEN the location list is marked as updated`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.Requested)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationAction.CountryListChanged(emptyList()),
            )

        assertEquals(LocationListUpdateState.Updated, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN an updated location list WHEN another engine state arrives THEN no new update is requested`() {
        val initialState =
            buildIPProtectionState(
                serviceStatus = ServiceState.Ready,
                updateState = LocationListUpdateState.Updated,
            )

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready),
            )

        assertEquals(LocationListUpdateState.Updated, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN an updated location list WHEN the user changes the location THEN the update state is preserved`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.Updated)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action =
                    IPProtectionAction.LocationAction.LocationChanged(
                        Country("JP", available = true),
                        userAction = true,
                    ),
            )

        assertEquals(LocationListUpdateState.Updated, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN an updated location list WHEN the location is reset THEN the update state is preserved`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.Updated)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action =
                    IPProtectionAction.LocationAction.LocationReset(
                        countryCode = "JP",
                        status = CachedLocationStatus.Missing,
                    ),
            )

        assertEquals(LocationListUpdateState.Updated, resultState.locationState.updateState)
    }

    @Test
    fun `WHEN the user signs out THEN the location list update state is reset`() {
        val initialState =
            buildIPProtectionState(
                serviceStatus = ServiceState.Ready,
                updateState = LocationListUpdateState.Updated,
            )

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.AccountAction.AccountManagerStateChanged(AccountStatus.NoAccount),
            )

        assertEquals(LocationListUpdateState.NotRequested, resultState.locationState.updateState)
    }

    @Test
    fun `WHEN LocationUpdateFailed is dispatched THEN the location list update is marked as failed`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.Requested)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationAction.LocationUpdateFailed(RuntimeException("unknown-error")),
            )

        assertEquals(LocationListUpdateState.Failed, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN no pending location list update WHEN LocationUpdateFailed is dispatched THEN the update state is unchanged`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.NotRequested)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationAction.LocationUpdateFailed(RuntimeException("unknown-error")),
            )

        assertEquals(LocationListUpdateState.NotRequested, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN a failed location list update WHEN ServiceStateChanged is dispatched THEN the update is not retried`() {
        val initialState =
            buildIPProtectionState(
                serviceStatus = ServiceState.Ready,
                updateState = LocationListUpdateState.Failed,
            )

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready),
            )

        assertEquals(LocationListUpdateState.Failed, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN a failed location list update WHEN CheckLocations is dispatched THEN the update is retried`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.Failed)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationAction.CheckLocations,
            )

        assertEquals(LocationListUpdateState.Requested, resultState.locationState.updateState)
    }

    @Test
    fun `GIVEN an updated location list WHEN CheckLocations is dispatched THEN no update is requested`() {
        val initialState = buildIPProtectionState(updateState = LocationListUpdateState.Updated)

        val resultState =
            iPProtectionReducer(
                state = initialState,
                action = IPProtectionAction.LocationAction.CheckLocations,
            )

        assertEquals(LocationListUpdateState.Updated, resultState.locationState.updateState)
    }
}

private val Number.bytes: DataVolume
    get() = DataVolume(this.toLong())
