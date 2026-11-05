/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.middleware

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import junit.framework.TestCase.assertEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabdata.coordinator.DefaultTabDataCoordinator
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.feature.tabgroups.storage.fakes.FakeTabGroupRepository
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.tabs.DefaultTabRepository
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.maxActiveTime
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination.ExpandedTabGroup
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.Mode
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class TabStorageMiddlewareTest {

    private val fakeDateTimeProvider = FakeDateTimeProvider(currentTime = 10L)

    @Test
    fun `WHEN the selected tab ID is updated THEN transform the data and dispatch an update`() = runTest {
        val expectedTabId = "1"
        val expectedTab = createTab(id = expectedTabId, url = "")
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(expectedTab)))
        val expectedState =
            TabsTrayState(
                selectedTabId = expectedTabId,
                normalTabsState =
                    TabsTrayState.NormalTabsState(
                        items = listOf(TabsTrayItem.Tab(tab = expectedTab, isFocused = true)),
                        tabCount = 1,
                    ),
                hasTabDataLoaded = true,
            )
        val store = createStore(browserStore = browserStore)

        browserStore.dispatch(TabListAction.SelectTabAction(tabId = expectedTabId))

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedState, store.state)
    }

    @Test
    fun `WHEN save is clicked from drag and drop for a new group THEN create the group with the two tabs`() = runTest {
        val repository = createRepository()
        val sourceTab = createTab(url = "https://mozilla.org")
        val destinationTab = createTab(url = "https://example.com")
        val expectedTitle = "Group 1"
        val expectedTheme = TabGroupTheme.Red
        val store =
            createStore(
                initialState =
                    TabsTrayState(
                        mode = Mode.DragAndDrop(sourceId = sourceTab.id, destinationId = destinationTab.id),
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                formState =
                                    TabGroupFormState(
                                        name = expectedTitle,
                                        tabGroupId = null,
                                        theme = expectedTheme,
                                    )
                            ),
                    ),
                browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(sourceTab, destinationTab))),
                tabGroupsEnabled = true,
                tabGroupRepository = repository,
                dateTimeProvider = fakeDateTimeProvider,
            )

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
        assertTrue(repository.tabGroupDataFlow.first().tabGroupAssignments.isEmpty())

        runCurrent()
        advanceUntilIdle()

        store.dispatch(TabGroupAction.SaveClicked)

        runCurrent()
        advanceUntilIdle()

        assertEquals(1, repository.tabGroupDataFlow.first().tabGroups.size)
        val storedGroup = repository.tabGroupDataFlow.first().tabGroups.first()
        assertEquals(
            TabGroup(
                id = storedGroup.id,
                title = expectedTitle,
                theme = expectedTheme.name,
                lastModified = fakeDateTimeProvider.currentTimeMillis(),
                tabIds = listOf(sourceTab.id, destinationTab.id),
            ),
            storedGroup,
        )
        assertEquals(
            mapOf(
                sourceTab.id to storedGroup.id,
                destinationTab.id to storedGroup.id,
            ),
            repository.tabGroupDataFlow.first().tabGroupAssignments,
        )
    }

    @Test
    fun `WHEN save is clicked in multiselect mode for a new group THEN create the group with selected tabs`() =
        runTest {
            val repository = createRepository()
            val tabs =
                listOf(
                    createTab(url = "https://mozilla.org"),
                    createTab(url = "https://example.com"),
                )
            val selectedTabs = tabs.map { TabsTrayItem.Tab(tab = it) }.toSet()
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode = Mode.Select(selectedTabs = selectedTabs),
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                        )
                                ),
                        ),
                    browserStore = BrowserStore(initialState = BrowserState(tabs = tabs)),
                    tabGroupsEnabled = true,
                    tabGroupRepository = repository,
                    dateTimeProvider = fakeDateTimeProvider,
                )

            assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(repository.tabGroupDataFlow.first().tabGroupAssignments.isEmpty())

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            assertEquals(1, repository.tabGroupDataFlow.first().tabGroups.size)
            val storedGroup = repository.tabGroupDataFlow.first().tabGroups.first()
            assertEquals(
                TabGroup(
                    id = storedGroup.id,
                    title = expectedTitle,
                    theme = expectedTheme.name,
                    lastModified = fakeDateTimeProvider.currentTimeMillis(),
                    tabIds = selectedTabs.map { it.id },
                ),
                storedGroup,
            )
            assertEquals(
                selectedTabs.associate { it.id to storedGroup.id },
                repository.tabGroupDataFlow.first().tabGroupAssignments,
            )
        }

    @Test
    fun `WHEN save is clicked for a non-starter new group with no selected tabs THEN an empty group is created without a homepage tab`() =
        runTest {
            val repository = createRepository()
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                            isStarterTabGroup = false,
                                        )
                                )
                        ),
                    tabGroupRepository = repository,
                    fenixBrowserUseCases = fenixBrowserUseCases,
                    dateTimeProvider = fakeDateTimeProvider,
                )

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            val storedGroup = repository.tabGroupDataFlow.first().tabGroups.single()
            assertEquals(
                TabGroup(
                    id = storedGroup.id,
                    title = expectedTitle,
                    theme = expectedTheme.name,
                    lastModified = fakeDateTimeProvider.currentTimeMillis(),
                    tabIds = emptyList(),
                ),
                storedGroup,
            )
            assertTrue(repository.tabGroupDataFlow.first().tabGroupAssignments.isEmpty())
            verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(any(), any(), any()) }
        }

    @Test
    fun `WHEN save is clicked for a starter tab group THEN a lazy homepage tab is added and the group opens expanded`() =
        runTest {
            val repository = createRepository()
            val newTabId = "new-tab-id"
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)
            every {
                fenixBrowserUseCases.addNewHomepageTab(private = false, startLoading = false, selectTab = false)
            } returns newTabId
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                            isStarterTabGroup = true,
                                        )
                                )
                        ),
                    tabGroupRepository = repository,
                    fenixBrowserUseCases = fenixBrowserUseCases,
                )

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            verify {
                fenixBrowserUseCases.addNewHomepageTab(private = false, startLoading = false, selectTab = false)
            }

            val storedGroup = repository.tabGroupDataFlow.first().tabGroups.first()
            assertEquals(expectedTitle, storedGroup.title)
            assertEquals(
                mapOf(newTabId to storedGroup.id),
                repository.tabGroupDataFlow.first().tabGroupAssignments,
            )

            assertEquals(Page.NormalTabs, store.state.selectedPage)
            val expanded = store.state.backStack.last() as ExpandedTabGroup
            assertEquals(storedGroup.id, expanded.group.id)
        }

    @Test
    fun `WHEN save is clicked with existing tab group id THEN update existing tab group`() = runTest {
        val existingId = "1"
        val expectedTitle = "New name"
        val expectedTheme = TabGroupTheme.Blue
        val existingGroup =
            TabGroup(
                id = existingId,
                title = "Old name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val repository = createRepository(initialTabGroups = listOf(existingGroup))
        val store =
            createStore(
                initialState =
                    TabsTrayState(
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                formState =
                                    TabGroupFormState(
                                        tabGroupId = existingId,
                                        name = expectedTitle,
                                        theme = expectedTheme,
                                    )
                            )
                    ),
                tabGroupRepository = repository,
                dateTimeProvider = fakeDateTimeProvider,
            )

        assertEquals(listOf(existingGroup), repository.tabGroupDataFlow.first().tabGroups)

        store.dispatch(TabGroupAction.SaveClicked)

        runCurrent()
        advanceUntilIdle()

        assertEquals(
            listOf(
                TabGroup(
                    id = existingId,
                    title = expectedTitle,
                    theme = expectedTheme.name,
                    lastModified = fakeDateTimeProvider.currentTimeMillis(),
                    tabIds = emptyList(),
                )
            ),
            repository.tabGroupDataFlow.first().tabGroups,
        )
        assertTrue(repository.tabGroupDataFlow.first().tabGroupAssignments.isEmpty())
    }

    @Test
    fun `WHEN save is clicked with no form state THEN no tab group writes occur`() = runTest {
        val repository = createRepository()
        val store =
            createStore(
                initialState = TabsTrayState(),
                tabGroupRepository = repository,
            )

        store.dispatch(TabGroupAction.SaveClicked)

        runCurrent()
        advanceUntilIdle()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
        assertTrue(repository.tabGroupDataFlow.first().tabGroupAssignments.isEmpty())
    }

    @Test
    fun `WHEN tab group delete is confirmed THEN remove the tab group and its tabs`() = runTest {
        val browserStore = BrowserStore()

        val firstTab = createTab("https://mozilla.org")
        browserStore.dispatch(TabListAction.AddTabAction(firstTab))

        val secondTab = createTab("https://example.com")
        browserStore.dispatch(TabListAction.AddTabAction(secondTab))

        val title = "Group 1"
        val theme = TabGroupTheme.Red
        val storedGroup =
            TabGroup(
                title = title,
                theme = theme.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )

        val repository =
            FakeTabGroupRepository(
                initialTabGroupData =
                    TabGroupData(
                        tabGroups = listOf(storedGroup),
                        tabGroupAssignments = mapOf(firstTab.id to storedGroup.id, secondTab.id to storedGroup.id),
                    )
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = browserStore,
                tabGroupRepository = repository,
            )

        val group =
            TabsTrayItem.TabGroup(
                id = storedGroup.id,
                title = title,
                theme = theme,
                tabs =
                    mutableListOf(
                        TabsTrayItem.Tab(firstTab),
                        TabsTrayItem.Tab(secondTab),
                    ),
            )

        assertEquals(listOf(storedGroup), repository.tabGroupDataFlow.first().tabGroups)
        assertEquals(2, browserStore.state.tabs.size)

        runCurrent()
        store.dispatch(TabGroupAction.DeleteConfirmed(group))

        runCurrent()
        advanceUntilIdle()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
        assertTrue(browserStore.state.tabs.isEmpty())
    }

    @Test
    fun `WHEN ungroup is confirmed THEN remove the tab group and its assignments but keep its tabs`() = runTest {
        val browserStore = BrowserStore()

        val firstTab = createTab("https://mozilla.org")
        browserStore.dispatch(TabListAction.AddTabAction(firstTab))

        val secondTab = createTab("https://example.com")
        browserStore.dispatch(TabListAction.AddTabAction(secondTab))

        val storedGroup =
            TabGroup(
                title = "Group 1",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = listOf(firstTab.id, secondTab.id),
            )

        val repository =
            FakeTabGroupRepository(
                initialTabGroupData =
                    TabGroupData(
                        tabGroups = listOf(storedGroup),
                        tabGroupAssignments = mapOf(firstTab.id to storedGroup.id, secondTab.id to storedGroup.id),
                    )
            )
        val store =
            createStore(
                browserStore = browserStore,
                tabGroupRepository = repository,
            )

        val group =
            TabsTrayItem.TabGroup(
                id = storedGroup.id,
                title = storedGroup.title,
                theme = TabGroupTheme.Red,
                tabs =
                    mutableListOf(
                        TabsTrayItem.Tab(firstTab),
                        TabsTrayItem.Tab(secondTab),
                    ),
            )

        assertEquals(listOf(storedGroup), repository.tabGroupDataFlow.first().tabGroups)
        assertEquals(2, browserStore.state.tabs.size)

        store.dispatch(TabGroupAction.UngroupConfirmed(group = group, dontAskAgain = false))

        runCurrent()
        advanceUntilIdle()

        assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
        assertTrue(repository.tabGroupDataFlow.first().tabGroupAssignments.isEmpty())
        assertEquals(2, browserStore.state.tabs.size)
    }

    @Test
    fun `GIVEN multiple tab groups exist WHEN delete is confirmed THEN remove the correct tab group`() = runTest {
        val tabGroup1 =
            TabGroup(
                title = "Tab Group 1",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val tabGroup2 =
            TabGroup(
                title = "Tab Group 2",
                theme = TabGroupTheme.Blue.name,
                lastModified = 1L,
                tabIds = emptyList(),
            )
        val repository =
            FakeTabGroupRepository(initialTabGroupData = TabGroupData(tabGroups = listOf(tabGroup1, tabGroup2)))
        val store = createStore(tabGroupRepository = repository)

        assertEquals(listOf(tabGroup1, tabGroup2), repository.tabGroupDataFlow.first().tabGroups)

        store.dispatch(
            TabGroupAction.DeleteConfirmed(
                group =
                    TabsTrayItem.TabGroup(
                        id = tabGroup1.id,
                        title = tabGroup1.title,
                        theme = TabGroupTheme.Red,
                        tabs = mutableListOf(),
                    )
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(listOf(tabGroup2), repository.tabGroupDataFlow.first().tabGroups)
    }

    @Test
    fun `WHEN a tab group is opened from tab groups page THEN reopen the tab group in the repository`() = runTest {
        val closedGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                closed = true,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val displayGroup =
            createTabGroup(
                id = closedGroup.id,
                title = closedGroup.title,
                theme = TabGroupTheme.valueOf(closedGroup.theme),
                closed = false,
            )
        val store =
            createStore(
                initialState = TabsTrayState(selectedPage = Page.TabGroups),
                tabGroupsEnabled = true,
                tabGroupRepository = createRepository(initialTabGroups = listOf(closedGroup)),
            )
        val expectedState =
            TabsTrayState(
                selectedPage = Page.NormalTabs,
                normalTabsState = TabsTrayState.NormalTabsState(),
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups = listOf(displayGroup.copy(lastModified = fakeDateTimeProvider.currentTimeMillis()))
                    ),
                backStack = TabsTrayState().backStack + ExpandedTabGroup(group = displayGroup),
                hasTabDataLoaded = true,
            )

        store.dispatch(
            TabGroupAction.OpenTabGroupClicked(
                group =
                    TabsTrayItem.TabGroup(
                        id = closedGroup.id,
                        title = closedGroup.title,
                        theme = TabGroupTheme.Red,
                        tabs = mutableListOf(),
                        closed = true,
                    )
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedState, store.state)
    }

    @Test
    fun `Given the tab groups feature is disabled WHEN initializing THEN the tab group data is not emitted`() =
        runTest {
            val expectedTab = createTab("test1")
            val expectedTabGroup =
                TabGroup(
                    title = "title",
                    theme = "Red",
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val expectedState =
                TabsTrayState(
                    normalTabsState =
                        TabsTrayState.NormalTabsState(
                            items = listOf(TabsTrayItem.Tab(expectedTab)),
                            tabCount = 1,
                        ),
                    hasTabDataLoaded = true,
                )
            val store =
                createStore(
                    tabGroupsEnabled = false,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(expectedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(expectedTabGroup),
                            initialTabGroupAssignments = listOf(expectedTab.id to expectedTabGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedState, store.state)
        }

    @Test
    fun `GIVEN the user has selected tabs WHEN adding tabs to an existing group THEN the selected tabs are added to the specified group`() =
        runTest {
            val tabs = MutableList(size = 10) { createTab(url = "") }
            val selectedTabs = MutableList(size = 10) { TabsTrayItem.Tab(tabs[it]) }
            val existingGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    initialState = TabsTrayState(mode = Mode.Select(selectedTabs = selectedTabs.toSet())),
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = tabs)),
                    tabGroupRepository = createRepository(initialTabGroups = listOf(existingGroup)),
                )
            val expectedTabGroupList =
                listOf(
                    createTabGroup(
                        id = existingGroup.id,
                        title = existingGroup.title,
                        theme = TabGroupTheme.Red,
                        tabs = selectedTabs,
                    )
                )
            val expectedState =
                TabsTrayState(
                    mode = Mode.Normal,
                    normalTabsState =
                        TabsTrayState.NormalTabsState(
                            items = expectedTabGroupList,
                            tabCount = selectedTabs.size,
                        ),
                    tabGroupState = TabsTrayState.TabGroupState(groups = expectedTabGroupList),
                    hasTabDataLoaded = true,
                )

            store.dispatch(TabGroupAction.SelectedTabsAddedToGroup(groupId = existingGroup.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedState, store.state)
        }

    @Test
    fun `GIVEN the user has selected 10 ungrouped tabs and 3 tab groups WHEN adding tabs to an existing group THEN the selected tabs are added to the specified group and the selected groups are deleted`() =
        runTest {
            val existingId = "12345"
            val tabs = MutableList(size = 40) { createTab(url = "") }
            val selectedTabs = MutableList(size = 40) { TabsTrayItem.Tab(tabs[it]) }
            val destinationTabGroup =
                TabGroup(
                    id = existingId,
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val tabGroupData =
                List(size = 3) {
                    TabGroup(
                        title = "Group $it",
                        theme = TabGroupTheme.Red.name,
                        lastModified = 0L,
                        tabIds = emptyList(),
                    )
                }
            // Assign tabs to the 3 multi-selected groups
            val groupedTabs =
                listOf(
                    selectedTabs.subList(10, 20),
                    selectedTabs.subList(20, 30),
                    selectedTabs.subList(30, 40),
                )
            val selectedTabGroups = tabGroupData.mapIndexed { index, group ->
                createTabGroup(
                    id = group.id,
                    title = group.title,
                    tabs = groupedTabs[index].toList(),
                )
            }
            val initialTabAssignments = mutableListOf<Pair<String, String>>()
            selectedTabGroups.forEach { group ->
                group.tabs.forEach { tab ->
                    initialTabAssignments.add(tab.id to group.id)
                }
            }
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode =
                                Mode.Select(
                                    selectedTabs = selectedTabs.toSet(),
                                    selectedTabGroups = selectedTabGroups.toSet(),
                                )
                        ),
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = tabs)),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = tabGroupData + destinationTabGroup,
                            initialTabGroupAssignments = initialTabAssignments,
                        ),
                )
            val expectedTabGroupList =
                listOf(
                    createTabGroup(
                        id = destinationTabGroup.id,
                        title = destinationTabGroup.title,
                        theme = TabGroupTheme.Red,
                        tabs = selectedTabs,
                    )
                )
            val expectedState =
                TabsTrayState(
                    mode = Mode.Normal,
                    normalTabsState =
                        TabsTrayState.NormalTabsState(
                            items = expectedTabGroupList,
                            tabCount = tabs.size,
                        ),
                    tabGroupState = TabsTrayState.TabGroupState(groups = expectedTabGroupList),
                    hasTabDataLoaded = true,
                )

            store.dispatch(TabGroupAction.SelectedTabsAddedToGroup(groupId = existingId))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedState, store.state)
        }

    @Test
    fun `GIVEN the user has selected a mix of tabs and groups WHEN adding to one of the selected groups THEN the tabs are added and the groups are merged into the destination group`() =
        runTest {
            val tabs = MutableList(size = 40) { createTab(url = "") }
            val selectedTabs = MutableList(size = 40) { TabsTrayItem.Tab(tabs[it]) }
            val tabGroupData =
                List(size = 3) {
                    TabGroup(
                        title = "Group $it",
                        theme = TabGroupTheme.Red.name,
                        lastModified = 0L,
                        tabIds = emptyList(),
                    )
                }
            // Assign tabs to the 3 multi-selected groups
            val groupedTabs =
                listOf(
                    selectedTabs.subList(10, 20),
                    selectedTabs.subList(20, 30),
                    selectedTabs.subList(30, 40),
                )
            val selectedTabGroups = tabGroupData.mapIndexed { index, group ->
                createTabGroup(
                    id = group.id,
                    title = group.title,
                    tabs = groupedTabs[index].toList(),
                    theme = TabGroupTheme.valueOf(group.theme),
                )
            }
            val initialTabAssignments = mutableListOf<Pair<String, String>>()
            selectedTabGroups.forEach { group ->
                group.tabs.forEach { tab ->
                    initialTabAssignments.add(tab.id to group.id)
                }
            }
            val destinationTabGroup = selectedTabGroups.first()
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode =
                                Mode.Select(
                                    selectedTabs = selectedTabs.toSet(),
                                    selectedTabGroups = selectedTabGroups.toSet(),
                                )
                        ),
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = tabs)),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = tabGroupData,
                            initialTabGroupAssignments = initialTabAssignments,
                        ),
                )
            val expectedTabGroupList = listOf(destinationTabGroup.copy(tabs = selectedTabs))
            val expectedState =
                TabsTrayState(
                    mode = Mode.Normal,
                    normalTabsState =
                        TabsTrayState.NormalTabsState(
                            items = expectedTabGroupList,
                            tabCount = tabs.size,
                        ),
                    tabGroupState = TabsTrayState.TabGroupState(groups = expectedTabGroupList),
                    hasTabDataLoaded = true,
                )

            store.dispatch(TabGroupAction.SelectedTabsAddedToGroup(groupId = destinationTabGroup.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedState, store.state)
        }

    @Test
    fun `GIVEN the user has at least one tab and one tab group WHEN the user adds a tab to an existing tab group THEN the tab is added to the specified group`() =
        runTest {
            val tab = createTab(url = "")
            val existingGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab))),
                    tabGroupRepository = createRepository(initialTabGroups = listOf(existingGroup)),
                )
            val expectedTabGroupList =
                listOf(
                    createTabGroup(
                        id = existingGroup.id,
                        title = existingGroup.title,
                        theme = TabGroupTheme.Red,
                        tabs = mutableListOf(TabsTrayItem.Tab(tab)),
                    )
                )
            val expectedState =
                TabsTrayState(
                    normalTabsState =
                        TabsTrayState.NormalTabsState(
                            items = expectedTabGroupList,
                            tabCount = 1,
                        ),
                    tabGroupState = TabsTrayState.TabGroupState(groups = expectedTabGroupList),
                    hasTabDataLoaded = true,
                )

            store.dispatch(TabGroupAction.TabAddedToExistingTabGroup(tabId = tab.id, groupId = existingGroup.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedState, store.state)
        }

    @Test
    fun `WHEN a user creates a group with multiple tabs THEN the tabs become blocked next to each other`() = runTest {
        val tabs = List(size = 10) { createTab(url = "$it") }
        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val selectedTabIndices = listOf(2, 3, 4, 7, 9)
        val selectedTabs = tabs.slice(selectedTabIndices).map { TabsTrayItem.Tab(tab = it) }
        val expectedTitle = "Group 1"
        val expectedTheme = TabGroupTheme.Red
        val store =
            createStore(
                initialState =
                    TabsTrayState(
                        mode = Mode.Select(selectedTabs = selectedTabs.toSet()),
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                formState =
                                    TabGroupFormState(
                                        name = expectedTitle,
                                        tabGroupId = null,
                                        theme = expectedTheme,
                                    )
                            ),
                    ),
                browserStore = browserStore,
                tabGroupsEnabled = true,
            )
        val expectedTabs = tabs.slice(listOf(0, 1)) + tabs.slice(selectedTabIndices) + tabs.slice(listOf(5, 6, 8))
        val expectedBrowserState = BrowserState(tabs = expectedTabs)

        runCurrent()
        advanceUntilIdle()

        store.dispatch(TabGroupAction.SaveClicked)

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a tab is reordered before a group THEN the tab id is sequenced before the first group tab`() = runTest {
        val tabs = fakeTabList()
        val groupTabs = tabs.slice(5..7)
        val tab = tabs[2]
        val group = fakeGroup()

        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))

        val store =
            setupTabsTrayStoreStateWithGroups(
                groups = listOf(group to groupTabs),
                browserStore = browserStore,
            )
        val expectedTabs = tabs.slice(0..1) + tabs.slice(3..4) + tab + groupTabs + tabs.slice(8..9)
        val expectedBrowserState = BrowserState(tabs = expectedTabs)

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = tab.id,
                destinationId = group.id,
                placeAfter = false,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a tab is reordered after a group THEN the tab id is sequenced after the last group tab`() = runTest {
        val tabs = fakeTabList()
        val groupTabs = tabs.slice(5..7)
        val tab = tabs[2]
        val group = fakeGroup()

        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups = listOf(group to groupTabs),
                browserStore = browserStore,
            )
        val expectedTabs = tabs.slice(0..1) + tabs.slice(3..4) + groupTabs + tab + tabs.slice(8..9)
        val expectedBrowserState = BrowserState(tabs = expectedTabs)

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = tab.id,
                destinationId = group.id,
                placeAfter = true,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a tab is reordered with null destination state does not change`() = runTest {
        val tabs = fakeTabList()
        val groupTabs = tabs.slice(5..7)
        val tab = tabs[2]
        val group = fakeGroup()

        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups = listOf(group to groupTabs),
                browserStore = browserStore,
            )
        val expectedBrowserState = browserStore.state.copy()

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = tab.id,
                destinationId = null,
                placeAfter = true,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a tab is reordered after an empty group the state does not change`() = runTest {
        val tabs = fakeTabList()
        val groupTabs = emptyList<TabSessionState>()
        val tab = tabs[2]
        val group = fakeGroup()

        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups = listOf(group to groupTabs),
                browserStore = browserStore,
            )
        val expectedBrowserState = browserStore.state.copy()

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = tab.id,
                destinationId = group.id,
                placeAfter = true,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a tab is reordered before an empty group the state does not change`() = runTest {
        val tabs = fakeTabList()
        val groupTabs = emptyList<TabSessionState>()
        val tab = tabs[2]
        val group = fakeGroup()

        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups = listOf(group to groupTabs),
                browserStore = browserStore,
            )
        val expectedBrowserState = browserStore.state.copy()

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = tab.id,
                destinationId = group.id,
                placeAfter = false,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a group is reordered before a tab THEN the grouped tab IDs are sequenced together before the tab`() =
        runTest {
            val tabs = fakeTabList()
            val groupTabs = tabs.slice(5..7)
            val destinationTab = tabs[2]
            val group = fakeGroup()
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups = listOf(group to groupTabs),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(0..1) + groupTabs + destinationTab + tabs.slice(3..4) + tabs.slice(8..9)
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = group.id,
                    destinationId = destinationTab.id,
                    placeAfter = false,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a group is reordered after a tab THEN the grouped tab IDs are sequenced together after the tab`() =
        runTest {
            val tabs = fakeTabList()
            val groupTabs = tabs.slice(5..7)
            val destinationTab = tabs[2]
            val group = fakeGroup()
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups = listOf(group to groupTabs),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(0..1) + destinationTab + groupTabs + tabs.slice(3..4) + tabs.slice(8..9)
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = group.id,
                    destinationId = destinationTab.id,
                    placeAfter = true,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a tab is reordered before another tab THEN the source tab is placed before the destination tab`() =
        runTest {
            val tabs = fakeTabList()
            val groupTabs = tabs.slice(5..7)
            val sourceTab = tabs[9]
            val destinationTab = tabs[2]
            val group = fakeGroup()

            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups = listOf(group to groupTabs),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(0..1) + sourceTab + destinationTab + tabs.slice(3..8)
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = sourceTab.id,
                    destinationId = destinationTab.id,
                    placeAfter = false,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a tab is reordered after another tab THEN the source tab is placed after the destination tab`() =
        runTest {
            val tabs = fakeTabList()
            val groupTabs = tabs.slice(5..7)
            val sourceTab = tabs[9]
            val destinationTab = tabs[2]
            val group = fakeGroup()

            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups = listOf(group to groupTabs),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(0..1) + destinationTab + sourceTab + tabs.slice(3..8)
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = sourceTab.id,
                    destinationId = destinationTab.id,
                    placeAfter = true,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a private tab is reordered after another private tab THEN the source tab is placed after the destination tab`() =
        runTest {
            val tabs = List(size = 10) { createTab(url = "$it", private = true) }
            val sourceTab = tabs[9]
            val destinationTab = tabs[2]

            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups = emptyList(),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(0..1) + destinationTab + sourceTab + tabs.slice(3..8)
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = sourceTab.id,
                    destinationId = destinationTab.id,
                    placeAfter = true,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a private tab is reordered before another private tab THEN the source tab is placed before the destination tab`() =
        runTest {
            val tabs = List(size = 10) { createTab(url = "$it", private = true) }
            val sourceTab = tabs[9]
            val destinationTab = tabs[2]

            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups = emptyList(),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(0..1) + sourceTab + destinationTab + tabs.slice(3..8)
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = sourceTab.id,
                    destinationId = destinationTab.id,
                    placeAfter = false,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a group is reordered before an empty group THEN the state does not change`() = runTest {
        val tabs = fakeTabList()
        val sourceGroup = fakeGroup(title = "Group 1")
        val targetGroup = fakeGroup(title = "Group 2")
        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups =
                    listOf(
                        sourceGroup to tabs.slice(0..2),
                        targetGroup to emptyList(),
                    ),
                browserStore = browserStore,
            )
        val expectedBrowserState = browserStore.state.copy()

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = sourceGroup.id,
                destinationId = targetGroup.id,
                placeAfter = false,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a group is reordered after an empty group THEN the state does not change`() = runTest {
        val tabs = fakeTabList()
        val sourceGroup = fakeGroup(title = "Group 1")
        val targetGroup = fakeGroup(title = "Group 2")
        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups =
                    listOf(
                        sourceGroup to tabs.slice(0..2),
                        targetGroup to emptyList(),
                    ),
                browserStore = browserStore,
            )
        val expectedBrowserState = browserStore.state.copy()

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = sourceGroup.id,
                destinationId = targetGroup.id,
                placeAfter = true,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a group is reordered before another group THEN the source group's tab IDs are sequenced together before the target group's tabs`() =
        runTest {
            val tabs = fakeTabList()
            val sourceGroupTabs = tabs.slice(0..2)
            val targetGroupTabs = tabs.slice(7..9)
            val sourceGroup = fakeGroup(title = "Group 1")
            val targetGroup = fakeGroup(title = "Group 2")
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups =
                        listOf(
                            sourceGroup to sourceGroupTabs,
                            targetGroup to targetGroupTabs,
                        ),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(3..6) + sourceGroupTabs + targetGroupTabs
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = sourceGroup.id,
                    destinationId = targetGroup.id,
                    placeAfter = false,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a group is reordered after another group THEN the source group's tab IDs are sequenced together after the target group's tabs`() =
        runTest {
            val tabs = fakeTabList()
            val sourceGroupTabs = tabs.slice(0..2)
            val targetGroupTabs = tabs.slice(7..9)
            val sourceGroup = fakeGroup(title = "Group 1")
            val targetGroup = fakeGroup(title = "Group 2")
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val store =
                setupTabsTrayStoreStateWithGroups(
                    groups =
                        listOf(
                            sourceGroup to sourceGroupTabs,
                            targetGroup to targetGroupTabs,
                        ),
                    browserStore = browserStore,
                )
            val expectedTabs = tabs.slice(3..6) + targetGroupTabs + sourceGroupTabs
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabsTrayAction.ReorderTabsTrayItem(
                    sourceId = sourceGroup.id,
                    destinationId = targetGroup.id,
                    placeAfter = true,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a group is merged into another group the tabs are sequenced together by destination`() = runTest {
        val tabs = List(size = 10) { createTab(url = "$it") }
        val sourceGroupTabs = tabs.slice(0..3)
        val ungroupedTabs = tabs.slice(4..6)
        val targetGroupTabs = tabs.slice(7..9)
        val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
        val sourceGroup = fakeGroup(title = "Group 1")
        val targetGroup = fakeGroup(title = "Group 2")
        val store =
            setupTabsTrayStoreStateWithGroups(
                groups =
                    listOf(
                        sourceGroup to sourceGroupTabs,
                        targetGroup to targetGroupTabs,
                    ),
                browserStore = browserStore,
            )
        val expectedTabs = ungroupedTabs + targetGroupTabs + sourceGroupTabs
        val expectedBrowserState = BrowserState(tabs = expectedTabs)

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabGroupAction.DragAndDropInitiated(
                sourceId = sourceGroup.id,
                destinationId = targetGroup.id,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedBrowserState, browserStore.state)
    }

    @Test
    fun `WHEN a user creates a group from drag and drop THEN the tabs are sequenced together by destination`() =
        runTest {
            val tabs = List(size = 10) { createTab(url = "$it") }
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val sourceTab = TabsTrayItem.Tab(tab = tabs[2])
            val destinationTab = TabsTrayItem.Tab(tab = tabs[4])
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode = Mode.DragAndDrop(sourceId = sourceTab.id, destinationId = destinationTab.id),
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                        )
                                ),
                        ),
                    browserStore = browserStore,
                    tabGroupsEnabled = true,
                )
            val expectedTabs =
                tabs.slice(listOf(0, 1, 3)) + tabs.slice(listOf(4, 2)) + tabs.slice(listOf(5, 6, 7, 8, 9))
            val expectedBrowserState = BrowserState(tabs = expectedTabs)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedBrowserState, browserStore.state)
        }

    @Test
    fun `WHEN a user adds a tab to an existing group that has at least one tab THEN the tab becomes blocked next to the group's last tab`() =
        runTest {
            val tabs = List(size = 20) { createTab(url = "$it") }
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val tabGroupTabs = tabs.take(10)
            val existingGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val tabAdded = tabs.last()
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = browserStore,
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(existingGroup),
                            initialTabGroupAssignments = tabGroupTabs.map { it.id to existingGroup.id },
                        ),
                )
            val expectedTabList = tabGroupTabs + tabAdded + tabs.subList(10, tabs.size - 1)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.TabAddedToExistingTabGroup(tabId = tabAdded.id, groupId = existingGroup.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedTabList.map { it.id }, browserStore.state.tabs.map { it.id })
        }

    @Test
    fun `WHEN adding multiple tabs to an existing group THEN the tabs become blocked next to the group's last tab`() =
        runTest {
            val tabs = List(size = 20) { createTab(url = "$it") }
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val tabGroupTabs = tabs.take(10)
            val existingGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val tabsAdded = tabs.takeLast(5)
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = browserStore,
                    initialState =
                        TabsTrayState(
                            mode = Mode.Select(selectedTabs = tabsAdded.map { TabsTrayItem.Tab(tab = it) }.toSet())
                        ),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(existingGroup),
                            initialTabGroupAssignments = tabGroupTabs.map { it.id to existingGroup.id },
                        ),
                )
            val expectedTabList = tabGroupTabs + tabsAdded + tabs.subList(10, tabs.size - tabsAdded.size)

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.SelectedTabsAddedToGroup(groupId = existingGroup.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedTabList.map { it.id }, browserStore.state.tabs.map { it.id })
        }

    @Test
    fun `WHEN a user closes a tab group THEN mark the group as closed in storage and update the UI`() = runTest {
        val tabs = List(size = 20) { createTab(url = "$it") }
        val openTabGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                closed = false,
                tabIds = emptyList(),
            )
        val displayGroup =
            createTabGroup(
                id = openTabGroup.id,
                title = openTabGroup.title,
                theme = TabGroupTheme.valueOf(openTabGroup.theme),
                closed = openTabGroup.closed,
                tabs = tabs.map { TabsTrayItem.Tab(tab = it) }.toMutableList(),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = tabs)),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(openTabGroup),
                        initialTabGroupAssignments = tabs.map { it.id to openTabGroup.id },
                    ),
            )
        val expectedState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(tabCount = 0),
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups =
                            listOf(
                                displayGroup.copy(
                                    closed = true,
                                    lastModified = fakeDateTimeProvider.currentTimeMillis(),
                                )
                            )
                    ),
                hasTabDataLoaded = true,
            )

        store.dispatch(TabGroupAction.CloseTabGroupClicked(group = displayGroup))

        runCurrent()
        advanceUntilIdle()

        assertEquals(expectedState, store.state)
    }

    @Test
    fun `GIVEN inactive tabs feature is enabled and inactive tabs exist WHEN tab group delete is confirmed THEN exclude inactive tabs from deletion`() =
        runTest {
            val browserStore = BrowserStore()

            val activeGroupedTab = createTab("https://mozilla.org")
            browserStore.dispatch(TabListAction.AddTabAction(activeGroupedTab))

            val inactiveTab =
                createTab(
                    id = "inactive_99",
                    url = "https://example.com",
                    createdAt = expiredTime(),
                    lastAccess = expiredTime(),
                )
            browserStore.dispatch(TabListAction.AddTabAction(inactiveTab))

            val title = "Group 1"
            val theme = TabGroupTheme.Red
            val storedGroup =
                TabGroup(
                    title = title,
                    theme = theme.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )

            val repository =
                FakeTabGroupRepository(
                    initialTabGroupData =
                        TabGroupData(
                            tabGroups = listOf(storedGroup),
                            tabGroupAssignments = mapOf(activeGroupedTab.id to storedGroup.id),
                        )
                )

            val store =
                createStore(
                    inactiveTabsEnabled = true,
                    tabGroupsEnabled = true,
                    browserStore = browserStore,
                    tabGroupRepository = repository,
                )

            val group =
                TabsTrayItem.TabGroup(
                    id = storedGroup.id,
                    title = title,
                    theme = theme,
                    tabs = mutableListOf(TabsTrayItem.Tab(activeGroupedTab)),
                )

            runCurrent()
            store.dispatch(TabGroupAction.DeleteConfirmed(group))

            runCurrent()
            advanceUntilIdle()

            assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())

            assertEquals(listOf(inactiveTab.id), browserStore.state.tabs.map { it.id })
            assertNull(browserStore.state.selectedTabId)
        }

    @Test
    fun `WHEN a user closes the last tab and delete group is confirmed THEN remove the tab group and its tabs`() =
        runTest {
            val browserStore = BrowserStore()

            val firstTab = createTab("https://mozilla.org")
            browserStore.dispatch(TabListAction.AddTabAction(firstTab))

            val title = "Group 1"
            val theme = TabGroupTheme.Red
            val storedGroup =
                TabGroup(
                    title = title,
                    theme = theme.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )

            val repository =
                FakeTabGroupRepository(
                    initialTabGroupData =
                        TabGroupData(
                            tabGroups = listOf(storedGroup),
                            tabGroupAssignments = mapOf(firstTab.id to storedGroup.id),
                        )
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = browserStore,
                    tabGroupRepository = repository,
                )

            val group =
                TabsTrayItem.TabGroup(
                    id = storedGroup.id,
                    title = title,
                    theme = theme,
                    tabs = mutableListOf(TabsTrayItem.Tab(firstTab)),
                )

            assertEquals(listOf(storedGroup), repository.tabGroupDataFlow.first().tabGroups)
            assertEquals(1, browserStore.state.tabs.size)

            runCurrent()
            store.dispatch(TabGroupAction.CloseTabAndDeleteGroupConfirmed(group))

            runCurrent()
            advanceUntilIdle()

            assertTrue(repository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(browserStore.state.tabs.isEmpty())
        }

    @Test
    fun `GIVEN multiple tab groups exist WHEN close tab and delete group is confirmed THEN remove the correct tab group`() =
        runTest {
            val tabGroup1 =
                TabGroup(
                    title = "Tab Group 1",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val tabGroup2 =
                TabGroup(
                    title = "Tab Group 2",
                    theme = TabGroupTheme.Blue.name,
                    lastModified = 1L,
                    tabIds = emptyList(),
                )
            val repository =
                FakeTabGroupRepository(initialTabGroupData = TabGroupData(tabGroups = listOf(tabGroup1, tabGroup2)))
            val store = createStore(tabGroupRepository = repository)

            assertEquals(listOf(tabGroup1, tabGroup2), repository.tabGroupDataFlow.first().tabGroups)

            store.dispatch(
                TabGroupAction.CloseTabAndDeleteGroupConfirmed(
                    group =
                        TabsTrayItem.TabGroup(
                            id = tabGroup1.id,
                            title = tabGroup1.title,
                            theme = TabGroupTheme.Red,
                            tabs = mutableListOf(),
                        )
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(listOf(tabGroup2), repository.tabGroupDataFlow.first().tabGroups)
        }

    @Test
    fun `WHEN dropping a tab onto a tab THEN the user is directed to the create group flow with required data`() =
        runTest {
            val tab = createTab(url = "")
            val otherTab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, otherTab, groupedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(storedGroup),
                            initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            val expectedState =
                store.state.copy(
                    mode = Mode.DragAndDrop(sourceId = tab.id, destinationId = otherTab.id),
                    tabGroupState =
                        store.state.tabGroupState.copy(
                            formState =
                                TabGroupFormState(
                                    tabGroupId = null,
                                    name = "",
                                    nextTabGroupNumber = 2,
                                    theme = TabGroupTheme.Pink,
                                    edited = false,
                                ),
                            dragProcessingState = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
                        ),
                    backStack = listOf(TabManagerNavDestination.Root, TabManagerNavDestination.EditTabGroup),
                )
            store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = tab.id, destinationId = otherTab.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expected = expectedState, actual = store.state)
        }

    @Test
    fun `WHEN dropping a group onto a group THEN the source group is merged into the destination group`() = runTest {
        val sourceStoredGroup =
            TabGroup(
                title = "Group 1",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val sourceGroupTabs =
            listOf(
                createTab(url = ""),
                createTab(url = ""),
                createTab(url = ""),
            )
        val destinationStoredGroup =
            TabGroup(
                title = "Group 2",
                theme = TabGroupTheme.Blue.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val destinationGroupTabs =
            listOf(
                createTab(url = ""),
                createTab(url = ""),
                createTab(url = ""),
                createTab(url = ""),
                createTab(url = ""),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = sourceGroupTabs + destinationGroupTabs)),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(sourceStoredGroup, destinationStoredGroup),
                        initialTabGroupAssignments =
                            sourceGroupTabs.map { it.id to sourceStoredGroup.id } +
                                destinationGroupTabs.map { it.id to destinationStoredGroup.id },
                    ),
            )
        val expectedTabGroupList =
            listOf(
                createTabGroup(
                    id = destinationStoredGroup.id,
                    title = destinationStoredGroup.title,
                    theme = TabGroupTheme.valueOf(destinationStoredGroup.theme),
                    tabs =
                        (destinationGroupTabs.map { TabsTrayItem.Tab(it) } +
                                sourceGroupTabs.map { TabsTrayItem.Tab(it) })
                            .toMutableList(),
                )
            )
        val expectedState =
            TabsTrayState(
                normalTabsState =
                    TabsTrayState.NormalTabsState(
                        items = expectedTabGroupList,
                        tabCount = 8,
                    ),
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups = expectedTabGroupList,
                        dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED,
                    ),
                hasTabDataLoaded = true,
            )

        runCurrent()
        advanceUntilIdle()

        store.dispatch(
            TabGroupAction.DragAndDropInitiated(
                sourceId = sourceStoredGroup.id,
                destinationId = destinationStoredGroup.id,
            )
        )

        runCurrent()
        advanceUntilIdle()

        assertEquals(expected = expectedState, actual = store.state)
    }

    @Test
    fun `WHEN dropping a tab onto a group THEN the tab is added to the group`() = runTest {
        val tab = createTab(url = "")
        val groupedTab = createTab(url = "")
        val storedGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(storedGroup),
                        initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                    ),
            )
        val expectedTabGroupList =
            listOf(
                createTabGroup(
                    id = storedGroup.id,
                    title = storedGroup.title,
                    theme = TabGroupTheme.Red,
                    tabs = mutableListOf(TabsTrayItem.Tab(groupedTab), TabsTrayItem.Tab(tab)),
                )
            )
        val expectedState =
            TabsTrayState(
                normalTabsState =
                    TabsTrayState.NormalTabsState(
                        items = expectedTabGroupList,
                        tabCount = 2,
                    ),
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups = expectedTabGroupList,
                        dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED,
                    ),
                hasTabDataLoaded = true,
            )

        runCurrent()
        advanceUntilIdle()

        store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = tab.id, destinationId = storedGroup.id))

        runCurrent()
        advanceUntilIdle()

        assertEquals(expected = expectedState, actual = store.state)
    }

    @Test
    fun `WHEN dropping a group onto a tab THEN the tab is added to the group`() = runTest {
        val tab = createTab(url = "")
        val groupedTab = createTab(url = "")
        val storedGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(storedGroup),
                        initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                    ),
            )
        val expectedTabGroupList =
            listOf(
                createTabGroup(
                    id = storedGroup.id,
                    title = storedGroup.title,
                    theme = TabGroupTheme.Red,
                    tabs = mutableListOf(TabsTrayItem.Tab(groupedTab), TabsTrayItem.Tab(tab)),
                )
            )
        val expectedState =
            TabsTrayState(
                normalTabsState =
                    TabsTrayState.NormalTabsState(
                        items = expectedTabGroupList,
                        tabCount = 2,
                    ),
                tabGroupState =
                    TabsTrayState.TabGroupState(
                        groups = expectedTabGroupList,
                        dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED,
                    ),
                hasTabDataLoaded = true,
            )

        runCurrent()
        advanceUntilIdle()

        store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = storedGroup.id, destinationId = tab.id))

        runCurrent()
        advanceUntilIdle()

        assertEquals(expected = expectedState, actual = store.state)
    }

    @Test
    fun `WHEN a user drops a group with at least one grouped tab onto a tab THEN the groups tabs are moved in front of the target tab`() =
        runTest {
            val tabs = List(size = 20) { createTab(url = "$it") }
            val browserStore = BrowserStore(initialState = BrowserState(tabs = tabs))
            val tabGroupTabs = tabs.take(10)
            val sourceGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val targetTab = tabs.last()
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = browserStore,
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(sourceGroup),
                            initialTabGroupAssignments = tabGroupTabs.map { it.id to sourceGroup.id },
                        ),
                )
            val expectedTabList = tabs.subList(10, tabs.size - 1) + tabGroupTabs + targetTab

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = sourceGroup.id, destinationId = targetTab.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(expectedTabList.map { it.id }, browserStore.state.tabs.map { it.id })
        }

    @Test
    fun `WHEN source id is not in the items list THEN only drag state is updated`() = runTest {
        val tab = createTab(url = "")
        val groupedTab = createTab(url = "")
        val storedGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(storedGroup),
                        initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                    ),
            )

        runCurrent()
        advanceUntilIdle()
        val initialState = store.state
        store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = "BadId", destinationId = tab.id))

        runCurrent()
        advanceUntilIdle()

        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED)
            )
        assertEquals(expectedState, store.state)
    }

    @Test
    fun `WHEN dropping a tab onto an illegal item THEN only drag state is updated`() = runTest {
        val tab = createTab(url = "")
        val groupedTab = createTab(url = "")
        val storedGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(storedGroup),
                        initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                    ),
            )

        runCurrent()
        advanceUntilIdle()
        val initialState = store.state
        store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = tab.id, destinationId = "BadId"))

        runCurrent()
        advanceUntilIdle()

        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED)
            )
        assertEquals(expectedState, store.state)
    }

    @Test
    fun `WHEN dropping a group onto an illegal item THEN only drag state is updated`() = runTest {
        val tab = createTab(url = "")
        val groupedTab = createTab(url = "")
        val storedGroup =
            TabGroup(
                title = "Name",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = emptyList(),
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                tabGroupRepository =
                    createRepository(
                        initialTabGroups = listOf(storedGroup),
                        initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                    ),
            )

        runCurrent()
        advanceUntilIdle()
        val initialState = store.state
        store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = storedGroup.id, destinationId = "BadId"))

        runCurrent()
        advanceUntilIdle()

        val expectedState =
            initialState.copy(
                tabGroupState =
                    initialState.tabGroupState.copy(dragProcessingState = TabsTrayState.DragProcessingState.COMPLETED)
            )
        assertEquals(expected = expectedState, actual = store.state)
    }

    @Test
    fun `WHEN TabClosed is dispatched AND group has multiple tabs THEN remove the tab`() = runTest {
        val browserStore = BrowserStore()

        val firstTab = createTab("https://mozilla.org")
        val secondTab = createTab("https://firefox.com")
        browserStore.dispatch(TabListAction.AddTabAction(firstTab))
        browserStore.dispatch(TabListAction.AddTabAction(secondTab))

        val storedGroup =
            TabGroup(
                id = "group-1",
                title = "Group",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = listOf(firstTab.id, secondTab.id),
            )
        val repository =
            FakeTabGroupRepository(
                initialTabGroupData =
                    TabGroupData(
                        tabGroups = listOf(storedGroup),
                        tabGroupAssignments = mapOf(firstTab.id to storedGroup.id, secondTab.id to storedGroup.id),
                    )
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = browserStore,
                tabGroupRepository = repository,
            )

        val group =
            TabsTrayItem.TabGroup(
                id = storedGroup.id,
                title = "Group",
                theme = TabGroupTheme.Red,
                tabs =
                    mutableListOf(
                        TabsTrayItem.Tab(firstTab),
                        TabsTrayItem.Tab(secondTab),
                    ),
            )

        assertEquals(2, browserStore.state.tabs.size)

        runCurrent()
        store.dispatch(TabGroupAction.TabClosed(tab = TabsTrayItem.Tab(firstTab), group = group))

        runCurrent()
        advanceUntilIdle()

        assertEquals(1, browserStore.state.tabs.size)
        assertEquals(secondTab.id, browserStore.state.tabs.first().id)
    }

    @Test
    fun `WHEN TabClosed is dispatched AND group has 1 tab THEN middleware does nothing`() = runTest {
        val browserStore = BrowserStore()

        val firstTab = createTab("https://mozilla.org")
        browserStore.dispatch(TabListAction.AddTabAction(firstTab))

        val storedGroup =
            TabGroup(
                id = "group1",
                title = "Group",
                theme = TabGroupTheme.Red.name,
                lastModified = 0L,
                tabIds = listOf(firstTab.id),
            )
        val repository =
            FakeTabGroupRepository(
                initialTabGroupData =
                    TabGroupData(
                        tabGroups = listOf(storedGroup),
                        tabGroupAssignments = mapOf(firstTab.id to storedGroup.id),
                    )
            )
        val store =
            createStore(
                tabGroupsEnabled = true,
                browserStore = browserStore,
                tabGroupRepository = repository,
            )

        val group =
            TabsTrayItem.TabGroup(
                id = storedGroup.id,
                title = "Group",
                theme = TabGroupTheme.Red,
                tabs = mutableListOf(TabsTrayItem.Tab(firstTab)),
            )

        assertEquals(1, browserStore.state.tabs.size)

        runCurrent()
        store.dispatch(TabGroupAction.TabClosed(tab = TabsTrayItem.Tab(firstTab), group = group))

        runCurrent()
        advanceUntilIdle()

        assertEquals(1, browserStore.state.tabs.size)
    }

    @Test
    fun `WHEN save is clicked from group creation with valid drag and drop data THEN entering group is updated`() =
        runTest {
            val repository = createRepository()
            val sourceTab = createTab(url = "https://mozilla.org")
            val destinationTab = createTab(url = "https://example.com")
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode = Mode.DragAndDrop(sourceId = sourceTab.id, destinationId = destinationTab.id),
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                        )
                                ),
                        ),
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(sourceTab, destinationTab))),
                    tabGroupsEnabled = true,
                    tabGroupRepository = repository,
                    dateTimeProvider = fakeDateTimeProvider,
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            assertNotNull(actual = store.state.tabGroupState.enteringGroupId)
        }

    @Test
    fun `WHEN save is clicked from group creation with valid multi-select data THEN entering group is updated`() =
        runTest {
            val repository = createRepository()
            val sourceTab = createTab(url = "https://mozilla.org")
            val destinationTab = createTab(url = "https://example.com")
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode =
                                Mode.Select(
                                    selectedTabs =
                                        setOf(TabsTrayItem.Tab(tab = sourceTab), TabsTrayItem.Tab(tab = destinationTab))
                                ),
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                        )
                                ),
                        ),
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(sourceTab, destinationTab))),
                    tabGroupsEnabled = true,
                    tabGroupRepository = repository,
                    dateTimeProvider = fakeDateTimeProvider,
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            assertNotNull(actual = store.state.tabGroupState.enteringGroupId)
        }

    @Test
    fun `WHEN save is clicked from group creation with invalid data THEN it is handled gracefully AND entering group is not updated`() =
        runTest {
            val repository = createRepository()
            val sourceTab = createTab(url = "https://mozilla.org")
            val destinationTab = createTab(url = "https://example.com")
            val expectedTitle = "Group 1"
            val expectedTheme = TabGroupTheme.Red
            val store =
                createStore(
                    initialState =
                        TabsTrayState(
                            mode = Mode.DragAndDrop(sourceId = sourceTab.id, destinationId = null),
                            tabGroupState =
                                TabsTrayState.TabGroupState(
                                    formState =
                                        TabGroupFormState(
                                            name = expectedTitle,
                                            tabGroupId = null,
                                            theme = expectedTheme,
                                        )
                                ),
                        ),
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(sourceTab, destinationTab))),
                    tabGroupsEnabled = true,
                    tabGroupRepository = repository,
                    dateTimeProvider = fakeDateTimeProvider,
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.SaveClicked)

            runCurrent()
            advanceUntilIdle()

            assertNull(actual = store.state.tabGroupState.enteringGroupId)
        }

    @Test
    fun `GIVEN tab dragged onto tab, WHEN DragAndDropInitiated is called, THEN drag state is EDIT_IN_PROGRESS`() =
        runTest {
            val tab = createTab(url = "")
            val otherTab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, otherTab, groupedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(storedGroup),
                            initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = tab.id, destinationId = otherTab.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(
                expected = TabsTrayState.DragProcessingState.EDIT_IN_PROGRESS,
                store.state.tabGroupState.dragProcessingState,
            )
        }

    @Test
    fun `GIVEN tab dragged onto group, WHEN DragAndDropInitiated is called, THEN drag handling state is COMPLETED`() =
        runTest {
            val tab = createTab(url = "")
            val otherTab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, otherTab, groupedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(storedGroup),
                            initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = tab.id, destinationId = storedGroup.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(
                expected = TabsTrayState.DragProcessingState.COMPLETED,
                store.state.tabGroupState.dragProcessingState,
            )
        }

    @Test
    fun `GIVEN group dragged onto tab, WHEN DragAndDropInitiated is called, THEN drag handling state is COMPLETED`() =
        runTest {
            val tab = createTab(url = "")
            val otherTab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Name",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, otherTab, groupedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(storedGroup),
                            initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = storedGroup.id, destinationId = tab.id))

            runCurrent()
            advanceUntilIdle()

            assertEquals(
                expected = TabsTrayState.DragProcessingState.COMPLETED,
                store.state.tabGroupState.dragProcessingState,
            )
        }

    @Test
    fun `GIVEN group dragged onto group, WHEN  DragAndDropInitiated is called, THEN drag handling state is COMPLETED`() =
        runTest {
            val tab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val otherGroupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Group 1",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val otherStoredGroup =
                TabGroup(
                    title = "Group 2",
                    theme = TabGroupTheme.Blue.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore =
                        BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab, otherGroupedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(storedGroup, otherStoredGroup),
                            initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabGroupAction.DragAndDropInitiated(sourceId = storedGroup.id, destinationId = otherStoredGroup.id)
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(
                expected = TabsTrayState.DragProcessingState.COMPLETED,
                store.state.tabGroupState.dragProcessingState,
            )
        }

    @Test
    fun `GIVEN invalid drop state, WHEN  DragAndDropInitiated is called, THEN drag handling state is COMPLETED`() =
        runTest {
            val tab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Group 1",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                    tabGroupRepository =
                        createRepository(
                            initialTabGroups = listOf(storedGroup),
                            initialTabGroupAssignments = listOf(groupedTab.id to storedGroup.id),
                        ),
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabGroupAction.DragAndDropInitiated(
                    sourceId = "notATab1",
                    destinationId = "notATab2",
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(
                expected = TabsTrayState.DragProcessingState.COMPLETED,
                store.state.tabGroupState.dragProcessingState,
            )
        }

    @Test
    fun `WHEN  DragAndDropInitiated is called, if an exception is thrown by the storage layer, THEN drag handling state is still COMPLETED`() =
        runTest {
            val tab = createTab(url = "")
            val groupedTab = createTab(url = "")
            val storedGroup =
                TabGroup(
                    title = "Group 1",
                    theme = TabGroupTheme.Red.name,
                    lastModified = 0L,
                    tabIds = emptyList(),
                )
            val store =
                createStore(
                    tabGroupsEnabled = true,
                    browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab, groupedTab))),
                    tabGroupRepository =
                        object :
                            TabGroupRepository by FakeTabGroupRepository(
                                initialTabGroupData =
                                    TabGroupData(
                                        tabGroups = listOf(storedGroup),
                                        tabGroupAssignments = mapOf(groupedTab.id to storedGroup.id),
                                    )
                            ) {
                            override suspend fun addTabGroupAssignment(tabId: String, tabGroupId: String) {
                                throw StorageLayerException()
                            }
                        },
                )

            runCurrent()
            advanceUntilIdle()

            store.dispatch(
                TabGroupAction.DragAndDropInitiated(
                    sourceId = tab.id,
                    destinationId = storedGroup.id,
                )
            )

            runCurrent()
            advanceUntilIdle()

            assertEquals(
                expected = TabsTrayState.DragProcessingState.COMPLETED,
                store.state.tabGroupState.dragProcessingState,
            )
        }

    private fun TestScope.createStore(
        initialState: TabsTrayState = TabsTrayState(),
        inactiveTabsEnabled: Boolean = false,
        tabGroupsEnabled: Boolean = false,
        browserStore: BrowserStore = BrowserStore(),
        tabGroupRepository: TabGroupRepository = createRepository(),
        fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true),
        dateTimeProvider: DateTimeProvider = fakeDateTimeProvider,
    ): TabsTrayStore {
        val tabDataCoordinator =
            DefaultTabDataCoordinator(
                tabRepository =
                    DefaultTabRepository(
                        browserStore = browserStore,
                        tabsUseCases = TabsUseCases(store = browserStore),
                    ),
                tabGroupRepository = tabGroupRepository,
                isInactiveTabsEnabled = { inactiveTabsEnabled },
                isTabGroupsEnabled = { tabGroupsEnabled },
                scope = backgroundScope,
                dispatcher = StandardTestDispatcher(testScheduler),
                dateTimeProvider = dateTimeProvider,
            )
        tabDataCoordinator.initialize()

        return TabsTrayStore(
            initialState = initialState,
            middlewares =
                listOf(
                    TabStorageMiddleware(
                        tabDataCoordinator = tabDataCoordinator,
                        fenixBrowserUseCases = fenixBrowserUseCases,
                        scope = backgroundScope,
                        mainScope = backgroundScope,
                    )
                ),
        )
    }

    private fun expiredTime(): Long = System.currentTimeMillis() - maxActiveTime - 1

    private fun createRepository(
        initialTabGroups: List<TabGroup> = emptyList(),
        initialTabGroupAssignments: List<Pair<String, String>> = emptyList(),
    ): FakeTabGroupRepository =
        FakeTabGroupRepository(
            initialTabGroupData =
                TabGroupData(
                    tabGroups = initialTabGroups,
                    tabGroupAssignments = initialTabGroupAssignments.associate { it.first to it.second },
                ),
            dateTimeProvider = fakeDateTimeProvider,
        )

    private fun fakeTabList(): List<TabSessionState> {
        return List(size = 10) { createTab(url = "$it") }
    }

    private fun fakeGroup(title: String = "Group 1"): TabGroup {
        return TabGroup(
            title = title,
            theme = TabGroupTheme.Red.name,
            lastModified = 0L,
            tabIds = emptyList(),
        )
    }

    /**
     * Store setup logic for tests that includes group creation and the group assignments of the tabs held by
     * [browserStore].
     */
    private fun TestScope.setupTabsTrayStoreStateWithGroups(
        groups: List<Pair<TabGroup, List<TabSessionState>>>,
        browserStore: BrowserStore,
    ): TabsTrayStore {
        val tabGroupAssignment =
            groups
                .map { groupPair ->
                    groupPair.second.map { it.id to groupPair.first.id }
                }
                .takeIf { it.isNotEmpty() }
                ?.reduce { acc, map -> acc + map } ?: emptyList()
        return createStore(
            tabGroupsEnabled = true,
            browserStore = browserStore,
            tabGroupRepository =
                createRepository(
                    initialTabGroups = groups.map { it.first },
                    initialTabGroupAssignments = tabGroupAssignment,
                ),
        )
    }
}

/** Simulates a failure raised by the storage layer behind the tab group repository. */
private class StorageLayerException : Exception("Storage layer exception")
