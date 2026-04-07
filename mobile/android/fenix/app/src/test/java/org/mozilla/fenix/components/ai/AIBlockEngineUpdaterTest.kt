/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ai

import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.ai.controls.AIFeatureBlock
import mozilla.components.support.test.fakes.engine.FakeEngine
import org.junit.Test
import kotlin.time.Duration.Companion.seconds

class AIBlockEngineUpdaterTest {
    @Test
    fun `updating AIFeatureBlock updates the engine`() = runTest {
        val engine = FakeEngine()
        val featureBlock = AIFeatureBlock.inMemory(false)

        backgroundScope.launch {
            AIBlockEngineUpdater(engine, featureBlock).start()
        }

        testScheduler.advanceTimeBy(1.seconds)

        assertTrue(engine.settings.browserML!!)
        assertTrue(engine.settings.extensionsML!!)

        featureBlock.block()

        testScheduler.advanceTimeBy(1.seconds)

        assertFalse(engine.settings.browserML!!)
        assertFalse(engine.settings.extensionsML!!)
    }
}
