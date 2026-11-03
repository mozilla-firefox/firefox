/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.settings

import java.io.IOException
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.fakes.android.FakePreferencesDataStore
import org.junit.Test

private const val ENGINE = "com.example.tts"
private const val OTHER_ENGINE = "come.example.other.tts"

@OptIn(ExperimentalCoroutinesApi::class)
class ListenSettingsTest {

    @Test
    fun `test that a language with no saved voice reads back as null`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)

        assertNull(settings.getSelectedVoiceId("en-US"))
    }

    @Test
    fun `test that the voice of each language is saved and read back on its own`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)
        settings.setSelectedVoiceId("en-US", "en-us-female")
        settings.setSelectedVoiceId("fr-FR", "fr-fr-male")

        assertEquals("en-us-female", settings.getSelectedVoiceId("en-US"))
        assertEquals("fr-fr-male", settings.getSelectedVoiceId("fr-FR"))
    }

    @Test
    fun `test that saving a voice for a language replaces the one saved before it`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)
        settings.setSelectedVoiceId("en-US", "en-us-female")
        settings.setSelectedVoiceId("en-US", "en-us-male")

        assertEquals("en-us-male", settings.getSelectedVoiceId("en-US"))
    }

    @Test
    fun `test that every voice is cleared when engine package is changed`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)
        settings.clearSavedVoicesOnEngineChange(ENGINE)
        settings.setSelectedVoiceId("en-US", "en-us-female")
        settings.setSelectedVoiceId("fr-FR", "fr-fr-male")

        settings.clearSavedVoicesOnEngineChange(OTHER_ENGINE)

        assertNull(settings.getSelectedVoiceId("en-US"))
        assertNull(settings.getSelectedVoiceId("fr-FR"))
    }

    @Test
    fun `test that the saved voices survive an engine that has not changed`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)
        settings.clearSavedVoicesOnEngineChange(ENGINE)
        settings.setSelectedVoiceId("en-US", "en-us-female")

        settings.clearSavedVoicesOnEngineChange(ENGINE)

        assertEquals("en-us-female", settings.getSelectedVoiceId("en-US"))
    }

    @Test
    fun `test that voices saved before any engine was recorded are cleared by the first one`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)
        settings.setSelectedVoiceId("en-US", "en-us-female")

        settings.clearSavedVoicesOnEngineChange(ENGINE)

        assertNull(settings.getSelectedVoiceId("en-US"))
    }

    @Test
    fun `test that the enabled state is unset when nothing has been saved`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)
        runCurrent()

        assertEquals(ListenEnabledState.Unset, settings.enabled.value)
    }

    @Test
    fun `test that the enabled state is loading until the data store has loaded`() = runTest {
        val settings = DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope)

        assertEquals(ListenEnabledState.Loading, settings.enabled.value)
    }

    @Test
    fun `test that the enabled state is saved and read back`() = runTest {
        val dataStore = FakePreferencesDataStore()
        val settings = DataStoreBackedSettings(dataStore, available = true, scope = backgroundScope)

        settings.setEnabled(false)
        runCurrent()
        assertEquals(ListenEnabledState.Disabled, settings.enabled.value)

        settings.setEnabled(true)
        runCurrent()
        assertEquals(ListenEnabledState.Enabled, settings.enabled.value)
    }

    @Test
    fun `test that a saved enabled state is loaded by new settings`() = runTest {
        val dataStore = FakePreferencesDataStore()
        DataStoreBackedSettings(dataStore, available = true, scope = backgroundScope).setEnabled(false)

        val settings = DataStoreBackedSettings(dataStore, available = true, scope = backgroundScope)
        runCurrent()

        assertEquals(ListenEnabledState.Disabled, settings.enabled.value)
    }

    @Test
    fun `test that availability is the value it was created with`() = runTest {
        assertTrue(
            DataStoreBackedSettings(FakePreferencesDataStore(), available = true, scope = backgroundScope).available
        )
        assertFalse(
            DataStoreBackedSettings(FakePreferencesDataStore(), available = false, scope = backgroundScope).available
        )
    }

    @Test
    fun `test that settings are ready only when available and enabled`() {
        assertTrue(ListenSettings.inMemory(available = true, enabled = ListenEnabledState.Enabled).shouldShow())
        assertFalse(ListenSettings.inMemory(available = true, enabled = ListenEnabledState.Disabled).shouldShow())
        assertFalse(ListenSettings.inMemory(available = true, enabled = ListenEnabledState.Unset).shouldShow())
        assertFalse(ListenSettings.inMemory(available = true, enabled = ListenEnabledState.Loading).shouldShow())
        assertFalse(ListenSettings.inMemory(available = false, enabled = ListenEnabledState.Enabled).shouldShow())
    }

    @Test
    fun `test that the enabled state is unset when the data store cannot be read`() = runTest {
        val settings =
            DataStoreBackedSettings(
                FakePreferencesDataStore(readFailure = IOException("unreadable")),
                available = true,
                scope = backgroundScope,
            )
        runCurrent()

        assertEquals(ListenEnabledState.Unset, settings.enabled.value)
    }

    @Test
    fun `test that a read failure other than an IOException is not swallowed`() = runTest {
        var caught: Throwable? = null
        val scope =
            CoroutineScope(
                StandardTestDispatcher(testScheduler) +
                    SupervisorJob() +
                    CoroutineExceptionHandler { _, e -> caught = e }
            )
        val settings =
            DataStoreBackedSettings(
                FakePreferencesDataStore(readFailure = IllegalStateException("broken")),
                available = true,
                scope = scope,
            )
        runCurrent()

        assertTrue(caught is IllegalStateException)
        assertEquals(ListenEnabledState.Loading, settings.enabled.value)
        scope.cancel()
    }
}
