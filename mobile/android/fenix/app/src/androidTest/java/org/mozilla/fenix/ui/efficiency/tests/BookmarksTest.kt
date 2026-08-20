/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import java.io.File
import org.junit.After
import org.junit.Ignore
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.stubFilePickerSelection
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarksHtmlFile
import org.mozilla.fenix.helpers.MockBrowserDataHelper.generateBookmarkFolder
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.htmlControlsFormAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.BookmarksSelectors
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors

class BookmarksTest : BaseTest() {

    // TODO (I. RIOS 3/20/2026): add to BaseTest for State Machine
    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    private var importedBookmarksFile: File? = null

    @After
    fun tearDownTempFileDirectory() {
        importedBookmarksFile?.parentFile?.deleteRecursively()
        importedBookmarksFile = null
    }

    @Ignore("Covered by verifyNavigationReachability[0: BookmarksPage (TBD) — Navigation Reachability]")
    @Test
    fun verifyBookmarksSectionTest() {
        on.bookmarks.navigateToPage()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833690
    @SmokeTest
    @Test
    fun deleteBookmarkFoldersTest() {
        val website = mockWebServer.getGenericAsset(1)
        val bookmarkFolderName = "My Folder"

        createBookmarkItem(website.url.toString(), website.title, null)

        on.bookmarks.navigateToPage().mozVerify(BookmarksSelectors.BOOKMARK_ITEM(website.title))
        on.bookmarks.createFolder(bookmarkFolderName).mozVerify(BookmarksSelectors.BOOKMARK_ITEM(bookmarkFolderName))
        on.bookmarks.openItemMenu(website.title).mozClick(BookmarksSelectors.EDIT_BUTTON)
        on.bookmarks
            .setParentFolder(bookmarkFolderName)
            .saveEditBookmark()
            .createFolder("My Folder 2")
            .mozVerify(BookmarksSelectors.BOOKMARK_ITEM("My Folder 2"))
        on.bookmarks.openItemMenu("My Folder 2").mozClick(BookmarksSelectors.EDIT_BUTTON)
        on.bookmarks
            .setParentFolder(bookmarkFolderName)
            .saveEditBookmark()
            .openItemMenu(bookmarkFolderName)
            .mozClick(BookmarksSelectors.DELETE_BUTTON)
            .mozClick(BookmarksSelectors.CANCEL_FOLDER_DELETION_BUTTON)
            .mozVerify(BookmarksSelectors.BOOKMARK_ITEM(bookmarkFolderName))
        on.bookmarks
            .openItemMenu(bookmarkFolderName)
            .mozClick(BookmarksSelectors.DELETE_BUTTON)
            .mozClick(BookmarksSelectors.DELETE_BUTTON)
            .mozVerifyElementAbsent(BookmarksSelectors.BOOKMARK_ITEM(bookmarkFolderName))
            .mozVerifyElementAbsent(BookmarksSelectors.BOOKMARK_ITEM("My Folder 2"))
            .mozVerifyElementAbsent(BookmarksSelectors.BOOKMARK_ITEM(website.title))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833693
    @SmokeTest
    @Test
    fun shareBookmarkTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        on.bookmarks.navigateToPage().mozVerify(BookmarksSelectors.BOOKMARK_ITEM(defaultWebPage.title))
        on.bookmarks.openItemMenu(defaultWebPage.title).mozClick(BookmarksSelectors.SHARE_BUTTON)
        on.shareOverlay.mozVerifyElementsByGroup("shareTabLayout")
        on.shareOverlay.verifySharingWithSelectedApp(
            appName = Constants.GMAIL_APP_NAME,
            appPackageName = Constants.PackageName.GMAIL_APP,
            content = defaultWebPage.url.toString(),
            subject = defaultWebPage.title,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833702
    @SmokeTest
    @Test
    fun openMultipleSelectedBookmarksInANewTabTest() {
        val webPages =
            listOf(
                mockWebServer.getGenericAsset(1),
                mockWebServer.getGenericAsset(2),
            )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        on.bookmarks
            .navigateToPage()
            .longClickBookmarkedItem(webPages[0].title)
            .selectBookmarkedItem(webPages[1].title)
            .verifyMultiSelectionCounter(2)
            .clickMultiSelectThreeDotButton()
            .mozClick(BookmarksSelectors.OPEN_IN_NEW_TAB_BUTTON)
        on.tabDrawer
            .mozVerify(TabDrawerSelectors.TABS_TRAY)
            .mozVerifyElementIsSelected(TabDrawerSelectors.NORMAL_BROWSING_OPEN_TABS_BUTTON)
        on.tabDrawer.verifyNormalTabsList().verifyExistingOpenTabs(webPages[0].title, webPages[1].title)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833704
    @SmokeTest
    @Test
    fun deleteMultipleSelectedBookmarksTest() {
        val webPages =
            listOf(
                mockWebServer.getGenericAsset(1),
                mockWebServer.getGenericAsset(2),
            )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        on.bookmarks
            .navigateToPage()
            .longClickBookmarkedItem(webPages[0].title)
            .selectBookmarkedItem(webPages[1].title)
            .verifyMultiSelectionCounter(2)
            .clickMultiSelectThreeDotButton()
            .mozClick(BookmarksSelectors.DELETE_BUTTON)
            .mozClick(BookmarksSelectors.CANCEL_FOLDER_DELETION_BUTTON)
        on.bookmarks.verifyBookmarkTitle(webPages[0].title).verifyBookmarkTitle(webPages[1].title)
        on.bookmarks
            .longClickBookmarkedItem(webPages[0].title)
            .selectBookmarkedItem(webPages[1].title)
            .verifyMultiSelectionCounter(2)
            .clickMultiSelectThreeDotButton()
            .mozClick(BookmarksSelectors.DELETE_BUTTON)
            .mozClick(BookmarksSelectors.DELETE_BUTTON)
        on.bookmarks
            .mozVerifyElementAbsent(BookmarksSelectors.BOOKMARK_ITEM(webPages[0].title))
            .mozVerifyElementAbsent(BookmarksSelectors.BOOKMARK_ITEM(webPages[1].title))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833712
    @SmokeTest
    @Test
    fun verifySearchForBookmarkedItemsTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.htmlControlsFormAsset
        val bookmarkFolderName = "My Folder"

        val newFolder = generateBookmarkFolder(title = bookmarkFolderName, position = null)
        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null, newFolder)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

        on.bookmarkSearch.navigateToPage()
        on.bookmarkSearch
            .typeSearch(firstWebPage.title)
            .verifySearchSuggestionsAreDisplayed(firstWebPage.url.toString())
            .verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
            .typeSearch("Android")
            .verifySuggestionsAreNotDisplayed(firstWebPage.url.toString())
            .verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2833691
    @SmokeTest
    @Test
    fun editBookmarksNameAndUrlTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)
        val editedWebPage = mockWebServer.getGenericAsset(2)
        on.browserPage
            .navigateToPage(defaultWebPage.url.toString())
            .mozClick(HomeSelectors.MAIN_MENU_BUTTON)
            .mozClick(MainMenuSelectors.BOOKMARK_THIS_PAGE_BUTTON)
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
        on.bookmarks
            .mozClearAndEnterText("Bookmark title", BookmarksSelectors.EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD)
            .mozClearAndEnterText(editedWebPage.url.toString(), BookmarksSelectors.EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD)
            .mozClick(BookmarksSelectors.NAVIGATE_UP_BUTTON)
        on.bookmarks.navigateToPage().mozClick(BookmarksSelectors.BOOKMARK_TITLE_TEXT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4227084
    @SmokeTest
    @Test
    fun verifyEmptyBookmarksViewImportBookmarksTest() {
        on.bookmarks
            .navigateToPage()
            .mozVerifyElementsByGroup("emptyBookmarksMenuView")
            .mozClick(BookmarksSelectors.IMPORT_BOOKMARKS_BUTTON)
            .mozVerify(BookmarksSelectors.IMPORT_MENU_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4227091
    @SmokeTest
    @Test
    fun verifyBookmarksImportFromFileTest() {
        val importedBookmarksFolder = getStringResource(R.string.bookmark_import_destination_default_name)
        importedBookmarksFile = createBookmarksHtmlFile()
        stubFilePickerSelection(importedBookmarksFile!!)

        on.bookmarks.navigateToPage().mozVerifyElementsByGroup("emptyBookmarksMenuView")
        on.bookmarks.importBookmarksFromFile().mozVerify(BookmarksSelectors.BOOKMARK_ITEM(importedBookmarksFolder))
    }
}
