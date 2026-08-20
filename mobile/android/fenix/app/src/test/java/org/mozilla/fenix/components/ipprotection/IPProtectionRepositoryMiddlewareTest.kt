/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ipprotection

import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.Store
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(mozilla.components.ExperimentalAndroidComponentsApi::class)
class IPProtectionRepositoryMiddlewareTest {

    private lateinit var fakeRepository: FakeIPProtectionRepository
    private lateinit var middleware: IPProtectionPreferencesMiddleware
    private lateinit var store: Store<IPProtectionState, IPProtectionAction>

    @Before
    fun setUp() {
        fakeRepository = FakeIPProtectionRepository()
        middleware = IPProtectionPreferencesMiddleware(fakeRepository)

        store =
            Store(
                initialState = IPProtectionState(),
                reducer = { state, _ -> state },
            )
    }

    @Test
    fun `GIVEN vpn has not been used yet WHEN action is Toggle THEN next is called and hasAlreadyUsedVpn becomes true`() {
        var nextCalled = false

        middleware.invoke(
            store = store,
            next = { nextCalled = true },
            action = IPProtectionAction.Toggle,
        )

        assertTrue(nextCalled)
        assertTrue(fakeRepository.hasAlreadyUsedVpn)
    }

    @Test
    fun `GIVEN vpn has not been used yet WHEN action is not Toggle THEN next is called and hasAlreadyUsedVpn remains false`() {
        val nonToggleAction = IPProtectionAction.ProxyActivationShown
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
            action = IPProtectionAction.Toggle,
        )

        assertTrue(nextCalled)
        assertTrue(fakeRepository.hasAlreadyUsedVpn)
    }

    private class FakeIPProtectionRepository : IPProtectionRepository {
        override var hasAlreadyUsedVpn: Boolean = false
    }
}
