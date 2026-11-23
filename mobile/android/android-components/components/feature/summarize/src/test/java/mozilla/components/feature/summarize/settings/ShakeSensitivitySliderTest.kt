/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.percentOffset
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.lib.shake.ShakeSensitivity
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ShakeSensitivitySliderTest {

    @get:Rule val composeTestRule = createComposeRule()

    private var value by mutableStateOf(ShakeSensitivity.Low)
    private val reported = mutableListOf<ShakeSensitivity>()

    private fun setContent() = composeTestRule.setContent {
        ShakeSensitivityPreference(
            isEnabled = true,
            value = value,
            onValueChange = {
                reported.add(it)
                value = it
            },
        )
    }

    private fun slider() = composeTestRule.onNodeWithContentDescription("Shake sensitivity")

    private fun sliderPosition() =
        slider().fetchSemanticsNode().config[SemanticsProperties.ProgressBarRangeInfo].current

    @Test
    fun `WHEN the thumb is released between two sensitivities THEN the nearest is reported once and the thumb settles on it`() {
        setContent()

        slider().performTouchInput {
            down(centerLeft)
            moveTo(percentOffset(0.4f, 0.5f))
            up()
        }
        composeTestRule.waitForIdle()

        assertEquals(listOf(ShakeSensitivity.Medium), reported)
        assertEquals(1f, sliderPosition(), 0.01f)
    }

    @Test
    fun `WHEN the value changes outside the slider THEN the thumb moves to it without reporting it back`() {
        setContent()

        value = ShakeSensitivity.High
        composeTestRule.waitForIdle()

        assertEquals(2f, sliderPosition(), 0.01f)
        assertEquals(emptyList<ShakeSensitivity>(), reported)
    }

    @Test
    fun `WHEN the thumb is grabbed again while settling THEN it follows the finger instead of the animation`() {
        setContent()
        composeTestRule.mainClock.autoAdvance = false

        slider().performTouchInput {
            down(centerLeft)
            moveTo(percentOffset(0.4f, 0.5f))
            up()
        }
        composeTestRule.mainClock.advanceTimeBy(50)
        slider().performTouchInput {
            down(percentOffset(0.9f, 0.5f))
            moveTo(percentOffset(0.95f, 0.5f))
        }
        composeTestRule.mainClock.advanceTimeBy(1000)

        assertTrue("${sliderPosition()}", sliderPosition() > 1.5f)
    }
}
