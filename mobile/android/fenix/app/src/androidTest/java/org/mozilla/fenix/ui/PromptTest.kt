package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying basic functionality of prompts
 *
 *  Including:
 *  - beforeunload prompt
 */

class PromptTest : TestSetup() {
    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @SmokeTest
    @Test
    fun verifyBeforeUnloadPrompt() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val promptWebPage = TestAssetHelper.getPromptAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(promptWebPage.url) {
            clickPageObject(MatcherHelper.itemWithResId("nameInput"))
        }

        navigationToolbar {
        }.enterURL(defaultWebPage.url) {
            verifyBeforeUnloadPromptExists()
        }
    }
}

private fun verifyBeforeUnloadPromptExists() =
    MatcherHelper.assertUIObjectExists(itemContainingText(getStringResource(R.string.mozac_feature_prompt_before_unload_dialog_body)))
