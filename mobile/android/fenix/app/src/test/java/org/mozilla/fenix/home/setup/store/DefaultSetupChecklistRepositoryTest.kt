/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.setup.store

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class DefaultSetupChecklistRepositoryTest {
    private val preferenceKeys = SetupChecklistPreference.entries

    private lateinit var settings: Settings

    @Before
    fun setup() {
        settings = Settings(testContext)
        every { testContext.settings() } returns settings
    }

    @Test
    fun `test the preference enum keys map to the correct preference key`() {
        assertEquals(R.string.pref_key_default_browser, preferenceKeys[0].preferenceKey)
        assertEquals(R.string.pref_key_fxa_signed_in, preferenceKeys[1].preferenceKey)
        assertEquals(R.string.pref_key_setup_step_theme, preferenceKeys[2].preferenceKey)
        assertEquals(R.string.pref_key_setup_step_toolbar, preferenceKeys[3].preferenceKey)
        assertEquals(R.string.pref_key_setup_step_extensions, preferenceKeys[4].preferenceKey)
        assertEquals(R.string.pref_key_search_widget_installed_2, preferenceKeys[5].preferenceKey)
        assertEquals(R.string.pref_key_setup_checklist_complete, preferenceKeys[6].preferenceKey)
    }

    @Test
    fun `WHEN toolbar preference THEN setPreference updates the preference value`() {
        assertFalse(settings.hasCompletedSetupStepToolbar)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.ToolbarComplete, true)

        assertTrue(settings.hasCompletedSetupStepToolbar)
    }

    @Test
    fun `WHEN theme preference THEN setPreference updates the preference value`() {
        assertFalse(settings.hasCompletedSetupStepTheme)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.ThemeComplete, true)

        assertTrue(settings.hasCompletedSetupStepTheme)
    }

    @Test
    fun `WHEN extensions complete preference THEN setPreference updates the preference value`() {
        assertFalse(settings.hasCompletedSetupStepExtensions)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.ExtensionsComplete, true)

        assertTrue(settings.hasCompletedSetupStepExtensions)
    }

    @Test
    fun `WHEN set to default preference THEN setPreference does not update the preference value`() {
        assertFalse(settings.isDefaultBrowser)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.SetToDefault, true)

        assertFalse(settings.isDefaultBrowser)
    }

    @Test
    fun `WHEN sign in preference THEN setPreference does not update the preference value`() {
        assertFalse(settings.signedInFxaAccount)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.SetToDefault, true)

        assertFalse(settings.signedInFxaAccount)
    }

    @Test
    fun `WHEN install search widget preference THEN setPreference does not update the preference value`() {
        assertFalse(settings.searchWidgetInstalled)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.SetToDefault, true)

        assertFalse(settings.searchWidgetInstalled)
    }

    @Test
    fun `WHEN show checklist preference THEN setPreference updates the preference value`() {
        assertFalse(settings.showSetupChecklist)

        val repository = DefaultSetupChecklistRepository(context = testContext)
        repository.setPreference(SetupChecklistPreference.ShowSetupChecklist, true)

        assertTrue(settings.showSetupChecklist)
    }

    @Test
    fun `GIVEN is default browser preference WHEN a change is made to the preference value THEN the repository emits the change`() =
        runTest {
            assertFalse(settings.isDefaultBrowser)
            val repository =
                DefaultSetupChecklistRepository(context = testContext, coroutineScope = this)
            settings.preferences.registerOnSharedPreferenceChangeListener(repository.onPreferenceChange)

            settings.isDefaultBrowser = true

            val result = repository.setupChecklistPreferenceUpdates.take(1).first()
            val expected = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                SetupChecklistPreference.SetToDefault,
                true,
            )
            assertEquals(expected, result)
        }

    @Test
    fun `GIVEN sign in preference WHEN a change is made to the preference value THEN the repository emits the change`() =
        runTest {
            assertFalse(settings.signedInFxaAccount)
            val repository =
                DefaultSetupChecklistRepository(context = testContext, coroutineScope = this)
            settings.preferences.registerOnSharedPreferenceChangeListener(repository.onPreferenceChange)

            settings.signedInFxaAccount = true

            val result = repository.setupChecklistPreferenceUpdates.take(1).first()
            val expected = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                SetupChecklistPreference.SignIn,
                true,
            )
            assertEquals(expected, result)
        }

    @Test
    fun `GIVEN theme step completed preference WHEN a change is made to the preference value THEN the repository emits the change`() =
        runTest {
            assertFalse(settings.hasCompletedSetupStepTheme)
            val repository =
                DefaultSetupChecklistRepository(context = testContext, coroutineScope = this)
            settings.preferences.registerOnSharedPreferenceChangeListener(repository.onPreferenceChange)

            settings.hasCompletedSetupStepTheme = true

            val result = repository.setupChecklistPreferenceUpdates.take(1).first()
            val expected = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                SetupChecklistPreference.ThemeComplete,
                true,
            )
            assertEquals(expected, result)
        }

    @Test
    fun `GIVEN toolbar step completed preference WHEN a change is made to the preference value THEN the repository emits the change`() =
        runTest {
            assertFalse(settings.hasCompletedSetupStepToolbar)
            val repository =
                DefaultSetupChecklistRepository(context = testContext, coroutineScope = this)
            settings.preferences.registerOnSharedPreferenceChangeListener(repository.onPreferenceChange)

            settings.hasCompletedSetupStepToolbar = true

            val result = repository.setupChecklistPreferenceUpdates.take(1).first()
            val expected = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                SetupChecklistPreference.ToolbarComplete,
                true,
            )
            assertEquals(expected, result)
        }

    @Test
    fun `GIVEN extension step completed preference WHEN a change is made to the preference value THEN the repository emits the change`() =
        runTest {
            assertFalse(settings.hasCompletedSetupStepExtensions)
            val repository =
                DefaultSetupChecklistRepository(context = testContext, coroutineScope = this)
            settings.preferences.registerOnSharedPreferenceChangeListener(repository.onPreferenceChange)

            settings.hasCompletedSetupStepExtensions = true

            val result = repository.setupChecklistPreferenceUpdates.take(1).first()
            val expected = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                SetupChecklistPreference.ExtensionsComplete,
                true,
            )
            assertEquals(expected, result)
        }

    @Test
    fun `GIVEN search widget added preference WHEN a change is made to the preference value THEN the repository emits the change`() =
        runTest {
            assertFalse(settings.searchWidgetInstalled)
            val repository =
                DefaultSetupChecklistRepository(context = testContext, coroutineScope = this)
            settings.preferences.registerOnSharedPreferenceChangeListener(repository.onPreferenceChange)

            settings.setSearchWidgetInstalled(true)

            val result = repository.setupChecklistPreferenceUpdates.take(1).first()
            val expected = SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                SetupChecklistPreference.InstallSearchWidget,
                true,
            )
            assertEquals(expected, result)
        }
}
