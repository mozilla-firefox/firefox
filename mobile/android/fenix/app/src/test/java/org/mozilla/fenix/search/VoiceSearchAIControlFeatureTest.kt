/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.utils.Settings

class VoiceSearchAIControlFeatureTest {

    private val settings: Settings = mockk(relaxed = true)

    @Test
    fun `isEnabled reflects shouldShowVoiceSearch from settings`() = runTest {
        every { settings.shouldShowVoiceSearch } returns false
        val feature = VoiceSearchAIControlFeature(settings, onUpdateWidget = {})
        assertFalse(feature.isEnabled.first())

        every { settings.shouldShowVoiceSearch } returns true
        assertTrue(feature.isEnabled.first())
    }

    @Test
    fun `set persists to settings and triggers widget update`() = runTest {
        every { settings.shouldShowVoiceSearch } returns true
        var widgetUpdated = false
        val feature = VoiceSearchAIControlFeature(settings, onUpdateWidget = { widgetUpdated = true })

        feature.set(false)
        every { settings.shouldShowVoiceSearch } returns false

        assertFalse(feature.isEnabled.first())
        verify { settings.shouldShowVoiceSearch = false }
        assertTrue(widgetUpdated)
    }
}
