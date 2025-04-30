/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.advanced

import android.content.Context
import android.view.View
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkObject
import mozilla.components.support.locale.LocaleManager
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.util.Locale

class BaseLocaleViewHolderTest {

    private val selectedLocale = Locale.Builder().setLanguage("en").setRegion("UK").build()
    private val view: View = mockk()
    private val context: Context = mockk()

    private val localeViewHolder = object : BaseLocaleViewHolder(view, selectedLocale) {
        override fun bind(locale: Locale) = Unit
    }

    @Before
    fun setup() {
        mockkObject(LocaleManager)
        every { view.context } returns context
    }

    @Test
    fun `verify other locale checker returns false`() {
        every { LocaleManager.getCurrentLocale(context) } returns mockk()
        val otherLocale = mockk<Locale>()

        assertFalse(localeViewHolder.isCurrentLocaleSelected(otherLocale, isDefault = true))
        assertFalse(localeViewHolder.isCurrentLocaleSelected(otherLocale, isDefault = false))
    }

    @Test
    fun `verify selected locale checker returns true`() {
        every { LocaleManager.getCurrentLocale(context) } returns mockk()

        assertFalse(localeViewHolder.isCurrentLocaleSelected(selectedLocale, isDefault = true))
        assertTrue(localeViewHolder.isCurrentLocaleSelected(selectedLocale, isDefault = false))
    }

    @Test
    fun `verify default locale checker returns true`() {
        every { LocaleManager.getCurrentLocale(context) } returns null

        assertTrue(localeViewHolder.isCurrentLocaleSelected(selectedLocale, isDefault = true))
        assertFalse(localeViewHolder.isCurrentLocaleSelected(selectedLocale, isDefault = false))
    }
}
