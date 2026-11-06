/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertAny
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.click
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performTouchInput
import androidx.test.espresso.Espresso.closeSoftKeyboard
import androidx.test.espresso.intent.Intents
import androidx.test.espresso.intent.matcher.IntentMatchers
import androidx.test.uiautomator.By
import androidx.test.uiautomator.By.textContains
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.compose.browser.awesomebar.AwesomeBarTestTags
import mozilla.components.compose.browser.awesomebar.AwesomeBarTestTags.CURRENT_URL_IN_SITE_DETAILS
import mozilla.components.compose.browser.awesomebar.AwesomeBarTestTags.EDIT_CURRENT_SITE_URL_BUTTON
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_EDIT_MODE
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_EDIT_MODE_HORIZONTAL_DIVIDER
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.SEARCH_SELECTOR
import mozilla.components.feature.qr.R as qrR
import mozilla.components.support.ktx.util.URLStringUtils
import org.junit.Assert.assertTrue
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.BookmarksTestTag.BOOKMARK_PLACEHOLDER
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.isPackageInstalled
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_QUICK_SEARCH
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.SPEECH_RECOGNITION
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FeatureSettingsHelper.Companion.settings
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.ext.waitNotNull

/** Implementation of Robot Pattern for the search fragment. */
class SearchRobot(private val composeTestRule: ComposeTestRule) {

