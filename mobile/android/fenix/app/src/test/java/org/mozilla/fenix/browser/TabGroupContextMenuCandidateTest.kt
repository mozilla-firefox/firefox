/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.HitResult
import mozilla.components.feature.tabdata.coordinator.DefaultTabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabDataCoordinator
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.feature.tabgroups.storage.fakes.FakeTabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.tabs.DefaultTabRepository
import org.mozilla.fenix.tabstray.data.TabGroupTheme

@RunWith(AndroidJUnit4::class)
class TabGroupContextMenuCandidateTest {

    private val appStoreActions = CaptureActionsMiddleware<AppState, AppAction>()
    private val appStore = AppStore(middlewares = listOf(appStoreActions))

    @Test
    fun `WHEN tab groups are enabled and a link is long-pressed in a normal tab THEN show the candidate`() {
        assertTrue(
            TabGroupContextMenuCandidate.shouldShowCandidate(
                tab = createTab(url = "https://mozilla.org"),
                hitResult = HitResult.UNKNOWN(LINK_URL),
                tabGroupsEnabled = true,
            )
        )
    }

    @Test
    fun `WHEN tab groups are disabled THEN do not show the candidate`() {
        assertFalse(
            TabGroupContextMenuCandidate.shouldShowCandidate(
                tab = createTab(url = "https://mozilla.org"),
                hitResult = HitResult.UNKNOWN(LINK_URL),
                tabGroupsEnabled = false,
            )
        )
    }

    @Test
    fun `WHEN the tab is private THEN do not show the candidate`() {
        assertFalse(
            TabGroupContextMenuCandidate.shouldShowCandidate(
                tab = createTab(url = "https://mozilla.org", private = true),
                hitResult = HitResult.UNKNOWN(LINK_URL),
                tabGroupsEnabled = true,
            )
        )
    }

    @Test
    fun `WHEN the long-pressed element is not an http link THEN do not show the candidate`() {
        assertFalse(
            TabGroupContextMenuCandidate.shouldShowCandidate(
                tab = createTab(url = "https://mozilla.org"),
                hitResult = HitResult.IMAGE("https://mozilla.org/image.png"),
                tabGroupsEnabled = true,
            )
        )
    }

    @Test
    fun `WHEN the candidate is created THEN it is labeled and identified as the tab group menu item`() {
        val candidate =
            TabGroupContextMenuCandidate.createOpenInNewTabInGroupCandidate(
                resources = testContext.resources,
                tabGroupsEnabled = true,
                tabGroupsStripEnabled = false,
                tabsUseCases = mockk(relaxed = true),
                tabDataCoordinator = FakeTabDataCoordinator(),
                appStore = appStore,
                scope = TestScope(),
            )

        assertEquals(TabGroupContextMenuCandidate.OPEN_IN_TAB_GROUP_ID, candidate.id)
        assertEquals("Open in tab group", candidate.label)
    }

    @Test
    fun `WHEN the link is opened THEN it opens in an unselected tab parented to the active tab and joins its group`() =
        runTest {
            val browserStore = browserStore()
            val repository = FakeTabGroupRepository()

            openLinkInActiveTabsGroup(browserStore, repository)

            val newTab = browserStore.state.tabs.last()
            assertEquals(2, browserStore.state.tabs.size)
            assertEquals(LINK_URL, newTab.content.url)
            assertEquals(PARENT_TAB_ID, newTab.parentId)
            assertEquals(PARENT_TAB_ID, browserStore.state.selectedTabId)

            val tabGroupData = repository.tabGroupDataFlow.first()
            val groupId = tabGroupData.tabGroups.single().id
            assertEquals(
                mapOf(PARENT_TAB_ID to groupId, newTab.id to groupId),
                tabGroupData.tabGroupAssignments,
            )
        }

