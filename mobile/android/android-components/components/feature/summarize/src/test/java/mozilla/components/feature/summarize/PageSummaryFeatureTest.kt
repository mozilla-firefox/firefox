/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.summarize.settings.SummarizationSettings
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PageSummaryFeatureTest {

    @Test
    fun `isEnabled reflects feature enabled status from settings`() = runTest {
        val settings = SummarizationSettings.inMemory(isFeatureEnabled = true)
        val feature = PageSummaryFeature(settings)

        assertTrue(feature.isEnabled.first())
    }

    @Test
    fun `isEnabled reflects feature disabled status from settings`() = runTest {
        val settings = SummarizationSettings.inMemory(isFeatureEnabled = false)
        val feature = PageSummaryFeature(settings)

        assertFalse(feature.isEnabled.first())
    }

    @Test
    fun `set enabled persists to settings`() = runTest {
        val settings = SummarizationSettings.inMemory(isFeatureEnabled = false)
        val feature = PageSummaryFeature(settings)

        feature.set(true)

        assertTrue(feature.isEnabled.first())
    }

    @Test
    fun `set disabled persists to settings`() = runTest {
        val settings = SummarizationSettings.inMemory(isFeatureEnabled = true)
        val feature = PageSummaryFeature(settings)

        feature.set(false)

        assertFalse(feature.isEnabled.first())
    }
}
