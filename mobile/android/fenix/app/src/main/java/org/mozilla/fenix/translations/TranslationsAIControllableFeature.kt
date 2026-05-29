/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureState
import org.mozilla.fenix.R
import mozilla.components.ui.icons.R as iconsR

/**
 * The definition of the translation feature for use in AI Controls.
 */
class TranslationsAIControllableFeature(
    private val settings: TranslationsEnabledSettings,
    private val browserStore: BrowserStore,
) : AIControllableFeature, AIFeatureMetadata by Companion {

    override val featureState: Flow<AIFeatureState>
        get() = settings.isEnabled.map { enabled ->
            if (enabled) AIFeatureState.Enabled else AIFeatureState.Disabled
        }

    override suspend fun set(enabled: Boolean) {
        settings.setEnabled(enabled)
        browserStore.dispatch(TranslationsAction.SetTranslationsEnabledAction(enabled))
    }

    companion object : AIFeatureMetadata {
        override val id: AIFeatureMetadata.FeatureId = AIFeatureMetadata.FeatureId("translations")
        override val description = AIFeatureMetadata.Description(
            titleRes = R.string.ai_controls_translations_title,
            descriptionRes = R.string.ai_controls_translations_description,
            iconRes = iconsR.drawable.mozac_ic_translate_24,
        )
    }
}
