/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.espresso.Espresso.pressBack
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.generateBookmarkFolder
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.composeBookmarksMenu
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.multipleSelectionToolbar
import org.mozilla.fenix.ui.robots.navigationToolbar

class BookmarksTestCompose : TestSetup() {
    private val testBookmark = object {
        var title: String = "Bookmark title"
        var url: String = "https://www.example.com/"
    }
    private val bookmarkFolderName = "My Folder"

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule(
                isNewBookmarksEnabled = true,
                isNavigationToolbarEnabled = false,
                isNavigationBarCFREnabled = false,
                isMenuRedesignEnabled = false,
                isMenuRedesignCFREnabled = false,
                shouldUseBottomToolbar = true,
            ),
        ) { it.activity }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833690
    @SmokeTest
    @Test
    fun deleteBookmarkFoldersTest() {
        val website = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(website.url.toString(), website.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
            verifyBookmarkTitle("Test_Page_1")
            createFolder(bookmarkFolderName)
            verifyFolderTitle(bookmarkFolderName)
        }.openThreeDotMenu("Test_Page_1") {
        }.clickEdit {
            clickParentFolderSelector()
            selectFolder(bookmarkFolderName)
            navigateUp()
            saveEditBookmark()
            createFolder("My Folder 2")
            verifyFolderTitle("My Folder 2")
        }.openThreeDotMenu("My Folder 2") {
        }.clickEdit {
            clickParentFolderSelector()
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
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        browserScreen {
            createBookmark(defaultWebPage.url)
        }.openThreeDotMenu {
        }.editBookmarkPage(composeTestRule) {
            verifyEditBookmarksView()
            changeBookmarkTitle(testBookmark.title)
            changeBookmarkUrl(testBookmark.url)
            saveEditBookmark()
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
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
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
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
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
            longClickBookmarkedItem(webPages[0].title)
            selectBookmarkedItem(webPages[1].title)
        }

        multipleSelectionToolbar {
            verifyMultiSelectionCounter(2, composeTestRule)
            clickMultiSelectThreeDotButton(composeTestRule)
        }.clickOpenInNewTabButton(composeTestRule) {
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
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
            longClickBookmarkedItem(webPages[0].title)
            selectBookmarkedItem(webPages[1].title)
        }

        multipleSelectionToolbar {
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

        multipleSelectionToolbar {
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
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getHTMLControlsFormAsset(mockWebServer)

        val newFolder = generateBookmarkFolder(title = bookmarkFolderName, position = null)
        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null, newFolder)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.clickSearchButton() {
            // Search for a valid term
            typeSearch(firstWebPage.title)
            verifySearchSuggestionsAreDisplayed(composeTestRule, firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(composeTestRule, secondWebPage.url.toString())
            // Search for invalid term
            typeSearch("Android")
            verifySuggestionsAreNotDisplayed(composeTestRule, firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(composeTestRule, secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833710
    @Test
    fun verifySearchBookmarksViewTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.clickSearchButton {
            verifySearchView()
            verifySearchToolbar(true)
            verifySearchSelectorButton()
            verifySearchEngineIcon("Bookmarks")
            verifySearchBarPlaceholder("Search bookmarks")
            verifySearchBarPosition(true)
            tapOutsideToDismissSearchBar()
            verifySearchToolbar(false)
        }
        composeBookmarksMenu(composeTestRule) {
        }.goBackToBrowserScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
        }

        exitMenu()

        browserScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.clickSearchButton {
            verifySearchToolbar(true)
            verifySearchEngineIcon("Bookmarks")
            verifySearchBarPosition(false)
            pressBack()
            verifySearchToolbar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833685
    @Test
    fun verifyAddBookmarkButtonTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.bookmarkPage {
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
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.bookmarkPage {
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
        browserScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
            selectFolder(bookmarkFolderName)
            verifyBookmarkedURL(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833694
    @Test
    fun copyBookmarkURLTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickCopy {
            waitForBookmarksSnackBarToBeGone(snackbarText = "URL copied")
        }.goBackToBrowserScreen {
        }.openNavigationToolbar {
        }.visitLinkFromClipboard {
            verifyUrl(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833695
    @Test
    fun openBookmarkInNewTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickOpenInNewTab {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()
        }.closeTabDrawer {
        }.goBack {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.openBookmarkWithTitle(defaultWebPage.title) {
            verifyUrl(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833696
    @Test
    fun openBookmarkInPrivateTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickOpenInPrivateTab {
            verifyTabTrayIsOpen()
            verifyPrivateBrowsingButtonIsSelected()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833697
    @Test
    fun deleteBookmarkTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarksMenu(composeTestRule) {
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickDelete {
            clickSnackbarButton(composeTestRule, "UNDO")
            waitForBookmarksSnackBarToBeGone("Deleted ${defaultWebPage.title}")
            verifyBookmarkedURL(defaultWebPage.url.toString())
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickDelete {
            verifyBookmarkIsDeleted(defaultWebPage.title)
        }
    }
}
