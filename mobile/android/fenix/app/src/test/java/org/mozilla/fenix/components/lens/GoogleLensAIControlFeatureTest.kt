/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.ai.controls.isEnabled
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.utils.Settings

class GoogleLensAIControlFeatureTest {

    private val settings: Settings = mockk(relaxed = true)

    @Test
    fun `GIVEN no user override WHEN isEnabled is collected THEN it emits true`() = runTest {
        every { settings.googleLensIntegrationUserEnabled } returns true
        val feature = GoogleLensAIControlFeature(settings)

        assertTrue(feature.isEnabled.first())
    }

    @Test
    fun `WHEN set is called THEN isEnabled and Settings reflect the new value`() = runTest {
        every { settings.googleLensIntegrationUserEnabled } returns true
        val feature = GoogleLensAIControlFeature(settings)

        feature.set(false)
        every { settings.googleLensIntegrationUserEnabled } returns false
        assertFalse(feature.isEnabled.first())
        verify { settings.googleLensIntegrationUserEnabled = false }

        feature.set(true)
        every { settings.googleLensIntegrationUserEnabled } returns true
        assertTrue(feature.isEnabled.first())
        verify { settings.googleLensIntegrationUserEnabled = true }
    }

    @Test
    fun `WHEN id is read THEN it returns the stable googleLens identifier`() {
        assertEquals("googleLens", GoogleLensAIControlFeature.id.value)
    }
}
