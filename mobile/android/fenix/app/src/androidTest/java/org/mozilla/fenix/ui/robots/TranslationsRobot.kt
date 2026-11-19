/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.uiautomator.By
import androidx.test.uiautomator.Direction
import androidx.test.uiautomator.UiSelector
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertItemIsChecked
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectIsGone
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone

class TranslationsRobot(private val composeTestRule: ComposeTestRule) {

    @OptIn(ExperimentalTestApi::class)
    fun verifyTranslationSheetIsDisplayed(isDisplayed: Boolean) {
        Log.i(
            TAG,
            "verifyTranslationSheetIsDisplayed: Trying to verify the Translations sheet is displayed $isDisplayed.",
        )
        if (isDisplayed) {
            for (i in 1..RETRY_COUNT) {
                Log.i(TAG, "verifyTranslationSheetIsDisplayed: Started try #$i")
                try {
                    composeTestRule.waitUntilAtLeastOneExists(hasText("Translate to"), waitingTimeLong)

                    break
                } catch (e: ComposeTimeoutException) {
                    Log.i(
                        TAG,
                        "verifyTranslationSheetIsDisplayed: ComposeTimeoutException caught, executing fallback methods",
                    )
                    if (i == RETRY_COUNT) {
                        throw e
                    } else {
                        browserScreen(composeTestRule) {}.openThreeDotMenu {}.clickRefreshButton {}
                    }
                }
            }
        } else {
            composeTestRule.onNodeWithText("Translate to").assertIsNotDisplayed()
        }
        Log.i(TAG, "verifyTranslationSheetIsDisplayed: Verified the Translations sheet is displayed $isDisplayed.")
    }

    fun closeTranslationsSheet() {
        Log.i(TAG, "closeTranslationsSheet: Trying to close the Translations sheet.")
        itemWithResId("$packageName:id/touch_outside").click()
        Log.i(TAG, "closeTranslationsSheet: Closed the Translations sheet.")
        waitUntilSnackbarGone()
    }

