/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureRegistry

/**
 * Creates the default implementation of [AIFeatureRegistry], which enforces unique feature IDs.
 */
fun AIFeatureRegistry.Companion.default() = object : AIFeatureRegistry {
    // LinkedHashMap allows us to maintain the order for later use.
    private val features = LinkedHashMap<AIFeatureMetadata.FeatureId, AIControllableFeature>()

    override fun register(feature: AIControllableFeature) {
        check(feature.id !in features.keys) {
            "AI feature with id=${feature.id} is already registered"
        }
        features[feature.id] = feature
    }

    override fun getFeatures(): List<AIControllableFeature> {
        return features.values.toList()
    }
}
