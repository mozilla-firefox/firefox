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
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
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
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab as createTrayTab
import org.mozilla.fenix.tabstray.data.createTabGroup as createTrayTabGroup
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.fakes.FakeTabManagerUiStateRepository
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class TabGroupFlowStorageMiddlewareTest {

    private val fakeDateTimeProvider = FakeDateTimeProvider(currentTime = 10L)
    private val captureMiddleware = CaptureActionsMiddleware<TabGroupFlowState, TabGroupAction>()
    private val tab = createTab(id = TAB_ID, url = "https://www.mozilla.org")
    private val existingGroup =
        TabGroup(id = GROUP_ID, title = GROUP_TITLE, theme = TabGroupTheme.Red.name, lastModified = 0L)
    private val tabTrayGroup = createTabGroup(id = GROUP_ID, title = GROUP_TITLE)

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
            TabGroupAction.ExistingGroupSelectedForMove(
                tabId = TAB_ID,
                groupId = destinationGroup.id,
                groupTitle = destinationGroup.title,
            )
        )
        runCurrent()

        assertEquals(mapOf(TAB_ID to destinationGroup.id), repository.tabGroupDataFlow.first().tabGroupAssignments)
    }

    @Test
    fun `WHEN moving the last tab and deleting its group is confirmed THEN the tab is reassigned and the source group is deleted`() =
        runTest {
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
                TabGroupAction.MoveTabAndDeleteGroupConfirmed(
                    sourceGroupId = GROUP_ID,
                    destinationGroupId = destinationGroup.id,
                    destinationGroupTitle = destinationGroup.title,
                )
            )
            runCurrent()

            val data = repository.tabGroupDataFlow.first()
            assertEquals(mapOf(TAB_ID to destinationGroup.id), data.tabGroupAssignments)
            assertEquals(listOf(destinationGroup.id), data.tabGroups.map { it.id })
        }

    @Test
    fun `GIVEN the tab is the last in its group WHEN an existing group is selected THEN storage is unchanged`() =
        runTest {
            val destinationGroup =
                TabGroup(id = "group-2", title = "Destination", theme = TabGroupTheme.Pink.name, lastModified = 0L)
            val repository =
                createRepository(
                    groups = listOf(existingGroup, destinationGroup),
                    assignments = mapOf(TAB_ID to GROUP_ID),
                )
            val initialData = repository.tabGroupDataFlow.first()
            val store =
                createStore(
                    repository = repository,
                    entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID),
                    groups =
                        listOf(
                            createTrayTabGroup(
                                id = GROUP_ID,
                                title = GROUP_TITLE,
                                tabs = listOf(createTrayTab("https://www.mozilla.org", id = TAB_ID)),
                            )
                        ),
                )

            store.dispatch(
                TabGroupAction.ExistingGroupSelectedForMove(
                    tabId = TAB_ID,
                    groupId = destinationGroup.id,
                    groupTitle = destinationGroup.title,
                )
            )
            store.dispatch(TabGroupAction.MoveTabAndDeleteGroupCanceled)
            runCurrent()

            assertEquals(initialData, repository.tabGroupDataFlow.first())
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
    fun `GIVEN a group pending deletion WHEN save is clicked THEN the tab is moved to the new group and the source group is deleted`() =
        runTest {
            val repository = createRepository(groups = listOf(existingGroup), assignments = mapOf(TAB_ID to GROUP_ID))
            val store =
                createStore(
                    repository = repository,
                    entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID),
                    formState = TabGroupFormState(tabGroupId = null, name = "Errands", theme = TabGroupTheme.Pink),
                    tabPendingNewGroup = TAB_ID,
                    groupPendingDeletion = GROUP_ID,
                )

            store.dispatch(TabGroupAction.SaveClicked)
            runCurrent()

            val data = repository.tabGroupDataFlow.first()
            val newGroup = data.tabGroups.single()
            assertEquals("Errands", newGroup.title)
            assertEquals(mapOf(TAB_ID to newGroup.id), data.tabGroupAssignments)
            captureMiddleware.assertLastAction(TabGroupAction.NewGroupCreated::class) {
                assertEquals(newGroup.id, it.id)
            }
        }

    @Test
    fun `GIVEN no group pending deletion WHEN save is clicked THEN the tab's previous group is kept`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup), assignments = mapOf(TAB_ID to GROUP_ID))
        val store =
            createStore(
                repository = repository,
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = TAB_ID),
                formState = TabGroupFormState(tabGroupId = null, name = "Errands"),
                tabPendingNewGroup = TAB_ID,
            )

        store.dispatch(TabGroupAction.SaveClicked)
        runCurrent()

        val data = repository.tabGroupDataFlow.first()
        assertEquals(2, data.tabGroups.size)
        assertTrue(data.tabGroups.any { it.id == GROUP_ID })
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

    @Test
    fun `WHEN the tab group is closed THEN the stored group is marked closed`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup))
        val store = createStore(repository = repository)

        store.dispatch(TabGroupAction.CloseTabGroupClicked(group = tabTrayGroup))
        runCurrent()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.single { it.id == GROUP_ID }.closed)
    }

    @Test
    fun `WHEN deletion is confirmed THEN the group is removed from storage`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup))
        val store = createStore(repository = repository)

        store.dispatch(TabGroupAction.DeleteConfirmed(group = tabTrayGroup))
        runCurrent()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.none { it.id == GROUP_ID })
    }

    @Test
    fun `WHEN closing the last tab and deleting is confirmed THEN the group is removed from storage`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup))
        val store = createStore(repository = repository)

        store.dispatch(TabGroupAction.CloseTabAndDeleteGroupConfirmed(group = tabTrayGroup))
        runCurrent()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.none { it.id == GROUP_ID })
    }

    @Test
    fun `WHEN ungroup is confirmed THEN the group and its assignments are removed`() = runTest {
        val repository = createRepository(groups = listOf(existingGroup), assignments = mapOf(TAB_ID to GROUP_ID))
        val store = createStore(repository = repository)

        store.dispatch(TabGroupAction.UngroupConfirmed(group = tabTrayGroup, dontAskAgain = false))
        runCurrent()

        val data = repository.tabGroupDataFlow.first()
        assertTrue(data.tabGroups.none { it.id == GROUP_ID })
        assertTrue(data.tabGroupAssignments.isEmpty())
    }

    @Test
    fun `GIVEN the confirmation is not skipped WHEN ungroup is requested THEN a confirmation is requested`() = runTest {
        val uiStateRepository =
            FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState(skipUngroupConfirmation = false))
        val store =
            createStore(
                repository = createRepository(groups = listOf(existingGroup)),
                uiStateRepository = uiStateRepository,
            )

        store.dispatch(TabGroupAction.UngroupRequested(group = tabTrayGroup))
        runCurrent()

        captureMiddleware.assertLastAction(TabGroupAction.UngroupConfirmationRequested::class) {
            assertEquals(tabTrayGroup, it.group)
        }
    }

    @Test
    fun `GIVEN the confirmation is skipped WHEN ungroup is requested THEN ungroup is confirmed directly`() = runTest {
        val uiStateRepository =
            FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState(skipUngroupConfirmation = true))
        val store =
            createStore(
                repository = createRepository(groups = listOf(existingGroup)),
                uiStateRepository = uiStateRepository,
            )

        store.dispatch(TabGroupAction.UngroupRequested(group = tabTrayGroup))
        runCurrent()

        captureMiddleware.assertLastAction(TabGroupAction.UngroupConfirmed::class) {
            assertEquals(tabTrayGroup, it.group)
            assertTrue(it.dontAskAgain)
        }
    }

    @Test
    fun `WHEN ungroup is confirmed with dont ask again THEN the skip preference is persisted`() = runTest {
        val uiStateRepository =
            FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState(skipUngroupConfirmation = false))
        val store =
            createStore(
                repository = createRepository(groups = listOf(existingGroup)),
                uiStateRepository = uiStateRepository,
            )

        store.dispatch(TabGroupAction.UngroupConfirmed(group = tabTrayGroup, dontAskAgain = true))
        runCurrent()

        assertTrue(uiStateRepository.uiState.value!!.skipUngroupConfirmation)
    }

    @Test
    fun `WHEN a tab in a multi-tab group is closed THEN that tab is removed`() = runTest {
        val browserStore =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(id = TAB_ID, url = "https://www.mozilla.org"),
                                createTab(id = SECOND_TAB_ID, url = "https://www.firefox.com"),
                            )
                    )
            )
        val repository =
            createRepository(
                groups = listOf(existingGroup),
                assignments = mapOf(TAB_ID to GROUP_ID, SECOND_TAB_ID to GROUP_ID),
            )
        val store = createStore(repository = repository, browserStore = browserStore)

        store.dispatch(
            TabGroupAction.TabClosed(
                tab = createTrayTab(id = TAB_ID, url = "https://www.mozilla.org"),
                group = tabTrayGroup,
            )
        )
        runCurrent()

        assertTrue(browserStore.state.tabs.none { it.id == TAB_ID })
    }

    @Test
    fun `WHEN a new tab is added to the group THEN a homepage tab is created and assigned to that group`() = runTest {
        val browserStore =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = EngineMiddleware.create(engine = mockk(relaxed = true), scope = backgroundScope),
            )
        val repository = createRepository(groups = listOf(existingGroup))
        val store = createStore(repository = repository, browserStore = browserStore)

        store.dispatch(TabGroupAction.AddNewTabToGroupClicked(group = tabTrayGroup))
        runCurrent()

        val newTab = browserStore.state.tabs.single { it.id != TAB_ID }
        assertEquals(ABOUT_HOME_URL, newTab.content.url)
        assertEquals(GROUP_ID, repository.tabGroupDataFlow.first().tabGroupAssignments[newTab.id])
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
        browserStore: BrowserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab))),
        entryPoint: TabGroupFlowEntryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tabId = TAB_ID),
        formState: TabGroupFormState? = null,
        tabPendingNewGroup: String? = null,
        groupPendingDeletion: String? = null,
        groups: List<TabsTrayItem.TabGroup> = emptyList(),
        uiStateRepository: FakeTabManagerUiStateRepository =
            FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState()),
    ): TabGroupFlowStore {
        val appStore = AppStore()
        val tabsUseCases = TabsUseCases(store = browserStore)
        val fenixBrowserUseCases =
            FenixBrowserUseCases(
                appStore = appStore,
                browserStore = browserStore,
                tabsUseCases = tabsUseCases,
                loadUrlUseCase = SessionUseCases(browserStore).loadUrl,
                searchUseCases = mockk(relaxed = true),
                homepageTitle = "",
                profiler = null,
            )
        val tabDataCoordinator =
            DefaultTabDataCoordinator(
                tabRepository =
                    DefaultTabRepository(
                        browserStore = browserStore,
                        tabsUseCases = tabsUseCases,
                        fenixBrowserUseCases = fenixBrowserUseCases,
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
                    groupPendingDeletion = groupPendingDeletion,
                    groups = groups,
                ),
            middlewares =
                listOf(
                    TabGroupFlowStorageMiddleware(
                        tabDataCoordinator = tabDataCoordinator,
                        uiStateRepository = uiStateRepository,
                        scope = backgroundScope,
                        mainScope = backgroundScope,
                    ),
                    captureMiddleware,
                ),
        )
    }

    private companion object {
        const val TAB_ID = "tab-1"
        const val SECOND_TAB_ID = "tab-2"
        const val GROUP_ID = "group-1"
        const val GROUP_TITLE = "Work"
    }
}
