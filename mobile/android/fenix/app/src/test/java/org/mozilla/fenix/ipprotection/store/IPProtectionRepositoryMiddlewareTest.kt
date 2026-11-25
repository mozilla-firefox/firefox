/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionAction.ProxyAction.ProxyStateChanged
import mozilla.components.feature.ipprotection.store.IPProtectionAction.UsageAction.UsageChanged
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class) // ProxyStateChanged
class IPProtectionRepositoryMiddlewareTest {

    private lateinit var fakeRepository: FakeIPProtectionRepository
    private lateinit var middleware: IPProtectionPreferencesMiddleware
    private lateinit var store: IPProtectionStore

    @Before
    fun setUp() {
        fakeRepository = FakeIPProtectionRepository()

        middleware = IPProtectionPreferencesMiddleware(preferences = fakeRepository)
        store = IPProtectionStore(middleware = listOf(middleware))
    }

    @Test
    fun `GIVEN vpn has not been used yet WHEN action is Toggle THEN next is called and hasAlreadyUsedVpn becomes true`() {
        var nextCalled = false

        middleware.invoke(
            store = store,
            next = { nextCalled = true },
            action = IPProtectionAction.ActivationAction.Toggle,
        )

        assertTrue(nextCalled)
        assertTrue(fakeRepository.hasAlreadyUsedVpn)
    }

    @Test
    fun `GIVEN vpn has not been used yet WHEN action is not Toggle THEN next is called and hasAlreadyUsedVpn remains false`() {
        val nonToggleAction = IPProtectionAction.ActivationAction.ProxyActivationShown
        var nextCalled = false

        middleware.invoke(
            store = store,
            next = { nextCalled = true },
            action = nonToggleAction,
        )

        assertTrue(nextCalled)
        assertFalse(fakeRepository.hasAlreadyUsedVpn)
    }

    @Test
    fun `GIVEN vpn is already used WHEN action is Toggle THEN next is called and hasAlreadyUsedVpn remains true`() {
        fakeRepository.hasAlreadyUsedVpn = true
        var nextCalled = false

        middleware.invoke(
            store = store,
            next = { nextCalled = true },
            action = IPProtectionAction.ActivationAction.Toggle,
        )

        assertTrue(nextCalled)
        assertTrue(fakeRepository.hasAlreadyUsedVpn)
    }

    @Test
    fun `GIVEN no saved reset time WHEN usage arrives THEN the reset time is saved, and there is no pending prompt`() {
        store.dispatch(UsageChanged(remaining = 200, max = 2000, resetTime = RESET_TIME.toString()))

        assertEquals(RESET_TIME, fakeRepository.lastUsageCycleResetTime)
        assertFalse(store.state.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a pending reset prompt WHEN the same reset time is reported again THEN the prompt stays pending`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        fakeRepository.lastDataLimitNoticeCycleResetTime = RESET_TIME

        store.dispatch(UsageChanged(remaining = 200, max = 2000, resetTime = NEXT_RESET_TIME.toString()))
        assertTrue(store.state.usageQuotaResetPromptPending)

        store.dispatch(UsageChanged(remaining = 180, max = 2000, resetTime = NEXT_RESET_TIME.toString()))
        assertTrue(store.state.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a saved reset time WHEN the reported reset time moves earlier THEN there is no pending prompt`() {
        fakeRepository.lastUsageCycleResetTime = NEXT_RESET_TIME

        store.dispatch(UsageChanged(remaining = 200, max = 2000, resetTime = RESET_TIME.toString()))

        assertEquals(RESET_TIME, fakeRepository.lastUsageCycleResetTime)
        assertFalse(store.state.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a saved reset time WHEN the reported reset time is in the future THEN there is a pending prompt`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        store.dispatch(UsageChanged(remaining = 200, max = 2000, resetTime = NEXT_RESET_TIME.toString()))

        assertTrue(store.state.usageQuotaResetPromptPending)
        assertTrue(fakeRepository.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a saved pending reset prompt WHEN the same cycle is first reported THEN the prompt is restored`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        fakeRepository.usageQuotaResetPromptPending = true
        assertFalse(store.state.usageQuotaResetPromptPending)

        store.dispatch(UsageChanged(remaining = 200, max = 2000, resetTime = RESET_TIME.toString()))

        assertTrue(store.state.usageQuotaResetPromptPending)
        assertTrue(fakeRepository.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN VPN is active WHEN the usage cycle advances THEN no reset prompt is saved`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        store.dispatch(ProxyStateChanged(ProxyState.ACTIVE))

        store.dispatch(UsageChanged(remaining = 2000, max = 2000, resetTime = NEXT_RESET_TIME.toString()))

        assertEquals(NEXT_RESET_TIME, fakeRepository.lastUsageCycleResetTime)
        assertFalse(store.state.usageQuotaResetPromptPending)
        assertFalse(fakeRepository.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a saved reset time WHEN quota becomes unmetered THEN the saved reset time is cleared`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME

        store.dispatch(UsageChanged(remaining = 200, max = 0, resetTime = NEXT_RESET_TIME.toString()))

        assertNull(fakeRepository.lastUsageCycleResetTime)
    }

    @Test
    fun `GIVEN a saved reset time WHEN the auth provider changes from FxA to Gpi THEN the saved reset time is cleared`() {
        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Fxa))
        store.dispatch(UsageChanged(remaining = 200, max = 1000, resetTime = NEXT_RESET_TIME.toString()))

        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Gpi))

        assertNull(fakeRepository.lastUsageCycleResetTime)
    }

