/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.fakes.android.FakeSharedPreferences
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SummarizationSettingsTest {

    @Test
    fun `test that we can set and retrieve that the user has consented to shake`() = runTest {
        val prefs = FakeSharedPreferences()
        val settings = SharedPrefsBackedSummarizationSettings(prefs)

        assertFalse(settings.hasConsentedToShake())

        settings.setHasConsentedToShake(true)

        assertTrue(settings.hasConsentedToShake())
    }
}
