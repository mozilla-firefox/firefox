/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithAppLocaleChanged
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import java.util.Locale

/**
 *  Tests for verifying basic functionality of browser navigation and page related interactions
 *
 *  Including:
 *  - Visiting a URL
 *  - Back and Forward navigation
 *  - Refresh
 *  - Find in page
 */

class NavigationToolbarTest : TestSetup() {
    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/987326
    // Swipes the nav bar left/right to switch between tabs
    @SmokeTest
    @Test
    @SkipLeaks
    fun swipeToSwitchTabTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
        }.openTabDrawer(composeTestRule) {
        }.openNewTab {
        }.submitQuery(secondWebPage.url.toString()) {
            swipeNavBarRight(secondWebPage.url.toString())
            verifyUrl(firstWebPage.url.toString())
            swipeNavBarLeft(firstWebPage.url.toString())
            verifyUrl(secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/987327
    @Test
    @SkipLeaks
    fun swipeToSwitchTabInRTLTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)
        val arabicLocale = Locale.Builder().setLanguage("ar").setRegion("AR").build()

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        runWithAppLocaleChanged(arabicLocale, composeTestRule.activityRule) {
            navigationToolbar {
            }.enterURLAndEnterToBrowser(firstWebPage.url) {
            }.openTabDrawer(composeTestRule) {
            }.openNewTab {
            }.submitQuery(secondWebPage.url.toString()) {
                swipeNavBarLeft(secondWebPage.url.toString())
                verifyUrl(firstWebPage.url.toString())
                swipeNavBarRight(firstWebPage.url.toString())
                verifyUrl(secondWebPage.url.toString())
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2265279
    @SmokeTest
    @Test
    fun verifySecurePageSecuritySubMenuTest() {
        val defaultWebPage = "https://mozilla-mobile.github.io/testapp/loginForm"
        val defaultWebPageTitle = "Login_form"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.toUri()) {
            verifyPageContent("Login Form")
        }.openSiteSecuritySheet {
            verifyQuickActionSheet(defaultWebPage, true)
            openSecureConnectionSubMenu(true)
            verifySecureConnectionSubMenu(defaultWebPageTitle, defaultWebPage, true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2265280
    @SmokeTest
    @Test
    fun verifyInsecurePageSecuritySubMenuTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }.openSiteSecuritySheet {
            verifyQuickActionSheet(defaultWebPage.url.toString(), false)
            openSecureConnectionSubMenu(false)
            verifySecureConnectionSubMenu(defaultWebPage.title, defaultWebPage.url.toString(), false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1661318
    @SmokeTest
    @Test
    @SkipLeaks
    fun verifyClearCookiesFromQuickSettingsTest() {
        val loginPage = "https://mozilla-mobile.github.io/testapp/loginForm"
        val originWebsite = "mozilla-mobile.github.io"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(loginPage.toUri()) {
            waitForPageToLoad(waitingTimeLong)
        }.openSiteSecuritySheet {
            clickQuickActionSheetClearSiteData()
            verifyClearSiteDataPrompt(originWebsite)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1360555
    @SmokeTest
    @Test
    fun goToHomeScreenTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            mDevice.waitForIdle()
        }.goToHomescreen {
            verifyHomeScreen()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2256552
    @SmokeTest
    @Test
    fun goToHomeScreenInPrivateModeTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
            togglePrivateBrowsingModeOnOff()
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            mDevice.waitForIdle()
        }.goToHomescreen {
            verifyHomeScreen()
        }
    }
}
