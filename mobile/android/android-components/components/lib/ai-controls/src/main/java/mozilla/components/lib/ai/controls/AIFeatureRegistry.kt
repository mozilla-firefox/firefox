/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import mozilla.components.concept.ai.controls.AIFeatureState

/**
 * Creates the implementation of [AIFeatureRegistry], which enforces unique feature IDs.
 */
fun AIFeatureRegistry.Companion.default(
    scope: CoroutineScope,
    context: Context,
): AIFeatureRegistry =
    DefaultAIFeatureRegistry(scope, AIFeatureBlockStorage.dataStore(context))

/**
 * Default implementation of [AIFeatureRegistry] that enforces unique feature IDs and
 * initializes feature states based on the block status stored in [AIFeatureBlockStorage].
 */
internal class DefaultAIFeatureRegistry(
    private val scope: CoroutineScope,
    private val storage: AIFeatureBlockStorage,
) : AIFeatureRegistry {
    // LinkedHashMap allows us to maintain the order for later use.
    private val features = LinkedHashMap<AIFeatureMetadata.FeatureId, AIControllableFeature>()

    override fun register(feature: AIControllableFeature) {
        check(feature.id !in features.keys) {
            "AI feature with id=${feature.id} is already registered"
        }

        scope.launch {
            if (feature.featureState.first() is AIFeatureState.Unknown) {
                val aiFeaturesBlocked = storage.isBlocked.first()
                feature.set(!aiFeaturesBlocked)
            }
        }

        features[feature.id] = CachedEnabledFeature(feature, scope)
    }

    override fun getFeatures(): List<AIControllableFeature> {
        return features.values.toList()
    }
}
