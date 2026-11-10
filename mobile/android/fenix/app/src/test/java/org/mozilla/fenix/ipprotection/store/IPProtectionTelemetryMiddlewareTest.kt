/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.ipprotection.store

import kotlin.reflect.KClass
import kotlin.test.assertNotNull
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.ActivationOperation
import mozilla.components.feature.ipprotection.store.CachedLocationStatus
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Recommended
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class IPProtectionTelemetryMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private var now = 0L
    private val middleware = IPProtectionTelemetryMiddleware(currentTimeInMillis = { now })

    @Test
    fun `GIVEN the authentication flow started WHEN it completes successfully THEN fxAccountFlowCompleted is recorded with the duration`() {
        assertNull(Vpn.fxAccountFlowCompleted.testGetValue())

        val store = createStore(initialStatus = AccountStatus.NeedsAuthentication)

        now = 1_000L
        store.transitionTo(AccountStatus.RequestingAuthentication)
        store.transitionTo(AccountStatus.AwaitingAuthentication)
        now = 3_500L
        store.transitionTo(AccountStatus.Authenticated)

        val events = Vpn.fxAccountFlowCompleted.testGetValue()
        assertNotNull(events)
        assertEquals(1, events.size)
        assertEquals("2500", events.single().extra?.get("duration_ms"))
        assertNull(Vpn.fxAuthorizationFlowCompleted.testGetValue())
    }

    @Test
    fun `GIVEN the authorization flow started WHEN it completes successfully THEN fxAuthorizationFlowCompleted is recorded with the duration`() {
        assertNull(Vpn.fxAuthorizationFlowCompleted.testGetValue())

        val store = createStore(initialStatus = AccountStatus.NeedsAuthorization)

        now = 1_000L
        store.transitionTo(AccountStatus.RequestingAuthorization)
        store.transitionTo(AccountStatus.AwaitingAuthorization)
        now = 2_000L
        store.transitionTo(AccountStatus.Authenticated)

        val events = Vpn.fxAuthorizationFlowCompleted.testGetValue()
        assertNotNull(events)
        assertEquals(1, events.size)
        assertEquals("1000", events.single().extra?.get("duration_ms"))
        assertNull(Vpn.fxAccountFlowCompleted.testGetValue())
    }

    @Test
    fun `GIVEN an auth flow WHEN it is cancelled THEN no telemetry is recorded`() {
        val store = createStore(initialStatus = AccountStatus.AwaitingAuthentication)

        store.transitionTo(AccountStatus.NeedsAuthentication)

        assertNull(Vpn.fxAccountFlowCompleted.testGetValue())
        assertNull(Vpn.fxAuthorizationFlowCompleted.testGetValue())
    }

    @Test
    fun `GIVEN the account status does not change THEN no telemetry is recorded`() {
        val store = createStore(initialStatus = AccountStatus.AwaitingAuthentication)

        store.transitionTo(AccountStatus.AwaitingAuthentication)

        assertNull(Vpn.fxAccountFlowCompleted.testGetValue())
        assertNull(Vpn.fxAuthorizationFlowCompleted.testGetValue())
    }

    @Test
    fun `GIVEN the VPN toggle failed with no or a non-proxy error THEN no error code is recorded`() {
        assertNull(Vpn.errorEncountered.testGetValue())

        val store = createStore(initialStatus = AccountStatus.EnrolledAndEntitled)

        store.dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))
        store.dispatch(
            IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate, RuntimeException("boom"))
        )

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals(2, events.size)
        assertTrue(events.all { it.extra?.get("error_code") == "null" })
    }

    @Test
    fun `GIVEN the location switch failed with no or a non-proxy error THEN no error code is recorded`() {
        assertNull(Vpn.locationSwitchError.testGetValue())

        val store = createStore(initialStatus = AccountStatus.EnrolledAndEntitled)

        store.dispatch(IPProtectionAction.LocationAction.LocationSwitchFailed())
        store.dispatch(IPProtectionAction.LocationAction.LocationSwitchFailed(RuntimeException("It's a null-pointer!")))

        val events = Vpn.locationSwitchError.testGetValue()
        assertNotNull(events)
        assertEquals(2, events.size)
        assertTrue(events.all { it.extra?.get("error_code") == "null" })
    }

    @Test
    fun `GIVEN the location list update failed THEN the error class name is recorded`() {
        assertNull(Vpn.locationUpdateError.testGetValue())

        val store = createStore(initialStatus = AccountStatus.EnrolledAndEntitled)

        store.dispatch(IPProtectionAction.LocationAction.LocationUpdateFailed(RuntimeException("generic-error")))

        val events = Vpn.locationUpdateError.testGetValue()
        assertNotNull(events)
        assertEquals(1, events.size)
        assertEquals("RuntimeException", events.first().extra?.get("error_code"))
    }

    @Test
    fun `GIVEN user has already finished auth flow successfully but service is still unauthenticated WHEN the VPN toggle failed THEN a generic network error telemetry is recorded`() {
        assertNull(Vpn.entitledAccountUnauthenticated.testGetValue())

        val store =
            createStore(
                initialStatus = AccountStatus.EnrolledAndEntitled,
                serviceStatus = ServiceState.Unauthenticated,
            )

        store.dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))

        val events = Vpn.entitledAccountUnauthenticated.testGetValue()
        assertNotNull(events)
        assertEquals(1, events.size)
    }

    @Test
    fun `GIVEN a failed activate THEN the operation is recorded as activate`() {
        val store = createStore(initialStatus = AccountStatus.EnrolledAndEntitled)

        store.dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals("activate", events.single().extra?.get("operation"))
    }

    @Test
    fun `GIVEN a failed deactivate THEN the operation is recorded as deactivate`() {
        val store = createStore(initialStatus = AccountStatus.EnrolledAndEntitled)

        store.dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Deactivate))

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals("deactivate", events.single().extra?.get("operation"))
    }

    @Test
    fun `GIVEN activate and deactivate both fail THEN each event keeps its own operation`() {
        val store = createStore(initialStatus = AccountStatus.EnrolledAndEntitled)

        store.dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))
        store.dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Deactivate))

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals(2, events.size)
        assertEquals(
            listOf("activate", "deactivate"),
            events.map { it.extra?.get("operation") },
        )
    }

    @Test
    fun `GIVEN each service state WHEN the VPN toggle fails THEN the service state is recorded in snake case`() {
        val expected =
            mapOf(
                ServiceState.Uninitialized to "uninitialized",
                ServiceState.Unavailable to "unavailable",
                ServiceState.Unauthenticated to "unauthenticated",
                ServiceState.Ready to "ready",
            )
        assertEquals(
            "Each ServiceState records a service_state value. List it here and in every service_state " +
                "description in the vpn category of app/metrics.yaml.",
            ServiceState.entries.toSet(),
            expected.keys,
        )

        expected.keys.forEach { serviceStatus ->
            createStore(serviceStatus = serviceStatus)
                .dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))
        }

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals(expected.values.toList(), events.map { it.extra?.get("service_state") })
    }

    @Test
    fun `GIVEN each proxy state WHEN the VPN toggle fails THEN the proxy state is recorded in snake case`() {
        val expected =
            mapOf(
                Uninitialized to "uninitialized",
                Authorized.Idle to "idle",
                Authorized.Activating to "activating",
                Authorized.Active to "active",
                Authorized.DataLimitReached to "data_limit_reached",
                Authorized.ConnectionError to "connection_error",
            )
        assertEquals(
            "Each ProxyStatus records a proxy_state value. List it here and in every proxy_state " +
                "description in the vpn category of app/metrics.yaml.",
            ProxyStatus::class.objectInstances(),
            expected.keys,
        )

        expected.keys.forEach { proxyStatus ->
            createStore(proxyStatus = proxyStatus)
                .dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))
        }

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals(expected.values.toList(), events.map { it.extra?.get("proxy_state") })
    }

    @Test
    fun `GIVEN each account state WHEN the VPN toggle fails THEN the account state is recorded in snake case`() {
        val expected =
            mapOf(
                AccountStatus.Uninitialized to "uninitialized",
                AccountStatus.WarmingUp to "warming_up",
                AccountStatus.NoAccount to "no_account",
                AccountStatus.NeedsAuthentication to "needs_authentication",
                AccountStatus.RequestingAuthentication to "requesting_authentication",
                AccountStatus.NeedsAuthorization to "needs_authorization",
                AccountStatus.RequestingAuthorization to "requesting_authorization",
                AccountStatus.AwaitingAuthentication to "awaiting_authentication",
                AccountStatus.AwaitingAuthorization to "awaiting_authorization",
                AccountStatus.AwaitingEnrollment to "awaiting_enrollment",
                AccountStatus.AuthFailed to "auth_failed",
                AccountStatus.Authenticated to "authenticated",
                AccountStatus.EnrolledAndEntitled to "enrolled_and_entitled",
                AccountStatus.TryAgain to "try_again",
            )
        assertEquals(
            "Each AccountStatus records an account_state value. List it here and in every account_state " +
                "description in the vpn category of app/metrics.yaml.",
            AccountStatus.entries.toSet(),
            expected.keys,
        )

        expected.keys.forEach { accountStatus ->
            createStore(initialStatus = accountStatus)
                .dispatch(IPProtectionAction.ActivationAction.ToggleFailed(ActivationOperation.Activate))
        }

        val events = Vpn.errorEncountered.testGetValue()
        assertNotNull(events)
        assertEquals(expected.values.toList(), events.map { it.extra?.get("account_state") })
    }

    @Test
    fun `GIVEN the user picks a location THEN the new country is recorded as a user action`() {
        val store = createStore(selectedLocation = Recommended)

        store.dispatch(
            IPProtectionAction.LocationAction.LocationChanged(
                Country(countryCode = "JP", available = true),
                userAction = true,
            )
        )
        store.dispatch(IPProtectionAction.LocationAction.LocationChanged(Recommended, userAction = true))

        val events = Vpn.locationChanged.testGetValue()
        assertNotNull(events)
        assertEquals(listOf("JP", "recommended"), events.map { it.extra?.get("to") })
        assertEquals(listOf("user_action", "user_action"), events.map { it.extra?.get("operation") })
    }

    @Test
    fun `GIVEN the proxy is active WHEN the user picks a location THEN proxy_state is recorded as active`() {
        val store = createStore(proxyStatus = Authorized.Active)

        store.dispatch(
            IPProtectionAction.LocationAction.LocationChanged(
                Country(countryCode = "JP", available = true),
                userAction = true,
            )
        )

        val extra = Vpn.locationChanged.testGetValue()?.single()?.extra
        assertNotNull(extra)
        assertEquals("active", extra["proxy_state"])
    }

    @Test
    fun `GIVEN a persisted location is restored THEN the operation is recorded as restore`() {
        val store = createStore(selectedLocation = Recommended)

        store.dispatch(
            IPProtectionAction.LocationAction.LocationChanged(
                location = Country(countryCode = "JP", available = true),
                userAction = false,
            )
        )

        val extra = Vpn.locationChanged.testGetValue()?.single()?.extra
        assertNotNull(extra)
        assertEquals("restore", extra["operation"])
        assertEquals("JP", extra["to"])
    }

    @Test
    fun `GIVEN a location switch fails THEN the error is recorded AND no location change is recorded for it`() {
        val store =
            createStore(
                selectedLocation = Country(countryCode = "JP", available = true),
                proxyStatus = Authorized.Active,
            )

        store.dispatch(
            IPProtectionAction.LocationAction.LocationChanged(
                Country(countryCode = "FR", available = true),
                userAction = true,
            )
        )
        store.dispatch(IPProtectionAction.LocationAction.LocationSwitchFailed())

        val events = Vpn.locationChanged.testGetValue()
        assertNotNull(events)
        assertEquals(listOf("FR"), events.map { it.extra?.get("to") })
        assertEquals(listOf("user_action"), events.map { it.extra?.get("operation") })
        assertNotNull(Vpn.locationSwitchError.testGetValue())
    }

    @Test
    fun `GIVEN a location is reset THEN the reset reason is recorded as missing`() {
        createStore().dispatch(IPProtectionAction.LocationAction.LocationReset("JP", CachedLocationStatus.Missing))

        val events = Vpn.locationReset.testGetValue()
        assertNotNull(events)
        assertEquals(listOf("missing"), events.map { it.extra?.get("reason") })
    }

    @Test
    fun `GIVEN a location is reset THEN the dropped country and the list counts are recorded`() {
        val store =
            createStore(
                selectedLocation = Country(countryCode = "JP", available = true),
                locations =
                    listOf(
                        Recommended,
                        Country(countryCode = "CA", available = true),
                        Country(countryCode = "GB", available = false),
                    ),
            )

        store.dispatch(IPProtectionAction.LocationAction.LocationReset("JP", CachedLocationStatus.Missing))

        val extra = Vpn.locationReset.testGetValue()?.single()?.extra
        assertNotNull(extra)
        assertEquals("JP", extra["location"])
        assertEquals("refresh", extra["operation"])
        assertEquals("2", extra["list_size"])
        assertEquals("1", extra["available_count"])
    }

    @Test
    fun `GIVEN a persisted location is unavailable at startup THEN operation is recorded as restore`() {
        val store = createStore(locations = listOf(Recommended, Country(countryCode = "CA", available = true)))

        store.dispatch(
            IPProtectionAction.LocationAction.PersistedLocationUnavailable("JP", CachedLocationStatus.Missing)
        )

        val extra = Vpn.locationReset.testGetValue()?.single()?.extra
        assertNotNull(extra)
        assertEquals("JP", extra["location"])
        assertEquals("missing", extra["reason"])
        assertEquals("restore", extra["operation"])
    }

    @Test
    fun `GIVEN the proxy is active WHEN a location is reset THEN the state at reset time is recorded`() {
        val store =
            createStore(
                selectedLocation = Country(countryCode = "JP", available = true),
                proxyStatus = Authorized.Active,
                serviceStatus = ServiceState.Ready,
                initialStatus = AccountStatus.EnrolledAndEntitled,
            )

        store.dispatch(IPProtectionAction.LocationAction.LocationReset("JP", CachedLocationStatus.Missing))

        val extra = Vpn.locationReset.testGetValue()?.single()?.extra
        assertNotNull(extra)
        assertEquals("active", extra["proxy_state"])
        assertEquals("ready", extra["service_state"])
        assertEquals("enrolled_and_entitled", extra["account_state"])
    }

    private fun createStore(
        initialStatus: AccountStatus = AccountStatus.Uninitialized,
        serviceStatus: ServiceState = ServiceState.Uninitialized,
        proxyStatus: ProxyStatus = Uninitialized,
        selectedLocation: Location = Recommended,
        locations: List<Location> = listOf(Recommended),
    ) =
        IPProtectionStore(
            initialState =
                IPProtectionState(
                    accountState = AccountState(status = initialStatus),
                    serviceStatus = serviceStatus,
                    proxyStatus = proxyStatus,
                    locationState = LocationState(selectedLocation = selectedLocation, locations = locations),
                ),
            middleware = listOf(middleware),
        )

    private fun IPProtectionStore.transitionTo(status: AccountStatus) {
        dispatch(IPProtectionAction.AccountAction.AccountStateChanged(status))
    }

    private fun KClass<out ProxyStatus>.objectInstances(): Set<ProxyStatus> =
        sealedSubclasses.flatMapTo(mutableSetOf()) { subclass ->
            subclass.objectInstance?.let(::setOf)
                ?: if (subclass.isSealed) subclass.objectInstances() else error("$subclass is not an object")
        }
}
