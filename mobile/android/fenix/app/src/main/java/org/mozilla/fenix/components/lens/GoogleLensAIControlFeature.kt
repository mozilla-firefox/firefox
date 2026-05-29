/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.map
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureState
import org.mozilla.fenix.R
import org.mozilla.fenix.utils.Settings
import mozilla.components.ui.icons.R as iconsR

/**
 * The AI Controls feature settings for the Google Lens integration. The toggle is local only and
 * is independent of the [Settings.googleLensIntegrationEnabled] Nimbus-backed flag — when this is
 * disabled, the standard QR scanner returns and the "Open with Google Lens" context menu entry is
 * hidden, regardless of the experiment cohort.
 */
class GoogleLensAIControlFeature(
    private val settings: Settings,
) : AIControllableFeature, AIFeatureMetadata by Companion {
    private val _revision = MutableStateFlow(0)
    override val featureState: Flow<AIFeatureState>
        get() = _revision.map {
            when {
                settings.googleLensIntegrationUserEnabled -> AIFeatureState.Enabled
                else -> AIFeatureState.Disabled
            }
        }

    override suspend fun set(enabled: Boolean) {
        settings.googleLensIntegrationUserEnabled = enabled
        _revision.value++
    }

    companion object : AIFeatureMetadata {
        override val id: AIFeatureMetadata.FeatureId = AIFeatureMetadata.FeatureId("googleLens")
        override val description: AIFeatureMetadata.Description = AIFeatureMetadata.Description(
            titleRes = R.string.ai_controls_google_lens_title,
            descriptionRes = R.string.ai_controls_google_lens_description,
            iconRes = iconsR.drawable.mozac_ic_image_24,
        )
    }
}
