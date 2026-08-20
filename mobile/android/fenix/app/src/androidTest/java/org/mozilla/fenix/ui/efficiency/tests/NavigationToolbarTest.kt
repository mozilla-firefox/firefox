/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.genericAssets
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsCustomizeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class NavigationToolbarTest : BaseTest() {

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/987326
    @SmokeTest
    @Test
    fun swipeToSwitchTabTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        // Disable the edge back-gesture so it doesn't intercept the horizontal swipes on the toolbar.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        on.browserPage.navigateToPage(firstWebPage.url.toString())
        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.FAB)
        on.searchBar
            .mozEnterText(secondWebPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()

        // A fast flick (low step count) on the navigation toolbar switches to the adjacent tab; a slow
        // drag doesn't register as a tab-switch gesture.
        on.browserPage.mozSwipeElement(ToolbarSelectors.TOOLBAR, SwipeDirection.RIGHT, steps = 2)
        on.browserPage.verifyUrl(firstWebPage.url.toString())
        on.browserPage.mozSwipeElement(ToolbarSelectors.TOOLBAR, SwipeDirection.LEFT, steps = 2)
        on.browserPage.verifyUrl(secondWebPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135067
    @SmokeTest
    @Test
    fun verifyTheNewTabButtonTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        // Normal browsing: opening a second tab via the toolbar "New tab" button bumps the counter to 2.
        on.browserPage.navigateToPage(firstPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("1"))
        on.browserPage.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
        on.browserPage.mozClick(ToolbarSelectors.NEW_TAB_BUTTON)
        on.searchBar
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()
        on.browserPage.mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("2"))

        // Switch to private browsing and repeat with the "New private tab" button.
        on.home.navigateToPage()
        on.home.mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)

        on.browserPage.navigateToPage(firstPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.PRIVATE_TAB_COUNTER_WITH_COUNT("1"))
        on.browserPage.mozVerify(ToolbarSelectors.NEW_PRIVATE_TAB_BUTTON)
        on.browserPage.mozClick(ToolbarSelectors.NEW_PRIVATE_TAB_BUTTON)
        on.searchBar
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()
        on.browserPage.mozVerify(ToolbarSelectors.PRIVATE_TAB_COUNTER_WITH_COUNT("2"))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4124764
    @SmokeTest
    @Test
    fun verifySwipeToolbarVerticallyOpensTheTabDrawerTest() {
        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())

        val secondPage = mockWebServer.getGenericAsset(2)

        on.browserPage.navigateToPage(secondPage.url.toString())
        on.toolbar.mozSwipeElement(ToolbarSelectors.TOOLBAR_URL_BOX_UIAUTOMATOR, SwipeDirection.DOWN, steps = 3)
        on.tabDrawer
            .verifyExistingOpenTabs(webPages[0].title, secondPage.title)
            .mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(webPages[0].title))
        on.browserPage.verifyPageContent(webPages[0].content)
        on.settingsCustomize.navigateToPage().mozClick(SettingsCustomizeSelectors.TOOLBAR_POSITION_BOTTOM)
        on.browserPage.navigateToPage()
        on.toolbar.mozSwipeElement(ToolbarSelectors.TOOLBAR_URL_BOX_UIAUTOMATOR, SwipeDirection.UP, steps = 3)
        on.tabDrawer
            .verifyExistingOpenTabs(webPages[0].title, secondPage.title)
            .mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(secondPage.title))
        on.browserPage.verifyPageContent(secondPage.content)
    }
}
