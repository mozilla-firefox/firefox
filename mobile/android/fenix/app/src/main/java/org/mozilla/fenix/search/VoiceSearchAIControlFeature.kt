/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.appwidget.AppWidgetManager
import android.content.Context
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.map
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureState
import org.mozilla.fenix.R
import org.mozilla.fenix.utils.Settings
import org.mozilla.gecko.search.SearchWidgetProvider
import mozilla.components.ui.icons.R as iconsR

/**
 * The AI Controls feature settings for voice search.
 */
class VoiceSearchAIControlFeature(
    private val settings: Settings,
    private val onUpdateWidget: () -> Unit,
) : AIControllableFeature, AIFeatureMetadata by Companion {
    // Since the feature outlives its collectors, we need both:
    // 1. a way to update collectors while they are actively subscribed
    // 2. a way to replenish our flow with the latest value from Settings on new subscriptions, in case they've changed
    //      while the object is still alive
    // pushing to the counter here will allow us to get 2 while the map will allow us to get 1
    private val _revision = MutableStateFlow(0)
    override val featureState: Flow<AIFeatureState>
        get() = _revision.map {
            when (settings.shouldShowVoiceSearch) {
                true -> AIFeatureState.Enabled
                else -> AIFeatureState.Disabled
            }
        }

    override suspend fun set(enabled: Boolean) {
        settings.shouldShowVoiceSearch = enabled
        _revision.value++
        onUpdateWidget()
    }

    companion object : AIFeatureMetadata {
        override val id: AIFeatureMetadata.FeatureId = AIFeatureMetadata.FeatureId("voiceSearch")
        override val description: AIFeatureMetadata.Description = AIFeatureMetadata.Description(
            titleRes = R.string.ai_controls_voice_search_title,
            descriptionRes = R.string.ai_controls_voice_search_description,
            iconRes = iconsR.drawable.mozac_ic_microphone_24,
        )

        /**
         * Updates the search widget.
         */
        fun updateWidget(context: Context) {
            val appWidgetManager = AppWidgetManager.getInstance(context)
            SearchWidgetProvider.updateAllWidgets(context, appWidgetManager)
        }
    }
}
