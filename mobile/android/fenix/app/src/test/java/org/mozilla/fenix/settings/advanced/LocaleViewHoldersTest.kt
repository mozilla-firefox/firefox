/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.advanced

import android.view.LayoutInflater
import android.view.View
import androidx.core.view.isVisible
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.mockkObject
import io.mockk.verify
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.LocaleSettingsItemBinding
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import java.util.Locale

@RunWith(FenixRobolectricTestRunner::class)
class LocaleViewHoldersTest {

    private val selectedLocale = Locale.Builder().setLanguage("en").setRegion("US").build()
    private lateinit var view: View
    private lateinit var interactor: LocaleSettingsViewInteractor
    private lateinit var localeViewHolder: LocaleViewHolder
    private lateinit var systemLocaleViewHolder: SystemLocaleViewHolder
    private lateinit var localeSettingsItemBinding: LocaleSettingsItemBinding

    @Before
    fun setup() {
        mockkObject(LocaleManager)
        every { LocaleManager.getCurrentLocale(any()) } returns null

        view = LayoutInflater.from(testContext)
            .inflate(R.layout.locale_settings_item, null)

        localeSettingsItemBinding = LocaleSettingsItemBinding.bind(view)
        interactor = mockk()

        localeViewHolder = LocaleViewHolder(view, selectedLocale, interactor)
        systemLocaleViewHolder = SystemLocaleViewHolder(view, selectedLocale, interactor)
    }

    @Test
    fun `bind LocaleViewHolder`() {
        localeViewHolder.bind(selectedLocale)

        assertEquals("English (United States)", localeSettingsItemBinding.localeTitleText.text)
        assertEquals("English (United States)", localeSettingsItemBinding.localeSubtitleText.text)
        assertFalse(localeSettingsItemBinding.localeSelectedIcon.isVisible)
    }

    @Test
    fun `LocaleViewHolder calls interactor on click`() {
        localeViewHolder.bind(selectedLocale)

        every { interactor.onLocaleSelected(selectedLocale) } just Runs
        view.performClick()
        verify { interactor.onLocaleSelected(selectedLocale) }
    }

    // Note that after we can run tests on SDK 30 the result of the locale.getDisplayName(locale) could differ and this test will fail
    @Test
    fun `GIVEN a locale is not properly identified in Android WHEN we bind locale THEN the title and subtitle are set from locale maps`() {
        val otherLocale = Locale.forLanguageTag("vec")

        localeViewHolder.bind(otherLocale)

        assertEquals("Vèneto", localeSettingsItemBinding.localeTitleText.text)
        assertEquals("Venetian", localeSettingsItemBinding.localeSubtitleText.text)
    }

    @Test
    fun `GIVEN a locale is not properly identified in Android and it is not mapped  WHEN we bind locale THEN the text is the capitalised code`() {
        val otherLocale = Locale.forLanguageTag("yyy")

        localeViewHolder.bind(otherLocale)

        assertEquals("Yyy", localeSettingsItemBinding.localeTitleText.text)
        assertEquals("Yyy", localeSettingsItemBinding.localeSubtitleText.text)
    }

    @Test
    fun `bind SystemLocaleViewHolder`() {
        systemLocaleViewHolder.bind(selectedLocale)

        assertEquals("Follow device language", localeSettingsItemBinding.localeTitleText.text)
        assertEquals("English (United States)", localeSettingsItemBinding.localeSubtitleText.text)
        assertTrue(localeSettingsItemBinding.localeSelectedIcon.isVisible)
    }

    @Test
    fun `SystemLocaleViewHolder calls interactor on click`() {
        systemLocaleViewHolder.bind(selectedLocale)

        every { interactor.onDefaultLocaleSelected() } just Runs
        view.performClick()
        verify { interactor.onDefaultLocaleSelected() }
    }
}
