/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabdata.coordinator.DefaultTabDataCoordinator
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.feature.tabgroups.storage.fakes.FakeTabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.tabs.DefaultTabRepository
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class TabGroupFlowStorageMiddlewareTest {

    private val fakeDateTimeProvider = FakeDateTimeProvider(currentTime = 10L)
    private val captureMiddleware = CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction>()
    private val tab = createTab(id = TAB_ID, url = "https://www.mozilla.org")
    private val existingGroup =
        TabGroup(id = GROUP_ID, title = GROUP_TITLE, theme = TabGroupTheme.Red.name, lastModified = 0L)

    @Test
    fun `WHEN a tab is added to an existing group THEN the tab is assigned to that group`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup))
        val store = createStore(repository = repository)

        store.dispatch(
            TabGroupAction.TabAddedToExistingTabGroup(tabId = TAB_ID, groupId = GROUP_ID, groupTitle = GROUP_TITLE)
        )
        runCurrent()

        assertEquals(mapOf(TAB_ID to GROUP_ID), repository.tabGroupDataFlow.first().tabGroupAssignments)
    }

    @Test
    fun `WHEN a tab is moved to an existing group THEN the tab is reassigned to that group`() = runTest {
        val destinationGroup =
            TabGroup(id = "group-2", title = "Destination", theme = TabGroupTheme.Pink.name, lastModified = 0L)
        val repository =
            createRepository(
                groups = listOf(existingGroup, destinationGroup),
                assignments = mapOf(TAB_ID to GROUP_ID),
            )
        val store =
            createStore(
                repository = repository,
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID),
            )

        store.dispatch(
            TabGroupAction.TabMovedToExistingTabGroup(
                tabId = TAB_ID,
                groupId = destinationGroup.id,
                groupTitle = destinationGroup.title,
            )
        )
        runCurrent()

        assertEquals(mapOf(TAB_ID to destinationGroup.id), repository.tabGroupDataFlow.first().tabGroupAssignments)
    }

    @Test
    fun `GIVEN a new group form and a pending tab WHEN save is clicked THEN the group is created with the tab`() =
        runTest {
            val repository = createRepository()
            val store =
                createStore(
                    repository = repository,
                    formState = TabGroupFormState(tabGroupId = null, name = GROUP_TITLE, theme = TabGroupTheme.Red),
                    tabPendingNewGroup = TAB_ID,
                )

            store.dispatch(TabGroupAction.SaveClicked)
            runCurrent()

            val data = repository.tabGroupDataFlow.first()
            val storedGroup = data.tabGroups.single()
            assertEquals(GROUP_TITLE, storedGroup.title)
            assertEquals(TabGroupTheme.Red.toStorageValue(), storedGroup.theme)
            assertEquals(mapOf(TAB_ID to storedGroup.id), data.tabGroupAssignments)
        }

    @Test
    fun `GIVEN a new group form and a pending tab WHEN the group is created THEN NewGroupCreated is dispatched with its ID`() =
        runTest {
            val repository = createRepository()
            val store =
                createStore(
                    repository = repository,
                    formState = TabGroupFormState(tabGroupId = null, name = GROUP_TITLE),
                    tabPendingNewGroup = TAB_ID,
                )

            store.dispatch(TabGroupAction.SaveClicked)
            runCurrent()

            val storedGroup = repository.tabGroupDataFlow.first().tabGroups.single()
            captureMiddleware.assertLastAction(TabGroupAction.NewGroupCreated::class) {
                assertEquals(storedGroup.id, it.id)
            }
        }

    @Test
    fun `GIVEN an existing group form WHEN save is clicked THEN the group is updated and no group is created`() =
        runTest {
            val repository = createRepository(groups = listOf(existingGroup), assignments = mapOf(TAB_ID to GROUP_ID))
            val store =
                createStore(
                    repository = repository,
                    entryPoint = TabGroupFlowEntryPoint.EditGroup(groupId = GROUP_ID),
                    formState = TabGroupFormState(tabGroupId = GROUP_ID, name = "Renamed", theme = TabGroupTheme.Pink),
                )

            store.dispatch(TabGroupAction.SaveClicked)
            runCurrent()

            val data = repository.tabGroupDataFlow.first()
            val storedGroup = data.tabGroups.single()
            assertEquals(GROUP_ID, storedGroup.id)
            assertEquals("Renamed", storedGroup.title)
            assertEquals(TabGroupTheme.Pink.toStorageValue(), storedGroup.theme)
            assertEquals(mapOf(TAB_ID to GROUP_ID), data.tabGroupAssignments)
            captureMiddleware.assertNotDispatched(TabGroupAction.NewGroupCreated::class)
        }

    @Test
    fun `GIVEN no form state WHEN save is clicked THEN storage is unchanged`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup))
        val initialData = repository.tabGroupDataFlow.first()
        val store = createStore(repository = repository, tabPendingNewGroup = TAB_ID)

        store.dispatch(TabGroupAction.SaveClicked)
        runCurrent()

        assertEquals(initialData, repository.tabGroupDataFlow.first())
        captureMiddleware.assertNotDispatched(TabGroupAction.NewGroupCreated::class)
    }

    @Test
    fun `GIVEN a new group form and no pending tab WHEN save is clicked THEN no group is created`() = runTest {
        val repository = createRepository()
        val store =
            createStore(
                repository = repository,
                formState = TabGroupFormState(tabGroupId = null, name = GROUP_TITLE),
            )

        store.dispatch(TabGroupAction.SaveClicked)
        runCurrent()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
        captureMiddleware.assertNotDispatched(TabGroupAction.NewGroupCreated::class)
    }

    private fun createRepository(
        groups: List<TabGroup> = emptyList(),
        assignments: Map<String, String> = emptyMap(),
    ) =
        FakeTabGroupRepository(
            initialTabGroupData = TabGroupData(tabGroups = groups, tabGroupAssignments = assignments),
            dateTimeProvider = fakeDateTimeProvider,
        )

    private fun TestScope.createStore(
        repository: FakeTabGroupRepository,
        entryPoint: TabGroupFlowEntryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = TAB_ID),
        formState: TabGroupFormState? = null,
        tabPendingNewGroup: String? = null,
    ): TabGroupFlowStore {
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab)))
        val appStore = AppStore()
        val tabsUseCases = TabsUseCases(store = browserStore)
        val tabDataCoordinator =
            DefaultTabDataCoordinator(
                tabRepository =
                    DefaultTabRepository(
                        browserStore = browserStore,
                        tabsUseCases = tabsUseCases,
                        fenixBrowserUseCases =
                            FenixBrowserUseCases(
                                appStore = appStore,
                                tabsUseCases = tabsUseCases,
                                loadUrlUseCase = SessionUseCases(browserStore).loadUrl,
                                searchUseCases = mockk(relaxed = true),
                                homepageTitle = "",
                                profiler = null,
                            ),
                    ),
                tabGroupRepository = repository,
                isInactiveTabsEnabled = { false },
                isTabGroupsEnabled = { true },
                scope = backgroundScope,
                dispatcher = StandardTestDispatcher(testScheduler),
                dateTimeProvider = fakeDateTimeProvider,
            )
        tabDataCoordinator.initialize()
        runCurrent()

        return TabGroupFlowStore(
            initialState =
                TabGroupFlowState(
                    entryPoint = entryPoint,
                    formState = formState,
                    tabPendingNewGroup = tabPendingNewGroup,
                ),
            middlewares =
                listOf(
                    TabGroupFlowStorageMiddleware(
                        tabDataCoordinator = tabDataCoordinator,
                        mainScope = backgroundScope,
                    ),
                    captureMiddleware,
                ),
        )
    }

    private companion object {
        const val TAB_ID = "tab-1"
        const val GROUP_ID = "group-1"
        const val GROUP_TITLE = "Work"
    }
}
