/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import mozilla.components.concept.ai.controls.AIFeatureState
import mozilla.components.concept.ai.controls.isEnabled
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class AIFeatureRegistryTest {

    private val testScope = TestScope()

    @Test(expected = IllegalStateException::class)
    fun `registry cannot re-register features`() {
        val featureA = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("a"),
            initialFeatureState = AIFeatureState.Enabled,
        )
        val featureBlockStorage = AIFeatureBlockStorage.inMemory(true)
        val registry = DefaultAIFeatureRegistry(
            scope = testScope,
            storage = featureBlockStorage,
        )

        registry.register(featureA)
        registry.register(featureA)
    }

    @Test
    fun `registry sets a feature to off if the feature state is unknown and AI features are blocked`() =
        runTest {
            // given that AI features are blocked
            val featureBlockStorage = AIFeatureBlockStorage.inMemory(true)
            val registry = DefaultAIFeatureRegistry(
                scope = testScope,
                storage = featureBlockStorage,
            )

            // given the feature
            val feature = createFeature(defaultValue = AIFeatureState.Unknown)

            // when we register the feature
            registry.register(feature)
            testScope.testScheduler.advanceUntilIdle()

            assertFalse(
                "Expected feature to be disabled when AI features are blocked",
                feature.isEnabled.first(),
            )
        }

    @Test
    fun `registry keeps a feature on if the feature state is unknown and AI features are not blocked`() =
        runTest {
            // given that AI features are not blocked
            val featureBlockStorage = AIFeatureBlockStorage.inMemory(false)
            val registry = DefaultAIFeatureRegistry(
                scope = testScope,
                storage = featureBlockStorage,
            )

            // given the feature
            val feature = createFeature(defaultValue = AIFeatureState.Enabled)

            // when we register the feature
            registry.register(feature)
            testScope.testScheduler.advanceUntilIdle()

            assertTrue(
                "Expected feature to be enabled if AI features are allowed",
                feature.isEnabled.first(),
            )
        }

    private fun createFeature(
        featureId: String = "test-feature",
        defaultValue: AIFeatureState,
    ): AIControllableFeature {
        return AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId(featureId),
            description = AIFeatureMetadata.Description(0, 0, 0),
            initialFeatureState = defaultValue,
        )
    }
}
