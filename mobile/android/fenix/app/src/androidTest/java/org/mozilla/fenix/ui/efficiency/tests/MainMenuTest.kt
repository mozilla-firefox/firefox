/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import androidx.core.net.toUri
import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.Constants.PackageName.PRINT_SPOOLER
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FeatureSettingsHelper.Companion.settings
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.articleSummaryAsset
import org.mozilla.fenix.helpers.TestAssetHelper.firstForeignWebPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.pdfFormAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.navigation.NavigationOptions
import org.mozilla.fenix.ui.efficiency.selectors.AddToHomeScreenSelectors
import org.mozilla.fenix.ui.efficiency.selectors.BookmarksSelectors
import org.mozilla.fenix.ui.efficiency.selectors.BookmarksSelectors.DELETE_BOOKMARK_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.CollectionsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.DownloadsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HistorySelectors
import org.mozilla.fenix.ui.efficiency.selectors.HistorySelectors.NAVIGATE_BACK_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.BACK_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.BOOKMARK_THIS_PAGE_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.DESKTOP_SITE_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.DESKTOP_SITE_OFF
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.DESKTOP_SITE_ON
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.EDIT_BOOKMARK_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors.FORWARD_BUTTON
import org.mozilla.fenix.ui.efficiency.selectors.SettingsAddonsManagerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsHomepageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSavedPasswordsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ShareOverlaySelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabHistorySelectors

class MainMenuTest : BaseTest() {

