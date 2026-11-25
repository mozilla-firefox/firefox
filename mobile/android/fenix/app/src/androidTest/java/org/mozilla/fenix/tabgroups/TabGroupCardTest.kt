/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.DeviceConfigurationOverride
import androidx.compose.ui.test.ForcedSize
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.draggable.ItemInteractionState
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import org.junit.Assert
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.LocalTabManagementFeatureHelper
import org.mozilla.fenix.tabstray.TabManagementFeatureHelper
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.ui.tabitems.AlphaKey
import org.mozilla.fenix.tabstray.ui.tabitems.ScaleKey
import org.mozilla.fenix.tabstray.ui.tabitems.TabsTrayItemClickHandler
import org.mozilla.fenix.tabstray.ui.tabitems.TabsTrayItemSelectionState
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class TabGroupCardTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val tabManagementFeatureHelper =
        object : TabManagementFeatureHelper {
            override val openingAnimationEnabled: Boolean = false
            override val tabGroupsDragAndDropEnabled: Boolean = false
            override val ungroupTabGroupEnabled: Boolean = true
            override val tabGroupsOnboardingEnabled: Boolean = false
            override val tabGroupsLiveReorderEnabled: Boolean = false
            override val tabGroupsStripEnabled: Boolean = false
            override val showTabGroupsInMenu: Boolean = false
        }

    @Test
    fun verifyUIElementsPresent() {
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest()
            }
        }
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_THUMBNAIL_FIRST,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_THUMBNAIL_SECOND,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_THUMBNAIL_THIRD,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_THUMBNAIL_FOURTH,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
    }

    @Test
    fun verifyClick() {
        var clicked = false
        var argumentReceived: String? = null
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    onClick = { arg ->
                        clicked = true
                        argumentReceived = arg
                    }
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).performClick()
        Assert.assertTrue(clicked)
        Assert.assertEquals("Test", argumentReceived)
    }

    @Test
    fun verifyLongClick() {
        var longClicked = false
        var argumentReceived: String? = null
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    onLongClick = { arg ->
                        longClicked = true
                        argumentReceived = arg
                    }
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).performTouchInput {
            longClick()
        }
        Assert.assertTrue(longClicked)
        Assert.assertEquals("Test", argumentReceived)
    }

    @Test
    fun verifyDelete() {
        var deleteClicked = false
        var argumentReceived: String? = null
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    onDeleteTabGroupClick = { arg ->
                        deleteClicked = true
                        argumentReceived = arg
                    }
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.DELETE_TAB_GROUP).performClick()
        Assert.assertTrue(deleteClicked)
        Assert.assertEquals("Test", argumentReceived)
    }

    @Test
    fun verifyMenuItems() {
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest()
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.EDIT_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.CLOSE_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.SHARE_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.UNGROUP_TAB_GROUP).assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.DELETE_TAB_GROUP).assertIsDisplayed()
    }

    @Test
    fun verifyUngroupNotDisplayedWhenFeatureDisabled() {
        val ungroupDisabledHelper =
            object : TabManagementFeatureHelper by tabManagementFeatureHelper {
                override val ungroupTabGroupEnabled: Boolean = false
            }

        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(featureHelper = ungroupDisabledHelper)
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.UNGROUP_TAB_GROUP).assertDoesNotExist()
    }

    @Test
    fun verifyThumbnailSizesSimilarOnSmallWindowPortrait() {
        composeTestRule.setContent {
            DeviceConfigurationOverride(DeviceConfigurationOverride.ForcedSize(DpSize(400.dp, 800.dp))) {
                FirefoxTheme {
                    ComposableUnderTest()
                }
            }
        }
        verifyThumbnailSizesSimilar()
    }

    @Test
    fun verifyThumbnailSizesSimilarOnSmallWindowLandscape() {
        composeTestRule.setContent {
            DeviceConfigurationOverride(DeviceConfigurationOverride.ForcedSize(DpSize(800.dp, 400.dp))) {
                FirefoxTheme {
                    ComposableUnderTest()
                }
            }
        }
        verifyThumbnailSizesSimilar()
    }

    @Test
    fun verifyThumbnailSizesSimilarOnLargeWindowPortrait() {
        composeTestRule.setContent {
            DeviceConfigurationOverride(DeviceConfigurationOverride.ForcedSize(DpSize(800.dp, 1280.dp))) {
                FirefoxTheme {
                    ComposableUnderTest()
                }
            }
        }
        verifyThumbnailSizesSimilar()
    }

    @Test
    fun verifyThumbnailSizesSimilarOnLargeWindowLandscape() {
        composeTestRule.setContent {
            DeviceConfigurationOverride(DeviceConfigurationOverride.ForcedSize(DpSize(1280.dp, 800.dp))) {
                FirefoxTheme {
                    ComposableUnderTest()
                }
            }
        }
        verifyThumbnailSizesSimilar()
    }

    @Test
    fun verifyEditTabGroupClick() {
        val group = createTabGroup()
        var editClicked = false
        var clickedGroup: TabsTrayItem.TabGroup? = null

        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    group = group,
                    onEditTabGroupClick = { arg ->
                        editClicked = true
                        clickedGroup = arg
                    },
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.EDIT_TAB_GROUP).performClick()

        assertTrue(editClicked)
        assertEquals(group, clickedGroup)
    }

    @Test
    fun verifyCloseTabGroupClick() {
        val group = createTabGroup()
        var closeClicked = false
        var clickedGroup: TabsTrayItem.TabGroup? = null

        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    group = group,
                    onCloseTabGroupClick = { arg ->
                        closeClicked = true
                        clickedGroup = arg
                    },
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.CLOSE_TAB_GROUP).performClick()

        assertTrue(closeClicked)
        assertEquals(group, clickedGroup)
    }

    @Test
    fun verifyUngroupTabGroupClick() {
        val group = createTabGroup()
        var ungroupClicked = false
        var clickedGroup: TabsTrayItem.TabGroup? = null

        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    group = group,
                    onUngroupTabGroupClick = { arg ->
                        ungroupClicked = true
                        clickedGroup = arg
                    },
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.UNGROUP_TAB_GROUP).performClick()

        assertTrue(ungroupClicked)
        assertEquals(group, clickedGroup)
    }

    private fun verifyThumbnailSizesSimilar() {
        val first =
            composeTestRule
                .onNodeWithTag(
                    testTag = TabsTrayTestTag.TAB_GROUP_THUMBNAIL_FIRST,
                    useUnmergedTree = true,
                )
                .fetchSemanticsNode()
                .size
        val second =
            composeTestRule
                .onNodeWithTag(
                    testTag = TabsTrayTestTag.TAB_GROUP_THUMBNAIL_SECOND,
                    useUnmergedTree = true,
                )
                .fetchSemanticsNode()
                .size
        val third =
            composeTestRule
                .onNodeWithTag(
                    testTag = TabsTrayTestTag.TAB_GROUP_THUMBNAIL_THIRD,
                    useUnmergedTree = true,
                )
                .fetchSemanticsNode()
                .size
        val fourth =
            composeTestRule
                .onNodeWithTag(
                    testTag = TabsTrayTestTag.TAB_GROUP_THUMBNAIL_FOURTH,
                    useUnmergedTree = true,
                )
                .fetchSemanticsNode()
                .size
        val thumbnails = listOf(first, second, third, fourth)
        val allowance = 10
        for (i in 1 until thumbnails.size) {
            assert(
                thumbnails[i].height in first.height - allowance..first.height + allowance,
                { "Height of thumbnail $i ${thumbnails[i].height} not within margin of error of ${first.height}" },
            )
            assert(
                thumbnails[i].width in first.width - allowance..first.width + allowance,
                { "Width of thumbnail $i ${thumbnails[i].width} not within margin of error of {${first.width}" },
            )
        }
    }

    @Test
    fun verifyDraggedItemScale() {
        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = true))
        }
        composeTestRule.mainClock.advanceTimeBy(50L)

        val draggedScale =
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[ScaleKey]

        assertEquals("Dragged item is scaled at 75%", 0.75f, draggedScale)
    }

    @Test
    fun verifyUndraggedItemScale() {
        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = false))
        }
        composeTestRule.mainClock.advanceTimeBy(50L)

        val undraggedScale =
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[ScaleKey]

        assertEquals("Dragged item is scaled at 100%", 1f, undraggedScale)
    }

    @Test
    fun verifyDraggedItemAlpha() {
        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = true))
        }
        composeTestRule.mainClock.advanceTimeBy(50L)

        val draggedAlpha =
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[AlphaKey]

        assertEquals("Dragged item opacity is 70%", 0.7f, draggedAlpha)
    }

    @Test
    fun verifyUndraggedItemAlpha() {
        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = false))
        }
        composeTestRule.mainClock.advanceTimeBy(50L)

        val undraggedAlpha =
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[AlphaKey]

        assertEquals("Undragged item opacity is 100%", 1f, undraggedAlpha)
    }

    @Test
    fun verifyMediaIndicatorVisible() {
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    group =
                        TabsTrayItem.TabGroup(
                            title = "Group 1",
                            theme = TabGroupTheme.Yellow,
                            tabs = mutableListOf(createTab(url = ABOUT_HOME_URL, isMediaActive = true)),
                        )
                )
            }
        }
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_ITEM_MEDIA_INDICATOR,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
    }

    @Test
    fun verifyMediaIndicatorNotVisible() {
        composeTestRule.setContent {
            FirefoxTheme {
                ComposableUnderTest(
                    group =
                        TabsTrayItem.TabGroup(
                            title = "Group 1",
                            theme = TabGroupTheme.Yellow,
                            tabs = mutableListOf(createTab(url = ABOUT_HOME_URL, isMediaActive = false)),
                        )
                )
            }
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_MEDIA_INDICATOR).assertDoesNotExist()
    }

    @Composable
    private fun ComposableUnderTest(
        modifier: Modifier = Modifier,
        group: TabsTrayItem.TabGroup =
            TabsTrayItem.TabGroup(
                title = "Group 1",
                theme = TabGroupTheme.Yellow,
                tabs = mutableListOf(createTab(url = ABOUT_HOME_URL)),
            ),
        onClick: (String) -> Unit = {},
        onLongClick: (String) -> Unit = {},
        interactionState: ItemInteractionState = ItemInteractionState(),
        onDeleteTabGroupClick: (String) -> Unit = {},
        onEditTabGroupClick: (TabsTrayItem.TabGroup) -> Unit = {},
        onCloseTabGroupClick: (TabsTrayItem.TabGroup) -> Unit = {},
        onShareTabGroupClick: (TabsTrayItem.TabGroup) -> Unit = {},
        onUngroupTabGroupClick: (TabsTrayItem.TabGroup) -> Unit = {},
        featureHelper: TabManagementFeatureHelper = tabManagementFeatureHelper,
    ) {
        CompositionLocalProvider(LocalTabManagementFeatureHelper provides featureHelper) {
            TabGroupCard(
                group = group,
                selectionState = TabsTrayItemSelectionState(),
                clickHandler =
                    TabsTrayItemClickHandler(
                        onClick = { onClick("Test") },
                        onLongClick = { onLongClick("Test") },
                        onCloseClick = {}, // Not implemented yet
                    ),
                interactionState = interactionState,
                modifier = modifier,
                onDeleteTabGroupClick = { onDeleteTabGroupClick("Test") },
                onEditTabGroupClick = { onEditTabGroupClick(group) },
                onCloseTabGroupClick = { onCloseTabGroupClick(group) },
                onShareTabGroupClick = { onShareTabGroupClick(group) },
                onUngroupTabGroupClick = { onUngroupTabGroupClick(group) },
            )
        }
    }
}
