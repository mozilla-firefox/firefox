/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.surveyScreen

class MicrosurveyTest : TestSetup() {
    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule(
                skipOnboarding = true,
                isMicrosurveyEnabled = true,
                isNavigationBarCFREnabled = false,
                isNavigationToolbarEnabled = true,
            ),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809354
    @SmokeTest
    @Test
    fun activationOfThePrintMicrosurveyTest() {
        val testPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.clickShareButtonFromRedesignedToolbar {
        }.clickPrintButton {
            mDevice.waitForIdle()
            mDevice.pressBack()
        }
        surveyScreen {
            verifyTheFirefoxLogo(composeTestRule)
            verifyTheSurveyTitle(getStringResource(R.string.microsurvey_prompt_printing_title), composeTestRule)
            verifyContinueSurveyButton(composeTestRule)
            verifyHomeScreenSurveyCloseButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809349
    @SmokeTest
    @Test
    fun verifyTheSurveyRemainsActivatedWhileChangingTabsTest() {
        val testPage1 = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val testPage2 = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage1.url) {
        }.clickShareButtonFromRedesignedToolbar {
        }.clickPrintButton {
            mDevice.waitForIdle()
            mDevice.pressBack()
        }
        surveyScreen {
            clickContinueSurveyButton(composeTestRule)
            verifyPleaseCompleteTheSurveyHeader(composeTestRule)
            selectAnswer("Very satisfied", composeTestRule)
        }.collapseSurveyByTappingBackButton {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(testPage2.url) {
            mDevice.waitForIdle()
            surveyScreen {
                verifyTheSurveyTitle(getStringResource(R.string.microsurvey_prompt_printing_title), composeTestRule)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809361
    @SmokeTest
    @Test
    fun verifyTheSurveyConfirmationSheetTest() {
        val testPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testPage.url) {
        }.clickShareButtonFromRedesignedToolbar {
        }.clickPrintButton {
            mDevice.waitForIdle()
            mDevice.pressBack()
        }
        surveyScreen {
            clickContinueSurveyButton(composeTestRule)
            expandSurveySheet(composeTestRule)
            selectAnswer("Very satisfied", composeTestRule)
            clickSubmitButton(composeTestRule)
            verifySurveyCompletedScreen(composeTestRule)
        }
    }
}
