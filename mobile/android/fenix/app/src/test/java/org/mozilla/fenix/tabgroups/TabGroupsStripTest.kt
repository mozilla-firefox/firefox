/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.utils.LocalUnderTest
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripState
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripStore
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class TabGroupsStripTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN the active tab is in a group THEN the strip is shown`() {
        setContent(groupId = "1")

        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP).assertExists()
    }

    @Test
    fun `WHEN the active tab is not in a group THEN the strip is hidden`() {
        setContent(groupId = null)

        composeTestRule.onNodeWithTag(TabGroupsTestTag.TAB_GROUPS_STRIP).assertDoesNotExist()
    }

    private fun setContent(groupId: String?) {
        val state: TabGroupsStripState =
            if (groupId == null) {
                TabGroupsStripState.TabGroupStripHidden
            } else {
                TabGroupsStripState.TabGroupStripVisible(
                    groupId = groupId,
                    groupTitle = "Group",
                    tabs =
                        listOf(
                            TabGroupsStripState.Tab(
                                id = "1",
                                url = "https://mozilla.org",
                                title = "Mozilla",
                                isFocused = true,
                            )
                        ),
                )
            }

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    TabGroupsStrip(store = TabGroupsStripStore(initialState = state))
                }
            }
        }
    }
}
