/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose.ext

import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.Locale as JavaLocale

class IntTest {

    @Test
    fun `WHEN the language is Arabic THEN translate the number to the proper symbol of that locale`() {
        val expected = "٥"
        val numberUnderTest = 5

        JavaLocale.setDefault(JavaLocale.forLanguageTag("ar"))

        assertEquals(expected, numberUnderTest.toLocaleString())
    }
}
