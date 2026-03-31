/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.ai.controls

/**
 * A registry for [AIControllableFeature]s that can be managed by AI controls.
 */
interface AIFeatureRegistry {
    /**
     * Registers [feature] with this registry.
     */
    fun register(feature: AIControllableFeature)

    /**
     * Returns all registered [AIControllableFeature]s.
     */
    fun getFeatures(): List<AIControllableFeature>

    companion object {
        /**
         * Creates a simple in-memory implementation of [AIFeatureRegistry] for use in tests or previews.
         */
        fun inMemory(): AIFeatureRegistry = InMemoryAIFeatureRegistry()
    }
}

private class InMemoryAIFeatureRegistry : AIFeatureRegistry {
    private val features = LinkedHashMap<AIFeatureMetadata.FeatureId, AIControllableFeature>()

    override fun register(feature: AIControllableFeature) {
        features[feature.id] = feature
    }

    override fun getFeatures(): List<AIControllableFeature> = features.values.toList()
}
