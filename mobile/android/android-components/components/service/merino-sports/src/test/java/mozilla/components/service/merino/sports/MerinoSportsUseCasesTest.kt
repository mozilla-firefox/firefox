/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MerinoSportsUseCasesTest {

    @After
    fun cleanup() {
        GlobalMerinoSportsDependencyProvider.provider = null
        GlobalMerinoSportsDependencyProvider.storage = null
    }

    @Test
    fun `GIVEN the provider returns matches WHEN refresh matches use case is called THEN store the matches and return true`() =
        runTest {
            val provider: MerinoSportsProvider = mock()
            val storage = MerinoMatchesStorage.inMemory()
            val matches = MerinoMatches(previous = emptyList(), current = emptyList(), upcoming = emptyList())

            GlobalMerinoSportsDependencyProvider.initialize(lazyOf(provider), lazyOf(storage))

            whenever(provider.getMatches()).thenReturn(matches)

            assertTrue(MerinoSportsUseCases().refreshMatches.invoke())
            assertEquals(matches, storage.matches.first())
        }

    @Test
    fun `GIVEN the provider fails to fetch matches WHEN refresh matches use case is called THEN keep the stored matches and return false`() =
        runTest {
            val provider: MerinoSportsProvider = mock()
            val storedMatches = MerinoMatches(previous = emptyList(), current = emptyList(), upcoming = emptyList())
            val storage = MerinoMatchesStorage.inMemory(initialMatches = storedMatches)

            GlobalMerinoSportsDependencyProvider.initialize(lazyOf(provider), lazyOf(storage))

            whenever(provider.getMatches()).thenReturn(null)

            assertFalse(MerinoSportsUseCases().refreshMatches.invoke())
            assertEquals(storedMatches, storage.matches.first())
        }

    @Test(expected = IllegalArgumentException::class)
    fun `GIVEN the dependencies are not initialized WHEN refresh matches use case is called THEN an exception is thrown`() =
        runTest {
            MerinoSportsUseCases().refreshMatches.invoke()
        }
}
