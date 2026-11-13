/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withText
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.CoreMatchers.endsWith
import org.junit.Assert.assertTrue
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.hasCousin
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.ui.robots.SettingsSubMenuAccessibilityRobot.Companion.DECIMAL_CONVERSION
import org.mozilla.fenix.ui.robots.SettingsSubMenuAccessibilityRobot.Companion.MIN_VALUE
import org.mozilla.fenix.ui.robots.SettingsSubMenuAccessibilityRobot.Companion.STEP_SIZE

/** Implementation of Robot Pattern for the settings Accessibility sub menu. */
class SettingsSubMenuAccessibilityRobot {

    companion object {
        const val STEP_SIZE = 5
        const val MIN_VALUE = 50
        const val DECIMAL_CONVERSION = 100f
    }

    fun clickFontSizingSwitch() = toggleFontSizingSwitch()

    fun verifyFontSizingMenuItems(
        composeTestRule: ComposeTestRule,
        isTheAutomaticFontSizingToggleChecked: Boolean,
        isTheFontSizingSliderEnabled: Boolean,
        isTheZoomOnAllWbsitesToggleChecked: Boolean,
    ) {
        // Automatic font sizing
        Log.i(TAG, "verifyFontSizingMenuItems: Trying to verify that the \"Automatic font sizing\" title is displayed")
        onView(withText(getStringResource(R.string.preference_accessibility_auto_size_2)))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyFontSizingMenuItems: Verified that the \"Automatic font sizing\" title is displayed")
        Log.i(
            TAG,
            "verifyFontSizingMenuItems: Trying to verify that the \"Automatic font sizing\" summary is displayed",
        )
        onView(withText(getStringResource(R.string.preference_accessibility_auto_size_summary)))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyFontSizingMenuItems: Verified that the \"Automatic font sizing\" summary is displayed")

        // Automatic font sizing toggle assertion
        Log.i(
            TAG,
            "verifyEnabledMenuItems: Trying to verify that the \"Automatic font sizing\" toggle is enabled: $isTheAutomaticFontSizingToggleChecked",
        )
        onView(withText(R.string.preference_accessibility_auto_size_2))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (isTheAutomaticFontSizingToggleChecked) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        )
                    )
                )
            )
        Log.i(
            TAG,
            "verifyEnabledMenuItems: Verified that the \"Automatic font sizing\" toggle is enabled: $isTheAutomaticFontSizingToggleChecked",
        )

        // Font size
        Log.i(TAG, "verifyFontSizingMenuItems: Trying to verify that the \"Font Size\" title is displayed")
        composeTestRule.onNodeWithTag("fontSizeTitle").assertIsDisplayed()
        Log.i(TAG, "verifyFontSizingMenuItems: Verified that the \"Font Size\" title is displayed")
        Log.i(TAG, "verifyFontSizingMenuItems: Trying to verify that the \"Font Size\" summary is displayed")
        composeTestRule.onNodeWithTag("fontSizeSubtitle").assertIsDisplayed()
        Log.i(TAG, "verifyFontSizingMenuItems: Verified that the \"Font Size\" summary is displayed")

        // Font size seek bar
        Log.i(TAG, "verifyEnabledMenuItems: Trying to verify that the seek bar is displayed")
        composeTestRule.onNodeWithTag("fontSizeSlider").assertIsDisplayed()
        Log.i(TAG, "verifyEnabledMenuItems: Verified that the seek bar is displayed")
        Log.i(TAG, "verifyEnabledMenuItems: Trying to verify that the seek bar value is set to 100%")
        composeTestRule.onNodeWithTag("fontSizeSliderValue").assertTextEquals("100 %")
        Log.i(TAG, "verifyEnabledMenuItems: Verified that the seek bar value is set to 100%")

        // Seek bar assertion
        if (isTheFontSizingSliderEnabled) {
            Log.i(TAG, "verifyEnabledMenuItems: Trying to verify that the seek bar is enabled")
            composeTestRule.onNodeWithTag("fontSizeSlider").assertIsEnabled()
            Log.i(TAG, "verifyEnabledMenuItems: Verified that the seek bar is displayed")
        } else {
            Log.i(TAG, "verifyEnabledMenuItems: Trying to verify that the seek bar is not enabled")
            composeTestRule.onNodeWithTag("fontSizeSlider").assertIsNotEnabled()
            Log.i(TAG, "verifyEnabledMenuItems: Verified that the seek bar is not enabled")
        }

        // Zoom on all websites
        Log.i(TAG, "verifyFontSizingMenuItems: Trying to verify that the \"Zoom on all websites\" title is displayed")
        onView(withText(getStringResource(R.string.preference_accessibility_force_enable_zoom)))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyFontSizingMenuItems: Verified that the \"Zoom on all websites\" title is displayed")
        Log.i(TAG, "verifyFontSizingMenuItems: Trying to verify that the \"Zoom on all websites\" summary is displayed")
        onView(withText(getStringResource(R.string.preference_accessibility_force_enable_zoom_summary)))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyFontSizingMenuItems: Verified that the \"Zoom on all websites\" summary is displayed")

        // Zoom on all websites toggle assertion
        Log.i(
            TAG,
            "verifyEnabledMenuItems: Trying to verify that the \"Zoom on all websites\" toggle is enabled: $isTheZoomOnAllWbsitesToggleChecked",
        )
        onView(withText(R.string.preference_accessibility_force_enable_zoom))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (isTheZoomOnAllWbsitesToggleChecked) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        )
                    )
                )
            )
        Log.i(
            TAG,
            "verifyEnabledMenuItems: Verified that the \"Zoom on all websites\" toggle is enabled: $isTheZoomOnAllWbsitesToggleChecked",
        )
    }

    fun changeTextSizeSlider(seekBarPercentage: Int, composeTestRule: ComposeTestRule) =
        adjustTextSizeSlider(seekBarPercentage, composeTestRule)

    fun verifyTextSizePercentage(textSize: Int, composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyTextSizePercentage: Trying to verify that the text size percentage is set to: $textSize")
        composeTestRule.onNodeWithTag("fontSizeSliderValue").assertTextEquals("$textSize %")
        Log.i(TAG, "verifyTextSizePercentage: Verified that the text size percentage is set to: $textSize")
    }

    class Transition {
        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "goBack: Waited for device to be idle")
            Log.i(TAG, "goBack: Trying to click the navigate up toolbar button")
            goBackButton().perform(click())
            Log.i(TAG, "goBack: Clicked the navigate up toolbar button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }
    }
}

