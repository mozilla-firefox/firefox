/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.TestAssetHelper.articleSummaryAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsCustomizeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ShareOverlaySelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class SettingsCustomizeSimpleToolbarTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909001
    @Critical
    @Test
    fun verifyTheSimpleToolbarLayoutOpenANewTabShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_LAYOUT_SIMPLE)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_OPEN_A_NEW_TAB)
        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
        on.browserPage.mozClick(ToolbarSelectors.NEW_TAB_BUTTON)
        on.searchBar.verifySearchBarPlaceholder()
        on.searchBar
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()
        on.browserPage.mozVerify(ToolbarSelectors.NEW_TAB_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909002
    @Critical
    @Test
    fun verifyTheSimpleToolbarLayoutShareShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        on.settingsCustomize
            .navigateToPage()
            .verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
            .verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_LAYOUT_SIMPLE)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_SHARE)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_SHARE)
        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_SHARE_BUTTON)
        on.browserPage.mozClick(ToolbarSelectors.SIMPLE_TOOLBAR_SHARE_BUTTON)
        on.shareOverlay.mozVerifyElementsByGroup(ShareOverlaySelectors.Group.SHARE_TAB_LAYOUT)
        on.shareOverlay.verifySharingWithSelectedApp(
            appName = Constants.GMAIL_APP_NAME,
            appPackageName = Constants.PackageName.GMAIL_APP,
            content = firstPage.url.toString(),
            subject = firstPage.title,
        )
        restartApp(composeRule.activityRule)
        on.searchBar
            .navigateToPage()
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()
        on.browserPage.mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_SHARE_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909005
    @Critical
    @Test
    fun verifyTheSimpleToolbarLayoutHomepageShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize
            .verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_LAYOUT_SIMPLE)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_HOMEPAGE)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_HOMEPAGE)
        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_HOMEPAGE_BUTTON)
        on.browserPage.mozClick(ToolbarSelectors.SIMPLE_TOOLBAR_HOMEPAGE_BUTTON)
        on.home.mozVerifyElementsByGroup(HomeSelectors.Group.TOP_SITES_COMPOSE).navigateToPage()
        on.searchBar
            .navigateToPage()
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()
        on.browserPage.mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_HOMEPAGE_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909006
    @Critical
    @Test
    fun verifyTheSimpleToolbarLayoutBackShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize
            .verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_LAYOUT_SIMPLE)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_BACK)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_BACK)
        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_BACK_BUTTON)
        on.searchBar
            .navigateToPage()
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.mozClick(ToolbarSelectors.SIMPLE_TOOLBAR_BACK_BUTTON)
        on.browserPage.verifyUrl(firstPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3931201
    @Critical
    @Test
    fun verifyTheSimpleToolbarLayoutNoneShortcutOptionTest() {
        val firstPage = mockWebServer.getGenericAsset(1)

        on.settingsCustomize.navigateToPage()
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_POSITION_TOP)
        on.settingsCustomize
            .verifyOptionIsSelected(SettingsCustomizeSelectors.TOOLBAR_LAYOUT_SIMPLE)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_NONE)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_NONE)
        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstPage.url.toString())
        on.toolbar.mozVerifyElementsByGroup(ToolbarSelectors.Group.SIMPLE_TOOLBAR_NONE_SHORTCUT_ITEMS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4270858
    @Critical
    @Test
    fun verifyTheSimpleToolbarLayoutSummarizePageShortcutOptionTest() {
        val defaultWebPage = mockWebServer.articleSummaryAsset

        on.settingsCustomize.navigateToPage().mozClick(SettingsCustomizeSelectors.SHORTCUT_SUMMARIZE_PAGES)
        on.settingsCustomize.verifyOptionIsSelected(SettingsCustomizeSelectors.SHORTCUT_SUMMARIZE_PAGES)
        on.home.navigateToPage()
        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.browserPage.mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_SUMMARIZE_PAGES_BUTTON)
        on.browserPage
            .mozClick(ToolbarSelectors.SIMPLE_TOOLBAR_SUMMARIZE_PAGES_BUTTON)
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4270857
    @Critical
    @Test
    fun verifyLongTapShortcutDisplaysEditShortcutTest() {
        val firstPage = mockWebServer.getGenericAsset(1)

        on.browserPage
            .navigateToPage(firstPage.url.toString())
            .mozLongClick(ToolbarSelectors.SIMPLE_TOOLBAR_NEW_TAB_BUTTON)
            .mozVerify(ToolbarSelectors.EDIT_SHORTCUT_BUTTON)
            .mozClick(ToolbarSelectors.EDIT_SHORTCUT_BUTTON)
        on.settingsCustomize
            .mozVerify(SettingsCustomizeSelectors.SETTINGS_CUSTOMIZE_TITLE)
            .mozClick(SettingsCustomizeSelectors.SHORTCUT_SHARE)
            .mozPressBack()
        on.browserPage
            .mozVerify(ToolbarSelectors.SIMPLE_TOOLBAR_SHARE_BUTTON)
            .mozLongClick(ToolbarSelectors.SIMPLE_TOOLBAR_SHARE_BUTTON)
            .mozVerify(ToolbarSelectors.EDIT_SHORTCUT_BUTTON)
            .mozClick(ToolbarSelectors.EDIT_SHORTCUT_BUTTON)
        on.settingsCustomize.mozVerify(SettingsCustomizeSelectors.SETTINGS_CUSTOMIZE_TITLE)
    }
}
