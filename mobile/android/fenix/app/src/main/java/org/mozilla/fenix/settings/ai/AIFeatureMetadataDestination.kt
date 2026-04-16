/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import androidx.annotation.StringRes
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.feature.summarize.PageSummaryFeature
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.translations.TranslationsAIControllableFeature

/**
 * Defines the types of navigation destinations that AI features can map to.
 */
sealed interface AIFeatureMetadataDestination {
    @get:StringRes
    val label: Int
}

/**
 * A navigation destination that should open a SUMO link.
 */
data class LearnMoreLink(val topic: SupportUtils.SumoTopic, override val label: Int) : AIFeatureMetadataDestination

/**
 * A navigation destination that navigates directly to somewhere on the nav graph.
 */
data class Destination(val id: Int, override val label: Int) : AIFeatureMetadataDestination

/**
 * Map from a feature to a destination.
 */
val AIFeatureMetadata.destination: AIFeatureMetadataDestination? get() = when (id) {
    PageSummaryFeature.id -> Destination(
        id = R.id.pageSummariesSettingsFragment,
        label = R.string.ai_controls_more_page_summary_settings,
    )
    TranslationsAIControllableFeature.id -> Destination(
        id = R.id.translations_settings_graph,
        label = R.string.ai_controls_more_translations_settings,
    )
    else -> null
}

/**
 * Allows for navigating to a destination.
 */
fun AIFeatureMetadataDestination.nav(fragment: Fragment) = when (this) {
    is Destination -> fragment.findNavController().navigate(id)
    is LearnMoreLink -> {
        val context = fragment.requireContext()
        SupportUtils.launchSandboxCustomTab(context, SupportUtils.getSumoURLForTopic(context, this.topic))
    }
}