private fun toggleFontSizingSwitch() {
    Log.i(TAG, "toggleFontSizingSwitch: Trying to click the \"Automatic font sizing\" toggle")
    // Toggle font size to off
    onView(withText("Automatic font sizing"))
        .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        .perform(click())
    Log.i(TAG, "toggleFontSizingSwitch: Clicked the \"Automatic font sizing\" toggle")
}

private fun adjustTextSizeSlider(seekBarPercentage: Int, composeTestRule: ComposeTestRule) {
    Log.i(TAG, "adjustTextSizeSlider: Trying to set the seek bar value to: $seekBarPercentage")
    composeTestRule.onNodeWithTag("fontSizeSlider").performSemanticsAction(SemanticsActions.SetProgress) {
        it(seekBarPercentage.toFloat()).let {}
    }
    Log.i(TAG, "adjustTextSizeSlider: Seek bar value was set to: $seekBarPercentage")
}

private fun goBackButton() = onView(allOf(withContentDescription("Navigate up")))

fun calculateStepSizeFromPercentage(textSizePercentage: Int): Int {
    return ((textSizePercentage - MIN_VALUE) / STEP_SIZE)
}

fun checkTextSizeOnWebsite(textSizePercentage: Int, components: Components) {
    Log.i(TAG, "checkTextSizeOnWebsite: Trying to verify that text size on website is: $textSizePercentage")
    // Checks the Gecko engine settings for the font size
    val textSize = calculateStepSizeFromPercentage(textSizePercentage)
    val newTextScale = ((textSize * STEP_SIZE) + MIN_VALUE).toFloat() / DECIMAL_CONVERSION
    assertTrue(
        "$TAG: text size on website was not set to: $textSizePercentage",
        components.core.engine.settings.fontSizeFactor == newTextScale,
    )
    Log.i(TAG, "checkTextSizeOnWebsite: Verified that text size on website is: $textSizePercentage")
}
