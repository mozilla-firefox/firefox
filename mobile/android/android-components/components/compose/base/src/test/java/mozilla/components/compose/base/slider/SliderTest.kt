/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.slider

import androidx.compose.material3.rememberSliderState
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.AcornTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

private const val SLIDER_TAG = "slider"

@RunWith(AndroidJUnit4::class)
class SliderTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val changes = mutableListOf<Float>()
    private var finishedCount = 0

    private fun setContent(value: Float, start: Float = 0f, end: Float = 1f, isEnabled: Boolean = true) {
        composeTestRule.setContent {
            AcornTheme {
                Slider(
                    state = rememberSliderState(value = value, trackRange = start..end),
                    onValueChange = { changes.add(it) },
                    modifier = Modifier.testTag(SLIDER_TAG),
                    isEnabled = isEnabled,
                    onValueChangeFinished = { finishedCount++ },
                )
            }
        }
    }

    private fun dragAcross() {
        composeTestRule.onNodeWithTag(SLIDER_TAG).performTouchInput {
            down(centerLeft)
            moveTo(center)
            moveTo(centerRight)
            up()
        }
    }

    @Test
    fun `WHEN shown THEN the slider reports its value within its range`() {
        setContent(value = 12f, start = 10f, end = 20f)

        val range =
            composeTestRule
                .onNodeWithTag(SLIDER_TAG)
                .fetchSemanticsNode()
                .config[SemanticsProperties.ProgressBarRangeInfo]

        assertEquals(12f, range.current, 0f)
        assertEquals(10f..20f, range.range)
    }

    @Test
    fun `WHEN the thumb is dragged to the end THEN the values stay in range and the drag finishes once`() {
        setContent(value = 12f, start = 10f, end = 20f)

        dragAcross()

        assertTrue("the drag never reached the slider", changes.isNotEmpty())
        assertTrue("a value fell outside the range: $changes", changes.all { it in 10f..20f })
        assertEquals(20f, changes.last(), 0.1f)
        assertEquals(1, finishedCount)
    }

    @Test
    fun `WHEN the slider is disabled THEN dragging it changes nothing`() {
        setContent(value = 0.5f, isEnabled = false)

        dragAcross()

        composeTestRule.onNodeWithTag(SLIDER_TAG).assertIsNotEnabled()
        assertTrue(changes.isEmpty())
        assertEquals(0, finishedCount)
    }
}