    @Before
    fun disablePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun restorePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // Converted from legacy MainMenuTest.verifyTheBookmarksMainMenuItemTest
    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080137
    @SmokeTest
    @Test
    fun verifyTheBookmarksMainMenuItemTest() {
        val testPage = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(testPage.url.toString())
        on.mainMenu.navigateToPage().mozClick(MainMenuSelectors.BOOKMARKS_BUTTON)
        // Reaching the Bookmarks view is the verification that the menu item opened it.
        on.bookmarks.navigateToPage()
        // Parity gap: legacy also returned to the browser and re-verified page content; the nav graph has
        // no stateful BookmarksPage -> BrowserPage return edge yet.
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080168
    @SmokeTest
    @Test
    fun verifyMainMenuItemsTest() {
        on.mainMenu.navigateToPage().mozVerifyElementsByGroup(MainMenuSelectors.Group.HOME_PAGE_MAIN_MENU_ITEMS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080124
    @SmokeTest
    @Test
    fun verifyTheBrowserViewMainMenuItemsTest() {
        val website = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(website.url.toString())
        on.mainMenu.navigateToPage().mozVerifyElementsByGroup(MainMenuSelectors.Group.BROWSER_VIEW_MAIN_MENU_ITEMS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080110
    @SmokeTest
    @Test
    fun verifyTheMoreMainMenuSubListTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset

        on.browserPage.navigateToPage(firstTestPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerifyElementsByGroup(MainMenuSelectors.Group.MORE_MAIN_MENU_SUB_LIST)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080172
    @SmokeTest
    @Test
    fun verifyTheExtensionsMenuOptionTest() {
        on.settingsAddonsManager.navigateToPage().mozVerifyElementsByGroup(SettingsAddonsManagerSelectors.Group.ADD_ONS)
        on.home.navigateToPage()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080181
    @SmokeTest
    @Test
    fun verifyTheHomePageSettingsMenuItemTest() {
        on.settings
            .navigateToPage()
            .mozVerify(SettingsSelectors.NAVIGATION_TOOLBAR)
            .mozVerify(SettingsSelectors.SETTINGS_TITLE)
        on.home.navigateToPage().mozVerify(HomeSelectors.HOME_WORDMARK_LOGO).mozVerify(HomeSelectors.HOME_WORDMARK_TEXT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080129
    @SmokeTest
    @Test
    fun verifyBookmarkPageMenuOptionTest() {
        val testPage = mockWebServer.getGenericAsset(1)
        on.browserPage
            .navigateToPage(testPage.url.toString())
            .mozClick(HomeSelectors.MAIN_MENU_BUTTON_UIAUTOMATOR)
            .mozClick(MainMenuSelectors.BOOKMARK_THIS_PAGE_BUTTON)
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
        on.bookmarks
            .mozVerifyElementsByGroup(BookmarksSelectors.Group.EDIT_BOOKMARKS_VIEW)
            .mozClick(BookmarksSelectors.DELETE_BOOKMARK_BUTTON)
        on.browserPage.mozClick(HomeSelectors.MAIN_MENU_BUTTON_UIAUTOMATOR)
        on.mainMenu.mozVerifyElementsByGroup(MainMenuSelectors.Group.BOOKMARK_ACTIONS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080138
    @SmokeTest
    @Test
    fun verifyTheDownloadsMenuItemTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.downloads.navigateToPage().mozVerifyElementsByGroup(DownloadsSelectors.Group.EMPTY_DOWNLOADS)
        on.browserPage.navigateToPage().verifyPageContent(testPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080129
    @SmokeTest
    @Test
    fun verifyTheBookmarkPageMenuOptionTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.mainMenu.navigateToPage().mozClick(BOOKMARK_THIS_PAGE_BUTTON)
        on.browserPage.navigateToPage()
        on.mainMenu.navigateToPage().mozClick(EDIT_BOOKMARK_BUTTON)
        on.bookmarks
            .mozVerifyElementsByGroup(BookmarksSelectors.Group.EDIT_BOOKMARKS_VIEW)
            .mozClick(DELETE_BOOKMARK_BUTTON)
        on.browserPage.navigateToPage()
        on.mainMenu.navigateToPage().mozVerify(BOOKMARK_THIS_PAGE_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080136
    @SmokeTest
    @Test
    fun verifyTheHistoryMenuItemTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.history
            .navigateToPage()
            .mozVerifyElementsByGroup(HistorySelectors.Group.HISTORY_MENU_VIEW_WITH_HISTORY_ITEMS)
            .mozClick(NAVIGATE_BACK_BUTTON)
        on.browserPage.verifyPageContent(testPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080133
    @SmokeTest
    @Test
    fun verifySwitchToDesktopSiteIsDisabledOnPDFsTest() {
        val pdfPage = mockWebServer.pdfFormAsset

        on.browserPage.navigateToPage(pdfPage.url.toString())
        on.mainMenu.navigateToPage().mozVerifyElementIsNotEnabled(MainMenuSelectors.DESKTOP_SITE_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080139
    @SmokeTest
    @Test
    fun verifyThePasswordsMenuItemTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.settingsSavedPasswords
            .navigateToPage()
            .mozVerifyElementsByGroup(SettingsSavedPasswordsSelectors.Group.EMPTY_SAVED_PASSWORDS_LIST)
        on.browserPage.navigateToPage().verifyPageContent(testPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080126
    @SmokeTest
    @Test
    fun verifyTheMainMenuForwardButtonTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val nextWebPage = mockWebServer.getGenericAsset(2)

        on.browserPage
            .navigateToPage(firstWebPage.url.toString())
            .verifyUrl(firstWebPage.url.toString())
            .navigateToPage(nextWebPage.url.toString(), forceNavigation = true)
            .verifyUrl(nextWebPage.url.toString())
        on.mainMenu.navigateToPage().mozClick(BACK_BUTTON)
        on.browserPage.navigateToPage().verifyUrl(firstWebPage.url.toString())
        on.mainMenu.navigateToPage().mozClick(FORWARD_BUTTON)
        on.browserPage.navigateToPage().verifyUrl(nextWebPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080125
    @SmokeTest
    @Test
    fun verifyTheMainMenuBackButtonTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val nextWebPage = mockWebServer.getGenericAsset(2)

        on.browserPage
            .navigateToPage(firstWebPage.url.toString())
            .verifyUrl(firstWebPage.url.toString())
            .navigateToPage(nextWebPage.url.toString(), forceNavigation = true)
            .verifyUrl(nextWebPage.url.toString())
        on.mainMenu.navigateToPage().mozClick(BACK_BUTTON)
        on.browserPage.navigateToPage().verifyUrl(firstWebPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080131
    @SmokeTest
    @Test
    fun verifyDesktopSiteModeOnOffIsEnabledTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.mainMenu.navigateToPage().mozVerify(DESKTOP_SITE_BUTTON).mozVerify(DESKTOP_SITE_OFF)
        on.mainMenu.mozClick(DESKTOP_SITE_BUTTON)
        on.browserPage.navigateToPage()
        on.mainMenu.navigateToPage().mozVerify(DESKTOP_SITE_ON)
        on.mainMenu.mozClick(DESKTOP_SITE_BUTTON)
        on.browserPage.navigateToPage()
        on.mainMenu.navigateToPage().mozVerify(DESKTOP_SITE_OFF)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080115
    @SmokeTest
    @Test
    fun verifyTheSaveToCollectionSubMenuOptionTest() {
        val collectionTitle = "First Collection"
        val firstTestPage = mockWebServer.getGenericAsset(1)
        val secondTestPage = mockWebServer.getGenericAsset(2)

        composeRule.activityRule.applySettingsExceptions {
            // Disabling these features to have better visibility of the Collections view
            it.isRecentlyVisitedFeatureEnabled = false
            it.isRecentTabsFeatureEnabled = false
        }

        MockBrowserDataHelper.createCollection(
            Pair(firstTestPage.url.toString(), firstTestPage.title),
            title = collectionTitle,
        )

        on.browserPage
            .navigateToPage(secondTestPage.url.toString())
            .mozClick(BrowserPageSelectors.MAIN_MENU_BUTTON)
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.SAVE_TO_COLLECTIONS_BUTTON)
            .mozClick(CollectionsSelectors.EXISTING_COLLECTION_WITH_TITLE(collectionTitle))
        on.home
            .navigateToPage()
            .mozClick(CollectionsSelectors.COLLECTION_WITH_TITLE(collectionTitle))
            .mozVerify(CollectionsSelectors.COLLECTION_TAB_WITH_TITLE(firstTestPage.title))
            .mozVerify(CollectionsSelectors.COLLECTION_TAB_WITH_TITLE(secondTestPage.title))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080111
    @SmokeTest
    @Test
    fun verifyTheTranslatePageSubMenuOptionTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        on.browserPage
            .navigateToPage(testPage.url.toString())
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
        on.browserPage.verifyTranslationSheetIsDisplayed().translatePageFromSheet()
        on.browserPage
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATED_BUTTON)
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATED_SHEET_SHOW_ORIGINAL_BUTTON)
            .mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATED_SHEET_SHOW_ORIGINAL_BUTTON)
        on.browserPage
            .verifyPageContent(testPage.content)
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerify(MainMenuSelectors.TRANSLATE_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080113
    @SmokeTest
    @Test
    fun verifyTheAddToShortcutsSubMenuOptionTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.ADD_TO_SHORTCUTS_BUTTON)
        on.browserPage.navigateToPage().mozVerifyElementsByGroup(BrowserPageSelectors.Group.ADDED_TO_SHORTCUTS_SNACKBAR)
        on.home
            .navigateToPage()
            .mozVerify(HomeSelectors.TOP_SITE_ITEM(testPage.title))
            .mozClick(HomeSelectors.TOP_SITE_ITEM(testPage.title))
        on.browserPage.mozVerify(BrowserPageSelectors.ENGINE_VIEW).navigateToPage()
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.REMOVE_FROM_SHORTCUTS_BUTTON)
        on.browserPage.navigateToPage()
        on.home.navigateToPage().mozWaitUntilAbsent(HomeSelectors.TOP_SITE_ITEM(testPage.title))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080114
    @SmokeTest
    @Test
    fun verifyTheAddToHomeScreenSubMenuOptionTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.addToHomescreen.navigateToPage().mozClickIfPresent(AddToHomeScreenSelectors.CANCEL_DIALOG_BUTTON)
        on.browserPage.navigateToPage()
        on.addToHomescreen
            .navigateToPage()
            .mozClickIfPresent(AddToHomeScreenSelectors.ADD_DIALOG_BUTTON)
            .mozClick(AddToHomeScreenSelectors.SYSTEM_PROMPT_ADD_TO_HOME_SCREEN_BUTTON)
            .mozClickIfPresent(AddToHomeScreenSelectors.HOME_SCREEN_SHORTCUT(testPage.title))
        on.browserPage.navigateToPage().verifyPageContent(testPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080118
    @SmokeTest
    @Test
    fun verifyTheSaveAsPDFSubMenuOptionTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.SAVE_AS_PDF_BUTTON)
        on.downloads
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.DOWNLOAD_DIALOG)
            .mozClick(DownloadsSelectors.DOWNLOAD_DIALOG_CONFIRM_BUTTON)
            .mozVerify(DownloadsSelectors.DOWNLOAD_COMPLETE_SNACKBAR, timeout = 15_000)
            .mozClick(DownloadsSelectors.DOWNLOAD_SNACK_BAR_OPEN_BUTTON)
            .mozVerifyFileOpensInExternalApp(Constants.PackageName.GOOGLE_DOCS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080128
    @SmokeTest
    @Test
    fun verifyTheShareButtonTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.mainMenu.navigateToPage().mozClick(MainMenuSelectors.SHARE_BUTTON)
        on.shareOverlay.mozVerifyElementsByGroup(ShareOverlaySelectors.Group.SHARE_TAB_LAYOUT)
        on.shareOverlay.verifySharingWithSelectedApp(
            appName = Constants.GMAIL_APP_NAME,
            appPackageName = Constants.PackageName.GMAIL_APP,
            content = testPage.url.toString(),
            subject = testPage.title,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080119
    @SmokeTest
    @Test
    fun verifyThePrintSubMenuOptionTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.PRINT_BUTTON)
            .mozVerifyNativeAppOpens(PRINT_SPOOLER)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4245654
    @SmokeTest
    @Test
    fun verifyTheMainMenuBackButtonLongPressTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val nextWebPage = mockWebServer.getGenericAsset(2)

        on.browserPage
            .navigateToPage(firstWebPage.url.toString())
            .navigateToPage(nextWebPage.url.toString(), forceNavigation = true)

        on.mainMenu.navigateToPage().mozLongClick(MainMenuSelectors.BACK_BUTTON)

        on.tabHistory
            .mozVerify(TabHistorySelectors.TAB_HISTORY_LIST)
            .mozVerify(TabHistorySelectors.TAB_HISTORY_ITEM(nextWebPage.url.toString()))
            .mozVerify(TabHistorySelectors.TAB_HISTORY_ITEM(firstWebPage.url.toString()))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4245662
    @SmokeTest
    @Test
    fun verifyTheMainMenuForwardButtonLongPressTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val nextWebPage = mockWebServer.getGenericAsset(2)

        on.browserPage
            .navigateToPage(firstWebPage.url.toString())
            .navigateToPage(nextWebPage.url.toString(), forceNavigation = true)

        on.mainMenu.navigateToPage().mozClick(MainMenuSelectors.BACK_BUTTON)

        on.browserPage.navigateToPage()

        on.mainMenu.navigateToPage().mozLongClick(MainMenuSelectors.FORWARD_BUTTON)
        on.tabHistory
            .mozVerify(TabHistorySelectors.TAB_HISTORY_LIST)
            .mozVerify(TabHistorySelectors.TAB_HISTORY_ITEM(nextWebPage.url.toString()))
            .mozVerify(TabHistorySelectors.TAB_HISTORY_ITEM(firstWebPage.url.toString()))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080151
    // Converted from legacy MainMenuTest.verifyRecommendedExtensionsListWhileNoExtensionIsInstalledTest
    @SmokeTest
    @Test
    fun verifyRecommendedExtensionsListWhileNoExtensionIsInstalledTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(genericURL.url.toString()).verifyPageContent(genericURL.content)
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
            .mozVerifyElementsByGroup(MainMenuSelectors.Group.EXPANDED_EXTENSIONS_MENU_ITEMS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080134
    // Converted from legacy MainMenuTest.verifyTheExtensionsMainMenuListTest
    @SmokeTest
    @Test
    fun verifyTheExtensionsMainMenuListTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        on.browserPage.navigateToPage(testPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
            .mozVerifyElementsByGroup(MainMenuSelectors.Group.EXPANDED_EXTENSIONS_MENU_ITEMS)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
            .mozVerifyElementAbsent(MainMenuSelectors.DISCOVER_MORE_EXTENSIONS_BUTTON)
        // Parity gap: legacy verifyTheRecommendedAddons asserted three named addons, each with its own
        // "Add <addon>" install button, retrying against a hardcoded candidate list. The port asserts a
        // recommended-addon row is present instead — which addons AMO recommends is server-driven, so
        // matching on names couples the test to remote data. The install-button check did not carry over.
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080160
    @SmokeTest
    @Test
    fun verifyTheExtensionsMenuListAfterRemovingAnExtensionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        // Install a recommended extension from the expanded Extensions submenu.
        on.browserPage.navigateToPage(genericURL.url.toString()).verifyPageContent(genericURL.content)
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
        val extension = on.mainMenu.installKnownRecommendedExtension()

        // Remove it through Manage extensions -> the add-ons manager.
        on.browserPage.navigateToPage(genericURL.url.toString(), forceNavigation = true)
        on.mainMenu.navigateToPage()
        on.settingsAddonsManager.navigateToPage().removeInstalledExtension(extension.name)

        // With no extensions installed, the "Try a recommended extension" entry point is shown again.
        on.browserPage.navigateToPage(genericURL.url.toString(), forceNavigation = true)
        on.mainMenu.navigateToPage().mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080163
    @SmokeTest
    @Test
    fun verifyTheManageExtensionsItemTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        // Install a recommended extension from the expanded Extensions submenu.
        on.browserPage.navigateToPage(genericURL.url.toString()).verifyPageContent(genericURL.content)
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
        val extension = on.mainMenu.installKnownRecommendedExtension()

        // Manage extensions opens the add-ons manager, where the extension is listed under Enabled.
        // Parity: legacy verifyAddonIsInstalled asserted the addon's install (+) button was invisible;
        // the extension appearing under the Enabled section is the equivalent "installed & enabled" signal.
        on.browserPage.navigateToPage(genericURL.url.toString(), forceNavigation = true)
        on.mainMenu.navigateToPage()
        on.settingsAddonsManager
            .navigateToPage()
            .mozVerify(SettingsAddonsManagerSelectors.ADD_ONS_LIST)
            .mozVerify(SettingsAddonsManagerSelectors.ENABLED_SECTION_TITLE)
            .mozVerify(SettingsAddonsManagerSelectors.INSTALLED_ADDON_ITEM(extension.name))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080156
    @SmokeTest
    @Test
    fun verifyTheExtensionInstallationTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        // Install a recommended extension from the expanded Extensions submenu.
        on.browserPage.navigateToPage(genericURL.url.toString()).verifyPageContent(genericURL.content)
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
        val extension = on.mainMenu.installKnownRecommendedExtension()

        // Re-open the Extensions submenu: the collapsed Extensions row now advertises the installed
        // extension, the extension is listed in the submenu, and "Discover more" has been replaced by
        // "Manage extensions".
        on.browserPage.navigateToPage(genericURL.url.toString(), forceNavigation = true)
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
            .mozVerify(MainMenuSelectors.EXTENSIONS_BUTTON_WITH_INSTALLED_EXTENSION(extension.menuLabel))
            .mozVerifyElementAbsent(MainMenuSelectors.DISCOVER_MORE_EXTENSIONS_BUTTON)
            .mozVerify(MainMenuSelectors.MANAGE_EXTENSIONS_BUTTON)
            .mozVerify(MainMenuSelectors.INSTALLED_EXTENSION_ITEM(extension.menuLabel))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080162
    @SmokeTest
    @Test
    fun verifyTheExtensionMenuListWhileExtensionsAreDisabledTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        // Install a recommended extension from the expanded Extensions submenu.
        on.browserPage.navigateToPage(genericURL.url.toString()).verifyPageContent(genericURL.content)
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.TRY_RECOMMENDED_EXTENSION_BUTTON)
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON)
        val extension = on.mainMenu.installKnownRecommendedExtension()

        // Open the installed extension's detail from Manage extensions and disable it.
        on.browserPage.navigateToPage(genericURL.url.toString(), forceNavigation = true)
        on.mainMenu
            .navigateToPage()
            .mozVerify(MainMenuSelectors.EXTENSIONS_BUTTON_WITH_INSTALLED_EXTENSION(extension.menuLabel))
        on.settingsAddonsManager.navigateToPage().disableInstalledExtension(extension.name)

        // With the only extension disabled, the menu advertises the "no extensions enabled" entry point.
        on.browserPage.navigateToPage(genericURL.url.toString(), forceNavigation = true)
        on.mainMenu.navigateToPage().mozVerify(MainMenuSelectors.NO_EXTENSIONS_ENABLED_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080117
    @SmokeTest
    @Test
    fun verifyTheOpenInAppSubMenuOptionIsEnabledTest() {
        val youtubeURL = "vnd.youtube://".toUri()

        on.browserPage.navigateToPage(youtubeURL.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerifyElementIsEnabled(MainMenuSelectors.OPEN_IN_APP_NAME_BUTTON("YouTube"))
            .mozClick(MainMenuSelectors.OPEN_IN_APP_NAME_BUTTON("YouTube"))
            .mozVerifyFileOpensInExternalApp(Constants.PackageName.YOUTUBE_APP)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036009
    @SmokeTest
    @Test
    fun verifyTheMoreMainMenuSummarizePageButtonTest() {
        composeRule.activityRule.applySettingsExceptions {
            it.hasSeenShakeToSummarizeToolbarCfr = true
        }

        val articlePage = mockWebServer.articleSummaryAsset

        on.browserPage.navigateToPage(articlePage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerify(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4105720
    @Critical
    @Test
    fun verifyTheCustomizeHomepageMenuItemTest() {
        // The test harness forces the Bookmarks and Recently visited homepage sections on (LaunchConfig defaults),
        // but bug 2066206 made both default off in the product. Override them here so this test asserts the real
        // shipped default state.
        composeRule.activityRule.applySettingsExceptions {
            it.isBookmarksHomeFeatureEnabled = false
            it.isRecentlyVisitedFeatureEnabled = false
        }

        // The privacy report row's label follows the longfox flag exactly as HomeSettingsFragment sets it, so derive
        // the
        // expected text from the same setting to keep this locator valid on every channel (developer/debug shows
        // "Help catch trackers"; nightly/beta/release show "Privacy report").
        val privacyReportLabel =
            if (settings.longfoxEnabled) {
                getStringResource(R.string.help_catch_trackers)
            } else {
                getStringResource(R.string.customize_toggle_privacy_report)
            }

        on.settingsHomepage
            .navigateToPage(
                navigationOptions =
                    NavigationOptions(via = listOf("MainMenuPage"), excludedPages = setOf("SettingsPage"))
            )
            .mozVerify(SettingsHomepageSelectors.WALLPAPERS_BUTTON)
            .mozVerifyOptionSwitchIsChecked(SettingsHomepageSelectors.SHORTCUTS_SWITCH)
            .mozVerifyOptionCheckBoxIsChecked(SettingsHomepageSelectors.SPONSORED_SHORTCUTS_CHECKBOX)
            .mozVerifyOptionSwitchIsChecked(SettingsHomepageSelectors.PRIVACY_REPORT_SWITCH(privacyReportLabel))
            .mozVerifyOptionSwitchIsChecked(SettingsHomepageSelectors.CONTINUE_SWITCH)
            .mozVerifyOptionSwitchIsNotChecked(SettingsHomepageSelectors.BOOKMARKS_SWITCH)
            .mozVerifyOptionSwitchIsNotChecked(SettingsHomepageSelectors.RECENTLY_VISITED_SWITCH)
            .mozVerifyOptionSwitchIsChecked(SettingsHomepageSelectors.STORIES_SWITCH)
            .mozVerifyOptionCheckBoxIsChecked(SettingsHomepageSelectors.SPONSORED_STORIES_CHECKBOX)
            .mozVerify(SettingsHomepageSelectors.OPENING_SCREEN_OPTION("Homepage"))
            .mozVerify(SettingsHomepageSelectors.OPENING_SCREEN_OPTION("Last tab"))
            .mozVerifyElementIsChecked(
                SettingsHomepageSelectors.OPENING_SCREEN_OPTION("Homepage after four hours of inactivity")
            )

        on.home.navigateToPage().mozVerify(HomeSelectors.HOMEPAGE_VIEW)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080157
    @Critical
    @Test
    fun verifyTheExtensionSettingPageAccessedFromTheExtensionsMenuTest() {
        val genericURL = mockWebServer.getGenericAsset(1)
        val addonName = "uBlock Origin"

        // Install by name from the add-ons manager, not the random main-menu "recommended" list: uBlock Origin has a
        // browser-action popup, so clicking its menu entry deterministically opens the settings engine view.
        on.settingsAddonsManager.navigateToPage().installAddon(addonName)
        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(genericURL.url.toString())
            .openMainMenu()
            .mozVerify(MainMenuSelectors.EXTENSIONS_BUTTON_WITH_INSTALLED_EXTENSION(addonName))
            .mozClick(MainMenuSelectors.EXTENSIONS_BUTTON_WITH_INSTALLED_EXTENSION(addonName))
            .mozVerify(MainMenuSelectors.INSTALLED_EXTENSION_ITEM(addonName))
            .mozClick(MainMenuSelectors.INSTALLED_EXTENSION_ITEM(addonName))
        on.browserPage.mozVerify(BrowserPageSelectors.EXTENSIONS_SETTINGS_ENGINE_VIEW, timeout = waitingTime)
    }
}