    @Test
    fun `GIVEN a saved reset time WHEN the auth provider changes from Gpi to FxA THEN the saved reset time is cleared`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Gpi))
        store.dispatch(UsageChanged(remaining = 200, max = 1000, resetTime = NEXT_RESET_TIME.toString()))

        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Fxa))

        assertNull(fakeRepository.lastUsageCycleResetTime)
    }

    @Test
    fun `GIVEN a pending reset prompt WHEN VPN becomes active THEN the saved prompt is cleared`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        store.dispatch(ProxyStateChanged(ProxyState.READY))
        store.dispatch(UsageChanged(remaining = 2000, max = 2000, resetTime = NEXT_RESET_TIME.toString()))
        assertTrue(store.state.usageQuotaResetPromptPending)

        store.dispatch(IPProtectionAction.ActivationAction.Toggle)
        assertTrue(fakeRepository.usageQuotaResetPromptPending)

        store.dispatch(ProxyStateChanged(ProxyState.ACTIVE))

        assertFalse(store.state.usageQuotaResetPromptPending)
        assertFalse(fakeRepository.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a pending reset prompt saved in the repository WHEN VPN becomes active THEN the saved prompt is cleared`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        fakeRepository.usageQuotaResetPromptPending = true

        store.dispatch(ProxyStateChanged(ProxyState.ACTIVE))

        assertFalse(fakeRepository.usageQuotaResetPromptPending)
        assertEquals(RESET_TIME, fakeRepository.lastUsageCycleResetTime)
    }

    @Test
    fun `GIVEN a pending reset prompt WHEN the auth provider changes from FxA to Gpi THEN the reset prompt is no longer pending`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        store.dispatch(ProxyStateChanged(ProxyState.READY))
        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Gpi))
        store.dispatch(UsageChanged(remaining = 2000, max = 2000, resetTime = NEXT_RESET_TIME.toString()))
        assertTrue(store.state.usageQuotaResetPromptPending)

        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Fxa))

        assertFalse(store.state.usageQuotaResetPromptPending)
        assertFalse(fakeRepository.usageQuotaResetPromptPending)
    }

    @Test
    fun `GIVEN a pending reset prompt WHEN the auth provider changes from Gpi to FxA THEN the reset prompt is no longer pending`() {
        fakeRepository.lastUsageCycleResetTime = RESET_TIME
        store.dispatch(ProxyStateChanged(ProxyState.READY))
        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Fxa))
        store.dispatch(UsageChanged(remaining = 2000, max = 2000, resetTime = NEXT_RESET_TIME.toString()))
        assertTrue(store.state.usageQuotaResetPromptPending)

        store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Gpi))

        assertFalse(store.state.usageQuotaResetPromptPending)
    }

    private companion object {
        val RESET_TIME = Instant.parse("2026-06-10T00:00:00Z")
        val NEXT_RESET_TIME = Instant.parse("2026-07-10T00:00:00Z")
    }
}
