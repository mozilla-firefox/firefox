/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.fake

import kotlinx.coroutines.flow.MutableStateFlow
import org.mozilla.fenix.tabgroups.TabGroupsFeatureDiscoveryConfiguration

/** [TabGroupsFeatureDiscoveryConfiguration] with settable values that counts the discovery events recorded. */
class FakeTabGroupsFeatureDiscoveryConfiguration(
    override var showMenuItem: Boolean = true,
    override var shouldHighlightMenuItem: Boolean = false,
    override val toolbarMenuButtonHighlight: MutableStateFlow<Boolean> = MutableStateFlow(false),
) : TabGroupsFeatureDiscoveryConfiguration {
    var menuItemExposureCount = 0
        private set

    var menuItemUsedCount = 0
        private set

    var toolbarOpenedCount = 0
        private set

    override fun recordMenuItemExposure() {
        menuItemExposureCount++
    }

    override fun recordMenuItemUsed() {
        menuItemUsedCount++
    }

    override fun recordToolbarOpened() {
        toolbarOpenedCount++
    }
}
