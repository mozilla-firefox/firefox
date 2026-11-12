/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.genericAssets
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.closeApp
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors.DELETE_TAB_GROUP_DIALOG_CANCEL_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors.DELETE_TAB_GROUP_DIALOG_DELETE_GROUP_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors.TAB_GROUPS_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors.TAB_ITEM_WITH_TITLE
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class TabbedBrowsingTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1046683
    @Test
    fun verifySyncedTabsWhenUserIsNotSignedInTest() {
        on.tabDrawer.navigateToPage().mozVerifyElementIsNotSelected(TabDrawerSelectors.SYNCED_TABS_BUTTON)
        on.tabDrawer
            .mozClick(TabDrawerSelectors.SYNCED_TABS_BUTTON)
            .mozVerifyElementIsSelected(TabDrawerSelectors.SYNCED_TABS_BUTTON)
        on.tabDrawer
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.TAB_DRAWER_UNAUTHENTICATED_SYNCED_TABS)
            .mozClick(TabDrawerSelectors.SIGN_IN_TO_SYNC_BUTTON)
        on.settingsTurnOnSync.mozVerifyReadiness()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903587
    @SmokeTest
    @Test
    fun verifyPrivateTabsTrayWithOpenTabTest() {
        val website = mockWebServer.getGenericAsset(1)

        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.PRIVATE_TABS_PAGE_BUTTON)
        on.searchBar.navigateToPage()
        on.browserPage.navigateToPage(website.url.toString())
        on.tabDrawer.navigateToPage().mozVerifyElementIsNotSelected(TabDrawerSelectors.NORMAL_BROWSING_OPEN_TABS_BUTTON)
        on.tabDrawer.mozVerifyElementIsSelected(TabDrawerSelectors.PRIVATE_TABS_PAGE_BUTTON)
        on.tabDrawer.mozVerifyElementIsNotSelected(TabDrawerSelectors.TAB_GROUPS_BUTTON)
        on.tabDrawer.mozVerifyElementIsNotSelected(TabDrawerSelectors.SYNCED_TABS_BUTTON)
        on.tabDrawer.mozVerify(TabDrawerSelectors.THREE_DOT_BUTTON)
        on.tabDrawer.mozVerify(TabDrawerSelectors.PRIVATE_TABS_LIST)
        on.tabDrawer.mozVerify(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(website.title))
        on.tabDrawer.mozVerify(TabDrawerSelectors.TAB_ITEM_CLOSE)
        on.tabDrawer.mozVerify(TabDrawerSelectors.TAB_ITEM_THUMBNAIL)
        on.tabDrawer.mozVerify(TabDrawerSelectors.FAB)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034499
    @SmokeTest
    @Test
    fun verifyTheTabsGroupButtonTabsTrayPositionTest() {
        on.tabDrawer.navigateToPage().mozVerifyElementsByGroup(TabDrawerSelectors.Group.TAB_DRAWER_BANNER_BUTTONS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034504
    @SmokeTest
    @Test
    fun verifyTheDeletionOfATabGroupTest() {
        val webPages = mockWebServer.genericAssets

        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())

        on.tabDrawer.navigateToPage()
        on.tabDrawer.selectAllTabsAndCreateTabGroup()
        on.tabDrawer.mozClick(TAB_GROUPS_BUTTON)
        on.tabDrawer
            .deleteTabGroupFromTabGroupPage()
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.DELETE_TAB_GROUP_DIALOG)
            .mozClick(DELETE_TAB_GROUP_DIALOG_CANCEL_BUTTON)
        on.tabDrawer
            .deleteTabGroupFromTabGroupPage()
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.DELETE_TAB_GROUP_DIALOG)
            .mozClick(DELETE_TAB_GROUP_DIALOG_DELETE_GROUP_BUTTON)
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.EMPTY_TAB_GROUPS_TAB_DRAWER_VIEW)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034505
    @SmokeTest
    @Test
    fun verifyTheClosingOfATabGroupTest() {
        val tabGroupTitle = "Group 1"
        val tabGroupColor = "Grey"

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())

        on.tabDrawer.navigateToPage()
        on.tabDrawer
            .selectAllTabsAndCreateTabGroup(tabGroupColor = tabGroupColor)
            .mozVerify(TAB_ITEM_WITH_TITLE(tabTitle = tabGroupTitle))
        on.tabDrawer
            .closeTabGroup()
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.EMPTY_NORMAL_BROWSING_TAB_DRAWER_VIEW)
        on.tabDrawer
            .openTabGroupFromTabGroupPage(
                tabGroupTitle = tabGroupTitle,
                numberOfTabs = 2,
                tabGroupColor = tabGroupColor,
            )
            .swipCloseTabGroupBottomSheet()
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.NORMAL_BROWSING_TAB_DRAWER_VIEW)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034506
    @SmokeTest
    @Test
    fun verifyTheCreationOfATabGroupBySelectingTabsTest() {
        val tabGroupTitle = "Mozilla"
        val tabGroupColor = "Grey"

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())

        on.tabDrawer.navigateToPage()
        on.tabDrawer.selectTabsAndCreateFirstTabGroup(
            tabTitle = webPages[0].title,
            tabGroupTitle = tabGroupTitle,
            tabGroupColor = tabGroupColor,
        )

        closeApp(composeRule.activityRule)
        restartApp(composeRule.activityRule)

        verifyGroup(
            tabGroupTitle = tabGroupTitle,
            tabGroupColor = tabGroupColor,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034502
    @Test
    fun verifyCreationOfNewGroupWhenGroupExistsTest() {
        val firstTabGroupTitle = "Mozilla"
        val firstTabGroupColor = "Grey"
        val secondTabGroupTitle = "Test"
        val secondTabGroupColor = "Blue"

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())

        on.tabDrawer.navigateToPage()
        on.tabDrawer.selectTabsAndCreateFirstTabGroup(
            tabTitle = webPages[0].title,
            tabGroupTitle = firstTabGroupTitle,
            tabGroupColor = firstTabGroupColor,
        )
        on.tabDrawer.selectTabsAndAddToNewTabGroup(
            tabTitle = webPages[1].title,
            tabGroupTitle = secondTabGroupTitle,
            tabGroupColor = secondTabGroupColor,
        )

        closeApp(composeRule.activityRule)
        restartApp(composeRule.activityRule)

        verifyGroup(
            tabGroupTitle = firstTabGroupTitle,
            tabGroupColor = firstTabGroupColor,
            numberOfTabs = 1,
        )
        verifyGroup(
            tabGroupTitle = secondTabGroupTitle,
            tabGroupColor = secondTabGroupColor,
            numberOfTabs = 1,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4162826
    @Critical
    @Test
    fun verifyAddingTabToExistingGroupViaSelectionTest() {
        val tabGroupTitle = "Mozilla"
        val tabGroupColor = "Grey"

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())

        on.tabDrawer.navigateToPage()
        on.tabDrawer.selectTabsAndCreateFirstTabGroup(
            tabTitle = webPages[0].title,
            tabGroupTitle = tabGroupTitle,
            tabGroupColor = tabGroupColor,
        )
        on.tabDrawer.selectTabsAndAddToExistingTabGroup(
            tabTitle = webPages[1].title,
            tabGroupTitle = tabGroupTitle,
            numberOfTabs = 1,
            tabGroupColor = tabGroupColor,
        )

        closeApp(composeRule.activityRule)
        restartApp(composeRule.activityRule)

        verifyGroup(
            tabGroupTitle = tabGroupTitle,
            tabGroupColor = tabGroupColor,
            numberOfTabs = 2,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2228470
    @SmokeTest
    @Test
    fun privateTabsDoNotPersistAfterClosingAppTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        on.home.mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)
        on.browserPage.navigateToPage(firstWebPage.url.toString())
        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.FAB)
        on.searchBar
            .mozEnterText(secondWebPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage()

        closeApp(composeRule.activityRule)
        restartApp(composeRule.activityRule)

        on.home.navigateToPage().mozVerify(HomeSelectors.PRIVATE_BROWSING_INFO_CARD_TITLE)
        on.tabDrawer.navigateToPage().mozVerify(TabDrawerSelectors.EMPTY_PRIVATE_TABS_LIST)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903592
    @SmokeTest
    @Test
    fun verifyCloseAllPrivateTabsNotificationTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        on.home.mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)
        on.browserPage
            .navigateToPage(defaultWebPage.url.toString())
            .verifyPageContentWithReload(defaultWebPage.url.toString(), defaultWebPage.content)

        on.notification.openNotificationTray().verifyPrivateTabsNotification().clickClosePrivateTabsNotification()

        // Tapping the notification erases private tabs and relaunches to the private homepage (which
        // also collapses the shade); the info card confirms both that the sheet closed and that we
        // landed on the private homepage.
        on.home.mozVerify(HomeSelectors.PRIVATE_BROWSING_INFO_CARD_TITLE)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903598
    @SmokeTest
    @Test
    fun shareTabsFromTabsTrayTest() {
        val firstWebsite = mockWebServer.getGenericAsset(1)
        val secondWebsite = mockWebServer.getGenericAsset(2)

        on.browserPage
            .navigateToPage(firstWebsite.url.toString())
            .verifyPageContentWithReload(firstWebsite.url.toString(), firstWebsite.content)
        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.FAB)
        on.searchBar
            .mozEnterText(secondWebsite.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage().verifyPageContentWithReload(secondWebsite.url.toString(), secondWebsite.content)

        on.tabDrawer
            .navigateToPage()
            .verifyExistingOpenTabs(firstWebsite.title, secondWebsite.title)
            .selectTabsForSharing(firstWebsite.title, secondWebsite.title)
            .shareSelectedTabs()

        on.shareOverlay
            .verifyShareTabsOverlay(firstWebsite.title, secondWebsite.title)
            .verifySharingWithSelectedApp(
                appName = Constants.GMAIL_APP_NAME,
                appPackageName = Constants.PackageName.GMAIL_APP,
                content = "${firstWebsite.url}\n\n${secondWebsite.url}",
                subject = "${firstWebsite.title}, ${secondWebsite.title}",
            )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3968085
    @SmokeTest
    @Test
    fun verifyTheSearchTabsFunctionalityTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        on.browserPage
            .navigateToPage(firstWebPage.url.toString())
            .verifyPageContentWithReload(firstWebPage.url.toString(), firstWebPage.content)
        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.FAB)
        on.searchBar
            .mozEnterText(secondWebPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.verifyPageContentWithReload(secondWebPage.url.toString(), secondWebPage.content).navigateToPage()

        on.tabDrawer
            .navigateToPage()
            .openTabSearch()
            .typeInTabSearch("android")
            .mozVerifyElementsByGroup(TabDrawerSelectors.Group.TAB_SEARCH_NO_RESULTS)
        on.tabDrawer
            .clearTabSearch()
            .typeInTabSearch("localhost")
            .mozVerify(TabDrawerSelectors.TAB_SEARCH_RESULT(firstWebPage.title))
            .mozClick(TabDrawerSelectors.TAB_SEARCH_RESULT(firstWebPage.title))

        on.browserPage.verifyPageContent(firstWebPage.content).mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("2"))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1126911
    @Critical
    @Test
    fun verifyTabsTrayGridViewTest() {
        appContext.components.settings.gridTabView = true
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[3].url.toString())

        on.tabDrawer
            .navigateToPage()
            .verifyNormalTabsList()
            .verifyOpenTabsOrder(position = 1, tabTitle = webPages[0].title)
            .verifyOpenTabsOrder(position = 2, tabTitle = webPages[1].title)
            .verifyOpenTabsOrder(position = 3, tabTitle = webPages[2].title)
            .verifyOpenTabsOrder(position = 4, tabTitle = webPages[3].title)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3024942
    @Critical
    @Test
    fun verifyTabsTrayListViewTest() {
        appContext.components.settings.gridTabView = false
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[3].url.toString())

        on.tabDrawer
            .navigateToPage()
            .verifyNormalTabsList()
            .verifyOpenTabsOrder(position = 1, tabTitle = webPages[0].title, isListViewEnabled = true)
            .verifyOpenTabsOrder(position = 2, tabTitle = webPages[1].title, isListViewEnabled = true)
            .verifyOpenTabsOrder(position = 3, tabTitle = webPages[2].title, isListViewEnabled = true)
            .verifyOpenTabsOrder(position = 4, tabTitle = webPages[3].title, isListViewEnabled = true)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2443938
    @Critical
    @Test
    fun selectMultipleTabsTest() {
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())

        on.tabDrawer
            .navigateToPage()
            .verifyExistingOpenTabs(webPages[0].title, webPages[1].title)
            .mozLongClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(webPages[0].title))
            .mozVerify(TabDrawerSelectors.SELECTION_COUNTER(1))
            .mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(webPages[1].title))
            .mozVerify(TabDrawerSelectors.SELECTION_COUNTER(2))
            .mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(webPages[1].title))
            .mozVerify(TabDrawerSelectors.SELECTION_COUNTER(1))
            .mozClick(TabDrawerSelectors.EXIT_MULTISELECT_BUTTON)
            .mozWaitUntilAbsent(TabDrawerSelectors.SELECTION_COUNTER(1))
            .mozVerify(TabDrawerSelectors.NORMAL_BROWSING_OPEN_TABS_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1121915
    @Critical
    @Test
    fun closePagesFromTabsSelectionScreenTest() {
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())

        on.tabDrawer
            .navigateToPage()
            .verifyExistingOpenTabs(webPages[0].title, webPages[1].title)
            .mozLongClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(webPages[0].title))
            .mozVerify(TabDrawerSelectors.SELECTION_COUNTER(1))
            .mozClick(TabDrawerSelectors.TAB_ITEM_WITH_TITLE(webPages[1].title))
            .mozVerify(TabDrawerSelectors.SELECTION_COUNTER(2))
            .mozClick(TabDrawerSelectors.CLOSE_SELECTED_TABS_BUTTON)
            .mozVerify(TabDrawerSelectors.TABS_CLOSED_SNACKBAR(2))
            .mozClick(TabDrawerSelectors.SNACKBAR_UNDO_BUTTON)

        on.tabDrawer.verifyExistingOpenTabs(webPages[0].title, webPages[1].title)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903599
    @Critical
    @Test
    fun closeAllTabsTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        on.browserPage
            .navigateToPage(defaultWebPage.url.toString())
            .verifyPageContentWithReload(defaultWebPage.url.toString(), defaultWebPage.content)
        on.tabDrawer
            .navigateToPage()
            .verifyNormalTabsList()
            .mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
            .mozVerify(TabDrawerSelectors.CLOSE_ALL_TABS_BUTTON)
            .mozVerify(TabDrawerSelectors.SELECT_TABS_BUTTON)
            .mozClick(TabDrawerSelectors.CLOSE_ALL_TABS_BUTTON)
            .mozVerify(TabDrawerSelectors.CLOSE_ALL_TABS_CONFIRM_BUTTON)
            .mozClick(TabDrawerSelectors.CLOSE_ALL_TABS_CONFIRM_BUTTON)
        on.browserPage.verifyTabCounter("0")

        on.home.navigateToPage()
        on.home.switchToPrivateBrowsingMode()
        on.browserPage
            .navigateToPage(defaultWebPage.url.toString())
            .verifyPageContentWithReload(defaultWebPage.url.toString(), defaultWebPage.content)
        on.tabDrawer
            .navigateToPage()
            .mozVerify(TabDrawerSelectors.PRIVATE_TABS_LIST)
            .mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
            .mozVerify(TabDrawerSelectors.CLOSE_ALL_TABS_BUTTON)
            .mozClick(TabDrawerSelectors.CLOSE_ALL_TABS_BUTTON)
            .mozVerify(TabDrawerSelectors.CLOSE_ALL_TABS_CONFIRM_BUTTON)
            .mozClick(TabDrawerSelectors.CLOSE_ALL_TABS_CONFIRM_BUTTON)
        on.browserPage.verifyTabCounter("0", isPrivateBrowsingEnabled = true)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903591
    @Critical
    @Test
    fun closingPrivateTabsTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        on.home.navigateToPage()
        on.home.switchToPrivateBrowsingMode()
        on.browserPage
            .navigateToPage(defaultWebPage.url.toString())
            .verifyPageContentWithReload(defaultWebPage.url.toString(), defaultWebPage.content)
        on.tabDrawer
            .navigateToPage()
            .verifyExistingOpenTabs(defaultWebPage.title)
            .closeTabWithTitle(defaultWebPage.title)
            .mozVerify(TabDrawerSelectors.PRIVATE_TAB_CLOSED_SNACKBAR)
            .mozClick(TabDrawerSelectors.SNACKBAR_UNDO_BUTTON)
        on.browserPage.verifyTabCounter("1", isPrivateBrowsingEnabled = true)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903589
    @Critical
    @Test
    fun switchBetweenNormalAndPrivateBrowsingTest() {
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val webPages = mockWebServer.genericAssets

        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())

        on.home.navigateToPage()
        on.home.switchToPrivateBrowsingMode()
        on.browserPage
            .navigateToPage(webPages[2].url.toString())
            .verifyPageContentWithReload(webPages[2].url.toString(), webPages[2].content)
        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.FAB)
        on.searchBar
            .mozEnterText(webPages[3].url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.browserPage.navigateToPage().verifyPageContentWithReload(webPages[3].url.toString(), webPages[3].content)

        // From private browsing, open the (private) tabs tray and switch pages across it.
        on.tabDrawer.navigateToPage().verifyExistingOpenTabs(webPages[2].title, webPages[3].title)
        on.tabDrawer.mozVerify(TabDrawerSelectors.PRIVATE_TABS_LIST)
        on.tabDrawer.mozClick(TabDrawerSelectors.NORMAL_BROWSING_OPEN_TABS_BUTTON)
        on.tabDrawer.mozVerify(TabDrawerSelectors.NORMAL_TABS_LIST)
        on.tabDrawer.verifyExistingOpenTabs(webPages[0].title, webPages[1].title)
        on.tabDrawer.mozClick(TabDrawerSelectors.PRIVATE_TABS_PAGE_BUTTON)
        on.tabDrawer.mozVerify(TabDrawerSelectors.PRIVATE_TABS_LIST)

        // New private tab lands on the private homepage; the browsing toggle returns to the normal homepage.
        // The new-tab FAB opens the toolbar in edit mode, so back out of it (keyboard first, then edit mode).
        on.tabDrawer.mozClick(TabDrawerSelectors.FAB)
        on.searchBar.mozPressBackUntilGone(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.home.navigateToPage().mozVerify(HomeSelectors.PRIVATE_BROWSING_INFO_CARD_TITLE)
        on.home.mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON).mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT(""))

        // Tab counter opens the normal tray; a new tab returns to the normal homepage.
        on.tabDrawer.navigateToPage().verifyExistingOpenTabs(webPages[0].title, webPages[1].title)
        on.tabDrawer.mozVerify(TabDrawerSelectors.NORMAL_TABS_LIST)
        on.tabDrawer.mozClick(TabDrawerSelectors.FAB)
        on.searchBar.mozPressBackUntilGone(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.home.navigateToPage().mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT(""))

        // Toggle back to private and confirm the private tray still holds the private tabs.
        on.home
            .mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)
            .mozVerify(HomeSelectors.PRIVATE_BROWSING_INFO_CARD_TITLE)
        on.tabDrawer.navigateToPage().verifyExistingOpenTabs(webPages[2].title, webPages[3].title)
        on.tabDrawer.mozVerify(TabDrawerSelectors.PRIVATE_TABS_LIST)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034516
    @Critical
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=2079797"])
    // Leak Canary: https://github.com/square/leakcanary/issues/2781
    @Test
    fun verifyMergeTabGroupsViaSelectionGridViewTest() {
        appContext.components.settings.gridTabView = true
        mergeTabGroupsViaSelection(isListViewEnabled = false)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4034516
    @Critical
    @Test
    fun verifyMergeTabGroupsViaSelectionListViewTest() {
        appContext.components.settings.gridTabView = false
        mergeTabGroupsViaSelection(isListViewEnabled = true)
    }

    private fun mergeTabGroupsViaSelection(isListViewEnabled: Boolean) {
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val firstGroup = "Group one"
        val secondGroup = "Group two"
        val thirdGroup = "Group three"
        val mergedGroup = "Merged"

        val webPages = mockWebServer.genericAssets
        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())

        // Gate on the tab titles rendering (they show the URL until the mock pages finish loading).
        on.tabDrawer.navigateToPage().waitForTabTitlesLoaded(webPages[0].title, webPages[1].title, webPages[2].title)
        on.tabDrawer.selectTabsAndCreateFirstTabGroup(
            tabTitle = webPages[0].title,
            tabGroupTitle = firstGroup,
            tabGroupColor = "Grey",
        )
        on.tabDrawer.selectTabsAndAddToNewTabGroup(
            tabTitle = webPages[1].title,
            tabGroupTitle = secondGroup,
            tabGroupColor = "Blue",
        )
        on.tabDrawer.selectTabsAndAddToNewTabGroup(
            tabTitle = webPages[2].title,
            tabGroupTitle = thirdGroup,
            tabGroupColor = "Red",
        )

        // Enter selection mode, select two of the three groups, and merge them into a new group.
        on.tabDrawer.mozClick(TabDrawerSelectors.THREE_DOT_BUTTON)
        on.tabDrawer.mozClick(TabDrawerSelectors.SELECT_TABS_BUTTON)
        on.tabDrawer.selectTabGroupCard(firstGroup, numberOfTabs = 1, tabGroupColor = "Grey", isListViewEnabled)
        on.tabDrawer.selectTabGroupCard(secondGroup, numberOfTabs = 1, tabGroupColor = "Blue", isListViewEnabled)
        on.tabDrawer.mergeSelectedTabGroupsIntoNewGroup(mergedGroup, "Green")

        // The merged group holds both tabs and shows in the tab groups tray; the merged-away groups are gone.
        on.tabDrawer.verifyTabGroupFromTabGroupPage(
            tabGroupTitle = mergedGroup,
            numberOfTabs = 2,
            tabGroupColor = "Green",
        )
        on.tabDrawer.mozWaitUntilAbsent(
            TabDrawerSelectors.TAB_GROUP_ITEM(firstGroup, numberOfTabs = 1, tabGroupColor = "Grey")
        )
        on.tabDrawer.mozWaitUntilAbsent(
            TabDrawerSelectors.TAB_GROUP_ITEM(secondGroup, numberOfTabs = 1, tabGroupColor = "Blue")
        )
    }

    private fun verifyGroup(tabGroupTitle: String, tabGroupColor: String, numberOfTabs: Int = 1) {
        on.home.navigateToPage()
        on.tabDrawer.navigateToPage().mozVerify(TAB_ITEM_WITH_TITLE(tabTitle = tabGroupTitle))
        on.tabDrawer.verifyTabGroupFromTabGroupPage(
            tabGroupTitle = tabGroupTitle,
            numberOfTabs = numberOfTabs,
            tabGroupColor = tabGroupColor,
        )
    }
}
