/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MenuGridItemTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val events = mutableListOf<MenuEvent>()

    @Test
    fun `GIVEN an item with a click event WHEN clicking it THEN dispatch that event`() {
        setItem(onClickEvent = Clicked)

        composeTestRule.onNodeWithText(TITLE).assertHasClickAction().performClick()

        assertEquals(listOf<MenuEvent>(Clicked), events)
    }

    @Test
    fun `GIVEN an item with a click event THEN announce it as a button`() {
        setItem(onClickEvent = Clicked)

        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
    }

    @Test
    fun `GIVEN an item with a long click event WHEN long clicking it THEN dispatch that event`() {
        setItem(onClickEvent = Clicked, onLongClickEvent = LongClicked)

        composeTestRule.onNodeWithText(TITLE).performTouchInput { longClick() }

        assertEquals(listOf<MenuEvent>(LongClicked), events)
    }

    @Test
    fun `GIVEN an item with only a long click event THEN announce it as a button`() {
        setItem(onClickEvent = null, onLongClickEvent = LongClicked)

        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
    }

    @Test
    fun `GIVEN an item with only a long click event WHEN clicking it THEN dispatch nothing`() {
        setItem(onClickEvent = null, onLongClickEvent = LongClicked)

        composeTestRule.onNodeWithText(TITLE).performClick()

        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN an item with only a long click event WHEN long clicking it THEN dispatch that event`() {
        setItem(onClickEvent = null, onLongClickEvent = LongClicked)

        composeTestRule.onNodeWithText(TITLE).performTouchInput { longClick() }

        assertEquals(listOf<MenuEvent>(LongClicked), events)
    }

    @Test
    fun `GIVEN an item without click or long click events THEN it cannot be clicked and is not announced as a button`() {
        setItem(onClickEvent = null)

        composeTestRule
            .onNodeWithText(TITLE)
            .assertHasNoClickAction()
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.Role))
    }

    @Test
    fun `GIVEN an item without click or long click events WHEN clicking it THEN dispatch nothing`() {
        setItem(onClickEvent = null)

        composeTestRule.onNodeWithText(TITLE).performClick()

        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN a disabled item with a click event WHEN clicking it THEN dispatch nothing`() {
        setItem(onClickEvent = Clicked, state = MenuItemState.DISABLED)

        composeTestRule.onNodeWithText(TITLE).performClick()

        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN a disabled item with a long click event WHEN long clicking it THEN dispatch nothing`() {
        setItem(onClickEvent = Clicked, onLongClickEvent = LongClicked, state = MenuItemState.DISABLED)

        composeTestRule.onNodeWithText(TITLE).performTouchInput { longClick() }

        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN an active item with a click event WHEN clicking it THEN dispatch that event`() {
        setItem(onClickEvent = Clicked, state = MenuItemState.ACTIVE)

        composeTestRule.onNodeWithText(TITLE).assertHasClickAction().performClick()

        assertEquals(listOf<MenuEvent>(Clicked), events)
    }

    @Test
    fun `GIVEN an active item THEN icon container color is surfaceContainerLowest`() {
        composeTestRule.setContent {
            AcornTheme {
                assertEquals(
                    MaterialTheme.colorScheme.surfaceContainerLowest,
                    MenuItemState.ACTIVE.iconContainerColor,
                )
                assertEquals(
                    Color.Transparent,
                    MenuItemState.DEFAULT.iconContainerColor,
                )
                assertEquals(
                    Color.Transparent,
                    MenuItemState.DISABLED.iconContainerColor,
                )
                assertEquals(
                    Color.Transparent,
                    MenuItemState.WARNING.iconContainerColor,
                )
            }
        }
    }

    private fun setItem(
        onClickEvent: MenuEvent?,
        onLongClickEvent: MenuEvent? = null,
        state: MenuItemState = MenuItemState.DEFAULT,
    ) {
        composeTestRule.setContent {
            AcornTheme {
                MenuGridItem(
                    title = Text.String(TITLE),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                    onClickEvent = onClickEvent,
                    onInteraction = { events += it },
                    contentDescription = null,
                    state = state,
                    onLongClickEvent = onLongClickEvent,
                )
            }
        }
    }

    private data object Clicked : MenuEvent

    private data object LongClicked : MenuEvent

    private companion object {
        const val TITLE = "Item"
    }
}
