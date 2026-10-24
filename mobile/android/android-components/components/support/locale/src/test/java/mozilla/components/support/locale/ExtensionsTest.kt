/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.locale

import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Test

class ExtensionsTest {

    @Test
    fun `GIVEN language tag WHEN converting THEN locale is created`() {
        val locale = "es".toLocale()

        assertEquals("es", locale.language)
    }

    @Test
    fun `GIVEN locale with hyphenated region WHEN converting THEN locale is created`() {
        val locale = "en-US".toLocale()

        assertEquals("en", locale.language)
        assertEquals("US", locale.country)
    }

    @Test
    fun `GIVEN locale with underscored region WHEN converting THEN locale is created`() {
        val locale = "en_US".toLocale()

        assertEquals("en", locale.language)
        assertEquals("US", locale.country)
    }

    @Test
    fun `GIVEN locale with unicode extension WHEN converting THEN locale is created`() {
        val locale = "fa-IR-u-nu-latn".toLocale()

        assertEquals("fa", locale.language)
        assertEquals("IR", locale.country)
    }

    @Test
    fun `GIVEN locale with script and region WHEN converting THEN locale is created`() {
        val locale = "zh-Hant-TW".toLocale()

        assertEquals("zh", locale.language)
        assertEquals("TW", locale.country)
    }

    @Test
    fun `GIVEN locale with script region and unicode extension WHEN converting THEN locale is created`() {
        val locale = "sr-Latn-RS-u-nu-latn".toLocale()

        assertEquals("sr", locale.language)
        assertEquals("RS", locale.country)
        assertEquals("Latn", locale.script)
        assertEquals("latn", locale.getUnicodeLocaleType("nu"))
    }

    @Test
    fun `GIVEN empty locale WHEN converting THEN root locale is returned`() {
        assertEquals(Locale.ROOT, "".toLocale())
    }

    @Test
    fun `GIVEN blank locale WHEN converting THEN root locale is returned`() {
        assertEquals(Locale.ROOT, "   ".toLocale())
    }
}
