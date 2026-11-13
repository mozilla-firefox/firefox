/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.repository.uistate.data

/**
 * Model for data emitted from the Tab Manager UI data storage layer.
 *
 * @property hasUserDismissedTabGroupOnboarding Whether the user has previously dismissed the onboarding.
 * @property tabGroupOnboardingImpressionCount How many times the user has been presented the onboarding.
 * @property hasUserEverHadOneTabGroup Whether the user has ever had a tab group.
 * @property hasViewedTabGroupsPage Whether the user has viewed the Tab Groups page.
 * @property skipUngroupConfirmation Whether the ungroup confirmation dialog should be skipped.
 * @property tabGroupMenuImpressionCount How many times the user has been shown the tab groups menu item.
 * @property tabGroupToolbarMenuHighlightImpressionCount How many times the user has opened the menu while the toolbar
 *   menu button was highlighted for Tab Groups.
 * @property hasUserEverUsedTabGroupMenuItem Whether the user has ever used the tab groups menu item.
 */
data class PersistedUIState(
    val hasUserDismissedTabGroupOnboarding: Boolean = false,
    val tabGroupOnboardingImpressionCount: Int = 0,
    val hasUserEverHadOneTabGroup: Boolean = false,
    val hasViewedTabGroupsPage: Boolean = false,
    val skipUngroupConfirmation: Boolean = false,
    val tabGroupMenuImpressionCount: Int = 0,
    val tabGroupToolbarMenuHighlightImpressionCount: Int = 0,
    val hasUserEverUsedTabGroupMenuItem: Boolean = false,
)
