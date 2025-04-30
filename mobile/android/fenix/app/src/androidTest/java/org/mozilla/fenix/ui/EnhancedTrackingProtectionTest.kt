/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.espresso.Espresso.pressBack
import mozilla.components.concept.engine.utils.EngineReleaseChannel
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getEnhancedTrackingProtectionAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.TestHelper.scrollToElementByText
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.enhancedTrackingProtection
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying basic UI functionality of Enhanced Tracking Protection
 *
 *  Including
 *  - Verifying default states
 *  - Verifying Enhanced Tracking Protection notification bubble
 *  - Verifying Enhanced Tracking Protection content sheet
 *  - Verifying Enhanced Tracking Protection content sheet details
 *  - Verifying Enhanced Tracking Protection toggle
 *  - Verifying Enhanced Tracking Protection options and functionality
 *  - Verifying Enhanced Tracking Protection site exceptions
 */

class EnhancedTrackingProtectionTest : TestSetup() {
    @get:Rule
    val activityTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/416046
    @Test
    fun testETPSettingsItemsAndSubMenus() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
            verifyEnhancedTrackingProtectionButton()
            verifySettingsOptionSummary("Enhanced Tracking Protection", "Standard")
        }.openEnhancedTrackingProtectionSubMenu {
            verifyEnhancedTrackingProtectionSummary()
            verifyLearnMoreText()
            verifyEnhancedTrackingProtectionTextWithSwitchWidget()
            verifyTrackingProtectionSwitchEnabled()
            verifyEnhancedTrackingProtectionOptionsEnabled()
            verifyEnhancedTrackingProtectionLevelSelected("Standard (default)", true)
            verifyStandardOptionDescription()
            verifyStrictOptionDescription()
            verifyGPCTextWithSwitchWidget()
            verifyGPCSwitchEnabled(false)
            selectTrackingProtectionOption("Custom")
            verifyCustomTrackingProtectionSettings()
            scrollToElementByText("Standard (default)")
            verifyWhatsBlockedByStandardETPInfo()
            pressBack()
            verifyWhatsBlockedByStrictETPInfo()
            pressBack()
            verifyWhatsBlockedByCustomETPInfo()
            pressBack()
        }.openExceptions {
            verifyTPExceptionsDefaultView()
            openExceptionsLearnMoreLink()
        }
        browserScreen {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            verifyETPLearnMoreURL()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1514599
    @Test
    fun verifyETPStateIsReflectedInTPSheetTest() {
        val genericPage = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            switchEnhancedTrackingProtectionToggle()
            verifyEnhancedTrackingProtectionOptionsEnabled(false)
        }.goBack {
            verifySettingsOptionSummary("Enhanced Tracking Protection", "Off")
            exitMenu()
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) { }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyETPSwitchVisibility(false)
        }.closeEnhancedTrackingProtectionSheet {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            switchEnhancedTrackingProtectionToggle()
            verifyEnhancedTrackingProtectionOptionsEnabled(true)
        }.goBack {
        }.goBackToBrowser { }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyETPSwitchVisibility(true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/339712
    // Tests adding ETP exceptions to websites and keeping that preference after restart
    @SmokeTest
    @Test
    fun disablingETPOnAWebsiteAddsItToExceptionListTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = "example.com"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {}
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.toggleEnhancedTrackingProtectionFromSheet {
            verifyEnhancedTrackingProtectionSheetStatus("OFF", false)
        }.closeEnhancedTrackingProtectionSheet {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(secondPage.toUri()) {
            verifyPageContent("Example Domain")
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyEnhancedTrackingProtectionSheetStatus("ON", true)
        }.toggleEnhancedTrackingProtectionFromSheet {
            verifyEnhancedTrackingProtectionSheetStatus("OFF", false)
        }
        restartApp(activityTestRule.activityRule)
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyEnhancedTrackingProtectionSheetStatus("OFF", false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/339714
    @Test
    fun enablingETPOnAWebsiteRemovesItFromTheExceptionListTest() {
        val trackingPage = getEnhancedTrackingProtectionAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(trackingPage.url) {
            verifyUrl(trackingPage.url.toString())
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.toggleEnhancedTrackingProtectionFromSheet {
            verifyEnhancedTrackingProtectionSheetStatus("OFF", false)
        }.closeEnhancedTrackingProtectionSheet {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
        }.openExceptions {
            verifySiteExceptionExists(trackingPage.url.host.toString(), true)
            exitMenu()
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.toggleEnhancedTrackingProtectionFromSheet {
            verifyEnhancedTrackingProtectionSheetStatus("ON", true)
        }.closeEnhancedTrackingProtectionSheet {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
        }.openExceptions {
            verifySiteExceptionExists(trackingPage.url.host.toString(), false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/339713
    // Tests removing TP exceptions individually or all at once
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1865781")
    @Test
    fun clearWebsitesFromTPExceptionListTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = "example.com"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {}
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.toggleEnhancedTrackingProtectionFromSheet {
            verifyEnhancedTrackingProtectionSheetStatus("OFF", false)
        }.closeEnhancedTrackingProtectionSheet {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(secondPage.toUri()) {
            verifyPageContent("Example Domain")
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.toggleEnhancedTrackingProtectionFromSheet {
            verifyEnhancedTrackingProtectionSheetStatus("OFF", false)
        }.closeEnhancedTrackingProtectionSheet {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
        }.openExceptions {
            removeOneSiteException(secondPage)
        }.disableExceptions {
            verifyTPExceptionsDefaultView()
            exitMenu()
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyEnhancedTrackingProtectionSheetStatus("ON", true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/417444
    @Test
    fun verifyTrackersBlockedWithStandardTPTest() {
        val genericPage = getGenericAsset(mockWebServer, 1)
        val trackingProtectionTest = getEnhancedTrackingProtectionAsset(mockWebServer).url

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
            verifyEnhancedTrackingProtectionButton()
            verifySettingsOptionSummary("Enhanced Tracking Protection", "Standard")
            exitMenu()
        }

        // browsing a generic page to allow GV to load on a fresh run
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) {
            verifyPageContent(genericPage.content)
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(trackingProtectionTest) {
            verifyTrackingProtectionWebContent("social not blocked")
            verifyTrackingProtectionWebContent("ads not blocked")
            verifyTrackingProtectionWebContent("analytics not blocked")
            verifyTrackingProtectionWebContent("Fingerprinting blocked")
            verifyTrackingProtectionWebContent("Cryptomining blocked")
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyEnhancedTrackingProtectionSheetStatus("ON", true)
        }.openDetails {
            // Third-party cookie tracker blocking in Nightly was disabled: https://bugzilla.mozilla.org/show_bug.cgi?id=1935156
            if (activityTestRule.activity.components.core.engine.version.releaseChannel == EngineReleaseChannel.BETA &&
                activityTestRule.activity.components.core.engine.version.releaseChannel == EngineReleaseChannel.RELEASE
            ) {
                verifyCrossSiteCookiesBlocked(true)
                navigateBackToDetails()
            }
            verifyCryptominersBlocked(true)
            navigateBackToDetails()
            verifyFingerprintersBlocked(true)
            navigateBackToDetails()
            verifyTrackingContentBlocked(false)
        }.closeEnhancedTrackingProtectionSheet {}
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/417441
    @Test
    fun verifyTrackersBlockedWithStrictTPTest() {
        appContext.settings().setStrictETP()
        val genericPage = getGenericAsset(mockWebServer, 1)
        val trackingProtectionTest = getEnhancedTrackingProtectionAsset(mockWebServer).url

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
            verifyEnhancedTrackingProtectionButton()
            verifySettingsOptionSummary("Enhanced Tracking Protection", "Strict")
            exitMenu()
        }

        // browsing a generic page to allow GV to load on a fresh run
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) {
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(trackingProtectionTest) {
            verifyTrackingProtectionWebContent("social blocked")
            verifyTrackingProtectionWebContent("ads blocked")
            verifyTrackingProtectionWebContent("analytics blocked")
            verifyTrackingProtectionWebContent("Fingerprinting blocked")
            verifyTrackingProtectionWebContent("Cryptomining blocked")
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
            verifyEnhancedTrackingProtectionSheetStatus("ON", true)
        }.openDetails {
            verifySocialMediaTrackersBlocked(true)
            navigateBackToDetails()
            verifyCryptominersBlocked(true)
            navigateBackToDetails()
            verifyFingerprintersBlocked(true)
            navigateBackToDetails()
            verifyTrackingContentBlocked(true)
            viewTrackingContentBlockList()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/561637
    @SmokeTest
    @Test
    fun verifyTrackersBlockedWithCustomTPTest() {
        val genericWebPage = getGenericAsset(mockWebServer, 1)
        val trackingPage = getEnhancedTrackingProtectionAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            selectTrackingProtectionOption("Custom")
            verifyCustomTrackingProtectionSettings()
        }.goBack {
            verifySettingsOptionSummary("Enhanced Tracking Protection", "Custom")
            exitMenu()
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericWebPage.url) {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(trackingPage.url) {
            verifyTrackingProtectionWebContent("social blocked")
            verifyTrackingProtectionWebContent("ads blocked")
            verifyTrackingProtectionWebContent("analytics blocked")
            verifyTrackingProtectionWebContent("Fingerprinting blocked")
            verifyTrackingProtectionWebContent("Cryptomining blocked")
        }

        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.openDetails {
            verifyCryptominersBlocked(true)
            navigateBackToDetails()
            verifyFingerprintersBlocked(true)
            navigateBackToDetails()
            verifyTrackingContentBlocked(true)
            viewTrackingContentBlockList()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/562710
    // Tests the trackers blocked with the following Custom TP set up:
    // - Cookies set to "All cookies"
    // - Tracking content option OFF
    // - Fingerprinters, cryptominers and redirect trackers checked
    @Test
    fun customizedTrackingProtectionOptionsTest() {
        val genericWebPage = getGenericAsset(mockWebServer, 1)
        val trackingPage = getEnhancedTrackingProtectionAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            selectTrackingProtectionOption("Custom")
            verifyCustomTrackingProtectionSettings()
            selectTrackingProtectionOption("Isolate cross-site cookies")
            selectTrackingProtectionOption("All cookies (will cause websites to break)")
            selectTrackingProtectionOption("Tracking content")
        }.goBackToHomeScreen {
            mDevice.waitForIdle()
        }.openNavigationToolbar {
            // browsing a basic page to allow GV to load on a fresh run
        }.enterURLAndEnterToBrowser(genericWebPage.url) {
            waitForPageToLoad()
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(trackingPage.url) {
            verifyTrackingProtectionWebContent("social not blocked")
            verifyTrackingProtectionWebContent("ads not blocked")
            verifyTrackingProtectionWebContent("analytics not blocked")
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.openDetails {
            verifyCrossSiteCookiesBlocked(true)
            navigateBackToDetails()
            verifyCryptominersBlocked(true)
            navigateBackToDetails()
            verifyFingerprintersBlocked(true)
            navigateBackToDetails()
            verifyTrackingContentBlocked(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/562709
    @Test
    fun verifyTrackersBlockedWithCustomTPOptionsDisabledTest() {
        val genericWebPage = getGenericAsset(mockWebServer, 1)
        val trackingPage = getEnhancedTrackingProtectionAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            selectTrackingProtectionOption("Custom")
            verifyCustomTrackingProtectionSettings()
            selectTrackingProtectionOption("Cookies")
            selectTrackingProtectionOption("Tracking content")
            selectTrackingProtectionOption("Cryptominers")
            selectTrackingProtectionOption("Known Fingerprinters")
            selectTrackingProtectionOption("Suspected Fingerprinters")
            selectTrackingProtectionOption("Redirect Trackers")
        }.goBackToHomeScreen {
            mDevice.waitForIdle()
        }.openNavigationToolbar {
            // browsing a basic page to allow GV to load on a fresh run
        }.enterURLAndEnterToBrowser(genericWebPage.url) {
            waitForPageToLoad()
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(trackingPage.url) {
            verifyTrackingProtectionWebContent("social not blocked")
            verifyTrackingProtectionWebContent("ads not blocked")
            verifyTrackingProtectionWebContent("analytics not blocked")
            verifyTrackingProtectionWebContent("Fingerprinting not blocked")
            verifyTrackingProtectionWebContent("Cryptomining not blocked")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2106997
    @Test
    fun verifyTrackingContentBlockedOnlyInPrivateTabsTest() {
        val genericWebPage = getGenericAsset(mockWebServer, 1)
        val trackingPage = getEnhancedTrackingProtectionAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            verifyEnhancedTrackingProtectionOptionsEnabled()
            selectTrackingProtectionOption("Custom")
            verifyCustomTrackingProtectionSettings()
            selectTrackingProtectionOption("In all tabs")
            selectTrackingProtectionOption("Only in Private tabs")
        }.goBackToHomeScreen {
        }.openNavigationToolbar {
            // browsing a basic page to allow GV to load on a fresh run
        }.enterURLAndEnterToBrowser(genericWebPage.url) {
            waitForPageToLoad()
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(trackingPage.url) {
            verifyTrackingProtectionWebContent("social not blocked")
            verifyTrackingProtectionWebContent("ads not blocked")
            verifyTrackingProtectionWebContent("analytics not blocked")
            verifyTrackingProtectionWebContent("Fingerprinting blocked")
            verifyTrackingProtectionWebContent("Cryptomining blocked")
        }.goToHomescreen {
        }.togglePrivateBrowsingMode()
        navigationToolbar {
        }.enterURLAndEnterToBrowser(trackingPage.url) {
            verifyTrackingProtectionWebContent("social blocked")
            verifyTrackingProtectionWebContent("ads blocked")
            verifyTrackingProtectionWebContent("analytics blocked")
            verifyTrackingProtectionWebContent("Fingerprinting blocked")
            verifyTrackingProtectionWebContent("Cryptomining blocked")
        }
        enhancedTrackingProtection {
        }.openEnhancedTrackingProtectionSheet {
        }.openDetails {
            // Third-party cookie tracker blocking in Nightly was disabled: https://bugzilla.mozilla.org/show_bug.cgi?id=1935156
            if (
                activityTestRule.activity.components.core.engine.version.releaseChannel == EngineReleaseChannel.BETA &&
                activityTestRule.activity.components.core.engine.version.releaseChannel == EngineReleaseChannel.RELEASE
            ) {
                verifyCrossSiteCookiesBlocked(true)
                navigateBackToDetails()
            }
            verifyCryptominersBlocked(true)
            navigateBackToDetails()
            verifyFingerprintersBlocked(true)
            navigateBackToDetails()
            verifyTrackingContentBlocked(true)
            viewTrackingContentBlockList()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2285368
    @SmokeTest
    @Test
    fun blockCookiesStorageAccessTest() {
        // With Standard TrackingProtection settings
        val genericWebPage = getGenericAsset(mockWebServer, 1)
        val testPage = mockWebServer.url("pages/cross-site-cookies.html").toString().toUri()
        val originHost = "mozilla-mobile.github.io"
        val currentHost = "localhost"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericWebPage.url) {
            waitForPageToLoad()
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(testPage) {
            waitForPageToLoad()
        }.clickRequestStorageAccessButton {
            verifyCrossOriginCookiesPermissionPrompt(originHost, currentHost)
        }.clickPagePermissionButton(allow = false) {
            verifyPageContent("access denied")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2285369
    @SmokeTest
    @Test
    fun allowCookiesStorageAccessTest() {
        // With Standard TrackingProtection settings
        val genericWebPage = getGenericAsset(mockWebServer, 1)
        val testPage = mockWebServer.url("pages/cross-site-cookies.html").toString().toUri()
        val originHost = "mozilla-mobile.github.io"
        val currentHost = "localhost"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericWebPage.url) {
            waitForPageToLoad()
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(testPage) {
            waitForPageToLoad()
        }.clickRequestStorageAccessButton {
            verifyCrossOriginCookiesPermissionPrompt(originHost, currentHost)
        }.clickPagePermissionButton(allow = true) {
            verifyPageContent("access granted")
        }
    }
}
