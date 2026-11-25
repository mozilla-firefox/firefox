/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import androidx.compose.runtime.staticCompositionLocalOf
import org.mozilla.fenix.Config
import org.mozilla.fenix.nimbus.FxNimbus

/** Feature helper for managing the release of the Tabs Tray UI enhancements. */
interface TabManagementFeatureHelper {

    /** Whether the Tab Manager opening animation is enabled. */
    val openingAnimationEnabled: Boolean

    /** Whether drag and drop is enabled for the Tab Groups feature. */
    val tabGroupsDragAndDropEnabled: Boolean

    /** Determines whether the "Ungroup" item is displayed in a tab group's three dot menu. */
    val ungroupTabGroupEnabled: Boolean

    /** Whether onboarding is enabled for the Tab Groups feature. */
    val tabGroupsOnboardingEnabled: Boolean

    /** Control whether reorder happens live during a drag and drop action for Tab Groups. */
    val tabGroupsLiveReorderEnabled: Boolean

    /** Whether the Tab Groups strip is shown while the active tab is in a group. */
    val tabGroupsStripEnabled: Boolean

    /** Control whether Tab Group options show up in the browser menu. */
    val showTabGroupsInMenu: Boolean
}

/** The default implementation of [TabManagementFeatureHelper]. */
data object DefaultTabManagementFeatureHelper : TabManagementFeatureHelper {

    override val openingAnimationEnabled: Boolean
        get() = Config.channel.isDebug || FxNimbus.features.tabManagementEnhancements.value().openingAnimationEnabled

    override val tabGroupsDragAndDropEnabled: Boolean
        get() = Config.channel.isDebug || FxNimbus.features.tabGroupsDragAndDrop.value().enabled

    override val tabGroupsLiveReorderEnabled: Boolean
        get() = FxNimbus.features.tabGroupsLiveReorder.value().enabled

    override val ungroupTabGroupEnabled: Boolean
        get() = Config.channel.isDebug || FxNimbus.features.tabGroupsUngroup.value().enabled

    override val tabGroupsOnboardingEnabled: Boolean
        get() = Config.channel.isDebug || FxNimbus.features.tabGroupsOnboarding.value().enabled

    override val tabGroupsStripEnabled: Boolean
        get() = FxNimbus.features.tabGroupsStrip.value().enabled

    override val showTabGroupsInMenu: Boolean
        get() = FxNimbus.features.tabGroupsMenu.value().enabled
}

val LocalTabManagementFeatureHelper =
    staticCompositionLocalOf<TabManagementFeatureHelper> {
        DefaultTabManagementFeatureHelper
    }
