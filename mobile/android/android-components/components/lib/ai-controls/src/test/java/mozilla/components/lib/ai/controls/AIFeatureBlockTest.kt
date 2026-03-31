/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AIFeatureBlockTest {

    @Test
    fun `block sets storage to blocked`() = runTest {
        val storage = AIFeatureBlockStorage.inMemory()
        val block = DefaultAIFeatureBlock(AIFeatureRegistry.default(), storage)

        block.block()

        assertTrue(storage.isBlocked.first())
    }

    @Test
    fun `block disables all registered features`() = runTest {
        val registry = AIFeatureRegistry.default()
        val featureA = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("a"),
            initialEnabled = true,
        )
        val featureB = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("b"),
            initialEnabled = true,
        )
        registry.register(featureA)
        registry.register(featureB)
        val block = DefaultAIFeatureBlock(registry, AIFeatureBlockStorage.inMemory())

        block.block()

        assertFalse(featureA.isEnabled.first())
        assertFalse(featureB.isEnabled.first())
    }

    @Test
    fun `block with empty registry only updates storage`() = runTest {
        val storage = AIFeatureBlockStorage.inMemory()
        val block = DefaultAIFeatureBlock(AIFeatureRegistry.default(), storage)

        block.block()

        assertTrue(storage.isBlocked.first())
    }

    @Test
    fun `unblock sets storage to unblocked`() = runTest {
        val storage = AIFeatureBlockStorage.inMemory(initialBlocked = true)
        val block = DefaultAIFeatureBlock(AIFeatureRegistry.default(), storage)

        block.unblock()

        assertFalse(storage.isBlocked.first())
    }

    @Test
    fun `unblock enables all registered features`() = runTest {
        val registry = AIFeatureRegistry.default()
        val featureA = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("a"),
            initialEnabled = false,
        )
        val featureB = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("b"),
            initialEnabled = false,
        )
        registry.register(featureA)
        registry.register(featureB)
        val block = DefaultAIFeatureBlock(registry, AIFeatureBlockStorage.inMemory())

        block.unblock()

        assertTrue(featureA.isEnabled.first())
        assertTrue(featureB.isEnabled.first())
    }

    @Test
    fun `isBlocked reflects storage state`() = runTest {
        val storage = AIFeatureBlockStorage.inMemory()
        val block = DefaultAIFeatureBlock(AIFeatureRegistry.default(), storage)

        assertFalse(block.isBlocked.first())

        block.block()
        assertTrue(block.isBlocked.first())

        block.unblock()
        assertFalse(block.isBlocked.first())
    }
}
