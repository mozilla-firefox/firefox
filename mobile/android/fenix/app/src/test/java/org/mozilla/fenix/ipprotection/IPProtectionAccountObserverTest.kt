/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection

import io.mockk.mockk
import kotlin.time.Instant
import mozilla.components.concept.sync.AuthType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.ipprotection.store.FakeIPProtectionRepository

private val RESET_TIME = Instant.parse("2026-11-01T00:00:00Z")

class IPProtectionAccountObserverTest {
    private lateinit var repository: FakeIPProtectionRepository
    private lateinit var observer: IPProtectionAccountObserver

    @Before
    fun setup() {
        repository =
            FakeIPProtectionRepository(
                hasAlreadyUsedVpn = true,
                lastDataLimitNoticeCycleResetTime = RESET_TIME,
            )
        observer = IPProtectionAccountObserver(repository)
    }

    @Test
    fun `GIVEN a data limit notice was shown WHEN the user logs out THEN the reset time is cleared`() {
        observer.onLoggedOut()

        assertNull(repository.lastDataLimitNoticeCycleResetTime)
    }

    @Test
    fun `GIVEN a data limit notice was shown WHEN the account is restored on startup THEN the reset time is kept`() {
        observer.onAuthenticated(mockk(), AuthType.Existing)

        assertEquals(RESET_TIME, repository.lastDataLimitNoticeCycleResetTime)
    }

    @Test
    fun `GIVEN the VPN was used WHEN the user logs out THEN hasAlreadyUsedVpn is kept`() {
        observer.onLoggedOut()

        assertTrue(repository.hasAlreadyUsedVpn)
    }
}
