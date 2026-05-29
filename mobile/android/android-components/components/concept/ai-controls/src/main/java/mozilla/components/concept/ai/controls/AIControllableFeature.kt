/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.ai.controls

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.flow.update
import mozilla.components.concept.ai.controls.AIFeatureState.Disabled
import mozilla.components.concept.ai.controls.AIFeatureState.Enabled
import mozilla.components.concept.ai.controls.AIFeatureState.Unknown

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
        val iconRes: Int,
    )

    val id: FeatureId
    val description: Description
}

/**
 * Describes the state of the AI feature. Represents all the possible states as
 * [Enabled], [Disabled] and [Unknown].
 */
sealed class AIFeatureState {

    /**
     * Describes the state when the AI feature has been enabled.
     */
    object Enabled : AIFeatureState()

    /**
     * Describes the state where the AI feature has been disabled.
     */
    object Disabled : AIFeatureState()

    /**
     * Describes the state where it is not known if the AI feature is enabled or disabled.
     * This state is likely to occur if we check the AI controls state before the underlying
     * AI feature is able to say whether it's enabled or not.
     *
     * Because it is possible for the [AIFeatureBlock.isBlocked] to be true, for a specific feature
     * to be turned on, we need to know if that feature is explicitly turned on or if the user
     * has simply never interacted with the AI controls, and that is when this comes in handy.
     */
    object Unknown : AIFeatureState()
}

/**
 * A feature that can be enabled or disabled by AI controls.
 */
interface AIControllableFeature : AIFeatureMetadata {
    val featureState: Flow<AIFeatureState>

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
            description: AIFeatureMetadata.Description = AIFeatureMetadata.Description(0, 0, 0),
            initialEnabled: Boolean = false,
        ): AIControllableFeature = InMemoryAIControllableFeature(id, description, initialEnabled)
    }
}

/**
 * Convenience function for mapping [AIControllableFeature.featureState] to a [Flow] of non-null [Boolean] values.
 * [AIFeatureState.Unknown] is resolved as not enabled
 */
val AIControllableFeature.isEnabled: Flow<Boolean>
    get() = featureState.mapNotNull {
        when (it) {
            is Enabled -> true
            is Disabled -> false
            is Unknown -> null
        }
    }

private class InMemoryAIControllableFeature(
    override val id: AIFeatureMetadata.FeatureId,
    override val description: AIFeatureMetadata.Description,
    initialEnabled: Boolean,
) : AIControllableFeature {

    private val _featureState =
        MutableStateFlow(if (initialEnabled) Enabled else Disabled)
    override val featureState: Flow<AIFeatureState>
        get() = _featureState

    override suspend fun set(enabled: Boolean) {
        _featureState.update {
            if (enabled) Enabled else Disabled
        }
    }
}
