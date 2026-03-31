/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.feature.summarize.settings.SummarizationSettings

/**
 * The feature that ties AI Controls to page summaries/shake to summarize.
 */
class PageSummaryFeature(
    private val settings: SummarizationSettings,
) : AIControllableFeature, AIFeatureMetadata by Companion {

    override val isEnabled: Flow<Boolean> = flow { emitAll(settings.getFeatureEnabledUserStatus()) }

    override suspend fun set(enabled: Boolean) {
        settings.setFeatureEnabledUserStatus(enabled)
    }

    companion object : AIFeatureMetadata {
        override val id = AIFeatureMetadata.FeatureId("ai.summarize.page")

        override val description = AIFeatureMetadata.Description(
            titleRes = R.string.mozac_ai_controls_page_summary_title,
            descriptionRes = R.string.mozac_ai_controls_page_summary_description,
        )
    }
}
