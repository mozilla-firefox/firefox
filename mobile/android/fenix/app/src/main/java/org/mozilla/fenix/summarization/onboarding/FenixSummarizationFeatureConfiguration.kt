/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization.onboarding

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.update
import org.mozilla.fenix.summarization.SummarizationSettingsBinding
import org.mozilla.fenix.utils.Settings

/**
 * Discovery settings for the summarize feature. It's a wrapper around [org.mozilla.fenix.utils.Settings] for easier
 * testing and narrowed API
 */
class FenixSummarizationFeatureConfiguration(
    private val settings: Settings,
    private val summarizationSettingsBinding: SummarizationSettingsBinding,
) : SummarizationFeatureDiscoveryConfiguration {

    override val shouldShowFeature: Boolean
        get() = summarizationSettingsBinding.isFeatureEnabled.value

    override val showMenuItem: Boolean
        get() = shouldShowFeature

    override val shouldHighlightMenuItem: Boolean
        get() = shouldShowFeature && settings.shakeToSummarizeMenuItemExposureCount.underMaxCount()

    override val shouldHighlightOverflowMenuItem: Boolean
        get() = shouldShowFeature && settings.shakeToSummarizeMoreMenuItemInteractionCount.underMaxCount()

    override val shouldToolbarShowCfr: Boolean
        get() = shouldShowFeature && !settings.shakeToSummarizeToolbarCfrShown

    /**
     * We determine if we should highlight the toolbar by checking the feature is enabled & checking the number of
     * interactions
     */
    val shouldHighlightToolbarMenuButton: Boolean
        get() = shouldShowFeature && settings.shakeToSummarizeToolbarInteractionCount.underMaxCount()

    private val _toolbarMenuButtonHighlight = MutableStateFlow(shouldHighlightToolbarMenuButton)
    override val toolbarMenuButtonHighlight: Flow<Boolean>
        get() =
            _toolbarMenuButtonHighlight.combine(summarizationSettingsBinding.isFeatureEnabled) {
                shouldHighlightToolbarMenuButton,
                isFeatureEnabled ->
                isFeatureEnabled && shouldHighlightToolbarMenuButton
            }

    override fun cacheDiscoveryEvent(event: SummarizeDiscoveryEvent) {
        when (event) {
            SummarizeDiscoveryEvent.CfrExposure -> recordCfrExposure()
            SummarizeDiscoveryEvent.MenuItemExposure -> recordMenuItemExposure()
            SummarizeDiscoveryEvent.MenuOverflowInteraction -> recordMenuOverflowItemInteraction()
            SummarizeDiscoveryEvent.ToolbarOverflowInteraction -> recordToolbarOverflowMenuInteraction()
        }
    }

    private fun recordMenuItemExposure() {
        if (shouldShowFeature && settings.shakeToSummarizeMenuItemExposureCount.underMaxCount()) {
            settings.shakeToSummarizeMenuItemExposureCount.increment()
        }
    }

    private fun recordCfrExposure() {
        if (shouldShowFeature && !settings.shakeToSummarizeToolbarCfrShown) {
            settings.shakeToSummarizeToolbarCfrShown = true
        }
    }

    private fun recordMenuOverflowItemInteraction() {
        if (shouldShowFeature && settings.shakeToSummarizeMoreMenuItemInteractionCount.underMaxCount()) {
            settings.shakeToSummarizeMoreMenuItemInteractionCount.increment()
        }
    }

    private fun recordToolbarOverflowMenuInteraction() {
        if (shouldShowFeature && settings.shakeToSummarizeToolbarInteractionCount.underMaxCount()) {
            settings.shakeToSummarizeToolbarInteractionCount.increment()

            _toolbarMenuButtonHighlight.update { shouldHighlightToolbarMenuButton }
        }
    }
}
