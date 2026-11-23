/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeUp
import androidx.test.espresso.Espresso.pressBack
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName

class MicrosurveysRobot {
    fun verifySurveyButton(composeTestRule: ComposeTestRule) {
        composeTestRule
            .onNodeWithText(getStringResource(R.string.preferences_take_survey), useUnmergedTree = true)
            .assertIsDisplayed()
    }

    fun verifySurveyNoThanksButton(composeTestRule: ComposeTestRule) {
        composeTestRule
            .onNodeWithText(getStringResource(R.string.preferences_not_take_survey), useUnmergedTree = true)
            .assertIsDisplayed()
    }

    fun verifyHomeScreenSurveyCloseButton(exists: Boolean) =
        assertUIObjectExists(itemWithDescription("Close"), exists = exists)

    fun verifyContinueSurveyButton(composeTestRule: ComposeTestRule, exists: Boolean) {
        if (exists) {
            Log.i(TAG, "verifyContinueSurveyButton: Waiting for $waitingTime until the \"Continue\" button exists")
            composeTestRule.waitUntilAtLeastOneExists(
                hasText(getStringResource(R.string.micro_survey_continue_button_label)),
                waitingTime,
            )
            Log.i(TAG, "verifyContinueSurveyButton: Waited for $waitingTime until the \"Continue\" button exists")
            Log.i(TAG, "verifyContinueSurveyButton: Trying to verify that the \"Continue\" button is displayed")
            composeTestRule.continueSurveyButton().assertIsDisplayed()
            Log.i(TAG, "verifyContinueSurveyButton: Verified that the \"Continue\" button is displayed")
        } else {
            Log.i(
                TAG,
                "verifyContinueSurveyButton: Waiting for $waitingTime until the \"Continue\" button does not exist",
            )
            composeTestRule.waitUntilDoesNotExist(
                hasText(getStringResource(R.string.micro_survey_continue_button_label)),
                waitingTime,
            )
            Log.i(
                TAG,
                "verifyContinueSurveyButton: Waited for $waitingTime until the \"Continue\" button does not exist",
            )
            Log.i(TAG, "verifyContinueSurveyButton: Trying to verify that the \"Continue\" button is not displayed")
            composeTestRule.continueSurveyButton().assertIsNotDisplayed()
            Log.i(TAG, "verifyContinueSurveyButton: Verifed that the \"Continue\" button is not displayed")
        }
    }

