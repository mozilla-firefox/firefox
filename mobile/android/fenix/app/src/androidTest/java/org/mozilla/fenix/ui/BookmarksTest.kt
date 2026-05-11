/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.test.espresso.Espresso.pressBack
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.generateBookmarkFolder
import org.mozilla.fenix.helpers.TestAssetHelper.genericAssets
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.htmlControlsFormAsset
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.composeBookmarksMenu
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.multipleSelectionToolbar
import org.mozilla.fenix.ui.robots.navigationToolbar
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

class BookmarksTest {
    private val testBookmark = object {
        var title: String = "Bookmark title"
        var url: String = "https://www.example.com/"
    }
    private val bookmarkFolderName = "My Folder"

    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule(
                isMenuRedesignCFREnabled = false,
                shouldUseBottomToolbar = true,
            ),
        ) { it.activity }

    @get:Rule(order = 2)
    val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833690
    @SmokeTest
    @Test
    fun deleteBookmarkFoldersTest() {
        val website = mockWebServer.getGenericAsset(1)

        createBookmarkItem(website.url.toString(), website.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkTitle("Test_Page_1")
            createFolder(bookmarkFolderName)
            verifyFolderTitle(bookmarkFolderName)
        }.openThreeDotMenu("Test_Page_1") {
        }.clickEdit {
            clickParentFolderSelector()
            expandSelectableFolder("Bookmarks")
            selectFolder(bookmarkFolderName)
            navigateUp()
            saveEditBookmark()
            createFolder("My Folder 2")
            verifyFolderTitle("My Folder 2")
        }.openThreeDotMenu("My Folder 2") {
        }.clickEdit {
            clickParentFolderSelector()
            expandSelectableFolder("Bookmarks")
            selectFolder(bookmarkFolderName)
            navigateUp()
            saveEditBookmark()
        }.openThreeDotMenu(bookmarkFolderName) {
        }.clickDelete {
            cancelFolderDeletion()
            verifyFolderTitle(bookmarkFolderName)
        }.openThreeDotMenu(bookmarkFolderName) {
        }.clickDelete {
            confirmDeletion()
            verifyBookmarkIsDeleted(bookmarkFolderName)
            verifyBookmarkIsDeleted("My Folder 2")
            verifyBookmarkIsDeleted("Test_Page_1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833691
    @SmokeTest
    @Test
    fun editBookmarksNameAndUrlTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.clickBookmarkThisPageButton {
            verifySnackBarText("Saved in “Bookmarks”")
            clickSnackbarButton(composeTestRule, "EDIT")
        }
        composeBookmarksMenu(composeTestRule) {
            verifyEditBookmarksView()
            changeBookmarkTitle(testBookmark.title)
            changeBookmarkUrl(testBookmark.url)
            saveEditBookmark()
        }
        browserScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkTitle(testBookmark.title)
            verifyBookmarkedURL("https://www.example.com/")
        }.openBookmarkWithTitle(testBookmark.title) {
            verifyUrl("example.com")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833693
    @SmokeTest
    @Test
    fun shareBookmarkTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkTitle(defaultWebPage.title)
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickShare {
            verifyShareTabLayout()
            verifySharingWithSelectedApp(
                appName = "Gmail",
                content = defaultWebPage.url.toString(),
                subject = defaultWebPage.title,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833702
    @SmokeTest
    @Test
    fun openMultipleSelectedBookmarksInANewTabTest() {
        val webPages = listOf(
            mockWebServer.getGenericAsset(1),
            mockWebServer.getGenericAsset(2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            longClickBookmarkedItem(webPages[0].title)
            selectBookmarkedItem(webPages[1].title)
        }

        multipleSelectionToolbar(composeTestRule) {
            verifyMultiSelectionCounter(2, composeTestRule)
            clickMultiSelectThreeDotButton(composeTestRule)
        }.clickOpenInNewTabButton {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()
            verifyNormalTabsList()
            verifyExistingOpenTabs(webPages[0].url.toString(), webPages[1].url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833704
    @SmokeTest
    @Test
    fun deleteMultipleSelectedBookmarksTest() {
        val webPages = listOf(
            mockWebServer.getGenericAsset(1),
            mockWebServer.getGenericAsset(2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            longClickBookmarkedItem(webPages[0].title)
            selectBookmarkedItem(webPages[1].title)
        }

        multipleSelectionToolbar(composeTestRule) {
            verifyMultiSelectionCounter(2, composeTestRule)
            clickMultiSelectThreeDotButton(composeTestRule)
            clickMultiSelectDeleteButton(composeTestRule)
        }

        composeBookmarksMenu(composeTestRule) {
            cancelFolderDeletion()
            verifyBookmarkTitle(webPages[0].title)
            verifyBookmarkTitle(webPages[1].title)
            longClickBookmarkedItem(webPages[0].title)
            selectBookmarkedItem(webPages[1].title)
        }

        multipleSelectionToolbar(composeTestRule) {
            verifyMultiSelectionCounter(2, composeTestRule)
            clickMultiSelectThreeDotButton(composeTestRule)
            clickMultiSelectDeleteButton(composeTestRule)
        }

        composeBookmarksMenu(composeTestRule) {
            confirmDeletion()
            verifyBookmarkIsDeleted(webPages[0].title)
            verifyBookmarkIsDeleted(webPages[1].title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833712
    @SmokeTest
    @Test
    fun verifySearchForBookmarkedItemsTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.htmlControlsFormAsset

        val newFolder = generateBookmarkFolder(title = bookmarkFolderName, position = null)
        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null, newFolder)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.clickSearchButton {
            // Search for a valid term
            typeSearch(firstWebPage.title)
            verifySearchSuggestionsAreDisplayed(firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
            // Search for invalid term
            typeSearch("Android")
            verifySuggestionsAreNotDisplayed(firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833710
    @Test
    fun verifySearchBookmarksViewTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.clickSearchButton {
            verifySearchToolbar(true)
            verifySearchSelectorButton()
            verifySearchEngineIcon("Bookmarks")
            verifySearchBarPlaceholder("Search bookmarks")
            verifySearchBarPosition()
            tapOutsideToDismissSearchBar(defaultWebPage.url.toString())
            verifySearchToolbar(false)
        }
        composeBookmarksMenu(composeTestRule) {
        }.goBackToBrowserScreen {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
        }

        exitMenu()

        browserScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.clickSearchButton {
            verifySearchToolbar(true)
            verifySearchEngineIcon("Bookmarks")
            verifySearchBarPosition()
            pressBack()
            verifySearchToolbar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833685
    @Test
    fun verifyAddBookmarkButtonTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.clickBookmarkThisPageButton {
            verifySnackBarText("Saved in “Bookmarks”")
            clickSnackbarButton(composeTestRule, "EDIT")
        }
        composeBookmarksMenu(composeTestRule) {
            verifyEditBookmarksView()
        }.goBackToBrowserScreen {
        }.openThreeDotMenu {
            verifyEditBookmarkButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833686
    @Test
    fun createBookmarkFolderTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.clickBookmarkThisPageButton {
            verifySnackBarText("Saved in “Bookmarks”")
            clickSnackbarButton(composeTestRule, "EDIT")
        }
        composeBookmarksMenu(composeTestRule) {
            clickParentFolderSelector()
            clickSelectFolderNewFolderButton()
            verifyAddFolderView()
            addNewFolderName(bookmarkFolderName)
            saveNewFolder()
            navigateUp()
        }
        browserScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyFolderTitle(bookmarkFolderName)
            verifyBookmarkFolderDescription(numberOfBookmarksInFolder = "1")
            selectFolder(bookmarkFolderName)
            verifyBookmarkedURL(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833694
    @Test
    fun moveBookmarkToDifferentFolderTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkTitle(defaultWebPage.title)
            createFolder(bookmarkFolderName)
            verifyFolderTitle(bookmarkFolderName)
            verifyBookmarkTitle(defaultWebPage.title)
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickMove {
            expandSelectableFolder("Bookmarks")
            selectFolder(bookmarkFolderName)
            navigateUp()
            verifyFolderTitle(bookmarkFolderName)
            verifyBookmarkFolderDescription(numberOfBookmarksInFolder = "1")
            selectFolder(bookmarkFolderName)
            verifyBookmarkTitle(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833695
    @Test
    fun openBookmarkInNewTabTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickOpenInNewTab {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()
        }.closeTabDrawer {
        }.goBack {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.openBookmarkWithTitle(defaultWebPage.title) {
            verifyUrl(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833696
    @Test
    fun openBookmarkInPrivateTabTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickOpenInPrivateTab {
            verifyTabTrayIsOpen()
            verifyPrivateBrowsingButtonIsSelected()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833697
    @Test
    fun deleteBookmarkTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkTitle(defaultWebPage.title)
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickDelete {
            verifyBookmarkIsDeleted(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833706
    @Test
    fun verifyOpenAllInNewTabsOptionTest() {
        val webPages = mockWebServer.genericAssets

        val rootFolderGuid = generateBookmarkFolder(title = "root", position = null)
        val subFolderGuid = generateBookmarkFolder(rootFolderGuid, "sub", null)

        generateBookmarkFolder(rootFolderGuid, "empty", null)
        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null, rootFolderGuid)
        createBookmarkItem(webPages[2].url.toString(), webPages[2].title, null, subFolderGuid)
        createBookmarkItem(webPages[3].url.toString(), webPages[3].title, null, rootFolderGuid)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.openThreeDotMenu("root") {
        }.clickOpenAllInTabs(composeTestRule) {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()

            verifyExistingOpenTabs("Test_Page_2", "Test_Page_4")

            // Bookmark that is not under the root folder should not be opened
            verifyNoExistingOpenTabs("Test_Page_1", "Test_Page_3")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833707
    @Test
    fun verifyOpenAllInPrivateTabsTest() {
        val webPages = listOf(
            mockWebServer.getGenericAsset(1),
            mockWebServer.getGenericAsset(2),
        )

        val rootFolderGuid = generateBookmarkFolder(title = "root", position = null)
        val subFolderGuid = generateBookmarkFolder(rootFolderGuid, "sub", null)

        generateBookmarkFolder(rootFolderGuid, "empty", null)
        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null, rootFolderGuid)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null, subFolderGuid)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.openThreeDotMenu("root") {
        }.clickOpenAllInPrivateTabs(composeTestRule) {
            verifyTabTrayIsOpen()
            verifyPrivateBrowsingButtonIsSelected()

            verifyExistingOpenTabs("Test_Page_1")
            verifyNoExistingOpenTabs("Test_Page_2")
        }
    }
}
