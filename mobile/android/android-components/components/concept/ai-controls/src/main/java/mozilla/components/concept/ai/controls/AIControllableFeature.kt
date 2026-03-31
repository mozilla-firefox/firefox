/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.ai.controls

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * Metadata defining an AI Feature.
 */
interface AIFeatureMetadata {
    /**
     * A unique identifier for an [AIControllableFeature].
     */
    @JvmInline
    value class FeatureId(val value: String)

    /**
     * Human-readable strings describing an [AIControllableFeature].
     */
    data class Description(
        val titleRes: Int,
        val descriptionRes: Int,
    )

    val id: FeatureId
    val description: Description
}

/**
 * A feature that can be enabled or disabled by AI controls.
 */
interface AIControllableFeature : AIFeatureMetadata {
    val isEnabled: Flow<Boolean>

    /**
     * Enables or disables this feature.
     */
    suspend fun set(enabled: Boolean)

    companion object {
        /**
         * Creates a simple in-memory implementation of [AIControllableFeature] for use in tests or previews.
         */
        fun inMemory(
            id: AIFeatureMetadata.FeatureId = AIFeatureMetadata.FeatureId("inMemory"),
            description: AIFeatureMetadata.Description = AIFeatureMetadata.Description(0, 0),
            initialEnabled: Boolean = false,
        ): AIControllableFeature = InMemoryAIControllableFeature(id, description, initialEnabled)
    }
}

private class InMemoryAIControllableFeature(
    override val id: AIFeatureMetadata.FeatureId,
    override val description: AIFeatureMetadata.Description,
    initialEnabled: Boolean,
) : AIControllableFeature {
    private val _isEnabled = MutableStateFlow(initialEnabled)
    override val isEnabled: Flow<Boolean> = _isEnabled

    override suspend fun set(enabled: Boolean) {
        _isEnabled.value = enabled
    }
}