    fun clickContinueSurveyButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickContinueSurveyButton: Waiting for $waitingTime until the \"Continue\" button exists")
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.micro_survey_continue_button_label)),
            waitingTime,
        )
        Log.i(TAG, "clickContinueSurveyButton: Waited for $waitingTime until the \"Continue\" button exists")
        Log.i(TAG, "clickContinueSurveyButton: Trying to click the \"Continue\" button")
        composeTestRule.continueSurveyButton().performClick()
        Log.i(TAG, "clickContinueSurveyButton: Clicked the \"Continue\" button")
    }

    fun verifyTheFirefoxLogo(composeTestRule: ComposeTestRule, exists: Boolean) {
        if (exists) {
            Log.i(TAG, "verifyTheFirefoxLogo: Waiting for $waitingTime until the survey logo exists")
            composeTestRule.waitUntilAtLeastOneExists(hasContentDescription("Firefox logo"), waitingTime)
            Log.i(TAG, "verifyTheFirefoxLogo: Waited for $waitingTime until the survey logo exists")
            Log.i(TAG, "verifyTheFirefoxLogo: Trying to verify that the the survey logo is displayed")
            composeTestRule.onNodeWithContentDescription("Firefox logo").assertIsDisplayed()
            Log.i(TAG, "verifyTheFirefoxLogo: Verified that the the survey logo is displayed")
        } else {
            Log.i(TAG, "verifyTheFirefoxLogo: Waiting for $waitingTime until the survey logo does not exist")
            composeTestRule.waitUntilDoesNotExist(hasContentDescription("Firefox logo"), waitingTime)
            Log.i(TAG, "verifyTheFirefoxLogo: Waited for $waitingTime until the survey logo does not exist")
            Log.i(TAG, "verifyTheFirefoxLogo: Trying to verify that the the survey logo is not displayed")
            composeTestRule.onNodeWithContentDescription("Firefox logo").assertIsNotDisplayed()
            Log.i(TAG, "verifyTheFirefoxLogo: Verified that the the survey logo is not displayed")
        }
    }

    fun verifyTheSurveyTitle(title: String, composeTestRule: ComposeTestRule, exists: Boolean) {
        if (exists) {
            Log.i(TAG, "verifyTheSurveyTitle: Waiting for $waitingTime until the survey title: $title exists")
            composeTestRule.waitUntilAtLeastOneExists(hasText(title), waitingTime)
            Log.i(TAG, "verifyTheSurveyTitle: Waited for $waitingTime until the survey title: $title exists")
            Log.i(TAG, "verifyTheSurveyTitle: Trying to verify that survey title: $title is displayed")
            composeTestRule.onNodeWithText(title).assertIsDisplayed()
            Log.i(TAG, "verifyTheSurveyTitle: Verified that survey title: $title is displayed")
        } else {
            Log.i(TAG, "verifyTheSurveyTitle: Waiting for $waitingTime until the survey title: $title does not exist")
            composeTestRule.waitUntilDoesNotExist(hasText(title), waitingTime)
            Log.i(TAG, "verifyTheSurveyTitle: Waited for $waitingTime until the survey title: $title does not exist")
            Log.i(TAG, "verifyTheSurveyTitle: Trying to verify that survey title: $title is not displayed")
            composeTestRule.onNodeWithText(title).assertIsNotDisplayed()
            Log.i(TAG, "verifyTheSurveyTitle: Verified that survey title: $title is not displayed")
        }
    }

    fun verifyPleaseCompleteTheSurveyHeader(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyPleaseCompleteTheSurveyHeader: Waiting for the compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "verifyPleaseCompleteTheSurveyHeader: Waited for the compose rule to be idle")
        Log.i(TAG, "verifyPleaseCompleteTheSurveyHeader: Trying to verify that the survey header is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.micro_survey_survey_header_2)).assertIsDisplayed()
        Log.i(TAG, "verifyPleaseCompleteTheSurveyHeader: Verified that the survey header is displayed")
    }

    fun expandSurveySheet(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "expandSurveySheet: Trying to swipe up the survey header")
        composeTestRule
            .onNode(hasAnyChild(hasText(getStringResource(R.string.micro_survey_survey_header_2))))
            .performTouchInput { swipeUp() }
        Log.i(TAG, "expandSurveySheet: Swiped up the survey header")
        Log.i(TAG, "expandSurveySheet: Waiting for the compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "expandSurveySheet: Waited for the compose rule to be idle")
    }

    fun selectAnswer(answer: String, composeTestRule: ComposeTestRule) {
        Log.i(TAG, "selectAnswer: Waiting for $waitingTime until for answer: $answer to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText(answer), waitingTime)
        Log.i(TAG, "selectAnswer: Waited for $waitingTime until for answer: $answer to exist")
        Log.i(TAG, "selectAnswer: Trying to click answer: $answer")
        composeTestRule.onNodeWithText(answer).performClick()
        Log.i(TAG, "selectAnswer: Clicked answer: $answer")
    }

    fun clickSubmitButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickSubmitButton: Waiting for the compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickSubmitButton: Waited for the compose rule to be idle")
        Log.i(TAG, "clickSubmitButton: Trying to click the \"Submit\" button")
        composeTestRule.submitButton().assertIsEnabled().performClick()
        Log.i(TAG, "clickSubmitButton: Clicked the \"Submit\" button")
    }

    fun verifySurveyCompletedScreen(composeTestRule: ComposeTestRule, exists: Boolean = true) {
        verifyPleaseCompleteTheSurveyHeader(composeTestRule)
        verifyTheFirefoxLogo(composeTestRule, exists)
        assertUIObjectExists(homescreenSurveyCloseButton())
        Log.i(
            TAG,
            "verifySurveyCompletedScreen: Trying to verify that the \"Thanks for your feedback!\" message is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.micro_survey_feedback_confirmation))
            .assertIsDisplayed()
        Log.i(TAG, "verifySurveyCompletedScreen: Verified that the \"Thanks for your feedback!\" message is displayed")
        Log.i(TAG, "verifySurveyCompletedScreen: Trying to verify that the privacy notice is displayed")
        composeTestRule.privacyNoticeLink().assertIsDisplayed()
        Log.i(TAG, "verifySurveyCompletedScreen: Verified that the privacy notice is displayed")
    }

    fun clickOutsideTheSurveyPrompt() {
        Log.i(TAG, "clickOutsideTheSurveyPrompt: Trying to click outside the survey prompt")
        itemWithResId("$packageName:id/engineView").clickTopLeft()
        Log.i(TAG, "clickOutsideTheSurveyPrompt: Clicked outside the survey prompt")
    }

    fun verifyThePrintSurveyPrompt(composeTestRule: ComposeTestRule, exists: Boolean) {
        verifyTheFirefoxLogo(composeTestRule, exists)
        verifyTheSurveyTitle(getStringResource(R.string.microsurvey_prompt_printing_title), composeTestRule, exists)
        verifyContinueSurveyButton(composeTestRule, exists)
        verifyHomeScreenSurveyCloseButton(exists)
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun clickSurveyButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickSurveyButton: Trying to click the survey button")
            composeTestRule
                .onNodeWithText(getStringResource(R.string.preferences_take_survey), useUnmergedTree = true)
                .performClick()
            Log.i(TAG, "clickSurveyButton: Clicked the survey button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickNoThanksSurveyButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickNoThanksSurveyButton: Waiting for $waitingTime for the \"No thanks\" button to exist")
            surveyNoThanksButton().waitForExists(waitingTime)
            Log.i(TAG, "clickNoThanksSurveyButton: Waited for $waitingTime for the \"No thanks\" button to exist")
            Log.i(TAG, "clickNoThanksSurveyButton: Trying to click the \"No thanks\" button")
            surveyNoThanksButton().click()
            Log.i(TAG, "clickNoThanksSurveyButton: Clicked the \"No thanks\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickHomeScreenSurveyCloseButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(
                TAG,
                "clickHomeScreenSurveyCloseButton: Waiting for $waitingTime for the close survey button to exist",
            )
            homescreenSurveyCloseButton().waitForExists(waitingTime)
            Log.i(TAG, "clickHomeScreenSurveyCloseButton: Waited for $waitingTime for the close survey button to exist")
            Log.i(TAG, "clickHomeScreenSurveyCloseButton: Trying to click the close survey button")
            homescreenSurveyCloseButton().click()
            Log.i(TAG, "clickHomeScreenSurveyCloseButton: Clicked the close survey button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun collapseSurveyByTappingBackButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "collapseSurveyByTappingBackButton: Trying to perform press back action")
            pressBack()
            Log.i(TAG, "collapseSurveyByTappingBackButton: Performed press back action")
            Log.i(TAG, "collapseSurveyByTappingBackButton: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "collapseSurveyByTappingBackButton: Waited for device to be idle")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }
    }
}

fun surveyScreen(
    composeTestRule: ComposeTestRule,
    interact: MicrosurveysRobot.() -> Unit,
): MicrosurveysRobot.Transition {
    MicrosurveysRobot().interact()
    return MicrosurveysRobot.Transition(composeTestRule)
}

private fun surveyNoThanksButton() = itemContainingText(getStringResource(R.string.preferences_not_take_survey))

private fun homescreenSurveyCloseButton() = itemWithDescription("Close")

private fun ComposeTestRule.continueSurveyButton() =
    onNodeWithText(getStringResource(R.string.micro_survey_continue_button_label))

private fun ComposeTestRule.submitButton() =
    onNodeWithText(getStringResource(R.string.micro_survey_submit_button_label))

private fun ComposeTestRule.privacyNoticeLink() = onNodeWithContentDescription("Privacy notice Links available")
