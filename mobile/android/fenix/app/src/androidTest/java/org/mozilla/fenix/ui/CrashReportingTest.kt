/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class CrashReportingTest : TestSetup() {
    @get:Rule
    val activityTestRule = AndroidComposeTestRule(
        HomeActivityIntentTestRule(
            isPocketEnabled = false,
            isWallpaperOnboardingEnabled = false,
        ),
    ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/308906
    @Test
    fun closeTabFromCrashedTabReporterTest() {
        homeScreen {
        }.openNavigationToolbar {
        }.openTabCrashReporter {
        }.clickTabCrashedCloseButton {
        }.openTabDrawer(activityTestRule) {
            verifyNoOpenTabsInNormalBrowsing()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2336134
    @Test
    fun restoreTabFromTabCrashedReporterTest() {
        val website = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(website.url) {}

        navigationToolbar {
        }.openTabCrashReporter {
            clickPageObject(itemWithResId("$packageName:id/restoreTabButton"))
            verifyPageContent(website.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1681928
    @SmokeTest
    @Test
    fun useAppWhileTabIsCrashedTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        homeScreen {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
            mDevice.waitForIdle()
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondWebPage.url.toString()) {
            waitForPageToLoad()
        }

        navigationToolbar {
        }.openTabCrashReporter {
            verifyTabCrashReporterView()
        }.openTabDrawer(activityTestRule) {
            verifyExistingOpenTabs(firstWebPage.title)
            verifyExistingOpenTabs(secondWebPage.title)
        }.closeTabDrawer {
        }.goToHomescreen {
            verifyExistingTopSitesList()
        }.openThreeDotMenu {
            verifySettingsButton()
        }
    }
}
