/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureState
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import mozilla.components.feature.listentopage.settings.ListenSettings
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R

/** The AI Controls feature settings for Listen to Page. */
class ListenToPageAIControlFeature(private val settings: ListenSettings) :
    AIControllableFeature, AIFeatureMetadata by Companion {

    override val featureState: Flow<AIFeatureState>
        get() =
            settings.enabled.mapNotNull { enabled ->
                when (enabled) {
                    // Waits for the stored value, so that the registry does not mistake a slow read for a new feature.
                    ListenEnabledState.Loading -> null
                    ListenEnabledState.Unset -> AIFeatureState.Unknown
                    ListenEnabledState.Enabled -> AIFeatureState.Enabled
                    ListenEnabledState.Disabled -> AIFeatureState.Disabled
                }
            }

    override suspend fun set(enabled: Boolean) {
        settings.setEnabled(enabled)
    }

    companion object : AIFeatureMetadata {
        override val id: AIFeatureMetadata.FeatureId = AIFeatureMetadata.FeatureId("listenToPage")
        override val description: AIFeatureMetadata.Description =
            AIFeatureMetadata.Description(
                titleRes = R.string.ai_controls_listen_to_page_title,
                descriptionRes = R.string.ai_controls_listen_to_page_description,
                iconRes = iconsR.drawable.mozac_ic_listen_to_page_24,
            )
    }
}
