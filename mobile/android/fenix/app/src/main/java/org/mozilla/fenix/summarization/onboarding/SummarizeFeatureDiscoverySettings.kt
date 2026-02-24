/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization.onboarding

import kotlinx.coroutines.flow.StateFlow

/**
 * Interface for managing the discovery of the summarization feature
 *
 * It helps to manage things like highlighting the menu item, and clearing it upon interaction
 */
interface SummarizeFeatureDiscoverySettings {
    /**
     * Determines if the feature can be shown
     */
    val canShowFeature: Boolean

    /**
     * Indicates whether the menu item should be highlighted.
     */
    val shouldHighlightMenuItem: Boolean

    /**
     * Indicates whether the menu item is visible.
     */
    val showMenuItem: Boolean

    /**
     * Indicates whether the overflow/"more" item within the expanded menu should be highlighted.
     */
    val shouldHighlightOverflowMenuItem: Boolean

    /**
     * Reactive state that indicates whether the three-dot menu button in the toolbar should be highlighted.
     *
     * It is a [StateFlow] because, unlike the rest, we require instant changes in this to reflect
     * in the UI
     */
    val toolbarMenuButtonHighlight: StateFlow<Boolean>

    /**
     * Saves a feature discovery event
     *
     * @param event The [SummarizeDiscoveryEvent] to record
     */
    fun cacheDiscoveryEvent(event: SummarizeDiscoveryEvent)
}

/**
 * Types of discovery events that may affect/change some settings/preferences
 */
sealed interface SummarizeDiscoveryEvent {
    /**
     * Event for the menu item being exposed to the user
     */
    data object MenuItemExposure : SummarizeDiscoveryEvent

    /**
     * Event for the menu overflow/"more" item interaction
     */
    data object MenuOverflowInteraction : SummarizeDiscoveryEvent

    /**
     * Event for the toolbar overflow menu interaction
     */
    data object ToolbarOverflowInteraction : SummarizeDiscoveryEvent
}
