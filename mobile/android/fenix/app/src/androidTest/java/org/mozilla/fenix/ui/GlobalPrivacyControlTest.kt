/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.TestAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGPCTestAsset
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests for Global Privacy Control setting.
 */

class GlobalPrivacyControlTest : TestSetup() {
    private lateinit var gpcPage: TestAsset

    @get:Rule
    val activityTestRule = HomeActivityIntentTestRule(
        isWallpaperOnboardingEnabled = false,
        skipOnboarding = true,
    )

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Before
    override fun setUp() {
        super.setUp()
        gpcPage = getGPCTestAsset(mockWebServer)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2429327
    @Test
    fun testGPCinNormalBrowsing() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(gpcPage.url) {
            verifyPageContent("GPC not enabled.")
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            scrollToGCPSettings()
            verifyGPCTextWithSwitchWidget()
            verifyGPCSwitchEnabled(false)
            switchGPCToggle()
        }.goBack {
        }.goBackToBrowser {
            verifyPageContent("GPC is enabled.")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2429364
    @Test
    fun testGPCinPrivateBrowsing() {
        homeScreen { }.togglePrivateBrowsingMode()
        navigationToolbar {
        }.enterURLAndEnterToBrowser(gpcPage.url) {
            verifyPageContent("GPC is enabled.")
        }.openThreeDotMenu {
        }.openSettings {
        }.openEnhancedTrackingProtectionSubMenu {
            scrollToGCPSettings()
            verifyGPCTextWithSwitchWidget()
            verifyGPCSwitchEnabled(false)
            switchGPCToggle()
        }.goBack {
        }.goBackToBrowser {
            verifyPageContent("GPC is enabled.")
        }
    }
}
