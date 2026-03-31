package mozilla.components.feature.summarize

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.feature.summarize.settings.SummarizationSettings

class PageSummaryFeature(
    private val settings: SummarizationSettings,
) : AIControllableFeature {
    override val id = AIControllableFeature.FeatureId(
        value = "ai.summarize.page"
    )

    override val description = AIControllableFeature.Description(
        titleRes = R.string.mozac_ai_controls_page_summary_title,
        descriptionRes = R.string.mozac_ai_controls_page_summary_description
    )

    override val isEnabled: Flow<Boolean> = flow { emitAll(settings.getFeatureEnabledUserStatus()) }

    override suspend fun set(enabled: Boolean) {
        settings.setFeatureEnabledUserStatus(enabled)
    }
}
