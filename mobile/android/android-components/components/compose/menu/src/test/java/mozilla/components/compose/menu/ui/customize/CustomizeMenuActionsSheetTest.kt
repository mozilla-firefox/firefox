/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomizeMenuActionsSheetTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val chosen = mutableListOf<Pair<Unit, String>>()
    private var dismissed = 0

    @Test
    fun `GIVEN no actions WHEN the sheet is shown THEN no row is shown AND the app is asked to dismiss`() {
        setContent(null)

        composeTestRule.onAllNodes(hasClickAction()).assertCountEquals(0)
        assertEquals(1, dismissed)
    }

    @Test
    fun `GIVEN the sheet is shown WHEN the actions become null without a choice THEN the app is asked to dismiss`() {
        val actions = setContent(listOf(action("First"), action("Second")))
        composeTestRule.onNodeWithText("First").assertIsDisplayed()

        actions.value = null
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithText("First").assertDoesNotExist()
        assertEquals(1, dismissed)
    }

    @Test
    fun `WHEN an action is chosen and the actions become null while the sheet hides THEN the rows stay shown, onAction ran once AND the app is asked to dismiss once`() {
        val actions = setContent(listOf(action("First"), action("Second")))
        composeTestRule.mainClock.autoAdvance = false

        composeTestRule.onNodeWithText("First").performClick()
        actions.value = null
        composeTestRule.mainClock.advanceTimeByFrame()

        composeTestRule.onNodeWithText("First").assertIsDisplayed()
        composeTestRule.onNodeWithText("Second").assertIsDisplayed()
        assertEquals(listOf(Unit to "First done"), chosen)
        assertEquals(0, dismissed)

        composeTestRule.mainClock.autoAdvance = true
        composeTestRule.waitForIdle()

        assertEquals(1, dismissed)
    }

    @Test
    fun `WHEN an action is chosen, the actions become null and another row is clicked while the sheet hides THEN onAction ran once`() {
        val actions = setContent(listOf(action("First"), action("Second")))
        composeTestRule.mainClock.autoAdvance = false

        composeTestRule.onNodeWithText("First").performClick()
        actions.value = null
        composeTestRule.mainClock.advanceTimeByFrame()
        composeTestRule.onNodeWithText("Second").performClick()
        composeTestRule.mainClock.autoAdvance = true
        composeTestRule.waitForIdle()

        assertEquals(listOf(Unit to "First done"), chosen)
    }

    private fun setContent(
        initialActions: List<CustomizeMenuAction<Unit>>?
    ): MutableState<List<CustomizeMenuAction<Unit>>?> {
        val actions = mutableStateOf(initialActions)
        composeTestRule.setContent {
            AcornTheme {
                CustomizeMenuActionsSheet(
                    actions = actions.value,
                    onAction = { result, announcement -> chosen += result to announcement },
                    onDismissRequest = { dismissed++ },
                )
            }
        }
        composeTestRule.waitForIdle()
        return actions
    }

    private fun action(
        label: String,
        result: Unit? = Unit,
        announcement: String = "$label done",
    ) =
        CustomizeMenuAction(
            label = label,
            icon = iconsR.drawable.mozac_ic_chevron_up_24,
            result = result,
            announcement = announcement,
        )
}
