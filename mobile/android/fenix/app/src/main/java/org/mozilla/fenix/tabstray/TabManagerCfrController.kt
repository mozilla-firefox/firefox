/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import androidx.annotation.VisibleForTesting
import kotlin.math.abs
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.ui.TabManagementFragment
import org.mozilla.fenix.utils.Settings

/** Controller for handling CFR logic in [TabManagementFragment] */
class TabManagerCfrController(
    private val settings: Settings,
    private val tabsTrayStore: TabsTrayStore,
    private val currentTimeProvider: () -> Long = { System.currentTimeMillis() },
) {
    /** Suppresses future shows of the banner and records the time of the last CFR. */
    fun onTabAutoCloseBannerDismiss() {
        settings.shouldShowAutoCloseTabsBanner = false
        settings.lastCfrShownTimeInMillis = currentTimeProvider()
    }

    /** Marks the CFR as shown and records telemetry. */
    fun onInactiveTabsCfrClick() {
        settings.shouldShowInactiveTabsOnboardingPopup = false
        settings.lastCfrShownTimeInMillis = currentTimeProvider()
        TabsTray.inactiveTabsCfrSettings.record(NoExtras())
        tabsTrayStore.dispatch(TabsTrayAction.DismissInactiveTabsCFR)
    }

    /** Marks the CFR as shown and records telemetry. */
    fun onInactiveTabsCfrDismiss() {
        settings.shouldShowInactiveTabsOnboardingPopup = false
        settings.lastCfrShownTimeInMillis = currentTimeProvider()
        TabsTray.inactiveTabsCfrDismissed.record(NoExtras())
        tabsTrayStore.dispatch(TabsTrayAction.DismissInactiveTabsCFR)
    }

    /**
     * Marks Tab Swipe CFR as ready to show if tab is adjacent to currently selected tab and the feature is eligible.
     */
    fun maybeMarkTabSwipeCfrReady(tab: TabsTrayItem.Tab) {
        if (!settings.canShowTabSwipeCfr()) return

        val normalTabs = tabsTrayStore.state.normalTabsState.items
        if (normalTabs.size < MIN_TABS_FOR_SWIPE_CFR) return

        val currentTabId = tabsTrayStore.state.selectedTabId
        currentTabId?.let {
            val currentTabPosition = getTabPositionFromId(normalTabs, currentTabId)
            val newTabPosition = getTabPositionFromId(normalTabs, tab.id)

            if (abs(currentTabPosition - newTabPosition) == 1) {
                settings.shouldShowTabSwipeCFR = true
            }
        }
    }

    private fun Settings.canShowTabSwipeCfr() =
        !hasShownTabSwipeCFR && !isTabStripEnabled && isSwipeToolbarToSwitchTabsEnabled

    @VisibleForTesting
    internal fun getTabPositionFromId(tabsList: List<TabsTrayItem>, tabId: String): Int {
        tabsList.forEachIndexed { index, tab ->
            if (tab is TabsTrayItem.Tab && tab.id == tabId) return index
        }
        return -1
    }

    companion object {
        private const val MIN_TABS_FOR_SWIPE_CFR = 2
    }
}
