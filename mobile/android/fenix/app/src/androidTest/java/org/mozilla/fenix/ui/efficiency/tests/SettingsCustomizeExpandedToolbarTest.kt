/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import android.content.pm.ActivityInfo
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Test
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.AppAndSystemHelper.setScreenOrientation
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.tallPageAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig
import org.mozilla.fenix.ui.efficiency.selectors.BookmarksSelectors
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsCustomizeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ShareOverlaySelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class SettingsCustomizeExpandedToolbarTest : BaseTest(LaunchConfig(shouldUseExpandedToolbar = true)) {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333211
    // Converted from legacy NavigationToolbarExpandedTest.verifyTheExpandedToolbarAddBookmarkButtonTest
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarAddBookmarkButtonTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(website.url.toString())
        on.browserPage.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
        on.browserPage.mozWaitUntilAbsent(BrowserPageSelectors.SNACKBAR)
        on.browserPage.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_EDIT_BOOKMARK_BUTTON)
        on.bookmarks.mozVerifyElementsByGroup(BookmarksSelectors.Group.EDIT_BOOKMARKS_VIEW)
        on.bookmarks.mozClick(BookmarksSelectors.DELETE_BOOKMARK_BUTTON)
        on.browserPage.verifyPageContent(website.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333212
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarShareButtonTest() {
        val website = mockWebServer.getGenericAsset(1)

        // Disable the edge back-gesture so it doesn't intercept taps on the bottom navigation bar.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        on.browserPage.navigateToPage(website.url.toString())
        on.browserPage.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
        on.shareOverlay.mozVerifyElementsByGroup(ShareOverlaySelectors.Group.SHARE_TAB_LAYOUT)
        on.shareOverlay.verifySharingWithSelectedApp(
            appName = Constants.GMAIL_APP_NAME,
            appPackageName = Constants.PackageName.GMAIL_APP,
            content = website.url.toString(),
            subject = website.title,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333175
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarItemsInLandscapeModeTest() {
        // setScreenOrientation requires the concretely-typed compose rule; BaseTest exposes it star-projected
        // but the runtime activity is always HomeActivity (see BaseTest's `{ it.activity }`).
        @Suppress("UNCHECKED_CAST")
        val orientationRule = composeRule as AndroidComposeTestRule<HomeActivityIntentTestRule, HomeActivity>

        val website = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(website.url.toString()).verifyPageContent(website.content)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)

        on.toolbar.verifyToolbarIsAtTop()
        on.browserPage.verifyUrl(website.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.SITE_INFO_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_BACK_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_FORWARD_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_REFRESH_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("1"))
        on.browserPage.mozVerify(BrowserPageSelectors.MAIN_MENU_BUTTON)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333213
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarNewTabButtonInLandscapeModeTest() {
        // setScreenOrientation requires the concretely-typed compose rule; BaseTest exposes it star-projected
        // but the runtime activity is always HomeActivity (see BaseTest's `{ it.activity }`).
        @Suppress("UNCHECKED_CAST")
        val orientationRule = composeRule as AndroidComposeTestRule<HomeActivityIntentTestRule, HomeActivity>

        val website = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(website.url.toString()).verifyPageContent(website.content)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)

        // Tapping New tab opens a fresh tab focused on the search bar.
        on.browserPage.mozClick(ToolbarSelectors.NEW_TAB_BUTTON)
        on.searchBar.mozVerify(SearchBarSelectors.SEARCH_BAR_PLACEHOLDER)
        on.searchBar.mozVerifyKeyboardVisible()

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333214
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarTabTrayButtonInLandscapeModeTest() {
        // setScreenOrientation requires the concretely-typed compose rule; BaseTest exposes it star-projected
        // but the runtime activity is always HomeActivity (see BaseTest's `{ it.activity }`).
        @Suppress("UNCHECKED_CAST")
        val orientationRule = composeRule as AndroidComposeTestRule<HomeActivityIntentTestRule, HomeActivity>

        val website = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(website.url.toString()).verifyPageContent(website.content)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)

        // The tab-counter button opens the tab tray; navigateToPage routes through that edge.
        on.tabDrawer.navigateToPage()
        on.tabDrawer.verifyExistingOpenTabs(website.title)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333178
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarMainMenuButtonInLandscapeModeTest() {
        // setScreenOrientation requires the concretely-typed compose rule; BaseTest exposes it star-projected
        // but the runtime activity is always HomeActivity (see BaseTest's `{ it.activity }`).
        @Suppress("UNCHECKED_CAST")
        val orientationRule = composeRule as AndroidComposeTestRule<HomeActivityIntentTestRule, HomeActivity>

        val website = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(website.url.toString()).verifyPageContent(website.content)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)

        on.browserPage.openMainMenu()
        on.mainMenu.mozVerifyElementsByGroup(MainMenuSelectors.Group.BROWSER_VIEW_MAIN_MENU_ITEMS)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333215
    // Converted from legacy NavigationToolbarExpandedTest.verifyTheExpandedToolbarMainMenuButtonTest
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarMainMenuButtonTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(website.url.toString())
        on.browserPage.verifyPageContent(website.content)
        on.mainMenu.navigateToPage().mozVerifyElementsByGroup(MainMenuSelectors.Group.BROWSER_VIEW_MAIN_MENU_ITEMS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333214
    // Converted from legacy NavigationToolbarExpandedTest.verifyTheExpandedToolbarTabTrayButtonTest
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarTabTrayButtonTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(website.url.toString())
        on.browserPage.verifyPageContent(website.content)
        on.tabDrawer.navigateToPage()
        on.tabDrawer.verifyExistingOpenTabs(website.title)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333213
    // Converted from legacy NavigationToolbarExpandedTest.verifyTheExpandedToolbarNewTabButtonTest
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarNewTabButtonTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(website.url.toString())
        on.browserPage.verifyPageContent(website.content)
        // Tap the expanded-toolbar "New tab" button (device-level content-desc, present on the browser
        // view because this class launches with shouldUseExpandedToolbar = true) — opens a new tab in
        // the search/edit view.
        on.browserPage.mozClick(ToolbarSelectors.NEW_TAB_BUTTON)
        // The new tab lands in the search view in edit mode: verify it opened (search-engine selector),
        // the address bar is in edit mode showing its placeholder, and the keyboard came up for input.
        on.searchBar.mozVerifyReadiness()
        on.searchBar.mozVerify(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.searchBar.mozVerify(SearchBarSelectors.SEARCH_BAR_PLACEHOLDER)
        on.searchBar.mozVerifyKeyboardVisible()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333206
    @SmokeTest
    @Test
    fun verifyHomepageItemsWithTabStripTest() {
        composeRule.activityRule.applySettingsExceptions {
            it.isTabStripEnabled = true
        }
        // With the tab strip enabled and the expanded toolbar, the address bar sits at the top while the
        // navigation bar with its actions sits at the bottom.
        on.toolbar.verifyToolbarIsAtTop()
        on.toolbar.verifyNavBarIsAtBottom()
        on.home.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
        on.home.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
        on.home.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
        on.home.mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("0"))
        on.home.mozVerify(HomeSelectors.MAIN_MENU_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333193
    @SmokeTest
    @Test
    fun verifyTheTabStripUITest() {
        val website = mockWebServer.getGenericAsset(1)

        composeRule.activityRule.applySettingsExceptions {
            it.isTabStripEnabled = true
        }

        on.browserPage
            .navigateToPage(website.url.toString())
            .verifyPageContent(website.content)
            .verifyUrl(website.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.SITE_INFO_BUTTON)

        // Tab strip enabled with the expanded toolbar: address bar at top, the open tab shown in the strip,
        // and the navigation bar with its actions at the bottom.
        on.toolbar.verifyToolbarIsAtTop()
        on.browserPage.mozVerify(ToolbarSelectors.TAB_STRIP_TAB(website.title))
        on.browserPage.mozVerify(ToolbarSelectors.TAB_STRIP_CLOSE_TAB_BUTTON(website.title))
        on.toolbar.verifyNavBarIsAtBottom()
        on.browserPage.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
        on.browserPage.mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("1"))
        on.browserPage.mozVerify(BrowserPageSelectors.MAIN_MENU_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333194
    @SmokeTest
    @Test
    fun verifyTheNewTabButtonWithTabStripEnabledTest() {
        val website = mockWebServer.getGenericAsset(1)

        composeRule.activityRule.applySettingsExceptions {
            it.isTabStripEnabled = true
        }

        on.browserPage.navigateToPage(website.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.TAB_STRIP_TAB_COUNTER_WITH_COUNT("1"))
        on.browserPage.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)

        // Tapping New tab opens a fresh tab focused on the search bar.
        on.browserPage.mozClick(ToolbarSelectors.NEW_TAB_BUTTON)
        on.searchBar.mozVerify(SearchBarSelectors.SEARCH_BAR_PLACEHOLDER)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333195
    @SmokeTest
    @Test
    fun verifyTabsTrayWithTabStripEnabledTest() {
        val website = mockWebServer.getGenericAsset(1)

        composeRule.activityRule.applySettingsExceptions {
            it.isTabStripEnabled = true
        }

        on.browserPage.navigateToPage(website.url.toString())
        on.tabDrawer.navigateToPage()
        on.tabDrawer.verifyExistingOpenTabs(website.title)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333216
    @Critical
    @Test
    fun verifyTheScrollToHideToolbarOptionTest() {
        val tallPage = mockWebServer.tallPageAsset

        on.settingsCustomize
            .navigateToPage()
            .mozVerifyOptionSwitchIsChecked(SettingsCustomizeSelectors.SCROLL_TO_HIDE_TOOLBAR_OPTION)
            .mozClick(SettingsCustomizeSelectors.SCROLL_TO_HIDE_TOOLBAR_OPTION)
            .mozVerifyOptionSwitchIsNotChecked(SettingsCustomizeSelectors.SCROLL_TO_HIDE_TOOLBAR_OPTION)
        on.browserPage
            .navigateToPage(tallPage.url.toString())
            .verifyPageContent(tallPage.content)
            .mozVerify(ToolbarSelectors.TOOLBAR)
        on.browserPage.scrollWebContent(SwipeDirection.UP).mozVerify(ToolbarSelectors.TOOLBAR)
        on.settingsCustomize
            .navigateToPage()
            .mozVerifyOptionSwitchIsNotChecked(SettingsCustomizeSelectors.SCROLL_TO_HIDE_TOOLBAR_OPTION)
            .mozClick(SettingsCustomizeSelectors.SCROLL_TO_HIDE_TOOLBAR_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsCustomizeSelectors.SCROLL_TO_HIDE_TOOLBAR_OPTION)
        on.browserPage.navigateToPage().mozVerify(ToolbarSelectors.TOOLBAR)
        on.browserPage.scrollWebContent(SwipeDirection.UP).mozWaitUntilAbsent(ToolbarSelectors.TOOLBAR)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3908989
    @Critical
    @Test
    fun verifyTheExpandedToolbarLayoutBackShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize
            .verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_ADD_BOOKMARK)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_BACK)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_BACK)
        on.home.navigateToPage()
        // Verify each button separately, not via mozVerifyElementsByGroup: the navbar rebuilds as navigation
        // state settles after load, so the group's "all present in one sweep" check is flaky.
        on.browserPage
            .navigateToPage(firstPage.url.toString())
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_BACK_BUTTON)
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
            .mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
            .mozVerify(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)
            .mozVerify(ToolbarSelectors.MAIN_MENU_BUTTON)
        on.searchBar
            .navigateToPage()
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_BACK_BUTTON)
        on.browserPage.verifyUrl(firstPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3908988
    @Critical
    @Test
    fun verifyTheExpandedToolbarLayoutHomePageShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize
            .verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_ADD_BOOKMARK)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_HOMEPAGE)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_HOMEPAGE)
        on.home.navigateToPage()
        // Verify each button separately, not via mozVerifyElementsByGroup: the navbar rebuilds as navigation
        // state settles after load, so the group's "all present in one sweep" check is flaky.
        on.browserPage
            .navigateToPage(firstPage.url.toString())
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_HOMEPAGE_BUTTON)
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
            .mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
            .mozVerify(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)
            .mozVerify(ToolbarSelectors.MAIN_MENU_BUTTON)
        on.browserPage.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_HOMEPAGE_BUTTON)
        on.home.mozVerifyElementsByGroup(HomeSelectors.Group.TOP_SITES_COMPOSE).navigateToPage()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3908986
    @Critical
    @Test
    fun verifyTheExpandedToolbarLayoutAddBookmarkShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_ADD_BOOKMARK)
        on.home.navigateToPage()
        // Verify each button separately, not via mozVerifyElementsByGroup: the navbar rebuilds as navigation
        // state settles after load, so the group's "all present in one sweep" check is flaky.
        on.browserPage
            .navigateToPage(firstPage.url.toString())
            .verifyUrl(firstPage.url.toString())
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
            .mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
            .mozVerify(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)
            .mozVerify(ToolbarSelectors.MAIN_MENU_BUTTON)
            .mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
        on.bookmarks.navigateToPage().mozVerify(BookmarksSelectors.BOOKMARK_ITEM_TITLE(firstPage.title))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3911787
    @Critical
    @Test
    fun verifyTheExpandedToolbarHomepageItemsTest() {
        on.home.navigateToPage()
        on.toolbar
            .verifyToolbarIsAtTop()
            .verifyNavBarIsAtBottom()
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
            .mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
            .mozVerify(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)
            .mozVerify(ToolbarSelectors.MAIN_MENU_BUTTON)
        on.settingsCustomize.navigateToPage().mozClick(SettingsCustomizeSelectors.TOOLBAR_POSITION_BOTTOM)
        on.home.navigateToPage()
        on.toolbar
            .verifyToolbarIsAtBottom()
            .verifyNavBarIsAtBottom()
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON)
            .mozVerify(ToolbarSelectors.EXPANDED_TOOLBAR_SHARE_BUTTON)
            .mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
            .mozVerify(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)
            .mozVerify(ToolbarSelectors.MAIN_MENU_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333225
    @Critical
    @Test
    fun swipeToSwitchExpandedToolbarTabsTest() {
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333176
    @Critical
    @Test
    fun verifyTheExpandedToolbarNavigationThroughThePagesInTheSameTabTest() {
        val firstWebPage = mockWebServer.getGenericAsset(4)
        val secondWebPage = mockWebServer.getGenericAsset(1)

        @Suppress("UNCHECKED_CAST")
        val orientationRule = composeRule as AndroidComposeTestRule<HomeActivityIntentTestRule, HomeActivity>

        on.browserPage
            .navigateToPage(firstWebPage.url.toString())
            .clickPageContent("Link 1")
            .verifyPageContent(secondWebPage.content)

        // Each back/forward loads a page, and GeckoView's orientation delegate (ScreenOrientationFeature) resets
        // requestedOrientation to UNSPECIFIED on every navigation - which falls back to portrait here. So landscape
        // has to be re-applied after each navigation rather than set once.
        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)

        on.toolbar.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_BACK_BUTTON)
        on.browserPage.verifyPageContent(firstWebPage.content)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        on.toolbar.mozClick(ToolbarSelectors.EXPANDED_TOOLBAR_FORWARD_BUTTON)
        on.browserPage.verifyPageContent(secondWebPage.content)

        setScreenOrientation(orientationRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333208
    @Critical
    @Test
    fun verifyTappingTheExpandedToolbarFromAnOpenedTabTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(firstWebPage.url.toString())
        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.CLEAR_BUTTON)
            .mozEnterText("local", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerify(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozVerify(SearchBarSelectors.VOICE_SEARCH_BUTTON)
            .mozVerify(SearchBarSelectors.CLEAR_BUTTON)
            .mozVerify(SearchBarSelectors.AWESOMEBAR_SUGGESTION)
            .mozVerify(SearchBarSelectors.SEARCH_SUGGESTION_WITH_TEXT(firstWebPage.url.toString()))
            .mozVerifyKeyboardVisible()
            .mozClick(SearchBarSelectors.CLEAR_BUTTON)
            .mozVerify(SearchBarSelectors.SEARCH_BAR_PLACEHOLDER)
            .mozEnterText("host", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerify(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozVerify(SearchBarSelectors.VOICE_SEARCH_BUTTON)
            .mozVerify(SearchBarSelectors.CLEAR_BUTTON)
            .mozVerify(SearchBarSelectors.AWESOMEBAR_SUGGESTION)
            .mozVerify(SearchBarSelectors.SEARCH_SUGGESTION_WITH_TEXT(firstWebPage.url.toString()))
            .mozVerifyKeyboardVisible()
    }
}
