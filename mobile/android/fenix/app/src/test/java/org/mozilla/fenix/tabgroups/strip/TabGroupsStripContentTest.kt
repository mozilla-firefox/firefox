/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.utils.LocalUnderTest
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabgroups.TabGroupsTestTag
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripState
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class TabGroupsStripContentTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val itemSize = 32.dp
    private val itemSpacing = 24.dp
    private val edgePadding = 8.dp
    private val expandButtonWidth = 24.dp

    private val state =
        TabGroupsStripState.TabGroupStripVisible(
            groupId = "group",
            groupTitle = "Group",
            tabs =
                listOf("1", "2", "3").map { id ->
                    TabGroupsStripState.Tab(
                        id = id,
                        url = "https://example.com/$id",
                        title = "Tab $id",
                        isFocused = id == "1",
                    )
                },
        )

    @Test
    fun `WHEN rendered THEN a favicon circle is shown for each tab`() {
        setContent {
            TabGroupsStripContent(state = state, onTabClick = {}, onAddTabClick = {}, onExpandGroupClick = {})
        }

        composeTestRule.onAllNodesWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_TAB).assertCountEquals(state.tabs.size)
    }

    @Test
    fun `WHEN a favicon circle is tapped THEN onTabClick is invoked with its tab id`() {
        var clickedId: String? = null
        setContent {
            TabGroupsStripContent(
                state = state,
                onTabClick = { clickedId = it },
                onAddTabClick = {},
                onExpandGroupClick = {},
            )
        }

        composeTestRule.onAllNodesWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_TAB).onFirst().performClick()

        assertEquals("1", clickedId)
    }

    @Test
    fun `WHEN rendered THEN the add tab and expand buttons are shown`() {
        setContent {
            TabGroupsStripContent(state = state, onTabClick = {}, onAddTabClick = {}, onExpandGroupClick = {})
        }

        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_ADD_BUTTON).assertExists()
        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_EXPAND_BUTTON).assertExists()
    }

    @Test
    fun `WHEN the add tab button is tapped THEN onAddTabClick is invoked`() {
        var addClicked = false
        setContent {
            TabGroupsStripContent(
                state = state,
                onTabClick = {},
                onAddTabClick = { addClicked = true },
                onExpandGroupClick = {},
            )
        }

        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_ADD_BUTTON).performClick()

        assertTrue(addClicked)
    }

    @Test
    fun `WHEN the expand button is tapped THEN onExpandGroupClick is invoked`() {
        var expandClicked = false
        setContent {
            TabGroupsStripContent(
                state = state,
                onTabClick = {},
                onAddTabClick = {},
                onExpandGroupClick = { expandClicked = true },
            )
        }

        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_EXPAND_BUTTON).performClick()

        assertTrue(expandClicked)
    }

    @Test
    fun `WHEN there are enough tabs to overflow THEN the add tab and expand buttons are still shown`() {
        val overflowingTabCount = 15
        val overflowingState =
            TabGroupsStripState.TabGroupStripVisible(
                groupId = "group",
                groupTitle = "Group",
                tabs =
                    List(overflowingTabCount) { index ->
                        TabGroupsStripState.Tab(
                            id = "$index",
                            url = "https://example.com/$index",
                            title = "Tab $index",
                            isFocused = index == 0,
                        )
                    },
            )

        setContent {
            TabGroupsStripContent(
                state = overflowingState,
                onTabClick = {},
                onAddTabClick = {},
                onExpandGroupClick = {},
            )
        }

        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_ADD_BUTTON).assertExists()
        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP_EXPAND_BUTTON).assertExists()
    }

    @Test
    fun `WHEN there are no tabs THEN the strip does not overflow`() {
        assertFalse(
            areTabsOverflowing(
                tabCount = 0,
                availableWidth = 400.dp,
                itemSize = itemSize,
                itemSpacing = itemSpacing,
                edgePadding = edgePadding,
                expandButtonWidth = expandButtonWidth,
            )
        )
    }

    @Test
    fun `WHEN the tabs fit within the available width THEN the strip does not overflow`() {
        assertFalse(
            areTabsOverflowing(
                tabCount = 3,
                availableWidth = 400.dp,
                itemSize = itemSize,
                itemSpacing = itemSpacing,
                edgePadding = edgePadding,
                expandButtonWidth = expandButtonWidth,
            )
        )
    }

    @Test
    fun `WHEN the tabs exceed the available width THEN the strip overflows`() {
        assertTrue(
            areTabsOverflowing(
                tabCount = 20,
                availableWidth = 400.dp,
                itemSize = itemSize,
                itemSpacing = itemSpacing,
                edgePadding = edgePadding,
                expandButtonWidth = expandButtonWidth,
            )
        )
    }

    private fun setContent(content: @Composable () -> Unit) {
        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    content()
                }
            }
        }
    }
}
