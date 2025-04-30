/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.Manifest
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.espresso.Espresso.pressBack
import androidx.test.rule.GrantPermissionRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.setNetworkEnabled
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getStorageTestAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.downloadRobot
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying the Settings for:
 *  Delete Browsing Data on quit
 *
 */
class SettingsDeleteBrowsingDataOnQuitTest : TestSetup() {
    @get:Rule(order = 0)
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(
                skipOnboarding = true,
            ),
        ) { it.activity }

    @get:Rule(order = 1)
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // Automatically allows app permissions, avoiding a system dialog showing up.
    @get:Rule(order = 2)
    val grantPermissionRule: GrantPermissionRule = GrantPermissionRule.grant(
        Manifest.permission.RECORD_AUDIO,
    )

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416048
    @Test
    fun deleteBrowsingDataOnQuitSettingTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            verifyNavigationToolBarHeader()
            verifyDeleteBrowsingOnQuitEnabled(false)
            verifyDeleteBrowsingOnQuitButtonSummary()
            verifyDeleteBrowsingOnQuitEnabled(false)
            clickDeleteBrowsingOnQuitButtonSwitch()
            verifyDeleteBrowsingOnQuitEnabled(true)
            verifyAllTheCheckBoxesText()
            verifyAllTheCheckBoxesChecked(true)
        }.goBack {
            verifySettingsOptionSummary("Delete browsing data on quit", "On")
        }.goBack {
        }.openThreeDotMenu {
            verifyQuitButtonExists()
            pressBack()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser("test".toUri()) {
        }.openThreeDotMenu {
            verifyQuitButtonExists()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416049
    @Test
    fun deleteOpenTabsOnQuitTest() {
        val testPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            clickDeleteBrowsingOnQuitButtonSwitch()
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.goToHomescreen {
        }.openThreeDotMenu {
            clickQuit()
            restartApp(composeTestRule.activityRule)
        }
        homeScreen {
        }.openTabDrawer(composeTestRule) {
            verifyNoOpenTabsInNormalBrowsing()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416050
    @Test
    fun deleteBrowsingHistoryOnQuitTest() {
        val genericPage =
            getStorageTestAsset(mockWebServer, "generic1.html")

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            clickDeleteBrowsingOnQuitButtonSwitch()
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) {
        }.goToHomescreen {
        }.openThreeDotMenu {
            clickQuit()
            restartApp(composeTestRule.activityRule)
        }

        homeScreen {
        }.openThreeDotMenu {
        }.openHistory {
            verifyEmptyHistoryView()
            exitMenu()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416051
    @Test
    fun deleteCookiesAndSiteDataOnQuitTest() {
        val storageWritePage =
            getStorageTestAsset(mockWebServer, "storage_write.html")
        val storageCheckPage =
            getStorageTestAsset(mockWebServer, "storage_check.html")

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            clickDeleteBrowsingOnQuitButtonSwitch()
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(storageWritePage.url) {
            clickPageObject(MatcherHelper.itemWithText("Set cookies"))
            verifyPageContent("Values written to storage")
        }.goToHomescreen {
        }.openThreeDotMenu {
            clickQuit()
            restartApp(composeTestRule.activityRule)
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(storageCheckPage.url) {
            verifyPageContent("Session storage empty")
            verifyPageContent("Local storage empty")
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(storageWritePage.url) {
            verifyPageContent("No cookies set")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1243096
    @SmokeTest
    @Test
    fun deleteDownloadsOnQuitTest() {
        val downloadTestPage = "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            clickDeleteBrowsingOnQuitButtonSwitch()
            exitMenu()
        }
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "smallZip.zip")
            verifyDownloadCompleteNotificationPopup()
        }.closeDownloadPrompt {
        }.goToHomescreen {
        }.openThreeDotMenu {
            clickQuit()
            mDevice.waitForIdle()
        }
        restartApp(composeTestRule.activityRule)
        homeScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyEmptyDownloadsList(composeTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416053
    @SmokeTest
    @Test
    fun deleteSitePermissionsOnQuitTest() {
        val testPage = "https://mozilla-mobile.github.io/testapp/permissions"
        val testPageHost = "mozilla-mobile.github.io"

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            clickDeleteBrowsingOnQuitButtonSwitch()
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.toUri()) {
            verifyPageContent("Open microphone")
        }.clickStartMicrophoneButton {
            verifyMicrophonePermissionPrompt(testPageHost)
            selectRememberPermissionDecision()
        }.clickPagePermissionButton(false) {
            verifyPageContent("Microphone not allowed")
        }.goToHomescreen {
        }.openThreeDotMenu {
            clickQuit()
            mDevice.waitForIdle()
        }
        restartApp(composeTestRule.activityRule)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.toUri()) {
            verifyPageContent("Open microphone")
        }.clickStartMicrophoneButton {
            verifyMicrophonePermissionPrompt(testPageHost)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416052
    @Test
    fun deleteCachedFilesOnQuitTest() {
        val pocketTopArticles = getStringResource(R.string.pocket_pinned_top_articles)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuDeleteBrowsingDataOnQuit {
            clickDeleteBrowsingOnQuitButtonSwitch()
            exitMenu()
        }
        homeScreen {
            verifyExistingTopSitesTabs(pocketTopArticles)
        }.openTopSiteTabWithTitle(pocketTopArticles) {
            verifyPocketPageContent()
        }.goToHomescreen {
        }.openThreeDotMenu {
            clickQuit()
            mDevice.waitForIdle()
        }
        // disabling wifi to prevent downloads in the background
        setNetworkEnabled(enabled = false)
        restartApp(composeTestRule.activityRule)
        navigationToolbar {
        }.enterURLAndEnterToBrowser("about:cache".toUri()) {
            verifyNetworkCacheIsEmpty("memory")
            verifyNetworkCacheIsEmpty("disk")
        }
        setNetworkEnabled(enabled = true)
    }
}
