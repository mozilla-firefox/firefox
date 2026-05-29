/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
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

    private lateinit var registry: AIFeatureRegistry

    private val featureBlockStorage = TestAIFeatureBlockStorage(initialBlocked = true)
    private val testScope = TestScope()

    @Before
    fun setUp() {
        registry = DefaultAIFeatureRegistry(
            scope = testScope,
            storage = featureBlockStorage,
        )
    }

    @Test(expected = IllegalStateException::class)
    fun `registry cannot re-register features`() {
        val featureA = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("a"),
            initialEnabled = true,
        )

        registry.register(featureA)
        registry.register(featureA)
    }

    @Test
    fun `registry sets a feature to off if the feature state is unknown and AI features are blocked`() =
        runTest {
            // given that AI features are blocked
            featureBlockStorage.blockedFlow.emit(true)

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
            featureBlockStorage.blockedFlow.emit(false)

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
        return TestAIControllableFeature(
            id = AIFeatureMetadata.FeatureId(featureId),
            description = AIFeatureMetadata.Description(0, 0, 0),
            defaultValue = defaultValue,
        )
    }

    private class TestAIFeatureBlockStorage(initialBlocked: Boolean) : AIFeatureBlockStorage {
        val blockedFlow = MutableStateFlow(initialBlocked)
        override val isBlocked: Flow<Boolean> = blockedFlow

        override suspend fun setBlocked(isBlocked: Boolean) {
            blockedFlow.value = isBlocked
        }
    }

    private class TestAIControllableFeature(
        override val id: AIFeatureMetadata.FeatureId,
        override val description: AIFeatureMetadata.Description,
        val defaultValue: AIFeatureState,
    ) : AIControllableFeature {

        private val _featureState = MutableStateFlow(defaultValue)
        override val featureState: Flow<AIFeatureState>
            get() = _featureState

        override suspend fun set(enabled: Boolean) {
            _featureState.tryEmit(if (enabled) AIFeatureState.Enabled else AIFeatureState.Disabled)
        }
    }
}
