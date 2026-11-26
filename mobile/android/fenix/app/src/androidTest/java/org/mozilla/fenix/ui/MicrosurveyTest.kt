/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Assume
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.Config
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.surveyScreen

class MicrosurveyTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule(
                skipOnboarding = true,
                isMicrosurveyEnabled = true,
            )
        ) {
            it.activity
        }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809354
    @SmokeTest
    @Test
    fun activationOfThePrintMicrosurveyTest() {
        // The print microsurvey is a dev/nightly-only example message; it never renders on beta/release, so skip there.
        Assume.assumeTrue(Config.channel.isNightlyOrDebug)

        val testPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {}
            .openThreeDotMenu {}
            .clickShareButton {}
            .clickPrintButton(composeTestRule) {
                mDevice.waitForIdle()
                mDevice.pressBack()
            }
        surveyScreen(composeTestRule) {
            verifyThePrintSurveyPrompt(composeTestRule = composeTestRule, exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809349
    @SmokeTest
    @Test
    fun verifyTheSurveyRemainsActivatedWhileChangingTabsTest() {
        // The print microsurvey is a dev/nightly-only example message; it never renders on beta/release, so skip there.
        Assume.assumeTrue(Config.channel.isNightlyOrDebug)

        val testPage1 = mockWebServer.getGenericAsset(1)
        val testPage2 = mockWebServer.getGenericAsset(2)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage1.url) {}
            .openThreeDotMenu {}
            .clickShareButton {}
            .clickPrintButton(composeTestRule) {
                mDevice.waitForIdle()
                mDevice.pressBack()
            }
        surveyScreen(composeTestRule) {
                clickContinueSurveyButton(composeTestRule)
                verifyPleaseCompleteTheSurveyHeader(composeTestRule)
                selectAnswer("Very satisfied", composeTestRule)
            }
            .collapseSurveyByTappingBackButton {}
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage2.url) {
                mDevice.waitForIdle()
                surveyScreen(composeTestRule) {
                    verifyTheSurveyTitle(
                        getStringResource(R.string.microsurvey_prompt_printing_title),
                        composeTestRule,
                        true,
                    )
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809361
    @Ignore(
        "Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295"
    )
    @SmokeTest
    @Test
    fun verifyTheSurveyConfirmationSheetTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {}
            .openThreeDotMenu {}
            .clickShareButton {}
            .clickPrintButton(composeTestRule) {
                mDevice.waitForIdle()
                mDevice.pressBack()
            }
        surveyScreen(composeTestRule) {
            clickContinueSurveyButton(composeTestRule)
            expandSurveySheet(composeTestRule)
            selectAnswer("Very satisfied", composeTestRule)
            clickSubmitButton(composeTestRule)
            verifySurveyCompletedScreen(composeTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809344
    @Ignore(
        "Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295"
    )
    @Test
    fun dismissTheSurveyPromptTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {}
            .openThreeDotMenu {}
            .clickShareButton {}
            .clickPrintButton(composeTestRule) {
                mDevice.waitForIdle()
                mDevice.pressBack()
            }
        surveyScreen(composeTestRule) {
                verifyThePrintSurveyPrompt(composeTestRule = composeTestRule, exists = true)
                clickOutsideTheSurveyPrompt()
                verifyThePrintSurveyPrompt(composeTestRule = composeTestRule, exists = true)
            }
            .clickHomeScreenSurveyCloseButton {}
        surveyScreen(composeTestRule) {
            verifyThePrintSurveyPrompt(composeTestRule = composeTestRule, exists = false)
        }
    }
}
