/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization.onboarding

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import org.mozilla.fenix.utils.Settings

/**
 * Discovery settings for the summarize feature. It's a wrapper around [org.mozilla.fenix.utils.Settings] for easier
 * testing and narrowed API
 */
class FenixSummarizeFeatureDiscoverySettings(
    private val settings: Settings,
) : SummarizeFeatureDiscoverySettings {
    override val canShowFeature: Boolean
        get() = settings.shakeToSummarizeFeatureFlagEnabled && settings.shakeToSummarizeFeatureUserPreference

    override val showMenuItem: Boolean
        get() = canShowFeature

    override val shouldHighlightMenuItem: Boolean
        get() = canShowFeature && settings.shakeToSummarizeMenuItemExposureCount.underMaxCount()

    override val shouldHighlightOverflowMenuItem: Boolean
        get() = canShowFeature && settings.shakeToSummarizeMoreMenuItemInteractionCount.underMaxCount()

    /**
     * We determine if we should highlight the toolbar by checking the feature flags &
     * checking the number of interactions
     */
    private val shouldHighlightToolbarMenuButton: Boolean
        get() = canShowFeature && settings.shakeToSummarizeToolbarInteractionCount.underMaxCount()

    private val _toolbarMenuButtonHighlight = MutableStateFlow(shouldHighlightToolbarMenuButton)
    override val toolbarMenuButtonHighlight: StateFlow<Boolean>
        get() = _toolbarMenuButtonHighlight

    override fun cacheDiscoveryEvent(event: SummarizeDiscoveryEvent) {
        when (event) {
            SummarizeDiscoveryEvent.MenuItemExposure -> recordMenuItemExposure()
            SummarizeDiscoveryEvent.MenuOverflowInteraction -> recordMenuOverflowItemInteraction()
            SummarizeDiscoveryEvent.ToolbarOverflowInteraction -> recordToolbarOverflowMenuInteraction()
        }
    }

    private fun recordMenuItemExposure() {
        if (canShowFeature && settings.shakeToSummarizeMenuItemExposureCount.underMaxCount()) {
            settings.shakeToSummarizeMenuItemExposureCount.increment()
        }
    }

    private fun recordMenuOverflowItemInteraction() {
        if (canShowFeature && settings.shakeToSummarizeMoreMenuItemInteractionCount.underMaxCount()) {
            settings.shakeToSummarizeMoreMenuItemInteractionCount.increment()
        }
    }

    private fun recordToolbarOverflowMenuInteraction() {
        if (canShowFeature && settings.shakeToSummarizeToolbarInteractionCount.underMaxCount()) {
            settings.shakeToSummarizeToolbarInteractionCount.increment()

            _toolbarMenuButtonHighlight.update { shouldHighlightToolbarMenuButton }
        }
    }
}
