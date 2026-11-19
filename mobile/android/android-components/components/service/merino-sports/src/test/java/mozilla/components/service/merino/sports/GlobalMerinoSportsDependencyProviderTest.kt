/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import mozilla.components.support.test.mock
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Test

class GlobalMerinoSportsDependencyProviderTest {

    @After
    fun cleanup() {
        GlobalMerinoSportsDependencyProvider.provider = null
        GlobalMerinoSportsDependencyProvider.storage = null
    }

    @Test
    fun `GIVEN the dependencies are initialized WHEN they are required THEN return them`() {
        val provider: MerinoSportsProvider = mock()
        val storage = MerinoMatchesStorage.inMemory()

        GlobalMerinoSportsDependencyProvider.initialize(lazyOf(provider), lazyOf(storage))

        assertSame(provider, GlobalMerinoSportsDependencyProvider.requireProvider())
        assertSame(storage, GlobalMerinoSportsDependencyProvider.requireStorage())
    }

    @Test
    fun `WHEN the dependencies are initialized THEN they are not created until they are required`() {
        val provider = lazy<MerinoSportsProvider> { mock() }
        val storage = lazy { MerinoMatchesStorage.inMemory() }

        GlobalMerinoSportsDependencyProvider.initialize(provider, storage)

        assertFalse(provider.isInitialized())
        assertFalse(storage.isInitialized())
    }

    @Test(expected = IllegalArgumentException::class)
    fun `GIVEN the dependencies are not initialized WHEN the provider is required THEN an exception is thrown`() {
        GlobalMerinoSportsDependencyProvider.requireProvider()
    }

    @Test(expected = IllegalArgumentException::class)
    fun `GIVEN the dependencies are not initialized WHEN the storage is required THEN an exception is thrown`() {
        GlobalMerinoSportsDependencyProvider.requireStorage()
    }
}