    fun verifySearchToolbar(isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(TAG, "verifyScanButton: Trying to verify that the edit mode toolbar is displayed")
            composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).assertIsDisplayed()
            Log.i(TAG, "verifyScanButton: Verified that the edit mode toolbar is displayed")
        } else {
            Log.i(TAG, "verifyScanButton: Trying to verify that the edit mode toolbar is not displayed")
            composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).assertIsNotDisplayed()
            Log.i(TAG, "verifyScanButton: Verified that the edit mode toolbar is not displayed")
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyScanButton(isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(TAG, "verifyScanButton: Waiting for $waitingTime until the scan QR button is exists")
            this@SearchRobot.composeTestRule.waitUntilExactlyOneExists(
                hasContentDescription(getStringResource(qrR.string.mozac_feature_qr_scanner)),
                waitingTime,
            )
            Log.i(TAG, "verifyScanButton: Waited for $waitingTime until the scan QR button is exists")
            Log.i(TAG, "verifyScanButton: Trying to verify that the scan QR button is displayed")
            this@SearchRobot.composeTestRule
                .onNodeWithContentDescription(getStringResource(qrR.string.mozac_feature_qr_scanner))
                .assertIsDisplayed()
            Log.i(TAG, "verifyScanButton: Verified that the scan QR button is displayed")
        } else {
            Log.i(TAG, "verifyScanButton: Waiting for $waitingTime until the scan QR button does not is exist")
            this@SearchRobot.composeTestRule.waitUntilDoesNotExist(
                hasContentDescription(getStringResource(qrR.string.mozac_feature_qr_scanner)),
                waitingTime,
            )
            Log.i(TAG, "verifyScanButton: Waited for $waitingTime until the scan QR button does not is exist")
            Log.i(TAG, "verifyScanButton: Trying to verify that the scan QR button is not displayed")
            this@SearchRobot.composeTestRule
                .onNodeWithContentDescription(getStringResource(qrR.string.mozac_feature_qr_scanner))
                .assertIsNotDisplayed()
            Log.i(TAG, "verifyScanButton: Verified that the scan QR button is not displayed")
        }
    }

    fun verifyVoiceSearchButton(isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(TAG, "verifyVoiceSearchButton: Trying to verify that the voice search button is displayed")
            this@SearchRobot.composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.voice_search_content_description))
                .assertIsDisplayed()
            Log.i(TAG, "verifyVoiceSearchButton: Verified that the voice search button is displayed")
        } else {
            Log.i(TAG, "verifyVoiceSearchButton: Trying to verify that the voice search button is not displayed")
            this@SearchRobot.composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.voice_search_content_description))
                .assertIsNotDisplayed()
            Log.i(TAG, "verifyVoiceSearchButton: Verified that the voice search button is not displayed")
        }
    }

    fun startVoiceSearch() {
        Log.i(TAG, "startVoiceSearch: Trying to click the voice search button button")
        this@SearchRobot.composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.voice_search_content_description))
            .performClick()
        Log.i(TAG, "startVoiceSearch: Clicked the voice search button button")
        grantSystemPermission()

        if (isPackageInstalled(GOOGLE_QUICK_SEARCH)) {
            Log.i(TAG, "startVoiceSearch: $GOOGLE_QUICK_SEARCH is installed")
            Log.i(TAG, "startVoiceSearch: Trying to verify the intent to: $GOOGLE_QUICK_SEARCH")
            Intents.intended(IntentMatchers.hasAction(SPEECH_RECOGNITION))
            Log.i(TAG, "startVoiceSearch: Verified the intent to: $GOOGLE_QUICK_SEARCH")
        }
    }

    fun closeVoiceSearchDialog() {
        Log.i(TAG, "closeVoiceSearchDialog: Trying to click device back button")
        mDevice.pressBack()
        Log.i(TAG, "closeVoiceSearchDialog: Clicked device back button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTheSuggestionsHeader(headerText: String) {
        Log.i(TAG, "verifyTheFirefoxSuggestHeader: Trying to verify the Firefox Suggest header is displayed.")
        this@SearchRobot.composeTestRule.waitUntilExactlyOneExists(hasText(headerText), waitingTime)
        this@SearchRobot.composeTestRule.onNodeWithText(headerText).assertIsDisplayed()
        Log.i(TAG, "verifyTheFirefoxSuggestHeader: Verified the Firefox Suggest header is displayed.")
    }

    /**
     * Verifies that the sponsored suggestions are displayed. For regular search suggestions, use
     * [verifySearchSuggestionsAreDisplayed].
     */
    @OptIn(ExperimentalTestApi::class)
    fun verifySponsoredSuggestionsResults(
        vararg searchSuggestions: String,
        searchTerm: String,
        shouldEditKeyword: Boolean = false,
        numberOfDeletionSteps: Int = 0,
        shouldUseSearchShort: Boolean = false,
        searchEngineName: String = "",
    ) {
        this@SearchRobot.composeTestRule.waitForIdle()
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifySponsoredSuggestionsResults: Started try #$i")
            try {
                for (searchSuggestion in searchSuggestions) {
                    Log.i(TAG, "verifySponsoredSuggestionsResults: Trying to perform \"Close soft keyboard\" action")
                    closeSoftKeyboard()
                    Log.i(TAG, "verifySponsoredSuggestionsResults: Performed \"Close soft keyboard\" action")
                    Log.i(
                        TAG,
                        "verifySponsoredSuggestionsResults: Waiting for $waitingTime ms until $searchSuggestion search suggestion exists",
                    )
                    this@SearchRobot.composeTestRule.waitUntilExactlyOneExists(hasText(searchSuggestion), waitingTime)
                    Log.i(
                        TAG,
                        "verifySponsoredSuggestionsResults: Waited for $waitingTime ms until $searchSuggestion search suggestion exists",
                    )
                }

                break
            } catch (e: ComposeTimeoutException) {
                Log.i(TAG, "verifySponsoredSuggestionsResults: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    mDevice.pressBack()
                    homeScreen(this@SearchRobot.composeTestRule) {}
                        .openSearch {
                            if (shouldUseSearchShort) {
                                clickSearchSelectorButton()
                                selectTemporarySearchMethod(searchEngineName)
                            }
                            typeSearch(searchTerm)
                            if (shouldEditKeyword) {
                                deleteSearchKeywordCharacters(numberOfDeletionSteps = numberOfDeletionSteps)
                            }
                        }
                }
            }
        }
        for (searchSuggestion in searchSuggestions) {
            Log.i(
                TAG,
                "verifySponsoredSuggestionsResults: Trying to verify that $searchSuggestion search suggestion exists",
            )
            this@SearchRobot.composeTestRule.onNodeWithText(searchSuggestion).assertIsDisplayed()
            Log.i(TAG, "verifySponsoredSuggestionsResults: Verified that $searchSuggestion search suggestion exists")
        }
    }

    /**
     * Verifies that the regular search suggestions are displayed. For sponsored suggestions, use
     * [verifySponsoredSuggestionsResults].
     */
    @OptIn(ExperimentalTestApi::class)
    fun verifySearchSuggestionsAreDisplayed(vararg searchSuggestions: String) {
        composeTestRule.waitForIdle()
        for (searchSuggestion in searchSuggestions) {
            Log.i(TAG, "verifySearchSuggestionsAreDisplayed: Trying to perform \"Close soft keyboard\" action.")
            closeSoftKeyboard()
            Log.i(TAG, "verifySearchSuggestionsAreDisplayed: Performed \"Close soft keyboard\" action.")
            Log.i(
                TAG,
                "verifySearchSuggestionsAreDisplayed: Waiting for $waitingTime ms until $searchSuggestion search suggestion exists.",
            )
            composeTestRule.waitUntilAtLeastOneExists(
                hasTestTag(AwesomeBarTestTags.SUGGESTION) and hasText(searchSuggestion, substring = true),
                waitingTime,
            )
            composeTestRule
                .onAllNodesWithTag(AwesomeBarTestTags.SUGGESTION)
                .assertAny(hasText(searchSuggestion, substring = true))
            Log.i(TAG, "verifySearchSuggestionsAreDisplayed: Verified $searchSuggestion search suggestion exists.")
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyBookmarkSearchSuggestionsAreDisplayed(vararg searchSuggestions: String) {
        composeTestRule.waitForIdle()
        for (searchSuggestion in searchSuggestions) {
            Log.i(TAG, "verifyBookmarkSearchSuggestionsAreDisplayed: Trying to perform \"Close soft keyboard\" action.")
            closeSoftKeyboard()
            Log.i(TAG, "verifyBookmarkSearchSuggestionsAreDisplayed: Performed \"Close soft keyboard\" action.")
            Log.i(
                TAG,
                "verifyBookmarkSearchSuggestionsAreDisplayed: Waiting for $waitingTime ms until $searchSuggestion search suggestion exists.",
            )
            composeTestRule.waitUntilAtLeastOneExists(
                hasText(searchSuggestion, substring = true),
                waitingTime,
            )
            composeTestRule.onNode(hasText(searchSuggestion, substring = true)).assertIsDisplayed()
            Log.i(
                TAG,
                "verifyBookmarkSearchSuggestionsAreDisplayed: Verified $searchSuggestion search suggestion exists.",
            )
        }
    }

    fun verifySuggestionsAreNotDisplayed(vararg searchSuggestions: String) {
        Log.i(TAG, "verifySuggestionsAreNotDisplayed: Waiting for compose test rule to be idle")
        this@SearchRobot.composeTestRule.waitForIdle()
        Log.i(TAG, "verifySuggestionsAreNotDisplayed: Waited for compose test rule to be idle")
        for (searchSuggestion in searchSuggestions) {
            Log.i(
                TAG,
                "verifySuggestionsAreNotDisplayed: Trying to verify that there are no $searchSuggestion related search suggestions",
            )
            this@SearchRobot.composeTestRule
                .onAllNodesWithTag(AwesomeBarTestTags.SUGGESTIONS)
                .assertAny(hasText(searchSuggestion).not())
            Log.i(
                TAG,
                "verifySuggestionsAreNotDisplayed: Verified that there are no $searchSuggestion related search suggestions",
            )
        }
    }

    fun verifyBookmarkSuggestionsAreNotDisplayed(vararg searchSuggestions: String) {
        Log.i(TAG, "verifyBookmarkSuggestionsAreNotDisplayed: Waiting for compose test rule to be idle")
        this@SearchRobot.composeTestRule.waitForIdle()
        Log.i(TAG, "verifyBookmarkSuggestionsAreNotDisplayed: Waited for compose test rule to be idle")
        for (searchSuggestion in searchSuggestions) {
            Log.i(
                TAG,
                "verifyBookmarkSuggestionsAreNotDisplayed: Trying to verify that there are no $searchSuggestion related search suggestions",
            )
            this@SearchRobot.composeTestRule.onNodeWithText(searchSuggestion).assertIsNotDisplayed()
            Log.i(
                TAG,
                "verifyBookmarkSuggestionsAreNotDisplayed: Verified that there are no $searchSuggestion related search suggestions",
            )
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifySearchSuggestionsCount(numberOfSuggestions: Int, searchTerm: String) {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifySearchSuggestionsCount: Started try #$i")
            try {
                Log.i(
                    TAG,
                    "verifySearchSuggestionsCount: Compose test rule is waiting for $waitingTime ms until the note count equals to: $numberOfSuggestions",
                )
                this@SearchRobot.composeTestRule.waitUntilNodeCount(
                    hasTestTag(AwesomeBarTestTags.SUGGESTION),
                    numberOfSuggestions,
                    waitingTime,
                )
                Log.i(
                    TAG,
                    "verifySearchSuggestionsCount: Compose test rule waited for $waitingTime ms until the note count equals to: $numberOfSuggestions",
                )
                Log.i(
                    TAG,
                    "verifySearchSuggestionsCount: Trying to verify that the count of the search suggestions equals: $numberOfSuggestions",
                )
                this@SearchRobot.composeTestRule
                    .onAllNodesWithTag(AwesomeBarTestTags.SUGGESTION)
                    .assertCountEquals(numberOfSuggestions)
                Log.i(
                    TAG,
                    "verifySearchSuggestionsCount: Verified that the count of the search suggestions equals: $numberOfSuggestions",
                )

                break
            } catch (e: ComposeTimeoutException) {
                Log.i(TAG, "verifySearchSuggestionsCount: ComposeTimeoutException caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(TAG, "verifySearchSuggestionsCount: Trying to click device back button")
                    mDevice.pressBack()
                    Log.i(TAG, "verifySearchSuggestionsCount: Clicked device back button")
                    homeScreen(this@SearchRobot.composeTestRule) {}
                        .openSearch {
                            typeSearch(searchTerm)
                        }
                }
            }
        }
    }

    fun verifyAllowSuggestionsInPrivateModeDialog() {
        Log.i(
            TAG,
            "verifyAllowSuggestionsInPrivateModeDialog: Trying to verify that the private browsing search suggestion title is displayed",
        )
        this@SearchRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.search_suggestions_onboarding_title), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyAllowSuggestionsInPrivateModeDialog: Verified that the private browsing search suggestion title is displayed",
        )
        Log.i(
            TAG,
            "verifyAllowSuggestionsInPrivateModeDialog: Trying to verify that the private browsing search suggestion message is displayed",
        )
        this@SearchRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.search_suggestions_onboarding_text), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyAllowSuggestionsInPrivateModeDialog: Verified that the private browsing search suggestion message is displayed",
        )
        Log.i(
            TAG,
            "verifyAllowSuggestionsInPrivateModeDialog: Trying to verify that the \"Learn more\" link is displayed",
        )
        this@SearchRobot.composeTestRule
            .onNodeWithContentDescription("Learn more Links available", useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(TAG, "verifyAllowSuggestionsInPrivateModeDialog: Verified that the \"Learn more\" link is displayed")
        Log.i(TAG, "verifyAllowSuggestionsInPrivateModeDialog: Trying to verify that the \"Allow\" button is displayed")
        this@SearchRobot.composeTestRule
            .onNodeWithText(
                getStringResource(R.string.search_suggestions_onboarding_allow_button),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyAllowSuggestionsInPrivateModeDialog: Verified that the \"Allow\" button is displayed")
        Log.i(
            TAG,
            "verifyAllowSuggestionsInPrivateModeDialog: Trying to verify that the \"Don't allow\" button is displayed",
        )
        this@SearchRobot.composeTestRule
            .onNodeWithText(
                getStringResource(R.string.search_suggestions_onboarding_do_not_allow_button),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyAllowSuggestionsInPrivateModeDialog: Verified that the \"Don't allow\" button is displayed")
    }

    fun denySuggestionsInPrivateMode() {
        Log.i(TAG, "denySuggestionsInPrivateMode: Trying to click the \"Don’t allow\" button")
        mDevice
            .findObject(
                UiSelector().text(getStringResource(R.string.search_suggestions_onboarding_do_not_allow_button))
            )
            .click()
        Log.i(TAG, "denySuggestionsInPrivateMode: Clicked the \"Don’t allow\" button")
    }

    fun allowSuggestionsInPrivateMode() {
        Log.i(TAG, "allowSuggestionsInPrivateMode: Trying to click the \"Allow\" button")
        mDevice
            .findObject(UiSelector().text(getStringResource(R.string.search_suggestions_onboarding_allow_button)))
            .click()
        Log.i(TAG, "allowSuggestionsInPrivateMode: Clicked the \"Allow\" button")
    }

    fun verifySearchSelectorButton() {
        Log.i(TAG, "verifySearchSelectorButton: Trying to verify that the search selector button is displayed")
        composeTestRule.onNodeWithTag(SEARCH_SELECTOR).assertIsDisplayed()
        Log.i(TAG, "verifySearchSelectorButton: Verified that the search selector button is displayed")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickSearchSelectorButton() {
        Log.i(TAG, "clickSearchSelectorButton: Waiting for $waitingTime until the search selector button exists")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(SEARCH_SELECTOR), waitingTime)
        Log.i(TAG, "clickSearchSelectorButton: Waited for $waitingTime until the search selector button exists")
        Log.i(TAG, "clickSearchSelectorButton: Trying to click the search selector button")
        composeTestRule.onNodeWithTag(SEARCH_SELECTOR).performClick()
        Log.i(TAG, "clickSearchSelectorButton: Clicked the search selector button")
    }

    fun verifySearchEngineIcon(name: String) {
        Log.i(
            TAG,
            "verifySearchEngineIcon: Trying to verify that search selector icon for: $name search engine is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.search_engine_selector_content_description, name))
            .assertIsDisplayed()
        Log.i(TAG, "verifySearchEngineIcon: Verified that search selector icon for: $name search engine is displayed")
    }

    fun verifySearchBarPlaceholder(searchHint: String) {
        Log.i(TAG, "verifySearchBarPlaceholder: Verify hint is $searchHint")
        this@SearchRobot.composeTestRule
            .onNodeWithTag(ADDRESSBAR_SEARCH_BOX)
            .assert(hasText(searchHint))
            .assertIsDisplayed()
        Log.i(TAG, "verifySearchBarPlaceholder: Verification successful")
    }

    fun verifyBookmarkSearchBarPlaceholder() {
        Log.i(TAG, "verifyBookmarkSearchBarPlaceholder: Verify placeholder is shown")
        this@SearchRobot.composeTestRule.onNodeWithTag(BOOKMARK_PLACEHOLDER, useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyBookmarkSearchBarPlaceholder: Verification successful")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifySearchShortcutList(vararg searchEngineNames: String, isSearchEngineDisplayed: Boolean) {
        for (searchEngineName in searchEngineNames) {
            if (isSearchEngineDisplayed) {
                Log.i(
                    TAG,
                    "verifySearchShortcutList: Waiting for $waitingTime until search selector: $searchEngineName exists",
                )
                composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(searchEngineName), waitingTime)
                Log.i(
                    TAG,
                    "verifySearchShortcutList: Waited for $waitingTime until search selector: $searchEngineName exists",
                )
                Log.i(
                    TAG,
                    "verifySearchShortcutList: Trying to verify the $searchEngineName search shortcut is displayed",
                )
                composeTestRule.onNodeWithContentDescription(searchEngineName).assertIsDisplayed()
                Log.i(TAG, "verifySearchShortcutList: Verified the $searchEngineName search shortcut is displayed")
            } else {
                Log.i(
                    TAG,
                    "verifySearchShortcutList: Trying to verify the $searchEngineName search shortcut is not displayed",
                )
                composeTestRule.onNodeWithContentDescription(searchEngineName).assertIsNotDisplayed()
                Log.i(TAG, "verifySearchShortcutList: Verified the $searchEngineName search shortcut is not displayed")
            }
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun selectTemporarySearchMethod(searchEngineName: String) {
        Log.i(TAG, "verifySearchShortcutList: Waiting for $waitingTime until search shortcut: $searchEngineName exists")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(searchEngineName), waitingTime)
        Log.i(TAG, "verifySearchShortcutList: Waited for $waitingTime until search shortcut: $searchEngineName exists")
        Log.i(TAG, "selectTemporarySearchMethod: Trying to click the $searchEngineName search shortcut")
        composeTestRule.onNodeWithContentDescription(searchEngineName).performClick()
        Log.i(TAG, "selectTemporarySearchMethod: Clicked the $searchEngineName search shortcut")
    }

    fun clickScanButton() {
        Log.i(TAG, "clickScanButton: Trying to click the scan button")
        this@SearchRobot.composeTestRule
            .onNodeWithContentDescription(getStringResource(qrR.string.mozac_feature_qr_scanner))
            .performClick()
        Log.i(TAG, "clickScanButton: Clicked the scan button")
    }

    fun clickDismissPermissionRequiredDialog() {
        Log.i(
            TAG,
            "clickDismissPermissionRequiredDialog: Waiting for $waitingTime ms for the \"Dismiss\" permission button to exist",
        )
        dismissPermissionButton().waitForExists(waitingTime)
        Log.i(
            TAG,
            "clickDismissPermissionRequiredDialog: Waited for $waitingTime ms for the \"Dismiss\" permission button to exist",
        )
        Log.i(TAG, "clickDismissPermissionRequiredDialog: Trying to click the \"Dismiss\" permission button")
        dismissPermissionButton().click()
        Log.i(TAG, "clickDismissPermissionRequiredDialog: Clicked the \"Dismiss\" permission button")
    }

    fun clickGoToPermissionsSettings() {
        Log.i(
            TAG,
            "clickGoToPermissionsSettings: Waiting for $waitingTime ms for the \"Go To Settings\" permission button to exist",
        )
        goToPermissionsSettingsButton().waitForExists(waitingTime)
        Log.i(
            TAG,
            "clickGoToPermissionsSettings: Waited for $waitingTime ms for the \"Go To Settings\" permission button to exist",
        )
        Log.i(TAG, "clickGoToPermissionsSettings: Trying to click the \"Go To Settings\" permission button")
        goToPermissionsSettingsButton().click()
        Log.i(TAG, "clickGoToPermissionsSettings: Clicked the \"Go To Settings\" permission button")
    }

    fun verifyScannerOpen() {
        Log.i(
            TAG,
            "verifyScannerOpen: Trying to verify that the device camera is opened or that the camera app error message exist",
        )
        assertTrue(
            "$TAG: Neither the device camera was opened nor the camera app error message was displayed",
            // The Lens camera shows its title whether or not a camera is available.
            mDevice
                .findObject(UiSelector().text(getStringResource(R.string.lens_camera_title_qr)))
                .waitForExists(waitingTime) ||
                mDevice.findObject(UiSelector().resourceId("$packageName:id/view_finder")).exists() ||
                // In case there is no camera available, an error will be shown.
                mDevice.findObject(UiSelector().resourceId("$packageName:id/camera_error")).exists(),
        )
        Log.i(
            TAG,
            "verifyScannerOpen: Verified that the device camera is opened or that the camera app error message exist",
        )
    }

    fun typeSearch(searchTerm: String) {
        Log.i(TAG, "typeSearch: Waiting for search box to appear")
        composeTestRule.waitUntil(waitingTime) {
            composeTestRule.onAllNodesWithTag(ADDRESSBAR_SEARCH_BOX).fetchSemanticsNodes().isNotEmpty()
        }
        Log.i(TAG, "typeSearch: Search box is ready")

        Log.i(TAG, "typeSearch: Performing text replacement with '$searchTerm'")
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTextReplacement(searchTerm)
        Log.i(TAG, "typeSearch: Text replacement done")

        Log.i(TAG, "typeSearch: Waiting for Compose to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "typeSearch: Compose is now idle")
    }

    fun typeBookmarkSearch(searchTerm: String) {
        Log.i(TAG, "typeBookmarkSearch: Waiting for search box to appear")
        composeTestRule.waitUntil(waitingTime) {
            composeTestRule.onAllNodesWithTag(ADDRESSBAR_SEARCH_BOX).fetchSemanticsNodes().isNotEmpty()
        }
        Log.i(TAG, "typeBookmarkSearch: Search box is ready")

        Log.i(TAG, "typeBookmarkSearch: Performing text replacement with '$searchTerm'")
        composeTestRule
            .onNode(hasSetTextAction() and hasAnyAncestor(hasTestTag(ADDRESSBAR_SEARCH_BOX)))
            .performTextReplacement(searchTerm)

        Log.i(TAG, "typeBookmarkSearch: Text replacement done")

        Log.i(TAG, "typeBookmarkSearch: Waiting for Compose to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "typeBookmarkSearch: Compose is now idle")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickClearButton() {
        Log.i(TAG, "openSearch: Waiting for $waitingTime until the clear toolbar button exists")
        composeTestRule.waitUntilAtLeastOneExists(
            hasContentDescription(getStringResource(toolbarR.string.mozac_clear_button_description)),
            waitingTime,
        )
        Log.i(TAG, "openSearch: Waited for $waitingTime until the clear toolbar button exists")
        Log.i(TAG, "clickClearButton: Trying to click the clear button")
        itemWithDescription(getStringResource(toolbarR.string.mozac_clear_button_description)).click()
        Log.i(TAG, "clickClearButton: Clicked the clear button")
        Log.i(TAG, "clickClearButton: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickClearButton: Waited for compose test rule to be idle")
    }

    fun tapOutsideToDismissSearchBar(libraryItem: String) {
        Log.i(TAG, "tapOutsideToDismissSearchBar: Trying to click outside the search bar")
        itemContainingText(libraryItem).click()
        Log.i(TAG, "tapOutsideToDismissSearchBar: Clicked outside the search bar")
    }

    @OptIn(ExperimentalTestApi::class)
    fun longClickToolbar() {
        Log.i(TAG, "longClickToolbar: Trying to perform \"Close soft keyboard\" action")
        closeSoftKeyboard()
        Log.i(TAG, "longClickToolbar: Performed \"Close soft keyboard\" action")
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
        Log.i(TAG, "longClickToolbar: Trying to perform long click on the toolbar")
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTouchInput { longClick() }
        Log.i(TAG, "longClickToolbar: Performed long click on the toolbar")
    }

    fun clickEditUrlButton() {
        Log.i(TAG, "clickEditURLButton: Trying to tap the \"Edit URL\" button in the awesomebar")
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
        composeTestRule.onNodeWithTag(EDIT_CURRENT_SITE_URL_BUTTON).performTouchInput { click() }
        Log.i(TAG, "clickEditURLButton: tapped the \"Edit URL\" button in the awesomebar")
    }

    fun clickPasteText() {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "clickPasteText: Started try #$i")
            try {
                Log.i(TAG, "clickPasteText: Waiting for $waitingTime ms for the \"Paste\" option to exist")
                mDevice.findObject(UiSelector().textContains("Paste")).waitForExists(waitingTime)
                Log.i(TAG, "clickPasteText: Waited for $waitingTime ms for the \"Paste\" option to exist")
                Log.i(TAG, "clickPasteText: Trying to click the \"Paste\" button")
                mDevice.findObject(By.textContains("Paste")).click()
                Log.i(TAG, "clickPasteText: Clicked the \"Paste\" button")

                break
            } catch (e: NullPointerException) {
                Log.i(TAG, "clickPasteText: NullPointerException caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    searchScreen(this@SearchRobot.composeTestRule) {}
                        .dismissSearchBar {}
                        .openSearch {
                            clickClearButton()
                            longClickToolbar()
                        }
                }
            }
        }
    }

    fun verifyTranslatedNavigationToolbarHint(toolbarHintString: String) {
        Log.i(
            TAG,
            "verifyTranslatedNavigationToolbarHint: Trying to verify that the translated toolbar has: $toolbarHintString as a hint",
        )
        composeTestRule
            .onNode(
                hasText(toolbarHintString),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTranslatedNavigationToolbarHint: Verified that the translated toolbar has: $toolbarHintString as a hint",
        )
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTypedToolbarText(expectedText: String, exists: Boolean) {
        Log.i(TAG, "verifyTypedToolbarText: Waiting for $waitingTime until the edited URL exists")
        composeTestRule.waitUntilAtLeastOneExists(
            hasTestTag(ADDRESSBAR_SEARCH_BOX) or hasText(CURRENT_URL_IN_SITE_DETAILS),
            waitingTime,
        )
        Log.i(TAG, "verifyTypedToolbarText: Waited for $waitingTime until the edited URL exists")
        Log.i(TAG, "verifyTypedToolbarText: Verifying that text '$expectedText' exists?: $exists")
        val normalizedExpectedText =
            normalizeWhitespace(expectedText).let {
                when (settings.showAddressBarInFocusMode) {
                    true -> URLStringUtils.toDisplayUrl(it).toString()
                    else -> it
                }
            }
        val actualText =
            when (settings.showAddressBarInFocusMode) {
                true ->
                    composeTestRule
                        .onNodeWithTag(CURRENT_URL_IN_SITE_DETAILS, true)
                        .fetchSemanticsNode()
                        .config
                        .toNormalizedToolbarText()
                false ->
                    composeTestRule
                        .onNodeWithTag(ADDRESSBAR_SEARCH_BOX)
                        .fetchSemanticsNode()
                        .config
                        .toNormalizedToolbarText()
            }

        assertTrue(
            "Expected toolbar text '$normalizedExpectedText' to ${if (exists) "exist" else "not exist"} in '$actualText'",
            if (exists) {
                actualText.contains(normalizedExpectedText)
            } else {
                !actualText.contains(normalizedExpectedText)
            },
        )
        Log.i(TAG, "verifyTypedToolbarText: Verification successful.")
    }

    private fun normalizeWhitespace(text: String): String = text.replace(Regex("\\s+"), " ").trim()

    private fun androidx.compose.ui.semantics.SemanticsConfiguration.toNormalizedToolbarText(): String {
        val textParts = buildList {
            getOrNull(SemanticsProperties.Text)?.let { annotations ->
                addAll(annotations.map { it.text })
            }
            getOrNull(SemanticsProperties.EditableText)?.let { editableText ->
                add(editableText.text)
            }
        }

        return normalizeWhitespace(textParts.joinToString(" "))
    }

    fun verifySearchBarPosition() {
        Log.i(TAG, "verifySearchBarPosition: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "verifySearchBarPosition: Waited for compose test rule to be idle")

        val editModeToolbar = mDevice.findObject(By.res(ADDRESSBAR_EDIT_MODE))
        val horizontalDivider = mDevice.findObject(By.res(ADDRESSBAR_EDIT_MODE_HORIZONTAL_DIVIDER))

        val editModeToolbarBottom = editModeToolbar.visibleBounds.bottom
        val horizontalDividerTop = horizontalDivider.visibleBounds.top
        // Allow small pixel differences due to measurement rounding
        val roundingTolerancePx = 5

        Log.i(TAG, "verifySearchBarPosition: Trying to click the edit mode toolbar")
        editModeToolbar.click()
        Log.i(TAG, "verifySearchBarPosition: Clicked the edit mode toolbar")

        Log.i(
            TAG,
            "verifySearchBarPosition: Trying to verify that the edit mode toolbar is above the horizontal divider",
        )
        assert(editModeToolbarBottom <= horizontalDividerTop + roundingTolerancePx) {
            "($TAG, ADDRESSBAR_EDIT_MODE is not above ADDRESSBAR_HORIZONTAL_DIVIDER: " +
                "editBottom=$editModeToolbarBottom, dividerTop=$horizontalDividerTop"
        }
        Log.i(TAG, "verifySearchBarPosition: Verified that the edit mode toolbar is above the horizontal divider")
    }

    fun deleteSearchKeywordCharacters(numberOfDeletionSteps: Int) {
        for (i in 1..numberOfDeletionSteps) {
            Log.i(TAG, "deleteSearchKeywordCharacters: Trying to click keyboard delete button $i times")
            mDevice.pressDelete()
            Log.i(TAG, "deleteSearchKeywordCharacters: Clicked keyboard delete button $i times")
            Log.i(
                TAG,
                "deleteSearchKeywordCharacters: Waiting for $waitingTimeShort ms for $appName window to be updated",
            )
            mDevice.waitForWindowUpdate(appName, waitingTimeShort)
            Log.i(
                TAG,
                "deleteSearchKeywordCharacters: Waited for $waitingTimeShort ms for $appName window to be updated",
            )
        }
    }

    fun verifyTextSelectionOptions(vararg textSelectionOptions: String) {
        for (textSelectionOption in textSelectionOptions) {
            mDevice.waitNotNull(Until.findObject(textContains(textSelectionOption)), waitingTime)
        }
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun dismissSearchBar(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "dismissSearchBar: Trying to click device back button")
            mDevice.pressBack()
            Log.i(TAG, "dismissSearchBar: Clicked device back button")
            composeTestRule.waitForIdle()

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun submitQuery(query: String, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "submitQuery: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "submitQuery: Waited for compose rule to be idle")

            Log.i(TAG, "submitQuery: Trying to set toolbar text to: $query")
            itemWithResId("ADDRESSBAR_SEARCH_BOX").setText(query)
            Log.i(TAG, "submitQuery: Toolbar text was set to: $query")

            Log.i(TAG, "submitQuery: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "submitQuery: Waited for compose rule to be idle")

            runCatching {
                Log.i(TAG, "submitQuery: Trying to perform Compose IME action perform on the toolbar")
                composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performImeAction()
                Log.i(TAG, "submitQuery: Compose IME action performed on the toolbar")
            }
                .onFailure { throwable ->
                    Log.e(
                        TAG,
                        "submitQuery: Compose IME action failed with: ${throwable::class.java.simpleName} - ${throwable.message}",
                    )
                    Log.d(TAG, "submitQuery: Falling back to UiDevice pressEnter()")
                    mDevice.pressEnter()
                    Log.d(TAG, "submitQuery: Fallback UiDevice pressEnter() completed")
                }

            Log.i(TAG, "submitQuery: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "submitQuery: Waited for compose rule to be idle")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickSearchEngineSettings(
            interact: SettingsSubMenuSearchRobot.() -> Unit
        ): SettingsSubMenuSearchRobot.Transition {
            Log.i(
                TAG,
                "clickSearchEngineSettings: Waiting for $waitingTime until the \"Search settings\" button exists",
            )
            composeTestRule.waitUntilAtLeastOneExists(
                hasContentDescription(getStringResource(R.string.search_settings_menu_item)),
                waitingTime,
            )
            Log.i(TAG, "clickSearchEngineSettings: Waited for $waitingTime until the \"Search settings\" button exists")
            Log.i(TAG, "clickSearchEngineSettings: Trying to click the \"Search settings\" button")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.search_settings_menu_item))
                .performClick()
            Log.i(TAG, "clickSearchEngineSettings: Clicked the \"Search settings\" button")

            SettingsSubMenuSearchRobot().interact()
            return SettingsSubMenuSearchRobot.Transition()
        }

        fun clickSearchSuggestion(
            searchSuggestion: String,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            mDevice.findObject(UiSelector().textContains(searchSuggestion)).also {
                Log.i(
                    TAG,
                    "clickSearchSuggestion: Waiting for $waitingTime ms for search suggestion: $searchSuggestion to exist",
                )
                it.waitForExists(waitingTime)
                Log.i(
                    TAG,
                    "clickSearchSuggestion: Waited for $waitingTime ms for search suggestion: $searchSuggestion to exist",
                )
                Log.i(
                    TAG,
                    "clickSearchSuggestion: Trying to click search suggestion: $searchSuggestion and wait for $waitingTimeShort ms for a new window",
                )
                it.clickAndWaitForNewWindow(waitingTimeShort)
                Log.i(
                    TAG,
                    "clickSearchSuggestion: Clicked search suggestion: $searchSuggestion and waited for $waitingTimeShort ms for a new window",
                )
            }

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }
    }
}

fun searchScreen(composeTestRule: ComposeTestRule, interact: SearchRobot.() -> Unit): SearchRobot.Transition {
    SearchRobot(composeTestRule).interact()
    return SearchRobot.Transition(composeTestRule)
}

private fun dismissPermissionButton() = mDevice.findObject(UiSelector().text("Dismiss"))

private fun goToPermissionsSettingsButton() = mDevice.findObject(UiSelector().text("Go to settings"))
