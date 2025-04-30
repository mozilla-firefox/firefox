/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("DEPRECATION")

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.rule.ActivityTestRule
import mozilla.components.concept.engine.utils.EngineReleaseChannel
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertNativeAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertYoutubeAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.clickSystemHomeScreenShortcutAddButton
import org.mozilla.fenix.helpers.AppAndSystemHelper.registerAndCleanupIdlingResources
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_DOCS
import org.mozilla.fenix.helpers.Constants.PackageName.PRINT_SPOOLER
import org.mozilla.fenix.helpers.DataGenerationHelper.createCustomTabIntent
import org.mozilla.fenix.helpers.DataGenerationHelper.getRecommendedExtensionTitle
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.RecyclerViewIdlingResource
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeVeryShort
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.helpers.TestHelper.closeApp
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.customTabScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.mainMenuScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class MainMenuTestCompose : TestSetup() {
    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule(
                skipOnboarding = true,
                isNavigationToolbarEnabled = true,
                isNavigationBarCFREnabled = false,
                isMenuRedesignEnabled = true,
                isMenuRedesignCFREnabled = false,
                isPageLoadTranslationsPromptEnabled = false,
            ),
        ) { it.activity }

    @get: Rule
    val intentReceiverActivityTestRule = ActivityTestRule(
        IntentReceiverActivity::class.java,
        true,
        false,
    )

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860735
    @SmokeTest
    @Test
    fun homepageRedesignedMenuItemsTest() {
        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifyHomeMainMenuItems()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860835
    @SmokeTest
    @Test
    fun webpageRedesignedMenuItemsTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifyPageMainMenuItems()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860842
    @SmokeTest
    @Test
    fun verifyTheNewTabButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickNewTabButton {
            verifySearchToolbar(isDisplayed = true)
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860843
    @SmokeTest
    @Test
    fun verifyTheNewPrivateTabButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickNewPrivateTabButton {
            verifySearchToolbar(isDisplayed = true)
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860844
    @SmokeTest
    @Test
    fun verifySwitchToDesktopSiteIsDisabledOnPDFsTest() {
        val pdfPage = TestAssetHelper.getPdfFormAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(pdfPage.url) {
            verifyPageContent(pdfPage.content)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            verifySwitchToDesktopSiteButtonIsEnabled(isEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860846
    @SmokeTest
    @Test
    fun findInPageTest() {
        val testPage = getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
            mDevice.waitForIdle()
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickFindInPageButton {
            verifyFindInPageNextButton()
            verifyFindInPagePrevButton()
            verifyFindInPageCloseButton()
            enterFindInPageQuery("a")
            verifyFindInPageResult("1/3")
            clickFindInPageNextButton()
            verifyFindInPageResult("2/3")
            clickFindInPageNextButton()
            verifyFindInPageResult("3/3")
            clickFindInPagePrevButton()
            verifyFindInPageResult("2/3")
            clickFindInPagePrevButton()
            verifyFindInPageResult("1/3")
        }.closeFindInPageWithCloseButton {
            verifyFindInPageBar(false)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickFindInPageButton {
            enterFindInPageQuery("3")
            verifyFindInPageResult("1/1")
        }.closeFindInPageWithBackButton {
            verifyFindInPageBar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860850
    @SmokeTest
    @Test
    fun verifyBookmarksMenuButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openBookmarks {
            verifyBookmarksMenuView()
        }.goBackToBrowserScreen {
            verifyPageContent(testPage.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860851
    @SmokeTest
    @Test
    fun verifyHistoryMenuButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openHistory {
            verifyHistoryMenuView()
        }.goBack {
            verifyPageContent(testPage.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860852
    @SmokeTest
    @Test
    fun verifyDownloadsMenuButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openDownloads {
            verifyEmptyDownloadsList(composeTestRule)
            exitMenu()
        }.exitDownloadsManagerToBrowser(composeTestRule) {
            verifyPageContent(testPage.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860853
    @SmokeTest
    @Test
    fun verifyPasswordsMenuButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openPasswords {
            verifySecurityPromptForLogins()
            tapSetupLater()
            verifyEmptySavedLoginsListView()
            TestHelper.exitMenu()
        }
        browserScreen {
            verifyPageContent(testPage.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860757
    // Verifies the main menu of a custom tab with a custom menu item
    @SmokeTest
    @Test
    fun verifyCustomTabMenuItemsTest() {
        val customMenuItem = "TestMenuItem"
        val customTabPage = getGenericAsset(mockWebServer, 1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
                customMenuItem,
            ),
        )

        customTabScreen {
            verifyCustomTabCloseButton()
        }.openMainMenuFromRedesignedToolbar {
            verifyRedesignedCustomTabsMainMenuItemsExist(customMenuItem, true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860761
    // The test opens a link in a custom tab then sends it to the browser
    @SmokeTest
    @Test
    fun openCustomTabInFirefoxTest() {
        val customTabPage = getGenericAsset(mockWebServer, 1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
            ),
        )

        customTabScreen {
            verifyCustomTabCloseButton()
        }.openMainMenuFromRedesignedToolbar {
        }.clickOpenInBrowserButtonFromRedesignedToolbar {
            verifyTabCounter("1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860762
    @SmokeTest
    @Test
    fun shareCustomTabUsingMainMenuButtonTest() {
        val customTabPage = getGenericAsset(mockWebServer, 1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
            ),
        )

        customTabScreen {
        }.openMainMenuFromRedesignedToolbar {
        }.clickShareButtonFromRedesignedMenu {
            verifyShareTabLayout()
            verifySharingWithSelectedApp(
                appName = "Gmail",
                content = customTabPage.url.toString(),
                subject = customTabPage.title,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860771
    @SmokeTest
    @Test
    fun verifyRecommendedExtensionsListTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            verifyRecommendedAddonsViewFromRedesignedMainMenu(composeTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860779
    @SmokeTest
    @Test
    fun verifyRedesignedMenuAfterRemovingAnExtensionTest() {
        val addonName = "uBlock Origin"
        val genericURL = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.openExtensionsFromMainMenu {
            waitForAddonsListProgressBarToBeGone()
            clickInstallAddon(addonName)
            verifyAddonPermissionPrompt(addonName)
            acceptPermissionToInstallAddon()
            verifyAddonInstallCompletedPrompt(addonName, composeTestRule.activityRule)
            closeAddonInstallCompletePrompt()
        }.goBack {
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu() {
            clickManageExtensionsButtonFromRedesignedMainMenu(composeTestRule)
        }.openDetailedMenuForAddon(addonName) {
        }.removeAddon(composeTestRule.activityRule) {
            verifySnackBarText("Successfully uninstalled $addonName")
            waitUntilSnackbarGone()
        }.goBack {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifyNoExtensionsButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860784
    @SmokeTest
    @Test
    fun verifyTheManageExtensionsSubMenuTest() {
        val addonName = "uBlock Origin"
        val genericURL = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.openExtensionsFromMainMenu {
            waitForAddonsListProgressBarToBeGone()
            clickInstallAddon(addonName)
            verifyAddonPermissionPrompt(addonName)
            acceptPermissionToInstallAddon()
            verifyAddonInstallCompletedPrompt(addonName, composeTestRule.activityRule)
            closeAddonInstallCompletePrompt()
        }.goBack {
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            clickManageExtensionsButtonFromRedesignedMainMenu(composeTestRule)
        }.goBack {
        }
        browserScreen {
            verifyPageContent(genericURL.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860812
    @SmokeTest
    @Test
    fun verifyTheSaveSubMenuItemsTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
            verifySaveSubMenuItems()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860813
    @SmokeTest
    @Test
    fun verifyTheBookmarkThisPageSubMenuItemsTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickBookmarkThisPageButton {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickEditBookmarkButton {
            verifyEditBookmarksView()
            clickDeleteInEditModeButton()
            confirmDeletion()
        }
        browserScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
            verifyBookmarkThisPageButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860814
    @SmokeTest
    @Test
    fun verifyTheAddToShortcutsSubMenuOptionTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
            verifyPageContent(testPage.content)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickAddToShortcutsButton() {
            verifySnackBarText(getStringResource(R.string.snackbar_added_to_shortcuts))
        }.goToHomescreenWithRedesignedToolbar {
            verifyExistingTopSitesTabs(testPage.title)
        }.openTopSiteTabWithTitle(testPage.title) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickRemoveFromShortcutsButton {
            verifySnackBarText(getStringResource(R.string.snackbar_top_site_removed))
        }.goToHomescreenWithRedesignedToolbar {
            verifyNotExistingTopSitesList(testPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860815
    @SmokeTest
    @Test
    fun verifyTheAddToHomeScreenSubMenuOptionTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickAddToHomeScreenButton {
            clickCancelShortcutButton()
        }
        browserScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickAddToHomeScreenButton {
            clickAddShortcutButton()
            clickSystemHomeScreenShortcutAddButton()
        }.openHomeScreenShortcut(testPage.title) {
            verifyPageContent(testPage.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860816
    @SmokeTest
    @Test
    fun verifyTheSaveToCollectionSubMenuOptionTest() {
        val collectionTitle = "First Collection"
        val firstTestPage = getGenericAsset(mockWebServer, 1)
        val secondTestPage = getGenericAsset(mockWebServer, 2)

        composeTestRule.activityRule.applySettingsExceptions {
            // Disabling these features to have better visibility of the Collections view
            it.isRecentlyVisitedFeatureEnabled = false
            it.isRecentTabsFeatureEnabled = false
        }

        MockBrowserDataHelper
            .createCollection(
                Pair(firstTestPage.url.toString(), firstTestPage.title),
                title = collectionTitle,
            )

        navigationToolbar {
        }.enterURLAndEnterToBrowser(secondTestPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickSaveToCollectionButton {
        }.selectExistingCollection(collectionTitle) {
            verifySnackBarText("Tab saved!")
        }.goToHomescreenWithRedesignedToolbar {
        }.expandCollection(collectionTitle) {
            verifyTabSavedInCollection(firstTestPage.title)
            verifyTabSavedInCollection(secondTestPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860817
    @SmokeTest
    @Test
    fun verifyTheSaveAsPDFSubMenuOptionTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            clickSaveButton()
        }.clickSaveAsPDFButton {
            verifyDownloadPrompt(testPage.title + ".pdf")
        }.clickDownload {
        }.clickOpen("application/pdf") {
            assertExternalAppOpens(GOOGLE_DOCS)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860796
    @SmokeTest
    @Test
    fun verifyTheDefaultToolsMenuItemsTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
            verifyTheDefaultToolsMenuItems()
            verifyReaderViewButtonIsEnabled(isEnabled = false)
            verifyOpenInAppButtonIsEnabled(isEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860798
    @SmokeTest
    @Test
    fun verifyTheReaderViewButtonTest() {
        val readerViewPage = TestAssetHelper.getLoremIpsumAsset(mockWebServer)
        val estimatedReadingTime = "1 - 2 minutes"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(readerViewPage.url) {
            verifyPageContent(readerViewPage.content)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
            verifyReaderViewButtonIsEnabled(isEnabled = true)
        }.clickTheReaderViewModeButton {
            waitForPageToLoad()
            verifyPageContent(estimatedReadingTime)
        }
        navigationToolbar {
            verifyReaderViewNavigationToolbarButton(isReaderViewEnabled = true)
        }
        browserScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
            verifyCustomizeReaderViewButtonIsDisplayed(isDisplayed = true)
        }.clickCustomizeReaderViewButton {
            verifyAppearanceFontGroup(true)
            verifyAppearanceFontSansSerif(true)
            verifyAppearanceFontSerif(true)
            verifyAppearanceFontIncrease(true)
            verifyAppearanceFontDecrease(true)
            verifyAppearanceFontSize(3)
            verifyAppearanceColorGroup(true)
            verifyAppearanceColorDark(true)
            verifyAppearanceColorLight(true)
            verifyAppearanceColorSepia(true)
        }.closeAppearanceMenu {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            openToolsMenu()
        }.clickTurnOffReaderViewButton {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            openToolsMenu()
            verifyReaderViewButtonIsEnabled(isEnabled = true)
            verifyCustomizeReaderViewButtonIsDisplayed(isDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860799
    @SmokeTest
    @Test
    fun verifyTheTranslatePageButtonsStatesTest() {
        val testPage = TestAssetHelper.getFirstForeignWebPageAsset(mockWebServer)

        navigationToolbar {
        }.enterURL(testPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
        }.clickTranslateButton {
            verifyTranslationSheetIsDisplayed(isDisplayed = true)
        }.clickTranslateButton {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            openToolsMenu()
        }.clickTranslatedToButton("English") {
            verifyTranslationSheetIsDisplayed(isDisplayed = true)
        }.clickShowOriginalButton {
            verifyPageContent(testPage.content)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            openToolsMenu()
            verifyTheDefaultToolsMenuItems()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860802
    @SmokeTest
    @Test
    fun verifyTheShareButtonTest() {
        val testPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
            verifyPageContent(testPage.content)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
        }.clickShareButton {
            verifyShareTabLayout()
            verifySharingWithSelectedApp(
                appName = "Gmail",
                content = testPage.url.toString(),
                subject = testPage.title,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860804
    @SmokeTest
    @Test
    fun verifyOpenInAppButtonIsEnabledTest() {
        val youtubeURL = "vnd.youtube://".toUri()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(youtubeURL) {
            waitForPageToLoad(waitingTime)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
            verifyOpenInAppButtonIsEnabled(appName = "YouTube", isEnabled = true)
            clickOpenInAppButton(appName = "YouTube")
            assertYoutubeAppOpens()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860746
    @SmokeTest
    @Test
    fun homeMainMenuExtensionsButtonOpensManageExtensionsTest() {
        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.openExtensionsFromMainMenu {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(composeTestRule.activity.findViewById(R.id.add_ons_list), 1),
            ) {
                verifyAddonsItems()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860845
    @SmokeTest
    @Test
    fun switchDesktopSiteModeOnOffTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifySwitchToDesktopSiteButton()
            clickSwitchToDesktopSiteButton()
        }
        browserScreen {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifySwitchToMobileSiteButton()
            clickSwitchToMobileSiteButton()
        }
        browserScreen {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifySwitchToDesktopSiteButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860801
    @SmokeTest
    @Test
    fun verifyPrintSubMenuOptionTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            mDevice.waitForIdle()
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            openToolsMenu()
            clickPrintContentButton()
            assertNativeAppOpens(PRINT_SPOOLER)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860778
    @SmokeTest
    @Test
    fun verifyRedesignedMenuAfterDisablingAnExtensionTest() {
        val addonName = "uBlock Origin"
        val genericURL = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.openExtensionsFromMainMenu {
            waitForAddonsListProgressBarToBeGone()
            clickInstallAddon(addonName)
            verifyAddonPermissionPrompt(addonName)
            acceptPermissionToInstallAddon()
            verifyAddonInstallCompletedPrompt(addonName, composeTestRule.activityRule)
            closeAddonInstallCompletePrompt()
        }.goBack {
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu() {
            clickManageExtensionsButtonFromRedesignedMainMenu(composeTestRule)
        }.openDetailedMenuForAddon(addonName) {
            disableExtension()
            verifySnackBarText("Successfully disabled $addonName")
            waitUntilSnackbarGone()
        }.goBack {
        }.goBack {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
            verifyNoExtensionsButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860775
    @SmokeTest
    @Test
    fun verifyExtensionInstallTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            val recommendedExtensionTitle = getRecommendedExtensionTitle(composeTestRule)
            installRecommendedAddon(recommendedExtensionTitle, composeTestRule)
            acceptPermissionToInstallAddon()
            verifyAddonInstallCompletedPrompt(recommendedExtensionTitle, composeTestRule.activityRule)
            closeAddonInstallCompletePrompt()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860741
    @Test
    fun verifyTheHomePageMainMenuSettingsButtonTest() {
        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.openSettings {
            verifySettingsToolbar()
        }.goBack {
            verifyHomeWordmark()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860743
    @Test
    fun verifyTheHomePageNewTabButtonInPrivateBrowsingModeTest() {
        homeScreen {
        }.togglePrivateBrowsingMode()

        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickNewTabButton {
            verifySearchToolbar(isDisplayed = true)
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860745
    @Test
    fun verifyTheHomePageNewPrivateTabButtonInNormalBrowsingModeTest() {
        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickNewPrivateTabButton {
            verifySearchToolbar(isDisplayed = true)
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860751
    @Test
    fun verifyTheHomePageMainMenuCustomizeHomepageButtonTest() {
        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.clickCustomizeHomepageButton {
            verifyHomePageView()
        }.goBackToHomeScreen {
            verifyHomeWordmark()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860725
    @Test
    fun verifyTheHomePageMainMenuCFRTest() {
        composeTestRule.activityRule.applySettingsExceptions {
            it.isMenuRedesignCFREnabled = true
        }
        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            verifyMainMenuCFR()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2728842
    @Test
    fun verifyFindInPageInPDFTest() {
        val testPage = getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
            clickPageObject(MatcherHelper.itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickFindInPageButton {
            verifyFindInPageNextButton()
            verifyFindInPagePrevButton()
            verifyFindInPageCloseButton()
            enterFindInPageQuery("l")
            verifyFindInPageResult("1/2")
            clickFindInPageNextButton()
            verifyFindInPageResult("2/2")
            clickFindInPagePrevButton()
            verifyFindInPageResult("1/2")
        }.closeFindInPageWithCloseButton {
            verifyFindInPageBar(false)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickFindInPageButton {
            enterFindInPageQuery("p")
            verifyFindInPageResult("1/1")
        }.closeFindInPageWithBackButton {
            verifyFindInPageBar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860854
    @Test
    fun verifyTheQuitFirefoxMenuItemTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            verifyDeleteBrowsingOnQuitEnabled(false)
            clickDeleteBrowsingOnQuitButtonSwitch()
            verifyDeleteBrowsingOnQuitEnabled(true)
        }.goBack {
            verifySettingsOptionSummary("Delete browsing data on quit", "On")
        }.goBack {
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            clickQuitFirefoxButton()
            restartApp(composeTestRule.activityRule)
        }
        homeScreen {
            verifyHomeWordmark()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860806
    @Test
    fun verifyTheDismissalWhenTappingOutsideTheToolsSubMenuTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            openToolsMenu()
        }.clickOutsideTheMainMenu {
            verifyToolsMenuDoesNotExist()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860819
    @Test
    fun verifyTheDismissalWhenTappingOutsideTheSaveSubMenuTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            clickSaveButton()
        }.clickOutsideTheMainMenu {
            verifySaveMenuDoesNotExist()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860759
    @Test
    fun verifyEnabledDesktopSiteToggleInCustomTabTest() {
        val customTabPage = getGenericAsset(mockWebServer, 1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
            ),
        )

        customTabScreen {
        }.openMainMenuFromRedesignedToolbar {
            verifySwitchToDesktopSiteButton(composeTestRule)
            clickSwitchToDesktopSiteButton(composeTestRule)
        }.openMainMenuFromRedesignedToolbar {
            verifySwitchToMobileSiteButton(composeTestRule)
            clickSwitchToMobileSiteButton(composeTestRule)
        }.openMainMenuFromRedesignedToolbar {
            verifySwitchToDesktopSiteButton(composeTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860760
    @Test
    fun customTabsFindInPageTest() {
        val customTabPage = getGenericAsset(mockWebServer, 3)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
            ),
        )

        customTabScreen {
        }.openMainMenuFromRedesignedToolbar {
        }.clickFindInPageButton(composeTestRule) {
            verifyFindInPageNextButton()
            verifyFindInPagePrevButton()
            verifyFindInPageCloseButton()
            enterFindInPageQuery("a")
            verifyFindInPageResult("1/3")
            clickFindInPageNextButton()
            verifyFindInPageResult("2/3")
            clickFindInPageNextButton()
            verifyFindInPageResult("3/3")
            clickFindInPagePrevButton()
            verifyFindInPageResult("2/3")
            clickFindInPagePrevButton()
            verifyFindInPageResult("1/3")
        }.closeFindInPageWithCloseButton {
            verifyFindInPageBar(false)
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
        }.clickFindInPageButton {
            enterFindInPageQuery("3")
            verifyFindInPageResult("1/1")
        }.closeFindInPageWithBackButton {
            verifyFindInPageBar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860765
    @Test
    fun verifyTheDismissalWhenTappingOutsideTheCustomTabMainMenuTest() {
        val customMenuItem = "TestMenuItem"
        val customTabPage = getGenericAsset(mockWebServer, 1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
            ),
        )

        customTabScreen {
        }.openMainMenuFromRedesignedToolbar {
        }.clickOutsideTheMainMenu {
        }
        customTabScreen {
            verifyRedesignedCustomTabsMainMenuItemsExist(customMenuItem, false, waitingTimeVeryShort)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860770
    @Test
    fun noAddonsInstalledExtensionPromotionBannerTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            verifyNoInstalledExtensionsPromotionBanner(composeTestRule)
        }.clickExtensionsPromotionBannerLearnMoreLink(composeTestRule) {
            verifyExtensionsPromotionBannerLearnMoreLinkURL()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860781
    @Test
    fun disabledAddonsExtensionPromotionBannerTest() {
        var recommendedExtensionTitle = ""
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            recommendedExtensionTitle = getRecommendedExtensionTitle(composeTestRule)
            installRecommendedAddon(recommendedExtensionTitle, composeTestRule)
            acceptPermissionToInstallAddon()
            verifyAddonInstallCompletedPrompt(recommendedExtensionTitle, composeTestRule.activityRule)
            closeAddonInstallCompletePrompt()
            clickManageExtensionsButtonFromRedesignedMainMenu(composeTestRule)
        }.openDetailedMenuForAddon(recommendedExtensionTitle) {
            disableExtension()
            waitUntilSnackbarGone()
        }.goBack {
        }.goBackToBrowser {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            verifyDisabledExtensionsPromotionBanner(composeTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860785
    @Test
    fun verifyTheDiscoverMoreExtensionSubMenuItemTest() {
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
        }.clickDiscoverMoreExtensionsButton(composeTestRule) {
            verifyUrl("addons.mozilla.org/en-US/android")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860790
    @Test
    fun verifyTheClosingBehaviourWhenTappingOutsideTheExtensionsSubMenuTest() {
        var recommendedExtensionTitle = ""
        val genericURL = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
        }
        mainMenuScreen(composeTestRule) {
        }.clickOutsideTheMainMenu {
            verifyExtensionsMenuDoesNotExist()
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            recommendedExtensionTitle = getRecommendedExtensionTitle(composeTestRule)
            installRecommendedAddon(recommendedExtensionTitle, composeTestRule)
            acceptPermissionToInstallAddon()
            verifyAddonInstallCompletedPrompt(recommendedExtensionTitle, composeTestRule.activityRule)
            closeAddonInstallCompletePrompt()
        }
        mainMenuScreen(composeTestRule) {
        }.clickOutsideTheMainMenu {
            verifyExtensionsMenuDoesNotExist()
        }
        browserScreen {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
            clickManageExtensionsButtonFromRedesignedMainMenu(composeTestRule)
        }.openDetailedMenuForAddon(recommendedExtensionTitle) {
            disableExtension()
            waitUntilSnackbarGone()
        }.goBack {
        }.goBackToBrowser {
        }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            expandMainMenu()
        }.openExtensionsFromMainMenu {
        }
        mainMenuScreen(composeTestRule) {
        }.clickOutsideTheMainMenu {
            verifyExtensionsMenuDoesNotExist()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2860800
    @Test
    fun verifyTheReportBrokenSiteOptionTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.clickReportBrokenSiteButton {
                verifyWebCompatReporterViewItems(composeTestRule, websiteURL = defaultWebPage.url.toString())
            }.closeWebCompatReporter {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            }.openSettings {
            }.openSettingsSubMenuDataCollection {
                clickUsageAndTechnicalDataToggle()
                verifyUsageAndTechnicalDataToggle(enabled = false)
            }

            exitMenu()

            browserScreen {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.clickReportBrokenSiteButton {
                verifyUrl("webcompat.com/issues/new")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939173
    @Test
    fun verifyTheWhatIsBrokenErrorMessageTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWebCompatReporterViewItems(composeTestRule, defaultWebPage.url.toString())
                verifyWhatIsBrokenField(composeTestRule)
                verifySendButtonIsEnabled(composeTestRule, isEnabled = false)
                clickChooseReasonField(composeTestRule)
                clickSiteSlowOrNotWorkingReason(composeTestRule)
                verifyChooseReasonErrorMessageIsNotDisplayed(composeTestRule)
                verifySendButtonIsEnabled(composeTestRule, isEnabled = true)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939175
    @Test
    fun verifyThatTheBrokenSiteFormCanBeCanceledTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWebCompatReporterViewItems(composeTestRule, defaultWebPage.url.toString())
                clickChooseReasonField(composeTestRule)
                clickSiteSlowOrNotWorkingReason(composeTestRule)
                clickBrokenSiteFormCancelButton(composeTestRule)
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWhatIsBrokenField(composeTestRule)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939176
    @Test
    fun verifyTheBrokenSiteFormSubmissionTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWebCompatReporterViewItems(composeTestRule, defaultWebPage.url.toString())
                clickChooseReasonField(composeTestRule)
                clickSiteSlowOrNotWorkingReason(composeTestRule)
                describeBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time")
                clickBrokenSiteFormSendButton(composeTestRule)
            }
            browserScreen {
                verifySnackBarText("Your report was sent")
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWhatIsBrokenField(composeTestRule)
                verifyBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time", isDisplayed = false)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939179
    @Test
    fun verifyThatTheBrokenSiteFormInfoPersistsTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWebCompatReporterViewItems(composeTestRule, defaultWebPage.url.toString())
                clickChooseReasonField(composeTestRule)
                clickSiteSlowOrNotWorkingReason(composeTestRule)
                describeBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time")
            }.closeWebCompatReporter {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time", isDisplayed = true)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939180
    @Test
    fun verifyTheBrokenSiteFormIsEmptyWithoutSubmittingThePreviousOneTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val firstWebPage = getGenericAsset(mockWebServer, 1)
            val secondWebPage = getGenericAsset(mockWebServer, 2)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(firstWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWebCompatReporterViewItems(composeTestRule, firstWebPage.url.toString())
                clickChooseReasonField(composeTestRule)
                clickSiteSlowOrNotWorkingReason(composeTestRule)
                describeBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time")
            }.closeWebCompatReporter {
            }.openTabDrawer(composeTestRule) {
            }.openNewTab {
            }.submitQuery(secondWebPage.url.toString()) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWhatIsBrokenField(composeTestRule)
                verifyBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time", isDisplayed = false)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939181
    @Test
    fun verifyThatTheBrokenSiteFormInfoIsErasedWhenKillingTheAppTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWebCompatReporterViewItems(composeTestRule, defaultWebPage.url.toString())
                clickChooseReasonField(composeTestRule)
                clickSiteSlowOrNotWorkingReason(composeTestRule)
                describeBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time")
            }
            closeApp(composeTestRule.activityRule)
            restartApp(composeTestRule.activityRule)

            browserScreen {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyWhatIsBrokenField(composeTestRule)
                verifyBrokenSiteProblem(composeTestRule, problemDescription = "Prolonged page loading time", isDisplayed = false)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2939182
    @Test
    fun verifyReportBrokenSiteFormNotDisplayedWhenTelemetryIsDisabledTest() {
        runWithCondition(
            // This test will not run on RC builds because the "Report site issue button" is not available.
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            homeScreen {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
            }.openSettings {
            }.openSettingsSubMenuDataCollection {
                clickUsageAndTechnicalDataToggle()
                verifyUsageAndTechnicalDataToggle(enabled = false)
            }

            exitMenu()

            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openThreeDotMenuFromRedesignedToolbar(composeTestRule) {
                openToolsMenu()
            }.openReportBrokenSite {
                verifyUrl("webcompat.com/issues/new")
            }
        }
    }
}