    fun clickTranslationsOptionsButton() {
        Log.i(TAG, "clickTranslationsOptionsButton: Trying to click the translations option button.")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.translation_option_bottom_sheet_title_heading2))
            .performClick()
        Log.i(TAG, "clickTranslationsOptionsButton: Clicked the translations options button.")
    }

    fun clickAlwaysOfferToTranslateOption() {
        Log.i(
            TAG,
            "clickAlwaysOfferToTranslateOption: Trying to click the \"Always offer to translate\" option button.",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_option_bottom_sheet_always_translate))
            .performClick()
        Log.i(TAG, "clickAlwaysOfferToTranslateOption: Clicked the \"Always offer to translate\" options button.")
        composeTestRule.waitForIdle()
    }

    fun clickAlwaysTranslateLanguageOption(languageToTranslate: String) {
        Log.i(
            TAG,
            "clickAlwaysTranslateLanguageOption: Trying to click the \"Always translate $languageToTranslate\" option button.",
        )
        composeTestRule.onNodeWithText("Always translate $languageToTranslate").performClick()
        Log.i(
            TAG,
            "clickAlwaysTranslateLanguageOption: Clicked the \"Always translate $languageToTranslate\" options button.",
        )
        composeTestRule.waitForIdle()
    }

    fun clickNeverTranslateLanguageOption(languageToTranslate: String) {
        Log.i(
            TAG,
            "clickNeverTranslateLanguageOption: Trying to click the \"Never translate $languageToTranslate\" option button.",
        )
        composeTestRule.onNodeWithText("Never translate $languageToTranslate").performClick()
        Log.i(
            TAG,
            "clickNeverTranslateLanguageOption: Clicked the \"Never translate $languageToTranslate\" options button.",
        )
    }

    fun clickNeverTranslateThisSiteOption() {
        Log.i(
            TAG,
            "clickNeverTranslateThisSiteOption: Trying to click the \"Never translate this site\" option button.",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_option_bottom_sheet_never_translate_site))
            .performClick()
        Log.i(TAG, "clickNeverTranslateThisSiteOption: Clicked the \"Never translate this site\" options button.")
    }

    fun verifyTheAlwaysTranslateLanguageDescription() {
        Log.i(
            TAG,
            "verifyTheAlwaysTranslateLanguageDescription: Trying to verify the \"Overrides offers to translate\" description is displayed.",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_option_bottom_sheet_switch_description))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheAlwaysTranslateLanguageDescription: Verified the \"Overrides offers to translate\" description is displayed.",
        )
    }

    fun verifyTheNeverTranslateLanguageDescription() {
        Log.i(
            TAG,
            "verifyTheNeverTranslateLanguageDescription: Trying to verify the \"Overrides offers to translate\" description is displayed.",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_option_bottom_sheet_switch_description))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheNeverTranslateLanguageDescription: Verified the \"Overrides offers to translate\" description is displayed.",
        )
    }

    fun verifyAlwaysOfferToTranslateOptionIsChecked(isChecked: Boolean) {
        Log.i(TAG, "verifyAlwaysOfferToTranslateOptionIsChecked: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "verifyAlwaysOfferToTranslateOptionIsChecked: Waited for compose test rule to be idle")
        Log.i(
            TAG,
            "verifyAlwaysOfferToTranslateOptionIsChecked: Trying to verify the \"Always offer to translate\" option is checked.",
        )
        assertItemIsChecked(
            mDevice.findObject(UiSelector().index(2).className("android.view.View")),
            isChecked = isChecked,
        )
        Log.i(
            TAG,
            "verifyAlwaysOfferToTranslateOptionIsChecked: Verified the \"Always offer to translate\" option is checked.",
        )
    }

    fun verifyAlwaysTranslateOptionIsChecked(isChecked: Boolean) {
        Log.i(TAG, "verifyAlwaysTranslateOptionIsChecked: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "verifyAlwaysTranslateOptionIsChecked: Waited for compose test rule to be idle")
        Log.i(
            TAG,
            "verifyAlwaysTranslateOptionIsChecked: Trying to verify the \"Always translate\" description is checked.",
        )
        assertItemIsChecked(
            mDevice.findObject(UiSelector().index(4).className("android.view.View")),
            isChecked = isChecked,
        )
        Log.i(TAG, "verifyAlwaysTranslateOptionIsChecked: Verified the \"Always translate\" description is checked.")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTheNeverTranslateThisSiteOptionIsChecked(isChecked: Boolean) {
        Log.i(
            TAG,
            "verifyTheNeverTranslateThisSiteOptionIsChecked: Waiting for $waitingTime ms until the \"Never translate this site\" option exists",
        )
        composeTestRule.waitUntilExactlyOneExists(
            hasText(getStringResource(R.string.translation_option_bottom_sheet_never_translate_site)),
            waitingTime,
        )
        Log.i(
            TAG,
            "verifyTheNeverTranslateThisSiteOptionIsChecked: Waited for $waitingTime ms until the \"Never translate this site\" option exists",
        )
        Log.i(
            TAG,
            "verifyTheNeverTranslateThisSiteOptionIsChecked: Trying to verify the \"Never translate this site\" description is checked.",
        )
        assertItemIsChecked(
            mDevice.findObject(UiSelector().index(5).className("android.view.View")),
            isChecked = isChecked,
        )
        Log.i(
            TAG,
            "verifyTheNeverTranslateThisSiteOptionIsChecked: Verified the \"Never translate this site\" description is checked.",
        )
    }

    fun verifyTheNeverTranslateLanguageOptionIsChecked(isChecked: Boolean) {
        Log.i(TAG, "verifyTheNeverTranslateLanguageOptionIsChecked: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "verifyTheNeverTranslateLanguageOptionIsChecked: Waited for compose test rule to be idle")
        Log.i(
            TAG,
            "verifyTheNeverTranslateLanguageOptionIsChecked: Trying to verify the \"Never translate\" description is checked.",
        )
        assertItemIsChecked(
            mDevice.findObject(UiSelector().index(4).className("android.view.View")),
            isChecked = isChecked,
        )
        Log.i(
            TAG,
            "verifyTheNeverTranslateLanguageOptionIsChecked: Verified the \"Never translate this site\" description is checked.",
        )
    }

    fun clickTranslateToDropdown() {
        Log.i(TAG, "clickTranslateToDropdown: Trying to click the \"Translate to\" dropdown.")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translations_bottom_sheet_translate_to))
            .performClick()
        Log.i(TAG, "clickTranslateToDropdown: Clicked the \"Translate to\" dropdown.")
    }

    fun clickTranslateToLanguage(translateToLanguage: String) {
        Log.i(
            TAG,
            "clickTranslateToLanguage: Trying to click the $translateToLanguage \"Translate to\" dropdown option.",
        )
        composeTestRule.onNodeWithText(translateToLanguage).performClick()
        Log.i(TAG, "clickTranslateToLanguage: Clicked the $translateToLanguage \"Translate to\" dropdown option.")
    }

    fun clickGoBackTranslationSheetButton() {
        Log.i(TAG, "clickGoBackTranslationSheetButton: Trying to click the \"Navigate back\" translation sheet button")
        composeTestRule.onNodeWithContentDescription("Navigate back").performClick()
        Log.i(TAG, "clickGoBackTranslationSheetButton: Clicked the \"Navigate back\" translation sheet button")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        @OptIn(ExperimentalTestApi::class)
        fun clickTranslateButton(
            pageWasNotPreviouslyTranslated: Boolean = true,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(TAG, "clickTranslateButton: Trying to click the Translate button from the Translations sheet.")
            composeTestRule.onNodeWithText("Translate").performClick()
            Log.i(TAG, "clickTranslateButton: Clicked the Translate button.")
            if (pageWasNotPreviouslyTranslated) {
                Log.i(
                    TAG,
                    "clickTranslateButton: Waiting for $waitingTime until the \"Translating in progress\" exists.",
                )
                composeTestRule.waitUntilAtLeastOneExists(
                    hasContentDescription(
                        getStringResource(
                            R.string.translations_bottom_sheet_translating_in_progress_content_description
                        )
                    ),
                    waitingTime,
                )
                Log.i(
                    TAG,
                    "clickTranslateButton: Waited for $waitingTime until the \"Translating in progress\" exists.",
                )
                for (i in 1..RETRY_COUNT) {
                    Log.i(TAG, "clickTranslateButton: Started try #$i")
                    try {
                        Log.i(
                            TAG,
                            "clickTranslateButton: Waiting for $waitingTimeLong until the \"Translating in progress\" to not exists.",
                        )
                        composeTestRule.waitUntilDoesNotExist(
                            hasContentDescription(
                                getStringResource(
                                    R.string.translations_bottom_sheet_translating_in_progress_content_description
                                )
                            ),
                            waitingTimeLong,
                        )
                        Log.i(
                            TAG,
                            "clickTranslateButton: Waited for $waitingTimeLong until the \"Translating in progress\" to not exists.",
                        )
                    } catch (e: ComposeTimeoutException) {
                        Log.i(TAG, "clickTranslateButton: ComposeTimeoutException caught, executing fallback methods")
                        if (i == RETRY_COUNT) {
                            throw e
                        } else {
                            Log.i(TAG, "clickTranslateButton: Translate sheet is still displayed. Trying to close it.")
                            TranslationsRobot(composeTestRule).closeTranslationsSheet()
                            Log.i(TAG, "clickTranslateButton: Closed the Translations sheet.")
                        }
                    }
                }
            } else {
                for (i in 1..RETRY_COUNT) {
                    Log.i(TAG, "clickTranslateButton: Started try #$i")
                    try {
                        Log.i(
                            TAG,
                            "clickTranslateButton: Waiting for $waitingTimeLong until the \"Translating in progress\" to not exists.",
                        )
                        composeTestRule.waitUntilDoesNotExist(
                            hasContentDescription(
                                getStringResource(R.string.translation_option_bottom_sheet_close_content_description)
                            ),
                            waitingTimeLong,
                        )
                        Log.i(
                            TAG,
                            "clickTranslateButton: Waited for $waitingTimeLong until the \"Translating in progress\" to not exists.",
                        )
                    } catch (e: ComposeTimeoutException) {
                        Log.i(TAG, "clickTranslateButton: ComposeTimeoutException caught, executing fallback methods")
                        if (i == RETRY_COUNT) {
                            throw e
                        } else {
                            Log.i(TAG, "clickTranslateButton: Translate sheet is still displayed. Trying to close it.")
                            TranslationsRobot(composeTestRule).closeTranslationsSheet()
                            Log.i(TAG, "clickTranslateButton: Closed the Translations sheet.")
                        }
                    }
                }
            }

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickShowOriginalButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickShowOriginalButton: Trying to click on the Show Original button.")
            composeTestRule.onNodeWithText("Show original").performClick()
            Log.i(TAG, "clickShowOriginalButton: Clicked on the Show Original button.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickNotNowButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickShowOriginalButton: Trying to click on the \"Not now\" button.")
            composeTestRule
                .onNodeWithText(getStringResource(R.string.translations_bottom_sheet_negative_button))
                .performClick()
            Log.i(TAG, "clickShowOriginalButton: Clicked on the \"Not now\" button.")
            composeTestRule.waitForIdle()
            mDevice.waitForIdle(waitingTimeLong)

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun swipeCloseTranslationsSheet(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            var retries = 0
            while (itemWithResId("$packageName:id/design_bottom_sheet").exists() && retries++ < 3) {
                Log.i(TAG, "swipeCloseTranslationsSheet: Started try #$retries")
                try {
                    Log.i(TAG, "swipeCloseTranslationsSheet: Trying to swipe down the Translations sheet.")
                    mDevice.findObject(By.res("$packageName:id/design_bottom_sheet")).swipe(Direction.DOWN, 1.0f)
                    Log.i(TAG, "swipeCloseTranslationsSheet: Swiped down the Translations sheet.")
                    composeTestRule.waitForIdle()
                    mDevice.waitForIdle(waitingTimeLong)
                    assertUIObjectIsGone(itemWithResId("$packageName:id/design_bottom_sheet"))

                    break
                } catch (e: AssertionError) {
                    Log.i(TAG, "swipeCloseTranslationsSheet: AssertionError caught, executing fallback methods")
                    if (retries == 3) {
                        throw e
                    }
                }
            }

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickTranslationSettingsButton(
            interact: SettingsTranslationsRobot.() -> Unit
        ): SettingsTranslationsRobot.Transition {
            Log.i(TAG, "clickTranslationSettingsButton: Trying to click the \"Translation settings\" button.")
            composeTestRule
                .onNodeWithText(getStringResource(R.string.translation_option_bottom_sheet_translation_settings))
                .performClick()
            Log.i(TAG, "clickTranslationSettingsButton: Clicked the \"Translation settings\" button.")

            SettingsTranslationsRobot(composeTestRule).interact()
            return SettingsTranslationsRobot.Transition(composeTestRule)
        }
    }
}

fun translationsRobot(
    composeTestRule: ComposeTestRule,
    interact: TranslationsRobot.() -> Unit,
): TranslationsRobot.Transition {
    TranslationsRobot(composeTestRule).interact()
    return TranslationsRobot.Transition(composeTestRule)
}
