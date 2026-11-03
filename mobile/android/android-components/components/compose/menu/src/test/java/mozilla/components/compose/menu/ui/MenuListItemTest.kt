/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.store.MenuEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MenuListItemTest {
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
    fun `GIVEN an item without a click event THEN it cannot be clicked and is not announced as a button`() {
        setItem(onClickEvent = null)

        composeTestRule
            .onNodeWithText(TITLE)
            .assertHasNoClickAction()
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.Role))
    }

    @Test
    fun `GIVEN an item without a click event WHEN clicking it THEN dispatch nothing`() {
        setItem(onClickEvent = null)

        composeTestRule.onNodeWithText(TITLE).performClick()

        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN a disabled item with a click event WHEN clicking it THEN dispatch nothing`() {
        setItem(onClickEvent = Clicked, state = MenuItemState.DISABLED)

        composeTestRule.onNodeWithText(TITLE).assertHasNoClickAction().performClick()

        assertTrue(events.isEmpty())
    }

    @Test
    fun `GIVEN an enabled item WHEN reading it THEN announce it as enabled`() {
        setItem(onClickEvent = Clicked)

        composeTestRule.onNodeWithText(TITLE).assertIsEnabled()
    }

    @Test
    fun `GIVEN a disabled item WHEN reading it THEN announce it as disabled`() {
        setItem(onClickEvent = Clicked, state = MenuItemState.DISABLED)

        composeTestRule.onNodeWithText(TITLE).assertIsNotEnabled()
    }

    @Test
    fun `GIVEN a disabled switch WHEN reading it THEN announce it as a disabled switch`() {
        setItem(onClickEvent = Clicked, role = Role.Switch, state = MenuItemState.DISABLED)

        composeTestRule
            .onNodeWithText(TITLE)
            .assertIsNotEnabled()
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Switch))
    }

    @Test
    fun `GIVEN a switch with a badge THEN announce the badge text as its state and only once`() {
        setItem(onClickEvent = Clicked, role = Role.Switch, badge = MenuItemBadge(Text.String(STATE_ON)))

        composeTestRule.onNodeWithText(STATE_ON).assertDoesNotExist()
        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, STATE_ON))
    }

    @Test
    fun `GIVEN a switch with a badge WHEN the badge text changes THEN announce the new state`() {
        val badgeText = mutableStateOf(STATE_ON)
        composeTestRule.setContent {
            AcornTheme {
                MenuListItem(
                    title = MenuItemTitle(Text.String(TITLE)),
                    onClickEvent = Clicked,
                    onInteraction = { events += it },
                    contentDescription = null,
                    role = Role.Switch,
                    badge = MenuItemBadge(Text.String(badgeText.value)),
                )
            }
        }

        badgeText.value = STATE_OFF

        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, STATE_OFF))
    }

    @Test
    fun `GIVEN a button with a badge THEN announce the badge text and no state`() {
        setItem(onClickEvent = Clicked, badge = MenuItemBadge(Text.String(STATE_ON)))

        composeTestRule.onNodeWithText(STATE_ON).assertExists()
        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.StateDescription))
    }

    @Test
    fun `GIVEN a switch without a badge THEN announce no state`() {
        setItem(onClickEvent = Clicked, role = Role.Switch)

        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.StateDescription))
    }

    private fun setItem(
        onClickEvent: MenuEvent?,
        state: MenuItemState = MenuItemState.DEFAULT,
        role: Role = Role.Button,
        badge: MenuItemBadge? = null,
    ) {
        composeTestRule.setContent {
            AcornTheme {
                MenuListItem(
                    title = MenuItemTitle(Text.String(TITLE)),
                    onClickEvent = onClickEvent,
                    onInteraction = { events += it },
                    contentDescription = null,
                    role = role,
                    badge = badge,
                    state = state,
                )
            }
        }
    }

    private data object Clicked : MenuEvent

    private companion object {
        const val TITLE = "Item"
        const val STATE_ON = "On"
        const val STATE_OFF = "Off"
    }
}
