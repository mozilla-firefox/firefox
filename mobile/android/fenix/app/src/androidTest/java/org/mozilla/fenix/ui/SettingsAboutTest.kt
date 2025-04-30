/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.test.uiautomator.UiSelector
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickRateButtonGooglePlay
import org.mozilla.fenix.ui.robots.homeScreen

/**
 *  Tests for verifying the main three dot menu options
 *
 */

class SettingsAboutTest : TestSetup() {
    @get:Rule
    val activityIntentTestRule = HomeActivityIntentTestRule.withDefaultSettingsOverrides()

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // Walks through the About settings menu to ensure all items are present
    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2092700
    @Test
    fun verifyAboutSettingsItemsTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
            verifyAboutHeading()
            verifyRateOnGooglePlay()
            verifyAboutFirefoxPreview()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246966
    @Test
    fun verifyRateOnGooglePlayButton() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
            clickRateButtonGooglePlay()
            verifyGooglePlayRedirect()
            // press back to return to the app, or accept ToS if still visible
            mDevice.pressBack()
            dismissGooglePlayToS()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246961
    @Test
    fun verifyAboutFirefoxMenuItems() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAboutFirefoxPreview {
            verifyAboutFirefoxPreviewInfo()
        }
    }

    @Test
    fun verifyLibrariesListInReleaseBuilds() {
        runWithCondition(!BuildConfig.DEBUG) {
            homeScreen {
            }.openThreeDotMenu {
            }.openSettings {
            }.openAboutFirefoxPreview {
                verifyLibrariesUsedLink()
                verifyTheLibrariesListNotEmpty()
            }
        }
    }
}

private fun dismissGooglePlayToS() {
    if (mDevice.findObject(UiSelector().textContains("Terms of Service")).exists()) {
        mDevice.findObject(UiSelector().textContains("ACCEPT")).click()
    }
}
