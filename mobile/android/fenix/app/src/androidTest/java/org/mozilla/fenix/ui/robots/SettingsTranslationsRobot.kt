/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.hasAnySibling
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime

class SettingsTranslationsRobot(private val composeTestRule: ComposeTestRule) {

    fun clickDownloadLanguagesButton() {
        Log.i(TAG, "clickDownloadLanguagesButton: Trying to click the \"Download languages\" button")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_settings_download_language))
            .performClick()
        Log.i(TAG, "clickDownloadLanguagesButton: Clicked the \"Download languages\" button")
    }

    fun clickLanguageToDownload(languageToDownload: String) {
        Log.i(TAG, "clickLanguageToDownload: Trying to click the $languageToDownload language download button")
        composeTestRule.onNodeWithContentDescription("$languageToDownload", substring = true).performClick()
        Log.i(TAG, "clickLanguageToDownload: Clicked the $languageToDownload language download button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyDownloadedLanguage(downloadedLanguage: String) {
        Log.i(TAG, "verifyDownloadedLanguage: Waiting until $downloadedLanguage is downloaded")
        composeTestRule.waitUntil(timeoutMillis = 30_000) {
            composeTestRule
                .onAllNodes(hasContentDescription("$downloadedLanguage", substring = true))
                .fetchSemanticsNodes()
                .isNotEmpty()
        }
        composeTestRule.onNodeWithContentDescription("$downloadedLanguage", substring = true).assertIsDisplayed()
        Log.i(TAG, "verifyDownloadedLanguage: Verified that $downloadedLanguage is downloaded")
    }

    fun verifyDownloadLanguageInSavingModePrompt() {
        assertUIObjectExists(
            itemContainingText("Download while in data saving mode"),
            itemContainingText("Always download in data saving mode"),
            itemContainingText("Download"),
            itemContainingText("Cancel"),
        )
    }

    fun clickCancelDownloadLanguageInSavingModePromptButton() {
        Log.i(TAG, "clickCancelDownloadLanguageInSavingModePromptButton: Trying to click the \"Cancel\" dialog button")
        composeTestRule.onNodeWithText("Cancel").performClick()
        Log.i(TAG, "clickCancelDownloadLanguageInSavingModePromptButton: Clicked the \"Cancel\" dialog button")
    }

    fun clickDownloadLanguageInSavingModePromptButton() {
        Log.i(TAG, "clickDownloadLanguageInSavingModePromptButton: Trying to click the \"Download\" dialog button")
        composeTestRule.onNodeWithText("Download").performClick()
        Log.i(TAG, "clickDownloadLanguageInSavingModePromptButton: Clicked the \"Download\" dialog button")
    }

    fun clickAutomaticTranslationButton() {
        Log.i(TAG, "clickAutomaticTranslationButton: Trying to click the \"Automatic translation\" button")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_settings_automatic_translation))
            .performClick()
        Log.i(TAG, "clickAutomaticTranslationButton: Clicked the \"Automatic translation\" button")
    }

    fun clickNeverTranslateTheseSitesButton() {
        Log.i(TAG, "clickNeverTranslateTheseSitesButton: Trying to click the \"Never translate these sites\" button")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.translation_settings_automatic_never_translate_sites))
            .performClick()
        Log.i(TAG, "clickNeverTranslateTheseSitesButton: Clicked the \"Never translate these sites\" button")
    }

    fun verifyNeverTranslateThisSiteRemoveButton(url: String) {
        Log.i(
            TAG,
            "verifyNeverTranslateThisSiteRemoveButton: Trying to verify that the \"Remove $url\" button is displayed",
        )
        composeTestRule.onNodeWithContentDescription("Remove $url").assertIsDisplayed()
        Log.i(TAG, "verifyNeverTranslateThisSiteRemoveButton: Verified that the \"Remove $url\" button is displayed")
    }

    fun clickNeverTranslateThisSiteRemoveButton(url: String) {
        Log.i(TAG, "clickNeverTranslateThisSiteRemoveButton: Trying to click the \"Remove $url\" button is displayed")
        composeTestRule.onNodeWithContentDescription("Remove $url").performClick()
        Log.i(TAG, "clickNeverTranslateThisSiteRemoveButton: Clicked the \"Remove $url\" button is displayed")
    }

    fun verifyDeleteNeverTranslateThisSiteDialog(url: String) {
        Log.i(
            TAG,
            "verifyDeleteNeverTranslateThisSiteDialog: Trying to verify that the \"Delete $url?\" dialog is displayed",
        )
        assertUIObjectExists(itemContainingText("Delete $url?"))
        Log.i(TAG, "verifyDeleteNeverTranslateThisSiteDialog: Verified that the \"Delete $url?\" dialog is displayed")
    }

    fun clickCancelDeleteNeverTranslateThisSiteDialog() {
        Log.i(TAG, "clickCancelDeleteNeverTranslateThisSiteDialog: Trying to click the \"Cancel\" dialog button")
        composeTestRule.onNodeWithText(getStringResource(R.string.dialog_delete_negative)).performClick()
        Log.i(TAG, "clickCancelDeleteNeverTranslateThisSiteDialog: Clicked the \"Cancel\" dialog button")
    }

    fun clickConfirmDeleteNeverTranslateThisSiteDialog() {
        Log.i(TAG, "clickConfirmDeleteNeverTranslateThisSiteDialog: Trying to click the \"Delete\" dialog button")
        composeTestRule.onNodeWithText(getStringResource(R.string.dialog_delete_positive)).performClick()
        Log.i(TAG, "clickConfirmDeleteNeverTranslateThisSiteDialog: Clicked the \"Delete\" dialog button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyAlwaysAutomaticallyTranslateForLanguage(languageToTranslate: String) {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyAlwaysAutomaticallyTranslateForLanguage: Started try #$i")
            try {
                Log.i(
                    TAG,
                    "verifyAlwaysAutomaticallyTranslateForLanguage: Waiting for $waitingTime ms until $languageToTranslate language exists",
                )
                composeTestRule.waitUntilExactlyOneExists(hasText(languageToTranslate), waitingTime)
                Log.i(
                    TAG,
                    "verifyAlwaysAutomaticallyTranslateForLanguage: Waited for $waitingTime ms until $languageToTranslate language exists",
                )

                break
            } catch (e: ComposeTimeoutException) {
                Log.i(
                    TAG,
                    "verifyAlwaysAutomaticallyTranslateForLanguage: ComposeTimeoutException caught, executing fallback methods",
                )
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(
                        TAG,
                        "verifyAlwaysAutomaticallyTranslateForLanguage: Trying to perform a one step scroll to end",
                    )
                    UiScrollable(UiSelector().scrollable(true)).scrollToEnd(1)
                    Log.i(TAG, "verifyAlwaysAutomaticallyTranslateForLanguage: Performed a one step scroll to end")
                }
            }
        }

        Log.i(
            TAG,
            "verifyNeverAutomaticallyTranslateForLanguage: Trying to verify that $languageToTranslate language is set to \"Never translate\"",
        )
        composeTestRule
            .onNodeWithText(languageToTranslate, useUnmergedTree = true)
            .assert(
                hasAnySibling(
                    hasText(getStringResource(R.string.automatic_translation_option_always_translate_title_preference))
                )
            )
        Log.i(
            TAG,
            "verifyNeverAutomaticallyTranslateForLanguage: Verified that $languageToTranslate language is set to \"Never translate\"",
        )
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyNeverAutomaticallyTranslateForLanguage(languageToTranslate: String) {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyNeverAutomaticallyTranslateForLanguage: Started try #$i")
            try {
                Log.i(
                    TAG,
                    "verifyNeverAutomaticallyTranslateForLanguage: Waiting for $waitingTime ms until $languageToTranslate language exists",
                )
                composeTestRule.waitUntilExactlyOneExists(hasText(languageToTranslate), waitingTime)
                Log.i(
                    TAG,
                    "verifyNeverAutomaticallyTranslateForLanguage: Waited for $waitingTime ms until $languageToTranslate language exists",
                )

                break
            } catch (e: ComposeTimeoutException) {
                Log.i(
                    TAG,
                    "verifyNeverAutomaticallyTranslateForLanguage: ComposeTimeoutException caught, executing fallback methods",
                )
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(
                        TAG,
                        "verifyNeverAutomaticallyTranslateForLanguage: Trying to perform a one step scroll to end",
                    )
                    UiScrollable(UiSelector().scrollable(true)).scrollToEnd(1)
                    Log.i(TAG, "verifyNeverAutomaticallyTranslateForLanguage: Performed a one step scroll to end")
                }
            }
        }
        Log.i(
            TAG,
            "verifyNeverAutomaticallyTranslateForLanguage: Trying to verify that $languageToTranslate language is set to \"Never translate\"",
        )
        composeTestRule
            .onNodeWithText(languageToTranslate, useUnmergedTree = true)
            .assert(
                hasAnySibling(
                    hasText(getStringResource(R.string.automatic_translation_option_never_translate_title_preference))
                )
            )
        Log.i(
            TAG,
            "verifyNeverAutomaticallyTranslateForLanguage: Verified that $languageToTranslate language is set to \"Never translate\"",
        )
    }

    fun clickLanguageFromAutomaticTranslationMenu(languageToTranslate: String) {
        Log.i(
            TAG,
            "clickLanguageFromAutomaticTranslationMenu: Trying to click $languageToTranslate language from the \"Automatic translation\" menu",
        )
        composeTestRule.onNodeWithText(languageToTranslate).performClick()
        Log.i(
            TAG,
            "clickLanguageFromAutomaticTranslationMenu: Clicked $languageToTranslate language from the \"Automatic translation\" menu",
        )
    }

    fun verifyAlwaysTranslateOptionState(isChecked: Boolean) =
        assertAutomaticTranslationOptionState(
            R.string.automatic_translation_option_always_translate_title_preference,
            isChecked,
        )

    fun verifyNeverTranslateOptionState(isChecked: Boolean) =
        assertAutomaticTranslationOptionState(
            R.string.automatic_translation_option_never_translate_title_preference,
            isChecked,
        )

    /**
     * Retrieve RadioButtonListItem by its label, rather than by its position, and assert it has the expected selection
     * state.
     */
    private fun assertAutomaticTranslationOptionState(titleId: Int, isSelected: Boolean) {
        val label = getStringResource(titleId)
        Log.i(TAG, "assertAutomaticTranslationOptionState: Asserting $label is selected: $isSelected")
        val option = composeTestRule.onNodeWithText(label)
        if (isSelected) option.assertIsSelected() else option.assertIsNotSelected()
        Log.i(TAG, "assertAutomaticTranslationOptionState: Asserted $label is selected: $isSelected")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun goBackToAutomaticTranslationSubMenu(interact: SettingsTranslationsRobot.() -> Unit): Transition {
            Log.i(TAG, "goBackToAutomaticTranslationSubMenu: Trying to click the navigate up button")
            itemWithDescription(getStringResource(R.string.action_bar_up_description)).click()
            Log.i(TAG, "goBackToAutomaticTranslationSubMenu: Clicked the navigate up button")

            SettingsTranslationsRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun goBackToTranslationSettingsSubMenu(interact: SettingsTranslationsRobot.() -> Unit): Transition {
            Log.i(TAG, "goBackToTranslationSettingsSubMenu: Trying to click the navigate up button")
            itemWithDescription(getStringResource(R.string.action_bar_up_description)).click()
            Log.i(TAG, "goBackToTranslationSettingsSubMenu: Clicked the navigate up button")

            SettingsTranslationsRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun goBackToTranslationOptionSheet(interact: TranslationsRobot.() -> Unit): TranslationsRobot.Transition {
            Log.i(TAG, "goBackToTranslationOptionSheet: Trying to click the navigate up button")
            itemWithDescription(getStringResource(R.string.action_bar_up_description)).click()
            Log.i(TAG, "goBackToTranslationOptionSheet: Clicked the navigate up button")

            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }
    }
}
