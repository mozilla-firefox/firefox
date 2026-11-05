/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.json.JSONObject
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.HardcodedNimbusFeatures
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class IPProtectionPromptRepositoryTest {
    private lateinit var settings: Settings

    private lateinit var repository: DefaultIPProtectionPromptRepository

    @Before
    fun setup() {
        settings = Settings(testContext)
        repository = DefaultIPProtectionPromptRepository(settings)
    }

    @Test
    fun `WHEN all conditions satisfied THEN show the prompt`() {
        settings.overrideIPProtectionEnabled = true
        repository.isShowingPrompt = false

        assertTrue(settings.isIPProtectionAvailable)
        assertTrue(repository.canShowIPProtectionPrompt())
    }

    @Test
    fun `WHEN the prompt is already showing THEN do not show the prompt`() {
        settings.overrideIPProtectionEnabled = true
        repository.isShowingPrompt = true

        assertTrue(settings.isIPProtectionAvailable)
        assertFalse(repository.canShowIPProtectionPrompt())
    }

    @Test
    fun `WHEN the IP Protection feature is not available THEN do not show the prompt`() {
        val hardcodedNimbus =
            HardcodedNimbusFeatures(
                testContext,
                "ip-protection" to
                    JSONObject(
                        """
                        {
                            "enabled": false
                        }
                        """
                            .trimIndent()
                    ),
            )
        hardcodedNimbus.connectWith(FxNimbus)
        settings.overrideIPProtectionEnabled = false
        repository.isShowingPrompt = false

        assertFalse(settings.isIPProtectionAvailable)

        assertFalse(repository.canShowIPProtectionPrompt())
    }
}
