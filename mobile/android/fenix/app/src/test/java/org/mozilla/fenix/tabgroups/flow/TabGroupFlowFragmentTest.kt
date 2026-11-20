/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.navigation3.scene.DialogSceneStrategy
import androidx.navigation3.ui.NavDisplay
import androidx.test.core.app.ApplicationProvider
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Rule
import org.junit.runner.RunWith
import org.mozilla.fenix.compose.navigation.BottomSheetSceneStrategy
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.MockkRetryTestRule
import org.mozilla.fenix.tabgroups.TabGroupsTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag.BOTTOM_SHEET_COLOR_LIST
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabGroupFlowFragmentTest {
    private lateinit var fragment: TabGroupFlowFragment

    @get:Rule val mockkRule = MockkRetryTestRule()

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @get:Rule val composeTestRule = createComposeRule()

    @Before
    fun setup() {
        fragment = TabGroupFlowFragment()
    }

    @Test
    fun `WHEN AddToTabGroup is the top destination, THEN AddToTabGroup composable is visible`() {
        val testBackStack =
            listOf(
                TabGroupFlowDestination.Root,
                TabGroupFlowDestination.AddToTabGroup(tabId = "123"),
            )
        val state = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = "123"))
        fragment.store =
            TabGroupFlowStore(
                initialState = state,
                middlewares = emptyList(),
            )
        composeTestRule.setContent {
            NavDisplay(
                backStack = testBackStack,
                onBack = {},
                sceneStrategies = listOf(BottomSheetSceneStrategy()),
                entryProvider =
                    fragment.getEntryProvider(
                        state = state,
                        windowSize = AcornWindowSize.Small,
                    ),
            )
        }
        composeTestRule.onNodeWithText("Add to").assertExists()
    }

    @Test
    fun `WHEN EditTabGroup is the top destination, THEN EditTabGroup composable is visible`() {
        val testBackStack =
            listOf(
                TabGroupFlowDestination.Root,
                TabGroupFlowDestination.EditTabGroup,
            )
        val state =
            TabGroupFlowState(
                formState =
                    TabGroupFormState(
                        tabGroupId = "123",
                        name = "Tab Group",
                    ),
                entryPoint = TabGroupFlowEntryPoint.EditGroup(groupId = "123"),
            )
        fragment.store =
            TabGroupFlowStore(
                initialState = state,
                middlewares = emptyList(),
            )
        composeTestRule.setContent {
            NavDisplay(
                backStack = testBackStack,
                onBack = {},
                sceneStrategies = listOf(BottomSheetSceneStrategy()),
                entryProvider =
                    fragment.getEntryProvider(
                        state = state,
                        windowSize = AcornWindowSize.Small,
                    ),
            )
        }
        TabGroupTheme.entries.forEach { entry ->
            composeTestRule.onNodeWithTag("$BOTTOM_SHEET_COLOR_LIST.${entry.name}").assertIsDisplayed()
        }
    }

    @Test
    fun `WHEN entry point arg is EditTabGroup, THEN backstack is instantiated correctly`() {
        val state =
            fragment.createInitialState(
                settings = Settings(appContext = ApplicationProvider.getApplicationContext()),
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.EditGroup(groupId = "123")),
            )

        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.EditTabGroup,
                ),
            actual = state.backStack,
        )
    }

    @Test
    fun `WHEN entry point arg is AddTabToGroup, THEN backstack is instantiated correctly`() {
        val tabId = "TestTabId"
        val state =
            fragment.createInitialState(
                settings = Settings(appContext = ApplicationProvider.getApplicationContext()),
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = tabId)),
            )

        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.AddToTabGroup(tabId = tabId),
                ),
            actual = state.backStack,
        )
    }

    @Test
    fun `WHEN entry point arg is MoveTabToGroup, THEN backstack is instantiated correctly`() {
        val tabId = "TestTabId"
        val entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = tabId)
        val state =
            fragment.createInitialState(
                settings = Settings(appContext = ApplicationProvider.getApplicationContext()),
                args = TabGroupFlowFragmentArgs(entryPoint = entryPoint),
            )

        assertEquals(
            expected =
                listOf(
                    TabGroupFlowDestination.Root,
                    TabGroupFlowDestination.MoveToTabGroup(tabId = tabId),
                ),
            actual = state.backStack,
        )
        assertEquals(entryPoint, state.entryPoint)
    }

    @Test
    fun `WHEN MoveToTabGroup is the top destination AND an existing group is picked, THEN the tab is moved to that group`() {
        val group = createTabGroup(id = "group-1", title = "Work")
        val state =
            TabGroupFlowState(
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                groups = listOf(group),
            )
        val captureMiddleware = CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction>()
        fragment.store =
            TabGroupFlowStore(
                initialState = state,
                middlewares = listOf(captureMiddleware),
            )
        composeTestRule.setContent {
            NavDisplay(
                backStack = listOf(TabGroupFlowDestination.Root, TabGroupFlowDestination.MoveToTabGroup(tabId = "123")),
                onBack = {},
                sceneStrategies = listOf(BottomSheetSceneStrategy()),
                entryProvider =
                    fragment.getEntryProvider(
                        state = state,
                        windowSize = AcornWindowSize.Small,
                    ),
            )
        }

        composeTestRule.onNodeWithText("Add to").assertExists()
        composeTestRule.onNodeWithTag("${TabsTrayTestTag.TAB_GROUP_ROOT}.${group.id}").performClick()

        captureMiddleware.assertLastAction(TabGroupAction.ExistingGroupSelectedForMove::class) {
            assertEquals(
                TabGroupAction.ExistingGroupSelectedForMove(tabId = "123", groupId = "group-1", groupTitle = "Work"),
                it,
            )
        }
        captureMiddleware.assertNotDispatched(TabGroupAction.TabAddedToExistingTabGroup::class)
    }

    @Test
    fun `GIVEN the tab is the last tab in its group WHEN an existing group is picked, THEN the selection is dispatched for the store to decide`() {
        val sourceGroup =
            createTabGroup(id = "source", title = "Errands", tabs = listOf(createTab("mozilla.org", id = "123")))
        val destinationGroup = createTabGroup(id = "destination", title = "Work")
        val captureMiddleware = renderMoveToTabGroup(tabId = "123", groups = listOf(sourceGroup, destinationGroup))

        composeTestRule.onNodeWithTag("${TabsTrayTestTag.TAB_GROUP_ROOT}.${destinationGroup.id}").performClick()

        captureMiddleware.assertLastAction(TabGroupAction.ExistingGroupSelectedForMove::class) {
            assertEquals(
                TabGroupAction.ExistingGroupSelectedForMove(
                    tabId = "123",
                    groupId = "destination",
                    groupTitle = "Work",
                ),
                it,
            )
        }
    }

    @Test
    fun `GIVEN the tab is the last tab in its group WHEN new group is picked, THEN TabAddedToNewTabGroup is dispatched`() {
        val sourceGroup =
            createTabGroup(id = "source", title = "Errands", tabs = listOf(createTab("mozilla.org", id = "123")))
        val captureMiddleware = renderMoveToTabGroup(tabId = "123", groups = listOf(sourceGroup))

        composeTestRule.onNodeWithTag(TabsTrayTestTag.ADD_TO_NEW_TAB_GROUP).performClick()

        captureMiddleware.assertLastAction(TabGroupAction.TabAddedToNewTabGroup::class) {
            assertEquals(TabGroupAction.TabAddedToNewTabGroup(tabId = "123"), it)
        }
    }

    fun `WHEN the move last tab dialog is confirmed, THEN MoveTabAndDeleteGroupConfirmed is dispatched with its args`() {
        val captureMiddleware = renderMoveLastTabDialog()

        composeTestRule.onNodeWithTag(TabGroupsTestTag.MOVE_LAST_TAB_AND_DELETE_DIALOG_CONFIRM_BUTTON).performClick()

        captureMiddleware.assertLastAction(TabGroupAction.MoveTabAndDeleteGroupConfirmed::class) {
            assertEquals(
                TabGroupAction.MoveTabAndDeleteGroupConfirmed(
                    sourceGroupId = "source",
                    destinationGroupId = "destination",
                    destinationGroupTitle = "Work",
                ),
                it,
            )
        }
    }

    @Test
    fun `WHEN the move last tab dialog is canceled, THEN MoveTabAndDeleteGroupCanceled is dispatched`() {
        val captureMiddleware = renderMoveLastTabDialog()

        composeTestRule.onNodeWithTag(TabGroupsTestTag.MOVE_LAST_TAB_AND_DELETE_DIALOG_CANCEL_BUTTON).performClick()

        captureMiddleware.assertLastAction(TabGroupAction.MoveTabAndDeleteGroupCanceled::class)
        captureMiddleware.assertNotDispatched(TabGroupAction.MoveTabAndDeleteGroupConfirmed::class)
    }

    @Test
    fun `WHEN fragment is instantiated, THEN form state is initially null`() {
        val tabId = "TestTabId"
        val state =
            fragment.createInitialState(
                settings = Settings(appContext = ApplicationProvider.getApplicationContext()),
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = tabId)),
            )

        assertNull(state.formState)
    }

    @Test
    fun `WHEN HNT is enabled in settings, THEN this is reflected in the fragment config`() {
        val tabId = "TestTabId"
        val settings = Settings(appContext = ApplicationProvider.getApplicationContext())
        settings.enableHomepageAsNewTab = true
        val state =
            fragment.createInitialState(
                settings = settings,
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = tabId)),
            )

        assertTrue(state.config.homepageAsNewTabEnabled)
    }

    @Test
    fun `WHEN HNT is disabled in settings, THEN this is reflected in the fragment config`() {
        val tabId = "TestTabId"
        val settings = Settings(appContext = ApplicationProvider.getApplicationContext())
        settings.enableHomepageAsNewTab = false
        val state =
            fragment.createInitialState(
                settings = settings,
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = tabId)),
            )

        assertFalse(state.config.homepageAsNewTabEnabled)
    }

    @Test
    fun `WHEN user's tabs is set to be in a grid in settings, THEN this is reflected in the fragment config`() {
        val tabId = "TestTabId"
        val settings = Settings(appContext = ApplicationProvider.getApplicationContext())
        settings.gridTabView = true
        val state =
            fragment.createInitialState(
                settings = settings,
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = tabId)),
            )

        assertTrue(state.config.displayTabsInGrid)
    }

    @Test
    fun `WHEN user's tabs is set to be in a list in settings, THEN this is reflected in the fragment config`() {
        val tabId = "TestTabId"
        val settings = Settings(appContext = ApplicationProvider.getApplicationContext())
        settings.gridTabView = false
        val state =
            fragment.createInitialState(
                settings = settings,
                args = TabGroupFlowFragmentArgs(entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = tabId)),
            )

        assertFalse(state.config.displayTabsInGrid)
    }

    private fun renderMoveToTabGroup(
        tabId: String,
        groups: List<TabsTrayItem.TabGroup>,
    ): CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction> =
        render(
            state =
                TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = tabId), groups = groups),
            top = TabGroupFlowDestination.MoveToTabGroup(tabId = tabId),
        )

    private fun renderMoveLastTabDialog(): CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction> =
        render(
            state = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123")),
            top =
                TabGroupFlowDestination.MoveLastTabAndDeleteGroupDialog(
                    sourceGroupId = "source",
                    destinationGroupId = "destination",
                    sourceGroupTitle = "Errands",
                    destinationGroupTitle = "Work",
                ),
        )

    private fun render(
        state: TabGroupFlowState,
        top: TabGroupFlowDestination,
    ): CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction> {
        val captureMiddleware = CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction>()
        fragment.store = TabGroupFlowStore(initialState = state, middlewares = listOf(captureMiddleware))
        composeTestRule.setContent {
            NavDisplay(
                backStack = listOf(TabGroupFlowDestination.Root, top),
                onBack = {},
                sceneStrategies = listOf(BottomSheetSceneStrategy(), DialogSceneStrategy()),
                entryProvider = fragment.getEntryProvider(state = state, windowSize = AcornWindowSize.Small),
            )
        }
        return captureMiddleware
    }
}
