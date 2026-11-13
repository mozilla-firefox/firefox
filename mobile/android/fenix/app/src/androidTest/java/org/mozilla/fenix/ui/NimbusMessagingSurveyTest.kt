/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.content.Context
import android.content.pm.ActivityInfo
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.json.JSONObject
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.experiments.nimbus.HardcodedNimbusFeatures
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.HomeScreenSection
import org.mozilla.fenix.nimbus.Homescreen
import org.mozilla.fenix.ui.robots.surveyScreen

/**
 * Tests for verifying basic functionality of the Nimbus Survey surface message
 *
 * Verifies a message can be displayed with all of the correct components
 */
class NimbusMessagingSurveyTest {
    private lateinit var context: Context
    private lateinit var hardcodedNimbus: HardcodedNimbusFeatures

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule.withDefaultSettingsOverrides()) { it.activity }

    @Before
    fun setUp() {
        context = TestHelper.appContext

        // Set up nimbus message
        hardcodedNimbus =
            HardcodedNimbusFeatures(
                context,
                "messaging" to
                    JSONObject(
                        """
                        {
                          "message-under-experiment": "test-survey-messaging-surface",
                          "messages": {
                            "test-survey-messaging-surface": {
                              "title": "Survey Message Test",
                              "text": "Some Nimbus Messaging text",
                              "surface": "survey",
                              "style": "SURVEY",
                              "action": "OPEN_URL",
                              "action-params": {
                                "url": "https://www.example.com"
                              },
                              "trigger": [
                                "ALWAYS"
                              ]
                            }
                          }
                        }
                        """
                            .trimIndent()
                    ),
            )

        // Remove some homescreen features not needed for testing
        FxNimbus.features.homescreen.withInitializer { _, _ ->
            // These are FML generated objects and enums
            Homescreen(
                sectionsEnabled =
                    mapOf(
                        HomeScreenSection.JUMP_BACK_IN to false,
                        HomeScreenSection.POCKET to false,
                        HomeScreenSection.POCKET_SPONSORED_STORIES to false,
                        HomeScreenSection.TOP_SITES to false,
                    )
            )
        }
        composeTestRule.activityRule.finishActivity()
        hardcodedNimbus.connectWith(FxNimbus)
        composeTestRule.activityRule.launchActivity(null)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809390
    @Test
    fun checkSurveyNavigatesCorrectly() {
        surveyScreen(composeTestRule) {
                verifySurveyButton(composeTestRule)
            }
            .clickSurveyButton {
                verifyUrl("example.com")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809389
    @Test
    fun checkSurveyNoThanksNavigatesCorrectly() {
        surveyScreen(composeTestRule) {
                verifySurveyNoThanksButton(composeTestRule)
            }
            .clickNoThanksSurveyButton {
                verifyTabCounter("0")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2809388
    @Test
    fun checkSurveyLandscapeLooksCorrect() {
        composeTestRule.activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        surveyScreen(composeTestRule) {
                verifySurveyNoThanksButton(composeTestRule)
                verifySurveyButton(composeTestRule)
            }
            .clickNoThanksSurveyButton {
                verifyTabCounter("0")
            }
    }
}
