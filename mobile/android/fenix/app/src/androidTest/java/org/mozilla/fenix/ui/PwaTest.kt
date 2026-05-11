/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.core.net.toUri
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.clickSystemHomeScreenShortcutAddButton
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.customTabScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.pwaScreen
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

class PwaTest {
    /* Updated externalLinks.html to v2.0,
       changed the hypertext reference to mozilla-mobile.github.io/testapp/downloads for "External link"
     */
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val externalLinksPWAPage = "https://mozilla-mobile.github.io/testapp/v2.0/externalLinks.html"
    private val emailLink = "mailto://example@example.com"
    private val phoneLink = "tel://1234567890"
    private val shortcutTitle = "TEST_APP"

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule(order = 2)
    val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/845695
    @Test
    fun externalLinkPWATest() {
        val externalLinkURL = "https://mozilla-mobile.github.io/testapp/downloads"

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(externalLinksPWAPage.toUri()) {
            verifyPageContent("Misc Link Types")
        }.openThreeDotMenu {
            clickTheMoreButton()
        }.clickAddAppToHomeScreenButton {
            clickSystemHomeScreenShortcutAddButton()
        }.openHomeScreenShortcut(shortcutTitle) {
            clickPageObject(composeTestRule, itemContainingText("External link"))
        }

        customTabScreen(composeTestRule) {
            verifyCustomTabToolbarTitle(externalLinkURL)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/845694
    @Test
    fun appLikeExperiencePWATest() {
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(externalLinksPWAPage.toUri()) {
            verifyPageContent("Misc Link Types")
        }.openThreeDotMenu {
            clickTheMoreButton()
        }.clickAddAppToHomeScreenButton {
            clickSystemHomeScreenShortcutAddButton()
        }.openHomeScreenShortcut(shortcutTitle) {
        }

        pwaScreen {
            verifyCustomTabToolbarIsNotDisplayed()
            verifyPwaActivityInCurrentTask()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/834200
    @SmokeTest
    @Test
    fun installPWAFromTheMainMenuTest() {
        val pwaPage = "https://mozilla-mobile.github.io/testapp/loginForm"

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(pwaPage.toUri()) {
            waitForPageToLoad()
            verifyUrl("mozilla-mobile.github.io/testapp/loginForm")
            verifyPageContent("Login Form")
        }.openThreeDotMenu {
            clickTheMoreButton()
        }.clickAddAppToHomeScreenButton {
            clickSystemHomeScreenShortcutAddButton()
        }.openHomeScreenShortcut("TEST_APP") {
            mDevice.waitForIdle()
            verifyNavURLBarHidden()
        }
    }
}
