/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import mozilla.components.compose.base.theme.Theme
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.TabsTrayTestTag.BOTTOM_SHEET_COLOR_LIST
import org.mozilla.fenix.tabstray.TabsTrayTestTag.EDIT_BOTTOM_SHEET_SAVE
import org.mozilla.fenix.tabstray.TabsTrayTestTag.GROUP_NAME
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.initializeTabGroupForm
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class EditTabGroupTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN a color is clicked THEN the color is emitted`() {
        var selectedColor: TabGroupTheme? = null
        composeTestRule.setContent {
            ComposableUnderTest(
                onColorClick = {
                    selectedColor = it
                }
            )
        }

        composeTestRule.onNodeWithTag("$BOTTOM_SHEET_COLOR_LIST.${TabGroupTheme.Green}").performClick()

        composeTestRule.runOnIdle {
            assertEquals(expected = TabGroupTheme.Green, actual = selectedColor)
        }
    }

    @Test
    fun `Verify all color items are placed`() {
        composeTestRule.setContent {
            ComposableUnderTest()
        }

        TabGroupTheme.entries.forEach { entry ->
            composeTestRule.onNodeWithTag("$BOTTOM_SHEET_COLOR_LIST.${entry.name}").assertIsDisplayed()
        }
    }

    @Test
    fun `WHEN group is created GIVEN blank name, unedited state, and nextGroupNumber 1 THEN name is default name Group 1`() {
        val initialState = fakeFormState(nextGroupNumber = 1)

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText("Group 1"))
    }

    @Test
    fun `WHEN group is created GIVEN blank name, unedited state, and nextGroupNumber 99 THEN name is default name Group 99`() {
        val initialState = fakeFormState(nextGroupNumber = 99)

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText("Group 99"))
    }

    @Test
    fun `WHEN group is created GIVEN blank name and edited state THEN name is not overridden with default`() {
        val initialState =
            TabGroupFormState(
                tabGroupId = "123",
                name = "",
                nextTabGroupNumber = 1,
                theme = TabGroupTheme.Yellow,
                edited = true,
            )

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText(""))
    }

    @Test
    fun `WHEN group is created GIVEN non-blank name and edited state THEN name is not overridden with default`() {
        val initialState =
            TabGroupFormState(
                tabGroupId = "123",
                name = "Test Group",
                nextTabGroupNumber = 1,
                theme = TabGroupTheme.Yellow,
                edited = true,
            )

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText("Test Group"))
    }

    @Test
    fun `WHEN group is created GIVEN non-blank name and unedited state THEN name is not overridden with default`() {
        val initialState =
            TabGroupFormState(
                tabGroupId = "123",
                name = "Test Group",
                nextTabGroupNumber = 1,
                theme = TabGroupTheme.Yellow,
                edited = false,
            )

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText("Test Group"))
    }

    @Test
    fun `WHEN the store is in create mode THEN the default name is shown and saved in form state`() {
        val expectedName = "Group 1"

        composeTestRule.setContent {
            ComposableUnderTest(
                initialState =
                    TabGroupFormState(
                        tabGroupId = null,
                        name = "",
                        nextTabGroupNumber = 1,
                        edited = false,
                    )
            )
        }

        composeTestRule.onNodeWithText(expectedName).assertIsDisplayed()
    }

    @Test
    fun `WHEN the UI is opened to edit an existing tab group THEN the group's name is shown`() {
        val expectedName = "test group"
        val group = createTabGroup(title = expectedName)
        val initialState = group.initializeTabGroupForm()

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithText(expectedName).assertIsDisplayed()
    }

    @Test
    fun `WHEN the UI is opened to edit an existing tab group THEN the header text displays EDIT_GROUP`() {
        val expectedName = "test group"
        val group = createTabGroup(title = expectedName)
        val initialState = group.initializeTabGroupForm()

        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        composeTestRule.onNodeWithText("Edit group").assertIsDisplayed()
    }

    @Test
    fun `WHEN group name is changed GIVEN name length exceeds MAX_TAB_GROUP_NAME_LENGTH THEN name is truncated`() {
        val initialName = "Test Group"
        val initialState =
            TabGroupFormState(
                tabGroupId = "123",
                name = initialName,
                nextTabGroupNumber = 1,
                theme = TabGroupTheme.Yellow,
                edited = true,
            )
        composeTestRule.setContent {
            ComposableUnderTest(initialState = initialState)
        }

        val expectedTruncatedName = "a".repeat(MAX_TAB_GROUP_NAME_LENGTH)
        val longName = expectedTruncatedName + "extra"

        composeTestRule.onNodeWithTag(GROUP_NAME).performTextReplacement(longName)

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText(expectedTruncatedName))
    }

    @Test
    fun `WHEN group name is changed GIVEN name length is exactly MAX_TAB_GROUP_NAME_LENGTH THEN name is updated`() {
        composeTestRule.setContent {
            ComposableUnderTest()
        }

        val maxName = "a".repeat(MAX_TAB_GROUP_NAME_LENGTH)
        composeTestRule.onNodeWithTag(GROUP_NAME).performTextReplacement(maxName)

        composeTestRule.onNodeWithTag(GROUP_NAME).assert(hasText(maxName))
    }

    @Test
    fun `WHEN the group is saved THEN the action is emitted`() {
        var wasSaveEmitted = false

        composeTestRule.setContent {
            ComposableUnderTest(onSaveClick = { wasSaveEmitted = true })
        }

        composeTestRule.onNodeWithTag(EDIT_BOTTOM_SHEET_SAVE).performClick()

        composeTestRule.runOnIdle {
            assertTrue(wasSaveEmitted)
        }
    }

    @Composable
    private fun ComposableUnderTest(
        initialState: TabGroupFormState = fakeFormState(),
        onColorClick: (TabGroupTheme) -> Unit = {},
        onSaveClick: () -> Unit = {},
    ) {
        var state by remember { mutableStateOf(initialState) }
        FirefoxTheme(theme = Theme.Light) {
            Surface {
                EditTabGroup(
                    formState = state,
                    onTabGroupNameChange = { newName ->
                        state = state.copy(name = newName)
                    },
                    onTabGroupThemeChange = { newTheme ->
                        onColorClick(newTheme)
                        state = state.copy(theme = newTheme)
                    },
                    onConfirmSave = {
                        onSaveClick()
                    },
                )
            }
        }
    }

    private fun fakeFormState(nextGroupNumber: Int = 1): TabGroupFormState {
        return TabGroupFormState(
            tabGroupId = "123",
            name = "",
            nextTabGroupNumber = nextGroupNumber,
            theme = TabGroupTheme.Yellow,
            edited = false,
        )
    }
}