    @Test
    fun `WHEN the parent tab is not in a group THEN the added to group snackbar is shown for the new group`() =
        runTest {
            val repository = FakeTabGroupRepository()

            openLinkInActiveTabsGroup(browserStore(), repository)

            val groupId = repository.tabGroupDataFlow.first().tabGroups.single().id
            appStoreActions.assertLastAction(AppAction.TabAddedToGroup::class) {
                assertEquals(AppAction.TabAddedToGroup(groupTitle = "Group 1", groupId = groupId), it)
            }
        }

    @Test
    fun `WHEN the parent tab is already in a group THEN the added to group snackbar is shown for that group`() =
        runTest {
            val group = tabGroup(title = "Research")
            val browserStore = browserStore()
            val repository =
                FakeTabGroupRepository(
                    initialTabGroupData =
                        TabGroupData(
                            tabGroups = listOf(group),
                            tabGroupAssignments = mapOf(PARENT_TAB_ID to group.id),
                        )
                )

            openLinkInActiveTabsGroup(browserStore, repository)

            val newTabId = browserStore.state.tabs.first { it.id != PARENT_TAB_ID }.id
            assertEquals(group.id, repository.tabGroupDataFlow.first().tabGroupAssignments[newTabId])
            appStoreActions.assertLastAction(AppAction.TabAddedToGroup::class) {
                assertEquals(AppAction.TabAddedToGroup(groupTitle = "Research", groupId = group.id), it)
            }
        }

    @Test
    fun `WHEN the tab groups strip is enabled THEN the tab joins a group without showing a snackbar`() = runTest {
        val browserStore = browserStore()
        val repository = FakeTabGroupRepository()

        openLinkInActiveTabsGroup(browserStore, repository, tabGroupsStripEnabled = true)

        val newTabId = browserStore.state.tabs.first { it.id != PARENT_TAB_ID }.id
        val tabGroupData = repository.tabGroupDataFlow.first()
        assertEquals(tabGroupData.tabGroups.single().id, tabGroupData.tabGroupAssignments[newTabId])
        appStoreActions.assertNoActionDispatched()
    }

    private fun browserStore() =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs = listOf(createTab(url = "https://mozilla.org", id = PARENT_TAB_ID)),
                    selectedTabId = PARENT_TAB_ID,
                ),
            middleware = EngineMiddleware.create(engine = mockk(relaxed = true), scope = TestScope()),
        )

    private suspend fun TestScope.openLinkInActiveTabsGroup(
        browserStore: BrowserStore,
        repository: FakeTabGroupRepository,
        tabGroupsStripEnabled: Boolean = false,
    ) =
        TabGroupContextMenuCandidate.openLinkInActiveTabsGroup(
            parentTab = browserStore.state.tabs.first { it.id == PARENT_TAB_ID },
            hitResult = HitResult.UNKNOWN(LINK_URL),
            resources = testContext.resources,
            tabsUseCases = TabsUseCases(browserStore),
            tabDataCoordinator = tabDataCoordinator(browserStore, repository),
            tabGroupsStripEnabled = tabGroupsStripEnabled,
            appStore = appStore,
        )

    private fun TestScope.tabDataCoordinator(browserStore: BrowserStore, repository: FakeTabGroupRepository) =
        DefaultTabDataCoordinator(
                tabRepository =
                    DefaultTabRepository(
                        browserStore = browserStore,
                        tabsUseCases = TabsUseCases(browserStore),
                        fenixBrowserUseCases = mockk(relaxed = true),
                    ),
                tabGroupRepository = repository,
                isInactiveTabsEnabled = { false },
                isTabGroupsEnabled = { true },
                scope = backgroundScope,
                dispatcher = StandardTestDispatcher(testScheduler),
            )
            .also { it.initialize() }

    private fun tabGroup(title: String) =
        TabGroup(
            id = "tab-group-id",
            title = title,
            theme = TabGroupTheme.default.toStorageValue(),
            lastModified = 0L,
            tabIds = listOf(PARENT_TAB_ID),
        )
}

private const val PARENT_TAB_ID = "parent-tab-id"
private const val LINK_URL = "https://mozilla.org/link"
