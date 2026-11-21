/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.middleware

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.mozilla.fenix.tabstray.data.TabStorageUpdate
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.fakes.FakeTabManagerUiStateRepository
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.UngroupTabGroupConfirmationDialog
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.PersistedUiStateUpdateReceived
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState

@OptIn(ExperimentalCoroutinesApi::class)
class TabManagerUiStateStorageMiddlewareTest {

    private var repository: FakeTabManagerUiStateRepository = FakeTabManagerUiStateRepository()

    private val captureActionsMiddleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()

    @Test
    fun `GIVEN the user has at least one tab group WHEN tab data is updated THEN the repository records the user as having a tab group`() =
        runTest {
            val store = createStore()

            store.dispatch(TabsTrayAction.TabDataUpdateReceived(tabStorageUpdate = createTabDataUpdateWithOneGroup()))

            runCurrent()
            advanceUntilIdle()

            assertTrue { repository.uiState.value!!.hasUserEverHadOneTabGroup }
        }

    @Test
    fun `GIVEN the user has no tab groups WHEN tab data is updated THEN the repository's record is updated`() =
        runTest {
            repository = FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState())
            val store = createStore()

            store.dispatch(TabsTrayAction.TabDataUpdateReceived(tabStorageUpdate = createTabDataUpdateWithZeroGroups()))

            runCurrent()
            advanceUntilIdle()

