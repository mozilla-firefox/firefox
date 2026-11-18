/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.HistorySelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.RecentlyClosedTabsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors

class RecentlyClosedTabsPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "RecentlyClosedTabsPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        builder.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.HISTORY_BUTTON),
                    NavigationStep.Click(HistorySelectors.RECENTLY_CLOSED_TABS_BUTTON),
                ),
        )

        // Opened from the tab manager's three-dot menu; back returns to the tray, so no return edge here.
        builder.register(
            from = "TabDrawerPage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(TabDrawerSelectors.THREE_DOT_BUTTON),
                    NavigationStep.Click(TabDrawerSelectors.RECENTLY_CLOSED_TABS_BUTTON),
                ),
        )

        builder.register(
            from = pageName,
            to = "HistoryPage",
            steps = listOf(NavigationStep.Click(RecentlyClosedTabsSelectors.NAVIGATE_UP_BUTTON)),
        )
    }

    override val selectorCatalog = RecentlyClosedTabsSelectors

    fun verifyRecentlyClosedItem(title: String, url: String): RecentlyClosedTabsPage {
        mozVerify(RecentlyClosedTabsSelectors.RECENTLY_CLOSED_ITEM(title))
        mozVerify(RecentlyClosedTabsSelectors.RECENTLY_CLOSED_ITEM_URL(url))
        return this
    }

    fun openRecentlyClosedItem(title: String): RecentlyClosedTabsPage {
        mozClick(RecentlyClosedTabsSelectors.RECENTLY_CLOSED_ITEM(title))
        return this
    }

    fun deleteRecentlyClosedItem(): RecentlyClosedTabsPage {
        mozClick(RecentlyClosedTabsSelectors.ITEM_DELETE_BUTTON)
        return this
    }

    fun verifyEmptyRecentlyClosedList(): RecentlyClosedTabsPage {
        mozVerify(RecentlyClosedTabsSelectors.EMPTY_RECENTLY_CLOSED_TABS_LIST)
        return this
    }
}
