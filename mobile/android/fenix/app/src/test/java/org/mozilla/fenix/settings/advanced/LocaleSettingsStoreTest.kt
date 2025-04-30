/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.advanced

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import java.util.Locale

class LocaleSettingsStoreTest {

    private lateinit var localeSettingsStore: LocaleSettingsStore
    private val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
    private val otherLocale = Locale.Builder().setLanguage("fr").build()

    @Before
    fun setup() {
        val localeList = listOf(
            Locale.Builder().setLanguage("fr").build(), // default
            otherLocale,
            selectedLocale,
        )

        localeSettingsStore =
            LocaleSettingsStore(LocaleSettingsState(localeList, localeList, selectedLocale))
    }

    @Test
    fun `change selected locale`() = runTest {
        localeSettingsStore.dispatch(LocaleSettingsAction.Select(otherLocale)).join()

        assertEquals(otherLocale, localeSettingsStore.state.selectedLocale)
    }

    @Test
    fun `change selected list by search query`() = runTest {
        localeSettingsStore.dispatch(LocaleSettingsAction.Search("Eng")).join()

        assertEquals(2, localeSettingsStore.state.searchedLocaleList.size)
        assertEquals(selectedLocale, localeSettingsStore.state.searchedLocaleList[1])
    }

    @Test
    fun `GIVEN search list is amended WHEN locale selected THEN reset search list`() = runTest {
        localeSettingsStore.dispatch(LocaleSettingsAction.Search("Eng")).join()
        assertEquals(2, localeSettingsStore.state.searchedLocaleList.size)

        localeSettingsStore.dispatch(LocaleSettingsAction.Search("fr")).join()
        localeSettingsStore.dispatch(LocaleSettingsAction.Select(otherLocale)).join()

        assertEquals(localeSettingsStore.state.localeList.size, localeSettingsStore.state.searchedLocaleList.size)
    }
}
