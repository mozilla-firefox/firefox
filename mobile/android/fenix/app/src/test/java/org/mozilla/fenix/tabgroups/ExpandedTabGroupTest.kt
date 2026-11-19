/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.material3.Surface
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import junit.framework.TestCase.assertTrue
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.utils.LocalUnderTest
import mozilla.components.compose.draggable.NoOpItemInteractionHandler
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.LocalTabManagementFeatureHelper
import org.mozilla.fenix.tabstray.TabManagementFeatureHelper
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.telemetry.TabCloseType
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class ExpandedTabGroupTest {
    @get:Rule val composeTestRule = createComposeRule()
    val testGroupTitle = "Test Tab Group"

    private val tabManagementFeatureHelper =
        object : TabManagementFeatureHelper {
            override val openingAnimationEnabled: Boolean = false
            override val tabGroupsEnabled: Boolean = true
            override val tabGroupsDragAndDropEnabled: Boolean = false
            override val ungroupTabGroupEnabled: Boolean = true
            override val tabGroupsOnboardingEnabled: Boolean = false
            override val tabGroupsLiveReorderEnabled: Boolean = false
            override val tabGroupsStripEnabled: Boolean = false
            override val showTabGroupsInMenu: Boolean = false
        }

    @Test
    fun verifyVisibleItems() {
        composeTestRule.setContent {
            CompositionLocalProvider(LocalTabManagementFeatureHelper provides tabManagementFeatureHelper) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(),
                            actions = expandedTabGroupActions(),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_BOTTOM_SHEET_ROOT).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.BOTTOM_SHEET_ADD_TAB_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.BOTTOM_SHEET_CIRCLE,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
    }

    @Test
    fun verifyMenuItems() {
        composeTestRule.setContent {
            CompositionLocalProvider(LocalTabManagementFeatureHelper provides tabManagementFeatureHelper) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(),
                            actions = expandedTabGroupActions(),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.EDIT_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.CLOSE_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.UNGROUP_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.DELETE_TAB_GROUP).assertIsDisplayed()
    }

    @Test
    fun verifyGridView() {
        val tab = createTab(url = "test tab")

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(tabs = mutableListOf(tab)),
                            actions = expandedTabGroupActions(),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GRID).assertIsDisplayed()
    }

    @Test
    fun verifyListView() {
        val tab = createTab(url = "test tab")

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(tabs = mutableListOf(tab)),
                            actions = expandedTabGroupActions(),
                            displayTabsInGrid = false,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_LIST).assertIsDisplayed()
    }

    @Test
    fun verifyTabGroupItemClick() {
        var itemClicked = false
        val tab = createTab(url = "test tab")

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(tabs = mutableListOf(tab)),
                            actions = expandedTabGroupActions(onItemClick = { if (it == tab) itemClicked = true }),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).performClick()

        assertTrue(itemClicked)
    }

    @Test
    fun verifyTabGroupItemCloseClick() {
        var itemClosed = false
        val tab = createTab(url = "test tab")

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(tabs = mutableListOf(tab)),
                            actions =
                                expandedTabGroupActions(
                                    onTabClose = { closedTab, _ ->
                                        if (closedTab == tab) itemClosed = true
                                    }
                                ),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_CLOSE).performClick()

        assertTrue(itemClosed)
    }

    @Test
    fun verifyDeleteTabGroupClick() {
        var deleteClicked = false
        val group = fakeTabGroup()

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = group,
                            actions = expandedTabGroupActions(onDeleteTabGroupClick = { deleteClicked = true }),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.DELETE_TAB_GROUP).performClick()

        assertTrue(deleteClicked)
    }

    @Test
    fun verifyEditTabGroupClick() {
        var editClicked = false

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(),
                            actions = expandedTabGroupActions(onEditTabGroupClick = { editClicked = true }),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.EDIT_TAB_GROUP).performClick()

        assertTrue(editClicked)
    }

    @Test
    fun verifyCloseTabGroupClick() {
        var closeClicked = false

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(),
                            actions = expandedTabGroupActions(onCloseTabGroupClick = { closeClicked = true }),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.CLOSE_TAB_GROUP).performClick()

        assertTrue(closeClicked)
    }

    @Test
    fun verifyUngroupTabGroupClick() {
        var ungroupClicked = false

        composeTestRule.setContent {
            CompositionLocalProvider(LocalTabManagementFeatureHelper provides tabManagementFeatureHelper) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(),
                            actions = expandedTabGroupActions(onUngroupTabGroupClick = { ungroupClicked = true }),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.UNGROUP_TAB_GROUP).performClick()

        assertTrue(ungroupClicked)
    }

    @Test
    fun verifyAddNewTabClick() {
        var addNewTabClicked = false

        composeTestRule.setContent {
            CompositionLocalProvider(LocalUnderTest provides true) {
                FirefoxTheme(theme = Theme.Light) {
                    Surface {
                        ExpandedTabGroup(
                            group = fakeTabGroup(),
                            actions = expandedTabGroupActions(onAddNewTabClick = { addNewTabClicked = true }),
                            displayTabsInGrid = true,
                            itemInteractionHandler = NoOpItemInteractionHandler,
                        )
                    }
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.BOTTOM_SHEET_ADD_TAB_BUTTON).performClick()

        assertTrue(addNewTabClicked)
    }

    private fun fakeTabGroup(tabs: MutableList<TabsTrayItem.Tab> = mutableListOf()): TabsTrayItem.TabGroup {
        return createTabGroup(
            title = testGroupTitle,
            tabs = tabs,
        )
    }

    private fun expandedTabGroupActions(
        onItemClick: (TabsTrayItem) -> Unit = {},
        onTabClose: (TabsTrayItem.Tab, TabCloseType) -> Unit = { _, _ -> },
        onDeleteTabGroupClick: () -> Unit = {},
        onEditTabGroupClick: () -> Unit = {},
        onCloseTabGroupClick: () -> Unit = {},
        onUngroupTabGroupClick: () -> Unit = {},
        onAddNewTabClick: (() -> Unit)? = {},
        onShareTabGroupClick: () -> Unit = {},
    ) =
        ExpandedTabGroupActions(
            onItemClick = onItemClick,
            onTabClose = onTabClose,
            onDeleteTabGroupClick = onDeleteTabGroupClick,
            onEditTabGroupClick = onEditTabGroupClick,
            onCloseTabGroupClick = onCloseTabGroupClick,
            onUngroupTabGroupClick = onUngroupTabGroupClick,
            onAddNewTabClick = onAddNewTabClick,
            onShareTabGroupClick = onShareTabGroupClick,
        )
}
