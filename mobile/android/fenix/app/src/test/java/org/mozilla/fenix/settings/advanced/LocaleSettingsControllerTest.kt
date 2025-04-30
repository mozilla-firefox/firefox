/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.advanced

import android.app.Activity
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.mockkObject
import io.mockk.mockkStatic
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyAll
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.locale.LocaleUseCases
import org.junit.Before
import org.junit.Test
import java.util.Locale

class LocaleSettingsControllerTest {

    private val activity = mockk<Activity>(relaxed = true)
    private val localeSettingsStore: LocaleSettingsStore = mockk(relaxed = true)
    private val browserStore: BrowserStore = mockk(relaxed = true)
    private val localeUseCases: LocaleUseCases = mockk(relaxed = true)
    private val mockState = LocaleSettingsState(mockk(), mockk(), mockk())

    private lateinit var controller: DefaultLocaleSettingsController

    @Before
    fun setup() {
        controller = spyk(
            DefaultLocaleSettingsController(
                activity,
                localeSettingsStore,
                browserStore,
                localeUseCases,
            ),
        )

        mockkObject(LocaleManager)
        mockkStatic("org.mozilla.fenix.settings.advanced.LocaleManagerExtensionKt")
    }

    @Test
    fun `don't set locale if same locale is chosen`() {
        val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
        every { localeSettingsStore.state } returns mockState.copy(selectedLocale = selectedLocale)
        every { LocaleManager.getCurrentLocale(activity) } returns mockk()

        controller.handleLocaleSelected(selectedLocale)

        verifyAll(inverse = true) {
            localeSettingsStore.dispatch(LocaleSettingsAction.Select(selectedLocale))
            browserStore.dispatch(SearchAction.RefreshSearchEnginesAction)
            LocaleManager.setNewLocale(activity, locale = selectedLocale)
            activity.recreate()
        }
        with(controller) {
            verify(inverse = true) {
                LocaleManager.updateBaseConfiguration(activity, selectedLocale)
            }
        }
    }

    @Test
    fun `set a new locale from the list if other locale is chosen`() {
        val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
        val otherLocale: Locale = mockk()
        every { localeUseCases.notifyLocaleChanged } returns mockk()
        every { localeSettingsStore.state } returns mockState.copy(selectedLocale = otherLocale)
        every { LocaleManager.setNewLocale(activity, localeUseCases, selectedLocale) } returns activity
        with(controller) {
            every { LocaleManager.updateBaseConfiguration(activity, selectedLocale) } just Runs
        }

        controller.handleLocaleSelected(selectedLocale)

        verify { localeSettingsStore.dispatch(LocaleSettingsAction.Select(selectedLocale)) }
        verify { browserStore.dispatch(SearchAction.RefreshSearchEnginesAction) }
        verify { LocaleManager.setNewLocale(activity, localeUseCases, selectedLocale) }
        verify { activity.recreate() }
        verify {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(0, 0)
        }

        with(controller) {
            verify { LocaleManager.updateBaseConfiguration(activity, selectedLocale) }
        }
    }

    @Test
    fun `set a new locale from the list if default locale is not selected`() {
        val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
        every { localeUseCases.notifyLocaleChanged } returns mockk()
        every { localeSettingsStore.state } returns mockState.copy(selectedLocale = selectedLocale)
        every { LocaleManager.getCurrentLocale(activity) } returns null
        every { LocaleManager.setNewLocale(activity, localeUseCases, selectedLocale) } returns activity

        with(controller) {
            every { LocaleManager.updateBaseConfiguration(activity, selectedLocale) } just Runs
        }

        controller.handleLocaleSelected(selectedLocale)

        verify { localeSettingsStore.dispatch(LocaleSettingsAction.Select(selectedLocale)) }
        verify { browserStore.dispatch(SearchAction.RefreshSearchEnginesAction) }
        verify { LocaleManager.setNewLocale(activity, localeUseCases, selectedLocale) }
        verify { activity.recreate() }
        verify {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(0, 0)
        }

        with(controller) {
            verify { LocaleManager.updateBaseConfiguration(activity, selectedLocale) }
        }
    }

    @Test
    fun `don't set default locale if default locale is already chosen`() {
        val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
        every { localeSettingsStore.state } returns mockState.copy(localeList = listOf(selectedLocale))
        every { LocaleManager.getCurrentLocale(activity) } returns null

        controller.handleDefaultLocaleSelected()

        verifyAll(inverse = true) {
            localeSettingsStore.dispatch(LocaleSettingsAction.Select(selectedLocale))
            browserStore.dispatch(SearchAction.RefreshSearchEnginesAction)
            LocaleManager.resetToSystemDefault(activity, localeUseCases)
            activity.recreate()
            with(controller) {
                LocaleManager.updateBaseConfiguration(activity, selectedLocale)
            }
        }
    }

    @Test
    fun `set the default locale as the new locale`() {
        val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
        every { localeUseCases.notifyLocaleChanged } returns mockk()
        every { localeSettingsStore.state } returns mockState.copy(localeList = listOf(selectedLocale))
        every { LocaleManager.resetToSystemDefault(activity, localeUseCases) } just Runs
        with(controller) {
            every { LocaleManager.updateBaseConfiguration(activity, selectedLocale) } just Runs
        }

        controller.handleDefaultLocaleSelected()

        verify { localeSettingsStore.dispatch(LocaleSettingsAction.Select(selectedLocale)) }
        verify { browserStore.dispatch(SearchAction.RefreshSearchEnginesAction) }
        verify { LocaleManager.resetToSystemDefault(activity, localeUseCases) }
        verify { activity.recreate() }
        verify {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(0, 0)
        }

        with(controller) {
            verify { LocaleManager.updateBaseConfiguration(activity, selectedLocale) }
        }
    }

    @Test
    fun `handle search query typed`() {
        val query = "Eng"

        controller.handleSearchQueryTyped(query)

        verify { localeSettingsStore.dispatch(LocaleSettingsAction.Search(query)) }
    }
}
