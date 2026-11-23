/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.assert
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.onChildAt
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.waitUntilAtLeastOneExists
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.navigation.NavigationFacts
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationOptions
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class TabDrawerPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "TabDrawerPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        builder.register(
            from = "HomePage",
            to = pageName,
            steps = listOf(NavigationStep.Click(ToolbarSelectors.TAB_COUNTER)),
        )

        // The tab counter is reachable from the browser too, so this edge is generally useful. It has to be
        // the content-description variant: UIAutomator (not Compose, which can hang while GeckoView is
        // active) AND not the testTag, because with shouldUseExpandedToolbar the counter moves to the bottom
        // navigation bar and carries no tag there.
        builder.register(
            from = "BrowserPage",
            to = pageName,
            steps = listOf(NavigationStep.Click(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)),
        )

        builder.register(
            from = pageName,
            to = "HomePage",
            steps = listOf(NavigationStep.PressBack),
            requires = setOf(NavigationFacts.RETURN_SURFACE_HOME),
        )

        builder.register(
            from = pageName,
            to = "BrowserPage",
            steps = listOf(NavigationStep.PressBack),
            requires = setOf(NavigationFacts.RETURN_SURFACE_BROWSER),
        )
    }

    override val selectorCatalog = TabDrawerSelectors

    override fun navigateToPage(
        url: String,
        forceNavigation: Boolean,
        navigationOptions: NavigationOptions,
    ): TabDrawerPage {
        super.navigateToPage(url, forceNavigation, navigationOptions)
        return this
    }

    fun closeTabWithTitle(title: String): TabDrawerPage {
        mozClickFirstWithParentText(TabDrawerSelectors.TAB_ITEM_CLOSE, title)
        return this
    }

    fun verifyMediaTabIndicatorDisplayed(): TabDrawerPage {
        mozVerify(TabDrawerSelectors.TAB_ITEM_MEDIA_INDICATOR)
        return this
    }

    // Polls until gone: the tray clears the indicator a recomposition after the store pause the test asserted.
    fun verifyMediaTabIndicatorNotDisplayed(): TabDrawerPage {
        mozWaitUntilAbsent(TabDrawerSelectors.TAB_ITEM_MEDIA_INDICATOR)
        return this
    }

    fun verifyNoOpenTabsInNormalBrowsing(): TabDrawerPage {
        mozVerifyElementsByGroup(TabDrawerSelectors.Group.EMPTY_NORMAL_BROWSING_TAB_DRAWER_VIEW)
        return this
    }

    fun verifyNormalTabsList(): TabDrawerPage {
        mozWaitUntilAbsent(TabDrawerSelectors.EMPTY_NORMAL_TABS_LIST)
        mozVerify(TabDrawerSelectors.NORMAL_TABS_LIST)
        return this
    }

    fun verifyExistingOpenTabs(vararg urls: String): TabDrawerPage {
        urls.forEach { url ->
            mozVerifyAnyHasChildWithText(TabDrawerSelectors.TAB_ITEM_ROOT, url)
        }
        return this
    }

    /**
     * Wait until each title has rendered in the tray, tree-agnostically (tabs created from a URL show the URL until the
     * page's title loads). Unlike [verifyExistingOpenTabs] this matches in both grid and list view, where a list row
     * merges its title into the row node rather than exposing it as a child.
     */
    fun waitForTabTitlesLoaded(vararg tabTitles: String): TabDrawerPage {
        tabTitles.forEach { composeRule.waitUntilAtLeastOneExists(hasText(it), waitingTime) }
        return this
    }

    fun selectTabsForSharing(vararg tabTitles: String): TabDrawerPage {
        mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.SELECT_TABS_BUTTON)
        tabTitles.forEachIndexed { index, title ->
            mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(title))
            mozVerify(TabDrawerSelectors.SELECTION_COUNTER(index + 1))
        }
        return this
    }

    fun shareSelectedTabs(): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_SELECTION_THREE_DOT_BUTTON)
        mozVerify(TabDrawerSelectors.SELECT_TABS_SHARE_BUTTON)
        mozClick(TabDrawerSelectors.SELECT_TABS_SHARE_BUTTON)
        return this
    }

    fun openTabSearch(): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_SEARCH_BUTTON)
        return this
    }

    /**
     * Tap the footer pill to open the protections dashboard (driven via a locally-instantiated
     * ProtectionsDashboardPage).
     */
    fun openTrackersBlockedDashboard(): TabDrawerPage {
        // Footer in the lazy tabs list, so wait for it to compose before tapping (mozClick does not poll).
        mozVerify(TabDrawerSelectors.TRACKERS_BLOCKED_DASHBOARD_PILL)
        mozClick(TabDrawerSelectors.TRACKERS_BLOCKED_DASHBOARD_PILL)
        return this
    }

    fun typeInTabSearch(query: String): TabDrawerPage {
        mozEnterText(query, TabDrawerSelectors.TAB_SEARCH_FIELD)
        return this
    }

    fun clearTabSearch(): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_SEARCH_CLEAR_BUTTON)
        return this
    }

    /**
     * Assert the tab at a 1-based [position] in the normal-browsing list is the one titled [tabTitle].
     *
     * Ordering cannot be expressed with the mozVerify* family, which only answers "does any node match". The wait is
     * delegated to the harness first because the positional assert below is a one-shot check and would race a list that
     * is still populating.
     *
     * With [isListViewEnabled] the tabs tray renders rows that merge their title into the row node, so the grid path's
     * child-text precondition and TAB_ITEM_ROOT child assert do not hold; the unmerged tree is read instead, matching
     * the legacy robot's list-view branch.
     */
    fun verifyOpenTabsOrder(position: Int, tabTitle: String, isListViewEnabled: Boolean = false): TabDrawerPage {
        if (isListViewEnabled) {
            composeRule.waitUntilAtLeastOneExists(hasText(tabTitle), waitingTime)
            composeRule
                .onNodeWithTag(TabsTrayTestTag.NORMAL_TABS_LIST, useUnmergedTree = true)
                .onChildAt(position - 1)
                .assert(hasAnyChild(hasText(tabTitle)))
        } else {
            mozVerifyAnyHasChildWithText(TabDrawerSelectors.TAB_ITEM_ROOT, tabTitle)
            composeRule
                .onNodeWithTag(TabsTrayTestTag.NORMAL_TABS_LIST)
                .onChildAt(position - 1)
                .assert(hasTestTag(TabsTrayTestTag.TAB_ITEM_ROOT))
                .assert(hasAnyChild(hasText(tabTitle)))
        }
        return this
    }

    /**
     * Open the private tab at 1-based [position] in the private-tabs list.
     *
     * Positional like verifyOpenTabsOrder above, and for the same reason: the mozClick family can only say "click
     * something matching", not "click the nth child". Note the legacy helper indexes from 0.
     *
     * Lands on the browser, so re-anchor with `on.browserPage.navigateToPage()` before returning here — a TabDrawerPage
     * -> TabDrawerPage path resolves to no steps and would silently do nothing.
     */
    fun openPrivateTab(position: Int): TabDrawerPage {
        mozVerify(TabDrawerSelectors.PRIVATE_TABS_LIST)
        composeRule.onNodeWithTag(TabsTrayTestTag.PRIVATE_TABS_LIST).onChildAt(position - 1).performClick()
        return this
    }

    /**
     * Close every open tab from the tab manager's three-dot menu, confirming the dialog it opens.
     *
     * Lands on the homepage, so re-anchor with `on.home.navigateToPage()` before asserting there. The confirmation step
     * is not optional: "Close all tabs" only arms the dialog, and skipping the confirm leaves every tab open — which
     * turns any later "no tabs" or search-group assertion into a false pass.
     */
    fun closeAllTabs(): TabDrawerPage {
        mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.CLOSE_ALL_TABS_BUTTON)
        mozVerify(TabDrawerSelectors.CLOSE_ALL_TABS_CONFIRM_BUTTON)
        mozClick(TabDrawerSelectors.CLOSE_ALL_TABS_CONFIRM_BUTTON)
        return this
    }

    private fun selectTabsAndTapAddToGroup(tabTitle: String) {
        mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.SELECT_TABS_BUTTON)
        mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(tabTitle))
        mozClick(TabDrawerSelectors.TAB_SELECTION_THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.ADD_TO_GROUP_THREE_DOT_BUTTON)
    }

    private fun longClickTabAndTapAddToGroup(tabTitle: String) {
        mozLongClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(tabTitle))
        mozClick(TabDrawerSelectors.TAB_SELECTION_THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.ADD_TO_GROUP_THREE_DOT_BUTTON)
    }

    fun selectTabsAndCreateFirstTabGroup(
        tabTitle: String,
        tabGroupTitle: String = "",
        tabGroupColor: String = "",
    ): TabDrawerPage {
        selectTabsAndTapAddToGroup(tabTitle)

        if (tabGroupTitle.isNotEmpty()) {
            mozEnterText(tabGroupTitle, TabDrawerSelectors.CREATE_TAB_GROUP_NAME_TEXT_FIELD)
        }

        if (tabGroupColor.isNotEmpty()) {
            mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_COLOR_BUTTON(tabGroupColor))
        }

        mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_SAVE_BUTTON)

        return this
    }

    fun selectTabsAndAddToExistingTabGroup(
        tabTitle: String,
        tabGroupTitle: String,
        numberOfTabs: Int,
        tabGroupColor: String,
    ): TabDrawerPage {
        longClickTabAndTapAddToGroup(tabTitle)
        mozClick(TabDrawerSelectors.TAB_GROUP_ITEM(tabGroupTitle, numberOfTabs, tabGroupColor))
        return this
    }

    fun selectTabsAndAddToNewTabGroup(
        tabTitle: String,
        tabGroupTitle: String = "",
        tabGroupColor: String = "",
    ): TabDrawerPage {
        selectTabsAndTapAddToGroup(tabTitle)
        mozClick(TabDrawerSelectors.ADD_TO_NEW_TAB_GROUP_BUTTON)

        if (tabGroupTitle.isNotEmpty()) {
            mozEnterText(tabGroupTitle, TabDrawerSelectors.CREATE_TAB_GROUP_NAME_TEXT_FIELD)
        }

        if (tabGroupColor.isNotEmpty()) {
            mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_COLOR_BUTTON(tabGroupColor))
        }

        mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_SAVE_BUTTON)
        return this
    }

    fun selectAllTabsAndCreateTabGroup(
        tabGroupTitle: String = "",
        tabGroupColor: String = "",
    ): TabDrawerPage {
        mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.SELECT_ALL_TABS_BUTTON)
        mozClick(TabDrawerSelectors.TAB_SELECTION_THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.ADD_TO_GROUP_THREE_DOT_BUTTON)

        if (tabGroupTitle.isNotEmpty()) {
            mozEnterText(tabGroupTitle, TabDrawerSelectors.CREATE_TAB_GROUP_NAME_TEXT_FIELD)
        }

        if (tabGroupColor.isNotEmpty()) {
            mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_COLOR_BUTTON(tabGroupColor))
        }

        mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_SAVE_BUTTON)

        return this
    }

    /**
     * Select a tab group card in the tray's selection mode. A grid card exposes its title as a child node, but a list
     * card exposes it only through its "Add to ..." content description, so the handle depends on [isListViewEnabled].
     */
    fun selectTabGroupCard(
        tabGroupTitle: String,
        numberOfTabs: Int,
        tabGroupColor: String,
        isListViewEnabled: Boolean,
    ): TabDrawerPage {
        val selector =
            if (isListViewEnabled) {
                TabDrawerSelectors.TAB_GROUP_ITEM(tabGroupTitle, numberOfTabs, tabGroupColor)
            } else {
                TabDrawerSelectors.TAB_ITEM_WITH_TITLE(tabGroupTitle)
            }
        mozClick(selector)
        return this
    }

    /**
     * Merge the already-selected tab groups into a new group from the multi-select menu, naming it [tabGroupTitle] and
     * coloring it [tabGroupColor]. Assumes selection mode is active with the groups to merge selected.
     */
    fun mergeSelectedTabGroupsIntoNewGroup(tabGroupTitle: String, tabGroupColor: String): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_SELECTION_THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.ADD_TO_GROUP_THREE_DOT_BUTTON)
        mozClick(TabDrawerSelectors.ADD_TO_NEW_TAB_GROUP_BUTTON)
        mozEnterText(tabGroupTitle, TabDrawerSelectors.CREATE_TAB_GROUP_NAME_TEXT_FIELD)
        mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_COLOR_BUTTON(tabGroupColor))
        mozClick(TabDrawerSelectors.CREATE_TAB_GROUP_SAVE_BUTTON)
        return this
    }

    fun deleteTabGroupFromTabGroupPage(): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_GROUP_MORE_OPTIONS_BUTTON)
        mozClick(TabDrawerSelectors.TAB_GROUP_MORE_OPTIONS_DELETE_BUTTON)

        return this
    }

    fun closeTabGroup(): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_GROUP_MORE_OPTIONS_BUTTON)
        mozClick(TabDrawerSelectors.TAB_GROUP_MORE_OPTIONS_CLOSE_BUTTON)

        return this
    }

    fun swipCloseTabGroupBottomSheet(): TabDrawerPage {
        mozSwipeElement(TabDrawerSelectors.TAB_GROUP_BOTTOM_SHEET_HANDLE, SwipeDirection.DOWN)
        mozWaitUntilAbsent(TabDrawerSelectors.TAB_GROUP_BOTTOM_SHEET_HANDLE)

        return this
    }

    fun openTabGroupFromTabGroupPage(tabGroupTitle: String, numberOfTabs: Int, tabGroupColor: String): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_GROUPS_BUTTON)
        mozClick(TabDrawerSelectors.TAB_GROUP_ITEM(tabGroupTitle, numberOfTabs, tabGroupColor))

        return this
    }

    fun verifyTabGroupFromTabGroupPage(tabGroupTitle: String, numberOfTabs: Int, tabGroupColor: String): TabDrawerPage {
        mozClick(TabDrawerSelectors.TAB_GROUPS_BUTTON)
        mozVerify(TabDrawerSelectors.TAB_GROUP_ITEM(tabGroupTitle, numberOfTabs, tabGroupColor))

        return this
    }
}
