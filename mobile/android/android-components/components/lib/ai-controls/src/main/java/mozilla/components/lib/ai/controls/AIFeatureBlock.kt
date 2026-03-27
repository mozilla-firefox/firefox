/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import android.content.Context
import mozilla.components.concept.ai.controls.AIFeatureBlock
import mozilla.components.concept.ai.controls.AIFeatureRegistry

/**
 * Creates the default implementation of [AIFeatureBlock], coordinating [registry] and [storage]
 * to block or unblock all registered features.
 */
fun AIFeatureBlock.Companion.default(
    context: Context,
    registry: AIFeatureRegistry,
): AIFeatureBlock = DefaultAIFeatureBlock(
    storage = AIFeatureBlockStorage.dataStore(context),
    registry = registry,
)

internal class DefaultAIFeatureBlock(
    private val registry: AIFeatureRegistry,
    private val storage: AIFeatureBlockStorage,
) : AIFeatureBlock {
    override val isBlocked = storage.isBlocked

    // Turn off all features
    override suspend fun block() {
        registry.getFeatures().forEach { it.set(false) }
        storage.setBlocked(true)
    }

    // Turn on all features
    override suspend fun unblock() {
        storage.setBlocked(false)
        registry.getFeatures().forEach { it.set(true) }
    }
}
