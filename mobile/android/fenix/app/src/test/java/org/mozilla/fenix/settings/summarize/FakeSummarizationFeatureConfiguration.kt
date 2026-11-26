/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.summarize

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.mozilla.fenix.summarization.onboarding.SummarizationFeatureDiscoveryConfiguration
import org.mozilla.fenix.summarization.onboarding.SummarizeDiscoveryEvent

/** Fake [SummarizationFeatureDiscoveryConfiguration] for testing */
class FakeSummarizationFeatureConfiguration(
    var expectedToolbarMenuButtonHighlight: Boolean = false,
    override var shouldToolbarShowCfr: Boolean = false,
) : SummarizationFeatureDiscoveryConfiguration {

    var menuItemExposureCount: Int = 0
    var cfrExposureCount: Int = 0
    var menuOverflowInteractionCount: Int = 0
    var toolbarOverflowMenuInteractionCount: Int = 0

    override var shouldShowFeature: Boolean = true

    override var shouldHighlightMenuItem: Boolean = true
    override var showMenuItem: Boolean = true
    override var shouldHighlightOverflowMenuItem: Boolean = true

    override val toolbarMenuButtonHighlight: StateFlow<Boolean>
        get() = MutableStateFlow(expectedToolbarMenuButtonHighlight)

    override fun cacheDiscoveryEvent(event: SummarizeDiscoveryEvent) {
        when (event) {
            SummarizeDiscoveryEvent.MenuItemExposure -> menuItemExposureCount++
            SummarizeDiscoveryEvent.MenuOverflowInteraction -> menuOverflowInteractionCount++
            SummarizeDiscoveryEvent.ToolbarOverflowInteraction -> toolbarOverflowMenuInteractionCount++
            SummarizeDiscoveryEvent.CfrExposure -> cfrExposureCount++
        }
    }
}
