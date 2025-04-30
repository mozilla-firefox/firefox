/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import java.util.Locale

@RunWith(FenixRobolectricTestRunner::class)
class LocaleUtilsTest {

    @Test
    fun `WHEN using getDisplayName on a 'de' locale THEN get the expected default name`() {
        val localizedLanguageName = LocaleUtils.getDisplayName(
            locale = Locale.forLanguageTag("de"),
        )
        assertEquals("Deutsch", localizedLanguageName)
    }

    @Test
    fun `WHEN using getLocalizedDisplayName with an 'en' locale on a 'de' locale THEN get the expected localized name`() {
        val localizedLanguageName = LocaleUtils.getLocalizedDisplayName(
            userLocale = Locale.forLanguageTag("en"),
            languageLocale = Locale.forLanguageTag("de"),
        )
        assertEquals("German", localizedLanguageName)
    }
}