            assertFalse(repository.uiState.value!!.hasUserEverHadOneTabGroup)
        }

    @Test
    fun `GIVEN the user has at least one tab group WHEN tab data is updated THEN the tab group onboarding is dismissed`() =
        runTest {
            val store = createStore()

            store.dispatch(TabsTrayAction.TabDataUpdateReceived(tabStorageUpdate = createTabDataUpdateWithOneGroup()))

            runCurrent()
            advanceUntilIdle()

            assertTrue { repository.uiState.value!!.hasUserDismissedTabGroupOnboarding }
        }

    @Test
    fun `GIVEN the user has no tab groups WHEN tab data is updated THEN the tab group onboarding is not dismissed`() =
        runTest {
            val store = createStore()

            store.dispatch(TabsTrayAction.TabDataUpdateReceived(tabStorageUpdate = createTabDataUpdateWithZeroGroups()))

            runCurrent()
            advanceUntilIdle()

            assertNull(repository.uiState.value?.hasUserDismissedTabGroupOnboarding)
        }

    @Test
    fun `WHEN the Store is initialized THEN the connection to the repository is established and non-null updates to the repository are dispatched to the Store`() =
        runTest {
            repository = FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState())
            createStore()

            runCurrent()
            advanceUntilIdle()

            captureActionsMiddleware.assertLastAction(clazz = PersistedUiStateUpdateReceived::class)
        }

    @Test
    fun `WHEN tab group onboarding is dismissed THEN the repo is updated`() = runTest {
        createStore().dispatch(TabGroupAction.OnboardingDismissed)

        runCurrent()
        advanceUntilIdle()

        assertTrue { repository.uiState.value!!.hasUserDismissedTabGroupOnboarding }
    }

    @Test
    fun `WHEN the tab group onboarding is shown THEN the impression count is incremented`() = runTest {
        createStore().dispatch(TabGroupAction.OnboardingShown)

        runCurrent()
        advanceUntilIdle()

        assertEquals(1, repository.uiState.value!!.tabGroupOnboardingImpressionCount)
    }

    @Test
    fun `WHEN the tab group onboarding is shown multiple times in a session THEN the impression count is incremented only once`() =
        runTest {
            val store = createStore()

            store.dispatch(TabGroupAction.OnboardingShown)
            store.dispatch(TabGroupAction.OnboardingShown)

            runCurrent()
            advanceUntilIdle()

            assertEquals(1, repository.uiState.value!!.tabGroupOnboardingImpressionCount)
        }

    @Test
    fun `GIVEN the user checked don't ask again WHEN ungroup is confirmed THEN the repo is updated`() = runTest {
        val store = createStore()

        store.dispatch(TabGroupAction.UngroupConfirmed(group = createTabGroup(), dontAskAgain = true))

        runCurrent()
        advanceUntilIdle()

        assertTrue { repository.uiState.value!!.skipUngroupConfirmation }
    }

    @Test
    fun `GIVEN the user did not check don't ask again WHEN ungroup is confirmed THEN the repo is not updated`() =
        runTest {
            repository = FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState())
            val store = createStore()

            store.dispatch(TabGroupAction.UngroupConfirmed(group = createTabGroup(), dontAskAgain = false))

            runCurrent()
            advanceUntilIdle()

            assertFalse(repository.uiState.value!!.skipUngroupConfirmation)
        }

    @Test
    fun `GIVEN the confirmation is skipped WHEN an ungroup is requested THEN the ungroup is confirmed without the dialog`() =
        runTest {
            val group = createTabGroup()
            val store =
                createStore(
                    initialTabsTrayState =
                        TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(skipUngroupConfirmation = true))
                )

            store.dispatch(TabGroupAction.UngroupRequested(group = group))

            runCurrent()
            advanceUntilIdle()

            captureActionsMiddleware.assertLastAction(clazz = TabGroupAction.UngroupConfirmed::class) { action ->
                assertEquals(group, action.group)
                assertTrue(action.dontAskAgain)
            }
            assertTrue(store.state.backStack.none { it is UngroupTabGroupConfirmationDialog })
        }

    @Test
    fun `GIVEN the confirmation is not skipped WHEN an ungroup is requested THEN the confirmation dialog is shown`() =
        runTest {
            val group = createTabGroup()
            val store =
                createStore(
                    initialTabsTrayState =
                        TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(skipUngroupConfirmation = false))
                )

            store.dispatch(TabGroupAction.UngroupRequested(group = group))

            runCurrent()
            advanceUntilIdle()

            assertEquals(UngroupTabGroupConfirmationDialog(group = group), store.state.backStack.last())
            captureActionsMiddleware.assertNotDispatched(clazz = TabGroupAction.UngroupConfirmed::class)
        }

    @Test
    fun `GIVEN the user has tab groups WHEN the tab groups page is selected THEN the repository records the page as viewed`() =
        runTest {
            val store =
                createStore(
                    initialTabsTrayState =
                        TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(groups = listOf(createTabGroup())))
                )

            store.dispatch(TabsTrayAction.PageSelected(Page.TabGroups))

            runCurrent()
            advanceUntilIdle()

            assertTrue { repository.uiState.value!!.hasViewedTabGroupsPage }
        }

    @Test
    fun `GIVEN the user has no tab groups WHEN the tab groups page is selected THEN the repository does not record the page as viewed`() =
        runTest {
            repository = FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState())
            val store =
                createStore(
                    initialTabsTrayState =
                        TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(groups = emptyList()))
                )

            store.dispatch(TabsTrayAction.PageSelected(Page.TabGroups))

            runCurrent()
            advanceUntilIdle()

            assertFalse(repository.uiState.value!!.hasViewedTabGroupsPage)
        }

    @Test
    fun `GIVEN the user has tab groups WHEN a page other than tab groups is selected THEN the repository does not record the page as viewed`() =
        runTest {
            repository = FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState())
            val store =
                createStore(
                    initialTabsTrayState =
                        TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(groups = listOf(createTabGroup())))
                )

            store.dispatch(TabsTrayAction.PageSelected(Page.SyncedTabs))

            runCurrent()
            advanceUntilIdle()

            assertFalse(repository.uiState.value!!.hasViewedTabGroupsPage)
        }

    private fun TestScope.createStore(initialTabsTrayState: TabsTrayState = TabsTrayState()) =
        TabsTrayStore(
            initialState = initialTabsTrayState,
            middlewares =
                listOf(
                    captureActionsMiddleware,
                    TabManagerUiStateStorageMiddleware(
                        uiStateRepository = repository,
                        scope = backgroundScope,
                    ),
                ),
        )

    private fun createTabDataUpdateWithOneGroup() =
        TabStorageUpdate(
            selectedTabId = "",
            normalItems = emptyList(),
            normalTabCount = 0,
            selectedNormalItemIndex = 0,
            inactiveTabs = emptyList(),
            privateTabs = emptyList(),
            selectedPrivateItemIndex = 0,
            tabGroups = listOf(createTabGroup()),
        )

    private fun createTabDataUpdateWithZeroGroups() =
        TabStorageUpdate(
            selectedTabId = "",
            normalItems = emptyList(),
            normalTabCount = 0,
            selectedNormalItemIndex = 0,
            inactiveTabs = emptyList(),
            privateTabs = emptyList(),
            selectedPrivateItemIndex = 0,
            tabGroups = emptyList(),
        )
}
