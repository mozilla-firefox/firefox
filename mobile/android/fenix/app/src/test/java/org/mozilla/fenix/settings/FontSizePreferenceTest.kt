/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.percentOffset
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class FontSizePreferenceTest {

    @get:Rule val composeTestRule = createComposeRule()

    private var value by mutableFloatStateOf(100f)
    private val reported = mutableListOf<Float>()
    private var finished = 0

    private fun setContent() = composeTestRule.setContent {
        FontSizePreference(
            isEnabled = true,
            value = value,
            onValueChange = {
                reported.add(it)
                value = it
            },
            onValueChangeFinished = { finished++ },
        )
    }

    private fun slider() = composeTestRule.onNodeWithTag("fontSizeSlider")

    private fun sliderPosition() =
        slider().fetchSemanticsNode().config[SemanticsProperties.ProgressBarRangeInfo].current

    @Test
    fun `WHEN the slider moves THEN the nearest step is reported, the change finishes and the slider follows it`() {
        setContent()

        listOf(152f, 175f).forEach { target ->
            slider().performSemanticsAction(SemanticsActions.SetProgress) { it(target) }
        }

        assertEquals(listOf(150f, 175f), reported)
        assertEquals(2, finished)
        assertEquals(175f, sliderPosition(), 0.01f)
    }

    @Test
    fun `WHEN the thumb is dragged THEN every reported size is a step, the change finishes once and the slider follows it`() {
        setContent()

        slider().performTouchInput {
            down(center)
            moveTo(percentOffset(0.7f, 0.5f))
            moveTo(percentOffset(0.9f, 0.5f))
            up()
        }
        composeTestRule.waitForIdle()

        assertTrue("$reported", reported.all { it % 5f == 0f })
        assertTrue("$reported", reported.last() > 150f)
        assertEquals(1, finished)
        assertEquals(reported.last(), sliderPosition(), 0.01f)
    }
}
