/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlinx.coroutines.flow.Flow

/** Decides how the tab groups menu item is shown to help users discover the feature, and records their interactions. */
interface TabGroupsFeatureDiscoveryConfiguration {
    /** Whether the tab groups item can be shown in the menu. */
    val showMenuItem: Boolean

    /** Emits whether the toolbar menu button should be highlighted to draw attention to the tab groups item. */
    val toolbarMenuButtonHighlight: Flow<Boolean>

    /** Whether the TabGroup menu item should be highlighted. */
    val shouldHighlightMenuItem: Boolean

    /** Records that the user used the tab groups menu item. */
    fun recordMenuItemUsed()

    /** Records that the tab groups menu item was shown to the user. */
    fun recordMenuItemExposure()

    /**
     * Records that the menu was opened while the toolbar menu button was highlighted for Tab Groups, so that the
     * highlight is dismissed.
     */
    fun recordToolbarOpened()
}
