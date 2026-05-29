/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.fakes.android.FakePreferencesDataStore
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.test.assertNull

class SummarizationSettingsTest {

    @Test
    fun `that user preference for feature returns null if the value has never been set`() =
        runTest {
            val dataStore = FakePreferencesDataStore()
            val settings = DataStoreBackedSettings(dataStore)

            assertNull(
                settings.getFeatureEnabledUserStatus().first(),
                "Expected initial preference to be null because it has not been previous set",
            )
        }

    @Test
    fun `that user preference for feature is persisted`() = runTest {
        val dataStore = FakePreferencesDataStore()
        val settings = DataStoreBackedSettings(dataStore)

        settings.setFeatureEnabledUserStatus(false)
        assertFalse(settings.getHasConsentedToShake().first())
    }

    @Test
    fun `that user preference for gesture is persisted`() = runTest {
        val dataStore = FakePreferencesDataStore()
        val settings = DataStoreBackedSettings(dataStore)

        settings.setGestureEnabledUserStatus(false)
        assertFalse(settings.getHasConsentedToShake().first())
    }

    @Test
    fun `that user preference for shake consent is persisted`() = runTest {
        val dataStore = FakePreferencesDataStore()
        val settings = DataStoreBackedSettings(dataStore)

        settings.setHasConsentedToShake(true)
        assertTrue(settings.getHasConsentedToShake().first())
    }

    @Test
    fun `if user rejects shake consent 3 times, gesture is disabled`() = runTest {
        val dataStore = FakePreferencesDataStore()
        val settings = DataStoreBackedSettings(dataStore)

        settings.incrementShakeConsentRejectedCount()
        settings.incrementShakeConsentRejectedCount()
        settings.incrementShakeConsentRejectedCount()
        assertFalse(settings.getGestureEnabledUserStatus().first())
    }
}
